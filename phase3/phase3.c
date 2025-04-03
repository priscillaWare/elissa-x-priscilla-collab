#include "phase3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void sys_terminate(USLOSS_Sysargs *args) {
    int dummy = 0;
    int *status = &dummy;
    int retval = join(status);
    while (retval != -2 && retval != -3) {
        retval = join(status);
    }
    int outstatus = args->arg1;
    quit(outstatus);
}

void trampoline(){
  int result = USLOSS_PsrSet(current | USLOSS_PSR_CURRENT_INT);
     if (result != USLOSS_DEV_OK) {
         USLOSS_Halt(1);
     }
     
  }

void sys_spawn(USLOSS_Sysargs *args) {
    int stacksize = (int)(long)args->arg3;
    int priority = (int)(long)args->arg4;
    printf("calling spork\n");
    int retval = spork((char *)args->arg5, (int (*)(void*))args->arg1, args->arg2, stacksize, priority);
    printf("spork returned %d\n", retval);

    args->arg1 = retval;
    args->arg4 = 0;
}

void sys_wait(USLOSS_Sysargs *args) {
    int dummy = 0;
    int *status = &dummy;
    int retval = join(status);
    printf("result = %d\n", retval);
    args->arg2 = status;
    args->arg1 = retval;
    if (retval == -2) {
      args->arg4 = -2;
      }
    else {
      args->arg4 = 0;
      }
}

void phase3_init(void){
    systemCallVec[3] = sys_spawn;
    systemCallVec[4] = sys_wait;
    systemCallVec[5] = sys_terminate;
  }


void phase3_start_service_processes(void){

  }

int kernSemCreate(int value, int *semaphore) {
  return 0;
  }


int kernSemP(int semaphore) {
  return 0;
  }


int kernSemV(int semaphore){
  return 0;
  }
