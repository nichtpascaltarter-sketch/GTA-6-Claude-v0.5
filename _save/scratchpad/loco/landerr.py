import re, sys, math
# landing error: where the heel was planned to land (scan start + direction * (kScanBack - L/2 + shift)) vs where it planted
fp = re.compile(r'(\d) (\d)(\d)(\d) pl(\d) st\s*([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+) sl\s*([-\d.]+) fy\s*([-\d.]+) SL\s*([-\d.]+) fz\s*([-\d.]+) ga\s*([-\d.]+) g\s*([-\d.]+),\s*([-\d.]+) sh\s*([-\d.]+) sd(\d) bp(\d) hw([-\d.]+),([-\d.]+)')
tp = re.compile(r't([\d.]+) L(\d+) yaw\s*([-\d.]+) tr\s*([-\d.]+) v\s*([-\d.]+) md\s*([-\d.]+),\s*([-\d.]+) ph([-\d.]+)')
sp = re.compile(r'SCAN foot (\d) from ([-\d.]+) ([-\d.]+) dir ([-\d.]+) ([-\d.]+) L ([-\d.]+)')
rows = []
for fn in sys.argv[1:]:
    pend = None; plan = [None, None]; prevPl = [0, 0]
    for ln in open(fn):
        m = sp.match(ln)
        if m:
            pend = dict(s=int(m.group(1)), x=float(m.group(2)), y=float(m.group(3)), dx=float(m.group(4)), dy=float(m.group(5)), L=float(m.group(6)))
            continue
        m = tp.search(ln)
        if not m: continue
        t, lab, v = float(m.group(1)), int(m.group(2)), float(m.group(5))
        ff = fp.findall(ln)
        if pend:
            # the plan of the scan answered this update
            f = ff[pend['s']]
            pend['sh'] = float(f[16]); pend['v'] = v; pend['t'] = t; pend['bp'] = int(f[18])
            plan[pend['s']] = pend; pend = None
        for f in ff:
            s = int(f[0]); pl = int(f[4])
            if pl and not prevPl[s] and plan[s]:
                P = plan[s]
                u = 0.25 - 0.5 * P['L'] + P['sh']
                px, py = P['x'] + P['dx'] * u, P['y'] + P['dy'] * u
                hx, hy = float(f[19]), float(f[20])
                e = (hx - px) * P['dx'] + (hy - py) * P['dy']
                low = min(float(f[6]), float(f[7]), float(f[8]))
                rows.append((fn[-5], t, lab, s, e * 100, P['v'], v, t - P['t'], P['sh'], P['bp']))
                plan[s] = None
            prevPl[s] = pl
print('trace  t     dir foot  err_cm  vScan vPlant  dt   shift bp')
for r in rows:
    print('%s %6.2f %s  %d  %6.1f   %.2f  %.2f  %.2f %5.2f %d' % (r[0], r[1], 'up' if r[2] == 5 else ('dn' if r[2] == 6 else '--'), r[3], r[4], r[5], r[6], r[7], r[8], r[9]))
