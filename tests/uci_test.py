#!/usr/bin/env python3
# Apex chess engine: UCI conformance and search-sanity tests.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Usage: python3 tests/uci_test.py path/to/apex
# Requires python-chess (pip install chess) to verify move legality independently.
import subprocess
import sys
import time

import chess

ENGINE = sys.argv[1] if len(sys.argv) > 1 else "./build/apex"
failures = 0


def check(cond, msg):
    global failures
    if not cond:
        failures += 1
        print("FAIL:", msg)


class Engine:
    def __init__(self, path):
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

    def send(self, cmd):
        self.p.stdin.write(cmd + "\n")
        self.p.stdin.flush()

    def read_until(self, prefix, timeout=30.0):
        lines = []
        deadline = time.time() + timeout
        while time.time() < deadline:
            line = self.p.stdout.readline()
            if not line:
                break
            line = line.strip()
            lines.append(line)
            if line.startswith(prefix):
                return lines
        raise TimeoutError(f"no '{prefix}' within {timeout}s; got {lines[-5:]}")

    def bestmove(self, timeout=30.0):
        lines = self.read_until("bestmove", timeout)
        return lines[-1].split()[1], lines

    def close(self):
        try:
            self.send("quit")
            self.p.wait(timeout=5)
        except Exception:
            self.p.kill()


def legal_in(fen_or_board, uci):
    board = fen_or_board if isinstance(fen_or_board, chess.Board) else chess.Board(fen_or_board)
    try:
        return chess.Move.from_uci(uci) in board.legal_moves
    except ValueError:
        return False


def test_handshake(e):
    e.send("uci")
    lines = e.read_until("uciok", 5)
    check(any(l.startswith("id name Apex") for l in lines), "id name missing")
    check(any("option name Hash" in l for l in lines), "Hash option missing")
    e.send("isready")
    e.read_until("readyok", 5)
    e.send("setoption name Hash value 32")
    e.send("setoption name Move Overhead value 20")
    e.send("ucinewgame")
    e.send("isready")
    e.read_until("readyok", 5)


def test_limits(e):
    # depth
    e.send("position startpos")
    e.send("go depth 5")
    bm, lines = e.bestmove(30)
    check(legal_in(chess.Board(), bm), f"depth bestmove illegal: {bm}")
    check(any(" depth 5 " in l for l in lines), "no depth 5 info line")
    # nodes
    e.send("go nodes 5000")
    bm, _ = e.bestmove(30)
    check(legal_in(chess.Board(), bm), f"nodes bestmove illegal: {bm}")
    # movetime is respected (with slack)
    t0 = time.time()
    e.send("go movetime 300")
    bm, _ = e.bestmove(5)
    dt = time.time() - t0
    check(dt < 0.6, f"movetime 300 took {dt:.3f}s")
    # clock: tiny time must not flag
    t0 = time.time()
    e.send("position startpos moves e2e4 e7e5")
    e.send("go wtime 200 btime 200 winc 0 binc 0")
    bm, _ = e.bestmove(5)
    dt = time.time() - t0
    check(dt < 0.2, f"wtime 200 took {dt:.3f}s")
    check(legal_in(chess.Board("rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"), bm),
          f"clock bestmove illegal {bm}")


def test_infinite_stop(e):
    e.send("position startpos")
    e.send("go infinite")
    time.sleep(0.3)
    # bestmove must not arrive before stop
    e.send("isready")
    lines = e.read_until("readyok", 5)
    check(not any(l.startswith("bestmove") for l in lines), "bestmove before stop in infinite mode")
    t0 = time.time()
    e.send("stop")
    bm, _ = e.bestmove(5)
    check(time.time() - t0 < 0.5, "stop was slow")
    check(legal_in(chess.Board(), bm), f"infinite bestmove illegal {bm}")


def test_positions(e):
    # Long move list including castling, en passant and promotion.
    moves = "e2e4 d7d5 e4d5 c7c5 d5c6 b7b6 c6c7 g8f6 c7d8q e8d8 g1f3 c8g4 f1e2 e7e6 e1g1".split()
    e.send("position startpos moves " + " ".join(moves))
    e.send("go depth 4")
    bm, _ = e.bestmove(30)
    b = chess.Board()
    for m in moves:
        b.push_uci(m)
    check(legal_in(b, bm), f"bestmove {bm} illegal after long move list")
    # FEN with moves
    fen = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
    e.send(f"position fen {fen} moves e1c1 h3g2")
    e.send("go depth 4")
    bm, _ = e.bestmove(30)
    b = chess.Board(fen)
    b.push_uci("e1c1")
    b.push_uci("h3g2")
    check(legal_in(b, bm), f"bestmove {bm} illegal in kiwipete line")


def test_terminal(e):
    # Checkmated / stalemated side: engine must answer without crashing.
    e.send("position fen rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3")
    e.send("go depth 3")
    bm, lines = e.bestmove(5)
    check(bm == "0000", f"mated position should give bestmove 0000, got {bm}")
    e.send("position fen 7k/5Q2/6K1/8/8/8/8/8 b - - 0 1")
    e.send("go depth 3")
    bm, _ = e.bestmove(5)
    check(bm == "0000", f"stalemated position should give bestmove 0000, got {bm}")


MATES = [
    # (fen, mate-in-N moves) : simple, independently verifiable mates
    ("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", 1),           # back-rank Rd8#
    ("k7/8/1K6/8/8/8/8/7R w - - 0 1", 1),                    # Rh8#
    ("r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5Q2/PPPP1PPP/RNB1K1NR w KQkq - 4 4", 1),  # Qxf7#
    ("6k1/8/6K1/8/8/8/8/R7 w - - 0 1", 1),
    ("7k/8/5K2/8/8/8/8/6R1 w - - 0 1", 2),
]


def test_mates(e):
    for fen, n in MATES:
        e.send("ucinewgame")
        e.send(f"position fen {fen}")
        e.send(f"go depth {2 * n + 2}")
        bm, lines = e.bestmove(60)
        scores = [l for l in lines if " score mate " in l]
        check(legal_in(fen, bm), f"{fen}: illegal {bm}")
        check(bool(scores) and f"score mate {n} " in scores[-1], f"{fen}: expected mate {n}, last info: {lines[-2] if len(lines) > 1 else lines}")
        # Verify the move actually leads to mate in n with python-chess (for n == 1).
        if n == 1:
            b = chess.Board(fen)
            b.push_uci(bm)
            check(b.is_checkmate(), f"{fen}: {bm} is not mate")


def test_draw_awareness(e):
    # KBK must be scored as a dead draw.
    e.send("position fen 4k3/8/8/8/8/8/8/4KB2 w - - 0 1")
    e.send("go depth 6")
    _, lines = e.bestmove(10)
    last = [l for l in lines if " score " in l][-1]
    check(" score cp 0 " in last, f"KBK not scored as draw: {last}")


def main():
    e = Engine(ENGINE)
    try:
        test_handshake(e)
        test_limits(e)
        test_infinite_stop(e)
        test_positions(e)
        test_terminal(e)
        test_mates(e)
        test_draw_awareness(e)
    finally:
        e.close()
    if failures:
        print(f"{failures} FAILURE(S)")
        sys.exit(1)
    print("ALL UCI TESTS PASSED")


if __name__ == "__main__":
    main()
