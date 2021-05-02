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
 *  Mapping of backing store
 *
 *	Every process contains the pointer blist which points to the first
 *	bframe of the backing store, if the value of the next index of the
 *	first frame is -1 the list ends otherwise it denotes to the next possible
 *	bsframe.
 *	
 *	struct bsframe -> contains the virtual address and the next index if present
 */


/*
 *  @breif : Initializes all the values in the va value of all bsframes to -1
 */
void backstore_init(){
    initlock(&back_store.lock, "back_store");
    for(int i = 0; i <BACKSTORE_SIZE/8; i++){
	back_store.back_store_allocation[i].va = -1;
    }
}

/*
 *  @breif : stores the page in the backing store mapping to the 
 *	     virtual address va, where the content of the page is stored 
 *	     in curproc->buf
 *  @param1 : pointer to the process
 *  @param2 : virtual address
 *  @retval : returns -1 on failure and 1 on sucess
 */
int store_page(struct proc *currproc, uint va){
    uint block_no;
    struct buf *frame;
    int i, j;
    int current_index;
    struct bsframe *temp = currproc->blist;
    struct bsframe *prev = temp;
    // if the blist is empty start it
    if(currproc->blist == 0){
	if((block_no = get_free_block()) == -1){
	    return -1;
	}
	currproc->blist = &(back_store.back_store_allocation[(block_no - BACKSTORE_START) / 8]);
	acquire(&back_store.lock);
	currproc->blist->va = va;
	currproc->blist->next_index = -1;
	release(&back_store.lock);
	for(j = 0; j < 8; j++){
		frame = bget(ROOTDEV, block_no + j);
		memmove(frame->data, currproc->buf + BSIZE*j, BSIZE);
		bwrite(frame);
		brelse(frame);
	}

	    //cprintf("got index : %d\n", i);
	return 1;
    }
    // finding if the virtual address for the given process is stored on the backing store
    while(1){
	if((uint)temp->va == va){
	    current_index = ((uint)temp - (uint)(back_store.back_store_allocation)) / sizeof(struct bsframe);
	    block_no = BACKSTORE_START + current_index * 8;
	    for(j = 0; j < 8; j++){
		frame = bget(ROOTDEV, block_no + j);
		memmove(frame->data, currproc->buf + BSIZE*j, BSIZE);
		bwrite(frame);
		brelse(frame);
	    }
	    //cprintf("got index : %d\n", i);
	    return 1;
		
	}
	if(temp->next_index == -1){
	    break;
	}
	prev = temp;
	temp = &(back_store.back_store_allocation[temp->next_index]);
    }
    if((block_no = get_free_block()) == -1){
	return -1;
    }
    
    //cprintf("received block no : %d\n", block_no);
    acquire(&back_store.lock);
    back_store.back_store_allocation[(block_no - BACKSTORE_START) / 8].va = va;
    back_store.back_store_allocation[(block_no - BACKSTORE_START) / 8].next_index = -1;
    release(&back_store.lock);
    prev->next_index = (block_no - BACKSTORE_START) / 8;
    /*if(currproc->index == MAX_BACK_PAGES - 1){
	return -1;
    }*/
    //currproc->back_blocks[currproc->index++] = block_no;
    for(i = 0; i < 8; i++){
	frame = bget(ROOTDEV, block_no + i);
	memmove(frame->data, (currproc->buf) + BSIZE*i, BSIZE);
	bwrite(frame);
	brelse(frame);
    }
    return 1; 
}

/*
 *  @breif : finds the freeblock in the backing store and returns the block no
 */
uint get_free_block(){
    int i;
    for(i = 0; i < BACKSTORE_SIZE/8; i++){
	if(back_store.back_store_allocation[i].va == -1){
	    return (BACKSTORE_START + i*8);
	}
    }
    return -1;
}
/*
 *  @breif : frees the bacling store for the given process
 */
void freebs(struct proc *curproc){
    /*for(int i = 0; i < curproc->index; i++){
	back_store_allocation[(curproc->back_blocks[i] - BACKSTORE_START) / 8] = -1;
    }    
    curproc->index = 0;*/
    struct bsframe *temp = curproc->blist;
    struct bsframe *prev =temp;
    if(temp == 0)
	return;
    acquire(&back_store.lock);
    while(temp->next_index != -1){
	temp->va = -1;
	prev = temp;
	temp = &(back_store.back_store_allocation[temp->next_index]);
	prev->next_index = -1;
    }
    temp->va = -1;
    release(&back_store.lock);
    return;
}

