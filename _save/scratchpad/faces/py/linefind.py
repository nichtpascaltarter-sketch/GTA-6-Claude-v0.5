import sys
from PIL import Image, ImageFilter
import numpy as np
# linefind.py image x0 y0 x1 y1 : for each row in the box, the column of the strongest dark high-pass response
# (the line), and the luminance a few px either side of it along the row (outside = toward x0, inside = toward x1)
img = Image.open(sys.argv[1]).convert('RGB')
a = np.asarray(img).astype(np.float32)
L = 0.2126*a[...,0] + 0.7152*a[...,1] + 0.0722*a[...,2]
bl = np.asarray(Image.fromarray(L.astype(np.uint8)).filter(ImageFilter.GaussianBlur(3))).astype(np.float32)
hp = bl - L   # positive where darker than surroundings
x0, y0, x1, y1 = map(int, sys.argv[2:6])
d = int(sys.argv[6]) if len(sys.argv) > 6 else 3
rs = []
for y in range(y0, y1):
    row = hp[y, x0:x1]
    x = x0 + int(np.argmax(row))
    if row.max() < 2: continue
    o = L[y, max(x-d-1,0):x-d+1].mean(); i = L[y, x+d:x+d+2].mean()
    rs.append(i / max(o, 1e-3))
    print(f"y{y}: line x{x} hp {row.max():.1f}  L outside {o:.0f} at line {L[y,x]:.0f} inside {i:.0f}  ratio in/out {i/max(o,1e-3):.2f}")
if rs: print("median ratio", np.median(rs))
