// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#include "uci.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#include "core/movegen.h"
#include "core/position.h"
#include "eval/eval.h"
#include "eval/eval_layout.h"
#include "eval/nnue/nnue.h"
#include "search/search.h"
#include "search/tt.h"
#include "tools/datagen.h"
#include "util/cpuinfo.h"

namespace apex {

namespace {

// Bench positions: varied openings, middlegames and endgames. Never change this list
// without bumping the bench methodology version in BENCHMARKS.md.
const char* BenchFens[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "rnbqkb1r/pp1p1ppp/4pn2/2p5/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 0 4",
    "r1bq1rk1/ppp2ppp/2np1n2/2b1p3/2B1P3/2PP1N2/PP3PPP/RNBQ1RK1 w - - 1 7",
    "r2q1rk1/pp2bppp/2n1bn2/3p4/3P4/2NBBN2/PP3PPP/R2Q1RK1 w - - 4 11",
    "2r3k1/5pp1/p3p2p/1p1qP3/3P4/P4Q1P/1P3PP1/2R3K1 w - - 0 28",
    "8/5pk1/6p1/3P4/2p5/2P3P1/5PK1/8 w - - 0 45",
    "8/8/4k3/3p4/3P4/4K3/8/8 w - - 0 60",
    "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
    "r1b1k2r/ppppnppp/2n2q2/2b5/3NP3/2P1B3/PP3PPP/RN1QKB1R w KQkq - 0 1",
    "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
    "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
    "8/8/8/8/5kp1/P7/8/1K1N4 w - - 0 1",
    "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - - 6 22",
    "r1q2rk1/2p1bppp/2Pp4/p6b/Q1PNp3/4B3/PP1R1PPP/2K4R w - - 2 18",
};

constexpr int DefaultBenchDepth = 6;

struct Engine {
    TranspositionTable tt;
    std::unique_ptr<Search> search;
    Position pos;
    std::thread worker;
    int hash_mb = 16;

    Engine() {
        tt.resize(hash_mb);
        search = std::make_unique<Search>(tt);
    }
    ~Engine() { wait(); }

    void wait() {
        if (worker.joinable()) {
            search->stop();
            worker.join();
        }
    }
};

void cmd_uci() {
    std::printf("id name %s %s\n", EngineName, EngineVersion);
    std::printf("id author the Apex developers\n");
    std::printf("option name Hash type spin default 16 min 1 max 65536\n");
    std::printf("option name Threads type spin default 1 min 1 max 1\n");
    std::printf("option name Move Overhead type spin default 10 min 0 max 5000\n");
    std::printf("option name Ponder type check default false\n");
    std::printf("option name EvalFile type string default <empty>\n");
    std::printf("uciok\n");
    std::fflush(stdout);
}

void cmd_setoption(Engine& e, std::istringstream& is) {
    std::string token, name, value;
    is >> token;  // "name"
    while (is >> token && token != "value") name += (name.empty() ? "" : " ") + token;
    while (is >> token) value += (value.empty() ? "" : " ") + token;
    std::string lname = name;
    std::transform(lname.begin(), lname.end(), lname.begin(), ::tolower);
    try {
        if (lname == "hash") {
            e.wait();
            e.hash_mb = std::clamp(std::stoi(value), 1, 65536);
            e.tt.resize(std::size_t(e.hash_mb));
        } else if (lname == "move overhead") {
            e.search->config.move_overhead_ms = std::clamp(std::stoi(value), 0, 5000);
        } else if (lname == "evalfile") {
            // M8: an NNUE network file; "<empty>" (the default) selects the hand-crafted eval.
            e.wait();
            if (value.empty() || value == "<empty>") {
                nnue::unload();
                std::printf("info string eval: hand-crafted\n");
            } else {
                std::string err;
                if (nnue::load(value, err))
                    std::printf("info string eval: NNUE %s\n", nnue::loaded_name().c_str());
                else
                    std::printf("info string EvalFile not loaded (%s); eval unchanged (%s)\n", err.c_str(),
                                nnue::loaded() ? nnue::loaded_name().c_str() : "hand-crafted");
            }
        } else if (lname == "threads" || lname == "ponder") {
            // Accepted for GUI compatibility; Lazy SMP arrives in M10.
        } else {
            std::printf("info string unknown option '%s'\n", name.c_str());
        }
    } catch (...) {
        std::printf("info string invalid value '%s' for option '%s'\n", value.c_str(), name.c_str());
    }
    std::fflush(stdout);
}

void cmd_position(Engine& e, std::istringstream& is) {
    std::string token, fen;
    is >> token;
    if (token == "startpos") {
        fen = Position::StartFEN;
        is >> token;  // possibly "moves"
    } else if (token == "fen") {
        while (is >> token && token != "moves") fen += token + " ";
    } else {
        return;
    }
    if (!e.pos.set(fen)) {
        std::printf("info string invalid fen, using start position\n");
        std::fflush(stdout);
    }
    while (is >> token) {
        Move m = e.pos.parse_uci(token);
        if (m.is_none()) {
            std::printf("info string illegal move '%s' ignored (and all after it)\n", token.c_str());
            std::fflush(stdout);
            break;
        }
        e.pos.make_move(m);
    }
}

void cmd_go(Engine& e, std::istringstream& is) {
    SearchLimits lim;
    std::string token;
    int perft_depth = 0;
    while (is >> token) {
        if (token == "wtime") is >> lim.time[WHITE];
        else if (token == "btime") is >> lim.time[BLACK];
        else if (token == "winc") is >> lim.inc[WHITE];
        else if (token == "binc") is >> lim.inc[BLACK];
        else if (token == "movestogo") is >> lim.movestogo;
        else if (token == "depth") is >> lim.depth;
        else if (token == "nodes") is >> lim.nodes;
        else if (token == "movetime") is >> lim.movetime;
        else if (token == "infinite") lim.infinite = true;
        else if (token == "perft") is >> perft_depth;
    }
    e.wait();

    if (perft_depth > 0) {
        auto t0 = std::chrono::steady_clock::now();
        MoveList list;
        generate_legal(e.pos, list);
        std::uint64_t total = 0;
        for (Move m : list) {
            e.pos.make_move(m);
            std::uint64_t n = perft_depth > 1 ? perft(e.pos, perft_depth - 1) : 1;
            e.pos.unmake_move(m);
            total += n;
            std::printf("%s: %llu\n", Position::move_to_uci(m).c_str(), (unsigned long long)n);
        }
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("\nNodes searched: %llu (%.3fs)\n\n", (unsigned long long)total, s);
        std::fflush(stdout);
        return;
    }

    // The search thread owns a copy of the position, so "position" during a search is harmless.
    e.worker = std::thread([&e, lim, pos = e.pos]() mutable {
        SearchResult r = e.search->run(pos, lim);
        // Under "go infinite" the GUI expects bestmove only after "stop".
        while (lim.infinite && !e.search->stopped()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        std::string out = "bestmove " + Position::move_to_uci(r.best);
        if (r.ponder) out += " ponder " + Position::move_to_uci(r.ponder);
        std::printf("%s\n", out.c_str());
        std::fflush(stdout);
    });
}

}  // namespace

void run_bench(int depth, int hash_mb) {
    TranspositionTable tt;
    tt.resize(std::size_t(hash_mb));
    Search search(tt);
    std::uint64_t total = 0;
    auto t0 = std::chrono::steady_clock::now();
    int idx = 0;
    for (const char* fen : BenchFens) {
        Position pos;
        if (!pos.set(fen)) {
            std::fprintf(stderr, "bench: invalid fen %s\n", fen);
            continue;
        }
        tt.clear();
        search.clear();
        SearchLimits lim;
        lim.depth = depth;
        SearchResult r = search.run(pos, lim, true);
        total += r.nodes;
        std::printf("%2d %-80s %10llu nodes  %-6s %s\n", ++idx, fen, (unsigned long long)r.nodes,
                    Position::move_to_uci(r.best).c_str(), score_to_uci(r.score).c_str());
    }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("\n===========================\n");
    std::printf("Total time (ms) : %llu\n", (unsigned long long)(s * 1000));
    std::printf("Nodes searched  : %llu\n", (unsigned long long)total);
    std::printf("Nodes/second    : %llu\n", (unsigned long long)(total / std::max(s, 1e-9)));
    // OpenBench-compatible summary line
    std::printf("%llu nodes %llu nps\n", (unsigned long long)total, (unsigned long long)(total / std::max(s, 1e-9)));
    std::fflush(stdout);
}

namespace {
void texel_features(const std::string& in_path, const std::string& out_path) {
    std::FILE* in = std::fopen(in_path.c_str(), "r");
    std::FILE* out = std::fopen(out_path.c_str(), "w");
    if (!in || !out) {
        std::printf("info string cannot open texel files\n");
        if (in) std::fclose(in);
        if (out) std::fclose(out);
        return;
    }
    std::vector<int> coeffs(evl::NUM_WEIGHTS);
    char line[512];
    long n = 0, bad = 0;
    Position pos;
    while (std::fgets(line, sizeof(line), in)) {
        std::string l(line);
        auto bar = l.find('|');
        if (bar == std::string::npos) { ++bad; continue; }
        std::string fen = l.substr(0, bar);
        double result = std::stod(l.substr(bar + 1));
        if (!pos.set(fen)) { ++bad; continue; }
        int phase = eval_features(pos, coeffs.data());
        std::fprintf(out, "%.1f %d %d %d", result, phase, pos.side_to_move() == WHITE ? 1 : 0, pos.rule50());
        for (int i = 0; i < evl::NUM_WEIGHTS; ++i)
            if (coeffs[i]) std::fprintf(out, " %d:%d", i, coeffs[i]);
        std::fputc('\n', out);
        ++n;
    }
    std::fclose(in);
    std::fclose(out);
    std::printf("info string texel-features: %ld positions, %ld skipped\n", n, bad);
    std::fflush(stdout);
}
}  // namespace

namespace {
// nnue-data <in> <out>: converts datagen text ("<fen> | <score white> | <result white>")
// into fixed 70-byte records for tools/nnue/train.py:
//   uint8 count, uint8 stm (0 white, 1 black), int16 score (white POV, cp),
//   uint8 result (0 black win, 1 draw, 2 white win), uint8 reserved,
//   uint16 feature[32] (white-perspective feature index, 0xFFFF padding).
void nnue_data(const std::string& in_path, const std::string& out_path) {
    std::FILE* in = std::fopen(in_path.c_str(), "r");
    std::FILE* out = std::fopen(out_path.c_str(), "ab");
    if (!in || !out) {
        std::printf("info string cannot open nnue-data files\n");
        if (in) std::fclose(in);
        if (out) std::fclose(out);
        return;
    }
    char line[512];
    long n = 0, bad = 0;
    Position pos;
    while (std::fgets(line, sizeof(line), in)) {
        std::string l(line);
        const auto b1 = l.find('|'), b2 = l.find('|', b1 == std::string::npos ? 0 : b1 + 1);
        if (b1 == std::string::npos || b2 == std::string::npos || !pos.set(l.substr(0, b1))) { ++bad; continue; }
        const int score = std::clamp(std::stoi(l.substr(b1 + 1, b2 - b1 - 1)), -32000, 32000);
        const double res = std::stod(l.substr(b2 + 1));
        unsigned char rec[70] = {};
        std::uint16_t feats[32];
        std::fill(feats, feats + 32, std::uint16_t(0xFFFF));
        int k = 0;
        Bitboard occ = pos.pieces();
        while (occ && k < 32) {
            const Square s = pop_lsb(occ);
            feats[k++] = std::uint16_t(nnue::feature(WHITE, pos.piece_on(s), s));
        }
        rec[0] = std::uint8_t(k);
        rec[1] = pos.side_to_move() == WHITE ? 0 : 1;
        const std::int16_t sc = std::int16_t(score);
        std::memcpy(rec + 2, &sc, 2);
        rec[4] = res > 0.75 ? 2 : (res < 0.25 ? 0 : 1);
        std::memcpy(rec + 6, feats, sizeof(feats));
        std::fwrite(rec, sizeof(rec), 1, out);
        ++n;
    }
    std::fclose(in);
    std::fclose(out);
    std::printf("info string nnue-data: %ld positions, %ld skipped\n", n, bad);
    std::fflush(stdout);
}
}  // namespace

int uci_main(int argc, char** argv) {
    Engine e;

    auto handle = [&](const std::string& line) -> bool {
        std::istringstream is(line);
        std::string token;
        is >> token;
        if (token == "quit") {
            e.wait();
            return false;
        } else if (token == "stop") {
            e.wait();
        } else if (token == "uci") {
            cmd_uci();
        } else if (token == "isready") {
            std::printf("readyok\n");
            std::fflush(stdout);
        } else if (token == "ucinewgame") {
            e.wait();
            e.tt.clear();
            e.search->clear();
        } else if (token == "setoption") {
            cmd_setoption(e, is);
        } else if (token == "position") {
            cmd_position(e, is);
        } else if (token == "go") {
            cmd_go(e, is);
        } else if (token == "ponderhit") {
            // Pondering is not implemented yet; nothing to do.
        } else if (token == "d") {
            std::printf("%s", e.pos.pretty().c_str());
            std::fflush(stdout);
        } else if (token == "eval") {
            std::printf("%s", eval_trace(e.pos).c_str());
            if (nnue::loaded()) {
                nnue::Evaluator ev;
                ev.reset(e.pos);
                std::printf("NNUE (%s): %d cp (side to move, before fifty-move damping)\n",
                            nnue::loaded_name().c_str(), ev.evaluate(e.pos));
            }
            std::fflush(stdout);
        } else if (token == "bench") {
            int depth = DefaultBenchDepth;
            if (!(is >> depth)) depth = DefaultBenchDepth;
            run_bench(depth, 16);
        } else if (token == "texel-features") {
            // texel-features <in> <out>: in = "<fen> | <result>" lines (tools/texel/extract.py).
            // out = "<result> <phase> <stm> <rule50> <idx>:<coef> ..." (nonzero coefficients).
            std::string in_path, out_path;
            is >> in_path >> out_path;
            texel_features(in_path, out_path);
        } else if (token == "nnue-data") {
            std::string in_path, out_path;
            is >> in_path >> out_path;
            nnue_data(in_path, out_path);
        } else if (token == "datagen") {
            // datagen <out> <games> <nodes> <seed>: self-play training data (src/tools/datagen.cpp).
            DatagenConfig cfg;
            is >> cfg.out_path >> cfg.games >> cfg.nodes >> cfg.seed;
            if (cfg.out_path.empty()) {
                std::printf("info string usage: datagen <out> <games> <nodes> <seed>\n");
                std::fflush(stdout);
            } else {
                run_datagen(cfg);
            }
        } else if (token == "cpuinfo") {
            std::printf("%s", cpu_report().c_str());
            std::fflush(stdout);
        } else if (!token.empty()) {
            std::printf("info string unknown command '%s'\n", token.c_str());
            std::fflush(stdout);
        }
        return true;
    };

    if (argc > 1) {
        std::string cmd;
        for (int i = 1; i < argc; ++i) cmd += std::string(i > 1 ? " " : "") + argv[i];
        handle(cmd);
        e.wait();
        return 0;
    }

    std::string line;
    while (std::getline(std::cin, line))
        if (!handle(line)) break;
    e.wait();
    return 0;
}

}  // namespace apex
