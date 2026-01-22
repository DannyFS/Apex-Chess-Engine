#pragma once

#include "position.h"
#include "tt.h"
#include "timeman.h"
#include "util/threads.h"
#include <atomic>
#include <memory>
#include <vector>

struct SearchLimits {
    int depth = 0;
    int movetime = 0;
    bool infinite = false;
    bool ponder = false;
    int wtime = 0, btime = 0, winc = 0, binc = 0, movestogo = 0;
};

class Search;

class Searcher {
public:
    Searcher(Search& parent, int id);
    void search(Position root, const SearchLimits& limits, bool is_main);

    Move best_move() const { return bestmove; }
    int nodes_searched() const { return nodes; }

private:
    Search& searcher_parent;
    int id = 0;
    Move bestmove = MOVE_NONE;
    int bestscore = 0;

    std::array<std::array<std::array<int, 64>, 64>, COLOR_NB> history{};
    std::array<std::array<Move, MAX_PLY>, 2> killers{}; // two killers per ply
    std::array<std::array<std::array<Move, 64>, 64>, COLOR_NB> countermove{};

    int nodes = 0;

    int search_pvs(Position& pos, int depth, int alpha, int beta, int ply, bool pv);
    int qsearch(Position& pos, int alpha, int beta, int ply);

    void update_history(Color c, Move m, int depth);
    int static_eval(Position& pos);
    bool time_up() const;
};

class Search {
public:
    Search();
    void set_threads(int n);
    void set_hash(int mb);
    void new_game();

    void go(Position& pos, const SearchLimits& limits);
    void stop();

    Move best_move() const { return bestmove.load(); }
    int best_score() const { return bestscore.load(); }
    int nodes() const { return searchers.empty() ? 0 : searchers[0]->nodes_searched(); }
    void update_best(Move m, int score) { bestmove.store(m); bestscore.store(score); }

    TranspositionTable& tt() { return table; }
    const TimeManager& timeman() const { return time_manager; }
    TimeManager& timeman() { return time_manager; }

    std::atomic<bool> stop_flag{false};

private:
    ThreadPool pool;
    std::vector<std::unique_ptr<Searcher>> searchers;
    TranspositionTable table;
    TimeManager time_manager;

    std::atomic<Move> bestmove{MOVE_NONE};
    std::atomic<int> bestscore{0};

    int thread_count = 1;
};
