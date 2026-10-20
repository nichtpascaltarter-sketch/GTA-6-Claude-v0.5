import re, sys
# kittab.py <kit output> [family]: the kit sweep per scenario - sunk past 1 / 3 cm %, lifted %, slide mean / max, deep frames
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (kit\.\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
d = load(sys.argv[1])
fam = sys.argv[2] if len(sys.argv) > 2 else ''
names = sorted(set(k.rsplit('.', 1)[0] for k in d if k.endswith('.planted_sunk_pct')))
for n in names:
    if fam not in n: continue
    g = lambda m: d.get(n + '.' + m, float('nan'))
    print('%-40s sk %5.1f/%5.1f lf %4.1f sl %5.2f/%5.1f fl %3d/%3.0f dp %3d st %3d' % (n[4:], g('planted_sunk_pct'), g('planted_sunk_3cm_pct'), g('planted_lifted_pct'),
          g('slide_cm_step_mean'), g('slide_cm_step_max'), g('planted_float_frames'), g('planted_float_max_mm'), g('planted_deep_frames'), g('steps')))
