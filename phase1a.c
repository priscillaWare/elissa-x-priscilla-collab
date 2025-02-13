#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "phase1.h"
#include "usloss.h"

static Process process_table[MAXPROC];

Process *running_process = NULL;
int next_pid = 1;

extern int testcase_main(void *arg);

void phase1_init(){
    // called once
    // initialize data structures including process table entry
    int psr = USLOSS_PsrGet();
    USLOSS_PsrSet(psr & ~USLOSS_PSR_CURRENT_INT);

    for (int i = 0; i < MAXPROC; i++) {
        process_table[i].pid = -1;
        process_table[i].status = 0;
        process_table[i].children = NULL;
        process_table[i].next = NULL;
    }
    process_table[0].pid = 1;
    next_pid++;
    process_table[0].priority = 6;
    strncpy(process_table[0].name, "init", MAXNAME);
    process_table[0].status = 0;

    running_process = &process_table[0];
    
    //start testcase main
    int testcase_pid = spork("testcase_main", testcase_main, NULL, USLOSS_MIN_STACK, 3);
    USLOSS_ContextSwitch(&old_ctx, &new_ctx);     // still need to figure ou how to properly use this function call...
    Process *testcaseProcess = &process_table[testcase_pid % MAXPROC];
    Process *initProcess = &process_table[0];

    if (testcase_pid != 0) {
        printf("PHASE1_INIT: Failed to start testcase_main.\n");
    }
    USLOSS_Halt(0);
    
    //bootstrapping
    while(1) {
        int status;
        int pid = join(&status);

        if (pid == -2) { // No more children left
            printf("Error: All children terminated. Halting system.\n");
            USLOSS_Halt(0);
        }
    }
    USLOSS_PsrSet(psr);
}

void wrapper(){
    // wrapper for a function... gets info about a process
}

int spork(char *name, int (*startFunc)(void*), void *arg, int stackSize,
int priority){

    // adds a new process to the table
    int psr = USLOSS_PsrGet();
    USLOSS_PsrSet(psr & ~USLOSS_PSR_CURRENT_INT);

    if (stackSize < USLOSS_MIN_STACK){
        return -2;
    }

    // puts the new process in the table
    int slot = next_pid % MAXPROC;
    if (process_table[slot].pid != -1 || startFunc == NULL || name == NULL || strlen(name) > MAXNAME){
        USLOSS_PsrSet(psr);
        return -1;
    }
    process_table[slot].pid = next_pid;
    next_pid++;
    process_table[slot].priority = priority;  // not super sure how to assign priority rn
    strncpy(process_table[0].name, name, MAXNAME);
    process_table[0].status = 0;
    process_table[slot].children = NULL;
    process_table[slot].next = running_process->children;
    running_process->children = &process_table[slot];

    // adds the process to the linked list of children processes 
    Process* cur = running_process->children;
    while (cur != NULL){
        cur = cur->next;
    }
    cur = &process_table[slot];

    USLOSS_PsrSet(psr);

    return process_table[slot].pid;
}

int join(int *status){
    // blocks the current process until one of its children has terminated; 
    // it then delivers the “status” of the child (the parameter 
    // that the child passed to quit()) back to the parent
    if (status == NULL){
        return -3;
    }
    Process *child = running_process->children;
    while (child != NULL){
        if (child->status == -1){
            *status = child->status;
            child->pid= -1;
            return child->pid;
        }
        child = child->next;
    }
    return -2;
}

void quit(int status){
    // NEVER RETURNS. terminates the current process with a 'status' value
    // If the parent of the proccess is already waiting in a join(), the
    // parent will be awoken. 
}

void quit_phase_1a(int status, int switchToPid) {
    running_process->status = -1;
    for (int i = 0; i < MAXPROC; i++){
        if (process_table[i].pid == switchToPid){
            USLOSS_ContextSwitch(running_process.context, process_table[i].context);
        }
    }
}

void zap(int pid){
    // SKIP
}

int getpid(){
    return USLOSS_PsrGet();
}

void dumpProcesses(){
    printf("\n---- Process Table ----\n");
    printf("PID  | Parent | Priority | Status      | Children\n");
    printf("-----------------------------------------------\n");

    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].pid != -1) { // Only print active processes
            printf("%-4d | %-6d | %-8d | %-2d | ",
                   process_table[i].pid,
                   process_table[i].parent_pid,
                   process_table[i].priority,
                   process_table[i].status
            // Print child PIDs
            Process *child = process_table[i].children;
            while (child) {
                printf("%d ", child->pid);
                child = child->next_sibling;
            }
            printf("\n");
        }
    }
    printf("---------------------------\n\n");
}

void blockMe(){
    // SKIP
}

int unblockProc(int pid){
    // SKIP
    return 0;
}

int unblockProc(int pid){
    // SKIP
    return 0;
}
