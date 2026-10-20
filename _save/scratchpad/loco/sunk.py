import re, sys
# planted frames with a sole point more than 3 cm under its ground, by label
pat = re.compile(r'(\d) (\d)(\d)(\d) pl(\d) st\s*([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+)')
lab = sys.argv[2]
rows = []
for ln in open(sys.argv[1]):
    m = re.search(r't([\d.]+) L(\d+) yaw\s*([-\d.]+) tr\s*([-\d.]+) v\s*([-\d.]+) md\s*([-\d.]+),\s*([-\d.]+) ph([-\d.]+)', ln)
    if not m or m.group(2) != lab: continue
    for f in pat.findall(ln):
        h, b, t = float(f[6]), float(f[7]), float(f[8])
        if int(f[4]) and min(h, b, t) < -3.0:
            rows.append('t %s foot %s h %.1f b %.1f t %.1f ph %s' % (m.group(1), f[0], h, b, t, m.group(8)))
print(len(rows), 'sunk frames')
print('\n'.join(rows[:int(sys.argv[3]) if len(sys.argv) > 3 else 30]))
