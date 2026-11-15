#!/usr/bin/env python3
# Apex chess engine: Texel tuner for the linear (mg, eg) evaluation weights.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Input: the output of `apex "texel-features <positions> <features>"` and the current
# src/eval/weights.inc. Output: a new weights.inc.
#
# Model (matches src/eval/eval.cpp exactly, before integer rounding):
#   e_white = (sum_i c_i * mg_i * phase + sum_i c_i * eg_i * (24 - phase)) / 24
#   v_white = (e_white + (+15 if white to move else -15)) * (200 - rule50) / 200
#   p_white = 1 / (1 + 10^(-K * v_white / 400))
# Loss: mean squared error between p_white and the game result (1, 0.5, 0), plus an
# optional L2 penalty l2 * sum((w - w_start)^2). Adam scales every weight's step the
# same way, so without the penalty weights of rare features (a queen with 25 moves)
# overfit to a handful of positions.
# K is fitted first with the starting weights and then frozen, so the tuner cannot
# "win" by rescaling the whole evaluation.
#
# Requires numpy and scipy.
import argparse
import re
import sys
import time

import numpy as np
import scipy.sparse as sp

TEMPO = 15
LN10 = np.log(10.0)


def load_features(path, num):
    rows, cols, vals = [], [], []
    res, phase, stm, r50 = [], [], [], []
    with open(path) as f:
        for r, line in enumerate(f):
            t = line.split()
            res.append(float(t[0]))
            phase.append(int(t[1]))
            stm.append(int(t[2]))
            r50.append(int(t[3]))
            for tok in t[4:]:
                i, c = tok.split(":")
                rows.append(r)
                cols.append(int(i))
                vals.append(int(c))
    n = len(res)
    X = sp.csr_matrix((np.array(vals, dtype=np.float64), (np.array(rows), np.array(cols))), shape=(n, num))
    return X, np.array(res), np.array(phase, dtype=np.float64), np.array(stm), np.array(r50, dtype=np.float64)


def load_weights(path):
    text = open(path).read()
    pairs = re.findall(r"S\((-?\d+),\s*(-?\d+)\)", text)
    w = np.array([[int(a), int(b)] for a, b in pairs], dtype=np.float64)
    return w[:, 0].copy(), w[:, 1].copy()


def write_weights(path, mg, eg, note):
    lines = [
        "// Apex chess engine: evaluation weights (mg, eg), generated.",
        "// SPDX-License-Identifier: GPL-3.0-or-later",
        f"// Layout: see eval_layout.h. Source: {note}",
        "// clang-format off",
    ]
    row = []
    for a, b in zip(np.rint(mg).astype(int), np.rint(eg).astype(int)):
        row.append(f"S({a},{b})")
        if len(row) == 8:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("// clang-format on")
    open(path, "w").write("\n".join(lines) + "\n")


class Model:
    def __init__(self, X, res, phase, stm, r50):
        self.X, self.XT, self.y = X, X.T.tocsr(), res
        self.fm = phase / 24.0
        self.fe = 1.0 - self.fm
        self.damp = (200.0 - r50) / 200.0
        self.tempo = np.where(stm == 1, TEMPO, -TEMPO).astype(np.float64)

    def value(self, mg, eg):
        e = self.fm * (self.X @ mg) + self.fe * (self.X @ eg)
        return (e + self.tempo) * self.damp

    def loss(self, mg, eg, K):
        p = 1.0 / (1.0 + np.power(10.0, -K * self.value(mg, eg) / 400.0))
        return np.mean((p - self.y) ** 2)

    def grad(self, mg, eg, K):
        v = self.value(mg, eg)
        p = 1.0 / (1.0 + np.power(10.0, -K * v / 400.0))
        # d loss / d v
        g = 2.0 * (p - self.y) * p * (1.0 - p) * (K * LN10 / 400.0) * self.damp / len(self.y)
        return self.XT @ (g * self.fm), self.XT @ (g * self.fe), np.mean((p - self.y) ** 2)


def fit_k(model, mg, eg):
    lo, hi = 0.1, 5.0
    for _ in range(60):  # golden-section search
        a = hi - (hi - lo) / 1.618
        b = lo + (hi - lo) / 1.618
        if model.loss(mg, eg, a) < model.loss(mg, eg, b):
            hi = b
        else:
            lo = a
    return (lo + hi) / 2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("features")
    ap.add_argument("--weights", default="src/eval/weights.inc")
    ap.add_argument("--out", required=True)
    ap.add_argument("--iters", type=int, default=3000)
    ap.add_argument("--lr", type=float, default=1.0)
    ap.add_argument("--valid", type=float, default=0.1, help="held-out fraction")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--note", default="Texel-tuned")
    ap.add_argument("--l2", type=float, default=0.0,
                    help="L2 penalty toward the starting weights (shrinks weights of rare features)")
    args = ap.parse_args()

    mg, eg = load_weights(args.weights)
    num = len(mg)
    t0 = time.time()
    X, res, phase, stm, r50 = load_features(args.features, num)
    print(f"loaded {X.shape[0]} positions, {X.nnz} nonzeros, {num} weights in {time.time() - t0:.0f}s", flush=True)

    rng = np.random.default_rng(args.seed)
    perm = rng.permutation(X.shape[0])
    nv = int(len(perm) * args.valid)
    vi, ti = perm[:nv], perm[nv:]
    train = Model(X[ti], res[ti], phase[ti], stm[ti], r50[ti])
    valid = Model(X[vi], res[vi], phase[vi], stm[vi], r50[vi])

    K = fit_k(train, mg, eg)
    print(f"K = {K:.4f}; start loss train {train.loss(mg, eg, K):.6f} valid {valid.loss(mg, eg, K):.6f}", flush=True)

    # Weights whose feature never occurs stay as they are (e.g. king material, pawns on rank 1/8).
    used = np.asarray((abs(X) > 0).sum(axis=0)).ravel() > 0
    params = np.concatenate([mg, eg])
    start = params.copy()
    mask = np.concatenate([used, used]).astype(np.float64)
    m = np.zeros_like(params)
    v = np.zeros_like(params)
    b1, b2, eps = 0.9, 0.999, 1e-8
    best = (valid.loss(mg, eg, K), params.copy(), 0)
    for it in range(1, args.iters + 1):
        gm, ge, tl = train.grad(params[:num], params[num:], K)
        g = (np.concatenate([gm, ge]) + 2.0 * args.l2 * (params - start)) * mask
        m = b1 * m + (1 - b1) * g
        v = b2 * v + (1 - b2) * g * g
        params -= args.lr * (m / (1 - b1 ** it)) / (np.sqrt(v / (1 - b2 ** it)) + eps)
        if it % 100 == 0 or it == args.iters:
            vl = valid.loss(params[:num], params[num:], K)
            if vl < best[0]:
                best = (vl, params.copy(), it)
            print(f"iter {it}: train {tl:.6f} valid {vl:.6f}", flush=True)
    vl, params, it = best
    print(f"best valid loss {vl:.6f} at iter {it}", flush=True)
    write_weights(args.out, params[:num], params[num:], f"{args.note} (K={K:.4f}, valid MSE {vl:.6f}, iter {it})")


if __name__ == "__main__":
    sys.exit(main())
