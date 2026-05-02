// ============================================================
// NeuralOS X — Adaptive Scheduler  (Phase 3)
// CL-2006 Operating Systems Lab | Spring 2026
// ============================================================
// Implements: 3-level ready queue, round-robin (L0), priority
// scheduling with aging (L1), FCFS (L2), adaptive weight vector
// update every SCHED_ADAPT_CYCLE cycles.
// ============================================================

#include "../include/kernel.h"
#include "resource_manager.cpp"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>

// ──────────────────────────────────────────────────────────────
//  Three-Level Ready Queues
//  L0 — Reflex   → Round Robin
//  L1 — Focused  → Priority + aging
//  L2 — Background → FCFS
// ──────────────────────────────────────────────────────────────
static constexpr int QUEUE_MAX = 32;

struct ReadyQueue {
    int  pids[QUEUE_MAX];
    int  head, tail, count;
};

static ReadyQueue q[3];   // q[0]=L0, q[1]=L1, q[2]=L2

static void queue_init(ReadyQueue& q) { q.head = q.tail = q.count = 0; }

static bool queue_push(ReadyQueue& q, int pid) {
    if (q.count >= QUEUE_MAX) return false;
    q.pids[q.tail] = pid;
    q.tail = (q.tail + 1) % QUEUE_MAX;
    q.count++;
    return true;
}

static int queue_pop_fifo(ReadyQueue& q) {
    if (q.count == 0) return -1;
    int pid = q.pids[q.head];
    q.head  = (q.head + 1) % QUEUE_MAX;
    q.count--;
    return pid;
}

// Pop highest effective_priority from q[1] (L1)
static int queue_pop_priority(ReadyQueue& q) {
    if (q.count == 0) return -1;
    PCB* table = get_proc_table();
    int  n     = get_proc_count();

    int best_idx = -1, best_prio = -1;
    // find index of highest-priority pid in queue
    for (int qi = 0; qi < q.count; qi++) {
        int real_i = (q.head + qi) % QUEUE_MAX;
        int pid    = q.pids[real_i];
        for (int ti = 0; ti < n; ti++) {
            if (table[ti].pid == pid && table[ti].effective_priority > best_prio) {
                best_prio = table[ti].effective_priority;
                best_idx  = real_i;
            }
        }
    }
    if (best_idx < 0) return queue_pop_fifo(q); // fallback

    int pid = q.pids[best_idx];
    // shift entries to fill gap
    int steps = (best_idx - q.head + QUEUE_MAX) % QUEUE_MAX;
    for (int s = 0; s < steps; s++) {
        int cur  = (q.head + s)     % QUEUE_MAX;
        int next = (q.head + s + 1) % QUEUE_MAX;
        q.pids[cur] = q.pids[next];
    }
    q.head  = (q.head + 1) % QUEUE_MAX;
    q.count--;
    return pid;
}

// ──────────────────────────────────────────────────────────────
//  Adaptive Weight Vector
// ──────────────────────────────────────────────────────────────
static WeightVector weights;
static int   sched_cycle      = 0;
static int   starvation_count = 0;   // procs that waited > threshold
static int   ctx_switches     = 0;

static void adapt_weights() {
    // If starvation high → boost w_age to help waiting processes
    if (starvation_count > 2) {
        weights.w_age      += 0.05f;
        weights.w_priority -= 0.03f;
        weights.w_burst    -= 0.02f;
    }
    // If context switches high → boost w_burst (prefer longer bursts)
    if (ctx_switches > 15) {
        weights.w_burst    += 0.04f;
        weights.w_io       -= 0.02f;
        weights.w_priority -= 0.02f;
    }
    // Clamp each weight to [0.05, 0.70]
    auto clamp = [](float v){ return v < 0.05f ? 0.05f : (v > 0.70f ? 0.70f : v); };
    weights.w_burst    = clamp(weights.w_burst);
    weights.w_priority = clamp(weights.w_priority);
    weights.w_age      = clamp(weights.w_age);
    weights.w_io       = clamp(weights.w_io);

    // Re-normalise so they sum to 1.0
    float total = weights.w_burst + weights.w_priority + weights.w_age + weights.w_io;
    weights.w_burst    /= total;
    weights.w_priority /= total;
    weights.w_age      /= total;
    weights.w_io       /= total;

    printf("[SCHED] Weight update cycle %d: burst=%.2f prio=%.2f age=%.2f io=%.2f\n",
           sched_cycle, weights.w_burst, weights.w_priority, weights.w_age, weights.w_io);

    starvation_count = 0;
    ctx_switches     = 0;
}

// ──────────────────────────────────────────────────────────────
//  Priority Aging
//  "If a process waits too long in the ready queue, its priority
//   should gradually increase (aging technique) to prevent
//   starvation."  — Project Manual
//
//  Mechanism:
//    • Every sched_tick(), wait_cycles increments for READY procs.
//    • Once wait_cycles >= AGING_THRESHOLD, effective_priority
//      increases by AGING_STEP each tick (capped at 20).
//    • When the process is dispatched, wait_cycles resets to 0 and
//      effective_priority resets to base_priority (see sched_tick).
//    • Each promotion is logged to the system log file.
// ──────────────────────────────────────────────────────────────
static void apply_aging() {
    PCB* table = get_proc_table();
    int  n     = get_proc_count();
    for (int i = 0; i < n; i++) {
        PCB& p = table[i];
        if (p.pid == 0 || p.state != ProcState::READY) continue;
        p.wait_cycles++;
        if (p.wait_cycles >= AGING_THRESHOLD) {
            int old_prio = p.effective_priority;
            p.effective_priority += AGING_STEP;
            if (p.effective_priority > 20) p.effective_priority = 20; // cap

            // Log every promotion event to the system log file
            if (p.effective_priority != old_prio) {
                log_event("AGING pid=%d name=%s base=%d effective=%d→%d wait_cycles=%d",
                          p.pid, p.name, p.base_priority,
                          old_prio, p.effective_priority, p.wait_cycles);
            }

            if (p.wait_cycles % 10 == 0) starvation_count++;
        }
    }
}

// ──────────────────────────────────────────────────────────────
//  Enqueue a process into the appropriate level
// ──────────────────────────────────────────────────────────────
bool sched_enqueue(PCB* pcb) {
    int lvl = (int)pcb->level;
    bool ok = queue_push(q[lvl], (int)pcb->pid);
    if (ok)
        printf("[SCHED] Enqueued  pid=%d (%s)  → L%d\n",
               pcb->pid, pcb->name, lvl);
    return ok;
}

// ──────────────────────────────────────────────────────────────
//  Pick next process to run (highest level first)
// ──────────────────────────────────────────────────────────────
static pid_t sched_pick_next() {
    // L0 Reflex — always first (RR)
    if (q[0].count > 0) return queue_pop_fifo(q[0]);
    // L1 Focused — priority queue
    if (q[1].count > 0) return queue_pop_priority(q[1]);
    // L2 Background — FCFS
    if (q[2].count > 0) return queue_pop_fifo(q[2]);
    return -1;
}

// ──────────────────────────────────────────────────────────────
//  Scheduler tick — called from main scheduler loop
// ──────────────────────────────────────────────────────────────
pid_t sched_tick() {
    sched_cycle++;
    apply_aging();

    if (sched_cycle % SCHED_ADAPT_CYCLE == 0)
        adapt_weights();

    pid_t next = sched_pick_next();
    if (next != -1) {
        PCB* table = get_proc_table();
        int  n     = get_proc_count();
        for (int i = 0; i < n; i++) {
            if (table[i].pid == next) {
                table[i].state       = ProcState::RUNNING;
                table[i].wait_cycles = 0;
                table[i].effective_priority = table[i].base_priority; // reset after run
                break;
            }
        }
        ctx_switches++;
        printf("[SCHED] Dispatch pid=%d  cycle=%d\n", next, sched_cycle);
    }
    return next;
}

// ──────────────────────────────────────────────────────────────
//  Scheduler init
// ──────────────────────────────────────────────────────────────
void scheduler_init() {
    queue_init(q[0]);
    queue_init(q[1]);
    queue_init(q[2]);
    sched_cycle      = 0;
    starvation_count = 0;
    ctx_switches     = 0;
    // default weights
    weights = { 0.25f, 0.35f, 0.25f, 0.15f };
    printf("[SCHED] Scheduler initialised. Weights: burst=%.2f prio=%.2f age=%.2f io=%.2f\n",
           weights.w_burst, weights.w_priority, weights.w_age, weights.w_io);
}

void print_scheduler_state() {
    printf("[SCHED] Queues — L0(Reflex):%d  L1(Focused):%d  L2(Background):%d\n",
           q[0].count, q[1].count, q[2].count);
}

// ──────────────────────────────────────────────────────────────
//  Aging Report — prints the priority/aging state of every process
// ──────────────────────────────────────────────────────────────
void print_aging_report() {
    PCB* table = get_proc_table();
    int  n     = get_proc_count();

    printf("\n[SCHED] ──── Aging Report (cycle %d) ────────────────────\n", sched_cycle);
    printf("  %-6s  %-20s  %-6s  %-6s  %-6s  %s\n",
           "PID", "Name", "Base", "Eff.", "Wait", "State");
    printf("  %-6s  %-20s  %-6s  %-6s  %-6s  %s\n",
           "------", "--------------------", "------", "------", "------", "----------");

    for (int i = 0; i < n; i++) {
        PCB& p = table[i];
        if (p.pid == 0) continue;

        const char* st = "?";
        switch (p.state) {
            case ProcState::READY:      st = "READY";      break;
            case ProcState::RUNNING:    st = "RUNNING";    break;
            case ProcState::BLOCKED:    st = "BLOCKED";    break;
            case ProcState::TERMINATED: st = "TERMINATED"; break;
            case ProcState::ZOMBIE:     st = "ZOMBIE";     break;
        }

        if (p.state == ProcState::TERMINATED) continue;

        printf("  %-6d  %-20s  %-6d  %-6d  %-6d  %s",
               p.pid, p.name, p.base_priority,
               p.effective_priority, p.wait_cycles, st);

        // Mark processes that have been aged
        if (p.effective_priority > p.base_priority)
            printf("  ↑ aged +%d", p.effective_priority - p.base_priority);

        printf("\n");
    }
    printf("[SCHED] ──────────────────────────────────────────────────\n\n");
}
