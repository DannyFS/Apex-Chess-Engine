// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#include "movegen.h"

#include "position.h"

namespace apex {

namespace {

inline void add_promotions(MoveList& list, Square from, Square to, bool capture, bool tactical, bool quiet) {
    // All promotions are classed as tactical.
    (void)quiet;
    if (!tactical) return;
    int base = capture ? MF_PROMO_CAP_N : MF_PROMO_N;
    list.add(Move(from, to, base + 3));  // queen first
    list.add(Move(from, to, base + 0));
    list.add(Move(from, to, base + 2));
    list.add(Move(from, to, base + 1));
}

template <Color Us, GenType T>
void gen_pawns(const Position& pos, MoveList& list) {
    constexpr Color Them = ~Us;
    constexpr int Up = Us == WHITE ? 8 : -8;
    constexpr Bitboard Rank7 = Us == WHITE ? rank_bb(6) : rank_bb(1);
    constexpr Bitboard Rank3 = Us == WHITE ? rank_bb(2) : rank_bb(5);
    constexpr bool Tactical = T != GEN_QUIET;
    constexpr bool Quiet = T != GEN_TACTICAL;

    Bitboard empty = ~pos.pieces();
    Bitboard enemies = pos.pieces(Them);
    Bitboard pawns = pos.pieces(Us, PAWN);
    Bitboard promo_pawns = pawns & Rank7;
    Bitboard other = pawns & ~Rank7;

    if constexpr (Quiet) {
        Bitboard b1 = shift<Up>(other) & empty;
        Bitboard b2 = shift<Up>(b1 & Rank3) & empty;
        while (b1) {
            Square to = pop_lsb(b1);
            list.add(Move(Square(to - Up), to, MF_QUIET));
        }
        while (b2) {
            Square to = pop_lsb(b2);
            list.add(Move(Square(to - 2 * Up), to, MF_DOUBLE_PUSH));
        }
    }

    if constexpr (Tactical) {
        constexpr int UpLeft = Us == WHITE ? 7 : -9;
        constexpr int UpRight = Us == WHITE ? 9 : -7;
        Bitboard l = shift<UpLeft>(other) & enemies;
        Bitboard r = shift<UpRight>(other) & enemies;
        while (l) {
            Square to = pop_lsb(l);
            list.add(Move(Square(to - UpLeft), to, MF_CAPTURE));
        }
        while (r) {
            Square to = pop_lsb(r);
            list.add(Move(Square(to - UpRight), to, MF_CAPTURE));
        }

        if (promo_pawns) {
            Bitboard pu = shift<Up>(promo_pawns) & empty;
            Bitboard pl = shift<UpLeft>(promo_pawns) & enemies;
            Bitboard pr = shift<UpRight>(promo_pawns) & enemies;
            while (pu) {
                Square to = pop_lsb(pu);
                add_promotions(list, Square(to - Up), to, false, true, true);
            }
            while (pl) {
                Square to = pop_lsb(pl);
                add_promotions(list, Square(to - UpLeft), to, true, true, true);
            }
            while (pr) {
                Square to = pop_lsb(pr);
                add_promotions(list, Square(to - UpRight), to, true, true, true);
            }
        }

        Square ep = pos.ep_square();
        if (ep != SQ_NONE) {
            Bitboard att = pawn_attacks(Them, ep) & other;
            while (att) list.add(Move(pop_lsb(att), ep, MF_EP));
        }
    }
}

template <Color Us, GenType T>
void gen_all(const Position& pos, MoveList& list) {
    gen_pawns<Us, T>(pos, list);

    Bitboard occ = pos.pieces();
    Bitboard enemies = pos.pieces(~Us);
    Bitboard empty = ~occ;

    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN, KING}) {
        Bitboard pcs = pos.pieces(Us, pt);
        while (pcs) {
            Square from = pop_lsb(pcs);
            Bitboard att = attacks_bb(pt, from, occ);
            if constexpr (T != GEN_QUIET) {
                Bitboard caps = att & enemies;
                while (caps) list.add(Move(from, pop_lsb(caps), MF_CAPTURE));
            }
            if constexpr (T != GEN_TACTICAL) {
                Bitboard quiets = att & empty;
                while (quiets) list.add(Move(from, pop_lsb(quiets), MF_QUIET));
            }
        }
    }

    if constexpr (T != GEN_TACTICAL) {
        if (!pos.in_check()) {
            constexpr int OO = Us == WHITE ? WHITE_OO : BLACK_OO;
            constexpr int OOO = Us == WHITE ? WHITE_OOO : BLACK_OOO;
            constexpr Square E1 = relative_square(Us, SQ_E1);
            if (pos.can_castle(OO) &&
                !(occ & (square_bb(relative_square(Us, SQ_F1)) | square_bb(relative_square(Us, SQ_G1)))))
                list.add(Move(E1, relative_square(Us, SQ_G1), MF_KING_CASTLE));
            if (pos.can_castle(OOO) &&
                !(occ & (square_bb(relative_square(Us, SQ_D1)) | square_bb(relative_square(Us, SQ_C1)) |
                         square_bb(relative_square(Us, SQ_B1)))))
                list.add(Move(E1, relative_square(Us, SQ_C1), MF_QUEEN_CASTLE));
        }
    }
}

}  // namespace

template <GenType T>
void generate(const Position& pos, MoveList& list) {
    if (pos.side_to_move() == WHITE) gen_all<WHITE, T>(pos, list);
    else gen_all<BLACK, T>(pos, list);
}

template void generate<GEN_TACTICAL>(const Position&, MoveList&);
template void generate<GEN_QUIET>(const Position&, MoveList&);
template void generate<GEN_ALL>(const Position&, MoveList&);

void generate_legal(const Position& pos, MoveList& list) {
    MoveList pseudo;
    generate<GEN_ALL>(pos, pseudo);
    for (Move m : pseudo)
        if (pos.legal(m)) list.add(m);
}

std::uint64_t perft(Position& pos, int depth) {
    MoveList list;
    generate_legal(pos, list);
    if (depth <= 1) return depth == 1 ? std::uint64_t(list.size()) : 1;
    std::uint64_t nodes = 0;
    for (Move m : list) {
        pos.make_move(m);
        nodes += perft(pos, depth - 1);
        pos.unmake_move(m);
    }
    return nodes;
}

}  // namespace apex
