import sys, re
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
a = load(sys.argv[1]); b = load(sys.argv[2])
pat = sys.argv[3] if len(sys.argv) > 3 else None
keys = [k for k in a if k in b]
for k in keys:
    if pat and not re.search(pat, k): continue
    va, vb = a[k], b[k]
    flag = ''
    if abs(va - vb) > 1e-3 + 0.05 * abs(va): flag = '  <-' if 'slide' in k or 'pivot' in k or 'float' in k else '  *'
    print("%-52s %10.3f %10.3f%s" % (k, va, vb, flag))
