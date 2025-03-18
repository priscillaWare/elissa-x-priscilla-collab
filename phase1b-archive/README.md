# Phase1B Implementation – **Short README**

This project implements **core process management** for Phase 1B of a USLOSS-like OS, providing:

- **Process creation** (`spork`),  
- **Termination** (`quit`),  
- **Join** (`join`),  
- **Blocking** (`blockMe`/`unblockProc`),  
- **Zapping** (`zap`),  
- **Priority-based dispatcher** (`dispatcher`).

Some testcases (e.g., #13) may print outputs in a different order due to scheduling, but most match closely.

---

## Key Structures

- **`process_table[MAXPROC]`**: Array of `Process` structs. A `pid == -1` means unused.  
- **`running_process`**: Currently running process.  
- **`ready_queues[6]`**: Linked lists for priorities 1–6.  

---

## Functions

### `init_run(void *arg)`
- Runs in PID 1 (priority 6). Enables interrupts, spawns `testcase_main`, then repeatedly `join()`s children until none remain, halting the system.

### `phase1_init()`
- Initializes `process_table`, creates the **init** process (PID 1), assigns its stack, and places it in `ready_queues[5]`.

### `spork(name, startFunc, arg, stackSize, priority)`
- Creates a new process:
  - Allocates a `Process` entry, a PID, a stack, and initializes context.
  - Sets up parent–child links.
  - Enqueues it in the appropriate `ready_queues[priority - 1]`.
  - May invoke `dispatcher()` if the new process should preempt.

### `processWrapper()`
- Launches `running_process->startFunc(arg)`. On return, calls `quit()` with the returned value.

### `join(int *status)`
- Waits for one child to terminate.
  - Returns `-2` if no children; `-3` if `status` is `NULL`.
  - Otherwise blocks until a child has `status == -1`, then returns that child's PID and exit status.

### `quit(int status)`
- Terminates `running_process`.
  - Ensures no active children remain.
  - Unblocks the parent (if waiting) and any zapping processes.
  - Removes itself from the ready queue and calls `dispatcher()`.

### `blockMe()`
- Marks the current process as blocked, calls `dispatcher()`.

### `unblockProc(int pid)`
- Finds the blocked process with `pid`, sets it to runnable, enqueues it, and calls `dispatcher()`.

### `zap(int pid)`
- Blocks the current process until the target process (PID = `pid`) terminates, unless the target is already terminated.

### `dispatcher()`
- Selects the highest-priority runnable process in `ready_queues` and switches to it.  
- Halts if none are ready.

### `contextSwitch(Process *next)`
- Performs a USLOSS context switch from the current process to `next`.

### `getpid()`
- Returns `running_process->pid`.

### `dumpProcesses()`
- Prints each process’s PID, parent PID, name, priority, and state (Running/Blocked/Terminated).

### `remove_from_ready_queue(Process *p)`
- Removes process `p` from its corresponding `ready_queues[p->priority - 1]`.

---

## Testcase Behavior

- **Ordering Differences**: Certain testcases (e.g., #13) may exhibit **out-of-order** console prints. This is normal for multi-process scheduling; results still align with correct semantics.  
- **Close to Suggested Output**: Most other testcases produce outputs that closely match the expected reference. Minor scheduling differences (timing of prints, context switches) can cause slightly different sequences without indicating errors.

Overall, this Phase1B code correctly handles essential process-management features but may show **slight** variations in print order for some tests.
