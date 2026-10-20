import sys, re, glob, subprocess, os
# evsum.py <tagA> <tagB>: compare two evalv.sh variants: better/worse counts per file group, interior sink frames (3 cm)
# by scenario (all 6 profile/rate files summed) at both paces
L = os.path.dirname(os.path.abspath(__file__))
a, b = sys.argv[1], sys.argv[2]
def load(f):
    d = {}
    for line in open(f):
        m = re.match(r'M (\S+)\s+([-\d.]+)', line)
        if m: d[m.group(1)] = float(m.group(2))
    return d
R = L + '/r9/' + (os.environ.get('EVP') or 'ev') + '_'
for grp in ['fs_p0_30', 'fs_p2_60', 's5_30', 's5_60']:
    out = subprocess.run(['python3', L + '/better.py', R + a + '_' + grp + '.txt', R + b + '_' + grp + '.txt'], capture_output=True, text=True).stdout
    print('%-9s %s' % (grp, out.strip().split('\n')[-1]))
# interior: sum over the 6 files of steps-weighted metrics
for pace in ['own', '0.5']:
    print('== interior pace %s: sunk3 %% (mean of 6) | slide max | float frames | lifted %%  (%s -> %s)' % (pace, a, b))
    rows = {}
    for t in (a, b):
        for f in sorted(glob.glob(R + t + '_sv_p*_' + pace + '.txt')):
            d = load(f)
            for k, v in d.items():
                sc, m = k.rsplit('.', 1)
                rows.setdefault(sc, {}).setdefault(t, {}).setdefault(m, []).append(v)
    for sc in sorted(rows):
        if 'planted_sunk_3cm_pct' not in rows[sc].get(a, {}): continue
        A, B = rows[sc][a], rows[sc][b]
        mean = lambda x: sum(x) / len(x)
        print('  %-26s sk3 %5.2f -> %5.2f | slmax %5.1f -> %5.1f | fl %4d -> %4d | lf %4.2f -> %4.2f' % (sc, mean(A['planted_sunk_3cm_pct']), mean(B['planted_sunk_3cm_pct']),
              max(A['slide_cm_step_max']), max(B['slide_cm_step_max']), sum(A['planted_float_frames']), sum(B['planted_float_frames']), mean(A['planted_lifted_pct']), mean(B['planted_lifted_pct'])))
