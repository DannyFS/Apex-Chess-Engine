# Apex: Next-Generation Chess Engine (research project)

A research-driven UCI chess engine project. The planned design is a CPU alpha-beta (PVS) search with a self-trained NNUE evaluation, with the GPU used for training. Every change is gated by correctness tests and SPRT match evidence.

**Status:** Stage 1–4 (audit, research, architecture, roadmap). No engine code yet.

| Document | Purpose |
|---|---|
| [RESEARCH.md](RESEARCH.md) | State-of-the-art survey and bibliography |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Candidate architectures, the selected design and its rationale |
| [ROADMAP.md](ROADMAP.md) | Milestones, exit criteria and realistic targets |
| [BENCHMARKS.md](BENCHMARKS.md) | Frozen test methodology plus the append-only results history |
| [EXPERIMENTS.md](EXPERIMENTS.md) | Hypothesis log, including rejected experiments |
| [CHANGELOG.md](CHANGELOG.md) | Project changes |

The previous prototype is preserved in git history at commit `3e6ee83`.
