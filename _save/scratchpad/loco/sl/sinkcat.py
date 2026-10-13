import sys, re, collections
# sunk stances (frames below -3 cm) by direction, the sole point that went lowest, and the stance's landing error
st = collections.OrderedDict()
for fn in sys.argv[1:]:
    for l in open(fn):
        if not l.startswith('SINK'): continue
        m = re.match(r'SINK run (\d+) (\w+) t([\d.]+) foot (\d) kind (\d) age ([\d.]+) h\s*([-\d.]+) b\s*([-\d.]+) t\s*([-\d.]+) heelInTread\s*([-\d.]+) toeInTread\s*([-\d.]+) stepT ([-\d.]+) v ([\d.]+) landErr ([-\d.]+) shift ([-\d.]+)', l)
        run, d, t, foot, kind, age = m.group(1), m.group(2), float(m.group(3)), m.group(4), m.group(5), float(m.group(6))
        key = (fn, run, d, foot, round(t - age, 2))
        h, b, tt = float(m.group(7)), float(m.group(8)), float(m.group(9))
        pt = 'heel' if min(h, b, tt) == h else ('ball' if min(h, b, tt) == b else 'toe')
        st.setdefault(key, []).append((kind, pt, float(m.group(14)), float(m.group(15))))
cnt = collections.Counter(); frames = collections.Counter()
for k, v in st.items():
    kind, pt, e, sh = v[0]
    eb = 'noplan' if e > 90 else ('short<-4' if e < -4 else ('short-4..-1' if e < -1 else ('ok-1..1' if e <= 1 else ('long1..4' if e <= 4 else 'long>4'))))
    cnt[(k[2], pt, eb)] += 1
    frames[(k[2], pt, eb)] += len(v)
for k in sorted(cnt): print('%-3s %-5s %-12s stances %3d frames %4d' % (k + (cnt[k], frames[k])))
