#pragma once

#include "util/types.h"
#include <chrono>

struct SearchLimits;

class TimeManager {
public:
    void init(const SearchLimits& limits, Color side, int move_overhead_ms);
    bool time_up() const;
    int time_left() const;
    int allocated() const { return alloc_ms; }

private:
    std::chrono::steady_clock::time_point start;
    int alloc_ms = 0;
    bool infinite = false;
};
