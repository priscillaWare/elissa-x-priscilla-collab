#ifndef PHASE1A_H
#define PHASE1A_H

#define MAXNAME 50
#define MAXPROC 50
#define MAXSYSCALLS 50
#define USLOSS_MIN_STACK (80 * 1024)
#include "usloss.h"

typedef struct process {
    int pid;
    int priority;
    int status;
    char name[MAXNAME];
    struct process* children;
    struct process* next;
    struct process *parent; 
    USLOSS_Context context;
    char *stack;  
    int (*startFunc)(void *);  
    void *arg;  
    int exit_status;
} Process;

extern int testcase_main(void *arg);
void phase1_init();
void quit_phase_1a(int status, int switchToPid);
void wrapper();
int spork(char *name, int (*startFunc)(void*), void *arg, int stackSize, int priority);
void quit(int switchToPid);
int join(int *status);
void zap(int pid);
int getpid();
void TEMP_switchTo(int newpid);
void dumpProcesses();
void blockMe();
int unblockProc(int pid);

#endif