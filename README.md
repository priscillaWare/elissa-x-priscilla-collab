# elissa-x-priscilla-collab

Purpose:

This module implements the core process management routines for Phase 1A of the USLOSS kernel.
It provides support for creating new processes ("spork"), terminating processes ("quit_phase_1a"), joining terminated child processes ("join"), context switching, and basic debugging (via dumpProcesses).

Design Overview:

Fixed-Size Process Table:
The system uses a fixed-size array (process_table) with MAXPROC slots to hold process information. Each process is assigned a unique PID that increases monotonically.
The table slot is determined using modulo arithmetic:
    slot = pid % MAXPROC
This means that even if the PIDs keep increasing, they map into one of the fixed slots. When a process terminates and is "joined", its slot is freed (its PID is set to -1) for reuse by a future process.

Process Structure:
Each process is represented by a Process structure that contains:

pid: Unique process identifier.
priority: Process priority (lower numbers represent higher priority).
status: Process state (0 = runnable, -1 = terminated).
name: Process name.
Pointers for building a linked list of children (children, next) and linking to its parent.
A USLOSS_Context structure used for context switching.
A pointer to the process's stack.
The starting function (startFunc) and its argument (arg).
exit_status: The exit value set when the process terminates.
termOrder: A counter indicating the order in which the process terminated (used by join).
Key Functions:

phase1_init()
Initializes the process table and creates the special init process (PID 1). The init process is responsible for bootstrapping the system and eventually calling testcase_main.

spork()
Creates a new process with a specified name, starting function, argument, stack size, and priority. It validates the parameters (e.g., stack size must be at least USLOSS_MIN_STACK, valid priority range is 1–7) and assigns a free slot in the process table using modulo arithmetic.

quit_phase_1a()
Terminates the currently running process. It checks that the process is in kernel mode and that it has no active (unjoined) children. It then marks the process as terminated, sets its exit status and termination order, and switches context to a specified process (usually the parent).

join()
Waits for a child process to terminate. It scans the parent's children list, selects the terminated child with the highest termination order (i.e., the most recently terminated), removes it from the list, and returns its PID and exit status. If no terminated child is found, it returns -2.

TEMP_switchTo()
Performs a temporary context switch to the process with the specified PID.

dumpProcesses()
Prints the current state of the process table (PID, parent PID, process name, priority, and state). This is used for debugging purposes.

processWrapper()
The entry function for every new process. It calls the process’s start function with its provided argument, and when that function returns, it calls quit_phase_1a to terminate the process.

Usage:
The module is integrated into the USLOSS kernel. The initialization is done via phase1_init(), which sets up the process table and creates the init process.

Authors:
Elissa Matlock
Priscilla Ware
