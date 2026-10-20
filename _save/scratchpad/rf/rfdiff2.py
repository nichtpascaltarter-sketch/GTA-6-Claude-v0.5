# Diff of two rfcheck outputs (base, fix) for the road-overlap fix, round 4 on (no edge may change index):
#   edges by index (which slots changed, which were appended), nodes by index,
#   buildings as multisets, each change with its distance to the nearest overlap spot (the base's --overlaps list) and
#   to the nearest changed road (chords of the changed edges, base and fix sides, sampled every 5 m),
#   bus stops, site places, interiors and story places as sets.
# Usage: python3 rfdiff2.py census_base.txt census_fix.txt ov_base.txt
import sys, re, math
from collections import Counter
base, fix, ovfile = sys.argv[1], sys.argv[2], sys.argv[3]
spots = []
for l in open(ovfile):
    m = re.match(r'overlap\s+(\d+) m from \((-?\d+), (-?\d+)\)', l)
    if m: spots.append((float(m.group(2)), float(m.group(3))))
def load(p):
    d = {'N': [], 'V': [], 'E': [], 'B': [], 'S': [], 'Q': [], 'I': [], 'P': [], 'R': [], 'T': []}
    for l in open(p):
        k = l[:1]
        if k in d and len(l) > 1 and l[1] == ' ': d[k].append(l.rstrip('\n'))
    return d
A, B = load(base), load(fix)
print('base:', A['N'][0] if A['N'] else '?')
print('fix: ', B['N'][0] if B['N'] else '?')
def strip(l): return l.split(' ', 2)[2]
def ends(l):
    f = strip(l).split('|')[2].split(' -> ')
    a = tuple(map(float, f[0].split())); b = tuple(map(float, f[1].split()))
    return a, b
na, nb = len(A['E']), len(B['E'])
changed = [i for i in range(min(na, nb)) if strip(A['E'][i]) != strip(B['E'][i])]
print('edges: %d base, %d fix; %d indices changed, %d appended, %d dropped off the end' % (na, nb, len(changed), max(0, nb - na), max(0, na - nb)))
for i in changed:
    print('  slot %d' % i)
    print('    -', strip(A['E'][i])[:160])
    print('    +', strip(B['E'][i])[:160])
for i in range(na, nb): print('  new %d' % i, strip(B['E'][i])[:160])
for i in range(nb, na): print('  gone off the end %d' % i, strip(A['E'][i])[:160])
# the changed roads, sampled along their chords
pts = []
def chord(l):
    a, b = ends(l)
    n = max(1, int(math.hypot(b[0] - a[0], b[1] - a[1]) / 5.0))
    for k in range(n + 1): pts.append((a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n))
for i in changed: chord(A['E'][i]); chord(B['E'][i])
for i in range(na, nb): chord(B['E'][i])
cell = {}
for p in pts: cell.setdefault((int(p[0] // 50), int(p[1] // 50)), []).append(p)
def dRoad(x, y):
    best = 1e9
    cx, cy = int(x // 50), int(y // 50)
    for r in range(0, 8):
        for gx in range(cx - r, cx + r + 1):
            for gy in range(cy - r, cy + r + 1):
                if max(abs(gx - cx), abs(gy - cy)) != r: continue
                for p in cell.get((gx, gy), []): best = min(best, math.hypot(x - p[0], y - p[1]))
        if best < r * 50: break
    return best
def dSpot(x, y): return min(math.hypot(x - a, y - b) for a, b in spots) if spots else 1e9
# nodes by index
va, vb = A['V'], B['V']
dv = [i for i in range(min(len(va), len(vb))) if va[i] != vb[i]]
print('nodes: %d base, %d fix, %d changed by index' % (len(va), len(vb), len(dv)))
for i in dv[:80]: print('  -', va[i]); print('  +', vb[i])
# buildings as multisets
ca, cb = Counter(A['B']), Counter(B['B'])
rem = sorted((ca - cb).elements()); add = sorted((cb - ca).elements())
def pos(l):
    f = l.split(); return float(f[1]), float(f[2])
print('buildings: %d base, %d fix; %d removed, %d added' % (len(A['B']), len(B['B']), len(rem), len(add)))
farS = [('-', l) for l in rem if dSpot(*pos(l)) > 150] + [('+', l) for l in add if dSpot(*pos(l)) > 150]
farR = [('-', l) for l in rem if dRoad(*pos(l)) > 60] + [('+', l) for l in add if dRoad(*pos(l)) > 60]
print('  beyond 150 m of an overlap spot: %d; beyond 60 m of a changed road: %d' % (len(farS), len(farR)))
for s, l in farS[:40]: print('   spot-far %s %s dSpot %.0f dRoad %.0f' % (s, l, dSpot(*pos(l)), dRoad(*pos(l))))
for s, l in farR[:40]: print('   road-far %s %s dSpot %.0f dRoad %.0f' % (s, l, dSpot(*pos(l)), dRoad(*pos(l))))
for (x, y) in spots:
    r = sum(1 for l in rem if math.hypot(pos(l)[0] - x, pos(l)[1] - y) <= 150)
    a = sum(1 for l in add if math.hypot(pos(l)[0] - x, pos(l)[1] - y) <= 150)
    if r or a: print('  spot (%.0f, %.0f): %d removed, %d added within 150 m' % (x, y, r, a))
for key, name in (('S', 'bus stops'), ('Q', 'site places'), ('I', 'interiors'), ('P', 'story places')):
    la, lb = A[key], B[key]
    sa, sb = Counter(la), Counter(lb)
    print('%s: %d base, %d fix, %d only in base, %d only in fix' % (name, len(la), len(lb), sum((sa - sb).values()), sum((sb - sa).values())))
    for l in sorted((sa - sb).elements())[:60]: print('  -', l[:200])
    for l in sorted((sb - sa).elements())[:60]: print('  +', l[:200])
# bus routes: length, legs and the stops in service order (name, position, route distance)
def routes(d):
    r = {}
    for l in d['R']:
        f = l.split()
        r[f[1]] = {'len': float(f[3]), 'legs': f[5:], 'stops': []}
    for l in d['T']:
        f = l.split(' ', 3)
        if f[1] in r: r[f[1]]['stops'].append(f[3])
    return r
ra, rb = routes(A), routes(B)
for k in sorted(set(ra) | set(rb), key=lambda x: int(x) if x.isdigit() else 0):
    a, b = ra.get(k), rb.get(k)
    if not a or not b: print('route %s only in %s' % (k, 'base' if a else 'fix')); continue
    same = a['stops'] == b['stops']
    print('route %s: %.0f m -> %.0f m, %d legs -> %d legs, %d stops -> %d stops%s' % (k, a['len'], b['len'], len(a['legs']), len(b['legs']), len(a['stops']), len(b['stops']), ', stops identical' if same else ''))
    if same and a['legs'] == b['legs']: continue
    la, lb = a['legs'], b['legs']
    if la != lb:
        sa, sb = set(la), set(lb)
        print('   legs only in base:', ' '.join(x for x in la if x not in sb))
        print('   legs only in fix: ', ' '.join(x for x in lb if x not in sa))
    if not same:
        for i in range(max(len(a['stops']), len(b['stops']))):
            x = a['stops'][i] if i < len(a['stops']) else '-'
            y = b['stops'][i] if i < len(b['stops']) else '-'
            print('   %2d %s %-60s | %s' % (i, '=' if x == y else '*', x[:60], y[:80]))
