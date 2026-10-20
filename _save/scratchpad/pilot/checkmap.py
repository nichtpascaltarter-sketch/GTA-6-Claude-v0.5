# scratch: the pilot's top-down check map. The baked terrain, the hand-built roads as the game reads them from world.bin
# (rbdump's output: carriageways, sidewalks, medians, centre lines, junction control, turning circles, names), the story
# places, and optionally today's procedural network as a faint underlay (orientation only, decision D3).
# usage: python3 -I checkmap.py <grids dir> <rbdump.txt> <census.txt> <out.png> [--window cx,cy,half,mpp] [--underlay] [--ids]
import math
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

gdir, dump, census, out = sys.argv[1:5]
opts = sys.argv[5:]
cx, cy, half, mpp = 1580.0, 340.0, 480.0, 0.5
for i, o in enumerate(opts):
    if o == "--window":
        cx, cy, half, mpp = (float(v) for v in opts[i + 1].split(","))
underlay, ids = "--underlay" in opts, "--ids" in opts
R, CELL, HALF = 2560, 8.0, 10240.0
H = np.fromfile(gdir + "/height.i16", dtype="<i2").reshape(R, R).astype(np.float32) * 0.01
W = np.fromfile(gdir + "/water.i16", dtype="<i2").reshape(R, R)

n = int(round(2 * half / mpp))
xs = cx - half + (np.arange(n) + 0.5) * mpp
ys = cy + half - (np.arange(n) + 0.5) * mpp
X, Y = np.meshgrid(xs, ys)
tx, ty = (X + HALF) / CELL - 0.5, (Y + HALF) / CELL - 0.5
ix, iy = np.clip(np.floor(tx).astype(int), 0, R - 2), np.clip(np.floor(ty).astype(int), 0, R - 2)
fx, fy = np.clip(tx - ix, 0, 1), np.clip(ty - iy, 0, 1)
h = (H[iy, ix] * (1 - fx) * (1 - fy) + H[iy, ix + 1] * fx * (1 - fy) + H[iy + 1, ix] * (1 - fx) * fy + H[iy + 1, ix + 1] * fx * fy)
nx, ny = np.clip(np.rint(tx).astype(int), 0, R - 1), np.clip(np.rint(ty).astype(int), 0, R - 1)
wc = W[ny, nx]
wl = np.where(wc == 1, 0.0, np.where(wc == 2, 4.0, -1e9)).astype(np.float32)
water = wl > h
gy, gx = np.gradient(h, mpp)
shade = np.clip(0.9 + 1.2 * (-gx * 0.6 + gy * 0.6), 0.6, 1.15)
img = np.stack([0.80 + 0 * h, 0.82 + 0 * h, 0.74 + 0 * h], -1) * shade[..., None]
depth = np.clip((wl - h) / 5.0, 0, 1)
wcol = np.stack([0.55 - 0.35 * depth, 0.75 - 0.35 * depth, 0.85 - 0.25 * depth], -1)
img[water] = wcol[water]
im = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), "RGB")
d = ImageDraw.Draw(im, "RGBA")


def P(x, y):
    return ((x - (cx - half)) / mpp, ((cy + half) - y) / mpp)


def font(sz):
    try:
        return ImageFont.truetype("DejaVuSans.ttf", sz)
    except OSError:
        return ImageFont.load_default()


f11, f13, f16, f22 = font(11), font(13), font(16), font(22)
bold = font(13)
try:
    bold = ImageFont.truetype("DejaVuSans-Bold.ttf", 13)
    f22 = ImageFont.truetype("DejaVuSans-Bold.ttf", 22)
except OSError:
    pass

# today's network, faint (D3: for orientation only)
if underlay:
    import re
    for line in open(census):
        if line.startswith("E "):
            parts = line.split("|")
            m = re.match(r"([-\d.]+) ([-\d.]+) -> ([-\d.]+) ([-\d.]+)", parts[2])
            x0, y0, x1, y1 = (float(v) for v in m.groups())
            if max(abs(x0 - cx), abs(x1 - cx)) > half + 300 or max(abs(y0 - cy), abs(y1 - cy)) > half + 300:
                continue
            d.line([P(x0, y0), P(x1, y1)], fill=(120, 60, 160, 70), width=max(1, int(4 / mpp)))

nodes, edges = [], []
for line in open(dump):
    t = line.split()
    if not t:
        continue
    if t[0] == "N":
        nodes.append({"id": t[1], "x": float(t[2]), "y": float(t[3]), "z": float(t[4]), "r": float(t[5]), "ctl": int(t[6]), "deg": int(t[8])})
    elif t[0] == "E":
        head, name, rest = line.split("|", 2)
        h_ = head.split()
        pts = [tuple(float(v) for v in p.split(",")) for p in rest.split()]
        edges.append({"id": h_[1], "n0": int(h_[2]), "n1": int(h_[3]), "cls": int(h_[4]), "flags": int(h_[5]), "lf": int(h_[6]), "lb": int(h_[7]),
                      "hw": float(h_[8]), "sw": float(h_[9]), "len": float(h_[12]), "name": name, "pts": pts})
MEDIAN = {0: 1.2, 1: 4.0}   # the classes' medians (roadnet.cpp kRoadInfo)


def line(pts, width_m, col):
    w = max(1, int(round(width_m / mpp)))
    d.line([P(p[0], p[1]) for p in pts], fill=col, width=w, joint="curve")
    for p in (pts[0], pts[-1]):   # round ends, so edges join at the nodes
        a, b = P(p[0], p[1])
        r = w / 2
        d.ellipse([a - r, b - r, a + r, b + r], fill=col)


# sidewalks, then kerbs and carriageways, then medians and centre lines, so the junctions merge
for e in edges:
    if e["sw"] > 0:
        line(e["pts"], 2 * (e["hw"] + e["sw"]), (214, 210, 200, 255))
for nd in nodes:
    if nd["deg"] == 1:
        e = [e for e in edges if nodes[e["n0"]] is nd or nodes[e["n1"]] is nd][0]
        rb = max(e["hw"] + 3.0, 9.0) + e["sw"]
        a, b = P(nd["x"], nd["y"])
        d.ellipse([a - rb / mpp, b - rb / mpp, a + rb / mpp, b + rb / mpp], fill=(214, 210, 200, 255))
for e in edges:
    line(e["pts"], 2 * e["hw"] + 0.3, (150, 150, 150, 255))
for nd in nodes:
    if nd["deg"] == 1:
        e = [e for e in edges if nodes[e["n0"]] is nd or nodes[e["n1"]] is nd][0]
        rb = max(e["hw"] + 3.0, 9.0)
        a, b = P(nd["x"], nd["y"])
        d.ellipse([a - rb / mpp, b - rb / mpp, a + rb / mpp, b + rb / mpp], fill=(72, 74, 78, 255))
for e in edges:
    line(e["pts"], 2 * e["hw"], (72, 74, 78, 255) if not (e["flags"] & 1) else (96, 92, 88, 255))
for e in edges:
    if e["cls"] in MEDIAN and e["lf"] and e["lb"]:
        line(e["pts"], MEDIAN[e["cls"]], (110, 150, 90, 255))
    elif e["lf"] and e["lb"] and e["cls"] != 4:
        line(e["pts"], 0.3, (230, 200, 60, 255))
# bridges: the deck's edges
for e in edges:
    if e["flags"] & 1:
        for side in (-1, 1):
            pts = e["pts"]
            off = []
            for i, p in enumerate(pts):
                q0, q1 = pts[max(0, i - 1)], pts[min(len(pts) - 1, i + 1)]
                dx, dy = q1[0] - q0[0], q1[1] - q0[1]
                L = math.hypot(dx, dy) or 1
                off.append((p[0] - dy / L * side * (e["hw"] + e["sw"] + 0.6), p[1] + dx / L * side * (e["hw"] + e["sw"] + 0.6)))
            d.line([P(*p) for p in off], fill=(60, 50, 40, 255), width=max(1, int(0.8 / mpp)))
# junction control
for nd in nodes:
    a, b = P(nd["x"], nd["y"])
    if nd["ctl"] == 2:
        r = 3.2 / mpp
        d.ellipse([a - r, b - r, a + r, b + r], fill=(30, 30, 30, 255))
        for k, col in enumerate(((220, 40, 40), (240, 180, 30), (40, 190, 70))):
            rr = 1.0 / mpp
            yy = b + (k - 1) * 2.0 / mpp
            d.ellipse([a - rr, yy - rr, a + rr, yy + rr], fill=col + (255,))
    elif nd["ctl"] == 1:
        r = 2.6 / mpp
        oct_ = [(a + r * math.cos(math.pi / 8 + k * math.pi / 4), b + r * math.sin(math.pi / 8 + k * math.pi / 4)) for k in range(8)]
        d.polygon(oct_, fill=(200, 30, 30, 255), outline=(255, 255, 255, 255))
    if ids:
        d.text((a + 4, b + 4), nd["id"].replace("cl.", ""), fill=(0, 0, 120, 255), font=f11)
# names: once per street, along its longest edge
best = {}
for e in edges:
    if e["name"] and (e["name"] not in best or e["len"] > best[e["name"]]["len"]):
        best[e["name"]] = e
for name, e in best.items():
    pts = e["pts"]
    mid = len(pts) // 2
    a, b = (pts[mid - 1], pts[mid]) if len(pts) > 2 else (pts[0], pts[1])
    mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
    ang = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
    if ang > 90 or ang < -90:
        ang += 180
    tw, th = d.textbbox((0, 0), name, font=bold)[2:]
    lab = Image.new("RGBA", (tw + 8, th + 6), (0, 0, 0, 0))
    ImageDraw.Draw(lab).text((4, 2), name, font=bold, fill=(255, 255, 255, 255), stroke_width=2, stroke_fill=(40, 40, 40, 255))
    lab = lab.rotate(ang, expand=True, resample=Image.BICUBIC)
    px_, py_ = P(mx, my)
    im.paste(lab, (int(px_ - lab.width / 2), int(py_ - lab.height / 2)), lab)
if ids:
    for e in edges:
        pts = e["pts"]
        mid = pts[len(pts) // 2] if len(pts) > 2 else ((pts[0][0] + pts[1][0]) / 2, (pts[0][1] + pts[1][1]) / 2)
        a, b = P(mid[0], mid[1])
        d.text((a + 2, b + 2), e["id"].replace("cl.", ""), fill=(120, 0, 0, 255), font=f11)
# the story places (today's positions, which places.ntw will re-anchor), and the pilot's circle
for line_ in open(census):
    if line_.startswith("P "):
        t = line_.split()
        x, y = float(t[3]), float(t[4])
        if abs(x - cx) < half and abs(y - cy) < half:
            a, b = P(x, y)
            d.ellipse([a - 6, b - 6, a + 6, b + 6], fill=(255, 200, 0, 255), outline=(0, 0, 0, 255), width=2)
            d.text((a + 9, b - 8), t[1], fill=(0, 0, 0, 255), font=bold, stroke_width=2, stroke_fill=(255, 255, 255, 255))
a0, b0 = P(1560 - 400, 350 + 400)
a1, b1 = P(1560 + 400, 350 - 400)
d.ellipse([a0, b0, a1, b1], outline=(200, 0, 0, 150), width=2)
# title, scale bar, legend
total = sum(e["len"] for e in edges)
sig = sum(1 for nd in nodes if nd["ctl"] == 2)
stop = sum(1 for nd in nodes if nd["ctl"] == 1)
d.rectangle([0, 0, n, 34], fill=(255, 255, 255, 220))
d.text((10, 6), "Calle Luna pilot: the streets", font=f22, fill=(20, 20, 20, 255))
d.text((380, 11), "%d nodes, %d edges, %.1f km; %d signals, %d all-way stops (the red circle: 400 m round (1560, 350))" % (len(nodes), len(edges), total / 1000, sig, stop),
       font=f13, fill=(40, 40, 40, 255))
sx, sy = 20, n - 30
d.rectangle([sx, sy, sx + 100 / mpp, sy + 6], fill=(20, 20, 20, 255))
d.text((sx, sy - 18), "100 m", font=f13, fill=(20, 20, 20, 255))
d.polygon([(n - 30, 50), (n - 38, 72), (n - 22, 72)], fill=(20, 20, 20, 255))
d.text((n - 35, 74), "N", font=f16, fill=(20, 20, 20, 255))
im.save(out)
print("checkmap: %s (%dx%d), %d nodes, %d edges" % (out, n, n, len(nodes), len(edges)))
