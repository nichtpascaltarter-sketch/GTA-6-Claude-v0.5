# imgdiff.py dirA dirB [prefix...]: per shot, mean and 99.9th-percentile absolute difference (8-bit) between A and B
import sys, os
from PIL import Image
import numpy as np
A, B = sys.argv[1], sys.argv[2]
pre = sys.argv[3:]
for f in sorted(os.listdir(A)):
    if not f.endswith('.png') or (pre and not any(f.startswith(p) for p in pre)): continue
    fb = os.path.join(B, f)
    if not os.path.exists(fb): print(f, 'missing in B'); continue
    a = np.asarray(Image.open(os.path.join(A, f)).convert('RGB')).astype(np.float32)
    b = np.asarray(Image.open(fb).convert('RGB')).astype(np.float32)
    d = np.abs(a - b).max(axis=2)
    print('%-22s mean %6.3f  p99.9 %5.1f  max %5.1f' % (f, d.mean(), np.percentile(d, 99.9), d.max()))
