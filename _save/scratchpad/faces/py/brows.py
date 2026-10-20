# brows.py cond dir [dir2 ...]: brow and socket darkness in the 1 m portraits (cond = day | night) at fixed spots
# (the viewer frames every person alike at 1 m: eyes at y 268, brows 255-264, forehead 242-250; eye columns x 466-470
# and 494-498): brow = darkest 2-row window in rows 255-264, socket = darkest 3-row window in rows 263-272, both against
# the forehead (linear), and in stops
import sys, math
from PIL import Image
import numpy as np
def lin(a):
    a = a / 255.0
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
cond = sys.argv[1]
for d in sys.argv[2:]:
    print("==", d, cond)
    for n in ["c0", "c1", "c7", "c17", "c27", "c38"]:
        try:
            im = np.asarray(Image.open(f"{d}/{n}_1m_{cond}.png").convert("RGB")).astype(np.float32)
        except Exception:
            print(n, "missing"); continue
        L = 0.2126 * lin(im[..., 0]) + 0.7152 * lin(im[..., 1]) + 0.0722 * lin(im[..., 2])
        fr, br, so = [], [], []
        for x0 in (466, 494):
            prof = L[:, x0:x0 + 5].mean(1)
            fr.append(prof[242:251].mean())
            b2 = np.convolve(prof[255:265], np.ones(2) / 2, mode="valid")
            br.append(b2.min())
            s3 = np.convolve(prof[263:273], np.ones(3) / 3, mode="valid")
            so.append(s3.min())
        f, b, s = np.mean(fr), np.mean(br), np.mean(so)
        print(f"{n}: forehead {f:.4f}  brow {b / f:.2f} ({math.log2(f / max(b, 1e-6)):.1f} stops below)  socket/eye {s / f:.3f} ({math.log2(f / max(s, 1e-6)):.1f} stops below)")
