# sockets.py dir [dir2 ...]: for the 1 m night portraits in each dir, the eye sockets against the forehead: a vertical
# profile through each eye (face centre found from the lit skin), forehead = brightest 4-row window above the eyes,
# socket = darkest 3-row window at the eyes; prints both in linear units and the gap in stops
import sys, math
from PIL import Image
import numpy as np
def lin(a):
    a = a / 255.0
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
for d in sys.argv[1:]:
    print("==", d)
    for n in ["c0", "c1", "c7", "c17", "c27", "c38"]:
        try:
            im = np.asarray(Image.open(f"{d}/{n}_1m_night.png").convert("RGB")).astype(np.float32)
        except Exception as e:
            print(n, "missing"); continue
        L = 0.2126 * lin(im[..., 0]) + 0.7152 * lin(im[..., 1]) + 0.0722 * lin(im[..., 2])
        reg = L[200:340, 400:560]
        thr = np.percentile(reg, 85)
        ys, xs = np.nonzero(reg > thr)
        cx = int(np.median(xs)) + 400
        res = []
        for side in (-1, 1):
            x0 = cx + side * 10
            prof = L[200:330, x0 - 2:x0 + 3].mean(1)
            # forehead: brightest 4-row window in the upper part; sockets: darkest 3-row window below it (within 40 rows)
            fw = np.convolve(prof, np.ones(4) / 4, mode="valid")
            fi = int(np.argmax(fw[:70]))
            sw = np.convolve(prof, np.ones(3) / 3, mode="valid")
            lo = fi + 6
            si = lo + int(np.argmin(sw[lo:lo + 40]))
            res.append((fw[fi], sw[si], fi + 200, si + 200))
        f = np.mean([r[0] for r in res]); s = np.mean([r[1] for r in res])
        print(f"{n}: cx {cx} forehead {f:.4f} (y{res[0][2]}) socket {s:.5f} (y{res[0][3]}) -> {math.log2(f / max(s, 1e-6)):.1f} stops below")
