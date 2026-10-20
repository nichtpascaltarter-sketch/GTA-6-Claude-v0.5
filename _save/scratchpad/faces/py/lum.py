import sys
from PIL import Image
import numpy as np
# usage: lum.py image x y w h [label] ... prints luminance percentiles of the region (linear-ish: sRGB luma)
im = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.float32)
args = sys.argv[2:]
i = 0
while i + 3 < len(args):
    x, y, w, h = map(int, args[i:i+4]); lab = ''
    i += 4
    if i < len(args) and not args[i].lstrip('-').isdigit():
        lab = args[i]; i += 1
    r = im[y:y+h, x:x+w]
    L = 0.2126*r[...,0] + 0.7152*r[...,1] + 0.0722*r[...,2]
    p = np.percentile(L, [5, 25, 50, 75, 95])
    m = r.reshape(-1,3).mean(0)
    print(f"{lab:12s} {x},{y} {w}x{h}: L p5 {p[0]:.0f} p25 {p[1]:.0f} p50 {p[2]:.0f} p75 {p[3]:.0f} p95 {p[4]:.0f}  mean rgb {m[0]:.0f},{m[1]:.0f},{m[2]:.0f}")
