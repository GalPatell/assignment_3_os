#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
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

extern uint64 map_shared_pages(struct proc*, struct proc*, uint64, uint64);
extern uint64 unmap_shared_pages(struct proc*, uint64, uint64);

uint64 sys_map_shared_pages(void) {
    uint64 src_va;
    int size;
    int target_pid;

    argaddr(0, &src_va);
    argint(1, &size);
    argint(2, &target_pid);

    // Find the target process
    struct proc *target_proc = find_proc_by_pid(target_pid);
    if (target_proc == 0) {
        return 0; // Process not found
    }
    
    uint64 result = map_shared_pages(target_proc, myproc(), src_va, size);
    
    // Release the lock we got from find_proc_by_pid
    release(&target_proc->lock);
    
    return result;
}

uint64 sys_unmap_shared_pages(void) {
    uint64 addr;
    int size;

    argaddr(0, &addr);
    argint(1, &size);

    return unmap_shared_pages(myproc(), addr, size);
}