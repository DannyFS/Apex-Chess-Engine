// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/position.h"

namespace apex {

// Static evaluation from the side to move's point of view, in centipawns.
// M3: bootstrap handcrafted evaluation (tapered material + piece-square tables).
// Will be Texel-tuned (M6) and then replaced by NNUE (M8).
Value evaluate(const Position& pos);

// Debug breakdown printed by the UCI "eval" command.
std::string eval_trace(const Position& pos);

// Texel tuning support: fills coeffs[evl::NUM_WEIGHTS] with white-minus-black feature
// counts and returns the game phase (0 = endgame .. 24 = opening).
int eval_features(const Position& pos, int* coeffs);

}  // namespace apex
