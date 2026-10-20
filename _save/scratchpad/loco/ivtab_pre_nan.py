import re, sys, math
# ivtab.py a.txt b.txt [label a] [label b]: interior rows side by side (steps, slide mean/max, floats n/max mm, sunk 1/3 cm %, lifted %)
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
a = load(sys.argv[1]); b = load(sys.argv[2])
la = sys.argv[3] if len(sys.argv) > 3 else 'a'; lb = sys.argv[4] if len(sys.argv) > 4 else 'b'
keys = sorted(set(k.rsplit('.', 1)[0] for k in list(a) + list(b) if k.endswith('.planted_sunk_pct')))
def row(d, k):
    g = lambda m: d.get(k + '.' + m, float('nan'))
    if math.isnan(g('steps')): return '%-55s' % '-'
    return 'st %3d sl %5.2f/%5.1f fl %3d/%3.0f sk %4.1f/%4.1f dp %2d lf %4.1f' % (g('steps'), g('slide_cm_step_mean'), g('slide_cm_step_max'), g('planted_float_frames'), g('planted_float_max_mm'), g('planted_sunk_pct'), g('planted_sunk_3cm_pct'), g('planted_deep_frames'), g('planted_lifted_pct'))
print('%-34s | %-55s | %-55s' % ('', la, lb))
for k in keys: print('%-34s | %s | %s' % (k, row(a, k), row(b, k)))
