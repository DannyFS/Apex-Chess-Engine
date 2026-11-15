// Apex chess engine: NNUE evaluation (M8).
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nnue.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

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

bool load(const std::string& path, std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { error = "cannot open " + path; return false; }
    std::vector<unsigned char> data;
    unsigned char buf[65536];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.insert(data.end(), buf, buf + n);
    std::fclose(f);

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
    const std::size_t slash = path.find_last_of("/\\");
    net_name = (slash == std::string::npos ? path : path.substr(slash + 1)) + " (fnv " + id + ")";
    return true;
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
}

void Evaluator::push_null() {
    stack[top + 1] = stack[top];
    ++top;
}

void Evaluator::push(const Position& pos, Move m) {
    Accumulator& dst = stack[top + 1];
    dst = stack[top];
    ++top;

    const Color us = pos.side_to_move();
    const Square from = m.from(), to = m.to();
    const Piece pc = pos.piece_on(from);
    const Piece placed = m.is_promotion() ? make_piece(us, m.promo_type()) : pc;

    for (Color c : {WHITE, BLACK}) {
        std::int16_t* a = dst.v[c];
        sub_feature(a, feature(c, pc, from));
        add_feature(a, feature(c, placed, to));
        if (m.is_ep()) {
            const Square cap = Square(int(to) + (us == WHITE ? -8 : 8));
            sub_feature(a, feature(c, make_piece(~us, PAWN), cap));
        } else if (m.is_capture()) {
            sub_feature(a, feature(c, pos.piece_on(to), to));
        } else if (m.is_castle()) {
            const bool oo = m.flag() == MF_KING_CASTLE;
            const Square rfrom = relative_square(us, oo ? SQ_H1 : SQ_A1);
            const Square rto = relative_square(us, oo ? SQ_F1 : SQ_D1);
            const Piece rook = make_piece(us, ROOK);
            sub_feature(a, feature(c, rook, rfrom));
            add_feature(a, feature(c, rook, rto));
        }
    }
}

Value Evaluator::evaluate(const Position& pos) const {
    const Accumulator& a = stack[top];
    const Color us = pos.side_to_move();
    const std::int16_t* sides[2] = {a.v[us], a.v[~us]};
    std::int64_t sum = 0;
    for (int s = 0; s < 2; ++s) {
        const std::int16_t* acc = sides[s];
        const std::int16_t* w = net->out_w + s * HIDDEN;
        // Each term x*w*x is at most 255 * 127 * 255 (8.3M); 16 terms fit in int32.
        std::int32_t part = 0;
        for (int i = 0; i < HIDDEN; ++i) {
            const int x = std::clamp<int>(acc[i], 0, QA);
            part += x * w[i] * x;
            if ((i & 15) == 15) { sum += part; part = 0; }
        }
        sum += part;
    }
    // sum is in units QA*QA*QB; SCReLU output / QA, plus bias (QA*QB), then scale.
    const std::int64_t v = (sum / QA + net->out_b) * SCALE / (std::int64_t(QA) * QB);
    return Value(std::clamp<std::int64_t>(v, VALUE_MATED_IN_MAX_PLY + 1, VALUE_MATE_IN_MAX_PLY - 1));
}

}  // namespace apex::nnue
