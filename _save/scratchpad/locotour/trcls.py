import re, sys
# trcls.py log: the v3l traces classified by their last frame (the planted foot first gone bad): over a wall's footprint
# (outer: |x| > 1.08 m; inner: |x| < 0.1 m with a < 2.3 m - beside the flights), or on the treads (deep or over a drop)
hdr = re.compile(r'locoplayer trace (\d+): switchback (\S+) (\S+) foot (\d), seg (\d+)')
fr = re.compile(r'locoplayer trace (\d+) f-\s*(\d+): pl(\d) hd(\d) st\s*(-?[\d.]+) age([\d.]+) land\s*(-?[\d.]+) scan(\d) \| heel\s*(-?[\d.]+)\s+(-?[\d.]+)\s+([-+][\d.]+) ball\s*(-?[\d.]+)\s+(-?[\d.]+)\s+([-+][\d.]+) toe\s*(-?[\d.]+)\s+(-?[\d.]+)\s+([-+][\d.]+) \| root\s*(-?[\d.]+)\s+(-?[\d.]+) z([\d.]+) yaw\s*([-+]?\d+) v([\d.]+) \| probe\s*(-?[\d.]+)\s+(-?[\d.]+) ([-+][\d.]+)')
H = {}; last = {}; frames = {}
for line in open(sys.argv[1], errors='replace'):
    m = hdr.search(line)
    if m: H[int(m.group(1))] = m.groups()[1:]; continue
    m = fr.search(line)
    if m:
        g = m.groups(); n = int(g[0]); frames.setdefault(n, []).append(g)
        if int(g[1]) == 0: last[n] = g
cls = {}
for n in sorted(H):
    g = last.get(n)
    if not g: continue
    pts = [(float(g[8]), float(g[9]), float(g[10])), (float(g[11]), float(g[12]), float(g[13])), (float(g[14]), float(g[15]), float(g[16]))]
    outer = any(abs(x) > 1.08 and h > 40 for x, a, h in pts)
    inner = any(abs(x) < 0.1 and a < 2.3 and h > 40 for x, a, h in pts)
    k = 'outer wall' if outer else 'inner wall' if inner else 'treads'
    cls.setdefault(k, []).append(n)
    print('%2d %-6s %-6s foot %s seg %2s | %-10s | heel %5.2f %5.2f %+6.1f ball %5.2f %5.2f %+6.1f toe %5.2f %5.2f %+6.1f | root %5.2f %5.2f z%s v%s | probe %s land %s' % (
        n, H[n][0], H[n][1], H[n][2], H[n][3], k, *pts[0], *pts[1], *pts[2], float(g[17]), float(g[18]), g[19], g[21], g[24], g[6]))
print({k: len(v) for k, v in cls.items()})
