# EXPERIMENTS.md

Log of every hypothesis, accepted **or rejected**. Append-only. Results live in BENCHMARKS.md; this file records the reasoning.

## Protocol
1. One major variable per experiment. Interacting changes get an ablation matrix.
2. Before running: write the hypothesis, the mechanism, the expected effect, the cost (CPU-hours or GPU-hours) and the stop rule.
3. Branch `exp/<id>-<slug>` from the champion. The champion is always the `bench`-signed tip of the main development branch.
4. Correctness gates → bench → STC SPRT → LTC SPRT (for gainers).
5. Decision: **accepted**, **rejected** or **inconclusive**, with the evidence linked. Rejected code is kept on its branch and never silently deleted.

## Template
```
## EXP-<nnn>: <title>   [status: planned|running|accepted|rejected|inconclusive]
Hypothesis:
Mechanism / why it could gain Elo:
Expected effect & cost:
Test plan & stop rule:
Result (link BENCHMARKS entry):
Analysis / what we learned:
```

## Queue (M5 search stack; one SPRT each, against the current champion)

Champion at queue start: M3 baseline (bench 8689072, depth 6). Bounds [0, 10] at STC unless noted (see ARCHITECTURE §7.1).
Cost estimate: 2–6 h of 4-core time per large-gain test; LTC confirmation only for changes that pass with a small margin.

| ID | Change | Hypothesis / mechanism | Expected |
|---|---|---|---|
| EXP-001 | Null-move pruning (adaptive R, no NMP in pawn-only endgames) | Cuts most non-PV fail-high subtrees; effective depth +2–3 plies | Large (+80 Elo or more) |
| EXP-002 | Killer moves + butterfly history ordering of quiets | Better ordering means more cutoffs and a smaller tree | Large |
| EXP-003 | Late move reductions (log·log formula) | Depends on EXP-002 ordering; the biggest single depth gain | Large |
| EXP-004 | Reverse futility pruning | Shallow non-PV nodes with eval far above beta return early | Medium–large |
| EXP-005 | Aspiration windows | Narrower root windows mean faster iterations | Medium |
| EXP-006 | Check extension (and qsearch check evasions already present) | Fewer horizon errors in forcing lines | Medium; may be subsumed later |
| EXP-007 | Late move pruning + futility pruning | Skip hopeless quiets at low depth | Medium |
| EXP-008 | SEE pruning (main search + qsearch) | Skip losing captures and quiets that hang material | Medium |
| EXP-009 | TT buckets and replacement policy | Better retention of deep entries | Small–medium |
| EXP-010 | Continuation history (1- and 2-ply) | Strongest ordering signal in modern engines | Medium |
| EXP-011 | Internal iterative reduction | Cheaper than IID for nodes with no TT move | Small–medium |
| EXP-012 | Singular extensions (with multi-cut return) | Extend forced moves; prune via multi-cut | Medium at LTC |
| EXP-013 | Capture history | Better capture ordering than MVV-LVA alone | Small–medium |
| EXP-014 | Correction history (pawn-key) | Online correction of systematic eval bias | Medium |

Each row becomes a full entry (template above) when it starts.

---

## EXP-001: Null-move pruning   [status: accepted]
Hypothesis: adding null-move pruning to the M3 baseline gains a lot of strength at STC.
Mechanism / why it could gain Elo: in non-PV nodes where static eval ≥ beta, letting the opponent move twice and still failing high at depth − R proves a cutoff cheaply. This prunes most refutation subtrees and gives roughly 2–3 plies more depth at equal time.
Implementation: R = 3 + depth/3 + min((eval − beta)/200, 3); requires depth ≥ 3, not in check, not a PV node, previous move not null, and side to move has a non-pawn piece (zugzwang guard). Mate scores from the null search are clamped to beta.
Bench: 8689072 → 3521991 (−59% nodes to depth 6).
Expected effect & cost: +80 Elo or more; SPRT [0, 10] at STC; about 1–3 h on 3 game slots.
Test plan & stop rule: fastchess, 8+0.08, 16 MB, UHO_Lichess_4852_v1, pentanomial SPRT [0, 10] normalized Elo, α = β = 0.05, max 20k games, seed 101.
Result: **H1 accepted** after 266 games (LLR 2.97). Elo +134.3 ± 36.2 (nElo +171.7 ± 41.8); W/L/D 151/53/62; pentanomial [3, 11, 45, 33, 41]; 0 time forfeits. See BENCHMARKS.md "EXP-001".
Analysis: as expected, null-move pruning is a very large gain over a pure alpha-beta baseline. The gain resolved quickly at [0, 10]. The zugzwang guard (non-pawn material) stays; a verification search can be tested later if endgame suites show zugzwang errors.

---

## EXP-002: Killer moves + butterfly history   [status: accepted]
Hypothesis: ordering quiet moves by killers (2 per ply) and a gravity-bounded butterfly history improves cutoff rates, so the tree is smaller and the search reaches more depth.
Implementation: killers scored just below captures; history[color][from][to] updated on quiet beta cutoffs with bonus min(16d² + 32d, 1200) and a malus for quiets tried before the cutoff; gravity h += b − h·|b|/16384. Tested on top of EXP-001.
Bench: 3521991 → 2017210.
Test plan: same as EXP-001, seed 102, base = EXP-001 champion.
Result: **H1 accepted** after 284 games (LLR 2.99). Elo +125.0 ± 35.1 (nElo +157.6 ± 40.4); W/L/D 154/56/74; pentanomial [4, 14, 45, 38, 41]; 0 time forfeits.
Analysis: much larger than typical reported gains for these heuristics in mature engines, which is expected because the baseline ordered quiets in generation order. It also prepares the ground for LMR (EXP-003), which depends on good quiet ordering.
