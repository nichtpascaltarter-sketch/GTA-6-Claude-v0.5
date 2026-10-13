import sys
import numpy as np
from PIL import Image
# imgstats.py img.png [x0,y0,x1,y1 (fractions) ...] : per region: mean sRGB, mean luminance (sRGB-encoded Y),
# share of clipped pixels (max channel >= 0.97 and min channel >= 0.9: near-white), share of very dark (Y < 0.04),
# saturation mean, p50/p90/p99 of luminance.
def srgb_to_lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
img = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.float32) / 255.0
H, W, _ = img.shape
regions = sys.argv[2:] or ['0,0,1,1']
for r in regions:
    x0, y0, x1, y1 = [float(v) for v in r.split(',')]
    sub = img[int(y0 * H):int(y1 * H), int(x0 * W):int(x1 * W)]
    lin = srgb_to_lin(sub)
    Y = lin @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)
    mx, mn = sub.max(axis=2), sub.min(axis=2)
    white = np.mean((mx >= 0.97) & (mn >= 0.9))
    clip = np.mean(mx >= 0.985)
    dark = np.mean(Y < 0.0032)  # sRGB ~ 0.04
    sat = np.mean((mx - mn) / np.maximum(mx, 1e-4))
    p = np.percentile(Y, [10, 50, 90, 99])
    m = sub.reshape(-1, 3).mean(axis=0)
    print('%-16s rgb %.3f %.3f %.3f  Ylin p10 %.4f p50 %.4f p90 %.4f p99 %.4f  white %.3f clip %.3f dark %.3f sat %.2f' %
          (r, m[0], m[1], m[2], p[0], p[1], p[2], p[3], white, clip, dark, sat))
