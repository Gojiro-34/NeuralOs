// ============================================================
// NeuralOS X — Task Binary: AdaptiveClock  (task_clock)
// CL-2006 Operating Systems Lab | Spring 2026
// ============================================================
// A background task that prints the current local time once per
// second.  It auto-terminates after 10 seconds of execution.
//
// Pipe protocol:
//   argv[1] = read  fd  (kernel → task, unused here)
//   argv[2] = write fd  (task → kernel, TASK_DONE on exit)
//
// The resource request/grant handshake is done by the kernel
// *before* exec, so this binary does NOT send one.
//
// Handles SIGTERM: sets a flag, cleans up, and exits gracefully.
// ============================================================

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>

// IPC message struct (must match kernel.h layout exactly) 
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

// Globals 
static volatile sig_atomic_t g_terminate = 0;
static int g_read_fd  = -1;
static int g_write_fd = -1;

// SIGTERM handler
static void sigterm_handler(int) {
    g_terminate = 1;
}

// Send TASK_DONE and exit cleanly 
static void send_task_done_and_exit() {
    IPC_Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_MsgType::TASK_DONE;
    msg.pid  = getpid();
    strncpy(msg.task_name, "AdaptiveClock", 31);

    if (g_write_fd >= 0) {
        write(g_write_fd, &msg, sizeof(msg));
        close(g_write_fd);
    }
    if (g_read_fd >= 0) close(g_read_fd);

    printf("[AdaptiveClock] Task finished (pid=%d). Goodbye!\n", getpid());
    _exit(0);
}

//  main 
int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "[AdaptiveClock] Usage: %s <read_fd> <write_fd>\n", argv[0]);
        return 1;
    }

    g_read_fd  = atoi(argv[1]);
    g_write_fd = atoi(argv[2]);

    // Install SIGTERM handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigterm_handler;
    sa.sa_flags   = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);

    printf("[AdaptiveClock] Started (pid=%d). Will run continuously.\n", getpid());

    int elapsed = 0;
    while (!g_terminate) {
        time_t now = time(nullptr);
        struct tm* lt = localtime(&now);

        char timebuf[64];
        strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", lt);
        // Using \r to overwrite the same line instead of \n
        printf("\r[AdaptiveClock] [%02ds elapsed] %s   ", ++elapsed, timebuf);
        fflush(stdout);

        sleep(1);
    }

    if (g_terminate) {
        printf("\n[AdaptiveClock] Received SIGTERM — shutting down.\n");
    }

    send_task_done_and_exit();
    return 0;   // unreachable
}
