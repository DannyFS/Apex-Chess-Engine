# Experiment patches not merged into the champion

Rejected and inconclusive experiments are kept here as `git format-patch` files, so that the code stays durable even though experiment branches live only in the test machine. Apply with `git am <file>` on top of the champion commit named in the patch header (or with a 3-way merge if the code has moved).

| Patch | Status | Evidence |
|---|---|---|
| EXP-006-check-extension.patch | inconclusive (+7.5 ± 9.6 Elo, 3000-game cap) | BENCHMARKS.md "EXP-006" |
| EXP-009-tt-buckets.patch | rejected (−5.8 ± 9.6 Elo, H0) | BENCHMARKS.md "EXP-009" |
| EXP-010-continuation-history.patch | inconclusive (+3.0 ± 9.4 Elo, 3000-game cap) | BENCHMARKS.md "EXP-010" |
| EXP-014-correction-history.patch | inconclusive (+9.6 ± 8.8 Elo, 3000-game cap); re-test candidate | BENCHMARKS.md "EXP-014" |
| EXP-016-texel-round3.patch | inconclusive (+0.2 ± 8.8 Elo, 3000-game cap) | BENCHMARKS.md "EXP-016" |
| EXP-020-nnue-256-hidden.patch | inconclusive (−0.7 ± 8.6 Elo, 3000-game cap); includes the 256-hidden net as a binary patch | BENCHMARKS.md "EXP-020" |
