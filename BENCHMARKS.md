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

### EXP-003 (late move reductions): 2026-11-13. New champion.
commit: (this commit)   machine: cloud-xeon-4vcpu   net: none   methodology: v1
change vs baseline (EXP-002): log-log LMR
bench: nodes=571441
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 103, [0, 10] → **H1 accepted**; games=216; Elo=+188.7 ± 41.1; nElo=+259.5 ± 46.3; pentanomial=[1, 4, 30, 33, 40]; LLR=2.96; time forfeits 0

### EXP-004 (reverse futility pruning): 2026-11-13. New champion.
commit: (this commit)   machine: cloud-xeon-4vcpu   net: none   methodology: v1
change vs baseline (EXP-003): RFP depth ≤ 8, margin 80·depth
bench: nodes=405629
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 104, [0, 10] → **H1 accepted**; games=398; Elo=+74.5 ± 25.7; nElo=+102.3 ± 34.1; pentanomial=[6, 26, 79, 54, 34]; LLR=2.96; time forfeits 0

### EXP-005 (aspiration windows): 2026-11-13. New champion.
commit: (this commit)   machine: cloud-xeon-4vcpu   net: none   methodology: v1
change vs baseline (EXP-004): aspiration windows from depth 4, ±20 cp, ×1.5 widening, full window once delta > 1000
bench: nodes=367806
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 105, [0, 10] → **H1 accepted**; games=1264; Elo=+25.6 ± 14.7; nElo=+33.5 ± 19.2; pentanomial=[45, 123, 229, 164, 71]; LLR=2.95; W/L/D 464/371/429
time forfeits: **1** (game 4, the baseline exp004, 36 ms overrun). It happened near the start of the run; a one-core `nice -j1` build of a later experiment ran during this match, within the one-core development allowance. One game in 1264 does not change the decision. Overruns are being watched: if they recur, the move-overhead default (10 ms) gets its own experiment.

### Methodology v1.1: 2026-11-14 (amends v1)
1. **Game cap for early-phase SPRTs.** A [0, 10] SPRT that has not resolved by **3000 games** is stopped and recorded as **inconclusive**. The change is not merged; it may be retried later (for example in a batch, at LTC, or with more hardware). Reason: an effect of about +5 Elo sits between the bounds and can need 10k+ games (more than 24 h on 3 game slots). A cap can only produce "not merged", so it cannot raise the false-positive rate. Introduced during EXP-006 (at about 2600 games, after seeing its interim results); recorded here for transparency. It applies to every SPRT from EXP-007 on.
2. **Interrupted runs.** The cloud container is reclaimed when the session goes idle, which kills running matches. A run interrupted *before* any decision is either (a) resumed from fastchess's saved state (`-config file=config.json`, same seed, same opening sequence), or (b) discarded and restarted from zero with a new seed. It is never restarted *because of* its interim score. Both are logged.

### EXP-006 (check extension): 2026-11-14. Inconclusive, not merged.
commit: a9229ad (branch exp/006-check-ext, not merged)   machine: cloud-xeon-4vcpu   net: none   methodology: v1 → v1.1 cap applied
change vs baseline (EXP-005): extend checking moves by one ply
bench: nodes=471163
run 1 (seed 106, started 2026-11-13 23:04): **aborted** by a container restart at about 400 games (Elo +8.7 ± 26.5, LLR 0.20). Discarded and restarted from zero with a new seed, without continuing.
run 2 (seed 1062): STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, [0, 10]. Interrupted by another container restart at 860 games; **resumed** from the saved fastchess state (same seed). Stopped at the 3000-game cap → **inconclusive**. games=3000; W/L/D 982/917/1101; Elo=+7.5 ± 9.6; nElo=+9.8 ± 12.4; pentanomial=[136, 313, 537, 378, 136]; LLR≈1.19 (from the PGN; fastchess reported 1.23 at 2845 games)
time forfeits: 1 (pair 721, the baseline exp005).

### EXP-007 (late move pruning + futility pruning): 2026-11-14. New champion.
commit: (this commit)   machine: cloud-xeon-4vcpu   net: none   methodology: v1.1
change vs baseline (EXP-005): LMP (skip quiets once legal > 3 + depth², depth ≤ 6) and futility pruning (skip quiets when static_eval + 100 + 100·depth ≤ alpha, depth ≤ 6), non-PV, not in check, non-checking quiets, after one move searched
bench: nodes=283803
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 107, [0, 10], cap 3000 → **H1 accepted**; games=286; Elo=+118.6 ± 32.2; nElo=+160.8 ± 40.3; pentanomial=[3, 15, 42, 51, 32]; LLR=2.99; W/L/D 138/44/104; time forfeits 0

### EXP-008 (SEE pruning): 2026-11-14. New champion.
commit: (this commit)   machine: cloud-xeon-4vcpu   net: none   methodology: v1.1
change vs baseline (EXP-007): main search (non-root, not in check, depth ≤ 8, after one move searched) skips quiets with SEE < −50·depth and captures with SEE < −100·depth; qsearch (not in check) skips captures with SEE < 0
bench: nodes=92972
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 108, [0, 10], cap 3000 → **H1 accepted**; games=320; Elo=+96.9 ± 27.6; nElo=+141.0 ± 38.1; pentanomial=[4, 15, 56, 60, 25]; LLR=2.95; W/L/D 148/61/111; time forfeits 0

### EXP-009 (TT buckets): 2026-11-14. Rejected.
commit: 5351ba5 (branch exp/009-tt-buckets, not merged)   machine: cloud-xeon-4vcpu   net: none   methodology: v1.1
change vs baseline (EXP-008): 4-entry, 64-byte TT buckets; new keys evict an empty slot, else the lowest depth − 8·age entry
bench: nodes=92972 (unchanged; the 16 MB bench table never fills at depth 6)
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 109, [0, 10], cap 3000 → **H0 accepted**; games=2806; Elo=−5.8 ± 9.6; nElo=−7.8 ± 12.9; pentanomial=[117, 343, 520, 316, 107]; LLR=−2.98; W/L/D 825/872/1109; time forfeits 0
