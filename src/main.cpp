// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/position.h"
#include "uci/uci.h"

int main(int argc, char** argv) {
    apex::init_core();
    return apex::uci_main(argc, argv);
}
