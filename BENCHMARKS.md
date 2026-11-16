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

### EXP-010 (continuation history): 2026-11-15. Inconclusive, not merged.
commit: aadc21b (branch exp/010-conthist, not merged)   machine: cloud-xeon-4vcpu   net: none   methodology: v1.1
change vs baseline (EXP-008): 1- and 2-ply continuation history added to quiet-move ordering scores
bench: nodes=90986
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 110, [0, 10], cap 3000 → **inconclusive**; games=3000; Elo=+3.0 ± 9.4; nElo=+4.0 ± 12.4; pentanomial=[124, 341, 553, 349, 133]; LLR=−0.25; W/L/D 932/906/1162; time forfeits 0

### EXP-015 (Texel-tuned evaluation, M6 v1): 2026-11-15. New champion.
commit: (this commit)   machine: cloud-xeon-4vcpu   net: none (tuned HCE)   methodology: v1.1
change vs baseline (EXP-008): evaluation rewritten as linear (mg, eg) weights × features, with new terms (mobility, passed/doubled/isolated pawns, rook files, king shelter, king-zone attacks), all 474 weights Texel-tuned (L2 5e-9) on 1.07M quiet positions from 12k Apex self-play games. Held-out MSE 0.09128 → 0.08543.
bench: nodes=92341; NPS about 10–15% below EXP-008 (more eval terms; noisy measurement during a match)
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 115, [0, 10], cap 3000 → **H1 accepted**; games=176; Elo=+312.4 ± 58.4; nElo=+459.4 ± 51.3; pentanomial=[0, 1, 12, 23, 52]; LLR=2.96; W/L/D 138/12/26; time forfeits 0

### EXP-014 (correction history): 2026-11-15. Inconclusive, not merged (re-test candidate).
commit: 945fb1e (branch exp/014-corrhist, not merged)   machine: cloud-xeon-4vcpu   net: none   methodology: v1.1
change vs baseline (EXP-015): pawn-key correction history added to the static eval (negamax and qsearch stand-pat)
bench: nodes=110102
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 114, [0, 10], cap 3000 → **inconclusive**; games=3000; Elo=+9.6 ± 8.8; nElo=+13.6 ± 12.4; pentanomial=[81, 354, 580, 371, 114]; LLR=2.14; W/L/D 941/858/1201; time forfeits 0
note: the 95% interval excludes zero, but the pre-declared rule (v1.1) is "not merged at the cap". It will get a fresh, independent confirmation test (new seed, no reuse of these games) rather than an exception.

### EXP-012 (singular extensions): 2026-11-15. New champion.
commit: (this commit)   machine: cloud-xeon-4vcpu   net: none (tuned HCE)   methodology: v1.1
change vs baseline (EXP-015): singular extensions with multi-cut (depth ≥ 8; TT lower bound with depth ≥ d−3; singular beta = tt_value − 2d; verification at (d−1)/2)
bench: nodes=92341 (unchanged: never triggers at bench depth 6); bench depth 9: 685369
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 112, [0, 10], cap 3000 → **H1 accepted**; games=770; Elo=+37.6 ± 17.9; nElo=+51.9 ± 24.5; pentanomial=[15, 79, 143, 104, 44]; LLR=2.95; W/L/D 258/175/337; time forfeits 0
methodology note: the depth-6 bench signature cannot see changes that act only at depth ≥ 8. From now on, such changes also record `bench 9` in their entry. Moving the signature to a deeper bench is a candidate for methodology v2.

### EXP-016 (Texel round 3 weights): 2026-11-16. Inconclusive, not merged.
commit: 13bcd8a (branch exp/016-texel-v3, not merged)   machine: cloud-xeon-4vcpu   net: none   methodology: v1.1
change vs baseline (EXP-012): weights re-tuned from EXP-015's on 1.39M positions (15.2k games, incl. 3.2k with the EXP-015 eval); held-out MSE 0.084250 → 0.083837
bench: nodes=113875
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 116, [0, 10], cap 3000 → **inconclusive**; games=3000; Elo=+0.2 ± 8.8; nElo=+0.3 ± 12.4; pentanomial=[98, 362, 577, 366, 97]; LLR=−1.16; W/L/D 885/883/1232; time forfeits 0

### EXP-011 (internal iterative reduction), run 1: 2026-11-15. Aborted (re-prioritisation), no result.
Stopped at 48 games, before any rating report, to free all 4 cores for NNUE generation-0 data (see EXPERIMENTS.md "M7/M8 plan"). Not stopped because of its score. It will be re-run from zero with a new seed; the partial PGN is kept under results/aborted.

### EXP-017 (NNUE generation 0): 2026-11-15. New champion; NNUE becomes the default evaluation.
commit: (this commit and the previous one)   machine: cloud-xeon-4vcpu   net: apex-gen0a.nnue, sha256 8793dfab4359a84f17c33b3fba5da3e069c2731bb9d917758bc231036672eb38   methodology: v1.1
change vs baseline (EXP-012, HCE): evaluation by the apex-768x128x2-1 network trained on 9,965,977 positions of our own HCE self-play (EXP-012 + datagen, 5000 nodes/move, 234 chunks of 500 games, seeds 1000–1001, 100000+, 200000+, 300000+); 20 epochs, target = 0.75 · sigmoid(score/400) + 0.25 · result; held-out MSE 0.006510
bench: nodes=144781 (new signature: the bench now runs with the embedded net; `setoption name EvalFile value <hce>` reproduces the old 92341)
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 117, [0, 10], cap 3000 → **H1 accepted**; games=280; Elo=+129.8 ± 36.2; nElo=+160.9 ± 40.7; pentanomial=[5, 14, 37, 44, 40]; LLR=2.95; W/L/D 156/56/68; time forfeits 0
diagnostics (development only, not decisions): a preliminary net on 3.2M positions lost −155 ± 60 Elo at 2+0.02 while datagen oversubscribed the cores (6 processes on 4 cores, 1 time forfeit), but was level at fixed 20k nodes (−24 ± 66). NPS after the lazy-accumulator/AVX2 optimisation: NNUE about 2.1M vs HCE about 1.5M on a 4 s search (loaded machine).

### EXP-018 (NNUE generation 1): 2026-11-16. New champion.
commit: (this commit)   machine: cloud-xeon-4vcpu   net: apex-gen1a.nnue, sha256 11a3b780eec529c1cf7b4c8339bbda58698253899be2debc8185cb36c49a5bdd   methodology: v1.1
change vs baseline (EXP-017, gen0a): network retrained (same architecture and trainer settings, 15 epochs) on gen 0 (9,965,977 positions) + gen 1 (14,928,296 positions from self-play of the EXP-017 engine at 5000 nodes/move, 337 chunks, seeds 1000000+, 2000000+, 3000000+, 4000000+); held-out MSE 0.007191 (not comparable to gen0a's: different validation mix)
bench: nodes=111869
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 118, [0, 10], cap 3000 → **H1 accepted**; games=214; Elo=+195.2 ± 45.5; nElo=+246.9 ± 46.6; pentanomial=[1, 8, 24, 29, 45]; LLR=2.97; W/L/D 142/33/39; time forfeits 0

### EXP-019 (NNUE generation 2, 128 hidden): 2026-11-16. New champion.
commit: (this commit)   machine: cloud-xeon-4vcpu   net: apex-gen2a.nnue, sha256 2863874b4cf6d40f0c55c819f17bc94ce42c90af3472311fe00820e352d65394   methodology: v1.1
change vs baseline (EXP-018, gen1a): net retrained on gen 1 (14,928,296) + gen 2 (14,947,206 positions from the EXP-018 engine, 335 chunks, seeds 10000000+ … 40000000+); gen-0 (HCE) data dropped; 15 epochs; held-out MSE 0.007347 (epoch 13 of the log; the written file is the epoch-15 state)
bench: nodes=79031
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 119, [0, 10], cap 3000 → **H1 accepted**; games=416; Elo=+67.7 ± 23.7; nElo=+98.0 ± 33.4; pentanomial=[4, 32, 80, 64, 28]; LLR=3.00; W/L/D 176/96/144; time forfeits 0
note: during this SPRT a 256-hidden training run was suspended (SIGSTOP) and datagen stopped, so the 3 game slots had the cores to themselves.

### EXP-020 (256-hidden NNUE, gen-2 data): 2026-11-17. Inconclusive, not merged.
commit: ee1e699 (branch exp/020-nnue256, not merged; patch in experiments/patches)   machine: cloud-xeon-4vcpu   net: apex-gen2b-256.nnue, sha256 5a8751c860c96ea77e1392200f56a83a0eeb2017caa33091a2b51cbf35667428   methodology: v1.1
change vs baseline (EXP-019, 128 hidden): same data and trainer, 256 hidden units (build APEX_NNUE_HIDDEN=256); held-out MSE 0.006977 vs about 0.00735
bench: nodes=86926; NPS about equal to the 128 build (4 s search: 2.03M vs 1.86M on a loaded machine)
SPRT: STC 8+0.08, 16 MB, conc 3, UHO_Lichess_4852_v1, seed 120, [0, 10], cap 3000 → **inconclusive**; games=3000; Elo=−0.7 ± 8.6; nElo=−1.0 ± 12.4; pentanomial=[97, 341, 618, 359, 85]; LLR=−1.49; W/L/D 967/973/1060; time forfeits 0
diagnostic: on 300 gen-3 positions the 256 net's evals are about 11% larger in magnitude (mean |eval| 421 vs 380 cp; search labels 485), with mean |difference| 86 cp.
