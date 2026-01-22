#pragma once

#include "util/types.h"
#include <array>

namespace Zobrist {
    extern std::array<std::array<u64, 64>, 12> piece;
    extern std::array<u64, 16> castling;
    extern std::array<u64, 8> ep_file;
    extern u64 side;

    void init();
}
