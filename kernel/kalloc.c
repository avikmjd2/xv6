// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define NPHYSPAGES ((PHYSTOP-KERNBASE)/PGSIZE)

struct 
{
  struct spinlock lock;
  int cnt[NPHYSPAGES];
} refcount;

static inline int
pa2ind(uint64 pa)
{
  return(pa-KERNBASE)/PGSIZE;
}

void krefincr(void *pa)
{
  acquire(&refcount.lock);
  refcount.cnt[pa2ind((uint64)pa)]++;
  release(&refcount.lock);
}

int krefget(void*pa)
{
  acquire(&refcount.lock);
  int c = refcount.cnt[pa2ind((uint64)pa)];
  release(&refcount.lock);
  return c;
}

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&refcount.lock,"refcount");
  freerange(end, (void *)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
  {
    refcount.cnt[pa2ind((uint64)p)]=1;
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&refcount.lock);
  int ind = pa2ind((uint64)pa);

  if(refcount.cnt[ind] <=0)
  {
    panic("kfree: refcount underflow");
  }

  refcount.cnt[ind]--;
  if(refcount.cnt[ind]>0)
  {
    release(&refcount.lock);
    return;
  }

  release(&refcount.lock);

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r)
  {
    memset((char *)r, 5, PGSIZE); // fill with junk
    acquire(&refcount.lock);
    refcount.cnt[pa2ind((uint64)r)]=1;
    release(&refcount.lock);

  }
  return (void *)r;
}
