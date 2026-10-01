#!/usr/bin/env python3
# Compare two tour logs: per stop the AI/vehicle/ped cpu ms, peds/vehicles, mean draws (gfx stats), frame ms if logged
import re, sys
def parse(path):
    stops = {}
    cur = None
    for line in open(path, errors='replace'):
        m = re.search(r'autoplay tour stop (\d+) (\S+) at', line)
        if m:
            cur = m.group(2); stops.setdefault(cur, {'draws': [], 'shot': None, 'disp': [], 'cbkb': [], 'pipes': []}); continue
        if cur is None: continue
        m = re.search(r'gfx \(frame \d+\): draws (\d+), dispatches (\d+).*?CB writes \d+ \(([\d.]+) KB\).*?pipelines (\d+)', line)
        if m:
            s = stops[cur]; s['draws'].append(int(m.group(1))); s['disp'].append(int(m.group(2))); s['cbkb'].append(float(m.group(3))); s['pipes'].append(int(m.group(4)))
            continue
        m = re.search(r'autoplay tour shot \d+ (\S+) \| peds (\d+) vehicles (\d+) \| cpu ms ai ([\d.]+) veh ([\d.]+) peds ([\d.]+)', line)
        if m:
            stops[m.group(1)]['shot'] = tuple(float(x) for x in m.groups()[1:])
    return stops
def mean(v): return sum(v) / len(v) if v else float('nan')
a, b = parse(sys.argv[1]), parse(sys.argv[2])
print("%-22s | %-34s | %-34s" % ("stop", "before: draws mean/max  ai/veh/peds ms", "after: draws mean/max  ai/veh/peds ms"))
for k in a:
    if k not in b: continue
    sa, sb = a[k], b[k]
    fa = "%6.0f/%-5d" % (mean(sa['draws']), max(sa['draws']) if sa['draws'] else 0)
    fb = "%6.0f/%-5d" % (mean(sb['draws']), max(sb['draws']) if sb['draws'] else 0)
    ca = "%.2f/%.2f/%.2f p%d v%d" % (sa['shot'][2], sa['shot'][3], sa['shot'][4], sa['shot'][0], sa['shot'][1]) if sa['shot'] else "-"
    cb = "%.2f/%.2f/%.2f p%d v%d" % (sb['shot'][2], sb['shot'][3], sb['shot'][4], sb['shot'][0], sb['shot'][1]) if sb['shot'] else "-"
    print("%-22s | %s %-22s | %s %-22s" % (k, fa, ca, fb, cb))
