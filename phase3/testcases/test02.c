/*
 * Simple Spawn test.
 */

#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3_usermode.h>
#include <stdio.h>

int start3(void *arg)
{
   USLOSS_Console("start3(): started, and immediately return'ing a 0\n");
   return 0;
}

