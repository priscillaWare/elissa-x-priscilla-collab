
/* This test case creates a single mailbox, has 3 high priority processes block
 * on it, and then a lower priority process releases it.  The release SHOULD
 * unblock the other processes.
 */

#include <phase1.h>
#include <phase2.h>
#include <usloss.h>
#include <stdio.h>

int Child1(void *);
int Child2(void *);

int mailbox;



int start2(void *arg)
{
    int kidPid;

    USLOSS_Console("start2(): started.  Creating mailbox.\n");

    mailbox = MboxCreate(0, 0);
    if (mailbox == -1) {
        USLOSS_Console("start2(): got non-zero mailbox result. Quitting...\n");
        quit(1);
    }

    USLOSS_Console("\nstart2(): calling spork for Child1a\n");
    spork("Child1a", Child1, "Child1a", USLOSS_MIN_STACK, 4);

    USLOSS_Console("start2(): calling spork for Child1b\n");
    spork("Child1b", Child1, "Child1b", USLOSS_MIN_STACK, 4);

    USLOSS_Console("start2(): calling spork for Child1c\n");
    spork("Child1c", Child1, "Child1c", USLOSS_MIN_STACK, 4);

    USLOSS_Console("start2(): calling spork for Child2\n");
    spork("Child2", Child2, NULL, USLOSS_MIN_STACK, 2);

    USLOSS_Console("\nstart2(): Parent done sporking.\n");

    int status;

    kidPid = join(&status);
    USLOSS_Console("Process %d joined with status: %d\n\n", kidPid, status);

    kidPid = join(&status);
    USLOSS_Console("Process %d joined with status: %d\n\n", kidPid, status);

    kidPid = join(&status);
    USLOSS_Console("Process %d joined with status: %d\n\n", kidPid, status);

    kidPid = join(&status);
    USLOSS_Console("Process %d joined with status: %d\n\n", kidPid, status);

    quit(0);
}

int Child1(void *arg)
{
    int result;

    USLOSS_Console("\n%s(): starting, blocking on a mailbox receive\n", arg);
    result = MboxRecv(mailbox, NULL, 0);
    USLOSS_Console("%s(): result = %d\n", arg, result);

    return 1;
}

int Child2(void *arg)
{
    USLOSS_Console("\nChild2(): starting, releasing mailbox\n\n");
    MboxRelease(mailbox);
    USLOSS_Console("Child2(): done\n");

    return 9;
}

