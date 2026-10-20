import sys, re
# worse.py a b: keys whose value got worse (higher slide/pivot/float/scuff, lower lowest_sole) by > 5% and > an absolute floor
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
a = load(sys.argv[1]); b = load(sys.argv[2])
floor = {'slide': 0.1, 'pivot': 0.5, 'float_frames': 2, 'float_max': 2, 'scuffs': 2, 'lowest': 2, 'sunk_pct': 0.3, 'sunk_3cm': 0.2, 'deep': 2, 'lifted': 0.3}
nw = nb = 0
for k in a:
    if k not in b: continue
    va, vb = a[k], b[k]
    kind = None
    for f in floor:
        if f in k: kind = f
    if not kind: continue
    worse = (vb - va) if kind != 'lowest' else (va - vb)
    if worse > floor[kind] and worse > 0.05 * abs(va):
        print("WORSE %-52s %10.3f %10.3f" % (k, va, vb)); nw += 1
    elif -worse > floor[kind] and -worse > 0.05 * abs(va):
        nb += 1
print("worse %d better %d" % (nw, nb))
