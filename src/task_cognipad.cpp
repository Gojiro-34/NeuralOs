// ============================================================
// NeuralOS X — Task Binary: CogniPad  (task_cognipad)
// CL-2006 Operating Systems Lab | Spring 2026
// ============================================================
// An interactive notepad task.  The user types lines of text
// which are buffered in memory and auto-saved to a file on
// disk (cognipad_<timestamp>.txt).
//
// Commands:
//   :save     — force-write buffer to disk
//   :quit     — save and exit
//   :exit     — save and exit
//   (any text) — append to the note buffer
//
// Pipe protocol:
//   argv[1] = read  fd  (kernel → task, unused here)
//   argv[2] = write fd  (task → kernel, TASK_DONE on exit)
//
// The resource request/grant handshake is done by the kernel
// *before* exec, so this binary does NOT send one.
//
// Handles SIGTERM: auto-saves, sends TASK_DONE, and exits.
// ============================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <csignal>
#include <unistd.h>
#include <sys/types.h>

// ── IPC message struct (must match kernel.h layout exactly) ──
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

// ── Buffer constants ─────────────────────────────────────────
static constexpr int MAX_LINES  = 1024;
static constexpr int LINE_LEN   = 512;

// ── Globals ──────────────────────────────────────────────────
static volatile sig_atomic_t g_terminate = 0;
static int g_read_fd  = -1;
static int g_write_fd = -1;

static char  g_buffer[MAX_LINES][LINE_LEN];
static int   g_line_count = 0;
static char  g_filename[128];

// ── SIGTERM handler ─────────────────────────────────────────
static void sigterm_handler(int) {
    g_terminate = 1;
}

// ── Save buffer to disk ──────────────────────────────────────
static void save_to_disk() {
    FILE* fp = fopen(g_filename, "w");
    if (!fp) {
        fprintf(stderr, "[CogniPad] ⚠  Could not open '%s' for writing.\n", g_filename);
        return;
    }
    for (int i = 0; i < g_line_count; ++i) {
        fprintf(fp, "%s\n", g_buffer[i]);
    }
    fclose(fp);
    printf("[CogniPad] ✓  Saved %d line(s) to '%s'\n", g_line_count, g_filename);
}

// ── Send TASK_DONE and exit cleanly ─────────────────────────
static void send_task_done_and_exit() {
    // Auto-save before exiting
    if (g_line_count > 0) {
        save_to_disk();
    }

    IPC_Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_MsgType::TASK_DONE;
    msg.pid  = getpid();
    strncpy(msg.task_name, "CogniPad", 31);

    if (g_write_fd >= 0) {
        write(g_write_fd, &msg, sizeof(msg));
        close(g_write_fd);
    }
    if (g_read_fd >= 0) close(g_read_fd);

    printf("[CogniPad] Task finished (pid=%d). Goodbye!\n", getpid());
    _exit(0);
}

// ── main ────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "[CogniPad] Usage: %s <read_fd> <write_fd>\n", argv[0]);
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

    // Generate timestamped filename
    time_t now = time(nullptr);
    struct tm* lt = localtime(&now);
    snprintf(g_filename, sizeof(g_filename),
             "cognipad_%04d%02d%02d_%02d%02d%02d.txt",
             lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday,
             lt->tm_hour, lt->tm_min, lt->tm_sec);

    printf("[CogniPad] Started (pid=%d). File: %s\n", getpid(), g_filename);
    printf("[CogniPad] Commands: :save  :quit  :exit\n");
    printf("─────────────────────────────────────────────────────\n");

    char line[LINE_LEN];

    while (!g_terminate) {
        printf("[CogniPad %03d] > ", g_line_count + 1);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            // EOF — auto-save and exit
            break;
        }

        // Strip trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        // Handle commands
        if (strcmp(line, ":quit") == 0 || strcmp(line, ":exit") == 0) {
            printf("[CogniPad] User requested exit.\n");
            break;
        }

        if (strcmp(line, ":save") == 0) {
            save_to_disk();
            continue;
        }

        // Append to buffer
        if (g_line_count < MAX_LINES) {
            strncpy(g_buffer[g_line_count], line, LINE_LEN - 1);
            g_buffer[g_line_count][LINE_LEN - 1] = '\0';
            g_line_count++;
        } else {
            printf("[CogniPad] ⚠  Buffer full (%d lines). Use :save and :quit.\n", MAX_LINES);
        }

        // Auto-save every 5 lines
        if (g_line_count > 0 && g_line_count % 5 == 0) {
            printf("[CogniPad] Auto-saving...\n");
            save_to_disk();
        }
    }

    if (g_terminate) {
        printf("[CogniPad] Received SIGTERM — auto-saving and shutting down.\n");
    }

    send_task_done_and_exit();
    return 0;   // unreachable
}
