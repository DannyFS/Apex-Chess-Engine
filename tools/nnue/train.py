#!/usr/bin/env python3
# Apex chess engine: NNUE trainer for "apex-768xHx2-1" networks (numpy + scipy, CPU).
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Input: one or more files of 70-byte records written by `apex "nnue-data <txt> <bin>"`.
# Output: a quantised network file loadable with `setoption name EvalFile value <file>`.
#
# Network (float domain, matching src/eval/nnue/nnue.cpp after quantisation):
#   a_p = b1 + sum_{f in features(p)} W1[f]          for p in {side to move, other side}
#   h_p = clamp(a_p, 0, 1)^2                         (SCReLU)
#   y   = h_stm . w2[:H] + h_other . w2[H:] + b2     (eval = y * SCALE centipawns)
# Loss: MSE between sigmoid(y * SCALE / 400) and
#   target = lambda * sigmoid(score_stm / 400) + (1 - lambda) * result_stm.
# Quantisation: W1, b1 x QA; w2 x QB; b2 x QA*QB. All weights are clipped to +-1.98,
# which keeps int16 accumulators and the int32 output partial sums in range.
import argparse
import struct
import sys
import time

import numpy as np
import scipy.sparse as sp

INPUTS, QA, QB, SCALE = 768, 255, 64, 400
HIDDEN = 128  # set from --hidden in main(); must match the engine build (CMake APEX_NNUE_HIDDEN)
CLIP = 1.98
REC = np.dtype([("n", "u1"), ("stm", "u1"), ("score", "<i2"), ("result", "u1"), ("pad", "u1"), ("f", "<u2", (32,))])


def load(paths):
    arrs = [np.fromfile(p, dtype=REC) for p in paths]
    return np.concatenate(arrs) if len(arrs) > 1 else arrs[0]


def flip(f):
    """White-perspective feature index -> black-perspective index."""
    rel, rest = f // 384, f % 384
    t, s = rest // 64, rest % 64
    return (1 - rel) * 384 + t * 64 + (s ^ 56)


def batch_matrices(rec):
    """CSR matrices (B x 768) for the side-to-move and other-side perspectives."""
    B = len(rec)
    f = rec["f"].astype(np.int64)
    mask = f != 0xFFFF
    rows = np.repeat(np.arange(B), 32).reshape(B, 32)[mask]
    fw = f[mask]
    fb = flip(fw)
    stm_black = (rec["stm"] == 1)[rows]
    stm_cols = np.where(stm_black, fb, fw)
    oth_cols = np.where(stm_black, fw, fb)
    ones = np.ones(len(rows), dtype=np.float32)
    Xs = sp.csr_matrix((ones, (rows, stm_cols)), shape=(B, INPUTS))
    Xo = sp.csr_matrix((ones, (rows, oth_cols)), shape=(B, INPUTS))
    return Xs, Xo


def targets(rec, lam):
    sign = np.where(rec["stm"] == 0, 1.0, -1.0)
    score_stm = rec["score"].astype(np.float32) * sign
    res_white = rec["result"].astype(np.float32) / 2.0
    res_stm = np.where(rec["stm"] == 0, res_white, 1.0 - res_white)
    return (lam / (1.0 + np.exp(-score_stm / 400.0)) + (1.0 - lam) * res_stm).astype(np.float32)


class Net:
    def __init__(self, rng):
        self.W1 = (rng.standard_normal((INPUTS, HIDDEN)) * 0.08).astype(np.float32)
        self.b1 = np.full(HIDDEN, 0.1, dtype=np.float32)
        self.w2 = (rng.standard_normal(2 * HIDDEN) * 0.05).astype(np.float32)
        self.b2 = np.zeros(1, dtype=np.float32)

    def params(self):
        return [self.W1, self.b1, self.w2, self.b2]

    def forward(self, Xs, Xo):
        As = Xs @ self.W1 + self.b1
        Ao = Xo @ self.W1 + self.b1
        Cs, Co = np.clip(As, 0, 1), np.clip(Ao, 0, 1)
        Hs, Ho = Cs * Cs, Co * Co
        y = Hs @ self.w2[:HIDDEN] + Ho @ self.w2[HIDDEN:] + self.b2[0]
        return y, (As, Ao, Cs, Co, Hs, Ho)

    def loss_grad(self, Xs, Xo, t):
        y, (As, Ao, Cs, Co, Hs, Ho) = self.forward(Xs, Xo)
        p = 1.0 / (1.0 + np.exp(-y * SCALE / 400.0))
        B = len(t)
        loss = float(np.mean((p - t) ** 2))
        dy = (2.0 * (p - t) * p * (1.0 - p) * (SCALE / 400.0) / B).astype(np.float32)
        gw2 = np.concatenate([Hs.T @ dy, Ho.T @ dy])
        gb2 = np.array([dy.sum()], dtype=np.float32)
        dAs = np.outer(dy, self.w2[:HIDDEN]) * (2.0 * Cs) * ((As > 0) & (As < 1))
        dAo = np.outer(dy, self.w2[HIDDEN:]) * (2.0 * Co) * ((Ao > 0) & (Ao < 1))
        gW1 = np.asarray(Xs.T @ dAs + Xo.T @ dAo, dtype=np.float32)
        gb1 = (dAs.sum(axis=0) + dAo.sum(axis=0)).astype(np.float32)
        return loss, [gW1, gb1, gw2, gb2]

    def loss(self, Xs, Xo, t):
        y, _ = self.forward(Xs, Xo)
        p = 1.0 / (1.0 + np.exp(-y * SCALE / 400.0))
        return float(np.mean((p - t) ** 2))


def export(net, path):
    q = lambda a, s: np.clip(np.rint(a * s), -32768, 32767).astype("<i2")
    with open(path, "wb") as f:
        f.write(b"APXN")
        f.write(struct.pack("<III", 1, INPUTS, HIDDEN))
        f.write(q(net.W1, QA).tobytes())
        f.write(q(net.b1, QA).tobytes())
        f.write(q(net.w2, QB).tobytes())
        f.write(q(net.b2, QA * QB).tobytes())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("data", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--epochs", type=int, default=20)
    ap.add_argument("--batch", type=int, default=16384)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--lr-drop", type=float, default=0.3, help="multiply lr by this at 75%% of the epochs")
    ap.add_argument("--wdl", type=float, default=0.25, help="weight of the game result in the target")
    ap.add_argument("--valid", type=float, default=0.01)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--hidden", type=int, default=128, help="hidden layer size (multiple of 128)")
    ap.add_argument("--init", help="start from this network file instead of random weights")
    args = ap.parse_args()
    global HIDDEN
    HIDDEN = args.hidden

    t0 = time.time()
    data = load(args.data)
    rng = np.random.default_rng(args.seed)
    perm = rng.permutation(len(data))
    nv = max(1, int(len(data) * args.valid))
    valid, train = data[perm[:nv]], data[perm[nv:]]
    print(f"{len(train)} train / {len(valid)} valid positions, loaded in {time.time() - t0:.0f}s", flush=True)
    Vs, Vo = batch_matrices(valid)
    vt = targets(valid, 1.0 - args.wdl)

    net = Net(rng)
    if args.init:
        raw = open(args.init, "rb").read()
        if raw[:4] != b"APXN" or struct.unpack("<III", raw[4:16]) != (1, INPUTS, HIDDEN):
            sys.exit(f"--init {args.init}: not an apex-768x{HIDDEN}x2-1 network")
        o = 16
        def take(n, scale):
            nonlocal o
            a = np.frombuffer(raw, "<i2", n, o).astype(np.float32) / scale
            o += 2 * n
            return a
        net.W1 = take(INPUTS * HIDDEN, QA).reshape(INPUTS, HIDDEN).copy()
        net.b1 = take(HIDDEN, QA).copy()
        net.w2 = take(2 * HIDDEN, QB).copy()
        net.b2 = take(1, QA * QB).copy()
        print(f"initialised from {args.init}", flush=True)
    m = [np.zeros_like(p) for p in net.params()]
    v = [np.zeros_like(p) for p in net.params()]
    b1, b2, eps, step = 0.9, 0.999, 1e-8, 0
    lr = args.lr
    for epoch in range(1, args.epochs + 1):
        if epoch == int(args.epochs * 0.75) + 1:
            lr *= args.lr_drop
        order = rng.permutation(len(train))
        te, tl, nb = time.time(), 0.0, 0
        for i in range(0, len(order) - args.batch + 1, args.batch):
            rec = train[order[i:i + args.batch]]
            Xs, Xo = batch_matrices(rec)
            loss, grads = net.loss_grad(Xs, Xo, targets(rec, 1.0 - args.wdl))
            step += 1
            for k, (p, g) in enumerate(zip(net.params(), grads)):
                m[k] = b1 * m[k] + (1 - b1) * g
                v[k] = b2 * v[k] + (1 - b2) * g * g
                p -= lr * (m[k] / (1 - b1 ** step)) / (np.sqrt(v[k] / (1 - b2 ** step)) + eps)
                np.clip(p, -CLIP, CLIP, out=p)
            tl += loss
            nb += 1
        vl = net.loss(Vs, Vo, vt)
        print(f"epoch {epoch}: train {tl / max(nb, 1):.6f} valid {vl:.6f} lr {lr:.1e} ({time.time() - te:.0f}s)", flush=True)
        export(net, args.out)
    print(f"wrote {args.out}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
