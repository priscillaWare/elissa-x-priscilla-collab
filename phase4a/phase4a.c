// phase4a.c

#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <stdio.h>
#include <string.h>

#define MAX_TERMINALS    4
#define MAX_LINE_LEN     MAXLINE
#define READ_QUEUE_SIZE  10
#define MAX_SLEEPERS     100

// --- Globals -------------------------------------------------------------

// For Sleep syscall
typedef struct {
    int pid;
    int wakeCycles;
} SleepEntry;
static SleepEntry sleepers[MAX_SLEEPERS];
static int        numSleepers = 0;
static int        clockCycles = 0;  // ~100ms ticks

// Per‐terminal line buffer for keyboard input
typedef struct {
    char lines[READ_QUEUE_SIZE][MAX_LINE_LEN+1];
    int head, tail, count;
} TermBuf;
static TermBuf termBuf[MAX_TERMINALS];

// Mailboxes for waking readers and serializing hardware writes
static int termReadMail[MAX_TERMINALS];
static int termWriteLock[MAX_TERMINALS];
static int termLock[MAX_TERMINALS];   // Lock to serialize both read/write per terminal

// --- Forward decls -------------------------------------------------------

static int  clockDriver(void *arg);
static int  termDriver(void *arg);
static void sys_sleep(USLOSS_Sysargs *args);
static void sys_termread(USLOSS_Sysargs *args);
static void sys_termwrite(USLOSS_Sysargs *args);

// --- Init & service startup --------------------------------------------

void phase4_init(void) {
    // Register our syscalls
    systemCallVec[SYS_SLEEP]     = sys_sleep;
    systemCallVec[SYS_TERMREAD]  = sys_termread;
    systemCallVec[SYS_TERMWRITE] = sys_termwrite;

    for (int u = 0; u < MAX_TERMINALS; ++u) {
        // Initialize the circular buffer
        termBuf[u].head = termBuf[u].tail = termBuf[u].count = 0;
        termBuf[u].lines[0][0] = '\0';

        // Mailbox to signal a complete line has arrived
        termReadMail[u] = MboxCreate(READ_QUEUE_SIZE, 0);

        // One‐slot mailbox to serialize writes to hardware
        termWriteLock[u] = MboxCreate(1, sizeof(int));
        { int one = 1; MboxSend(termWriteLock[u], &one, sizeof(one)); }
    }
    for (int i = 0; i < MAX_TERMINALS; i++) {
        termLock[i] = MboxCreate(1, sizeof(int));
        int one = 1;
        MboxSend(termLock[i], &one, sizeof(one));
    }
    
}


void phase4_start_service_processes(void) {
    // Spawn the clock driver
    spork("clockDriver", clockDriver, NULL, USLOSS_MIN_STACK, 3);

    // Spawn one driver per terminal unit
    for (int unit = 0; unit < MAX_TERMINALS; ++unit) {
        char name[16];
        snprintf(name, sizeof(name), "termDriver%d", unit);
        spork(name, termDriver, (void *)(long)unit, USLOSS_MIN_STACK, 3);
    }
}

// Don’t modify phase5_start_service_processes—keep it as the default NOP.

// --- Clock driver & Sleep syscall --------------------------------------

static int clockDriver(void *arg) {
    int status;
    // Enable 100ms clock interrupts
    USLOSS_DeviceOutput(USLOSS_CLOCK_DEV, 0, NULL);

    while (1) {
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        clockCycles++;
        for (int i = 0; i < numSleepers; ) {
            if (sleepers[i].wakeCycles <= clockCycles) {
                unblockProc(sleepers[i].pid);
                sleepers[i] = sleepers[--numSleepers];
            } else {
                i++;
            }
        }
    }
    return 0;
}

static void sys_sleep(USLOSS_Sysargs *args) {
    int seconds = (int)(long)args->arg1;
    if (seconds < 0 || numSleepers >= MAX_SLEEPERS) {
        args->arg4 = (void *)(long)-1;
        return;
    }
    sleepers[numSleepers++] = (SleepEntry){ getpid(), clockCycles + seconds*10 };
    blockMe();
    args->arg4 = (void *)0;
}

// --- Terminal read/write syscalls ---------------------------------------

static void sys_termread(USLOSS_Sysargs *args) {
    int unit  = (int)(long)args->arg3;
    char *buf = (char*)args->arg1;
    int   size = (int)(long)args->arg2;

    // Validate
    if (unit < 0 || unit >= MAX_TERMINALS || size <= 0) {
        args->arg4 = (void *)(long)-1;
        return;
    }

    // Wait until a complete line has been buffered by termDriver
    while (termBuf[unit].count == 0) {
        MboxRecv(termReadMail[unit], NULL, 0);
    }

    // Dequeue one line
    TermBuf *tb = &termBuf[unit];
    int idx = tb->head;
    int len = strlen(tb->lines[idx]);
    int n   = (len < size ? len : size);
    memcpy(buf, tb->lines[idx], n);

    tb->head  = (tb->head + 1) % READ_QUEUE_SIZE;
    tb->count--;

    args->arg2 = (void *)(long)n;
    args->arg4 = (void *)0;
}

static void sys_termwrite(USLOSS_Sysargs *args) {
    int unit = (int)(long)args->arg3;
    char *buf = (char *)args->arg1;
    int len = (int)(long)args->arg2;

    if (unit < 0 || unit >= MAX_TERMINALS || len < 0) {
        args->arg4 = (void *)(long)-1;
        return;
    }

    int lock;
    MboxRecv(termLock[unit], &lock, sizeof(lock));  // <- NEW

    for (int i = 0; i < len; i++) {
        unsigned char ch = buf[i];
        int ctrl = (ch << 8) | 0x1 | 0x2 | 0x4;
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)ctrl);
        int status;
        waitDevice(USLOSS_TERM_DEV, unit, &status);
    }

    MboxSend(termLock[unit], &lock, sizeof(lock));  // <- NEW

    args->arg2 = (void *)(long)len;
    args->arg4 = (void *)0;
}

// --- Terminal driver process -------------------------------------------

static int termDriver(void *arg) {
    int unit = (int)(long)arg, status;
    // Unmask both recv & xmit interrupts
    int ctrl = USLOSS_TERM_CTRL_RECV_INT(1) | USLOSS_TERM_CTRL_XMIT_INT(1);
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)ctrl);

    while (1) {
        int lock;
        MboxRecv(termLock[unit], &lock, sizeof(lock));  // <- NEW
    
        waitDevice(USLOSS_TERM_DEV, unit, &status);
    
        if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY) {
            char c = USLOSS_TERM_STAT_CHAR(status);
            if (c == '\0') {
                MboxSend(termLock[unit], &lock, sizeof(lock));
                continue;
            }
            TermBuf *tb = &termBuf[unit];
            int pos = strlen(tb->lines[tb->tail]);
            if (pos < MAX_LINE_LEN) {
                tb->lines[tb->tail][pos] = c;
                tb->lines[tb->tail][pos+1] = '\0';
            }
            if (c == '\n' || pos+1 == MAX_LINE_LEN) {
                tb->count = (tb->count < READ_QUEUE_SIZE ? tb->count+1 : READ_QUEUE_SIZE);
                tb->tail = (tb->tail + 1) % READ_QUEUE_SIZE;
                tb->lines[tb->tail][0] = '\0';
                MboxCondSend(termReadMail[unit], NULL, 0);
            }
        }
    
        MboxSend(termLock[unit], &lock, sizeof(lock));  // <- NEW
    }
    return 0;
}
    
    
