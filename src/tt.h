#pragma once

#include "util/types.h"
#include <vector>

struct TTEntry {
    u64 key = 0;
    Move move = MOVE_NONE;
    i16 score = 0;
    i16 eval = 0;
    i16 depth = 0;
    u8 bound = BOUND_NONE;
    u8 gen = 0;
};

class TranspositionTable {
public:
    void resize_mb(int mb);
    void clear();

    TTEntry* probe(u64 key);
    void store(u64 key, Move move, int depth, int score, int eval, Bound bound);

    void new_generation();
    u64 hashfull() const;

private:
    struct Cluster { TTEntry entry[4]; };
    std::vector<Cluster> table;
    u8 generation = 1;
};

inline int score_to_tt(int score, int ply) {
    if (score > MATE_IN_MAX_PLY) return score + ply;
    if (score < -MATE_IN_MAX_PLY) return score - ply;
    return score;
}

inline int score_from_tt(int score, int ply) {
    if (score > MATE_IN_MAX_PLY) return score - ply;
    if (score < -MATE_IN_MAX_PLY) return score + ply;
    return score;
}
