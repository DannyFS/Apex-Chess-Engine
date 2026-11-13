#!/usr/bin/env bash
# Downloads the standard opening books (CC0, github.com/official-stockfish/books) into ./books
set -euo pipefail
mkdir -p books
cd books
for f in UHO_Lichess_4852_v1.epd 8moves_v3.pgn; do
  [ -f "$f" ] && continue
  curl -fsSL -o "$f.zip" "https://github.com/official-stockfish/books/raw/master/$f.zip"
  unzip -o -q "$f.zip" && rm "$f.zip"
done
ls -la
