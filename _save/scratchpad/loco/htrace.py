import re, sys
# compact view of a locometer LM_TRACE file: htrace.py file [t0 t1]
fn = sys.argv[1]
t0 = float(sys.argv[2]) if len(sys.argv) > 2 else -1e9
t1 = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9
only = sys.argv[4] if len(sys.argv) > 4 else None   # (one foot: '0' or '1')
num = r'(-?[\d.]+)'
fr = re.compile(r'(\d) (\d)(\d)(\d) pl(\d) st\s*' + num + r' h\s*' + num + r' b\s*' + num + r' t\s*' + num + r' sl\s*' + num +
                r' fy\s*' + num + r'.*? dr\s*' + num + r' pr\s*' + num + r' \| pc' + num + r' cy\s*' + num + r' k(\d) pz\s*' + num + r' pe\s*' + num + r' hd(\d)[^|]*\|')
tl = re.compile(r't' + num + r' L(\d+) yaw\s*' + num + r' tr\s*' + num + r' v' + num + r' md\s*' + num + r',\s*' + num + r' ph' + num + r' mw' + num + r'.*sk' + num)
for line in open(fn):
    m = tl.search(line)
    if not m: continue
    t = float(m.group(1))
    if t < t0 or t > t1: continue
    out = 't%6.3f yaw%6.1f tr%5.2f v%4.2f mw%4.2f ph%5.3f sk%4.1f |' % (t, float(m.group(3)), float(m.group(4)), float(m.group(5)), float(m.group(9)), float(m.group(8)), float(m.group(10)) * 100)
    for f in fr.finditer(line):
        s = f.group(1); dn = f.group(2) + f.group(3) + f.group(4); pl = f.group(5); st = float(f.group(6))
        if only is not None and s != only: continue
        h, b, tt, sl, fy, dr, pr = [float(f.group(i)) for i in (7, 8, 9, 10, 11, 12, 13)]
        pc, cy, k, pz, pe, hd = float(f.group(14)), float(f.group(15)), f.group(16), float(f.group(17)), float(f.group(18)), f.group(19)
        state = 'P' if pl == '1' else ('H' if hd == '1' else ('S' if st >= 0 else '-'))
        out += ' %s%s %s h%5.1f b%5.1f t%5.1f sl%5.1f fy%6.1f pc%5.3f cy%5.2f pz%5.1f pe%5.1f dr%4.1f pr%5.1f |' % (s, state, dn, h, b, tt, sl if sl > -50 else -1, fy, pc, cy, pz, pe, dr * 100, pr * 100)
    print(out)
