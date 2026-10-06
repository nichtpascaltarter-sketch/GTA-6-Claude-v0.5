import re, sys
# planted frames of a label with a sole point 1..3 cm under its ground (and lifted ones), with the plan events around
lines = open(sys.argv[1]).read().split('\n')
tl = re.compile(r't([\d.]+) L(\d+) yaw')
pat = re.compile(r'(\d) (\d)(\d)(\d) pl(\d) st\s*([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+)')
lab = sys.argv[2]
for ln in lines:
    if ln.startswith('PLANT') or ln.startswith('SCAN'):
        print('   ', ln[:200]); continue
    m = tl.search(ln)
    if not m or m.group(2) != lab: continue
    for f in pat.findall(ln):
        h, b, t = float(f[6]), float(f[7]), float(f[8])
        if int(f[4]) and min(h, b, t) < -1.0:
            print('t %s foot %s h %.1f b %.1f t %.1f' % (m.group(1), f[0], h, b, t))
