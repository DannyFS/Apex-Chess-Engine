#!/usr/bin/env python3
import argparse
import json
import math
import os
import random

import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import Dataset, DataLoader

try:
    import zstandard as zstd
except ImportError:
    zstd = None

import chess

INPUT_DIM = 64 * 10 * 64
H1 = 256
H2 = 32

PIECE_INDEX = {
    chess.PAWN: 0,
    chess.KNIGHT: 1,
    chess.BISHOP: 2,
    chess.ROOK: 3,
    chess.QUEEN: 4,
}


def piece_index(piece: chess.Piece):
    if piece.piece_type == chess.KING:
        return None
    idx = PIECE_INDEX[piece.piece_type]
    if piece.color == chess.BLACK:
        idx += 5
    return idx


def feature_index(king_sq, piece, sq):
    idx = piece_index(piece)
    if idx is None:
        return None
    return king_sq * (10 * 64) + idx * 64 + sq


def open_reader(path):
    if path.endswith('.zst') and zstd is not None:
        f = open(path, 'rb')
        dctx = zstd.ZstdDecompressor()
        return dctx.stream_reader(f)
    return open(path, 'r', encoding='utf-8')


class JsonlDataset(Dataset):
    def __init__(self, files):
        self.samples = []
        for path in files:
            with open_reader(path) as f:
                for raw in f:
                    if isinstance(raw, bytes):
                        raw = raw.decode('utf-8')
                    raw = raw.strip()
                    if not raw:
                        continue
                    self.samples.append(json.loads(raw))

    def __len__(self):
        return len(self.samples)

    def __getitem__(self, idx):
        rec = self.samples[idx]
        board = chess.Board(rec['fen'])
        wking = board.king(chess.WHITE)
        bking = board.king(chess.BLACK)
        feat_w = []
        feat_b = []
        for sq, piece in board.piece_map().items():
            if piece.piece_type == chess.KING:
                continue
            fi_w = feature_index(wking, piece, sq)
            fi_b = feature_index(bking, piece, sq)
            if fi_w is not None:
                feat_w.append(fi_w)
            if fi_b is not None:
                feat_b.append(fi_b)
        stm = 0 if board.turn == chess.WHITE else 1
        score = rec.get('score_cp', 0)
        wdl = rec.get('wdl', None)
        return feat_w, feat_b, stm, float(score), wdl


def collate(batch):
    feat_w_all = []
    feat_b_all = []
    offsets_w = [0]
    offsets_b = [0]
    stm = []
    score = []
    wdl = []

    for fw, fb, s, sc, wd in batch:
        feat_w_all.extend(fw)
        offsets_w.append(len(feat_w_all))
        feat_b_all.extend(fb)
        offsets_b.append(len(feat_b_all))
        stm.append(s)
        score.append(sc)
        wdl.append(wd)

    return (
        torch.tensor(feat_w_all, dtype=torch.long),
        torch.tensor(offsets_w[:-1], dtype=torch.long),
        torch.tensor(feat_b_all, dtype=torch.long),
        torch.tensor(offsets_b[:-1], dtype=torch.long),
        torch.tensor(stm, dtype=torch.long),
        torch.tensor(score, dtype=torch.float32),
        wdl,
    )


class NNUEModel(nn.Module):
    def __init__(self, input_dim=INPUT_DIM, h1=H1, h2=H2):
        super().__init__()
        self.embed = nn.EmbeddingBag(input_dim, h1, mode='sum')
        self.b1 = nn.Parameter(torch.zeros(h1))
        self.fc2 = nn.Linear(h1 * 2, h2)
        self.fc3 = nn.Linear(h2, 1)

    def forward(self, feat_w, off_w, feat_b, off_b, stm):
        h1_w = self.embed(feat_w, off_w) + self.b1
        h1_b = self.embed(feat_b, off_b) + self.b1
        h1_w = torch.clamp(h1_w, 0.0, 127.0)
        h1_b = torch.clamp(h1_b, 0.0, 127.0)
        h1_cat = torch.cat([h1_w, h1_b], dim=1)
        h1_swap = torch.cat([h1_b, h1_w], dim=1)
        stm = stm.view(-1, 1).float()
        h1 = h1_cat * (1.0 - stm) + h1_swap * stm
        h2 = F.relu(self.fc2(h1))
        out = self.fc3(h2).squeeze(1)
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--data', nargs='+', required=True)
    ap.add_argument('--epochs', type=int, default=5)
    ap.add_argument('--batch-size', type=int, default=256)
    ap.add_argument('--lr', type=float, default=1e-3)
    ap.add_argument('--mse-weight', type=float, default=1.0)
    ap.add_argument('--ce-weight', type=float, default=0.1)
    ap.add_argument('--device', default='cuda' if torch.cuda.is_available() else 'cpu')
    ap.add_argument('--out', default='nnue.pt')
    args = ap.parse_args()

    dataset = JsonlDataset(args.data)
    loader = DataLoader(dataset, batch_size=args.batch_size, shuffle=True, collate_fn=collate, num_workers=2)

    model = NNUEModel().to(args.device)
    opt = torch.optim.AdamW(model.parameters(), lr=args.lr)
    scaler = torch.cuda.amp.GradScaler(enabled=(args.device.startswith('cuda')))

    for epoch in range(args.epochs):
        total_loss = 0.0
        for feat_w, off_w, feat_b, off_b, stm, score, wdl in loader:
            feat_w = feat_w.to(args.device)
            off_w = off_w.to(args.device)
            feat_b = feat_b.to(args.device)
            off_b = off_b.to(args.device)
            stm = stm.to(args.device)
            score = score.to(args.device)

            with torch.cuda.amp.autocast(enabled=(args.device.startswith('cuda'))):
                pred = model(feat_w, off_w, feat_b, off_b, stm)
                mse = F.mse_loss(pred, score)
                loss = mse * args.mse_weight

                if wdl[0] is not None:
                    # WDL loss if present
                    wdl_targets = []
                    for w in wdl:
                        if w is None:
                            wdl_targets.append([1,0,0])
                        else:
                            wdl_targets.append(w)
                    wdl_t = torch.tensor(wdl_targets, dtype=torch.float32, device=args.device)
                    wdl_t = wdl_t / wdl_t.sum(dim=1, keepdim=True)
                    logits = pred.unsqueeze(1).repeat(1,3)
                    ce = -(wdl_t * F.log_softmax(logits, dim=1)).sum(dim=1).mean()
                    loss = loss + ce * args.ce_weight

            opt.zero_grad(set_to_none=True)
            scaler.scale(loss).backward()
            scaler.step(opt)
            scaler.update()
            total_loss += loss.item()

        print(f"epoch {epoch+1} loss {total_loss/len(loader):.4f}")

    torch.save({'model': model.state_dict()}, args.out)
    print(f"saved {args.out}")


if __name__ == '__main__':
    main()
