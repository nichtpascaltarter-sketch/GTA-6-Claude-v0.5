import re, sys
# tourcmp.py log_a log_b: the locotour summaries side by side, and each log's big slides (> 20 cm) with ped, foot, size
def load(fn):
    d = {'steps': {}, 'big': [], 'feet': '', 'bystance': ''}
    for line in open(fn, errors='replace'):
        m = re.search(r'locotour steps (.+?)\s+n\s+(\d+) \| slide cm/step mean ([\d.]+) p90 ([\d.]+) max ([\d.]+) \| (?:pivot deg/step mean ([\d.]+) \| )?scuffs (\d+)', line)
        if m:
            d['steps'][m.group(1).strip()] = (int(m.group(2)), float(m.group(3)), float(m.group(4)), float(m.group(5)), m.group(6), int(m.group(7)))
            continue
        m = re.search(r'locotour big slide: ped (\d+) foot (\d) ([\d.]+) cm over (\d+) frames.*?pivot (\d+) deg, speed ([\d.]+) m/s.*?turning ([\d.]+) rad/s \| stance (\d+).*?moveW ([\d.]+).*?activity (\d+)', line)
        if m:
            d['big'].append((int(m.group(1)), int(m.group(2)), float(m.group(3)), int(m.group(4)), int(m.group(5)), float(m.group(6)), float(m.group(7)), int(m.group(8)), float(m.group(9)), int(m.group(10))))
            continue
        if 'locotour feet:' in line: d['feet'] = line.split('locotour feet:')[1].strip()
        if 'slides over 20 cm by stance' in line: d['bystance'] = line.split('locotour ')[1].strip()
    return d
a, b = load(sys.argv[1]), load(sys.argv[2])
print('%-22s | %-40s | %-40s' % ('steps', sys.argv[1].split('/')[-1], sys.argv[2].split('/')[-1]))
for k in a['steps']:
    va, vb = a['steps'][k], b['steps'].get(k)
    fa = 'n %4d mean %5.2f p90 %5.2f max %6.2f sc %2d' % (va[0], va[1], va[2], va[3], va[5])
    fb = 'n %4d mean %5.2f p90 %5.2f max %6.2f sc %2d' % (vb[0], vb[1], vb[2], vb[3], vb[5]) if vb else '-'
    print('%-22s | %-40s | %-40s' % (k, fa, fb))
print('feet A:', a['feet']); print('feet B:', b['feet'])
print('A:', a['bystance']); print('B:', b['bystance'])
for nm, d in (('A', a), ('B', b)):
    print('== big slides %s: %d' % (nm, len(d['big'])))
    for x in sorted(d['big'], key=lambda x: -x[2]):
        print('   ped %3d foot %d %6.1f cm / %3d fr, pivot %3d deg, %.2f m/s, turn %.2f, stance %d, moveW %.2f, act %d' % x)
