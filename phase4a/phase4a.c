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

#define MAXLINE 80
#define MAX_LINES_BUFFERED 10

typedef struct {
    char lines[MAX_LINES_BUFFERED][MAXLINE + 1];
    int head, tail, count;
    int mutexMbox;
    int lineReadyMbox;
} TermBuffer;

TermBuffer termBuffers[MAX_TERMS];

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
static int TermDriver(void *arg);
static void sys_sleep(USLOSS_Sysargs *args);
static void sys_term_read(USLOSS_Sysargs *args);
static void sys_term_write(USLOSS_Sysargs *args);

// Syscall table registration (called in startup)
extern void (*systemCallVec[])(USLOSS_Sysargs *);

// Spawn the single clock driver once startProcesses() has run
void phase4_start_service_processes(void) {
    clockMbox = MboxCreate(100, sizeof(SleepReq));
    // in your initialization code, after creating clockMbox:
    spork("ClockDriver", ClockDriver, NULL, USLOSS_MIN_STACK, /* high priority */ 2);

    for (int i = 0; i < MAX_TERMS; i++) {
      char name[20];
      sprintf(name, "TermDriver%d", i);
      spork(name, TermDriver, NULL, USLOSS_MIN_STACK, 2);
    }
}

static int TermDriver(void *arg) {
    int unit = (int)(long)arg;
    int status;
    char currLine[MAXLINE + 1];
    int currLen = 0;

    while (1) {
        waitDevice(USLOSS_TERM_DEV, unit, &status);

        int recvChar = USLOSS_TERM_STAT_CHAR(status);  // extract char
        int recvStatus = USLOSS_TERM_STAT_RECV(status); // see if it's valid

        if (recvStatus == USLOSS_DEV_BUSY) {
            // valid character received
            currLine[currLen++] = recvChar;

            if (recvChar == '\n' || currLen == MAXLINE) {
                currLine[currLen] = '\0';

                // enqueue into line buffer
                if (MboxCondSend(termBuffers[unit].lineReadyMbox, currLine, strlen(currLine) + 1) < 0) {
                    // Buffer full — discard oldest? Or just drop this one.
                    // Currently just dropping the line.
                }

                currLen = 0;
            }
        }

        // Optional: else if writing, update write status, etc.
    }

    return 0;  // never reached
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

static void sys_term_read(USLOSS_Sysargs *args) {
    char *userBuf = (char *) args->arg1;
    int userBufSize = (int)(long) args->arg2;
    int unit = (int)(long) args->arg3;

    if (unit < 0 || unit >= MAX_TERMS || userBuf == NULL || userBufSize <= 0) {
        args->arg4 = (void*)(long)-1;
        return;
    }

    char line[MAXLINE + 1];
    int rc = MboxRecv(termBuffers[unit].lineReadyMbox, &line, sizeof(line));
    if (rc < 0) {
        args->arg4 = (void*)(long)-1;
        return;
    }

    int len = strlen(line);
    if (userBufSize < len) len = userBufSize;

    memcpy(userBuf, line, len);
    args->arg2 = (void*)(long)len;
    args->arg4 = (void*)(long)0;
}

static void sys_term_write(USLOSS_Sysargs *args) {
    char *userBuf = (char *) args->arg1;
    int userBufSize = (int)(long) args->arg2;
    int unit = (int)(long) args->arg3;

    if (unit < 0 || unit >= MAX_TERMS || userBuf == NULL || userBufSize <= 0) {
        args->arg4 = (void*)(long)-1;
        return;
    }

    MboxSend(termBuffers[unit].mutexMbox, NULL, 0);

    for (int i = 0; i < userBufSize; i++) {
        int status;
        int ch = userBuf[i];
        USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)ch);
        waitDevice(USLOSS_TERM_DEV, unit, &status);
    }

    MboxRecv(termBuffers[unit].mutexMbox, NULL, 0);
    args->arg2 = (void*)(long)userBufSize;
    args->arg4 = (void*)(long)0;
}

void phase4_init(void) {
    systemCallVec[SYS_SLEEP] = sys_sleep;
    systemCallVec[SYS_TERMREAD] = sys_term_read;
    systemCallVec[SYS_TERMWRITE] = sys_term_write;

    for (int i = 0; i < MAX_TERMS; i++) {
        termBuffers[i].head = termBuffers[i].tail = termBuffers[i].count = 0;
        termBuffers[i].mutexMbox = MboxCreate(1, 0);  // mutual exclusion
        termBuffers[i].lineReadyMbox = MboxCreate(MAX_LINES_BUFFERED, sizeof(char[MAXLINE + 1]));
    }
}
