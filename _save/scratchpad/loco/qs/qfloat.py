import glob, re, sys, statistics as st
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
tags = sys.argv[1:]
print('float max mm / float frames (mean) / lifted % / scuffs - per config:', ' | '.join(tags))
for hz in ('30', '60'):
    for sc, suf in (('crowd', ''), ('blocked', 'b'), ('blocked', 's')):
        for d in ('up', 'down'):
            out = []
            for tag in tags:
                vals = [load(f) for f in sorted(glob.glob('q_%s_%s_?%s.txt' % (tag, hz, suf)))]
                k = 'stairs17.%s.%s.' % (sc, d)
                out.append('%5.0f %5.1f %4.2f %4.1f' % (max(x[k + 'planted_float_max_mm'] for x in vals), st.mean(x[k + 'planted_float_frames'] for x in vals),
                                                      st.mean(x[k + 'planted_lifted_pct'] for x in vals), st.mean(x[k + 'scuffs'] for x in vals)))
            print('%s %-8s%-1s %-4s' % (hz, sc, suf, d), ' | '.join(out))
