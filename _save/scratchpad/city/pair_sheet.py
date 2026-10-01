#!/usr/bin/env python3
# pair_sheet.py BEFORE_DIR AFTER_DIR OUT.png "Title" view1 view2 ... : one row per view, before left / after right
import sys, os
from PIL import Image, ImageDraw, ImageFont
bd, ad, out, title = sys.argv[1:5]
views = sys.argv[5:]
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 20)
    small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 16)
except Exception:
    font = small = ImageFont.load_default()
def load(d, n):
    for ext in (".png", ".bmp", ".ppm"):
        p = os.path.join(d, n + ext)
        if os.path.exists(p):
            return Image.open(p).convert("RGB")
    return None
rows = []
W, H = 640, 360
for v in views:
    a, b = load(bd, v), load(ad, v)
    if a is None or b is None:
        print("missing", v); continue
    a, b = a.resize((W, H)), b.resize((W, H))
    row = Image.new("RGB", (2 * W + 12, H + 26), (20, 20, 24))
    row.paste(a, (0, 26)); row.paste(b, (W + 12, 26))
    dr = ImageDraw.Draw(row)
    dr.text((6, 3), "BEFORE  " + v, fill=(255, 200, 120), font=small)
    dr.text((W + 18, 3), "AFTER  " + v, fill=(140, 230, 140), font=small)
    rows.append(row)
sheet = Image.new("RGB", (2 * W + 12, 34 + sum(r.height + 6 for r in rows)), (12, 12, 14))
ImageDraw.Draw(sheet).text((8, 6), title, fill=(255, 255, 255), font=font)
y = 34
for r in rows:
    sheet.paste(r, (0, y)); y += r.height + 6
sheet.save(out)
print(out, len(rows), "rows")
