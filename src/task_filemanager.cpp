// ============================================================
// NeuralOS X — Task: FileManager
// CL-2006 Operating Systems Lab | Spring 2026
// ------------------------------------------------------------
// Lists files in the current directory and allows the user to
// copy, move, or delete files using simple menu commands.
// Receives pipe FDs via argv[1] (read) and argv[2] (write).
// Sends TASK_DONE over the write pipe on clean exit.
// Handles SIGTERM for graceful shutdown.
// ============================================================

#include "../include/kernel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>

static int g_write_fd = -1;
static volatile bool g_quit = false;

static void handle_sigterm(int) { g_quit = true; }

static void send_task_done(int write_fd) {
    IPC_Message msg;
    msg.type   = IPC_MsgType::TASK_DONE;
    msg.pid    = getpid();
    msg.ram_mb = 0;
    msg.hdd_mb = 0;
    strncpy(msg.task_name, "FileManager", 31);
    write(write_fd, &msg, sizeof(msg));
}

static void list_files() {
    /*
     * Opens the current directory and prints each entry
     * with its type (file/dir) and size in bytes.
     */
    DIR* d = opendir(".");
    if (!d) { perror("opendir"); return; }

    printf("\n  %-30s  %-6s  %s\n", "Name", "Type", "Size");
    printf("  %-30s  %-6s  %s\n", "------------------------------", "------", "--------");

    struct dirent* entry;
    struct stat st;
    while ((entry = readdir(d)) != nullptr) {
        if (entry->d_name[0] == '.') continue; // skip hidden
        stat(entry->d_name, &st);
        const char* type = S_ISDIR(st.st_mode) ? "DIR" : "FILE";
        printf("  %-30s  %-6s  %ld bytes\n", entry->d_name, type, (long)st.st_size);
    }
    closedir(d);
    printf("\n");
}

static void copy_file(const char* src, const char* dst) {
    FILE* in  = fopen(src, "rb");
    if (!in)  { printf("  [FM] Cannot open source: %s\n", src); return; }
    FILE* out = fopen(dst, "wb");
    if (!out) { printf("  [FM] Cannot create dest: %s\n", dst); fclose(in); return; }

    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        fwrite(buf, 1, n, out);

    fclose(in); fclose(out);
    printf("  [FM] Copied '%s' → '%s'\n", src, dst);
}

static void move_file(const char* src, const char* dst) {
    if (rename(src, dst) == 0)
        printf("  [FM] Moved '%s' → '%s'\n", src, dst);
    else
        perror("  [FM] move failed");
}

static void delete_file(const char* path) {
    if (remove(path) == 0)
        printf("  [FM] Deleted '%s'\n", path);
    else
        perror("  [FM] delete failed");
}

int main(int argc, char* argv[]) {
    int read_fd  = (argc >= 2) ? atoi(argv[1]) : -1;
    int write_fd = (argc >= 3) ? atoi(argv[2]) : -1;
    g_write_fd   = write_fd;

    signal(SIGTERM, handle_sigterm);

    printf("\n╔══════════════════════════════════╗\n");
    printf("║   NeuralOS X — File Manager      ║\n");
    printf("╚══════════════════════════════════╝\n");
    printf("  Commands: ls | cp <src> <dst> | mv <src> <dst> | rm <file> | exit\n\n");

    list_files();

    char line[256];
    while (!g_quit) {
        printf("FM> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';

        if (strcmp(line, "exit") == 0) break;
        if (strcmp(line, "ls")   == 0) { list_files(); continue; }

        char cmd[16], a1[128], a2[128];
        int parts = sscanf(line, "%15s %127s %127s", cmd, a1, a2);

        if (strcmp(cmd, "cp") == 0 && parts == 3) { copy_file(a1, a2);  continue; }
        if (strcmp(cmd, "mv") == 0 && parts == 3) { move_file(a1, a2);  continue; }
        if (strcmp(cmd, "rm") == 0 && parts >= 2) { delete_file(a1);    continue; }

        printf("  [FM] Unknown command.\n");
    }

    printf("  [FM] FileManager exiting.\n");
    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
