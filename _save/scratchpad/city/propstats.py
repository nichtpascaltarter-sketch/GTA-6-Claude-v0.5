#!/usr/bin/env python3
# propstats.py LOG [LOG2]: per shot view, the props line(s) logged while the camera sat on it (between the previous
# view's "Saved" line and its own), last one wins: in-view prop instances and triangles (GPU-culled readback)
import re, sys
def per_view(path):
    out, cur = {}, None
    for ln in open(path, errors='replace'):
        m = re.search(r'props \(frame (\d+)\): camera (\d+) instances, (\d+) triangles', ln)
        if m: cur = (int(m.group(2)), int(m.group(3))); continue
        m = re.search(r'Saved .*\\([^\\]+)\.bmp', ln)
        if m:
            out[m.group(1)] = cur
    return out
a = per_view(sys.argv[1])
b = per_view(sys.argv[2]) if len(sys.argv) > 2 else {}
for v, s in a.items():
    if b:
        t = b.get(v)
        if s and t:
            print(f"{v:28s} before {s[0]:4d} inst {s[1]:7d} tris | after {t[0]:4d} inst {t[1]:7d} tris | {t[1]-s[1]:+7d} ({(t[1]-s[1])*100.0/max(s[1],1):+.1f}%)")
        else:
            print(f"{v:28s} before {s} after {t}")
    else:
        print(f"{v:28s} {s}")
