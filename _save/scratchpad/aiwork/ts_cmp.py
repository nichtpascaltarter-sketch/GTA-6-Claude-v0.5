# AI agent: summarise harness runs (collisions, red, deadlocks, ...) - usage: ts_cmp.py FILE...
import re, sys
keys = [('coll', r'^collisions: (\d+)'), ('hard', r'hard \(>3000 Ns\): (\d+)'), ('static', r'^static impacts \(walls/props >2500 Ns\): (\d+)'),
        ('red', r'^red-light violations: (\d+)'), ('stopsign', r'stop-sign violations: (\d+)'), ('stuck', r'^stuck recoveries: (\d+)'),
        ('3pt', r'three-point turns: (\d+)'), ('reloc', r'relocalizations: (\d+)'), ('dead', r'deadlocks \(>60 s waits\): (\d+)'),
        ('resp', r'respawns: (\d+)'), ('pedhit', r'^pedestrians hit: (\d+)')]
print('%-28s' % 'run' + ''.join('%8s' % k for k, _ in keys))
for f in sys.argv[1:]:
    txt = open(f).read()
    vals = []
    for k, rx in keys:
        m = re.search(rx, txt, re.M)
        vals.append(m.group(1) if m else '-')
    print('%-28s' % f.split('/')[-1].replace('.txt', '') + ''.join('%8s' % v for v in vals))
