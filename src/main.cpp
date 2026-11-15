// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <string>

#include "core/position.h"
#include "eval/nnue/nnue.h"
#include "uci/uci.h"

int main(int argc, char** argv) {
    apex::init_core();
    std::string err;
    if (!apex::nnue::load_embedded(err))
        std::printf("info string embedded NNUE not loaded (%s); using the hand-crafted eval\n", err.c_str());
    return apex::uci_main(argc, argv);
}
