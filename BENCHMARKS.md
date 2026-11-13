# BENCHMARKS.md

**Append-only.** Never edit or delete an entry. Corrections are recorded as new entries that reference the old one.

## Methodology (v1, frozen until explicitly versioned)

Every changed setting creates a new methodology version.

### Correctness gates (must pass before any strength test)
Perft suite; Zobrist fuzz; UCI conformance; `bench` signature recorded.

### Performance metrics (per commit, single-threaded, fixed hash)
- `bench`: total nodes (signature), NPS, and time over a fixed set of about 50 positions at fixed depth.
- Per-position nodes to depth, effective branching factor `(N_d / N_{d-2})^{1/2}`, TT hit rate, `hashfull`, evals per second.
- Search-stability probe: how often the best move changes between iterations on the bench set.
- NPS is measured as a median of 5 runs on an idle machine. A *machine ID* from `hwinfo` is mandatory.

### Strength tests
| Test | Tool | TC | Openings | Stop rule |
|---|---|---|---|---|
| Gainer | fastchess | STC 8+0.08, 1 thread, 16 MB | UHO book, paired colours | SPRT [0, 3] (early phase [0, 10]), α=β=0.05, pentanomial |
| Gainer confirmation | fastchess | LTC 40+0.4, 1 thread, 64 MB | same | SPRT [0, 3] |
| Simplification | fastchess | STC | same | SPRT [−3, 1] |
| SMP | fastchess | 8+0.08, 1 vs N threads | same | fixed games, Elo ± 95% CI |
| Absolute rating | fastchess gauntlet | 40/15-equivalent or 60+0.6 | balanced book | at least 1000 games per opponent; Elo ± 95% CI (Ordo/BayesElo) |

Draws and adjudications use fastchess defaults, which are recorded per run. Hardware time-scaling: the time control is scaled to a reference NPS so that results stay comparable across machines.

### Tactical and positional suites (diagnostic only; never used for keep/reject decisions)
WAC (300 positions), Arasan test suite, ERET, STS (strategic), Lichess puzzle database (CC0) sample, and endgame positions checked against Syzygy. Report: solved count at a fixed time per position. Elo decisions come **only** from games.

### Entry template
```
### <version> — <date>
commit: <sha>   machine: <hwinfo id>   net: <sha256/none>   methodology: v1
change vs baseline: ...
bench: nodes=<n> nps=<n>
SPRT: <tc> bounds [e0,e1] → H0/H1 accepted; games=<n>; Elo=<x ± ci>; pentanomial=[...]; LLR=<v>
notes: ...
```

## History
_Entries in chronological order._

### 2026-11-13: audit of the previous engine (commit 3e6ee83), cloud container (4 vCPU Xeon @2.1 GHz)
Smoke test only; not a benchmark. Tests failed to link; `go perft` unsupported (returned `bestmove 0000`); `go depth 12` on the start position gave score cp 0 at every depth (no network loaded), with about 3.2 Mnps reported. The engine was retired; see ARCHITECTURE.md §1.

### M3-baseline: 2026-11-13
commit: ca423ff (engine code identical through f2c7723)   machine: cloud-xeon-4vcpu (Intel Xeon @2.10GHz, 4 vCPU, AVX-512; see tools/hwinfo.sh)   net: none (bootstrap HCE)   methodology: v1
change vs baseline: first engine (PVS + TT + qsearch + MVV-LVA, untuned material/PST eval)
bench (depth 6, 20 positions, 16 MB): nodes=8689072 (identical under GCC 13 -march=native and Clang 18 x86-64-v2); nps about 2.5M
correctness: test_core (21-position perft + fuzz) and --deep (1.52B perft nodes) pass; ASan/UBSan pass; tests/uci_test.py passes
validation run 1 (self-play, 1+0.01, conc 4, UHO_Lichess_4852_v1, seed 1): 1000 games, 0 crashes, 0 illegal moves, **10 time forfeits (1–13 ms overrun)**. The forfeits cluster where compilation was running concurrently on the same 4 cores (oversubscription).
validation run 2 (same, seed 2, idle machine): 1000 games, **0 time forfeits, 0 crashes, 0 illegal moves**. Self-play score 49.55% (Elo -3.1 ± 15.9, a sanity check for symmetric play).
methodology note: matches must not share cores with other work. From now on SPRTs on this 4-vCPU machine use concurrency 3, leaving one core for development.

### EXP-001 (null-move pruning): 2026-11-13. New champion.
commit: 2b492de   machine: cloud-xeon-4vcpu   net: none (bootstrap HCE)   methodology: v1
change vs baseline (M3-baseline): adaptive null-move pruning
bench: nodes=3521991
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 101, bounds [0, 10] nElo → **H1 accepted**; games=266; Elo=+134.3 ± 36.2; nElo=+171.7 ± 41.8; pentanomial=[3, 11, 45, 33, 41]; LLR=2.97 (−2.94, 2.94); time forfeits 0

### EXP-002 (killers + history): 2026-11-13. New champion.
commit: (this commit)   machine: cloud-xeon-4vcpu   net: none   methodology: v1
change vs baseline (EXP-001): killer moves + butterfly history for quiet ordering
bench: nodes=2017210
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 102, [0, 10] → **H1 accepted**; games=284; Elo=+125.0 ± 35.1; nElo=+157.6 ± 40.4; pentanomial=[4, 14, 45, 38, 41]; LLR=2.99; time forfeits 0
