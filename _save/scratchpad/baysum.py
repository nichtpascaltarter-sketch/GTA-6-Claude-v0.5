#!/usr/bin/env python3
"""Bayshore sims summary: /tmp/city/sim95_<district>_<variant>_<seed>.txt -> per district and variant, each seed's
district deadlocks / collisions (the run's metrics) and the ones within R m of the target (3890,3120), and the sums."""
import glob, math, re, sys
from collections import defaultdict

TX, TY, R = 3890.0, 3120.0, 60.0
# each district's changed spots (near = within R of any): bay9/mid the target; boug the old Bougainvillea/S 14th junction;
# laurel the 6-way and the new join; levee the Almeria duplicate's ends and middle and the Coral Drive split
SPOTS = {"bay9": [(3890.0, 3120.0)], "mid": [(3890.0, 3120.0)], "boug": [(3600.0, -1450.0), (3600.0, -1350.0)],
         "laurel": [(4799.2, 7871.8), (4800.0, 7846.5)], "j6223": [(3815.9, 1985.6)], "j6225": [(2874.0, -2487.6)],
         "j6232": [(4789.6, 7256.8)], "j6233": [(4390.9, 8348.8)], "levee": [(-2960.1, 2802.3), (-2953.0, 2980.0), (-2946.9, 3161.8), (-2932.4, 2802.1)]}


def near(dist, x, y):
    return any(math.hypot(x - a, y - b) <= R for a, b in SPOTS.get(dist, [(TX, TY)]))
rows = defaultdict(dict)   # (district, variant) -> seed -> numbers
for f in glob.glob("/tmp/city/sim95_*_*_*.txt"):
    m = re.match(r".*/sim95_([a-z0-9]+)_([a-z0-9]+)_([0-9]+)\.txt$", f)
    if not m:
        continue
    dist, var, seed = m.group(1), m.group(2), int(m.group(3))
    txt = open(f, errors="replace").read()
    if "==== Traffic metrics" not in txt:
        continue   # unfinished
    dl = int(re.search(r"deadlocks \(>60 s waits\): (\d+)", txt).group(1))
    co = int(re.search(r"^collisions: (\d+)", txt, re.M).group(1))
    nd = nc = 0
    nodes = defaultdict(int)
    for line in txt.splitlines():
        if line.startswith("DEADLOCK"):
            p = re.search(r" at (-?[\d.]+) (-?[\d.]+)", line)
            g = re.search(r"gateNode (\d+)", line)
            if g:
                nodes[g.group(1)] += 1
            if p and near(dist, float(p.group(1)), float(p.group(2))):
                nd += 1
        elif line.startswith("COLLISION"):
            p = re.search(r" at (-?[\d.]+) (-?[\d.]+)", line)
            if p and near(dist, float(p.group(1)), float(p.group(2))):
                nc += 1
    ncl = sum(1 for line in txt.splitlines() if line.startswith("COLLISION"))
    rows[(dist, var)][seed] = (dl, co, nd, nc, ncl, dict(nodes))

order = {"fx65": 0, "bay": 1, "bay3": 2}
for dist in sorted({d for d, _ in rows}):
    print(f"== {dist} (near = within {R:.0f} m of {SPOTS.get(dist)})")
    for var in sorted({v for d, v in rows if d == dist}, key=lambda v: order.get(v, 9)):
        s = rows[(dist, var)]
        seeds = sorted(s)
        line = " ".join(f"s{k}:{s[k][0]}/{s[k][1]}({s[k][2]}/{s[k][3]})" for k in seeds)
        tot = [sum(s[k][i] for k in seeds) for i in range(4)]
        print(f"  {var:5} seeds {len(seeds)}: deadlocks/collisions (near dl/coll) {line}")
        print(f"        sum: deadlocks {tot[0]}, collisions {tot[1]}; near: deadlocks {tot[2]}, collisions {tot[3]}")
    if len(sys.argv) > 1:
        # the same seeds for every variant only
        common = None
        for var in order:
            if (dist, var) in rows:
                ks = set(rows[(dist, var)])
                common = ks if common is None else common & ks
        if common:
            print(f"  common seeds {sorted(common)}:")
            for var in sorted({v for d, v in rows if d == dist}, key=lambda v: order.get(v, 9)):
                s = rows[(dist, var)]
                tot = [sum(s[k][i] for k in common) for i in range(4)]
                print(f"    {var:5} deadlocks {tot[0]:3}  collisions {tot[1]:3}  near deadlocks {tot[2]:3}  near collisions {tot[3]:3}")
