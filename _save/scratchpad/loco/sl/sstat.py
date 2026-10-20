import sys, re, statistics as st
# mean +- sd over the seed offsets of the sinking metrics, per config
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
hz = sys.argv[1]
cols = [('crowd up', '', 'stairs17.crowd.up.planted_sunk_3cm_pct'), ('crowd dn', '', 'stairs17.crowd.down.planted_sunk_3cm_pct'),
        ('block up', 'b', 'stairs17.blocked.up.planted_sunk_3cm_pct'), ('block dn', 'b', 'stairs17.blocked.down.planted_sunk_3cm_pct'),
        ('blkSV up', 's', 'stairs17.blocked.up.planted_sunk_3cm_pct'), ('blkSV dn', 's', 'stairs17.blocked.down.planted_sunk_3cm_pct')]
print('%-10s' % hz, ' '.join('%13s' % c[0] for c in cols))
for tag in sys.argv[2:]:
    out = []
    for name, suf, key in cols:
        v = []
        for k in range(8):
            try: v.append(load('s_%s_%s_%d%s.txt' % (tag, hz, k, suf))[key])
            except Exception: pass
        out.append('%6.2f+-%-5.2f' % (st.mean(v), st.stdev(v) / len(v) ** 0.5) if len(v) > 1 else '    n/a      ')
    print('%-10s' % tag, ' '.join(out))
