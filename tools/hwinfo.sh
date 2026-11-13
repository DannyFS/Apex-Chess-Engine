#!/usr/bin/env bash
# Apex chess engine: hardware / toolchain report for Linux (milestone M0).
# SPDX-License-Identifier: GPL-3.0-or-later
# Usage: tools/hwinfo.sh [path/to/apex] > hwinfo-linux.txt
run() { command -v "$1" >/dev/null 2>&1 && "$@" 2>&1 | head -n 1 || echo "not found"; }

echo "=== Apex hardware report (Linux) ==="
echo "date: $(date -Iseconds)"
echo "os: $(. /etc/os-release 2>/dev/null && echo "$PRETTY_NAME") kernel $(uname -r)"
echo "cpu: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
echo "logical cpus: $(nproc)"
command -v lscpu >/dev/null && lscpu | grep -E '^(Core|Socket|Thread|L2|L3)' | sed 's/^/  /'
echo "isa flags: $(grep -m1 '^flags' /proc/cpuinfo | tr ' ' '\n' | grep -E '^(popcnt|avx2|bmi2|avx512f|avx512bw|avx512_vnni|avx_vnni)$' | tr '\n' ' ')"
echo "ram: $(free -h | awk '/Mem:/ {print $2}')"
echo "--- gpu ---"
echo "nvidia-smi: $(run nvidia-smi --query-gpu=name,memory.total,driver_version,compute_cap --format=csv,noheader)"
echo "nvcc: $(command -v nvcc >/dev/null && nvcc --version | tail -n 2 | head -n 1 || echo 'not found')"
echo "--- toolchain ---"
for t in cmake g++ clang++ ninja python3 git cargo; do echo "$t: $(run $t --version)"; done
if [ -n "$1" ] && [ -x "$1" ]; then echo "--- engine cpuid report ---"; "$1" cpuinfo; fi
