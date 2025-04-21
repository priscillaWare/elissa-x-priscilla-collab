// phase4a.c
// Milestone 4a: Sleep, TermRead, TermWrite syscall handlers

#include <usyscall.h>      // for SYS_SLEEP, SYS_TERMREAD, SYS_TERMWRITE
#include "phase1.h"        // spork, getpid, blockMe, unblockProc
#include "phase2.h"        // MboxCreate, MboxSend, MboxRecv, waitDevice
#include <usloss.h>        // USLOSS_DeviceOutput, USLOSS_DeviceInput, USLOSS_TERM_STAT_CHAR
#include "phase4_usermode.h"
#include <stdlib.h>
#include <string.h>

#define TICKS_PER_SEC (200 / USLOSS_CLOCK_MS)
#define MAX_TERMS     4

// If missing, define these:
#ifndef USLOSS_DEV_CMD_READ
#define USLOSS_DEV_CMD_READ  0
#endif
#ifndef USLOSS_DEV_CMD_WRITE
#define USLOSS_DEV_CMD_WRITE 1
#endif

// Sleep request struct
typedef struct {
    int pid;
    int wakeupTick;
} SleepReq;

static int  clockMbox;
static int  clockPid;
static int  tickCount;

// Forward declarations of syscall handlers and driver
static int  ClockDriver(void *arg);
static void sys_sleep(USLOSS_Sysargs *args);
static void sys_term_read(USLOSS_Sysargs *args);
static void sys_term_write(USLOSS_Sysargs *args);

// Syscall table registration (called in startup)
extern void (*systemCallVec[])(USLOSS_Sysargs *);

void phase4_init(void) {
    systemCallVec[SYS_SLEEP]      = sys_sleep;
    //systemCallVec[SYS_TERMREAD]   = sys_term_read;
    //systemCallVec[SYS_TERMWRITE]  = sys_term_write;
}

// Spawn the single clock driver once startProcesses() has run
void phase4_start_service_processes(void) {
    clockMbox = MboxCreate(100, sizeof(SleepReq));
    // in your initialization code, after creating clockMbox:
    spork("ClockDriver", ClockDriver, NULL, USLOSS_MIN_STACK, /* high priority */ 2);
}

// Clock driver: waits 100ms ticks, updates tickCount, unblocks sleepers
static int ClockDriver(void *arg) {
    int status;
    SleepReq req;
    while (1) {
        waitDevice(USLOSS_CLOCK_DEV, 0, &status);
        tickCount++;
        int rc;
        while ((rc = MboxCondRecv(clockMbox, &req, sizeof(req))) > 0) {
            if (req.wakeupTick <= tickCount) {
                unblockProc(req.pid);
            } else {
                // not yet due, put it back and stop scanning
                MboxSend(clockMbox, &req, sizeof(req));
                break;
            }
        }
    }
    return 0;  // never reached
}

// Sleep syscall: enqueue and block
static void sys_sleep(USLOSS_Sysargs *args) {
    int seconds = (int)(long) args->arg1;
    if (seconds < 0) {
        args->arg4 = (void*)(long)-1;
        return;
    }
    SleepReq req = { getpid(), tickCount + seconds * TICKS_PER_SEC };
    MboxSend(clockMbox, &req, sizeof(req));
    blockMe();
    args->arg4 = (void*)(long)0;
}

//----------------------------------------
// syscall handler: TermRead
//----------------------------------------
// static void sys_term_read(USLOSS_Sysargs *args) {
//     char *buffer = (char *) args->arg1;
//     int   bufSize = (int)(long) args->arg2;
//     int   unit    = (int)(long) args->arg3;
//     int   num;

//     int rc = TermReadKernel(buffer, bufSize, unit, &num);

//     args->arg2 = (void*)(long) num;
//     args->arg4 = (void*)(long) rc;
// }

// //----------------------------------------
// // syscall handler: TermWrite
// //----------------------------------------
// static void sys_term_write(USLOSS_Sysargs *args) {
//     char *buf    = (char *) args->arg1;
//     int   size   = (int)(long) args->arg2;
//     int   unit   = (int)(long) args->arg3;
//     int  *lenOut = (int *) args->arg4;

//     int rc = TermWriteKernel(buf, size, unit, lenOut);

//     args->arg2 = (void*)(long) *lenOut;
//     args->arg4 = (void*)(long) rc;
// }

// //----------------------------------------
// // Helpers for TermRead
// //----------------------------------------
// static int TermReadKernel(char *buffer, int bufSize, int unit, int *lenOut) {
//     int status, count = 0;
//     if (!buffer || bufSize <= 0 || unit < 0 || unit >= MAX_TERMS) {
//         if (lenOut) *lenOut = 0;
//         return -1;
//     }
//     while (count < bufSize) {
//         waitDevice(USLOSS_TERM_DEV, unit, &status);
//         if (status != USLOSS_DEV_OK) continue;
//         char c = (char) USLOSS_TERM_STAT_CHAR(status);
//         buffer[count++] = c;
//         if (c == '\n') break;
//     }
//     if (lenOut) *lenOut = count;
//     return 0;
// }
// static void sys_term_read(USLOSS_Sysargs *args);

// //----------------------------------------
// // Helpers for TermWrite
// //----------------------------------------
// static int TermWriteKernel(char *buffer, int bufSize, int unit, int *lenOut) {
//     int status;
//     if (!buffer || bufSize < 0 || unit < 0 || unit >= MAX_TERMS) {
//         if (lenOut) *lenOut = 0;
//         return -1;
//     }
//     for (int i = 0; i < bufSize; i++) {
//         USLOSS_DeviceRequest devReq = {
//             .opr  = USLOSS_DEV_CMD_WRITE,
//             .reg1 = &buffer[i],
//             .reg2 = NULL
//         };
//         USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, &devReq);
//         waitDevice(USLOSS_TERM_DEV, unit, &status);
//         if (status != USLOSS_DEV_OK) {
//             if (lenOut) *lenOut = i;
//             return status;
//         }
//     }
//     if (lenOut) *lenOut = bufSize;
//     return 0;
// }
