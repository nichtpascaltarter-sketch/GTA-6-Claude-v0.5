import re, sys
# perstop2.py logA logB : per tour stop (matched by name), mean synctimer pass times over the blocks logged before each
# stop's shot for runs A and B, the ratio B/A and the ratio normalised by the lighting pass (machine-load swings), plus
# B's decor plant counts (--gfxstats "props" lines) and both runs' draw counts.
def parse(path):
    stops = {}
    order = []
    blocks, gfx, props = [], [], []
    cur = None
    for line in open(path, errors='replace'):
        m = re.search(r'autoplay tour shot (\d+) (\S+)', line)
        if m:
            name = m.group(1) + ' ' + m.group(2)
            stops[name] = (blocks, gfx, props)
            order.append(name)
            blocks, gfx, props = [], [], []
            continue
        if 'Pass timings' in line:
            cur = {}
            blocks.append(cur)
            continue
        m = re.search(r'gfx \(frame \d+\): draws (\d+), dispatches (\d+), indirect (\d+)', line)
        if m: gfx.append(tuple(float(x) for x in m.groups()))
        m = re.search(r'props \(frame \d+\): camera (\d+) instances, (\d+) triangles; decor plants (\d+) placed \(room for \d+\), (\d+) drawn', line)
        if m: props.append(tuple(float(x) for x in m.groups()))
        if cur is not None:
            m = re.match(r'^(\S[\w+ ]*?)\s+([\d.]+) ms\s*$', line.rstrip('\n'))
            if m: cur[m.group(1).strip()] = float(m.group(2))
            elif line.strip() == '' or line.startswith('['): cur = None
    return stops, order
(A, oa), (B, ob) = parse(sys.argv[1]), parse(sys.argv[2])
keys = ['shadows', 'gbuffer', 'lighting', 'total']
def mean(bl, k): return sum(b.get(k, 0) for b in bl) / max(len(bl), 1)
print('%-24s %-22s %-22s %-14s %-22s %-10s %-16s' % ('stop', 'shadows A/B (norm)', 'gbuffer A/B (norm)', 'lighting A/B', 'total A/B (norm)', 'draws A/B', 'decor placed/drawn'))
tot = {k: [0.0, 0.0] for k in keys}
for name in oa:
    if name not in B: continue
    ba = [b for b in A[name][0] if b.get('gbuffer', 0) > 20]
    bb = [b for b in B[name][0] if b.get('gbuffer', 0) > 20]
    if not ba or not bb: continue
    la, lb = mean(ba, 'lighting'), mean(bb, 'lighting')
    norm = la / lb if lb > 0 else 1.0
    cols = []
    for k in ['shadows', 'gbuffer']:
        a, b = mean(ba, k), mean(bb, k)
        cols.append('%5.0f/%5.0f (%+4.0f%%)' % (a, b, (b * norm / a - 1) * 100 if a > 0 else 0))
    cols.append('%5.0f/%5.0f' % (la, lb))
    a, b = mean(ba, 'total'), mean(bb, 'total')
    cols.append('%5.0f/%5.0f (%+4.0f%%)' % (a, b, (b * norm / a - 1) * 100 if a > 0 else 0))
    ga, gb = A[name][1], B[name][1]
    cols.append('%4.0f/%4.0f' % (sum(x[0] for x in ga) / max(len(ga), 1), sum(x[0] for x in gb) / max(len(gb), 1)))
    pb = B[name][2]
    cols.append('%4.0f/%4.0f' % (max([x[2] for x in pb] or [0]), max([x[3] for x in pb] or [0])))
    for k in keys:
        tot[k][0] += mean(ba, k); tot[k][1] += mean(bb, k)
    print('%-24s %-22s %-22s %-14s %-22s %-10s %-16s' % ((name,) + tuple(cols)))
n = tot['lighting'][0] / tot['lighting'][1] if tot['lighting'][1] else 1.0
print('sum: ' + '  '.join('%s %.0f/%.0f (%+.1f%% norm)' % (k, tot[k][0], tot[k][1], (tot[k][1] * n / tot[k][0] - 1) * 100 if tot[k][0] else 0) for k in keys))
