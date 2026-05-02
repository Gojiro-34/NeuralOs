// ============================================================
// NeuralOS X — Task: FileCopySimulator
// CL-2006 Operating Systems Lab | Spring 2026
// ------------------------------------------------------------
// Asks the user for source and destination filenames, then
// copies the file while displaying a live progress percentage.
// Receives pipe FDs via argv[1] (read) and argv[2] (write).
// Sends TASK_DONE on finish. Handles SIGTERM.
// ============================================================

#include "../include/kernel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>

static volatile bool g_quit = false;
static void handle_sigterm(int) { g_quit = true; }

static void send_task_done(int write_fd) {
    IPC_Message msg;
    msg.type   = IPC_MsgType::TASK_DONE;
    msg.pid    = getpid();
    msg.ram_mb = 0; msg.hdd_mb = 0;
    strncpy(msg.task_name, "FileCopySimulator", 31);
    write(write_fd, &msg, sizeof(msg));
}

static void copy_with_progress(const char* src, const char* dst) {
    /*
     * Opens src for reading and dst for writing. Copies in 512-byte
     * chunks, printing a progress bar after each chunk. Uses stat()
     * to get the total file size for percentage calculation.
     */
    struct stat st;
    if (stat(src, &st) != 0) { perror("  [FC] stat"); return; }
    long total = (long)st.st_size;
    if (total == 0) { printf("  [FC] Source file is empty.\n"); return; }

    FILE* in  = fopen(src, "rb");
    if (!in)  { perror("  [FC] open src"); return; }
    FILE* out = fopen(dst, "wb");
    if (!out) { perror("  [FC] open dst"); fclose(in); return; }

    char buf[512];
    long copied = 0;
    size_t n;

    printf("  Copying '%s' → '%s' (%ld bytes)\n", src, dst, total);

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0 && !g_quit) {
        fwrite(buf, 1, n, out);
        copied += (long)n;

        int pct      = (int)((copied * 100) / total);
        int filled   = pct / 5;  // bar out of 20 chars
        printf("\r  [");
        for (int i = 0; i < 20; i++) printf("%c", i < filled ? '█' : '░');
        printf("] %3d%%  (%ld / %ld bytes)", pct, copied, total);
        fflush(stdout);
        usleep(10000); // slow down to make progress visible
    }

    printf("\n  [FC] Copy complete.\n");
    fclose(in);
    fclose(out);
}

int main(int argc, char* argv[]) {
    int read_fd  = (argc >= 2) ? atoi(argv[1]) : -1;
    int write_fd = (argc >= 3) ? atoi(argv[2]) : -1;

    signal(SIGTERM, handle_sigterm);

    printf("\n╔══════════════════════════════════╗\n");
    printf("║  NeuralOS X — File Copy          ║\n");
    printf("╚══════════════════════════════════╝\n");

    char src[256], dst[256];

    printf("  Source file: ");
    fflush(stdout);
    if (!fgets(src, sizeof(src), stdin)) goto done;
    src[strcspn(src, "\n")] = '\0';

    printf("  Destination: ");
    fflush(stdout);
    if (!fgets(dst, sizeof(dst), stdin)) goto done;
    dst[strcspn(dst, "\n")] = '\0';

    if (strlen(src) == 0 || strlen(dst) == 0) {
        printf("  [FC] Empty filename, aborting.\n");
        goto done;
    }

    copy_with_progress(src, dst);

done:
    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
