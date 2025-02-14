#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phase1a.h"
#include "usloss.h"

static Process process_table[MAXPROC];

Process *running_process = NULL;
int next_pid = 1;

extern int testcase_main(void *arg);

void phase1_init() {
    int psr = USLOSS_PsrGet();
    int result = USLOSS_PsrSet(psr & ~USLOSS_PSR_CURRENT_INT);
    if (result != USLOSS_ERR_OK) {
        USLOSS_Console("Error: USLOSS_PsrSet failed in phase1_init()\n");
        USLOSS_Halt(1);
    }

    // Initialize the process table.
    for (int i = 0; i < MAXPROC; i++) {
        process_table[i].pid = -1;
        process_table[i].status = 0;
        process_table[i].children = NULL;
        process_table[i].next = NULL;
        process_table[i].exit_status = 0;
    }
    
    // Create the special init process with PID 1, priority 6.
    process_table[1].pid = 1;
    process_table[1].priority = 6;
    strncpy(process_table[1].name, "init", MAXNAME);
    process_table[1].status = 0;
    running_process = &process_table[1];
    next_pid++;

    // Spork testcase_main (this creates a child process).
    int testcase_pid = spork("testcase_main", testcase_main, NULL, USLOSS_MIN_STACK, 3);
    if (testcase_pid < 0) {
        USLOSS_Console("PHASE1_INIT: Failed to start testcase_main.\n");
        USLOSS_Halt(1);
    }
    TEMP_switchTo(testcase_pid);
    // After bootstrap is complete, init enters a loop to call join repeatedly.
    while (1) {
        int status;
        int childPid = join(&status);
        if (childPid == -2) { // No children have terminated.
            USLOSS_Halt(0);
        }
    
    }
    USLOSS_PsrSet(psr);
}



void quit_phase_1a(int status, int switchToPid) {
    if (running_process == NULL) {
        USLOSS_Console("ERROR: quit_phase_1a() called with no running process!\n");
        USLOSS_Halt(1);
    }

    USLOSS_Console("quit_phase_1a(): Process %d quitting, switching to %d\n",
                   running_process->pid, switchToPid);

    // Mark the running process as terminated and save its exit status.
    running_process->status = -1;
    running_process->exit_status = status;

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
}



void processWrapper() {
    if (running_process == NULL || running_process->startFunc == NULL) {
        USLOSS_Console("ERROR: processWrapper() called with NULL function!\n");
        USLOSS_Halt(1);
    }
    int rc = running_process->startFunc(running_process->arg);
    
    // If the process is the testcase_main process, halt the simulation.
    if (strcmp(running_process->name, "testcase_main") == 0) {
        //USLOSS_Console("Phase 1A TEMPORARY HACK: testcase_main() returned, simulation will now halt.\n");
        USLOSS_Halt(rc);
    } else {
        // Otherwise, switch to the parent process.
        if (running_process->parent != NULL)
            quit_phase_1a(rc, running_process->parent->pid);
        else {
            USLOSS_Console("ERROR: process with no parent is trying to quit.\n");
            USLOSS_Halt(1);
        }
    }
}

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

int join(int *status) {
    if (status == NULL) {
        return -3; // Invalid status pointer.
    }
    // Iterate over the process table to find a terminated child.
    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].pid != -1 &&
            process_table[i].parent == running_process &&
            process_table[i].status == -1) {
            *status = process_table[i].exit_status;
            int childPid = process_table[i].pid;
            // Clean up the terminated child's entry.
            process_table[i].pid = -1;
            process_table[i].status = 0;
            return childPid;
        }
    }
    return -2;  // No terminated children found.
}



void TEMP_switchTo(int newpid) {
    USLOSS_Console("TEMP_switchTo(): Attempting switch to PID %d\n", newpid);

    if (running_process == NULL) {
        USLOSS_Console("ERROR: No running process!\n");
        USLOSS_Halt(1);
    }

    Process *new_process = NULL;
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

    USLOSS_Console("TEMP_switchTo(): Switching from PID %d to PID %d\n", running_process->pid, new_process->pid);

    USLOSS_Context *old_context = &running_process->context;
    USLOSS_Context *new_context = &new_process->context;

    running_process = new_process;
    USLOSS_ContextSwitch(old_context, new_context);
}

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
