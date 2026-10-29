# A district's streets for the hand-built world (docs/HANDBUILT_WORLD.md section 5.3), as an authoring script. The
# streets are designed below by hand, as centrelines with their names, classes, lanes, widths, junction controls and
# bridges; the script finds where they meet, splits them into nodes and edges, takes each node's height from the baked
# terrain and writes the district's roads.ntw. So far it holds Calle Luna's streets (section 10.1): the pilot's heart
# (stage 1) and the south bank (stage 2's first part), for world/districts/cl/roads.ntw.
# Its output is the data: run it once per design change, then format the file (ntwfmt) and review it with the
# validator (worldv) and in-game shots. Ids are stable: a node already in the output file keeps its id (matched by
# position, within a metre), an edge keeps its id while it runs between the same two nodes, and new ones take the next
# free numbers; so a re-run of an unchanged design, formatted, rewrites the file byte for byte. Two nodes that would
# take one node's id, or two edges one edge's, stop the script.
# usage: python3 -I tools/author/roads.py <grids dir> <out roads.ntw> [--report]
#   the grids dir: `worlddump grids world/terrain <dir>`; worlddump is tools/author/worlddump.cpp, a host tool:
#   g++ -std=c++17 -O1 -I. tools/author/worlddump.cpp -o worlddump. Then format the file: `ntwfmt <out roads.ntw>`
#   (tools/ntwfmt.cpp). It needs numpy.
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
# Calle Luna's heart, round (1560, 350). Calle Luna, the main street, runs east-west through the middle: shops with
# flats over them, and one-way service alleys behind the shops. Avenida Flores is the second shopping street, two lanes each
# way; Calle Siete and Calle Nueve are residential. The Paseo del Rio follows the river's north bank, with the boatyard
# and the riverfront between it and the water. Bougainvillea Boulevard is the through route north-south, and crosses
# the river on a bascule bridge to the south bank's Calle del Muelle. The Paseo de los Heroes has a planted median north
# of Calle Luna, and Avenida Palma jogs where it crosses Calle Luna. Streets that will run on into the rest of the
# district (stage 2) end at the pilot's edge in turning circles (streets: the mesher paves turning circles only on those).
#
# The south bank (stage 2, Calle Luna Sur): Calle del Muelle runs on along the river both ways; Bougainvillea
# Boulevard runs on south from the bascule bridge, and a second bridge carries Avenida del Sol over the river to it;
# Flamingo Boulevard is the east one. Between them residential avenidas every 120-140 m and calles every 105-120 m, with
# Avenida del Mercado as the shopping street and Avenida de la Estacion along the SkyLine's line (the elevated loop's
# south side runs over a clear strip beside its north sidewalk, its centre 21.4 m from the street's). Calle del
# Viaducto runs beside the I-38 viaduct, whose corridor stays clear east of it. The cemetery fills the block from
# Caoba to Tamarindo and Estacion to Veintiuno, so Almendro stops at Estacion and Diecinueve breaks there. The three
# through routes run on past Calle Veintiuno in turning circles at the district's bound, for the districts south.
#
# Lane widths and medians stay the class's: today's RoadNetwork and road mesher draw those, until the lane graph and
# the mesher read authored ones (RoadOverrides). Shoulders, parking and sidewalks set the widths.
#
# Each street: (key, name, class, lanes, fields, points, clip). `clip` cuts the centreline between its meetings with two
# other streets (by key); without it the points are the ends, and an end that meets no other street is a dead end.
RIVER = [(1190, 245), (1300, 222), (1400, 205), (1500, 186), (1580, 176), (1660, 182), (1740, 212), (1820, 245), (1900, 280), (1960, 300),
         (2010, 318)]
MUELLE = [(1000, 92), (1075, 79), (1150, 66), (1190, 62), (1300, 52), (1400, 30), (1470, 15), (1560, 5), (1640, 12), (1720, 35), (1780, 62),
          (1850, 80), (1920, 100), (1990, 130), (2060, 155), (2110, 163), (2160, 150), (2230, 115), (2300, 85), (2390, 50)]
# Calle del Viaducto, 32 m west of the I-38 viaduct's centreline (which leans east going south), Calle Once to Veintiuno
VIADUCTO = [(2481.4, -100), (2583.7, -870)]
SOUTH_END = -912   # (the through routes' turning circles, inside the district's bound at y -930)
AVENIDA = {"parking": "both:2.40", "sidewalk": "both:3.50"}
SHOP = {"parking": "both:2.40", "sidewalk": "both:4.50"}
HOMES = {"parking": "both:2.40", "sidewalk": "both:2.50"}
ALLEY = {"shoulder": "both:0.50", "sidewalk": "none", "speed": "6.0"}
STREETS = [
    # east-west
    ("river", "Paseo del Rio", "street", "1/1", {"parking": "left:2.40", "sidewalk": "both:4.00"}, RIVER, None),
    ("alley_s", None, "lane", "1/0", ALLEY, [(1300, 298), (1880, 298)], ("ceiba", "sol")),   # one-way: eastbound
    ("luna", "Calle Luna", "avenue", "1/1", SHOP, [(1150, 350), (2000, 350)], ("bougainvillea", "este")),
    ("alley_n", None, "lane", "1/0", ALLEY, [(1880, 403), (1300, 403)], ("ceiba", "sol")),   # one-way: westbound
    ("siete", "Calle Siete", "street", "1/1", HOMES, [(1150, 456), (2000, 456)], ("bougainvillea", "este")),
    ("flores", "Avenida Flores", "avenue", "2/2", {"parking": "both:2.40", "sidewalk": "both:4.00"}, [(1150, 562), (1600, 560), (2000, 548)],
     ("bougainvillea", "este")),
    ("nueve", "Calle Nueve", "street", "1/1", HOMES, [(1150, 660), (2000, 660)], ("bougainvillea", "este")),
    ("muelle", "Calle del Muelle", "street", "1/1", HOMES, MUELLE, None),
    # north-south
    ("bridge", "Bougainvillea Boulevard", "boulevard", "2/2", {"sidewalk": "both:2.50"}, [(1190, 62), (1190, 245)], None),
    ("bougainvillea", "Bougainvillea Boulevard", "boulevard", "2/2", {"sidewalk": "both:4.50"}, [(1190, 245), (1190, 660)], None),
    ("bougainvillea_n", "Bougainvillea Boulevard", "street", "1/1", HOMES, [(1190, 660), (1190, 710)], None),   # (on north in stage 2)
    ("ceiba", "Avenida Ceiba", "street", "1/1", HOMES, [(1330, 150), (1330, 720)], ("river", "nueve")),
    ("heroes_s", "Paseo de los Heroes", "street", "1/1", HOMES, [(1470, 150), (1470, 360)], ("river", "luna")),
    ("heroes_n", "Paseo de los Heroes", "boulevard", "1/1", {"parking": "both:2.40", "sidewalk": "both:3.50"}, [(1470, 340), (1470, 720)],
     ("luna", "nueve")),
    ("coral", "Avenida Coral", "street", "1/1", HOMES, [(1610, 150), (1610, 720)], ("river", "nueve")),
    ("palma_s", "Avenida Palma", "street", "1/1", HOMES, [(1700, 150), (1700, 360)], ("river", "luna")),
    ("palma_n", "Avenida Palma", "street", "1/1", HOMES, [(1740, 340), (1740, 720)], ("luna", "nueve")),
    ("sol", "Avenida del Sol", "avenue", "2/2", {"parking": "both:2.40", "sidewalk": "both:3.50"}, [(1850, 150), (1850, 720)], ("river", "nueve")),
    ("este", "Avenida Este", "street", "1/1", HOMES, [(1960, 300), (1960, 710)], None),
    # the south bank (stage 2): its edges first, then north-south west to east, then east-west north to south
    ("poniente", "Avenida Poniente", "street", "1/1", HOMES, [(1000, 92), (1000, -870)], None),
    ("viaducto", "Calle del Viaducto", "street", "1/1", HOMES, VIADUCTO, None),
    ("veintiuno", "Calle Veintiuno", "street", "1/1", HOMES, [(1000, -870), VIADUCTO[1]], None),
    ("bougainvillea_s", "Bougainvillea Boulevard", "boulevard", "2/2", {"sidewalk": "both:3.50"}, [(1190, 62), (1190, SOUTH_END)], None),
    ("mangle", "Avenida Mangle", "street", "1/1", HOMES, [(1330, 200), (1330, -950)], ("muelle", "veintiuno")),
    ("caoba", "Avenida Caoba", "street", "1/1", HOMES, [(1470, 200), (1470, -950)], ("muelle", "veintiuno")),
    ("almendro", "Avenida Almendro", "street", "1/1", HOMES, [(1610, 200), (1610, -950)], ("muelle", "estacion")),
    ("tamarindo", "Avenida Tamarindo", "street", "1/1", HOMES, [(1730, 200), (1730, -950)], ("muelle", "veintiuno")),
    ("sol_bridge", "Avenida del Sol", "avenue", "2/2", {"sidewalk": "both:2.50"}, [(1850, 80), (1850, 300)], ("muelle", "river")),
    ("sol_s", "Avenida del Sol", "avenue", "2/2", AVENIDA, [(1850, 80), (1850, SOUTH_END)], None),
    ("guayacan", "Avenida Guayacan", "street", "1/1", HOMES, [(1980, 200), (1980, -950)], ("muelle", "veintiuno")),
    ("jacaranda", "Avenida Jacaranda", "street", "1/1", HOMES, [(2110, 200), (2110, -950)], ("muelle", "veintiuno")),
    ("flamboyan", "Avenida Flamboyan", "street", "1/1", HOMES, [(2250, 200), (2250, -950)], ("muelle", "veintiuno")),
    ("flamingo", "Flamingo Boulevard", "boulevard", "2/2", {"sidewalk": "both:3.50"}, [(2390, 50), (2390, SOUTH_END)], None),
    ("ribera", "Calle Ribera", "street", "1/1", HOMES, [(1800, 30), (2300, 30)], ("sol_s", "flamboyan")),
    ("once", "Calle Once", "street", "1/1", HOMES, [(950, -100), (2700, -100)], ("poniente", "viaducto")),
    ("trece", "Calle Trece", "street", "1/1", HOMES, [(950, -205), (2700, -205)], ("poniente", "viaducto")),
    ("mercado", "Avenida del Mercado", "avenue", "1/1", SHOP, [(950, -310), (2700, -310)], ("poniente", "viaducto")),
    ("quince", "Calle Quince", "street", "1/1", HOMES, [(950, -420), (2700, -420)], ("poniente", "viaducto")),
    ("diecisiete", "Calle Diecisiete", "street", "1/1", HOMES, [(950, -540), (2700, -540)], ("poniente", "viaducto")),
    ("estacion", "Avenida de la Estacion", "avenue", "1/1", SHOP, [(950, -650), (2700, -650)], ("poniente", "viaducto")),
    ("diecinueve_w", "Calle Diecinueve", "street", "1/1", HOMES, [(950, -755), (1600, -755)], ("poniente", "caoba")),
    ("diecinueve_e", "Calle Diecinueve", "street", "1/1", HOMES, [(1600, -755), (2700, -755)], ("tamarindo", "viaducto")),
]
# The bridges: Bougainvillea Boulevard's bascule bridge from Calle del Muelle over the river to the Paseo del Rio, and
# Avenida del Sol's (stage 2) the same way, each a deck at 5.50 m (about 4.5 m over the water) on 40 m ramps
BRIDGES = {"bridge": (5.50, 40.0), "sol_bridge": (5.50, 40.0)}

# Junction control, by two of the streets that meet there. Signals, with their offset into the plan's cycle (a green
# wave along Calle Luna at 11 m/s and along Avenida Flores at 13); all-way stops; and every other junction priority by
# road class ("none"), so that today's derivation never puts signals at an alley's mouth.
SIGNALS = {("luna", "bougainvillea"): 0, ("luna", "heroes_s"): 25, ("luna", "coral"): 38, ("luna", "sol"): 0, ("flores", "bougainvillea"): 30,
           ("flores", "heroes_n"): 51, ("flores", "sol"): 20, ("river", "bougainvillea"): 15, ("river", "sol"): 45,
           ("muelle", "sol_s"): 0, ("mercado", "bougainvillea_s"): 10, ("mercado", "sol_s"): 40, ("mercado", "flamingo"): 25,
           ("estacion", "bougainvillea_s"): 20, ("estacion", "sol_s"): 50, ("estacion", "flamingo"): 35}
STOPS = [("siete", "ceiba"), ("siete", "heroes_n"), ("siete", "coral"), ("siete", "palma_n"), ("nueve", "sol"), ("luna", "este"), ("flores", "este")]
STOPS += [(a, b) for a in ("trece", "diecisiete") for b in ("mangle", "caoba", "almendro", "tamarindo", "guayacan", "jacaranda", "flamboyan")]
PLAN = "lib.signal.cl_twophase"
BULB = {}   # turning circles at the dead ends, by class: 10 m (as the road mesher paves them: half the road and 4.5 m)

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
        if match[0][0] in nodeId.values():
            sys.exit("roads.py: two nodes are within 1 m of node cl.n%04d (at %.2f,%.2f): move one" % (match[0][0], match[0][1], match[0][2]))
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
        if any(f.get("id") == match[0][0] for f in edges):
            sys.exit("roads.py: two edges run from cl.n%04d to cl.n%04d (edge cl.e%04d): put a node on one" % (match[0][1], match[0][2], match[0][0]))
        e["id"] = match[0][0]
    else:
        e["id"] = next_e
        next_e += 1

# each node's edge count
degree = [0] * len(nodes)
for e in edges:
    degree[e["n0"]] += 1
    degree[e["n1"]] += 1

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
    return "control=none" if degree[n] >= 3 else ""


lines = ["ntw 1 roads district=cl",
         "# Calle Luna: the heart (stage 1's pilot) and the south bank (stage 2), docs/HANDBUILT_WORLD.md section 10.1. Written",
         "# by tools/author/roads.py from its street design; edit the design and re-run it, or edit here and keep the ids."]
for i in sorted(range(len(nodes)), key=lambda i: nodeId[i]):
    ctl = control(i)
    bulb = ""
    if degree[i] == 1:
        cls = streets[[e for e in edges if i in (e["n0"], e["n1"])][0]["street"]]["cls"]
        bulb = " bulb=%s" % fmt(BULB.get(cls, 10.0))
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
    if e["street"] in BRIDGES:
        deck_z, ramp = BRIDGES[e["street"]]
        length = cum(e["shape"])[-1]
        za, zb = z[e["n0"]], z[e["n1"]]
        prof = [(0.0, za), (ramp, deck_z), (length - ramp, deck_z), (length, zb)]
        head += " profile=" + ";".join("%s:%s" % (fmt(s), fmt(v)) for s, v in prof)
        sub.append("  deck from=%s to=%s kind=bridge" % (fmt(ramp - 10.0), fmt(length - ramp + 10.0)))
    lines.append(head)
    lines.extend(sub)
    lines.append("end")
open(OUT, "w").write("\n".join(lines) + "\n")
print("roads.py: %d nodes, %d edges -> %s" % (len(nodes), len(edges), OUT))
if "--report" in sys.argv:
    total = sum(cum(e["shape"])[-1] for e in edges)
    sig = sum(1 for i in range(len(nodes)) if "signal" in control(i))
    stop = sum(1 for i in range(len(nodes)) if "stop" in control(i))
    print("  %.0f m of street, %d signals, %d all-way stops; ground %.2f..%.2f m at the nodes" % (total, sig, stop, min(z), max(z)))
    for e in sorted(edges, key=lambda e: cum(e["shape"])[-1])[:5]:
        print("  shortest: cl.e%04d %s %.1f m" % (e["id"], e["street"], cum(e["shape"])[-1]))
