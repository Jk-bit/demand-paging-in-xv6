### Backing store code and problem solving->
    - Only Stack and heap are going to be stored in the backing store and the elf file
      is going to be backup(ed) in the file system.
    - There is static array of the storing the virtual address of the page it contains and the index i 
      denotes the index in the backing store starting from BACK_STORE_START.
    - Every process will have a linked list of it allocated backing store blocks

    - As every blocks is of size 512 Bytes we will read/write 8 times from the backing store
      to get/store the page.

### Page fault  handling code -->
    - checks if the address that resulted into the fault is from the elf file or not 
	if yes we read it from the filesystem, if the address that cause the fault is from
	stack/heap we locate the block :
	    - We traverse through the linkedlist of blocks and blocks as the index into the	
		static array of backing store we get the starting virtual address of the page
		that is stored in that respective block(block to block*7).
