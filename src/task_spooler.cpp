// ============================================================
// NeuralOS X — Task: PrintSpooler
// CL-2006 Operating Systems Lab | Spring 2026
// ------------------------------------------------------------
// Background task that simulates printing a document by
// printing fake document lines one by one with a delay,
// showing a page/line counter. Auto-exits when done.
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
    strncpy(msg.task_name, "PrintSpooler", 31);
    write(write_fd, &msg, sizeof(msg));
}

int main(int argc, char* argv[]) {
    int read_fd  = (argc >= 2) ? atoi(argv[1]) : -1;
    int write_fd = (argc >= 3) ? atoi(argv[2]) : -1;

    signal(SIGTERM, handle_sigterm);

    /*
     * Simulates spooling a 2-page document to a printer.
     * Each line is printed with a 300ms delay to simulate
     * the mechanical pace of a real printer.
     */
    const char* doc[] = {
        "NeuralOS X — System Report",
        "==========================",
        "Date: 2026-05-02",
        "Prepared by: Kernel v3.0",
        "",
        "Section 1: Process Summary",
        "  Total processes launched : 18",
        "  Active processes         : 5",
        "  Terminated processes     : 13",
        "",
        "Section 2: Memory Usage",
        "  RAM Total  : 2048 MiB",
        "  RAM Used   : 148 MiB",
        "  RAM Free   : 1900 MiB",
        "",
        "Section 3: Scheduler Stats",
        "  Context switches : 42",
        "  Avg wait cycles  : 3.2",
        "  Starvation events: 0",
        "",
        "-- End of Report --",
    };
    int lines = (int)(sizeof(doc) / sizeof(doc[0]));

    printf("\n╔══════════════════════════════════╗\n");
    printf("║   NeuralOS X — Print Spooler     ║\n");
    printf("╚══════════════════════════════════╝\n");
    printf("  [SPOOLER] Sending document to printer...\n\n");

    for (int i = 0; i < lines && !g_quit; i++) {
        printf("  [LINE %02d/%02d] %s\n", i + 1, lines, doc[i]);
        fflush(stdout);
        usleep(300000); // 300ms per line
    }

    printf("\n  [SPOOLER] Print job complete.\n");
    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
