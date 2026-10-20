import re, sys, collections
# quick-step drag and sink frames by step progress u, and whether the step began with a sole point under its ground
rx = re.compile(r'QS run (\d+) (\w+) t([\d.]+) foot (\d) u ([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+) .*? yh ([-\d.]+)')
bins = collections.Counter(); sinkb = collections.Counter(); startSunk = [0, 0]
for fn in sys.argv[1:]:
    last = {}
    for ln in open(fn):
        m = rx.match(ln)
        if not m: continue
        run, d, t, s, u = m.group(1), m.group(2), float(m.group(3)), m.group(4), float(m.group(5))
        h, b, tt, yh = float(m.group(6)), float(m.group(7)), float(m.group(8)), float(m.group(9))
        lo = min(h, b, tt)
        k = (run, s)
        if u < 0.01:
            startSunk[0] += 1
            if lo < -3: startSunk[1] += 1
        if k in last and t - last[k][0] < 0.05 and u > 0.01:
            mv = abs(yh - last[k][1]) * 100
            ub = '%.1f' % (min(int(u * 5), 4) / 5.0)
            if lo < 1.5 and mv > 1: bins[ub] += mv
            if lo < -3: sinkb[ub] += 1
        last[k] = (t, yh)
print('drag cm by u bin:', ' '.join('%s:%.0f' % (k, bins[k]) for k in sorted(bins)))
print('sunk>3cm frames by u bin:', ' '.join('%s:%d' % (k, sinkb[k]) for k in sorted(sinkb)))
print('steps starting with a sole point >3 cm under its ground: %d of %d' % (startSunk[1], startSunk[0]))
