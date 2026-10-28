#!/usr/bin/env python3
"""Watch C-BitBoard play itself through UCI (python-chess). Prints SAN, score, depth."""
import sys, chess, chess.engine
path = sys.argv[1] if len(sys.argv) > 1 else "./cbitboard"
plies = int(sys.argv[2]) if len(sys.argv) > 2 else 16
mt = float(sys.argv[3]) if len(sys.argv) > 3 else 0.25
eng = chess.engine.SimpleEngine.popen_uci([path])
print("engine:", eng.id["name"])
b = chess.Board()
for _ in range(plies):
    if b.is_game_over(): break
    n = b.fullmove_number; white = b.turn
    r = eng.play(b, chess.engine.Limit(time=mt), info=chess.engine.INFO_ALL)
    sc = r.info["score"].white(); sc = f"M{sc.mate()}" if sc.is_mate() else f"{sc.score()/100:+.2f}"
    san = b.san(r.move)
    print(f"{n:>3}{'.' if white else '...'} {san:<7} eval {sc:>6}  depth {r.info.get('depth')}  nodes {r.info.get('nodes')}")
    b.push(r.move)
print(); print(b); print(b.fen())
eng.quit()
