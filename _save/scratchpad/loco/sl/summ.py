import sys, re
# one line per config: the stair sinking metrics that matter
keys = ['stairs17.walk.up.planted_sunk_3cm_pct', 'stairs17.walk.down.planted_sunk_3cm_pct',
        'stairs17.crowd.up.planted_sunk_3cm_pct', 'stairs17.crowd.down.planted_sunk_3cm_pct',
        'stairs17.blocked.up.planted_sunk_3cm_pct', 'stairs17.blocked.down.planted_sunk_3cm_pct',
        'stairs17.crowd.up.land_gait_err_med_cm', 'stairs17.crowd.down.land_gait_err_med_cm',
        'stairs17.walk.up.land_gait_err_med_cm', 'stairs17.walk.down.land_gait_err_med_cm']
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
hdr = ['w.up', 'w.dn', 'cr.up', 'cr.dn', 'bl.up', 'bl.dn', 'crErrU', 'crErrD', 'wErrU', 'wErrD', 'blS.up', 'blS.dn']
print('%-14s' % 'config', ' '.join('%7s' % h for h in hdr))
for tag in sys.argv[2:]:
    d = load('g_%s_%s.txt' % (tag, sys.argv[1])); e = load('g_%s_%ss.txt' % (tag, sys.argv[1]))
    vals = [d.get(k, float('nan')) for k in keys] + [e.get('stairs17.blocked.up.planted_sunk_3cm_pct', float('nan')), e.get('stairs17.blocked.down.planted_sunk_3cm_pct', float('nan'))]
    print('%-14s' % tag, ' '.join('%7.2f' % v for v in vals))
