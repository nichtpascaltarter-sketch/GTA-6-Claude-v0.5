import re, sys, math
# turnzone.py <iev err>...: sunk/lifted frames (past 3 cm) on the landing turn - the landing (or winders) and the first two
# treads of the next flight - against the rest of the stairs, per scenario
sys.path.insert(0, '/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco')
import importlib.util
spec = importlib.util.spec_from_file_location('iev', '/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco/iev.py')
for fn in sys.argv[1:]:
    tot = {}
    for line in open(fn):
        m = re.match(r'IEV (\S+) p(\d+) run (\d+) t([\d.]+) foot (\d) low\s*([-\d.]+) .*? at\s*([-\d.]+),\s*([-\d.]+)', line)
        if not m: continue
        name = m.group(1)
        if not any(k in name for k in ('switchback', 'lstair', 'winder')): continue
        x, y, low = float(m.group(7)), float(m.group(8)), float(m.group(6))
        g = {'switchback.': (0.28, 9), 'switchback_tight': (0.27, 9), 'lstair': (0.28, 8), 'winder': (0.28, 7)}
        tread, n = next(v for k, v in g.items() if k in name)
        top = 2.0 + n * tread
        if 'switchback' in name:
            zone = 'turn' if y >= top or (x >= 0 and y > top - 2 * tread) else 'flight'
        else:
            W = 0.9 if 'tight' in name else 1.0
            xs = 0.5 * W
            zone = 'turn' if (y >= top and x < xs + 2 * tread) else 'flight'
        d = tot.setdefault(name, {'turn': [0, 0], 'flight': [0, 0]})
        d[zone][0 if low < 0 else 1] += 1
    print('== ' + fn)
    for name in sorted(tot):
        d = tot[name]
        print('  %-26s turn zone: sunk %4d lifted %4d | rest: sunk %4d lifted %4d' % (name, d['turn'][0], d['turn'][1], d['flight'][0], d['flight'][1]))
