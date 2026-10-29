# The street-front rules checked over the building files tools/author/street.py wrote (docs/HANDBUILT_WORLD.md section
# 5.3), independently of street.py's own draw: an authoring tool, run after a street.py batch and before its review.
#
# What it checks, per block face (a block's files that face one street from one side, as street.py writes them):
# - the rules every face keeps: no palette twice side by side and no plan with the same palette twice within three
#   placements (in the block's placement order, with the palettes as drawn: a gable casita's casita palette read as the
#   street palette of its colour, and a twin read as the gable plan it was drawn as); no three casitas alike side by
#   side; no two bungalows side by side;
# - on a face whose houses were drawn per face (their files say "per face"), the variety rules: (a) its first house from
#   the west differs from its west neighbour's first (the nearest face west of it on the same side of the street);
#   (b) its first three differ from its west neighbour's, and from those of the face across the street where that one
#   was drawn per face too; (c) no other face of the street has its whole sequence of houses;
# - with a base world (the same world before the batch): every corner plan stands where it stood (its place line
#   unchanged), and each face's casitas and bungalows are counted before and after, with the district's bungalow share.
# Houses are named by letter: G the gable casita, T its flat-roofed twin, B the bungalow (corner plans are not houses).
#
# usage: python3 -I tools/author/rulecheck.py <world dir> [<base world dir>]
# It prints the counts and one PROBLEM line per broken rule, and exits 1 if there is any.
import os
import re
import sys

LETTER = {"lib.plan.house.casita_d": "G", "lib.plan.house.casita_e": "T", "lib.plan.house.bungalow_c": "B"}
GABLE, TWIN, BUNGALOW = "lib.plan.house.casita_d", "lib.plan.house.casita_e", "lib.plan.house.bungalow_c"
CORNER = "lib.plan.shop.corner_c"
MARK = "written by tools/author/street.py"


def load(world):
    """Every building file street.py wrote, in every district: its block, file, number, plan, place and palette."""
    out = []
    root = os.path.join(world, "districts")
    for dist in sorted(os.listdir(root)):
        blocks = os.path.join(root, dist, "blocks")
        if not os.path.isdir(blocks):
            continue
        for b in sorted(os.listdir(blocks)):
            d = os.path.join(blocks, b)
            if not os.path.isdir(d):
                continue
            for f in sorted(os.listdir(d)):
                if not f.endswith(".ntw") or f == "block.ntw":
                    continue
                text = open(os.path.join(d, f), encoding="utf-8").read()
                lines = text.split("\n")
                if len(lines) < 5 or MARK not in lines[2]:
                    continue
                head = re.search(r"ntw 1 building id=(\S+)", lines[0])
                place = re.search(r"place plan=(\S+) at=([-\d.]+),([-\d.]+) z=\S+ yaw=([-\d.]+)( mirror=1)?", text)
                pal = re.search(r"config palette=(\S+)", text)
                if not head or not place or not pal:
                    continue
                out.append({"block": dist + "." + b, "file": f, "num": int(re.search(r"(\d+)$", head.group(1)).group(1)),
                            "plan": place.group(1), "x": float(place.group(2)), "yaw": float(place.group(4)),
                            "place": place.group(0), "palette": pal.group(1), "street": lines[1].split("facing ")[-1],
                            "per_face": "per face" in lines[2]})
    return out


def faces_of(blds):
    """(block, street, yaw) -> the face's buildings in placement order"""
    faces = {}
    for r in blds:
        faces.setdefault((r["block"], r["street"], round(r["yaw"]) % 360), []).append(r)
    for k in faces:
        faces[k].sort(key=lambda r: r["num"])
    return faces


def houses(face):
    return sorted((r for r in face if r["plan"] in LETTER), key=lambda r: r["x"])


def seq_of(face):
    """the face's houses from the west, as letters"""
    return "".join(LETTER[r["plan"]] for r in houses(face))


def turn(a, b):
    return abs(((a - b) + 180) % 360 - 180)


def check(blds):
    problems = []
    # the palettes, in each block's placement order
    byblock = {}
    for r in blds:
        byblock.setdefault(r["block"], []).append(r)
    for b, rs in sorted(byblock.items()):
        rs.sort(key=lambda r: r["num"])
        drawn = []
        for r in rs:
            plan = GABLE if r["plan"] == TWIN else r["plan"]
            pal = r["palette"].replace(".casita.", ".calle.") if plan == GABLE else r["palette"]
            drawn.append((plan, pal, r))
        for k, (plan, pal, r) in enumerate(drawn):
            if k >= 1 and drawn[k - 1][1] == pal:
                problems.append("%s: palette %s twice side by side (%s, %s)" % (b, pal, drawn[k - 1][2]["file"], r["file"]))
            for j in range(max(0, k - 3), k):
                if drawn[j][0] == plan and drawn[j][1] == pal:
                    problems.append("%s: plan %s with palette %s twice within three (%s, %s)" % (b, plan, pal, drawn[j][2]["file"], r["file"]))
    # side by side: neighbouring placements of a face
    faces = faces_of(blds)
    for key, face in sorted(faces.items()):
        for k in range(1, len(face)):
            a, p = face[k], face[k - 1]
            if p["num"] != a["num"] - 1:
                continue
            if a["plan"] == p["plan"] == BUNGALOW:
                problems.append("%s: two bungalows side by side (%s, %s)" % (key[0], p["file"], a["file"]))
            if k >= 2 and a["plan"] in (GABLE, TWIN) and a["plan"] == p["plan"] == face[k - 2]["plan"] and face[k - 2]["num"] == a["num"] - 2:
                problems.append("%s: three casitas alike side by side (%s and the two before it)" % (key[0], a["file"]))
    # (a), (b) and (c) on the faces drawn per face
    hfaces = {k: f for k, f in faces.items() if seq_of(f)}
    for key, face in sorted(hfaces.items()):
        if not any(r["per_face"] for r in face):
            continue
        b, street, yw = key
        mine = seq_of(face)
        xs = [r["x"] for r in houses(face)]
        lo, hi = min(xs), max(xs)
        wests = [(max(r["x"] for r in houses(f)), k) for k, f in hfaces.items()
                 if k != key and k[1] == street and turn(k[2], yw) < 10 and max(r["x"] for r in houses(f)) < lo]
        if wests:
            wk = max(wests)[1]
            w = seq_of(hfaces[wk])
            if mine[:1] == w[:1]:
                problems.append("%s (%s): (a) its first house %s is its west neighbour %s's" % (b, street, mine[:1], wk[0]))
            if mine[:3] == w[:3]:
                problems.append("%s (%s): (b) its first three %s are its west neighbour %s's" % (b, street, mine[:3], wk[0]))
        for k, f in sorted(hfaces.items()):
            if k[1] != street or turn(k[2], yw) <= 170 or not any(r["per_face"] for r in f):
                continue
            fx = [r["x"] for r in houses(f)]
            if min(fx) < hi and max(fx) > lo and seq_of(f)[:3] == mine[:3]:
                problems.append("%s (%s): (b) its first three %s are those across the street (%s)" % (b, street, mine[:3], k[0]))
        for k, f in sorted(hfaces.items()):
            if k != key and k[1] == street and seq_of(f) == mine:
                problems.append("%s (%s): (c) its sequence %s is %s's too" % (b, street, mine, k[0]))
    return problems, hfaces


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: python3 -I tools/author/rulecheck.py <world dir> [<base world dir>]")
    blds = load(sys.argv[1])
    problems, hfaces = check(blds)
    print("rulecheck: %d street.py files, %d house faces (%d drawn per face)" % (
        len(blds), len(hfaces), sum(1 for f in hfaces.values() if any(r["per_face"] for r in f))))
    if len(sys.argv) > 2:
        base = load(sys.argv[2])
        was = {(r["block"], r["file"]): r["place"] for r in base if r["plan"] == CORNER}
        now = {(r["block"], r["file"]): r["place"] for r in blds if r["plan"] == CORNER}
        for k in sorted(set(was) | set(now)):
            if was.get(k) != now.get(k):
                problems.append("%s/%s: a corner moved or changed (%s -> %s)" % (k[0], k[1], was.get(k), now.get(k)))
        print("corners: %d in the base, %d now" % (len(was), len(now)))
        bf = {k: seq_of(f) for k, f in faces_of(base).items() if seq_of(f)}
        tb = tn = th = 0
        for key in sorted(set(bf) | set(hfaces)):
            s0, s1 = bf.get(key, ""), seq_of(hfaces[key]) if key in hfaces else ""
            tb, tn, th = tb + s0.count("B"), tn + s1.count("B"), th + len(s1)
            mark = "" if s0 == s1 else "  (per face)" if any(r["per_face"] for r in hfaces.get(key, [])) else "  (changed, not per face)"
            print("  %s %-14s %-8s -> %-8s casitas %d -> %d, bungalows %d -> %d%s" % (
                key[0], key[1], s0, s1, len(s0) - s0.count("B"), len(s1) - s1.count("B"), s0.count("B"), s1.count("B"), mark))
        if th:
            print("bungalows: %d -> %d of %d houses (%.1f%% -> %.1f%%)" % (tb, tn, th, 100.0 * tb / th, 100.0 * tn / th))
    for p in problems:
        print("PROBLEM " + p)
    print("rulecheck: %d problem(s)" % len(problems))
    sys.exit(1 if problems else 0)


main()
