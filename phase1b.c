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
 
 static Process process_table[MAXPROC];
 Process *running_process = NULL;
 static int next_pid = 0;
 
 // Queues for scheduling
 static Process *ready_queues[6] = {NULL, NULL, NULL, NULL, NULL, NULL};
 
extern int testcase_main(void *arg);
 
int init_run(void *arg) {

    int current = USLOSS_PsrGet();
    int result = USLOSS_PsrSet(current | USLOSS_PSR_CURRENT_INT);
    if (result != USLOSS_DEV_OK) {
        USLOSS_Halt(1);
    }

    extern void phase2_start_service_processes(void);
    extern void phase3_start_service_processes(void);
    extern void phase4_start_service_processes(void);
    extern void phase5_start_service_processes(void);
    
    // Call the service process functions so their messages are printed.
    phase2_start_service_processes();
    phase3_start_service_processes();
    phase4_start_service_processes();
    phase5_start_service_processes();

    int testcase_pid = spork("testcase_main", testcase_main, NULL, USLOSS_MIN_STACK, 3);
    if (testcase_pid < 0) {
        USLOSS_Halt(1);
    }


    dispatcher();  // Ensures the first process actually starts

    while (1) {
        int status;
        int childPid = join(&status);
        if (childPid == -2) {
            USLOSS_Console("init_run(): No more children. Halting system.\n");
            USLOSS_Halt(0);
        }
    }
    return 0;
}



/*
 * phase1_init: Called by the startup code to initialize the kernel.
 * Sets up the process table and creates the special init process.
 */
void phase1_init(void) {

    for (int i = 0; i < MAXPROC; i++) {
        process_table[i].pid = -1;
        process_table[i].status = 0;
        process_table[i].children = NULL;
        process_table[i].next = NULL;
        process_table[i].parent = NULL;
        process_table[i].exit_status = 0;
        process_table[i].stack = NULL;
    }


    Process *init_proc = &process_table[1];
    init_proc->pid = 1;
    init_proc->priority = 6;
    strncpy(init_proc->name, "init", MAXNAME);
    init_proc->status = 0;  // Runnable
    init_proc->parent = NULL;
    init_proc->startFunc = init_run;

    char *init_stack = malloc(USLOSS_MIN_STACK);
    if (init_stack == NULL) {
        USLOSS_Console("ERROR: malloc failed for init process\n");
        USLOSS_Halt(1);
    }
    init_proc->stack = init_stack;
    
    USLOSS_ContextInit(&init_proc->context, init_stack, USLOSS_MIN_STACK, NULL, processWrapper);
    ready_queues[5] = init_proc;  // Priority 6 goes to ready_queues[5]
    next_pid += 2;
}



int spork(char *name, int (*startFunc)(void*), void *arg, int stackSize, int priority) {
    USLOSS_Console("spork(): Attempting to create process %s\n", name);

    if (priority < 1 || priority > 5) {
        USLOSS_Console("spork(): ERROR - Invalid priority %d\n", priority);
        return -1;
    }
    if (stackSize < USLOSS_MIN_STACK) {
        USLOSS_Console("spork(): ERROR - Stack size too small\n");
        return -2;
    }

    USLOSS_Console("spork(): next_pid before assignment = %d\n", next_pid);

    int slot;
    for (slot = 0; slot < MAXPROC; slot++) {
        if (process_table[slot].pid == -1) break;
    }
    if (slot == MAXPROC) {
        USLOSS_Console("spork(): ERROR - No free process slots\n");
        return -1;
    }

    Process *new_proc = &process_table[slot];
    new_proc->pid = next_pid++;  // 🚨 Increment PID
    new_proc->priority = priority;
    strncpy(new_proc->name, name, MAXNAME);
    new_proc->status = 0;  // Mark as runnable
    new_proc->stack = malloc(stackSize);
    if (!new_proc->stack) {
        USLOSS_Console("spork(): ERROR - Stack allocation failed\n");
        USLOSS_Halt(1);
    }
    new_proc->startFunc = startFunc;
    new_proc->arg = arg;

    USLOSS_ContextInit(&new_proc->context, new_proc->stack, stackSize, NULL, processWrapper);

    USLOSS_Console("spork(): Created process %s with PID %d (next_pid now %d)\n", name, new_proc->pid, next_pid);

    if (running_process) {
        new_proc->parent = running_process;
        new_proc->next = running_process->children;
        running_process->children = new_proc;
    }

    new_proc->next = ready_queues[priority - 1];
    ready_queues[priority - 1] = new_proc;

    USLOSS_Console("spork(): Process %d added to ready queue %d\n", new_proc->pid, priority - 1);
    return new_proc->pid;
}


void processWrapper(void) {
    USLOSS_Console("processWrapper(): Running PID %d\n", running_process->pid);

    if (running_process == NULL || running_process->startFunc == NULL) {
        USLOSS_Console("processWrapper(): ERROR - Null function pointer!\n");
        USLOSS_Halt(1);
    }
    
    int rc = running_process->startFunc(running_process->arg);
    
    USLOSS_Console("processWrapper(): Process PID %d returned with status %d, calling quit()\n",
                    running_process->pid, rc);
    
    quit(rc);
}

int join(int *status) {
    if (status == NULL) {
        USLOSS_Console("ERROR: join() called with NULL status pointer.\n");
        return -3; // Invalid status pointer.
    }

    Process *parent = running_process;
    Process *child = parent->children;
    Process *target = NULL;
    int highestOrder = -1;  // Find the most recently terminated child

    while (child != NULL) {
        if (child->status == -1) {  
            if (child->termOrder > highestOrder) {
                highestOrder = child->termOrder;
                target = child;
            }
        }
        child = child->next;
    }

    if (target != NULL) {
        *status = target->exit_status; 
        int childPid = target->pid;

        USLOSS_Console("join(): Process %d joined with exit status %d\n", childPid, *status);

        // Remove target from parent's children list
        if (parent->children == target) {
            parent->children = target->next;
        } else {
            Process *prev = parent->children;
            while (prev != NULL && prev->next != target)
                prev = prev->next;
            if (prev != NULL)
                prev->next = target->next;
        }

        // Mark the child as cleaned up
        target->pid = -1;
        target->status = 0;
        return childPid;
    }

    USLOSS_Console("join(): No terminated children found for PID %d, returning -2\n", running_process->pid);
    return -2;  // No terminated children found
}


 
void quit(int status) {
    USLOSS_Console("quit(): Process %d is terminating with status %d\n", running_process->pid, status);

    if (running_process->children) {
        USLOSS_Console("ERROR: Process %d called quit() with active children.\n", running_process->pid);
        USLOSS_Halt(1);
    }

    running_process->status = -1;  // 🚨 Mark process as terminated
    running_process->exit_status = status;  // 🚨 Store exit status

    USLOSS_Console("quit(): Process %d exit status set to %d\n", running_process->pid, status);

    // Wake up parent if they are waiting (blocked in join)
    if (running_process->parent) {
        Process *parent = running_process->parent;
        if (parent->status == 2) {  // Parent is waiting
            parent->status = 0;  // Unblock parent
            USLOSS_Console("quit(): Unblocking parent PID %d\n", parent->pid);
            dispatcher();
        }
    }

    dispatcher();  // Switch to another process
}

 
 void blockMe(void) {
     running_process->status = 1;
     dispatcher();
 }
 
 int unblockProc(int pid) {
     for (int i = 0; i < MAXPROC; i++) {
         if (process_table[i].pid == pid && process_table[i].status == 1) {
             process_table[i].status = 0;
             if (!ready_queues[process_table[i].priority - 1]) {
                 ready_queues[process_table[i].priority - 1] = &process_table[i];
             } else {
                 Process *temp = ready_queues[process_table[i].priority - 1];
                 while (temp->next) temp = temp->next;
                 temp->next = &process_table[i];
             }
             dispatcher();
             return 0;
         }
     }
     return -2;
 }
 
 void zap(int pid) {
     for (int i = 0; i < MAXPROC; i++) {
         if (process_table[i].pid == pid) {
             running_process->status = 2;
             dispatcher();
             return;
         }
     }
     USLOSS_Console("ERROR: Attempted to zap invalid PID %d.\n", pid);
     USLOSS_Halt(1);
 }
 
 void dispatcher(void) {

    for (int i = 0; i < 6; i++) {
        if (ready_queues[i]) {
            Process *next = ready_queues[i];

            // 🚨 Ensure we are not switching to the currently running process
            if (running_process == next) {
                return;
            }

            ready_queues[i] = ready_queues[i]->next;
            next->next = NULL;
            
            contextSwitch(next);
            return;
        }
    }

    USLOSS_Console("dispatcher(): No ready processes found. Halting.\n");
    USLOSS_Halt(0);
}



void contextSwitch(Process *next) {
    if (running_process == next) {
        return;
    }

    USLOSS_Console("contextSwitch(): Switching from PID %d to PID %d\n",
                    running_process ? running_process->pid : -1, next->pid);

    Process *old_process = running_process;
    running_process = next;

    if (old_process) {
        USLOSS_Console("contextSwitch(): Performing USLOSS_ContextSwitch...\n");
        USLOSS_ContextSwitch(&old_process->context, &running_process->context);
        USLOSS_Console("contextSwitch(): ERROR - We should never return here!\n");
        USLOSS_Halt(1);
    } else {
        USLOSS_Console("contextSwitch(): No old process, calling processWrapper() for PID %d\n", running_process->pid);
        processWrapper();
    }
}



void dumpProcesses(void) {
    USLOSS_Console("PID  PPID  NAME              PRIORITY  STATE\n");
    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].pid != -1) {
            int pid = process_table[i].pid;
            // If the process has a parent, print its PID; otherwise, 0.
            int ppid = (process_table[i].parent != NULL) ? process_table[i].parent->pid : 0;
            char state[32];
            
            // If this process is currently running, mark as "Running".
            if (running_process && process_table[i].pid == running_process->pid) {
                strcpy(state, "Running");
            }
            // If the process is terminated (status == -1), print "Terminated(exit_status)".
            else if (process_table[i].status == -1) {
                snprintf(state, sizeof(state), "Terminated(%d)", process_table[i].exit_status);
            }
            else {
                strcpy(state, "Runnable");
            }
            
            // Print the process info with right-aligned PID and PPID.
            USLOSS_Console(" %4d %4d %-17s %-9d %-15s\n", pid, ppid, process_table[i].name, process_table[i].priority, state);
        }
    }
}
 