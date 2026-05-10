// ============================================================
// NeuralOS X — fork/exec Process Launcher  (Phase 3)
// CL-2006 Operating Systems Lab | Spring 2026
// ============================================================
// Implements: fork() + execvp() task launch, IPC pipe setup,
// resource request/grant handshake, PCB registration,
// SIGCHLD-based zombie reaping.
// ============================================================

#include "../include/kernel.h"
#include "scheduler.cpp"

#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

// ──────────────────────────────────────────────────────────────
//  Task Descriptor (static list of all 18 tasks — Phase 3
//  implements the first 5 as real executables; rest as stubs)
// ──────────────────────────────────────────────────────────────
struct TaskDesc {
    const char* name;
    const char* binary;      // argv[0] passed to execvp
    int32_t     ram_mb;
    int32_t     hdd_mb;
    int         base_priority;
    Archetype   archetype;
    SchedLevel  level;
};

static const TaskDesc TASKS[] = {
    // name,                binary,                 ram, hdd, prio, archetype,          level
    { "NeuralShell",        "./bin/task_shell",      32,   1,   8,  Archetype::REFLEX,      SchedLevel::L0_REFLEX },
    { "CogniPad",           "./bin/task_cognipad",   24,   5,   6,  Archetype::FOCUSED,     SchedLevel::L1_FOCUSED },
    { "CalculatorPro",      "./bin/task_calculator", 12,   0,   7,  Archetype::REFLEX,      SchedLevel::L0_REFLEX },
    { "AdaptiveClock",      "./bin/task_clock",       8,   0,   3,  Archetype::BACKGROUND,  SchedLevel::L2_BACKGROUND },
    { "SysPulse",           "./bin/task_syspulse",   16,   0,   4,  Archetype::BACKGROUND,  SchedLevel::L2_BACKGROUND },
    { "FileManager",        "./bin/task_filemanager",20,   5,   5,  Archetype::FOCUSED,     SchedLevel::L1_FOCUSED },
    { "FileInfoInspector",  "./bin/task_fileinfo",   10,   0,   6,  Archetype::REFLEX,      SchedLevel::L0_REFLEX },
    { "MusicPlayer",        "./bin/task_music",      18,   0,   2,  Archetype::BACKGROUND,  SchedLevel::L2_BACKGROUND },
    { "GhostTyper",         "./bin/task_ghost",      14,   0,   5,  Archetype::FOCUSED,     SchedLevel::L1_FOCUSED },
    { "Minesweeper",        "./bin/task_minesweeper",40,   2,   5,  Archetype::FOCUSED,     SchedLevel::L1_FOCUSED },
    { "SnakeGame",          "./bin/task_snake",      36,   2,   5,  Archetype::FOCUSED,     SchedLevel::L1_FOCUSED },
    { "FileCopySimulator",  "./bin/task_filecopy",   22,   5,   3,  Archetype::BACKGROUND,  SchedLevel::L2_BACKGROUND },
    { "PrintSpooler",       "./bin/task_spooler",    20,   1,   4,  Archetype::BACKGROUND,  SchedLevel::L2_BACKGROUND },
    { "MemStressTester",    "./bin/task_memstress",  64,   0,   9,  Archetype::REFLEX,      SchedLevel::L0_REFLEX },
    { "CognitiveHUD",       "./bin/task_hud",        28,   0,   2,  Archetype::BACKGROUND,  SchedLevel::L2_BACKGROUND },
    { "DeadlockArena",      "./bin/task_deadlock",   30,   0,   8,  Archetype::FOCUSED,     SchedLevel::L1_FOCUSED },
    { "ProcessGraveyard",   "./bin/task_graveyard",  16,   0,   2,  Archetype::BACKGROUND,  SchedLevel::L2_BACKGROUND },
    { "KernelModeConsole",  "./bin/task_kernel_con", 20,   0,  10,  Archetype::REFLEX,      SchedLevel::L0_REFLEX },
};
static constexpr int NUM_TASKS = (int)(sizeof(TASKS) / sizeof(TASKS[0]));

// ──────────────────────────────────────────────────────────────
//  SIGCHLD handler — reap zombies
// ──────────────────────────────────────────────────────────────
static void sigchld_handler(int) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[KERNEL] Child pid=%d exited (status=%d)\n", pid, WEXITSTATUS(status));
        on_process_exit(pid);
    }
}

// ──────────────────────────────────────────────────────────────
//  launch_task()
//  Creates a pipe pair, forks, and in the child execs the task
//  binary. The child first sends an IPC_Message resource request
//  and waits for GRANTED before doing real work (or exits on DENIED).
// ──────────────────────────────────────────────────────────────
pid_t launch_task(const TaskDesc& td) {
    // kernel→child pipe  (write_fd[1] → child reads grants from read_fd[0])
    int k2c[2], c2k[2];
    if (pipe(k2c) < 0 || pipe(c2k) < 0) {
        perror("pipe");
        return -1;
    }

    printf("[KERNEL] Launching task: %s  (RAM=%d HDD=%d)\n",
           td.name, td.ram_mb, td.hdd_mb);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        close(k2c[0]); close(k2c[1]);
        close(c2k[0]); close(c2k[1]);
        return -1;
    }

    if (pid == 0) {
        // ── Child Process ────────────────────────────────────────
        // Become a process group leader so kill(-pid) can terminate
        // both xterm and the task binary inside it.
        setpgid(0, 0);

        // Close ends we don't use
        close(k2c[1]);  // child reads from k2c[0]
        close(c2k[0]);  // child writes to c2k[1]

        // ── Send resource request to kernel ──────────────────────
        IPC_Message req;
        req.type   = IPC_MsgType::RESOURCE_REQUEST;
        req.pid    = getpid();
        req.ram_mb = td.ram_mb;
        req.hdd_mb = td.hdd_mb;
        strncpy(req.task_name, td.name, 31);
        write(c2k[1], &req, sizeof(req));

        // ── Wait for kernel reply ─────────────────────────────────
        IPC_Message reply;
        ssize_t n = read(k2c[0], &reply, sizeof(reply));
        if (n <= 0 || reply.type == IPC_MsgType::RESOURCE_DENIED) {
            fprintf(stderr, "[TASK:%s] Resources DENIED — exiting\n", td.name);
            close(k2c[0]);
            close(c2k[1]);
            _exit(1);
        }

        // ── Resources GRANTED — launch in a separate xterm window ─
        // Pass pipe FDs as argv so the binary can use them for IPC.
        char fd_read_buf[16], fd_write_buf[16];
        snprintf(fd_read_buf,  sizeof(fd_read_buf),  "%d", k2c[0]);
        snprintf(fd_write_buf, sizeof(fd_write_buf), "%d", c2k[1]);

        // Build xterm window title
        char title_buf[64];
        snprintf(title_buf, sizeof(title_buf),
                 "NeuralOS X  —  %s  [pid=%d]", td.name, getpid());

        // Launch in a new xterm window with styled appearance
        char* xterm_argv[] = {
            (char*)"xterm",
            (char*)"-T",        title_buf,          // window title
            (char*)"-fa",       (char*)"Monospace",  // font family
            (char*)"-fs",       (char*)"13",         // font size
            (char*)"-bg",       (char*)"#1a1a2e",    // dark background
            (char*)"-fg",       (char*)"#00ff88",    // green text
            (char*)"-geometry", (char*)"90x30",      // 90 cols × 30 rows
            (char*)"-e",                             // execute command:
            (char*)td.binary,
            fd_read_buf,
            fd_write_buf,
            nullptr
        };
        execvp("xterm", xterm_argv);

        // ── xterm not found — try direct exec as fallback ────────
        fprintf(stderr, "[TASK:%s] xterm not available, launching in current terminal\n",
                td.name);
        char* argv[] = {
            (char*)td.binary,
            fd_read_buf,
            fd_write_buf,
            nullptr
        };
        execvp(td.binary, argv);

        // ── Both failed — fall back to a built-in stub ───────────
        fprintf(stderr, "[TASK:%s] execvp failed (%s) — running built-in stub\n",
                td.name, strerror(errno));
        printf("[STUB:%s] Running task simulation (pid=%d)...\n", td.name, getpid());
        usleep(200000 + (getpid() % 300) * 1000); // 200-500 ms of "work"
        printf("[STUB:%s] Task complete.\n", td.name);
        close(k2c[0]);
        close(c2k[1]);
        _exit(0);
    }

    // ── Parent (Kernel) Process ──────────────────────────────────
    close(k2c[0]);  // kernel writes to k2c[1], child reads from k2c[0]
    close(c2k[1]);  // kernel reads from c2k[0], child writes to c2k[1]

    // Register PCB (state = BLOCKED until grant confirmed)
    PCB* pcb = register_process(pid, td.name, td.archetype,
                                 td.level, td.base_priority,
                                 c2k[0], k2c[1]);
    if (!pcb) {
        fprintf(stderr, "[KERNEL] PCB table full — killing pid=%d\n", pid);
        kill(pid, SIGKILL);
        close(k2c[1]); close(c2k[0]);
        return -1;
    }

    // Read the resource request from the child
    IPC_Message req;
    ssize_t n = read(c2k[0], &req, sizeof(req));
    if (n < (ssize_t)sizeof(req)) {
        fprintf(stderr, "[KERNEL] Short read from pid=%d\n", pid);
        kill(pid, SIGKILL);
        return -1;
    }

    // Handle and reply
    bool granted = handle_resource_request(&req, k2c[1], pcb);

    if (granted) {
        // Enqueue into scheduler
        sched_enqueue(pcb);
    }

    return pid;
}

// ──────────────────────────────────────────────────────────────
//  launch_task_by_name()
// ──────────────────────────────────────────────────────────────
pid_t launch_task_by_name(const char* name) {
    for (int i = 0; i < NUM_TASKS; i++) {
        if (strcmp(TASKS[i].name, name) == 0)
            return launch_task(TASKS[i]);
    }
    fprintf(stderr, "[KERNEL] Unknown task: %s\n", name);
    return -1;
}

// ──────────────────────────────────────────────────────────────
//  install_signal_handlers()
// ──────────────────────────────────────────────────────────────
void install_signal_handlers() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sa.sa_flags   = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, nullptr);
    printf("[KERNEL] Signal handlers installed.\n");
}

// ──────────────────────────────────────────────────────────────
//  shutdown_all()
//  Sends SIGTERM to all running tasks, waits 2s, then SIGKILL.
// ──────────────────────────────────────────────────────────────
void shutdown_all() {
    printf("\n[KERNEL] ──── Initiating Shutdown ────\n");
    PCB*  table = get_proc_table();
    int   n     = get_proc_count();

    for (int i = 0; i < n; i++) {
        if (table[i].pid != 0 && table[i].state != ProcState::TERMINATED) {
            printf("[KERNEL] SIGTERM → pid=%d (%s)\n",
                   table[i].pid, table[i].name);
            // Kill entire process group (xterm + child task binary)
            kill(-table[i].pid, SIGTERM);
        }
    }

    sleep(2);

    for (int i = 0; i < n; i++) {
        if (table[i].pid != 0 && table[i].state != ProcState::TERMINATED) {
            printf("[KERNEL] SIGKILL → pid=%d (%s)\n",
                   table[i].pid, table[i].name);
            // Kill entire process group (xterm + child task binary)
            kill(-table[i].pid, SIGKILL);
        }
    }

    // reap
    int status;
    while (waitpid(-1, &status, WNOHANG) > 0);
    printf("[KERNEL] Shutdown complete.\n");
}
