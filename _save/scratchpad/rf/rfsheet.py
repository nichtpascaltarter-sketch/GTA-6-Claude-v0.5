# Before/after sheets of the road-overlap corners: for each shot name, the base (left) and the fix (right), labelled.
# Usage: python3 rfsheet.py <base dir> <fix dir> <out dir>
import sys, os
from PIL import Image, ImageDraw, ImageFont
bdir, fdir, odir = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(odir, exist_ok=True)
try:
    font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 22)
except Exception:
    font = ImageFont.load_default()
def load(d, n):
    for ext in (".png", ".bmp"):
        p = os.path.join(d, n + ext)
        if os.path.exists(p): return Image.open(p).convert("RGB")
    return None
names = sorted(set(os.path.splitext(f)[0] for f in os.listdir(bdir) if f.endswith((".bmp", ".png"))) |
               set(os.path.splitext(f)[0] for f in os.listdir(fdir) if f.endswith((".bmp", ".png"))))
rows = []
for n in names:
    a, b = load(bdir, n), load(fdir, n)
    if a is None or b is None:
        print("missing", n, "base" if a is None else "fix"); continue
    w = 960
    a = a.resize((w, int(a.height * w / a.width))); b = b.resize((w, int(b.height * w / b.width)))
    h = max(a.height, b.height)
    im = Image.new("RGB", (2 * w + 10, h + 40), (20, 20, 20))
    im.paste(a, (0, 40)); im.paste(b, (w + 10, 40))
    d = ImageDraw.Draw(im)
    d.text((10, 8), n + "  -  HEAD (base)", fill=(255, 255, 255), font=font)
    d.text((w + 20, 8), n + "  -  road fix", fill=(255, 220, 120), font=font)
    p = os.path.join(odir, n + ".png")
    im.save(p)
    rows.append(im)
    print("wrote", p)
if rows:
    W = rows[0].width
    sheet = Image.new("RGB", (W, sum(r.height for r in rows)), (20, 20, 20))
    y = 0
    for r in rows:
        sheet.paste(r, (0, y)); y += r.height
    sheet = sheet.resize((W // 2, sheet.height // 2))
    sheet.save(os.path.join(odir, "all_corners.png"))
    print("wrote", os.path.join(odir, "all_corners.png"))
