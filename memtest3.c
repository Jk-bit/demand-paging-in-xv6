#include "types.h"
#include "user.h"
char arr[15*1024*1024] = {0};

int main(){
    uint i = 0;
    for(i = 0; i < (15 * 1024 * 1024); i++){
	arr[i] = 1;
	if(i % (1024 * 1024) == 0){
	    printf(1, "**********************************thousand**********************************************\n");
	}
    }
    exit();
}
