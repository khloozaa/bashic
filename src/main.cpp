#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

using Clock = std::chrono::steady_clock;
struct P {
    int x, y;
    bool operator==(const P& o) const { return x == o.x && y == o.y; }
};
const int W = 28, H = 18;
termios old;
volatile std::sig_atomic_t qt = 0;
void onInt(int) { qt = 1; }

void restore() {
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    std::cout << "\033[0m\033[?25h\033[?1049l" << std::flush;
}
void raw() {
    tcgetattr(STDIN_FILENO, &old);
    std::atexit(restore);
    termios t = old;
    t.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
    std::cout << "\033[?1049h\033[?25l\033[2J" << std::flush;
}
bool rdy(int ms) {
    pollfd p{STDIN_FILENO, POLLIN, 0};
    return poll(&p, 1, ms) > 0;
}
char key(int ms) {
    char c;
    if (!rdy(ms) || read(STDIN_FILENO, &c, 1) != 1) return 0;
    if (c == 27) {
        char a, b;
        if (!rdy(5)) return 'q';
        if (read(STDIN_FILENO, &a, 1) != 1 || a != '[') return 0;
        if (!rdy(5) || read(STDIN_FILENO, &b, 1) != 1) return 0;
        return b == 'A' ? 'w' : b == 'B' ? 's' : b == 'C' ? 'd' : b == 'D' ? 'a' : 0;
    }
    if (c == ' ') return 'p';
    if (c == '\n' || c == '\r') return 'r';
    return (char)std::tolower((unsigned char)c);
}
std::string path() {
    const char* h = std::getenv("HOME");
    return std::string(h ? h : ".") + "/.termsnake_highscore";
}
int load() {
    std::ifstream f(path());
    int v = 0;
    f >> v;
    return std::max(v, 0);
}
void save(int v) { std::ofstream(path()) << v << '\n'; }

struct Game {
    std::deque<P> snake, turns;
    std::vector<char> grd = std::vector<char>(W * H);
    P dir{1, 0}, fd{0, 0};
    int score = 0, best = 0;
    bool dd = false, wn = false, psd = false;
    std::mt19937 rng{std::random_device{}()};
    static int idx(P p) { return p.y * W + p.x; }
    void reset() {
        snake = {{10, 9}, {9, 9}, {8, 9}};
        turns.clear();
        std::fill(grd.begin(), grd.end(), 0);
        for (P p : snake) grd[idx(p)] = 1;
        dir = {1, 0};
        score = 0;
        dd = wn = psd = false;
        spawn();
    }
    bool spawn() {
        std::vector<int> cls;
        for (int i = 0; i < W * H; i++)
            if (!grd[i]) cls.push_back(i);
        if (cls.empty()) return false;
        int i = cls[std::uniform_int_distribution<size_t>(0, cls.size() - 1)(rng)];
        fd = {i % W, i / W};
        return true;
    }
    void turn(P d) {
        P last = turns.empty() ? dir : turns.back();
        if (d == last || (d.x + last.x == 0 && d.y + last.y == 0)) return;
        if (turns.size() < 2) turns.push_back(d);
    }
    void step() {
        if (!turns.empty()) {
            dir = turns.front();
            turns.pop_front();
        }
        P next{snake.front().x + dir.x, snake.front().y + dir.y};
        if (next.x < 0 || next.x >= W || next.y < 0 || next.y >= H) {
            dd = true;
            return;
        }
        if (grd[idx(next)] && !(next == snake.back())) {
            dd = true;
            return;
        }
        bool eat = next == fd;
        if (!eat) {
            grd[idx(snake.back())] = 0;
            snake.pop_back();
        }
        snake.push_front(next);
        grd[idx(next)] = 1;
        if (eat) {
            score++;
            if (!spawn()) wn = dd = true;
        }
    }
    int tick() const { return std::max(60, 150 - score * 3); }
};
void draw(const Game& g) {
    std::string out = "\033[H";
    for (int y = 0; y < H; y++) {
        int last = -1;
        for (int x = 0; x < W; x++) {
            P p{x, y};
            int c = (x + y) % 2 ? 236 : 234;
            if (p == g.fd) c = 196;
            else if (p == g.snake.front()) c = 46;
            else if (g.grd[Game::idx(p)]) c = 34;
            if (c != last) {
                out += "\033[48;5;" + std::to_string(c) + "m";
                last = c;
            }
            out += "  ";
        }
        out += "\033[0m\n";
    }
    out += " Score: " + std::to_string(g.score) + "   Best: " + std::to_string(g.best);
    if (g.wn) out += "   YOU WIN!";
    else if (g.dd) out += "   GAME OVER";
    else if (g.psd) out += "   PAUSED";
    out += "\033[K";
    std::cout << out << std::flush;
}
void play(Game& g) {
    while (!qt) {
        draw(g);
        auto end = Clock::now() + std::chrono::milliseconds(g.tick());
        for (;;) {
            int left = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now()).count();
            if (left <= 0 || qt) break;
            switch (key(left)) {
                case 'w': if (!g.psd) g.turn({0, -1}); break;
                case 's': if (!g.psd) g.turn({0, 1}); break;
                case 'a': if (!g.psd) g.turn({-1, 0}); break;
                case 'd': if (!g.psd) g.turn({1, 0}); break;
                case 'p': g.psd = !g.psd; draw(g); break;
                case 'q': qt = 1; break;
            }
        }
        if (qt) return;
        if (g.psd) continue;
        g.step();
        if (g.dd) return;
    }
}
/////////|||||||||||||||||||||||||||||||||||/////////
/////////|||||||||||||||||||||||||||||||||||/////////
int main() {
    std::signal(SIGINT, onInt);
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        std::cerr << "termsnake must be run in a terminal.\n";
        return 1;
    }
    winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col != 0 &&
        (ws.ws_col < 2 * W || ws.ws_row < H + 2)) {
        std::cerr << "Terminal too small: need " << 2 * W << "x" << H + 2 << ".\n";
        return 1;
    }
    raw();
    Game g;
    g.best = load();
    while (!qt) {
        g.reset();
        play(g);
        if (g.score > g.best) {
            g.best = g.score;
            save(g.best);
        }
        if (qt) break;
        draw(g);
        while (!qt) {
            char k = key(100);
            if (k == 'q') qt = 1;
            if (k == 'r') break;
        }
    }
}
