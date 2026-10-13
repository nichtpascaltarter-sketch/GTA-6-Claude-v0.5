# bar.py cond dir [dir2 ...]: the "goggle bar" at 1 m (viewer framing: forehead rows 244-252, the bar rows 258-266;
# the face centre column 478-482 = the nose bridge, the lid columns 456-468 and 492-504): stops below the forehead
import sys, math
from PIL import Image
import numpy as np
def lin(a):
    a = a / 255.0
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
cond = sys.argv[1]
for d in sys.argv[2:]:
    out = []
    for n in ["c0", "c1", "c7", "c17", "c27", "c38", "c13", "c20", "c14", "c18"]:
        try:
            im = np.asarray(Image.open(f"{d}/{n}_1m_{cond}.png").convert("RGB")).astype(np.float32)
        except Exception:
            continue
        L = 0.2126 * lin(im[..., 0]) + 0.7152 * lin(im[..., 1]) + 0.0722 * lin(im[..., 2])
        fore = L[244:253, 470:491].mean()
        bridge = L[258:267, 478:483].mean()
        lids = 0.5 * (L[259:264, 456:469].mean() + L[259:264, 492:505].mean())
        out.append(f"{n} bridge {math.log2(fore / max(bridge, 1e-6)):.1f} lids {math.log2(fore / max(lids, 1e-6)):.1f}")
    print(f"{d:10s} {cond:5s} | " + " | ".join(out))
