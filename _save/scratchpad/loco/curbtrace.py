import sys, re
# frames of an LM_TRACE dump where a foot point is more than 50 mm below the ground under it (label 14: the curb up)
pat = re.compile(r'(\d) (\d)(\d)(\d) pl(\d) st\s*([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+)')
tl = re.compile(r't([\d.]+) L(\d+) yaw\s*([-\d.]+) tr\s*([-\d.]+) v\s*([-\d.]+) md\s*([-\d.]+),\s*([-\d.]+) ph([-\d.]+)')
for line in open(sys.argv[1]):
    m = tl.search(line)
    if not m or m.group(2) != sys.argv[2]: continue
    for f in pat.findall(line):
        h, b, t = float(f[6]), float(f[7]), float(f[8])
        if min(h, b, t) < -5.0:
            print("t %s foot %s pl%s st %s | heel %.1f ball %.1f toe %.1f cm | ph %s" % (m.group(1), f[0], f[4], f[5], h, b, t, m.group(8)))
