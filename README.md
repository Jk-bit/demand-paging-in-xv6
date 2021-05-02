### Page fault handling
    At first fault the code and the data are accessed from the xv6 native file system
    For further faults in the program the code and data are written to thr backing store


### Page replacament algorithm
     	AVL is a 3 bit value (bit 10 11 12) which is available for us 
     	So in our code we have used it to signify the priority to replace the 
     	page during the page replacement
     	    The priority is based on the logic that whichever page faults first is
     	    given the available avl value starting from 0, once there are 8 pages 
     	    fulfilling the 3 bits, all other pages faulting after that will get the
     	    avl value as 7.
     	    When there is a page fault and the user memory is full and there is a 
     	    need of page replacement we page out the page with avl value zero and
     	    all other pages's avl of a process is deceremented

### Backing store mapping
 	Every process contains the pointer blist which points to the first
 	bframe of the backing store, if the value of the next index of the
 	first frame is -1 the list ends otherwise it denotes to the next possible
 	bsframe.
 	
 	struct bsframe -> contains the virtual address and the next index if present

