#define MAXNAME 50
#define USLOSS_MIN_STACK
#define MAXPROC 50

typedef struct process {
    int pid;
    int priority;
    int status;
    struct process* children;
    struct process* next;
    USLOSS_Context* context; 
} Process;

void phase1_init();
void wrapper();
void spork(char *name, int (*startFunc)(void*), void *arg, int stackSize, int priority);
int join(int *status);
void quit(int status);
void zap(int pid);
int getpid();
void dumpProcesses();
void blockMe();
int unblockProc(int pid);

