# Sign-off report for the road-overlap fix: the changed spots (changed edge slots grouped by place), the buildings that
# changed at each (removed / added, as multisets of the census B lines), the ones beyond 150 m of every changed spot, and
# bus 12's stops base vs fix.
# Usage: python3 rfreport.py census_base.txt census_fix.txt > report.txt
import sys, math
from collections import Counter
base, fix = sys.argv[1], sys.argv[2]
def load(p):
    d = {'N': [], 'E': [], 'B': [], 'R': [], 'T': [], 'P': []}
    for l in open(p):
        k = l[:1]
        if k in d and len(l) > 1 and l[1] == ' ': d[k].append(l.rstrip('\n'))
    return d
A, B = load(base), load(fix)
def strip(l): return l.split(' ', 2)[2]
def name(l): return strip(l).split('|')[0]
def ends(l):
    f = strip(l).split('|')[2].split(' -> ')
    return tuple(map(float, f[0].split())), tuple(map(float, f[1].split()))
def chord(l, step=5.0):
    a, b = ends(l)
    n = max(1, int(math.hypot(b[0] - a[0], b[1] - a[1]) / step))
    return [(a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n) for k in range(n + 1)]
na, nb = len(A['E']), len(B['E'])
changed = [i for i in range(min(na, nb)) if strip(A['E'][i]) != strip(B['E'][i])]
items = []   # (slot, points, names)
for i in changed:
    pa, pb = chord(A['E'][i]), chord(B['E'][i])
    # a slot refilled far away (a new piece of another road) is two places: keep its sides apart
    ca = (sum(p[0] for p in pa) / len(pa), sum(p[1] for p in pa) / len(pa))
    cb = (sum(p[0] for p in pb) / len(pb), sum(p[1] for p in pb) / len(pb))
    if math.hypot(ca[0] - cb[0], ca[1] - cb[1]) > 300:
        items.append((i, pa, {name(A['E'][i])}))
        items.append((i, pb, {name(B['E'][i])}))
    else:
        items.append((i, pa + pb, {name(A['E'][i]), name(B['E'][i])}))
# group into spots: items within 200 m of each other (single linkage on their points)
def dmin(p, q):
    best = 1e18
    for a in p[::3]:
        for b in q[::3]:
            d = (a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2
            if d < best: best = d
    return math.sqrt(best)
parent = list(range(len(items)))
def find(x):
    while parent[x] != x:
        parent[x] = parent[parent[x]]; x = parent[x]
    return x
for x in range(len(items)):
    for y in range(x + 1, len(items)):
        if find(x) != find(y) and dmin(items[x][1], items[y][1]) < 200: parent[find(x)] = find(y)
groups = {}
for x in range(len(items)): groups.setdefault(find(x), []).append(x)
spots = []
for g in groups.values():
    pts = [p for x in g for p in items[x][1]]
    names = set().union(*[items[x][2] for x in g])
    slots = sorted(set(items[x][0] for x in g))
    c = (sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts))
    spots.append({'c': c, 'pts': pts, 'names': names, 'slots': slots})
spots.sort(key=lambda s: (-s['c'][1], s['c'][0]))
def dSpot(s, x, y): return min(math.hypot(x - p[0], y - p[1]) for p in s['pts'])
ca, cb = Counter(A['B']), Counter(B['B'])
rem = sorted((ca - cb).elements()); add = sorted((cb - ca).elements())
def pos(l):
    f = l.split(); return float(f[1]), float(f[2])
print('buildings: %d base, %d fix (%+d); %d removed, %d added (a changed building counts once each way)' % (len(A['B']), len(B['B']), len(B['B']) - len(A['B']), len(rem), len(add)))
print('changed edge slots: %d of %d (every other edge keeps its index and seed); spots: %d' % (len(changed), na, len(spots)))
assigned = {}
for tag, lst in (('-', rem), ('+', add)):
    for l in lst:
        x, y = pos(l)
        ds = [(dSpot(s, x, y), k) for k, s in enumerate(spots)]
        d, k = min(ds) if ds else (1e9, -1)
        assigned.setdefault(k, []).append((d, tag, l))
for k, s in enumerate(spots):
    lst = assigned.get(k, [])
    near = [t for t in lst if t[0] <= 150]
    print('\nspot %d at (%.0f, %.0f): %s; slots %s' % (k + 1, s['c'][0], s['c'][1], ', '.join(sorted(s['names'])), ' '.join(map(str, s['slots']))))
    print('  buildings within 150 m: %d removed, %d added' % (sum(1 for t in near if t[1] == '-'), sum(1 for t in near if t[1] == '+')))
    for d, tag, l in sorted(near, key=lambda t: (pos(t[2])[1], pos(t[2])[0], t[1])):
        f = l.split()
        print('    %s (%.1f, %.1f) %.0fx%.0f m style %s h %s seed %s  [%.0f m from the changed roads]' % (tag, float(f[1]), float(f[2]), 2 * float(f[3]), 2 * float(f[4]), f[9], f[11], f[13], d))
farl = [t for k in assigned for t in assigned[k] if t[0] > 150]
print('\nbuildings changed beyond 150 m of every changed spot: %d' % len(farl))
for d, tag, l in sorted(farl, key=lambda t: t[0]):
    f = l.split()
    print('    %s (%.1f, %.1f) style %s h %s seed %s  [%.0f m from the nearest changed road]' % (tag, float(f[1]), float(f[2]), f[9], f[11], f[13], d))
# bus 12
def stops(d, num):
    return [l.split(' ', 3)[3] for l in d['T'] if l.split(' ', 3)[1] == num]
def rlen(d, num):
    for l in d['R']:
        f = l.split()
        if f[1] == num: return float(f[3])
    return 0
for num in ('12', '9', '3', '7'):
    sa, sb = stops(A, num), stops(B, num)
    if sa == sb:
        print('\nbus %s: %.0f m -> %.0f m, stops identical (%d)' % (num, rlen(A, num), rlen(B, num), len(sa)))
        continue
    print('\nbus %s: %.0f m -> %.0f m, %d stops -> %d stops' % (num, rlen(A, num), rlen(B, num), len(sa), len(sb)))
    for i in range(max(len(sa), len(sb))):
        x = sa[i] if i < len(sa) else '-'
        y = sb[i] if i < len(sb) else '-'
        print('   %2d %s %-62s | %s' % (i + 1, '=' if x == y else '*', x[:62], y[:80]))
pa, pb = Counter(A['P']), Counter(B['P'])
print('\nstory places: %d base, %d fix, %d changed' % (len(A['P']), len(B['P']), sum((pa - pb).values())))
