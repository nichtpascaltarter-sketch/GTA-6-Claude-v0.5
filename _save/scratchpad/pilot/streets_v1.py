# The Calle Luna pilot's streets (docs/HANDBUILT_WORLD.md section 10.1, stage 1), as an authoring script (section 5.3):
# the streets are designed here by hand as centrelines, and the script finds where they meet, splits them into nodes
# and edges, takes each node's height from the baked terrain and writes world/districts/cl/roads.ntw. Its output is the
# data; run it once per design change and review the result in the check map. Ids are stable: a node or edge that is
# already in the output file keeps its id (matched by position, and by street and ends), and new ones take the next
# free numbers. usage: python3 -I streets.py <grids dir> <out roads.ntw> [--report]
import math
import os
import re
import sys

import numpy as np

GRIDS, OUT = sys.argv[1], sys.argv[2]
R, CELL, HALF = 2560, 8.0, 10240.0
H = np.fromfile(os.path.join(GRIDS, "height.i16"), dtype="<i2").reshape(R, R).astype(np.float64) * 0.01


def ground(x, y):
    tx, ty = (x + HALF) / CELL - 0.5, (y + HALF) / CELL - 0.5
    ix, iy = int(math.floor(tx)), int(math.floor(ty))
    fx, fy = tx - ix, ty - iy
    return (H[iy, ix] * (1 - fx) * (1 - fy) + H[iy, ix + 1] * fx * (1 - fy) + H[iy + 1, ix] * (1 - fx) * fy + H[iy + 1, ix + 1] * fx * fy)


# ------------------------------------------------------------------------------------------------------------ the design
# Calle Luna's heart, round (1560, 350): a ring of the Paseo del Rio (south, along the river), Bougainvillea Boulevard
# (west), Calle Nueve (north) and Avenida Este (east), with Calle Luna, the main street, through the middle. Shops line
# Calle Luna and Avenida Flores, with service alleys behind Calle Luna's; houses and walk-ups fill the side streets. The
# Paseo de los Heroes is a boulevard with a planted median north of Calle Luna, and a bascule bridge south of the river
# drive takes it over to the south bank's Calle del Muelle. Avenida Palma jogs where it crosses Calle Luna.
#
# Lane widths and medians stay the class's: today's RoadNetwork and road mesher draw those, until the lane graph and
# the mesher read authored ones (RoadOverrides). Shoulders, parking and sidewalks set the widths.
#
# Each street: (key, name, class, lanes, fields, points, clip). `clip` cuts the centreline between its meetings with two
# other streets (by key); without it the points are the ends.
RIVER = [(1190, 245), (1300, 222), (1400, 205), (1500, 186), (1580, 176), (1660, 182), (1740, 212), (1820, 245), (1900, 280), (1960, 300)]
MUELLE = [(1350, 42), (1400, 30), (1470, 15), (1560, 5), (1640, 12), (1720, 35), (1780, 62)]
SHOP = {"parking": "both:2.40", "sidewalk": "both:4.50"}
HOMES = {"parking": "both:2.40", "sidewalk": "both:2.50"}
ALLEY = {"shoulder": "both:0.50", "sidewalk": "none", "speed": "6.0"}
STREETS = [
    # east-west
    ("river", "Paseo del Rio", "street", "1/1", {"parking": "left:2.40", "sidewalk": "left:3.00,right:4.50"}, RIVER, None),
    ("alley_s", None, "lane", "1/1", ALLEY, [(1300, 298), (1880, 298)], ("ceiba", "sol")),
    ("luna", "Calle Luna", "street", "1/1", SHOP, [(1150, 350), (2000, 350)], ("bougainvillea", "este")),
    ("alley_n", None, "lane", "1/1", ALLEY, [(1300, 403), (1880, 403)], ("ceiba", "sol")),
    ("siete", "Calle Siete", "street", "1/1", HOMES, [(1150, 456), (2000, 456)], ("bougainvillea", "este")),
    ("flores", "Avenida Flores", "avenue", "2/2", {"parking": "both:2.40", "sidewalk": "both:4.00"}, [(1150, 562), (1600, 560), (2000, 548)], ("bougainvillea", "este")),
    ("nueve", "Calle Nueve", "street", "1/1", HOMES, [(1150, 660), (2000, 660)], ("bougainvillea", "este")),
    ("muelle", "Calle del Muelle", "street", "1/1", HOMES, MUELLE, None),
    # north-south
    ("bougainvillea", "Bougainvillea Boulevard", "boulevard", "2/2", {"sidewalk": "both:4.50"}, [(1190, 150), (1190, 720)], ("river", "nueve")),
    ("ceiba", "Avenida Ceiba", "street", "1/1", HOMES, [(1330, 150), (1330, 720)], ("river", "nueve")),
    ("heroes_s", "Paseo de los Heroes", "street", "1/1", HOMES, [(1470, 150), (1470, 360)], ("river", "luna")),
    ("heroes_n", "Paseo de los Heroes", "boulevard", "1/1", {"parking": "both:2.40", "sidewalk": "both:3.50"}, [(1470, 340), (1470, 720)], ("luna", "nueve")),
    ("coral", "Avenida Coral", "street", "1/1", HOMES, [(1610, 150), (1610, 720)], ("river", "nueve")),
    ("palma_s", "Avenida Palma", "street", "1/1", HOMES, [(1700, 150), (1700, 360)], ("river", "luna")),
    ("palma_n", "Avenida Palma", "street", "1/1", HOMES, [(1740, 340), (1740, 720)], ("luna", "nueve")),
    ("sol", "Avenida del Sol", "avenue", "2/2", {"parking": "both:2.40", "sidewalk": "both:3.50"}, [(1850, 150), (1850, 720)], ("river", "nueve")),
    ("este", "Avenida Este", "street", "1/1", HOMES, [(1960, 290), (1960, 720)], ("river", "nueve")),
]
# The bridge: the Paseo de los Heroes from the river drive over the river to Calle del Muelle, a deck at 5.50 m (about
# 4.5 m over the water) on 40 m approaches
BRIDGE = ("bridge", "Paseo de los Heroes", "street", "1/1", {"sidewalk": "both:2.50"}, [(1470, 200), (1470, 10)], ("river", "muelle"))
BRIDGE_DECK_Z, BRIDGE_RAMP = 5.50, 40.0
STREETS.append(BRIDGE)

# junction control by the two streets that meet there (anything not listed is derived: priority by road class)
SIGNALS = {("luna", "bougainvillea"): 0, ("luna", "heroes_s"): 20, ("luna", "sol"): 40, ("flores", "bougainvillea"): 30,
           ("flores", "heroes_n"): 50, ("flores", "sol"): 10, ("river", "bougainvillea"): 15, ("river", "bridge"): 35}
STOPS = [("luna", "ceiba"), ("luna", "coral"), ("luna", "palma_s"), ("luna", "palma_n"), ("luna", "este"),
         ("siete", "ceiba"), ("siete", "heroes_n"), ("siete", "coral"), ("siete", "palma_n"), ("siete", "sol")]
PLAN = "lib.signal.cl_twophase"
BULB = 9.0   # the dead ends of Calle del Muelle

# --------------------------------------------------------------------------------------------------------- geometry
EPS = 0.01


def seglen(a, b):
    return math.hypot(b[0] - a[0], b[1] - a[1])


def cum(pts):
    d = [0.0]
    for i in range(1, len(pts)):
        d.append(d[-1] + seglen(pts[i - 1], pts[i]))
    return d


def at(pts, s):
    d = cum(pts)
    s = max(0.0, min(s, d[-1]))
    for i in range(1, len(pts)):
        if s <= d[i] + 1e-9:
            t = (s - d[i - 1]) / max(d[i] - d[i - 1], 1e-12)
            return (pts[i - 1][0] + (pts[i][0] - pts[i - 1][0]) * t, pts[i - 1][1] + (pts[i][1] - pts[i - 1][1]) * t)
    return pts[-1]


def cut(pts, s0, s1):
    """The polyline between arc lengths s0 < s1."""
    d = cum(pts)
    out = [at(pts, s0)]
    for i in range(1, len(pts) - 1):
        if s0 + EPS < d[i] < s1 - EPS:
            out.append(pts[i])
    out.append(at(pts, s1))
    return out


def meets(p, q):
    """Every point where polylines p and q cross or touch: (s along p, s along q, point)."""
    dp, dq = cum(p), cum(q)
    found = []
    for i in range(len(p) - 1):
        a, b = p[i], p[i + 1]
        for j in range(len(q) - 1):
            c, e = q[j], q[j + 1]
            r = (b[0] - a[0], b[1] - a[1])
            s = (e[0] - c[0], e[1] - c[1])
            den = r[0] * s[1] - r[1] * s[0]
            if abs(den) < 1e-12:
                continue
            t = ((c[0] - a[0]) * s[1] - (c[1] - a[1]) * s[0]) / den
            u = ((c[0] - a[0]) * r[1] - (c[1] - a[1]) * r[0]) / den
            if -1e-9 <= t <= 1 + 1e-9 and -1e-9 <= u <= 1 + 1e-9:
                pt = (a[0] + r[0] * t, a[1] + r[1] * t)
                pt_ = (dp[i] + t * seglen(a, b), dq[j] + u * seglen(c, e), pt)
                if all(math.hypot(f[2][0] - pt[0], f[2][1] - pt[1]) > 0.01 for f in found):   # (a shared vertex meets twice)
                    found.append(pt_)
    return found


streets = {s[0]: {"key": s[0], "name": s[1], "cls": s[2], "lanes": s[3], "f": s[4], "pts": [tuple(map(float, p)) for p in s[5]], "clip": s[6]} for s in STREETS}
order = [s[0] for s in STREETS]
# clip each street between its meetings with the two named streets (on their unclipped lines)
for k in order:
    st = streets[k]
    if not st["clip"]:
        continue
    ends = []
    for other in st["clip"]:
        m = meets(st["pts"], streets[other]["pts"])
        if len(m) != 1:
            sys.exit("%s meets %s %d times" % (k, other, len(m)))
        ends.append(m[0][0])
    s0, s1 = min(ends), max(ends)
    st["pts"] = cut(st["pts"], s0, s1)
# the nodes: every meeting of two clipped streets, and every street's ends
nodes = []          # [x, y]
nodeOf = {}         # (street key) -> list of (s, node index)


def nodeAt(pt):
    for i, n in enumerate(nodes):
        if math.hypot(n[0] - pt[0], n[1] - pt[1]) < 0.5:
            return i
    nodes.append([pt[0], pt[1]])
    return len(nodes) - 1


meetsAt = {}       # node index -> set of street keys
for i, a in enumerate(order):
    for b in order[i + 1:]:
        for sa, sb, pt in meets(streets[a]["pts"], streets[b]["pts"]):
            n = nodeAt(pt)
            for key, sk in ((a, sa), (b, sb)):
                if all(nn != n for _, nn in nodeOf.get(key, [])):   # (three streets meeting at one point)
                    nodeOf.setdefault(key, []).append((sk, n))
            meetsAt.setdefault(n, set()).update((a, b))
for k in order:
    p = streets[k]["pts"]
    for s, pt in ((0.0, p[0]), (cum(p)[-1], p[-1])):
        n = nodeAt(pt)
        if all(nn != n for _, nn in nodeOf.get(k, [])):
            nodeOf.setdefault(k, []).append((s, n))
        meetsAt.setdefault(n, set()).add(k)
# snap each node to one exact point, and the streets' ends to it
for n in nodes:
    n[0], n[1] = round(n[0], 2), round(n[1], 2)

# --------------------------------------------------------------------------------------------------------- ids
old_nodes, old_edges, next_n, next_e = [], [], 1, 1
if os.path.exists(OUT):
    for line in open(OUT):
        m = re.match(r"node cl\.n(\d+) at=([-\d.]+),([-\d.]+)", line)
        if m:
            old_nodes.append((int(m.group(1)), float(m.group(2)), float(m.group(3))))
        m = re.match(r"edge cl\.e(\d+) from=cl\.n(\d+) to=cl\.n(\d+)", line)
        if m:
            old_edges.append((int(m.group(1)), int(m.group(2)), int(m.group(3))))
    next_n = max([o[0] for o in old_nodes] + [0]) + 1
    next_e = max([o[0] for o in old_edges] + [0]) + 1
nodeId = {}
for i in sorted(range(len(nodes)), key=lambda i: (nodes[i][0], nodes[i][1])):
    match = [o for o in old_nodes if math.hypot(o[1] - nodes[i][0], o[2] - nodes[i][1]) < 1.0]
    if match:
        nodeId[i] = match[0][0]
    else:
        nodeId[i] = next_n
        next_n += 1

# --------------------------------------------------------------------------------------------------------- edges
edges = []
for k in order:
    st = streets[k]
    pts = st["pts"]
    lst = sorted(nodeOf[k])
    for (s0, n0), (s1, n1) in zip(lst, lst[1:]):
        if s1 - s0 < 1.0:
            sys.exit("%s: two nodes %.2f m apart" % (k, s1 - s0))
        shape = cut(pts, s0, s1)
        shape[0] = tuple(nodes[n0])
        shape[-1] = tuple(nodes[n1])
        edges.append({"street": k, "n0": n0, "n1": n1, "shape": shape})
for e in sorted(edges, key=lambda e: (nodeId[e["n0"]], nodeId[e["n1"]])):
    match = [o for o in old_edges if o[1] == nodeId[e["n0"]] and o[2] == nodeId[e["n1"]]]
    if match:
        e["id"] = match[0][0]
    else:
        e["id"] = next_e
        next_e += 1

# heights: each node on the ground (the bridge's ends too)
z = [round(ground(n[0], n[1]), 2) for n in nodes]


def fmt(v):
    s = "%.2f" % v
    return "0.00" if s == "-0.00" else s


def pts_s(p):
    return ";".join("%s,%s" % (fmt(a), fmt(b)) for a, b in p)


def control(n):
    ks = meetsAt.get(n, set())
    for (a, b), off in SIGNALS.items():
        if a in ks and b in ks:
            return "control=signal plan=%s offset=%.1f" % (PLAN, off)
    for a, b in STOPS:
        if a in ks and b in ks:
            return "control=stop"
    return ""


lines = ["ntw 1 roads district=cl",
         "# The heart of Calle Luna, the stage 1 pilot (docs/HANDBUILT_WORLD.md section 10.1). Written by the city agent's",
         "# authoring script from the streets designed in it; edit the design and re-run it, or edit here and keep the ids."]
for i in sorted(range(len(nodes)), key=lambda i: nodeId[i]):
    ctl = control(i)
    dead = [k for k in meetsAt.get(i, set())]
    bulb = " bulb=%s" % fmt(BULB) if len(dead) == 1 and dead[0] == "muelle" else ""
    lines.append(("node cl.n%04d at=%s,%s z=%s %s%s" % (nodeId[i], fmt(nodes[i][0]), fmt(nodes[i][1]), fmt(z[i]), ctl, bulb)).rstrip().replace("  ", " "))
for e in sorted(edges, key=lambda e: e["id"]):
    st = streets[e["street"]]
    f = dict(st["f"])
    head = "edge cl.e%04d from=cl.n%04d to=cl.n%04d class=%s" % (e["id"], nodeId[e["n0"]], nodeId[e["n1"]], st["cls"])
    if st["name"]:
        head += ' name="%s"' % st["name"]
    head += " lanes=%s" % st["lanes"]
    for key in ("lanewidth", "median", "shoulder", "parking", "sidewalk", "speed", "flags"):
        if key in f:
            head += " %s=%s" % (key, f[key])
    head += " shape=%s" % pts_s(e["shape"])
    sub = []
    if e["street"] == "bridge":
        length = cum(e["shape"])[-1]
        za, zb = z[e["n0"]], z[e["n1"]]
        prof = [(0.0, za), (BRIDGE_RAMP, BRIDGE_DECK_Z), (length - BRIDGE_RAMP, BRIDGE_DECK_Z), (length, zb)]
        head += " profile=" + ";".join("%s:%s" % (fmt(s), fmt(v)) for s, v in prof)
        sub.append("  deck from=%s to=%s kind=bridge" % (fmt(BRIDGE_RAMP - 10.0), fmt(length - BRIDGE_RAMP + 10.0)))
    lines.append(head)
    lines.extend(sub)
    lines.append("end")
open(OUT, "w").write("\n".join(lines) + "\n")
print("streets.py: %d nodes, %d edges -> %s" % (len(nodes), len(edges), OUT))
if "--report" in sys.argv:
    total = sum(cum(e["shape"])[-1] for e in edges)
    sig = sum(1 for i in range(len(nodes)) if "signal" in control(i))
    stop = sum(1 for i in range(len(nodes)) if "stop" in control(i))
    print("  %.0f m of street, %d signals, %d all-way stops; ground %.2f..%.2f m at the nodes" % (total, sig, stop, min(z), max(z)))
    for e in sorted(edges, key=lambda e: cum(e["shape"])[-1])[:5]:
        print("  shortest: cl.e%04d %s %.1f m" % (e["id"], e["street"], cum(e["shape"])[-1]))
