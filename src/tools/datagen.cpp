// Apex chess engine: self-play training-data generation (M7).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Each game starts from the initial position plus 8 or 9 uniformly random legal
// plies (seeded, so a run is reproducible), discards openings the engine already
// scores beyond +-1000 cp, and then plays itself with a fixed node limit per move.
// Positions are recorded when the side to move is not in check, the chosen move is
// quiet and the score is not a mate score; this is the usual NNUE-data filter.
// Adjudication: win after 4 consecutive plies with |score| >= 2000 (same sign);
// draw after 10 consecutive plies with |score| <= 10 once 80 plies are played;
// draw at 400 plies. Otherwise games end by the rules (mate, stalemate, repetition,
// fifty moves, insufficient material).
#include "datagen.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "core/movegen.h"
#include "core/position.h"
#include "core/prng.h"
#include "search/search.h"
#include "search/tt.h"

namespace apex {

namespace {
const char* StartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

struct Record {
    std::string fen;
    int score_white;
};
}  // namespace

void run_datagen(const DatagenConfig& cfg) {
    std::FILE* out = std::fopen(cfg.out_path.c_str(), "a");
    if (!out) {
        std::printf("info string datagen: cannot open %s\n", cfg.out_path.c_str());
        std::fflush(stdout);
        return;
    }
    TranspositionTable tt;
    tt.resize(std::size_t(cfg.hash_mb));
    Search search(tt);
    PRNG rng(cfg.seed);
    const auto t0 = std::chrono::steady_clock::now();
    long positions = 0;
    int played = 0;

    while (played < cfg.games) {
        Position pos;
        pos.set(StartFen);
        tt.clear();
        search.clear();

        // Random opening.
        bool ok = true;
        const int plies = cfg.random_plies + int(rng.rand() & 1);
        for (int i = 0; i < plies; ++i) {
            MoveList legal;
            generate_legal(pos, legal);
            if (legal.empty()) { ok = false; break; }
            pos.make_move(legal[int(rng.rand() % std::uint64_t(legal.size()))].move);
        }
        if (!ok) continue;
        {
            MoveList legal;
            generate_legal(pos, legal);
            if (legal.empty()) continue;
            SearchLimits lim;
            lim.nodes = std::uint64_t(cfg.nodes);
            SearchResult r = search.run(pos, lim, true);
            if (std::abs(r.score) > 1000) continue;  // lopsided opening
        }

        std::vector<Record> recs;
        double result = 0.5;
        int win_run = 0, win_sign = 0, draw_run = 0;
        for (int ply = 0;; ++ply) {
            MoveList legal;
            generate_legal(pos, legal);
            const bool in_check = pos.in_check();
            if (legal.empty()) {
                result = in_check ? (pos.side_to_move() == WHITE ? 0.0 : 1.0) : 0.5;
                break;
            }
            if (pos.is_fifty_move_draw() || pos.is_repetition(0) || pos.insufficient_material() || ply >= 400) {
                result = 0.5;
                break;
            }
            SearchLimits lim;
            lim.nodes = std::uint64_t(cfg.nodes);
            SearchResult r = search.run(pos, lim, true);
            if (!r.best) break;  // should not happen with legal moves
            const int s = r.score;
            const int sw = pos.side_to_move() == WHITE ? s : -s;

            if (!in_check && !r.best.is_tactical() && std::abs(s) < VALUE_MATE_IN_MAX_PLY)
                recs.push_back({pos.fen(), sw});

            // Adjudication
            if (std::abs(sw) >= 2000) {
                const int sign = sw > 0 ? 1 : -1;
                win_run = (sign == win_sign) ? win_run + 1 : 1;
                win_sign = sign;
                if (win_run >= 4) { result = sign > 0 ? 1.0 : 0.0; break; }
            } else {
                win_run = 0;
                win_sign = 0;
            }
            draw_run = (ply >= 80 && std::abs(sw) <= 10) ? draw_run + 1 : 0;
            if (draw_run >= 10) { result = 0.5; break; }

            pos.make_move(r.best);
        }

        for (const Record& rec : recs) std::fprintf(out, "%s | %d | %.1f\n", rec.fen.c_str(), rec.score_white, result);
        std::fflush(out);
        positions += long(recs.size());
        ++played;
        if (played % 50 == 0 || played == cfg.games) {
            const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("info string datagen: %d games, %ld positions, %.0f pos/s\n", played, positions,
                        positions / std::max(sec, 1e-3));
            std::fflush(stdout);
        }
    }
    std::fclose(out);
}

}  // namespace apex
