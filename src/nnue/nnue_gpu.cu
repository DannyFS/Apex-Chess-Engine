#include "nnue/nnue_gpu.h"
#include "nnue/nnue_features.h"
#include "position.h"

#ifdef APEX_USE_CUDA
#include <cuda_runtime.h>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <cstring>
#include <array>

namespace NNUE {

struct GpuTask {
    std::atomic<int> state{0}; // 0 empty, 1 ready, 2 done
    int16_t* h1 = nullptr;
    int result = 0;
};

static constexpr int MAX_TASKS = 1024;

struct GpuCtx {
    bool initialized = false;
    int device = 0;
    int batch = 64;

    std::vector<GpuTask> tasks;
    std::atomic<uint32_t> head{0};
    std::atomic<uint32_t> tail{0};

    std::thread worker;
    std::atomic<bool> stop{false};

    int16_t* h1_batch = nullptr; // pinned host
    int32_t* out_batch = nullptr; // pinned host

    int16_t* d_h1 = nullptr;
    int32_t* d_out = nullptr;

    int8_t* d_w2 = nullptr;
    int32_t* d_b2 = nullptr;
    int16_t* d_w3 = nullptr;
    int32_t d_b3 = 0;

    int hidden1_total = NNUE_HIDDEN1_TOTAL;
    int hidden2 = NNUE_HIDDEN2;
    int q2 = NNUE_Q2;
    int q3 = NNUE_Q3;

    cudaStream_t stream{};
} ctx;

__global__ void eval_kernel(const int16_t* h1, int32_t* out,
                            const int8_t* w2, const int32_t* b2,
                            const int16_t* w3, int32_t b3,
                            int hidden1_total, int hidden2, int q2, int q3) {
    int idx = blockIdx.x;
    const int16_t* h1v = h1 + idx * hidden1_total;
    int32_t score = b3;
    for (int j = 0; j < hidden2; ++j) {
        int32_t sum = b2[j];
        const int8_t* w2col = w2 + j;
        for (int i = 0; i < hidden1_total; ++i) {
            sum += w2col[i * hidden2] * h1v[i];
        }
        if (sum < 0) sum = 0;
        int32_t max2 = 127 * q2;
        if (sum > max2) sum = max2;
        int16_t h2 = (int16_t)(sum >> 6);
        score += w3[j] * h2;
    }
    out[idx] = score / q3;
}

static void worker_loop() {
    while (!ctx.stop.load(std::memory_order_relaxed)) {
        uint32_t t = ctx.tail.load(std::memory_order_relaxed);
        uint32_t h = ctx.head.load(std::memory_order_relaxed);
        if (t == h) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }

        int batch = 0;
        std::vector<uint32_t> indices;
        indices.reserve(ctx.batch);

        while (t < h && batch < ctx.batch) {
            GpuTask& task = ctx.tasks[t % ctx.tasks.size()];
            if (task.state.load(std::memory_order_acquire) != 1) {
                t++;
                continue;
            }
            std::memcpy(ctx.h1_batch + batch * ctx.hidden1_total, task.h1, sizeof(int16_t) * ctx.hidden1_total);
            indices.push_back(t % ctx.tasks.size());
            batch++;
            t++;
        }
        ctx.tail.store(t, std::memory_order_relaxed);

        if (batch == 0) continue;

        size_t h1_bytes = sizeof(int16_t) * ctx.hidden1_total * batch;
        size_t out_bytes = sizeof(int32_t) * batch;

        cudaMemcpyAsync(ctx.d_h1, ctx.h1_batch, h1_bytes, cudaMemcpyHostToDevice, ctx.stream);
        eval_kernel<<<batch, 1, 0, ctx.stream>>>(ctx.d_h1, ctx.d_out, ctx.d_w2, ctx.d_b2, ctx.d_w3, ctx.d_b3,
                                                  ctx.hidden1_total, ctx.hidden2, ctx.q2, ctx.q3);
        cudaMemcpyAsync(ctx.out_batch, ctx.d_out, out_bytes, cudaMemcpyDeviceToHost, ctx.stream);
        cudaStreamSynchronize(ctx.stream);

        for (int i = 0; i < batch; ++i) {
            GpuTask& task = ctx.tasks[indices[i]];
            task.result = ctx.out_batch[i];
            task.state.store(2, std::memory_order_release);
        }
    }
}

bool GpuEvaluator::init(const NnueWeights& w, int device, int batch) {
    if (ctx.initialized) return true;
    ctx.device = device;
    ctx.batch = batch;
    ctx.hidden1_total = w.hidden1 * 2;
    ctx.hidden2 = w.hidden2;
    ctx.q2 = w.q2;
    ctx.q3 = w.q3;
    ctx.d_b3 = w.b3;

    cudaSetDevice(device);
    cudaStreamCreate(&ctx.stream);

    size_t w2_bytes = sizeof(int8_t) * w.w2.size();
    size_t b2_bytes = sizeof(int32_t) * w.b2.size();
    size_t w3_bytes = sizeof(int16_t) * w.w3.size();

    cudaMalloc(&ctx.d_w2, w2_bytes);
    cudaMalloc(&ctx.d_b2, b2_bytes);
    cudaMalloc(&ctx.d_w3, w3_bytes);

    cudaMemcpy(ctx.d_w2, w.w2.data(), w2_bytes, cudaMemcpyHostToDevice);
    cudaMemcpy(ctx.d_b2, w.b2.data(), b2_bytes, cudaMemcpyHostToDevice);
    cudaMemcpy(ctx.d_w3, w.w3.data(), w3_bytes, cudaMemcpyHostToDevice);

    cudaMalloc(&ctx.d_h1, sizeof(int16_t) * ctx.hidden1_total * ctx.batch);
    cudaMalloc(&ctx.d_out, sizeof(int32_t) * ctx.batch);

    cudaMallocHost(&ctx.h1_batch, sizeof(int16_t) * ctx.hidden1_total * ctx.batch);
    cudaMallocHost(&ctx.out_batch, sizeof(int32_t) * ctx.batch);

    ctx.tasks.resize(MAX_TASKS);
    for (auto& t : ctx.tasks) {
        cudaMallocHost(&t.h1, sizeof(int16_t) * ctx.hidden1_total);
    }

    ctx.stop.store(false, std::memory_order_relaxed);
    ctx.worker = std::thread(worker_loop);
    ctx.initialized = true;
    initialized = true;
    return true;
}

void GpuEvaluator::shutdown() {
    if (!ctx.initialized) return;
    ctx.stop.store(true, std::memory_order_relaxed);
    if (ctx.worker.joinable()) ctx.worker.join();

    cudaFree(ctx.d_w2);
    cudaFree(ctx.d_b2);
    cudaFree(ctx.d_w3);
    cudaFree(ctx.d_h1);
    cudaFree(ctx.d_out);
    cudaFreeHost(ctx.h1_batch);
    cudaFreeHost(ctx.out_batch);
    for (auto& t : ctx.tasks) cudaFreeHost(t.h1);
    cudaStreamDestroy(ctx.stream);
    ctx.initialized = false;
    initialized = false;
}

void GpuEvaluator::request_stop() {
    ctx.stop.store(true, std::memory_order_relaxed);
}

int GpuEvaluator::evaluate(const Position& pos, bool& used_gpu) {
    used_gpu = false;
    if (!ctx.initialized) return pos.simple_eval();

    static thread_local std::array<int16_t, NNUE_HIDDEN1_TOTAL> h1{};
    const NNUEState& st = pos.nnue_state();
    Color stm = pos.side_to_move();
    Color opp = ~stm;
    int max1 = 127 * NNUE_Q1;

    for (int i = 0; i < NNUE_HIDDEN1; ++i) {
        int v = st.accum[stm][i];
        if (v < 0) v = 0; else if (v > max1) v = max1;
        h1[i] = (int16_t)(v >> 6);
    }
    for (int i = 0; i < NNUE_HIDDEN1; ++i) {
        int v = st.accum[opp][i];
        if (v < 0) v = 0; else if (v > max1) v = max1;
        h1[i + NNUE_HIDDEN1] = (int16_t)(v >> 6);
    }

    uint32_t idx = ctx.head.fetch_add(1, std::memory_order_relaxed);
    GpuTask& task = ctx.tasks[idx % ctx.tasks.size()];
    auto start = std::chrono::steady_clock::now();

    while (task.state.load(std::memory_order_acquire) != 0) {
        if (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count() > 200) {
            return pos.simple_eval();
        }
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    std::memcpy(task.h1, h1.data(), sizeof(int16_t) * NNUE_HIDDEN1_TOTAL);
    task.state.store(1, std::memory_order_release);

    while (task.state.load(std::memory_order_acquire) != 2) {
        if (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count() > 500) {
            task.state.store(0, std::memory_order_release);
            return pos.simple_eval();
        }
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    int result = task.result;
    task.state.store(0, std::memory_order_release);
    used_gpu = true;
    return result;
}

} // namespace NNUE

#else

namespace NNUE {

bool GpuEvaluator::init(const NnueWeights&, int, int) { return false; }
void GpuEvaluator::shutdown() {}
void GpuEvaluator::request_stop() {}
int GpuEvaluator::evaluate(const Position& pos, bool& used_gpu) { used_gpu = false; return pos.simple_eval(); }

} // namespace NNUE

#endif
