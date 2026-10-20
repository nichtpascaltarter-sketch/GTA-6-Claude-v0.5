import re, sys
# in-game landing error from the locostairs trace: heel along-position at the plant vs the planned heel (pU) of the frame
# before; positive = further along the walk than planned
ln_re = re.compile(r'locostairs trace (\d+) (up|dn) a\s*([-\d.]+) v\s*([-\d.]+) ph([-\d.]+)')
ft_re = re.compile(r'\| (\d) pl(\d) .*? u\s*([-\d.]+),\s*([-\d.]+) pU\s*([-\d.]+) st\s*([-\d.]+) SL\s*([-\d.]+) sh\s*([-\d.]+)')
def pct(a, q):
    a = sorted(a)
    return a[min(len(a) - 1, int(q * (len(a) - 1) + 0.5))] if a else float('nan')
for fn in sys.argv[1:]:
    prev = {}
    errs = {'up': [], 'dn': []}
    for ln in open(fn):
        m = ln_re.search(ln)
        if not m: continue
        uid, w, v = m.group(1), m.group(2), float(m.group(4))
        parts = ln.split('|')[1:]
        for p in parts:
            f = ft_re.search('|' + p)
            if not f: continue
            s, pl, uh, pU, sh = int(f.group(1)), int(f.group(2)), float(f.group(3)), float(f.group(5)), float(f.group(8))
            k = (uid, w, s)
            if k in prev:
                ppl, ppU, psh = prev[k]
                if pl and not ppl and ppU > -90:
                    e = (ppU - uh) if w == 'up' else (uh - ppU)
                    errs[w].append((e * 100, psh * 100, v))
            prev[k] = (pl, pU, sh)
    for w in ('up', 'dn'):
        E = [e[0] for e in errs[w]]
        if not E: continue
        print('%s %s n %3d  err cm: mean %5.2f  p10 %5.2f  med %5.2f  p90 %5.2f  max %5.2f' % (fn.split('/')[-1], w, len(E), sum(E) / len(E), pct(E, .1), pct(E, .5), pct(E, .9), max(E)))
        if '-v' in sys.argv[0:1]: pass
