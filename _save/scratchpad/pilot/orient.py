# scratch: an orientation map of the pilot area. The baked terrain (relief, water, the Calle Luna region) with today's
# procedural roads and buildings from the census drawn faintly over it: orientation only (decision D3), nothing is
# exported from them. usage: python3 -I orient.py <grids dir> <census.txt> <out.png> [cx cy half m/px]
import math
import re
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

gdir, census, out = sys.argv[1], sys.argv[2], sys.argv[3]
cx, cy, half, mpp = (float(v) for v in (sys.argv[4:8] if len(sys.argv) >= 8 else (1560, 350, 600, 1.0)))
R, CELL, HALF = 2560, 8.0, 10240.0
H = np.fromfile(gdir + "/height.i16", dtype="<i2").reshape(R, R).astype(np.float32) * 0.01
W = np.fromfile(gdir + "/water.i16", dtype="<i2").reshape(R, R)
G = np.fromfile(gdir + "/region.i16", dtype="<i2").reshape(R, R)
levels = {1: 0.0, 2: 4.0}
REG_CALLE_LUNA = 5

n = int(round(2 * half / mpp))
xs = cx - half + (np.arange(n) + 0.5) * mpp
ys = cy + half - (np.arange(n) + 0.5) * mpp          # image rows run north to south
X, Y = np.meshgrid(xs, ys)
tx, ty = (X + HALF) / CELL - 0.5, (Y + HALF) / CELL - 0.5
ix, iy = np.clip(np.floor(tx).astype(int), 0, R - 2), np.clip(np.floor(ty).astype(int), 0, R - 2)
fx, fy = np.clip(tx - ix, 0, 1), np.clip(ty - iy, 0, 1)
h = (H[iy, ix] * (1 - fx) * (1 - fy) + H[iy, ix + 1] * fx * (1 - fy) + H[iy + 1, ix] * (1 - fx) * fy + H[iy + 1, ix + 1] * fx * fy)
nx, ny = np.clip(np.rint(tx).astype(int), 0, R - 1), np.clip(np.rint(ty).astype(int), 0, R - 1)
wc, reg = W[ny, nx], G[ny, nx]
wl = np.full(h.shape, -1e9, dtype=np.float32)
for c, lv in levels.items():
    wl[wc == c] = lv
water = wl > h
gy, gx = np.gradient(h, mpp)
shade = np.clip(0.75 + 0.9 * (-gx * 0.7 + gy * 0.7), 0.35, 1.25)
base = np.stack([0.62 + 0.004 * h, 0.66 + 0.002 * h, 0.55 + 0.0 * h], -1)
base[reg == REG_CALLE_LUNA] = base[reg == REG_CALLE_LUNA] * np.array([1.08, 0.98, 0.9])
img = np.clip(base * shade[..., None], 0, 1)
depth = np.clip((wl - h) / 6.0, 0, 1)
wcol = np.stack([0.45 - 0.35 * depth, 0.70 - 0.40 * depth, 0.80 - 0.30 * depth], -1)
img[water] = wcol[water]
im = Image.fromarray((img * 255).astype(np.uint8), "RGB")
d = ImageDraw.Draw(im, "RGBA")


def px(x, y):
    return ((x - (cx - half)) / mpp, ((cy + half) - y) / mpp)


try:
    font = ImageFont.truetype("DejaVuSans.ttf", 11)
except OSError:
    font = ImageFont.load_default()
# grid every 100 m
for g in range(int(math.floor((cx - half) / 100)) * 100, int(cx + half) + 1, 100):
    a, _ = px(g, 0)
    d.line([(a, 0), (a, n)], fill=(0, 0, 0, 40))
    d.text((a + 2, 2), str(g), fill=(0, 0, 0, 160), font=font)
for g in range(int(math.floor((cy - half) / 100)) * 100, int(cy + half) + 1, 100):
    _, b = px(0, g)
    d.line([(0, b), (n, b)], fill=(0, 0, 0, 40))
    d.text((2, b + 2), str(g), fill=(0, 0, 0, 160), font=font)
names = {}
for line in open(census):
    if line.startswith("B "):
        t = line.split()
        bx, by, hx, hy, ax, ay = float(t[1]), float(t[2]), float(t[3]), float(t[4]), float(t[6]), float(t[7])
        if abs(bx - cx) > half + 50 or abs(by - cy) > half + 50:
            continue
        ux, uy = ax, ay            # the building's axis
        vx, vy = -uy, ux
        pts = [px(bx + ux * sx * hy + vx * sy * hx, by + uy * sx * hy + vy * sy * hx) for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        d.polygon(pts, outline=(90, 60, 40, 110))
    elif line.startswith("E "):
        parts = line.rstrip("\n").split("|")
        name = parts[0].split(" ", 2)[2] if len(parts[0].split(" ", 2)) > 2 else ""
        cls = int(parts[1].split()[1])
        m = re.match(r"([-\d.]+) ([-\d.]+) -> ([-\d.]+) ([-\d.]+)", parts[2])
        x0, y0, x1, y1 = (float(v) for v in m.groups())
        if max(abs(x0 - cx), abs(x1 - cx)) > half + 300 or max(abs(y0 - cy), abs(y1 - cy)) > half + 300:
            continue
        lanes = parts[5].split()[1] if len(parts) > 5 else "1/1"
        lf, lb = (int(v) for v in lanes.split("/"))
        wpx = max(1, int(round((lf + lb) * 3.3 / mpp)))
        col = (200, 40, 40, 90) if cls <= 2 else ((40, 40, 200, 80) if cls <= 4 else (60, 60, 60, 70))
        d.line([px(x0, y0), px(x1, y1)], fill=col, width=wpx)
        if name and name not in names and abs((x0 + x1) / 2 - cx) < half and abs((y0 + y1) / 2 - cy) < half:
            names[name] = ((x0 + x1) / 2, (y0 + y1) / 2, cls)
for name, (x, y, cls) in names.items():
    a, b = px(x, y)
    d.text((a + 3, b - 12), "%s (c%d)" % (name, cls), fill=(20, 20, 90, 200), font=font)
# the pilot circle and its places
a0, b0 = px(1560 - 400, 350 + 400)
a1, b1 = px(1560 + 400, 350 - 400)
d.ellipse([a0, b0, a1, b1], outline=(220, 0, 0, 200), width=2)
for line in open(census):
    if line.startswith("P "):
        t = line.split()
        x, y = float(t[3]), float(t[4])
        if abs(x - cx) < half and abs(y - cy) < half:
            a, b = px(x, y)
            d.ellipse([a - 4, b - 4, a + 4, b + 4], fill=(255, 200, 0, 255), outline=(0, 0, 0, 255))
            d.text((a + 6, b - 6), t[1], fill=(0, 0, 0, 255), font=font)
im.save(out)
inside = (X - 1560) ** 2 + (Y - 350) ** 2 < 400 ** 2
print("pilot circle: ground %.2f..%.2f m (mean %.2f), water %.1f%%, Calle Luna region %.1f%%" %
      (h[inside & ~water].min(), h[inside & ~water].max(), h[inside & ~water].mean(), 100 * water[inside].mean(), 100 * (reg[inside] == REG_CALLE_LUNA).mean()))
print("wrote", out, im.size)
