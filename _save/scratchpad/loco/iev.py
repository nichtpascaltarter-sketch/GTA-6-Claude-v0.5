import sys, re, math
# iev.py <stderr log> [min_frames]: LM_IEV episodes (consecutive frames of one foot planted on the stairs and sunk or
# lifted past 3 cm) with where they are on the stair - the geometry as locometer.cpp's interior() table has it
G = {  # name: (kind, rise, tread, width, land, n, R, ang, rIn, rOut)
    'switchback.up_1.0': (5, .175, .28, 1.1, 1.1, 9), 'switchback.down_1.0': (5, .175, .28, 1.1, 1.1, 9),
    'switchback_tight.up_1.0': (5, .18, .27, .9, .9, 9), 'switchback_tight.down_1.0': (5, .18, .27, .9, .9, 9),
    'lstair.up_1.0': (7, .175, .28, 1.0, 1.0, 8), 'lstair.down_1.0': (7, .175, .28, 1.0, 1.0, 8),
    'winder.up_0.9': (8, .18, .28, 1.0, 1.0, 7), 'winder.down_0.9': (8, .18, .28, 1.0, 1.0, 7),
    'spiral.up_0.7': (6, .2, 0, 0, 0, 10, .55, .5236, .1, .85), 'spiral.down_0.7': (6, .2, 0, 0, 0, 10, .55, .5236, .1, .85),
    'spiral_tight.up_0.6': (6, .2, 0, 0, 0, 13, .5, .3927, .1, .8), 'spiral_tight.down_0.6': (6, .2, 0, 0, 0, 13, .5, .3927, .1, .8),
    'curved.up_1.0': (6, .17, 0, 0, 0, 16, 3.0, .1, 2.3, 3.7), 'curved.down_1.0': (6, .17, 0, 0, 0, 16, 3.0, .1, 2.3, 3.7),
}
Y0 = 2.0
def where(name, x, y):
    g = G.get(name)
    if not g: return '?'
    k = g[0]
    if k == 6:
        th = math.atan2(y, x)
        if th < 0: th += 2 * math.pi
        r = math.hypot(x, y)
        n, a = g[5], g[7]
        return 'tr%d r%.2f' % (int(th / a), r) if th < n * a else 'top r%.2f' % r
    rise, tread, W, land, n = g[1:6]
    top = Y0 + n * tread
    if y < Y0: return 'floor' if k != 5 or x < 0 else 'floor2'
    if k == 5:
        if x < 0 and y < top: return 'f1 tr%d u%.2f' % (int((y - Y0) / tread), (y - Y0) % tread)
        if y >= top: return 'land d%.2f' % (y - top)
        return 'f2 tr%d u%.2f' % (int((top - y) / tread), (top - y) % tread)
    xs = 0.5 * W
    if y < top: return 'f1 tr%d u%.2f' % (int((y - Y0) / tread), (y - Y0) % tread) if x < xs else 'under'
    if x < xs: return ('land' if k == 7 else 'wind') + ' %.2f,%.2f' % (x - xs, y - top)
    return 'f2 tr%d u%.2f' % (int((x - xs) / tread), (x - xs) % tread)
pat = re.compile(r'IEV (\S+) p(\d+) run (\d+) t([\d.]+) foot (\d) low\s*([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+) at\s*([-\d.]+),\s*([-\d.]+) fy\s*([-\d.]+) gz\s*([-\d.]+)/\s*([-\d.]+)/\s*([-\d.]+) age ([\d.]+) st\s*([-\d.]+) v ([\d.]+) tr\s*([-\d.]+) yaw\s*([-\d.]+)')
minf = int(sys.argv[2]) if len(sys.argv) > 2 else 1
eps = []
cur = {}
for line in open(sys.argv[1]):
    m = pat.match(line)
    if not m: continue
    name, p, run, t, foot = m.group(1), int(m.group(2)), int(m.group(3)), float(m.group(4)), int(m.group(5))
    low = float(m.group(6))
    key = (name, run, foot)
    e = cur.get(key)
    sign = low < 0
    if e and t - e['t1'] < 0.04 and e['sign'] == sign:
        e['t1'] = t; e['n'] += 1
        if abs(low) > abs(e['low']): e.update(low=low, hbt=(m.group(7), m.group(8), m.group(9)), at=(float(m.group(10)), float(m.group(11))), age=float(m.group(16)), st=float(m.group(17)), tr=float(m.group(19)))
    else:
        e = dict(name=name, p=p, run=run, foot=foot, t0=t, t1=t, n=1, sign=sign, low=low, hbt=(m.group(7), m.group(8), m.group(9)),
                 at=(float(m.group(10)), float(m.group(11))), age=float(m.group(16)), st=float(m.group(17)), tr=float(m.group(19)), v=float(m.group(18)))
        cur[key] = e
        eps.append(e)
by = {}
for e in eps:
    if e['n'] < minf: continue
    by.setdefault(e['name'], []).append(e)
for name, es in by.items():
    sinks = [e for e in es if e['sign']]
    lifts = [e for e in es if not e['sign']]
    print('== %s: %d sunk episodes (%d frames), %d lifted (%d frames)' % (name, len(sinks), sum(e['n'] for e in sinks), len(lifts), sum(e['n'] for e in lifts)))
    for e in sorted(es, key=lambda e: -abs(e['low']) * e['n'] ** 0.5)[:14]:
        print('  %s p%d run %3d t%6.3f foot %d %3d fr low %6.1f h/b/t %s/%s/%s at %5.2f,%5.2f %-16s age0 %.2f st %5.2f tr %5.2f' % (
            'SINK' if e['sign'] else 'LIFT', e['p'], e['run'], e['t0'], e['foot'], e['n'], e['low'], e['hbt'][0], e['hbt'][1], e['hbt'][2],
            e['at'][0], e['at'][1], where(name, *e['at']), e['age'], e['st'], e['tr']))
