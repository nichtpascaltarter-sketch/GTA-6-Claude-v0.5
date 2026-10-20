import sys, re, glob, statistics as st
# mean over seeds (min for lowest_sole, max for *_max) of the main stair metrics; usage: mstat.py <hz> <suffix: ''|b|s> <scenario prefix> tags...
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
hz, suf, pre = sys.argv[1], sys.argv[2], sys.argv[3]
keys = ['slide_cm_step_mean', 'slide_cm_step_p90', 'slide_cm_step_max', 'planted_sunk_3cm_pct', 'planted_lifted_pct', 'planted_float_frames', 'planted_float_max_mm', 'scuffs', 'quick_steps', 'lowest_sole_mm']
short = ['slMean', 'slP90', 'slMax', 'sunk3', 'lifted', 'flFr', 'flMax', 'scuffs', 'qs', 'lowest']
print('%-5s %-4s %s' % (hz + suf, 'dir', ' '.join('%7s' % k for k in short)))
for tag in sys.argv[4:]:
    v = [load(f) for f in sorted(glob.glob('q_%s_%s_?%s.txt' % (tag, hz, suf)))]
    for d in ['up', 'down']:
        out = []
        for k in keys:
            a = [x.get('%s.%s.%s' % (pre, d, k), float('nan')) for x in v]
            out.append('%7.2f' % (min(a) if 'lowest' in k else (max(a) if k.endswith('_max') or k.endswith('max_mm') else st.mean(a))))
        print('%-5s %-4s %s' % (tag, d, ' '.join(out)))
