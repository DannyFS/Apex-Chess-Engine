// Apex chess engine
// SPDX-License-Identifier: GPL-3.0-or-later
#include "cpuinfo.h"

#include <cstring>
#include <thread>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

namespace apex {

namespace {

#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
#define APEX_X86 1
void cpuid(int leaf, int sub, unsigned r[4]) {
#if defined(_MSC_VER)
    int t[4];
    __cpuidex(t, leaf, sub);
    for (int i = 0; i < 4; ++i) r[i] = unsigned(t[i]);
#else
    __cpuid_count(leaf, sub, r[0], r[1], r[2], r[3]);
#endif
}
#endif

std::string compiler() {
#if defined(__clang__)
    return "clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__GNUC__)
    return "gcc " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
    return "unknown";
#endif
}

std::string build_isa() {
    std::string s;
#if defined(__AVX512F__)
    s += " avx512f";
#endif
#if defined(__AVX512VNNI__)
    s += " avx512vnni";
#endif
#if defined(__AVX2__)
    s += " avx2";
#endif
#if defined(__BMI2__)
    s += " bmi2";
#endif
#if defined(__POPCNT__) || defined(__AVX2__)
    s += " popcnt";
#endif
    return s.empty() ? " baseline" : s;
}

}  // namespace

std::string cpu_report() {
    std::string out;
#ifdef APEX_X86
    unsigned r[4];
    char brand[49] = {};
    cpuid(0x80000000, 0, r);
    if (r[0] >= 0x80000004) {
        for (int i = 0; i < 3; ++i) {
            cpuid(0x80000002 + i, 0, r);
            std::memcpy(brand + 16 * i, r, 16);
        }
    }
    cpuid(1, 0, r);
    bool popcnt = r[2] & (1u << 23);
    bool osxsave = r[2] & (1u << 27);
    cpuid(7, 0, r);
    bool avx2 = r[1] & (1u << 5), bmi2 = r[1] & (1u << 8), avx512f = r[1] & (1u << 16),
         avx512bw = r[1] & (1u << 30), avx512vnni = r[2] & (1u << 11);
    cpuid(7, 1, r);
    bool avxvnni = r[0] & (1u << 4);
    out += "cpu: " + std::string(brand) + "\n";
    out += "logical cpus: " + std::to_string(std::thread::hardware_concurrency()) + "\n";
    out += "runtime isa:";
    if (popcnt) out += " popcnt";
    if (avx2 && osxsave) out += " avx2";
    if (bmi2) out += " bmi2";
    if (avx512f) out += " avx512f";
    if (avx512bw) out += " avx512bw";
    if (avx512vnni) out += " avx512vnni";
    if (avxvnni) out += " avxvnni";
    out += "\n";
#else
    out += "cpu: non-x86\n";
#endif
    out += "build isa:" + build_isa() + "\n";
    out += "compiler: " + compiler() + "\n";
    return out;
}

}  // namespace apex
