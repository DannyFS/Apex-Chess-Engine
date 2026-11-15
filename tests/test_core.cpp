// Apex chess engine: core correctness tests (movegen, make/unmake, hashing, SEE).
// SPDX-License-Identifier: GPL-3.0-or-later
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/movegen.h"
#include "core/position.h"
#include "core/prng.h"
#include "eval/nnue/nnue.h"

using namespace apex;

static int failures = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (!(cond)) {                                                \
            ++failures;                                               \
            std::printf("FAIL %s:%d: %s | ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                 \
            std::printf("\n");                                        \
        }                                                             \
    } while (0)

struct PerftCase {
    const char* fen;
    int depth;
    std::uint64_t nodes;
};

// Reference values: chessprogramming.org "Perft Results" (positions 1-6) and the widely used
// edge-case suite (en passant pins, discovered checks, castling, promotions).
static const PerftCase QuickSuite[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 6, 11030083},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 5, 15833292},
    {"r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", 5, 15833292},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594},
    {"3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1", 6, 1134888},
    {"8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1", 6, 1015133},
    {"8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 6, 1440467},
    {"5k2/8/8/8/8/8/8/4K2R w K - 0 1", 6, 661072},
    {"3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", 6, 803711},
    {"r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 4, 1274206},
    {"r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1", 4, 1720476},
    {"2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1", 6, 3821001},
    {"8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1", 5, 1004658},
    {"4k3/1P6/8/8/8/8/K7/8 w - - 0 1", 6, 217342},
    {"8/P1k5/K7/8/8/8/8/8 w - - 0 1", 6, 92683},
    {"K1k5/8/P7/8/8/8/8/8 w - - 0 1", 6, 2217},
    {"8/k1P5/8/1K6/8/8/8/8 w - - 0 1", 7, 567584},
    {"8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1", 4, 23527},
};

static const PerftCase DeepSuite[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 6, 119060324},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 5, 193690690},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 7, 178633661},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 6, 706045033},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 5, 89941194},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 5, 164075551},
};

static void test_perft(bool deep) {
    std::uint64_t total = 0;
    auto t0 = std::chrono::steady_clock::now();
    auto run = [&](const PerftCase& c) {
        Position pos;
        CHECK(pos.set(c.fen), "fen rejected: %s", c.fen);
        std::uint64_t n = perft(pos, c.depth);
        total += n;
        CHECK(n == c.nodes, "%s depth %d: got %llu expected %llu", c.fen, c.depth, (unsigned long long)n,
              (unsigned long long)c.nodes);
        CHECK(pos.fen() == Position(c.fen).fen(), "position not restored after perft: %s", c.fen);
    };
    for (const auto& c : QuickSuite) run(c);
    if (deep)
        for (const auto& c : DeepSuite) run(c);
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("perft: %llu nodes in %.2fs (%.1f Mnps)\n", (unsigned long long)total, s, total / s / 1e6);
}

static void test_fen_roundtrip() {
    for (const auto& c : QuickSuite) {
        Position pos(c.fen);
        Position pos2(pos.fen());
        CHECK(pos.fen() == pos2.fen(), "roundtrip %s", c.fen);
        CHECK(pos.key() == pos2.key(), "key roundtrip %s", c.fen);
    }
    // Invalid FENs must be rejected.
    Position p;
    CHECK(!p.set("8/8/8/8/8/8/8/8 w - - 0 1"), "no kings accepted");
    CHECK(!p.set("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1"), "bad side accepted");
    CHECK(!p.set("4k3/8/8/8/8/8/8/4K2r b - - 0 1"), "side not to move in check accepted");
    // Irrelevant en-passant squares are dropped (canonical hashing).
    Position a("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");
    Position b("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    CHECK(a.key() == b.key(), "irrelevant ep square changes the key");
}

// Random playouts: incremental state must always equal recomputed state, and
// pseudo_legal() must agree exactly with the generator on random 16-bit moves.
static void test_fuzz(int games) {
    PRNG rng(12345);
    std::uint64_t moves_played = 0, pl_checks = 0;
    for (int g = 0; g < games; ++g) {
        Position pos(QuickSuite[g % 7].fen);
        std::vector<Move> played;
        for (int ply = 0; ply < 200; ++ply) {
            MoveList pseudo, legal;
            generate<GEN_ALL>(pos, pseudo);
            generate_legal(pos, legal);

            // Generator partitions
            MoveList tac, qui;
            generate<GEN_TACTICAL>(pos, tac);
            generate<GEN_QUIET>(pos, qui);
            CHECK(tac.size() + qui.size() == pseudo.size(), "tactical+quiet != all in %s", pos.fen().c_str());
            for (Move m : tac) CHECK(m.is_tactical(), "non-tactical in tactical list");
            for (Move m : qui) CHECK(!m.is_tactical(), "tactical in quiet list");

            for (Move m : pseudo) CHECK(pos.pseudo_legal(m), "generated move %s not pseudo_legal in %s",
                                        Position::move_to_uci(m).c_str(), pos.fen().c_str());
            for (int i = 0; i < 64; ++i) {
                Move r(std::uint16_t(rng.rand() & 0xFFFF));
                bool in_list = pseudo.contains(r);
                CHECK(pos.pseudo_legal(r) == in_list, "pseudo_legal mismatch for raw %u (%s) in %s", r.raw(),
                      Position::move_to_uci(r).c_str(), pos.fen().c_str());
                ++pl_checks;
            }
            // gives_check must match the post-move state.
            for (Move m : legal) {
                bool gc = pos.gives_check(m);
                pos.make_move(m);
                CHECK(gc == pos.in_check(), "gives_check wrong for %s in %s", Position::move_to_uci(m).c_str(),
                      pos.fen().c_str());
                pos.unmake_move(m);
            }

            if (legal.empty()) break;
            Move m = legal[int(rng.rand() % legal.size())].move;
            std::string before = pos.fen();
            Key kb = pos.key();
            pos.make_move(m);
            ++moves_played;
            std::string why;
            CHECK(pos.is_consistent(&why), "inconsistent after %s from %s: %s", Position::move_to_uci(m).c_str(),
                  before.c_str(), why.c_str());
            // The key must equal that of a fresh position built from the FEN.
            CHECK(pos.key() == Position(pos.fen()).key(), "key differs from fresh FEN after %s",
                  Position::move_to_uci(m).c_str());
            // Null-move round trip
            if (!pos.in_check()) {
                Key k = pos.key();
                pos.make_null();
                CHECK(pos.is_consistent(&why), "inconsistent after null: %s", why.c_str());
                pos.unmake_null();
                CHECK(pos.key() == k, "null unmake key");
            }
            played.push_back(m);
            (void)kb;
        }
        // Unwind everything and verify restoration.
        Position fresh(QuickSuite[g % 7].fen);
        Position tmp(QuickSuite[g % 7].fen);
        for (Move m : played) tmp.make_move(m);
        for (auto it = played.rbegin(); it != played.rend(); ++it) tmp.unmake_move(*it);
        CHECK(tmp.fen() == fresh.fen() && tmp.key() == fresh.key(), "full unwind failed for game %d", g);
    }
    std::printf("fuzz: %llu moves played, %llu raw pseudo_legal checks\n", (unsigned long long)moves_played,
                (unsigned long long)pl_checks);
}

static void test_draws() {
    // Repetition: shuffle knights back and forth.
    Position pos;
    const char* seq[] = {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1", "f6g8"};
    int i = 0;
    for (const char* s : seq) {
        Move m = pos.parse_uci(s);
        CHECK(!m.is_none(), "parse %s", s);
        pos.make_move(m);
        ++i;
        if (i == 4) CHECK(pos.is_repetition(100), "twofold within search should count");
        if (i == 4) CHECK(!pos.is_repetition(0), "twofold before root should not count");
    }
    CHECK(pos.is_repetition(0), "threefold before root should count");

    Position f("4k3/8/8/8/8/8/8/4K2R w - - 99 80");
    CHECK(!f.is_fifty_move_draw(), "99 plies is not a draw");
    f.make_move(f.parse_uci("h1h2"));
    CHECK(f.is_fifty_move_draw(), "100 plies is a draw");

    CHECK(Position("4k3/8/8/8/8/8/8/4KB2 w - - 0 1").insufficient_material(), "KBK");
    CHECK(Position("4k3/8/8/8/8/8/8/4KN2 w - - 0 1").insufficient_material(), "KNK");
    CHECK(!Position("4k3/8/8/8/8/8/8/3NKN2 w - - 0 1").insufficient_material(), "KNNK is not auto-draw");
    CHECK(!Position("4k3/8/8/8/8/8/8/4K2R w - - 0 1").insufficient_material(), "KRK");

    // Mate and stalemate detection via move generation.
    Position mate("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    MoveList l;
    generate_legal(mate, l);
    CHECK(mate.in_check() && l.empty(), "fool's mate not detected");
    Position stale("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    MoveList l2;
    generate_legal(stale, l2);
    CHECK(!stale.in_check() && l2.empty(), "stalemate not detected");
}

static void test_see() {
    struct SeeCase {
        const char* fen;
        const char* move;
        int value;
    };
    // value = exact SEE result with P=100 N=B=300 R=500 Q=900
    const SeeCase cases[] = {
        {"1k1r4/1pp4p/p7/4p3/8/P5P1/1PP4P/2K1R3 w - - 0 1", "e1e5", 100},
        {"1k1r3q/1ppn3p/p4b2/4p3/8/P2N2P1/1PP1R1BP/2K1Q3 w - - 0 1", "d3e5", -200},
        {"4k3/8/8/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5", 100},
        {"4k3/8/2p5/3p4/4P3/8/8/4K3 w - - 0 1", "e4d5", 0},
        {"4k3/8/2n5/8/8/2Q5/8/4K3 w - - 0 1", "c3c6", 300},
        {"4k3/1p6/2n5/8/8/2Q5/8/4K3 w - - 0 1", "c3c6", -600},
    };
    for (const auto& c : cases) {
        Position pos(c.fen);
        Move m = pos.parse_uci(c.move);
        CHECK(!m.is_none(), "see parse %s", c.move);
        CHECK(pos.see_ge(m, c.value), "see_ge(%s, %d) should hold in %s", c.move, c.value, c.fen);
        CHECK(!pos.see_ge(m, c.value + 1), "see_ge(%s, %d) should fail in %s", c.move, c.value + 1, c.fen);
    }
}


// M8: incremental NNUE accumulator updates must equal a full refresh after every move
// (including castling, en passant and promotions), using a random network.
static void test_nnue_incremental(int games) {
    using namespace apex::nnue;
    const char* path = "test_nnue_random.bin";
    {
        PRNG rng(4242);
        std::FILE* f = std::fopen(path, "wb");
        CHECK(f != nullptr, "cannot create %s", path);
        if (!f) return;
        std::fwrite("APXN", 1, 4, f);
        std::uint32_t hdr[3] = {1, INPUTS, HIDDEN};
        std::fwrite(hdr, sizeof(hdr), 1, f);
        auto put = [&](int count, int range) {
            for (int i = 0; i < count; ++i) {
                std::int16_t v = std::int16_t(int(rng.rand() % std::uint64_t(2 * range + 1)) - range);
                std::fwrite(&v, sizeof(v), 1, f);
            }
        };
        put(INPUTS * HIDDEN, 60);
        put(HIDDEN, 100);
        put(2 * HIDDEN, 120);
        put(1, 500);
        std::fclose(f);
    }
    std::string err;
    CHECK(load(path, err), "load random net: %s", err.c_str());
    std::remove(path);
    if (!loaded()) return;

    PRNG rng(77);
    const char* starts[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2P5/8/8/8/8/5p2/K6k w - - 0 1",
        "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
    };
    Evaluator inc, fresh;
    for (int g = 0; g < games; ++g) {
        Position pos(starts[g % 4]);
        inc.reset(pos);
        for (int ply = 0; ply < 120; ++ply) {
            MoveList legal;
            generate_legal(pos, legal);
            if (legal.empty()) break;
            Move m = legal[int(rng.rand() % std::uint64_t(legal.size()))].move;
            if (ply < 60 && (rng.rand() & 7) == 0) {  // occasionally a null move
                if (!pos.in_check()) {
                    inc.push_null();
                    pos.make_null();
                    fresh.reset(pos);
                    CHECK(inc.evaluate(pos) == fresh.evaluate(pos), "nnue mismatch after null in %s",
                          pos.fen().c_str());
                    pos.unmake_null();
                    inc.pop();
                }
            }
            inc.push(pos, m);
            pos.make_move(m);
            fresh.reset(pos);
            CHECK(inc.evaluate(pos) == fresh.evaluate(pos), "nnue mismatch after %s: %s",
                  Position::move_to_uci(m).c_str(), pos.fen().c_str());
            if (failures) break;
        }
    }
    unload();
}

int main(int argc, char** argv) {
    init_core();
    bool deep = argc > 1 && std::strcmp(argv[1], "--deep") == 0;
    test_fen_roundtrip();
    test_draws();
    test_see();
    test_fuzz(deep ? 2000 : 200);
    test_perft(deep);
    test_nnue_incremental(deep ? 400 : 60);
    if (failures) {
        std::printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    std::printf("ALL TESTS PASSED\n");
    return 0;
}
