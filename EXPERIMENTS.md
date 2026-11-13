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

## Queue
_Opens at milestone M5. Planned first entries: NMP, LMR, RFP (see ROADMAP Phase 2)._
