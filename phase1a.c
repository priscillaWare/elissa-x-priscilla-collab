/* 
 * File: phase1a.c
 * Purpose: Implements the core process management routines for Phase 1A of USLOSS.
 *          These routines include process creation (spork), process termination (quit_phase_1a),
 *          context switching (TEMP_switchTo), process joining (join), and debugging support (dumpProcesses).
 *
 *          The file uses a fixed-size process table with MAXPROC slots. Each new process is assigned
 *          a unique PID and mapped to a slot using: slot = pid % MAXPROC. When a process terminates,
 *          its slot is freed (pid set to -1) so that it can be reused by a later process.
 *
 * Authors: Elissa Matlock and Priscilla Ware
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phase1a.h"
#include "usloss.h"

// Fixed-size process table. Each process is stored in one slot.
static Process process_table[MAXPROC];
// Pointer to the currently running process.
Process *running_process = NULL;
// Next PID to assign 
int next_pid = 0;
// Counter to record the termination order (used by join() to pick the most recent terminated child).
static int terminationCounter = 0;

extern int testcase_main(void *arg);

// init_run: the starting function for the special init process.
// It sets up interrupts, starts the service processes, creates testcase_main, and then switches to it.
int init_run(void *arg) {
    // Enable interrupts by setting the USLOSS_PSR_CURRENT_INT bit.
    int current = USLOSS_PsrGet();
    USLOSS_PsrSet(current | USLOSS_PSR_CURRENT_INT);

    // External declarations for service processes (Phase 2 through 5).
    extern void phase2_start_service_processes(void);
    extern void phase3_start_service_processes(void);
    extern void phase4_start_service_processes(void);
    extern void phase5_start_service_processes(void);
    
    // Call the service process functions so their messages are printed.
    phase2_start_service_processes();
    phase3_start_service_processes();
    phase4_start_service_processes();
    phase5_start_service_processes();
    
    // Create the testcase_main process.
    int testcase_pid = spork("testcase_main", testcase_main, NULL, USLOSS_MIN_STACK, 3);
    if (testcase_pid < 0) {
        USLOSS_Console("ERROR: Failed to start testcase_main.\n");
        USLOSS_Halt(1);
    }

    // Print a message indicating that we are switching to testcase_main.
    USLOSS_Console("Phase 1A TEMPORARY HACK: init() manually switching to testcase_main() after using spork() to create it.\n");

    // Switch to the testcase_main process.
    TEMP_switchTo(testcase_pid);

    // After testcase_main returns, init loops calling join() to reap any terminated children.
    while (1) {
        int status;
        int childPid = join(&status);
        if (childPid == -2) { // -2 indicates no children have terminated.
            USLOSS_Console("Phase 1A TEMPORARY HACK: testcase_main() returned, simulation will now halt.\n");
            USLOSS_Halt(0);
        }
    }
    USLOSS_Halt(0);  // Ensure we never return!
    return 0;
}

/*
 * phase1_init: Called by the startup code to initialize the kernel.
 * Sets up the process table and creates the special init process.
 */
void phase1_init(void) {
    int i;

    // Initialize the entire process table.
    for (i = 0; i < MAXPROC; i++) {
        process_table[i].pid = -1;           // Mark slot as free.
        process_table[i].status = 0;           // Status 0 means runnable.
        process_table[i].children = NULL;      // No children yet.
        process_table[i].next = NULL;          // Not linked to any sibling.
        process_table[i].parent = NULL;        // No parent assigned yet.
        process_table[i].exit_status = 0;      // No exit status.
        process_table[i].stack = NULL;         // No stack allocated.
    }

    /* Create the special init process with PID 1 */
    process_table[1].pid = 1;                     // Hard-code PID 1 for init.
    process_table[1].priority = 6;                // Set priority for init.
    strncpy(process_table[1].name, "init", MAXNAME); // Name init.
    process_table[1].status = 0;                  // Mark init as runnable.
    process_table[1].parent = NULL;               // init has no parent.
    process_table[1].startFunc = init_run;        // Set init's start function.

    // Allocate a stack for init.
    char *init_stack = malloc(USLOSS_MIN_STACK);
    if (init_stack == NULL) {
        USLOSS_Console("ERROR: malloc failed for init process\n");
        USLOSS_Halt(1);
    }
    process_table[1].stack = init_stack;

    // Initialize the context for the init process; processWrapper will call init_run.
    USLOSS_ContextInit(&process_table[1].context, init_stack, USLOSS_MIN_STACK, NULL, processWrapper);

    // Commented out assignment because running_process is set later by the scheduler.
    // running_process = &process_table[1];
    next_pid += 2;  /* Next process will get PID 2 (since PID 1 is init) */
}


// quit_phase_1a: Terminates the current process and switches context to the specified process.
// It checks that kernel mode is enabled and that the process has no active children.
void quit_phase_1a(int status, int switchToPid) {
    // Ensure we are in kernel mode.
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call quit_phase_1a while in user mode!\n");
        USLOSS_Halt(1);
    }
    // If the process still has active children, it is an error.
    if (running_process->children != NULL) {
        USLOSS_Console("ERROR: Process pid %d called quit() while it still had children.\n", running_process->pid);
        USLOSS_Halt(1);
    }

    // Mark the process as terminated and record its exit status.
    running_process->status = -1;
    running_process->exit_status = status;  // Use the provided status (e.g., 2)
    running_process->termOrder = terminationCounter++; // Record termination order

    // Find the process in the table with PID equal to switchToPid.
    Process *new_process = NULL;
    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].pid == switchToPid) {
            new_process = &process_table[i];
            break;
        }
    }
    // If not found, that's an error.
    if (new_process == NULL || new_process->pid == -1) {
        USLOSS_Console("ERROR: quit_phase_1a() failed, switchToPid %d not found\n", switchToPid);
        USLOSS_Halt(1);
    }
    
    // Save the current process, then set running_process to the new process.
    Process *old_process = running_process;
    running_process = new_process;

    // Perform a context switch: this should never return.
    USLOSS_ContextSwitch(&old_process->context, &new_process->context);

    // If context switch returns unexpectedly, halt with an error.
    USLOSS_Console("ERROR: Context switch returned unexpectedly in quit_phase_1a! (old PID %d, new PID %d)\n", 
                   old_process->pid, new_process->pid);
    USLOSS_Halt(1);
}

// processWrapper: Called as the initial context for every new process.
// It invokes the process's start function with its argument and, when that returns,
// calls quit_phase_1a with the return value.
void processWrapper(void) {
    if (running_process == NULL || running_process->startFunc == NULL) {
        USLOSS_Console("ERROR: processWrapper() called with NULL function!\n");
        USLOSS_Halt(1);
    }
    
    // Call the process's start function, passing its stored argument.
    int rc = running_process->startFunc(running_process->arg);
    
    // When the start function returns, terminate the process.
    // The parent's PID is used as the switch-to target (or 0 if no parent).
    quit_phase_1a(rc, (running_process->parent ? running_process->parent->pid : 0));
    
    while (1) { }  // Should never reach here.
}


// spork: Creates a new process with the given parameters.
// It checks for kernel mode, validates stack size and priority, finds a free slot
// using modulo arithmetic, initializes the process table entry, allocates a stack,
// and initializes the process's context.
int spork(char *name, int (*startFunc)(void*), void *arg, int stackSize, int priority) {
    // Ensure we are in kernel mode.
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
        USLOSS_Halt(1);
    }

    // Validate the stack size; if too small, return -2.
    if (stackSize < USLOSS_MIN_STACK) {
        return -2;
    }
    // Validate priority; assuming valid priorities are 1 through 7.
    if (priority < 1 || priority > 7) {
        return -1;
    }
    
    Process *parent = running_process;
    int pid, slot;
    int attempts = 0;
    // Loop to find a free slot using modulo arithmetic.
    do {
        pid = next_pid++;
        slot = pid % MAXPROC;
        attempts++;
        if (attempts > MAXPROC) {  // Prevent infinite loop if no slot available.
            return -1;
        }
    } while (process_table[slot].pid != -1);  // Slot must be free (pid == -1)

    // Initialize the process table entry with the new process's information.
    process_table[slot].pid = pid;
    process_table[slot].priority = priority;
    strncpy(process_table[slot].name, name, MAXNAME);
    process_table[slot].status = 0;              // Process is runnable.
    process_table[slot].children = NULL;         // No children yet.
    process_table[slot].next = NULL;             // Not linked yet.
    process_table[slot].parent = parent;         // Current process is the parent.
    process_table[slot].exit_status = 0;
    process_table[slot].termOrder = 0;
    
    // Allocate a stack for the new process using the provided stackSize.
    char *stack = malloc(stackSize);
    if (stack == NULL) {
        USLOSS_Console("ERROR: malloc failed for process %s\n", name);
        USLOSS_Halt(1);
    }
    process_table[slot].stack = stack;
    process_table[slot].startFunc = startFunc;
    process_table[slot].arg = arg;
    
    // Initialize the process context so that processWrapper() will run.
    USLOSS_ContextInit(&process_table[slot].context, stack, stackSize, NULL, processWrapper);
    
    // Link the new process into the parent's children list.
    if (parent->children == NULL) {
        parent->children = &process_table[slot];
    } else {
        Process *temp = parent->children;
        while (temp->next != NULL)
            temp = temp->next;
        temp->next = &process_table[slot];
    }
    
    // Return the new process's PID.
    return pid;
}


// join: Waits for a terminated child process.
// It searches the parent's children list for a terminated child (status == -1)
// and chooses the one with the highest termOrder (the most recently terminated).
// It then removes that child from the list, frees its slot (by setting pid to -1),
// and returns its PID. If no terminated child is found, returns -2.
int join(int *status) {
    if (status == NULL) {
        USLOSS_Console("ERROR: join() called with NULL status pointer.\n");
        return -3; // Invalid status pointer.
    }
    
    Process *parent = running_process;
    Process *child = parent->children;
    Process *target = NULL;
    int highestOrder = -1;  // Start with -1 so any termination order is higher.
    
    // Iterate over the parent's children list.
    while (child != NULL) {
        // If the child is terminated (status == -1) and has a higher termOrder,
        // select it.
        if (child->status == -1 && child->termOrder > highestOrder) {
            highestOrder = child->termOrder;
            target = child;
        }
        child = child->next;
    }
    
    if (target != NULL) {
        *status = target->exit_status;
        int childPid = target->pid;
        // Remove the target from the parent's children list.
        if (parent->children == target) {
            parent->children = target->next;
        } else {
            Process *prev = parent->children;
            while (prev != NULL && prev->next != target)
                prev = prev->next;
            if (prev != NULL)
                prev->next = target->next;
        }
        // Mark the target's slot as free by setting pid to -1 and status to 0.
        target->pid = -1;
        target->status = 0;
        return childPid;
    }
    
    return -2;  // No terminated child found.
}


// TEMP_switchTo: Temporarily switches execution to the process with the given PID.
// If no running process is present (during bootstrapping), it calls processWrapper() directly.
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
        // Directly start the process since there's no current process to switch from.
        processWrapper();
        USLOSS_Halt(0);
    }

    // Otherwise, find the new process in the process table.
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

    // Save the current process and switch running_process to the new one.
    Process *old_process = running_process;
    running_process = new_process;
    // Perform the context switch.
    USLOSS_ContextSwitch(&old_process->context, &new_process->context);
}


// dumpProcesses: Prints out the contents of the process table.
// Each slot is printed in order (0 to MAXPROC-1), showing PID, PPID, process name, priority, and state.
void dumpProcesses(void) {
    USLOSS_Console(" PID  PPID  NAME              PRIORITY  STATE\n");
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

// getpid: Returns the PID of the currently running process.
int getpid() {
    return running_process ? running_process->pid : -1;
}

void blockMe() {
    // Function not implemented in Phase 1A.
}

int unblockProc(int pid) {
    // Function not implemented in Phase 1A.
    return 0;
}

void zap(int pid) {
    // Function not implemented in Phase 1A.
}
