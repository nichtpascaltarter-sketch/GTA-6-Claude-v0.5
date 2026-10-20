import re, sys
# chsum.py log...: the chase runs (LOCO_CHASE=1 locoplayer, block v3f) - each segment's chasers (there / stuck, time,
# way, mean speed) and the no-room lines, then the chase summary lines (lpcmp.py has them side by side)
seg = re.compile(r'locoplayer seg (\d+): (\S+) (\S+) (\S+), ([\d.]+) s, ([\d.]+) m, ([\d.]+) m/s')
ch = re.compile(r'locoplayer seg (\d+) chasers: chase3 (.*), chase5 (.*)$')
for fn in sys.argv[1:]:
    print(fn.split('/')[-1])
    segs = {}
    for line in open(fn, errors='replace'):
        m = seg.search(line)
        if m: segs[int(m.group(1))] = '%s %s %s %.2f m/s' % (m.group(2), m.group(3), m.group(4), float(m.group(7)))
        m = ch.search(line)
        if m: print('  seg %2s %-30s | chase3 %-32s | chase5 %s' % (m.group(1), segs.get(int(m.group(1)), ''), m.group(2), m.group(3)))
        if 'no room for chaser' in line: print('  ' + line.split('] ', 1)[-1].strip())
