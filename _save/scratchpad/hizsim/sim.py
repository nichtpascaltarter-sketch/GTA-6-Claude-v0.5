# Offline model of the SSR HiZ trace (ssrtrace.hlsli) on a synthetic wet-ground + wall scene, comparing the old
# uv * mipSize cell addressing with mip-0-unit addressing. Mirror rays from every ground half-res pixel.
import numpy as np, sys
W, H = 960, 540
HW, HH = W // 2, H // 2
fovY = np.radians(60.0); aspect = W / H; near = 0.1
pitch = np.radians(float(sys.argv[1]) if len(sys.argv) > 1 else -12.0)
camH = float(sys.argv[2]) if len(sys.argv) > 2 else 1.8
wallY = float(sys.argv[3]) if len(sys.argv) > 3 else 20.0
iters = int(sys.argv[4]) if len(sys.argv) > 4 else 32
biasK = float(sys.argv[5]) if len(sys.argv) > 5 else 0.0
f = np.array([0, np.cos(pitch), np.sin(pitch)]); u = np.array([0, -np.sin(pitch), np.cos(pitch)]); r = np.array([1.0, 0, 0])
th = np.tan(fovY / 2)
def project(P):  # P (...,3) -> uv (...,2), depth
    zf = P @ f
    xn = (P @ r) / (zf * th * aspect); yn = (P @ u) / (zf * th)
    return np.stack([xn * 0.5 + 0.5, -yn * 0.5 + 0.5], -1), near / zf, zf
def scene(d):  # ray dirs (...,3) from camera -> t, normal id (0 none, 1 ground, 2 wall)
    tg = np.where(d[..., 2] < -1e-9, -camH / np.minimum(d[..., 2], -1e-9), np.inf)
    tw = np.where(d[..., 1] > 1e-9, wallY / np.maximum(d[..., 1], 1e-9), np.inf)
    zw = tw * d[..., 2]
    tw = np.where((zw > -camH) & (zw < 10 - camH), tw, np.inf)
    t = np.minimum(tg, tw); kind = np.where(np.isinf(t), 0, np.where(tg < tw, 1, 2))
    return t, kind
# full-res depth
ys, xs = np.mgrid[0:H, 0:W]
xn = (xs + 0.5) / W * 2 - 1; yn = -((ys + 0.5) / H * 2 - 1)
D = f[None, None] + xn[..., None] * th * aspect * r[None, None] + yn[..., None] * th * u[None, None]
t, kind = scene(D)
zf = t * (D @ f)
depth = np.where(kind > 0, near / np.where(kind > 0, zf, 1), 0.0)
# HiZ pyramid (closest = max), odd-source footprint rule
def down(src):
    sh, sw = src.shape; dh, dw = max(sh // 2, 1), max(sw // 2, 1)
    out = np.zeros((dh, dw))
    for y in range(dh):
        ey = 3 if (sh & 1) and y == dh - 1 else 2
        for x in range(dw):
            ex = 3 if (sw & 1) and x == dw - 1 else 2
            out[y, x] = src[2 * y:min(2 * y + ey, sh), 2 * x:min(2 * x + ex, sw)].max()
    return out
mips = [down(depth)]
while mips[-1].shape[0] > 1 or mips[-1].shape[1] > 1:
    mips.append(down(mips[-1]))
maxMip = len(mips) - 1
mip0 = np.array([HW, HH], float)
# rays from ground half-res pixels (offset 0,0)
hy, hx = np.mgrid[0:HH, 0:HW]
px, py = hx * 2, hy * 2
g = kind[py, px] == 1
P = D[py, px] * t[py, px][..., None]
P = P[g]; pix = np.stack([hx[g], hy[g]], -1)
V = -P / np.linalg.norm(P, axis=-1, keepdims=True)
R = -V.copy(); R[:, 2] = -R[:, 2]  # reflect(-V, up)
P0 = P.copy()
P = P + np.array([0, 0, 1.0]) * (biasK * (P @ f))[:, None]
# makeScreenRay
vz = P @ f; rz = R @ f
L = np.full(len(P), 400.0)
L = np.where(rz < 0, np.minimum(L, (vz - near * 2) / np.maximum(-rz, 1e-9) * 0.98), L)
s0uv, s0d, _ = project(P); s1uv, s1d, _ = project(P + R * np.maximum(L, 0.01)[:, None])
o = np.concatenate([s0uv, s0d[:, None]], -1); d = np.concatenate([s1uv - s0uv, (s1d - s0d)[:, None]], -1)
tcl = np.ones(len(P))
for k in range(2):
    tcl = np.where(d[:, k] > 0, np.minimum(tcl, (1 - o[:, k]) / np.where(d[:, k] > 0, d[:, k], 1)), tcl)
    tcl = np.where(d[:, k] < 0, np.minimum(tcl, -o[:, k] / np.where(d[:, k] < 0, d[:, k], -1)), tcl)
tMax = np.maximum(tcl, 0)
def trace(newMap):
    n = len(P)
    invD = np.where(np.abs(d) > np.array([1e-9, 1e-9, 1e-12]), 1.0 / np.where(np.abs(d) > 1e-30, d, 1e-30), 1e30)
    fo = (d[:, :2] >= 0).astype(float)
    nudge = np.where(d[:, :2] >= 0, 1.0, -1.0) * 0.005 / mip0
    cell0 = np.floor(o[:, :2] * mip0)
    plane0 = (cell0 + fo) / mip0 + nudge
    t0 = (plane0 - o[:, :2]) * invD[:, :2]
    tt = t0.min(1)
    pos = o + d * tt[:, None]
    mip = np.zeros(n, int); active = np.ones(n, bool); it = np.zeros(n, int)
    for i in range(iters):
        active &= (mip >= 0) & (tt <= tMax)
        if not active.any(): break
        it += active
        surf = np.zeros(n); xyPlane = np.zeros((n, 2))
        for m in range(maxMip + 1):
            sel = active & (mip == m)
            if not sel.any(): continue
            arr = mips[m]; msz = np.array([arr.shape[1], arr.shape[0]], float)
            if newMap:
                ct = 2.0 ** m
                last = np.maximum(np.floor(mip0 / ct), 1) - 1
                cell = np.clip(np.floor(pos[sel, :2] * mip0 / ct), 0, last)
                lo = cell * ct / mip0; hi = np.where(cell >= last, 1.0, (cell + 1) * ct / mip0)
                xp = np.where(d[sel, :2] >= 0, hi, lo) + nudge[sel]
            else:
                cp = pos[sel, :2] * msz
                cell = np.minimum(np.floor(cp), msz - 1); cell = np.maximum(cell, 0)
                xp = (np.floor(cp) + fo[sel]) / msz + nudge[sel]
            surf[sel] = arr[cell[:, 1].astype(int), cell[:, 0].astype(int)]
            xyPlane[sel] = xp
        tp = np.concatenate([(xyPlane - o[:, :2]) * invD[:, :2], ((surf - o[:, 2]) * invD[:, 2])[:, None]], -1)
        tp[:, 2] = np.where(d[:, 2] < 0, tp[:, 2], 1e30)
        tp[:, 2] = np.where(tp[:, 2] <= tt, 1e30, tp[:, 2])
        tMin = tp.min(1)
        above = surf < pos[:, 2]
        skipped = (tMin != tp[:, 2]) & above
        tt = np.where(active & above, tMin, tt)
        pos = o + d * tt[:, None]
        mip = np.where(active, np.minimum(np.where(skipped, mip + 1, mip - 1), maxMip), mip)
    found = (mip < 0) & (tt <= tMax)
    hit = pos
    ok = found & (hit[:, 0] > 0) & (hit[:, 0] < 1) & (hit[:, 1] > 0) & (hit[:, 1] < 1)
    hp = np.minimum((hit[:, :2] * np.array([W, H])).astype(int), np.array([W - 1, H - 1]))
    hd = depth[hp[:, 1], hp[:, 0]]
    ok &= hd > 0
    zr = near / np.maximum(hit[:, 2], 1e-7); zs = near / np.maximum(hd, 1e-7)
    thick = np.maximum(0.25, zs * 0.035)
    HP = D[hp[:, 1], hp[:, 0]] * t[hp[:, 1], hp[:, 0]][:, None]
    hk = kind[hp[:, 1], hp[:, 0]]
    hN = np.where((hk == 1)[:, None], np.array([0, 0, 1.0]), np.array([0, -1.0, 0]))
    toHit = HP - P0
    front = (hN * toHit).sum(1) < 0
    acc = ok & (np.abs(zr - zs) < thick) & front & (np.linalg.norm(toHit, axis=1) > 0.05)
    conf = np.where(acc, np.clip((1 - it / iters) * 4, 0, 1), 0)
    # analytic reflection hit on the wall
    tw = np.where(R[:, 1] > 1e-9, (wallY - P0[:, 1]) / np.maximum(R[:, 1], 1e-9), np.inf)
    WP = P0 + R * np.where(np.isinf(tw), 0, tw)[:, None]
    wallOK = np.isfinite(tw) & (WP[:, 2] > -camH) & (WP[:, 2] < 10 - camH)
    wuv, _, _ = project(WP)
    onscreen = wallOK & (wuv[:, 0] > 0) & (wuv[:, 0] < 1) & (wuv[:, 1] > 0) & (wuv[:, 1] < 1)
    err = np.linalg.norm((hit[:, :2] - wuv) * np.array([W, H]), axis=1)
    return acc, conf, onscreen, err, it
for name, nm in (("old", False), ("new", True)):
    acc, conf, onscreen, err, it = trace(nm)
    good = acc & onscreen & (err < 3); bad = acc & ~(onscreen & (err < 3))
    miss = onscreen & ~acc
    print(f"{name}: wall-visible rays {onscreen.sum()}, good hits {good.sum()} ({good.sum()/max(onscreen.sum(),1)*100:.1f}%), "
          f"misses {miss.sum()}, wrong hits {bad.sum()}, mean conf (visible) {conf[onscreen].mean():.3f}, mean iters {it[onscreen].mean():.1f}")
    img = np.zeros((HH, HW, 3), np.uint8)
    img[pix[:, 1], pix[:, 0]] = np.where(good[:, None], [60, 200, 60], np.where(bad[:, None], [230, 60, 230], np.where(miss[:, None], [20, 20, 20], [90, 90, 140])))
    img[pix[:, 1], pix[:, 0], 1] = np.where(good, (40 + 215 * conf).astype(np.uint8), img[pix[:, 1], pix[:, 0], 1])
    hdr = f"P6 {HW} {HH} 255\n".encode()
    open(f"/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/hizsim/{name}.ppm", "wb").write(hdr + img.tobytes())
