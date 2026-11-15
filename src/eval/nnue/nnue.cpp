// Apex chess engine: NNUE evaluation (M8).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nnue.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace apex::nnue {

namespace {
std::unique_ptr<Network> net;
std::string net_name;

// File layout (little-endian): "APXN" magic, uint32 version (1), uint32 inputs,
// uint32 hidden, then int16 ft_w[inputs*hidden], ft_b[hidden], out_w[2*hidden], out_b.
constexpr char Magic[4] = {'A', 'P', 'X', 'N'};
constexpr std::uint32_t Version = 1;

// FNV-1a 64 over the file bytes: a short identifier for logs (not a security hash).
std::uint64_t fnv1a(const std::vector<unsigned char>& data) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : data) h = (h ^ c) * 1099511628211ULL;
    return h;
}

inline void add_feature(std::int16_t* acc, int f) {
    const std::int16_t* w = net->ft_w + f * HIDDEN;
    for (int i = 0; i < HIDDEN; ++i) acc[i] = std::int16_t(acc[i] + w[i]);
}
inline void sub_feature(std::int16_t* acc, int f) {
    const std::int16_t* w = net->ft_w + f * HIDDEN;
    for (int i = 0; i < HIDDEN; ++i) acc[i] = std::int16_t(acc[i] - w[i]);
}
}  // namespace

namespace {
#include "embedded_net.inc"

bool load_bytes(const std::vector<unsigned char>& data, const std::string& name, std::string& error) {
    const std::size_t header = 16;
    const std::size_t body = (std::size_t(INPUTS) * HIDDEN + HIDDEN + 2 * HIDDEN + 1) * sizeof(std::int16_t);
    if (data.size() != header + body) { error = "wrong file size"; return false; }
    if (std::memcmp(data.data(), Magic, 4) != 0) { error = "bad magic"; return false; }
    std::uint32_t hdr[3];
    std::memcpy(hdr, data.data() + 4, sizeof(hdr));
    if (hdr[0] != Version || hdr[1] != INPUTS || hdr[2] != HIDDEN) { error = "unsupported architecture"; return false; }

    auto nw = std::make_unique<Network>();
    const unsigned char* p = data.data() + header;
    auto take = [&](void* dst, std::size_t bytes) { std::memcpy(dst, p, bytes); p += bytes; };
    take(nw->ft_w, sizeof(nw->ft_w));
    take(nw->ft_b, sizeof(nw->ft_b));
    take(nw->out_w, sizeof(nw->out_w));
    take(&nw->out_b, sizeof(nw->out_b));
    net = std::move(nw);

    char id[32];
    std::snprintf(id, sizeof(id), "%016llx", (unsigned long long)fnv1a(data));
    net_name = name + " (fnv " + id + ")";
    return true;
}
}  // namespace

bool load(const std::string& path, std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error = "cannot open " + path; return false; }
    std::vector<unsigned char> data;
    unsigned char buf[65536];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);
    const std::size_t slash = path.find_last_of("/\\");
    return load_bytes(data, slash == std::string::npos ? path : path.substr(slash + 1), error);
}

bool load_embedded(std::string& error) {
    std::vector<unsigned char> data(EmbeddedNet, EmbeddedNet + sizeof(EmbeddedNet));
    return load_bytes(data, std::string(EmbeddedNetName) + " [embedded]", error);
}

bool loaded() { return net != nullptr; }
const std::string& loaded_name() { return net_name; }
void unload() { net.reset(); net_name.clear(); }

void Evaluator::reset(const Position& pos) {
    top = 0;
    Accumulator& a = stack[0];
    for (Color c : {WHITE, BLACK}) {
        std::memcpy(a.v[c], net->ft_b, sizeof(a.v[c]));
        Bitboard occ = pos.pieces();
        while (occ) {
            const Square s = pop_lsb(occ);
            add_feature(a.v[c], feature(c, pos.piece_on(s), s));
        }
    }
    computed[0] = 1;
}

void Evaluator::push_null() {
    ++top;
    deltas[top].n_add = deltas[top].n_sub = 0;
    computed[top] = 0;
}

void Evaluator::push(const Position& pos, Move m) {
    ++top;
    Delta& d = deltas[top];
    computed[top] = 0;

    const Color us = pos.side_to_move();
    const Square from = m.from(), to = m.to();
    const Piece pc = pos.piece_on(from);
    const Piece placed = m.is_promotion() ? make_piece(us, m.promo_type()) : pc;
    d.n_add = 1;
    d.n_sub = 1;
    for (Color c : {WHITE, BLACK}) {
        d.sub[c][0] = feature(c, pc, from);
        d.add[c][0] = feature(c, placed, to);
    }
    if (m.is_ep()) {
        const Square cap = Square(int(to) + (us == WHITE ? -8 : 8));
        for (Color c : {WHITE, BLACK}) d.sub[c][1] = feature(c, make_piece(~us, PAWN), cap);
        d.n_sub = 2;
    } else if (m.is_capture()) {
        for (Color c : {WHITE, BLACK}) d.sub[c][1] = feature(c, pos.piece_on(to), to);
        d.n_sub = 2;
    } else if (m.is_castle()) {
        const bool oo = m.flag() == MF_KING_CASTLE;
        const Square rfrom = relative_square(us, oo ? SQ_H1 : SQ_A1);
        const Square rto = relative_square(us, oo ? SQ_F1 : SQ_D1);
        const Piece rook = make_piece(us, ROOK);
        for (Color c : {WHITE, BLACK}) {
            d.sub[c][1] = feature(c, rook, rfrom);
            d.add[c][1] = feature(c, rook, rto);
        }
        d.n_sub = d.n_add = 2;
    }
}

// Brings stack[top] up to date from the nearest computed ancestor, one fused pass
// (copy + all adds/subs) per ply.
void Evaluator::update_to_top() {
    int j = top;
    while (!computed[j]) --j;
    for (int k = j + 1; k <= top; ++k) {
        const Delta& d = deltas[k];
        for (Color c : {WHITE, BLACK}) {
            const std::int16_t* src = stack[k - 1].v[c];
            std::int16_t* dst = stack[k].v[c];
            if (d.n_add == 0 && d.n_sub == 0) {
                std::memcpy(dst, src, sizeof(stack[k].v[c]));
                continue;
            }
            const std::int16_t* a0 = net->ft_w + d.add[c][0] * HIDDEN;
            const std::int16_t* s0 = net->ft_w + d.sub[c][0] * HIDDEN;
            if (d.n_add == 1 && d.n_sub == 1) {
                for (int i = 0; i < HIDDEN; ++i) dst[i] = std::int16_t(src[i] + a0[i] - s0[i]);
            } else if (d.n_add == 1) {  // capture / en passant
                const std::int16_t* s1 = net->ft_w + d.sub[c][1] * HIDDEN;
                for (int i = 0; i < HIDDEN; ++i) dst[i] = std::int16_t(src[i] + a0[i] - s0[i] - s1[i]);
            } else {  // castling
                const std::int16_t* a1 = net->ft_w + d.add[c][1] * HIDDEN;
                const std::int16_t* s1 = net->ft_w + d.sub[c][1] * HIDDEN;
                for (int i = 0; i < HIDDEN; ++i) dst[i] = std::int16_t(src[i] + a0[i] + a1[i] - s0[i] - s1[i]);
            }
        }
        computed[k] = 1;
    }
}

namespace {
// Sum over one perspective of clamp(acc, 0, QA)^2 * w. Each term is at most
// 255 * 255 * 127 (8.3M), so the 128-term sum (at most 1.06e9) fits in int32.
inline std::int32_t screlu_dot(const std::int16_t* acc, const std::int16_t* w) {
#if defined(__AVX2__)
    const __m256i zero = _mm256_setzero_si256();
    const __m256i qa = _mm256_set1_epi16(QA);
    __m256i sum = _mm256_setzero_si256();
    for (int i = 0; i < HIDDEN; i += 16) {
        __m256i x = _mm256_load_si256(reinterpret_cast<const __m256i*>(acc + i));
        x = _mm256_min_epi16(_mm256_max_epi16(x, zero), qa);
        const __m256i wv = _mm256_load_si256(reinterpret_cast<const __m256i*>(w + i));
        const __m256i xw = _mm256_mullo_epi16(x, wv);              // |x*w| <= 32385: fits int16
        sum = _mm256_add_epi32(sum, _mm256_madd_epi16(xw, x));     // (x*w)*x, pairwise into int32
    }
    __m128i s = _mm_add_epi32(_mm256_castsi256_si128(sum), _mm256_extracti128_si256(sum, 1));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, 0x4E));
    s = _mm_add_epi32(s, _mm_shuffle_epi32(s, 0xB1));
    return _mm_cvtsi128_si32(s);
#else
    std::int32_t sum = 0;
    for (int i = 0; i < HIDDEN; ++i) {
        const int x = std::clamp<int>(acc[i], 0, QA);
        sum += x * w[i] * x;
    }
    return sum;
#endif
}
}  // namespace

Value Evaluator::evaluate(const Position& pos) {
    if (!computed[top]) update_to_top();
    const Accumulator& a = stack[top];
    const Color us = pos.side_to_move();
    const std::int64_t sum = std::int64_t(screlu_dot(a.v[us], net->out_w)) + screlu_dot(a.v[~us], net->out_w + HIDDEN);
    // sum is in units QA*QA*QB; SCReLU output / QA, plus bias (QA*QB), then scale.
    const std::int64_t v = (sum / QA + net->out_b) * SCALE / (std::int64_t(QA) * QB);
    return Value(std::clamp<std::int64_t>(v, VALUE_MATED_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1));
}

}  // namespace apex::nnue
