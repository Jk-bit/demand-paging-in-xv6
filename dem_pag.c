#include "defs.h"
#include "mmu.h"
#include "proc.h"

/* 
 * @breif allocates a page table entry in the pgdir
 * and loads the page in the memory
 * @param1 fault_addr : the virtual address that resulted in fault
 */
void page_fault_handler(unsigned int fault_addr){
    // rounding it down to the base address of the page
    fault_addr = PGROUNDDOWN(fault_addr);
    struct proc *currproc = myproc();
    char *mem = kalloc();
    if(mem == 0){
	panic("No memory");
	//page_replacement(fault_addr);
    }
    if(mappages(currproc->pgdir, (char *)fault_addr, PGSIZE, V2P(mem), PTE_W | PTE_U | PTE_P) < 0){
	    panic("mappages");
    }
    // if the page is from stack or heap ---> TODO need to handle more like the page needed is heap or stack
    if(currproc->raw_elf_size < fault_addr){
	load_frame(mem, (char *)fault_addr);
    }
    // if the faulted page is from the elf
    else{
	// if the file size is enough for the page
	if(currproc->raw_elf_size > fault_addr + PGSIZE)
	    readi(currproc->ip, P2V(mem), fault_addr, PGSIZE);
	// if the filesize is not enough the append it with zeroes
	else{
	    readi(currproc->ip, P2V(mem), fault_addr, currproc->raw_elf_size - fault_addr);
	    stosb(mem + (currproc->raw_elf_size - fault_addr), 0, PGROUNDUP(fault_addr) - currproc->raw_elf_size);	    
	}
    }
}


/*
 * @breif planning to implement local page replacement
 * algorithm
 * @param1 fault_addr : virtual address which caused the fault
 */
void page_replacement(unsigned int fault_addr){
    
}

/*
 * @breif a page from the backing store will be loaded in thr
 * main memory
 * @param1 : physical address in the memory
 * @param2 : virtual/logical address of the memory ??is this needed??
 */

void load_frame(char *pa, char *va){

}
