// ============================================================
// NeuralOS X — Kernel Entry Point  (Phase 3)
// CL-2006 Operating Systems Lab | Spring 2026
//
// Usage: ./NeuralOS_X <RAM_GB> <HDD_GB> <CORES>
// Example: ./NeuralOS_X 1 10 2
// ============================================================

#include "process_launcher.cpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>

// ──────────────────────────────────────────────────────────────
//  Scheduler background thread
// ──────────────────────────────────────────────────────────────
static volatile bool g_running = true;  // set to false on shutdown

static void* scheduler_thread(void*) {
    /*
     * Runs sched_tick() continuously in the background.
     * Re-enqueues READY processes so they keep getting dispatched.
     * Sleeps TIME_QUANTUM_MS between ticks to simulate time slices.
     */
    while (g_running) {
        pid_t chosen = sched_tick();
        if (chosen == -1) {
            // Queues empty — re-enqueue any READY processes
            PCB* table = get_proc_table();
            int  n     = get_proc_count();
            for (int i = 0; i < n; i++) {
                if (table[i].pid != 0 && table[i].state == ProcState::READY)
                    sched_enqueue(&table[i]);
            }
        }
        usleep(TIME_QUANTUM_MS * 1000);
    }
    return nullptr;
}

// ──────────────────────────────────────────────────────────────
//  Boot Animation
// ──────────────────────────────────────────────────────────────
static void boot_animation(int ram_gb, int hdd_gb, int cores) {
    /*
     * Displays a text-mode boot screen with staged loading messages.
     * Uses usleep() to simulate real boot delay between stages.
     */
    const char* stages[] = {
        "Hardware Init",
        "Memory Map",
        "Core Detection",
        "Scheduler Calibration",
        "Shell Ready"
    };
    printf("\n");
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║          NeuralOS X — Sentient Scheduling            ║\n");
    printf("║        CL-2006 Operating Systems Lab | 2026          ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n");
    printf("  RAM: %d GB  |  HDD: %d GB  |  Cores: %d\n\n", ram_gb, hdd_gb, cores);

    for (int i = 0; i < 5; i++) {
        printf("  [BOOT] Stage %d/5: %s ...", i + 1, stages[i]);
        fflush(stdout);
        usleep(300000);
        printf(" OK\n");
    }
    printf("\n  NeuralOS X is LIVE.\n\n");
}

// ──────────────────────────────────────────────────────────────
//  Task menu — lists all 18 available tasks with RAM cost
// ──────────────────────────────────────────────────────────────
static void print_task_menu() {
    /*
     * Prints a numbered list of all available tasks so the user
     * can select one by number to launch it as a new process.
     */
    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║              NeuralOS X — Task Menu                  ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");

    // Mirror the TASKS[] array order from process_launcher.cpp
    const char* task_info[][2] = {
        {  "1",  "NeuralShell        [32 MB]  — Interactive shell" },
        {  "2",  "CogniPad           [24 MB]  — Auto-save notepad" },
        {  "3",  "CalculatorPro      [12 MB]  — Calculator" },
        {  "4",  "AdaptiveClock      [ 8 MB]  — Live clock (background)" },
        {  "5",  "SysPulse           [16 MB]  — System monitor (background)" },
        {  "6",  "FileManager        [20 MB]  — File operations" },
        {  "7",  "FileInfoInspector  [10 MB]  — File info viewer" },
        {  "8",  "MusicPlayer        [18 MB]  — Music player (background)" },
        {  "9",  "GhostTyper         [14 MB]  — Typewriter effect" },
        { "10",  "Minesweeper        [40 MB]  — Minesweeper game" },
        { "11",  "SnakeGame          [36 MB]  — Snake game" },
        { "12",  "FileCopySimulator  [22 MB]  — File copy task" },
        { "13",  "PrintSpooler       [20 MB]  — Print spooler (background)" },
        { "14",  "MemStressTester    [64 MB]  — Memory stress test" },
        { "15",  "CognitiveHUD       [28 MB]  — HUD display (background)" },
        { "16",  "DeadlockArena      [30 MB]  — Deadlock demo" },
        { "17",  "ProcessGraveyard   [16 MB]  — Terminated process log" },
        { "18",  "KernelModeConsole  [20 MB]  — Kernel mode console" },
    };

    for (int i = 0; i < 18; i++)
        printf("║  [%2s] %s\n", task_info[i][0], task_info[i][1]);

    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║  Commands: ps | kill <pid> | kernel | shutdown | ?  ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n\n");
}

// ──────────────────────────────────────────────────────────────
//  Map user-entered number → task name
// ──────────────────────────────────────────────────────────────
static const char* task_name_by_number(int n) {
    /*
     * Returns the canonical task name string for a given menu
     * number (1-18). Returns nullptr if out of range.
     */
    static const char* names[] = {
        "NeuralShell", "CogniPad", "CalculatorPro", "AdaptiveClock",
        "SysPulse", "FileManager", "FileInfoInspector", "MusicPlayer",
        "GhostTyper", "Minesweeper", "SnakeGame", "FileCopySimulator",
        "PrintSpooler", "MemStressTester", "CognitiveHUD", "DeadlockArena",
        "ProcessGraveyard", "KernelModeConsole"
    };
    if (n < 1 || n > 18) return nullptr;
    return names[n - 1];
}

// ──────────────────────────────────────────────────────────────
//  Kernel Mode Console
//  Allows force-killing processes — simulates privileged mode.
// ──────────────────────────────────────────────────────────────
static void kernel_mode_console() {
    /*
     * Enters a restricted sub-shell that represents kernel mode.
     * In kernel mode the user can force-kill (SIGKILL) any process,
     * simulating direct hardware-level process termination.
     * Type 'exit' to return to user mode.
     */
    printf("\n[KERNEL MODE] Entered. Type 'fkill <pid>' or 'exit'.\n");
    log_event("KERNEL_MODE entered by user");
    char line[128];
    while (true) {
        printf("kernel# ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        // Strip newline
        line[strcspn(line, "\n")] = '\0';

        if (strcmp(line, "exit") == 0) {
            printf("[KERNEL MODE] Returning to user mode.\n\n");
            log_event("KERNEL_MODE exited by user");
            break;
        }

        if (strncmp(line, "fkill ", 6) == 0) {
            pid_t target = (pid_t)atoi(line + 6);
            if (target <= 0) {
                printf("[KERNEL MODE] Invalid pid.\n");
                continue;
            }
            if (kill(target, SIGKILL) == 0) {
                printf("[KERNEL MODE] SIGKILL sent to pid=%d\n", target);
                log_event("KERNEL_MODE fkill pid=%d", target);
            } else {
                perror("[KERNEL MODE] kill failed");
            }
            continue;
        }

        // Also allow ps in kernel mode
        if (strcmp(line, "ps") == 0) {
            print_resource_state();
            continue;
        }

        printf("[KERNEL MODE] Unknown command: %s\n", line);
    }
}

// ──────────────────────────────────────────────────────────────
//  Interactive Shell Loop
// ──────────────────────────────────────────────────────────────
static void run_shell(int ram_mb) {
    /*
     * Main user-facing shell loop. Runs after boot completes.
     * Auto-starts AdaptiveClock and SysPulse as background tasks.
     * Accepts user commands in a loop until 'shutdown' is typed.
     *
     * Supported commands:
     *   <number>       — launch task by menu number
     *   ps             — show all running processes
     *   kill <pid>     — send SIGTERM to a process
     *   kernel         — enter kernel mode
     *   shutdown       — shut down NeuralOS X
     *   ?              — show task menu again
     */

    // ── Auto-start background tasks ─────────────────────────────
    printf("[KERNEL] Auto-starting background tasks...\n");
    launch_task_by_name("AdaptiveClock");
    usleep(80000);
    launch_task_by_name("SysPulse");
    usleep(80000);
    printf("\n");

    print_task_menu();

    char line[128];
    while (true) {
        printf("NeuralOS> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            // EOF (Ctrl+D) — treat as shutdown
            break;
        }

        // Strip trailing newline / whitespace
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) == 0) continue;

        // ── Command: shutdown ────────────────────────────────────
        if (strcmp(line, "shutdown") == 0) {
            printf("\n[SHELL] Shutdown requested.\n");
            break;
        }

        // ── Command: ? (show menu) ───────────────────────────────
        if (strcmp(line, "?") == 0) {
            print_task_menu();
            continue;
        }

        // ── Command: ps ──────────────────────────────────────────
        if (strcmp(line, "ps") == 0) {
            print_resource_state();
            print_scheduler_state();
            continue;
        }

        // ── Command: aging (show aging report) ───────────────────
        if (strcmp(line, "aging") == 0) {
            print_aging_report();
            continue;
        }

        // ── Command: kill <pid> ──────────────────────────────────
        if (strncmp(line, "kill ", 5) == 0) {
            pid_t target = (pid_t)atoi(line + 5);
            if (target <= 0) {
                printf("[SHELL] Usage: kill <pid>\n");
                continue;
            }
            if (kill(target, SIGTERM) == 0)
                printf("[SHELL] SIGTERM sent to pid=%d\n", target);
            else
                perror("[SHELL] kill failed");
            continue;
        }

        // ── Command: kernel (enter kernel mode) ──────────────────
        if (strcmp(line, "kernel") == 0) {
            kernel_mode_console();
            continue;
        }

        // ── Command: number (launch task) ────────────────────────
        // Check if the input is a valid integer
        bool is_number = true;
        for (int i = 0; line[i] != '\0'; i++) {
            if (line[i] < '0' || line[i] > '9') { is_number = false; break; }
        }

        if (is_number) {
            int choice = atoi(line);
            const char* name = task_name_by_number(choice);
            if (!name) {
                printf("[SHELL] Invalid choice. Enter 1-18 or type '?' for menu.\n");
                continue;
            }

            // RAM guard — warn user if we are near capacity
            // (resource_manager will deny if truly full)
            printf("[SHELL] Launching '%s'...\n", name);
            pid_t p = launch_task_by_name(name);
            if (p > 0)
                printf("[SHELL] Task '%s' started with pid=%d\n\n", name, p);
            else
                printf("[SHELL] Failed to launch '%s' (RAM full or error).\n\n", name);
            continue;
        }

        printf("[SHELL] Unknown command '%s'. Type '?' for help.\n", line);
    }
}

// ──────────────────────────────────────────────────────────────
//  main()
// ──────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    /*
     * Entry point. Parses hardware parameters, boots NeuralOS X,
     * initialises all subsystems, starts the scheduler thread,
     * then hands control to the interactive shell.
     */
    int ram_gb = 1;
    int hdd_gb = 10;
    int cores  = 2;

    if (argc >= 2) ram_gb = atoi(argv[1]);
    if (argc >= 3) hdd_gb = atoi(argv[2]);
    if (argc >= 4) cores  = atoi(argv[3]);

    if (ram_gb <= 0) ram_gb = 1;
    if (hdd_gb <= 0) hdd_gb = 10;
    if (cores  <= 0) cores  = 1;

    int ram_mb = ram_gb * 1024;
    int hdd_mb = hdd_gb * 1024;

    // ── Signal handlers ─────────────────────────────────────────
    install_signal_handlers();

    // ── Boot animation ──────────────────────────────────────────
    boot_animation(ram_gb, hdd_gb, cores);

    // ── Subsystem initialisation ────────────────────────────────
    uint64_t ram_bytes = (uint64_t)ram_mb * 1024ULL * 1024ULL;
    init_memory(ram_bytes);
    resource_manager_init(ram_mb, hdd_mb);
    scheduler_init();

    // ── Start scheduler background thread ───────────────────────
    pthread_t sched_tid;
    if (pthread_create(&sched_tid, nullptr, scheduler_thread, nullptr) != 0) {
        perror("[KERNEL] Failed to start scheduler thread");
        return 1;
    }
    pthread_detach(sched_tid);

    // ── Interactive shell ────────────────────────────────────────
    run_shell(ram_mb);

    // ── Shutdown ─────────────────────────────────────────────────
    g_running = false;   // stop scheduler thread
    usleep(200000);      // let it exit cleanly

    printf("\n[KERNEL] Initiating shutdown...\n");
    shutdown_all();

    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║        NeuralOS X — Goodbye. Stay Curious.           ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n\n");
    return 0;
}
