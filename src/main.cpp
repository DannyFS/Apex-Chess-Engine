// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>

#include "core/movegen.h"
#include "core/position.h"

using namespace apex;

// Temporary M2 driver: perft / divide. The UCI front end replaces this in M3.
int main(int argc, char** argv) {
    init_core();
    if (argc >= 3 && std::string(argv[1]) == "perft") {
        int depth = std::stoi(argv[2]);
        std::string fen = argc >= 4 ? argv[3] : Position::StartFEN;
        Position pos;
        if (!pos.set(fen)) {
            std::fprintf(stderr, "invalid fen\n");
            return 1;
        }
        auto t0 = std::chrono::steady_clock::now();
        MoveList list;
        generate_legal(pos, list);
        std::uint64_t total = 0;
        for (Move m : list) {
            pos.make_move(m);
            std::uint64_t n = depth > 1 ? perft(pos, depth - 1) : 1;
            pos.unmake_move(m);
            total += n;
            std::printf("%s: %llu\n", Position::move_to_uci(m).c_str(), (unsigned long long)n);
        }
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("\nNodes: %llu\nTime: %.3fs\nNPS: %.0f\n", (unsigned long long)total, s, total / s);
        return 0;
    }
    std::printf("Apex 0.1-dev\nusage: apex perft <depth> [fen]\n");
    return 0;
}
