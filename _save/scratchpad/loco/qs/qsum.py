import sys, re, collections
# quick steps on the flights (LM_QSLOG lines): per step its direction (forward / back along the walk), the drag (frames
# with a sole point within 1.5 cm of its ground or below it while the foot moves, summed movement), the deepest sole
# point, where in the step (u) the drag happens. Usage: qsum.py files... [-v]
rx = re.compile(r'QS run (\d+) (\w+) t([\d.]+) foot (\d) u ([-\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+) g ([-\d.]+) ([-\d.]+) ([-\d.]+) fz ([-\d.]+) gIn ([-\d.]+) yh ([-\d.]+) yt ([-\d.]+) yFrom ([-\d.]+) yTo ([-\d.]+) lift ([-\d.]+) pz ([-\d.]+)')
verbose = '-v' in sys.argv
steps = collections.OrderedDict()
for fn in [a for a in sys.argv[1:] if a != '-v']:
    last = {}
    for ln in open(fn):
        m = rx.match(ln)
        if not m: continue
        run, d, t, s, u = m.group(1), m.group(2), float(m.group(3)), m.group(4), float(m.group(5))
        k0 = (fn, run, s)
        # a new step when u restarts or time jumps
        if k0 not in last or u < last[k0][0] - 1e-3 or t - last[k0][1] > 0.1:
            key = (fn, run, s, t)
            last[k0] = (u, t, key)
        key = last[k0][2]
        last[k0] = (u, t, key)
        steps.setdefault(key, []).append(dict(d=d, t=t, u=u, h=float(m.group(6)), b=float(m.group(7)), tt=float(m.group(8)),
                                           yh=float(m.group(14)), yt=float(m.group(15)), yF=float(m.group(16)), yT=float(m.group(17)),
                                           fz=float(m.group(12)), gIn=float(m.group(13)), g=(float(m.group(9)), float(m.group(10)), float(m.group(11)))))
agg = collections.defaultdict(lambda: [0, 0.0, 0, 0.0, 0])
rows = []
for key, fr in steps.items():
    d = fr[0]['d']
    dirn = 'fwd' if fr[0]['yT'] - fr[0]['yF'] > 0.02 else ('back' if fr[0]['yT'] - fr[0]['yF'] < -0.02 else 'turn')
    drag = 0.0; dragU = []; low = 1e9
    for i in range(1, len(fr)):
        a, b = fr[i - 1], fr[i]
        lo = min(b['h'], b['b'], b['tt'])
        low = min(low, lo)
        mv = abs(b['yh'] - a['yh'])
        if lo < 1.5 and mv > 0.01:
            drag += mv * 100; dragU.append(b['u'])
    c = (d, dirn)
    agg[c][0] += 1; agg[c][1] += drag; agg[c][2] += 1 if drag > 5 else 0; agg[c][3] = min(agg[c][3], low); agg[c][4] += 1 if low < -3 else 0
    rows.append((drag, key, d, dirn, low, dragU, fr))
print('%-4s %-5s %6s %9s %9s %9s %8s' % ('dir', 'move', 'steps', 'drag_cm', 'drag>5cm', 'lowest', 'sunk>3'))
for c in sorted(agg):
    n, dsum, nd, low, ns = agg[c]
    print('%-4s %-5s %6d %9.1f %9d %9.1f %8d' % (c[0], c[1], n, dsum, nd, low, ns))
rows.sort(key=lambda r: -r[0])
if verbose:
    for r in rows[:6]:
        print('--- drag %.1f cm %s %s %s lowest %.1f at u %s' % (r[0], r[2], r[3], r[1][1:], r[4], ' '.join('%.2f' % u for u in r[5])))
        for f in r[6]:
            print('   u %.2f h%6.1f b%6.1f t%6.1f g %.2f %.2f %.2f fz %.3f gIn %.3f yh %.3f yt %.3f from %.3f to %.3f' % (f['u'], f['h'], f['b'], f['tt'], f['g'][0], f['g'][1], f['g'][2], f['fz'], f['gIn'], f['yh'], f['yt'], f['yF'], f['yT']))
