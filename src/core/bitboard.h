// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <bit>
#include <string>

#include "types.h"

namespace apex {

constexpr Bitboard FILE_A_BB = 0x0101010101010101ULL;
constexpr Bitboard FILE_H_BB = FILE_A_BB << 7;
constexpr Bitboard RANK_1_BB = 0xFFULL;
constexpr Bitboard RANK_8_BB = RANK_1_BB << 56;

constexpr Bitboard square_bb(Square s) { return 1ULL << s; }
constexpr Bitboard file_bb(int f) { return FILE_A_BB << f; }
constexpr Bitboard rank_bb(int r) { return RANK_1_BB << (8 * r); }

inline int popcount(Bitboard b) { return std::popcount(b); }
inline Square lsb(Bitboard b) { return Square(std::countr_zero(b)); }
inline Square msb(Bitboard b) { return Square(63 - std::countl_zero(b)); }
inline Square pop_lsb(Bitboard& b) {
    Square s = lsb(b);
    b &= b - 1;
    return s;
}
constexpr bool more_than_one(Bitboard b) { return b & (b - 1); }

template <int D>
constexpr Bitboard shift(Bitboard b) {
    if constexpr (D == 8) return b << 8;
    else if constexpr (D == -8) return b >> 8;
    else if constexpr (D == 16) return b << 16;
    else if constexpr (D == -16) return b >> 16;
    else if constexpr (D == 1) return (b & ~FILE_H_BB) << 1;
    else if constexpr (D == -1) return (b & ~FILE_A_BB) >> 1;
    else if constexpr (D == 9) return (b & ~FILE_H_BB) << 9;
    else if constexpr (D == 7) return (b & ~FILE_A_BB) << 7;
    else if constexpr (D == -7) return (b & ~FILE_H_BB) >> 7;
    else if constexpr (D == -9) return (b & ~FILE_A_BB) >> 9;
    else return 0;
}

template <Color C>
constexpr Bitboard pawn_attacks_bb(Bitboard b) {
    return C == WHITE ? shift<7>(b) | shift<9>(b) : shift<-7>(b) | shift<-9>(b);
}

namespace bb {

struct Magic {
    Bitboard mask;
    Bitboard magic;
    Bitboard* attacks;
    unsigned shift;
    unsigned index(Bitboard occ) const { return unsigned(((occ & mask) * magic) >> shift); }
};

extern Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
extern Bitboard KnightAttacks[SQUARE_NB];
extern Bitboard KingAttacks[SQUARE_NB];
extern Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];  // squares strictly between, plus the target square
extern Bitboard LineBB[SQUARE_NB][SQUARE_NB];     // full line through both squares (0 if not aligned)
extern Magic RookMagics[SQUARE_NB];
extern Magic BishopMagics[SQUARE_NB];

void init();

}  // namespace bb

inline Bitboard pawn_attacks(Color c, Square s) { return bb::PawnAttacks[c][s]; }
inline Bitboard knight_attacks(Square s) { return bb::KnightAttacks[s]; }
inline Bitboard king_attacks(Square s) { return bb::KingAttacks[s]; }
inline Bitboard bishop_attacks(Square s, Bitboard occ) {
    const bb::Magic& m = bb::BishopMagics[s];
    return m.attacks[m.index(occ)];
}
inline Bitboard rook_attacks(Square s, Bitboard occ) {
    const bb::Magic& m = bb::RookMagics[s];
    return m.attacks[m.index(occ)];
}
inline Bitboard queen_attacks(Square s, Bitboard occ) { return bishop_attacks(s, occ) | rook_attacks(s, occ); }

inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occ) {
    switch (pt) {
    case KNIGHT: return knight_attacks(s);
    case BISHOP: return bishop_attacks(s, occ);
    case ROOK: return rook_attacks(s, occ);
    case QUEEN: return queen_attacks(s, occ);
    case KING: return king_attacks(s);
    default: return 0;
    }
}

inline Bitboard between_bb(Square a, Square b) { return bb::BetweenBB[a][b]; }
inline Bitboard line_bb(Square a, Square b) { return bb::LineBB[a][b]; }
inline bool aligned(Square a, Square b, Square c) { return line_bb(a, b) & square_bb(c); }

std::string pretty(Bitboard b);

}  // namespace apex
