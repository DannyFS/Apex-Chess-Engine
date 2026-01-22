#pragma once

#include "util/types.h"
#include <string>
#include <vector>

class Position;

namespace NNUE {

constexpr int NNUE_PIECE_TYPES = 10; // excluding kings
constexpr int NNUE_INPUT_DIM = 64 * NNUE_PIECE_TYPES * 64; // 40960
constexpr int NNUE_HIDDEN1 = 256;
constexpr int NNUE_HIDDEN2 = 32;
constexpr int NNUE_HIDDEN1_TOTAL = NNUE_HIDDEN1 * 2;

constexpr int NNUE_Q1 = 64;
constexpr int NNUE_Q2 = 64;
constexpr int NNUE_Q3 = 64;

struct NnueWeights {
    int input_dim = NNUE_INPUT_DIM;
    int hidden1 = NNUE_HIDDEN1;
    int hidden2 = NNUE_HIDDEN2;
    int q1 = NNUE_Q1;
    int q2 = NNUE_Q2;
    int q3 = NNUE_Q3;

    std::vector<i16> w1; // input_dim * hidden1
    std::vector<i32> b1; // hidden1
    std::vector<i8> w2;  // (hidden1*2) * hidden2
    std::vector<i32> b2; // hidden2
    std::vector<i16> w3; // hidden2
    i32 b3 = 0;
};

enum class Backend { CPU_NNUE = 0, GPU_NNUE = 1 };

bool load(const std::string& path);
bool loaded();
const NnueWeights& weights();

void set_backend(Backend b);
Backend backend();

void set_gpu_config(int device, int batch, bool force);
void benchmark_backends();

int evaluate(::Position& pos);
void refresh(::Position& pos);

}
