import re, sys
# lptot.py log...: totals over the locoplayer summary lines (planted frames, sunk 3 cm, lifted, deep) by group
pat = re.compile(r'locoplayer (\S+)\s+(\S+)\s+(\S+)\s+steps\s+(\d+) \| slide cm/step mean ([\d.]+) p90 ([\d.]+) max ([\d.]+) \| scuffs (\d+) \| planted foot-frames (\d+) \| sunk 1 cm ([\d.]+)%, 3 cm ([\d.]+)% \| lifted ([\d.]+)% \| deep (\d+) \| lowest sole (-?[\d.]+) mm')
for fn in sys.argv[1:]:
    G = {}
    for line in open(fn, errors='replace'):
        m = pat.search(line)
        if not m: continue
        st, way, gait = m.group(1), m.group(2), m.group(3)
        pf = int(m.group(9)); s3 = float(m.group(11)) * pf / 100; lf = float(m.group(12)) * pf / 100; dp = int(m.group(13)); n = int(m.group(4))
        for key in ['all player' if gait in ('walk', 'run', 'sprint') else 'chasers', 'player ' + gait if gait in ('walk', 'run', 'sprint') else gait, st + ' ' + ('player' if gait in ('walk','run','sprint') else gait)]:
            g = G.setdefault(key, [0, 0., 0., 0, 0]); g[0] += pf; g[1] += s3; g[2] += lf; g[3] += dp; g[4] += n
    print(fn.split('/')[-1])
    for k in sorted(G):
        pf, s3, lf, dp, n = G[k]
        print('  %-22s steps %4d frames %5d  sunk3 %5.1f%%  lifted %5.1f%%  deep %4d' % (k, n, pf, 100 * s3 / max(pf, 1), 100 * lf / max(pf, 1), dp))
