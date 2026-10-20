#!/usr/bin/env python3
# turnflight.py <animator.cpp in> <animator.cpp out>   (on c13 = runlead.py's output, animator 1a3b7124; no header change)
# Turning onto a flight from a landing:
#  - a swing re-scanned late (past mid-swing, its landing already fitted to a tread) keeps that tread unless another
#    needs much less of a move: the correction cannot carry the foot to a tread fitted that late, and the plant would
#    trust it anyway (the toes ended up in the riser, or the heel short of the nose)
#  - the stairs stay seen for about 1.8 m of the way walked (it was 1.5 s): a slow walk across a landing (0.5 m/s, the
#    AI's stair pace) took longer than that, and its swing onto the next flight then neither scanned nor fitted its landing
import sys

def sub(s, old, new, count=1):
    n = s.count(old)
    if n != count:
        raise SystemExit('expected %d, found %d: %r' % (count, n, old[:120]))
    return s.replace(old, new)

src, dst = sys.argv[1:3]
s = open(src).read()

s = sub(s, """    if (walking) A.stairSeen += dt;   // (standing on stairs, they stay seen: its steps land on treads too)""",
        """    // (seen for about 1.8 m of the way walked - at a slow walk across a landing as at a walk; standing on stairs, they stay
    // seen: its steps land on treads too)
    if (walking) A.stairSeen += dt * Clamp(Max(in.speed, 0.f) / 1.2f, 0.4f, 1.f);""")

s = sub(s, """        bool stepped = false, bestBall = false;
        float bestCost = 1e9f, bestShift = 0.f, bestG = -9.f, bestRear = 9.f;
        for (int k0 = 0, k1; k0 < kGroundScan; k0 = k1 + 1) {""", """        bool stepped = false, bestBall = false;
        float bestCost = 1e9f, bestShift = 0.f, bestG = -9.f, bestRear = 9.f;
        // (re-scanned past mid-swing with a tread already fitted: that tread unless another needs much less of a move - a
        // fit that late cannot be reached)
        const float keepLand = A.scanDone[s] && swingU[s] > 0.5f ? A.stairLand[s] : -9.f;
        for (int k0 = 0, k1; k0 < kGroundScan; k0 = k1 + 1) {""")
s = sub(s, """            const float uh = lo <= hi ? Clamp(uHeel, lo, hi) : (upAhead ? hi : (upBehind && frontEdge ? 0.5f * (lo + hi) : lo)),
                        cost = fabsf(uh - uHeel) + (lo <= hi ? 0.f : 0.1f);
            const float gl = g[Clamp((int)floorf((uh + L) / kGroundScanStep + 0.5f), k0, k1)];
            if (cost < bestCost && fabsf(uh - uHeel) <= 0.2f * scale && fabsf(gl - gFoot) < 0.4f) {
                bestCost = cost;
                bestShift = uh - uHeel;
                bestG = gl;
                bestBall = rearEdge && !upBehind && uh < r - 0.05f;   // (the heel well over the nose)""",
        """            const float uh = lo <= hi ? Clamp(uHeel, lo, hi) : (upAhead ? hi : (upBehind && frontEdge ? 0.5f * (lo + hi) : lo));
            const float gl = g[Clamp((int)floorf((uh + L) / kGroundScanStep + 0.5f), k0, k1)];
            const float cost = fabsf(uh - uHeel) + (lo <= hi ? 0.f : 0.1f) + (keepLand > -8.f && fabsf(gl - keepLand) > 0.03f ? 0.15f : 0.f);
            if (cost < bestCost && fabsf(uh - uHeel) <= 0.2f * scale && fabsf(gl - gFoot) < 0.4f) {
                bestCost = cost;
                bestShift = uh - uHeel;
                bestG = gl;
                bestBall = rearEdge && !upBehind && uh < r - 0.05f;   // (the heel well over the nose)""")

open(dst, 'w').write(s)
