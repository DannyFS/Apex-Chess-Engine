# ARCHITECTURE.md: Architecture Proposal (Stage 3, v0.1)

Status: **proposed**, awaiting Stage-1 hardware detection on the Windows development machine. Section 2 records what is known and unknown.

---

## 1. Audit of the previous implementation (commit `3e6ee83`, "Initial ApexNNUE engine")

The old tree (about 2.6k lines plus vendored Fathom) was built and smoke-tested on 2026-11-13 in the cloud container:

| Check | Result |
|---|---|
| Engine build (GCC 13, Release) | Builds, with warnings |
| Test targets (`test_perft`, `test_movegen`) | **Fail to link** |
| `go perft 5` | Not implemented. The engine started a normal search and returned `bestmove 0000` (an illegal null move). |
| `go depth 12` from the start position | Score `cp 0` at every depth, PV `f2f3`. With no network file loaded, the evaluation is effectively zero. |
| Training pipeline | `teacher_label.py` relies on an external teacher engine for labels, a distillation approach that we are deliberately not using (see §6). |

**Decision:** as requested, the old tree has been removed from the working branch. It is **not destroyed**: it remains reachable at commit `3e6ee83` and on `main`. The only part worth reusing is the idea of vendoring Fathom (MIT) for Syzygy probing, and we will re-vendor it from upstream.

---

## 2. Hardware assessment

| Item | Development PC (from the brief) | Cloud container (measured) |
|---|---|---|
| OS | Windows | Linux 6.18 |
| CPU | **Unknown: to detect** | Intel Xeon @ 2.10 GHz, 4 vCPU, AVX2/BMI2/AVX-512F/BW/VNNI |
| RAM | **Unknown** | 15 GiB |
| GPU | RTX 5060 Ti 16 GB (Blackwell, sm_120) | none |
| Compilers | **Unknown** (MSVC / clang-cl / MinGW?) | GCC 13.3, Clang 18.1, CMake 3.28, Python 3.13 |

**Stage-1 action:** run the hardware-detection script (milestone M0) on the Windows machine and commit its output to `BENCHMARKS.md`. Until then, every decision below assumes a modern desktop CPU with at least AVX2.

**Implications:**
- **Search runs on the CPU.** NNUE incremental evaluation costs roughly 10⁻⁷ s, while a single PCIe round trip to the GPU costs about 10⁻⁵ s. Alpha-beta cannot batch evaluations without speculation.
- **The GPU is the training and data-generation accelerator.** A 5060 Ti can train a 1024–2048 hidden-unit NNUE on about 1B positions in hours (estimate [VERIFY with bullet on the target machine]).
- **CPU core count determines testing throughput.** An SPRT run of about 20k STC games at 8+0.08 uses roughly 20k × 2 × 12 s ≈ 133 CPU-hours, or about 8 hours on 16 threads. This is the real bottleneck of the project.
- **The cloud container is useful for development**: building, perft, unit tests, short SPRTs with 3 game threads. It is **not** a reference machine. Every benchmark records which machine produced it.

---

## 3. Candidate architectures

### A. Modern CPU alpha-beta with our own NNUE
Bitboards (magic or PEXT), PVS with the full tier-1 to tier-3 pruning and ordering stack from RESEARCH §1, Lazy SMP, a single-layer NNUE with king buckets and output buckets (later multi-layer), trained by self-play on the GPU.

### B. GPU neural MCTS (AlphaZero/Leela-like)
A transformer or ResNet policy-value network on the GPU, PUCT search, trained by self-play reinforcement learning.

### C. Hybrid: architecture A plus neural guidance
Architecture A plus a small policy network (on the CPU for high-depth nodes, or GPU-batched at the root and at shallow-ply PV nodes only), learned search control, and GPU-distilled training labels.

### D. CPU MCTS with small value/policy networks (Monty-like)
Included as a reference point.

| Criterion | A | B | C | D |
|---|---|---|---|---|
| Expected ceiling on our hardware | **High** (3400–3600+ CCRL is plausible with sustained work) | Medium. Without Leela's training compute, our own nets would be far weaker. | ≥ A **if** the guidance pays for its cost [HYP] | Medium |
| Evidence base | Overwhelming: all top CPU engines | Strong, but only at massive training scale | Weak and anecdotal | Moderate |
| Implementation complexity | Medium–high | High | Highest | Medium |
| Training requirement | Moderate (1–5B self-play positions per net) | Enormous (millions of RL self-play games) | A plus policy training | Moderate |
| Hardware fit | CPU search plus GPU training: **excellent** | GPU-bound; 5060 Ti is mid-range | Good | CPU |
| Time to a strong baseline | Weeks | Months to years | Weeks for the A part | Months |
| Future research potential | High (NNUE features, search, tuning) | High but compute-limited | Highest novelty | Medium |

## 4. Recommendation

**Build Architecture A as the champion line, designed from day one so that C-style experiments plug in without forking the codebase.**

Rationale:
1. A has the strongest evidence and the best fit to our hardware: the CPU searches and the GPU trains.
2. B's strength comes from training compute we cannot match. Running Leela's networks would make us a Leela GUI, not an engine.
3. C's ideas are only measurable *relative to a strong A baseline*. Neural guidance that beats a weak engine proves nothing. A is therefore a prerequisite for C anyway.
4. Our novelty budget goes where the evidence leaves room: NNUE input features (threats and pawn pairs are recent and still moving), training-data methodology, learned search control and GPU-distilled labels.

## 5. Module layout (planned)

```
src/
  core/     types, bitboards, attacks (magic/PEXT), zobrist, position, movegen, SEE
  eval/     evaluator interface; hce/ (bootstrap only); nnue/ (arch, inference, SIMD kernels)
  search/   search, qsearch, history tables, movepicker, tt, timeman, threads (Lazy SMP)
  uci/      protocol, options, bench command
  tb/       Syzygy (vendored Fathom, MIT)
  exp/      experimental modules behind compile-time or UCI flags; never on by default in the champion
tests/      unit tests (minimal in-house harness), perft suites, UCI conformance, search regression
tools/      datagen (in-engine), trainer configs (bullet), match/SPRT scripts, hwinfo, bench history
nets/       versioned network metadata (weights via release assets; hashes in-repo)
```

Engineering rules:
- C++20, CMake, GCC/Clang/MSVC (clang-cl preferred on Windows for codegen parity with Linux).
- The build targets `x86-64-v3` (AVX2+BMI2) and `x86-64-v4` (AVX-512/VNNI) with runtime selection or separate binaries.
- **Determinism:** a single-threaded search with the same hash size and the same `bench` input must produce an identical node count on every platform. The `bench` node count is the commit signature.
- The network is embedded in the binary (incbin) with a SHA-256 identifier, so the engine never silently runs without a network (the old build ran with eval 0 in exactly this way).

## 6. Data and training policy

- **Bootstrap:** a small Texel-tuned HCE (material, PSTs, mobility, basic king safety and pawns), whose sole purpose is to produce generation-0 self-play data.
- **Then:** iterative self-play datagen (about 5k nodes per move, random opening plies, adjudication) → bullet training on the 5060 Ti → SPRT against the champion → repeat.
- **No Stockfish or Leela-labelled data in the champion line.** This keeps the engine original, avoids licensing ambiguity, and avoids silently inheriting another engine's biases. A *teacher-labelled* net may be run as a clearly labelled experiment, to measure how much we leave on the table, but it will never be released as the champion.

## 7. Owner decisions (resolved 2026-11-13)

1. **Licence: GPL-3.0-or-later.** Ideas and code from GPL engines may be adapted with attribution. Any adapted code must cite its source in a comment.
2. **Name: Apex.**
3. **Testing budget: 4 cores** on the development PC, plus this cloud container (4 vCPU) for development runs.

### 7.1 Consequences of a 4-core testing budget
At STC 8+0.08, a game lasts about 25 s of wall time per game slot, so 4 slots give roughly 500–550 games per hour.

| Expected patch size | SPRT bounds | Typical games to resolve | Wall time on 4 cores |
|---|---|---|---|
| Large (+30 Elo or more: early search features, first NNUE) | [0, 10] | 1–3k | 2–6 h |
| Medium (+5 to +15) | [0, 5] | 5–15k | 10–30 h |
| Small (+1 to +3, late-stage tuning) | [0, 2] | 30–80k+ | days to weeks: **impractical** |

**Policy:**
- During the early phases (M5–M9), most changes are large, so [0, 10] bounds are appropriate and affordable.
- Changes that bring no measurable gain at that resolution are rejected or batched, never merged on faith.
- Once gains shrink below about 5 Elo, more testing hardware becomes the main lever for strength. The options then are cloud workers, an OpenBench instance, or community workers.
- Runs on the cloud container are recorded as a separate machine and never mixed into the same SPRT as games played on the development PC.
