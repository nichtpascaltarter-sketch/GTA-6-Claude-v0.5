import re, sys, math
# usage: trace.py log uid [t0 t1]
log, uid = sys.argv[1], sys.argv[2]
t0 = float(sys.argv[3]) if len(sys.argv) > 3 else -1e9
t1 = float(sys.argv[4]) if len(sys.argv) > 4 else 1e9
num = r'(-?[\d.]+)'
hdr = re.compile(r'locotour trace (\d+) t' + num + r' pos ' + num + r' ' + num + r' v ' + num + r' \(f' + num + r' s' + num + r'\) yaw ' + num +
                 r' tr\s*' + num + r' in ' + num + r' md ' + num + r',' + num + r' mw ' + num + r' ph ' + num + r' stance (-?\d+) act (-?\d+)')
foot = re.compile(r'\| (\d) pl(\d) hd(\d) st\s*' + num + r' pc\s*' + num + r' cy\s*' + num + r' pv\s*' + num + r' age\s*' + num +
                  r' h\s*' + num + r' b\s*' + num + r' t\s*' + num + r' x\s*' + num + r' y\s*' + num)
prev = {}
for line in open(log, errors='replace'):
    if 'locotour trace ' + uid + ' ' not in line: continue
    m = hdr.search(line)
    if not m: continue
    t = float(m.group(2))
    if t < t0 or t > t1: continue
    g = m.groups()
    out = 't%7.3f v%5.2f f%5.2f s%5.2f yaw%7.1f tr%6.2f mw%4.2f ph%4.2f |' % (t, float(g[4]), float(g[5]), float(g[6]), float(g[7]), float(g[8]), float(g[12]), float(g[13]))
    for f in foot.finditer(line):
        s = int(f.group(1)); pl, hd = f.group(2), f.group(3)
        st, pc, cy, pv, age, h, b, tt, x, y = [float(v) for v in f.groups()[3:]]
        dx = 0.0
        if s in prev: dx = math.hypot(x - prev[s][0], y - prev[s][1]) * 100
        prev[s] = (x, y)
        state = 'P' if pl == '1' else ('H' if hd == '1' else ('S' if st >= 0 else '-'))
        out += ' %d%s st%5.2f pc%5.3f cy%5.2f pv%4.2f h%5.1f b%5.1f t%5.1f mv%5.1f |' % (s, state, st, pc, cy, pv, h, b, tt, dx)
    print(out)
