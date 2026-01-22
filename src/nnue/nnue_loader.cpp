#include "nnue/nnue_loader.h"
#include "nnue/nnue_cpu.h"
#include "nnue/nnue_gpu.h"
#include "nnue/nnue_features.h"
#include "position.h"
#include <fstream>
#include <chrono>
#include <iostream>

namespace NNUE {

static NnueWeights g_weights;
static bool g_loaded = false;
static Backend g_backend = Backend::CPU_NNUE;
static int g_gpu_device = 0;
static int g_gpu_batch = 64;
static bool g_force_gpu = false;
static GpuEvaluator g_gpu;

bool load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        g_loaded = false;
        return false;
    }

    char magic[8] = {};
    u32 version = 0;
    in.read(magic, 8);
    in.read(reinterpret_cast<char*>(&version), sizeof(u32));
    if (std::string(magic, magic + 7) != "ANNUv1") {
        g_loaded = false;
        return false;
    }

    in.read(reinterpret_cast<char*>(&g_weights.input_dim), sizeof(u32));
    in.read(reinterpret_cast<char*>(&g_weights.hidden1), sizeof(u32));
    in.read(reinterpret_cast<char*>(&g_weights.hidden2), sizeof(u32));
    in.read(reinterpret_cast<char*>(&g_weights.q1), sizeof(u32));
    in.read(reinterpret_cast<char*>(&g_weights.q2), sizeof(u32));
    in.read(reinterpret_cast<char*>(&g_weights.q3), sizeof(u32));

    size_t w1_size = (size_t)g_weights.input_dim * g_weights.hidden1;
    size_t b1_size = (size_t)g_weights.hidden1;
    size_t w2_size = (size_t)g_weights.hidden1 * 2 * g_weights.hidden2;
    size_t b2_size = (size_t)g_weights.hidden2;
    size_t w3_size = (size_t)g_weights.hidden2;

    g_weights.w1.resize(w1_size);
    g_weights.b1.resize(b1_size);
    g_weights.w2.resize(w2_size);
    g_weights.b2.resize(b2_size);
    g_weights.w3.resize(w3_size);

    in.read(reinterpret_cast<char*>(g_weights.w1.data()), w1_size * sizeof(i16));
    in.read(reinterpret_cast<char*>(g_weights.b1.data()), b1_size * sizeof(i32));
    in.read(reinterpret_cast<char*>(g_weights.w2.data()), w2_size * sizeof(i8));
    in.read(reinterpret_cast<char*>(g_weights.b2.data()), b2_size * sizeof(i32));
    in.read(reinterpret_cast<char*>(g_weights.w3.data()), w3_size * sizeof(i16));
    in.read(reinterpret_cast<char*>(&g_weights.b3), sizeof(i32));

    g_loaded = in.good();

#ifdef APEX_USE_CUDA
    if (g_loaded && g_backend == Backend::GPU_NNUE) {
        g_gpu.init(g_weights, g_gpu_device, g_gpu_batch);
    }
#endif

    return g_loaded;
}

bool loaded() { return g_loaded; }
const NnueWeights& weights() { return g_weights; }

void set_backend(Backend b) {
    g_backend = b;
#ifdef APEX_USE_CUDA
    if (g_loaded && g_backend == Backend::GPU_NNUE && !g_gpu.ready()) {
        g_gpu.init(g_weights, g_gpu_device, g_gpu_batch);
    }
#endif
}

Backend backend() { return g_backend; }

void set_gpu_config(int device, int batch, bool force) {
    g_gpu_device = device;
    g_gpu_batch = batch;
    g_force_gpu = force;
#ifdef APEX_USE_CUDA
    if (g_loaded) g_gpu.init(g_weights, g_gpu_device, g_gpu_batch);
#endif
}

void benchmark_backends() {
    if (!g_loaded) return;
    Position pos;
    pos.set_startpos();
    NNUE::accum_init(pos, pos.nnue_state(), g_weights);

    const int iters = 2000;
    auto t0 = std::chrono::steady_clock::now();
    int cpu_sum = 0;
    for (int i = 0; i < iters; ++i) cpu_sum += evaluate_cpu(pos, g_weights);
    auto t1 = std::chrono::steady_clock::now();
    auto cpu_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

#ifdef APEX_USE_CUDA
    bool used_gpu = false;
    auto t2 = std::chrono::steady_clock::now();
    int gpu_sum = 0;
    for (int i = 0; i < iters; ++i) gpu_sum += g_gpu.evaluate(pos, used_gpu);
    auto t3 = std::chrono::steady_clock::now();
    auto gpu_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count();

    if (!g_force_gpu && (gpu_ms == 0 || gpu_ms > cpu_ms)) {
        g_backend = Backend::CPU_NNUE;
    }
#else
    (void)cpu_sum;
#endif
}

int evaluate(Position& pos) {
    if (!g_loaded) return pos.simple_eval();
    if (g_backend == Backend::CPU_NNUE) return evaluate_cpu(pos, g_weights);
#ifdef APEX_USE_CUDA
    bool used_gpu = false;
    int s = g_gpu.evaluate(pos, used_gpu);
    if (!used_gpu && g_backend == Backend::GPU_NNUE) {
        return evaluate_cpu(pos, g_weights);
    }
    return s;
#else
    return evaluate_cpu(pos, g_weights);
#endif
}

void refresh(Position& pos) {
    if (!g_loaded) return;
    NNUE::accum_init(pos, pos.nnue_state(), g_weights);
}

} // namespace NNUE
