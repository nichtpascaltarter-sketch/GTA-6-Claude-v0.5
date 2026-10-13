import re, sys
# per stance on the stair (labels 5 up / 6 down): plan at the plant vs the ground the foot found, speed at scan vs plant
fp = re.compile(r'(\d) (\d)(\d)(\d) pl(\d) st\s*([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+) sl\s*([-\d.]+) fy\s*([-\d.]+) SL\s*([-\d.]+) fz\s*([-\d.]+) ga\s*([-\d.]+) g\s*([-\d.]+),\s*([-\d.]+) sh\s*([-\d.]+) sd(\d) bp(\d)')
tp = re.compile(r't([\d.]+) L(\d+) yaw\s*([-\d.]+) tr\s*([-\d.]+) v\s*([-\d.]+) md\s*([-\d.]+),\s*([-\d.]+) ph([-\d.]+)')
out = []
for fn in sys.argv[1:]:
    st = [None, None]; scanV = [None, None]; prevSd = [0, 0]; prevPl = [0, 0]
    for ln in open(fn):
        m = tp.search(ln)
        if not m: continue
        t, lab, v = float(m.group(1)), int(m.group(2)), float(m.group(5))
        for f in fp.findall(ln):
            s = int(f[0]); pl = int(f[4]); h, b, tt = float(f[6]), float(f[7]), float(f[8])
            SL, fz, gh, gt, sh, sd, bp = float(f[11]), float(f[12]), float(f[14]), float(f[15]), float(f[16]), int(f[17]), int(f[18])
            if sd and not prevSd[s]: scanV[s] = v
            if pl and not prevPl[s]:
                st[s] = dict(fn=fn, t=t, lab=lab, s=s, SL=SL, gh=gh, gt=gt, sh=sh, bp=bp, vScan=scanV[s], vPl=v, low=min(h, b, tt), lowAt=0, n=0, hi=max(h,b,tt) if min(h,b,tt) > 0 else 0)
            if pl and st[s]:
                L = min(h, b, tt)
                st[s]['n'] += 1
                if L < st[s]['low']: st[s]['low'] = L; st[s]['lowAt'] = st[s]['n'] - 1
                st[s]['hi'] = max(st[s]['hi'], L)
            if not pl and prevPl[s] and st[s]:
                if st[s]['lab'] in (5, 6): out.append(st[s])
                st[s] = None; scanV[s] = None
            prevSd[s] = sd; prevPl[s] = pl
bad = [o for o in out if o['low'] < -3.0 or o['hi'] > 1.5]
print(len(out), 'stances,', len([o for o in out if o['low'] < -3]), 'with a sunk frame,', len([o for o in out if o['hi'] > 1.5]), 'with a lifted frame')
for o in bad[:int(60)]:
    vs = '%.2f' % o['vScan'] if o['vScan'] is not None else ' -- '
    print('%s t%6.2f %s ft%d SL%6.2f gHeel%6.2f gToe%6.2f sh%5.2f bp%d vScan %s vPl %.2f low %6.1f (frame %d of %d) hi %5.1f' % (o['fn'][-5], o['t'], 'up' if o['lab'] == 5 else 'dn', o['s'], o['SL'], o['gh'], o['gt'], o['sh'], o['bp'], vs, o['vPl'], o['low'], o['lowAt'], o['n'], o['hi']))
