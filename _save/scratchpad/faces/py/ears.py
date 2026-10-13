# ears.py cond dir: at 1 m, the ears' brightness against the cheeks (stops; + = ears brighter). The viewer frames every
# head alike (eyes at the image centre, y 268); the ears span rows 262-284 at the head's outer edge. The skin edge is
# found per row as the outermost pixel within 70 px of the centre whose colour differs from the background column.
import sys, math
from PIL import Image
import numpy as np
def lin(a):
    a = a / 255.0
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)
cond, d = sys.argv[1], sys.argv[2]
out = []
for n in ["c0", "c1", "c7", "c17", "c27", "c38", "c13", "c20", "c14", "c18"]:
    try:
        im = np.asarray(Image.open(f"{d}/{n}_1m_{cond}.png").convert("RGB")).astype(np.float32)
    except Exception:
        continue
    L = 0.2126 * lin(im[..., 0]) + 0.7152 * lin(im[..., 1]) + 0.0722 * lin(im[..., 2])
    ear, cheek, earC, cheekC = [], [], [], []
    for y in range(266, 282):
        row = im[y]
        bg = (row[400] + row[560]) * 0.5
        diff = np.abs(row - bg).sum(axis=1)
        xs = [x for x in range(410, 551) if diff[x] > 40]
        if len(xs) < 20:
            continue
        x0, x1 = xs[0], xs[-1]
        ear += list(L[y, x0 + 1:x0 + 4]) + list(L[y, x1 - 3:x1])
        cheek += list(L[y, x0 + 12:x0 + 18]) + list(L[y, x1 - 17:x1 - 11])
        earC += list(im[y, x0 + 1:x0 + 4]) + list(im[y, x1 - 3:x1])
        cheekC += list(im[y, x0 + 12:x0 + 18]) + list(im[y, x1 - 17:x1 - 11])
    if ear and cheek:
        e, c = np.mean(earC, axis=0), np.mean(cheekC, axis=0)
        out.append(f"{n} {math.log2(max(np.mean(ear), 1e-6) / max(np.mean(cheek), 1e-6)):+.1f} R/G {e[0] / max(e[1], 1):.2f} vs {c[0] / max(c[1], 1):.2f}")
print(f"{d:10s} {cond:5s} ears vs cheeks (stops): " + " | ".join(out))
