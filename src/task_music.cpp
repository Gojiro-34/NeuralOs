// ============================================================
// NeuralOS X — Task: MusicPlayer
// CL-2006 Operating Systems Lab | Spring 2026
// ------------------------------------------------------------
// Background task that simulates playing a song. Prints a
// progress bar updating every second for 15 seconds, then exits.
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
    strncpy(msg.task_name, "MusicPlayer", 31);
    write(write_fd, &msg, sizeof(msg));
}

static void print_progress_bar(int elapsed, int total) {
    /*
     * Prints an ASCII progress bar showing elapsed/total seconds.
     * Bar width is 30 characters. Overwrites the current line
     * using carriage return so it appears animated.
     */
    int bar_width = 30;
    int filled    = (elapsed * bar_width) / total;

    printf("\r  ♪ Playing... [");
    for (int i = 0; i < bar_width; i++)
        printf("%c", i < filled ? '█' : '░');
    printf("] %02d:%02d / 00:15  ", elapsed / 60, elapsed % 60);
    fflush(stdout);
}

int main(int argc, char* argv[]) {
    int read_fd  = (argc >= 2) ? atoi(argv[1]) : -1;
    int write_fd = (argc >= 3) ? atoi(argv[2]) : -1;

    signal(SIGTERM, handle_sigterm);

    const int DURATION = 15;
    const char* tracks[] = {
        "NeuralBeats - Kernel Groove",
        "SysPulse - Memory Lane",
        "AdaptiveClock - Tick Tock Remix"
    };
    const char* track = tracks[getpid() % 3];

    printf("\n╔══════════════════════════════════╗\n");
    printf("║   NeuralOS X — Music Player      ║\n");
    printf("╚══════════════════════════════════╝\n");
    printf("  Now playing: %s\n\n", track);

    for (int t = 0; t <= DURATION && !g_quit; t++) {
        print_progress_bar(t, DURATION);
        sleep(1);
    }

    printf("\n  ♪ Playback complete.\n");
    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
