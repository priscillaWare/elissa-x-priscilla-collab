
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
static int blockedWriters[USLOSS_TERM_UNITS];
static int diskLock;     // one‐slot mailbox as mutex
static int diskMail; 
static int diskStatus[MAXPROC];
static int termXmitMail[MAX_TERMINALS];



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
static int dummyproc(char *arg);

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
    for (int u = 0; u < MAX_TERMINALS; u++) {
        // existing init
        termBuf[u].head = termBuf[u].tail = termBuf[u].count = 0;
        for (int i = 0; i < READ_QUEUE_SIZE; i++) {
            termBuf[u].lines[i][0] = '\0';
        }
        termReadMail[u]  = MboxCreate(READ_QUEUE_SIZE, 0);
        termWriteLock[u] = MboxCreate(1, sizeof(int));
        { int one = 1; MboxSend(termWriteLock[u], &one, sizeof(one)); }

        termXmitMail[u] = MboxCreate(1, 0);

    }

    // Initialize disk queue mailboxes
    diskLock = MboxCreate(1, sizeof(int));
    { int one = 1;  MboxSend(diskLock, &one, sizeof(one)); }
    diskMail = MboxCreate(MAX_DISK_REQUESTS, 0);
}

void phase4_start_service_processes(void) {
    // Clock driver
    spork("clockDriver", clockDriver, NULL, USLOSS_MIN_STACK, 2);

    // Terminal drivers
    for (int unit = 0; unit < MAX_TERMINALS; ++unit) {
        char name[16];
        snprintf(name, sizeof(name), "termDriver%d", unit);

        // enable both RECV and XMIT interrupts and leave them on forever
        int ctrl = 0;
        ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
        ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)ctrl);

        spork(name, termDriver, (void*)(long)unit, USLOSS_MIN_STACK, 2);
    }

    // Disk driver
    spork("diskDriver1", diskDriver, NULL, USLOSS_MIN_STACK, 2);
    spork("clockDriverDummy", clockDriver, NULL, USLOSS_MIN_STACK, 2);
}


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

// -------------------------------------------------------
// sys_termwrite
//    – preserves RECV_INT on every output
// -------------------------------------------------------
static void sys_termwrite(USLOSS_Sysargs *args) {
    int    unit = (int)(long)args->arg3;
    char  *buf  = (char*) args->arg1;
    int    len  = (int)(long)args->arg2;

    if (unit < 0 || unit >= MAX_TERMINALS || len < 0) {
        args->arg4 = (void*)(long)-1;
        return;
    }

    // 1) Serialize writers
    int slot;
    MboxRecv(termWriteLock[unit], &slot, sizeof(slot));
    int ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
    // 2) For each byte: send to device, then wait on our xmit-mailbox
    for (int i = 0; i < len; i++) {
        
        unsigned char ch = buf[i];
        ctrl = 0;
        ctrl = USLOSS_TERM_CTRL_CHAR(ctrl,      ch);
        ctrl = USLOSS_TERM_CTRL_XMIT_CHAR(ctrl);
        ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
        // leave RECV_INT on so driver still sees incoming chars
        ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);

        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)ctrl);
        
        blockedWriters[unit]++;  
        MboxRecv(termXmitMail[unit], NULL, 0);  
        blockedWriters[unit]--;

        //USLOSS_Console("when do we start writin?\n");
    }

    // 3) Release writer lock
    MboxSend(termWriteLock[unit], &slot, sizeof(slot));

    // 4) Return success
    args->arg2 = (void*)(long)len;
    args->arg4 = (void*)0;
}


// -------------------------------------------------------
// sys_termread
//    – dequeue exactly one full line
//    – blocks on a blocking MboxRecv
// -------------------------------------------------------
static void sys_termread(USLOSS_Sysargs *args) {
    int    unit = (int)(long) args->arg3;
    char  *buf  = (char*) args->arg1;
    int     max = (int)(long) args->arg2;

    if (unit < 0 || unit >= MAX_TERMINALS || max <= 0) {
        args->arg4 = (void*)(long)-1;
        return;
    }

    TermBuf *tb = &termBuf[unit];

    // wait until at least one full line is in the queue
    while (tb->count == 0) {
        MboxRecv(termReadMail[unit], NULL, 0);
    }

    // copy out head line
    int idx    = tb->head;
    int length = strlen(tb->lines[idx]);
    int n      = (length < max) ? length : max;
    memcpy(buf, tb->lines[idx], n);
    buf[n] = '\0';

    tb->head  = (tb->head + 1) % READ_QUEUE_SIZE;
    tb->count--;

    args->arg2 = (void*)(long)n;
    args->arg4 = (void*)0;
}

static int termDriver(void *arg) {
    int unit   = (int)(long) arg;
    int status;

    while (1) {
        // 1) Wait for any terminal interrupt
        waitDevice(USLOSS_TERM_INT, unit, &status);

        // 2) Ack & read the status register
        USLOSS_DeviceInput(USLOSS_TERM_DEV, unit, &status);

        // -- RECEIVE side: buffer input into lines[], flush on '\n', EOF, or full-line --
        if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_READY) {
            char    c  = USLOSS_TERM_STAT_CHAR(status);
            TermBuf *tb = &termBuf[unit];
            int     pos = strlen(tb->lines[tb->tail]);

            if (c != 0 && pos < MAX_LINE_LEN) {
                tb->lines[tb->tail][pos]   = c;
                tb->lines[tb->tail][pos+1] = '\0';
                pos++;
            }
            if (c == '\n' || c == 0 || pos == MAX_LINE_LEN) {
                if (tb->count < READ_QUEUE_SIZE) {
                    tb->count++;
                } else {
                    tb->head = (tb->head + 1) % READ_QUEUE_SIZE;
                }
                tb->tail = (tb->tail + 1) % READ_QUEUE_SIZE;
                tb->lines[tb->tail][0] = '\0';
                MboxSend(termReadMail[unit], NULL, 0);
            }
        }

        if (USLOSS_TERM_STAT_XMIT(status) == USLOSS_DEV_READY) {
            // first, re-enable both receive and transmit interrupts
            int ctrl = 0;
            ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
            ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
            USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)ctrl);
        
            // if a writer is actually blocked
            if (blockedWriters[unit] > 0) {
                MboxCondSend(termXmitMail[unit], NULL, 0);
            }
        } else {
            // any other interrupt (receive), just re-arm interrupts
            int ctrl = 0;
            ctrl = USLOSS_TERM_CTRL_RECV_INT(ctrl);
            ctrl = USLOSS_TERM_CTRL_XMIT_INT(ctrl);
            USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void*)(long)ctrl);
        }
        
    }
    // never reached
    return 0;
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
        MboxSend(diskMail, NULL, 0);
    }
    MboxSend(diskLock, NULL, 0);
}

static void sys_diskread(USLOSS_Sysargs *args) {
    DiskRequest req;

    req.unit       = (int)(long) args->arg5;
    req.track      = (int)(long) args->arg3;
    req.firstBlock = (int)(long) args->arg4;
    req.numBlocks  = (int)(long) args->arg2;
    req.buffer     =  args->arg1;
    req.isRead     =  1;
    req.pid        =  getpid();

    enqueue_disk_request(&req);
    blockMe();                         // wait until diskDriver calls unblockProc()
    args->arg4 = (void*)(long)0;       // return code = 0
    args->arg1 = (void*)(long)0;
}

static void sys_diskwrite(USLOSS_Sysargs *args) {
    int unit       = (int)(long) args->arg5;
    int track      = (int)(long) args->arg3;
    int firstBlock = (int)(long) args->arg4;
    int numBlocks  = (int)(long) args->arg2;
    void *buffer   =  args->arg1;
    
    // 1) Reject invalid unit or start-sector immediately
    if (unit < 0 || unit >= USLOSS_DISK_UNITS ||
        firstBlock < 0 || firstBlock >= USLOSS_DISK_TRACK_SIZE ||
            numBlocks <= 0) {
        args->arg4 = (void*)(long)-1;
        return;
    }
    
    // 2) Build the request and clear its status slot
    DiskRequest req = { unit, track, firstBlock, numBlocks, buffer, 0, getpid() };
    diskStatus[req.pid] = 0;
    
    enqueue_disk_request(&req);
    blockMe();      // wakes once diskDriver calls unblockProc()
    
    // 3) Return code 0 (I/O was attempted), status = what the driver recorded
    args->arg4 = (void*)(long)0;
    args->arg1 = (void*)(long)diskStatus[req.pid];
}

//-----------------------------------------------------------------------------
// diskDriver – service disk requests in C‐SCAN order, using USLOSS_DeviceRequest
//-----------------------------------------------------------------------------
static int diskDriver(void *arg) {
    int status;
    int currentTrack = 0;
    const int trackSize = USLOSS_DISK_TRACK_SIZE;  // sectors per track

    while (1) {
        // 1) Wait for a disk request to arrive
        MboxRecv(diskMail, NULL, 0);
        MboxRecv(diskLock, NULL, 0);

        // 2) Select request using C-SCAN scheduling
        int bestIdx   = -1;
        int bestTrack = INT_MAX;
        // First look for the smallest track ≥ currentTrack
        for (int i = 0; i < dqCount; i++) {
            int idx   = (dqHead + i) % MAX_DISK_REQUESTS;
            int track = diskQueue[idx].track;
            if (track >= currentTrack && track < bestTrack) {
                bestTrack = track;
                bestIdx   = idx;
            }
        }
        // If none found, wrap around and take the smallest track
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

        // 3) Dequeue the selected request
        DiskRequest req = diskQueue[(dqHead + bestIdx) % MAX_DISK_REQUESTS];
        for (int j = bestIdx; j < dqCount - 1; j++) {
            int src = (dqHead + j + 1) % MAX_DISK_REQUESTS;
            int dst = (dqHead + j)     % MAX_DISK_REQUESTS;
            diskQueue[dst] = diskQueue[src];
        }
        dqCount--;
        MboxSend(diskLock, NULL, 0);

        // 4) Initialize this process’s status slot
        diskStatus[req.pid] = USLOSS_DEV_OK;

        // 5) For each block, compute its absolute track/sector, seek if needed, then R/W
        int lastTrack = -1;
        for (int i = 0; i < req.numBlocks && diskStatus[req.pid] == USLOSS_DEV_OK; i++) {
            int absBlock = req.firstBlock + i;
            int track    = req.track + absBlock / trackSize;
            int sector   = absBlock % trackSize;

            // SEEK if crossing into a new track
            if (track != lastTrack) {
                USLOSS_DeviceRequest dr;
                dr.opr  = USLOSS_DISK_SEEK;
                dr.reg1 = (void*)(long)track;
                dr.reg2 = NULL;
                USLOSS_DeviceOutput(USLOSS_DISK_DEV, req.unit, &dr);
                waitDevice(USLOSS_DISK_DEV, req.unit, &status);
                if (status != USLOSS_DEV_OK) {
                    diskStatus[req.pid] = USLOSS_DEV_ERROR;
                    break;
                }
                lastTrack = track;
            }

            // READ or WRITE the sector
            USLOSS_DeviceRequest dr;
            dr.opr  = req.isRead ? USLOSS_DISK_READ : USLOSS_DISK_WRITE;
            dr.reg1 = (void*)(long)sector;
            dr.reg2 = (void*)((char*)req.buffer + i * USLOSS_DISK_SECTOR_SIZE);
            USLOSS_DeviceOutput(USLOSS_DISK_DEV, req.unit, &dr);
            waitDevice(USLOSS_DISK_DEV, req.unit, &status);
            if (status != USLOSS_DEV_OK) {
                diskStatus[req.pid] = USLOSS_DEV_ERROR;
                break;
            }
        }

        // 6) Wake the requesting process, it will check diskStatus[pid]
        unblockProc(req.pid);

        // 7) Update scan position
        if (lastTrack >= 0) {
            currentTrack = lastTrack;
        }
    }

    // never reached
    return 0;
}

static int dummyproc(char *arg) {
    Terminate(0);   // immediately exit
    return 0;       // never reached
}