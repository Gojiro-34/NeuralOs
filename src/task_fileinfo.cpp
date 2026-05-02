// ============================================================
// NeuralOS X — Task: FileInfoInspector
// CL-2006 Operating Systems Lab | Spring 2026
// ------------------------------------------------------------
// Asks the user for a filename and prints its size,
// permissions (rwx style), and last-modified timestamp.
// Receives pipe FDs via argv[1] (read) and argv[2] (write).
// Sends TASK_DONE on exit. Handles SIGTERM.
// ============================================================

#include "../include/kernel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <ctime>

static volatile bool g_quit = false;
static void handle_sigterm(int) { g_quit = true; }

static void send_task_done(int write_fd) {
    IPC_Message msg;
    msg.type   = IPC_MsgType::TASK_DONE;
    msg.pid    = getpid();
    msg.ram_mb = 0; msg.hdd_mb = 0;
    strncpy(msg.task_name, "FileInfoInspector", 31);
    write(write_fd, &msg, sizeof(msg));
}

static void print_permissions(mode_t mode) {
    /*
     * Converts a stat() mode_t bitmask into a human-readable
     * rwxrwxrwx permission string and prints it.
     */
    char perm[11];
    perm[0]  = S_ISDIR(mode)  ? 'd' : '-';
    perm[1]  = (mode & S_IRUSR) ? 'r' : '-';
    perm[2]  = (mode & S_IWUSR) ? 'w' : '-';
    perm[3]  = (mode & S_IXUSR) ? 'x' : '-';
    perm[4]  = (mode & S_IRGRP) ? 'r' : '-';
    perm[5]  = (mode & S_IWGRP) ? 'w' : '-';
    perm[6]  = (mode & S_IXGRP) ? 'x' : '-';
    perm[7]  = (mode & S_IROTH) ? 'r' : '-';
    perm[8]  = (mode & S_IWOTH) ? 'w' : '-';
    perm[9]  = (mode & S_IXOTH) ? 'x' : '-';
    perm[10] = '\0';
    printf("  Permissions : %s\n", perm);
}

static void inspect_file(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        perror("  [FI] stat failed");
        return;
    }
    char timebuf[64];
    struct tm* tm_info = localtime(&st.st_mtime);
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm_info);

    printf("\n  ── File Info: %s ──\n", path);
    printf("  Size        : %ld bytes\n", (long)st.st_size);
    print_permissions(st.st_mode);
    printf("  Last Modified: %s\n", timebuf);
    printf("  Inode       : %lu\n\n", (unsigned long)st.st_ino);
}

int main(int argc, char* argv[]) {
    int read_fd  = (argc >= 2) ? atoi(argv[1]) : -1;
    int write_fd = (argc >= 3) ? atoi(argv[2]) : -1;

    signal(SIGTERM, handle_sigterm);

    printf("\n╔══════════════════════════════════╗\n");
    printf("║  NeuralOS X — File Inspector     ║\n");
    printf("╚══════════════════════════════════╝\n");
    printf("  Enter a filename to inspect (or 'exit'):\n\n");

    char line[256];
    while (!g_quit) {
        printf("FileInfo> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, "exit") == 0) break;
        if (strlen(line) == 0) continue;
        inspect_file(line);
    }

    printf("  [FI] FileInfoInspector exiting.\n");
    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
