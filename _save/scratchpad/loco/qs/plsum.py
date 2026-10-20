import re, sys
# plsum.py <tagA> <tagB> ...: the player-speed harness files pl_<tag>_{s,sb}_<v>_<hz>.txt side by side: sunk3 / lifted / slide max
L = '/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco/r9/'
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
def agg(d, suffix, m, f=lambda v: sum(v)/len(v)):
    v = [x for k, x in d.items() if k.endswith(suffix + '.' + m)]
    return f(v) if v else float('nan')
tags = sys.argv[1:]
for kind, tag in (('straight', 's'), ('switchback', 'sb')):
    for v in ('1.55', '4.1', '6.9'):
        for hz in ('60', '30'):
            row = '%-10s %4s %s ' % (kind, v, hz)
            for b in tags:
                d = load(L + 'pl_%s_%s_%s_%s.txt' % (b, tag, v, hz))
                if kind == 'straight':
                    up = (agg(d, '.up', 'planted_sunk_3cm_pct'), agg(d, '.up', 'planted_lifted_pct'), agg(d, '.up', 'slide_cm_step_max', max))
                    dn = (agg(d, '.down', 'planted_sunk_3cm_pct'), agg(d, '.down', 'planted_lifted_pct'), agg(d, '.down', 'slide_cm_step_max', max))
                else:
                    up = tuple(d.get('switchback.up_1.0.' + m, float('nan')) for m in ('planted_sunk_3cm_pct', 'planted_lifted_pct', 'slide_cm_step_max'))
                    dn = tuple(d.get('switchback.down_1.0.' + m, float('nan')) for m in ('planted_sunk_3cm_pct', 'planted_lifted_pct', 'slide_cm_step_max'))
                row += '| %s up %4.1f/%4.1f/%5.1f dn %4.1f/%4.1f/%5.1f ' % ((b.replace('lm_', ''),) + up + dn)
            print(row)
