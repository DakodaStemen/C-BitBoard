#!/usr/bin/env python3
"""Match C-BitBoard against Stockfish (UCI_LimitStrength) via python-chess.

  python tools/match.py --engine ./cbitboard --sf /path/to/stockfish \
      --sf-elo 1500 --games 100 --movetime 0.1 --workers 8

Reports W/D/L, score and the Elo difference implied by the score (with a 95%
interval). The absolute Elo is only as good as Stockfish's UCI_Elo calibration,
which is defined for a specific time control and hardware - treat as rough.
"""
import argparse, math, random, json
from concurrent.futures import ProcessPoolExecutor
import chess, chess.engine

def play(args):
    eng_path, sf_path, sf_elo, mt, idx, seed = args
    rng = random.Random(seed)
    b = chess.Board()
    for _ in range(4):                       # 4 random plies for opening variety
        b.push(rng.choice(list(b.legal_moves)))
    # same opening is played twice with colours swapped
    ours_white = (idx % 2 == 0)
    me = chess.engine.SimpleEngine.popen_uci([eng_path])
    sf = chess.engine.SimpleEngine.popen_uci([sf_path])
    sf.configure({"Threads": 1, "Hash": 16, "UCI_LimitStrength": True, "UCI_Elo": sf_elo})
    lim = chess.engine.Limit(time=mt)
    try:
        while not b.is_game_over(claim_draw=True) and len(b.move_stack) < 300:
            mine = (b.turn == chess.WHITE) == ours_white
            r = (me if mine else sf).play(b, lim)
            b.push(r.move)
    finally:
        me.quit(); sf.quit()
    res = b.result(claim_draw=True)
    if res == "*": res = "1/2-1/2"
    pts = {"1-0": 1.0, "0-1": 0.0, "1/2-1/2": 0.5}[res]
    return pts if ours_white else 1.0 - pts

def elo_from_score(s):
    s = min(max(s, 1e-3), 1 - 1e-3)
    return -400 * math.log10(1 / s - 1)

if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", default="./cbitboard"); ap.add_argument("--sf", required=True)
    ap.add_argument("--sf-elo", type=int, default=1500); ap.add_argument("--games", type=int, default=100)
    ap.add_argument("--movetime", type=float, default=0.1); ap.add_argument("--workers", type=int, default=8)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()
    jobs = [(a.engine, a.sf, a.sf_elo, a.movetime, i, a.seed + i // 2) for i in range(a.games)]
    with ProcessPoolExecutor(a.workers) as ex:
        scores = list(ex.map(play, jobs))
    n = len(scores); w = sum(s == 1 for s in scores); d = sum(s == .5 for s in scores); l = n - w - d
    mean = sum(scores) / n
    var = sum((s - mean) ** 2 for s in scores) / (n - 1) / n
    lo, hi = mean - 1.96 * math.sqrt(var), mean + 1.96 * math.sqrt(var)
    out = dict(sf_elo=a.sf_elo, games=n, wins=w, draws=d, losses=l, score=round(mean, 4),
               elo_diff=round(elo_from_score(mean), 1),
               elo_diff_95=[round(elo_from_score(lo), 1), round(elo_from_score(hi), 1)],
               est_elo=round(a.sf_elo + elo_from_score(mean)), movetime=a.movetime)
    print(json.dumps(out))
