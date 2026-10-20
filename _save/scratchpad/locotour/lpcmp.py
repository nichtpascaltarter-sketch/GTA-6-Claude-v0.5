import re, sys
# lpcmp.py log_a log_b: the locoplayer summary lines of two runs side by side (per stair / way / gait: steps, slide
# mean / p90 / max cm, planted frames sunk 3 cm %, lifted %, deep frames, lowest sole mm), the spiral walking line
pat = re.compile(r'locoplayer (\S+)\s+(\S+)\s+(\S+)\s+steps\s+(\d+) \| slide cm/step mean ([\d.]+) p90 ([\d.]+) max ([\d.]+) \| scuffs (\d+) \| planted foot-frames (\d+) \| sunk 1 cm ([\d.]+)%, 3 cm ([\d.]+)% \| lifted ([\d.]+)% \| deep (\d+) \| lowest sole (-?[\d.]+) mm')
def load(fn):
    d, extra = {}, []
    for line in open(fn, errors='replace'):
        m = pat.search(line)
        if m:
            g = m.groups()
            d[(g[0], g[1], g[2])] = dict(steps=int(g[3]), mean=float(g[4]), p90=float(g[5]), mx=float(g[6]), sc=int(g[7]), pf=int(g[8]), s1=float(g[9]), s3=float(g[10]), lf=float(g[11]), deep=int(g[12]), low=float(g[13]))
        elif 'locoplayer spiral walking line' in line or 'locoplayer: ' in line:
            extra.append(line.split('] ', 1)[-1].strip())
    return d, extra
(a, ea), (b, eb) = load(sys.argv[1]), load(sys.argv[2])
na, nb = sys.argv[1].split('/')[-1], sys.argv[2].split('/')[-1]
print('%-28s | %-52s | %-52s' % ('', na, nb))
for k in sorted(set(a) | set(b)):
    def f(v): return 'n%4d sl %4.2f/%4.2f/%5.1f sk3 %5.1f%% lf %4.1f%% dp %3d lo %6.0f' % (v['steps'], v['mean'], v['p90'], v['mx'], v['s3'], v['lf'], v['deep'], v['low']) if v else '-'
    print('%-28s | %-52s | %-52s' % (' '.join(k), f(a.get(k)), f(b.get(k))))
for e in ea: print('A: ' + e)
for e in eb: print('B: ' + e)
