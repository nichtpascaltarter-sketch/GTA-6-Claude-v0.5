import math
def norm(v):
    l = math.sqrt(sum(x*x for x in v)); return tuple(x/l for x in v)
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def add(*vs): return tuple(sum(c) for c in zip(*vs))
def mul(v, s): return tuple(x*s for x in v)
def pile(sacks):
    NU, NV = 8, 5
    pz = [0, .12, .36, .64, .87, 1.]; pr = [.62, .95, 1., .84, .4, .09]
    worst = []
    for k, (x, y, yaw, tilt, rx, ry, h) in enumerate(sacks):
        H = (math.cos(yaw), math.sin(yaw), 0.); side = (-H[1], H[0], 0.)
        Z = norm(add(mul((0, 0, 1), math.cos(tilt)), mul(H, math.sin(tilt)))); X = norm(cross(side, Z)); Y = side
        c = (x, y, 0.)
        if k == 4: c = (x - H[0]*.25, y - H[1]*.25, .34)
        if k == 5: c = (x - H[0]*.24, y - H[1]*.24, ry*.85)
        bump, lean = 0.14 * 1.4, 0.12   # worst-case lump and lean
        rmax = 0.
        for j in range(NV + 1):
            for i in range(NU):
                a = 2*math.pi*i/NU
                kk = pr[j] * (1 + bump * math.sin(math.pi*min(pz[j]*1.1, 1)))
                p = add(c, mul(X, math.cos(a)*rx*kk + lean*pz[j]**2*h), mul(Y, math.sin(a)*ry*kk), mul(Z, pz[j]*h))
                rmax = max(rmax, math.hypot(p[0], p[1]))
        top = add(c, mul(Z, h), mul(Z, 0.03 + 0.13))   # knot and ears (0.13 long)
        rmax = max(rmax, math.hypot(top[0], top[1]) + 0.1)
        worst.append(round(rmax, 2))
    return worst
cur = [(-0.28, 0.12, 0.3, 0., 0.3, 0.26, 0.62), (0.22, 0.2, 1.9, 0., 0.28, 0.25, 0.56), (0.04, -0.24, 3.4, 0., 0.27, 0.24, 0.52),
       (0.5, -0.12, 4.6, 0., 0.22, 0.2, 0.42), (-0.05, 0.05, 0.9, 1.15, 0.25, 0.23, 0.58), (-0.55, -0.3, 2.6, 1.5, 0.21, 0.19, 0.5)]
print('current  per-sack max radius:', pile(cur), 'max', max(pile(cur)))
import sys
for k5 in [(-0.42, -0.3, 2.6, 1.5, 0.21, 0.19, 0.46), (-0.4, -0.28, 2.6, 1.5, 0.21, 0.19, 0.44), (-0.36, -0.3, 2.6, 1.5, 0.21, 0.19, 0.44), (-0.4, -0.3, 2.3, 1.5, 0.21, 0.19, 0.44)]:
    s = cur[:5] + [k5]
    w = pile(s); print(k5, w, 'max', max(w))
