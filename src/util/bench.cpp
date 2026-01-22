#include "util/bench.h"
#include "nnue/nnue_loader.h"
#include <chrono>
#include <iostream>
#include <vector>

void run_bench(Search& searcher) {
    std::vector<std::string> fens = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r1bq1rk1/ppp2ppp/2np1n2/4p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 7",
        "r4rk1/pp1b1ppp/2n1pn2/q7/2BP4/2P2N2/PP1N1PPP/R2Q1RK1 w - - 0 10",
        "2rq1rk1/pp1b1ppp/2n1pn2/8/3P4/2P2N2/PP1N1PPP/R2Q1RK1 w - - 0 12",
        "r1bqk1nr/pppp1ppp/2n5/4p3/1bB1P3/2NP1N2/PPP2PPP/R1BQK2R w KQkq - 2 4"
    };

    SearchLimits limits;
    limits.depth = 9;

    u64 total_nodes = 0;
    auto t0 = std::chrono::steady_clock::now();

    for (const auto& fen : fens) {
        Position pos;
        pos.set_fen(fen);
        if (NNUE::loaded()) NNUE::refresh(pos);
        searcher.go(pos, limits);
        total_nodes += searcher.nodes();
    }

    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    double nps = ms > 0 ? (total_nodes * 1000.0 / ms) : 0.0;

    std::cout << "bench nodes " << total_nodes << " time " << ms << " ms nps " << (u64)nps << std::endl;
}
