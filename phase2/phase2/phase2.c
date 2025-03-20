#include "phase2.h"
#include "phase2def.h"
#include "usloss.h"
#include "phase1.h"
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

void nullsys(USLOSS_Sysargs *args) {
    /* Implementation of a system call. */
}

void interrupt_handler(int uhhh, void* ummm) {
    dispatcher();
}

void phase2_init(void) {
    USLOSS_IntVec[USLOSS_CLOCK_INT] = interrupt_handler;
    for (int i = 0; i < MAXMBOX; i++) {
        mailboxes[i].isActive = (i < 7) ? 1 : 0;
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
    
    if (mailboxes[mbox_id].consumers != NULL) {
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
    
    if (freeSlotCount == 0)
         return -2;
    
    while (sq->count >= mbox->numSlots) {
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

int MboxRecv(int mbox_id, void *msg_ptr, int msg_max_size) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
         return -1;
    if (msg_max_size < 0)
         return -1;
    
    Mbox *mbox = &mailboxes[mbox_id];
    SlotQueue *sq = &mailboxSlotQueues[mbox_id];
    
    if (sq->count == 0) {
         int pid = getpid();
         mbox->consumers = &shadowProcesses[pid % MAXPROC];
         mbox->consumers->pid = pid;
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

int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size) {
    return 0;
}

int MboxCondRecv(int mbox_id, void *msg_ptr, int msg_size) { 
    return 0;
}

void waitDevice(int type, int unit, int *status) {
}

void wakeupByDevice(int type, int unit, int status) {
}

void phase2_start_service_processes(void) {
}
