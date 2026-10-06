import re, sys
# usage: gfxstats.py log... : averages of the --gfxstats lines per log (frames after the first two lines)
pat = re.compile(r'gfx \(frame (\d+)\): draws (\d+), dispatches (\d+), indirect (\d+) \| CB writes (\d+) \(([\d.]+) KB\), dynamic ([\d.]+) KB \| root CBVs (\d+), root constants (\d+) \| tables (\d+) \((\d+) descriptors\) \| pipelines (\d+)')
names = ['draws','dispatches','indirect','cbWrites','cbKB','dynKB','rootCBVs','rootConsts','tables','descriptors','pipelines']
for path in sys.argv[1:]:
    rows = []
    for line in open(path, errors='replace'):
        m = pat.search(line)
        if m: rows.append([float(x) for x in m.groups()[1:]])
    rows = [r for r in rows if r[0] >= 50]   # skip loading-screen frames
    if not rows: print(path, 'no stats'); continue
    avg = [sum(r[i] for r in rows) / len(rows) for i in range(len(names))]
    print('%-40s n=%3d  ' % (path.split('/')[-1], len(rows)) + '  '.join('%s %.0f' % (n, a) if n not in ('cbKB','dynKB') else '%s %.1f' % (n, a) for n, a in zip(names, avg)))
