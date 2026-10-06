import sys
from PIL import Image, ImageDraw
d, out, cols = sys.argv[1], sys.argv[2], int(sys.argv[3])
names = sys.argv[4:]
ims = []
for n in names:
    for ext in ('.ppm', '.png', '.bmp'):
        try:
            ims.append((n, Image.open(f"{d}/{n}{ext}").convert('RGB'))); break
        except Exception: pass
w, h = 640, 360
rows = (len(ims) + cols - 1) // cols
g = Image.new('RGB', (cols * w, rows * h), (0, 0, 0))
for i, (n, im) in enumerate(ims):
    im = im.resize((w, h))
    ImageDraw.Draw(im).text((6, 4), n, fill=(255, 255, 0))
    g.paste(im, ((i % cols) * w, (i // cols) * h))
g.save(out)
print(out, g.size)
