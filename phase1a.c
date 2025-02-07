#include <stdio.h>
#include <stdlib.h>

void phase1_init(){
    // called once
    // initialize data structures including process table entry
}

void wrapper(){
    // wrapper for a function... gets info about a process
}

void spork(char *name, int (*startFunc)(void*), void *arg, int stackSize,
int priority){
    // creates a new process, which is a child of the current process
}

void join(int *status){
    // blocks the current process until one of its children has terminated; 
    // it then delivers the “status” of the child (the parameter 
    // that the child passed to quit()) back to the parent
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
}

void dumpProcesses(){
    // prints human-readable debug data about the process table
}

void blockMe(){
    // SKIP
}

void unblockProc(){
    // SKIP
}
