#!/usr/bin/env python3
# pair_sheet.py OUT.png "Title" BEFORE_DIR AFTER_DIR "before label" "after label" name1 name2 ... : rows of before | after
import sys, os
from PIL import Image, ImageDraw, ImageFont
out, title, bd, ad, bl, al = sys.argv[1:7]
names = sys.argv[7:]
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 20)
    small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16)
except Exception:
    font = small = ImageFont.load_default()
W, H = 640, 360
def load(d, n):
    for ext in (".png", ".bmp", ".ppm"):
        p = os.path.join(d, n + ext)
        if os.path.exists(p):
            return Image.open(p).convert("RGB").resize((W, H), Image.LANCZOS)
    print("missing", d, n)
    return None
rows = [(n, load(bd, n), load(ad, n)) for n in names]
rows = [r for r in rows if r[1] is not None and r[2] is not None]
sheet = Image.new("RGB", (2 * W + 12, 34 + len(rows) * (H + 34)), (14, 14, 18))
dr = ImageDraw.Draw(sheet)
dr.text((8, 6), title, fill=(255, 255, 255), font=font)
for i, (n, b, a) in enumerate(rows):
    y = 34 + i * (H + 34)
    dr.text((4, y + 4), f"{bl}  {n}", fill=(240, 190, 120), font=small)
    dr.text((W + 16, y + 4), f"{al}  {n}", fill=(140, 230, 140), font=small)
    sheet.paste(b, (0, y + 26))
    sheet.paste(a, (W + 12, y + 26))
sheet.save(out)
print(out, len(rows))
