#!/usr/bin/env python3
"""Measure perft Mnodes/s and search knodes/s; print markdown tables (best of N runs)."""
import re, subprocess, sys
BIN = sys.argv[1] if len(sys.argv) > 1 else "./cbitboard"
N = int(sys.argv[2]) if len(sys.argv) > 2 else 5
rows = []
for line in open("tests/perft.tsv"):
    if line.startswith("#") or not line.strip(): continue
    name, fen, depth, exp, tier = line.rstrip("\n").split("\t")
    if tier != "quick": continue
    best = 0.0; nodes = 0
    for _ in range(N):
        o = subprocess.run([BIN, fen, depth], capture_output=True, text=True).stdout
        nodes = int(re.search(r"Nodes: (\d+)", o).group(1))
        m = re.search(r"\(([\d.]+) Mnodes/s", o)
        if m: best = max(best, float(m.group(1)))
    rows.append((name, depth, nodes, best))
print("| Position | Depth | Nodes | Mnodes/s (best of %d) |\n|---|---|---|---|" % N)
for r in rows: print("| %s | %s | %s | %.1f |" % (r[0], r[1], f"{r[2]:,}", r[3]))
best = 0.0
for _ in range(N):
    o = subprocess.run([BIN, "bench", "10"], capture_output=True, text=True).stdout
    tn = int(re.search(r"Total nodes: (\d+)", o).group(1)); nps = float(re.search(r"NPS: (\d+)", o).group(1))
    best = max(best, nps)
print("\n| Search bench (8 positions, depth 10) | Nodes | knodes/s (best of %d) |\n|---|---|---|" % N)
print("| `./cbitboard bench 10` | %s | %.0f |" % (f"{tn:,}", best / 1000))
