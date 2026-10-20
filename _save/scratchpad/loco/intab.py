import sys, re
# intab.py prefix tags... : interior table per profile/rate file suffix
pre = sys.argv[1]; tags = sys.argv[2].split(','); suffixes = sys.argv[3].split(',')
keys = ['switchback.up_1.0', 'switchback.down_1.0', 'switchback_tight.up_1.0', 'switchback_tight.down_1.0', 'lstair.up_1.0', 'lstair.down_1.0',
        'winder.up_0.9', 'winder.down_0.9', 'spiral.up_0.7', 'spiral.down_0.7', 'spiral_tight.up_0.6', 'spiral_tight.down_0.6',
        'curved.up_1.0', 'curved.down_1.0', 'room.doorway_1.1', 'room.turnback_1.1']
def load(fn):
    d = {}
    for line in open(fn):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
for suf in suffixes:
    data = {t: load('%s_%s_%s.txt' % (pre, t, suf)) for t in tags}
    print('== %s: %s' % (suf, ' | '.join(tags)))
    for k in keys:
        row = '%-26s' % k
        for t in tags:
            d = data[t]
            g = lambda m: d.get(k + '.' + m, float('nan'))
            row += ' | sl %5.2f/%5.1f fl %3d/%3.0f sk %4.1f/%4.1f dp %3d lf %4.1f' % (g('slide_cm_step_mean'), g('slide_cm_step_max'), g('planted_float_frames'), g('planted_float_max_mm'),
                                                                                g('planted_sunk_pct'), g('planted_sunk_3cm_pct'), g('planted_deep_frames'), g('planted_lifted_pct'))
        print(row)
