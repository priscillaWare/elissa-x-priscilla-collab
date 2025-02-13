#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phase1.h"
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

    for (int i = 0; i < MAXPROC; i++) {
        process_table[i].pid = -1;
        process_table[i].status = 0;
        process_table[i].children = NULL;
        process_table[i].next = NULL;
        process_table[i].exit_status = 0;  // Added missing exit status initialization
    }

    process_table[1].pid = 1;
    next_pid++;
    process_table[1].priority = 6;
    strncpy(process_table[1].name, "init", MAXNAME);
    process_table[1].status = 0;
    running_process = &process_table[1];

    int testcase_pid = spork("testcase_main", testcase_main, NULL, USLOSS_MIN_STACK, 3);
    if (testcase_pid < 0) {
        USLOSS_Console("PHASE1_INIT: Failed to start testcase_main.\n");
        USLOSS_Halt(1);
    } 

    TEMP_switchTo(testcase_pid);

    USLOSS_Halt(0);
}

void quit_phase_1a(int status, int switchToPid) {
    if (running_process == NULL) {
        USLOSS_Console("ERROR: quit_phase_1a() called with no running process!\n");
        USLOSS_Halt(1);
    }

    USLOSS_Console("quit_phase_1a(): Process %d quitting, switching to %d\n",
                   running_process->pid, switchToPid);

    running_process->status = -1;   // Mark process as terminated
    running_process->exit_status = status;  // Store the exit status

    // Find process to switch to
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

    // Perform the context switch
    USLOSS_ContextSwitch(&running_process->context, &new_process->context);
}

void processWrapper() {
    if (running_process == NULL || running_process->startFunc == NULL) {
        USLOSS_Console("ERROR: processWrapper() called with NULL function!\n");
        USLOSS_Halt(1);
    }
    running_process->startFunc(running_process->arg);
    quit_phase_1a(0, 0);
}

int spork(char *name, int (*startFunc)(void*), void *arg, int stackSize, int priority) {
    int slot = next_pid % MAXPROC;
    if (process_table[slot].pid != -1) {
        USLOSS_Console("ERROR: No available slots in process table for %s\n", name);
        return -1;
    }

    int pid = next_pid++;
    process_table[slot].pid = pid;
    process_table[slot].priority = priority;
    strncpy(process_table[slot].name, name, MAXNAME);
    process_table[slot].status = 0;
    process_table[slot].children = NULL;
    process_table[slot].next = NULL;  // Initialize the next pointer

    char *stack = malloc(USLOSS_MIN_STACK);
    if (stack == NULL) {
        USLOSS_Console("ERROR: malloc failed for %s\n", name);
        USLOSS_Halt(1);
    }
    process_table[slot].stack = stack;

    process_table[slot].startFunc = startFunc;
    process_table[slot].arg = arg;

    USLOSS_ContextInit(&process_table[slot].context, stack, USLOSS_MIN_STACK, NULL, processWrapper);

    // Link the new process into the parent's children list.
    Process *parent = running_process;
    if (parent->children == NULL) {
        parent->children = &process_table[slot];
    } else {
        Process *temp = parent->children;
        while (temp->next != NULL) {
            temp = temp->next;
        }
        temp->next = &process_table[slot];
    }

    USLOSS_Console("spork() successful, PID: %d\n", pid);
    return pid;

}

int join(int *status) {
    if (status == NULL) {
        return -3;  // Invalid status pointer
    }

    Process *child = running_process->children;
    Process *prev = NULL;

    while (child != NULL) {
        if (child->status == -1) {  // Found a terminated child
            *status = child->exit_status;  // Get child's exit status
            int childPid = child->pid;

            // Remove child from the list
            if (prev == NULL) {
                running_process->children = child->next;
            } else {
                prev->next = child->next;
            }

            // Mark process as invalid
            child->pid = -1;
            child->status = 0;
            return childPid;
        }
        prev = child;
        child = child->next;
    }
    return -2;  // No terminated children found
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
    printf("\n---- Process Table ----\n");
    printf("PID  | Priority | Status | Children\n");
    printf("-----------------------------------\n");

    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].pid != -1) {
            printf("%-4d | %-8d | %-6d | ", process_table[i].pid, process_table[i].priority, process_table[i].status);

            Process *child = process_table[i].children;
            while (child) {
                printf("%d ", child->pid);
                child = child->next;
            }
            printf("\n");
        }
    }
    printf("---------------------------\n\n");
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
