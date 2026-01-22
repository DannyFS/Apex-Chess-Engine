#!/usr/bin/env python3
import argparse
import json
import os
import sys

import chess
import chess.engine

try:
    import zstandard as zstd
except ImportError:
    zstd = None


def open_reader(path):
    if path.endswith('.zst') and zstd is not None:
        f = open(path, 'rb')
        dctx = zstd.ZstdDecompressor()
        return dctx.stream_reader(f)
    return open(path, 'r', encoding='utf-8')


def open_writer(path):
    if path.endswith('.zst') and zstd is not None:
        cctx = zstd.ZstdCompressor(level=3)
        f = open(path, 'wb')
        return cctx.stream_writer(f)
    return open(path, 'w', encoding='utf-8')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--engine', required=True)
    ap.add_argument('--input', required=True)
    ap.add_argument('--output', required=True)
    ap.add_argument('--depth', type=int, default=12)
    ap.add_argument('--movetime', type=int, default=0)
    ap.add_argument('--threads', type=int, default=1)
    args = ap.parse_args()

    engine = chess.engine.SimpleEngine.popen_uci(args.engine)
    engine.configure({'Threads': args.threads})

    reader = open_reader(args.input)
    writer = open_writer(args.output)

    def write_line(line):
        if isinstance(writer, (zstd.ZstdCompressionWriter,)):
            writer.write((line + '\n').encode('utf-8'))
        else:
            writer.write(line + '\n')

    count = 0
    for raw in reader:
        if isinstance(raw, bytes):
            raw = raw.decode('utf-8')
        raw = raw.strip()
        if not raw:
            continue
        rec = json.loads(raw)
        board = chess.Board(rec['fen'])
        limit = chess.engine.Limit(depth=args.depth) if args.movetime == 0 else chess.engine.Limit(time=args.movetime / 1000.0)
        info = engine.analyse(board, limit)
        score = info['score'].pov(board.turn)
        cp = score.score(mate_score=10000)
        wdl = score.wdl(model='lichess') if hasattr(score, 'wdl') else None
        rec['score_cp'] = int(cp)
        if wdl is not None:
            rec['wdl'] = [wdl.wins, wdl.draws, wdl.losses]
        write_line(json.dumps(rec))
        count += 1
        if count % 1000 == 0:
            print(f'labeled {count}', file=sys.stderr)

    engine.quit()
    writer.flush(); writer.close()
    if hasattr(reader, 'close'): reader.close()


if __name__ == '__main__':
    main()
