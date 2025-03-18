/*
 * File: phase1b.c
 * Purpose: Implements the core process management routines for Phase 1B of USLOSS.
 *          Adds process blocking, zapping, and a dispatcher with priority scheduling.
 * Authors: Elissa Matlock and Priscilla Ware
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include "phase1b.h"
 #include "usloss.h"
 
 /*
  * Global data structures
  * ----------------------
  * process_table: an array of all possible processes (maximum MAXPROC).
  * running_process: pointer to the currently running process.
  * next_pid: tracks the next available PID to assign.
  * ready_queues: an array of 6 lists (or queues), each storing processes of a given priority.
  */
 static Process process_table[MAXPROC];
 Process *running_process = NULL;
 static int next_pid = 0;
 
 // Queues for scheduling. Index 0 corresponds to priority=1, up to index 5 for priority=6.
 static Process *ready_queues[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
 
 extern int testcase_main(void *arg);
 
 /*
  * init_run
  * --------
  * This function is the starting function for the "init" process (PID 1).
  * It enables interrupts, starts various service processes (phase2...phase5),
  * spawns the testcase_main process, and then repeatedly waits (joins) for children
  * until there are no more processes. Once no children remain, it halts the system.
  */
 int init_run(void *arg) {
 
     // Enable interrupts by modifying the PSR (Processor Status Register).
     int current = USLOSS_PsrGet();
     int result = USLOSS_PsrSet(current | USLOSS_PSR_CURRENT_INT);
     if (result != USLOSS_DEV_OK) {
         USLOSS_Halt(1);
     }
 
     // These extern calls are placeholders that print some messages.
     // They represent starting service processes in later phases of the OS.
     extern void phase2_start_service_processes(void);
     extern void phase3_start_service_processes(void);
     extern void phase4_start_service_processes(void);
     extern void phase5_start_service_processes(void);
 
     phase2_start_service_processes();
     phase3_start_service_processes();
     phase4_start_service_processes();
     phase5_start_service_processes();
 
     // Spawn a test-case main process at priority 3.
     int testcase_pid = spork("testcase_main", testcase_main, NULL, USLOSS_MIN_STACK, 3);
     if (testcase_pid < 0) {
         USLOSS_Halt(1);
     }
 
     // Switch to the highest-priority ready process (likely testcase_main).
     dispatcher();
 
     // Loop forever, waiting for children to exit via join.
     // If join returns -2, it means no children remain, so we can halt the system.
     while (1) {
         int status;
         int childPid = join(&status);
         if (childPid == -2) {
             USLOSS_Halt(0);
         }
     }
     return 0;
 }
 
 /*
  * phase1_init
  * -----------
  * Initializes the entire process table by setting each slot to "empty" (-1).
  * Creates and configures the special "init" process (PID 1, priority 6).
  * Also allocates a stack for the init process, initializes its context,
  * and places it on the ready queue for priority=6.
  */
 void phase1_init(void) {
 
     // Initialize each slot in the process table to show it's unused.
     for (int i = 0; i < MAXPROC; i++) {
         process_table[i].pid = -1;
         process_table[i].status = -1;   // -1 often indicates "not in use" or "terminated"
         process_table[i].children = NULL;
         process_table[i].sibling = NULL;
         process_table[i].next = NULL;
         process_table[i].parent = NULL;
         process_table[i].exit_status = 0;
         process_table[i].stack = NULL;
     }
 
     // Create the init process (PID=1, priority=6).
     Process *init_proc = &process_table[1];
     init_proc->pid = 1;
     init_proc->priority = 6;
     strncpy(init_proc->name, "init", MAXNAME);
     init_proc->status = 0;  // 0 = Runnable
     init_proc->parent = NULL;
     init_proc->startFunc = init_run;
 
     // Allocate a stack for the init process.
     char *init_stack = malloc(USLOSS_MIN_STACK);
     if (init_stack == NULL) {
         USLOSS_Console("ERROR: malloc failed for init process\n");
         USLOSS_Halt(1);
     }
     init_proc->stack = init_stack;
 
     // Initialize the USLOSS context for init.
     USLOSS_ContextInit(&init_proc->context, init_stack, USLOSS_MIN_STACK, NULL, processWrapper);
 
     // Place init in the ready_queues at index 5 (priority=6).
     ready_queues[5] = init_proc;
 
     // Bump next_pid by 2 because we assigned PID=1 manually.
     // (Some code might skip PIDs 1 and 2, depending on design.)
     next_pid += 2;
 }
 
 /*
  * spork
  * -----
  * Create a new process with the specified attributes:
  *   - name
  *   - start function
  *   - argument
  *   - stack size
  *   - priority
  * Allocates a free slot from the process table, assigns a PID, sets up the process's context,
  * links the new process to its parent, and places it in the ready queue.
  *
  * Returns the new PID on success, or a negative number on error.
  */
 int spork(char *name, int (*startFunc)(void*), void *arg, int stackSize, int priority) {
     // Must be called from kernel mode (not user mode).
     if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
         USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
         USLOSS_Halt(1);
     }
 
     // Priority must be between 1 and 5 in this system.
     if (priority < 1 || priority > 5) {
         USLOSS_Console("spork(): ERROR - Invalid priority %d\n", priority);
         return -1;
     }
 
     // Check if stackSize is large enough.
     if (stackSize < USLOSS_MIN_STACK) {
         USLOSS_Console("spork(): ERROR - Stack size too small\n");
         return -2;
     }
 
     // Find a free slot in the process_table, starting at next_pid % MAXPROC.
     // If none found, wrap around to 0.
     int slot = -1;
     for (int i = next_pid % MAXPROC; i < MAXPROC; i++) {
         if (process_table[i].pid == -1) {
             slot = i;
             break;
         }
     }
     if (slot == -1) {
         for (int i = 0; i < next_pid % MAXPROC; i++) {
             if (process_table[i].pid == -1) {
                 slot = i;
                 break;
             }
         }
     }
     // If slot is still -1, no free entry exists.
     if (slot == -1) {
         USLOSS_Console("spork(): ERROR - No free process slots\n");
         return -1;
     }
 
     // Prepare the new process's slot.
     Process *new_proc = &process_table[slot];
 
     // Generate a new PID. (This example tries to skip certain PIDs if needed.)
     int pid;
     do {
         pid = next_pid++;
     } while (pid > 2 && (pid % 50 == 1 || pid % 50 == 2));
 
     new_proc->pid = pid;
     new_proc->priority = priority;
     strncpy(new_proc->name, name, MAXNAME);
     new_proc->status = 0;  // 0 = Runnable
     new_proc->stack = malloc(stackSize);
     if (!new_proc->stack) {
         USLOSS_Console("ERROR: malloc failed for process %s\n", name);
         USLOSS_Halt(1);
     }
     new_proc->startFunc = startFunc;
     new_proc->arg = arg;
 
     // Set up USLOSS context for the new process.
     USLOSS_ContextInit(&new_proc->context, new_proc->stack, stackSize, NULL, processWrapper);
 
     // Link child to the running process (its parent) if there is one.
     if (running_process) {
         new_proc->parent = running_process;
 
         // Decide how to insert new_proc into the parent's child list:
         // - If new_proc's priority is higher than the parent's, we append at the end.
         // - Otherwise, we prepend it at the beginning.
         if (new_proc->priority > running_process->priority) {
             new_proc->sibling = NULL;
             if (running_process->children == NULL) {
                 running_process->children = new_proc;
             } else {
                 Process *temp = running_process->children;
                 while (temp->sibling != NULL) {
                     temp = temp->sibling;
                 }
                 temp->sibling = new_proc;
             }
         } else {
             new_proc->sibling = running_process->children;
             running_process->children = new_proc;
         }
     } else {
         // If there is no running process, this new process is top-level.
         new_proc->parent = NULL;
     }
 
     // Insert the new process into the appropriate ready queue (based on priority).
     int idx = new_proc->priority - 1;
     if (ready_queues[idx] == NULL) {
         ready_queues[idx] = new_proc;
         new_proc->next = NULL;
     } else {
         Process *temp = ready_queues[idx];
         while (temp->next != NULL) {
             temp = temp->next;
         }
         temp->next = new_proc;
         new_proc->next = NULL;
     }
 
     // If the new process has the same or higher priority than the current one,
     // we may need to switch to it immediately.
     if (running_process && new_proc->priority <= running_process->priority) {
         dispatcher();
     }
 
     return new_proc->pid;
 }
 
 /*
  * processWrapper
  * --------------
  * A small wrapper function that is called first in a new process's context.
  * It calls the process's startFunc with the given argument.
  * If the process returns, we automatically quit with the return code.
  * This ensures that if a process's function exits normally, the kernel
  * can clean up properly.
  */
 void processWrapper(void) {
     if (running_process == NULL || running_process->startFunc == NULL) {
         USLOSS_Halt(1);
     }
 
     // Call the actual start function.
     int rc = running_process->startFunc(running_process->arg);
 
     // If the process returns, check if it still has children. That's an error in this design.
     if (running_process->children != NULL) {
         USLOSS_Console("ERROR: Process pid %d called quit() while it still had children.\n",
                        running_process->pid);
         USLOSS_Halt(1);
     }
 
     // Terminate using the return code from startFunc.
     quit(rc);
 }
 
 /*
  * join
  * ----
  * Allows a parent process to wait for one of its children to quit.
  * If a child is already terminated, return immediately with the child's PID and exit status.
  * Otherwise, block until a child finishes.
  *
  * Return values:
  *   -2 if the parent has no children at all
  *   -3 if status is NULL (error)
  *   Otherwise, returns the child's PID on success.
  */
 int join(int *status) {
     if (status == NULL) {
         USLOSS_Console("ERROR: join() called with NULL status pointer.\n");
         return -3;
     }
 
     Process *parent = running_process;
 
     // If the parent has no children, return -2 immediately.
     if (parent->children == NULL) {
         return -2;
     }
 
     // Loop until we find a terminated child or block if none are terminated yet.
     while (1) {
         Process *prev = NULL;
         Process *child = parent->children;
 
         // Traverse the linked list of children to check if any have status = -1 (terminated).
         while (child != NULL) {
             if (child->status == -1) {  // Found a terminated child
                 *status = child->exit_status;  // Retrieve exit code
                 int childPid = child->pid;
 
                 // Remove the child from the parent's child list.
                 if (prev == NULL) {
                     parent->children = child->sibling;
                 } else {
                     prev->sibling = child->sibling;
                 }
 
                 // Free the child's resources.
                 free(child->stack);
                 child->stack = NULL;
                 child->pid = -1;
                 child->sibling = NULL;
 
                 return childPid;
             }
             prev = child;
             child = child->sibling;
         }
 
         // If no child is terminated yet, we block the parent and let the system run other processes.
         parent->status = 1; // 1 = Blocked
         // You might remove the parent from ready queue here if needed.
         dispatcher();
     }
 }
 
 /*
  * quit
  * ----
  * Terminates the currently running process, setting its status to -1 (terminated),
  * and unblocking the parent and any processes that zapped this process.
  * This function must be called only in kernel mode, and only if the process has no children.
  */
 void quit(int status) {
     // Must be in kernel mode to call quit.
     if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
         USLOSS_Console("ERROR: Someone attempted to call quit while in user mode!\n");
         USLOSS_Halt(1);
     }
 
     // Quitting with active children is an error.
     if (running_process->children != NULL) {
         USLOSS_Console("ERROR: Process %d quitting with active children!\n", running_process->pid);
         USLOSS_Halt(1);
     }
 
     // Mark this process as terminated, store the exit status.
     running_process->status = -1;
     running_process->exit_status = status;
 
     // If the parent is blocked waiting on join(), make it runnable again.
     if (running_process->parent && running_process->parent->status == 1) {
         running_process->parent->status = 0; // 0 = Runnable
         // Depending on your design, you might reinsert the parent in the ready queue here.
         // Example:
         // int prio = running_process->parent->priority - 1;
         // running_process->parent->next = ready_queues[prio];
         // ready_queues[prio] = running_process->parent;
     }
 
     // Unblock any processes that called zap() on this process.
     for (int i = 0; i < MAXPROC; i++) {
         if (process_table[i].status == 1 && process_table[i].zap_target == running_process) {
             process_table[i].status = 0; // make them runnable again
             process_table[i].zap_target = NULL;
 
             // Insert them into ready queue
             int prio = process_table[i].priority - 1;
             process_table[i].next = ready_queues[prio];
             ready_queues[prio] = &process_table[i];
         }
     }
 
     // Remove the current process from the ready queue and dispatch a new process.
     remove_from_ready_queue(running_process);
     dispatcher();
 }
 
 /*
  * blockMe
  * -------
  * Blocks the currently running process by setting its status to 1 (Blocked),
  * then calls dispatcher to allow another process to run.
  */
 void blockMe(void) {
     running_process->status = 1;
     dispatcher();
 }
 
 /*
  * unblockProc
  * -----------
  * Given a PID, this function finds that blocked process in the process table,
  * sets its status to 0 (Runnable), reinserts it into the ready queue,
  * and calls dispatcher.
  *
  * Returns 0 on success, or -2 if the PID doesn't correspond to a blocked process.
  */
 int unblockProc(int pid) {
     for (int i = 0; i < MAXPROC; i++) {
          if (process_table[i].pid == pid && process_table[i].status == 1) {
              process_table[i].status = 0; // 0 = Runnable
 
              // Add the unblocked process to the end of its priority queue.
              if (!ready_queues[process_table[i].priority - 1]) {
                  ready_queues[process_table[i].priority - 1] = &process_table[i];
              } else {
                  Process *temp = ready_queues[process_table[i].priority - 1];
                  while (temp->next) {
                      temp = temp->next;
                  }
                  temp->next = &process_table[i];
              }
              dispatcher();
              return 0;
          }
     }
     return -2;
 }
 
 /*
  * zap
  * ---
  * Blocks the current process until the target process (PID = pid) quits.
  * If the target does not exist, or is this same process, it's an error.
  * If the target is already terminated, return immediately.
  */
 void zap(int pid) {
     // A process cannot zap itself.
     if (pid == running_process->pid) {
         USLOSS_Console("ERROR: Process cannot zap itself.\n");
         USLOSS_Halt(1);
     }
 
     // Find the target in the process table.
     Process *target = NULL;
     for (int i = 0; i < MAXPROC; i++) {
         if (process_table[i].pid == pid) {
             target = &process_table[i];
             break;
         }
     }
     if (!target) {
         USLOSS_Console("ERROR: Attempted to zap a non-existent process (PID %d).\n", pid);
         USLOSS_Halt(1);
     }
 
     // If target is already terminated, we can just return.
     if (target->status == -1) {
         return;
     }
 
     // Otherwise, block this process until target terminates.
     running_process->status = 1;  // Block
     running_process->zap_target = target;
     dispatcher();
 
     // After dispatcher returns, it means the target has terminated
     // and we are unblocked. There's no need to do anything else here.
 }
 
 /*
  * dispatcher
  * ----------
  * Chooses the highest-priority runnable process from the ready queues and
  * switches to it. If no ready process is found, we halt.
  */
 void dispatcher(void) {
     Process *next = NULL;
 
     // The ready_queues are in ascending order of priority index:
     // index 0 -> priority 1
     // index 5 -> priority 6
     // We want the highest priority (lowest index) that has a runnable process.
     for (int i = 0; i < 6; i++) {
         if (ready_queues[i] != NULL && ready_queues[i]->status != 1) {
             next = ready_queues[i];
             break;
         }
     }
 
     // If we didn't find a runnable process, there's nothing left to run—halt the system.
     if (next == NULL) {
         USLOSS_Console("ERROR: No ready processes! Halting.\n");
         USLOSS_Halt(0);
     }
 
     contextSwitch(next);
 }
 
 /*
  * contextSwitch
  * -------------
  * Performs the actual context switch from the old running process to the new one.
  * If there's no old process (old_process == NULL), we simply call processWrapper() to start the new process.
  */
 void contextSwitch(Process *next) {
     // If the next process is the same as the current one, there's nothing to switch.
     if (running_process == next) {
         USLOSS_Halt(0);
         return;
     }
 
     Process *old_process = running_process;
     running_process = next; // Switch the global pointer.
 
     // If we have an old process, we do a real context switch.
     // Otherwise, just call processWrapper() to begin the new process's function.
     if (old_process != NULL) {
         USLOSS_ContextSwitch(&old_process->context, &running_process->context);
     } else {
         processWrapper();
     }
 }
 
 /*
  * getpid
  * ------
  * Returns the PID of the currently running process.
  */
 int getpid(void) {
     return running_process->pid;
 }
 
 /*
  * dumpProcesses
  * -------------
  * Prints out a list of all processes in the process_table,
  * including their PID, parent's PID, name, priority, and state.
  */
 void dumpProcesses(void) {
     USLOSS_Console("PID  PPID  NAME              PRIORITY  STATE\n");
     for (int i = 0; i < MAXPROC; i++) {
         if (process_table[i].pid != -1) {
             int pid = process_table[i].pid;
             int ppid = (process_table[i].parent != NULL) ? process_table[i].parent->pid : 0;
             char state[64];
 
             if (running_process && process_table[i].pid == running_process->pid) {
                 strcpy(state, "Running");
             } else if (process_table[i].status == -1) {
                 snprintf(state, sizeof(state), "Terminated(%d)", process_table[i].exit_status);
             } else if (process_table[i].status == 2) {
                 // If your code sets 2 = Zapped, you could show it here.
                 snprintf(state, sizeof(state), "Zapped (waiting on PID %d)",
                          process_table[i].zap_target ? process_table[i].zap_target->pid : -1);
             } else if (process_table[i].status == 1) {
                 strcpy(state, "Blocked(waiting)");
             } else {
                 strcpy(state, "Runnable");
             }
 
             USLOSS_Console("%3d %5d  %-17s %-9d %-15s\n",
                            pid, ppid, process_table[i].name,
                            process_table[i].priority, state);
         }
     }
 }
 
 /*
  * remove_from_ready_queue
  * -----------------------
  * Given a process, removes it from the ready queue that matches its priority.
  * This function sets the process's 'next' pointer to NULL, effectively
  * detaching it from the linked list of ready processes.
  */
 void remove_from_ready_queue(Process *p) {
     int prio = p->priority;
     Process **cur = &ready_queues[prio - 1];
 
     // Walk the linked list. If we find 'p', we unlink it from the chain.
     while (*cur != NULL) {
         if (*cur == p) {
             *cur = (*cur)->next;
             p->next = NULL;
             return;
         }
         cur = &((*cur)->next);
     }
 }
 