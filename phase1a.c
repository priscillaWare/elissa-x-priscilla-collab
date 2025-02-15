#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phase1a.h"
#include "usloss.h"

// Phase1a
// Authors: Elissa Matlock, Priscilla Ware
// Description: This program implements the first part of functionality for a kernel


static Process process_table[MAXPROC];
Process *running_process = NULL;
int next_pid = 0;
static int terminationCounter = 0;

extern int testcase_main(void *arg);

// init_run(void) - this function is the main() to the init process. It calls
// the service processes, creates the testcase main process, and enters its loop
// where it join()s until there are no more children.
int init_run(void *arg) {
    extern void phase2_start_service_processes(void);
    extern void phase3_start_service_processes(void);
    extern void phase4_start_service_processes(void);
    extern void phase5_start_service_processes(void);
    
    // Call the service process functions so their messages are printed.
    phase2_start_service_processes();
    phase3_start_service_processes();
    phase4_start_service_processes();
    phase5_start_service_processes();

    // make testcase_main process
    int testcase_pid = spork("testcase_main", testcase_main, NULL, USLOSS_MIN_STACK, 3);
    if (testcase_pid < 0) {
        USLOSS_Console("ERROR: Failed to start testcase_main.\n");
        USLOSS_Halt(1);
    }

    USLOSS_Console("Phase 1A TEMPORARY HACK: init() manually switching to testcase_main() after using spork() to create it.\n");

    TEMP_switchTo(testcase_pid);
    // After bootstrap is complete, init enters a loop to call join repeatedly.
    while (1) {
        int status;
        int childPid = join(&status);
        if (childPid == -2) { // No children have terminated.
            USLOSS_Console("Phase 1A TEMPORARY HACK: testcase_main() returned, simulation will now halt.\n");
            USLOSS_Halt(0);
        }

    }
    USLOSS_Halt(0);  // Ensure we never return!
    return 0;
}

/*
 * phase1_init: Called by the startup code to initialize the kernel.
 * It sets up the process table and creates the special init process.
 */
void phase1_init(void) {
    int i;

    /* Initialize the process table */
    for (i = 0; i < MAXPROC; i++) {
        process_table[i].pid = -1;
        process_table[i].status = 0;
        process_table[i].children = NULL;
        process_table[i].next = NULL;
        process_table[i].parent = NULL;
        process_table[i].exit_status = 0;
        process_table[i].stack = NULL;
    }

    /* Create the special init process with PID 1 */
    process_table[1].pid = 1;
    process_table[1].priority = 6;
    strncpy(process_table[1].name, "init", MAXNAME);
    process_table[1].status = 0;  /* 0 means runnable */
    process_table[1].parent = NULL;
    process_table[1].startFunc = init_run;

    // Allocate stack and initialize context for init process (PID 1)
    char *init_stack = malloc(USLOSS_MIN_STACK);
    if (init_stack == NULL) {
        USLOSS_Console("ERROR: malloc failed for init process\n");
        USLOSS_Halt(1);
    }
    process_table[1].stack = init_stack;

    // Ensure processWrapper() is called so init_run() actually executes!
    USLOSS_ContextInit(&process_table[1].context, init_stack, USLOSS_MIN_STACK, NULL, processWrapper);

    // running_process = &process_table[1];
    next_pid+=2;  /* Next process will get PID 2 */
}


// quit_phase_1a(int status, int switchToPid) - this function terminates the running process
// and performs a context switch into the process indicated by switchToPid.
void quit_phase_1a(int status, int switchToPid) {

    // Mark the running process as terminated and save its exit status.
    running_process->status = -1;
    running_process->exit_status = status;
    // Record the termination order.
    running_process->termOrder = terminationCounter++;

    // Find the process to switch to by its PID.
    Process *new_process = NULL;
    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].pid == switchToPid) {
            new_process = &process_table[i];
            break;
        }
    }
    if (new_process == NULL || new_process->pid == -1) {
        USLOSS_Console("ERROR: quit_phase_1a() failed, switchToPid %d not found\n", switchToPid);
        USLOSS_Halt(1);
    }
    
    // Update running_process to point to the new process before switching.
    Process *old_process = running_process;
    running_process = new_process;

    USLOSS_ContextSwitch(&old_process->context, &new_process->context);

    // Should never return.
    USLOSS_Console("ERROR: Context switch returned unexpectedly in quit_phase_1a! (old PID %d, new PID %d)\n", old_process->pid, new_process->pid);
    USLOSS_Halt(1);
}

// processWrapper() executes the function of a process and terminates it when
// it is no longer running
void processWrapper(void) {
    if (running_process == NULL || running_process->startFunc == NULL) {
        USLOSS_Console("ERROR: processWrapper() called with NULL function!\n");
        USLOSS_Halt(1);
    }
    
    // Pass the argument stored in running_process instead of NULL.
    int rc = running_process->startFunc(running_process->arg);
    
    quit_phase_1a(rc, (running_process->parent ? running_process->parent->pid : 0));
    
    while (1) { }  // Should never return
}

// spork(char *name, int (*startFunc)(void*), void *arg, int stackSize, int priority) - creates
// a child of the currently runing process. returns the process id of the child.
int spork(char *name, int (*startFunc)(void*), void *arg, int stackSize, int priority) {
    // Save the parent pointer immediately.
    Process *parent = running_process;

    int pid = next_pid++;
    int slot = next_pid % MAXPROC;
    if (process_table[slot].pid != -1) {
        USLOSS_Console("ERROR: No available slots in process table for %s\n", name);
        return -1;
    }


    process_table[slot].pid = pid;
    process_table[slot].priority = priority;
    strncpy(process_table[slot].name, name, MAXNAME);
    process_table[slot].status = 0;
    process_table[slot].children = NULL;
    process_table[slot].next = NULL;

    // Set the child's parent pointer.
    process_table[slot].parent = parent;

    char *stack = malloc(USLOSS_MIN_STACK);
    if (stack == NULL) {
        USLOSS_Console("ERROR: malloc failed for %s\n", name);
        USLOSS_Halt(1);
    }
    process_table[slot].stack = stack;

    process_table[slot].startFunc = startFunc;
    process_table[slot].arg = arg;

    USLOSS_ContextInit(&process_table[slot].context, stack, USLOSS_MIN_STACK, NULL, processWrapper);

    // Link the child into the parent's children list.
    if (parent->children == NULL) {
        parent->children = &process_table[slot];
    } else {
        Process *temp = parent->children;
        while (temp->next != NULL) {
            temp = temp->next;
        }         
        temp->next = &process_table[slot];
    }
    return pid;
}

// join(int *status) - kills the child of the currently running process and
// frees a slot on the process table. returns the process id of the child if found.
// If there are no more children, the function returns -2
int join(int *status) {
    if (status == NULL) {
        USLOSS_Console("ERROR: join() called with NULL status pointer.\n");
        return -3; // Invalid status pointer.
    }

    Process *parent = running_process;
    Process *child = parent->children;
    Process *target = NULL;
    int highestOrder = -1;  // Initialize to -1 so that any nonnegative termOrder will be higher.

    while (child != NULL) {
        if (child->status == -1) {
            // Choose the terminated child with the highest termOrder.
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
        // Remove target from the parent's children list.
        if (parent->children == target) {
            parent->children = target->next;
        } else {
            Process *prev = parent->children;
            while (prev != NULL && prev->next != target)
                prev = prev->next;
            if (prev != NULL)
                prev->next = target->next;
        }
        // Mark the target as cleaned up.
        target->pid = -1;
        target->status = 0;
        return childPid;
    }
    return -2;  // No terminated children found.
}

// TEMP_switchTo(int newpid) - performs a context switch to the process indicated
// by newpid.
void TEMP_switchTo(int newpid) {
    Process *new_process = NULL;

    // Check if we're bootstrapping (no current running process).
    if (running_process == NULL) {
        for (int i = 0; i < MAXPROC; i++) {
            if (process_table[i].pid == newpid) {
                new_process = &process_table[i];
                break;
            }
        }
        if (new_process == NULL || new_process->pid == -1) {
            USLOSS_Halt(1);
        }
        running_process = new_process;
        // Instead of context switching from a non-existent process,
        // directly start the process by calling processWrapper.
        processWrapper();
        USLOSS_Halt(0);
    }

    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].pid == newpid) {
            new_process = &process_table[i];
            break;
        }
    }
    if (new_process == NULL || new_process->pid == -1) {
        USLOSS_Console("ERROR: TEMP_switchTo(%d) failed, process not found\n", newpid);
        USLOSS_Halt(1);
    }

    Process *old_process = running_process;
    running_process = new_process;
    USLOSS_ContextSwitch(&old_process->context, &new_process->context);

}

// dumpProcesses() - displays information about the process table in a readable format
void dumpProcesses() {
    USLOSS_Console("-**************** Calling dumpProcesses() *******************\n");
    USLOSS_Console("- PID  PPID  NAME              PRIORITY  STATE\n");

    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].pid != -1) {
            int pid = process_table[i].pid;
            int ppid = (process_table[i].parent != NULL) ? process_table[i].parent->pid : 0;
            char state[32];
            
            if (running_process && process_table[i].pid == running_process->pid) {
                strcpy(state, "Running");
            } else if (process_table[i].status == -1) {
                snprintf(state, sizeof(state), "Terminated(%d)", process_table[i].exit_status);
            } else {
                strcpy(state, "Runnable");
            }
            
            USLOSS_Console("- %-4d %-5d %-17s %-9d %-15s\n", pid, ppid, process_table[i].name, process_table[i].priority, state);
        }
    }
}

int getpid() {
    return running_process ? running_process->pid : -1;
}

void blockMe() {
    // SKIP
}

int unblockProc(int pid) {
    // SKIP
    return 0;
}
