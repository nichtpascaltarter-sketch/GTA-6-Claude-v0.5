# AI agent: harness DEADLOCK lines split into those at / queued back from the overlapping road edges (SP/ts/overlaps.txt:
# roads.cpp geometry, not the AI - the deadlocked car, or the head of its chain of blockers, at an overlap node or within
# 150 m of an overlap) and the rest, by gate node - usage: ts_dead.py FILE...
import re, sys, collections, math
S = '/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad'
ovn = set(); ovp = []
for line in open(S + '/ts/overlaps.txt'):
    m = re.search(r'from \(([-\d]+), ([-\d]+)\)', line)
    if m: ovp.append((float(m.group(1)), float(m.group(2))))
    for a, b in re.findall(r'nodes (\d+)-(\d+)', line):
        ovn.add(int(a)); ovn.add(int(b))
def near(x, y): return any(math.hypot(x - px, y - py) < 150. for px, py in ovp)
for f in sys.argv[1:]:
    lines = open(f).read().split('\n')
    tot = o = 0; other = collections.Counter()
    for i, line in enumerate(lines):
        m = re.match(r'DEADLOCK t=[\d.]+ car \d+ at ([-\d.]+) ([-\d.]+) .*?gateNode (-?\d+)', line)
        if not m: continue
        tot += 1
        x, y, n = float(m.group(1)), float(m.group(2)), int(m.group(3))
        nodes = {n}
        for k in range(i + 1, min(i + 6, len(lines))):
            mm = re.match(r'\s+-> car \d+ .*?gateNode (-?\d+)', lines[k])
            if not mm: break
            nodes.add(int(mm.group(1)))
        if nodes & ovn or near(x, y): o += 1
        else: other[n] += 1
    print('%-22s deadlocks %3d  overlap-related %3d  other %3d: %s' % (f.split('/')[-1].replace('.txt', ''), tot, o, tot - o,
          ' '.join('n%d:%d' % (k, v) for k, v in other.most_common(8))))
