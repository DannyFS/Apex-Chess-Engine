#include "nnue/nnue_cpu.h"
#include "nnue/nnue_features.h"
#include "position.h"
#include <algorithm>

namespace NNUE {

static inline int clamp0(int v, int maxv) {
    return v < 0 ? 0 : (v > maxv ? maxv : v);
}

int evaluate_cpu(const Position& pos, const NnueWeights& w) {
    const NNUEState& st = pos.nnue_state();
    Color stm = pos.side_to_move();
    Color opp = ~stm;

    static thread_local std::array<i16, NNUE_HIDDEN1_TOTAL> h1{};
    static thread_local std::array<i16, NNUE_HIDDEN2> h2{};

    const int max1 = 127 * w.q1;
    for (int i = 0; i < w.hidden1; ++i) {
        int v = clamp0(st.accum[stm][i], max1);
        h1[i] = static_cast<i16>(v >> 6);
    }
    for (int i = 0; i < w.hidden1; ++i) {
        int v = clamp0(st.accum[opp][i], max1);
        h1[i + w.hidden1] = static_cast<i16>(v >> 6);
    }

    const int max2 = 127 * w.q2;
    for (int j = 0; j < w.hidden2; ++j) {
        i32 sum = w.b2[j];
        const i8* w2col = &w.w2[j];
        for (int i = 0; i < w.hidden1 * 2; ++i) {
            sum += w2col[i * w.hidden2] * h1[i];
        }
        sum = clamp0(sum, max2);
        h2[j] = static_cast<i16>(sum >> 6);
    }

    i32 out = w.b3;
    for (int j = 0; j < w.hidden2; ++j) {
        out += w.w3[j] * h2[j];
    }

    int score = static_cast<int>(out / w.q3);
    return score;
}

}
