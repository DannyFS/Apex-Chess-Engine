#!/usr/bin/env python3
import argparse
import json
import os
import random
import sys

import chess
import chess.pgn
import chess.engine

try:
    import zstandard as zstd
except ImportError:
    zstd = None


def open_writer(path):
    if path.endswith('.zst') and zstd is not None:
        cctx = zstd.ZstdCompressor(level=3)
        f = open(path, 'wb')
        return cctx.stream_writer(f)
    return open(path, 'w', encoding='utf-8')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pgn')
    ap.add_argument('--out', required=True)
    ap.add_argument('--positions-per-game', type=int, default=4)
    ap.add_argument('--max-games', type=int, default=1000)
    ap.add_argument('--shard-size', type=int, default=10000)
    ap.add_argument('--seed', type=int, default=1234)
    ap.add_argument('--selfplay-engine')
    ap.add_argument('--selfplay-games', type=int, default=100)
    ap.add_argument('--selfplay-movetime', type=int, default=100)
    args = ap.parse_args()

    random.seed(args.seed)
    os.makedirs(args.out, exist_ok=True)

    shard_idx = 0
    count = 0
    writer = None

    def next_writer():
        nonlocal shard_idx, writer
        if writer:
            writer.flush()
            writer.close()
        shard_path = os.path.join(args.out, f'positions_{shard_idx:04d}.jsonl.zst')
        writer = open_writer(shard_path)
        shard_idx += 1

    next_writer()

    if args.selfplay_engine:
        engine = chess.engine.SimpleEngine.popen_uci(args.selfplay_engine)
        for g in range(args.selfplay_games):
            board = chess.Board()
            positions = []
            ply = 0
            while not board.is_game_over():
                result = engine.play(board, chess.engine.Limit(time=args.selfplay_movetime / 1000.0))
                board.push(result.move)
                ply += 1
                positions.append((board.fen(), ply))
            outcome = 0
            res = board.result()
            if res == '1-0':
                outcome = 1
            elif res == '0-1':
                outcome = -1
            samples = random.sample(positions, min(args.positions_per_game, len(positions)))
            for fen, ply in samples:
                record = {'fen': fen, 'ply': ply, 'outcome': outcome}
                line = json.dumps(record)
                if isinstance(writer, (zstd.ZstdCompressionWriter,)):
                    writer.write((line + '\n').encode('utf-8'))
                else:
                    writer.write(line + '\n')
                count += 1
                if count % args.shard_size == 0:
                    next_writer()
        engine.quit()
    else:
        if not args.pgn:
            print('Either --pgn or --selfplay-engine is required', file=sys.stderr)
            return
        with open(args.pgn, 'r', encoding='utf-8', errors='ignore') as f:
            game_count = 0
            while game_count < args.max_games:
                game = chess.pgn.read_game(f)
                if game is None:
                    break
                game_count += 1

                board = game.board()
                positions = []
                ply = 0
                for move in game.mainline_moves():
                    board.push(move)
                    ply += 1
                    if board.is_game_over():
                        break
                    positions.append((board.fen(), ply))

                if not positions:
                    continue

                samples = random.sample(positions, min(args.positions_per_game, len(positions)))
                result = game.headers.get('Result', '*')
                outcome = 0
                if result == '1-0':
                    outcome = 1
                elif result == '0-1':
                    outcome = -1
                elif result == '1/2-1/2':
                    outcome = 0

                for fen, ply in samples:
                    record = {
                        'fen': fen,
                        'ply': ply,
                        'outcome': outcome,
                    }
                    line = json.dumps(record)
                    if isinstance(writer, (zstd.ZstdCompressionWriter,)):
                        writer.write((line + '\n').encode('utf-8'))
                    else:
                        writer.write(line + '\n')
                    count += 1
                    if count % args.shard_size == 0:
                        next_writer()

    if writer:
        writer.flush()
        writer.close()

    print(f'Wrote {count} positions into {shard_idx} shards')


if __name__ == '__main__':
    main()
