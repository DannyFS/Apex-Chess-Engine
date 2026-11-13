#!/usr/bin/env python3
# Apex chess engine: match / SPRT runner (wraps fastchess).
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Examples:
#   # SPRT gainer test at STC, new build vs champion build
#   python3 tools/match.py --new build/apex --base champion/apex --preset stc --sprt 0 5
#   # Fixed-game validation match
#   python3 tools/match.py --new build/apex --base build/apex --tc 1+0.01 --games 1000
#
# Presets are part of BENCHMARKS.md methodology v1. Changing them requires a new methodology version.
import argparse
import datetime
import os
import re
import subprocess
import sys

PRESETS = {
    # name: (tc, hash MB)
    "stc": ("8+0.08", 16),
    "ltc": ("40+0.4", 64),
    "vstc": ("2+0.02", 8),  # development smoke tests only; never used for keep/reject decisions
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--new", required=True, help="path to candidate engine")
    ap.add_argument("--base", required=True, help="path to baseline engine")
    ap.add_argument("--new-name", default="new")
    ap.add_argument("--base-name", default="base")
    ap.add_argument("--preset", choices=PRESETS.keys())
    ap.add_argument("--tc", help="override time control, e.g. 8+0.08")
    ap.add_argument("--hash", type=int)
    ap.add_argument("--games", type=int, default=20000, help="max games (pairs = games/2)")
    ap.add_argument("--sprt", nargs=2, type=float, metavar=("ELO0", "ELO1"), help="normalized-Elo SPRT bounds")
    ap.add_argument("--concurrency", type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument("--book", default=os.environ.get("APEX_BOOK", "books/UHO_Lichess_4852_v1.epd"))
    ap.add_argument("--fastchess", default=os.environ.get("FASTCHESS", "fastchess"))
    ap.add_argument("--out", default="results")
    ap.add_argument("--label", default="match")
    ap.add_argument("--seed", type=int, default=None)
    args = ap.parse_args()

    tc, hash_mb = PRESETS.get(args.preset, ("8+0.08", 16))
    tc = args.tc or tc
    hash_mb = args.hash or hash_mb

    args.out = os.path.abspath(args.out)
    args.book = os.path.abspath(args.book)
    if os.sep in args.fastchess:
        args.fastchess = os.path.abspath(args.fastchess)
    os.makedirs(args.out, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    base = os.path.join(args.out, f"{stamp}-{args.label}")
    fmt = "pgn" if args.book.endswith(".pgn") else "epd"

    cmd = [
        args.fastchess,
        "-engine", f"cmd={os.path.abspath(args.new)}", f"name={args.new_name}",
        "-engine", f"cmd={os.path.abspath(args.base)}", f"name={args.base_name}",
        "-each", "proto=uci", f"tc={tc}", f"option.Hash={hash_mb}", "option.Threads=1",
        "-rounds", str(args.games // 2), "-repeat",
        "-concurrency", str(args.concurrency),
        "-openings", f"file={args.book}", f"format={fmt}", "order=random",
        "-pgnout", f"file={base}.pgn",
        "-report", "penta=true",
        "-ratinginterval", "100",
        "-recover",
    ]
    if args.seed is not None:
        cmd += ["-srand", str(args.seed)]
    if args.sprt:
        cmd += ["-sprt", f"elo0={args.sprt[0]}", f"elo1={args.sprt[1]}", "alpha=0.05", "beta=0.05", "model=normalized"]

    print(" ".join(cmd), flush=True)
    log = open(base + ".log", "w")
    log.write(" ".join(cmd) + "\n")
    # fastchess writes config.json (for -recover) into its working directory; keep it out of the repo.
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, cwd=args.out)
    tail = []
    for line in proc.stdout:
        log.write(line)
        tail.append(line)
        tail = tail[-40:]
        if re.search(r"Elo:|LLR|Games:|Ptnml|illegal|disconnect|timeout|loses on time", line, re.I):
            print(line, end="", flush=True)
    proc.wait()
    log.close()
    print("".join(tail[-15:]))
    print(f"log: {base}.log  pgn: {base}.pgn")
    sys.exit(proc.returncode)


if __name__ == "__main__":
    main()
