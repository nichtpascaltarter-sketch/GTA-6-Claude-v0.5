import sys, re
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
keys = ['slide_cm_step_mean', 'slide_cm_step_p90', 'slide_cm_step_max', 'pivot_deg_step_mean', 'planted_float_frames', 'planted_float_max_mm', 'scuffs', 'steps', 'lowest_sole_mm']
short = ['slMean', 'slP90', 'slMax', 'pivMean', 'flFr', 'flMax', 'scuffs', 'steps', 'lowest']
hz = sys.argv[1]
tags = sys.argv[2:]
d = {t: load('tw_%s_%s.txt' % (t, hz)) for t in tags}
scen = sorted(set(k.rsplit('.', 1)[0] for k in d[tags[0]] if k.startswith('turn')))
for sc in scen:
    print('%s @%s Hz %s' % (sc, hz, ' '.join('%7s' % k for k in short)))
    for t in tags:
        print('   %-6s %s' % (t, ' '.join('%7.2f' % d[t].get(sc + '.' + k, float('nan')) for k in keys)))
