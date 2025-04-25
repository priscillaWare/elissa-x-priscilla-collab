/*
 * phase4.c
 * Phase 4a: Clock and Terminal drivers (Milestone 4a)
 * Uses Phase1 primitives blockMe/unblockProc for Sleep and correct terminal wakeups
 */

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
 
 // Sleep bookkeeping
 typedef struct {
     int pid;
     int wakeCycles;
 } SleepEntry;
 static SleepEntry sleepers[MAX_SLEEPERS];
 static int        numSleepers = 0;
 static int        clockCycles = 0;    // each tick ~100ms
 
 // Terminal input buffering
 typedef struct {
     char lines[READ_QUEUE_SIZE][MAX_LINE_LEN+1];
     int head, tail, count;
 } TermBuf;
 static TermBuf termBuf[MAX_TERMINALS];
 // Track which reader is waiting per terminal unit
 static int pendingReader[MAX_TERMINALS];
 
 // Terminal write locks
 static int termWriteLock[MAX_TERMINALS];  // mailbox IDs
 
 // Forward declarations
 static void clockDriver(void *arg);
 static void termDriver(void *arg);
 static int  sys_sleep(USLOSS_Sysargs *args);
 static int  sys_termread(USLOSS_Sysargs *args);
 static int  sys_termwrite(USLOSS_Sysargs *args);
 
 void phase4_init(void) {
     systemCallVec[SYS_SLEEP]     = sys_sleep;
     systemCallVec[SYS_TERMREAD]  = sys_termread;
     systemCallVec[SYS_TERMWRITE] = sys_termwrite;
 
     // Init terminal buffers, write locks, and pendingReader
     for (int u = 0; u < MAX_TERMINALS; ++u) {
         termBuf[u].head = termBuf[u].tail = termBuf[u].count = 0;
         termBuf[u].lines[0][0] = '\0';
         pendingReader[u] = -1;
         termWriteLock[u] = MboxCreate(1, sizeof(int));
         // one-slot "lock" initialized to 1
         int one = 1;
         MboxSend(termWriteLock[u], &one, sizeof(one));
     }
 }
 
 void phase4_start_service_processes(void) {
     // Spawn clock driver
     spork("clockDriver", clockDriver, NULL, USLOSS_MIN_STACK, 3);
     // Spawn terminal drivers
     for (int unit = 0; unit < MAX_TERMINALS; ++unit) {
         char name[16];
         snprintf(name, sizeof(name), "termDriver%d", unit);
         spork(name, termDriver, (void *)(long)unit, USLOSS_MIN_STACK, 3);
     }
 }
 
 static void clockDriver(void *arg) {
     int status;
     // Enable clock interrupts (100ms tick)
     USLOSS_DeviceOutput(USLOSS_CLOCK_DEV, 0, NULL);
 
     while (1) {
         waitDevice(USLOSS_CLOCK_DEV, 0, &status);
         clockCycles++;
         // Wake any sleepers whose time has come
         for (int i = 0; i < numSleepers; ) {
             if (sleepers[i].wakeCycles <= clockCycles) {
                 unblockProc(sleepers[i].pid);
                 sleepers[i] = sleepers[--numSleepers];
             } else {
                 i++;
             }
         }
     }
 }
 
 static int sys_sleep(USLOSS_Sysargs *args) {
     int seconds = (long)args->arg1;
     if (seconds < 0 || numSleepers >= MAX_SLEEPERS) {
         args->arg4 = (void *)(long)-1;
         return 0;
     }
     int pid = getpid();
     sleepers[numSleepers++] = (SleepEntry){ pid, clockCycles + seconds * 10 };
     blockMe();
     args->arg4 = (void *)0;
     return 0;
 }
 
 static int sys_termread(USLOSS_Sysargs *args) {
     int unit  = (long)args->arg3;
     char *buf = args->arg1;
     int   size = (long)args->arg2;
     if (unit < 0 || unit >= MAX_TERMINALS || size <= 0) {
         args->arg4 = (void *)(long)-1;
         return 0;
     }
     // Save caller pid to wake up later
     pendingReader[unit] = getpid();
     // Wait for termDriver to complete a line
     while (termBuf[unit].count == 0) {
         blockMe();
     }
     // Dequeue one line
     TermBuf *tb = &termBuf[unit];
     int idx = tb->head;
     int len = strlen(tb->lines[idx]);
     int n = len < size ? len : size;
     memcpy(buf, tb->lines[idx], n);
     args->arg2 = (void *)(long)n;
     tb->head = (tb->head + 1) % READ_QUEUE_SIZE;
     tb->count--;
     args->arg4 = (void *)0;
     return 0;
 }
 
 static int sys_termwrite(USLOSS_Sysargs *args) {
     int unit  = (long)args->arg3;
     char *buf = args->arg1;
     int   len  = (long)args->arg2;
     if (unit < 0 || unit >= MAX_TERMINALS || len < 0) {
         args->arg4 = (void *)(long)-1;
         return 0;
     }
     int lock;
     MboxRecv(termWriteLock[unit], &lock, sizeof(lock));
     for (int i = 0; i < len; ++i) {
         int control = USLOSS_TERM_CTRL_XMIT_CHAR(buf[i])
                       | USLOSS_TERM_CTRL_RECV_INT(1)
                       | USLOSS_TERM_CTRL_XMIT_INT(1);
         USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)control);
         waitDevice(USLOSS_TERM_DEV, unit, NULL);
     }
     args->arg2 = (void *)(long)len;
     args->arg4 = (void *)0;
     MboxSend(termWriteLock[unit], &lock, sizeof(lock));
     return 0;
 }
 
 static void termDriver(void *arg) {
     int unit = (int)(long)arg;
     int status;
     int ctrl = USLOSS_TERM_CTRL_RECV_INT(1);
     USLOSS_DeviceOutput(USLOSS_TERM_DEV, unit, (void *)(long)ctrl);
 
     while (1) {
         waitDevice(USLOSS_TERM_DEV, unit, &status);
         if (USLOSS_TERM_STAT_RECV(status) == USLOSS_DEV_BUSY) {
             char c = USLOSS_TERM_STAT_CHAR(status);
             TermBuf *tb = &termBuf[unit];
             strncat(tb->lines[tb->tail], &c, 1);
             int llen = strlen(tb->lines[tb->tail]);
             if (c == '\n' || llen == MAX_LINE_LEN) {
                 tb->count = tb->count < READ_QUEUE_SIZE ? tb->count+1 : READ_QUEUE_SIZE;
                 tb->tail  = (tb->tail + 1) % READ_QUEUE_SIZE;
                 tb->lines[tb->tail][0] = '\0';
                 // Wake the blocked reader
                 if (pendingReader[unit] != -1) {
                     unblockProc(pendingReader[unit]);
                     pendingReader[unit] = -1;
                 }
             }
         }
     }
 }