/* test releasing a mailbox with a number of blocked senders (all of
 * various higher or equal priorities than the releaser),

 * and then trying to receive
 * and send to the now unused mailbox.
 */

#include <stdio.h>
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>

int XXp1(void *);
int XXp2(void *);
int XXp3(void *);
int XXp4(void *);
char buf[256];
int mbox_id;



int start2(void *arg)
{
  int status;
  int result;
  int kidpid;

  USLOSS_Console("start2(): started\n");

  mbox_id = MboxCreate(1, 13);
  USLOSS_Console("start2(): MboxCreate returned id = %d\n", mbox_id);

  kidpid = spork("XXp1", XXp1, NULL, 2 * USLOSS_MIN_STACK, 5);
  kidpid = spork("XXp2", XXp2, NULL, 2 * USLOSS_MIN_STACK, 4);
  kidpid = spork("XXp3", XXp3, NULL, 2 * USLOSS_MIN_STACK, 3);
  kidpid = spork("XXp4", XXp4, NULL, 2 * USLOSS_MIN_STACK, 5);

  USLOSS_Console("start2(): sending message to mailbox %d\n", mbox_id);
  result = MboxSend(mbox_id, "hello there", 12);
  USLOSS_Console("start2(): after send of message, result = %d\n\n", result);

  join(&status);
  join(&status);
  join(&status);
  join(&status);

  result = kidpid;  // to avoid warning about kidpid set but not used

  quit(0);
}

int XXp1(void *arg)
{
   int result;

   USLOSS_Console("XXp1(): sending message to mailbox %d\n", mbox_id);
   result = MboxSend(mbox_id, "hello there", 12);
   USLOSS_Console("XXp1(): after send of message, result = %d\n", result);

   quit(3);
}

int XXp2(void *arg)
{
   int result;

   USLOSS_Console("XXp2(): sending message to mailbox %d\n", mbox_id);
   result = MboxSend(mbox_id, "hello there", 12);
   USLOSS_Console("XXp2(): after send of message, result = %d\n", result);

   quit(3);
}

int XXp3(void *arg)
{
   int result;

   USLOSS_Console("XXp3(): sending message to mailbox %d\n", mbox_id);
   result = MboxSend(mbox_id, "hello there", 12);
   USLOSS_Console("XXp3(): after send of message, result = %d\n", result);

   quit(3);
}

int XXp4(void *arg)
{
   int result;

   USLOSS_Console("\nXXp4(): Releasing MailBox %d\n", mbox_id);
   result = MboxRelease(mbox_id);
   USLOSS_Console("\nXXp4(): after release of mailbox, result = %d\n", result);

   quit(3);
}

