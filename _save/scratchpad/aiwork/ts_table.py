# AI agent: set 19 harness table - per scenario the sums over its seeds for base and the set 19 build(s): collisions
# (hard), red runs, deadlocks (overlap-related / other, as ts_dead.py), waits over 30 s behind a parked car, stuck
# recoveries, static impacts. Usage: ts_table.py  (reads SP/ts)
import re, os, math, collections
S = '/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad'
T = S + '/ts/'
ovn = set(); ovp = []
for line in open(T + 'overlaps.txt'):
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
    return r
rows = [
    ('I-38 ramps, 3 seeds', ['q_ramp_base_%d', 'q_ramp_s19b_%d', 'q_ramp_s19x_%d', 'q_ramp_s19y_%d'], [1, 2, 3]),
    ('Midtown, 3 seeds', ['q_mid_base_%d', 'q_mid_s19b_%d', 'q_mid_s19x_%d', 'q_mid_s19y_%d'], [1, 2, 3]),
    ('Sol Beach, 2 seeds', ['q_beach_base_%d', None, 'q_beach_s19x_%d', 'q_beach_s19y_%d'], [1, 2]),
    ('downtown', ['r_dt1_base', 'r_dt1_s19b', 'r_dt1_s19x', 'r_dt1_s19y'], [None]),
    ('Calle Luna', ['r_luna_base', 'r_luna_s19b', 'r_luna_s19x', 'r_luna_s19y'], [None]),
    ('highway', ['r_hwy_base', 'r_hwy_s19b', 'r_hwy_s19x', 'r_hwy_s19y'], [None]),
    ('parked: ramps', ['p_ramp_base', None, 'p_ramp_s19x', 'p_ramp_s19y'], [None]),
    ('parked: Midtown 2 seeds', ['p_mid_base%s', None, 'p_mid_s19x%s', 'p_mid_s19y%s'], ['', '_2']),
    ('parked: downtown', ['p_dt1_base', None, 'p_dt1_s19x', 'p_dt1_s19y'], [None]),
]
keys = ['coll', 'collov', 'hard', 'red', 'dead', 'deadov', 'park', 'stuck', 'static']
print('%-26s %-5s ' % ('scenario', 'build') + ' '.join('%6s' % k for k in keys))
for name, pats, seeds in rows:
    for tag, pat in zip(['base', 'v2', 'v6', 'v7'], pats):
        if not pat: continue
        tot = collections.Counter(); n = 0; missing = False
        for s in seeds:
            f = T + ((pat % s) if s is not None else pat) + '.txt'
            if not os.path.exists(f): missing = True; continue
            r = stats(f)
            if r is None: missing = True; continue
            tot.update(r); n += 1
        if n == 0: continue
        print('%-26s %-5s ' % (name, tag) + ' '.join('%6d' % tot[k] for k in keys) + ('  (incomplete)' if missing else ''))
# --signoff: base against the set's final build (v7) only, as the sign-off's table
import sys
if '--signoff' in sys.argv:
    print()
    print('%-24s %-5s %11s %5s %4s %11s %7s %6s %6s' % ('scenario', 'build', 'coll (ovl)', 'hard', 'red', 'wait>60 (ovl)', 'parked', 'stuck', 'walls'))
    for name, pats, seeds in rows:
        for tag, pat in (('base', pats[0]), ('s19', pats[3])):
            tot = collections.Counter(); n = 0
            for s in seeds:
                f = T + ((pat % s) if s is not None else pat) + '.txt'
                if not os.path.exists(f): continue
                r = stats(f)
                if r is None: continue
                tot.update(r); n += 1
            if n < len(seeds): print('%-24s %-5s (incomplete)' % (name, tag)); continue
            park = ('%d' % tot['park']) if name.startswith('parked') else '-'
            print('%-24s %-5s %6d (%2d) %5d %4d %8d (%2d) %7s %6d %6d' % (name, tag, tot['coll'], tot['collov'], tot['hard'], tot['red'], tot['dead'], tot['deadov'], park, tot['stuck'], tot['static']))
