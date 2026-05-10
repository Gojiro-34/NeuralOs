// ============================================================
// NeuralOS X — Task Binary: SysPulse  (task_syspulse)
// CL-2006 Operating Systems Lab | Spring 2026
// ============================================================
// A background system-monitor task that prints simulated RAM
// and CPU usage statistics every 2 seconds.  Auto-terminates
// after 8 seconds of execution.
//
// Pipe protocol:
//   argv[1] = read  fd  (kernel → task, unused here)
//   argv[2] = write fd  (task → kernel, TASK_DONE on exit)
//
// Handles SIGTERM: cleans up and exits gracefully.
// ============================================================

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>

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

static volatile sig_atomic_t g_terminate = 0;
static int g_read_fd  = -1;
static int g_write_fd = -1;

static void sigterm_handler(int) { g_terminate = 1; }

static void send_task_done_and_exit() {
    IPC_Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_MsgType::TASK_DONE;
    msg.pid  = getpid();
    strncpy(msg.task_name, "SysPulse", 31);
    if (g_write_fd >= 0) { write(g_write_fd, &msg, sizeof(msg)); close(g_write_fd); }
    if (g_read_fd >= 0) close(g_read_fd);
    printf("[SysPulse] Task finished (pid=%d). Goodbye!\n", getpid());
    _exit(0);
}

static double clamp(double v, double lo, double hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "[SysPulse] Usage: %s <read_fd> <write_fd>\n", argv[0]);
        return 1;
    }
    g_read_fd  = atoi(argv[1]);
    g_write_fd = atoi(argv[2]);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);

    srand((unsigned)(getpid() ^ time(nullptr)));

    printf("[SysPulse] Started (pid=%d). Will run continuously.\n", getpid());

    const int INTERVAL = 2, TOTAL_RAM = 512;
    double cpu = 15.0 + (rand() % 30);
    double ram = 120.0 + (rand() % 100);

    bool first = true;
    for (int t = 0; !g_terminate; t += INTERVAL) {
        cpu = clamp(cpu + (rand() % 21) - 10, 2.0, 98.0);
        ram = clamp(ram + (rand() % 41) - 20, 50.0, TOTAL_RAM - 20.0);
        double rpct = 100.0 * ram / TOTAL_RAM;

        char cb[21] = {}, rb[21] = {};
        for (int i = 0; i < 20; i++) {
            cb[i] = (i < (int)(cpu / 5.0)) ? '#' : '-';
            rb[i] = (i < (int)(rpct / 5.0)) ? '#' : '-';
        }

        if (!first) {
            printf("\033[4A"); // Move cursor up 4 lines
        }
        first = false;

        // Extra spaces added at the end to clear any leftover characters
        printf("[SysPulse] T+%02ds ────────────────────────────      \n", t);
        printf("  CPU:  [%s] %5.1f%%          \n", cb, cpu);
        printf("  RAM:  [%s] %5.1f%%  (%d/%d MiB)          \n", rb, rpct, (int)ram, TOTAL_RAM);
        printf("  Load: %.2f  %.2f  %.2f          \n",
               cpu/100*2, cpu/100*1.8+0.1, cpu/100*1.5+0.2);
        fflush(stdout);
        sleep(INTERVAL);
    }

    if (g_terminate) printf("\n[SysPulse] Received SIGTERM — shutting down.\n");
    send_task_done_and_exit();
    return 0;
}
