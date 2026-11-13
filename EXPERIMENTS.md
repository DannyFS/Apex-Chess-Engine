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
