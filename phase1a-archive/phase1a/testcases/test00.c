
#include <stdio.h>
#include <usloss.h>
#include <phase1.h>

int XXp1(void *);

int tm_pid = -1;

int testcase_main()
{
    int status, pid1, kidpid;

    tm_pid = getpid();

    USLOSS_Console("testcase_main(): started\n");
    USLOSS_Console("EXPECTATION: Simple spork()/join() should complete.\n");

    pid1 = spork("XXp1", XXp1, "XXp1", USLOSS_MIN_STACK, 2);
    USLOSS_Console("Phase 1A TEMPORARY HACK: Manually switching to the recently created XXp1()\n");
    TEMP_switchTo(pid1);
    
    USLOSS_Console("testcase_main(): after spork of child %d\n", pid1);
    
    USLOSS_Console("testcase_main(): performing join\n");
    kidpid = join(&status);
    
    if (status != 3)
    {
        USLOSS_Console("ERROR: kidpid %d status %d\n", kidpid,status);
        USLOSS_Halt(1);
    }
    USLOSS_Console("testcase_main(): exit status for child %d is %d\n", kidpid, status); 
    return 0;
}

int XXp1(void *arg)
{
    int i;

    USLOSS_Console("XXp1(): started\n");
    USLOSS_Console("XXp1(): arg = '%s'\n", arg);

    for(i = 0; i < 100; i++)
        ;

    quit_phase_1a(3, tm_pid);
}

