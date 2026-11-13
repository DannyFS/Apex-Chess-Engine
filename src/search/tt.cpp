// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tt.h"

#include <algorithm>
#include <cstring>

namespace apex {

void TranspositionTable::resize(std::size_t mb) {
    std::size_t n = std::max<std::size_t>(1, mb) * 1024 * 1024 / sizeof(TTEntry);
    entries.assign(n, TTEntry{});
    generation = 0;
}

void TranspositionTable::clear() {
    std::fill(entries.begin(), entries.end(), TTEntry{});
    generation = 0;
}

bool TranspositionTable::probe(Key key, TTData& out) const {
    const TTEntry& e = entries[index(key)];
    if (e.key != key || e.bound() == BOUND_NONE) return false;
    out.move = Move(e.move);
    out.score = e.score;
    out.eval = e.eval;
    out.depth = e.depth;
    out.bound = e.bound();
    return true;
}

void TranspositionTable::store(Key key, Move move, Value score, Value eval, int depth, Bound bound) {
    TTEntry& e = entries[index(key)];
    bool same = e.key == key;
    // Keep the old move if we have none for the same position.
    if (move || !same) e.move = move.raw();
    // Replace: different position, exact bound, stale generation, or comparable depth.
    if (!same || bound == BOUND_EXACT || (e.gen_bound & 0xFC) != generation || depth + 3 >= e.depth) {
        e.key = key;
        e.score = std::int16_t(score);
        e.eval = std::int16_t(eval);
        e.depth = std::uint8_t(std::clamp(depth, 0, 255));
        e.gen_bound = std::uint8_t(generation | bound);
    }
}

void TranspositionTable::prefetch(Key key) const {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(&entries[index(key)]);
#else
    (void)key;
#endif
}

int TranspositionTable::hashfull() const {
    int used = 0;
    std::size_t n = std::min<std::size_t>(1000, entries.size());
    for (std::size_t i = 0; i < n; ++i)
        if (entries[i].bound() != BOUND_NONE && (entries[i].gen_bound & 0xFC) == generation) ++used;
    return int(used * 1000 / std::max<std::size_t>(n, 1));
}

}  // namespace apex
