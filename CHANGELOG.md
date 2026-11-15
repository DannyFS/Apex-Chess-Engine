# CHANGELOG.md

## [Unreleased]

### 2026-11-13 – 2026-11-15: Engine, search stack, tuned eval, first NNUE
Self-play gains are measured against the previous champion at 8+0.08 (BENCHMARKS.md). They add up only approximately and are not CCRL ratings.
- M2/M3: bitboard core (magic bitboards, perft-verified movegen, SEE), UCI engine with PVS, TT, qsearch, time management; test suite, CI, fastchess match runner.
- M5 search stack, accepted: null-move pruning (+134), killers + history (+125), LMR (+189), reverse futility pruning (+74), aspiration windows (+26), LMP + futility pruning (+119), SEE pruning (+97), singular extensions (+38).
- Not merged (kept as patches in experiments/patches): check extension, TT buckets, continuation history, correction history, Texel round 3.
- M6: linear tunable HCE with Texel tuner (+312).
- M7/M8: self-play data generator, NNUE inference (apex-768x128x2-1, lazy accumulators, AVX2), numpy trainer; generation-0 net embedded as the default evaluation (+130 vs the tuned HCE). Bench signature: 144781.
- Methodology v1.1: 3000-game cap for [0, 10] SPRTs; rules for interrupted runs.

### 2026-11-13: Project restart
- Removed the previous ApexNNUE prototype from the development branch at the owner's request. It is preserved at commit `3e6ee83` and on `main`. Audit findings are in ARCHITECTURE.md §1.
- Added the project documentation set: README, RESEARCH (Stage-2 report v0.1), ARCHITECTURE (Stage-3 proposal), ROADMAP, BENCHMARKS (methodology v1), EXPERIMENTS and CHANGELOG.
- No engine code yet. Implementation starts at milestone M0/M2 after the owner decisions in ARCHITECTURE.md §7.
