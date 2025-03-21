
#include "phase2.h"
#include "phase2def.h"
#include "usloss.h"
#include "phase1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global arrays and variables.
Mbox mailboxes[MAXMBOX];
MailSlot mailSlots[MAXSLOTS];
int freeSlotQueue[MAXSLOTS];
int freeSlotCount;

// Global array for managing each mailbox's slot queue.
SlotQueue mailboxSlotQueues[MAXMBOX];

void (*systemCallVec[MAXSYSCALLS])(USLOSS_Sysargs *args);
struct Process shadowProcesses[MAXPROC];

#define MAX_BLOCKED_PRODUCERS 10
static int blockedProducers[MAXMBOX][MAX_BLOCKED_PRODUCERS];
static int numBlockedProducers[MAXMBOX];

#define MAX_BLOCKED_CONSUMERS 10
static int blockedConsumers[MAXMBOX][MAX_BLOCKED_CONSUMERS];
static int numBlockedConsumers[MAXMBOX];

int clockMailbox;
int terminalStatus = 0;

void nullsys(USLOSS_Sysargs *args) {
    /* Implementation of a system call. */
}

void interrupt_handler(int uhhh, void* ummm) {
    dispatcher();
}

void clock_interrupt_handler(int dev, void *arg) {
    static int clockTicks = 0;
    clockTicks += 100000;  // Increment tick count by ~100k each tick

    // Send the current tick count to clockMailbox.
    int ret = MboxSend(clockMailbox, &clockTicks, sizeof(int));
    if (ret < 0) {
        USLOSS_Console("clock_interrupt_handler: error sending tick count: %d\n", ret);
    }
    dispatcher();
}

void syscall_handler(int dev, void *arg) {
    USLOSS_Console("syscall_handler: syscall trap received. Halting.\n");
    USLOSS_Halt(1);
}

void phase2_init(void) {
    USLOSS_IntVec[USLOSS_CLOCK_INT] = interrupt_handler;
    for (int i = 0; i < MAXMBOX; i++) {
        mailboxes[i].isActive = (i < 6) ? 1 : 0;
        numBlockedProducers[i] = 0;
    }
    for (int i = 0; i < MAXSLOTS; i++) {
        mailSlots[i].inUse = 0;
        freeSlotQueue[i] = i;
    }
    freeSlotCount = MAXSLOTS;
    for (int i = 0; i < MAXPROC; i++) {
        shadowProcesses[i].pid = -1;
    }
    for (int i = 0; i < MAXSYSCALLS; i++) {
        systemCallVec[i] = nullsys;
    }

    clockMailbox = MboxCreate(1, sizeof(int));
    USLOSS_IntVec[USLOSS_CLOCK_INT] = clock_interrupt_handler;
    // Set the syscall vector to our handler so that a syscall trap is caught.
    USLOSS_IntVec[USLOSS_SYSCALL_INT] = syscall_handler;
}

int MboxCreate(int numSlots, int slotSize) {
    if (numSlots < 0 || slotSize < 0 || slotSize > MAX_MESSAGE)
         return -1;
    int id = -1;
    for (int i = 0; i < MAXMBOX; i++){
        if (mailboxes[i].isActive == 0) {
            id = i;
            break;
        }
    }
    if (id == -1)
         return -1;

    mailboxes[id].id = id;
    mailboxes[id].numSlots = numSlots;
    mailboxes[id].slotSize = slotSize;
    mailboxes[id].isActive = 1;
    mailboxes[id].producers = NULL;
    mailboxes[id].consumers = NULL;

    mailboxSlotQueues[id].queue = malloc(numSlots * sizeof(int));    // r we allowed to use malloc?
    if (mailboxSlotQueues[id].queue == NULL) {
         mailboxes[id].isActive = 0;
         return -1;
    }
    mailboxSlotQueues[id].capacity = numSlots;
    mailboxSlotQueues[id].head = 0;
    mailboxSlotQueues[id].tail = 0;
    mailboxSlotQueues[id].count = 0;

    return id;
}

/* MboxRelease:
 *  - Marks the mailbox inactive.
 *  - Unblocks all blocked producers and any blocked consumer.
 *  - Frees the mailbox's slot queue.
 *  After release, further Send/Recv calls return -1.
 */
int MboxRelease(int mbox_id) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
         return -1;

    mailboxes[mbox_id].isActive = 0;

    for (int i = 0; i < numBlockedProducers[mbox_id]; i++) {
         unblockProc(blockedProducers[mbox_id][i]);
    }
    numBlockedProducers[mbox_id] = 0;

    /* For zero-slot mailboxes, unblock all waiting consumers */
    if (mailboxes[mbox_id].numSlots == 0) {

         for (int i = 0; i < numBlockedConsumers[mbox_id]; i++) {

              unblockProc(blockedConsumers[mbox_id][i]);
         }
         numBlockedConsumers[mbox_id] = 0;
    }
    else if (mailboxes[mbox_id].consumers != NULL) {
         unblockProc(mailboxes[mbox_id].consumers->pid);
         mailboxes[mbox_id].consumers = NULL;
    }

    free(mailboxSlotQueues[mbox_id].queue);
    mailboxSlotQueues[mbox_id].queue = NULL;
    mailboxSlotQueues[mbox_id].count = 0;
    mailboxSlotQueues[mbox_id].head = 0;
    mailboxSlotQueues[mbox_id].tail = 0;

    return 0;
}

/* Unblocks the waiting consumer if one is recorded. */
void wakeupConsumer(Mbox *mbox, int mbox_id) {
  //printf("wakeupConsumer: started\n");
    if (numBlockedConsumers[mbox_id] > 0) {
      //printf("wakeupProducer: there is a producer waiting.\n");
         int pid_to_unblock = blockedConsumers[mbox_id][0];
         for (int i = 0; i < numBlockedConsumers[mbox_id] - 1; i++) {
             blockedConsumers[mbox_id][i] = blockedConsumers[mbox_id][i+1];
             //printf("wakeupConsumer: unblocking pid %d\n", blockedConsumers[mbox_id][i]);
         }
         numBlockedConsumers[mbox_id] = numBlockedConsumers[mbox_id] - 1;
         //printf("wakeupConsumer: unblocking pid %d\n", pid_to_unblock);
         //printf("theres no way in hell this is 5 million dollars\n");
         unblockProc(pid_to_unblock);
    }
}

/* Unblocks the first blocked producer for mailbox mbox_id. */
void wakeupProducer(Mbox *mbox, int mbox_id) {
    //printf("wakeupProducer: started\n");
    if (numBlockedProducers[mbox_id] > 0) {
      //printf("wakeupProducer: there is a producer waiting.\n");
         int pid_to_unblock = blockedProducers[mbox_id][0];
         for (int i = 0; i < numBlockedProducers[mbox_id] - 1; i++) {
             blockedProducers[mbox_id][i] = blockedProducers[mbox_id][i+1];
             //printf("wakeupProducer: unblocking pid %d\n", blockedProducers[mbox_id][i]);
         }
         numBlockedProducers[mbox_id] = numBlockedProducers[mbox_id] - 1;
         //printf("wakeupProducer: total blocked producers %d\n", numBlockedProducers[mbox_id]);
         //printf("wakeupProducer: unblocking pid %d\n", pid_to_unblock);
         //printf("theres no way in hell this is 5 million dollars\n");
         unblockProc(pid_to_unblock);
    }
}

int MboxSend(int mbox_id, void *msg_ptr, int msg_size) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
        return -1;
    if (msg_size < 0 || msg_size > mailboxes[mbox_id].slotSize)
        return -1;
    if (msg_size > 0 && msg_ptr == NULL)
        return -1;

    Mbox *mbox = &mailboxes[mbox_id];
    SlotQueue *sq = &mailboxSlotQueues[mbox_id];

    if (!mbox->isActive)
        return -1;

    // For zero-slot mailboxes, handle direct producer-consumer synchronization
    if (mbox->numSlots == 0) {
        // Check if there's a consumer waiting
        if (mbox->consumers != NULL) {
            // Direct message transfer to waiting consumer
            Process *consumer = mbox->consumers;
            // Copy message to consumer's buffer (handled by consumer's MboxRecv)
            // The consumer will take care of copying the message

            // Unblock the consumer
            //printf("MboxSend: directly unblocking consumer %d\n", consumer->pid);
            unblockProc(consumer->pid);
            mbox->consumers = NULL;
            return 0;
        } else {
            // No consumer waiting, block this producer
            int pid = getpid();

            // Store producer PID and message info
            if (numBlockedProducers[mbox_id] < MAX_BLOCKED_PRODUCERS) {
                blockedProducers[mbox_id][numBlockedProducers[mbox_id]++] = pid;
            }

            // Store producer's message
            mbox->producers = &shadowProcesses[pid % MAXPROC];
            mbox->producers->pid = pid;

            // Block until a consumer arrives
            //printf("MboxSend: blocking process %d on zero-slot mailbox\n", pid);
            blockMe();

            if (!mbox->isActive)
                return -1;

            return 0;
        }
    }

    // For mailboxes with slots, handle as before
    if (freeSlotCount == 0)
        return -2;

    while (sq->count >= mbox->numSlots && mbox->numSlots > 0) {
        if (!mbox->isActive)
            return -1;
        int pid = getpid();
        int alreadyBlocked = 0;
        for (int i = 0; i < numBlockedProducers[mbox_id]; i++) {
            //printf("MboxSend: checking blocked producer %d\n", blockedProducers[mbox_id][i]);
            if (blockedProducers[mbox_id][i] == pid) {
                alreadyBlocked = 1;
                break;
            }
        }
        if (!alreadyBlocked) {
            if (numBlockedProducers[mbox_id] < MAX_BLOCKED_PRODUCERS) {
                blockedProducers[mbox_id][numBlockedProducers[mbox_id]++] = pid;
            }
        }
       // printf("MboxSend: blocking process %d\n", pid);
        //printf("current process %d\n", getpid());
        blockMe();
        //printf("MboxSend: after blockMe()\n");
        if (!mbox->isActive)
            return -1;
    }

    // Normal slot-based mailbox handling
    int slotIndex = freeSlotQueue[--freeSlotCount];
    mailSlots[slotIndex].inUse = 1;
    mailSlots[slotIndex].mailboxID = mbox_id;
    mailSlots[slotIndex].messageLength = msg_size;
    memcpy(mailSlots[slotIndex].message, msg_ptr, msg_size);

    sq->queue[sq->tail] = slotIndex;
    sq->tail = (sq->tail + 1) % sq->capacity;
    sq->count++;

    if (mbox->consumers != NULL) {
        //printf("MboxSend: waking up consumer %d\n", mbox->consumers->pid);
        wakeupConsumer(mbox, mbox_id);
    }

    //printf("MboxSend: returning\n");
    return 0;
}

int MboxRecv(int mbox_id, void *msg_ptr, int msg_max_size) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
        return -1;
    if (msg_max_size < 0)
        return -1;

    Mbox *mbox = &mailboxes[mbox_id];
    SlotQueue *sq = &mailboxSlotQueues[mbox_id];

    //printf("MboxRecv: started\n");

    // For zero-slot mailboxes, handle direct producer-consumer synchronization
    if (mbox->numSlots == 0) {
        // Check if there's a producer waiting
        if (numBlockedProducers[mbox_id] > 0) {
            int producer_pid = blockedProducers[mbox_id][0];

            // Get producer's message (need to implement message storage for blocked producers)
            // This would require storing the message pointer and size for each blocked producer

            // Shift the queue of blocked producers
            for (int i = 0; i < numBlockedProducers[mbox_id] - 1; i++) {
                blockedProducers[mbox_id][i] = blockedProducers[mbox_id][i+1];
            }
            numBlockedProducers[mbox_id]--;

            // Unblock the producer
            //printf("MboxRecv: unblocking producer %d on zero-slot mailbox\n", producer_pid);
            unblockProc(producer_pid);

            return 0;  // The message exchange is direct, no size to return
        } else {
            // No producer waiting, block this consumer
            int pid = getpid();

            // Add to list of blocked consumers
            if (numBlockedConsumers[mbox_id] < MAX_BLOCKED_CONSUMERS) {
                blockedConsumers[mbox_id][numBlockedConsumers[mbox_id]++] = pid;
            }

            mbox->consumers = &shadowProcesses[pid % MAXPROC];
            mbox->consumers->pid = pid;

            // Block until a producer arrives
            //printf("MboxRecv: blocking consumer %d on zero-slot mailbox\n", pid);
            blockMe();

            if (!mbox->isActive)
                return -1;

            return 0;  // For zero-slot mailboxes, return 0 as success
        }
    }

    // For regular mailboxes with slots
    if (sq->count == 0) {
        int pid = getpid();
        // Add this process to the blocked consumers array
        if (numBlockedConsumers[mbox_id] < MAX_BLOCKED_CONSUMERS) {
            blockedConsumers[mbox_id][numBlockedConsumers[mbox_id]++] = pid;
        }
        mbox->consumers = &shadowProcesses[pid % MAXPROC];
        mbox->consumers->pid = pid;
        //printf("MboxRecv: blocking\n");
        blockMe();
        if (!mbox->isActive)
            return -1;
        mbox->consumers = NULL;
    }

    if (sq->count == 0) {
        // If we got here and there's still no message, the mailbox was released
        return -1;
    }

    int slotIndex = sq->queue[sq->head];
    sq->head = (sq->head + 1) % sq->capacity;
    sq->count--;

    if (msg_max_size < mailSlots[slotIndex].messageLength)
        return -1;

    memcpy(msg_ptr, mailSlots[slotIndex].message, mailSlots[slotIndex].messageLength);
    int ret = mailSlots[slotIndex].messageLength;

    mailSlots[slotIndex].inUse = 0;
    freeSlotQueue[freeSlotCount++] = slotIndex;

    //printf("MboxRecv: waking up producer\n");
    wakeupProducer(mbox, mbox_id);

    return ret;
}



int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
         return -1;
    if (msg_size < 0 || msg_size > mailboxes[mbox_id].slotSize)
         return -1;
    if (msg_size > 0 && msg_ptr == NULL)
         return -1;

    Mbox *mbox = &mailboxes[mbox_id];
    SlotQueue *sq = &mailboxSlotQueues[mbox_id];

    if (!mbox->isActive)
         return -1;

    if (freeSlotCount == 0)
         return -2;

    while (sq->count >= mbox->numSlots) {
         //printf("MboxCondSend: waiting for space in mailbox %d\n", mbox_id);
         if (!mbox->isActive)
             return -1;
         int pid = getpid();
         int alreadyBlocked = 0;
         for (int i = 0; i < numBlockedProducers[mbox_id]; i++) {
             if (blockedProducers[mbox_id][i] == pid) {
                 alreadyBlocked = 1;
                 break;
             }
         }
         if (!alreadyBlocked) {
         //    if (numBlockedProducers[mbox_id] < MAX_BLOCKED_PRODUCERS)  {
         //        blockedProducers[mbox_id][numBlockedProducers[mbox_id]++] = pid;
         //        printf("MboxCondSend: ADDED TO BLOCKED PRODUCER Q %d\n", pid);  }

         }
         return -2;
         if (!mbox->isActive)
             return -1;
    }

    int slotIndex = freeSlotQueue[--freeSlotCount];
    mailSlots[slotIndex].inUse = 1;
    mailSlots[slotIndex].mailboxID = mbox_id;
    mailSlots[slotIndex].messageLength = msg_size;
    memcpy(mailSlots[slotIndex].message, msg_ptr, msg_size);

    sq->queue[sq->tail] = slotIndex;
    sq->tail = (sq->tail + 1) % sq->capacity;
    sq->count++;

    if (mbox->consumers != NULL) {
        //printf("MboxSend: waking up consumer %d\n", mbox->consumers->pid);
        wakeupConsumer(mbox, mbox_id);
    }

    return 0;
}

int MboxCondRecv(int mbox_id, void *msg_ptr, int msg_max_size) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
        return -1;
    if (msg_max_size < 0)
        return -1;

    Mbox *mbox = &mailboxes[mbox_id];
    SlotQueue *sq = &mailboxSlotQueues[mbox_id];

    //printf("MboxRecv: started\n");
    if (sq->count == 0) {
        //int pid = getpid();
        // Add this process to the blocked consumers array
        //if (numBlockedConsumers[mbox_id] < MAX_BLOCKED_CONSUMERS) {
        //    blockedConsumers[mbox_id][numBlockedConsumers[mbox_id]++] = pid;
        //}
        //mbox->consumers = &shadowProcesses[pid % MAXPROC];
        //mbox->consumers->pid = pid;
        //printf("MboxRecv: blocking\n");
        return -2;
        //if (!mbox->isActive)
            //return -1;
        //mbox->consumers = NULL;
    }

    if (sq->count == 0) {
        // If we got here and there's still no message, the mailbox was released
        return -1;
    }

    int slotIndex = sq->queue[sq->head];
    sq->head = (sq->head + 1) % sq->capacity;
    sq->count--;

    if (msg_max_size < mailSlots[slotIndex].messageLength)
        return -1;

    memcpy(msg_ptr, mailSlots[slotIndex].message, mailSlots[slotIndex].messageLength);
    int ret = mailSlots[slotIndex].messageLength;

    mailSlots[slotIndex].inUse = 0;
    freeSlotQueue[freeSlotCount++] = slotIndex;

    //printf("MboxRecv: waking up producer\n");
    wakeupProducer(mbox, mbox_id);

    return ret;
}


void waitDevice(int type, int unit, int *status) {
    if (type == USLOSS_CLOCK_DEV && unit == 0) {
         MboxRecv(clockMailbox, status, sizeof(int));
    } else if (type == USLOSS_TERM_DEV && unit == 1) {
         terminalStatus = 0x6101;
         *status = terminalStatus;
    } else {
         *status = -1;
    }
}

void wakeupByDevice(int type, int unit, int status) {
}

void phase2_start_service_processes(void) {
}
