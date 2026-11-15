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

---

## EXP-003: Late move reductions   [status: accepted]
Hypothesis: with history-based quiet ordering (EXP-002), late quiet moves rarely raise alpha, so searching them at reduced depth (with a full-depth re-search on fail-high) saves a lot of effort at little risk.
Implementation: R = int(0.75 + ln(d)·ln(m)/2.25) for quiet, non-checking moves when not in check, depth ≥ 3 and the move is beyond the first (two in PV nodes); −1 in PV nodes, −1 for killers; clamped to [0, d − 2]. Null-window reduced search → full-depth null-window re-search → PV re-search.
Bench: 2017210 → 571441.
Result: **H1 accepted** after 216 games (LLR 2.96). Elo +188.7 ± 41.1 (nElo +259.5 ± 46.3); W/L/D 134/27/55; pentanomial [1, 4, 30, 33, 40]; 0 time forfeits.
Analysis: the largest single gain so far, consistent with LMR being the dominant depth multiplier in modern engines. The formula and adjustments are untuned (SPSA candidate in M10); history-based reduction adjustments are a later experiment.

---

## EXP-004: Reverse futility pruning   [status: accepted]
Hypothesis: at shallow depth in non-PV nodes, a static eval far above beta almost always holds, so returning early saves effort.
Implementation: depth ≤ 8, not in check, non-PV, |beta| not a mate score, static_eval − 80·depth ≥ beta → return static_eval. No "improving" term yet (later experiment).
Bench: 571441 → 405629.
Result: **H1 accepted** after 398 games (LLR 2.96). Elo +74.5 ± 25.7 (nElo +102.3 ± 34.1); W/L/D 173/89/136; pentanomial [6, 26, 79, 54, 34]; 0 time forfeits.
Analysis: a solid gain, smaller than NMP/LMR as expected. The margin (80/ply) is untuned and depends on the eval scale, so it must be re-checked when NNUE replaces the HCE (see RESEARCH §1.16).

---

## EXP-005: Aspiration windows   [status: accepted]
Hypothesis: starting each iteration from depth 4 with a narrow window around the previous score makes iterations cheaper, because most nodes get tighter bounds.
Implementation: window ±20 cp; on fail-low, beta = (alpha + beta)/2 and alpha = score − delta; on fail-high, beta = score + delta; delta grows ×1.5; full window once delta > 1000.
Bench: 405629 → 367806.
Result: **H1 accepted** after 1264 games (LLR 2.95). Elo +25.6 ± 14.7 (nElo +33.5 ± 19.2); W/L/D 464/371/429; pentanomial [45, 123, 229, 164, 71]; 1 time forfeit by the baseline (see BENCHMARKS.md).
Analysis: the first "medium" gain. It needed about 4× as many games as EXP-001 to 004, as the 4-core budget table predicted. The window size and growth factor are untuned (SPSA candidates in M10).

---

## EXP-006: Check extension   [status: inconclusive, not merged]
Hypothesis: extending checking moves by one ply reduces horizon errors in forcing lines.
Implementation: new_depth = depth − 1 + gives_check, for every checking move (no SEE condition, no extension limit).
Bench: 367806 → 471163 (+28% nodes: extensions grow the tree).
Result: **inconclusive** at the 3000-game cap (methodology v1.1). Elo +7.5 ± 9.6 (nElo +9.8 ± 12.4), LLR ≈ 1.19. Run 1 was aborted by a container restart and discarded; run 2 was resumed once after a restart. See BENCHMARKS.md.
Analysis: probably a small positive effect, too small to confirm at [0, 10] on this hardware. An unconditional check extension is crude; modern engines extend only some checks (for example, SEE ≥ 0 checks) or get the effect through singular extensions (EXP-012). Retry candidates: SEE-gated check extension, or re-testing after EXP-012. EXP-007 and later experiments were rebased onto the champion without it.

---

## EXP-007: Late move pruning + futility pruning   [status: accepted]
Hypothesis: at shallow depth in non-PV nodes, late quiet moves, and quiet moves that cannot lift a low static eval up to alpha, almost never matter. Skipping them saves effort.
Implementation: LMP: skip quiets once legal > 3 + depth² (depth ≤ 6). Futility: skip quiets when static_eval + 100 + 100·depth ≤ alpha (depth ≤ 6). Both only in non-PV nodes, not in check, for non-checking quiets, after at least one move has been searched. Tested on the EXP-005 champion (rebased off the inconclusive EXP-006).
Bench: 367806 → 283803.
Result: **H1 accepted** after 286 games (LLR 2.99). Elo +118.6 ± 32.2 (nElo +160.8 ± 40.3); W/L/D 138/44/104; pentanomial [3, 15, 42, 51, 32]; 0 time forfeits.
Analysis: much larger than expected ("medium"). The untuned HCE probably makes many quiet moves look similar, so cutting them at low depth frees a lot of time for depth. Both pruning rules are tied together in this test; an ablation (LMP only / futility only) is queued as a low-priority diagnostic. The margins are eval-scale dependent and must be re-checked after NNUE.

---

## EXP-008: SEE pruning (main search + qsearch)   [status: accepted]
Hypothesis: moves that lose material by static exchange rarely help at shallow depth, and losing captures almost never help in qsearch. Skipping them cuts a large part of the tree.
Implementation: main search: non-root, not in check, depth ≤ 8, after one move searched; skip quiets with SEE < −50·depth and captures with SEE < −100·depth. Qsearch: not in check, skip captures with SEE < 0. Tested on the EXP-007 champion.
Bench: 283803 → 92972 (−67%; most of the reduction is from qsearch).
Result: **H1 accepted** after 320 games (LLR 2.95). Elo +96.9 ± 27.6 (nElo +141.0 ± 38.1); W/L/D 148/61/111; pentanomial [4, 15, 56, 60, 25]; 0 time forfeits.
Analysis: large, as qsearch explosion is a known weakness of MVV-LVA-only qsearch. Main-search and qsearch parts are not separated in this test (ablation is a low-priority diagnostic). The thresholds are untuned.

---

## EXP-009: TT buckets and replacement policy   [status: rejected]
Hypothesis: 4-entry cache-line buckets with depth/age replacement keep deep entries longer than a single always-replace slot, so the search reuses more work.
Implementation: 64-byte buckets of 4 × 16-byte entries; same-key updates keep the old rule; new keys evict an empty slot first, else the lowest depth − 8·age.
Bench: unchanged (92972); a 1 MB depth-14 start-position search confirmed the different behaviour.
Result: **H0 accepted** after 2806 games (LLR −2.98). Elo −5.8 ± 9.6. See BENCHMARKS.md.
Analysis: no gain at STC with 16 MB. Likely reasons: (1) at 8+0.08 the engine searches about 1M nodes per move, so a 1M-entry table is under little pressure and replacement policy barely matters; (2) the 4-way probe costs a little speed; (3) "depth − 8·age" evicts shallow but recent entries that are useful for move ordering near the leaves. Retry candidates: re-test at LTC (64 MB, more pressure per entry) or under a smaller hash; try 3-entry buckets with 10-byte entries. The branch is kept.

---

## EXP-010: Continuation history   [status: inconclusive, not merged]
Hypothesis: indexing quiet-move history by the previous one and two moves (piece, to-square) captures follow-up patterns that butterfly history misses, so ordering and cutoffs improve.
Implementation: cont[prev piece][prev to][piece][to] (int16, heap) for 1 and 2 plies back, added to butterfly history in quiet scoring; same bonus/malus and gravity. Null moves and the root use a sentinel row.
Bench: 92972 → 90986.
Result: **inconclusive** at the 3000-game cap. Elo +3.0 ± 9.4, LLR −0.25. See BENCHMARKS.md.
Analysis: in modern engines continuation history is a major ordering signal, but here it does not show up at this resolution. Likely reasons: (1) it is used only for ordering, not in LMR/pruning decisions, where most of its value comes from in strong engines; (2) with LMP pruning late quiets anyway, ordering among quiets matters less; (3) the HCE is noisy. Retry candidate after M6/NNUE: continuation history combined with history-based LMR adjustments. The code is kept as a patch.

---

## EXP-015: Texel-tuned evaluation (M6 v1)   [status: accepted]
Hypothesis: the hand-written M3 evaluation (material + PST + bishop pair) is far from optimal. Adding standard positional terms and fitting all weights to game results from our own self-play gives a large gain.
Implementation:
- Evaluation refactor: every term is a (mg, eg) weight times an integer feature count (src/eval/eval_layout.h, src/eval/weights.inc). New terms: mobility tables per piece (safe squares: not own pieces, not attacked by enemy pawns), passed pawns by rank, doubled, isolated, rook on open/semi-open file, king shelter pawns, king-zone attacks per piece type. With the hand-written weights and zero weights for the new terms, the refactor is identical to the old eval (bench 92972 unchanged).
- Data: tools/texel/extract.py took 1,072,299 unique quiet positions (≥ 8 plies after the book, not in check, the move played was not a capture/promotion) from all 12,032 Apex self-play games so far (validation and SPRT PGNs). Labels are game results. No external engine data (ARCHITECTURE §6).
- Tuning: tools/texel/tune.py; K fitted then frozen (1.269); Adam, 3000 iterations, 10% held out; L2 penalty 5e-9 toward the start weights. Without the penalty (run v1) rare features overfit (for example queen mobility 25 got +825 mg); v1 was never played.
Bench: 92972 → 92341.
Result: **H1 accepted** after 176 games (LLR 2.96). Elo +312.4 ± 58.4 (nElo +459.4 ± 51.3); W/L/D 138/12/26; pentanomial [0, 1, 12, 23, 52]; 0 time forfeits.
Analysis: by far the largest single gain, confirming that evaluation quality was the main weakness after the M5 search work. Consequences:
1. All pruning margins (RFP 80/ply, futility 100+100·d, SEE thresholds, aspiration δ) were set for the old eval scale. The tuned eval has larger piece values (queen 974/938 vs 980/960, but much larger positional swings), so the margins should be re-tested.
2. Experiments that were inconclusive with the old eval (EXP-006 check extension, EXP-010 continuation history) may behave differently now.
3. The next Texel round should use games played by this stronger engine (more and better data); this is cheap to repeat, because every SPRT produces thousands of games.

---

## EXP-014: Pawn-structure correction history   [status: inconclusive, re-test candidate]
Hypothesis: the static eval has systematic errors per pawn structure that the search can measure. Learning a per-pawn-key correction online makes pruning decisions (RFP, NMP, futility) and qsearch stand-pat more accurate.
Implementation: corr[stm][pawn_key & 16383] (scaled by 256) added to the static eval; updated at node end with weight min(depth + 1, 16) toward best − static_eval, only when not in check, the best move is quiet or none, the score is not a mate score, and the bound is consistent with the sign of the error.
Bench: 92341 → 110102.
Result: **inconclusive** at the 3000-game cap: Elo +9.6 ± 8.8 (nElo +13.6 ± 12.4), LLR 2.14 (73% of the way to H1). See BENCHMARKS.md.
Analysis: very likely a real gain of about +5 to +15 Elo, below what [0, 10] can confirm in 3000 games. Plan: an independent confirmation test after the current queue (fresh seed), possibly with bounds [0, 5] and a larger cap, or bundled with a Texel re-tune if both pass separately.

---

## EXP-012: Singular extensions with multi-cut   [status: accepted]
Hypothesis: when the TT move is much better than every alternative (singular), it is a forced move and should be searched one ply deeper. When even the alternatives beat beta at reduced depth, the node has several refutations and can be cut (multi-cut).
Implementation: at depth ≥ 8, for the TT move with a lower-bound entry of depth ≥ d − 3 and a non-mate score: search all other moves at depth (d − 1)/2 with a null window at singular_beta = tt_value − 2d (the TT move is excluded via excluded[ply]). All fail low → extension 1; singular_beta ≥ beta → return singular_beta. RFP, NMP, TT cutoffs and TT stores are disabled inside the excluded-move search; a node whose only legal move is excluded returns alpha.
Bench: depth 6 unchanged (92341); depth 9 494602 → 685369 (measured on the EXP-008 base; on EXP-015 the champion value is from this entry).
Result: **H1 accepted** after 770 games (LLR 2.95). Elo +37.6 ± 17.9 (nElo +51.9 ± 24.5); W/L/D 258/175/337; pentanomial [15, 79, 143, 104, 44]; 0 time forfeits.
Analysis: a clear gain even at STC (usually reported as larger at longer time controls). It also suggests the earlier, crude check extension (EXP-006) mostly targeted the same forced lines. Untuned parameters (depth 8, margin 2d, half-depth verification) are SPSA candidates; double extensions and negative extensions are later experiments.
