import sys, re
# why.py <trace.err> <t0> <foot> [back_s]: the debug events (SCAN fits, PLANT, HOLD, QSTEP, STEPEND) of one foot in the
# back_s seconds before trace time t0, and a compact per-frame line (state, h/b/t, ground under heel/toe, heel world xy,
# fit fields) every 2nd frame
fn, t0, foot = sys.argv[1], float(sys.argv[2]), sys.argv[3]
back = float(sys.argv[4]) if len(sys.argv) > 4 else 1.2
lines = open(fn).read().split('\n')
fr_t = {}
for ln in lines:
    m = re.search(r'f(\d+) t([\d.]+) L', ln)
    if m: fr_t[int(m.group(1))] = float(m.group(2))
def t_of(f):
    return fr_t.get(f, fr_t.get(f + 1, -1))
for ln in lines:
    m = re.match(r'(SCAN|PLANT|HOLD|QSTEP|STEPEND) f(\d+) foot (\d)', ln)
    if m:
        t = t_of(int(m.group(2)))
        if m.group(3) == foot and t0 - back <= t <= t0 + 0.1: print('%7.3f %s' % (t, ln[:230]))
        continue
    m = re.search(r'f(\d+) t([\d.]+) L', ln)
    if not m: continue
    t = float(m.group(2))
    if not (t0 - back <= t <= t0 + 0.1) or int(m.group(1)) % 2: continue
    part = ln.split(' | ')
    seg = [p for p in ln.split('| ') if p.startswith(foot + ' ') and ' pl' in p[:12]]
    if seg:
        s = seg[0]
        g = lambda k: (re.search(k + r'\s*(-?[\d.]+)', s) or [None, '?'])[1]
        st = 'P' if ' pl1' in s else ('S' if float(g('st')) >= 0 else '-')
        print('%7.3f   %s st%s h%s b%s t%s SL%s g%s hw%s fy%s' % (t, st, g('st'), g(' h'), g(' b'), g(' t'), g('SL'), re.search(r' g\s*(-?[\d.]+,\s*-?[\d.]+)', s).group(1), re.search(r'hw(-?[\d.]+,-?[\d.]+)', s).group(1), g('fy')))
