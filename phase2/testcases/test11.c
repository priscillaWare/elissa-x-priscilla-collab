
/* Creates a 5-slot mailbox. Creates XXp1 that conditionally sends eight hello
 * messages to the mailbox, five of which should succeed and three will return
 * -2.  XXp1 then blocks on a receive on its reply mailbox.
 * Creates XXp3 which receives the five hello messages that should be
 * available from the slots.  XXp3 then sends to XXp1's reply mailbox.
 * Since XXp3 is lower priority than XXp1, XXp1 runs again.
 * XXp1 wakes up from its reply mailbox and sends eight goodbye messages to
 * the mailbox, five of which should succeed and three will return -2.  XXp1
 * then quits.
 * XXp3 should pick up the five good-bye messages from the mailbox and quit.
 */

#include <stdio.h>
#include <string.h>

#include <usloss.h>
#include <phase1.h>
#include <phase2.h>

int XXp1(void *);
int XXp3(void *);
char buf[256];

int mbox_id, reply_mbox;



int start2(void *arg)
{
   int kid_status, kidpid;

   USLOSS_Console("start2(): started\n");

   mbox_id = MboxCreate(5, 50);
   USLOSS_Console("start2(): MboxCreate returned id = %d\n", mbox_id);

   reply_mbox = MboxCreate(0, 50);
   USLOSS_Console("start2(): MboxCreate returned id = %d\n", reply_mbox);

   kidpid = spork("XXp1", XXp1, NULL, 2*USLOSS_MIN_STACK, 1);
   kidpid = spork("XXp3", XXp3, NULL, 2*USLOSS_MIN_STACK, 2);

   kidpid = join(&kid_status);
   USLOSS_Console("start2(): joined with kid %d, status = %d\n", kidpid, kid_status);

   kidpid = join(&kid_status);
   USLOSS_Console("start2(): joined with kid %d, status = %d\n", kidpid, kid_status);

   quit(0);
}


int XXp1(void *arg)
{
   int i, result;
   char buffer[20];

   USLOSS_Console("XXp1(): started\n");

   for (i = 0; i < 8; i++) {
      USLOSS_Console("XXp1(): conditionally sending message #%d to mailbox %d\n", i, mbox_id);
      sprintf(buffer, "hello there, #%d", i);
      result = MboxCondSend(mbox_id, buffer, strlen(buffer)+1);
      USLOSS_Console("XXp1(): after conditional send of message #%d, result = %d\n", i, result);
   }

   MboxRecv(reply_mbox, NULL, 0);

   for (i = 0; i < 8; i++) {
      USLOSS_Console("XXp1(): conditionally sending message #%d to mailbox %d\n", i, mbox_id);
      sprintf(buffer, "good-bye, #%d", i);
      result = MboxCondSend(mbox_id, buffer, strlen(buffer)+1);
      USLOSS_Console("XXp1(): after conditional send of message #%d, result = %d\n", i, result);
   }

   quit(3);
}


int XXp3(void *arg)
{
   char buffer[100];
   int i, result;

   /* BUGFIX: initialize buffers to predictable contents */
   memset(buffer, 'x', sizeof(buffer)-1);
   buffer[sizeof(buffer)-1] = '\0';

   USLOSS_Console("XXp3(): started\n");

   for (i = 0; i < 5; i++) {
      USLOSS_Console("XXp3(): receiving message #%d from mailbox %d\n", i, mbox_id);
      result = MboxRecv(mbox_id, buffer, 100);
      USLOSS_Console("XXp3(): after receipt of message #%d, result = %d   message = '%s'\n", i, result, buffer);
   }

   MboxSend(reply_mbox, NULL, 0);

   /* BUGFIX: initialize buffers to predictable contents */
   memset(buffer, 'x', sizeof(buffer)-1);
   buffer[sizeof(buffer)-1] = '\0';

   for (i = 0; i < 5; i++) {
      USLOSS_Console("XXp3(): receiving message #%d from mailbox %d\n", i, mbox_id);
      result = MboxRecv(mbox_id, buffer, 100);
      USLOSS_Console("XXp3(): after receipt of message #%d, result = %d   message = '%s'\n", i, result, buffer);
   }

   quit(4);
}

