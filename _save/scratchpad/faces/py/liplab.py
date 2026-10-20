# liplab.py dir [shot] : in-game lip colour at 0.5 m (sRGB screenshots): the reddest 12% of pixels in the mouth box
# (the vermilion) and the chin under it, as CIELAB; prints lip L a b, chin L a b and lip - chin per person
import sys, os
from PIL import Image
import numpy as np
def lab(rgb):
    c = rgb / 255.0
    lin = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    M = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = lin @ M.T
    xyz = xyz / np.array([0.9505, 1.0, 1.089])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    L = 116 * f[..., 1] - 16
    a = 500 * (f[..., 0] - f[..., 1])
    b = 200 * (f[..., 1] - f[..., 2])
    return np.stack([L, a, b], -1)
d = sys.argv[1]
shot = sys.argv[2] if len(sys.argv) > 2 else '0p5m_day'
for n in ['c0', 'c1', 'c7', 'c17', 'c27', 'c38', 'c10', 'c11', 'c4', 'c20']:
    p = f'{d}/{n}_{shot}.png'
    if not os.path.exists(p):
        continue
    im = np.asarray(Image.open(p).convert('RGB')).astype(np.float64)
    box = lab(im[300:380, 430:530])
    a = box[..., 1]
    thr = np.percentile(a, 88)
    ys, xs = np.nonzero(a >= thr)
    lip = box[ys, xs].mean(0)
    ybot = 300 + int(np.percentile(ys, 95))
    chin = lab(im[ybot + 10:ybot + 22, 465:495]).reshape(-1, 3).mean(0)
    dl = lip - chin
    print(f'{n:4s} lip L {lip[0]:5.1f} a {lip[1]:5.1f} b {lip[2]:5.1f} | chin L {chin[0]:5.1f} a {chin[1]:5.1f} b {chin[2]:5.1f} | d {dl[0]:+5.1f} {dl[1]:+5.1f} {dl[2]:+5.1f}')
