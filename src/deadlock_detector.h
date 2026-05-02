#pragma once
// ============================================================
// NeuralOS X — Deadlock Detector  (Phase 3)
// CL-2006 Operating Systems Lab | Spring 2026
// ============================================================
// Resource Allocation Graph (RAG) with DFS-based cycle detection.
// Resource IDs:  0 = RAM,  1 = HDD
// ============================================================

#include <sys/types.h>
#include <vector>

// ── RAG mutation ─────────────────────────────────────────────
// Call when a process requests a resource (not yet granted).
void rag_add_request(pid_t pid, int resource_id);

// Call immediately after the resource is granted to the process.
void rag_add_allocation(pid_t pid, int resource_id);

// Call when a process terminates — releases all holds and requests.
void rag_remove(pid_t pid);

// ── Cycle detection ──────────────────────────────────────────
// Returns true if a deadlock cycle exists among currently blocked
// processes.  cycle_pids is filled with the PIDs in the cycle
// (in detection order); it is cleared before the search.
// Thread-safe: acquires the internal RAG mutex.
bool detect_deadlock(std::vector<pid_t>& cycle_pids);
