// ============================================================
// NeuralOS X — Task: DeadlockArena
// CL-2006 Operating Systems Lab | Spring 2026
// ------------------------------------------------------------
// Simulates two threads each trying to acquire two mutexes in
// opposite order, creating a classic circular wait deadlock.
// A watchdog thread detects the deadlock after a timeout and
// prints "Deadlock detected among processes" as required.
// Receives pipe FDs via argv[1] (read) and argv[2] (write).
// Sends TASK_DONE on finish. Handles SIGTERM.
// ============================================================

#include "../include/kernel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

static volatile bool g_quit       = false;
static volatile bool g_deadlocked = false;
static void handle_sigterm(int) { g_quit = true; }

static void send_task_done(int write_fd) {
    IPC_Message msg;
    msg.type   = IPC_MsgType::TASK_DONE;
    msg.pid    = getpid();
    msg.ram_mb = 0; msg.hdd_mb = 0;
    strncpy(msg.task_name, "DeadlockArena", 31);
    write(write_fd, &msg, sizeof(msg));
}

static pthread_mutex_t mutex_A = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutex_B = PTHREAD_MUTEX_INITIALIZER;

// Thread 1: acquires A then tries B
static void* thread_one(void*) {
    /*
     * Thread 1 locks mutex A first, sleeps to let Thread 2 lock B,
     * then attempts to lock B — which is already held by Thread 2.
     * This creates the circular wait condition.
     */
    printf("  [T1] Acquiring Mutex A...\n"); fflush(stdout);
    pthread_mutex_lock(&mutex_A);
    printf("  [T1] Mutex A acquired. Sleeping 1s...\n"); fflush(stdout);
    sleep(1);
    printf("  [T1] Trying to acquire Mutex B...\n"); fflush(stdout);
    pthread_mutex_lock(&mutex_B);   // ← will block forever (deadlock)
    printf("  [T1] Mutex B acquired.\n");
    pthread_mutex_unlock(&mutex_B);
    pthread_mutex_unlock(&mutex_A);
    return nullptr;
}

// Thread 2: acquires B then tries A
static void* thread_two(void*) {
    /*
     * Thread 2 locks mutex B first, sleeps to let Thread 1 lock A,
     * then attempts to lock A — which is already held by Thread 1.
     * This completes the circular wait (deadlock).
     */
    printf("  [T2] Acquiring Mutex B...\n"); fflush(stdout);
    pthread_mutex_lock(&mutex_B);
    printf("  [T2] Mutex B acquired. Sleeping 1s...\n"); fflush(stdout);
    sleep(1);
    printf("  [T2] Trying to acquire Mutex A...\n"); fflush(stdout);
    pthread_mutex_lock(&mutex_A);   // ← will block forever (deadlock)
    printf("  [T2] Mutex A acquired.\n");
    pthread_mutex_unlock(&mutex_A);
    pthread_mutex_unlock(&mutex_B);
    return nullptr;
}

// Watchdog: detects deadlock after timeout
static void* watchdog(void* arg) {
    /*
     * Waits 3 seconds — enough time for both threads to reach their
     * blocked state. Then declares a deadlock, cancels both threads,
     * and signals the main thread to continue.
     */
    int* tids_signal = (int*)arg;
    sleep(3);
    (void)tids_signal;

    printf("\n  ╔══════════════════════════════════════════╗\n");
    printf("  ║  Deadlock detected among processes       ║\n");
    printf("  ║  T1 holds A, waits B                     ║\n");
    printf("  ║  T2 holds B, waits A  → circular wait    ║\n");
    printf("  ╚══════════════════════════════════════════╝\n\n");
    fflush(stdout);

    g_deadlocked = true;
    return nullptr;
}

int main(int argc, char* argv[]) {
    int read_fd  = (argc >= 2) ? atoi(argv[1]) : -1;
    int write_fd = (argc >= 3) ? atoi(argv[2]) : -1;

    signal(SIGTERM, handle_sigterm);

    printf("\n╔══════════════════════════════════╗\n");
    printf("║   NeuralOS X — Deadlock Arena    ║\n");
    printf("╚══════════════════════════════════╝\n");
    printf("  Spawning Thread-1 and Thread-2...\n\n");

    pthread_t t1, t2, wd;
    pthread_create(&t1, nullptr, thread_one, nullptr);
    pthread_create(&t2, nullptr, thread_two, nullptr);
    pthread_create(&wd, nullptr, watchdog,   nullptr);

    // Wait for watchdog to fire
    pthread_join(wd, nullptr);

    // Cancel deadlocked threads
    pthread_cancel(t1);
    pthread_cancel(t2);
    pthread_join(t1, nullptr);
    pthread_join(t2, nullptr);

    // Unlock mutexes in case they are still held
    pthread_mutex_unlock(&mutex_A);
    pthread_mutex_unlock(&mutex_B);

    printf("  [DA] Deadlock resolved by kernel intervention.\n");
    printf("  [DA] DeadlockArena exiting.\n");

    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
