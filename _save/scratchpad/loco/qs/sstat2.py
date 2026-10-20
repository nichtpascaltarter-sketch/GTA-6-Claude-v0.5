import sys, re, glob, statistics as st
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
keys = ['up.planted_sunk_3cm_pct', 'down.planted_sunk_3cm_pct', 'up.slide_cm_step_max', 'down.slide_cm_step_max', 'up.quick_steps', 'down.quick_steps', 'up.scuffs', 'down.scuffs', 'up.planted_float_max_mm', 'down.planted_float_max_mm', 'up.planted_lifted_pct', 'down.planted_lifted_pct']
hz = sys.argv[1]
print('%-10s %s' % (hz, ' '.join('%9s' % k.replace('planted_', '').replace('_pct', '%').replace('_cm_step_max', 'Max').replace('_max_mm', 'Max')[:9] for k in keys)))
for tag in sys.argv[2:]:
    v = [load(f) for f in sorted(glob.glob('ss_%s_%s_?.txt' % (tag, hz)))]
    out = []
    for k in keys:
        a = [x['stairs17.stops.' + k] for x in v]
        out.append('%9.2f' % (max(a) if 'Max' in k or 'max' in k else st.mean(a)))
    print('%-10s %s' % (tag, ' '.join(out)))
