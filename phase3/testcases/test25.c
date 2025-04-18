/* recursive terminate test & child cleanup*/

#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3_usermode.h>
#include <stdio.h>

int Child1(void *);
int Child2(void *);
int Child2a(void *);
int Child2b(void *);
int Child2c(void *);


int start3(void *arg)
{
    int pid;
    int status;

    USLOSS_Console("start3(): started\n");

    Spawn("Child1", Child1, "Child1", USLOSS_MIN_STACK, 4, &pid);
    USLOSS_Console("start3(): spawned process %d\n", pid);

    Wait(&pid, &status);
    USLOSS_Console("start3(): child %d returned status of %d\n", pid, status);

    USLOSS_Console("start3(): done\n");
    Terminate(0);
}


int Child1(void *arg)
{
    int pid;
    int status;

    GetPID(&pid);
    USLOSS_Console("%s(): starting, pid = %d\n", arg, pid);

    Spawn("Child2", Child2, "Child2", USLOSS_MIN_STACK, 2, &pid);
    USLOSS_Console("%s(): spawned process %d\n", arg, pid);

    Wait(&pid, &status);

    Spawn("Child2b", Child2b, "Child2b", USLOSS_MIN_STACK, 2, &pid);
    USLOSS_Console("%s(): spawned process %d\n", arg, pid);

    Wait(&pid, &status);
    USLOSS_Console("%s(): child %d returned status of %d\n", arg, pid, status);

    USLOSS_Console("%s(): done\n", arg);
    return 9;
}

int Child2(void *arg)
{
    int pid, i;

    GetPID(&pid);
    USLOSS_Console("%s(): starting, pid = %d\n", arg, pid);
    for (i = 0; i != MAXPROC-10; i++){
        Spawn("Child2a", Child2a, "Child2a", USLOSS_MIN_STACK, 3, &pid);
        if (pid >= 0)
            USLOSS_Console("%s(): spawned process %d\n", arg, pid);
        else
            USLOSS_Console("%s(): spawned attempt failed, pid = %d\n", arg, pid);
        }

    DumpProcesses();

    USLOSS_Console("%s(): Terminating self and all my children\n", arg);
    return 10;
}

int Child2a(void *arg)
{
    int pid;

    GetPID(&pid);
    USLOSS_Console("%s(): starting the code for Child2a: pid=%d\n", arg, pid);
    Terminate(11);
    return 0;
}

int Child2b(void *arg)
{
    int pid, status;

    GetPID(&pid);
    USLOSS_Console("%s(): starting, pid = %d\n", arg, pid);

    Spawn("Child2c", Child2c, "Child2c", USLOSS_MIN_STACK, 1, &pid);
    USLOSS_Console("%s(): spawned process %d\n", arg, pid);

    Wait(&pid, &status);

    Terminate(50);
}

int Child2c(void *arg)
{
    USLOSS_Console("%s(): starting the code for Child2c\n", arg);
    Terminate(11);
}

