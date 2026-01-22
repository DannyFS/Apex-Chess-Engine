#pragma once

#include "nnue/nnue_loader.h"

class Position;

namespace NNUE {
int evaluate_cpu(const Position& pos, const NnueWeights& w);
}
