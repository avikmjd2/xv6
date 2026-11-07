import os
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def parse_and_plot(log_path="log.txt", output_path="mlfq_timeline.png"):
    if not os.path.exists(log_path):
        print(f"Error: {log_path} not found.")
        return

    data_by_pid = {}
    ticks_all = []

    with open(log_path, "r", errors="ignore") as f:
        for line in f:
            if "[PLOT]" in line:
                parts = line.strip().split()
                try:
                    idx = parts.index("[PLOT]")
                    tick = int(parts[idx + 1])
                    pid = int(parts[idx + 2])
                    queue = int(parts[idx + 3])
                    
                    # Ignore init (PID 1) and sh (PID 2)
                    if pid in (1, 2):
                        continue

                    if pid not in data_by_pid:
                        data_by_pid[pid] = {"ticks": [], "queues": []}
                    data_by_pid[pid]["ticks"].append(tick)
                    data_by_pid[pid]["queues"].append(queue)
                    ticks_all.append(tick)
                except (ValueError, IndexError):
                    continue

    if not data_by_pid:
        print("No [PLOT] lines found for test processes in log.txt.")
        return

    min_tick = min(ticks_all)
    max_tick = max(ticks_all)

    fig, ax = plt.subplots(figsize=(12, 6))

    # Clean, distinct color palette for processes
    palette = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd', '#8c564b']

    sorted_pids = sorted(data_by_pid.keys())
    for i, pid in enumerate(sorted_pids):
        info = data_by_pid[pid]
        rel_ticks = [t - min_tick for t in info["ticks"]]
        
        # Slight vertical offset (+/- 0.05) so multiple processes at the same queue don't overlap completely
        offset = (i - len(sorted_pids) / 2.0) * 0.06
        shifted_queues = [q + offset for q in info["queues"]]

        c = palette[i % len(palette)]
        ax.scatter(rel_ticks, shifted_queues, label=f"Process (PID {pid})", 
                   color=c, s=35, alpha=0.9, edgecolors='none')

    # Draw vertical red dashed lines for Priority Boosts every 48 ticks
    boost_period = 48
    first_boost = boost_period - (min_tick % boost_period)
    duration = max_tick - min_tick
    b = first_boost
    boost_labeled = False
    while b <= duration:
        label = 'Priority Boost (every 48 ticks)' if not boost_labeled else ""
        ax.axvline(x=b, color='red', linestyle='--', alpha=0.75, linewidth=1.5, label=label)
        boost_labeled = True
        b += boost_period

    ax.set_yticks([0, 1, 2, 3])
    ax.set_yticklabels(["Queue 0 (Highest, 1 tick)", 
                        "Queue 1 (4 ticks)", 
                        "Queue 2 (8 ticks)", 
                        "Queue 3 (Lowest, 16 ticks)"])

    ax.set_xlabel("Time Elapsed (ticks)", fontsize=12, fontweight='bold')
    ax.set_ylabel("Priority Queue", fontsize=12, fontweight='bold')
    ax.set_title("MLFQ Scheduler: Process Queue Progression Over Time", fontsize=14, fontweight='bold')
    ax.grid(True, linestyle='--', alpha=0.4)
    ax.set_ylim(3.5, -0.5)  # Inverted so Queue 0 is at top, Queue 3 is at bottom
    ax.legend(loc='upper right', framealpha=0.9)

    # Add watermark
    fig.text(0.5, 0.5, 'avik.majumder', fontsize=40, color='gray',
             ha='center', va='center', alpha=0.2, rotation=30)

    plt.tight_layout()
    plt.savefig(output_path, dpi=300)
    print(f"Success! Clean plot saved to {output_path}")

if __name__ == "__main__":
    parse_and_plot()
