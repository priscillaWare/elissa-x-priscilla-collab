#include "phase2.h"
#include "usloss.h"
#include "usyscall.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global arrays */
Mbox mailboxes[MAXMBOX];
MailSlot mailSlots[MAXSLOTS];
void (*systemCallVec[MAXSYSCALLS])(int, void*);

//#define MAX_PROCS 100
Process shadowProcesses[MAX_PROC];

/* Forward declaration for our default syscall handler */
static void nullsys(int callNum, void *arg);

/* MboxCreate: Create a new mailbox.
 * Returns the mailbox ID on success, or -1 on error.
 */
int MboxCreate(int numSlots, int slotSize) {
    if (numSlots < 0 || slotSize < 0 || slotSize > MAX_MESSAGE)
        return -1;
    // Find an unused mailbox slot.
    int mboxID = -1;
    for (int i = 0; i < MAXMBOX; i++) {
        if (!mailboxes[i].isActive) {
            mboxID = i;
            break;
        }
    }
    if (mboxID == -1)
        return -1;  // No available mailbox

    mailboxes[mboxID].id = mboxID;
    mailboxes[mboxID].numSlots = numSlots;
    mailboxes[mboxID].slotSize = slotSize;
    mailboxes[mboxID].isActive = 1;
    // Initialize additional fields (e.g., blocked queues) here as needed.
    return mboxID;
}

/* MboxRelease: Destroy a mailbox.
 * Returns 0 on success, or -1 on error.
 */
int MboxRelease(int mailboxID) {
    if (mailboxID < 0 || mailboxID >= MAXMBOX || !mailboxes[mailboxID].isActive)
        return -1;

    // Mark the mailbox as inactive.
    mailboxes[mailboxID].isActive = 0;
    // In a complete implementation, you should also:
    //   - Free any mail slots allocated to this mailbox.
    //   - Unblock any blocked producers/consumers and have them return -1.
    return 0;
}

int MboxSend(int mailboxID, void *message, int messageSize) {
    // Validate parameters.
    if (mailboxID < 0 || mailboxID >= MAXMBOX || !mailboxes[mailboxID].isActive)
        return -1;
    if (messageSize < 0 || messageSize > mailboxes[mailboxID].slotSize)
        return -1;
    if (messageSize > 0 && message == NULL)
        return -1;

    Mbox *mbox = &mailboxes[mailboxID];

    // First, check if a consumer is waiting.
    if (!queue_is_empty(mbox->consumers)) {
        // Remove the first waiting consumer from the consumer queue.
        Process *consumer = dequeue(mbox->consumers);
        // Deliver the message directly to the consumer.
        if (consumer->recvBuffer && consumer->maxRecvSize >= messageSize) {
            if (messageSize > 0)
                memcpy(consumer->recvBuffer, message, messageSize);
            consumer->recvMsgSize = messageSize;
        } else {
            consumer->recvMsgSize = -1;
        }
        // Wake the consumer up so it can return.
        wakeup(consumer);
        return 0;
    }

    // No consumer waiting: try to find a free global mail slot.
    // If one is available, enqueue the message into the mailbox's slot queue.
    while (1) {
        int slotFound = -1;
        for (int i = 0; i < MAXSLOTS; i++) {
            if (!mailSlots[i].used) {
                slotFound = i;
                break;
            }
        }
        if (slotFound != -1) {
            // A free mail slot is available.
            mailSlots[slotFound].used = 1;
            mailSlots[slotFound].messageLength = messageSize;
            if (messageSize > 0)
                memcpy(mailSlots[slotFound].message, message, messageSize);
            // Enqueue the slot index into the mailbox's slot queue.
            enqueue(mbox->slots, slotFound);
            return 0;
        }
        // No free mail slot is available: block the producer.
        // Before blocking, add the producer to the mailbox's producer waiting queue.
        Process *producer = current_process();
        enqueue(mbox->producers, producer);

        // Block the producer until a mail slot is freed.
        block(producer);

        // When the process wakes up, check whether the mailbox is still active.
        if (!mbox->isActive)
            return -1;
    }
}


/* MboxRecv: Receive a message from a mailbox.
 * May block until a message is available.
 * Returns the size of the received message on success, or -1 on error.
 */
int MboxRecv(int mailboxID, void *message, int maxMessageSize) {
    // Validate parameters.
    if (mailboxID < 0 || mailboxID >= MAXMBOX || !mailboxes[mailboxID].isActive)
        return -1;
    if (maxMessageSize < mailboxes[mailboxID].slotSize)
        return -1;  // Receiver's buffer is too small

    Mbox *mbox = &mailboxes[mailboxID];

    // Loop until we can deliver a message.
    while (1) {
        // First, check if there is a queued message in the mailbox's slot queue.
        if (!queue_is_empty(mbox->slots)) {
            // Remove the oldest queued message (mail slot index) from the slot queue.
            int slotIndex = dequeue(mbox->slots);
            int msgLen = mailSlots[slotIndex].messageLength;
            if (msgLen > maxMessageSize) {
                // Message is too large for the receiver's buffer.
                // Free the slot and return an error.
                mailSlots[slotIndex].used = 0;
                return -1;
            }
            // Copy the message from the slot to the receiver's buffer.
            if (msgLen > 0 && message != NULL)
                memcpy(message, mailSlots[slotIndex].message, msgLen);
            // Mark the slot as free.
            mailSlots[slotIndex].used = 0;

            // If any producers are blocked waiting for a free slot, wake one.
            if (!queue_is_empty(mbox->producers)) {
                Process *producer = dequeue(mbox->producers);
                wakeup(producer);
            }
            return msgLen;
        }
        Process *consumer = current_process();
        // Save the consumer's receive parameters for potential direct delivery.
        consumer->recvBuffer = message;
        consumer->maxRecvSize = maxMessageSize;
        // Enqueue the consumer on the mailbox's consumer waiting queue.
        enqueue(mbox->consumers, consumer);

        // Block the consumer until a message is delivered.
        block(consumer);
        // When the consumer is woken, check if a message was delivered directly.
        if (consumer->recvMsgSize != 0) {
            int delivered = consumer->recvMsgSize;
            // Reset the field for next use.
            consumer->recvMsgSize = 0;
            return delivered;
        }
    }
}


/* MboxCondSend: Conditional send that returns -2 immediately if it would block.
 */
int MboxCondSend(int mailboxID, void *message, int messageSize) {
    /*
     * For the conditional version, if no slot is available and no consumer is waiting,
     * immediately return -2 instead of blocking.
     */
    int ret = MboxSend(mailboxID, message, messageSize);
    if (ret == -2)
        return -2;
    return ret;
}

/* MboxCondRecv: Conditional receive that returns -2 immediately if no message is available.
 */
int MboxCondRecv(int mailboxID, void *message, int maxMessageSize) {
    int foundSlot = -1;
    for (int i = 0; i < MAXSLOTS; i++) {
        if (mailSlots[i].used) {
            foundSlot = i;
            break;
        }
    }
    if (foundSlot == -1)
        return -2;  // No message available

    int msgLen = mailSlots[foundSlot].messageLength;
    if (msgLen > maxMessageSize)
        return -1;
    if (msgLen > 0 && message != NULL)
        memcpy(message, mailSlots[foundSlot].message, msgLen);
    mailSlots[foundSlot].used = 0;
    return msgLen;
}

/* waitDevice: Block until an interrupt arrives for a given device, and
 * store the device's status in the out parameter.
 */
void waitDevice(int type, int unit, int *status) {
    // Validate device type and unit.
    if (type == USLOSS_CLOCK_DEV && unit != 0) {
        fprintf(stderr, "Invalid unit for clock device\n");
        exit(1);
    }
    if (type == USLOSS_DISK_DEV && (unit < 0 || unit > 1)) {
        fprintf(stderr, "Invalid unit for disk device\n");
        exit(1);
    }
    if (type == USLOSS_TERM_DEV && (unit < 0 || unit > 3)) {
        fprintf(stderr, "Invalid unit for terminal device\n");
        exit(1);
    }

    /*
     * In a full implementation, waitDevice() would block on the mailbox corresponding
     * to the device until an interrupt is delivered. Once an interrupt occurs,
     * the handler would send a message (with the device status) to that mailbox.
     *
     * For this skeleton, we simulate an immediate device interrupt by simply
     * setting the status to 0.
     */
    if (status)
        *status = 0;
}

/* nullsys: Default system call handler.
 * Prints an error message and terminates the simulation.
 */
static void nullsys(int callNum, void *arg) {
    fprintf(stderr, "Invalid system call: %d\n", callNum);
    exit(1);
}

/* phase2_init: Initialize Phase 2 data structures.
 * This is called during bootstrap
 */
void phase2_init(void) {
    // Initialize mailboxes.
    for (int i = 0; i < MAXMBOX; i++) {
        mailboxes[i].isActive = 0;
        // Initialize additional mailbox fields if needed.
    }
    // Initialize global mail slots.
    for (int i = 0; i < MAXSLOTS; i++) {
        mailSlots[i].used = 0;
    }
    // Initialize the syscall vector to point to nullsys.
    for (int i = 0; i < MAXSYSCALLS; i++) {
        systemCallVec[i] = nullsys;
    }
    // Initialize your shadow process table as needed.
    // (This example leaves it uninitialized.)
}

/* phase2_start_service_processes: Start any service processes required by Phase 2.
 * Called after processes are running, but before testcases begin.
 */
void phase2_start_service_processes(void) {

}
