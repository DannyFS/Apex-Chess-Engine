// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#include "eval.h"

#include <algorithm>
#include <cstdio>

namespace apex {

namespace {

// Untuned, hand-written starting values. Their only job is to make the baseline
// play sensible chess; Texel tuning (M6) and NNUE (M8) supersede them.
constexpr int MgValue[PIECE_TYPE_NB] = {90, 320, 335, 480, 980, 0};
constexpr int EgValue[PIECE_TYPE_NB] = {110, 300, 320, 530, 960, 0};
constexpr int PhaseWeight[PIECE_TYPE_NB] = {0, 1, 1, 2, 4, 0};
constexpr int MaxPhase = 24;
constexpr int Tempo = 15;

// Piece-square tables from White's point of view, a1 = index 0 (rank 1 first).
// clang-format off
constexpr int PawnMg[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
     -5,   0,   0, -10, -10,   0,   0,  -5,
     -5,  -2,   5,   5,   5,   0,  -2,  -5,
     -5,   0,  10,  20,  20,   5,   0,  -5,
      0,   5,  12,  22,  22,  10,   5,   0,
     10,  15,  20,  30,  30,  20,  15,  10,
     40,  40,  40,  40,  40,  40,  40,  40,
      0,   0,   0,   0,   0,   0,   0,   0,
};
constexpr int PawnEg[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,
      5,   5,   5,   5,   5,   5,   5,   5,
     15,  15,  12,  10,  10,  12,  15,  15,
     30,  30,  25,  20,  20,  25,  30,  30,
     55,  55,  50,  45,  45,  50,  55,  55,
     90,  90,  85,  80,  80,  85,  90,  90,
      0,   0,   0,   0,   0,   0,   0,   0,
};
constexpr int KnightPst[64] = {
    -50, -35, -25, -20, -20, -25, -35, -50,
    -35, -20,   0,   5,   5,   0, -20, -35,
    -25,   5,  10,  15,  15,  10,   5, -25,
    -20,   5,  15,  20,  20,  15,   5, -20,
    -20,  10,  18,  25,  25,  18,  10, -20,
    -25,  10,  20,  25,  25,  20,  10, -25,
    -35, -20,   5,  10,  10,   5, -20, -35,
    -50, -35, -25, -20, -20, -25, -35, -50,
};
constexpr int BishopPst[64] = {
    -20, -10, -12, -10, -10, -12, -10, -20,
    -10,  10,   5,   5,   5,   5,  10, -10,
    -10,  10,  10,  10,  10,  10,  10, -10,
    -10,   5,  12,  15,  15,  12,   5, -10,
    -10,   8,  10,  15,  15,  10,   8, -10,
    -10,   5,   8,  10,  10,   8,   5, -10,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -20, -10, -10, -10, -10, -10, -10, -20,
};
constexpr int RookMg[64] = {
     -5,   0,   5,  10,  10,   5,   0,  -5,
    -10,  -5,   0,   0,   0,   0,  -5, -10,
    -10,  -5,   0,   0,   0,   0,  -5, -10,
    -10,  -5,   0,   0,   0,   0,  -5, -10,
     -5,   0,   0,   5,   5,   0,   0,  -5,
      0,   5,   5,  10,  10,   5,   5,   0,
     15,  20,  20,  20,  20,  20,  20,  15,
      5,   5,   5,  10,  10,   5,   5,   5,
};
constexpr int QueenPst[64] = {
    -20, -10, -10,  -5,  -5, -10, -10, -20,
    -10,   0,   5,   0,   0,   0,   0, -10,
    -10,   5,   5,   5,   5,   5,   0, -10,
     -5,   0,   5,   5,   5,   5,   0,  -5,
     -5,   0,   5,   5,   5,   5,   0,  -5,
    -10,   0,   5,   5,   5,   5,   0, -10,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -20, -10, -10,  -5,  -5, -10, -10, -20,
};
constexpr int KingMg[64] = {
     20,  35,  10, -10,   0, -10,  35,  25,
     10,  10, -10, -25, -25, -15,  10,  15,
    -20, -30, -40, -50, -50, -40, -30, -20,
    -40, -50, -60, -70, -70, -60, -50, -40,
    -50, -60, -70, -80, -80, -70, -60, -50,
    -60, -70, -80, -90, -90, -80, -70, -60,
    -70, -80, -90, -95, -95, -90, -80, -70,
    -80, -90, -95,-100,-100, -95, -90, -80,
};
constexpr int KingEg[64] = {
    -50, -35, -25, -20, -20, -25, -35, -50,
    -30, -15,  -5,   0,   0,  -5, -15, -30,
    -20,  -5,  15,  20,  20,  15,  -5, -20,
    -15,   0,  20,  30,  30,  20,   0, -15,
    -15,   0,  20,  30,  30,  20,   0, -15,
    -20,  -5,  15,  20,  20,  15,  -5, -20,
    -30, -15,  -5,   0,   0,  -5, -15, -30,
    -50, -35, -25, -20, -20, -25, -35, -50,
};
// clang-format on

struct Score {
    int mg = 0, eg = 0;
};

inline void add_pst(Score& s, PieceType pt, Square sq) {
    switch (pt) {
    case PAWN: s.mg += PawnMg[sq]; s.eg += PawnEg[sq]; break;
    case KNIGHT: s.mg += KnightPst[sq]; s.eg += KnightPst[sq]; break;
    case BISHOP: s.mg += BishopPst[sq]; s.eg += BishopPst[sq]; break;
    case ROOK: s.mg += RookMg[sq]; break;
    case QUEEN: s.mg += QueenPst[sq]; s.eg += QueenPst[sq]; break;
    case KING: s.mg += KingMg[sq]; s.eg += KingEg[sq]; break;
    default: break;
    }
}

struct Breakdown {
    Score side[COLOR_NB];
    int phase = 0;
};

Breakdown compute(const Position& pos) {
    Breakdown b;
    for (Color c : {WHITE, BLACK}) {
        for (int pt = PAWN; pt <= KING; ++pt) {
            Bitboard pcs = pos.pieces(c, PieceType(pt));
            while (pcs) {
                Square sq = pop_lsb(pcs);
                Square rel = relative_square(c, sq);  // mirror vertically for Black
                b.side[c].mg += MgValue[pt];
                b.side[c].eg += EgValue[pt];
                add_pst(b.side[c], PieceType(pt), rel);
                b.phase += PhaseWeight[pt];
            }
        }
        if (popcount(pos.pieces(c, BISHOP)) >= 2) {
            b.side[c].mg += 30;
            b.side[c].eg += 50;
        }
    }
    b.phase = std::min(b.phase, MaxPhase);
    return b;
}

}  // namespace

Value evaluate(const Position& pos) {
    Breakdown b = compute(pos);
    int mg = b.side[WHITE].mg - b.side[BLACK].mg;
    int eg = b.side[WHITE].eg - b.side[BLACK].eg;
    int v = (mg * b.phase + eg * (MaxPhase - b.phase)) / MaxPhase;
    v = pos.side_to_move() == WHITE ? v : -v;
    v += Tempo;
    // Damp toward zero as the fifty-move counter grows.
    v = v * (200 - pos.rule50()) / 200;
    return std::clamp(v, VALUE_MATED_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1);
}

std::string eval_trace(const Position& pos) {
    Breakdown b = compute(pos);
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "white mg %d eg %d | black mg %d eg %d | phase %d/%d\n"
                  "final (side to move): %d cp\n",
                  b.side[WHITE].mg, b.side[WHITE].eg, b.side[BLACK].mg, b.side[BLACK].eg, b.phase, MaxPhase,
                  evaluate(pos));
    return buf;
}

}  // namespace apex
