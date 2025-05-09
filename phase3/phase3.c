#include "phase3typedef.h"
#include "phase3.h"
#include "phase2.h"
#include "phase1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Semaphore sems[MAXSEMS];  
int mbox_lock_id;     
int queued;          

// sys_terminate: Terminates the current process, with the status specified. This function never
// returns. Calls join() over and over until it returns -2, then calls quit.
void sys_terminate(USLOSS_Sysargs *args) {
    int dummy = 0;
    int *status = &dummy;
    int retval = join(status);
    while (retval != -2 && retval != -3) {
        retval = join(status);
    }
    int outstatus = (int)(long)args->arg1;
    quit(outstatus);
}


// trampoline: a wrapper for the user function. disables kernel mode, then
// calls the user function with the specified arguments
void trampoline(void *arg) {
    int result = USLOSS_PsrSet(USLOSS_PSR_CURRENT_INT);
    if (result != USLOSS_DEV_OK) {
        USLOSS_Halt(1);
    }

    functionArgs *wrapper = (functionArgs*)arg;
    int (*original_func)(void*) = wrapper->function;
    void *func_arg = wrapper->argument;

    int ret = original_func(func_arg);

    free(wrapper);  
    Terminate(ret);  
}


// sys_spawn: creates a new user mode process. calls spork() and returns the
// pid of the created process through args->arg1
void sys_spawn(USLOSS_Sysargs *args) {
    int stacksize = (int)(long)args->arg3;
    int priority = (int)(long)args->arg4;

    functionArgs *wrapper = (functionArgs*)malloc(sizeof(functionArgs));
    wrapper->function = (int (*)(void*))(args->arg1);
    wrapper->argument = args->arg2;
    int retval = spork((char*)args->arg5, trampoline, wrapper, stacksize, priority);

    args->arg1 = (void*)(long)retval;
    args->arg4 = (void*)0;
}

// sys_wait: Calls join(), and returns the PID and status that join() provided.
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

// sys_semcreate: creates a new semaphore and returns the semaphore ID, for use in later function calls.
// initializes value to store in the semaphore to args-arg1. returns -1 if the initial value is negative
// or if no semaphores are available. Otherwise returns 0.
void sys_semcreate(USLOSS_Sysargs *args) {
  int initial_value = (int)(long) args->arg1;

  if (initial_value < 0) {
      args->arg4 = -1;
      return;
  }

  int index = -1;
  for (int i = 0; i < MAXSEMS; i++) {
      if (!sems[i].in_use) {
          index = i;
          break;
      }
  }

  if (index == -1) {
      args->arg4 = -1; 
      return;
  }

  sems[index].value = initial_value;
  sems[index].mboxID = MboxCreate(0, 0); 
  sems[index].in_use = 1;

  args->arg1 = (void*)(long)index;
  args->arg4 = 0;
}


// sys_semp: executes the P() operation, which decrements the value of the semaphore by 1, unless
// the value is 0 in which case the process blocks via zero slot mailbox.
void sys_semp(USLOSS_Sysargs *args) {
  int index = (int)(long) args->arg1;

  if (index < 0 || index >= MAXSEMS || !sems[index].in_use) {
      args->arg4 = (void *)(long)-1;
      return;
  }

  sems[index].value--;  

  if (sems[index].value < 0) {
      sems[index].blocked++;
      MboxRecv(sems[index].mboxID, NULL, 0);  
  }

  args->arg4 = (void *)(long)0;
}


// sys_semv: executes the V() operation, which increments the value of the semaphore by 1, and
// wakes up any process blocked on P() via zero slot mailbox.
void sys_semv(USLOSS_Sysargs *args) {
  int index = (int)(long) args->arg1;

  if (index < 0 || index >= MAXSEMS || !sems[index].in_use) {
      args->arg4 = (void *)(long)-1;
      return;
  }

  sems[index].value++; 

  if (sems[index].value <= 0 && sems[index].blocked > 0) {
      sems[index].blocked--;  
      MboxSend(sems[index].mboxID, NULL, 0);  
  }

  args->arg4 = (void *)(long)0;
}

// sys_gettime: returns time of day via out pointer, arg1
void sys_gettime(USLOSS_Sysargs *args) {
    args->arg1 = currentTime();
}

// sys_getpid: returns the pid via out pointer, arg1
void sys_getpid(USLOSS_Sysargs *args) {
  args->arg1 = getpid();
}

void phase3_init(void){
  for (int i = 0; i < MAXSEMS; i++) {
    sems[i].value = -1;
    sems[i].mboxID = -1;
    sems[i].in_use = 0;
  }
    queued = 0;
    mbox_lock_id = MboxCreate(0,0);

    systemCallVec[3] = sys_spawn;
    systemCallVec[4] = sys_wait;
    systemCallVec[5] = sys_terminate;
    systemCallVec[16] = sys_semcreate;
    systemCallVec[17] = sys_semp;
    systemCallVec[18] = sys_semv;
    systemCallVec[20] = sys_gettime;
    systemCallVec[22] = sys_getpid;
  }


void phase3_start_service_processes(void){

  }

// kernSemCreate: creates a new semaphore and returns the semaphore ID, for use in later function calls.
// initializes value to store in the semaphore to args-arg1. returns -1 if the initial value is negative
// or if no semaphores are available. Must be called from kernel mode.
int kernSemCreate(int value, int *semaphore) {
  int index = -1;
  for (int i = 0; i < MAXSEMS; i++) {
      if (!sems[i].in_use) {
          index = i;
          break;
      }
  }

  if (index == -1) {
      return -1; // no free semaphores
  }

  sems[index].value = value;
  sems[index].mboxID = MboxCreate(0, 0);
  sems[index].in_use = 1;

  *semaphore = index; // output the semaphore ID
  return 0;
}


// kernSemP: executes the P() operation, which decrements the value of the semaphore by 1, unless
// the value is 0 in which case the process blocks via zero slot mailbox. Must be called in kernel mode.
int kernSemP(int semaphore) {
  if (semaphore < 0 || semaphore >= MAXSEMS || !sems[semaphore].in_use) {
      return -1;
  }

  if (sems[semaphore].value == 0) {
      MboxSend(sems[semaphore].mboxID, NULL, 0);
  } else {
      sems[semaphore].value--;
  }

  return 0;
}


// kernSemV: executes the V() operation, which increments the value of the semaphore by 1, and
// wakes up any process blocked on P() via zero slot mailbox. Must be called in kernel mode.
int kernSemV(int semaphore) {
  if (semaphore < 0 || semaphore >= MAXSEMS || !sems[semaphore].in_use) {
      return -1;
  }

  sems[semaphore].value++;
  MboxRecv(sems[semaphore].mboxID, NULL, 0); // wake one up

  return 0;
}