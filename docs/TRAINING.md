# NNUE Training Pipeline

## Dependencies
- Python 3.9+
- `python-chess`
- `torch`
- Optional: `zstandard`

Install:
```bash
pip install python-chess torch zstandard
```

## 1) Generate positions
From PGN:
```bash
python tools/training/gen_positions.py --pgn games.pgn --out data/shards --max-games 5000 --positions-per-game 4
```

Selfplay:
```bash
python tools/training/gen_positions.py --selfplay-engine ./build/ApexNNUE --selfplay-games 500 --selfplay-movetime 50 --out data/selfplay
```

## 2) Teacher labeling
Use a strong engine to label positions:
```bash
python tools/training/teacher_label.py --engine stockfish --input data/shards/positions_0000.jsonl.zst --output data/labeled_0000.jsonl.zst --depth 14
```

## 3) Train NNUE
```bash
python tools/training/train_nnue.py --data data/labeled_*.jsonl.zst --epochs 5 --batch-size 256 --out nnue.pt
```

## 4) Export
```bash
python tools/training/export_nnue.py --checkpoint nnue.pt --output apex.nnue
```

## 5) Run engine with NNUE
```bash
./build/ApexNNUE
setoption name NNUEFile value apex.nnue
setoption name EvalBackend value CPU_NNUE
```

## Validation & Regression
- Quick selfplay match:
```bash
python tools/training/eval_match.py --engine-a ./build/ApexNNUE --engine-b ./build/ApexNNUE --games 20 --time 100
```

- Use tactical suites (external) and compare win rate or score consistency.

## Notes
- The HalfKP feature mapping matches the engine's `nnue_features` implementation.
- The export format is a compact binary with quantized weights.
- For stronger models, increase training data and epochs; consider OneCycleLR and larger batch sizes on GPU.
