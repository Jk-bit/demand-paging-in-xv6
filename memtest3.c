#include "types.h"
#include "user.h"

#define SIZE 100 * 1024 * 1024


char arr[SIZE] = {0};

int main(){
    printf(1, "Memory testing for a big array, reading 100 MB long Charater array byte by byte and printing its occurence at each MB\n");
    uint i = 0;
    for(i = 0; i < (SIZE); i++){
	arr[i] = 1;
	if(i % (1024 * 1024) == 0){
	    printf(1, "*********************%d MB accessed******************\n", (i / (1024 * 1024)));
	}
    }
    exit();
}
