#include "phase2.h"
#include "usloss.h"
#include "phase1.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Mbox mailboxes[MAXMBOX];
MailSlot mailSlots[MAXSLOTS];

void (*systemCallVec[MAXSYSCALLS])(USLOSS_Sysargs *args);

struct Process shadowProcesses[MAXPROC];

void nullsys(USLOSS_Sysargs *args) {
    // Implementation of a system call
}

void interrupt_handler(int uhhh, void* ummm){
    dispatcher();
}

void phase2_init(void) {
  // Initialize mailboxes.
    USLOSS_IntVec[USLOSS_CLOCK_INT] = interrupt_handler;
    for (int i = 0; i < MAXMBOX; i++) {
        if (i < 7){
            mailboxes[i].isActive = 1;
          }
        else {
            mailboxes[i].isActive = 0;
          }
    }
    // Initialize global mail slots.
    for (int i = 0; i < MAXSLOTS; i++) {
        mailSlots[i].inUse = 0;
    }
    for (int i = 0; i < MAXPROC; i++) {
        shadowProcesses[i].pid = -1;
    }

    for (int i = 0; i < MAXSYSCALLS; i++) {
        systemCallVec[i] = nullsys;
    }
  }


int MboxCreate(int numSlots, int slotSize) {
    if (numSlots < 0 || slotSize < 0 || slotSize > MAX_MESSAGE) {
        return -1;
    }
    int id = -1;
    for (int i = 0; i < MAXMBOX; i++){
        if (mailboxes[i].isActive == 0){
            id = i;
            break;
        }
    }

    if (id == -1) {
        return -1;  // No available mailbox
    }

    mailboxes[id].id = id;
    mailboxes[id].numSlots = numSlots;
    mailboxes[id].slotSize = slotSize;
    mailboxes[id].isActive = 1;
    mailboxes[id].slots = &mailSlots[id];
    // Initialize additional fields (e.g., blocked queues) here as needed.
    return id;

  }

  void phase2_start_service_processes(void) {

}

int MboxRelease(int mbox_id){
   return 0;
}

int MboxSend(int mbox_id, void *msg_ptr, int msg_size){
    if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive) {
        return -1;  }
    if (msg_size < 0 || msg_size > mailboxes[mbox_id].slotSize)  {
        return -1;  }
    if (msg_size > 0 && msg_ptr == NULL)  {
        return -1;  }

    Mbox *mbox = &mailboxes[mbox_id];
    if (mbox->slots->inUse == 0){
      memcpy(mbox->slots->message, msg_ptr, msg_size);
      mbox->slots->inUse = 1;
      if (mbox->consumers != NULL){
        zap(mbox->consumers->pid);
      }
    }

    return 0;

}


int MboxRecv(int mbox_id, void *msg_ptr, int msg_max_size){
  if (mbox_id < 0 || mbox_id >= MAXMBOX || !mailboxes[mbox_id].isActive) {
        printf("MboxRecv: mbox_id %d is invalid\n", mbox_id);
        return -1;  }
  if (msg_max_size < 0)  {
        printf("MboxRecv: msg_ptr is NULL\n");
        return -1;  }

  Mbox *mbox = &mailboxes[mbox_id];
  if (mbox->slots->inUse == 0){
    int id = getpid();
    int slot = id % MAXPROC;
    shadowProcesses[slot].pid = id;
    mbox->consumers = &shadowProcesses[slot];
  }
  while (mbox->slots->inUse == 0) {
    blockMe();
  }
  if (mbox->slots->inUse == 1){
      memcpy(msg_ptr, mbox->slots->message, msg_max_size);
  }

  return strlen(mbox->slots->message) + 1;     // +1 for the null terminator
 }

int MboxCondSend(int mbox_id, void *msg_ptr, int msg_size){
  return 0;
}

int MboxCondRecv(int mbox_id, void *msg_ptr, int msg_size){ return 0; }

void waitDevice(int type, int unit, int *status){

  }

void wakeupByDevice(int type, int unit, int status){


}

