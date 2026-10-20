import re, sys
# perstop3.py logA logB : per tour stop, mean pass times (blocks logged before each stop's shot, gbuffer > 20 ms) for
# runs A and B: gbuffer, post, lighting, total; B/A normalised by the lighting pass (machine-load swings).
def parse(path):
    stops, order, blocks, cur = {}, [], [], None
    for line in open(path, errors='replace'):
        m = re.search(r'autoplay tour shot (\d+) (\S+)', line)
        if m:
            name = m.group(1) + ' ' + m.group(2); stops[name] = blocks; order.append(name); blocks = []; continue
        if 'Pass timings' in line:
            cur = {}; blocks.append(cur); continue
        if cur is not None:
            m = re.match(r'^(\S[\w+ ]*?)\s+([\d.]+) ms\s*$', line.rstrip('\n'))
            if m: cur[m.group(1).strip()] = float(m.group(2))
            elif line.strip() == '' or line.startswith('['): cur = None
    return stops, order
(A, oa), (B, ob) = parse(sys.argv[1]), parse(sys.argv[2])
keys = ['gbuffer', 'post', 'lighting', 'total']
def mean(bl, k): return sum(b.get(k, 0) for b in bl) / max(len(bl), 1)
print('%-26s %-24s %-24s %-14s %-24s' % ('stop', 'gbuffer A/B (norm)', 'post A/B (norm)', 'lighting A/B', 'total A/B (norm)'))
tot = {k: [0.0, 0.0] for k in keys}
for name in oa:
    if name not in B: continue
    ba = [b for b in A[name] if b.get('gbuffer', 0) > 20]; bb = [b for b in B[name] if b.get('gbuffer', 0) > 20]
    if not ba or not bb: continue
    la, lb = mean(ba, 'lighting'), mean(bb, 'lighting'); norm = la / lb if lb > 0 else 1.0
    cols = []
    for k in keys:
        a, b = mean(ba, k), mean(bb, k)
        if k == 'lighting': cols.append('%5.0f/%5.0f' % (a, b))
        else: cols.append('%6.0f/%6.0f (%+4.0f%%)' % (a, b, (b * norm / a - 1) * 100 if a > 0 else 0))
        tot[k][0] += a; tot[k][1] += b
    print('%-26s %-24s %-24s %-14s %-24s   (%d/%d blocks)' % ((name,) + tuple(cols) + (len(ba), len(bb))))
n = tot['lighting'][0] / tot['lighting'][1] if tot['lighting'][1] else 1.0
print('sum: ' + '  '.join('%s %.0f/%.0f (%+.1f%% norm)' % (k, tot[k][0], tot[k][1], (tot[k][1] * n / tot[k][0] - 1) * 100 if tot[k][0] else 0) for k in keys))
