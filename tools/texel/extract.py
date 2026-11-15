#!/usr/bin/env python3
# Apex chess engine: extract Texel-tuning positions from self-play PGNs.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Output: one line per position, "<fen> | <result>" with result from White's point
# of view (1.0, 0.5, 0.0). Positions are kept only if they are "quiet enough" for
# a static evaluation to be judged against the game result:
#   - at least --skip plies after the game's start position (the book exit),
#   - side to move not in check,
#   - the move actually played is not a capture or a promotion,
#   - fewer than --max-plies plies played (very long shuffles add little signal).
# Duplicate positions (same placement, side, castling, ep) are kept once.
import argparse
import sys

import chess
import chess.pgn

RESULTS = {"1-0": 1.0, "0-1": 0.0, "1/2-1/2": 0.5}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pgns", nargs="+")
    ap.add_argument("--out", required=True)
    ap.add_argument("--skip", type=int, default=8)
    ap.add_argument("--max-plies", type=int, default=300)
    args = ap.parse_args()

    seen = set()
    games = kept = 0
    with open(args.out, "w") as out:
        for path in args.pgns:
            with open(path) as f:
                while True:
                    game = chess.pgn.read_game(f)
                    if game is None:
                        break
                    res = RESULTS.get(game.headers.get("Result", "*"))
                    if res is None:
                        continue
                    games += 1
                    board = game.board()
                    for ply, move in enumerate(game.mainline_moves()):
                        if ply >= args.max_plies:
                            break
                        if (ply >= args.skip and not board.is_check() and not board.is_capture(move)
                                and move.promotion is None):
                            key = board.epd()
                            if key not in seen:
                                seen.add(key)
                                out.write(f"{board.fen()} | {res}\n")
                                kept += 1
                        board.push(move)
                    if games % 1000 == 0:
                        print(f"{games} games, {kept} positions", file=sys.stderr, flush=True)
    print(f"done: {games} games, {kept} positions", file=sys.stderr)


if __name__ == "__main__":
    main()
