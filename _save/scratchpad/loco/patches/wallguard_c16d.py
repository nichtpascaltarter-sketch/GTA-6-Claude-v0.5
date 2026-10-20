#!/usr/bin/env python3
# wallguard.py <animator.cpp> <character.h> [...more character.h]: on c14 (d30f47a5) - a swing's landing with no ground
# within a stair's reach under its toes (a wall's footprint, a drop: the game's probe at its clamp, the body not going
# down stairs, no tread fitted there) is pulled in, its toes to within 0.25 m of where the body will be then (the game's
# body keeps 0.3 m off walls), and that probe is not taken as the ground (the foot used to sink 30-45 cm towards it).
# The switchback's 180-degree turn at a run: the outer foot came down in the outer wall.
# Env (debug trees only, via WG_DEBUG define): none - this is the release patch.
import sys
def sub(s, old, new, n=1):
    c = s.count(old)
    if c != n: raise SystemExit('expected %d found %d: %r' % (n, c, old[:100]))
    return s.replace(old, new)
a = sys.argv[1]
s = open(a).read()
# the pull, kept with the world
s = sub(s, """        A.stairDir[s] = rotate(qBack, A.stairDir[s]);
""", """        A.stairDir[s] = rotate(qBack, A.stairDir[s]);
        A.wallPull[s] = rotate(qBack, A.wallPull[s]);
""")
# the probes: one at the clamp below the root with the body not going down stairs and no tread fitted there is not the
# ground (a wall's footprint, a drop); a swing's landing probe so has its landing pulled in
s = sub(s, """            if (A.probeAhead[s]) {
                A.groundAhead[s] += (gIn[s] - A.groundAhead[s]) * ks;
                *fz[s] -= dzRoot;
            } else if (clamped[s]) {
                *fz[s] -= dzRoot;
            } else {""", """            // (a probe at the game's clamp below the root, within 0.5 m of it and no tread fitted there - a drop steeper
            // than any stair: a wall's footprint, a ledge - is not the ground; a swing's landing probed so is pulled in, below)
            const bool none = terrain && in.footProbes && gIn[s] < -0.42f && A.stairLand[s] < -8.f && length(vec2(A.probeP[s].x, A.probeP[s].y)) < 0.5f;
            if (A.probeAhead[s]) {
                if (none) A.wallSeen[s] = true;
                else A.groundAhead[s] += (gIn[s] - A.groundAhead[s]) * ks;
                *fz[s] -= dzRoot;
            } else if (clamped[s] || none) {
                *fz[s] -= dzRoot;
            } else {""")
# cleared with the fit
s = sub(s, """        if (A.stepT[s] >= 0.f || A.planted[s] || A.footHold[s]) {
            A.stairShift[s] = 0.f;
            A.stairLand[s] = -9.f;
            A.stairBall[s] = false;
            A.scanDone[s] = false;
        }""", """        if (A.stepT[s] >= 0.f || A.planted[s] || A.footHold[s]) {
            A.stairShift[s] = 0.f;
            A.stairLand[s] = -9.f;
            A.stairBall[s] = false;
            A.scanDone[s] = false;
            A.wallPull[s] = vec3(0);
            A.wallSeen[s] = false;
        }""")
# the swing goes onto it (as onto a fitted tread: quicker late in the swing)
s = sub(s, """                const float tLand = A.stairLand[s] > -8.f && swingU[s] > 0.f && cycle > 0.f ? (1.f - swingU[s]) * (1.f - duty) * cycle / Max(spd, 0.3f) : 1.f;
                float k = expf(-dt / Clamp(0.35f * tLand, 0.025f, 0.07f));
                const vec3 to = A.stairDir[s] * A.stairShift[s];""", """                const bool fitted = A.stairLand[s] > -8.f || length2(A.wallPull[s]) > 1e-6f;
                const float tLand = fitted && swingU[s] > 0.f && cycle > 0.f ? (1.f - swingU[s]) * (1.f - duty) * cycle / Max(spd, 0.3f) : 1.f;
                float k = expf(-dt / Clamp(0.35f * tLand, 0.025f, 0.07f));
                const vec3 to = A.stairDir[s] * A.stairShift[s] + A.wallPull[s];""")
# the pull: where the toes will land (the probe's point) to within 0.25 m of where the body will be then
s = sub(s, """        vec3 m = pa + A.plantCorr[s] * w + rotate(qz(A.corrYaw[s] * w), mid - pa);
        const bool swinging = swingU[s] > 0.f && cycle > 0.f && !A.planted[s] && !A.footHold[s] && A.stepT[s] < 0.f;""", """        vec3 m = pa + A.plantCorr[s] * w + rotate(qz(A.corrYaw[s] * w), mid - pa);
        const bool swinging = swingU[s] > 0.f && cycle > 0.f && !A.planted[s] && !A.footHold[s] && A.stepT[s] < 0.f;
        // (a swing's landing probed with no ground within a stair's reach under its toes - a wall's footprint, a drop: the
        // switchback's turn at a run put the outer foot in the outer wall - pulled in, its toes to within 0.25 m of where
        // the body will be when it lands (the body keeps 0.3 m off a wall), at most 0.6 m in all; kept till it lands)
        const vec3 mNoPull = m - A.wallPull[s] * w;
        if (A.wallSeen[s] && swinging) {
            const float sT = Clamp(cycle * (1.f - swingU[s]) * (1.f - duty) * A.rootRatio, 0.f, 1.2f);
            const vec3 toes = landAhead(mNoPull, sT, leadM + A.stairShift[s] + 0.65f * L) + A.wallPull[s], at = landAhead(vec3(0.f), sT, 0.f);
            const vec2 dv(toes.x - at.x, toes.y - at.y);
            const float dl = length(dv), rs = 0.25f * scale;
            if (dl > rs) {
                vec3 np = A.wallPull[s] + vec3(dv.x, dv.y, 0.f) * (rs / dl - 1.f);
                const float nl = length(np);
                if (nl > 0.6f * scale) np = np * (0.6f * scale / nl);
                A.wallPull[s] = np;
            }
        }
        A.wallSeen[s] = false;""")
s = sub(s, """        if (A.probeAhead[s]) {
            const float sT = Clamp(cycle * (swinging ? (1.f - swingU[s]) * (1.f - duty) : 1.f - phP) * A.rootRatio, 0.f, 1.2f);
            m = landAhead(m, sT, leadM + A.stairShift[s] + 0.65f * L);
        }""", """        if (A.probeAhead[s]) {
            const float sT = Clamp(cycle * (swinging ? (1.f - swingU[s]) * (1.f - duty) : 1.f - phP) * A.rootRatio, 0.f, 1.2f);
            m = landAhead(mNoPull, sT, leadM + A.stairShift[s] + 0.65f * L) + A.wallPull[s];
        }""")
# the scan along the landing: the pulled one
s = sub(s, """            vec3 lm = pa + A.plantCorr[s] * w + rotate(qz(A.corrYaw[s] * w), mid - pa);
            const float sT = Clamp(cycle * (1.f - swingU[s]) * (1.f - duty) * A.rootRatio, 0.f, 1.2f);
            lm = landAhead(lm, sT, leadM);""", """            vec3 lm = pa + (A.plantCorr[s] - A.wallPull[s]) * w + rotate(qz(A.corrYaw[s] * w), mid - pa);
            const float sT = Clamp(cycle * (1.f - swingU[s]) * (1.f - duty) * A.rootRatio, 0.f, 1.2f);
            lm = landAhead(lm, sT, leadM) + A.wallPull[s];""")
# resets: the LOD path and reset()
s = sub(s, """                planted[s] = plantedPrev[s] = footHold[s] = probeAhead[s] = scanDone[s] = false;
                stairShift[s] = 0.f;
                stairLand[s] = -9.f;
                stairRear[s] = 9.f;""", """                planted[s] = plantedPrev[s] = footHold[s] = probeAhead[s] = scanDone[s] = false;
                stairShift[s] = 0.f;
                stairLand[s] = -9.f;
                stairRear[s] = 9.f;
                wallPull[s] = vec3(0);
                wallSeen[s] = false;""")
s = sub(s, """        stairLand[k] = -9.f;
        stairRear[k] = 9.f;
        stairBall[k] = ballProbe[k] = false;
        scanDone[k] = false;""", """        stairLand[k] = -9.f;
        stairRear[k] = 9.f;
        stairBall[k] = ballProbe[k] = false;
        scanDone[k] = false;
        wallPull[k] = vec3(0);
        wallSeen[k] = false;""")
# the scan: a sample over a drop steeper than any stair is no ground - the foot keeps off it, heel to toe tip
s = sub(s, """        const float gFoot = g[Clamp((int)floorf((uHeel + L) / kGroundScanStep + 0.5f), 0, kGroundScan - 1)];   // (where it was to land)""",
"""        // (past a drop of 40 cm or more between two samples 5 cm apart - no stair has one: a wall's footprint, a ledge -
        // is no ground while it stays that low: the foot keeps off it, heel to toe tip. Either way along the scan)
        bool cliff[kGroundScan] = {}, anyCliff = false;
        for (int k = 1; k < kGroundScan; k++)
            if (g[k] < g[k - 1] - 0.4f)
                for (int j = k; j < kGroundScan && g[j] < g[k - 1] - 0.4f; j++) cliff[j] = anyCliff = true;
            else if (g[k - 1] < g[k] - 0.4f)
                for (int j = k - 1; j >= 0 && g[j] < g[k] - 0.4f; j--) cliff[j] = anyCliff = true;
        int kFoot = Clamp((int)floorf((uHeel + L) / kGroundScanStep + 0.5f), 0, kGroundScan - 1);
        for (int dk = 1; cliff[kFoot] && dk < kGroundScan; dk++) {   // (where it was to land is no ground: the nearest that is, the body's side first)
            if (kFoot - dk >= 0 && !cliff[kFoot - dk]) kFoot -= dk;
            else if (kFoot + dk < kGroundScan && !cliff[kFoot + dk]) kFoot += dk;
        }
        const float gFoot = g[kFoot];   // (where it was to land)""")
s = sub(s, """            const bool upAhead = frontEdge && g[k1 + 1] > g[k1], upBehind = rearEdge && g[k0 - 1] > g[k0];
            if (frontEdge && fabsf(g[k1 + 1] - g[k1]) > 0.06f && fabsf(g[k1 + 1] - g[k1]) < 0.4f) stepped = true;
            float lo = -1e9f, hi = 1e9f;""", """            const bool upAhead = frontEdge && g[k1 + 1] > g[k1], upBehind = rearEdge && g[k0 - 1] > g[k0];
            if (frontEdge && fabsf(g[k1 + 1] - g[k1]) > 0.06f && fabsf(g[k1 + 1] - g[k1]) < 0.4f) stepped = true;
            // (no ground there: not a landing; next to it the foot keeps off it - toe tip short of it ahead, heel past it behind)
            bool runCliff = false;
            for (int k = k0; k <= k1; k++) runCliff = runCliff || cliff[k];
            if (runCliff) continue;
            const bool cliffAhead = frontEdge && cliff[k1 + 1], cliffBehind = rearEdge && cliff[k0 - 1];
            float lo = -1e9f, hi = 1e9f;""")
s = sub(s, """            if (upAhead) hi = f - mg - footLen - 0.055f * scale - 0.5f * Max(in.speed, 0.f) * dt;
            else if (frontEdge) hi = f - mg - L;
            if (upBehind) lo = r + mg + 0.04f * scale;
            else if (rearEdge) lo = r + mg - 0.7f * L;   // (on the forefoot: the ground under it is probed under the ball)""",
"""            if (upAhead) hi = f - mg - footLen - 0.055f * scale - 0.5f * Max(in.speed, 0.f) * dt;
            else if (cliffAhead) hi = f - mg - footLen;
            else if (frontEdge) hi = f - mg - L;
            if (upBehind) lo = r + mg + 0.04f * scale;
            else if (cliffBehind) lo = r + mg;
            else if (rearEdge) lo = r + mg - 0.7f * L;   // (on the forefoot: the ground under it is probed under the ball)""")
s = sub(s, """            if (cost < bestCost && fabsf(uh - uHeel) <= 0.2f * scale && fabsf(gl - gFoot) < 0.4f) {
                bestCost = cost;
                bestShift = uh - uHeel;
                bestG = gl;
                bestBall = rearEdge && !upBehind && uh < r - 0.05f;   // (the heel well over the nose)
                bestRear = rearEdge && !upBehind ? uh - r : 9.f;      // (how far ahead of a nose lower behind)
            }
        }
        stepped = stepped && bestCost < 1e8f;""", """            if (cost < bestCost && fabsf(uh - uHeel) <= (anyCliff ? 0.45f : 0.2f) * scale && fabsf(gl - gFoot) < 0.4f) {
                bestCost = cost;
                bestShift = uh - uHeel;
                bestG = gl;
                bestBall = rearEdge && !upBehind && !cliffBehind && uh < r - 0.05f;   // (the heel well over the nose)
                bestRear = rearEdge && !upBehind && !cliffBehind ? uh - r : 9.f;      // (how far ahead of a nose lower behind)
            }
        }
        stepped = (stepped || anyCliff) && bestCost < 1e8f;""")
open(a, 'w').write(s)
for h in sys.argv[2:]:
    t = open(h).read()
    t = sub(t, """    float strikeLeadRun = 0.3f;               // the same at a jog or a run, in m (learned there: about 0.3 m at any run pace)
""", """    float strikeLeadRun = 0.3f;               // the same at a jog or a run, in m (learned there: about 0.3 m at any run pace)
    vec3 wallPull[2] = {vec3(0), vec3(0)};    // a swing's landing pulled in off a wall or a drop - no ground within a stair's reach under its toes (kept with the world)
    bool wallSeen[2] = {false, false};        // its last landing probe found none there
""")
    open(h, 'w').write(t)
print('wallguard applied')
