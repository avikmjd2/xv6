#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_waitx(void)
{
  uint64 addr, wtime, rtime, resp;
  argaddr(0, &addr); //Manually read the users argument 0,1,2,3 (params) from users trapframe. Now the control has been taken by the kernel.
  argaddr(1, &wtime);
  argaddr(2, &rtime);
  argaddr(3, &resp);
  return waitx(addr, wtime, rtime, resp);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// TODO(mini-project 2): register an alarm handler to be invoked every
// `ticks` ticks of CPU time consumed by this process. sigalarm(0, 0)
// cancels a pending alarm. Return 0 on success, non-zero on failure.
uint64
sys_sigalarm(void)
{
  int ticks;
  uint64 handler;

  argint(0,&ticks);
  argaddr(1,&handler);

  if(ticks<0) return -1;

  struct proc* p = myproc();

  // a live alarm needs a handler inside the process, on an executable user
  // page (address 0 is valid: user text starts there)
  if(ticks>0)
  {
    pte_t *pte;
    if(handler >= p->sz || (pte = walk(p->pagetable, handler, 0)) == 0 ||
       (*pte & (PTE_V|PTE_U|PTE_X)) != (PTE_V|PTE_U|PTE_X))
      return -1;
  }
  p->alarm_interval = ticks;
  p->alarm_handler=handler;
  p->alarm_ticks_elapsed=0;

  if(ticks==0)
  {
    p->alarm_handler_running = 0;
  }

  return 0;
}

// TODO(mini-project 2): restore the process state saved when the alarm
// handler was invoked, so that the interrupted user code resumes as if
// nothing had happened.
uint64
sys_sigreturn(void)
{
  struct proc* p = myproc();
  if(!p->alarm_handler_running)
    return -1;   // sigreturn without a running handler: nothing to restore
  *p->trapframe = p->alarm_saved_tf;
  //mark the alarm hanlder as finished so future alarms can fire
  p->alarm_handler_running = 0;

  return p->trapframe->a0;
}
