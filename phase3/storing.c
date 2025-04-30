// /* phase3.c - Kernel-mode implementation for Phase 3 */

// #include "phase3.h"
// #include "phase2.h"
// #include "phase1.h"
// #include "phase3_kernelInterfaces.h"  // Declares spawn_process, join, quit, etc.
// #include "phase3typedef.h"            // Declares MAXSEMS, Semaphore, PCB, MAX_PROCESSES, etc.
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <ucontext.h>
// #include <usyscall.h>   // Defines USLOSS_Sysargs and related functions

// #ifndef SYS_FINISH
// #define SYS_FINISH 5
// #endif


// /*--------------------- Global Variables ---------------------*/

// /* Process Control Block (PCB) table and scheduler variables.
//    (PCB structure should be defined in phase3typedef.h.) */
// PCB pcbTable[MAX_PROCESSES];
// int next_pid = 4;
// int current = -1;   // index of the current process

// /* A context that the kernel uses to resume when a process terminates. */
// ucontext_t scheduler_context;

// /* System call vector array.
//    The size MAXSYSCALLS should be defined appropriately (e.g., in phase2.h). */
// static Semaphore semaphores[MAXSEMS];
// extern void (*systemCallVec[MAXSYSCALLS])(USLOSS_Sysargs *args);


// /* Default system call handler: prints an error and halts the simulation. */
// void nullsys(USLOSS_Sysargs *args) {
//     USLOSS_Console("nullsys():",
//                    args->number, USLOSS_PsrGet());
//     USLOSS_Halt(1);
// }

// void terminateHandler(USLOSS_Sysargs *args) {
//   int status = (int)(long) args->arg1;
//   USLOSS_Console("status: %d\n", status);
//   quit(status);
// }

// void finishHandler(USLOSS_Sysargs *args) {
//   USLOSS_Halt(0);
// }

// void waitHandler(USLOSS_Sysargs *args) {
//   int status;
//   int pid = join(&status);  // join is a kernel function that blocks until a child finishes.
//   USLOSS_Console("waitHandler: pid %d\n", pid);
//   args->arg1 = (void *)(long)pid;   // return child's PID
//   args->arg2 = (void *)(long)status; // return child's exit status
//   args->arg4 = (void *)(long)0;      // indicate success
// }

// /*------------------- Kernel-Mode Spawn Handler -------------------*/

// /* spawnHandler: This function is registered as the kernel handler for SYS_SPAWN.
//    It extracts arguments from the USLOSS_Sysargs structure, calls spawn_process,
//    and returns the new process's PID back in the USLOSS_Sysargs structure. */
// void spawnHandler(USLOSS_Sysargs *args) {
//     USLOSS_Console("Entering the spawn handler\n");
//     char *name = (char *) args->arg5;
//     int (*func)(void*) = (int (*)(void*)) args->arg1;
//     void *childArg = args->arg2;
//     int stack_size = (int)(long) args->arg3;
//     int priority   = (int)(long) args->arg4;
    
//     int pid = spawn_process(name, func, childArg, stack_size, priority);
//     USLOSS_Console("we got here\n");
//     args->arg1 = (void *)(long) pid;  // Return PID to user mode.
//     args->arg4 = (void *)(long) 0;      // Optionally, return success status.
// }

// /*---------------------- Initialization ----------------------*/

// /* phase3_init: Initializes the syscall vector and the semaphore array.
//    (This function is called early during system bootstrap.) */
//    void phase3_init(void) {
//     int i;
//     /* Initialize the system call vector.
//        Set all syscalls to the default handler, then override the ones you implement. */
//     for (i = 0; i < MAXSYSCALLS; i++) {
//         systemCallVec[i] = nullsys;
//     }
//     systemCallVec[SYS_SPAWN] = spawnHandler;
//     systemCallVec[SYS_TERMINATE] = terminateHandler;  // Add this line.
//     systemCallVec[SYS_FINISH] = finishHandler;
//     systemCallVec[SYS_WAIT] = waitHandler;
    
//     /* Initialize the fixed-length semaphore array. */
//     for (i = 0; i < MAXSEMS; i++) {
//         semaphores[i].valid = 0;
//         semaphores[i].value = 0;
//         semaphores[i].mboxID = -1;
//     }
// }


// /* phase3_start_service_processes: Stub for starting any service processes.
//    (Currently a no-op.) */
// void phase3_start_service_processes(void) {
//     // No service processes to start in this implementation.
// }

// /*------------------- Process Management -------------------*/

// /* processWrapper: Wraps the execution of a new process.
//    When the process function returns, it calls quit with the return value.
//    (Assumes quit is implemented elsewhere, perhaps in the Phase 1 library.) */
// void processWrapper(void *arg) {
//     PCB *pcb = (PCB*) arg;
//     int ret = pcb->func(pcb->arg);
//     quit(ret);
// }

// /* spawn_process: Kernel implementation to create a new process.
//    It allocates a PCB slot, creates a new ucontext with its own stack, and
//    sets the process to run processWrapper. */
// int spawn_process(char *name, int (*func)(void *), void *arg, int stack_size, int priority) {
//     (void) name;      // Process name is ignored in this simple simulation.
//     (void) priority;  // Priority is not handled here.
//     PCB *pcb = NULL;
//     int slot = -1;
//     for (int i = 0; i < MAX_PROCESSES; i++) {
//         if (!pcbTable[i].valid) {
//             slot = i;
//             pcb = &pcbTable[i];
//             break;
//         }
//     }
//     if (pcb == NULL) {
//         USLOSS_Console("spawn_process: No free PCB slot\n");
//         return -1;
//     }
    
//     pcb->pid = next_pid++;
//     pcb->func = func;
//     pcb->arg = arg;
//     pcb->finished = 0;
//     pcb->valid = 1;
//     pcb->exitStatus = 0;
//     pcb->stack = malloc(stack_size);
//     if (pcb->stack == NULL) {
//         USLOSS_Console("spawn_process: malloc failed\n");
//         pcb->valid = 0;
//         return -1;
//     }
    
//     /* Initialize the new process's context. */
//     getcontext(&pcb->context);
//     pcb->context.uc_stack.ss_sp = pcb->stack;
//     pcb->context.uc_stack.ss_size = stack_size;
//     /* When the process function returns, control goes to scheduler_context.
//        processWrapper will call quit to finish the process. */
//     pcb->context.uc_link = &scheduler_context;
//     makecontext(&pcb->context, (void (*)())processWrapper, 1, pcb);
    
//     USLOSS_Console("spawn_process: Created process with pid %d\n", pcb->pid);
//     return pcb->pid;
// }

// /*---------------------- Semaphore Functions ----------------------*/

// /* kernSemCreate: Creates a semaphore and its associated mailbox. */
// int kernSemCreate(int value, int *semaphore) {
//     if (value < 0 || semaphore == NULL) {
//         return -1;
//     }
//     for (int i = 0; i < MAXSEMS; i++) {
//         if (!semaphores[i].valid) {
//             semaphores[i].valid = 1;
//             semaphores[i].value = value;
//             /* Create a mailbox for blocking processes.
//                For example, 100 slots and message size equal to sizeof(int). */
//             semaphores[i].mboxID = MboxCreate(100, sizeof(int));
//             if (semaphores[i].mboxID < 0) {
//                 semaphores[i].valid = 0;
//                 return -1;
//             }
//             *semaphore = i;
//             return 0;
//         }
//     }
//     return -1;  // No available semaphore slot.
// }

// /* kernSemP: P (wait) operation on a semaphore.
//    If the semaphore value becomes negative, the process blocks on its mailbox. */
// int kernSemP(int semID) {
//     if (semID < 0 || semID >= MAXSEMS || !semaphores[semID].valid) {
//         return -1;
//     }
//     semaphores[semID].value--;
//     if (semaphores[semID].value < 0) {
//         int dummy;
//         MboxRecv(semaphores[semID].mboxID, &dummy, sizeof(dummy));
//     }
//     return 0;
// }

// /* kernSemV: V (signal) operation on a semaphore.
//    If processes are waiting (value <= 0), a dummy message is sent to wake one. */
// int kernSemV(int semID) {
//     if (semID < 0 || semID >= MAXSEMS || !semaphores[semID].valid) {
//         return -1;
//     }
//     semaphores[semID].value++;
//     if (semaphores[semID].value <= 0) {
//         int dummy = 0;
//         MboxSend(semaphores[semID].mboxID, &dummy, sizeof(dummy));
//     }
//     return 0;
// }
