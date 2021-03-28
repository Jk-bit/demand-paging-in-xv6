#include "defs.h"
#include "mmu.h"
#include "proc.h"

/* 
 * @breif allocates a page table entry in the pgdir
 * and loads the page in the memory
 * @param1 fault_addr : the virtual address that resulted in fault
 */
void page_fault_handler(unsigned int fault_addr){
    fault_addr = PGROUNDDOWN(fault_addr);
    struct proc *currproc = myproc();
    char *mem = kalloc();
    if(mem == 0){
	page_replacement(fault_addr);
    }
    if(mappages(currproc->pgdir, (char *)fault_addr, PGSIZE, V2P(mem), PTE_W | PTE_U) < 0){
	panic("mappages");
    }
    load_frame(mem, (char *)fault_addr);
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
