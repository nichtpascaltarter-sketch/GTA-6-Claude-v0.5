#!/usr/bin/env python3
"""FOR LATER round sims summary: /tmp/city/sim97_<district>_<variant>_<seed>.txt -> per district and variant, each seed's
district deadlocks / collisions and the ones within R m of the district's changed spots, and the sums over common seeds."""
import glob, math, re, sys
from collections import defaultdict

R = 60.0
# each district's changed spots: levee = Almeria's 129 m remnant (its corner off the join) and Levee Road's junction with
# Coral Drive; lev2 = the 140 m remnant further south; bay9/mid = 9th Avenue's 25 m piece in Bayshore Boulevard; the
# others = the other shared stretches (10th, 11th Avenue in Bayshore; Del Mar Boulevard, Bayshore Way in Old Cutler Road)
SPOTS = {"levee": [(-2961.0, 2674.0), (-2961.0, 2740.0), (-2956.0, 2802.0)],
         "lev2": [(-2950.0, 2177.0), (-2948.0, 2240.0), (-2950.0, 2307.0)],
         "bay9": [(3900.7, 3437.5)], "mid": [(3900.7, 3437.5)],
         "e135": [(3801.1, 1947.0)], "e182": [(3699.0, -537.5)], "e1005": [(2620.3, -4027.0)], "e1138": [(2945.5, -2852.4)]}


def near(dist, x, y):
    return any(math.hypot(x - a, y - b) <= R for a, b in SPOTS.get(dist, []))


rows = defaultdict(dict)   # (district, variant) -> seed -> numbers
for f in glob.glob("/tmp/city/sim97_*_*_*.txt"):
    m = re.match(r".*/sim97_([a-z0-9]+)_([a-z0-9]+)_([0-9]+)\.txt$", f)
    if not m:
        continue
    dist, var, seed = m.group(1), m.group(2), int(m.group(3))
    txt = open(f, errors="replace").read()
    if "==== Traffic metrics" not in txt:
        continue   # unfinished
    dl = int(re.search(r"deadlocks \(>60 s waits\): (\d+)", txt).group(1))
    co = int(re.search(r"^collisions: (\d+)", txt, re.M).group(1))
    nd = nc = 0
    for line in txt.splitlines():
        if line.startswith("DEADLOCK") or line.startswith("COLLISION"):
            p = re.search(r" at (-?[\d.]+) (-?[\d.]+)", line)
            if p and near(dist, float(p.group(1)), float(p.group(2))):
                if line.startswith("DEADLOCK"):
                    nd += 1
                else:
                    nc += 1
    rows[(dist, var)][seed] = (dl, co, nd, nc)

order = {"fl68": 0, "fl4": 1}
for dist in sorted({d for d, _ in rows}):
    print(f"== {dist} (near = within {R:.0f} m of {SPOTS.get(dist)})")
    vs = sorted({v for d, v in rows if d == dist}, key=lambda v: order.get(v, 9))
    for var in vs:
        s = rows[(dist, var)]
        line = " ".join(f"s{k}:{s[k][0]}/{s[k][1]}({s[k][2]}/{s[k][3]})" for k in sorted(s))
        print(f"  {var:5} deadlocks/collisions (near dl/coll) {line}")
    common = None
    for var in vs:
        ks = set(rows[(dist, var)])
        common = ks if common is None else common & ks
    if common:
        for var in vs:
            s = rows[(dist, var)]
            tot = [sum(s[k][i] for k in common) for i in range(4)]
            print(f"    common seeds {len(common)} {var:5}: deadlocks {tot[0]:4}  collisions {tot[1]:3}  near deadlocks {tot[2]:3}  near collisions {tot[3]:3}")
