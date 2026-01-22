#include "movegen.h"

static inline void add_move(MoveList& list, int from, int to, u32 flags = MF_NONE, PieceType promo = PIECE_TYPE_NB) {
    list.push(make_move(from, to, promo, flags));
}

void generate_pseudo_moves(const Position& pos, MoveList& list, bool captures_only) {
    Color us = pos.side_to_move();
    Color them = ~us;
    Bitboard occ_us = pos.occupancy(us);
    Bitboard occ_them = pos.occupancy(them);
    Bitboard occ_all = pos.occupancy_all();

    // Pawns
    Bitboard pawns = pos.pieces_bb(us == WHITE ? W_PAWN : B_PAWN);
    while (pawns) {
        int sq = pop_lsb(pawns);
        int r = rank_of(sq);
        int f = file_of(sq);

        if (us == WHITE) {
            int one = sq + 8;
            if (!captures_only && one < 64 && !(occ_all & (1ULL << one))) {
                if (r == 6) {
                    add_move(list, sq, one, MF_PROMOTION, QUEEN);
                    add_move(list, sq, one, MF_PROMOTION, ROOK);
                    add_move(list, sq, one, MF_PROMOTION, BISHOP);
                    add_move(list, sq, one, MF_PROMOTION, KNIGHT);
                } else {
                    add_move(list, sq, one, MF_NONE);
                    if (r == 1) {
                        int two = sq + 16;
                        if (!(occ_all & (1ULL << two))) add_move(list, sq, two, MF_DOUBLE_PUSH);
                    }
                }
            }
            // captures
            if (f > 0) {
                int cap = sq + 7;
                if (cap < 64 && (occ_them & (1ULL << cap))) {
                    if (r == 6) {
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, QUEEN);
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, ROOK);
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, BISHOP);
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, KNIGHT);
                    } else {
                        add_move(list, sq, cap, MF_CAPTURE);
                    }
                }
            }
            if (f < 7) {
                int cap = sq + 9;
                if (cap < 64 && (occ_them & (1ULL << cap))) {
                    if (r == 6) {
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, QUEEN);
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, ROOK);
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, BISHOP);
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, KNIGHT);
                    } else {
                        add_move(list, sq, cap, MF_CAPTURE);
                    }
                }
            }
            // en passant
            if (pos.ep_square() != -1) {
                int ep = pos.ep_square();
                if (ep == sq + 7 && f > 0) add_move(list, sq, ep, MF_ENPASSANT | MF_CAPTURE);
                if (ep == sq + 9 && f < 7) add_move(list, sq, ep, MF_ENPASSANT | MF_CAPTURE);
            }
        } else { // BLACK
            int one = sq - 8;
            if (!captures_only && one >= 0 && !(occ_all & (1ULL << one))) {
                if (r == 1) {
                    add_move(list, sq, one, MF_PROMOTION, QUEEN);
                    add_move(list, sq, one, MF_PROMOTION, ROOK);
                    add_move(list, sq, one, MF_PROMOTION, BISHOP);
                    add_move(list, sq, one, MF_PROMOTION, KNIGHT);
                } else {
                    add_move(list, sq, one, MF_NONE);
                    if (r == 6) {
                        int two = sq - 16;
                        if (!(occ_all & (1ULL << two))) add_move(list, sq, two, MF_DOUBLE_PUSH);
                    }
                }
            }
            if (f > 0) {
                int cap = sq - 9;
                if (cap >= 0 && (occ_them & (1ULL << cap))) {
                    if (r == 1) {
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, QUEEN);
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, ROOK);
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, BISHOP);
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, KNIGHT);
                    } else {
                        add_move(list, sq, cap, MF_CAPTURE);
                    }
                }
            }
            if (f < 7) {
                int cap = sq - 7;
                if (cap >= 0 && (occ_them & (1ULL << cap))) {
                    if (r == 1) {
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, QUEEN);
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, ROOK);
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, BISHOP);
                        add_move(list, sq, cap, MF_CAPTURE | MF_PROMOTION, KNIGHT);
                    } else {
                        add_move(list, sq, cap, MF_CAPTURE);
                    }
                }
            }
            if (pos.ep_square() != -1) {
                int ep = pos.ep_square();
                if (ep == sq - 9 && f > 0) add_move(list, sq, ep, MF_ENPASSANT | MF_CAPTURE);
                if (ep == sq - 7 && f < 7) add_move(list, sq, ep, MF_ENPASSANT | MF_CAPTURE);
            }
        }
    }

    // Knights
    Bitboard knights = pos.pieces_bb(us == WHITE ? W_KNIGHT : B_KNIGHT);
    while (knights) {
        int sq = pop_lsb(knights);
        Bitboard attacks = Bitboards::knight_attacks[sq] & ~occ_us;
        while (attacks) {
            int to = pop_lsb(attacks);
            if (occ_them & (1ULL << to)) add_move(list, sq, to, MF_CAPTURE);
            else if (!captures_only) add_move(list, sq, to, MF_NONE);
        }
    }

    // Bishops
    Bitboard bishops = pos.pieces_bb(us == WHITE ? W_BISHOP : B_BISHOP);
    while (bishops) {
        int sq = pop_lsb(bishops);
        Bitboard attacks = Bitboards::bishop_attacks(sq, occ_all) & ~occ_us;
        while (attacks) {
            int to = pop_lsb(attacks);
            if (occ_them & (1ULL << to)) add_move(list, sq, to, MF_CAPTURE);
            else if (!captures_only) add_move(list, sq, to, MF_NONE);
        }
    }

    // Rooks
    Bitboard rooks = pos.pieces_bb(us == WHITE ? W_ROOK : B_ROOK);
    while (rooks) {
        int sq = pop_lsb(rooks);
        Bitboard attacks = Bitboards::rook_attacks(sq, occ_all) & ~occ_us;
        while (attacks) {
            int to = pop_lsb(attacks);
            if (occ_them & (1ULL << to)) add_move(list, sq, to, MF_CAPTURE);
            else if (!captures_only) add_move(list, sq, to, MF_NONE);
        }
    }

    // Queens
    Bitboard queens = pos.pieces_bb(us == WHITE ? W_QUEEN : B_QUEEN);
    while (queens) {
        int sq = pop_lsb(queens);
        Bitboard attacks = Bitboards::queen_attacks(sq, occ_all) & ~occ_us;
        while (attacks) {
            int to = pop_lsb(attacks);
            if (occ_them & (1ULL << to)) add_move(list, sq, to, MF_CAPTURE);
            else if (!captures_only) add_move(list, sq, to, MF_NONE);
        }
    }

    // King
    Bitboard king = pos.pieces_bb(us == WHITE ? W_KING : B_KING);
    if (king) {
        int sq = std::countr_zero(king);
        Bitboard attacks = Bitboards::king_attacks[sq] & ~occ_us;
        while (attacks) {
            int to = pop_lsb(attacks);
            if (occ_them & (1ULL << to)) add_move(list, sq, to, MF_CAPTURE);
            else if (!captures_only) add_move(list, sq, to, MF_NONE);
        }
    }

    // Castling
    if (!captures_only) {
        if (us == WHITE) {
            if ((pos.castling_rights() & 1) && !(occ_all & ((1ULL << 5) | (1ULL << 6))) &&
                !pos.in_check(WHITE) && !pos.is_attacked(5, BLACK) && !pos.is_attacked(6, BLACK)) {
                add_move(list, 4, 6, MF_CASTLING);
            }
            if ((pos.castling_rights() & 2) && !(occ_all & ((1ULL << 1) | (1ULL << 2) | (1ULL << 3))) &&
                !pos.in_check(WHITE) && !pos.is_attacked(2, BLACK) && !pos.is_attacked(3, BLACK)) {
                add_move(list, 4, 2, MF_CASTLING);
            }
        } else {
            if ((pos.castling_rights() & 4) && !(occ_all & ((1ULL << 61) | (1ULL << 62))) &&
                !pos.in_check(BLACK) && !pos.is_attacked(61, WHITE) && !pos.is_attacked(62, WHITE)) {
                add_move(list, 60, 62, MF_CASTLING);
            }
            if ((pos.castling_rights() & 8) && !(occ_all & ((1ULL << 57) | (1ULL << 58) | (1ULL << 59))) &&
                !pos.in_check(BLACK) && !pos.is_attacked(58, WHITE) && !pos.is_attacked(59, WHITE)) {
                add_move(list, 60, 58, MF_CASTLING);
            }
        }
    }
}

void generate_legal_moves(Position& pos, MoveList& list, bool captures_only) {
    MoveList pseudo;
    generate_pseudo_moves(pos, pseudo, captures_only);
    for (int i = 0; i < pseudo.size; ++i) {
        Move m = pseudo.moves[i];
        pos.do_move(m);
        if (!pos.in_check(~pos.side_to_move())) {
            list.push(m);
        }
        pos.undo_move();
    }
}
