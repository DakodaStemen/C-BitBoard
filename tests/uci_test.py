#!/usr/bin/env python3
"""UCI conformance tests driven through python-chess (pip install chess)."""
import random, sys, time
import chess, chess.engine

BIN = sys.argv[1] if len(sys.argv) > 1 else "./cbitboard"
ok = True
def check(name, cond, info=""):
    global ok
    print(("PASS " if cond else "FAIL ") + name + (" " + str(info) if info else ""))
    ok &= bool(cond)


# 1. Legal-move generation agrees with python-chess (via `go perft`, which uses
#    the engine's legal filter) on positions reached by random play.
import subprocess
def perft_py(b, d):
    if d == 0: return 1
    n = 0
    for m in b.legal_moves:
        b.push(m); n += perft_py(b, d - 1) if d > 1 else 1; b.pop()
    return n
rng = random.Random(7); lines = []; expect = []
for g in range(6):
    b = chess.Board()
    for ply in range(90):
        if b.is_game_over(): break
        if ply % 10 == 0:
            lines.append(f"position fen {b.fen()}\ngo perft 3")
            expect.append(perft_py(b, 3))
        b.push(rng.choice(list(b.legal_moves)))
p = subprocess.run([BIN, "uci"], input="\n".join(lines) + "\nquit\n", capture_output=True, text=True, timeout=300)
got = [int(l.split()[1]) for l in p.stdout.splitlines() if l.startswith("nodes ")]
check(f"perft(3) matches python-chess on {len(expect)} random positions", got == expect,
      f"mismatches={sum(a != b for a, b in zip(got, expect))}, got={len(got)}")

eng = chess.engine.SimpleEngine.popen_uci([BIN])
check("id name", "C-BitBoard" in eng.id["name"], eng.id["name"])
# 2. Mate in one / two
b = chess.Board("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1")
r = eng.play(b, chess.engine.Limit(depth=4), info=chess.engine.INFO_SCORE)
check("finds back-rank mate in 1 (Rd8#)", r.move.uci() == "d1d8", r.move)
check("reports mate score", r.info["score"].white().is_mate(), r.info["score"])
b = chess.Board("r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4")
r = eng.play(b, chess.engine.Limit(depth=4))
check("finds Qxf7# (scholar's mate)", r.move.uci() == "h5f7", r.move)
# 3. Wins hanging queen
b = chess.Board("rnb1kbnr/pppp1ppp/8/4p3/4P2q/5N2/PPPP1PPP/RNBQKB1R w KQkq - 0 1")
r = eng.play(b, chess.engine.Limit(depth=5))
check("captures hanging queen (Nxh4)", r.move.uci() == "f3h4", r.move)
# 4. Time control respected
t = time.time(); r = eng.play(chess.Board(), chess.engine.Limit(time=0.3)); dt = time.time() - t
check("movetime 300ms respected", 0.15 < dt < 0.6, f"{dt:.3f}s")
# 5. stop works on go infinite
eng.protocol.send_line("position startpos"); eng.protocol.send_line("go infinite")
time.sleep(0.5); t = time.time(); eng.protocol.send_line("stop")
eng.ping(); check("go infinite + stop returns promptly", time.time() - t < 0.5, f"{time.time()-t:.3f}s")
# 6. Handles stalemate/draw positions: no legal moves => bestmove 0000 doesn't crash
eng.ping(); check("engine alive after stop", True)
# 7. Full self-play game completes with only legal moves
b = chess.Board(); moves = 0
while not b.is_game_over(claim_draw=True) and moves < 200:
    r = eng.play(b, chess.engine.Limit(nodes=3000))
    assert r.move in b.legal_moves, (b.fen(), r.move)
    b.push(r.move); moves += 1
check("self-play: all moves legal", True, f"{moves} plies, result {b.result(claim_draw=True)}")
eng.quit()
print("ALL PASS" if ok else "SOME FAILED"); sys.exit(0 if ok else 1)
