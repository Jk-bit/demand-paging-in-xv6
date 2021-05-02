#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "buf.h"
#include "back_store.h"
#include "file.h"
extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0){
	return 0;
    }
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm, int avl)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P){
	panic("remap");
    }
    *pte = pa | perm | avl;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W | PTE_P}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0 | PTE_P},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W | PTE_P}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W | PTE_P}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm | PTE_P , 0) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;
  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U|PTE_P, 0);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem = (char *)V2P(0);
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    //mem = kalloc();
    /*if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }*/
    //memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U, 0) < 0){
      cprintf("allocuvm out of memory (2)\n");
      /*deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);*/
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0){
        panic("kfree");
      }
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)(pgdir));
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U ;
  *pte = *pte;
}

void clearptep(pde_t *pgdir, char *uva){
    pte_t *pte;
    pte = walkpgdir(pgdir, uva, 0);
    if(pte == 0){
	panic("clearptep");
    }
    *pte &= ~PTE_P;
    *pte = *pte;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(struct proc *dst, struct proc *src)
{
  pde_t *d;
  pte_t *pte;
  uint i, flags, pa;
  char *mem = 0;
  int avl = 0;
    //char *stack_page;
  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < src->raw_elf_size; i += PGSIZE){
    if((pte = walkpgdir(src->pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P)){
      if(mappages(d, (char*)i, PGSIZE, V2P(mem), PTE_W | PTE_U, 0) < 0) {
	goto bad;
      }
    }
    else{
	pa = PTE_ADDR(*pte);
	flags = PTE_FLAGS(*pte);
	avl = PTE_AVL(*pte);
	if((mem = kalloc()) == 0){
	    goto bad;
	}	
	memmove(mem, (char *)P2V(pa), PGSIZE);
	if(mappages(d, (char *)i, PGSIZE, V2P(mem), flags, avl) < 0){
	    kfree(mem);
	    goto bad;
	}
    }
    //pa = PTE_ADDR(*pte);

    //flags = PTE_FLAGS(*pte);
    /*if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);*/
    
  }
  pte = walkpgdir(src->pgdir, (char *)(PGROUNDUP(src->raw_elf_size) + PGSIZE), 0);
  if(*pte & PTE_P){
    memmove(dst->buf, (char *)(PGROUNDUP(src->raw_elf_size) + PGSIZE), PGSIZE);
    if(store_page(dst, (PGROUNDUP(src->raw_elf_size) + PGSIZE)) < 0){
	panic("no memory to store stack in backing store\n");
    }
  }
  else{
    load_frame(dst->buf, (char *)(PGROUNDUP(src->raw_elf_size) + PGSIZE));
    if(store_page(dst, (PGROUNDUP(src->raw_elf_size) + PGSIZE)) < 0){
	panic("no memory to store stack in backing store\n");
    }
  }

  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
    // Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.


void page_fault_handler(unsigned int fault_addr){
    // rounding it down to the base address of the page
    //if the required address is beyond the user memory space 
    //checked in usertests.c --> sbrktest()
    uint avl;
    if((uint)fault_addr >= KERNBASE){
	cprintf("crossed the boundary of the user memory\n");
	myproc()->killed = 1;
	return;
	//exit();
    }
    fault_addr = PGROUNDDOWN(fault_addr);
    struct proc *currproc = myproc();

    struct elfhdr elf;
    struct proghdr ph;
    struct inode *ip;
    int i, off;
    char *mem;
    int loaded = 0;
    //pte_t *pte;
    //uint eip = currproc->tf->eip;
    //cprintf("I am back");
    //cprintf("faultaddress : %d\n", fault_addr);
/*back:mem = kalloc();
    if(mem == 0){
	cprintf("fault address sz : %d %d\n", fault_addr, currproc->sz);
	page_replacement(currproc);
	goto back;
    } */
    while((mem = kalloc()) == 0){
	//cprintf("fault address %d\n", fault_addr);
	page_replacement(currproc);
    }
    if(currproc->avl < 8){
	
	//cprintf("fault addrs : %d %d\n", fault_addr, currproc->avl);
	(currproc->avl) += 1;
    }
    //cprintf("currproc in ipid, pgflt : %d %d\n",currproc->pid, currproc->avl);
    avl = GETAVL(((currproc->avl) - 1));
	//cprintf("checking av : %d\n", avl);
    if(mappages(currproc->pgdir, (char *)fault_addr, PGSIZE, V2P(mem), PTE_W | PTE_U | PTE_P, avl) < 0){
	    panic("mappages");
    }
    /*pte = walkpgdir(currproc->pgdir, (void *)fault_addr, 0);
    if(*pte & PTE_P)
	cprintf("Good");
    if(pte == 0){
	cprintf("fault address %d\n", fault_addr);
	panic("page faulthandller : pgdir not allocated");
    }*/
    //*pte = V2P(mem) | avl | PTE_W | PTE_U | PTE_P;  
    // if the page is from stack or heap
    if((fault_addr > currproc->raw_elf_size ||  currproc->codeonbs) && load_frame(mem, (char *)fault_addr) == 1){
	//cprintf("Here we loaded zero");
	loaded = 1;
	loaded++;
    }
    
    // if the faulted page is from the elf
    else{

	//ip from the path
	begin_op();
	ip = namei(currproc->path);
	if(ip == 0){
//	    cprintf("addr of ip : %d\n", ip);
	    panic("Namei path");
	}
	ilock(ip);
	readi(ip, (char *)&elf, 0, sizeof(elf));

	for(i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph)){
	    if(readi(ip, (char *)&ph, off, sizeof(ph)) != sizeof(ph))
		panic("Prog header unable to read");

	    if(ph.vaddr <= fault_addr && fault_addr <= ph.vaddr + ph.memsz){
		// if the file size is enough for the page
		if(ph.vaddr + ph.filesz >= fault_addr + PGSIZE){
		    loaduvm(currproc->pgdir, (char *)(ph.vaddr + fault_addr), ip, ph.off + fault_addr, PGSIZE);
    		    //readi(ip, (mem), ph.off + fault_addr, PGSIZE);
		}
		// if the filesize is not enough the append it with zeroes
		else{
		    // if the fault address(page) is for the bss section completely
		    if(fault_addr >= ph.filesz){
			if(fault_addr + PGSIZE <= ph.memsz){
			    stosb(mem, 0, PGSIZE);
			}
			else{
			    //memset(mem, 0, ph.memsz - fault_addr);
			    stosb(mem, 0, ph.memsz - fault_addr);
			}
		    }
		    // if the required page overlaps with (text + data) and bss section
		    else{

			loaduvm(currproc->pgdir, (char *)(ph.vaddr + fault_addr), ip, ph.off + fault_addr, ph.filesz - fault_addr);
			//cprintf("%d\n", readi(ip, (mem), fault_addr, ph.memsz - fault_addr));
			stosb((mem + (ph.filesz - fault_addr)), 0, PGSIZE - (ph.filesz - fault_addr));	    
			ip = namei(currproc->path);
		    }
		}
	    }
	}
	iunlockput(ip);    
	end_op();
    }
}    

/* TODO Not part of academic project but trying to implement in future
 * @param1 struct currproc : structure of the process
 */
void page_replacement(struct proc *currproc){
    pte_t *pte;
    uint i;
    uint avl = 8, pa = 0, min_va = currproc->sz;
    //uint dummy;
    uint flags;
    char reach_avl_max = 0;
    uint pte_avl;
    //if this is the case its process first page we need to look for pages from other 
    //priocess therefore we need to look for global replacement
    //cprintf("eip %d\n" , currproc->tf->eip);
    if(currproc->avl == 0){
	panic("global replacement");
    }
    else{
	//cprintf("currproc sz : %d\n", currproc->sz);
	for(i = 0; i < (currproc->sz); i += PGSIZE){
	    if(i == PGROUNDUP(currproc->raw_elf_size))
		continue;
	    if((pte = walkpgdir(currproc->pgdir, (void *)i, 0)) == 0){
		panic("page replacement : copyuvm should exist");
	    }
	    if(*pte & (PTE_P)){
		//cprintf("currproc-sz : %d\n", currproc->sz);
		pa = PTE_ADDR(*pte); 
		flags = PTE_FLAGS(*pte);
		//cprintf("virual address : %d\n", i );
		//flags = PTE_FLAGS(*pte);
		//cprintf("avl, realavl%d %d\n", avl, PTE_AVL(*pte) >> 9);
		if(avl > (PTE_AVL(*pte) >> 9)){
		    avl =  PTE_AVL(*pte) >> 9;
		    //cprintf("avl : %d\n",avl);
		    min_va = i;
		}
		//*pte = *pte & ~(0xE00);
		if((uint)(PTE_AVL(*pte) >> 9) > 0 ){
		    pte_avl = PTE_AVL(*pte) >> 9;
		    //cprintf("decreasing avl : %d\n", (((PTE_AVL(*pte) >> 9) - 1)) );
		    if(pte_avl == 7 ){
			if(reach_avl_max == 0){
			    reach_avl_max = 1;
			  //  *pte = pa |  GETAVL((pte_avl - 1)) | flags;
			
			}
			else
			    continue;
		    }
		    *pte = pa |  GETAVL((pte_avl - 1)) | flags ;
		}
		//*pte = pa | GETAVL(((PTE_AVL(*pte)>>9) - 1)) | flags;
		//*pte = *pte | GETAVL((avl - 1));
	    }
		
	}
    
	//cprintf("min_va : %d\n", min_va);
	pte = walkpgdir(currproc->pgdir, (void *)min_va, 0);
	pa = PTE_ADDR(*pte);
	//if((*pte & 0x40)){
	    //cprintf("I am dirty");
	    memmove((currproc->buf), (char *)P2V(pa), PGSIZE);
	    if(store_page(currproc, min_va) == -1){
		panic("Backing store size over");
	    }
//	}
	//*pte = *pte & 0xFFFFF000;
	*pte = pa | PTE_W | PTE_U;
	//*pte = *pte;
	char *va = P2V(pa);
	currproc->codeonbs = 1;
//	cprintf("pa : PHYSTOP end %d %d %d\n", (pa), PHYSTOP, end);
	kfree(va);
    }
}

/*
 * @breif a page from the backing store will be loaded in thr
 * main memory
 * @param1 : physical address in the memory
 * @param2 : virtual/logical address of the memory ??is this needed??
 */
int load_frame(char *pa, char *va){
    struct buf *buff;
    struct proc *currproc = myproc();
    //int i, j;
    int j;
    struct bsframe *temp = currproc->blist;
    int current_index;
    uint block_no;
    while(1){
	if((char *)(temp->va) == va){
	    current_index = ((uint)temp - (uint)(back_store.back_store_allocation)) / sizeof(struct bsframe);
	    block_no = BACKSTORE_START + current_index * 8;
	    break;
	}
	if(temp->next_index == -1){
	    return -1;
	}
	temp = &(back_store.back_store_allocation[temp->next_index]);
    }
    for(j = 0; j < 8; j++){
	    buff = bread(ROOTDEV, (block_no) + j);
	    memmove((pa + BSIZE * j), buff->data, BSIZE);
	    brelse(buff);
    }
    return 1;

    /*for(i = 0; i < currproc->index; i++){
	if(back_store_allocation[(currproc->back_blocks[i] - BACKSTORE_START) / 8] == (uint)va){
	    //cprintf("GOT %d\n", i);
	    break;
	}
    }
    
    if(i < currproc->index){
	for(j = 0; j < 8; j++){
	    buff = bread(ROOTDEV, (currproc->back_blocks[i]) + j);
	    memmove((pa + BSIZE * j), buff->data, BSIZE);
	    brelse(buff);
	}
	return 1;
    }*/
    //panic("No such frame in backing store");
}
