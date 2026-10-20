# AI agent: set 20 harness table on HEAD 7b9146d's world (tree61) - base (61b = HEAD's AI) against set 20 (61f): per
# scenario the sums over its seeds: collisions (at the overlap sites), hard, red runs, waits over 60 s (at the
# overlaps), waits over 30 s behind a parked car, stuck recoveries, wall/prop hits; then highway lane keeping (time-
# weighted over the ramp and highway runs), the meeting test, the dead-end circles. Usage: ts_table20.py [F]
import re, os, math, collections, sys
S = '/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad'
T = S + '/ts/'
F = sys.argv[1] if len(sys.argv) > 1 else '61f'
ovn = set(); ovp = []
if os.path.exists(T + 'overlaps61.txt'):
    for line in open(T + 'overlaps61.txt'):
        m = re.search(r'from \(([-\d]+), ([-\d]+)\)', line)
        if m: ovp.append((float(m.group(1)), float(m.group(2))))
        for a, b in re.findall(r'nodes (\d+)-(\d+)', line):
            ovn.add(int(a)); ovn.add(int(b))
def near(x, y): return any(math.hypot(x - px, y - py) < 150. for px, py in ovp)
def stats(f):
    txt = open(f).read()
    if not re.search(r' rc=0', txt): return None
    g = lambda rx: int(re.search(rx, txt, re.M).group(1)) if re.search(rx, txt, re.M) else 0
    r = dict(coll=g(r'^collisions: (\d+)'), hard=g(r'hard \(>3000 Ns\): (\d+)'), static=g(r'^static impacts \(walls/props >2500 Ns\): (\d+)'),
             red=g(r'^red-light violations: (\d+)'), stuck=g(r'^stuck recoveries: (\d+)'), dead=g(r'deadlocks \(>60 s waits\): (\d+)'),
             park=g(r'waits over 30 s behind one: (\d+)'))
    lines = txt.split('\n'); ov = 0
    for i, line in enumerate(lines):
        m = re.match(r'DEADLOCK t=[\d.]+ car \d+ at ([-\d.]+) ([-\d.]+) .*?gateNode (-?\d+)', line)
        if not m: continue
        nodes = {int(m.group(3))}
        for k in range(i + 1, min(i + 6, len(lines))):
            mm = re.match(r'\s+-> car \d+ .*?gateNode (-?\d+)', lines[k])
            if not mm: break
            nodes.add(int(mm.group(1)))
        if nodes & ovn or near(float(m.group(1)), float(m.group(2))): ov += 1
    r['deadov'] = ov
    co = 0
    for m in re.finditer(r'^COLLISION t=[\d.]+ imp \d+ at ([-\d.]+) ([-\d.]+)', txt, re.M):
        if near(float(m.group(1)), float(m.group(2))): co += 1
    r['collov'] = co
    # highway lane keeping: [kind][state] -> (seconds, rms, over %, max, median s, left over %, max left)
    hk = {}
    for m in re.finditer(r'^highway lane keeping, (cars|long vehicles), (in lane|changing lanes) \(v>15 m/s, (\d+) s\): RMS ([\d.]+) m, over 0.5 m ([\d.]+)%, max ([\d.]+) m; median lane \((\d+) s\): over 0.3 m left ([\d.]+)%, max left ([\d.]+) m', txt, re.M):
        hk[(m.group(1), m.group(2))] = tuple(float(x) for x in m.groups()[2:])
    r['hk'] = hk
    return r
rows = [
    ('I-38 ramps, 3 seeds', 'q_ramp_%s_%d', [1, 2, 3]),
    ('Midtown, 3 seeds', 'q_mid_%s_%d', [1, 2, 3]),
    ('Sol Beach, 2 seeds', 'q_beach_%s_%d', [1, 2]),
    ('downtown', 'r_dt1_%s', [None]),
    ('Calle Luna', 'r_luna_%s', [None]),
    ('highway', 'r_hwy_%s', [None]),
    ('parked: ramps', 'p_ramp_%s', [None]),
    ('parked: Midtown 2 seeds', 'p_mid_%s%s', ['', '_2']),
    ('parked: downtown', 'p_dt1_%s', [None]),
    ('I-38 ramps, seeds 4-6', 'q_ramp_%s_%d', [4, 5, 6]),
]
def fname(pat, v, s):
    return T + ((pat % (v, s)) if s is not None else (pat % v)) + '.txt'
print('%-24s %-5s %11s %5s %4s %13s %7s %6s %6s' % ('scenario', 'build', 'coll (ovl)', 'hard', 'red', 'wait>60 (ovl)', 'parked', 'stuck', 'walls'))
tot = {'61b': collections.Counter(), F: collections.Counter()}
hkacc = {'61b': collections.defaultdict(lambda: [0., 0., 0., 0., 0., 0., 0.]), F: collections.defaultdict(lambda: [0., 0., 0., 0., 0., 0., 0.])}
for name, pat, seeds in rows:
    for tag, v in (('base', '61b'), ('s20', F)):
        t = collections.Counter(); n = 0
        for s in seeds:
            f = fname(pat, v, s)
            if not os.path.exists(f): continue
            r = stats(f)
            if r is None: continue
            t.update({k: x for k, x in r.items() if k != 'hk'}); n += 1
            if 'ramp' in pat or 'hwy' in pat:
                for key, (sec, rms, over, mx, msec, lover, lmax) in r['hk'].items():
                    a = hkacc[v][key]
                    a[0] += sec; a[1] += rms * rms * sec; a[2] += over * sec; a[3] = max(a[3], mx); a[4] += msec; a[5] += lover * msec; a[6] = max(a[6], lmax)
        if n < len(seeds): print('%-24s %-5s (incomplete: %d of %d)' % (name, tag, n, len(seeds))); continue
        if not name.endswith('4-6'): tot[v].update(t)
        park = ('%d' % t['park']) if name.startswith('parked') else '-'
        print('%-24s %-5s %6d (%2d) %5d %4d %8d (%2d) %7s %6d %6d' % (name, tag, t['coll'], t['collov'], t['hard'], t['red'], t['dead'], t['deadov'], park, t['stuck'], t['static']))
print()
for v in ('61b', F):
    t = tot[v]
    print('the 15 runs, %s: collisions %d (%d at overlaps), hard %d, red %d, waits>60 %d (%d at overlaps), parked waits %d, stuck %d, walls %d' % (
        'base' if v == '61b' else 's20', t['coll'], t['collov'], t['hard'], t['red'], t['dead'], t['deadov'], t['park'], t['stuck'], t['static']))
print()
print('highway lane keeping above 15 m/s (I-38 ramps seeds 1-6 + highway run; time-weighted):')
for key in [('cars', 'in lane'), ('cars', 'changing lanes'), ('long vehicles', 'in lane'), ('long vehicles', 'changing lanes')]:
    for tag, v in (('base', '61b'), ('s20', F)):
        a = hkacc[v].get(key)
        if not a or a[0] <= 0: continue
        print('  %-14s %-15s %-4s %5.0f s: RMS %.3f m, over 0.5 m %5.2f%%, max %.2f m | median lane %4.0f s: over 0.3 m left %5.2f%%, max left %.2f m' % (
            key[0], key[1], tag, a[0], math.sqrt(a[1] / a[0]), a[2] / a[0], a[3], a[4], a[5] / a[4] if a[4] else 0., a[6]))
print()
for pair in ('Guar', 'Boul'):
    for tag, v in (('base', '61b'), ('s20', F)):
        f = T + 'mt_2907_%s_%s.txt' % (v, pair)
        if os.path.exists(f):
            m = [l for l in open(f) if l.startswith('meet test:') and 'meeting points' in l]
            print('meet %s %-4s %s' % (pair, tag, m[0].strip() if m else '(incomplete)'))
for c in ('4870', 'city'):
    for tag, v in (('base', '61b'), ('s20', F)):
        f = T + 'ut_%s_%s.txt' % (c, v)
        if os.path.exists(f):
            txt = open(f).read()
            m = re.search(r'^dead-end test: .*$', txt, re.M)
            st = re.findall(r'stuck (\d+), strayed ([\d.]+) m', txt)
            stuck = sum(int(a) for a, b in st)
            print('uturn %-4s %-4s %s; stuck sum %d' % (c, tag, m.group(0) if m else '(incomplete)', stuck))
