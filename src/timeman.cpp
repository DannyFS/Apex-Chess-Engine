#include "timeman.h"
#include "search.h"

void TimeManager::init(const SearchLimits& limits, Color side, int move_overhead_ms) {
    start = std::chrono::steady_clock::now();
    infinite = limits.infinite || (limits.movetime == 0 && limits.wtime == 0 && limits.btime == 0 && limits.depth > 0);

    if (limits.movetime > 0) {
        alloc_ms = limits.movetime;
        return;
    }

    int time = (side == WHITE) ? limits.wtime : limits.btime;
    int inc = (side == WHITE) ? limits.winc : limits.binc;

    if (limits.movestogo <= 0) {
        alloc_ms = time / 20 + inc / 2; // default 20 moves left
    } else {
        alloc_ms = time / limits.movestogo + inc / 2;
    }

    alloc_ms -= move_overhead_ms;
    if (alloc_ms < 10) alloc_ms = 10;
}

bool TimeManager::time_up() const {
    if (infinite) return false;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    return elapsed >= alloc_ms;
}

int TimeManager::time_left() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    return alloc_ms - static_cast<int>(elapsed);
}
