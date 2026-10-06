import re, sys
# tview.py trace t0 t1 foot : per frame, one foot's plan fields with the body's speed / yaw / phase
fp = re.compile(r'(\d) (\d)(\d)(\d) pl(\d) st\s*([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+) sl\s*([-\d.]+) fy\s*([-\d.]+) SL\s*([-\d.]+) fz\s*([-\d.]+) ga\s*([-\d.]+) g\s*([-\d.]+),\s*([-\d.]+) sh\s*([-\d.]+) sd(\d) bp(\d)')
tp = re.compile(r't([\d.]+) L(\d+) yaw\s*([-\d.]+) tr\s*([-\d.]+) v\s*([-\d.]+) md\s*([-\d.]+),\s*([-\d.]+) ph([-\d.]+) mw([-\d.]+)')
fn, t0, t1, ft = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), int(sys.argv[4])
for ln in open(fn):
    m = tp.search(ln)
    if not m: continue
    t = float(m.group(1))
    if t < t0 or t > t1: continue
    for f in fp.findall(ln):
        if int(f[0]) != ft: continue
        print('t%.3f L%s v%s ph%s mw%s yaw%6s tr%5s | pl%s h%6s b%6s t%6s SL%6s fz%6s ga%6s gH%6s gT%6s sh%5s sd%s bp%s' % (t, m.group(2), m.group(5), m.group(8), m.group(9), m.group(3), m.group(4), f[4], f[6], f[7], f[8], f[11], f[12], f[13], f[14], f[15], f[16], f[17], f[18]))
