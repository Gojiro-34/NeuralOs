// ============================================================
// NeuralOS X — Resource Manager  (Phase 3)
// CL-2006 Operating Systems Lab | Spring 2026
// ============================================================
// Implements: IPC-based grant/deny, RAM & HDD enforcement,
// kernel mood state transitions (CALM / STRESSED / CRITICAL),
// process table management.
// ============================================================

#include "../include/kernel.h"
#include "memory.cpp"    // pull in memory subsystem

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cstdarg>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

// ──────────────────────────────────────────────────────────────
//  Process Table
// ──────────────────────────────────────────────────────────────
static constexpr int MAX_PROCS = 32;
static PCB   proc_table[MAX_PROCS];
static int   proc_count = 0;
static pthread_mutex_t proc_table_mutex = PTHREAD_MUTEX_INITIALIZER;

// ──────────────────────────────────────────────────────────────
//  Resource Tracking
// ──────────────────────────────────────────────────────────────
static int32_t max_ram_mb       = 0;
static int32_t current_ram_used = 0;
static int32_t max_hdd_mb       = 0;
static int32_t current_hdd_used = 0;   // persists across task lifecycles

static pthread_mutex_t resource_mutex = PTHREAD_MUTEX_INITIALIZER;

// ──────────────────────────────────────────────────────────────
//  Kernel Mood State
// ──────────────────────────────────────────────────────────────
static KernelMood kernel_mood = KernelMood::CALM;

static const char* mood_str(KernelMood m) {
    switch (m) {
        case KernelMood::CALM:     return "CALM";
        case KernelMood::STRESSED: return "STRESSED";
        case KernelMood::CRITICAL: return "CRITICAL";
    }
    return "UNKNOWN";
}

static void update_kernel_mood() {
    double ram_pct = (max_ram_mb > 0)
                     ? (100.0 * current_ram_used / max_ram_mb)
                     : 0.0;

    KernelMood prev = kernel_mood;

    if (ram_pct >= 90.0)      kernel_mood = KernelMood::CRITICAL;
    else if (ram_pct >= 70.0) kernel_mood = KernelMood::STRESSED;
    else                       kernel_mood = KernelMood::CALM;

    if (kernel_mood != prev)
        printf("[KERNEL] Mood transition: %s → %s  (RAM %.1f%%)\n",
               mood_str(prev), mood_str(kernel_mood), ram_pct);
}

// ──────────────────────────────────────────────────────────────
//  System Log File
// ──────────────────────────────────────────────────────────────
static FILE* log_file = nullptr;

static uint64_t now_ms() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000ULL + tv.tv_usec / 1000ULL;
}

static void log_event(const char* fmt, ...) {
    if (!log_file) return;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(log_file, "[%llu ms] [%s] %s\n",
            (unsigned long long)now_ms(), mood_str(kernel_mood), buf);
    fflush(log_file);
}

// ──────────────────────────────────────────────────────────────
//  Resource Manager Init
// ──────────────────────────────────────────────────────────────
void resource_manager_init(int32_t ram_mb, int32_t hdd_mb) {
    max_ram_mb = ram_mb;
    max_hdd_mb = hdd_mb;
    current_ram_used = 0;
    current_hdd_used = 0;
    proc_count = 0;
    memset(proc_table, 0, sizeof(proc_table));

    log_file = fopen("logs/neuralOS_log.txt", "a");
    if (!log_file) {
        // try current dir
        log_file = fopen("neuralOS_log.txt", "a");
    }

    printf("[RM] Resource Manager initialised: RAM=%d MiB  HDD=%d MiB\n",
           max_ram_mb, max_hdd_mb);
    log_event("Resource Manager init: RAM=%d MiB HDD=%d MiB", ram_mb, hdd_mb);
}

// ──────────────────────────────────────────────────────────────
//  Find a free PCB slot
// ──────────────────────────────────────────────────────────────
static PCB* find_free_pcb() {
    for (int i = 0; i < MAX_PROCS; i++)
        if (proc_table[i].state == ProcState::TERMINATED ||
            proc_table[i].pid == 0)
            return &proc_table[i];
    return nullptr;
}

static PCB* find_pcb_by_pid(pid_t p) {
    for (int i = 0; i < MAX_PROCS; i++)
        if (proc_table[i].pid == p)
            return &proc_table[i];
    return nullptr;
}

// ──────────────────────────────────────────────────────────────
//  Grant / Deny Logic
// ──────────────────────────────────────────────────────────────
static bool resource_check(int32_t ram_mb, int32_t hdd_mb) {
    pthread_mutex_lock(&resource_mutex);
    bool ok = ((current_ram_used + ram_mb) <= max_ram_mb) &&
              ((current_hdd_used + hdd_mb) <= max_hdd_mb);
    // CRITICAL mood: deny all new launches
    if (kernel_mood == KernelMood::CRITICAL) ok = false;
    pthread_mutex_unlock(&resource_mutex);
    return ok;
}

static void resource_commit(int32_t ram_mb, int32_t hdd_mb) {
    pthread_mutex_lock(&resource_mutex);
    current_ram_used += ram_mb;
    current_hdd_used += hdd_mb;
    update_kernel_mood();
    pthread_mutex_unlock(&resource_mutex);
}

static void resource_release(int32_t ram_mb, int32_t /*hdd_mb*/) {
    pthread_mutex_lock(&resource_mutex);
    current_ram_used -= ram_mb;
    if (current_ram_used < 0) current_ram_used = 0;
    // HDD usage persists (files remain on disk) — do NOT subtract hdd_mb
    update_kernel_mood();
    pthread_mutex_unlock(&resource_mutex);
}

// ──────────────────────────────────────────────────────────────
//  Handle IPC Request from a Child Process
//  Returns true if GRANTED, false if DENIED.
// ──────────────────────────────────────────────────────────────
bool handle_resource_request(IPC_Message* req, int reply_fd, PCB* pcb) {
    bool granted = resource_check(req->ram_mb, req->hdd_mb);

    IPC_Message reply;
    reply.pid     = req->pid;
    reply.ram_mb  = req->ram_mb;
    reply.hdd_mb  = req->hdd_mb;
    strncpy(reply.task_name, req->task_name, 31);

    if (granted) {
        reply.type = IPC_MsgType::RESOURCE_GRANTED;
        resource_commit(req->ram_mb, req->hdd_mb);

        pthread_mutex_lock(&proc_table_mutex);
        pcb->state   = ProcState::READY;
        pcb->ram_mb  = req->ram_mb;
        pcb->hdd_mb  = req->hdd_mb;
        pthread_mutex_unlock(&proc_table_mutex);

        printf("[RM] GRANTED  pid=%-6d  task=%-24s  RAM=%3d MiB  HDD=%3d MiB"
               "  (used %d/%d MiB RAM)\n",
               req->pid, req->task_name, req->ram_mb, req->hdd_mb,
               current_ram_used, max_ram_mb);
        log_event("GRANTED pid=%d task=%s ram=%d hdd=%d",
                  req->pid, req->task_name, req->ram_mb, req->hdd_mb);
    } else {
        reply.type = IPC_MsgType::RESOURCE_DENIED;

        pthread_mutex_lock(&proc_table_mutex);
        pcb->state = ProcState::TERMINATED;
        pthread_mutex_unlock(&proc_table_mutex);

        printf("[RM] DENIED   pid=%-6d  task=%-24s  RAM=%3d MiB  HDD=%3d MiB"
               "  (used %d/%d MiB RAM, mood=%s)\n",
               req->pid, req->task_name, req->ram_mb, req->hdd_mb,
               current_ram_used, max_ram_mb, mood_str(kernel_mood));
        log_event("DENIED pid=%d task=%s reason=insufficient_resources mood=%s",
                  req->pid, req->task_name, mood_str(kernel_mood));
    }

    write(reply_fd, &reply, sizeof(reply));
    return granted;
}

// ──────────────────────────────────────────────────────────────
//  Release resources when a process terminates
// ──────────────────────────────────────────────────────────────
void on_process_exit(pid_t pid) {
    pthread_mutex_lock(&proc_table_mutex);
    PCB* pcb = find_pcb_by_pid(pid);
    if (!pcb) { pthread_mutex_unlock(&proc_table_mutex); return; }

    int32_t ram = pcb->ram_mb;
    int32_t hdd = pcb->hdd_mb;
    pcb->state  = ProcState::TERMINATED;
    pthread_mutex_unlock(&proc_table_mutex);

    resource_release(ram, hdd);
    mem_print_stats();

    log_event("TERMINATED pid=%d name=%s ram_freed=%d", pid, pcb->name, ram);
    printf("[RM] Process pid=%d (%s) terminated — released %d MiB RAM\n",
           pid, pcb->name, ram);
}

// ──────────────────────────────────────────────────────────────
//  Print current resource state
// ──────────────────────────────────────────────────────────────
void print_resource_state() {
    printf("\n[RM] ──── Resource Snapshot ────────────────────────────\n");
    printf("  RAM:  %d / %d MiB used  (%.1f%%)\n",
           current_ram_used, max_ram_mb,
           max_ram_mb ? 100.0*current_ram_used/max_ram_mb : 0.0);
    printf("  HDD:  %d / %d MiB used  (%.1f%%)\n",
           current_hdd_used, max_hdd_mb,
           max_hdd_mb ? 100.0*current_hdd_used/max_hdd_mb : 0.0);
    printf("  Mood: %s\n", mood_str(kernel_mood));
    printf("  Procs in table: %d\n", proc_count);

    pthread_mutex_lock(&proc_table_mutex);
    for (int i = 0; i < MAX_PROCS; i++) {
        PCB& p = proc_table[i];
        if (p.pid == 0) continue;
        const char* st = "?";
        switch (p.state) {
            case ProcState::READY:      st = "READY";      break;
            case ProcState::RUNNING:    st = "RUNNING";    break;
            case ProcState::BLOCKED:    st = "BLOCKED";    break;
            case ProcState::TERMINATED: st = "TERMINATED"; break;
            case ProcState::ZOMBIE:     st = "ZOMBIE";     break;
        }
        if (p.state != ProcState::TERMINATED)
            printf("    pid=%-6d  %-24s  %-10s  RAM=%d MiB\n",
                   p.pid, p.name, st, p.ram_mb);
    }
    pthread_mutex_unlock(&proc_table_mutex);
    printf("[RM] ──────────────────────────────────────────────────\n\n");
}

// ──────────────────────────────────────────────────────────────
//  Expose pcb helpers to scheduler
// ──────────────────────────────────────────────────────────────
PCB* get_proc_table()   { return proc_table; }
int  get_proc_count()   { return MAX_PROCS; }

PCB* register_process(pid_t pid, const char* name, Archetype arch,
                      SchedLevel level, int base_priority,
                      int read_fd, int write_fd) {
    pthread_mutex_lock(&proc_table_mutex);
    PCB* pcb = find_free_pcb();
    if (!pcb) { pthread_mutex_unlock(&proc_table_mutex); return nullptr; }

    memset(pcb, 0, sizeof(PCB));
    pcb->pid               = pid;
    strncpy(pcb->name, name, 31);
    pcb->state             = ProcState::BLOCKED;  // waiting for resource grant
    pcb->archetype         = arch;
    pcb->level             = level;
    pcb->base_priority     = base_priority;
    pcb->effective_priority= base_priority;
    pcb->wait_cycles       = 0;
    pcb->burst_estimate    = 50;  // default burst estimate ms
    pcb->read_fd           = read_fd;
    pcb->write_fd          = write_fd;
    pcb->arrival_time_ms   = now_ms();
    proc_count++;
    pthread_mutex_unlock(&proc_table_mutex);
    return pcb;
}
