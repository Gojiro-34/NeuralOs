# NeuralOS X

A Mini Operating System Simulator built in C++17 for the CL-2006 Operating Systems Lab (Spring 2026). NeuralOS X simulates core OS concepts — process management, CPU scheduling, memory management, IPC, resource allocation, and deadlock detection — running on a Linux/POSIX host via `fork()`, `exec()`, pipes, and pthreads.

---

## Quick Start

```bash
# Build the kernel
make

# Build and run with defaults (1 GB RAM, 10 GB HDD, 2 cores)
make run

# Run with custom resources
./NeuralOS_X <RAM_GB> <HDD_GB> <CORES>
./NeuralOS_X 2 20 4

# Clean build artifacts
make clean
```

> **Note:** Task binaries must be compiled separately:
> ```bash
> cd src && make -f Makefile.tasks
> ```

---

## Requirements

- Linux / POSIX-compatible OS
- g++ with C++17 support
- pthreads

---

## Project Structure

```
NeuralOS_Phase3/
├── Makefile                  # Top-level build (kernel binary)
├── NeuralOS_X                # Compiled kernel executable
├── include/
│   └── kernel.h              # Core header (PCB, IPC, enums, constants)
├── src/
│   ├── main.cpp              # Kernel entry point & interactive shell
│   ├── scheduler.cpp         # 3-level adaptive scheduler with aging
│   ├── resource_manager.cpp  # RAM/HDD tracking, mood state, IPC grant/deny
│   ├── memory.cpp            # E820 memory map, page allocator
│   ├── process_launcher.cpp  # fork/exec launcher, SIGCHLD reaper
│   ├── deadlock_detector.cpp # RAG-based cycle detection (DFS)
│   ├── Makefile.tasks        # Build rules for all 18 task binaries
│   └── task_*.cpp            # 18 task binaries (see Tasks section)
├── bin/                      # Compiled task binaries
└── logs/
    └── neuralOS_log.txt      # Runtime system log
```

---

## Shell Commands

Once the kernel boots, interact via the `NeuralOS>` prompt:

| Command | Description |
|---|---|
| `<1-18>` | Launch a task by number |
| `ps` | Show running processes and resource state |
| `aging` | Show aging report (priority/wait state per process) |
| `kill <pid>` | Send SIGTERM to a process |
| `kernel` | Enter Kernel Mode Console (`fkill`, `ps`, `exit`) |
| `shutdown` | Graceful system shutdown |
| `?` | Show task menu |

---

## Tasks

| # | Name | RAM | Level | Description |
|---|---|---|---|---|
| 1 | NeuralShell | 32 MB | L0 Reflex | Interactive shell |
| 2 | CogniPad | 24 MB | L1 Focused | Auto-save notepad |
| 3 | CalculatorPro | 12 MB | L0 Reflex | Calculator |
| 4 | AdaptiveClock | 8 MB | L2 Background | Live clock (10s) |
| 5 | SysPulse | 16 MB | L2 Background | System monitor |
| 6 | FileManager | 20 MB | L1 Focused | File operations |
| 7 | FileInfoInspector | 10 MB | L0 Reflex | File info viewer |
| 8 | MusicPlayer | 18 MB | L2 Background | Music player |
| 9 | GhostTyper | 14 MB | L1 Focused | Typewriter effect |
| 10 | Minesweeper | 40 MB | L1 Focused | Minesweeper game |
| 11 | SnakeGame | 36 MB | L1 Focused | Snake game |
| 12 | FileCopySimulator | 22 MB | L2 Background | File copy task |
| 13 | PrintSpooler | 20 MB | L2 Background | Print spooler |
| 14 | MemStressTester | 64 MB | L0 Reflex | Memory stress test |
| 15 | CognitiveHUD | 28 MB | L2 Background | HUD display |
| 16 | DeadlockArena | 30 MB | L1 Focused | Deadlock demo |
| 17 | ProcessGraveyard | 16 MB | L2 Background | Terminated process log |
| 18 | KernelModeConsole | 20 MB | L0 Reflex | Kernel mode console |

All tasks communicate with the kernel over bidirectional pipes and handle `SIGTERM` for graceful shutdown.

---

## Architecture

### Scheduler

Three-level Multi-Level Queue (MLQ):

- **L0 Reflex** — Round Robin
- **L1 Focused** — Priority Queue (highest `effective_priority` first)
- **L2 Background** — FCFS

**Priority Aging** prevents starvation: after 5 idle cycles (`AGING_THRESHOLD`), a process's effective priority increases by 1 per tick (capped at 20). On dispatch, priority and wait counter reset.

**Adaptive Weight Vector** `[w_burst, w_priority, w_age, w_io]` is recalculated every 10 scheduler ticks — boosting age weight under high starvation, boosting burst weight under high context-switch load.

### Memory Manager

Simulates a real x86 E820 memory map with 4 entries. Allocation is page-based (4 KiB pages) using a linked free list. Slab-style pre-allocated pools (`region_pool[64]`, `page_pool[4096]`) avoid dynamic allocation after init.

### Resource Manager & Kernel Mood

Tracks RAM and HDD usage across all processes. The kernel mood escalates based on RAM pressure:

| Mood | Condition | Effect |
|---|---|---|
| CALM | RAM < 70% | Normal operation |
| STRESSED | RAM 70–90% | Logged warning |
| CRITICAL | RAM ≥ 90% | All new launches denied |

### IPC Protocol

Each task communicates via a pair of pipes (`kernel→task`, `task→kernel`) using structured `IPC_Message` packets:

```
RESOURCE_REQUEST → kernel grants or denies → task executes or exits
TASK_DONE        → sent by task on clean exit
HEARTBEAT        → periodic liveness signal
```

Pipe file descriptors are passed as `argv[1]` and `argv[2]` to each `execvp`'d binary.

### Deadlock Detection

A Resource Allocation Graph (RAG) tracks two resource types: RAM (`id=0`) and HDD (`id=1`). Process nodes are encoded as positive integers; resource nodes as negative. An iterative DFS with 3-colour marking detects back-edges (cycles), which indicate deadlock. Affected PIDs are returned for resolution.

---

## Key Constants

| Constant | Value | Description |
|---|---|---|
| `AGING_THRESHOLD` | 5 | Cycles before aging activates |
| `AGING_STEP` | 1 | Priority boost per aging event |
| `TIME_QUANTUM_MS` | 100 | Round-robin time quantum |
| `SCHED_ADAPT_CYCLE` | 10 | Weight adaptation interval |
| `PAGE_SIZE_BYTES` | 4096 | 4 KiB page size |
| `MAX_PROCS` | 32 | Maximum concurrent processes |

---

## Logging

All kernel events (process launches, resource grants/denials, mood transitions, scheduling decisions, priority aging) are timestamped and written to `logs/neuralOS_log.txt` at runtime.

---

## Course

CL-2006 Operating Systems Lab — Spring 2026
