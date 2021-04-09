#include "defs.h"
#include "param.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "back_store.h"



/*
 * @breif stores the currproc->buf which contains the page to 
 * be page out in the backing store
 * @param currproc : pointer to process which needs to page out the buffer
 * @param va : The starting virtual address of the page whose buff is going to be page out.
 */

void backstore_init(){
    for(int i = 0; i <BACKSTORE_SIZE/8; i++){
	back_store_allocation[i] = -1;
    }
}


void store_page(struct proc *currproc, uint va){
    uint block_no;
    int i;
    if((block_no = get_free_block()) == -1){
	panic("No free block in back_store");	
    }
    back_store_allocation[block_no - BACKSTORE_START] = va;
    if(currproc->index == MAX_BACK_PAGES - 1){
	panic("Process out of bound of backstore memory");
    }
    currproc->back_blocks[currproc->index++] = block_no;
    struct buf *frame;
    //Come up with a better solution
    for(i = 0; i < 8; i++){
	frame = bget(ROOTDEV, block_no + i);
	memmove(frame->data, (currproc->buf) + BSIZE*i, BSIZE);
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


