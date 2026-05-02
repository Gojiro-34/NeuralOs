// ============================================================
// NeuralOS X — Task: MemStressTester
// CL-2006 Operating Systems Lab | Spring 2026
// ------------------------------------------------------------
// Allocates memory in increasing chunks in a loop, reports
// total allocated and system response, then frees everything
// and exits after 5 seconds. Demonstrates memory pressure.
// Receives pipe FDs via argv[1] (read) and argv[2] (write).
// Sends TASK_DONE on finish. Handles SIGTERM.
// ============================================================

#include "../include/kernel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <vector>

static volatile bool g_quit = false;
static void handle_sigterm(int) { g_quit = true; }

static void send_task_done(int write_fd) {
    IPC_Message msg;
    msg.type   = IPC_MsgType::TASK_DONE;
    msg.pid    = getpid();
    msg.ram_mb = 0; msg.hdd_mb = 0;
    strncpy(msg.task_name, "MemStressTester", 31);
    write(write_fd, &msg, sizeof(msg));
}

int main(int argc, char* argv[]) {
    int read_fd  = (argc >= 2) ? atoi(argv[1]) : -1;
    int write_fd = (argc >= 3) ? atoi(argv[2]) : -1;

    signal(SIGTERM, handle_sigterm);

    /*
     * Allocates chunks of 1 MiB per iteration using malloc().
     * Writes a pattern to each chunk to ensure pages are actually
     * committed by the OS (not just reserved). Reports progress
     * every second. Frees all memory before exiting.
     */
    printf("\n╔══════════════════════════════════╗\n");
    printf("║  NeuralOS X — Mem Stress Tester  ║\n");
    printf("╚══════════════════════════════════╝\n");
    printf("  Running for 5 seconds...\n\n");

    std::vector<void*> chunks;
    const size_t CHUNK = 1 * 1024 * 1024; // 1 MiB
    int elapsed = 0;
    const int DURATION = 5;

    while (elapsed < DURATION && !g_quit) {
        void* p = malloc(CHUNK);
        if (p) {
            memset(p, 0xAB, CHUNK); // commit the pages
            chunks.push_back(p);
        }

        long total_mb = (long)chunks.size();
        printf("  [%d/%ds] Allocated: %ld MiB total  (%s)\n",
               elapsed + 1, DURATION, total_mb,
               p ? "OK" : "malloc failed");
        fflush(stdout);

        sleep(1);
        elapsed++;
    }

    // Free everything
    for (void* p : chunks) free(p);
    printf("\n  [MS] Released %zu MiB. Stress test complete.\n",
           chunks.size());

    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
