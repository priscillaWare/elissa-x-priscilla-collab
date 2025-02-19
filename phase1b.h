/* 
 * File: phase1a.h
 * Purpose: Header file for the implementation of phase1a.c
 *
 * Authors: Elissa Matlock and Priscilla Ware
 */
#ifndef PHASE1A_H
#define PHASE1A_H

// Maximum length for a process name.
#define MAXNAME 50
// Maximum number of processes in the fixed-size process table.
#define MAXPROC 50
// Maximum number of system calls supported.
#define MAXSYSCALLS 50
// Minimum stack size for a process.
#define USLOSS_MIN_STACK (80 * 1024)

#include "usloss.h"

// Process structure definition.
// This structure holds all the necessary fields for a process.
typedef struct process {
    int pid;                // Unique process identifier.
    int priority;           // Process priority (lower numbers indicate higher priority).
    int status;             // Process status: 0 means runnable, -1 means terminated.
    char name[MAXNAME];     // Process name.
    struct process* children; // Pointer to the head of the linked list of child processes.
    struct process* next;   // Pointer to the next process in a linked list (e.g., in parent's children list).
    struct process *parent; // Pointer to the parent process.
    USLOSS_Context context; // Process context (for context switching).
    char *stack;            // Pointer to the process's stack.
    int (*startFunc)(void *); // Function pointer to the process's starting function.
    void *arg;              // Argument to be passed to the start function.
    int exit_status;        // Exit status of the process when it terminates.
    int termOrder;          // Termination order (used to determine join() order).
} Process;

// Prototype for the main test case function.
extern int testcase_main(void *arg);

// Function prototypes for Phase 1A functions:

// phase1_init: Initializes the process table and creates the special 'init' process.
void phase1_init(void);

// quit_phase_1a: Terminates the current process (after checking for active children) and switches context.
void quit_phase_1a(int status, int switchToPid);

// processWrapper: A wrapper that calls the process's start function and then calls quit_phase_1a.
void processWrapper(void);

// spork: Creates a new process with the given name, start function, argument, stack size, and priority.
// Returns the new process's PID on success, or a negative value on error.
int spork(char *name, int (*startFunc)(void*), void *arg, int stackSize, int priority);

// quit: A simplified version of quit_phase_1a (often used in later phases).
void quit(int switchToPid);

// join: Waits for a child process to terminate, returns the PID of the terminated child and sets its exit status.
int join(int *status);

// zap: Suspends execution of the process with the given PID (not implemented in Phase 1A).
void zap(int pid);

// getpid: Returns the PID of the currently running process.
int getpid(void);

// TEMP_switchTo: Temporarily switches context to the process with the given PID.
void TEMP_switchTo(int newpid);

// dumpProcesses: Prints the current process table information (for debugging).
void dumpProcesses(void);

// blockMe: Blocks the calling process (placeholder for later phases).
void blockMe(void);

// unblockProc: Unblocks the process with the given PID (placeholder for later phases).
int unblockProc(int pid);

#endif
