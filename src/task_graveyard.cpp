// ============================================================
// NeuralOS X — Task: ProcessGraveyard
// CL-2006 Operating Systems Lab | Spring 2026
// ------------------------------------------------------------
// Reads the system log file (logs/neuralOS_log.txt) and lists
// all terminated process entries found in it, formatted as a
// "graveyard" of dead processes with their names and timestamps.
// Receives pipe FDs via argv[1] (read) and argv[2] (write).
// Sends TASK_DONE on finish. Handles SIGTERM.
// ============================================================

#include "../include/kernel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>

static volatile bool g_quit = false;
static void handle_sigterm(int) { g_quit = true; }

static void send_task_done(int write_fd) {
    IPC_Message msg;
    msg.type   = IPC_MsgType::TASK_DONE;
    msg.pid    = getpid();
    msg.ram_mb = 0; msg.hdd_mb = 0;
    strncpy(msg.task_name, "ProcessGraveyard", 31);
    write(write_fd, &msg, sizeof(msg));
}

int main(int argc, char* argv[]) {
    int read_fd  = (argc >= 2) ? atoi(argv[1]) : -1;
    int write_fd = (argc >= 3) ? atoi(argv[2]) : -1;

    signal(SIGTERM, handle_sigterm);

    printf("\n╔══════════════════════════════════╗\n");
    printf("║  NeuralOS X — Process Graveyard  ║\n");
    printf("╚══════════════════════════════════╝\n");
    printf("  Reading system log for terminated processes...\n\n");

    /*
     * Searches logs/neuralOS_log.txt for lines containing
     * "TERMINATED". Extracts the timestamp and process name
     * and prints them in a formatted graveyard table.
     */
    const char* log_paths[] = { "logs/neuralOS_log.txt", "neuralOS_log.txt" };
    FILE* log = nullptr;
    for (int i = 0; i < 2; i++) {
        log = fopen(log_paths[i], "r");
        if (log) break;
    }

    if (!log) {
        printf("  [GY] No log file found. No processes have died yet.\n");
        goto done;
    }

    {
        char line[512];
        int  count = 0;

        printf("  %-20s  %-24s  %s\n", "Timestamp (ms)", "Process Name", "RAM Freed");
        printf("  %-20s  %-24s  %s\n", "--------------------",
               "------------------------", "---------");

        while (fgets(line, sizeof(line), log) && !g_quit) {
            if (strstr(line, "TERMINATED") == nullptr) continue;

            // Example log line:
            // [1234567890 ms] [CALM] TERMINATED pid=42 name=CogniPad ram_freed=24
            char ts[32] = "?";
            char name[64] = "?";
            int  ram = 0;

            // Extract timestamp from brackets
            char* ts_start = strchr(line, '[');
            char* ts_end   = ts_start ? strchr(ts_start, ']') : nullptr;
            if (ts_start && ts_end) {
                int len = (int)(ts_end - ts_start - 1);
                if (len > 0 && len < 30) {
                    strncpy(ts, ts_start + 1, len);
                    ts[len] = '\0';
                }
            }

            // Extract name= field
            char* n = strstr(line, "name=");
            if (n) sscanf(n, "name=%63s", name);

            // Extract ram_freed= field
            char* r = strstr(line, "ram_freed=");
            if (r) sscanf(r, "ram_freed=%d", &ram);

            printf("  %-20s  %-24s  %d MiB\n", ts, name, ram);
            count++;
        }

        if (count == 0)
            printf("  (No terminated processes found in log.)\n");
        else
            printf("\n  Total processes in graveyard: %d\n", count);

        fclose(log);
    }

done:
    printf("\n  Press Enter to exit...\n");
    while (!g_quit) {
        int ch = getchar();
        if (ch == '\n' || ch == EOF) break;
    }

    printf("\n  [GY] ProcessGraveyard exiting.\n");
    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
