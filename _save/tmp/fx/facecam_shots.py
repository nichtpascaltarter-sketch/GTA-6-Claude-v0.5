#!/usr/bin/env python3
# Facecam shot list for the character viewer (--viewer characters --facecam ...): prints the arguments.
# usage: facecam_shots.py people dists hours [camYawDay] ; e.g. facecam_shots.py 27,7,17,10 0.6,1,4 15.5,23
# The camera stands on the open ground west of the lineup (ground ~4.38 m), eye height 1.6 m, looking along camYaw
# (degrees, 0 north, 90 west); each shot gets its own position (3 m apart) so the viewer can tell them apart.
import sys
people = [int(x) for x in sys.argv[1].split(',')]
dists = [float(x) for x in sys.argv[2].split(',')]
hours = [float(x) for x in sys.argv[3].split(',')]
yawDay = float(sys.argv[4]) if len(sys.argv) > 4 else -90.0
args = []
k = 0
for h in hours:
    night = h >= 19 or h < 6
    for p in people:
        for d in dists:
            x = -330.0 + 3.0 * k
            y = 1450.0
            z = 4.38 + 1.6
            pitch = -1.0 if d < 2 else -2.0
            yaw = 0.0 if night else yawDay
            name = 'fc%d_%sm_%s' % (p, ('%g' % d).replace('.', 'p'), 'night' if night else 'day')
            args.append('--shot %.2f,%.2f,%.2f,%.1f,%.1f,%g,%s --facecam %d,%g,0' % (x, y, z, yaw, pitch, h, name, p, d))
            k += 1
print(' '.join(args))
