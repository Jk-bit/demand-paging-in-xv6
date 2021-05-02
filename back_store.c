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
    initlock(&back_store.lock, "back_store");
    for(int i = 0; i <BACKSTORE_SIZE/8; i++){
	back_store.back_store_allocation[i].va = -1;
    }
}


int store_page(struct proc *currproc, uint va){
    uint block_no;
    struct buf *frame;
    int i, j;
    int current_index;
    //cprintf("currproc->index : %d\n",currproc->index);
    struct bsframe *temp = currproc->blist;
    struct bsframe *prev = temp;
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
    /*for(i = 0; i < currproc->index; i++){
	if((back_store_allocation[(currproc->back_blocks[i] - BACKSTORE_START) / 8]) == va){
	    block_no = currproc->back_blocks[i];
	    for(j = 0; j < 8; j++){
		frame = bget(ROOTDEV, block_no + j);
		memmove(frame->data, currproc->buf + BSIZE*j, BSIZE);
		bwrite(frame);
		brelse(frame);
	    }
	    //cprintf("got index : %d\n", i);
	    return 1;
	}
    }*/
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


uint get_free_block(){
    int i;
    for(i = 0; i < BACKSTORE_SIZE/8; i++){
	if(back_store.back_store_allocation[i].va == -1){
	    return (BACKSTORE_START + i*8);
	}
    }
    return -1;
}

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

