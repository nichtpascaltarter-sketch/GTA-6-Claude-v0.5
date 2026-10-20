import sys, re
# better.py a b [filter]: like worse.py, but lists both the worse and the better keys (same floors), sorted by change
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
a = load(sys.argv[1]); b = load(sys.argv[2])
flt = sys.argv[3] if len(sys.argv) > 3 else None
floor = {'slide': 0.1, 'pivot': 0.5, 'float_frames': 2, 'float_max': 2, 'scuffs': 2, 'lowest': 2, 'sunk_pct': 0.3, 'sunk_3cm': 0.2, 'deep': 2, 'lifted': 0.3}
rows = []
for k in a:
    if k not in b or (flt and flt not in k): continue
    va, vb = a[k], b[k]
    kind = None
    for f in floor:
        if f in k: kind = f
    if not kind: continue
    worse = (vb - va) if kind != 'lowest' else (va - vb)
    if abs(worse) > floor[kind] and abs(worse) > 0.05 * abs(va):
        rows.append(('WORSE' if worse > 0 else 'better', k, va, vb))
for r in sorted(rows, key=lambda r: (r[0], r[1])):
    print("%-6s %-56s %10.3f %10.3f" % r)
print("worse %d better %d" % (sum(1 for r in rows if r[0] == 'WORSE'), sum(1 for r in rows if r[0] == 'better')))
