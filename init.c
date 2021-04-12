// init: The initial user-level program

#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

char *argv[] = { "sh", 0 };

int
main(void)
{
  int pid, wpid;

  if(open("console", O_RDWR) < 0){
    mknod("console", 1, 1);
    open("console", O_RDWR);
  }
  dup(0);  // stdout
  dup(0);  // stderr
    pid = 0;
  for(;;){
    printf(1, "init: starting sh\n");
    pid = fork();
    printf(1, "pid : %d\n", pid);
    if(pid < 0){
      printf(1, "init: fork failed\n");
      exit();
    }
    if(pid == 0){

      printf(1, "%d\n", 1);
      exec("sh", argv);
      printf(1, "init: exec sh failed\n");
      exit();
    }
    printf(1, "Down in init");
    while((wpid=wait()) >= 0 && wpid != pid)
      printf(1, "zombie!\n");
  }
}
