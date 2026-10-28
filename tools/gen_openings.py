#!/usr/bin/env python3
"""Generate a small, fixed, roughly-balanced opening book (EPD) for self-play.

  python tools/gen_openings.py --engine ./cbitboard --count 200 --out tools/openings.epd

Random legal 8-ply lines (seeded => reproducible), kept only if the engine's own
depth-6 score is within +/-60 cp. Needs `pip install chess`. The committed
tools/openings.epd was produced with the defaults below.
"""
import argparse, random
import chess, chess.engine

ap = argparse.ArgumentParser()
ap.add_argument("--engine", default="./cbitboard"); ap.add_argument("--count", type=int, default=200)
ap.add_argument("--plies", type=int, default=8); ap.add_argument("--seed", type=int, default=2024)
ap.add_argument("--depth", type=int, default=6); ap.add_argument("--window", type=int, default=60)
ap.add_argument("--out", default="tools/openings.epd")
a = ap.parse_args()
rng = random.Random(a.seed)
eng = chess.engine.SimpleEngine.popen_uci([a.engine])
seen, out = set(), []
while len(out) < a.count:
    b = chess.Board()
    for _ in range(a.plies):
        if b.is_game_over(): break
        b.push(rng.choice(list(b.legal_moves)))
    if b.is_game_over(): continue
    key = b.epd()
    if key in seen: continue
    seen.add(key)
    sc = eng.analyse(b, chess.engine.Limit(depth=a.depth))["score"].white().score(mate_score=10000)
    if abs(sc) <= a.window: out.append(key)
eng.quit()
open(a.out, "w").write("\n".join(out) + "\n")
print(f"wrote {len(out)} openings to {a.out}")
