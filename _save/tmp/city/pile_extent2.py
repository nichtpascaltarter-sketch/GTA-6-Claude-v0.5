import math
exec(open('/tmp/city/pile_extent.py').read().split("cur = [")[0])
def pile2(sacks):
    # sacks: (x, y, yaw, tilt, rx, ry, h, pose)
    out = []
    for (x, y, yaw, tilt, rx, ry, h, pose) in sacks:
        H = (math.cos(yaw), math.sin(yaw), 0.); side = (-H[1], H[0], 0.)
        Z = norm(add(mul((0, 0, 1), math.cos(tilt)), mul(H, math.sin(tilt)))); X = norm(cross(side, Z)); Y = side
        c = (x, y, 0.)
        if pose == 1: c = (x - H[0]*.25, y - H[1]*.25, .34)
        if pose == 2: c = (x - H[0]*.24, y - H[1]*.24, ry*.85)
        pz = [0, .12, .36, .64, .87, 1.]; pr = [.62, .95, 1., .84, .4, .09]
        rmax = 0.
        for j in range(6):
            for i in range(8):
                a = 2*math.pi*i/8
                kk = pr[j] * (1 + 0.196 * math.sin(math.pi*min(pz[j]*1.1, 1)))
                p = add(c, mul(X, math.cos(a)*rx*kk + 0.12*pz[j]**2*h), mul(Y, math.sin(a)*ry*kk), mul(Z, pz[j]*h))
                rmax = max(rmax, math.hypot(p[0], p[1]))
        top = add(c, mul(Z, h), mul(Z, 0.16))
        rmax = max(rmax, math.hypot(top[0], top[1]) + 0.1)
        out.append(round(rmax, 2))
    return out
p1 = [(-0.2, 0.08, 1.2, 0., 0.29, 0.26, 0.6, 0), (0.24, 0.14, 2.7, 0., 0.24, 0.22, 0.48, 0), (0.06, -0.22, 4.2, 0., 0.26, 0.23, 0.54, 0), (0.38, -0.32, 0.6, 1.5, 0.2, 0.18, 0.44, 2)]
w = pile2(p1); print('pile1', w, 'max', max(w))
p0 = [(-0.28, 0.12, 0.3, 0., 0.3, 0.26, 0.62, 0), (0.22, 0.2, 1.9, 0., 0.28, 0.25, 0.56, 0), (0.04, -0.24, 3.4, 0., 0.27, 0.24, 0.52, 0), (0.5, -0.12, 4.6, 0., 0.22, 0.2, 0.42, 0), (-0.05, 0.05, 0.9, 1.15, 0.25, 0.23, 0.58, 1), (-0.4, -0.3, 2.3, 1.5, 0.21, 0.19, 0.44, 2)]
w = pile2(p0); print('pile0', w, 'max', max(w))
for k3 in [(0.34, -0.3, 0.6, 1.5, 0.2, 0.18, 0.42, 2), (0.32, -0.32, 0.4, 1.5, 0.2, 0.18, 0.44, 2), (0.36, -0.28, 0.8, 1.5, 0.2, 0.18, 0.42, 2)]:
    w = pile2(p1[:3] + [k3]); print(k3, w, 'max', max(w))
print('--- lying along the front')
for k3 in [(0.14, -0.5, 0.0, 1.5, 0.2, 0.18, 0.42, 2), (0.1, -0.48, 0.2, 1.5, 0.2, 0.18, 0.42, 2), (0.08, -0.46, -0.15, 1.5, 0.2, 0.18, 0.4, 2)]:
    w = pile2(p1[:3] + [k3]); print(k3, w, 'max', max(w))
