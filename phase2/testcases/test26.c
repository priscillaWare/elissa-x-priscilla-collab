/* start2 creates a 5 slot mailbox, then spork's XXp3 at priority 5.
 * XXp3 sends 5 messages to the mailbox (filling all the slots); XXp3 then
 * creates three instances of XXp2 running at priorities 4, 3, 2 respectively.
 * Each instance of XXp2 sends a message to the mailbox (and blocks, since
 * the mailbox slots are all full). This leaves all three instances of XXp2
 * blocked on the mailbox in the order 4, 3, 2.
 * XXp3 then spork's XXp1 running at priority 1. XXp1 receives 8 messages,
 * printing the contents of each.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <usloss.h>
#include <phase1.h>
#include <phase2.h>

int XXp1(void *);
int XXp2(void *);
int XXp3(void *);

int mbox_id;



int start2(void *arg)
{
    int kid_status, kidpid;

    USLOSS_Console("start2(): started\n");

    mbox_id = MboxCreate(5, MAX_MESSAGE);
    USLOSS_Console("start2(): MboxCreate returned id = %d\n", mbox_id);

    kidpid = spork("XXp3", XXp3, NULL, 2 * USLOSS_MIN_STACK, 4);

    kidpid = join(&kid_status);
    USLOSS_Console("start2(): joined with kid %d, status = %d\n", kidpid, kid_status);

    quit(0);
}

int XXp1(void *arg)
{
    int i, result;
    char buffer[MAX_MESSAGE];

    /* BUGFIX: initialize buffers to predictable contents */
    memset(buffer, 'x', sizeof(buffer)-1);
    buffer[sizeof(buffer)-1] = '\0';

    USLOSS_Console("XXp1(): started\n");

    for (i = 1; i <= 8; i++) {
        result = MboxRecv(mbox_id, buffer, MAX_MESSAGE);
        USLOSS_Console("XXp1(): received message #%d rc %d   message '%s'\n", i, result, buffer);
    }

    USLOSS_Console("XXp1(): done sending, now quitting\n\n");

    quit(1);
}

int XXp2(void *arg)
{
    int result = -5;
    int my_priority = (int)(long)arg;

    USLOSS_Console("XXp2(): started\n");

    USLOSS_Console("XXp2(): priority %d, sending message to mailbox %d\n", my_priority, mbox_id);

    switch (my_priority) {
    case 2:
        result = MboxSend(mbox_id, "Eighth message", 15);
        break;
    case 3:
        result = MboxSend(mbox_id, "Seventh message", 16);
        break;
    case 4:
        result = MboxSend(mbox_id, "Sixth message", 14);
        break;
    default:
        USLOSS_Console("XXp2(): problem in switch!!!!\n");
    }

    USLOSS_Console("XXp2(): priority %d, after sending message, result = %d\n", my_priority, result);

    quit(my_priority);
}

int XXp3(void *arg)
{
    int kidpid, status, i;
    int result;

    USLOSS_Console("\nXXp3(): started, about to send 5 messages to the mailbox\n");

    for (i = 1; i <= 5; i++) {
        USLOSS_Console("XXp3(): sending message #%d to mailbox %d\n", i, mbox_id);

        switch (i) {
        case 1:
            result = MboxSend(mbox_id, "First message", 14);
            break;
        case 2:
            result = MboxSend(mbox_id, "Second message", 15);
            break;
        case 3:
            result = MboxSend(mbox_id, "Third message", 14);
            break;
        case 4:
            result = MboxSend(mbox_id, "Fourth message", 15);
            break;
        case 5:
            result = MboxSend(mbox_id, "Fifth message", 14);
            break;
        default:
            USLOSS_Console("XXp3(): problem in switch!!!!\n");
        }

        USLOSS_Console("XXp3(): MboxSend rc %d\n", result);
    }

    USLOSS_Console("\nXXp3(): spork'ing XXp2 at priority 4\n");
    kidpid = spork("XXp2", XXp2, (void*)(long)4, 2 * USLOSS_MIN_STACK, 4);

    USLOSS_Console("\nXXp3(): spork'ing XXp2 at priority 3\n");
    kidpid = spork("XXp2", XXp2, (void*)(long)3, 2 * USLOSS_MIN_STACK, 3);

    USLOSS_Console("\nXXp3(): spork'ing XXp2 at priority 2\n");
    kidpid = spork("XXp2", XXp2, (void*)(long)2, 2 * USLOSS_MIN_STACK, 2);

    USLOSS_Console("\nXXp3(): spork'ing XXp1 at priority 1\n");
    kidpid = spork("XXp1", XXp1, NULL, 2 * USLOSS_MIN_STACK, 1);

    for (i = 0; i < 4; i++) {
        kidpid = join(&status);
        USLOSS_Console("XXp3(): join'd with child %d whose status is %d\n", kidpid, status);
    }

    quit(5);
}

