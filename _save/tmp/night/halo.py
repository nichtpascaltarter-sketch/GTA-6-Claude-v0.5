import sys
import numpy as np
from PIL import Image
# halo.py img x0 y0 x1 y1 : share of pixels brighter (luma) than all 4 neighbours by more than 2/255 and 4/255
img = np.asarray(Image.open(sys.argv[1]).convert('RGB')).astype(np.float32)
x0, y0, x1, y1 = [int(v) for v in sys.argv[2:6]]
Y = img[..., 0] * 0.2126 + img[..., 1] * 0.7152 + img[..., 2] * 0.0722
c = Y[y0:y1, x0:x1]
n = np.maximum(np.maximum(Y[y0-1:y1-1, x0:x1], Y[y0+1:y1+1, x0:x1]), np.maximum(Y[y0:y1, x0-1:x1-1], Y[y0:y1, x0+1:x1+1]))
o = c - n
print('%s overshoot>2: %.4f  >4: %.4f  mean overshoot of those: %.2f' % (sys.argv[1], np.mean(o > 2), np.mean(o > 4), o[o > 2].mean() if np.any(o > 2) else 0))
