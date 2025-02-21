
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
        process_table[i].status = -1;
        process_table[i].children = NULL;
        process_table[i].sibling = NULL;  // Initialize sibling pointer
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
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
        USLOSS_Halt(1);
    }

    if (priority < 1 || priority > 5) {
        USLOSS_Console("spork(): ERROR - Invalid priority %d\n", priority);
        return -1;
    }
    if (stackSize < USLOSS_MIN_STACK) {
        USLOSS_Console("spork(): ERROR - Stack size too small\n");
        return -2;
    }
    // After assigning a new pid (with any desired skipping):
    int pid;
    do {
        pid = next_pid++;
    } while (pid > 2 && (pid % 50 == 1 || pid % 50 == 2));

    // Map the pid to a slot index:
    int slot = pid % MAXPROC;
    if (process_table[slot].pid != -1) {
        USLOSS_Console("spork(): ERROR - Process slot %d already in use\n", slot);
        return -1;
    }
    Process *new_proc = &process_table[slot];
    new_proc->pid = pid;

    new_proc->priority = priority;
    strncpy(new_proc->name, name, MAXNAME);
    new_proc->status = 0;  // Mark as runnable
    new_proc->stack = malloc(stackSize);
    if (!new_proc->stack) {
        USLOSS_Halt(1);
    }
    new_proc->startFunc = startFunc;
    new_proc->arg = arg;

    USLOSS_ContextInit(&new_proc->context, new_proc->stack, stackSize, NULL, processWrapper);

    if (running_process) {
        new_proc->parent = running_process;
        // Insert new child at the head of the parent's children list.
        new_proc->sibling = running_process->children;
        running_process->children = new_proc;
    }

    new_proc->next = ready_queues[priority - 1];
    ready_queues[priority - 1] = new_proc;

    dispatcher();      // Let the dispatcher run the newly created process.
    return new_proc->pid;
}



void processWrapper(void) {
    if (running_process == NULL || running_process->startFunc == NULL) {
        USLOSS_Halt(1);
    }
    
    int rc = running_process->startFunc(running_process->arg);
    
    quit(rc);
}


int join(int *status) {
    if (status == NULL) {
        USLOSS_Console("ERROR: join() called with NULL status pointer.\n");
        return -3;
    }

    Process *parent = running_process;
    if (parent->children == NULL) {
        return -2;
    }
    
    while (1) {
        Process *prev = NULL;
        Process *child = parent->children;
        while (child != NULL) {
            if (child->status == -1) {  // terminated child found
                *status = child->exit_status;
                int childPid = child->pid;
                
                // Unlink child from parent's children list using sibling pointers.
                if (prev == NULL)
                    parent->children = child->sibling;
                else
                    prev->sibling = child->sibling;
                
                // Free the child's allocated stack memory.
                free(child->stack);
                child->stack = NULL;
                
                // Clean up the child's entry.
                child->pid = -1;
                child->status = -1;
                child->next = NULL;
                child->sibling = NULL;
                
                return childPid;
            }
            prev = child;
            child = child->sibling;
        }
        
        // No terminated child found; block the parent.
        running_process->status = 1;
        remove_from_ready_queue(running_process);
        dispatcher();
        // When resumed, loop and check children again.
    }
}

 
void quit(int status) {

    if (running_process->children) {
        USLOSS_Console("ERROR: Process %d called quit() with active children.\n", running_process->pid);
        USLOSS_Halt(1);
    }

    running_process->status = -1;          // Mark process as terminated
    running_process->exit_status = status;   // Store exit status

    // Wake up parent if it is waiting (blocked in join)
    if (running_process->parent) {
        Process *parent = running_process->parent;
        if (parent->status == 1) {  // Parent is waiting
            unblockProc(parent->pid);  // Properly reinsert the parent into the ready queue
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
        if (ready_queues[i] != NULL && ready_queues[i]->status != -1 && running_process != ready_queues[i]) {
            Process *next = ready_queues[i];

            if (ready_queues[i]->parent != NULL && ready_queues[i]->priority > ready_queues[i]->parent->priority && ready_queues[i]->parent->status != 1) {
                return;
            }

            //ready_queues[i] = ready_queues[i]->next;
            //next->next = NULL;

            contextSwitch(next);
            return;
        }
    }
    USLOSS_Halt(0);
}


int getpid(void) {
    return running_process->pid;
}


void contextSwitch(Process *next) {

    if (running_process == next) {
        return;
    }

    Process *old_process = running_process;
    if (running_process == NULL) {
      old_process = NULL;
    }
    running_process = next;

    if (old_process != NULL) {
        USLOSS_ContextSwitch(&old_process->context, &running_process->context);
        //USLOSS_Console("contextSwitch(): ERROR - We should never return here!\n");
        //USLOSS_Halt(1);
    } else {
        processWrapper();
    }
}


void dumpProcesses(void) {
    USLOSS_Console("PID  PPID  NAME              PRIORITY  STATE\n");
    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].pid != -1) {
            int pid = process_table[i].pid;
            int ppid = (process_table[i].parent != NULL) ? process_table[i].parent->pid : 0;
            char state[32];
            if (running_process && process_table[i].pid == running_process->pid)
                strcpy(state, "Running");
            else if (process_table[i].status == -1)
                snprintf(state, sizeof(state), "Terminated(%d)", process_table[i].exit_status);
            else
                strcpy(state, "Runnable");
            USLOSS_Console(" %4d %4d %-17s %-9d %-15s\n",
                           pid, ppid, process_table[i].name,
                           process_table[i].priority, state);
        }
    }
}



void remove_from_ready_queue(Process *p) {
    int prio = p->priority;
    Process **cur = &ready_queues[prio - 1];
    while (*cur != NULL) {
        if (*cur == p) {
            *cur = (*cur)->next;
            p->next = NULL;
            return;
        }
        cur = &((*cur)->next);
    }
}