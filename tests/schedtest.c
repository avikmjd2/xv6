#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


int main()
{
    int n=3;
    for(int i=0;i<n;i++)
    {
        int pid = fork();
        if(pid<0)
        {
            printf("fork failed\n");
            exit(1);
        }
        if(pid==0)
        {
            for(int step = 0;step<10;step++)
            {
                volatile int dummy = 0;
                for (int j = 0; j < 50000000; j++) 
                {
                    dummy += 1;
                }
                printf("[%d] tick-loop %d\n", getpid(), step);
            }
            exit(0);
        }
    }

    int total_turnaround = 0;
    int total_wait = 0;
    int total_resp = 0;
    for (int i = 0; i < n; i++) 
    {
        int status,wtime,rtime,resp;
        waitx(&status,&wtime,&rtime,&resp);
        int ttime = wtime + rtime;
        total_turnaround+=ttime;
        total_wait+=wtime;
        total_resp+=resp;
    }
    printf("METRICS\n");
    printf("AVERAGE Turnaround Time: %d\n", total_turnaround / n);
    printf("AVERAGE Waiting Time:    %d\n", total_wait / n);
    printf("AVERAGE Response Time:   %d\n", total_resp  / n);
    exit(0);
    return 0;
}