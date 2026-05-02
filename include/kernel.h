#pragma once
// ============================================================
// NeuralOS X — Phase 3 Core Header
// CL-2006 Operating Systems Lab | Spring 2026
// ============================================================

#include <cstdint>
#include <cstring>
#include <string>
#include <sys/types.h>

// ──────────────────────────────────────────────────────────────
//  E820 Memory Map (matching instructor hint at 0x9000 / 0x9008)
// ──────────────────────────────────────────────────────────────
struct E820 {
    uint64_t base;   // Start address of region
    uint64_t length; // Length in bytes
    uint32_t type;   // 1 = usable RAM, 2 = reserved, 3 = ACPI, 4 = NVS, 5 = bad
    uint32_t attrs;  // Extended attributes (bit 0 = valid if ACPI ≥ 3.0)
};

// ──────────────────────────────────────────────────────────────
//  Free Page / Region Tracking  (instructor-mandated structs)
// ──────────────────────────────────────────────────────────────
struct FreeRegion {
    uint64_t base;
    uint64_t length;
    FreeRegion* next;
};

struct Page {
    uint64_t address;   // Physical base address of page
    bool      in_use;
    Page*     next;
};

// ──────────────────────────────────────────────────────────────
//  IPC Request / Response  (sent child → kernel over pipe)
// ──────────────────────────────────────────────────────────────
enum class IPC_MsgType : uint8_t {
    RESOURCE_REQUEST = 1,
    RESOURCE_GRANTED,
    RESOURCE_DENIED,
    TASK_DONE,
    HEARTBEAT
};

struct IPC_Message {
    IPC_MsgType type;
    pid_t       pid;
    char        task_name[32];
    int32_t     ram_mb;
    int32_t     hdd_mb;
};

// ──────────────────────────────────────────────────────────────
//  Process States
// ──────────────────────────────────────────────────────────────
enum class ProcState { READY, RUNNING, BLOCKED, TERMINATED, ZOMBIE };

// ──────────────────────────────────────────────────────────────
//  Process Archetype
// ──────────────────────────────────────────────────────────────
enum class Archetype { REFLEX, FOCUSED, BACKGROUND, DORMANT };

// ──────────────────────────────────────────────────────────────
//  Scheduler Level
// ──────────────────────────────────────────────────────────────
enum class SchedLevel { L0_REFLEX = 0, L1_FOCUSED = 1, L2_BACKGROUND = 2 };

// ──────────────────────────────────────────────────────────────
//  Process Control Block (PCB)
// ──────────────────────────────────────────────────────────────
struct PCB {
    pid_t      pid;
    char       name[32];
    ProcState  state;
    Archetype  archetype;
    SchedLevel level;

    int32_t    ram_mb;
    int32_t    hdd_mb;

    int        base_priority;   // 1-10, higher = more urgent
    int        effective_priority;
    int        wait_cycles;     // cycles spent waiting → drives aging
    int        burst_estimate;  // estimated remaining CPU burst (ms)

    int        read_fd;         // pipe end kernel reads responses from
    int        write_fd;        // pipe end kernel writes grants to

    // Stats
    uint64_t   cpu_time_ms;
    uint64_t   arrival_time_ms;
};

// ──────────────────────────────────────────────────────────────
//  Kernel Mood State
// ──────────────────────────────────────────────────────────────
enum class KernelMood { CALM, STRESSED, CRITICAL };

// ──────────────────────────────────────────────────────────────
//  Adaptive Weight Vector  W = [w_burst, w_priority, w_age, w_io]
// ──────────────────────────────────────────────────────────────
struct WeightVector {
    float w_burst    = 0.25f;
    float w_priority = 0.35f;
    float w_age      = 0.25f;
    float w_io       = 0.15f;
};

// ──────────────────────────────────────────────────────────────
//  Pipe pair helpers
// ──────────────────────────────────────────────────────────────
struct PipePair {
    int fds[2]; // fds[0] = read end, fds[1] = write end
};

// Aging coefficient – raise effective priority for long-waiting procs
constexpr int   AGING_STEP        = 1;    // priority points per long wait
constexpr int   AGING_THRESHOLD   = 5;    // cycles before aging kicks in
constexpr int   TIME_QUANTUM_MS   = 100;  // default RR quantum
constexpr int   SCHED_ADAPT_CYCLE = 10;   // adapt weights every N cycles
constexpr int   PAGE_SIZE_BYTES   = 4096; // 4 KiB pages
