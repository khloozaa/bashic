// termsnake - a colorful Snake game for your terminal (C++17, no libraries)
//
// Linux / macOS: uses termios + poll.   Windows: uses conio + ANSI colors.
// Controls: arrows or WASD to move, space to pause, q or Esc to quit.

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#ifdef _WIN32
  #define NOMINMAX
  #include <conio.h>
  #include <windows.h>
  #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
    #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
  #endif
#else
  #include <poll.h>
  #include <sys/ioctl.h>
  #include <termios.h>
  #include <unistd.h>
#endif

// ---------------------------------------------------------------- settings
constexpr int W = 28;   // playfield width  (cells)
constexpr int H = 18;   // playfield height (cells)

// 256-color palette numbers
constexpr int C_WALL = 240, C_FIELD_A = 234, C_FIELD_B = 236;
constexpr int C_HEAD = 46,  C_BODY = 34,     C_FOOD = 196;

// ---------------------------------------------------------------- basics
struct Pt {
    int x, y;
    bool operator==(const Pt& o) const { return x == o.x && y == o.y; }
};

enum class Key { None, Up, Down, Left, Right, Pause, Confirm, Quit };

static volatile std::sig_atomic_t g_quit = 0;           // set by Ctrl+C
static void onSignal(int) { g_quit = 1; }

// ---------------------------------------------------------------- terminal
// RAII: sets up the terminal in the constructor, restores it in the destructor.
class Terminal {
public:
    Terminal() {
#ifdef _WIN32
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(out, &mode))
            SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#else
        tcgetattr(STDIN_FILENO, &saved_);
        termios raw = saved_;
        raw.c_lflag &= ~(ICANON | ECHO);   // no Enter needed, no echo (Ctrl+C still works)
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
#endif
        std::cout << "\033[?1049h\033[?25l\033[2J" << std::flush;  // alt screen, hide cursor
    }
    ~Terminal() {
        std::cout << "\033[0m\033[?25h\033[?1049l" << std::flush;  // show cursor, leave alt screen
#ifndef _WIN32
        tcflush(STDIN_FILENO, TCIFLUSH);
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_);
#endif
    }
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;
private:
#ifndef _WIN32
    termios saved_{};
#endif
};

#ifdef _WIN32
static Key readKey(int ms) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!_kbhit()) {
        if (std::chrono::steady_clock::now() >= end || g_quit) return Key::None;
        Sleep(1);
    }
    int c = _getch();
    if (c == 0 || c == 224) {                        // arrow keys come as two codes
        switch (_getch()) {
            case 72: return Key::Up;    case 80: return Key::Down;
            case 75: return Key::Left;  case 77: return Key::Right;
        }
        return Key::None;
    }
    switch (c) {
        case 'w': case 'W': return Key::Up;
        case 's': case 'S': return Key::Down;
        case 'a': case 'A': return Key::Left;
        case 'd': case 'D': return Key::Right;
        case ' ': case 'p': case 'P': return Key::Pause;
        case '\r': case 'r': case 'R': return Key::Confirm;
        case 'q': case 'Q': case 27: return Key::Quit;
    }
    return Key::None;
}
#else
static bool inputReady(int ms) {
    pollfd p{STDIN_FILENO, POLLIN, 0};
    return poll(&p, 1, ms) > 0;
}

// Waits up to `ms` milliseconds for one key press.
static Key readKey(int ms) {
    if (!inputReady(ms)) return Key::None;
    unsigned char c;
    if (read(STDIN_FILENO, &c, 1) != 1) return Key::None;
    if (c == 27) {                                    // Esc, or the start of an arrow key
        if (!inputReady(5)) return Key::Quit;         // lone Esc
        unsigned char seq[2] = {0, 0};
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return Key::None;
        if (seq[0] != '[' || !inputReady(5)) return Key::None;
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return Key::None;
        switch (seq[1]) {
            case 'A': return Key::Up;    case 'B': return Key::Down;
            case 'C': return Key::Right; case 'D': return Key::Left;
        }
        return Key::None;
    }
    switch (c) {
        case 'w': case 'W': return Key::Up;
        case 's': case 'S': return Key::Down;
        case 'a': case 'A': return Key::Left;
        case 'd': case 'D': return Key::Right;
        case ' ': case 'p': case 'P': return Key::Pause;
        case '\n': case '\r': case 'r': case 'R': return Key::Confirm;
        case 'q': case 'Q': return Key::Quit;
    }
    return Key::None;
}
#endif

// ---------------------------------------------------------------- high score
static std::string scorePath() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return std::string(home ? home : ".") + "/.termsnake_highscore";
}
static int loadBest() {
    std::ifstream f(scorePath());
    int v = 0;
    if (f >> v && v > 0) return v;
    return 0;
}
static void saveBest(int v) {
    std::ofstream f(scorePath());
    if (f) f << v << '\n';
}

// ---------------------------------------------------------------- game logic
class Game {
public:
    std::deque<Pt> snake;        // snake.front() is the head
    std::deque<Pt> turns;        // queued direction changes (max 2)
    std::vector<char> occ;       // occ[y*W+x] == 1 if the snake is on that cell
    Pt dir{1, 0}, food{0, 0};
    int score = 0, best = 0;
    bool dead = false, won = false, paused = false;

    void reset() {
        snake.clear(); turns.clear();
        occ.assign(W * H, 0);
        dir = {1, 0};
        score = 0; dead = won = paused = false;
        for (int i = 0; i < 3; i++) {                 // head at the center, body to its left
            Pt p{W / 2 - i, H / 2};
            snake.push_back(p);
            occ[idx(p)] = 1;
        }
        placeFood();
    }

    // Remember a turn, but ignore reversing (that would hit the neck).
    void queueTurn(Pt d) {
        Pt last = turns.empty() ? dir : turns.back();
        if (d == last) return;
        if (d.x == -last.x && d.y == -last.y) return;
        if (turns.size() < 2) turns.push_back(d);
    }

    // One tick of the game: move, eat, collide.
    void step() {
        if (!turns.empty()) { dir = turns.front(); turns.pop_front(); }
        Pt head = snake.front();
        Pt nh{head.x + dir.x, head.y + dir.y};
        bool eating = (nh == food);

        if (nh.x < 0 || nh.x >= W || nh.y < 0 || nh.y >= H) { dead = true; return; }

        if (!eating) {                                // tail moves away first, so the
            Pt tail = snake.back();                   // head may legally enter its cell
            snake.pop_back();
            occ[idx(tail)] = 0;
        }
        if (occ[idx(nh)]) { dead = true; return; }    // ran into itself

        snake.push_front(nh);
        occ[idx(nh)] = 1;

        if (eating) {
            score++;
            if ((int)snake.size() == W * H) { won = dead = true; }
            else placeFood();
        }
    }

    // Milliseconds per tick: starts slow, speeds up as you score.
    int tickMs() const { return std::max(55, 140 - score * 3); }

    static int idx(Pt p) { return p.y * W + p.x; }

private:
    std::mt19937 rng{std::random_device{}()};

    void placeFood() {
        std::vector<Pt> freeCells;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                if (!occ[y * W + x]) freeCells.push_back({x, y});
        if (freeCells.empty()) return;
        food = freeCells[std::uniform_int_distribution<size_t>(0, freeCells.size() - 1)(rng)];
    }
};

// ---------------------------------------------------------------- drawing
static int cellColor(const Game& g, int x, int y) {
    if (x < 0 || x >= W || y < 0 || y >= H) return C_WALL;
    Pt p{x, y};
    if (p == g.snake.front()) return C_HEAD;
    if (g.occ[Game::idx(p)]) return C_BODY;
    if (p == g.food) return C_FOOD;
    return ((x + y) % 2) ? C_FIELD_A : C_FIELD_B;
}

static void render(const Game& g) {
    std::string out;
    out.reserve(8192);
    out += "\033[H";                                  // cursor to top-left (no flicker)
    for (int y = -1; y <= H; y++) {
        int last = -1;
        for (int x = -1; x <= W; x++) {
            int c = cellColor(g, x, y);
            if (c != last) { out += "\033[48;5;" + std::to_string(c) + "m"; last = c; }
            out += "  ";                              // two spaces = one square "pixel"
        }
        out += "\033[0m\033[K\n";
    }
    out += "\033[0m Score: " + std::to_string(g.score) + "   Best: " + std::to_string(g.best) + "\033[K\n";
    if (g.won)         out += " YOU WIN!  Enter: play again   q: quit\033[K";
    else if (g.dead)   out += " GAME OVER   Enter: play again   q: quit\033[K";
    else if (g.paused) out += " PAUSED   Space: resume   q: quit\033[K";
    else               out += " Arrows/WASD: move   Space: pause   q: quit\033[K";
    std::cout << out << std::flush;
}

// ---------------------------------------------------------------- main loop
// Plays one round. Returns when the snake dies or the player quits.
static void playRound(Game& g) {
    using clock = std::chrono::steady_clock;
    while (!g_quit) {
        render(g);
        auto deadline = clock::now() + std::chrono::milliseconds(g.tickMs());

        // Wait for the next tick, but react to keys the moment they arrive.
        while (!g_quit) {
            auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
            if (left <= 0) break;
            switch (readKey((int)left)) {
                case Key::Up:    if (!g.paused) g.queueTurn({0, -1}); break;
                case Key::Down:  if (!g.paused) g.queueTurn({0, 1});  break;
                case Key::Left:  if (!g.paused) g.queueTurn({-1, 0}); break;
                case Key::Right: if (!g.paused) g.queueTurn({1, 0});  break;
                case Key::Pause: g.paused = !g.paused; render(g);     break;
                case Key::Quit:  g_quit = 1;                          break;
                default: break;
            }
        }
        if (g_quit) return;
        if (g.paused) continue;

        g.step();
        if (g.dead) return;
    }
}

int main() {
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

#ifndef _WIN32
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        std::cerr << "termsnake must be run in a terminal.\n";
        return 1;
    }
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 &&
        (ws.ws_col < 2 * (W + 2) || ws.ws_row < H + 4)) {
        std::cerr << "Terminal too small: need at least " << 2 * (W + 2) << "x" << H + 4
                  << " (yours is " << ws.ws_col << "x" << ws.ws_row << ").\n";
        return 1;
    }
#endif

    Terminal term;                                    // restored automatically on exit
    Game game;
    game.best = loadBest();

    while (!g_quit) {
        game.reset();
        playRound(game);
        if (g_quit) break;

        if (game.score > game.best) { game.best = game.score; saveBest(game.best); }
        render(game);                                 // shows GAME OVER
        for (;;) {                                    // wait: Enter = again, q = quit
            if (g_quit) break;
            Key k = readKey(100);
            if (k == Key::Quit) { g_quit = 1; break; }
            if (k == Key::Confirm) break;
        }
    }
    return 0;
}
