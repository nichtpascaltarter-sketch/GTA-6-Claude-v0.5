#!/usr/bin/env python3
# shotgfx.py LOG [LOG2]: per saved shot, the mean draws of the gfx stats lines logged since the previous shot
import re, sys
def parse(p):
    out = {}; cur = []
    for line in open(p, errors='replace'):
        m = re.search(r'gfx \(frame \d+\): draws (\d+)', line)
        if m: cur.append(int(m.group(1))); continue
        m = re.search(r'Saved .*\\([^\\]+)\.bmp', line)
        if m:
            out[m.group(1)] = (sum(cur[-4:]) / len(cur[-4:])) if cur else float('nan'); cur = []
    return out
a = parse(sys.argv[1]); b = parse(sys.argv[2]) if len(sys.argv) > 2 else {}
for k in a:
    if b: print("%-16s %6.0f -> %6.0f" % (k, b.get(k, float('nan')), a[k]))
    else: print("%-16s %6.0f" % (k, a[k]))
