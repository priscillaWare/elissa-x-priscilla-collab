/* Check Time of day */

#include <usloss.h>
#include <usyscall.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3_usermode.h>
#include <assert.h>


int start3(void *arg)
{
    int i, start, middle, end, elapsed;

    USLOSS_Console("start3(): started\n");

    GetTimeofDay(&start);

    for (i = 0; i < 100000; i++)
        ;
    GetTimeofDay(&middle);
    elapsed = middle - start;
    USLOSS_Console("start3(): elapsed time in middle = %2d ",elapsed);
    USLOSS_Console("Should be close, but does not have to be an exact match\n");

    for (i = 0; i < 100000; i++)
        ;
    GetTimeofDay(&end);
    elapsed = end - start;
    USLOSS_Console("start3(): elapsed time at end    = %2d ",elapsed);
    USLOSS_Console("Should be close, but does not have to be an exact match\n");
  
    Terminate(0);
}

