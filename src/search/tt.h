// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/types.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

namespace apex {

#if !(defined(_MSC_VER) && !defined(__clang__))
__extension__ typedef unsigned __int128 u128;
#endif

enum Bound : std::uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

struct TTEntry {
    std::uint64_t key;
    std::uint16_t move;
    std::int16_t score;
    std::int16_t eval;
    std::uint8_t depth;
    std::uint8_t gen_bound;  // bits 0-1 bound, bits 2-7 generation

    Bound bound() const { return Bound(gen_bound & 3); }
};
static_assert(sizeof(TTEntry) == 16);

struct TTData {
    Move move;
    Value score;
    Value eval;
    int depth;
    Bound bound;
};

// Single-entry-per-slot table (M3 baseline). Bucketing, lockless SMP safety and
// aging-based replacement are separate, individually tested improvements.
class TranspositionTable {
public:
    void resize(std::size_t mb);
    void clear();
    void new_search() { generation = (generation + 4) & 0xFC; }

    bool probe(Key key, TTData& out) const;
    void store(Key key, Move move, Value score, Value eval, int depth, Bound bound);
    void prefetch(Key key) const;
    int hashfull() const;  // permille

private:
    std::size_t index(Key key) const {
#if defined(_MSC_VER) && !defined(__clang__)
        return std::size_t(__umulh(key, std::uint64_t(entries.size())));
#else
        return std::size_t((static_cast<u128>(key) * entries.size()) >> 64);
#endif
    }
    std::vector<TTEntry> entries;
    std::uint8_t generation = 0;
};

// Mate scores are stored relative to the node, not the root.
inline Value value_to_tt(Value v, int ply) {
    if (v >= VALUE_MATE_IN_MAX_PLY) return v + ply;
    if (v <= VALUE_MATED_IN_MAX_PLY) return v - ply;
    return v;
}
inline Value value_from_tt(Value v, int ply) {
    if (v == VALUE_NONE) return v;
    if (v >= VALUE_MATE_IN_MAX_PLY) return v - ply;
    if (v <= VALUE_MATED_IN_MAX_PLY) return v + ply;
    return v;
}

}  // namespace apex
