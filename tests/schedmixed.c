#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main()
{
    int n = 3;
    int pids[3];

    for (int i = 0; i < n; i++)
    {
        int pid = fork();
        if (pid < 0)
        {
            printf("fork failed\n");
            exit(1);
        }
        if (pid == 0)
        {
            int mypid = getpid();
            if (i == 0)
            {
                // Process 0: Long CPU-bound (20 heavy loops)
                for (int step=0; step<20; step++)
                {
                    volatile int dummy = 0;
                    for (int j=0; j<50000000; j++)
                    {
                        dummy += 1;
                    }
                    printf("[PID %d - LONG CPU] step %d\n", mypid, step);
                }
            }
            else if (i == 1)
            {
                // Process 1: Short CPU-bound (only 2 loops)
                for (int step=0; step<2; step++)
                {
                    volatile int dummy = 0;
                    for (int j = 0; j < 50000000; j++)
                    {
                        dummy += 1;
                    }
                    printf("[PID %d - SHORT CPU] step %d\n", mypid, step);
                }
            }
            else
            {
                // Process 2: Interactive / I/O-bound (short bursts + sleep)
                for (int step = 0; step < 10; step++)
                {
                    volatile int dummy = 0;
                    for (int j = 0; j < 5000000; j++)
                    {
                        dummy += 1;
                    }
                    printf("[PID %d - INTERACTIVE] burst %d -> yielding\n", mypid, step);
                    pause(1); 
                }
            }
            exit(0);
        }
        pids[i] = pid;
    }

    int total_turnaround = 0;
    int total_wait = 0;
    int total_resp = 0;

    printf("\n=== WORKLOAD STARTED ===\n");
    for (int i = 0; i < n; i++)
    {
        int status, wtime, rtime, resp;
        int exited_pid = waitx(&status, &wtime, &rtime, &resp);
        int ttime = wtime + rtime;

        char *role = "Unknown";
        if (exited_pid == pids[0]) role = "Long CPU (20 loops)";
        else if (exited_pid == pids[1]) role = "Short CPU (2 loops)";
        else if (exited_pid == pids[2]) role = "Interactive (10 sleeps)";

        printf("\n[%s | PID %d]\n", role, exited_pid);
        printf("  Turnaround Time : %d ticks\n", ttime);
        printf("  Waiting Time    : %d ticks\n", wtime);
        printf("  Response Time   : %d ticks\n", resp);
        printf("  Run Time (CPU)  : %d ticks\n", rtime);

        total_turnaround += ttime;
        total_wait += wtime;
        total_resp += resp;
    }


    printf("AVERAGE METRICS ACROSS ALL 3 PROCESSES\n");
    printf("AVERAGE Turnaround Time: %d\n", total_turnaround / n);
    printf("AVERAGE Waiting Time:    %d\n", total_wait / n);
    printf("AVERAGE Response Time:   %d\n", total_resp / n);

    exit(0);
    return 0;
}