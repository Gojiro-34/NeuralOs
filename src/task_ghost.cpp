// ============================================================
// NeuralOS X — Task: GhostTyper
// CL-2006 Operating Systems Lab | Spring 2026
// ------------------------------------------------------------
// Types out random sentences character by character with a
// realistic delay between keystrokes, simulating a ghost
// typing on the terminal. Runs through 5 sentences then exits.
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
    strncpy(msg.task_name, "GhostTyper", 31);
    write(write_fd, &msg, sizeof(msg));
}

static void ghost_type(const char* sentence) {
    /*
     * Prints each character of the sentence one at a time with
     * a randomised delay of 40–120ms to simulate human typing.
     * Flushes stdout after each character for live display.
     */
    for (int i = 0; sentence[i] != '\0' && !g_quit; i++) {
        putchar(sentence[i]);
        fflush(stdout);
        int delay = 40000 + (rand() % 80000); // 40–120 ms
        usleep(delay);
    }
    printf("\n");
    usleep(600000); // pause between sentences
}

int main(int argc, char* argv[]) {
    int read_fd  = (argc >= 2) ? atoi(argv[1]) : -1;
    int write_fd = (argc >= 3) ? atoi(argv[2]) : -1;

    signal(SIGTERM, handle_sigterm);
    srand(getpid());

    const char* sentences[] = {
        "The kernel awakens from its slumber...",
        "Memory pages drift like leaves in the wind.",
        "Processes are born, they run, they die.",
        "The scheduler watches all with patient eyes.",
        "In the silence between clock ticks, the OS thinks."
    };
    int count = (int)(sizeof(sentences) / sizeof(sentences[0]));

    printf("\n╔══════════════════════════════════╗\n");
    printf("║   NeuralOS X — Ghost Typer       ║\n");
    printf("╚══════════════════════════════════╝\n\n");

    for (int i = 0; i < count && !g_quit; i++) {
        printf("  > ");
        fflush(stdout);
        ghost_type(sentences[i]);
    }

    printf("\n  [Ghost] The ghost has spoken.\n");
    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
