import sys, struct
import numpy as np
def load_bmp(p):
    d = open(p,'rb').read()
    off = struct.unpack_from('<I', d, 10)[0]; w = struct.unpack_from('<i', d, 18)[0]; h = struct.unpack_from('<i', d, 22)[0]
    rb = (w*3+3)//4*4
    a = np.frombuffer(d, np.uint8, rb*abs(h), off).reshape(abs(h), rb)[:, :w*3].reshape(abs(h), w, 3)
    if h > 0: a = a[::-1]
    # BMP stores B,G,R; screenshot wrote B=src[2], G=src[1], R=src[0] -> source bytes = (R,G,B) = (a[...,2], a[...,1], a[...,0])
    src = np.stack([a[...,2], a[...,1], a[...,0]], -1)
    return src
p = sys.argv[1]
s = load_bmp(p)
h, w, _ = s.shape
print(p, w, h)
# adjacent-pixel diff per row, find band rows
d = np.abs(s[:,1:].astype(int) - s[:,:-1].astype(int)).mean(axis=(1,2))
red = ((s[...,0] > 100) & (s[...,1] < 40) & (s[...,2] < 40)).mean(axis=1)
for y in range(0, h, max(1, h//40)):
    print('row %3d diff %5.1f red %.2f first px bytes: %s' % (y, d[y], red[y], ' '.join('%02x%02x%02x' % tuple(s[y, x]) for x in range(8))))
