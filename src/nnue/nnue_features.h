#pragma once

#include "util/types.h"
#include "nnue/nnue_loader.h"
#include <array>

class Position;

namespace NNUE {

struct NNUEState {
    std::array<std::array<i32, NNUE_HIDDEN1>, COLOR_NB> accum{};
};

inline int piece_index(Piece p) {
    switch (p) {
        case W_PAWN: return 0;
        case W_KNIGHT: return 1;
        case W_BISHOP: return 2;
        case W_ROOK: return 3;
        case W_QUEEN: return 4;
        case B_PAWN: return 5;
        case B_KNIGHT: return 6;
        case B_BISHOP: return 7;
        case B_ROOK: return 8;
        case B_QUEEN: return 9;
        default: return -1;
    }
}

inline int feature_index(int king_sq, Piece p, int sq) {
    int idx = piece_index(p);
    if (idx < 0) return -1;
    return king_sq * (NNUE_PIECE_TYPES * 64) + idx * 64 + sq;
}

void accum_init(const Position& pos, NNUEState& st, const NnueWeights& w);
void accum_recompute_side(const Position& pos, NNUEState& st, const NnueWeights& w, Color perspective);
void accum_update_move(const Position& pos_before, NNUEState& st, const NnueWeights& w,
                       Piece moved, int from, int to, Piece captured, int cap_sq, bool king_moved, Color side_moved);
void accum_add_piece_both(const Position& pos_before, NNUEState& st, const NnueWeights& w, Piece p, int sq);
void accum_remove_piece_both(const Position& pos_before, NNUEState& st, const NnueWeights& w, Piece p, int sq);

} // namespace NNUE
