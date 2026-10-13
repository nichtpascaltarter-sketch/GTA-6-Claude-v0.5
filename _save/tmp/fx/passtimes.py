import re, sys
# passtimes.py log... : mean per-pass synctimer milliseconds over the logged blocks with a non-trivial gbuffer
for path in sys.argv[1:]:
    blocks = []
    cur = None
    for line in open(path, errors='replace'):
        if 'Pass timings' in line:
            cur = {}
            blocks.append(cur)
            continue
        if cur is not None:
            m = re.match(r'^(\S[\w+ ]*?)\s+([\d.]+) ms\s*$', line.rstrip('\n'))
            if m: cur[m.group(1).strip()] = float(m.group(2))
            elif line.strip() == '' or line.startswith('['): cur = None
    blocks = [b for b in blocks if b.get('gbuffer', 0) > 50]
    if not blocks: print(path, 'no timings'); continue
    keys = ['shadows', 'gbuffer', 'lighting', 'ao+gi', 'fog', 'post', 'total']
    print('%-24s n=%d  ' % (path.split('/')[-1], len(blocks)) + '  '.join('%s %.0f' % (k, sum(b.get(k, 0) for b in blocks) / len(blocks)) for k in keys))
