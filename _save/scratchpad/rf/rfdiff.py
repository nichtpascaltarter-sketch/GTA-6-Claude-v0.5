# Diff of two rfcheck outputs (base, fix) for the road-overlap fix: counts, edges by index, buildings as sets (with the
# distance of each change to the nearest overlap spot of the base's --overlaps list), bus stops, places, interiors.
import sys, re, math
base, fix, ovfile = sys.argv[1], sys.argv[2], sys.argv[3]
spots = []
for l in open(ovfile):
    m = re.match(r'overlap\s+(\d+) m from \((-?\d+), (-?\d+)\)', l)
    if m: spots.append((float(m.group(2)), float(m.group(3))))
def near(x, y):
    return min(math.hypot(x - a, y - b) for a, b in spots) if spots else 1e9
def load(p):
    d = {'N': [], 'V': [], 'E': [], 'B': [], 'S': [], 'Q': [], 'I': [], 'P': []}
    for l in open(p):
        k = l[:1]
        if k in d and len(l) > 1 and l[1] == ' ': d[k].append(l.rstrip('\n'))
    return d
A, B = load(base), load(fix)
print('base:', A['N'][0] if A['N'] else '?')
print('fix: ', B['N'][0] if B['N'] else '?')
# edges by content (the index left out: dropped slots close up), and the edges whose seed moved to another index
def strip(l): return l.split(' ', 2)[2]
from collections import Counter
xa, xb = Counter(strip(l) for l in A['E']), Counter(strip(l) for l in B['E'])
er, ea_ = list((xa - xb).elements()), list((xb - xa).elements())
print('edges: %d base, %d fix; %d only in base, %d only in fix' % (len(A['E']), len(B['E']), len(er), len(ea_)))
for l in sorted(er): print('  -', l[:150])
for l in sorted(ea_): print('  +', l[:150])
def ekey(l):
    f = strip(l).split('|'); return f[0] + '|' + f[2]
ia = {ekey(l): int(l.split()[1]) for l in A['E']}
moved = sum(1 for l in B['E'] if ekey(l) in ia and ia[ekey(l)] != int(l.split()[1]))
print('edges at another index (same content): %d' % moved)
# buildings as multisets
from collections import Counter
ca, cb = Counter(A['B']), Counter(B['B'])
rem = list((ca - cb).elements()); add = list((cb - ca).elements())
def pos(l):
    f = l.split(); return float(f[1]), float(f[2])
far_r = [l for l in rem if near(*pos(l)) > 150]; far_a = [l for l in add if near(*pos(l)) > 150]
print('buildings: %d removed, %d added; beyond 150 m of a spot: %d removed, %d added' % (len(rem), len(add), len(far_r), len(far_a)))
for l in far_r[:30]: print('  far -', l, 'd %.0f' % near(*pos(l)))
for l in far_a[:30]: print('  far +', l, 'd %.0f' % near(*pos(l)))
# per spot: buildings changed within 150 m
for (x, y) in spots:
    r = sum(1 for l in rem if math.hypot(pos(l)[0] - x, pos(l)[1] - y) <= 150)
    a = sum(1 for l in add if math.hypot(pos(l)[0] - x, pos(l)[1] - y) <= 150)
    if r or a: print('  spot (%.0f, %.0f): %d removed, %d added within 150 m' % (x, y, r, a))
for key, name in (('S', 'bus stops'), ('Q', 'site places'), ('I', 'interiors'), ('P', 'story places'), ('V', 'nodes')):
    la, lb = A[key], B[key]
    if key == 'V':
        d = [i for i in range(min(len(la), len(lb))) if la[i] != lb[i]]
        print('%s: %d base, %d fix, %d changed by index' % (name, len(la), len(lb), len(d)))
        for i in d[:20]: print('  -', la[i]); print('  +', lb[i])
        continue
    sa, sb = set(la), set(lb)
    print('%s: %d base, %d fix, %d only in base, %d only in fix' % (name, len(la), len(lb), len(sa - sb), len(sb - sa)))
    for l in sorted(sa - sb)[:40]: print('  -', l[:170])
    for l in sorted(sb - sa)[:40]: print('  +', l[:170])
