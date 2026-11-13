// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "types.h"

namespace apex {

class Position;

struct ScoredMove {
    Move move;
    int score;
};

class MoveList {
public:
    void add(Move m) { moves[n++] = {m, 0}; }
    int size() const { return n; }
    bool empty() const { return n == 0; }
    void clear() { n = 0; }
    ScoredMove& operator[](int i) { return moves[i]; }
    const ScoredMove& operator[](int i) const { return moves[i]; }
    void remove(int i) { moves[i] = moves[--n]; }
    bool contains(Move m) const {
        for (int i = 0; i < n; ++i)
            if (moves[i].move == m) return true;
        return false;
    }

    struct Iter {
        const ScoredMove* p;
        Move operator*() const { return p->move; }
        Iter& operator++() { ++p; return *this; }
        bool operator!=(const Iter& o) const { return p != o.p; }
    };
    Iter begin() const { return {moves}; }
    Iter end() const { return {moves + n}; }

private:
    ScoredMove moves[256];
    int n = 0;
};

enum GenType { GEN_TACTICAL, GEN_QUIET, GEN_ALL };

// Pseudo-legal generation. GEN_TACTICAL = captures + all promotions; GEN_QUIET = everything else.
template <GenType T>
void generate(const Position& pos, MoveList& list);

// Fully legal moves.
void generate_legal(const Position& pos, MoveList& list);

std::uint64_t perft(Position& pos, int depth);

}  // namespace apex
