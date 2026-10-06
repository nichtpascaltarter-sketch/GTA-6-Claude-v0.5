#!/usr/bin/env python3
# grid_sheet.py OUT.png "Title" COLS DIR name1 name2 ... : labelled grid of shots (bmp/png/ppm) from DIR
import sys, os
from PIL import Image, ImageDraw, ImageFont
out, title, cols, d = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
names = sys.argv[5:]
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 20)
    small = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 16)
except Exception:
    font = small = ImageFont.load_default()
W, H = 640, 360
ims = []
for n in names:
    for ext in (".png", ".bmp", ".ppm"):
        p = os.path.join(d, n + ext)
        if os.path.exists(p):
            ims.append((n, Image.open(p).convert("RGB").resize((W, H), Image.LANCZOS)))
            break
    else:
        print("missing", n)
rows = (len(ims) + cols - 1) // cols
sheet = Image.new("RGB", (cols * W + (cols - 1) * 12, 34 + rows * (H + 12)), (14, 14, 18))
dr = ImageDraw.Draw(sheet)
dr.text((8, 6), title, fill=(255, 255, 255), font=font)
for i, (n, im) in enumerate(ims):
    x, y = (i % cols) * (W + 12), 34 + (i // cols) * (H + 12)
    sheet.paste(im, (x, y))
    dr.rectangle([x, y, x + len(n) * 10 + 12, y + 22], fill=(0, 0, 0))
    dr.text((x + 5, y + 2), n, fill=(140, 230, 140), font=small)
sheet.save(out)
print(out, len(ims))
