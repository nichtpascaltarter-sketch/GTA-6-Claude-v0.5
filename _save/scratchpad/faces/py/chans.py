# chans.py img x0 y0 x1 y1 scale out.png [gain]: the crop's R, G and B channels as linear grey images side by side
# (debug views are written as linearToSrgb(value): decoded back to linear, then shown with gain and re-encoded)
import sys
from PIL import Image, ImageDraw
import numpy as np
img, x0, y0, x1, y1, sc, out = sys.argv[1], *map(int, sys.argv[2:7]), sys.argv[7]
gain = float(sys.argv[8]) if len(sys.argv) > 8 else 1.0
a = np.asarray(Image.open(img).convert("RGB")).astype(np.float32)[y0:y1, x0:x1] / 255.0
lin = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
w, h = (x1 - x0) * sc, (y1 - y0) * sc
canvas = Image.new("RGB", (w * 3 + 20, h + 18), (40, 40, 40))
d = ImageDraw.Draw(canvas)
for c, name in enumerate("RGB"):
    v = np.clip(lin[..., c] * gain, 0, 1)
    s = np.where(v <= 0.0031308, v * 12.92, 1.055 * v ** (1 / 2.4) - 0.055)
    im = Image.fromarray((s * 255).astype(np.uint8)).resize((w, h), Image.NEAREST)
    canvas.paste(im, (c * (w + 10), 18))
    d.text((c * (w + 10) + 4, 3), f"{name} x{gain}", fill=(255, 255, 0))
canvas.save(out)
