#!/usr/bin/env python3
import argparse
import chess
import chess.engine
import random


def play_game(engine_a, engine_b, time_ms, inc_ms):
    board = chess.Board()
    engines = [engine_a, engine_b]
    while not board.is_game_over():
        idx = 0 if board.turn == chess.WHITE else 1
        limit = chess.engine.Limit(time=time_ms/1000.0, increment=inc_ms/1000.0)
        result = engines[idx].play(board, limit)
        board.push(result.move)
    return board.result()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--engine-a', required=True)
    ap.add_argument('--engine-b', required=True)
    ap.add_argument('--games', type=int, default=20)
    ap.add_argument('--time', type=int, default=100)
    ap.add_argument('--inc', type=int, default=0)
    args = ap.parse_args()

    eng_a = chess.engine.SimpleEngine.popen_uci(args.engine_a)
    eng_b = chess.engine.SimpleEngine.popen_uci(args.engine_b)

    score_a = 0
    for g in range(args.games):
        if g % 2 == 0:
            res = play_game(eng_a, eng_b, args.time, args.inc)
            if res == '1-0': score_a += 1
            elif res == '1/2-1/2': score_a += 0.5
        else:
            res = play_game(eng_b, eng_a, args.time, args.inc)
            if res == '0-1': score_a += 1
            elif res == '1/2-1/2': score_a += 0.5
        print(f"game {g+1}/{args.games} result {res} scoreA {score_a}")

    eng_a.quit(); eng_b.quit()
    print(f"Final score A: {score_a}/{args.games}")


if __name__ == '__main__':
    main()
