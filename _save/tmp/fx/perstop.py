import re, sys
# perstop.py logA logB : per tour stop, mean synctimer pass times (shadows, gbuffer, lighting, total) and gfx stats
# (draws, indirect, dynKB, descriptors) over the blocks logged before each stop's shot, for two runs side by side.
def parse(path):
    stops = []   # (name, timer blocks, gfx samples)
    blocks, gfx = [], []
    cur = None
    for line in open(path, errors='replace'):
        m = re.search(r'autoplay tour shot (\d+) (\S+)', line)
        if m:
            stops.append((m.group(1) + ' ' + m.group(2), blocks, gfx))
            blocks, gfx = [], []
            continue
        if 'Pass timings' in line:
            cur = {}
            blocks.append(cur)
            continue
        m = re.search(r'gfx \(frame \d+\): draws (\d+), dispatches (\d+), indirect (\d+) .*dynamic ([\d.]+) KB .*tables \d+ \((\d+) descriptors\)', line)
        if m:
            gfx.append(tuple(float(x) for x in m.groups()))
        if cur is not None:
            m = re.match(r'^(\S[\w+ ]*?)\s+([\d.]+) ms\s*$', line.rstrip('\n'))
            if m: cur[m.group(1).strip()] = float(m.group(2))
            elif line.strip() == '' or line.startswith('['): cur = None
    return stops
A, B = parse(sys.argv[1]), parse(sys.argv[2])
keys = ['shadows', 'gbuffer', 'lighting', 'total']
print('%-26s %s' % ('stop', '   '.join('%-17s' % k for k in keys + ['draws', 'indirect', 'dynKB'])))
for (na, ba, ga), (nb, bb, gb) in zip(A, B):
    ba = [b for b in ba if b.get('gbuffer', 0) > 20]
    bb = [b for b in bb if b.get('gbuffer', 0) > 20]
    def mean(bl, k): return sum(b.get(k, 0) for b in bl) / max(len(bl), 1)
    def gmean(g, i): return sum(x[i] for x in g) / max(len(g), 1)
    cols = ['%6.0f / %6.0f' % (mean(ba, k), mean(bb, k)) for k in keys]
    cols += ['%6.0f / %6.0f' % (gmean(ga, i), gmean(gb, i)) for i in (0, 2, 3)]
    print('%-26s %s' % (na, '   '.join('%-17s' % c for c in cols)))
