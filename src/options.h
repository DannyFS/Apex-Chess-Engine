#pragma once

#include "nnue/nnue_loader.h"
#include <string>

struct EngineOptions {
    int Threads = 1;
    int Hash = 64;
    int MoveOverhead = 30;
    std::string SyzygyPath = "";
    int SyzygyProbeDepth = 1;
    int SyzygyProbeLimit = 5;
    NNUE::Backend EvalBackend = NNUE::Backend::CPU_NNUE;
    int GPUDevice = 0;
    int GPUBatchSize = 64;
    std::string NNUEFile = "";
    bool UsePonder = false;
    int MultiPV = 1;
    bool DebugLog = false;
};

extern EngineOptions Options;
