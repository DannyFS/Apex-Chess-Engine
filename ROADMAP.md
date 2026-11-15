# ROADMAP.md

Each milestone has **exit criteria**, and a milestone is done only when they are met with recorded evidence. Elo targets are goals, not predictions.

## Phase 0: Foundations (Stages 1–4)
- **M0. Environment.** `tools/hwinfo` (CPU model, cores, ISA flags, RAM, GPU/VRAM, CUDA, compilers, OS) run on the Windows development PC and in the cloud. Reproducible CMake presets. CI: build and test on Linux and Windows.
  *Exit:* hardware report committed; CI green.
- **M1. Research and architecture.** RESEARCH.md v0.1 and ARCHITECTURE.md v0.1 (this commit); owner decisions on licence and test-core budget.

## Phase 1: Correct engine (Stage 5)
- **M2. Board core.** Bitboards, magic/PEXT attacks, Zobrist, make/unmake, legal movegen, SEE.
  *Exit:* perft matches reference values on the standard suite (start position, Kiwipete, positions 3–6, plus about 100 edge-case positions: en passant pins, castling through check, promotions) to depth 6+. Zobrist incremental hash equals the full recompute in a randomised fuzz test (10⁷ moves). Perft throughput is recorded.
- **M3. Minimal UCI engine.** UCI protocol, PVS, ID, TT, qsearch, MVV-LVA, PeSTO-style tapered PSTs, time management, repetition and fifty-move-rule handling, mate/stalemate.
  *Exit:* passes UCI conformance (fastchess validation games, 1000 games with no illegal moves or time losses). `bench` command produces a deterministic signature.
- **M4. Test infrastructure.** fastchess plus SPRT scripts, an opening book (UHO), a gauntlet harness, an auto-appending `BENCHMARKS.md` log, and a self-hosted OpenBench instance if the core budget allows.
  *Exit:* the first SPRT completes end to end and is logged.

## Phase 2: Strong classical search (Stage 6–7)
- **M5. Search stack.** One feature per SPRT: NMP → LMR → RFP → killers/history → aspiration → LMP/futility → SEE pruning → continuation history → IIR → singular extensions → capture history → correction history.
  *Target:* approximately 2800–3000 CCRL-equivalent with the HCE (gauntlet-measured).
- **M6. Bootstrap HCE plus Texel tuning.** *Exit:* tuned HCE beats the untuned version with SPRT confirmation.

## Phase 3: NNUE (the largest single jump)
- **M7. Datagen.** In-engine self-play generator (multi-threaded, compact binary format, filtering, deduplication).
- **M8. Network v1.** `768 → 256×2 → 1`, SCReLU, int16 quantisation, bullet trainer on the 5060 Ti, AVX2/AVX-512 inference, lazy accumulators.
  *Exit:* SPRT win against the HCE champion. Expected margin: very large [HYP: +200 Elo or more].
- **M9. Net iteration.** Larger hidden layer (512 → 1024 → 1536+), king buckets with Finny tables, output buckets, more data and retuned margins. Each net is versioned in `nets/`.
  *Target:* 3300+ CCRL-equivalent.

## Phase 4: Scaling and polish
- **M10.** Lazy SMP (SMP scaling SPRT at 1 vs 4/8 threads), Syzygy probing via Fathom, time-management tuning, SPSA tuning of search parameters, multi-PV, pondering.
- **M11.** First external rating: gauntlet against about 6 engines of known CCRL rating spanning ±200 Elo.

## Phase 5: Research (Stage 7 onward; ranked by expected value)
1. NNUE input features: threat inputs and pawn-pair-style features at our network size.
2. Multi-layer NNUE head (Stockfish-style L1 → L2 → L3).
3. Data methodology: λ blending, filtering, adjudication, opening diversity, data volume curves.
4. GPU-distilled labels: a large policy/value net on the GPU labels positions for NNUE training [HYP].
5. Learned search control (LMR and pruning predictors) against an SPSA baseline [HYP].
6. CPU policy head for ordering at high-depth nodes [HYP].
7. A one-off measurement of GPU-batched evaluation in alpha-beta, to document the expected negative result.

## Realistic outcome bands (honest estimate)
| Band | What it takes | Likelihood with our setup |
|---|---|---|
| 3000 CCRL | M5 complete plus a decent HCE | High |
| 3300–3500 | Own NNUE, about 1–3B positions, sustained SPRT | Good, with months of steady testing |
| 3500–3650 (top-15 open-source tier) | Many net generations, large-scale testing (thousands of CPU-hours a month) | Possible; testing compute is the limit |
| Stockfish level (~3800+) | Hundreds of contributors and a distributed testing farm running about 10⁹ games over years | **Not realistic** with one machine. Requires a different order of resources. |

## Status (2026-11-16)
| Milestone | Status | Evidence |
|---|---|---|
| M0 Environment | partial: cloud machine recorded; **the Windows PC report (`tools/hwinfo.ps1`) is still pending** | BENCHMARKS.md |
| M1 Research / architecture | done | RESEARCH.md, ARCHITECTURE.md |
| M2 Board core | done (perft 27 positions incl. 1.5B-node deep suite, Zobrist fuzz, ASan/UBSan) | test_core |
| M3 Minimal UCI engine | done (UCI tests; 1000 validation games without forfeits on an idle machine) | BENCHMARKS.md M3-baseline |
| M4 Test infrastructure | done for one machine (fastchess SPRT runner, UHO book); no OpenBench | tools/match.py |
| M5 Search stack | mostly done: 8 features accepted; check extension, TT buckets, continuation history, correction history inconclusive or rejected (patches kept); IIR and capture history still to test | EXPERIMENTS.md |
| M6 Texel-tuned HCE | done (+312) | EXP-015 |
| M7 Datagen | done (in-engine, single-threaded per process, text plus binary conversion) | src/tools/datagen.cpp |
| M8 Network v1 | done with a **128-unit** net (not 256) trained on the CPU (numpy), because this environment has no GPU and PyTorch's download host is blocked. +130 vs the tuned HCE | EXP-017 |
| M9 Net iteration | in progress: generation-1 data from the NNUE engine | EXPERIMENTS.md |
| M10–M11 | not started | |

No CCRL-equivalent rating has been measured yet (M11); all gains so far are self-play.
