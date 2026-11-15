# Apex: Next-Generation Chess Engine (research project)

A research-driven UCI chess engine. The planned design is a CPU alpha-beta (PVS) search with a self-trained NNUE evaluation, with the GPU used for training. Every change is gated by correctness tests and SPRT match evidence.

**Status:** M2 (board core) and M3 (baseline UCI engine) complete. M5 (search stack) is in progress, one SPRT-tested change at a time; the first M6 Texel-tuned evaluation is merged. Current champion: EXP-015 (bench 92341). Every accepted and rejected change is listed in [EXPERIMENTS.md](EXPERIMENTS.md), with results in [BENCHMARKS.md](BENCHMARKS.md). See [ROADMAP.md](ROADMAP.md).

License: [GPL-3.0-or-later](LICENSE).

## Build

Requirements: CMake ≥ 3.20 and a C++20 compiler (GCC ≥ 11, Clang ≥ 14 or MSVC 2022).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release        # default target: x86-64-v3 (AVX2/BMI2)
cmake --build build -j
```

`-DAPEX_ARCH=native|v4|v3|v2` selects the instruction set (`v4` = AVX-512). On Windows (MSVC):

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Test

```bash
./build/test_core            # perft (21 positions), fuzzing, SEE, draws: about 3 s
./build/test_core --deep     # adds deep perft (1.5 billion nodes): about 20 s
python3 tests/uci_test.py build/apex   # UCI conformance; needs `pip install chess`
./build/apex bench           # deterministic node-count signature + NPS
```

## Use

Apex speaks UCI and works in any UCI GUI (Cute Chess, Arena, BanksiaGUI, En Croissant) or with fastchess / cutechess-cli. Extra console commands: `d` (board), `eval`, `bench [depth]`, `cpuinfo`, `go perft <n>`, `texel-features <positions> <out>`.

## Tuning the evaluation (Texel)

```bash
python3 tools/texel/extract.py results/*.pgn --out positions.txt     # quiet positions + game results
./build/apex "texel-features positions.txt features.txt"              # feature counts per position
python3 tools/texel/tune.py features.txt --l2 5e-9 --out src/eval/weights.inc   # needs numpy, scipy
```

## Repository layout

```
src/core     bitboards, magics, position, move generation, SEE
src/eval     evaluation (bootstrap HCE; NNUE from M8)
src/search   search, transposition table
src/uci      UCI protocol, bench
src/util     CPU feature report
tests        core tests (C++), UCI conformance (Python)
tools        match/SPRT runner (fastchess), Texel tuner, hardware reports
experiments  patches of experiments that were not merged
```

## Documents

| Document | Purpose |
|---|---|
| [RESEARCH.md](RESEARCH.md) | State-of-the-art survey and bibliography |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Candidate architectures, the selected design and its rationale |
| [ROADMAP.md](ROADMAP.md) | Milestones, exit criteria and realistic targets |
| [BENCHMARKS.md](BENCHMARKS.md) | Frozen test methodology plus the append-only results history |
| [EXPERIMENTS.md](EXPERIMENTS.md) | Hypothesis log, including rejected experiments |
| [CHANGELOG.md](CHANGELOG.md) | Project changes |

The previous prototype is preserved in git history at commit `3e6ee83`.
