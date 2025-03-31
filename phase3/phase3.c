#include "phase3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void phase3_init(void){

  }


void phase3_start_service_processes(void){

  }


int Spawn(char *name, int (*func)(void *), void *arg, int stack size, int priority, int *pid) {
  return 0;
  }


int Wait(int *pid, int *status) {
  return 0;
  }


void Terminate(int status) {

  }


int SemCreate(int value, int *semaphore) {
  return 0;
  }


int SemP(int semaphore) {
  return 0;
  }


int SemV(int semaphore) {
  return 0;
  }


int kernSemCreate(int value, int *semaphore) {
  return 0;
  }
  
  
int kernSemP(int semaphore) {
  return 0;
  }
  
  
int kernSemV(int semaphore){
  return 0;
  }


void GetTimeofDay(int *tod) {
  
  }


void GetPID(int *pid){

  }


void DumpProcesses(){

  }


