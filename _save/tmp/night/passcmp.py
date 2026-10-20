import re, sys
def parse(path):
    blocks = {}; cur = None; fr = None
    for line in open(path, errors='replace'):
        m = re.search(r'Pass timings, device idle at boundaries \(frame (\d+)', line)
        if m: fr = int(m.group(1)); cur = {}; blocks[fr] = cur; continue
        if cur is not None:
            m = re.match(r'^(\S[\w+ ]*?)\s+([\d.]+) ms\s*$', line.rstrip('\n'))
            if m: cur[m.group(1).strip()] = float(m.group(2))
            elif line.strip() == '' or line.startswith('['): cur = None
    return blocks
A, B = parse(sys.argv[1]), parse(sys.argv[2])
keys = ['gbuffer', 'shadows', 'lighting', 'fog', 'post', 'total']
frames = sorted(set(A) & set(B))
print('frame ' + ' '.join('%17s' % k for k in keys) + '   lighting/gbuffer A->B')
sa = {k: 0.0 for k in keys}; sb = {k: 0.0 for k in keys}
for f in frames:
    a, b = A[f], B[f]
    for k in keys: sa[k] += a.get(k, 0); sb[k] += b.get(k, 0)
    print('%5d ' % f + ' '.join('%8.0f/%8.0f' % (a.get(k, 0), b.get(k, 0)) for k in keys) + '   %.3f -> %.3f' % (a['lighting'] / a['gbuffer'], b['lighting'] / b['gbuffer']))
n = len(frames)
print('sum   ' + ' '.join('%8.0f/%8.0f' % (sa[k], sb[k]) for k in keys))
print('B/A raw: ' + ', '.join('%s %+.1f%%' % (k, 100 * (sb[k] / sa[k] - 1)) for k in keys))
g = sb['gbuffer'] / sa['gbuffer']
print('B/A normalised by gbuffer (load): ' + ', '.join('%s %+.1f%%' % (k, 100 * (sb[k] / sa[k] / g - 1)) for k in keys))
