#include "util/zobrist.h"
#include <random>

namespace Zobrist {
    std::array<std::array<u64, 64>, 12> piece{};
    std::array<u64, 16> castling{};
    std::array<u64, 8> ep_file{};
    u64 side = 0;

    void init() {
        std::mt19937_64 rng(20240101ULL);
        auto rand64 = [&]() { return rng(); };

        for (int p = 0; p < 12; ++p)
            for (int sq = 0; sq < 64; ++sq)
                piece[p][sq] = rand64();
        for (int i = 0; i < 16; ++i) castling[i] = rand64();
        for (int i = 0; i < 8; ++i) ep_file[i] = rand64();
        side = rand64();
    }
}
