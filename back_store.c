#include "back_store.h"
#include "ide.h"
#include "defs.h"
/*
 * @breif stores the currproc->buf which contains the page to 
 * be page out in the backing store
 * @param currproc : pointer to process which needs to page out the buffer
 * @param va : The starting virtual address of the page whose buff is going to be page out.
 */
void store_page(struct proc *currproc, unit va){
    unit block_no;
    int i;
    block_list *ptr_blist;
    if((block_no = get_free_block()) == 0){
	panic("No free block in back_store");	
    }
    back_store_allocation[block_no - BACKSTORE_START] = va;
    llinsert(currproc->block_list, block_no);
    struct buf *frame;
    //Come up with a better solution
    for(i = 0; i < 8; i++){
	frame = bget(ROOT_DEV, block_no + i);
	frame->data = (currproc->buf) + PGSIZE*i;
	bwrite(frame);
    }
    
}


uint get_free_block(){
    int i;
    for(i = 0; i < BACKSTORE_SIZE/8; i++){
	if(back_store_allocation[i] == -1){
	    return BACKSTORE_START + i;
	}
    }
    return 0;
}


