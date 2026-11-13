// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#include "bitboard.h"

#include <cstdlib>
#include <cstring>

#include "prng.h"

namespace apex {

std::string square_to_string(Square s) {
    if (s == SQ_NONE) return "-";
    return std::string{char('a' + file_of(s)), char('1' + rank_of(s))};
}

namespace bb {

Bitboard PawnAttacks[COLOR_NB][SQUARE_NB];
Bitboard KnightAttacks[SQUARE_NB];
Bitboard KingAttacks[SQUARE_NB];
Bitboard BetweenBB[SQUARE_NB][SQUARE_NB];
Bitboard LineBB[SQUARE_NB][SQUARE_NB];
Magic RookMagics[SQUARE_NB];
Magic BishopMagics[SQUARE_NB];

namespace {

Bitboard RookTable[0x19000];   // 102400 entries
Bitboard BishopTable[0x1480];  // 5248 entries

// Slow reference ray-walker used for table generation and tests.
Bitboard sliding_attack(const int deltas[4][2], Square sq, Bitboard occ) {
    Bitboard attacks = 0;
    for (int d = 0; d < 4; ++d) {
        int f = file_of(sq), r = rank_of(sq);
        while (true) {
            f += deltas[d][0];
            r += deltas[d][1];
            if (f < 0 || f > 7 || r < 0 || r > 7) break;
            Square s = make_square(f, r);
            attacks |= square_bb(s);
            if (occ & square_bb(s)) break;
        }
    }
    return attacks;
}

constexpr int RookDeltas[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
constexpr int BishopDeltas[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};

void init_magics(const int deltas[4][2], Magic magics[], Bitboard table[]) {
    // Fixed seeds per rank (deterministic, fast convergence).
    constexpr std::uint64_t Seeds[8] = {728, 10316, 55013, 32803, 12281, 15100, 16645, 255};
    Bitboard occupancy[4096], reference[4096];
    int epoch[4096] = {}, cnt = 0;
    Bitboard* next = table;

    for (int s = 0; s < 64; ++s) {
        Square sq = Square(s);
        Bitboard edges = ((RANK_1_BB | RANK_8_BB) & ~rank_bb(rank_of(sq))) |
                         ((FILE_A_BB | FILE_H_BB) & ~file_bb(file_of(sq)));
        Magic& m = magics[s];
        m.mask = sliding_attack(deltas, sq, 0) & ~edges;
        m.shift = 64 - popcount(m.mask);
        m.attacks = next;

        // Carry-Rippler enumeration of all subsets of the mask.
        int size = 0;
        Bitboard b = 0;
        do {
            occupancy[size] = b;
            reference[size] = sliding_attack(deltas, sq, b);
            ++size;
            b = (b - m.mask) & m.mask;
        } while (b);
        next += size;

        PRNG rng(Seeds[rank_of(sq)]);
        for (int i = 0; i < size;) {
            for (m.magic = 0; popcount((m.magic * m.mask) >> 56) < 6;) m.magic = rng.sparse_rand();
            ++cnt;
            for (i = 0; i < size; ++i) {
                unsigned idx = m.index(occupancy[i]);
                if (epoch[idx] < cnt) {
                    epoch[idx] = cnt;
                    m.attacks[idx] = reference[i];
                } else if (m.attacks[idx] != reference[i]) {
                    break;
                }
            }
        }
    }
}

}  // namespace

void init() {
    for (int s = 0; s < 64; ++s) {
        Square sq = Square(s);
        Bitboard b = square_bb(sq);
        PawnAttacks[WHITE][s] = pawn_attacks_bb<WHITE>(b);
        PawnAttacks[BLACK][s] = pawn_attacks_bb<BLACK>(b);

        KnightAttacks[s] = 0;
        KingAttacks[s] = 0;
        constexpr int kn[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
        constexpr int kg[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
        for (int i = 0; i < 8; ++i) {
            int f = file_of(sq) + kn[i][0], r = rank_of(sq) + kn[i][1];
            if (f >= 0 && f < 8 && r >= 0 && r < 8) KnightAttacks[s] |= square_bb(make_square(f, r));
            f = file_of(sq) + kg[i][0];
            r = rank_of(sq) + kg[i][1];
            if (f >= 0 && f < 8 && r >= 0 && r < 8) KingAttacks[s] |= square_bb(make_square(f, r));
        }
    }

    init_magics(RookDeltas, RookMagics, RookTable);
    init_magics(BishopDeltas, BishopMagics, BishopTable);

    for (int a = 0; a < 64; ++a) {
        for (int b = 0; b < 64; ++b) {
            Square sa = Square(a), sb = Square(b);
            LineBB[a][b] = 0;
            BetweenBB[a][b] = square_bb(sb);
            if (a == b) continue;
            if (bishop_attacks(sa, 0) & square_bb(sb)) {
                LineBB[a][b] = (bishop_attacks(sa, 0) & bishop_attacks(sb, 0)) | square_bb(sa) | square_bb(sb);
                BetweenBB[a][b] |= bishop_attacks(sa, square_bb(sb)) & bishop_attacks(sb, square_bb(sa));
            } else if (rook_attacks(sa, 0) & square_bb(sb)) {
                LineBB[a][b] = (rook_attacks(sa, 0) & rook_attacks(sb, 0)) | square_bb(sa) | square_bb(sb);
                BetweenBB[a][b] |= rook_attacks(sa, square_bb(sb)) & rook_attacks(sb, square_bb(sa));
            }
        }
    }
}

}  // namespace bb

std::string pretty(Bitboard b) {
    std::string s = "+---+---+---+---+---+---+---+---+\n";
    for (int r = 7; r >= 0; --r) {
        for (int f = 0; f < 8; ++f) s += (b & square_bb(make_square(f, r))) ? "| X " : "|   ";
        s += "| " + std::to_string(r + 1) + "\n+---+---+---+---+---+---+---+---+\n";
    }
    s += "  a   b   c   d   e   f   g   h\n";
    return s;
}

}  // namespace apex
