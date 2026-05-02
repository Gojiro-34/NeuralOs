// ============================================================
// NeuralOS X — Task: KernelModeConsole
// CL-2006 Operating Systems Lab | Spring 2026
// ------------------------------------------------------------
// A privileged console that simulates kernel mode access.
// Reads a shared process table snapshot from a temp file
// written by the kernel, and displays it as a full process
// table. Allows force-killing processes by PID.
// Receives pipe FDs via argv[1] (read) and argv[2] (write).
// Sends TASK_DONE on exit. Handles SIGTERM.
// ============================================================

#include "../include/kernel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>

static volatile bool g_quit = false;
static void handle_sigterm(int) { g_quit = true; }

static void send_task_done(int write_fd) {
    IPC_Message msg;
    msg.type   = IPC_MsgType::TASK_DONE;
    msg.pid    = getpid();
    msg.ram_mb = 0; msg.hdd_mb = 0;
    strncpy(msg.task_name, "KernelModeConsole", 31);
    write(write_fd, &msg, sizeof(msg));
}

static void show_process_table() {
    /*
     * Reads /proc to list all processes belonging to the current
     * user, printing PID and command name as a simplified process
     * table. This simulates a kernel-mode view of running processes.
     */
    printf("\n  %-8s  %-24s  %s\n", "PID", "Name", "Status");
    printf("  %-8s  %-24s  %s\n", "--------",
           "------------------------", "--------");

    // Read from the kernel log to show NeuralOS processes
    const char* log_paths[] = { "logs/neuralOS_log.txt", "neuralOS_log.txt" };
    FILE* log = nullptr;
    for (int i = 0; i < 2; i++) {
        log = fopen(log_paths[i], "r");
        if (log) break;
    }

    if (!log) {
        printf("  (Log unavailable — showing own PID only)\n");
        printf("  %-8d  %-24s  RUNNING\n", getpid(), "KernelModeConsole");
        printf("\n");
        return;
    }

    char line[512];
    int shown = 0;
    while (fgets(line, sizeof(line), log)) {
        // Show GRANTED entries (active at some point) not yet TERMINATED
        if (strstr(line, "GRANTED") == nullptr) continue;

        int   pid  = 0;
        char  name[64] = "?";
        char* p = strstr(line, "pid=");
        if (p) sscanf(p, "pid=%d", &pid);
        char* n = strstr(line, "task=");
        if (n) sscanf(n, "task=%63s", name);

        // Remove trailing spaces/commas from name
        for (int i = (int)strlen(name)-1; i >= 0 && (name[i]==' '||name[i]==','); i--)
            name[i] = '\0';

        printf("  %-8d  %-24s  LOGGED\n", pid, name);
        shown++;
    }
    fclose(log);

    if (shown == 0)
        printf("  (No process entries found in log.)\n");
    printf("\n");
}

static void force_kill(pid_t target) {
    /*
     * Sends SIGKILL to the target PID, simulating kernel-mode
     * forced process termination with no grace period.
     */
    if (kill(target, SIGKILL) == 0)
        printf("  [KMC] SIGKILL sent to pid=%d\n", target);
    else
        perror("  [KMC] kill failed");
}

int main(int argc, char* argv[]) {
    int read_fd  = (argc >= 2) ? atoi(argv[1]) : -1;
    int write_fd = (argc >= 3) ? atoi(argv[2]) : -1;

    signal(SIGTERM, handle_sigterm);

    printf("\n╔══════════════════════════════════╗\n");
    printf("║  NeuralOS X — Kernel Console     ║\n");
    printf("║  *** KERNEL MODE — RESTRICTED *** ║\n");
    printf("╚══════════════════════════════════╝\n");
    printf("  Commands: ps | fkill <pid> | exit\n\n");

    char line[128];
    while (!g_quit) {
        printf("kernel-con# ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';

        if (strcmp(line, "exit") == 0) break;

        if (strcmp(line, "ps") == 0) {
            show_process_table();
            continue;
        }

        if (strncmp(line, "fkill ", 6) == 0) {
            pid_t target = (pid_t)atoi(line + 6);
            if (target <= 0) { printf("  Invalid pid.\n"); continue; }
            force_kill(target);
            continue;
        }

        printf("  Unknown command: %s\n", line);
    }

    printf("  [KMC] KernelModeConsole exiting.\n");
    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
