import re, sys
# plants.py <trace err>: each plant (pl 0 -> 1) of the traced run with the swing's last fit (SL, shift, scan done, ball probe)
prev = {0: '0', 1: '0'}
pat = re.compile(r'(\d) (\d)(\d)(\d) pl(\d) st\s*([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+).*?SL\s*([-\d.]+) fz\s*([-\d.]+).*?sh\s*([-\d.]+) sd(\d) bp(\d) hw([-\d.]+),([-\d.]+).*?\| pc([\d.]+) cy\s*([-\d.]+) k(\d) pz\s*([-\d.]+) pe\s*([-\d.]+) hd(\d)')
tp = re.compile(r't([\d.]+) L(\d+) yaw\s*([-\d.]+) tr\s*([-\d.]+) v([\d.]+) md\s*([-\d.]+),\s*([-\d.]+) ph([\d.]+) mw([\d.]+)')
last = {}
held = {0: False, 1: False}
for line in open(sys.argv[1]):
    m = tp.search(line)
    if not m: continue
    t = float(m.group(1)); ph = float(m.group(8)); mw = float(m.group(9))
    for f in pat.finditer(line):
        s = int(f.group(1)); pl = f.group(5); hd = f.group(22)
        if hd == '1' and not held[s]: print('t%6.3f foot %d HOLD at %6.3f,%6.3f h/b/t %5.1f/%5.1f/%5.1f ph%.3f | swing SL %s sh %s' % (t, s, float(f.group(15)), float(f.group(16)), float(f.group(7)), float(f.group(8)), float(f.group(9)), (ph - 0.5 * s) % 1.0, last.get(s, ('', '', ''))[1], last.get(s, ('', '', ''))[2]))
        held[s] = hd == '1'
        if pl == '0' and hd == '0': last[s] = (t, f.group(10), f.group(12), f.group(13))
        if pl == '1' and prev[s] == '0':
            fph = (ph - 0.5 * s) % 1.0
            print('t%6.3f foot %d plant heel %6.3f,%6.3f fz %5.2f h/b/t %5.1f/%5.1f/%5.1f ph%.3f mw%.2f | last swing SL %s sh %s sd %s' % (t, s, float(f.group(15)), float(f.group(16)), float(f.group(11)), float(f.group(7)), float(f.group(8)), float(f.group(9)), fph, mw, *last.get(s, ('', '', '', ''))[1:]))
        prev[s] = pl
