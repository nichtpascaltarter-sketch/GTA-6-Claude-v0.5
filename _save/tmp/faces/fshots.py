#!/usr/bin/env python3
# Face portrait shots with the viewer's --facecam (src/game/viewer.cpp): one --shot + one --facecam per portrait.
# usage: fshots.py "27,7,17" "0.5,1,4" "15.5,22" [camYaw] -> prints the argument list
import sys
chars = [int(x) for x in sys.argv[1].split(',')]
dists = [float(x) for x in sys.argv[2].split(',')]
hours = [float(x) for x in sys.argv[3].split(',')]
yaw = float(sys.argv[4]) if len(sys.argv) > 4 else -132.0
fyaw = float(sys.argv[5]) if len(sys.argv) > 5 else 0.0
G = 4.38
args = []
n = 0
for h in hours:
    tag = 'day' if 6 < h < 19 else 'night'
    for i in chars:
        for d in dists:
            # a unique camera position per shot (the viewer matches the shot by position and hour)
            x = -280.0 + 0.37 * n
            y = 1480.0 - 0.29 * n
            z = G + 1.62
            name = 'c%d_%sm_%s' % (i, ('%g' % d).replace('.', 'p'), tag)
            args.append('--shot %.3f,%.3f,%.3f,%.1f,0,%g,%s --facecam %d,%g,%g' % (x, y, z, yaw, h, name, i, d, fyaw))
            n += 1
print(' '.join(args))
