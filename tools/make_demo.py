#!/usr/bin/env python3
"""Render docs/demo.gif from REAL command output.

Each scene's command is actually executed; stdout lines are captured with their
real arrival timestamps (gaps are clamped so the GIF stays short) and replayed
into a terminal-style canvas. Needs: pip install pillow chess ; and ./cbitboard built.
"""
import subprocess, time, sys, os, select
from PIL import Image, ImageDraw, ImageFont

COLS, ROWS = 96, 30
FS = 15
FONT = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", FS)
BOLD = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", FS)
CW = int(FONT.getlength("M")) ; CH = FS + 5
W, H = COLS * CW + 24, ROWS * CH + 24 + 22
BG, FG, GREEN, CYAN, DIM, YEL = (24,26,33), (220,223,228), (130,220,130), (110,200,255), (130,135,145), (240,210,120)

SCENES = [
 ("./cbitboard 5", "perft: startpos depth 5", None),
 ('./cbitboard "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -" 4', "perft: Kiwipete depth 4", None),
 ("./tests/perft_suite.sh ./cbitboard quick", "perft suite", None),
 ("printf 'position startpos moves e2e4 e7e5\\ngo movetime 1200\\n' | ./cbitboard", "UCI search (no args = UCI mode)", None),
 ("python tools/selfplay.py ./cbitboard 14 0.25", "engine plays itself over UCI", None),
]

def run(cmd):
    p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    t0 = time.time(); buf = b""; out = []
    while True:
        r, _, _ = select.select([p.stdout], [], [], 0.05)
        if r:
            d = os.read(p.stdout.fileno(), 65536)
            if not d: break
            buf += d
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                out.append((time.time() - t0, line.decode(errors="replace")))
        elif p.poll() is not None:
            break
    if buf: out.append((time.time() - t0, buf.decode(errors="replace")))
    return out

def color_for(line):
    if line.startswith(("info", "bestmove")): return CYAN
    if "pass" in line or "ALL" in line: return GREEN
    if line.startswith("Nodes") or line.startswith("Time"): return YEL
    return FG

def render(lines, title, cursor=True):
    im = Image.new("RGB", (W, H), BG); d = ImageDraw.Draw(im)
    d.rectangle([0, 0, W, 22], fill=(40,43,52))
    for i, c in enumerate([(255,95,86),(255,189,46),(39,201,63)]): d.ellipse([10+i*18, 6, 20+i*18, 16], fill=c)
    d.text((W//2 - 120, 3), f"C-BitBoard - {title}", font=FONT, fill=DIM)
    y = 34
    for text, col in lines[-ROWS:]:
        d.text((12, y), text[:COLS], font=BOLD if col == GREEN and text.startswith("$") else FONT, fill=col); y += CH
    return im

def main(outpath):
    frames, durs = [], []
    def add(im, ms):
        if frames and durs and im.tobytes() == frames[-1].tobytes(): durs[-1] += ms; return
        frames.append(im); durs.append(ms)
    for cmd, title, _ in SCENES:
        print("running:", cmd, file=sys.stderr)
        out = run(cmd)
        screen = []
        shown = "$ " + cmd
        # typing
        for k in range(0, len(shown) - 1, 3):
            add(render(screen + [(shown[:k+2] + "_", GREEN)], title), 45)
        screen.append((shown[:COLS*2], GREEN)); add(render(screen, title), 250)
        last = 0
        for t, line in out:
            gap = min(max(t - last, 0.03), 0.6); last = t
            screen.append((line, color_for(line))); add(render(screen, title), int(gap * 1000))
        add(render(screen, title), 1800)
        screen.clear()
    frames[0].save(outpath, save_all=True, append_images=frames[1:], duration=durs, loop=0, optimize=True)
    print(f"{outpath}: {len(frames)} frames, {sum(durs)/1000:.1f}s", file=sys.stderr)

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "docs/demo.gif")
