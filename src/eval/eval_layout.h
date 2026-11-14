// Apex chess engine: index layout of the tunable evaluation weights.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every evaluation term is a (mg, eg) weight times an integer feature count
// (white count minus black count). This keeps the eval linear in its weights so
// that Texel tuning (tools/texel) is a logistic regression over exported features.
#pragma once

namespace apex::evl {

enum : int {
    MATERIAL = 0,                    // [piece type], 6
    PST = MATERIAL + 6,              // [piece type][relative square], 6 * 64
    BISHOP_PAIR = PST + 6 * 64,      // 1
    MOB_KNIGHT = BISHOP_PAIR + 1,    // [0..8]
    MOB_BISHOP = MOB_KNIGHT + 9,     // [0..13]
    MOB_ROOK = MOB_BISHOP + 14,      // [0..14]
    MOB_QUEEN = MOB_ROOK + 15,       // [0..27]
    PASSED = MOB_QUEEN + 28,         // [relative rank], 8
    DOUBLED = PASSED + 8,            // per extra pawn on a file
    ISOLATED = DOUBLED + 1,          // per isolated pawn
    ROOK_OPEN = ISOLATED + 1,        // rook on a file with no pawns
    ROOK_SEMI = ROOK_OPEN + 1,       // rook on a file with only enemy pawns
    KING_SHELTER = ROOK_SEMI + 1,    // own pawns in the 3x2 block in front of the king
    KING_ATTACK = KING_SHELTER + 1,  // [N, B, R, Q]: attacks on squares next to the enemy king
    NUM_WEIGHTS = KING_ATTACK + 4
};

}  // namespace apex::evl
