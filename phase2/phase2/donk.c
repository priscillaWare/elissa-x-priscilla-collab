// #include "phase2def.h"
// #include "phase2.h"
// #include "usloss.h"
// #include "phase1.h"
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>

// /* Global data from Phase 2 */
// Mbox mailboxes[MAXMBOX];
// MailSlot mailSlots[MAXSLOTS];
// SlotQueue mailboxSlotQueues[MAXMBOX];

// void (*systemCallVec[MAXSYSCALLS])(USLOSS_Sysargs *args);

// struct Process shadowProcesses[MAXPROC];

// /* Forward declarations for our new helper functions */
// static void enqueueProducer(int mbox_id, Process *p);
// static void enqueueConsumer(int mbox_id, Process *p);
// Process *current_process(void);

// /* Minimal current_process implementation using the shadow process table.
//    This maps the current PID (from getpid()) into an index into shadowProcesses.
// */
// Process *current_process(void) {
//     int pid = getpid();
//     int slot = pid % MAXPROC;

//     if (shadowProcesses[slot].pid != pid) {
//         USLOSS_Console("current_process(): ERROR - Process table not initialized for PID %d\n", pid);
//         return &shadowProcesses[slot];  // Prevent NULL pointer crash
//     }

//     return &shadowProcesses[slot];
// }

// /* Minimal implementation of enqueueProducer.
//    This adds process p to the tail of the mailbox’s producer queue.
// */
// static void enqueueProducer(int mbox_id, Process *p) {
//     Mbox *mbox = &mailboxes[mbox_id];
//     p->next = NULL;
//     if (mbox->producers == NULL) {
//         mbox->producers = p;
//     } else {
//         Process *cur = mbox->producers;
//         while (cur->next != NULL)
//             cur = (Process *) cur->next;  // cast because header typo: Processs -> Process
//         cur->next = p;
//     }
// }

// /* Minimal implementation of enqueueConsumer.
//    This adds process p to the tail of the mailbox’s consumer queue.
// */
// static void enqueueConsumer(int mbox_id, Process *p) {
//     if (p == NULL || p->pid == -1) {
//         USLOSS_Console("enqueueConsumer(): ERROR - Invalid process (PID -1) attempted to enqueue!\n");
//         return;
//     }

//     Mbox *mbox = &mailboxes[mbox_id];
//     p->next = NULL;
//     if (mbox->consumers == NULL) {
//         mbox->consumers = p;
//     } else {
//         Process *cur = mbox->consumers;
//         while (cur->next != NULL)
//             cur = cur->next;
//         cur->next = p;
//     }
//     USLOSS_Console("enqueueConsumer(): Added process %d to mailbox %d queue\n", p->pid, mbox_id);
// }


// // Returns 1 if the consumer with PID p->pid is already enqueued in the consumer queue for mailbox mbox_id; otherwise returns 0.
// static int isConsumerEnqueued(int mbox_id, Process *p) {
//     Mbox *mbox = &mailboxes[mbox_id];
//     Process *cur = mbox->consumers;
//     while (cur != NULL) {
//         if (cur->pid == p->pid)
//             return 1;
//         cur = (Process *) cur->next;  // cast to Process* if needed due to header typo
//     }
//     return 0;
// }


// /* The rest of your code remains largely unchanged. */

// void nullsys(USLOSS_Sysargs *args) {
//     // Implementation of a system call (dummy)
// }

// void interrupt_handler(int uhhh, void* ummm) {
//     dispatcher();
// }

// void phase2_init(void) {
//     USLOSS_IntVec[USLOSS_CLOCK_INT] = interrupt_handler;
//     for (int i = 0; i < MAXMBOX; i++) {
//         if (i < 7) {
//             mailboxes[i].isActive = 1;
//         } else {
//             mailboxes[i].isActive = 0;
//         }
//         // Initialize the blocked queues to NULL.
//         mailboxes[i].consumers = NULL;
//         mailboxes[i].producers = NULL;
//         // Note: Do not assign a single slot pointer; we use the slot queue.
//     }
//     // Initialize global mail slots.
//     for (int i = 0; i < MAXSLOTS; i++) {
//         mailSlots[i].inUse = 0;
//     }
//     for (int i = 0; i < MAXPROC; i++) {
//         shadowProcesses[i].pid = -1;
//     }
//     for (int i = 0; i < MAXSYSCALLS; i++) {
//         systemCallVec[i] = nullsys;
//     }
// }

// int MboxCreate(int numSlots, int slotSize) {
//     if (numSlots < 0 || slotSize < 0 || slotSize > MAX_MESSAGE) {
//         return -1;
//     }
//     int id = -1;
//     for (int i = 0; i < MAXMBOX; i++){
//         if (mailboxes[i].isActive == 0){
//             id = i;
//             break;
//         }
//     }
//     if (id == -1) {
//         return -1;  // No available mailbox
//     }
//     mailboxes[id].id = id;
//     mailboxes[id].numSlots = numSlots;
//     mailboxes[id].slotSize = slotSize;
//     mailboxes[id].isActive = 1;
//     mailboxes[id].consumers = NULL;
//     mailboxes[id].producers = NULL;
//     /* Initialize the slot queue for this mailbox */
//     mailboxSlotQueues[id].capacity = numSlots;
//     mailboxSlotQueues[id].queue = malloc(sizeof(int) * numSlots);
//     if (mailboxSlotQueues[id].queue == NULL) {
//         exit(1);
//     }
//     mailboxSlotQueues[id].head = 0;
//     mailboxSlotQueues[id].tail = 0;
//     mailboxSlotQueues[id].count = 0;
//     return id;
// }

// void phase2_start_service_processes(void) {
//     // No service processes in this implementation.
// }

// int MboxRelease(int mbox_id) {
//    return 0;
// }

// int MboxSend(int mbox_id, void *msg_ptr, int msg_size) {
//     if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
//         return -1;
//     if (msg_size < 0 || msg_size > mailboxes[mbox_id].slotSize)
//         return -1;
//     if (msg_size > 0 && msg_ptr == NULL)
//         return -1;

//     Mbox *mbox = &mailboxes[mbox_id];
//     int result = -1;

//     while (1) {
//         // Deliver directly to a waiting consumer
//         if (mbox->consumers != NULL) {
//             Process *consumer = mbox->consumers;
//             mbox->consumers = consumer->next;  // Remove from queue

//             memcpy(consumer->messageBuffer, msg_ptr, msg_size);
//             consumer->messageLength = msg_size;
//             consumer->messageBuffer[msg_size] = '\0';  // Null terminate

//             USLOSS_Console("MboxSend(): Delivered message '%s' to process %d\n", consumer->messageBuffer, consumer->pid);

//             int unblockStatus = unblockProc(consumer->pid);
//             if (unblockStatus == -1) {
//                 USLOSS_Console("MboxSend(): ERROR - Failed to unblock process %d!\n", consumer->pid);
//                 return -1;
//             }

//             return 0; // Message successfully sent
//         }

//         // Check if there is space in the mailbox
//         if (mailboxSlotQueues[mbox_id].count < mailboxes[mbox_id].numSlots) {
//             int slotFound = -1;
//             for (int i = 0; i < MAXSLOTS; i++) {
//                 if (!mailSlots[i].inUse) {
//                     slotFound = i;
//                     break;
//                 }
//             }
//             if (slotFound == -1) {
//                 USLOSS_Console("MboxSend(): ERROR - No available mail slots!\n");
//                 return -2;  // No slots available
//             }

//             mailSlots[slotFound].inUse = 1;
//             mailSlots[slotFound].messageLength = msg_size;
//             memcpy(mailSlots[slotFound].message, msg_ptr, msg_size);

//             int tail = mailboxSlotQueues[mbox_id].tail;
//             mailboxSlotQueues[mbox_id].queue[tail] = slotFound;
//             mailboxSlotQueues[mbox_id].tail = (tail + 1) % mailboxSlotQueues[mbox_id].capacity;
//             mailboxSlotQueues[mbox_id].count++;

//             return 0; // Message successfully queued
//         }

//         // If mailbox is full, block the sender
//         Process *producer = current_process();
//         enqueueProducer(mbox_id, producer);
//         blockMe();
//     }
//     return result;
// }


// int MboxRecv(int mbox_id, void *msg_ptr, int msg_max_size) {
//     if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive)
//         return -1;
//     if (msg_ptr == NULL)
//         return -1;

//     Mbox *mbox = &mailboxes[mbox_id];

//     while (1) {
//         USLOSS_Console("MboxRecv: mailbox %d: count=%d, head=%d, tail=%d\n",
//                        mbox_id,
//                        mailboxSlotQueues[mbox_id].count,
//                        mailboxSlotQueues[mbox_id].head,
//                        mailboxSlotQueues[mbox_id].tail);

//         // Check if a message is available in a slot
//         if (mailboxSlotQueues[mbox_id].count > 0) {
//             int head = mailboxSlotQueues[mbox_id].head;
//             int slotIndex = mailboxSlotQueues[mbox_id].queue[head];

//             mailboxSlotQueues[mbox_id].head = (head + 1) % mailboxSlotQueues[mbox_id].capacity;
//             mailboxSlotQueues[mbox_id].count--;

//             int msgLen = mailSlots[slotIndex].messageLength;
//             if (msgLen > msg_max_size) {
//                 USLOSS_Console("MboxRecv(): ERROR - Message too large for buffer!\n");
//                 return -1;
//             }

//             memcpy(msg_ptr, mailSlots[slotIndex].message, msgLen);
//             ((char*)msg_ptr)[msgLen] = '\0';  // Null terminate for safety

//             mailSlots[slotIndex].inUse = 0;

//             USLOSS_Console("MboxRecv(): Received message '%s' (length %d)\n", (char*)msg_ptr, msgLen);

//             // If any producer is blocked, unblock them
//             if (mbox->producers != NULL) {
//                 unblockProc(mbox->producers->pid);
//                 mbox->producers = NULL;
//             }

//             return msgLen;
//         } 

//         // No message available, block and wait
//         Process *consumer = current_process();
//         consumer->blockedOn = mbox_id;

//         if (!isConsumerEnqueued(mbox_id, consumer)) {
//             enqueueConsumer(mbox_id, consumer);
//         }

//         USLOSS_Console("MboxRecv(): Process %d blocking on mailbox %d\n", getpid(), mbox_id);
//         blockMe();

//         // Once unblocked, check if a message has been assigned
//         if (consumer->messageLength > 0) {
//             if (consumer->messageLength > msg_max_size) {
//                 USLOSS_Console("MboxRecv(): ERROR - Received message too large!\n");
//                 return -1;
//             }
//             memcpy(msg_ptr, consumer->messageBuffer, consumer->messageLength);
//             ((char*)msg_ptr)[consumer->messageLength] = '\0';  // Null terminate

//             USLOSS_Console("MboxRecv(): Message received '%s' (length %d)\n", (char*)msg_ptr, consumer->messageLength);
//             return consumer->messageLength;
//         }

//         return -1;  // Should never reach here
//     }
// }


// int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size) {
//     return 0;
// }

// int MboxCondRecv(int mbox_id, void *msg_ptr, int msg_size) {
//     return 0;
// }

// void waitDevice(int type, int unit, int *status) {
//     // Implementation of device wait (not used in test06)
// }

// void wakeupByDevice(int type, int unit, int status) {
//     // Implementation of device wakeup (not used in test06)
// }
