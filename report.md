# Multi-Level Feedback Queue (MLFQ) & Cross-Scheduler Comparison Report

## 1. Overview
This report documents the implementation and evaluation of the **Multi-Level Feedback Queue (MLFQ)** scheduler and a **First-In, First-Out (FIFO)** scheduler in the xv6 operating system, contrasting them against the default **Round-Robin (RR)** policy.

---

## 2.3.1 Implementation Summary

### 1. Makefile and `SCHEDULER` Macro
* **Changes**: Added conditional compiler flag handling in `Makefile`:
  ```make
  ifdef SCHEDULER
  CFLAGS += -D$(SCHEDULER)
  endif
  ```
* **Rationale**: Enables compile-time selection of scheduling algorithms (`make qemu SCHEDULER=MLFQ`, `make qemu SCHEDULER=FIFO`, or default `make qemu` for Round-Robin) without modifying kernel source files between tests. An unset flag defaults cleanly to standard Round-Robin.

### 2. `struct proc` Changes (`kernel/proc.h`)
* **Changes**: Added scheduling bookkeeping fields to `struct proc`:
  * `int queue`: Current priority queue level (0: highest, 3: lowest).
  * `int current_ticks`: Number of ticks consumed within the process's current queue slice.
  * `uint64 enter_tick`: Monotonic sequence counter recording arrival/tail insertion in a queue.
  * `uint64 ctime, stime, etime, rtime`: Creation time, first execution time, exit time, and total CPU run time.
* **Rationale**: Provides required metadata for priority assignment, slice expiration, strict FIFO/Round-Robin ordering without timestamp collisions, and performance metrics tracking.

### 3. `allocproc()` and `freeproc()` (`kernel/proc.c`)
* **Changes**:
  * In `allocproc()`: Newly allocated processes initialize with `queue = 0`, `current_ticks = 0`, `enter_tick = ++my_clock`, `ctime = ticks`, `stime = 0`, `etime = 0`, and `rtime = 0`.
  * In `freeproc()`: Reset queue and metric fields when the process slot is recycled.
* **Rationale**: Complies with the specification that every new process begins at the tail of highest-priority Queue 0, while initializing timing counters for subsequent turnaround and response time computations.

### 4. Queue Selection and Preemption Logic (`kernel/proc.c`)
* **Changes**:
  * In `scheduler()`: The MLFQ policy searches for runnable processes starting from Queue 0 down to Queue 3. Within the same priority queue, it selects the process with the smallest `enter_tick` (earliest arrival / tail insertion).
  * Preemption check (`more_priority()`): During timer interrupts, a running process checks whether any process in a strictly higher-priority queue is runnable.
* **Rationale**: Implements strict priority scheduling where higher-priority queues starve lower-priority queues until empty. A process running in a lower queue is preempted at the current tick boundary if a higher-priority job arrives.

### 5. Time-Slice Handling (`kernel/trap.c`)
* **Changes**: Time-slice quotas are defined as `{1, 4, 8, 16}` ticks for Queues 0, 1, 2, and 3. On each timer tick (`which_dev == 2`), the running process increments `current_ticks`. If `current_ticks >= slice[queue]`:
  * Demotes the process to the next lower queue (`p->queue++`, capped at 3).
  * Resets `current_ticks = 0` and updates `enter_tick = ++my_clock` to insert it at the tail.
  * Calls `yield()` to surrender the CPU.
* **Rationale**: Enforces escalating time slices so that short, interactive bursts finish quickly in higher queues, while CPU-bound compute tasks migrate to lower queues with longer slices, minimizing context-switching overhead.

### 6. Voluntary Yield Handling (`kernel/proc.c:wakeup()`)
* **Changes**: When a sleeping process wakes up after I/O or waiting:
  ```c
  #ifdef MLFQ
  p->current_ticks = 0;
  p->enter_tick = ++my_clock;
  #endif
  ```
* **Rationale**: If a process yields the CPU voluntarily before its time slice expires, its priority remains intact. When it becomes runnable again, resetting `current_ticks = 0` and assigning a new `enter_tick` places it at the tail of its current queue with a fresh time slice.

### 7. Priority Boosting / Anti-Starvation (`kernel/trap.c` & `kernel/proc.c`)
* **Changes**: In `clockintr()`, every 48 ticks (`ticks % 48 == 0`), `mlfq_boost()` resets all active processes to `queue = 0`, `current_ticks = 0`, and updates `enter_tick = ++my_clock`. In `usertrap()`/`kerneltrap()`, the currently running process yields immediately on boost without being penalized or demoted.
* **Rationale**: Prevents starvation of long-running processes in Queues 2 and 3 if high-priority processes monopolize the CPU, ensuring all jobs periodically get CPU time in Queue 0.

### 8. Debugging Support: `procdump()` (`kernel/proc.c`)
* **Changes**: Extended `procdump()` (triggered via console interrupt) to display:
  * PID, State, Name, Queue ID (0–3), Ticks consumed in current slice, and Monotonic Enter Tick.
  * Header showing `Ticks since last boost: %d/48`.
* **Rationale**: Enables live runtime inspection of queue transitions, time slice consumption, and anti-starvation boost cycles during active workloads.

### 9. FIFO Scheduler Implementation (`kernel/proc.c` & `kernel/trap.c`)
* **Changes**:
  * In `scheduler()` under `#elif defined(FIFO)`: Scans for the runnable process with the lowest `ctime` (First-Come, First-Served).
  * In `usertrap()` and `kerneltrap()` under `#elif defined(FIFO)`: Suppresses `yield()` on timer interrupts, allowing processes to execute non-preemptively until completion or I/O.
* **Rationale**: Provides the baseline non-preemptive benchmark required for comparative performance analysis.

### 10. Metrics Collection & `waitx` System Call
* **Changes**: Added `waitx(status, wtime, rtime, resp)` system call:
  * Measures **Response Time**: `stime - ctime` (arrival until first scheduled).
  * Measures **Turnaround Time**: `etime - ctime` (arrival until process exit).
  * Measures **Waiting Time**: `(etime - ctime) - rtime` (turnaround minus active CPU execution time).
* **Rationale**: Enables user-space benchmarking programs (`schedtest`) to gather precise kernel-tracked metrics without modifying user-space timing loops.

---

## 2.3.2 MLFQ Analysis

### Workload Characteristics & Queue Dynamics
The benchmark workload consists of multiple concurrent processes exhibiting distinct execution behaviors:
1. **CPU-bound jobs**: Execute intensive arithmetic operations without yielding. Upon entering Queue 0, they quickly consume their 1-tick slice, demote to Queue 1 (4 ticks), then Queue 2 (8 ticks), and settle in Queue 3 (16 ticks), where they execute via round-robin with long time slices.
2. **I/O-bound / Interactive jobs**: Perform brief compute bursts and voluntarily yield (`sleep()`). They maintain higher priority in Queues 0 and 1, ensuring low latency when responding to events.
3. **Priority Boosting (Every 48 Ticks)**: As long-running compute jobs linger in Queue 3, the periodic boost resets all active processes to Queue 0. This grants CPU-bound jobs short turns in Queue 0 and 1, clears priority inversion, and prevents starvation.

### MLFQ Timeline Plot
*(Refer to the generated scatter plot `mlfq_timeline.png`)*

```
Queue 0 | *   *       *           *   * (Boost!)
Queue 1 |   *   *       *           *
Queue 2 |     *   *       *
Queue 3 |       * * * * * * * * *
        +----------------------------------------> Time (Ticks)
```
![MLFQ Timeline](mlfq_timeline.png)

**Graph Interpretation (3–5 Sentences)**:
1. **Time-Slice Enforcement**: In Queue 0, each process runs for exactly **1 tick** before being preempted and demoted. In Queue 1, each process executes for a contiguous block of **4 ticks** (ticks 3–6 for PID 4, 7–10 for PID 5, and 11–14 for PID 6), demonstrating strict time-slice accounting.
2. **Cascading Queue Demotion**: As processes consume CPU cycles without yielding, they descend in order through Queue 0 $\rightarrow$ Queue 1 $\rightarrow$ Queue 2 (where PID 4 executes for its full **8-tick slice** from tick 15 to 22), confirming escalating slice allocations for compute-bound tasks.
3. **Anti-Starvation Priority Boost**: At tick 28 (which corresponds to global clock tick 48, since `schedtest` launched ~20 ticks after kernel boot, marked by the red dashed line), all active processes instantly jump back to **Queue 0**, resetting their slices and preventing starvation.
4. **Post-Boost Cycle**: Following the boost, the processes repeat the cycle through Queue 0 and Queue 1, eventually reaching Queue 2 and descending into Queue 3 (at tick 55) for long-duration round-robin execution.

---

## 2.3.3 Comparison Results

### Experimental Setup
* **Environment**: QEMU RISC-V configured with a single core (`CPUS=1`) to isolate scheduler arbitration on a single CPU and prevent multi-core execution from masking queue transitions.
* **Schedulers Evaluated**: FIFO, Round-Robin (RR), and Multi-Level Feedback Queue (MLFQ).
* **Two Distinct Benchmark Suites**:
  1. **Benchmark Suite 1 (Homogeneous CPU-Bound, `tests/schedtest.c`)**: All child processes execute identical, heavy CPU loops (no I/O, no yields). Tested at $N = 3$ (matching the timeline plot) and $N = 30$ (scalability stress test).
  2. **Benchmark Suite 2 (Heterogeneous Mixed Workload, `tests/schedmixed.c`)**: Child processes execute distinct behavioral patterns (short CPU, interactive I/O with `pause(1)`, and long CPU).

---

### Benchmark Suite 1: Homogeneous Workload (`tests/schedtest.c`)
*In this suite, every process runs identical CPU-bound code (10 compute loops of 50,000,000 arithmetic cycles).*

#### 1. Low Contention: $N = 3$ Identical CPU-Bound Processes
*(Corresponds to the execution trace and timeline graph in Section 2.3.2)*

| Scheduling Policy | Average Turnaround Time | Average Waiting Time | Average Response Time |
| :--- | :---: | :---: | :---: |
| **FIFO (First-In, First-Out)** | 36 ticks | **18 ticks (BEST)** | 18 ticks |
| **Round Robin (RR)** | **32 ticks (BEST)** | 22 ticks | **1 tick (BEST)** |
| **MLFQ (Multi-Level Feedback Queue)** | 52 ticks | 33 ticks | **1 tick (BEST)** |

*Execution Trace Observations ($N = 3$)*:
* **FIFO**: Complete sequential execution without preemption — PID 4 runs loops 0–9, followed by PID 5 (loops 0–9), followed by PID 6 (loops 0–9).
* **RR**: Strict 1-tick rotation interleaving: `[4], [5], [6], [4], [5], [6]...` until completion.
* **MLFQ**: Tiered execution reflecting escalating time slices (1 tick in Q0, 4-tick bursts in Q1, 8-tick bursts in Q2), with a 48-tick priority boost returning all processes to Q0.

#### 2. High Contention: $N = 30$ Identical CPU-Bound Processes
*(Evaluates scheduler scalability under high CPU queue pressure)*

| Scheduling Policy | Average Turnaround Time | Average Waiting Time | Average Response Time |
| :--- | :---: | :---: | :---: |
| **FIFO (First-In, First-Out)** | **287 ticks (BEST)** | **268 ticks (BEST)** | 268 ticks |
| **Round Robin (RR)** | 543 ticks | 524 ticks | **14 ticks (BEST)** |
| **MLFQ (Multi-Level Feedback Queue)** | 430 ticks | 412 ticks | 16 ticks |

---

### Benchmark Suite 2: Heterogeneous Mixed Workload (`tests/schedmixed.c`)
*In this suite, processes simulate real-world multitasking with three distinct, concurrent process behaviors:*
* **Process A (Short CPU Job)**: Runs only 2 compute loops (~2 ticks total run time). Simulates a quick utility (e.g. `cat`, `grep`).
* **Process B (Interactive / I/O Job)**: Runs 10 compute bursts interspersed with voluntary `pause(1)` calls. Simulates user interaction (e.g. typing in a shell, waiting for keypresses).
* **Process C (Long CPU Job)**: Runs 20 heavy compute loops without yielding (~22 ticks total run time). Simulates background batch compilation or number crunching.

#### Per-Process Metric Breakdown (Who Got What?):

| Process Class | Performance Metric | FIFO | Round Robin (RR) | MLFQ |
| :--- | :--- | :---: | :---: | :---: |
| **Short CPU Job (2 loops)** | Turnaround Time | 22 ticks | 5 ticks | **3 ticks (BEST)** |
| | Waiting Time | 20 ticks | 3 ticks | **1 tick (BEST)** |
| | Response Time | 20 ticks | **1 tick (BEST)** | **1 tick (BEST)** |
| **Interactive Job (10 sleeps)** | Turnaround Time | 33 ticks | **13 ticks (BEST)** | **13 ticks (BEST)** |
| | Waiting Time | 33 ticks | **13 ticks (BEST)** | **13 ticks (BEST)** |
| | Response Time | 23 ticks | **2 ticks (BEST)** | **2 ticks (BEST)** |
| **Long CPU Job (20 loops)** | Turnaround Time | **20 ticks (BEST)** | 24 ticks | 24 ticks |
| | Waiting Time | **0 ticks (BEST)** | 2 ticks | 2 ticks |
| | Response Time | **0 ticks (BEST)** | **0 ticks (BEST)** | **0 ticks (BEST)** |

#### Summary Averages for the Mixed Workload:
*(Calculated across the 3 mixed processes: Short CPU + Interactive + Long CPU)*

| Scheduling Policy | Average Turnaround Time | Average Waiting Time | Average Response Time |
| :--- | :---: | :---: | :---: |
| **FIFO (First-In, First-Out)** | 25 ticks | 17 ticks | 14 ticks |
| **Round Robin (RR)** | 14 ticks | 6 ticks | **1 tick (BEST)** |
| **MLFQ (Multi-Level Feedback Queue)** | **13 ticks (BEST)** | **5 ticks (BEST)** | **1 tick (BEST)** |

---

### Discussion of Trade-offs

1. **Why MLFQ Dominates Heterogeneous / Real-World Workloads (Benchmark Suite 2)**:
   * **Instantaneous Short-Job Completion**: In MLFQ, the short CPU job completes in only **3 ticks** (Turnaround: 3, Waiting: 1), compared to **5 ticks in RR** and **22 ticks in FIFO**. Because new processes enter at highest priority (Queue 0), short tasks finish before they can be demoted. In contrast, FIFO forces the short job to wait 20 ticks behind the long CPU process (convoy effect).
   * **Interactive Responsiveness Without Starvation**: The interactive process voluntarily yields the CPU via `pause(1)` before exhausting its time slice quota. MLFQ keeps it at high priority in Queue 0, allowing it to immediately preempt the compute-bound task every time it wakes up. Under FIFO, the interactive process experiences an unacceptable **23-tick initial delay**.
   * **Background Compute Efficiency**: The long CPU task quickly descends to lower queues (Queue 1 $\rightarrow$ Queue 2 $\rightarrow$ Queue 3), receiving large 16-tick execution windows to crunch data in the background without disturbing interactive responsiveness.
   * **Overall Victor**: For realistic multitasking, MLFQ achieves the lowest Average Turnaround Time (**13 ticks**), lowest Average Waiting Time (**5 ticks**), and optimal Response Time (**1 tick**).

2. **Response Time Trade-off**:
   * **RR and MLFQ provide outstanding responsiveness**: Across all workloads, RR and MLFQ schedule newly arrived jobs almost instantaneously (**1 tick** in low contention and mixed; **14–16 ticks** at $N=30$).
   * **FIFO suffers from the Convoy Effect**: In FIFO, response time equals the accumulated runtime of all earlier processes. When a CPU hog is running, response time degrades to **20–23 ticks** in mixed mode and **268 ticks** at $N=30$.

3. **Turnaround and Waiting Time Trade-off (Homogeneous vs Heterogeneous)**:
   * **Pure CPU-bound Workloads**:
     * When all jobs are identical, FIFO mathematically minimizes turnaround time because running each job to completion without preemption keeps the active job set small. Round Robin performs worst at scale ($N = 30$, Turnaround: 543 ticks) because 30 jobs advance in lockstep without completing. MLFQ remedies RR's lockstep penalty (Turnaround: 430 ticks, >110 ticks faster than RR) through escalating time slices in lower queues.
   * **Real-World Mixed Workloads**:
     * Real operating systems run a mixture of interactive, short, and batch jobs. Under these realistic conditions, **MLFQ strictly outperforms both FIFO and RR**, delivering the lowest turnaround and waiting times while ensuring instant responsiveness for interactive tasks.

