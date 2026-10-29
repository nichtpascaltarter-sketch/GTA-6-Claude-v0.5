import sys, os
from PIL import Image, ImageChops, ImageStat
src, dst, prev = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(dst, exist_ok=True)
names = sorted(f[:-4] for f in os.listdir(src) if f.endswith('.png'))
thumbs = []
for n in names:
    im = Image.open(os.path.join(src, n + '.png')).convert('RGB')
    im.save(os.path.join(dst, n + '.jpg'), quality=92)
    t = im.resize((640, 360), Image.LANCZOS); thumbs.append(t)
    p = os.path.join(prev, n + '.jpg')
    if os.path.exists(p):
        a = im.resize((480, 270)); b = Image.open(p).convert('RGB').resize((480, 270))
        d = ImageStat.Stat(ImageChops.difference(a, b)).mean
        print('%-28s %s  mean abs diff vs prev %.1f' % (n, im.size, sum(d) / 3))
    else:
        print('%-28s %s  (no previous)' % (n, im.size))
sheet = Image.new('RGB', (1920, 720), (20, 20, 20))
for i, t in enumerate(thumbs):
    sheet.paste(t, ((i % 3) * 640, (i // 3) * 360))
sheet.save(os.path.join(dst, '_sheet.jpg'), quality=88)
print('sheet', len(thumbs))
