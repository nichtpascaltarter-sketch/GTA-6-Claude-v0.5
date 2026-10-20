import sys, re, glob, statistics as st
# per config and rate: sunk-3cm % (mean over seeds) for crowd / blocked / blocked+SEPVEL up and down, and the quick
# steps' drag (cm, summed) and frames sunk > 3 cm (summed over seeds)
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
rx = re.compile(r'QS run (\d+) (\w+) t([\d.]+) foot (\d) u ([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+) .*? yh ([-\d.]+)')
def qs(files):
    drag = 0.0; sunk = 0; n = 0
    for fn in files:
        last = {}
        for ln in open(fn):
            m = rx.match(ln)
            if not m: continue
            run, s, t, u = m.group(1), m.group(4), float(m.group(3)), float(m.group(5))
            lo = min(float(m.group(6)), float(m.group(7)), float(m.group(8))); yh = float(m.group(9))
            k = (run, s)
            if u < 0.01: n += 1
            if k in last and t - last[k][0] < 0.05 and u > 0.01:
                mv = abs(yh - last[k][1]) * 100
                if lo < 1.5 and mv > 1: drag += mv
            if lo < -3: sunk += 1
            last[k] = (t, yh)
    return n, drag, sunk
hz = sys.argv[1]
print('%-8s %s | %s' % (hz, ' '.join('%8s' % c for c in ['cr.up', 'cr.dn', 'bl.up', 'bl.dn', 'sv.up', 'sv.dn']), 'qs steps / drag cm / sunk frames (crowd, blocked, blocked+SV)'))
for tag in sys.argv[2:]:
    out = []
    for suf, key in [('', 'stairs17.crowd.up'), ('', 'stairs17.crowd.down'), ('b', 'stairs17.blocked.up'), ('b', 'stairs17.blocked.down'), ('s', 'stairs17.blocked.up'), ('s', 'stairs17.blocked.down')]:
        v = [load(f)[key + '.planted_sunk_3cm_pct'] for f in sorted(glob.glob('q_%s_%s_?%s.txt' % (tag, hz, suf)))]
        out.append('%8.2f' % st.mean(v) if v else '     n/a')
    q = [qs(sorted(glob.glob('qq_%s_%s_?%s.txt' % (tag, hz, suf)))) for suf in ['', 'b', 's']]
    print('%-8s %s | %s' % (tag, ' '.join(out), '  '.join('%d/%.0f/%d' % x for x in q)))
