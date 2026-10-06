import sys
from PIL import Image
import numpy as np
# seg.py image x0 y0 x1 y1 [n] : luminance sampled along the segment (bilinear), averaged over a 3 px wide strip
a = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.float32)
L = 0.2126*a[...,0] + 0.7152*a[...,1] + 0.0722*a[...,2]
x0, y0, x1, y1 = map(float, sys.argv[2:6]); n = int(sys.argv[6]) if len(sys.argv) > 6 else 15
d = np.array([x1-x0, y1-y0]); d /= np.linalg.norm(d); p = np.array([-d[1], d[0]])
def bil(x, y):
    i, j = int(np.floor(x)), int(np.floor(y)); fx, fy = x-i, y-j
    return (L[j,i]*(1-fx)*(1-fy) + L[j,i+1]*fx*(1-fy) + L[j+1,i]*(1-fx)*fy + L[j+1,i+1]*fx*fy)
out = []
for k in range(n+1):
    t = k/n; x = x0+(x1-x0)*t; y = y0+(y1-y0)*t
    v = np.mean([bil(x+p[0]*s, y+p[1]*s) for s in (-1,0,1)])
    out.append(v)
print(' '.join(f"{v:.0f}" for v in out))
