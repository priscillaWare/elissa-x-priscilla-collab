## Features
- Implements syscalls to support user-mode processes
- Uses a semaphore system for processes
- Uses a zero slot mailbox system for synchronization and wakeup mechanisms

## File Overview
- phase3.c - syscall handlers and semaphore system
- phase3typedef.h - contains the struct functionArgs used to handle user functions in new processes

## Function List
- sys_terminate: Terminates the current process, with the status specified.
- trampoline: a wrapper for the user function.
- sys_spawn: creates a new user mode process.
- sys_wait: Calls join(), and returns the PID and status that join() provided
- sys_semcreate: creates a new semaphore and returns the semaphore ID
- sys_semp: executes the P() operation
- sys_semv: executes the V() operation
- sys_gettime: returns time of day via out pointer, arg1
- sys_getpid: returns the pid via out pointer, arg1
- phase3_init: initializes any data structures and fills in the systemCallVec[]
- phase3_start_service_processes
- kernSemCreate: creates a new semaphore and returns the semaphore ID
- kernSemP: executes the P() operation
- kernSemV: executes the V() operation

## Known Issues
Testcase 10: values do not match exactly, but are close to the expected output
Testcase 20: order matches the alternate ouput specified in the testcase
