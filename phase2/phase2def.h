/* phase2def.h - internal definitions for Phase 2
 *
 * Since phase2.h cannot be changed and does not define our required types,
 * we define them here.
 */

 #ifndef _PHASE2DEF_H
 #define _PHASE2DEF_H
 
 #include "phase2.h"  // bring in constants like MAXMBOX, MAXSLOTS, and MAX_MESSAGE
 
 // Define the mailbox type.
 typedef struct Mailbox {
     int id;
     int numSlots;              // Maximum number of slots in this mailbox
     int slotSize;              // Maximum message size
     int isActive;              // 1 if active, 0 if released
     struct Process *producers; // Queue of blocked producers
     struct Process *consumers; // Queue of blocked consumers
 } Mbox;
 
 // Define the mail slot type.
 typedef struct MailSlot {
     int id;             // Slot ID
     int mailboxID;      // Mailbox ID this slot belongs to
     int messageLength;  // Length of the message stored here
     int inUse;          // 1 if this slot is currently in use, 0 otherwise
     char message[MAX_MESSAGE]; // Message data
     struct MailSlot *next;     // For use in a linked list if needed
 } MailSlot;
 
 // Define the process type used for queuing blocked processes.
 typedef struct Process {
     int pid;
     int blockedOn;         // Mailbox ID that the process is blocked on (-1 if not blocked)
     int priority;
     struct Process *next;  // Next process in the queue
 } Process;
 
 // Define a structure to manage a mailbox's slot queue.
 typedef struct {
     int *queue;      // Array of indices into the global mailSlots array.
     int capacity;    // Should equal the mailbox's numSlots.
     int head;        // Index of the first element.
     int tail;        // Index of the next available slot.
     int count;       // Number of indices currently stored.
 } SlotQueue;
 
// Declare the global array for mailbox slot queues.
extern SlotQueue mailboxSlotQueues[MAXMBOX];
 
 #endif
 