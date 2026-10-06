import re, sys
# in-game locostairs trace: per stance of the traced walkers on a flight, the planted foot against the treads
tp = re.compile(r'locostairs trace (\d+) (up|dn) a\s*([-\d.]+) v\s*([-\d.]+) ph([-\d.]+) mw([-\d.]+)(.*)')
fp = re.compile(r'\| (\d) pl(\d) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+) g\s*([-\d.]+),\s*([-\d.]+)')
st = {}
rows = []
for ln in open(sys.argv[1]):
    m = tp.search(ln)
    if not m: continue
    uid, d, a, v = m.group(1), m.group(2), float(m.group(3)), float(m.group(4))
    for f in fp.findall(m.group(7)):
        s, pl = int(f[0]), int(f[1])
        h, b, t, gh, gt = float(f[2]), float(f[3]), float(f[4]), float(f[5]), float(f[6])
        k = (uid, s)
        if pl:
            low = min(h, b, t)
            if k not in st: st[k] = dict(uid=uid, d=d, s=s, a=a, v=v, gh=gh, gt=gt, h0=h, b0=b, t0=t, n=0, sunk=0, low=low)
            S = st[k]; S['n'] += 1; S['low'] = min(S['low'], low)
            if low < -3: S['sunk'] += 1
        elif k in st:
            rows.append(st.pop(k))
print('%d stances; %d with a frame sunk 3 cm' % (len(rows), len([r for r in rows if r['sunk']])))
for r in rows:
    if r['sunk']:
        print('%s %s foot %d a %6.2f v %.2f | at plant h %5.1f b %5.1f t %5.1f (heel g %5.2f, toe g %5.2f) | sunk %d of %d frames, lowest %.1f' % (r['uid'], r['d'], r['s'], r['a'], r['v'], r['h0'], r['b0'], r['t0'], r['gh'], r['gt'], r['sunk'], r['n'], r['low']))
