// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#include "eval.h"

#include <algorithm>
#include <cstdio>

#include "eval_layout.h"

namespace apex {

using namespace evl;

namespace {

struct S {
    int mg, eg;
};

// Weights: hand-written M3 values, replaced by Texel-tuned values in M6.
constexpr S W[NUM_WEIGHTS] = {
#include "weights.inc"
};

constexpr int PhaseWeight[PIECE_TYPE_NB] = {0, 1, 1, 2, 4, 0};
constexpr int MaxPhase = 24;
constexpr int Tempo = 15;

// Accumulates the white-minus-black score and, optionally, the feature counts.
struct Acc {
    int mg = 0, eg = 0;
    int* coeffs = nullptr;
    void add(Color c, int idx, int n = 1) {
        const int k = c == WHITE ? n : -n;
        mg += k * W[idx].mg;
        eg += k * W[idx].eg;
        if (coeffs) coeffs[idx] += k;
    }
};

int compute(const Position& pos, Acc& a) {
    int phase = 0;
    const Bitboard occ = pos.pieces();
    const Bitboard pawns[COLOR_NB] = {pos.pieces(WHITE, PAWN), pos.pieces(BLACK, PAWN)};
    const Bitboard pawn_att[COLOR_NB] = {pawn_attacks_bb<WHITE>(pawns[WHITE]), pawn_attacks_bb<BLACK>(pawns[BLACK])};

    for (Color c : {WHITE, BLACK}) {
        const Color them = ~c;
        const Bitboard mob_area = ~pos.pieces(c) & ~pawn_att[them];
        const Bitboard king_zone = king_attacks(pos.king_square(them));

        for (int pt = PAWN; pt <= KING; ++pt) {
            Bitboard pcs = pos.pieces(c, PieceType(pt));
            while (pcs) {
                const Square sq = pop_lsb(pcs);
                a.add(c, MATERIAL + pt);
                a.add(c, PST + pt * 64 + relative_square(c, sq));
                phase += PhaseWeight[pt];
                if (pt == PAWN || pt == KING) continue;

                const Bitboard att = attacks_bb(PieceType(pt), sq, occ);
                const int mob = popcount(att & mob_area);
                if (pt == KNIGHT) a.add(c, MOB_KNIGHT + mob);
                else if (pt == BISHOP) a.add(c, MOB_BISHOP + mob);
                else if (pt == ROOK) a.add(c, MOB_ROOK + mob);
                else a.add(c, MOB_QUEEN + mob);
                if (att & king_zone) a.add(c, KING_ATTACK + pt - KNIGHT, popcount(att & king_zone));

                if (pt == ROOK) {
                    const Bitboard f = file_bb(file_of(sq));
                    if (!(f & pawns[c])) a.add(c, (f & pawns[them]) ? ROOK_SEMI : ROOK_OPEN);
                }
            }
        }
        if (popcount(pos.pieces(c, BISHOP)) >= 2) a.add(c, BISHOP_PAIR);

        // Pawn structure
        Bitboard p = pawns[c];
        while (p) {
            const Square sq = pop_lsb(p);
            const int f = file_of(sq), r = rank_of(sq);
            const Bitboard adj = (f > 0 ? file_bb(f - 1) : 0) | (f < 7 ? file_bb(f + 1) : 0);
            if (!(adj & pawns[c])) a.add(c, ISOLATED);
            // Passed: no enemy pawn ahead on this or adjacent files.
            Bitboard ahead = 0;
            for (int rr = r + (c == WHITE ? 1 : -1); rr >= 0 && rr <= 7; rr += (c == WHITE ? 1 : -1))
                ahead |= rank_bb(rr);
            if (!(ahead & (adj | file_bb(f)) & pawns[them])) a.add(c, PASSED + relative_rank(c, r));
        }
        for (int f = 0; f < 8; ++f) {
            const int n = popcount(pawns[c] & file_bb(f));
            if (n > 1) a.add(c, DOUBLED, n - 1);
        }

        // King shelter: own pawns on the king's file and neighbours, one or two ranks ahead.
        const Square ksq = pos.king_square(c);
        const int kf = file_of(ksq), kr = rank_of(ksq);
        const Bitboard files = file_bb(kf) | (kf > 0 ? file_bb(kf - 1) : 0) | (kf < 7 ? file_bb(kf + 1) : 0);
        Bitboard ranks = 0;
        for (int d = 1; d <= 2; ++d) {
            const int rr = kr + (c == WHITE ? d : -d);
            if (rr >= 0 && rr <= 7) ranks |= rank_bb(rr);
        }
        const int shelter = popcount(files & ranks & pawns[c]);
        if (shelter) a.add(c, KING_SHELTER, shelter);
    }
    return std::min(phase, MaxPhase);
}

int finish(const Position& pos, const Acc& a, int phase) {
    int v = (a.mg * phase + a.eg * (MaxPhase - phase)) / MaxPhase;
    v = pos.side_to_move() == WHITE ? v : -v;
    v += Tempo;
    // Damp toward zero as the fifty-move counter grows.
    v = v * (200 - pos.rule50()) / 200;
    return std::clamp(v, VALUE_MATED_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1);
}

}  // namespace

Value evaluate(const Position& pos) {
    Acc a;
    const int phase = compute(pos, a);
    return finish(pos, a, phase);
}

int eval_features(const Position& pos, int* coeffs) {
    std::fill(coeffs, coeffs + NUM_WEIGHTS, 0);
    Acc a;
    a.coeffs = coeffs;
    return compute(pos, a);
}

std::string eval_trace(const Position& pos) {
    Acc a;
    const int phase = compute(pos, a);
    char buf[256];
    std::snprintf(buf, sizeof(buf), "white-minus-black mg %d eg %d | phase %d/%d\nfinal (side to move): %d cp\n",
                  a.mg, a.eg, phase, MaxPhase, finish(pos, a, phase));
    return buf;
}

}  // namespace apex
