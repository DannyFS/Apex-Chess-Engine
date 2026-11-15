// Apex chess engine: self-play training-data generation (M7).
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace apex {

struct DatagenConfig {
    std::string out_path;
    int games = 1000;
    int nodes = 5000;          // soft node limit per move
    std::uint64_t seed = 1;
    int random_plies = 8;      // random opening plies (8 or 9, chosen per game)
    int hash_mb = 16;
};

// Plays self-play games and appends "<fen> | <score> | <result>" lines, with score
// in centipawns and result (1.0/0.5/0.0) both from White's point of view.
void run_datagen(const DatagenConfig& cfg);

}  // namespace apex
