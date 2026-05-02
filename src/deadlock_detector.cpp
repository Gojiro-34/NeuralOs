// ============================================================
// NeuralOS X — Deadlock Detector  (Phase 3)
// CL-2006 Operating Systems Lab | Spring 2026
// ============================================================
// Maintains a Resource Allocation Graph (RAG) and detects
// circular waits among processes using iterative DFS.
//
// Graph model (two resource types: RAM=0, HDD=1):
//   • Allocation edge : resource_id → pid   (resource held by process)
//   • Request edge    : pid → resource_id   (process waiting for resource)
//
// A cycle in this bipartite graph indicates deadlock.
// ============================================================

#include "deadlock_detector.h"

#include <cstdio>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <stack>
#include <algorithm>
#include <pthread.h>

// ──────────────────────────────────────────────────────────────
//  Resource IDs
// ──────────────────────────────────────────────────────────────
static constexpr int NUM_RESOURCES = 2;   // 0 = RAM, 1 = HDD

static const char* resource_name(int id) {
    switch (id) {
        case 0: return "RAM";
        case 1: return "HDD";
        default: return "UNKNOWN";
    }
}

// ──────────────────────────────────────────────────────────────
//  RAG State
// ──────────────────────────────────────────────────────────────
// holder[resource_id] = pid of the process currently holding it
//                       (0  = resource is free)
static pid_t holder[NUM_RESOURCES] = {0, 0};

// waiters[resource_id] = set of pids waiting for that resource
static std::unordered_set<pid_t> waiters[NUM_RESOURCES];

// all_pids: union of every pid currently in the graph
static std::unordered_set<pid_t> all_pids;

static pthread_mutex_t rag_mutex = PTHREAD_MUTEX_INITIALIZER;

// ──────────────────────────────────────────────────────────────
//  RAG Mutation
// ──────────────────────────────────────────────────────────────
void rag_add_request(pid_t pid, int resource_id) {
    if (resource_id < 0 || resource_id >= NUM_RESOURCES) return;
    pthread_mutex_lock(&rag_mutex);
    waiters[resource_id].insert(pid);
    all_pids.insert(pid);
    pthread_mutex_unlock(&rag_mutex);

    printf("[DEADLOCK] RAG: pid=%d waiting for %s\n",
           pid, resource_name(resource_id));
}

void rag_add_allocation(pid_t pid, int resource_id) {
    if (resource_id < 0 || resource_id >= NUM_RESOURCES) return;
    pthread_mutex_lock(&rag_mutex);
    // Resource granted: remove from waiters, record as holder
    waiters[resource_id].erase(pid);
    holder[resource_id] = pid;
    all_pids.insert(pid);
    pthread_mutex_unlock(&rag_mutex);

    printf("[DEADLOCK] RAG: pid=%d allocated %s\n",
           pid, resource_name(resource_id));
}

void rag_remove(pid_t pid) {
    pthread_mutex_lock(&rag_mutex);

    // Release all resources held by pid
    for (int r = 0; r < NUM_RESOURCES; r++) {
        if (holder[r] == pid)
            holder[r] = 0;
        waiters[r].erase(pid);
    }
    all_pids.erase(pid);

    pthread_mutex_unlock(&rag_mutex);

    printf("[DEADLOCK] RAG: pid=%d removed from graph\n", pid);
}

// ──────────────────────────────────────────────────────────────
//  Cycle Detection — DFS on the RAG
// ──────────────────────────────────────────────────────────────
// The RAG is bipartite: process nodes (pid) and resource nodes
// (resource_id, encoded as negative ints to avoid collision with pids).
//
// Encoding:  process node  =  (int)pid
//            resource node =  -(resource_id + 1)   [so 0 maps to -1]
//
// Edges from a process node  p:  p → -(r+1)  for each resource r pid p waits for
// Edges from a resource node r:  -(r+1) → holder[r]  if resource is held
//
// We run DFS from every unvisited process node, looking for back-edges.
// ──────────────────────────────────────────────────────────────

// Build adjacency list from a snapshot of the RAG.
// Keys and values are the encoded integers described above.
static std::unordered_map<int, std::vector<int>>
build_graph_snapshot(pid_t snap_holder[NUM_RESOURCES],
                     std::unordered_set<pid_t> snap_waiters[NUM_RESOURCES],
                     const std::unordered_set<pid_t>& snap_pids)
{
    std::unordered_map<int, std::vector<int>> adj;

    // Process → resource edges  (request edges)
    for (int r = 0; r < NUM_RESOURCES; r++) {
        int res_node = -(r + 1);
        for (pid_t p : snap_waiters[r]) {
            adj[(int)p].push_back(res_node);
        }
    }

    // Resource → process edges  (allocation edges)
    for (int r = 0; r < NUM_RESOURCES; r++) {
        if (snap_holder[r] != 0) {
            int res_node = -(r + 1);
            adj[res_node].push_back((int)snap_holder[r]);
        }
    }

    // Ensure every known pid has an entry (even if no outgoing edges)
    for (pid_t p : snap_pids) {
        adj.emplace((int)p, std::vector<int>{});
    }

    return adj;
}

// Iterative DFS; fills `cycle` with the nodes forming the cycle if found.
static bool dfs_find_cycle(int start,
                            std::unordered_map<int, std::vector<int>>& adj,
                            std::unordered_set<int>& visited_global,
                            std::vector<int>& cycle)
{
    // colour: 0 = white (unseen), 1 = grey (on stack), 2 = black (done)
    std::unordered_map<int, int> colour;
    std::unordered_map<int, int> parent;   // for path reconstruction

    struct Frame { int node; size_t edge_idx; };
    std::stack<Frame> stk;

    colour[start] = 1;
    stk.push({start, 0});

    while (!stk.empty()) {
        Frame& f = stk.top();
        int u = f.node;

        auto it = adj.find(u);
        if (it == adj.end() || f.edge_idx >= it->second.size()) {
            // All neighbours processed — colour black and pop
            colour[u] = 2;
            visited_global.insert(u);
            stk.pop();
            continue;
        }

        int v = it->second[f.edge_idx++];

        if (colour.count(v) == 0) colour[v] = 0;   // ensure entry

        if (colour[v] == 1) {
            // Back-edge found: reconstruct the cycle
            cycle.clear();
            cycle.push_back(v);
            // Walk the stack to extract the cycle path
            std::stack<Frame> tmp = stk;
            std::vector<int> path;
            while (!tmp.empty()) {
                path.push_back(tmp.top().node);
                tmp.pop();
            }
            std::reverse(path.begin(), path.end());
            bool in_cycle = false;
            for (int node : path) {
                if (node == v) in_cycle = true;
                if (in_cycle) cycle.push_back(node);
            }
            return true;
        }

        if (colour[v] == 0) {
            colour[v] = 1;
            parent[v] = u;
            stk.push({v, 0});
        }
        // colour[v] == 2 → already fully processed, skip
    }
    return false;
}

// ──────────────────────────────────────────────────────────────
//  Public API: detect_deadlock
// ──────────────────────────────────────────────────────────────
bool detect_deadlock(std::vector<pid_t>& cycle_pids) {
    cycle_pids.clear();

    // Take a snapshot under the lock so we don't hold it during DFS
    pthread_mutex_lock(&rag_mutex);
    pid_t snap_holder[NUM_RESOURCES];
    std::unordered_set<pid_t> snap_waiters[NUM_RESOURCES];
    std::unordered_set<pid_t> snap_pids = all_pids;
    for (int r = 0; r < NUM_RESOURCES; r++) {
        snap_holder[r]  = holder[r];
        snap_waiters[r] = waiters[r];
    }
    pthread_mutex_unlock(&rag_mutex);

    if (snap_pids.empty()) return false;

    auto adj = build_graph_snapshot(snap_holder, snap_waiters, snap_pids);

    std::unordered_set<int> visited_global;
    std::vector<int> raw_cycle;

    // Run DFS from every unvisited process node
    for (pid_t p : snap_pids) {
        int node = (int)p;
        if (visited_global.count(node)) continue;
        raw_cycle.clear();
        if (dfs_find_cycle(node, adj, visited_global, raw_cycle)) {
            // Extract only process nodes (positive) from the cycle
            for (int n : raw_cycle) {
                if (n > 0)
                    cycle_pids.push_back((pid_t)n);
            }
            // Deduplicate while preserving order
            std::vector<pid_t> deduped;
            std::unordered_set<pid_t> seen;
            for (pid_t pid : cycle_pids) {
                if (seen.insert(pid).second)
                    deduped.push_back(pid);
            }
            cycle_pids = deduped;
            return true;
        }
    }
    return false;
}
