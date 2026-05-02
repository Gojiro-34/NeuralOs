// ============================================================
// NeuralOS X — Task Binary: NeuralShell  (task_shell)
// CL-2006 Operating Systems Lab | Spring 2026
// ============================================================
// An interactive mini-shell.  Displays a prompt and accepts
// basic commands:
//   ls           — list files in the current directory
//   pwd          — print working directory
//   echo <text>  — echo the rest of the line
//   help         — show available commands
//   clear        — clear the screen
//   exit / quit  — exit the shell
//
// Pipe protocol:
//   argv[1] = read  fd  (kernel → task, unused here)
//   argv[2] = write fd  (task → kernel, TASK_DONE on exit)
//
// Handles SIGTERM: cleans up and exits gracefully.
// ============================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>

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
    strncpy(msg.task_name, "NeuralShell", 31);
    if (g_write_fd >= 0) { write(g_write_fd, &msg, sizeof(msg)); close(g_write_fd); }
    if (g_read_fd >= 0) close(g_read_fd);
    printf("[NeuralShell] Shell exited (pid=%d). Goodbye!\n", getpid());
    _exit(0);
}

// ── Built-in: ls ────────────────────────────────────────────
static void builtin_ls(const char* path) {
    const char* target = (path && strlen(path) > 0) ? path : ".";
    DIR* dir = opendir(target);
    if (!dir) {
        printf("  ls: cannot open '%s'\n", target);
        return;
    }
    struct dirent* ent;
    int count = 0;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.' && strlen(ent->d_name) <= 2) continue; // skip . and ..

        // Check if directory
        char fullpath[512];
        snprintf(fullpath, sizeof(fullpath), "%s/%s", target, ent->d_name);
        struct stat st;
        bool is_dir = false;
        if (stat(fullpath, &st) == 0 && S_ISDIR(st.st_mode))
            is_dir = true;

        printf("  %s%s", ent->d_name, is_dir ? "/" : "");
        count++;
        if (count % 4 == 0) printf("\n");
        else                printf("\t");
    }
    if (count % 4 != 0) printf("\n");
    if (count == 0) printf("  (empty directory)\n");
    closedir(dir);
}

// ── Built-in: pwd ───────────────────────────────────────────
static void builtin_pwd() {
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd))) {
        printf("  %s\n", cwd);
    } else {
        printf("  pwd: error\n");
    }
}

// ── Built-in: echo ──────────────────────────────────────────
static void builtin_echo(const char* text) {
    printf("  %s\n", text ? text : "");
}

// ── Built-in: help ──────────────────────────────────────────
static void builtin_help() {
    printf("  ┌─────────────────────────────────────────┐\n");
    printf("  │  NeuralShell — Available Commands       │\n");
    printf("  ├─────────────────────────────────────────┤\n");
    printf("  │  ls [path]   — list directory contents  │\n");
    printf("  │  pwd         — print working directory  │\n");
    printf("  │  echo <text> — echo text to stdout      │\n");
    printf("  │  clear       — clear the screen         │\n");
    printf("  │  help        — show this help           │\n");
    printf("  │  exit / quit — exit the shell           │\n");
    printf("  └─────────────────────────────────────────┘\n");
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "[NeuralShell] Usage: %s <read_fd> <write_fd>\n", argv[0]);
        return 1;
    }
    g_read_fd  = atoi(argv[1]);
    g_write_fd = atoi(argv[2]);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);

    printf("[NeuralShell] Started (pid=%d). Type 'help' for commands.\n", getpid());

    char line[512];

    while (!g_terminate) {
        printf("\n\033[1;36mneuralOS\033[0m:\033[1;34m~\033[0m$ ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        if (strlen(line) == 0) continue;

        // Parse command
        char cmd[64] = {};
        char arg[448] = {};
        sscanf(line, "%63s", cmd);

        // Extract argument (everything after the first space)
        const char* argp = strchr(line, ' ');
        if (argp) {
            while (*argp == ' ') argp++;
            strncpy(arg, argp, sizeof(arg) - 1);
        }

        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            printf("  Exiting NeuralShell...\n");
            break;
        } else if (strcmp(cmd, "ls") == 0) {
            builtin_ls(arg);
        } else if (strcmp(cmd, "pwd") == 0) {
            builtin_pwd();
        } else if (strcmp(cmd, "echo") == 0) {
            builtin_echo(arg);
        } else if (strcmp(cmd, "help") == 0) {
            builtin_help();
        } else if (strcmp(cmd, "clear") == 0) {
            printf("\033[2J\033[H");
        } else {
            printf("  neuralsh: command not found: %s\n", cmd);
            printf("  Type 'help' for available commands.\n");
        }
    }

    if (g_terminate) printf("[NeuralShell] Received SIGTERM — shutting down.\n");
    send_task_done_and_exit();
    return 0;
}
