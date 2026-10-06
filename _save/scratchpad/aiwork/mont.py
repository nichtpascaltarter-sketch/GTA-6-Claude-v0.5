# AI agent: a montage of a run's shots for a look. Usage: mont.py DIR OUT.png [N=8] [COLS=4] [PATTERN=*.bmp] [WIDTH=0]
# (N evenly spaced shots in name order; WIDTH > 0 scales each tile to that width)
import sys, glob, os
from PIL import Image
d, out = sys.argv[1], sys.argv[2]
n = int(sys.argv[3]) if len(sys.argv) > 3 else 8
cols = int(sys.argv[4]) if len(sys.argv) > 4 else 4
pat = sys.argv[5] if len(sys.argv) > 5 else '*.bmp'
width = int(sys.argv[6]) if len(sys.argv) > 6 else 0
fs = sorted(glob.glob(os.path.join(d, pat)))
if not fs:
    print('no shots'); sys.exit(1)
step = max(1, len(fs) / n)
sel = [fs[min(len(fs) - 1, int(i * step))] for i in range(min(n, len(fs)))]
ims = [Image.open(f).convert('RGB') for f in sel]
if width > 0:
    ims = [im.resize((width, int(im.size[1] * width / im.size[0]))) for im in ims]
w, h = ims[0].size
rows = (len(ims) + cols - 1) // cols
m = Image.new('RGB', (w * cols, h * rows))
for i, im in enumerate(ims):
    m.paste(im, ((i % cols) * w, (i // cols) * h))
m.save(out)
print(out, [os.path.basename(f) for f in sel])
