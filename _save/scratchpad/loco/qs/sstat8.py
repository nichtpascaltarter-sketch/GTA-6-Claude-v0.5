import re, glob, statistics as st, sys
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
keys = ['slide_cm_step_mean', 'slide_cm_step_p90', 'slide_cm_step_max', 'planted_sunk_3cm_pct', 'planted_lifted_pct', 'planted_float_frames', 'planted_float_max_mm', 'scuffs', 'quick_steps', 'steps']
for hz in ('30', '60'):
    print('== stops %s Hz' % hz, ' '.join('%8s' % k[:8] for k in keys))
    for tag in sys.argv[1:]:
        v = [load(f) for f in sorted(glob.glob('ss_%s_%s_?.txt' % (tag, hz)))]
        for d in ('up', 'down'):
            out = []
            for k in keys:
                a = [x['stairs17.stops.%s.%s' % (d, k)] for x in v]
                out.append('%8.2f' % (max(a) if k.endswith('max') or k.endswith('max_mm') else st.mean(a)))
            print('%-6s n%d %-4s %s' % (tag, len(v), d, ' '.join(out)))
