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
_No engine exists yet. The first entry will be the M3 baseline._

### 2026-11-13: audit of the previous engine (commit 3e6ee83), cloud container (4 vCPU Xeon @2.1 GHz)
Smoke test only; not a benchmark. Tests failed to link; `go perft` unsupported (returned `bestmove 0000`); `go depth 12` on the start position gave score cp 0 at every depth (no network loaded), with about 3.2 Mnps reported. The engine was retired; see ARCHITECTURE.md §1.
