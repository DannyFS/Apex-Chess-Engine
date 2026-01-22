#pragma once

#include "util/types.h"
#include <string>

class Position;

namespace Syzygy {

enum WDLResult {
    TB_LOSS = 0,
    TB_BLESSED_LOSS = 1,
    TB_DRAW = 2,
    TB_CURSED_WIN = 3,
    TB_WIN = 4
};

bool init(const std::string& path);
void free();

int probe_wdl(const Position& pos, bool& success);
int probe_root(const Position& pos, Move& best_move, int& wdl, int& dtz);

} // namespace Syzygy
