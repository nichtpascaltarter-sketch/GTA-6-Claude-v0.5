#!/usr/bin/env python3
# Compare two story-regression logs: per mission passed / FAILED and the reason
import re, sys
def parse(path):
    res = {}
    summary = []
    for line in open(path, errors='replace'):
        m = re.search(r'\[missiontest\]\s+(\S+): (passed|FAILED|could not start)(.*)', line)
        if m: res[m.group(1)] = (m.group(2), m.group(3).strip()[:120])
        elif 'SUMMARY' in line or 'roam:' in line and 'checks ok' in line:
            summary.append(line.strip()[:200])
    return res, summary
(a, sa), (b, sb) = parse(sys.argv[1]), parse(sys.argv[2])
keys = list(a.keys()) + [k for k in b if k not in a]
changed = 0
for k in keys:
    ra, rb = a.get(k, ('-', '')), b.get(k, ('-', ''))
    flag = '' if ra[0] == rb[0] else '   <== CHANGED'
    if flag: changed += 1
    print("%-28s before %-7s after %-7s%s" % (k, ra[0], rb[0], flag))
    if flag: print("      before: %s\n      after:  %s" % (ra[1], rb[1]))
print("before summary:", *sa, sep='\n  ')
print("after summary:", *sb, sep='\n  ')
print("missions: %d before, %d after, %d changed" % (len(a), len(b), changed))
