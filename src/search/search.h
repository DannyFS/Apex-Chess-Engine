// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

#include "core/movegen.h"
#include "core/position.h"
#include "eval/nnue/nnue.h"
#include "search/tt.h"

namespace apex {

struct SearchLimits {
    std::int64_t time[COLOR_NB] = {-1, -1};  // ms remaining; -1 = not given
    std::int64_t inc[COLOR_NB] = {0, 0};
    int movestogo = 0;
    int depth = 0;           // 0 = no limit
    std::uint64_t nodes = 0; // 0 = no limit
    std::int64_t movetime = -1;
    bool infinite = false;
};

struct SearchResult {
    Move best = Move::none();
    Move ponder = Move::none();
    Value score = VALUE_NONE;
    int depth = 0;
    std::uint64_t nodes = 0;
};

struct SearchConfig {
    int move_overhead_ms = 10;
};

class Search {
public:
    using Clock = std::chrono::steady_clock;

    explicit Search(TranspositionTable& tt) : tt(tt) {}

    // Runs iterative deepening. If `silent`, no UCI info lines are printed.
    SearchResult run(Position& pos, const SearchLimits& limits, bool silent = false);
    void stop() { stop_flag.store(true, std::memory_order_relaxed); }
    bool stopped() const { return stop_flag.load(std::memory_order_relaxed); }
    void clear();  // ucinewgame: wipes heuristics (none yet in M3)

    SearchConfig config;

private:
    struct PV {
        Move moves[MAX_PLY + 1];
        int length = 0;
    };

    Value negamax(Position& pos, Value alpha, Value beta, int depth, int ply, bool pv_node);
    Value qsearch(Position& pos, Value alpha, Value beta, int ply, bool pv_node);
    void check_time();
    void setup_time(const Position& pos, const SearchLimits& limits);
    std::int64_t elapsed_ms() const;
    void print_info(int depth, Value score, const PV& pv) const;

    // M8: NNUE accumulators (used only when a network is loaded; HCE otherwise).
    nnue::Evaluator nn;
    bool use_nnue = false;
    Value static_evaluate(const Position& pos) const;
    void make_move(Position& pos, Move m) {
        if (use_nnue) nn.push(pos, m);
        pos.make_move(m);
    }
    void unmake_move(Position& pos, Move m) {
        pos.unmake_move(m);
        if (use_nnue) nn.pop();
    }

    TranspositionTable& tt;
    std::atomic<bool> stop_flag{false};
    SearchLimits lim;
    Clock::time_point start;
    std::int64_t soft_ms = 0, hard_ms = 0;
    bool time_managed = false;
    std::uint64_t nodes = 0;
    int seldepth = 0;
    PV pv_table[MAX_PLY + 1];
    Move root_best = Move::none();

    // EXP-012: move excluded from the search at each ply (singular-extension verification).
    Move excluded[MAX_PLY + 2] = {};

    // Quiet-move ordering heuristics (EXP-002)
    Move killers[MAX_PLY + 2][2] = {};
    int history[COLOR_NB][SQUARE_NB][SQUARE_NB] = {};
    void update_quiet_stats(const Position& pos, Move best, const Move* quiets, int n_quiets, int depth, int ply);
    void score_moves(const Position& pos, MoveList& list, Move tt_move, int ply) const;
};

std::string score_to_uci(Value v);

}  // namespace apex
