# RESEARCH.md: State of the Art in Computer Chess (Stage 2 report, v0.1)

**Status:** first research pass, 2026-11-13. This is a living document. Each claim is tagged:

- **[EST]** Established. Long-standing technique, published and/or used by every top engine.
- **[COMM]** Community knowledge. Proven in strong engines but documented mainly in source code, forum threads or the Chessprogramming Wiki, not in peer-reviewed papers.
- **[HYP]** Hypothesis. Our own conjecture; it must be tested before we rely on it.
- **[VERIFY]** I believe this is correct but have not yet confirmed it against a primary source in this project.

Elo figures from other projects are their own measurements on their own hardware. They show orders of magnitude and are not predictions for our engine.

---

## 0. Executive summary

1. **The dominant paradigm since 2020 is CPU alpha-beta (PVS) search with an efficiently updatable neural network (NNUE) evaluation.** Stockfish switched to NNUE in version 12 (Sept 2020) and gained about 130 Elo from that change alone [S-SF12]. Every engine in the top tier of current rating lists follows this pattern.
2. **NNUE keeps evolving.** Stockfish 18 added *threat input* features (SFNNv10). Stockfish 19 (2026) replaced some threat features with pawn-pair features (SFNNv16) and retired the secondary "small" network that was introduced in 16.1 [S-SF18, S-SF19]. Input-feature engineering is still an active source of Elo.
3. **GPU neural MCTS (AlphaZero / Leela) is competitive at the very top, but only with very large networks trained on billions of self-play games.** Transformer policy/value nets ([P-MONROE24]) improved this considerably. For our purposes, Leela's strength depends on the GPU and on years of distributed training compute that we do not have.
4. **Searchless transformers reach grandmaster level** ([P-RUOSS24]: 270M parameters, Lichess blitz Elo about 2895 against humans). That is far below top engines, so this approach is relevant to research but not to maximising strength.
5. **For a single developer workstation (RTX 5060 Ti 16 GB plus a desktop CPU), the evidence strongly favours CPU alpha-beta search with our own NNUE trained on the GPU.** The GPU's best use is **training and data generation**, not in-search inference. See §5.
6. **The decisive long-term factor is not any single algorithm. It is (a) testing throughput (SPRT games per day) and (b) training-data quality and volume.** The engines that improve fastest have the best testing infrastructure.

---

## 1. Search algorithms

Each technique below lists its purpose, mechanism, cost, implementations, limitations and how we will test it. "All top engines" means at least Stockfish, Ethereal, Berserk, Obsidian, PlentyChess, Reckless and Stormphrax, based on their public sources [VERIFY per engine before citing specifics].

### 1.1 Negamax alpha-beta [EST]
- **Purpose:** an exact minimax value with pruning of provably irrelevant subtrees.
- **Mechanism:** `score = -search(child, -beta, -alpha)`. A cut occurs when `score >= beta`. With perfect move ordering the minimal tree has about `b^⌈d/2⌉ + b^⌊d/2⌋ − 1` leaf nodes, against `b^d` for plain minimax [P-KNUTH75].
- **Cost/limitation:** the efficiency depends entirely on move ordering. Search can stop only on an exact bound.
- **Test:** perft-validated movegen, then compare against brute-force minimax on small depths (the scores must be identical).

### 1.2 Principal Variation Search / NegaScout [EST]
- **Purpose:** prove that non-PV moves are worse using null-window searches `(alpha, alpha+1)`, which are cheaper than full-window searches.
- **Mechanism:** search the first move with the full window and the rest with a zero window. Re-search with the full window if `alpha < score < beta` [P-REINEFELD83].
- **Cost:** re-searches. These are rare when ordering is good.
- **Test:** node count to fixed depth on a bench set, then SPRT.

### 1.3 Iterative deepening and aspiration windows [EST]
- **ID:** search depth 1, 2, …, n. The previous iteration's PV, TT entries and history scores seed the ordering. ID also gives the anytime behaviour that time management needs.
- **Aspiration:** search around the previous score with window `[s−δ, s+δ]`. On a fail-low or fail-high, widen the window, typically by geometric growth of δ. Modern engines start δ at about 10–20 cp and make it depend on the score's volatility [COMM].
- **Limitation:** failing repeatedly in unstable positions. Interactions with the TT can cause *search instability*: different results on a re-search.

### 1.4 Transposition table and Zobrist hashing [EST]
- **Zobrist:** 64-bit XOR of random keys per (piece, square), side to move, castling rights and en-passant file, updated incrementally on make/unmake [P-ZOBRIST70]. The collision probability per probe is about `entries/2^64`. In practice, store 16 bits of the key and verify that the TT move is legal before using it.
- **TT:** stores (key, move, score, static eval, depth, bound type, age). It provides cutoffs, move ordering and evaluation caching. Modern designs use buckets of 3 entries in 32 bytes, an aging and replacement policy (depth plus age), lockless access, and prefetch on make.
- **Pitfalls:** mate scores must be adjusted by ply when stored and probed. TT cutoffs in PV nodes cause PV truncation and graph-history-interaction (GHI) errors around repetitions.
- **Test:** hit rate, `hashfull`, the Elo effect of the replacement policy, and stress tests for correctness under threads.

### 1.5 Null-move pruning (NMP) [EST]
- **Idea:** let the opponent move twice. If `search(null, depth−1−R)` still fails high, the real position almost certainly does too [P-DONNINGER93].
- **Modern form:** adaptive `R = 3 + depth/3 + min((eval−beta)/200, 3)` or similar [COMM]. Only when `eval >= beta`, the side has non-pawn material (zugzwang risk), and not in check. Verification search at high depth.
- **Limitation:** zugzwang (pawn endings). Interacts with singular extensions and with the TT.

### 1.6 Late Move Reductions (LMR) [EST/COMM]
- **Idea:** with good ordering, late moves rarely raise alpha, so search them at reduced depth and re-search at full depth if they beat alpha.
- **Modern form:** `R = base + log(depth)·log(moveNumber)/C`, adjusted by history score, PV/non-PV node, improving flag, cut-node status, TT-move capture and similar. This is one of the largest single Elo sources in modern engines [COMM]. Stockfish and others use a fractional (1/1024-ply) reduction granularity.
- **Test:** heavily parameterised, so tune with SPSA (§7.2).

### 1.7 Futility, reverse futility (static null move) and razoring [EST/COMM]
- **Reverse futility pruning (RFP):** at shallow depth in non-PV nodes, if `staticEval − margin(depth, improving) >= beta`, return early.
- **Futility pruning:** at shallow depth, skip quiet moves when `staticEval + margin(depth) <= alpha` [P-HEINZ98].
- **Razoring:** if `eval + margin(depth) < alpha` at low depth, drop into quiescence search.
- **Limitation:** these rely on a well-calibrated eval. Margins must be retuned whenever the network changes.

### 1.8 Late move pruning (LMP), SEE pruning and history pruning [COMM]
- **LMP:** skip quiet moves after N moves at depth d, with `N ≈ (3+d²)/(2−improving)` or similar.
- **SEE pruning:** skip moves whose static exchange evaluation is below `−k·depth`.
- **History pruning:** skip quiets with very negative history scores at low depth.

### 1.9 Extensions: check, singular and multi-cut [EST/COMM]
- **Check extension** [EST]. Many modern engines have *removed* a blanket check extension because SE and LMR adjustments subsume it [COMM, VERIFY].
- **Singular extension** [P-ANANTHARAMAN90]: if the TT move is much better than all alternatives, which is shown by a reduced-depth search excluding it failing low at `ttScore − margin`, extend it by 1 ply. Modern engines add *double and triple extensions* and *negative extensions*.
- **Multi-cut** [P-BJORNSSON01]: if several moves fail high at reduced depth, prune. In modern engines this mostly appears as a by-product of singular search (when `singularBeta >= beta`, return early) [COMM].
- **Risk:** search explosion. Requires a limit on the extension count per path.

### 1.10 Internal iterative deepening and reductions (IID → IIR) [COMM]
- Classic IID ran a shallow search to find a move when there was no TT move. The modern *IIR* (internal iterative reduction) simply reduces depth by 1 when there is no TT move. It is cheaper and tests better in many engines.

### 1.11 Quiescence search [EST]
- Searches captures (and promotions; optionally checks at the first qsearch ply) until the position is quiet, using a stand-pat lower bound. This mitigates the horizon effect.
- **Pruning:** delta pruning, SEE-negative capture pruning, and a TT probe in qsearch.

### 1.12 Move ordering [EST/COMM]
Order: TT move → good captures (MVV-LVA plus capture history, filtered by SEE) → killers [P-AKL77] → countermove → quiets by history → bad captures.

Histories:
- **Butterfly/main history** [P-SCHAEFFER89]: indexed `[side][from][to]`, with *gravity* updates `h += bonus − h·|bonus|/MAX`.
- **Continuation histories** [COMM]: indexed `[prevPiece][prevTo][piece][to]` for plies −1, −2, −4 (and −6). These are among the strongest ordering signals in modern engines.
- **Capture history:** indexed `[piece][to][capturedType]`.
- **Pawn-structure history and threat-aware history** (indexing by whether the from/to square is attacked) [COMM].

### 1.13 Correction history [COMM]
- **Purpose:** learn the systematic error of the static evaluation online during search, keyed by pawn structure, material, non-pawn piece configuration and continuation.
- **Mechanism:** after a search, if the search score differs from the static eval and the bound type allows it, update `corr[key] += w·(searchScore − staticEval)` with gravity. The adjusted eval feeds every pruning decision (RFP, NMP, futility).
- **History:** introduced in Stockfish in 2023 and adopted widely since. It is reported as a large Elo gainer across engines [COMM, VERIFY original author/PR].
- **Test:** an early candidate in our improvement loop, ablated per key type.

### 1.14 Search instability and the horizon effect [EST]
Sources include TT-graph interactions (GHI), repetition and fifty-move-rule path dependence, aggressive pruning, and asymmetric extensions. Mitigations: no TT cutoffs in PV nodes; a fifty-move-rule-aware TT score (or no TT cutoff near the 50-move limit); upcoming-repetition detection (the cuckoo-table method, used in Stockfish [COMM]); and capped extensions.

### 1.15 Parallel search: Lazy SMP [COMM]
Threads search the same root independently and share the TT. Depth or move-ordering diversity among threads gives an effective speedup. Every top CPU engine uses it. It is simple and robust, and it scales well to about 64+ threads. A known cost: results are **non-deterministic** with more than one thread, so all SPRT testing is done single-threaded except for dedicated SMP tests.

### 1.16 Interaction warnings (from experience reported across engines) [COMM]
- NMP plus SE plus the TT can over-prune in zugzwang-like positions.
- LMR depends on history quality. Changing the history schemes changes the optimal LMR parameters.
- Every pruning margin depends on the evaluation scale. A new network can silently break tuned margins, so the network's output scale must be normalised to a fixed reference (for example "100 = 50% win probability at material+1 pawn"). Stockfish normalises its eval to a win-rate model [COMM, VERIFY exact definition].

**Essential (tier 1):** PVS, ID, aspiration, TT, qsearch, NMP, LMR, RFP, killers/history, MVV-LVA/SEE.
**Strong complements (tier 2):** continuation history, singular extensions, LMP, SEE pruning, IIR, correction history, capture history.
**Refinements (tier 3):** multi-cut via SE, double/triple/negative extensions, cuckoo repetition detection, threat-aware histories, fractional LMR, and many tuned conditions.

---

## 2. Evaluation

### 2.1 Handcrafted evaluation (HCE) [EST]
Material, tapered piece-square tables (middlegame/endgame interpolated by phase), mobility, king safety (attack units, pawn shield, open files), pawn structure (doubled, isolated, backward, connected, passed with rank- and blocker-dependent bonuses), threats, space, and endgame scaling. Tuning: *Texel tuning* minimises `Σ (result − σ(K·eval))²` over labelled positions [COMM, CPW Texel's Tuning Method]. Strong HCE engines (for example Ethereal before its NNUE versions, and Stockfish 11) reached about 3300–3450 CCRL [VERIFY figures].

**Role in this project:** HCE is a **bootstrap evaluator only**, used to generate the first self-play data. It will not be the final evaluation.

### 2.2 NNUE [EST]
- **Origin:** Yu Nasu (2018) for computer shogi [P-NASU18]. Ported to Stockfish in 2020 [S-SF12].
- **Key idea:** the first layer is a very wide, very sparse linear layer over piece-square features. Because a move changes only 2–4 features, its output (the *accumulator*) is **updated incrementally**: add or subtract a few weight columns instead of a full matrix product. The deeper layers are tiny and run in integer SIMD. A full eval costs a few hundred nanoseconds or less on a modern CPU, which is low enough for millions of evals per second per core.
- **Feature sets:**
  - *768* (piece × square × colour): simplest.
  - *HalfKP / HalfKA* (king-square × piece × square, per perspective): richer, with larger tables. Stockfish uses HalfKAv2_hm (horizontally mirrored king buckets) [COMM, nnue-pytorch docs].
  - *King buckets* (a few groups of king squares): the common compromise in engines other than Stockfish.
  - *Threat inputs* (Stockfish 18, SFNNv10): (attacker piece, square) → (attacked piece, square) pairs [S-SF18].
  - *Pawn-pair features* (Stockfish 19, SFNNv16) [S-SF19].
- **Network shapes:** typical non-Stockfish engines use `(768×KB) → N (perspective, ×2) → 1` with N = 512–2048, SCReLU activation, and **output buckets** by material count. Stockfish uses a multi-layer head (L1 → 15/16 → 32 → 1) with *PSQT* skip outputs and layer stacks per bucket [COMM, nnue-pytorch].
- **Inference tricks:** int16 accumulators; int8 or int16 weights; lazy accumulator updates (only when an eval is needed); "Finny tables" (accumulator caches per king-bucket refresh) [COMM]; and AVX2/AVX-512/VNNI kernels.
- **Cost:** O(changed features × N) per move plus O(N) per eval. The memory footprint is tens to low hundreds of MB.

### 2.3 Is HCE+NNUE hybrid evaluation worth it? [HYP: probably not]
Stockfish removed its classical evaluation entirely in version 16 (2023) [VERIFY]. A hybrid is unlikely to help unless the net is weak. We will not pursue it beyond the bootstrap phase.

### 2.4 Network training data [EST/COMM]
- **Labels:** blend of game result (WDL) and search score, `target = λ·σ(score/scale) + (1−λ)·result` [COMM]. Typical λ is around 0.5–1.0. Engines report that this matters a lot.
- **Data volume:** competitive nets are trained on billions of positions. Strong hobby nets (2024–26) use about 1–5 billion positions from self-play at around 5k nodes per move [COMM, VERIFY].
- **Filtering:** remove positions in check, positions whose best move is a capture or promotion (non-quiet), and early opening plies. Deduplicate.
- **Iteration:** net_n generates data → train net_n+1 → SPRT test → repeat. Gains shrink each generation but stay positive for many generations [COMM].
- **Overfitting to a teacher:** labelling with Stockfish or Leela gives fast strength but imports their biases and raises *licensing and originality* concerns. Many rating lists and tournaments (CCRL, TCEC) scrutinise derived nets. **Our policy:** own self-play data only, starting from zero or HCE knowledge (see ARCHITECTURE.md §6).

---

## 3. Alternative architectures

### 3.1 AlphaZero-style neural MCTS [EST]
- Policy-value network `f(s) → (p, v)`. PUCT selection `a* = argmax Q(s,a) + c·P(s,a)·√N(s)/(1+N(s,a))` [P-ROSIN11, P-SILVER17]. Trained purely by self-play reinforcement learning.
- **AlphaZero:** 5,000 first-generation TPUs for self-play and 64 second-generation TPUs for training; it beat Stockfish 8 under the match conditions it reported [P-SILVER17, P-SILVER18].
- **Leela Chess Zero:** an open reproduction with distributed volunteer training since 2018 and transformer networks since 2022–24 [S-LC0, P-MONROE24].
- **Cost:** about 10³–10⁵ nodes per second on a consumer GPU versus 10⁶–10⁸ for CPU alpha-beta. It compensates through much higher per-node quality.
- **For us:** competitive strength needs the network that Leela already has. Training one ourselves would take orders of magnitude more compute than one GPU provides. Using Leela's nets would make us a Leela client, not a new engine. **Not chosen as the primary architecture.**

### 3.2 MCTS with CPU networks (Monty) [COMM]
Monty ([S-MONTY]) runs PUCT on the CPU with small value and policy networks, trained with the same tools as NNUE engines. It shows that MCTS can reach strong-engine strength on a CPU, but it remains well behind top alpha-beta engines [VERIFY current rating]. It is relevant as a *research comparison*, not as our main line.

### 3.3 Transformers and searchless play [EST research]
- [P-RUOSS24] DeepMind: a 270M-parameter transformer trained on Stockfish-annotated data plays about 2895 Lichess blitz Elo against humans without search, and it degrades against engines.
- [P-MONROE24] ChessFormer: position-encoding choices matter. It reports beating AlphaZero's playing strength at 8× less compute.
- **For us:** these show that large networks can encode a lot of chess, but the inference cost per position (milliseconds on a GPU) is incompatible with alpha-beta node rates.

### 3.4 Neural guidance inside alpha-beta [HYP]
- **Policy-net move ordering:** in principle better ordering means more cutoffs, so more effective depth. In practice, history heuristics are already very good and nearly free, and any per-node network cost must pay for itself in node rate. A *tiny CPU policy head* at high-depth nodes only (for example depth ≥ 8, where the cost is amortised) is a plausible experiment. Some engines have experimented with this [VERIFY].
- **GPU batching in alpha-beta:** alpha-beta is fundamentally sequential, since each cutoff decision depends on the previous result. Batching requires speculative evaluation. PCIe round-trip latency (≥ 5–20 µs) is about 100× an NNUE eval. **[HYP, strongly expected negative]**. We may measure it once to put a number on the record, but we expect it to lose.
- **Learned pruning and search control:** use offline-learned features to predict LMR and pruning decisions. Plausible, but SPSA-tuned formulas are a strong baseline. This is a later-stage research item.

### 3.5 Distillation and quantisation [EST]
- Quantisation-aware training (int8/int16) is standard for NNUE [COMM, nnue-pytorch, bullet].
- Distilling a large GPU network (policy/value) into an NNUE is plausible as a *data-labelling* strategy: a big GPU net labels positions, and the NNUE learns from it. This is our best potential use of the GPU *beyond* plain training and is a **Stage-7 research hypothesis**.

---

## 4. Testing methodology (established practice)

- **SPRT** [P-WALD45] with the GSPRT approximation and **pentanomial** game-pair statistics, as used in Fishtest and OpenBench [S-FISHTEST, S-OPENBENCH]. Each opening is played twice with colours reversed, and the pair is the unit of observation. This reduces variance substantially compared with treating each game separately.
- **Typical bounds:** gainers `[0, 3]` or `[0, 5]` normalised Elo; simplifications `[−3, 1]` (non-regression). α = β = 0.05.
- **Time controls:** STC 8s+0.08s and LTC 40s+0.4s (single thread), with a hash size that scales with the time control [COMM, OpenBench defaults VERIFY].
- **Openings:** large unbalanced books (UHO: "unbalanced human openings") reduce draw rates and increase test sensitivity [S-SFBOOKS].
- **Runners:** fastchess [S-FASTCHESS] (preferred; the modern runner used by OpenBench and Stockfish, with pentanomial reporting) and cutechess-cli [S-CUTECHESS].
- **Bench signature:** each commit records the node count of a fixed-depth search over a fixed position set. This catches unintended functional changes (OpenBench convention).
- **Rating lists for absolute calibration:** CCRL [S-CCRL], CEGT, SP-CC. They differ in time controls and hardware, so we report against one list consistently.

## 5. Hardware implications (summary; details in ARCHITECTURE.md §2)

- NNUE inference is CPU-bound and latency-critical. This favours a CPU with AVX2 (minimum), AVX-512/VNNI where available, and as many cores as possible for **parallel test games**, not only for SMP search.
- The RTX 5060 Ti (Blackwell, 16 GB) is very well suited to **NNUE training** (bullet supports CUDA) and adequate for experimenting with small policy/value networks. Blackwell (sm_120) needs CUDA 12.8 or later [VERIFY toolchain specifics on the dev machine].
- PCIe latency rules out per-node GPU evaluation inside alpha-beta.

---

## 6. Bibliography

Status: ✔ = confirmed in this session via search or direct knowledge of a stable identifier (arXiv ID, DOI-like venue); ◐ = well-known reference whose exact link or detail is still to be re-checked.

### Papers
| Key | Reference | Status | Summary |
|---|---|---|---|
| P-KNUTH75 | D. E. Knuth, R. W. Moore, "An Analysis of Alpha-Beta Pruning", *Artificial Intelligence* 6(4), 1975 | ◐ | Formal analysis of alpha-beta; minimal-tree size. |
| P-ZOBRIST70 | A. L. Zobrist, "A New Hashing Method with Application for Game Playing", Tech. Rep. 88, Univ. of Wisconsin, 1970 | ◐ | Zobrist hashing. |
| P-REINEFELD83 | A. Reinefeld, "An Improvement to the Scout Tree Search Algorithm", *ICCA Journal* 6(4), 1983 | ◐ | NegaScout/PVS. |
| P-AKL77 | S. Akl, M. Newborn, "The Principal Continuation and the Killer Heuristic", ACM Annual Conf., 1977 | ◐ | Killer moves. |
| P-SCHAEFFER89 | J. Schaeffer, "The History Heuristic and Alpha-Beta Search Enhancements in Practice", *IEEE TPAMI* 11(11), 1989 | ◐ | History heuristic; measured enhancement interactions. |
| P-ANANTHARAMAN90 | T. Anantharaman, M. Campbell, F. Hsu, "Singular Extensions: Adding Selectivity to Brute-Force Searching", *Artificial Intelligence* 43(1), 1990 | ◐ | Singular extensions (Deep Thought). |
| P-DONNINGER93 | C. Donninger, "Null Move and Deep Search", *ICCA Journal* 16(3), 1993 | ◐ | Recursive null-move pruning. |
| P-HEINZ98 | E. A. Heinz, "Extended Futility Pruning", *ICCA Journal* 21(2), 1998 | ◐ | Futility pruning. |
| P-BJORNSSON01 | Y. Björnsson, T. A. Marsland, "Multi-cut αβ-pruning in game-tree search", *Theoretical Computer Science* 252, 2001 | ◐ | Multi-cut pruning. |
| P-WALD45 | A. Wald, "Sequential Tests of Statistical Hypotheses", *Ann. Math. Statist.* 16(2), 1945 | ◐ | SPRT. |
| P-SPALL92 | J. C. Spall, "Multivariate Stochastic Approximation Using a Simultaneous Perturbation Gradient Approximation", *IEEE TAC* 37(3), 1992 | ◐ | SPSA, our parameter-tuning method. |
| P-KOCSIS06 | L. Kocsis, C. Szepesvári, "Bandit Based Monte-Carlo Planning", ECML 2006 | ◐ | UCT. |
| P-ROSIN11 | C. D. Rosin, "Multi-armed bandits with episode context", *Annals of Math. and AI* 61, 2011 | ◐ | PUCT (basis of AlphaZero selection). |
| P-LAI15 | M. Lai, "Giraffe: Using Deep Reinforcement Learning to Play Chess", [arXiv:1509.01549](https://arxiv.org/abs/1509.01549), 2015 | ✔ | Early neural evaluation plus TD-learning in alpha-beta. |
| P-SILVER17 | D. Silver et al., "Mastering Chess and Shogi by Self-Play with a General Reinforcement Learning Algorithm", [arXiv:1712.01815](https://arxiv.org/abs/1712.01815), 2017 | ✔ | AlphaZero preprint. |
| P-SILVER18 | D. Silver et al., "A general reinforcement learning algorithm that masters chess, shogi, and Go through self-play", *Science* 362(6419), 2018 | ◐ | AlphaZero journal version. |
| P-NASU18 | Y. Nasu, "Efficiently Updatable Neural-Network-based Evaluation Functions for Computer Shogi", 2018 (Japanese; English translation circulated by the Stockfish community) | ◐ | Origin of NNUE. |
| P-SCHRITT19 | J. Schrittwieser et al., "Mastering Atari, Go, Chess and Shogi by Planning with a Learned Model", [arXiv:1911.08265](https://arxiv.org/abs/1911.08265), 2019 | ✔ | MuZero; learned dynamics model. |
| P-RUOSS24 | A. Ruoss et al., "Grandmaster-Level Chess Without Search" (later "Amortized Planning with Large-Scale Transformers: A Case Study on Chess"), [arXiv:2402.04494](https://arxiv.org/abs/2402.04494), 2024 | ✔ | 270M transformer; searchless GM-level play. |
| P-MONROE24 | D. Monroe, P. A. Chalmers, The Leela Chess Zero Team, "Mastering Chess with a Transformer Model", [arXiv:2409.12272](https://arxiv.org/abs/2409.12272), Sept 2024 (v2 Oct 2024) | ✔ | ChessFormer; position encoding is key; Leela's transformer architecture. |

### Engines, tools and project sources
| Key | Source | Status | Notes |
|---|---|---|---|
| S-SF | [official-stockfish/Stockfish](https://github.com/official-stockfish/Stockfish) | ✔ | GPL-3.0. Reference opponent; reference for techniques. |
| S-SF12 | [Stockfish 12 release blog](https://stockfishchess.org/blog/2020/stockfish-12/) | ✔ | NNUE introduction, about +130 Elo. |
| S-SF18 | [Stockfish 18 release blog](https://stockfishchess.org/blog/2026/stockfish-18/) | ✔ | SFNNv10, threat inputs, shared-memory weights. |
| S-SF19 | [Stockfish 19 release blog](https://stockfishchess.org/blog/2026/stockfish-19/) | ✔ | SFNNv16; pawn-pair features; small net retired; reported up to +44 Elo over SF18. |
| S-NNUEPT | [official-stockfish/nnue-pytorch](https://github.com/official-stockfish/nnue-pytorch) (docs/nnue.md) | ◐ | The best technical write-up of NNUE internals. |
| S-FISHTEST | [official-stockfish/fishtest](https://github.com/official-stockfish/fishtest) | ◐ | Distributed SPRT; pentanomial statistics. |
| S-SFBOOKS | [official-stockfish/books](https://github.com/official-stockfish/books) | ◐ | UHO opening books. |
| S-LC0 | [LeelaChessZero/lc0](https://github.com/LeelaChessZero/lc0) | ◐ | GPU MCTS reference. |
| S-ETHEREAL | [AndyGrant/Ethereal](https://github.com/AndyGrant/Ethereal) | ◐ | Clean, well-documented alpha-beta engine. |
| S-OPENBENCH | [AndyGrant/OpenBench](https://github.com/AndyGrant/OpenBench) | ◐ | Distributed SPRT/SPSA framework we plan to self-host. |
| S-BULLET | [jw1912/bullet](https://github.com/jw1912/bullet) | ◐ | GPU NNUE trainer (Rust/CUDA) used by many top engines. Verify licence. |
| S-MONTY | [official-monty/Monty](https://github.com/official-monty/Monty) | ◐ | CPU MCTS engine with value/policy nets. |
| S-FASTCHESS | [Disservin/fastchess](https://github.com/Disservin/fastchess) | ◐ | Match runner; pentanomial SPRT. |
| S-CUTECHESS | [cutechess/cutechess](https://github.com/cutechess/cutechess) | ◐ | Classic match runner. |
| S-FATHOM | [jdart1/Fathom](https://github.com/jdart1/Fathom) | ◐ | MIT-licensed Syzygy probing. |
| S-CPW | [Chessprogramming Wiki](https://www.chessprogramming.org/) | ✔ | Encyclopedic reference for every technique above. |
| S-CCRL | [CCRL](https://computerchess.org.uk/ccrl/4040/) | ✔ | Rating-list calibration. A CCRL 40/15 list from July 2026 showed SF18 about 3827 ([talkchess](https://talkchess.com/viewtopic.php?p=995896)). |
| Engines to study | Berserk, Obsidian, PlentyChess, Reckless, Stormphrax, Viridithas, Koivisto, Alexandria | ◐ | Modern open-source top engines; read the source per technique and verify each licence before borrowing ideas or code. |

### Open research questions for the next pass
1. Exact current Stockfish NNUE layer sizes (SFNNv16), and whether threat or pawn-pair inputs are worth their cost at our network size.
2. Licence of bullet and the Leela training data (in case we ever consider external data).
3. Published SMP scaling numbers for Lazy SMP at 8–32 threads.
4. Whether any engine has shown a *measured* gain from a learned policy for alpha-beta ordering.
