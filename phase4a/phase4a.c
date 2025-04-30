// phase4a.c

#include <usloss.h>
#include <usyscall.h> 
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>
#include <phase4.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>  

#define MAX_TERMINALS    4
#define MAX_LINE_LEN     MAXLINE
#define READ_QUEUE_SIZE  10
#define MAX_SLEEPERS     100
#define MAX_DISK_REQUESTS 100 
#define MAXPROC  50
static int diskDriverStarted = 0;   // have we sporked the driver yet?


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
// Disk request structure
typedef struct {
    int       unit;
    int       track;
    int       firstBlock;
    int       numBlocks;
    void     *buffer;
    int       isRead;        // 1 = read, 0 = write
    int       pid;           // caller’s PID, to unblock
} DiskRequest;

// Mailboxes for waking readers and serializing hardware writes
static DiskRequest diskQueue[MAX_DISK_REQUESTS];
static int         dqHead = 0, dqCount = 0;
static int termReadMail[MAX_TERMINALS];
static int termWriteLock[MAX_TERMINALS];
static int termLock[MAX_TERMINALS];   // Lock to serialize both read/write per terminal
// Tracks last written line (to ignore echoed characters in termDriver)
static char lastWritten[MAX_TERMINALS][MAX_LINE_LEN+1];
static int  echoIndex[MAX_TERMINALS]; // Index of next echo char expected
static int termIOBusyLock[MAX_TERMINALS];
static int diskLock;     // one‐slot mailbox as mutex
static int diskMail; 
static int diskStatus[MAXPROC];


// --- Forward decls -------------------------------------------------------

static int  clockDriver(void *arg);
static int  termDriver(void *arg);
static void sys_sleep(USLOSS_Sysargs *args);
static void sys_termread(USLOSS_Sysargs *args);
static void sys_termwrite(USLOSS_Sysargs *args);
static void sys_disksize(USLOSS_Sysargs *args);
static void sys_diskread(USLOSS_Sysargs *args);
static void sys_diskwrite(USLOSS_Sysargs *args);
static int  diskDriver(void *arg);

static void diskTrap(int dev, void *arg) {
    int unit   = (int)(long)arg;
    int status;
    // Acknowledge the interrupt and read the status
    USLOSS_DeviceInput(dev, unit, &status);
}


// --- Init & service startup --------------------------------------------
void phase4_init(void) {
    // Register syscalls
    systemCallVec[SYS_SLEEP]     = sys_sleep;
    systemCallVec[SYS_TERMREAD]  = sys_termread;
    systemCallVec[SYS_TERMWRITE] = sys_termwrite;
    systemCallVec[SYS_DISKSIZE]  = sys_disksize;
    systemCallVec[SYS_DISKREAD]  = sys_diskread;
    systemCallVec[SYS_DISKWRITE] = sys_diskwrite;

    // Initialize terminal mailboxes and locks
    for (int u = 0; u < MAX_TERMINALS; ++u) {
        termBuf[u].head = termBuf[u].tail = termBuf[u].count = 0;
        termReadMail[u] = MboxCreate(READ_QUEUE_SIZE, 0);
        termWriteLock[u] = MboxCreate(1, sizeof(int));
        { int one = 1; MboxSend(termWriteLock[u], &one, sizeof(one)); }
        termLock[u] = MboxCreate(1, sizeof(int));
        { int one = 1; MboxSend(termLock[u], &one, sizeof(one)); }
        termIOBusyLock[u] = MboxCreate(1, sizeof(int));
        { int one = 1; MboxSend(termIOBusyLock[u], &one, sizeof(one)); }
    }

    // Initialize disk queue mailboxes
    diskLock = MboxCreate(1, sizeof(int));
    { int one = 1;  MboxSend(diskLock, &one, sizeof(one)); }
    diskMail = MboxCreate(0, 0);
}

void phase4_start_service_processes(void) {
    // Clock driver
    spork("clockDriver", clockDriver, NULL, USLOSS_MIN_STACK, 2);

    // Terminal drivers
    for (int unit = 0; unit < MAX_TERMINALS; ++unit) {
        char name[16];
        snprintf(name, sizeof(name), "termDriver%d", unit);
        spork(name, termDriver, (void *)(long)unit, USLOSS_MIN_STACK, 2);
    }

    // Disk driver
    spork("diskDriver", diskDriver, NULL, USLOSS_MIN_STACK, 2);
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
static void sys_termwrite(USLOSS_Sysargs *args) {
    int  unit = (int)(long)args->arg3;
    char *buf  = (char*) args->arg1;
    int   len  = (int)(long)args->arg2;

    if (unit<0 || unit>=MAX_TERMINALS || len<0) {
        args->arg4 = (void*)(long)-1;
        return;
    }

    // serialize writers
    int slot;
    MboxRecv(termWriteLock[unit], &slot, sizeof(slot));

    // send each byte to the real terminal hardware
    for (int i = 0; i < len; i++) {
        unsigned char ch = buf[i];
        int ctrl = 0;
        // preserve RECV interrupts!
        ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
        ctrl = USLOSS_TERM_CTRL_CHAR(ctrl, ch);
        ctrl = USLOSS_TERM_CTRL_XMIT_CHAR(ctrl);
        ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)ctrl);

        int status;
        waitDevice(USLOSS_TERM_DEV, unit, &status);
    }

    MboxSend(termWriteLock[unit], &slot, sizeof(slot));
    args->arg2 = (void*)(long)len;
    args->arg4 = (void*)0;
}


// --- sys_termread: dequeue a full line from termBuf ---
static void sys_termread(USLOSS_Sysargs *args) {
    int unit = (int)(long) args->arg3;
    char *buf = (char*) args->arg1;
    int  size = (int)(long) args->arg2 + 1;

    if (unit<0 || unit>=MAX_TERMINALS || size<=0) {
        args->arg4 = (void*)(long)-1;
        return;
    }

    TermBuf *tb = &termBuf[unit];
    while (tb->count == 0) {
        MboxRecv(termReadMail[unit], NULL, 0);
    }

    int idx = tb->head;
    int len     = strlen(tb->lines[idx]);        // includes the '\n'
    int maxCopy = size - 1;                      // reserve space for NUL
    int n       = (len < maxCopy) ? len : maxCopy;
    memcpy(buf, tb->lines[idx], n);
    buf[n] = '\0';                               // ensure C‐string

    tb->head  = (tb->head + 1) % READ_QUEUE_SIZE;
    tb->count--;

    args->arg2 = (void*)(long)n;                 // # of chars read, excl. NUL
    args->arg4 = (void*)0;
}


// --- termDriver: capture real keyboard input into termBuf too ---
static int termDriver(void *arg) {
    int unit = (int)(long)arg, status;

    // unmask both recv & xmit so writing works AND we get recv interrupts
    int ctrl = 0;
    ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
    ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
    USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)ctrl);

    while (1) {
        waitDevice(USLOSS_TERM_DEV, unit, &status);

        if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY) {
            char c = USLOSS_TERM_STAT_CHAR(status);
            TermBuf *tb = &termBuf[unit];
            int pos = strlen(tb->lines[tb->tail]);
            if (pos < MAX_LINE_LEN) {
                tb->lines[tb->tail][pos] = c;
                tb->lines[tb->tail][pos+1] = '\0';
            }
            if (c == '\n' || pos+1 == MAX_LINE_LEN) {
                if (tb->count < READ_QUEUE_SIZE) {
                    tb->count++;
                }
                tb->tail = (tb->tail + 1) % READ_QUEUE_SIZE;
                tb->lines[tb->tail][0] = '\0';
                MboxCondSend(termReadMail[unit], NULL, 0);
            }
        }
    }
    return 0;  // never reached
}
// --- Disk syscalls & driver ---------------------------------------------
//-----------------------------------------------------------------------------
// sys_disksize -- syscall handler for DiskSize(unit, &sector, &track, &disk)
//-----------------------------------------------------------------------------
static void sys_disksize(USLOSS_Sysargs *args) {
    int unit      = (int)(long) args->arg1;   // 0 or 1
    int status;
    int numTracks;
    USLOSS_DeviceRequest req;

    // Build the TRACKS request so the disk will DMA‐write numTracks
    req.opr  = USLOSS_DISK_TRACKS;
    req.reg1 = &numTracks;
    req.reg2 = NULL;

    // Post it …
    USLOSS_DeviceOutput(USLOSS_DISK_DEV, unit, &req);

    // … and wait for the interrupt
    waitDevice(USLOSS_DISK_DEV, unit, &status);

    // Return values back to userland
    args->arg1 = (void*)(long) USLOSS_DISK_SECTOR_SIZE; // 512 bytes/sector
    args->arg2 = (void*)(long) USLOSS_DISK_TRACK_SIZE;  // 16 sectors/track
    args->arg3 = (void*)(long) numTracks;               // filled by DMA
    args->arg4 = (void*)(long) 0;                       // success
}

static void enqueue_disk_request(DiskRequest *req) {
    MboxRecv(diskLock, NULL, 0);
    if (dqCount < MAX_DISK_REQUESTS) {
        int idx = (dqHead + dqCount) % MAX_DISK_REQUESTS;
        diskQueue[idx] = *req;
        dqCount++;
    }
    MboxSend(diskLock, NULL, 0);
    MboxSend(diskMail, NULL, 0);
}


static void sys_diskread(USLOSS_Sysargs *args) {
    if (!diskDriverStarted) {
        spork("diskDriver", diskDriver, NULL, USLOSS_MIN_STACK, 3);
        diskDriverStarted = 1;
    }
    //USLOSS_Console("starting to read: \n");
    DiskRequest req = {
        .unit       = (int)(long) args->arg2,    // disk unit (0 or 1)
        .track      = (int)(long) args->arg3,    // starting track
        .firstBlock = (int)(long) args->arg4,    // starting sector
        .numBlocks  = (int)(long) args->arg5,    // number of sectors
        .buffer     =        args->arg1,         // user buffer pointer
        .isRead     =        1,                  // READ operation
        .pid        =  getpid()                   // for unblockProc()
    };

    enqueue_disk_request(&req);  // push onto diskQueue[]
    blockMe();                   // sleep until diskDriver unblocks us

    // Upon waking, return hardware status (always READY=0 here) and
    // a syscall‐level success code of 0
    args->arg1 = (void*)(long) USLOSS_DEV_READY;
    args->arg4 = (void*)(long) 0;
}

static void sys_diskwrite(USLOSS_Sysargs *args) {
    if (!diskDriverStarted) {
        spork("diskDriver", diskDriver, NULL, USLOSS_MIN_STACK, 3);
        diskDriverStarted = 1;
    } 
    DiskRequest req = {
        .unit       = (int)(long) args->arg2,    // disk unit
        .track      = (int)(long) args->arg3,    // starting track
        .firstBlock = (int)(long) args->arg4,    // starting sector
        .numBlocks  = (int)(long) args->arg5,    // number of sectors
        .buffer     =        args->arg1,         // user buffer pointer
        .isRead     =        0,                  // WRITE operation
        .pid        =  getpid()
    };

    enqueue_disk_request(&req);
    blockMe();
    args->arg1 = (void*)(long) USLOSS_DEV_READY;
    args->arg4 = (void*)(long) 0;
}

//-----------------------------------------------------------------------------
// diskDriver – service disk requests in C‐SCAN order, using USLOSS_DeviceRequest
//-----------------------------------------------------------------------------
static int diskDriver(void *arg) {
    int currentTrack = 0, status;
    while (1) {
        // wait for a request
        MboxRecv(diskMail, NULL, 0);
        MboxRecv(diskLock, NULL, 0);

        // C-SCAN to pick bestIdx …
        int bestIdx = -1, bestTrack = INT_MAX;
        for (int i = 0; i < dqCount; i++) {
            int idx   = (dqHead + i) % MAX_DISK_REQUESTS;
            int track = diskQueue[idx].track;
            if (track >= currentTrack && track < bestTrack) {
                bestTrack = track;
                bestIdx   = idx;
            }
        }
        if (bestIdx < 0) {
            bestTrack = INT_MAX;
            for (int i = 0; i < dqCount; i++) {
                int idx   = (dqHead + i) % MAX_DISK_REQUESTS;
                int track = diskQueue[idx].track;
                if (track < bestTrack) {
                    bestTrack = track;
                    bestIdx   = idx;
                }
            }
        }

        // dequeue
        DiskRequest req = diskQueue[bestIdx];
        for (int j = bestIdx; j < dqCount-1; j++) {
            int src = (dqHead + j + 1) % MAX_DISK_REQUESTS;
            int dst = (dqHead + j)     % MAX_DISK_REQUESTS;
            diskQueue[dst] = diskQueue[src];
        }
        dqCount--;
        MboxSend(diskLock, NULL, 0);

        // perform I/O (wrap-around per block)…
        for (int blk = 0; blk < req.numBlocks; blk++) {
            int logical     = req.firstBlock + blk;
            int blockTrack  = req.track + (logical / USLOSS_DISK_TRACK_SIZE);
            int blockNumber = logical % USLOSS_DISK_TRACK_SIZE;
            USLOSS_DeviceRequest dr;

            // SEEK
            dr.opr  = USLOSS_DISK_SEEK;
            dr.reg1 = (void*)(long) blockTrack;
            dr.reg2 = NULL;
            USLOSS_DeviceOutput(USLOSS_DISK_DEV, req.unit, &dr);
            waitDevice(USLOSS_DISK_DEV, req.unit, &status);

            // READ or WRITE
            dr.opr  = req.isRead ? USLOSS_DISK_READ
                                 : USLOSS_DISK_WRITE;
            dr.reg1 = (void*)(long) blockNumber;
            dr.reg2 = (void*) ((char*)req.buffer
                           + blk * USLOSS_DISK_SECTOR_SIZE);
            USLOSS_DeviceOutput(USLOSS_DISK_DEV, req.unit, &dr);
            waitDevice(USLOSS_DISK_DEV, req.unit, &status);
        }

        // unblock the process
        unblockProc(req.pid);
        currentTrack = req.track;
    }
    return 0;
}