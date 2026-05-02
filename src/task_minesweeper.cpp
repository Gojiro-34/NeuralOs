// ============================================================
// NeuralOS X — Task: Minesweeper
// CL-2006 Operating Systems Lab | Spring 2026
// ------------------------------------------------------------
// A fully playable text-based Minesweeper on a 5×5 grid with
// 5 mines. Player enters row and column to reveal a cell.
// Game ends on mine hit or all safe cells revealed.
// Receives pipe FDs via argv[1] (read) and argv[2] (write).
// Sends TASK_DONE on exit. Handles SIGTERM.
// ============================================================

#include "../include/kernel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <ctime>

static volatile bool g_quit = false;
static void handle_sigterm(int) { g_quit = true; }

static void send_task_done(int write_fd) {
    IPC_Message msg;
    msg.type   = IPC_MsgType::TASK_DONE;
    msg.pid    = getpid();
    msg.ram_mb = 0; msg.hdd_mb = 0;
    strncpy(msg.task_name, "Minesweeper", 31);
    write(write_fd, &msg, sizeof(msg));
}

#define ROWS 5
#define COLS 5
#define MINES 5

static bool mine[ROWS][COLS];
static bool revealed[ROWS][COLS];
static int  adj[ROWS][COLS];

static void place_mines() {
    /*
     * Randomly places MINES mines on the grid using rand().
     * Counts adjacent mines for each cell and stores in adj[][].
     */
    srand((unsigned)time(nullptr) ^ getpid());
    memset(mine,     false, sizeof(mine));
    memset(revealed, false, sizeof(revealed));
    memset(adj,      0,     sizeof(adj));

    int placed = 0;
    while (placed < MINES) {
        int r = rand() % ROWS;
        int c = rand() % COLS;
        if (!mine[r][c]) { mine[r][c] = true; placed++; }
    }

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (mine[r][c]) continue;
            int count = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    int nr = r + dr, nc = c + dc;
                    if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS && mine[nr][nc])
                        count++;
                }
            adj[r][c] = count;
        }
}

static void print_board(bool show_mines) {
    printf("\n    ");
    for (int c = 0; c < COLS; c++) printf(" %d ", c);
    printf("\n    ");
    for (int c = 0; c < COLS; c++) printf("───");
    printf("\n");
    for (int r = 0; r < ROWS; r++) {
        printf(" %d |", r);
        for (int c = 0; c < COLS; c++) {
            if (revealed[r][c]) {
                if (mine[r][c]) printf(" * ");
                else if (adj[r][c]) printf(" %d ", adj[r][c]);
                else printf(" . ");
            } else if (show_mines && mine[r][c]) {
                printf(" M ");
            } else {
                printf(" # ");
            }
        }
        printf("\n");
    }
    printf("\n");
}

static int count_revealed() {
    int n = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (revealed[r][c]) n++;
    return n;
}

int main(int argc, char* argv[]) {
    int read_fd  = (argc >= 2) ? atoi(argv[1]) : -1;
    int write_fd = (argc >= 3) ? atoi(argv[2]) : -1;

    signal(SIGTERM, handle_sigterm);
    place_mines();

    printf("\n╔══════════════════════════════════╗\n");
    printf("║   NeuralOS X — Minesweeper       ║\n");
    printf("║   Grid: 5×5  |  Mines: 5         ║\n");
    printf("╚══════════════════════════════════╝\n");
    printf("  Enter: row col  (e.g. '2 3')  |  'exit' to quit\n");

    int safe_cells = ROWS * COLS - MINES;
    bool game_over = false;
    bool won       = false;

    while (!g_quit && !game_over) {
        print_board(false);
        printf("Mines> ");
        fflush(stdout);

        char line[64];
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';

        if (strcmp(line, "exit") == 0) break;

        int r, c;
        if (sscanf(line, "%d %d", &r, &c) != 2) {
            printf("  Invalid input. Use: row col\n"); continue;
        }
        if (r < 0 || r >= ROWS || c < 0 || c >= COLS) {
            printf("  Out of bounds.\n"); continue;
        }
        if (revealed[r][c]) {
            printf("  Already revealed.\n"); continue;
        }

        revealed[r][c] = true;

        if (mine[r][c]) {
            game_over = true;
            printf("\n  💥 BOOM! You hit a mine!\n");
            print_board(true);
        } else if (count_revealed() == safe_cells) {
            game_over = true;
            won       = true;
            printf("\n  ✓ You win! All safe cells revealed.\n");
            print_board(true);
        }
    }

    if (!won && !game_over) printf("  [MS] Game exited.\n");

    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
