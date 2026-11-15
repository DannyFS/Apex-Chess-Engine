// Apex chess engine: NNUE evaluation (M8), architecture "apex-768x128x2-1".
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Inputs: 768 features per perspective (own/their colour x piece type x square, the
// board mirrored vertically for Black's perspective). One hidden layer of HIDDEN
// int16 neurons per perspective (accumulators updated incrementally), SCReLU
// activation, then one output neuron over [side to move, other side].
// Quantisation: input weights x QA (255), output weights x QB (64); the output is
// scaled by SCALE (400) into centipawns. The trainer is tools/nnue/train.py.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/position.h"

namespace apex::nnue {

constexpr int INPUTS = 768;
constexpr int HIDDEN = 128;
constexpr int QA = 255;
constexpr int QB = 64;
constexpr int SCALE = 400;

struct Network {
    alignas(64) std::int16_t ft_w[INPUTS * HIDDEN];  // [feature][neuron]
    alignas(64) std::int16_t ft_b[HIDDEN];
    alignas(64) std::int16_t out_w[2 * HIDDEN];      // [stm neurons..., other neurons...]
    std::int16_t out_b;
};

struct Accumulator {
    alignas(64) std::int16_t v[COLOR_NB][HIDDEN];
};

// Loads a network file (format documented in tools/nnue/README.md). Returns false
// and leaves the current state unchanged on any error.
bool load(const std::string& path, std::string& error);
bool loaded();
const std::string& loaded_name();  // file name and SHA-256 prefix of the loaded net
void unload();

// Feature index of piece pc on square sq, seen from perspective `persp`.
inline int feature(Color persp, Piece pc, Square sq) {
    const int rel = color_of(pc) == persp ? 0 : 1;
    const int s = persp == WHITE ? int(sq) : int(sq) ^ 56;
    return rel * 384 + int(type_of(pc)) * 64 + s;
}

// Accumulator stack for one search thread.
class Evaluator {
public:
    void reset(const Position& pos);             // full refresh at the root
    void push(const Position& pos, Move m);      // before pos.make_move(m)
    void push_null();                            // before pos.make_null()
    void pop() { --top; }
    Value evaluate(const Position& pos) const;   // side-to-move score in centipawns

private:
    std::vector<Accumulator> stack = std::vector<Accumulator>(MAX_PLY + 8);
    int top = 0;
};

}  // namespace apex::nnue
