import re, sys, statistics as st
# kitcmp.py <tagA> <tagB> [fam ...]: kit sweeps r9/kit<fam>_<tag>_{iv5,pace}.txt compared - per family and way: mean /
# median sunk 3 cm %, mean lifted %, worst slide max, and how many scenarios got better / worse by > 0.5 points sunk3
R = '/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco/r9/'
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
a, b = sys.argv[1], sys.argv[2]
fams = sys.argv[3:] or ['spiral', 'winder', 'curved']
for fam in fams:
    for p in ('iv5', 'pace'):
        A, B = load(R + 'kit%s_%s_%s.txt' % (fam, a, p)), load(R + 'kit%s_%s_%s.txt' % (fam, b, p))
        for way in ('up', 'down'):
            keys = sorted(k[:-len('.planted_sunk_3cm_pct')] for k in A if k.endswith('.' + way + '.planted_sunk_3cm_pct') and k in B)
            if not keys: continue
            sa = [A[k + '.planted_sunk_3cm_pct'] for k in keys]; sb = [B[k + '.planted_sunk_3cm_pct'] for k in keys]
            la = [A.get(k + '.planted_lifted_pct', 0) for k in keys]; lb = [B.get(k + '.planted_lifted_pct', 0) for k in keys]
            ma = max(A.get(k + '.slide_cm_step_max', 0) for k in keys); mb = max(B.get(k + '.slide_cm_step_max', 0) for k in keys)
            better = sum(1 for x, y in zip(sa, sb) if y < x - 0.5); worse = sum(1 for x, y in zip(sa, sb) if y > x + 0.5)
            print('%-7s %-4s %-4s n%3d | sk3 mean %5.2f -> %5.2f median %5.2f -> %5.2f | lifted %4.2f -> %4.2f | slide max %5.1f -> %5.1f | better %d worse %d' % (
                fam, p, way, len(keys), st.mean(sa), st.mean(sb), st.median(sa), st.median(sb), st.mean(la), st.mean(lb), ma, mb, better, worse))
