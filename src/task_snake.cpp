// ============================================================
// NeuralOS X — Task: SnakeGame
// CL-2006 Operating Systems Lab | Spring 2026
// ------------------------------------------------------------
// A simple text-based Snake game on a 10×20 grid.
// Uses simple grid printing (no ncurses dependency).
// Player enters direction (w/a/s/d) then presses Enter each turn.
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
    strncpy(msg.task_name, "SnakeGame", 31);
    write(write_fd, &msg, sizeof(msg));
}

#define ROWS 10
#define COLS 20
#define MAX_LEN 100

static int snake_r[MAX_LEN], snake_c[MAX_LEN];
static int snake_len;
static int food_r, food_c;
static int dir_r, dir_c; // direction vector

static void place_food() {
    /*
     * Places food at a random cell not occupied by the snake.
     */
    bool on_snake;
    do {
        food_r = rand() % ROWS;
        food_c = rand() % COLS;
        on_snake = false;
        for (int i = 0; i < snake_len; i++)
            if (snake_r[i] == food_r && snake_c[i] == food_c)
                on_snake = true;
    } while (on_snake);
}

static void print_grid(int score) {
    /*
     * Clears the screen with newlines and redraws the game grid.
     * Snake head is 'O', body is 'o', food is '*', empty is '.'.
     */
    printf("\n  Score: %d  |  Controls: w/a/s/d + Enter  |  'q' to quit\n", score);
    printf("  +");
    for (int c = 0; c < COLS; c++) printf("-");
    printf("+\n");

    for (int r = 0; r < ROWS; r++) {
        printf("  |");
        for (int c = 0; c < COLS; c++) {
            if (r == food_r && c == food_c) { printf("*"); continue; }
            bool is_snake = false;
            for (int i = 0; i < snake_len; i++) {
                if (snake_r[i] == r && snake_c[i] == c) {
                    printf("%c", i == 0 ? 'O' : 'o');
                    is_snake = true; break;
                }
            }
            if (!is_snake) printf(".");
        }
        printf("|\n");
    }
    printf("  +");
    for (int c = 0; c < COLS; c++) printf("-");
    printf("+\n");
}

int main(int argc, char* argv[]) {
    int read_fd  = (argc >= 2) ? atoi(argv[1]) : -1;
    int write_fd = (argc >= 3) ? atoi(argv[2]) : -1;

    signal(SIGTERM, handle_sigterm);
    srand((unsigned)time(nullptr) ^ getpid());

    // Init snake in middle
    snake_len = 3;
    snake_r[0] = ROWS/2; snake_c[0] = COLS/2;
    snake_r[1] = ROWS/2; snake_c[1] = COLS/2 - 1;
    snake_r[2] = ROWS/2; snake_c[2] = COLS/2 - 2;
    dir_r = 0; dir_c = 1; // moving right
    place_food();

    printf("\n╔══════════════════════════════════╗\n");
    printf("║   NeuralOS X — Snake Game        ║\n");
    printf("╚══════════════════════════════════╝\n");

    int score    = 0;
    bool running = true;
    char line[16];

    while (!g_quit && running) {
        print_grid(score);
        printf("  Move> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;
        char ch = line[0];

        if (ch == 'q') break;
        if (ch == 'w') { dir_r = -1; dir_c =  0; }
        if (ch == 's') { dir_r =  1; dir_c =  0; }
        if (ch == 'a') { dir_r =  0; dir_c = -1; }
        if (ch == 'd') { dir_r =  0; dir_c =  1; }

        // Move: shift body
        for (int i = snake_len - 1; i > 0; i--) {
            snake_r[i] = snake_r[i-1];
            snake_c[i] = snake_c[i-1];
        }
        snake_r[0] += dir_r;
        snake_c[0] += dir_c;

        // Wall collision
        if (snake_r[0] < 0 || snake_r[0] >= ROWS ||
            snake_c[0] < 0 || snake_c[0] >= COLS) {
            printf("\n  ✗ Hit the wall! Game over. Score: %d\n", score);
            running = false; break;
        }

        // Self collision
        for (int i = 1; i < snake_len; i++) {
            if (snake_r[0] == snake_r[i] && snake_c[0] == snake_c[i]) {
                printf("\n  ✗ Ate yourself! Game over. Score: %d\n", score);
                running = false; break;
            }
        }
        if (!running) break;

        // Food
        if (snake_r[0] == food_r && snake_c[0] == food_c) {
            score++;
            if (snake_len < MAX_LEN) {
                snake_r[snake_len] = snake_r[snake_len-1];
                snake_c[snake_len] = snake_c[snake_len-1];
                snake_len++;
            }
            place_food();
        }
    }

    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
