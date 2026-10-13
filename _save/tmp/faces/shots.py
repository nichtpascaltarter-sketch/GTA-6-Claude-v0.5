#!/usr/bin/env python3
# Face shot list for the character viewer (--viewer characters --count N --clip 0): character i stands at
# (-300 + 1.6 i, 1520), turned to face south (-y); the camera stands d metres south of its eyes looking north.
import sys
G = float(sys.argv[1]) if len(sys.argv) > 1 else 4.38
eyes = {}
for line in open('/tmp/faces/eyes.txt'):
    p = line.split()
    i = int(p[0].rstrip(':'))
    ey = float(p[13]); ez = float(p[14])
    eyes[i] = (ey, ez)
chars = [int(x) for x in (sys.argv[2] if len(sys.argv) > 2 else '27,7,17,10,13,38').split(',')]
dists = [float(x) for x in (sys.argv[3] if len(sys.argv) > 3 else '1,4').split(',')]
hours = [float(x) for x in (sys.argv[4] if len(sys.argv) > 4 else '10.5,22').split(',')]
out = []
for h in hours:
    for i in chars:
        ey, ez = eyes[i]
        x = -300 + 1.6 * i
        for d in dists:
            y = 1520 - ey - d
            z = G + ez - 0.01
            pitch = -1.0 if d < 2 else -2.0
            name = 'c%d_%sm_%s' % (i, ('%g' % d).replace('.', 'p'), 'day' if h < 19 and h > 6 else 'night')
            out.append('%.3f,%.3f,%.3f,0,%.1f,%g,%s' % (x, y, z, pitch, h, name))
print(' '.join(out))
