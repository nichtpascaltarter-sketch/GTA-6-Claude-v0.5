import sys, re
# stepdiff.py old new [min]: steps (LM_STEPS lines) matched by run, foot and start time; those whose slide changed most
def load(fn):
    d = {}
    for line in open(fn):
        m = re.search(r'step run (\d+) foot (\d) t ([\d.]+)-([\d.]+) slide ([\d.]+) cm pivot ([\d.]+) deg maxframe ([\d.]+)', line)
        if m: d[(int(m.group(1)), int(m.group(2)), round(float(m.group(3)), 2))] = (float(m.group(4)), float(m.group(5)), float(m.group(6)), float(m.group(7)))
    return d
a, b = load(sys.argv[1]), load(sys.argv[2])
mn = float(sys.argv[3]) if len(sys.argv) > 3 else 1.0
rows = []
for k in a:
    if k in b: rows.append((b[k][1] - a[k][1], k, a[k], b[k]))
rows.sort(key=lambda r: -abs(r[0]))
for d, k, x, y in rows:
    if abs(d) < mn: break
    print('run %2d foot %d t %.2f-%.2f: slide %5.2f -> %5.2f cm, pivot %5.1f -> %5.1f deg, maxframe %4.2f -> %4.2f' % (k[0], k[1], k[2], x[0], x[1], y[1], x[2], y[2], x[3], y[3]))
print('matched %d of %d / %d' % (len(rows), len(a), len(b)))
