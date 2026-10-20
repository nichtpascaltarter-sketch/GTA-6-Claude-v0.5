import sys, re, glob, statistics as st
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
keys = ['up.slide_cm_step_mean', 'up.slide_cm_step_p90', 'down.slide_cm_step_mean', 'down.slide_cm_step_p90', 'up.steps', 'down.steps', 'up.planted_float_frames', 'down.planted_float_frames', 'up.planted_across_riser_pct', 'down.planted_across_riser_pct', 'up.lowest_sole_mm', 'down.lowest_sole_mm']
hz = sys.argv[1]
print('%-8s %s' % (hz, ' '.join('%8s' % k.replace('slide_cm_step_', 'sl').replace('planted_', '').replace('_frames', '').replace('across_riser_pct', 'acr%').replace('lowest_sole_mm', 'low')[:8] for k in keys)))
for tag in sys.argv[2:]:
    v = [load(f) for f in sorted(glob.glob('ss_%s_%s_?.txt' % (tag, hz)))]
    out = []
    for k in keys:
        a = [x['stairs17.stops.' + k] for x in v]
        out.append('%8.2f' % (min(a) if 'lowest' in k else st.mean(a)))
    print('%-8s %s' % (tag, ' '.join(out)))
