# xv6-riscv: MLFQ Scheduling, Copy-on-Write Fork, Alarms & Symbolic Links

Four OS features implemented from scratch in the kernel of
[xv6-riscv](https://github.com/mit-pdos/xv6-riscv), MIT's teaching operating system
for 64-bit RISC-V. The work covers scheduling, virtual memory, signals-style upcalls
and the file system. All of it runs on a multi-core kernel (3 harts by default under QEMU).

| Feature | Subsystem | Files touched (main ones) |
|---|---|---|
| **MLFQ + FIFO schedulers**, `waitx` metrics syscall | Scheduler / process mgmt | `proc.c`, `proc.h`, `trap.c`, `sysproc.c` |
| **Copy-on-write `fork()`** | Virtual memory / physical allocator | `vm.c`, `kalloc.c`, `riscv.h`, `trap.c` |
| **`sigalarm` / `sigreturn`** (periodic user-space upcalls) | Traps / syscalls | `trap.c`, `sysproc.c`, `proc.h`, `exec.c` |
| **Symbolic links** (`symlink`, `O_NOFOLLOW`, `ln -s`, `readlink`) | File system / syscalls | `sysfile.c`, `fcntl.h`, `stat.h`, `user/ln.c`, `user/ls.c` |

---

## Highlights

- **MLFQ scheduler** with 4 priority queues (time slices of 1 / 4 / 8 / 16 ticks), a periodic
  priority boost every 48 ticks to prevent starvation, and immediate preemption when a
  higher-priority process becomes runnable. It is chosen at compile time with
  `make SCHEDULER=MLFQ`, alongside a non-preemptive **FIFO** policy and the default
  round-robin policy.
- **`waitx` system call** reporting per-process turnaround, waiting and response time
  from kernel-tracked timestamps, used to benchmark the three schedulers against each other.
- **Copy-on-write fork:** `fork()` shares pages read-only instead of copying them. A
  per-physical-page reference count in the allocator and a software PTE bit track sharing.
  The copy happens lazily in the page-fault handler. Kernel writes to user memory
  (`read`, `pipe`, `wait`, ...) resolve COW faults too.
- **Alarm upcalls:** `sigalarm(n, handler)` runs a user handler every `n` ticks of CPU time.
  The full trapframe is saved and restored by `sigreturn`, so the interrupted code resumes
  transparently, including register `a0`.
- **Symbolic links** as a new inode type. `open()` follows chains of up to 10 links
  (loop-safe), `O_NOFOLLOW` opens the link itself, and `ls` shows links without following them.
- Each feature has its own user-space test program. All of them pass on the merged kernel,
  alongside the stock `usertests` suite (see [Testing](#testing)).

---

## 1. Scheduling: MLFQ, FIFO and `waitx`

The scheduler is selected at compile time:

```sh
make qemu                       # default: round-robin
make qemu SCHEDULER=MLFQ        # multi-level feedback queue
make qemu SCHEDULER=FIFO        # non-preemptive first-come-first-served
```

### MLFQ design

| Queue | Priority | Time slice |
|---|---|---|
| 0 | highest | 1 tick |
| 1 | | 4 ticks |
| 2 | | 8 ticks |
| 3 | lowest | 16 ticks |

- **New processes** start at the tail of queue 0.
- **Selection:** `scheduler()` picks the runnable process from the highest non-empty queue.
  Within a queue, the process with the smallest `enter_tick` goes first.
  `enter_tick` is a monotonic counter that gives exact FIFO/round-robin order within a queue
  without timestamp collisions.
- **Demotion:** a process that uses its whole slice moves down one queue (capped at 3) and
  goes to the tail of the new queue.
- **Voluntary yield:** a process that sleeps before its slice expires keeps its queue. On
  wakeup it gets a fresh slice and goes to the tail of its queue, so I/O-bound and
  interactive jobs stay at high priority.
- **Preemption:** on every timer tick the running process checks (`more_priority()`) whether
  a strictly higher-priority process is runnable, and yields if so.
- **Anti-starvation boost:** every 48 ticks `mlfq_boost()` moves all live processes back to
  queue 0.
- **Debugging:** `Ctrl-O` or `Ctrl-P` in the console calls `procdump()`, which prints every
  process's queue, ticks used in its slice, and `enter_tick`, plus the position within the
  boost window.

### `waitx(&status, &wtime, &rtime, &resp)`

A `wait()` variant that also returns kernel-measured metrics for the reaped child:

- **Response time** = first scheduled − created
- **Turnaround time** = exited − created
- **Waiting time** = turnaround − CPU run time

### Benchmark results (mixed workload, `CPUS=1`)

`tests/schedmixed.c` runs a short CPU job, an interactive job (compute bursts plus `pause(1)`)
and a long CPU job at the same time. Values are averages in ticks, measured on a single
core so queue behavior isn't masked by other CPUs. The full analysis, including a
homogeneous N=3 / N=30 suite, is in [`report.md`](report.md).

| Scheduler | Avg turnaround | Avg waiting | Avg response |
|---|---:|---:|---:|
| FIFO | 25 | 17 | 14 |
| Round-robin | 14 | 6 | 1 |
| **MLFQ** | **13** | **5** | **1** |

The short job finishes in 3 ticks under MLFQ, versus 5 under round-robin and 22 under FIFO,
where it waits behind the long job (the convoy effect). The queue timeline plot is
[`mlfq_timeline.png`](mlfq_timeline.png). `plot_mlfq.py` generates it from `[PLOT] tick pid queue` lines in `log.txt`; to regenerate it, re-enable the commented-out `printk("[PLOT] ...")` in `usertrap()` and capture the console output.

---

## 2. Copy-on-Write `fork()`

Stock xv6 copies every user page on `fork()`. Here the child shares the parent's physical pages.

- **`uvmcopy`:** for every writable page, clear `PTE_W` and set a software bit `PTE_COW`
  (RISC-V RSW bit 8) in both the parent and the child, map the same physical page into the
  child, and increment its reference count.
- **Reference counts (`kalloc.c`):** one counter per physical page, protected by its own
  spinlock. `kalloc()` sets it to 1, `kfree()` decrements and only returns the page to the
  free list when it reaches zero.
- **Fault handling (`vmfault` via `usertrap`):** a store to a COW page raises a page fault.
  - If the page is shared, allocate a new page, copy it, remap it writable with `PTE_COW`
    cleared, and drop one reference on the old page.
  - If this process is the last owner (count == 1), just re-enable `PTE_W` in place, with no copy.
- **Kernel-side writes:** `copyout()` resolves COW faults itself. A syscall like `read()` or
  `wait()` that writes into a shared page therefore gets a private copy, and never corrupts
  the other process.
- **Cleanup:** `uvmunmap` and the error path of `uvmcopy` go through `kfree`, so counts stay
  balanced across `exit`, `exec` and failed forks.

---

## 3. `sigalarm` / `sigreturn`

```c
int sigalarm(int ticks, void (*handler)());   // ticks == 0 cancels the alarm
int sigreturn(void);                          // called at the end of the handler
```

- After `ticks` timer ticks of CPU time, `usertrap()` saves the whole trapframe into
  `p->alarm_saved_tf` and redirects `epc` to the handler.
- `sigreturn()` copies the saved trapframe back and returns the saved `a0`, so the
  interrupted code continues as if nothing happened.
- **Not re-entrant:** a flag stops a new alarm from firing while a handler is still running.
- **Validation:** `sigalarm` rejects negative intervals and handler addresses that are not a
  valid, user-accessible, executable page. `sigreturn` fails if no handler is running.
- **Lifecycle:** alarm state is reset on `fork`, `exec` and process exit. The old image's
  handler address is meaningless after `exec`.

---

## 4. Symbolic Links

- New inode type `T_SYMLINK`. The target path is stored in the link's data block.
- **`symlink(target, path)`:** a new syscall. It creates the link inside a single log
  transaction and frees the new inode if any step fails.
- **`open()` follows links**, up to 10 hops (returning `-1` beyond that), so link loops can't
  hang the kernel. The link's inode lock is released before resolving the next path.
- **`O_NOFOLLOW`** opens the link itself, which `readlink` and `ls` build on.
- `open(O_CREATE)` through a link creates the target. Opening a link itself for writing is refused.
- **User programs:**
  - `ln -s target path` creates a symlink.
  - `readlink path` prints a link's target.
  - `ls` reports links with type 4 without following them.
- **Shell `>>`** now opens with the new `O_APPEND` flag, so appends no longer overwrite
  from offset 0.

---

## System call numbers added

| # | Name | Feature |
|---|---|---|
| 23 | `symlink` | Symbolic links |
| 24 | `waitx` | Scheduler metrics |
| 25 | `sigalarm` | Alarms |
| 26 | `sigreturn` | Alarms |

---

## Building and running

**Prerequisites:** a RISC-V bare-metal GCC toolchain (`riscv64-unknown-elf-gcc` or
`riscv64-linux-gnu-gcc`) and `qemu-system-riscv64`.

```sh
make qemu                        # build and boot (3 cores by default)
make qemu CPUS=1                 # single core
make qemu SCHEDULER=MLFQ         # choose a scheduler
```

The compiler flags are not tracked as dependencies, so run `make clean` before switching
`SCHEDULER` or you may get a stale build.

> **Note:** the file named `README` (no extension) is packed into the xv6 disk image by
> `mkfs`, so it is deliberately left out of this document. This file is `README.md`.

## Testing

Run these inside xv6, or use the automated runner:

```sh
./test-xv6.py cowtest        # copy-on-write fork tests
./test-xv6.py alarmtest      # alarm tests
./test-xv6.py -q usertests   # xv6's stock regression suite (quick mode)
```

| Program | What it checks |
|---|---|
| `cowtest` | **simple:** allocates 2/3 of physical RAM and forks, which only succeeds if pages are shared. **three:** forks several processes from a 1/4-of-RAM image and checks that copied pages are freed again. **file:** checks that `copyout()` (e.g. `read()` through a pipe) resolves COW faults without corrupting the other process. |
| `alarmtest` | **test0:** the handler is called. **test1:** the interrupted code resumes correctly. **test2:** no handler re-entry. **test3:** register `a0` is preserved. |
| `symlinktest` | Link creation and following, hop limit and loops, `O_CREATE` on an existing link, and concurrent link creation. |
| `schedtest`, `schedmixed` | Print `waitx` metrics for homogeneous and mixed workloads. Run them under each scheduler to compare. |

On the merged kernel, `cowtest`, `alarmtest`, `symlinktest` and `usertests -q` pass with
both the default and `SCHEDULER=MLFQ` builds (`symlinktest` was run on the default build).

## Known limitations

- Only `open()` follows symbolic links. `exec` and `chdir` do not, so `cd` into a link to a
  directory, or running a program through a link, fails.
- `symlink()` does not yet include the check that `link()` and `create()` use to reject
  entries in an already-unlinked directory.
- The reference-count table is a flat array sized for the fixed 128 MB of RAM that xv6 maps.
- MLFQ and FIFO are selected at compile time, not at run time.

## Future plans

**Correctness and hardening**
- Make `exec` and `chdir` follow symbolic links, ideally through one shared path-resolution
  helper in `fs.c` (`namex`) rather than special-casing `open()`.
- Add the unlinked-directory guard to `symlink()`, matching `link()` and `create()`.
- Stress-test the COW reference counts under heavy multi-core `fork`/`exit` load, and with
  `CPUS=1`, to back up the locking analysis with measurements.
- Add a `symlinktest` case for symlinks inside a removed directory, and tests for `exec`/`chdir`
  through links once those are supported.

**Scheduling**
- Choose the scheduler at run time (for example through a syscall or boot argument) instead of
  at compile time.
- Make the MLFQ parameters (queue count, slice lengths, boost interval) configurable.
- Repeat the benchmarks on multiple cores and with larger, more varied workloads.
- Per-CPU run queues in place of the single `mlfq_lock`, to reduce contention.

**Memory**
- Expose a statistic showing how many pages are currently shared by COW.
- Size the reference-count table from the detected RAM size instead of the fixed 128 MB layout.

**Signals**
- Grow `sigalarm` into a small signal facility: a per-process handler table, signal masks, and
  `kill` delivering signals other than termination.

**Project hygiene**
- Drop `log.txt`, and add a CI job that builds the kernel and runs `cowtest`, `alarmtest` and
  `usertests -q` on every push.

## Ideas for further extensions

Possible next features, each chosen to build on something already in this kernel.

| Idea | Area | What it involves | Builds on | Difficulty |
|---|---|---|---|---|
| Syscall tracing (`strace`-style) | Syscalls | A `trace(mask)` syscall that prints each traced syscall's name, arguments and return value, inherited by children | `syscall()` dispatch table | Easy |
| `ps`-style process info syscall | Observability | Export pid, state, MLFQ queue, ticks used and memory size to user space, and add a `ps` program | `procdump()`, MLFQ fields in `struct proc` | Easy |
| Priority / `nice` and a lottery or stride scheduler | Scheduling | A `setpriority` syscall, aging to avoid starvation, and a third proportional-share policy to benchmark against MLFQ | `SCHEDULER` macro, `waitx` metrics, `schedtest` | Easy to Medium |
| Complete symlink semantics | File system | Resolve relative link targets against the link's own directory (they are currently resolved against the caller's cwd), follow links in `exec`/`chdir`, and add an `lstat` syscall | `sys_open` follow loop, `T_SYMLINK` | Easy to Medium |
| Doubly-indirect blocks | File system | Raise the maximum file size beyond the current limit by adding a second level of indirection in `bmap`/`itrunc` | `fs.c`, `usertests` big-file tests | Medium |
| Per-CPU free lists in `kalloc` | Memory | One free list per core with work stealing, to remove contention on the single allocator lock; the COW refcount lock needs rethinking too | `kalloc.c`, COW reference counts | Medium |
| Full signals | Syscalls | `sigaction`, signal masks, `kill(pid, sig)`, and a user-space trampoline so handlers return automatically | Trapframe save/restore from `sigalarm`/`sigreturn` | Medium |
| `mmap` / `munmap` | Memory | Anonymous and file-backed mappings, private ones backed by COW, faulted in lazily | `vmfault`, COW, lazy allocation | Medium to Hard |
| Kernel threads (`clone`) | Processes | Threads that share an address space, with a shared page-table reference count and a user-level mutex or futex | `proc.c`, `uvmcopy`, page reference counts | Hard |
| Swapping / page eviction | Memory | Evict cold pages to disk and fault them back in, using a clock algorithm and the existing page reference counts | `kalloc.c` refcounts, `vmfault`, `virtio_disk` | Hard |

## Repository layout

```
kernel/       kernel sources (proc.c, trap.c, vm.c, kalloc.c, sysfile.c, ...)
user/         user programs and tests (cowtest, alarmtest, symlinktest, ln, readlink, ...)
tests/        scheduler benchmarks (schedtest, schedmixed)
report.md     MLFQ / FIFO / RR analysis with measured results
plot_mlfq.py  generates mlfq_timeline.png from [PLOT] trace lines in log.txt
```

## Credits

Built on [xv6-riscv](https://github.com/mit-pdos/xv6-riscv) by MIT PDOS, a re-implementation
of Unix Version 6 by Russ Cox, Frans Kaashoek, Robert Morris and contributors. xv6 is
distributed under the license in [`LICENSE`](LICENSE) (Apache 2.0). The MLFQ, copy-on-write,
alarm and symbolic-link features were implemented by Avik Majumder.
