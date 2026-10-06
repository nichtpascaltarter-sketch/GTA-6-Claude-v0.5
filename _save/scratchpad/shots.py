#!/usr/bin/env python3
# Generates --shot arguments for an interior from the game log's plan lines.
# usage: shots.py <log> <interior-name-or-index> <hour> [views...]
import sys, re, math
log, key, hour = sys.argv[1], sys.argv[2], float(sys.argv[3])
views = sys.argv[4:] or ['street', 'door', 'in_front', 'in_back', 'in_left', 'in_right']
rx = re.compile(r"interior '(.*?)' kind (\d+) building (-?\d+) at \(([-\d.]+), ([-\d.]+), ([-\d.]+)\) ax \(([-\d.]+), ([-\d.]+)\) x ([-\d.]+)\.\.([-\d.]+) depth ([-\d.]+) ceil ([-\d.]+)")
defs = []
for line in open(log, errors='ignore'):
    m = rx.search(line)
    if m:
        g = m.groups()
        defs.append(dict(name=g[0], kind=int(g[1]), o=(float(g[3]), float(g[4]), float(g[5])), ax=(float(g[6]), float(g[7])),
                         x0=float(g[8]), x1=float(g[9]), depth=float(g[10]), ceil=float(g[11])))
if not defs:
    sys.exit("no interiors in log")
d = None
for i, dd in enumerate(defs):
    if dd['name'] == key or str(i) == key:
        d = dd
if d is None:
    sys.exit("interior not found: " + key + " (have: " + ", ".join(x['name'] for x in defs) + ")")
ox, oy, oz = d['o']; axx, axy = d['ax']; ayx, ayy = -axy, axx
def W(x, y, z):
    return (ox + axx * x + ayx * y, oy + axy * x + ayy * y, oz + z)
def yawdeg(dx, dy):  # local direction -> world yaw degrees
    wx, wy = axx * dx + ayx * dy, axy * dx + ayy * dy
    return math.degrees(math.atan2(-wx, wy))
tag = re.sub(r'[^a-z0-9]+', '_', d['name'].lower()).strip('_')
x0, x1, dep, ceil = d['x0'], d['x1'], d['depth'], d['ceil']
out = []
for v in views:
    if v == 'street': p, dx, dy, pitch = W(0, -11, 1.7), 0, 1, -3
    elif v == 'door': p, dx, dy, pitch = W(0, -3.2, 1.65), 0, 1, -8
    elif v == 'in_front': p, dx, dy, pitch = W(x0 + 1.0, 0.9, 1.7), (x1 - x0) * 0.6, dep * 0.8, -12
    elif v == 'in_front2': p, dx, dy, pitch = W(x1 - 1.0, 0.9, 1.7), -(x1 - x0) * 0.6, dep * 0.8, -12
    elif v == 'in_back': p, dx, dy, pitch = W(x1 - 1.2, dep - 4.0, 1.8), -(x1 - x0) * 0.5, -dep * 0.6, -10
    elif v == 'in_back2': p, dx, dy, pitch = W(x0 + 1.2, dep - 4.0, 1.8), (x1 - x0) * 0.5, -dep * 0.6, -10
    elif v == 'in_left': p, dx, dy, pitch = W(x0 + 0.8, dep * 0.45, 1.6), 1, 0.15, -10
    elif v == 'in_right': p, dx, dy, pitch = W(x1 - 0.8, dep * 0.45, 1.6), -1, 0.15, -10
    elif v == 'top': p, dx, dy, pitch = W(0, -18, 14), 0, 1, -35
    elif v.startswith('L:'):
        # L:name:x,y,z,dx,dy,pitch in interior-local coordinates
        _, vname, nums = v.split(':')
        lx, ly, lz, dx, dy, pitch = [float(t) for t in nums.split(',')]
        p = W(lx, ly, lz)
        v = vname
    else: continue
    out.append("--shot %.1f,%.1f,%.2f,%.1f,%.1f,%.2f,%s_%s_%02d" % (p[0], p[1], p[2], yawdeg(dx, dy), pitch, hour, tag, v, int(hour)))
print(" ".join(out))
