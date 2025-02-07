#define MAXNAME
#define USLOSS_MIN_STACK
#define MAXPROC

struct process {
    int x;  //placeholder
    int y;  //placeholder
    struct process* children;
    struct process* next;
}
