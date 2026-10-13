import re, sys
# the SCAN / PLANT lines of the traced run (between its first and last trace line), each with the time of the trace
# line after it; and the sunk / lifted planted frames of a label
lines = open(sys.argv[1]).read().split('\n')
tl = re.compile(r't([\d.]+) L(\d+) yaw')
idx = [i for i, ln in enumerate(lines) if tl.search(ln)]
a, b = idx[0], idx[-1]
pat = re.compile(r'(\d) (\d)(\d)(\d) pl(\d) st\s*([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+)')
pend = []
lab = sys.argv[2] if len(sys.argv) > 2 else None
for i in range(a, b + 1):
    ln = lines[i]
    m = tl.search(ln)
    if not m:
        if ln.startswith('SCAN') or ln.startswith('PLANT'): pend.append(ln)
        continue
    t, L = m.group(1), m.group(2)
    for p in pend: print('  @%s L%s  %s' % (t, L, p))
    pend = []
    if lab and L != lab: continue
    for f in pat.findall(ln):
        h, bb, tt = float(f[6]), float(f[7]), float(f[8])
        if int(f[4]) and (min(h, bb, tt) < -3.0 or min(h, bb, tt) > 1.5):
            print('t %s L%s foot %s h %.1f b %.1f t %.1f' % (t, L, f[0], h, bb, tt))
