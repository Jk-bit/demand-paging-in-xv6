#include "param.h"
#include "buf.h"


/*
 * Each page requires 4 blocks to be stored
 * back_store_buf acts as a cache dor the process
 * back_store_allocation :
 * index i in  the array denotes the physical memory of the 
 * page stored at the block no (i+1)*4
 */
typedef struct back_store_buf{
    struct buf page[4];
    int block_start;
}back_store_buf;

/*
 *  structure containing inode and block_list which acts as a 
 *  backing store for the process
 */

struct block_list{
    int block_no;
    struct block_list *next;
}block_list;

typedef struct back_store_proc{
    struct inode *ip;
    struct block_list;
}back_store_proc;

static int back_store_allocation[BACKSTORE_SIZE/4];
