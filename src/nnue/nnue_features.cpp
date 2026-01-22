#include "nnue/nnue_features.h"
#include "position.h"

namespace NNUE {

static inline void add_feature(NNUEState& st, const NnueWeights& w, Color perspective, int king_sq, Piece p, int sq, int sign) {
    int idx = feature_index(king_sq, p, sq);
    if (idx < 0) return;
    const i16* row = &w.w1[idx * w.hidden1];
    auto& acc = st.accum[perspective];
    for (int i = 0; i < w.hidden1; ++i) {
        acc[i] += sign * row[i];
    }
}

void accum_init(const Position& pos, NNUEState& st, const NnueWeights& w) {
    for (int c = 0; c < COLOR_NB; ++c) {
        for (int i = 0; i < w.hidden1; ++i) st.accum[c][i] = w.b1[i];
    }

    int wking = pos.king_square(WHITE);
    int bking = pos.king_square(BLACK);

    for (int sq = 0; sq < 64; ++sq) {
        Piece p = pos.piece_at(sq);
        if (p == NO_PIECE) continue;
        if (piece_type(p) == KING) continue;
        add_feature(st, w, WHITE, wking, p, sq, +1);
        add_feature(st, w, BLACK, bking, p, sq, +1);
    }
}

void accum_recompute_side(const Position& pos, NNUEState& st, const NnueWeights& w, Color perspective) {
    for (int i = 0; i < w.hidden1; ++i) st.accum[perspective][i] = w.b1[i];
    int king_sq = pos.king_square(perspective);
    for (int sq = 0; sq < 64; ++sq) {
        Piece p = pos.piece_at(sq);
        if (p == NO_PIECE || piece_type(p) == KING) continue;
        add_feature(st, w, perspective, king_sq, p, sq, +1);
    }
}

void accum_update_move(const Position& pos_before, NNUEState& st, const NnueWeights& w,
                       Piece moved, int from, int to, Piece captured, int cap_sq, bool king_moved, Color side_moved) {
    int wking = pos_before.king_square(WHITE);
    int bking = pos_before.king_square(BLACK);

    if (king_moved) {
        // For the side whose king moved, recompute later using updated position.
        // Here we only update the other side's accumulator for moved/captured pieces (kings excluded anyway).
        Color other = ~side_moved;
        int o_king = (other == WHITE) ? wking : bking;
        if (piece_type(moved) != KING) {
            add_feature(st, w, other, o_king, moved, from, -1);
            add_feature(st, w, other, o_king, moved, to, +1);
        }
        if (captured != NO_PIECE && piece_type(captured) != KING) {
            add_feature(st, w, other, o_king, captured, cap_sq, -1);
        }
        return;
    }

    // Update both sides with old king squares
    if (piece_type(moved) != KING) {
        add_feature(st, w, WHITE, wking, moved, from, -1);
        add_feature(st, w, WHITE, wking, moved, to, +1);
        add_feature(st, w, BLACK, bking, moved, from, -1);
        add_feature(st, w, BLACK, bking, moved, to, +1);
    }
    if (captured != NO_PIECE && piece_type(captured) != KING) {
        add_feature(st, w, WHITE, wking, captured, cap_sq, -1);
        add_feature(st, w, BLACK, bking, captured, cap_sq, -1);
    }
}

void accum_add_piece_both(const Position& pos_before, NNUEState& st, const NnueWeights& w, Piece p, int sq) {
    if (p == NO_PIECE || piece_type(p) == KING) return;
    int wking = pos_before.king_square(WHITE);
    int bking = pos_before.king_square(BLACK);
    add_feature(st, w, WHITE, wking, p, sq, +1);
    add_feature(st, w, BLACK, bking, p, sq, +1);
}

void accum_remove_piece_both(const Position& pos_before, NNUEState& st, const NnueWeights& w, Piece p, int sq) {
    if (p == NO_PIECE || piece_type(p) == KING) return;
    int wking = pos_before.king_square(WHITE);
    int bking = pos_before.king_square(BLACK);
    add_feature(st, w, WHITE, wking, p, sq, -1);
    add_feature(st, w, BLACK, bking, p, sq, -1);
}

} // namespace NNUE
