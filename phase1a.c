#include <stdio.h>
#include <stdlib.h>
#include "phase1.h"
#include "usloss.h"

static Process process_table[MAXPROC];

Process* running_procces = NULL;
int next_pid = 1;

extern int testcase_main(void *arg);

void phase1_init(){
    // called once
    // initialize data structures including process table entry
    int psr = USLOSS_PsrGet();
    USLOSS_PsrSet(psr & ~USLOSS_PSR_CURRENT_INT);

    for (int i = 0; i < MAXPROC; i++) {
        process_table[i].pid = -1;
    }
    process_table[0].pid = 1;
    next_pid++;
    process_table[0].priority = 6;
    strncpy(process_table[0].name, "init", MAXNAME);
    process_table[0].status = 0;
    
    //start testcase main
    int testcase_pid = spork("testcase_main", testcase_main, NULL, USLOSS_MIN_STACK, 3);
    USLOSS_ContextSwitch(USLOSS_Context *old, USLOSS_Context *new);     // still need to figure ou how to properly use this function call...
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
    if (process_table[slot].pid == -1 || startFunc == NULL || name == NULL || strlen(name) > MAXNAME){
        return -1;
    }
    process_table[slot].pid = next_pid;
    next_pid++;
    process_table[0].priority = 6;
    strncpy(processTable[0].name, name, MAXNAME);
    process_table[0].status = 0;

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
    return 0;
}

void quit(int status){
    // NEVER RETURNS. terminates the current process with a 'status' value
    // If the parent of the proccess is already waiting in a join(), the
    // parent will be awoken. 
}

void zap(int pid){
    // SKIP
}

int getpid(){
    // returns the PID of the current executing process
    return 0;
}

void dumpProcesses(){
    // prints human-readable debug data about the process table
    printf("Nice Things\n");
}

void blockMe(){
    // SKIP
}

int unblockProc(int pid){
    // SKIP
    return 0;
}
