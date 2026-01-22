#pragma once

#include "nnue/nnue_loader.h"
#include <atomic>

class Position;

namespace NNUE {

class GpuEvaluator {
public:
    bool init(const NnueWeights& w, int device, int batch);
    void shutdown();
    bool ready() const { return initialized; }

    int evaluate(const Position& pos, bool& used_gpu);

    void request_stop();

private:
    bool initialized = false;
};

} // namespace NNUE
