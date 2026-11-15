# NNUE tools (M7–M9)

Pipeline, all on our own self-play data (ARCHITECTURE §6):

```bash
./build/apex "datagen gen0.txt 500 5000 1000"          # self-play: "<fen> | <score white> | <result white>"
./build/apex "nnue-data gen0.txt gen0.bin"               # 70-byte binary records
python3 tools/nnue/train.py gen0.bin --out apex.nnue     # numpy + scipy, CPU
./build/apex                                             # then: setoption name EvalFile value apex.nnue
```

## Network: apex-768x128x2-1 (file version 1)
- Inputs: 768 per perspective = (own/their colour) × (piece type) × (square), mirrored vertically for Black's perspective (`nnue::feature`).
- Hidden: 128 int16 accumulators per perspective, updated incrementally in search.
- Activation: SCReLU, clamp(x, 0, QA)².
- Output: one neuron over [side to move, other side]; eval = (Σ / QA + bias) · 400 / (QA · QB) centipawns.
- Quantisation: QA = 255 (input weights and biases), QB = 64 (output weights), output bias × QA·QB.

## File format (little-endian)
| Offset | Content |
|---|---|
| 0 | magic `APXN` |
| 4 | uint32 version = 1 |
| 8 | uint32 inputs = 768 |
| 12 | uint32 hidden = 128 |
| 16 | int16 ft_w[768][128], ft_b[128], out_w[256], out_b |

The engine identifies a loaded file by name and an FNV-1a hash (printed on `setoption`). Network files are not committed (see .gitignore); a released net will be referenced by hash.

## Binary training records (`nnue-data`)
70 bytes: uint8 piece count, uint8 side to move (0 white), int16 score (white POV), uint8 result (0 black win / 1 draw / 2 white win), uint8 reserved, uint16 feature[32] (white-perspective indices, 0xFFFF padding).
