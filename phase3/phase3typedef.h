//
// Created by prisc on 3/31/2025.
//

#ifndef PHASE3TYPEDEF_H
#define PHASE3TYPEDEF_H

typedef struct {
    int (*function)(void*);  // user function
    void *argument;          // argument to function
} functionArgs;

typedef struct {
    int value;
    int mboxID;
    int in_use;
    int blocked; // number of processes blocked in P
} Semaphore;


#endif //PHASE3TYPEDEF_H
