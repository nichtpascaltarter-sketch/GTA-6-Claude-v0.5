#!/usr/bin/env python3
# Before/after sheets: for each district, the aerial and the street view, baseline (left) and new (right), labelled.
import sys, os
from PIL import Image, ImageDraw, ImageFont
before_dir, after_dir, out_dir, tag = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
districts = [
    ("Calle Luna", ["calle_luna_air", "calle_luna_street"]),
    ("Downtown", ["downtown_air", "downtown_street"]),
    ("Canvas District (Midtown)", ["canvas_air", "canvas_street"]),
    ("Sol Beach", ["beach_air", "beach_street"]),
    ("North Porto Sol", ["north_city_air", "north_city_street"]),
    ("The Grove", ["grove_air", "grove_street"]),
    ("Westbrook (suburbs)", ["westbrook_air"]),
    ("Palmetto Flats", ["flats_air", "flats_street"]),
    ("Fort Castell", ["fort_castell_air", "fort_castell_street"]),
    ("Okahatchee (Lake Town)", ["lake_town_air", "lake_town_street"]),
    ("Key Solano", ["key_solano_air"]),
]
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 20)
    small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16)
except Exception:
    font = small = ImageFont.load_default()
def load(d, n):
    for ext in (".png", ".bmp"):
        p = os.path.join(d, n + ext)
        if os.path.exists(p):
            return Image.open(p).convert("RGB")
    return None
os.makedirs(out_dir, exist_ok=True)
made = 0
for name, views in districts:
    rows = []
    for v in views:
        a, b = load(before_dir, v), load(after_dir, v)
        if a is None or b is None:
            continue
        W, H = 640, 360
        a, b = a.resize((W, H)), b.resize((W, H))
        row = Image.new("RGB", (2 * W + 12, H + 26), (20, 20, 24))
        row.paste(a, (0, 26)); row.paste(b, (W + 12, 26))
        dr = ImageDraw.Draw(row)
        dr.text((6, 3), "BEFORE  " + v, fill=(255, 200, 120), font=small)
        dr.text((W + 18, 3), "AFTER  " + v, fill=(140, 230, 140), font=small)
        rows.append(row)
    if not rows:
        continue
    sheet = Image.new("RGB", (rows[0].width, 34 + sum(r.height + 6 for r in rows)), (12, 12, 14))
    ImageDraw.Draw(sheet).text((8, 6), name + " - " + tag, fill=(255, 255, 255), font=font)
    y = 34
    for r in rows:
        sheet.paste(r, (0, y)); y += r.height + 6
    fn = os.path.join(out_dir, name.split(" (")[0].lower().replace(" ", "_") + ".png")
    sheet.save(fn); made += 1
print("sheets:", made, "->", out_dir)
