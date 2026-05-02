// ============================================================
// NeuralOS X — Task: CognitiveHUD
// CL-2006 Operating Systems Lab | Spring 2026
// ------------------------------------------------------------
// Background task that displays a live system HUD showing
// simulated CPU usage, RAM usage, and disk I/O stats.
// Updates every 2 seconds for 20 seconds then auto-exits.
// Receives pipe FDs via argv[1] (read) and argv[2] (write).
// Sends TASK_DONE on finish. Handles SIGTERM.
// ============================================================

#include "../include/kernel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <ctime>

static volatile bool g_quit = false;
static void handle_sigterm(int) { g_quit = true; }

static void send_task_done(int write_fd) {
    IPC_Message msg;
    msg.type   = IPC_MsgType::TASK_DONE;
    msg.pid    = getpid();
    msg.ram_mb = 0; msg.hdd_mb = 0;
    strncpy(msg.task_name, "CognitiveHUD", 31);
    write(write_fd, &msg, sizeof(msg));
}

static void print_bar(const char* label, int pct) {
    /*
     * Prints a labelled ASCII bar chart row for a given
     * percentage value (0–100). Bar width is 20 characters.
     */
    int filled = pct / 5;
    printf("  %-8s [", label);
    for (int i = 0; i < 20; i++)
        printf("%c", i < filled ? '█' : '░');
    printf("] %3d%%\n", pct);
}

int main(int argc, char* argv[]) {
    int read_fd  = (argc >= 2) ? atoi(argv[1]) : -1;
    int write_fd = (argc >= 3) ? atoi(argv[2]) : -1;

    signal(SIGTERM, handle_sigterm);
    srand((unsigned)time(nullptr) ^ getpid());

    printf("\n╔══════════════════════════════════╗\n");
    printf("║   NeuralOS X — Cognitive HUD     ║\n");
    printf("╚══════════════════════════════════╝\n");

    int ticks = 0;
    const int MAX_TICKS = 10; // 10 × 2s = 20s

    // Simulated baseline values
    int cpu  = 20 + rand() % 20;
    int ram  = 30 + rand() % 20;
    int disk = 5  + rand() % 10;

    while (ticks < MAX_TICKS && !g_quit) {
        // Drift values slightly each tick
        cpu  = cpu  + (rand() % 11 - 5); if (cpu  < 5)  cpu  = 5;  if (cpu  > 95) cpu  = 95;
        ram  = ram  + (rand() % 7  - 3); if (ram  < 10) ram  = 10; if (ram  > 90) ram  = 90;
        disk = disk + (rand() % 9  - 4); if (disk < 1)  disk = 1;  if (disk > 80) disk = 80;

        time_t now = time(nullptr);
        char tbuf[32];
        strftime(tbuf, sizeof(tbuf), "%H:%M:%S", localtime(&now));

        printf("\n  ── HUD Snapshot @ %s (tick %d/%d) ──\n", tbuf, ticks+1, MAX_TICKS);
        print_bar("CPU",  cpu);
        print_bar("RAM",  ram);
        print_bar("DISK", disk);
        printf("  Uptime: %d sec  |  Processes: %d\n", ticks * 2, 3 + ticks % 5);
        fflush(stdout);

        sleep(2);
        ticks++;
    }

    printf("\n  [HUD] CognitiveHUD shutting down.\n");
    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
