
/*
 * File: phase1b.c
 * Purpose: Implements the core process management routines for Phase 1B of USLOSS.
 *          Adds process blocking, zapping, and a dispatcher with priority scheduling.
 * Authors: Elissa Matlock and Priscilla Ware
 */

 #include <stdio.h>
 #include <stdlib.h>
 #include <string.h>
 #include "phase1b.h"
 #include "usloss.h"

 static Process process_table[MAXPROC];
 Process *running_process = NULL;
 static int next_pid = 0;

 // Queues for scheduling
 static Process *ready_queues[6] = {NULL, NULL, NULL, NULL, NULL, NULL};

extern int testcase_main(void *arg);

int init_run(void *arg) {

    int current = USLOSS_PsrGet();
    int result = USLOSS_PsrSet(current | USLOSS_PSR_CURRENT_INT);
    if (result != USLOSS_DEV_OK) {
        USLOSS_Halt(1);
    }

    extern void phase2_start_service_processes(void);
    extern void phase3_start_service_processes(void);
    extern void phase4_start_service_processes(void);
    extern void phase5_start_service_processes(void);

    // Call the service process functions so their messages are printed.
    phase2_start_service_processes();
    phase3_start_service_processes();
    phase4_start_service_processes();
    phase5_start_service_processes();

    int testcase_pid = spork("testcase_main", testcase_main, NULL, USLOSS_MIN_STACK, 3);
    if (testcase_pid < 0) {
        USLOSS_Halt(1);
    }


    dispatcher();  // Ensures the first process actually starts

    while (1) {
        int status;
        int childPid = join(&status);
        if (childPid == -2) {
            USLOSS_Halt(0);
        }
    }
    return 0;
}


/*
 * phase1_init: Called by the startup code to initialize the kernel.
 * Sets up the process table and creates the special init process.
 */
void phase1_init(void) {

    for (int i = 0; i < MAXPROC; i++) {
        process_table[i].pid = -1;
        process_table[i].status = -1;
        process_table[i].children = NULL;
        process_table[i].sibling = NULL;  // Initialize sibling pointer
        process_table[i].next = NULL;
        process_table[i].parent = NULL;
        process_table[i].exit_status = 0;
        process_table[i].stack = NULL;
    }

    Process *init_proc = &process_table[1];
    init_proc->pid = 1;
    init_proc->priority = 6;
    strncpy(init_proc->name, "init", MAXNAME);
    init_proc->status = 0;  // Runnable
    init_proc->parent = NULL;
    init_proc->startFunc = init_run;

    char *init_stack = malloc(USLOSS_MIN_STACK);
    if (init_stack == NULL) {
        USLOSS_Console("ERROR: malloc failed for init process\n");
        USLOSS_Halt(1);
    }
    init_proc->stack = init_stack;

    USLOSS_ContextInit(&init_proc->context, init_stack, USLOSS_MIN_STACK, NULL, processWrapper);
    ready_queues[5] = init_proc;  // Priority 6 goes to ready_queues[5]
    next_pid += 2;
}


int spork(char *name, int (*startFunc)(void*), void *arg, int stackSize, int priority) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call spork while in user mode!\n");
        USLOSS_Halt(1);
    }
    if (priority < 1 || priority > 5) {
        USLOSS_Console("spork(): ERROR - Invalid priority %d\n", priority);
        return -1;
    }
    if (stackSize < USLOSS_MIN_STACK) {
        USLOSS_Console("spork(): ERROR - Stack size too small\n");
        return -2;
    }

    int slot = -1;
    for (int i = next_pid % MAXPROC; i < MAXPROC; i++) {
        if (process_table[i].pid == -1) {
            slot = i;
            break;
        }
    }
    // If no slot found, check from beginning
    if (slot == -1) {
        for (int i = 0; i < next_pid % MAXPROC; i++) {
            if (process_table[i].pid == -1) {
                slot = i;
                break;
            }
        }
    }

    if (slot == -1) {
        USLOSS_Console("spork(): ERROR - No free process slots\n");
        return -1;
    }

    Process *new_proc = &process_table[slot];

    int pid;
    do {
        pid = next_pid++;
    } while (pid > 2 && (pid % 50 == 1 || pid % 50 == 2));

    new_proc->pid = pid;
    new_proc->priority = priority;
    strncpy(new_proc->name, name, MAXNAME);
    new_proc->status = 0;
    new_proc->stack = malloc(stackSize);
    if (!new_proc->stack) {
        USLOSS_Console("ERROR: malloc failed for process %s\n", name);
        USLOSS_Halt(1);
    }
    new_proc->startFunc = startFunc;
    new_proc->arg = arg;
    USLOSS_ContextInit(&new_proc->context, new_proc->stack, stackSize, NULL, processWrapper);

    if (running_process) {
        new_proc->parent = running_process;

        if (new_proc->priority > running_process->priority) {
            new_proc->sibling = NULL;
            // FIFO insertion into the parent's children list:
            if (running_process->children == NULL) {
                running_process->children = new_proc;
                new_proc->sibling = NULL;
            } else {
                Process *temp = running_process->children;
                while (temp->sibling != NULL)
                    temp = temp->sibling;
                temp->sibling = new_proc;
                new_proc->sibling = NULL;
            }
        } else {
            new_proc->sibling = running_process->children;
            running_process->children = new_proc;
        }
    } else {
        new_proc->parent = NULL;
    }

    int idx = new_proc->priority - 1;
    if (ready_queues[idx] == NULL) {
        ready_queues[idx] = new_proc;
        new_proc->next = NULL;
    } else {
        Process *temp = ready_queues[idx];
        while (temp->next != NULL)
            temp = temp->next;
        temp->next = new_proc;
        new_proc->next = NULL;
    }


    if (running_process && new_proc->priority <= running_process->priority) {
        dispatcher();
    }

    return new_proc->pid;
}


void processWrapper(void) {
    if (running_process == NULL || running_process->startFunc == NULL)
        USLOSS_Halt(1);

    int rc = running_process->startFunc(running_process->arg);

    // If the process did not join with all its children, its children list will be non-NULL.
    if (running_process->children != NULL) {
        USLOSS_Console("ERROR: Process pid %d called quit() while it still had children.\n", running_process->pid);
        USLOSS_Halt(1);
    }

    quit(rc);
}


int join(int *status) {
    if (status == NULL) {
        USLOSS_Console("ERROR: join() called with NULL status pointer.\n");
        return -3;
    }
    Process *parent = running_process;
    if (parent->children == NULL) {
        return -2;
    }

    while (1) {
        Process *prev = NULL;
        Process *child = parent->children;
        while (child != NULL) {
            if (child->status == -1) {
                *status = child->exit_status;
                int childPid = child->pid;

                if (prev == NULL)
                    parent->children = child->sibling;
                else
                    prev->sibling = child->sibling;

                free(child->stack);
                child->stack = NULL;
                child->pid = -1;
                child->sibling = NULL;

                return childPid;
            }
            prev = child;
            child = child->sibling;
        }

        parent->status = 1;
        remove_from_ready_queue(parent);
        dispatcher();
    }
}


void quit(int status) {
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
        USLOSS_Console("ERROR: Someone attempted to call quit while in user mode!\n");
        USLOSS_Halt(1);
    }

    if (running_process->children != NULL) {
        USLOSS_Console("ERROR: Process %d quitting with active children!\n", running_process->pid);
        USLOSS_Halt(1);
    }

    running_process->status = -1;  // TERMINATED (was 2)
    running_process->exit_status = status;

    // Unblock parent if waiting on join()
    if (running_process->parent && running_process->parent->status == 1) {
        running_process->parent->status = 0;  // 0 = Runnable
        int prio = running_process->parent->priority - 1;
        running_process->parent->next = ready_queues[prio];
        ready_queues[prio] = running_process->parent;
    }

    // Unblock any process waiting via zap()
    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].status == 1 && process_table[i].zap_target == running_process) {
          USLOSS_Console("quit(): unblocking process process: %s\n", process_table[i].status);
            process_table[i].status = 0;  // 0 = Runnable
            process_table[i].zap_target = NULL;
            int prio = process_table[i].priority - 1;
            process_table[i].next = ready_queues[prio];
            ready_queues[prio] = &process_table[i];
        }
    }

    remove_from_ready_queue(running_process);
    dispatcher();

}



void blockMe(void) {
     running_process->status = 1;
     dispatcher();
}


int unblockProc(int pid) {
    for (int i = 0; i < MAXPROC; i++) {
         if (process_table[i].pid == pid && process_table[i].status == 1) {
             process_table[i].status = 0;
             if (!ready_queues[process_table[i].priority - 1]) {
                 ready_queues[process_table[i].priority - 1] = &process_table[i];
             } else {
                 Process *temp = ready_queues[process_table[i].priority - 1];
                 while (temp->next) temp = temp->next;
                 temp->next = &process_table[i];
             }
             dispatcher();
             return 0;
         }
     }
    return -2;
}


void zap(int pid) {
    if (pid == running_process->pid) {
        USLOSS_Console("ERROR: Process cannot zap itself.\n");
        USLOSS_Halt(1);
    }

    Process *target = NULL;
    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].pid == pid) {
            target = &process_table[i];
            break;
        }
    }
    if (!target) {
        USLOSS_Console("ERROR: Attempted to zap a non-existent process (PID %d).\n", pid);
        USLOSS_Halt(1);
    }

    // If the target is already terminated, return immediately
    if (target->status == -1)
        return;

    running_process->status = 1;  // Block self
    running_process->zap_target = target;

    //remove_from_ready_queue(running_process);
    dispatcher();

    // Instead of checking target->status, wait until our zap_target gets cleared
    //while (target->pid != -1) {
       // dispatcher();
   // }

}


void dispatcher(void) {
    Process *next = NULL;

    // Find the highest-priority process to run
    for (int i = 0; i < 6; i++) {
        if (ready_queues[i] != NULL && ready_queues[i]->status != 1 && running_process->pid != ready_queues[i]->pid) {
            next = ready_queues[i];
            break;
        }
    }

    if (next == NULL) {
        USLOSS_Console("ERROR: No ready processes! Halting.\n");
        USLOSS_Halt(0);
    }

    contextSwitch(next);
}


void contextSwitch(Process *next) {
    USLOSS_Console("dispatcher(): next is %s with status %d\n", next->name, next->exit_status);
    if (running_process == next) {
        USLOSS_Console("FUCK\n");
        USLOSS_Halt(0);
        return;
    }

    Process *old_process = running_process;
    running_process = next;  // <- Make sure this happens!

    if (old_process != NULL) {
        USLOSS_ContextSwitch(&old_process->context, &running_process->context);
    } else {
        processWrapper();
    }
}


int getpid(void) {
    return running_process->pid;
}


void dumpProcesses(void) {
    USLOSS_Console("PID  PPID  NAME              PRIORITY  STATE\n");
    for (int i = 0; i < MAXPROC; i++) {
        if (process_table[i].pid != -1) {
            int pid = process_table[i].pid;
            int ppid = (process_table[i].parent != NULL) ? process_table[i].parent->pid : 0;
            char state[64];

            if (running_process && process_table[i].pid == running_process->pid) {
                strcpy(state, "Running");
            } else if (process_table[i].status == -1) {
                snprintf(state, sizeof(state), "Terminated(%d)", process_table[i].exit_status);
            } else if (process_table[i].status == 2) {  // ✅ Show Zapped status
                snprintf(state, sizeof(state), "Zapped (waiting on PID %d)",
                         process_table[i].zap_target ? process_table[i].zap_target->pid : -1);
            } else if (process_table[i].status == 1) {
                strcpy(state, "Blocked(waiting for zap target to quit)");
            } else {
                strcpy(state, "Runnable");
            }

            USLOSS_Console("%3d %5d  %-17s %-9d %-15s\n",
                           pid, ppid, process_table[i].name,
                           process_table[i].priority, state);
        }
    }
}


void remove_from_ready_queue(Process *p) {
  USLOSS_Console("removing: %s\n", p->name);
    int prio = p->priority;
    Process **cur = &ready_queues[prio - 1];
    while (*cur != NULL) {
        if (*cur == p) {
            *cur = (*cur)->next;
            p->next = NULL;
            return;
        }
        cur = &((*cur)->next);
    }
}
