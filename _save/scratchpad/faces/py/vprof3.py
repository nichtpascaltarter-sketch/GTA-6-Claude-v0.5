# vprof3.py img x0 x1 y0 y1 [scale]: per row, the linear mean of R, G and B over columns x0..x1 (debug views decoded
# from sRGB), each times scale (default 4: undoes the views' 0.25)
import sys
from PIL import Image
import numpy as np
img = sys.argv[1]
x0, x1, y0, y1 = map(int, sys.argv[2:6])
sc = float(sys.argv[6]) if len(sys.argv) > 6 else 4.0
a = np.asarray(Image.open(img).convert("RGB")).astype(np.float32) / 255.0
lin = np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
for y in range(y0, y1):
    m = lin[y, x0:x1].mean(0) * sc
    print(f"y{y}: R {m[0]:6.3f}  G {m[1]:6.3f}  B {m[2]:6.3f}   " + "r" * int(m[0] * 20) + " | " + "g" * int(m[1] * 20))
