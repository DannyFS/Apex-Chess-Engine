// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace apex {

// xorshift64* (Vigna). Deterministic; used for Zobrist keys and magic search.
class PRNG {
public:
    explicit PRNG(std::uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
    std::uint64_t rand() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 2685821657736338717ULL;
    }
    std::uint64_t sparse_rand() { return rand() & rand() & rand(); }

private:
    std::uint64_t s;
};

}  // namespace apex
