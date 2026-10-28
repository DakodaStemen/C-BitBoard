#!/usr/bin/env bash
# SPRT a candidate build against a baseline build with fastchess.
#   tools/sprt.sh <baseline-binary> <candidate-binary>
# Tunables via env (defaults are the project's fixed settings):
#   FASTCHESS=fastchess  TC=8+0.08  HASH=16  CONCURRENCY=$(nproc/2)  ROUNDS=20000
#   ELO0=0 ELO1=3 ALPHA=0.05 BETA=0.05  BOOK=tools/openings.epd  SEED=42
#   EXTRA="nodes=20000"   # extra `-each` options, e.g. fixed nodes instead of time
# The script only *runs* the test; read the verdict from fastchess's final output.
set -eu
[[ $# -eq 2 ]] || { echo "usage: $0 <baseline> <candidate>"; exit 2; }
BASE=$(realpath "$1"); NEW=$(realpath "$2")
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FASTCHESS=${FASTCHESS:-fastchess}
command -v "$FASTCHESS" >/dev/null || { echo "fastchess not found (set FASTCHESS=/path or see https://github.com/Disservin/fastchess/releases)"; exit 1; }
TC=${TC:-8+0.08}; HASH=${HASH:-16}
CONC=${CONCURRENCY:-$(( $(nproc) / 2 > 0 ? $(nproc) / 2 : 1 ))}
mkdir -p "$ROOT/sprt-out"
# shellcheck disable=SC2086
exec "$FASTCHESS" \
  -engine cmd="$NEW"  name=candidate \
  -engine cmd="$BASE" name=baseline \
  -each tc="$TC" option.Hash="$HASH" ${EXTRA:-} \
  -openings file="$ROOT/${BOOK:-tools/openings.epd}" format=epd order=random \
  -srand "${SEED:-42}" -repeat -games 2 -rounds "${ROUNDS:-20000}" \
  -concurrency "$CONC" -recover \
  -sprt elo0="${ELO0:-0}" elo1="${ELO1:-3}" alpha="${ALPHA:-0.05}" beta="${BETA:-0.05}" \
  -draw movenumber=40 movecount=8 score=10 \
  -resign movecount=4 score=600 \
  -ratinginterval 50 \
  -pgnout file="$ROOT/sprt-out/games.pgn"
