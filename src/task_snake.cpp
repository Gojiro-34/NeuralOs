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
#include <termios.h>
#include <sys/select.h>

static volatile bool g_quit = false;
static struct termios orig_termios;
static bool raw_mode_enabled = false;

static void disable_raw_mode() {
    if (raw_mode_enabled) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
        raw_mode_enabled = false;
    }
}

static void handle_sigterm(int) { 
    g_quit = true; 
    disable_raw_mode();
}

static void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    raw_mode_enabled = true;
}

static int _kbhit() {
    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

static int _getch() {
    char ch;
    if (read(STDIN_FILENO, &ch, 1) > 0) return ch;
    return 0;
}

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
     * Clears the screen with ANSI escapes and redraws the game grid.
     * Snake head is 'O', body is 'o', food is '*', empty is '.'.
     */
    printf("\033[2J\033[H"); // Clear screen and move cursor to top-left
    printf("\n  Score: %d  |  Controls: Arrows or w/a/s/d, 'p' pause, 'q' quit\n", score);
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

    printf("\n╔══════════════════════════════════╗\n");
    printf("║   NeuralOS X — Snake Game        ║\n");
    printf("╚══════════════════════════════════╝\n");

    bool play_again = true;
    while (!g_quit && play_again) {
        // Init snake in middle
        snake_len = 3;
        snake_r[0] = ROWS/2; snake_c[0] = COLS/2;
        snake_r[1] = ROWS/2; snake_c[1] = COLS/2 - 1;
        snake_r[2] = ROWS/2; snake_c[2] = COLS/2 - 2;
        dir_r = 0; dir_c = 1; // moving right
        place_food();

        int score    = 0;
        bool running = true;
        bool paused  = false;
        const char* death_reason = "";

        enable_raw_mode();

        while (!g_quit && running) {
            print_grid(score);
            if (paused) {
                printf("  *** PAUSED *** Press 'p' to resume.\n");
            }
            fflush(stdout);

            bool dir_changed = false;
            for(int t = 0; t < 200; t += 10) {
                if (_kbhit() && (!dir_changed || paused)) {
                    int ch = _getch();
                    if (ch == 'q') {
                        running = false;
                        play_again = false;
                        break;
                    }
                    if (ch == 'p') {
                        paused = !paused;
                        break;
                    }
                    if (!paused) {
                        if (ch == '\033') { // Arrow keys (POSIX escape sequence)
                            char seq[2];
                            if (read(STDIN_FILENO, &seq[0], 1) == 1 && read(STDIN_FILENO, &seq[1], 1) == 1) {
                                if (seq[0] == '[') {
                                    switch(seq[1]) {
                                        case 'A': if (dir_r !=  1) { dir_r = -1; dir_c =  0; dir_changed = true; } break; // Up
                                        case 'B': if (dir_r != -1) { dir_r =  1; dir_c =  0; dir_changed = true; } break; // Down
                                        case 'C': if (dir_c != -1) { dir_r =  0; dir_c =  1; dir_changed = true; } break; // Right
                                        case 'D': if (dir_c !=  1) { dir_r =  0; dir_c = -1; dir_changed = true; } break; // Left
                                    }
                                }
                            }
                        } else {
                            if (ch == 'w' && dir_r !=  1) { dir_r = -1; dir_c =  0; dir_changed = true; }
                            else if (ch == 's' && dir_r != -1) { dir_r =  1; dir_c =  0; dir_changed = true; }
                            else if (ch == 'a' && dir_c !=  1) { dir_r =  0; dir_c = -1; dir_changed = true; }
                            else if (ch == 'd' && dir_c != -1) { dir_r =  0; dir_c =  1; dir_changed = true; }
                        }
                    }
                }
                usleep(10000); // 10ms
            }
            if (!running && g_quit) break;
            if (!running) break;

            if (paused) continue;

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
                death_reason = "Hit the wall!";
                running = false; break;
            }

            // Self collision
            for (int i = 1; i < snake_len; i++) {
                if (snake_r[0] == snake_r[i] && snake_c[0] == snake_c[i]) {
                    death_reason = "Ate yourself!";
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

        disable_raw_mode();

        if (!g_quit && !running && play_again) {
            if (strlen(death_reason) > 0) {
                printf("\r\n  ✗ %s Game over. Score: %d\r\n", death_reason, score);
            }
            printf("\r\n  [Try Again? Press 'r' to restart | Press 'q' to quit]\r\n");
            
            enable_raw_mode();
            while (!g_quit) {
                if (_kbhit()) {
                    int ch = _getch();
                    if (ch == 'r' || ch == 'R') {
                        play_again = true;
                        break;
                    }
                    if (ch == 'q' || ch == 'Q') {
                        play_again = false;
                        break;
                    }
                }
                usleep(50000);
            }
            disable_raw_mode();
        }
    }

    if (write_fd >= 0) send_task_done(write_fd);
    if (read_fd  >= 0) close(read_fd);
    if (write_fd >= 0) close(write_fd);
    return 0;
}
