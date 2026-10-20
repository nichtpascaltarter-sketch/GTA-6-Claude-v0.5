import sys, re
# parse an LM_TRACE stderr dump: report frames where a planted foot is more than 15 mm off the ground
pat = re.compile(r'(\d) (\d)(\d)(\d) pl(\d) st\s*([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+) sl\s*([-\d.]+) fy\s*([-\d.]+)')
tl = re.compile(r't([\d.]+) L(\d+) yaw\s*([-\d.]+) tr\s*([-\d.]+) v\s*([-\d.]+) md\s*([-\d.]+),\s*([-\d.]+) ph([-\d.]+) mw([-\d.]+)')
n = 0
for line in open(sys.argv[1]):
    feet = pat.findall(line)
    m = tl.search(line)
    if len(feet) < 2 or not m: continue
    t, lab = float(m.group(1)), int(m.group(2))
    for f in feet:
        s, pl = int(f[0]), int(f[4])
        h, b, tt = float(f[6]), float(f[7]), float(f[8])
        if pl and min(h, b, tt) > 1.5 and lab:
            n += 1
            if n <= int(sys.argv[2] if len(sys.argv) > 2 else 40):
                print("t %.3f foot %d h %.1f b %.1f t %.1f st %s | yaw %s tr %s v %s md %s,%s ph %s mw %s" % (t, s, h, b, tt, f[5], m.group(3), m.group(4), m.group(5), m.group(6), m.group(7), m.group(8), m.group(9)))
print("float frames", n)
