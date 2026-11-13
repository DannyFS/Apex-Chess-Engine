// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace apex {

// Human-readable report: CPU brand, runtime ISA support, ISA the binary was built for, compiler.
std::string cpu_report();

}  // namespace apex
