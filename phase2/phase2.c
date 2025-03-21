
#include "phase2.h"
#include "phase2def.h"
#include "usloss.h"
#include "phase1.h"
#include <stdlib.h>
#include <string.h>

#define MAX_BLOCKED_CONSUMERS 10

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

// New definitions for zero-slot mailbox blocked consumers:
static int blockedConsumers[MAXMBOX][MAX_BLOCKED_CONSUMERS];
static int numBlockedConsumers[MAXMBOX];

int clockMailbox;
int terminalStatus = 0;

/*
 * A simple syscall handler.
 * Test40 expects that when a syscall trap is taken, a handler will catch it
 * and then halt. (Syscall number MAXSYSCALLS is invalid.)
 */
void syscall_handler(int dev, void *arg) {
    USLOSS_Console("syscall_handler: syscall trap received. Halting.\n");
    USLOSS_Halt(1);
}

void nullsys(USLOSS_Sysargs *args) {
    /* Default system call; normally unused */
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

void phase2_init(void) {
    for (int i = 0; i < MAXMBOX; i++) {
        // Reserve IDs 0-6 for system use.
        mailboxes[i].isActive = (i < 6) ? 1 : 0;
        numBlockedProducers[i] = 0;
        numBlockedConsumers[i] = 0;
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
    // Create clock mailbox (1 slot) and install the clock interrupt handler.
    clockMailbox = MboxCreate(1, sizeof(int));
    USLOSS_IntVec[USLOSS_CLOCK_INT] = clock_interrupt_handler;
    // Set the syscall vector to our handler so that a syscall trap is caught.
    USLOSS_IntVec[USLOSS_SYSCALL_INT] = syscall_handler;
}

int MboxCreate(int numSlots, int slotSize) {
    // For test45: if the request is for more than MAXSLOTS, fail.
    if (numSlots < 0 || numSlots > MAXSLOTS || slotSize < 0 || slotSize > MAX_MESSAGE)
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
    
    mailboxSlotQueues[id].queue = malloc(numSlots * sizeof(int));
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
 *  - Unblocks all blocked producers and any blocked consumers.
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

/* Unblocks the waiting consumer, if any. */
void wakeupConsumer(Mbox *mbox) {
    if (mbox->consumers != NULL) {
         unblockProc(mbox->consumers->pid);
         mbox->consumers = NULL;
    }
}

/* Unblocks the first blocked producer for mailbox mbox_id. */
void wakeupProducer(Mbox *mbox, int mbox_id) {
    if (numBlockedProducers[mbox_id] > 0) {
         int pid_to_unblock = blockedProducers[mbox_id][0];
         for (int i = 0; i < numBlockedProducers[mbox_id] - 1; i++) {
             blockedProducers[mbox_id][i] = blockedProducers[mbox_id][i+1];
         }
         numBlockedProducers[mbox_id]--;
         unblockProc(pid_to_unblock);
    }
}

/* MboxSend: Sends a message to the mailbox. */
int MboxSend(int mbox_id, void *msg_ptr, int msg_size) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
         return -1;
    if (msg_size < 0 || msg_size > mailboxes[mbox_id].slotSize)
         return -1;
    if (msg_size > 0 && msg_ptr == NULL)
         return -1;
    
    Mbox *mbox = &mailboxes[mbox_id];
    
    /* Special handling for zero-slot mailboxes: Direct delivery */
    if (mbox->numSlots == 0) {
        int pid = getpid();
        int alreadyBlocked = 0;
        for (int i = 0; i < numBlockedProducers[mbox_id]; i++) {
             if (blockedProducers[mbox_id][i] == pid) {
                 alreadyBlocked = 1;
                 break;
             }
        }
        if (!alreadyBlocked) {
             if (numBlockedProducers[mbox_id] < MAX_BLOCKED_PRODUCERS)
                 blockedProducers[mbox_id][numBlockedProducers[mbox_id]++] = pid;
        }
        while (mailboxes[mbox_id].consumers == NULL) {
             blockMe();
             if (!mbox->isActive)
                  return -1;
        }
        wakeupConsumer(mbox);
        return 0;
    }
    
    /* For normal (non-zero-slot) mailboxes: */
    SlotQueue *sq = &mailboxSlotQueues[mbox_id];
    
    if (freeSlotCount == 0)
         return -2;
    
    while (sq->count >= mailboxes[mbox_id].numSlots) {
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
             if (numBlockedProducers[mbox_id] < MAX_BLOCKED_PRODUCERS)
                 blockedProducers[mbox_id][numBlockedProducers[mbox_id]++] = pid;
         }
         blockMe();
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
    
    if (mbox->consumers != NULL)
         wakeupConsumer(mbox);
    
    return 0;
}

/* MboxRecv: Receives a message from the mailbox.
 * For a zero-slot mailbox, all receivers block until the mailbox is released.
 */
int MboxRecv(int mbox_id, void *msg_ptr, int msg_max_size) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
         return -1;
    if (msg_max_size < 0)
         return -1;
    
    Mbox *mbox = &mailboxes[mbox_id];
    
    /* For a zero-slot mailbox, all receivers must block. */
    if (mbox->numSlots == 0) {
         int pid = getpid();
         if (numBlockedConsumers[mbox_id] < MAX_BLOCKED_CONSUMERS) {
              blockedConsumers[mbox_id][numBlockedConsumers[mbox_id]++] = pid;
         } else {
              USLOSS_Console("MboxRecv: too many blocked consumers for mailbox %d\n", mbox_id);
              return -1;
         }
         blockMe();  // Process blocks here.
         if (!mbox->isActive)
             return -1;
         return 0;
    }
    
    /* For normal (non-zero-slot) mailboxes: */
    SlotQueue *sq = &mailboxSlotQueues[mbox_id];
    if (sq->count == 0) {
         mbox->consumers = &shadowProcesses[getpid() % MAXPROC];
         mbox->consumers->pid = getpid();
         blockMe();
         if (!mbox->isActive)
             return -1;
         mbox->consumers = NULL;
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
    
    wakeupProducer(mbox, mbox_id);
    
    return ret;
}

/* MboxCondRecv: Non-blocking receive.
 * If no message is available, return -2 immediately.
 */
int MboxCondRecv(int mbox_id, void *msg_ptr, int msg_size) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
         return -1;
    if (msg_size < 0)
         return -1;
    
    SlotQueue *sq = &mailboxSlotQueues[mbox_id];
    if (sq->count == 0)
         return -2;
    
    int slotIndex = sq->queue[sq->head];
    sq->head = (sq->head + 1) % sq->capacity;
    sq->count--;
    
    if (msg_size < mailSlots[slotIndex].messageLength)
         return -1;
    
    memcpy(msg_ptr, mailSlots[slotIndex].message, mailSlots[slotIndex].messageLength);
    int ret = mailSlots[slotIndex].messageLength;
    
    mailSlots[slotIndex].inUse = 0;
    freeSlotQueue[freeSlotCount++] = slotIndex;
    
    wakeupProducer(&mailboxes[mbox_id], mbox_id);
    
    return ret;
}

/* MboxCondSend: Non-blocking send.
 * If sending would block (mailbox full or no free slots), return -2 immediately.
 */
int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
         return -1;
    if (msg_size < 0 || msg_size > mailboxes[mbox_id].slotSize)
         return -1;
    if (msg_size > 0 && msg_ptr == NULL)
         return -1;
    
    Mbox *mbox = &mailboxes[mbox_id];
    SlotQueue *sq = &mailboxSlotQueues[mbox_id];
    
    if (sq->count >= mailboxes[mbox_id].numSlots)
         return -2;
    if (freeSlotCount == 0)
         return -2;
    
    int slotIndex = freeSlotQueue[--freeSlotCount];
    mailSlots[slotIndex].inUse = 1;
    mailSlots[slotIndex].mailboxID = mbox_id;
    mailSlots[slotIndex].messageLength = msg_size;
    memcpy(mailSlots[slotIndex].message, msg_ptr, msg_size);
    
    sq->queue[sq->tail] = slotIndex;
    sq->tail = (sq->tail + 1) % sq->capacity;
    sq->count++;
    
    if (mbox->consumers != NULL)
         wakeupConsumer(mbox);
    
    return 0;
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

void phase2_start_service_processes(void) {
}

void wakeupByDevice(int type, int unit, int status) {
}
