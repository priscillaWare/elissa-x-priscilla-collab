#include "phase2.h"
#include "phase2def.h"
#include "usloss.h"
#include "phase1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Global data structures and arrays for mailboxes, slots, and process management.
Mbox mailboxes[MAXMBOX];
MailSlot mailSlots[MAXSLOTS];
int freeSlotQueue[MAXSLOTS];
int freeSlotCount;
SlotQueue mailboxSlotQueues[MAXMBOX];

void (*systemCallVec[MAXSYSCALLS])(USLOSS_Sysargs *args);
struct Process shadowProcesses[MAXPROC];
Process processTable[MAXPROC];

#define MAX_BLOCKED_PRODUCERS 10
#define MAX_BLOCKED_CONSUMERS 10
static int blockedProducers[MAXMBOX][MAX_BLOCKED_PRODUCERS];
static int numBlockedProducers[MAXMBOX];
static int blockedConsumers[MAXMBOX][MAX_BLOCKED_CONSUMERS];
static int numBlockedConsumers[MAXMBOX];

#define MAX_SYSCALLS 50
void (*sys_vec[MAX_SYSCALLS])(USLOSS_Sysargs *args);

int clockMailbox;
int terminalStatus = 0;

/**
 * nullsys:
 * Default handler for unimplemented syscalls.
 * Prints error and halts system.
 */
void nullsys(USLOSS_Sysargs *args) {
    USLOSS_Console("nullsys(): Program called an unimplemented syscall. syscall no: %d   PSR: 0x%02x\n",
                   args->number, USLOSS_PsrGet());
    USLOSS_Halt(1);
}

/**
 * init_syscall_table:
 * Initializes the syscall vector with nullsys as default for all entries.
 */
void init_syscall_table() {
    for (int i = 0; i < MAX_SYSCALLS; i++) {
        sys_vec[i] = nullsys;
    }
}

/**
 * syscall_handler:
 * Top-level handler invoked on syscall interrupt.
 * Dispatches to appropriate syscall handler based on syscall number.
 */
void syscall_handler(int type, void *arg) {
    USLOSS_Sysargs *args = (USLOSS_Sysargs *)arg;

    if (args->number < 0 || args->number >= MAX_SYSCALLS || sys_vec[args->number] == NULL) {
        USLOSS_Console("syscallHandler(): Invalid syscall number %d\n", args->number);
        USLOSS_Halt(1);
    }

    sys_vec[args->number](args);
}

/**
 * interrupt_handler:
 * General interrupt handler used to invoke dispatcher.
 */
void interrupt_handler(int type, void *arg) {
    dispatcher();
}

/**
 * clock_interrupt_handler:
 * Handles clock device interrupts.
 * Increments clockTicks and sends current value to clockMailbox.
 */
void clock_interrupt_handler(int dev, void *arg) {
    static int clockTicks = 0;
    clockTicks += 100000;

    int ret = MboxSend(clockMailbox, &clockTicks, sizeof(int));
    if (ret < 0) {
        USLOSS_Console("clock_interrupt_handler: error sending tick count: %d\n", ret);
    }
    dispatcher();
}

/**
 * phase2_init:
 * Initializes Phase 2 by setting up interrupt vectors, syscall table, 
 * mailbox and slot structures, and creates a mailbox for clock ticks.
 */
void phase2_init(void) {
    init_syscall_table();
    sys_vec[0] = nullsys;

    USLOSS_IntVec[USLOSS_SYSCALL_INT] = syscall_handler;
    USLOSS_IntVec[USLOSS_CLOCK_INT] = interrupt_handler;

    for (int i = 0; i < MAXMBOX; i++) {
        mailboxes[i].isActive = (i < 6);
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
}

/**
 * MboxCreate:
 * Initializes a mailbox with a specified number of slots and slot size.
 * Returns a mailbox ID or -1 on error.
 */
int MboxCreate(int numSlots, int slotSize) {
    if (numSlots > MAXSLOTS || numSlots < 0 || slotSize < 0 || slotSize > MAX_MESSAGE)
        return -1;

    int id = -1;
    for (int i = 0; i < MAXMBOX; i++) {
        if (!mailboxes[i].isActive) {
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

/**
 * MboxRelease:
 * Deactivates a mailbox, unblocks any waiting producers/consumers,
 * and frees internal queue memory. Further sends/receives will fail.
 */
int MboxRelease(int mbox_id) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
        return -1;

    mailboxes[mbox_id].isActive = 0;

    for (int i = 0; i < numBlockedProducers[mbox_id]; i++) {
        unblockProc(blockedProducers[mbox_id][i]);
    }
    numBlockedProducers[mbox_id] = 0;

    if (mailboxes[mbox_id].numSlots == 0) {
        for (int i = 0; i < numBlockedConsumers[mbox_id]; i++) {
            unblockProc(blockedConsumers[mbox_id][i]);
        }
        numBlockedConsumers[mbox_id] = 0;
    } else if (mailboxes[mbox_id].consumers != NULL) {
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

/**
 * wakeupConsumer:
 * Unblocks the first waiting consumer for the specified mailbox.
 * Called after a message is placed in a zero-slot mailbox or when space is available.
 */
void wakeupConsumer(Mbox *mbox, int mbox_id) {
    if (numBlockedConsumers[mbox_id] > 0) {
        int pid_to_unblock = blockedConsumers[mbox_id][0];
        for (int i = 0; i < numBlockedConsumers[mbox_id] - 1; i++) {
            blockedConsumers[mbox_id][i] = blockedConsumers[mbox_id][i + 1];
        }
        numBlockedConsumers[mbox_id]--;
        unblockProc(pid_to_unblock);
    }
}

/**
 * wakeupProducer:
 * Unblocks the first waiting producer for the specified mailbox.
 * Called after a message is received and a slot becomes free.
 */
void wakeupProducer(Mbox *mbox, int mbox_id) {
    if (numBlockedProducers[mbox_id] > 0) {
        int pid_to_unblock = blockedProducers[mbox_id][0];
        for (int i = 0; i < numBlockedProducers[mbox_id] - 1; i++) {
            blockedProducers[mbox_id][i] = blockedProducers[mbox_id][i + 1];
        }
        numBlockedProducers[mbox_id]--;
        unblockProc(pid_to_unblock);
    }
}

/**
 * MboxSend:
 * Sends a message to the specified mailbox.
 * If full, blocks the sender (unless zero-slot).
 * If zero-slot and no consumer is waiting, blocks.
 * Returns 0 on success, -1 on failure, or -2 if slot unavailable.
 */
int MboxSend(int mbox_id, void *msg_ptr, int msg_size) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
        return -1;
    if (msg_size < 0 || msg_size > mailboxes[mbox_id].slotSize)
        return -1;
    if (msg_size > 0 && msg_ptr == NULL)
        return -1;

    Mbox *mbox = &mailboxes[mbox_id];
    SlotQueue *sq = &mailboxSlotQueues[mbox_id];

    // Zero-slot mailbox: direct send-receive (synchronous)
    if (mbox->numSlots == 0) {
        if (mbox->consumers != NULL) {
            unblockProc(mbox->consumers->pid);
            mbox->consumers = NULL;
            return 0;
        } else {
            int pid = getpid();
            if (numBlockedProducers[mbox_id] < MAX_BLOCKED_PRODUCERS) {
                blockedProducers[mbox_id][numBlockedProducers[mbox_id]++] = pid;
            }
            mbox->producers = &shadowProcesses[pid % MAXPROC];
            mbox->producers->pid = pid;
            blockMe();

            if (!mbox->isActive)
                return -1;
            return 0;
        }
    }

    // Slot-based mailbox: wait if full
    if (freeSlotCount == 0)
        return -2;

    while (sq->count >= mbox->numSlots && mbox->numSlots > 0) {
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

        if (!alreadyBlocked && numBlockedProducers[mbox_id] < MAX_BLOCKED_PRODUCERS) {
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

    if (mbox->consumers != NULL) {
        wakeupConsumer(mbox, mbox_id);
    }

    return 0;
}

/**
 * MboxRecv:
 * Receives a message from the specified mailbox.
 * If empty, blocks the receiver unless using zero-slot.
 * For zero-slot, blocks if no producer is waiting.
 * Returns number of bytes received, or -1 on error.
 */
int MboxRecv(int mbox_id, void *msg_ptr, int msg_max_size) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
        return -1;
    if (msg_max_size < 0)
        return -1;

    Mbox *mbox = &mailboxes[mbox_id];
    SlotQueue *sq = &mailboxSlotQueues[mbox_id];

    // Zero-slot mailbox: direct receive from blocked producer
    if (mbox->numSlots == 0) {
        if (numBlockedProducers[mbox_id] > 0) {
            int producer_pid = blockedProducers[mbox_id][0];
            for (int i = 0; i < numBlockedProducers[mbox_id] - 1; i++) {
                blockedProducers[mbox_id][i] = blockedProducers[mbox_id][i + 1];
            }
            numBlockedProducers[mbox_id]--;
            unblockProc(producer_pid);
            return 0;
        } else {
            int pid = getpid();
            if (numBlockedConsumers[mbox_id] < MAX_BLOCKED_CONSUMERS) {
                blockedConsumers[mbox_id][numBlockedConsumers[mbox_id]++] = pid;
            }

            mbox->consumers = &shadowProcesses[pid % MAXPROC];
            mbox->consumers->pid = pid;
            blockMe();

            if (!mbox->isActive)
                return -1;
            return 0;
        }
    }

    // Slot-based mailbox
    if (sq->count == 0) {
        int pid = getpid();
        if (numBlockedConsumers[mbox_id] < MAX_BLOCKED_CONSUMERS) {
            blockedConsumers[mbox_id][numBlockedConsumers[mbox_id]++] = pid;
        }
        mbox->consumers = &shadowProcesses[pid % MAXPROC];
        mbox->consumers->pid = pid;
        blockMe();
        if (!mbox->isActive)
            return -1;
        mbox->consumers = NULL;
    }

    if (sq->count == 0)
        return -1;

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

/**
 * MboxCondSend:
 * Sends a message to a mailbox only if space is available.
 * Returns:
 *   0 on success,
 *  -1 on error,
 *  -2 if mailbox is full.
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

    if (!mbox->isActive)
        return -1;
    if (freeSlotCount == 0 || sq->count >= mbox->numSlots)
        return -2;

    int slotIndex = freeSlotQueue[--freeSlotCount];
    mailSlots[slotIndex].inUse = 1;
    mailSlots[slotIndex].mailboxID = mbox_id;
    mailSlots[slotIndex].messageLength = msg_size;
    memcpy(mailSlots[slotIndex].message, msg_ptr, msg_size);

    sq->queue[sq->tail] = slotIndex;
    sq->tail = (sq->tail + 1) % sq->capacity;
    sq->count++;

    if (mbox->consumers != NULL) {
        wakeupConsumer(mbox, mbox_id);
    }

    return 0;
}

/**
 * MboxCondRecv:
 * Receives a message from a mailbox if one is immediately available.
 * Returns:
 *   >0 = bytes copied,
 *   -1 = error,
 *   -2 = no message ready.
 */
int MboxCondRecv(int mbox_id, void *msg_ptr, int msg_max_size) {
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
        return -1;
    if (msg_max_size < 0)
        return -1;

    Mbox *mbox = &mailboxes[mbox_id];
    SlotQueue *sq = &mailboxSlotQueues[mbox_id];

    if (sq->count == 0)
        return -2;

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

/**
 * waitDevice:
 * Blocks the calling process until a device (e.g., clock or terminal) generates an event.
 * Uses mailboxes for clock devices, simulates terminal device here.
 * 
 * Parameters:
 *   - type: device type (USLOSS_CLOCK_DEV or USLOSS_TERM_DEV)
 *   - unit: device unit (0 for clock, 1 for terminal)
 *   - status: pointer to an int where the device status will be stored
 */
void waitDevice(int type, int unit, int *status) {
    if (type == USLOSS_CLOCK_DEV && unit == 0) {
        MboxRecv(clockMailbox, status, sizeof(int));
    } else if (type == USLOSS_TERM_DEV && unit == 1) {
        terminalStatus = 0x6101;  // Simulated terminal status
        *status = terminalStatus;
    } else {
        *status = -1;  // Invalid or unhandled device
    }
}

/**
 * wakeupByDevice:
 * Placeholder for device interrupt handlers to unblock relevant processes.
 * Currently unused.
 */
void wakeupByDevice(int type, int unit, int status) {
    // To be implemented if needed
}

/**
 * phase2_start_service_processes:
 * Placeholder for starting any phase 2-specific service daemons or helper processes.
 */
void phase2_start_service_processes(void) {
    // No service processes defined for now
}
