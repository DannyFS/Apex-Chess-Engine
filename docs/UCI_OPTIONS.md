# UCI Options

- `Threads` (spin): Number of search threads.
- `Hash` (spin): Transposition table size in MB.
- `MoveOverhead` (spin): Time overhead per move in ms.
- `SyzygyPath` (string): Path to Syzygy tablebases.
- `SyzygyProbeDepth` (spin): Max depth to probe WDL.
- `SyzygyProbeLimit` (spin): Max pieces to probe.
- `EvalBackend` (combo): `CPU_NNUE` or `GPU_NNUE`.
- `GPUDevice` (spin): CUDA device id.
- `GPUBatchSize` (spin): GPU eval batch size.
- `NNUEFile` (string): Path to `.nnue` file.
- `UsePonder` (check): Ponder support (not fully exploited).
- `MultiPV` (spin): Multi-PV lines (currently best line only).
- `DebugLog` (check): Enable extra logging (minimal).
