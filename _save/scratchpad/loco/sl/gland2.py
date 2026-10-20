import re, sys
# per-landing detail from the locostairs trace: error (cm, + = further along than planned), the shift, updates since the
# plan last changed, the foot's phase at the plant, speed
ln_re = re.compile(r'\[\s*([\d.]+)\] locostairs trace (\d+) (up|dn) a\s*([-\d.]+) v\s*([-\d.]+) ph([-\d.]+)')
ft_re = re.compile(r'\| (\d) pl(\d) .*? u\s*([-\d.]+),\s*([-\d.]+) pU\s*([-\d.]+) st\s*([-\d.]+) SL\s*([-\d.]+) sh\s*([-\d.]+)')
want = sys.argv[2] if len(sys.argv) > 2 else 'up'
prev = {}; since = {}
rows = []
for ln in open(sys.argv[1]):
    m = ln_re.search(ln)
    if not m: continue
    uid, w, v, ph = m.group(2), m.group(3), float(m.group(5)), float(m.group(6))
    for p in ln.split('|')[1:]:
        f = ft_re.search('|' + p)
        if not f: continue
        s, pl, uh, pU, sh = int(f.group(1)), int(f.group(2)), float(f.group(3)), float(f.group(5)), float(f.group(8))
        k = (uid, w, s)
        if k in prev:
            ppl, ppU, psh = prev[k]
            since[k] = 0 if abs(pU - ppU) > 1e-4 else since.get(k, 0) + 1
            if pl and not ppl and ppU > -90:
                e = (ppU - uh) if w == 'up' else (uh - ppU)
                fph = (ph - (0.5 if s else 0.0)) % 1.0
                rows.append((w, uid, s, e * 100, psh * 100, since.get(k, -1), fph, v))
        prev[k] = (pl, pU, sh)
print('dir uid  s  err_cm  shift_cm  upd_since_plan  foot_ph  v')
for r in rows:
    if r[0] == want: print('%s %s %d %6.1f %7.1f %5d %8.3f %5.2f' % r)
