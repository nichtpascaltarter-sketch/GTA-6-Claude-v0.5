#!/usr/bin/env python3
# overlay_sheet.py OUT.png BEFORE_DIR AFTER_DIR name1 name2 ...: the debug text (top-left) of each shot, before above after
import sys, os
from PIL import Image, ImageDraw, ImageFont
out, bd, ad = sys.argv[1:4]
names = sys.argv[4:]
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 14)
except Exception:
    font = ImageFont.load_default()
W, H = 760, 92
def crop(d, n):
    for ext in (".bmp", ".png"):
        p = os.path.join(d, n + ext)
        if os.path.exists(p):
            return Image.open(p).convert("RGB").crop((0, 0, W, H))
    return None
rows = []
for n in names:
    a, b = crop(bd, n), crop(ad, n)
    if a is None or b is None:
        print("missing", n); continue
    rows.append((n, a, b))
sheet = Image.new("RGB", (W + 150, len(rows) * (2 * H + 10)), (20, 20, 24))
dr = ImageDraw.Draw(sheet)
for i, (n, a, b) in enumerate(rows):
    y = i * (2 * H + 10)
    dr.text((W + 6, y + 4), n, fill=(255, 255, 255), font=font)
    dr.text((W + 6, y + 30), "before", fill=(240, 190, 120), font=font)
    dr.text((W + 6, y + H + 30), "after", fill=(140, 230, 140), font=font)
    sheet.paste(a, (0, y)); sheet.paste(b, (0, y + H))
sheet.save(out)
print(out, len(rows))
