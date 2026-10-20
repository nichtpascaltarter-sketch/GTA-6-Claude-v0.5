# An independent port of src/world/roomlight.h's roomLight(), reading the table packed the way the HLSL copy may hold
# it: proves the spec ports, and that the packed table is right, against the test vectors (C++ results).
# usage: python3 port.py roomlight.h vectors.txt out_table.hlsl
import re
import sys

M = 0xFFFFFFFF
src = open(sys.argv[1]).read()
a = src.index("static const RoomInterval kTable")
b = src.index("};", a)
rows = []
for line in src[a:b].split("\n"):
    l = line.strip()
    if l.startswith("//"):
        continue
    for m in re.finditer(r"\{(\d+), (\d+), (\d+), (\d+), (\d+), (\d+)\}", l):
        rows.append(tuple(int(x) for x in m.groups()))
assert len(rows) == 13 * 2 * 3, len(rows)
names = re.findall(r"^\s+(RS_[A-Z_]+)(?: = 0)?,", src[src.index("enum RoomSchedule"):src.index("RS_COUNT")], re.M)
assert len(names) == 13, names
packed = []
for (st, en, ch, lv, ji, da) in rows:
    assert st < 65536 and en < 65536 and max(ch, lv, ji, da) < 256
    packed.append((st | (en << 16), ch | (lv << 8) | (ji << 16) | (da << 24)))


def rh(x):
    x &= M
    x ^= x >> 16
    x = (x * 0x7FEB352D) & M
    x ^= x >> 15
    x = (x * 0x846CA68B) & M
    x ^= x >> 16
    return x


def daylit(s):
    return 1 if (2 <= s <= 5) or s in (10, 11, 12) else 0


def light(schedule, seed, windows, day, minute, night):
    rs = rh(((seed & 4095) * 0x9E3779B9 + 0x632BE5AB) & M)
    if windows != 0 and daylit(schedule) and night < 48 + ((rs >> 26) & 63):
        return 0
    dimmer = 191 + ((rs >> 20) & 63)
    sc = schedule if schedule < 13 else 1
    best = 0
    for back in (0, 1):
        if back == 1 and day == 0:
            continue
        d = (day - back) & M
        base = (sc * 2 + (1 if d % 7 >= 5 else 0)) * 3
        m = minute + back * 1440
        for k in range(3):
            x, y = packed[base + k]
            start, end = x & 0xFFFF, x >> 16
            chance, level, jitter, daily = y & 255, (y >> 8) & 255, (y >> 16) & 255, y >> 24
            if chance == 0:
                continue
            h = rh(rs ^ rh((((d * 0x632BE5AB) & M if daily else 0x51ED270B) + k * 0x2545F491 + 7) & M))
            if (h & 255) > chance:
                continue
            span = 2 * jitter + 1
            s0, s1 = start + (h >> 8) % span, end + (h >> 16) % span
            if m + jitter < s0 or m + jitter >= s1:
                continue
            best = max(best, level * dimmer // 255)
    return best


vecs = [tuple(int(x) for x in m.groups()) for m in re.finditer(r"\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\}", open(sys.argv[2]).read())]
bad = [v for v in vecs if light(*v[:6]) != v[6]]
print("port: %d vectors, %d differ" % (len(vecs), len(bad)))
for v in bad[:5]:
    print("  ", v, "->", light(*v[:6]))
lit_ = sum(1 for v in vecs if v[6] > 0)
print("port: %d of the vectors are lit" % lit_)

with open(sys.argv[3], "w") as f:
    f.write("// src/world/roomlight.h's kTable, packed for the HLSL copy of roomLight(). Generated from the header by an independent\n")
    f.write("// port that matches all %d test vectors. Index: (schedule * 2 + weekend) * 3 + k (weekend: day %% 7 >= 5).\n" % len(vecs))
    f.write("//   x = start | end << 16;  y = chance | level << 8 | jitter << 16 | daily << 24\n")
    f.write("static const uint2 kRoomIntervals[%d] = {\n" % len(packed))
    for s in range(13):
        for wk in range(2):
            items = ", ".join("uint2(0x%08xu, 0x%08xu)" % packed[(s * 2 + wk) * 3 + k] for k in range(3))
            f.write("    %s,   // %s, %s\n" % (items, names[s], "weekends" if wk else "weekdays"))
    f.write("};\n")
print("wrote", sys.argv[3])
swept = sweptBad = sweptLit = 0
if len(sys.argv) > 4:   # a sweep from rlsweep.cpp: schedule seed windows day minute night level per line
    for line in open(sys.argv[4]):
        v = [int(t) for t in line.split()]
        swept += 1
        sweptLit += v[6] > 0
        if light(*v[:6]) != v[6]:
            sweptBad += 1
            if sweptBad <= 5:
                print("  sweep", v, "->", light(*v[:6]))
    print("port: sweep of %d inputs (%d lit), %d differ" % (swept, sweptLit, sweptBad))
sys.exit(1 if bad or len(vecs) != 104 or sweptBad or (len(sys.argv) > 4 and swept < 100000) else 0)
