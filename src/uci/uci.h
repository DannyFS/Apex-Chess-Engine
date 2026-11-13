// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace apex {

constexpr const char* EngineName = "Apex";
constexpr const char* EngineVersion = "0.1-dev";

// Runs the UCI loop on stdin/stdout. Command-line arguments are executed as a
// single command first (e.g. `apex bench`), and the loop is skipped.
int uci_main(int argc, char** argv);

// Fixed-depth search over the bench positions; prints "<nodes> nodes <nps> nps".
void run_bench(int depth, int hash_mb);

}  // namespace apex
