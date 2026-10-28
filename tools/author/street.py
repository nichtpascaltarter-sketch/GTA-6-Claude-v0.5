# Street-front buildings for one block of the hand-built world (docs/HANDBUILT_WORLD.md section 5.3): an authoring
# script, run once per design change. It reads the block's parcels (block.ntw), the district's streets (roads.ntw) and
# the library's plans and palettes, and writes one building file per placement, as hand-written ones are: its plan,
# `at` (the plan's origin), z (its ground floor, just over the sidewalk in front of it), yaw (square to the street),
# mirror and palette. Its output is the data: review the summary, the validator and the shots, and edit the files by
# hand where the rules don't do (a re-run overwrites only the files it wrote, which say so).
#
# The rules:
# - a parcel's buildings stand along its front, 0.6 m behind the sidewalk, in from its sides; on an avenue they make a
#   street wall: one or two of the street-front plans side by side, their party walls touching; on a street a house
#   stands alone in the middle of its parcel;
# - a corner parcel (another street runs by its side) takes the corner plan at that end, its side-street wall (the
#   plan's right, or mirrored its left) towards the side street;
# - a plan fits the frontage and the depth, and suits the parcel's use;
# - no plan with the same palette twice within three placements, no palette twice side by side, and no house plan
#   twice in neighbouring parcels;
# - mirrors alternate, so that neighbours pair on their party walls;
# - z: the sidewalk's height in front of the building (the street's profile, and 0.15 m of kerb) plus 0.01 m, and with
#   --ground (the compiled ground on a grid: "grid x0= y0= step= nx= ny=" and ny rows of nx heights) also at least
#   0.05 m over the ground under the footprint.
# It prints its seed and a summary: placements per plan, and each parcel left empty with the reason.
#
# usage: python3 -I tools/author/street.py <world dir> <block id> [--seed N] [--ground grid.txt] [--write]
import math
import os
import random
import re
import sys

WORLD, BLOCK = sys.argv[1], sys.argv[2]
OPTS = sys.argv[3:]
SEED = int(OPTS[OPTS.index("--seed") + 1]) if "--seed" in OPTS else 1
GROUND = OPTS[OPTS.index("--ground") + 1] if "--ground" in OPTS else None
WRITE = "--write" in OPTS
MARK = "# written by tools/author/street.py"
# The district's plans by role (Calle Luna): the street wall's, the corners', the houses'
STREET_WALL = ["lib.plan.shop.flats_a", "lib.plan.midrise.walkup_b"]
CORNER = "lib.plan.shop.corner_c"
HOUSES = ["lib.plan.house.casita_d", "lib.plan.house.bungalow_c"]
SETBACK, SIDE_GAP, HOUSE_GAP = 0.6, 0.4, 1.5   # (m: behind the sidewalk; in from a parcel's side; a house's side yard)


# ------------------------------------------------------------------------------------------------ reading NTW
def records(path):
    """The file's records: (type, id or None, {key: value}, line), continuation lines joined, 'end' dropped."""
    out, cur = [], None
    for n, raw in enumerate(open(path, encoding="utf-8"), 1):
        line = raw.rstrip("\n")
        body = line.split(" #")[0] if not line.lstrip().startswith("#") else ""
        if not body.strip() or body.strip() == "end":
            continue
        if line.startswith(" ") and cur is not None and "=" in body.split()[0]:   # (a continuation; a word first: a child record)
            cur[3] += " " + body.strip()
            continue
        cur = [None, None, None, body.strip(), n]
        out.append(cur)
    res = []
    for _, _, _, text, n in out:
        toks = re.findall(r'(\w+)="([^"]*)"|(\S+)', text)
        words = [t[2] if t[2] else t[0] + "=" + t[1] for t in toks]
        kv = {}
        for w in words[1:]:
            if "=" in w:
                k, v = w.split("=", 1)
                kv[k] = v
        rid = words[1] if len(words) > 1 and "=" not in words[1] else None
        res.append((words[0], rid, kv, n))
    return res


def pts(s):
    return [tuple(float(v) for v in p.split(",")) for p in s.split(";")]


district = BLOCK.split(".")[0]
bdir = os.path.join(WORLD, "districts", district, "blocks", BLOCK.split(".")[1])
parcels = [(rid, kv) for t, rid, kv, _ in records(os.path.join(bdir, "block.ntw")) if t == "parcel"]
nodes, edges = {}, {}
for t, rid, kv, _ in records(os.path.join(WORLD, "districts", district, "roads.ntw")):
    if t == "node":
        nodes[rid] = (pts(kv["at"])[0], float(kv.get("z", "0")))
    elif t == "edge":
        edges[rid] = kv
plans = {}
for root, _, files in os.walk(os.path.join(WORLD, "lib", "plans")):
    for f in sorted(files):
        for t, rid, kv, _ in records(os.path.join(root, f)):
            if t == "ntw" and "id" in kv:
                pid = kv["id"]
            elif t == "meta":
                w, d = (float(v) for v in kv["size"].split("x"))
                plans[pid] = {"w": w, "d": d, "cls": kv.get("class", ""), "family": kv.get("family", ""), "floors": int(kv.get("floors", "1"))}
            elif t == "facade":
                plans[pid]["palette"] = kv.get("palette", "")
palettes = {}
for f in sorted(os.listdir(os.path.join(WORLD, "lib", "palettes"))):
    fam = None
    for t, rid, kv, _ in records(os.path.join(WORLD, "lib", "palettes", f)):
        if t == "ntw":
            fam = kv["family"]
        elif t == "palette":
            palettes.setdefault(fam, []).append("lib.palette.%s.%s" % (fam, rid))
grid = None
if GROUND:
    lines = open(GROUND).read().split("\n")
    i = next(k for k, l in enumerate(lines) if l.startswith("grid "))
    h = dict(kv.split("=") for kv in lines[i].split()[1:])
    grid = (float(h["x0"]), float(h["y0"]), float(h["step"]), int(h["nx"]), int(h["ny"]),
            [[float(v) for v in l.split()] for l in lines[i + 1:i + 1 + int(h["ny"])]])


def ground(x, y):
    x0, y0, st, nx, ny, g = grid
    fx, fy = (x - x0) / st, (y - y0) / st
    i, j = max(0, min(int(fx), nx - 2)), max(0, min(int(fy), ny - 2))
    tx, ty = fx - i, fy - j
    return (g[j][i] * (1 - tx) + g[j][i + 1] * tx) * (1 - ty) + (g[j + 1][i] * (1 - tx) + g[j + 1][i + 1] * tx) * ty


# ------------------------------------------------------------------------------------------------ geometry
def shape(e):
    return pts(e["shape"]) if "shape" in e else [nodes[e["from"]][0], nodes[e["to"]][0]]


def nearest(p, e):
    """(distance, plan distance along, length, segment direction) of point p to edge e's centreline."""
    P = shape(e)
    best, acc, total = None, 0.0, sum(math.dist(P[k], P[k + 1]) for k in range(len(P) - 1))
    for k in range(len(P) - 1):
        a, b = P[k], P[k + 1]
        L = math.dist(a, b)
        u = ((b[0] - a[0]) / L, (b[1] - a[1]) / L)
        t = max(0.0, min(L, (p[0] - a[0]) * u[0] + (p[1] - a[1]) * u[1]))
        q = (a[0] + u[0] * t, a[1] + u[1] * t)
        d = math.dist(p, q)
        if best is None or d < best[0]:
            best = (d, acc + t, total, u)
        acc += L
    return best


def sidewalk_z(p):
    """The sidewalk's height at p: the nearest street's profile (its nodes' heights along its plan length, as
    worldmodel_roads.cpp builds an edge without a profile) and the kerb's 0.15 m (roadnet.cpp roadSurfaceHeight)."""
    best = None
    for eid, e in edges.items():
        if e.get("class") == "lane":
            continue
        d, s, L, _ = nearest(p, e)
        if best is None or d < best[0]:
            z0, z1 = nodes[e["from"]][1], nodes[e["to"]][1]
            best = (d, z0 + (z1 - z0) * s / L + 0.15)
    return best[1]


def inside(poly, p):
    c = False
    for k in range(len(poly)):
        a, b = poly[k], poly[(k + 1) % len(poly)]
        if (a[1] > p[1]) != (b[1] > p[1]) and p[0] < a[0] + (p[1] - a[1]) * (b[0] - a[0]) / (b[1] - a[1]):
            c = not c
    return c


def extent(poly, u, n, o, t0, t1):
    """The parcel's span along the street (s) where a footprint t0..t1 deep fits: the overlap of its spans at
    depths t0..t1 (every 0.5 m)."""
    lo, hi = -1e9, 1e9
    t = t0
    while t <= t1 + 1e-6:
        xs = []
        for k in range(len(poly)):
            a, b = poly[k], poly[(k + 1) % len(poly)]
            ta = (a[0] - o[0]) * n[0] + (a[1] - o[1]) * n[1]
            tb = (b[0] - o[0]) * n[0] + (b[1] - o[1]) * n[1]
            if (ta - t) * (tb - t) <= 0 and ta != tb:
                f = (t - ta) / (tb - ta)
                x = (a[0] + (b[0] - a[0]) * f, a[1] + (b[1] - a[1]) * f)
                xs.append((x[0] - o[0]) * u[0] + (x[1] - o[1]) * u[1])
        if len(xs) < 2:
            return None
        lo, hi = max(lo, min(xs)), min(hi, max(xs))
        t += 0.5
    return (lo, hi) if hi > lo else None


# ------------------------------------------------------------------------------------------------ faces
rng = random.Random(SEED)
print("street.py: block %s, seed %d" % (BLOCK, SEED))
faces = {}
for pid, kv in parcels:
    faces.setdefault(kv["front"].split(":")[0], []).append((pid, kv))
placements, skipped = [], []
for eid, plist in faces.items():
    e = edges[eid]
    P = [p for _, kv in plist for p in pts(kv["poly"])]
    cx, cy = sum(p[0] for p in P) / len(P), sum(p[1] for p in P) / len(P)
    _, _, _, u = nearest((cx, cy), e)
    o = shape(e)[0]
    n = (-u[1], u[0])
    if (cx - o[0]) * n[0] + (cy - o[1]) * n[1] < 0:
        n = (-n[0], -n[1])
    yaw = math.degrees(math.atan2(-n[0], n[1])) % 360.0   # (plan +y, into the building, is (-sin yaw, cos yaw))
    xdir = (math.cos(math.radians(yaw)), math.sin(math.radians(yaw)))
    flip = xdir[0] * u[0] + xdir[1] * u[1] < 0            # (the plan's +x runs against the street's s)
    avenue = e.get("class") == "avenue"
    # the face's front line: the parcels' fronts (their nearest points to the street), the median, and the setback
    fronts = sorted(min((p[0] - o[0]) * n[0] + (p[1] - o[1]) * n[1] for p in pts(kv["poly"])) for _, kv in plist)
    tf = round(fronts[len(fronts) // 2] + SETBACK, 2)
    plist.sort(key=lambda r: min((p[0] - o[0]) * u[0] + (p[1] - o[1]) * u[1] for p in pts(r[1]["poly"])))
    name = e.get("name", eid)
    prev_house = None
    for idx, (pid, kv) in enumerate(plist):
        poly = pts(kv["poly"])
        if kv.get("use") not in ("mixed", "commercial", "residential"):
            skipped.append((pid, "use %s takes none of the street's plans" % kv.get("use")))
            continue
        # a corner: another street (not a lane) crosses the front street and runs by the parcel's first or last point,
        # on the parcel's side of the front street, within its sidewalk's reach (its half width and sidewalk, and 3 m)
        ends = {}
        for oid, oe in edges.items():
            if oid == eid or oe.get("class") == "lane" or "sidewalk" not in oe:
                continue
            lanes = max(int(v) for v in oe.get("lanes", "1/1").split("/"))
            reach = lanes * 3.3 + (2.4 if "parking" in oe else 0.0) + float(oe["sidewalk"].split(":")[-1]) + 3.0
            for k, side in ((0, "lo"), (1, "hi")):
                # (the ends of its front: its points within 3 m of the front line)
                sp = [((p[0] - o[0]) * u[0] + (p[1] - o[1]) * u[1], p) for p in poly if (p[0] - o[0]) * n[0] + (p[1] - o[1]) * n[1] < tf + 3.0]
                q = min(sp)[1] if k == 0 else max(sp)[1]
                d, _, _, ou = nearest(q, oe)
                P2 = shape(oe)
                mid = ((P2[0][0] + P2[-1][0]) / 2, (P2[0][1] + P2[-1][1]) / 2)
                if d < reach and abs(ou[0] * u[0] + ou[1] * u[1]) < 0.8 and (mid[0] - o[0]) * n[0] + (mid[1] - o[1]) * n[1] > 1.0:
                    ends[side] = oid
        corner = ("lo" in ends) if ("lo" in ends) != ("hi" in ends) else None

        def lay(tl):
            """The parcel's row with its front on the line tl: [(plan, s at its low end, mirrored)], or a reason."""
            # (its span from 0.5 m behind the line, as a parcel's front edge isn't always square to the street; at a
            # corner's end at the line itself, the corner's cut)
            span = extent(poly, u, n, o, tl + 0.5, tl + 14.0)
            front = extent(poly, u, n, o, tl, tl)
            if not span or not front:
                return "no 14 m deep footprint fits behind its front"
            span = (max(span[0], front[0]) if corner is True else span[0], min(span[1], front[1]) if corner is False else span[1])
            # (in from its sides; at a corner the building stands at the corner, and closer to its neighbour when it must)
            s0, s1 = span[0] + (0.05 if corner is True else SIDE_GAP), span[1] - (0.05 if corner is False else SIDE_GAP)
            if corner is not None and s1 - s0 < plans[CORNER]["w"] + min(plans[p]["w"] for p in STREET_WALL):
                s0, s1 = (span[0] + 0.05, span[1] - 0.15) if corner else (span[0] + 0.15, span[1] - 0.05)
            free = s1 - s0
            row = []
            if avenue or corner is not None:
                seq = []
                if corner is not None and free >= plans[CORNER]["w"]:
                    rest = free - plans[CORNER]["w"]
                    fill = [p for p in STREET_WALL if plans[p]["w"] <= rest]
                    fill = [max(fill, key=lambda p: plans[p]["w"])] if fill and avenue else []
                    seq = ([CORNER] + fill) if corner else (fill + [CORNER])
                elif avenue:
                    pairs = sorted(((a, b) for a in STREET_WALL for b in STREET_WALL if plans[a]["w"] + plans[b]["w"] <= free),
                                   key=lambda ab: -(plans[ab[0]]["w"] + plans[ab[1]]["w"]))
                    if pairs:
                        # (a pair that fills the frontage to within 4 m)
                        best = plans[pairs[0][0]]["w"] + plans[pairs[0][1]]["w"]
                        pick = [ab for ab in pairs if plans[ab[0]]["w"] + plans[ab[1]]["w"] > best - 4.01]
                        seq = list(rng.choice(pick))
                    else:
                        fits = [p for p in STREET_WALL if plans[p]["w"] <= free]
                        seq = [max(fits, key=lambda p: plans[p]["w"])] if fits else []
                if not seq:
                    return "a %.1f m frontage takes no street-front plan" % free
                # the row against the corner's end, else in the middle
                total = sum(plans[p]["w"] for p in seq)
                s = s0 if corner is True else (s1 - total if corner is False else (s0 + s1 - total) / 2)
                for k, p in enumerate(seq):
                    # the corner plan's side-street wall is its right (x = w), which lies at low s when the plan's +x
                    # runs against s: mirrored when the corner is at the other end; the others alternate
                    m = (flip != corner) if p == CORNER else k % 2 == 0
                    row.append((p, s, m))
                    s += plans[p]["w"]
            else:
                fits = [p for p in HOUSES if plans[p]["w"] + 2 * HOUSE_GAP <= free + 2 * SIDE_GAP]
                fits = [p for p in fits if p != prev_house] or fits
                if not fits:
                    return "a %.1f m frontage takes no house" % free
                p = fits[0] if len(fits) == 1 else rng.choice(fits)
                row.append((p, (s0 + s1 - plans[p]["w"]) / 2, idx % 2 == 1))
            # (every corner of every footprint inside the parcel)
            for p, s, m in row:
                for a in (0.0, plans[p]["w"]):
                    for b in (0.0, plans[p]["d"]):
                        if not inside(poly, (o[0] + u[0] * (s + a) + n[0] * (tl + b), o[1] + u[1] * (s + a) + n[1] * (tl + b))):
                            return "a footprint pokes out of it"
            return row

        # (on the face's front line, else up to 0.9 m further back where its front or a corner's cut asks for it)
        state = rng.getstate()
        for back in (0.0, 0.3, 0.6, 0.9):
            rng.setstate(state)
            got = lay(tf + back)
            if isinstance(got, list):
                break
        if not isinstance(got, list):
            rng.setstate(state)
            skipped.append((pid, lay(tf)))
            continue
        row, tl = got, tf + back
        if "--debug" in OPTS:
            print("    (%s: ends %s, front line t %.2f)" % (pid, ends, tl))
        if row and plans[row[0][0]]["cls"] == "house":
            prev_house = row[0][0]
        for p, s, m in row:
            w, d = plans[p]["w"], plans[p]["d"]
            # the plan's origin: its front-left corner (x = 0) at the low or high s end, the front on the front line
            sx = s + w if flip else s
            at = (o[0] + u[0] * sx + n[0] * tl, o[1] + u[1] * sx + n[1] * tl)
            # (the sidewalk in front: on the street's profile, and on the grid where there is one)
            fpts = [(o[0] + u[0] * (s + a) + n[0] * (tl - 1.5), o[1] + u[1] * (s + a) + n[1] * (tl - 1.5)) for a in (0.2, w / 2, w - 0.2)]
            fz = max([sidewalk_z(q) for q in fpts] + ([ground(*q) for q in fpts] if grid else []))
            z = fz + 0.01
            note = ""
            if grid:
                gmax = max(ground(o[0] + u[0] * (s + a) + n[0] * (tl + b), o[1] + u[1] * (s + a) + n[1] * (tl + b))
                           for a in [w * k / 8 for k in range(9)] for b in [d * k / 8 for k in range(9)])
                if gmax + 0.05 > z:
                    note = " (raised %.2f m over the ground under it)" % (gmax + 0.05 - z)
                    z = gmax + 0.05
            placements.append({"parcel": pid, "plan": p, "at": at, "z": math.ceil(z * 100 - 1e-6) / 100, "yaw": yaw, "mirror": m,
                               "street": name, "corner": p == CORNER, "note": note})

# ------------------------------------------------------------------------------------------------ palettes, ids
def family(p):
    pal = plans[p].get("palette", "")
    return pal.split(".")[2] if pal.count(".") >= 3 else "calle"


recent = []
for pl in placements:
    fam = palettes[family(pl["plan"])]
    avoid = {r[1] for r in recent[-1:]} | {r[1] for r in recent[-3:] if r[0] == pl["plan"]}
    choice = [c for c in fam if c not in avoid] or fam
    pl["palette"] = rng.choice(choice)
    recent.append((pl["plan"], pl["palette"]))
letter = {"shop": "s", "midrise": "m", "house": "h"}
existing = {f for f in os.listdir(bdir) if f.endswith(".ntw") and f != "block.ntw"}
for k, pl in enumerate(placements, 1):
    pl["id"] = "%s.%s%02d" % (BLOCK, letter.get(plans[pl["plan"]]["cls"], "b"), k)

# ------------------------------------------------------------------------------------------------ output
WHAT = {"lib.plan.shop.flats_a": "a shop with a flat over it", "lib.plan.midrise.walkup_b": "a three-storey walk-up",
        "lib.plan.shop.corner_c": "the corner shop with a flat over it, its windows on both streets",
        "lib.plan.house.casita_d": "a two-storey casita", "lib.plan.house.bungalow_c": "a bungalow"}
counts = {}
for pl in placements:
    counts[pl["plan"]] = counts.get(pl["plan"], 0) + 1
    fn = os.path.join(bdir, pl["id"].split(".")[2] + ".ntw")
    text = ("ntw 1 building id=%s block=%s parcel=%s\n# %s, facing %s\n%s (seed %d): a re-run rewrites this file, unless this line goes\n"
            "place plan=%s at=%.2f,%.2f z=%.2f yaw=%.1f%s\nconfig palette=%s\n") % (
        pl["id"], BLOCK, pl["parcel"], WHAT.get(pl["plan"], pl["plan"]), pl["street"], MARK, SEED, pl["plan"], pl["at"][0], pl["at"][1], pl["z"],
        pl["yaw"], " mirror=1" if pl["mirror"] else "", pl["palette"])
    print("  %s %-28s %s at %.2f,%.2f z %.2f yaw %.1f%s %s%s" % (pl["id"], pl["plan"], pl["parcel"], pl["at"][0], pl["at"][1], pl["z"], pl["yaw"],
                                                               " mirror" if pl["mirror"] else "", pl["palette"].split(".")[-1], pl["note"]))
    if WRITE:
        base = os.path.basename(fn)
        if base in existing and MARK not in open(fn, encoding="utf-8").read():
            print("    (kept %s: written by hand)" % base)
            continue
        with open(fn, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
print("placements: %d (%s)" % (len(placements), ", ".join("%s %d" % (p.split(".")[-1], c) for p, c in sorted(counts.items()))))
for pid, why in skipped:
    print("  left empty: %s: %s" % (pid, why))
