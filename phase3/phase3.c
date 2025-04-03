#include "phase3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int sems[MAXSEMS];
int mbox_lock_id;
int queued;


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

void trampoline(void *arg){
  //int current = USLOSS_PsrGet();
  int result = USLOSS_PsrSet(USLOSS_PSR_CURRENT_INT);
     if (result != USLOSS_DEV_OK) {
         USLOSS_Halt(1);
     }

    int (*original_func)(void*) = (int (*)(void*))arg; // Cast 'arg' back to the original function pointer
    original_func(arg);  // Call the original function
    Terminate(0);
  }

void sys_spawn(USLOSS_Sysargs *args) {
    int stacksize = (int)(long)args->arg3;
    int priority = (int)(long)args->arg4;
    int retval = spork((char *)args->arg5, (int (*)(void*))trampoline, args->arg1, stacksize, priority);

    args->arg1 = retval;
    args->arg4 = 0;
}

void sys_wait(USLOSS_Sysargs *args) {
    int dummy = 0;
    int *status = &dummy;
    int retval = join(status);
    args->arg2 = *status;
    args->arg1 = retval;
    if (retval == -2) {
      args->arg4 = -2;
      }
    else {
      args->arg4 = 0;
      }
}

void sys_semcreate(USLOSS_Sysargs *args) {
   int index = -1;
   for (int i = 0; i < MAXSEMS; i++) {
     if (sems[i] == -1){
       index = i;
       break;
     }
   }
   if (index == -1){
     args->arg1 = 0;
     args->arg4 = -1;
     }
   else {
     sems[index] = args->arg1;
     args->arg1 = index;
     args->arg4 = 0;
   }
}

void sys_semp(USLOSS_Sysargs *args) {
    int index = args->arg1;
    if (sems[index] == 0) {
      queued++;
      MboxSend(mbox_lock_id, NULL, 0);
    }
    sems[index]--;
    if (index >= 0 && index < MAXSEMS) {
      args->arg4 = 0;
    }
    else {
      args->arg4 = 0;
      }
}

void sys_semv(USLOSS_Sysargs *args) {
    int index = args->arg1;
    sems[index]++;
    if (index >= 0 && index < MAXSEMS) {
      args->arg4 = 0;
    }
    else {
      args->arg4 = 0;
      }
    if (queued > 0) {
      queued--;
      MboxRecv(mbox_lock_id, NULL, 0);
      }
}

void sys_getpid(USLOSS_Sysargs *args) {
  args->arg1 = getpid();
}

void phase3_init(void){
    for (int i = 0; i < MAXSEMS; i++) {
      sems[i] = -1;
    }
    queued = 0;
    mbox_lock_id = MboxCreate(0,0);

    systemCallVec[3] = sys_spawn;
    systemCallVec[4] = sys_wait;
    systemCallVec[5] = sys_terminate;
    systemCallVec[16] = sys_semcreate;
    systemCallVec[17] = sys_semp;
    systemCallVec[18] = sys_semv;
    systemCallVec[22] = sys_getpid;
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


int kernSemV(int semaphore){
  return 0;
  }
