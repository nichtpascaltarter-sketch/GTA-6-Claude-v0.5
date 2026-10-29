#!/usr/bin/env python3
"""Buildings census diff: bdiff.py <census a> <census b> -> changed / removed / added B records, clustered by place
(within 120 m of each other), with heights, styles and the clusters' boxes."""
import sys, math
from collections import Counter

STYLE = {0: "tower", 1: "midrise", 2: "condo", 3: "deco", 4: "shops", 5: "strip", 6: "house", 7: "villa", 8: "warehouse", 9: "factory", 10: "farmhouse", 11: "barn", 12: "motel", 13: "gas station", 14: "garage", 15: "church", 16: "shack"}


def load(path):
    out = {}
    order = []
    for line in open(path, errors="replace"):
        t = line.split()
        if not t or t[0] != "B":
            continue
        key = (t[1], t[2])
        rec = {"x": float(t[1]), "y": float(t[2]), "w": t[3], "d": t[4], "ax": (t[6], t[7]), "style": int(t[9]),
               "h": float(t[11]), "seed": t[13], "line": line.strip()}
        out.setdefault(key, []).append(rec)
        order.append(key)
    return out, order


A, oa = load(sys.argv[1])
B, ob = load(sys.argv[2])
changed, removed, added = [], [], []
for k, ra in A.items():
    rb = B.get(k)
    if not rb:
        removed += ra
        continue
    la = sorted(r["line"] for r in ra)
    lb = sorted(r["line"] for r in rb)
    if la != lb:
        for x, y in zip(sorted(ra, key=lambda r: r["line"]), sorted(rb, key=lambda r: r["line"])):
            if x["line"] != y["line"]:
                changed.append((x, y))
for k, rb in B.items():
    if k not in A:
        added += rb
print(f"buildings: {sum(len(v) for v in A.values())} -> {sum(len(v) for v in B.values())}; changed {len(changed)}, "
      f"removed {len(removed)}, added {len(added)}")
# order: same records, different order?
sa = [k for k in oa if k in B]
sb = [k for k in ob if k in A]
print(f"records in both, same order: {sa == sb}")

items = [("changed", c[0]["x"], c[0]["y"], c) for c in changed] + [("removed", r["x"], r["y"], r) for r in removed] + \
        [("added", r["x"], r["y"], r) for r in added]
clusters = []
for it in items:
    for c in clusters:
        if any(math.hypot(it[1] - o[1], it[2] - o[2]) < 120 for o in c):
            c.append(it)
            break
    else:
        clusters.append([it])
# merge clusters that touch
merged = True
while merged:
    merged = False
    for i in range(len(clusters)):
        for j in range(i + 1, len(clusters)):
            if any(math.hypot(a[1] - b[1], a[2] - b[2]) < 120 for a in clusters[i] for b in clusters[j]):
                clusters[i] += clusters[j]
                del clusters[j]
                merged = True
                break
        if merged:
            break
for c in sorted(clusters, key=lambda c: (min(i[1] for i in c), min(i[2] for i in c))):
    xs = [i[1] for i in c]
    ys = [i[2] for i in c]
    cnt = Counter(i[0] for i in c)
    print(f"-- around {sum(xs) / len(xs):.0f},{sum(ys) / len(ys):.0f} (box {min(xs):.0f}..{max(xs):.0f}, {min(ys):.0f}..{max(ys):.0f}): "
          + ", ".join(f"{n} {k}" for k, n in sorted(cnt.items())))
    for kind, x, y, r in sorted(c, key=lambda i: (i[0], i[1], i[2])):
        if kind == "changed":
            a, b = r
            diffs = []
            if a["h"] != b["h"]:
                diffs.append(f"h {a['h']} -> {b['h']}")
            if a["style"] != b["style"]:
                diffs.append(f"style {STYLE.get(a['style'])} -> {STYLE.get(b['style'])}")
            if (a["w"], a["d"]) != (b["w"], b["d"]):
                diffs.append(f"size {a['w']}x{a['d']} -> {b['w']}x{b['d']}")
            if a["seed"] != b["seed"]:
                diffs.append("seed")
            if a["ax"] != b["ax"]:
                diffs.append("axis")
            print(f"   changed {x:.1f},{y:.1f} {STYLE.get(a['style'])} h {a['h']}: {'; '.join(diffs)}")
        else:
            print(f"   {kind} {x:.1f},{y:.1f} {STYLE.get(r['style'])} {r['w']}x{r['d']} h {r['h']}")
