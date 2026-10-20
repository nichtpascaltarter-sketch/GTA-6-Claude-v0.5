import sys, re, math, glob
# ievcat.py <glob of .err files> [min_frames]: LM_IEV episodes (iev.py's grouping) totalled by scenario and by where they
# are (floor before the flight, flight 1 / 2 by tread, landing / winders, spiral treads, top) - sinks and lifts, frames
exec(open(__file__.replace('ievcat.py', 'iev.py')).read().split('pat = re.compile')[0])   # (G, Y0, where())
pat = re.compile(r'IEV (\S+) p(\d+) run (\d+) t([\d.]+) foot (\d) low\s*([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+) at\s*([-\d.]+),\s*([-\d.]+) fy\s*([-\d.]+) gz\s*([-\d.]+)/\s*([-\d.]+)/\s*([-\d.]+) age ([\d.]+) st\s*([-\d.]+) v ([\d.]+) tr\s*([-\d.]+) yaw\s*([-\d.]+)')
minf = int(sys.argv[2]) if len(sys.argv) > 2 else 1
def cls(name, x, y):
    w = where(name, x, y)
    if w.startswith('f1 tr0') : return 'f1.first'
    if w.startswith('f2 tr0') : return 'f2.first'
    if w.startswith('f1') or w.startswith('f2'):
        g = G.get(name); n = g[5] if g else 99
        k = int(w.split()[1][2:])
        if k >= n - 1: return w[:2] + '.last'
        return w[:2] + '.mid'
    if w.startswith('tr'):
        k = int(w.split()[0][2:]); g = G[name]
        return 'sp.first' if k == 0 else ('sp.last' if k >= g[5] - 1 else 'sp.mid')
    return w.split()[0]
tot = {}
files = sorted(glob.glob(sys.argv[1]))
for fn in files:
    cur = {}
    eps = []
    for line in open(fn):
        m = pat.match(line)
        if not m: continue
        name, run, t, foot, low = m.group(1), int(m.group(3)), float(m.group(4)), int(m.group(5)), float(m.group(6))
        key = (name, run, foot); e = cur.get(key); sign = low < 0
        if e and t - e['t1'] < 0.04 and e['sign'] == sign:
            e['t1'] = t; e['n'] += 1
            if abs(low) > abs(e['low']): e['low'] = low
        else:
            e = dict(name=name, n=1, t1=t, sign=sign, low=low, c=cls(name, float(m.group(10)), float(m.group(11))))
            cur[key] = e; eps.append(e)
    for e in eps:
        if e['n'] < minf: continue
        k = (e['name'], 'SINK' if e['sign'] else 'LIFT', e['c'])
        a = tot.setdefault(k, [0, 0, 0.0])
        a[0] += 1; a[1] += e['n']; a[2] = max(a[2], abs(e['low']))
print('%d files' % len(files))
last = None
for k in sorted(tot, key=lambda k: (k[0], k[1], -tot[k][1])):
    if k[0] != last: print('== ' + k[0]); last = k[0]
    a = tot[k]
    print('   %s %-10s eps %4d frames %5d worst %5.1f cm' % (k[1], k[2], a[0], a[1], a[2]))
