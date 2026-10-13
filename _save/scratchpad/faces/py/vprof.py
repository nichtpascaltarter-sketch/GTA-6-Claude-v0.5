import sys
from PIL import Image
import numpy as np
# vprof.py image x0 x1 y0 y1 : mean luminance per row over columns x0..x1 (prints row, L, rgb)
im = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.float32)
x0, x1, y0, y1 = map(int, sys.argv[2:6])
for y in range(y0, y1):
    r = im[y, x0:x1]
    L = 0.2126*r[:,0] + 0.7152*r[:,1] + 0.0722*r[:,2]
    m = r.mean(0)
    print(f"y{y}: L {L.mean():5.1f} (min {L.min():5.1f} max {L.max():5.1f})  rgb {m[0]:.0f},{m[1]:.0f},{m[2]:.0f}  " + '#' * int(L.mean() / 4))
