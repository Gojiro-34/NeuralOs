// ============================================================
// NeuralOS X — Task Binary: CalculatorPro  (task_calculator)
// CL-2006 Operating Systems Lab | Spring 2026
// ============================================================
// An interactive task that prompts the user for two numbers and
// an arithmetic operator (+, -, *, /, %).  Displays the result
// and loops until the user types "exit".
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
#include <cstdlib>
#include <cstring>
#include <cmath>
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

// ── Globals ──────────────────────────────────────────────────
static volatile sig_atomic_t g_terminate = 0;
static int g_read_fd  = -1;
static int g_write_fd = -1;

// ── SIGTERM handler ─────────────────────────────────────────
static void sigterm_handler(int) {
    g_terminate = 1;
}

// ── Send TASK_DONE and exit cleanly ─────────────────────────
static void send_task_done_and_exit() {
    IPC_Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = IPC_MsgType::TASK_DONE;
    msg.pid  = getpid();
    strncpy(msg.task_name, "CalculatorPro", 31);

    if (g_write_fd >= 0) {
        write(g_write_fd, &msg, sizeof(msg));
        close(g_write_fd);
    }
    if (g_read_fd >= 0) close(g_read_fd);

    printf("[CalculatorPro] Task finished (pid=%d). Goodbye!\n", getpid());
    _exit(0);
}

// ── main ────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "[CalculatorPro] Usage: %s <read_fd> <write_fd>\n", argv[0]);
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

    printf("[CalculatorPro] Started (pid=%d). Type 'exit' to quit.\n", getpid());
    printf("─────────────────────────────────────────────────────\n");

    char line[256];

    while (!g_terminate) {
        printf("\n[CalculatorPro] Enter expression (num1 op num2) or 'exit': ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            // EOF on stdin — treat as exit
            break;
        }

        // Strip trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        // Check for exit command
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            printf("[CalculatorPro] User requested exit.\n");
            break;
        }

        // Parse: <num1> <op> <num2>
        double a = 0, b = 0;
        char   op = '\0';

        int parsed = sscanf(line, "%lf %c %lf", &a, &op, &b);
        if (parsed != 3) {
            printf("[CalculatorPro] ⚠  Invalid format. Use: <num1> <op> <num2>\n");
            printf("                   Operators: +  -  *  /  %%\n");
            continue;
        }

        double result = 0;
        bool   valid  = true;

        switch (op) {
            case '+': result = a + b; break;
            case '-': result = a - b; break;
            case '*': result = a * b; break;
            case '/':
                if (b == 0.0) {
                    printf("[CalculatorPro] ⚠  Division by zero!\n");
                    valid = false;
                } else {
                    result = a / b;
                }
                break;
            case '%':
                if (b == 0.0) {
                    printf("[CalculatorPro] ⚠  Modulo by zero!\n");
                    valid = false;
                } else {
                    result = fmod(a, b);
                }
                break;
            default:
                printf("[CalculatorPro] ⚠  Unknown operator '%c'. Use +, -, *, /, %%\n", op);
                valid = false;
                break;
        }

        if (valid) {
            printf("[CalculatorPro] ✓  %.6g %c %.6g = %.6g\n", a, op, b, result);
        }
    }

    if (g_terminate) {
        printf("[CalculatorPro] Received SIGTERM — shutting down.\n");
    }

    send_task_done_and_exit();
    return 0;   // unreachable
}
