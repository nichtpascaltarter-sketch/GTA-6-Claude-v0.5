import sys
# standing on stairs: each foot stays on its tread. A step on stairs (quick, standing, or a foot coming down while
# standing) scans the way along the foot where it lands, early in the step, and keeps that scan with the world; its
# landing is fitted onto one tread as a swing's is (toes off a riser ahead, heel off a riser behind, ball on the tread)
# and it comes down at that tread's height. A standing step that would only land the foot back where it stands is
# skipped; a foot that comes down while standing on stairs does so as a short fitted step; the stair state is kept
# while standing (stairSeen only runs while walking).
# usage: stance.py <animator.cpp on qsdrag2> <character.h>
fn, fh = sys.argv[1], sys.argv[2]
s = open(fn).read()
def rep(old, new, cnt=1):
    global s
    if s.count(old) != cnt: sys.exit('count %d for: %s' % (s.count(old), old[:90]))
    s = s.replace(old, new)
# a held foot's probe at the game's clamp measures nothing (the root's vertical move came out short, and the heights
# kept with the world - a step's fitted landing among them - drifted): it is kept with the world as the rest is
rep("""        bool held[2] = {false, false};   // probed under a foot planted for the last two updates
        int n = 0;
        if (terrain && in.footProbes) {
            for (int s = 0; s < 2; s++)
                if (A.planted[s] && A.plantedPrev[s] && A.stepT[s] < 0.f) {
                    held[s] = true;""",
"""        bool held[2] = {false, false};   // probed under a foot planted for the last two updates
        bool clamped[2] = {false, false};   // held, but probed at the game's clamp
        int n = 0;
        if (terrain && in.footProbes) {
            for (int s = 0; s < 2; s++)
                if (A.planted[s] && A.plantedPrev[s] && A.stepT[s] < 0.f) {
                    // (a probe at the game's clamp, 0.45 m off the root - a foot two risers or more from the root's tread -
                    // measures nothing: that foot is kept with the world, as the rest is)
                    if (fabsf(gIn[s]) > 0.44f || fabsf(A.groundRaw[s]) > 0.44f) {
                        clamped[s] = true;
                        continue;
                    }
                    held[s] = true;""")
rep("""            if (A.probeAhead[s]) {
                A.groundAhead[s] += (gIn[s] - A.groundAhead[s]) * ks;
                *fz[s] -= dzRoot;
            } else {""",
"""            if (A.probeAhead[s]) {
                A.groundAhead[s] += (gIn[s] - A.groundAhead[s]) * ks;
                *fz[s] -= dzRoot;
            } else if (clamped[s]) {
                *fz[s] -= dzRoot;
            } else {""")
# heights kept with the world
rep("""            if (A.stairLand[s] > -8.f) A.stairLand[s] -= dzRoot;
            if (A.probeAhead[s]) {""",
"""            if (A.stairLand[s] > -8.f) A.stairLand[s] -= dzRoot;
            if (A.stepScanHave[s])
                for (int k = 0; k < kGroundScan; k++) A.stepG[s][k] -= dzRoot;
            if (A.stepFitLand[s] > -8.f) A.stepFitLand[s] -= dzRoot;
            if (A.probeAhead[s]) {""")
rep("""                // (a quick step on stairs - its ground probed where it lands - goes over the higher of that and the ground it
                // left until late in the step, its trailing toe or heel past the edge, then down onto it: not through a nose)
                const bool qs = A.stepT[s] >= 0.f && A.stairSeen < 1.5f;
                const float gi = qs && A.stepT[s] < 0.85f ? Max(gIn[s], *fz[s] - dzRoot) : gIn[s];""",
"""                // (a quick step on stairs - its ground probed where it lands, or the tread its landing is fitted to - goes over
                // the higher of that and the ground it left until late in the step, its trailing toe or heel past the edge,
                // then down onto it: not through a nose)
                const bool qs = A.stepT[s] >= 0.f && A.stairSeen < 1.5f;
                const float gl = qs && A.stepFitLand[s] > -8.f ? A.stepFitLand[s] : gIn[s];
                const float gi = qs && A.stepT[s] < 0.85f ? Max(gl, *fz[s] - dzRoot) : gl;""")
# the step's scan kept with the world
rep("""        A.scanLand[s] = rotate(qBack, A.scanLand[s]) - d;
""",
"""        A.scanLand[s] = rotate(qBack, A.scanLand[s]) - d;
        A.stepScanFrom[s] = rotate(qBack, A.stepScanFrom[s]) - d;
        A.stepScanDir[s] = rotate(qBack, A.stepScanDir[s]);
""")
# stairs stay seen while standing; a step's scan answered
rep("""    A.stairSeen += dt;
    if (A.scanWant && in.groundScanValid && walking) {""",
"""    if (walking) A.stairSeen += dt;   // (standing on stairs, they stay seen: its steps land on treads too)
    if (A.scanWant && in.groundScanValid && A.scanForStep) {
        // (a step's: kept with the world, its landing fitted to it as the step goes)
        const int s = A.scanFoot & 1;
        for (int k = 0; k < kGroundScan; k++) A.stepG[s][k] = in.groundScan[k];
        A.stepScanFrom[s] = A.scanFrom;
        A.stepScanDir[s] = A.scanDir;
        A.stepScanHave[s] = true;
    } else if (A.scanWant && in.groundScanValid && walking) {""")
rep("""    A.scanWant = false;
    if (A.plantOn > 0.f) {""",
"""    A.scanWant = false;
    A.scanForStep = false;
    // a step's landing heel h moved onto a tread of its scan, as a swing's landing is: the least move (at most a fifth of a
    // foot length) that keeps the toes off a riser ahead, the heel off a riser behind and the ball on the tread. False when
    // the scan does not cover it or the ground scanned does not step
    auto fitTread = [&](int s, vec3 h, float& oShift, float& oLand, bool& oBall) -> bool {
        if (!A.stepScanHave[s]) return false;
        const float* g = A.stepG[s];
        const vec3 dir = A.stepScanDir[s];
        const float uHeel = dot(vec2(h.x - A.stepScanFrom[s].x, h.y - A.stepScanFrom[s].y), vec2(dir.x, dir.y));
        if (uHeel < -0.1f || uHeel + L > (kGroundScan - 0.5f) * kGroundScanStep) return false;
        const float toeLen = sk.boneLength[s ? B_TOE_R : B_TOE_L], footLen = L + toeLen;
        const float mg = 0.02f * scale + 0.5f * kGroundScanStep;
        const float gFoot = g[Clamp((int)floorf((uHeel + L) / kGroundScanStep + 0.5f), 0, kGroundScan - 1)];
        float bestCost = 1e9f, bestShift = 0.f, bestG = -9.f;
        bool bestBall = false, stepped = false;
        for (int k0 = 0, k1; k0 < kGroundScan; k0 = k1 + 1) {
            k1 = k0;
            while (k1 + 1 < kGroundScan && fabsf(g[k1 + 1] - g[k1]) < 0.03f) k1++;
            const bool rearEdge = k0 > 0, frontEdge = k1 < kGroundScan - 1;
            const float r = (k0 - 0.5f) * kGroundScanStep, f = (k1 + 0.5f) * kGroundScanStep;
            const bool upAhead = frontEdge && g[k1 + 1] > g[k1], upBehind = rearEdge && g[k0 - 1] > g[k0];
            if ((frontEdge && fabsf(g[k1 + 1] - g[k1]) > 0.06f) || (rearEdge && fabsf(g[k0 - 1] - g[k0]) > 0.06f)) stepped = true;
            float lo = -1e9f, hi = 1e9f;
            if (upAhead) hi = f - mg - footLen - 0.055f * scale;
            else if (frontEdge) hi = f - mg - L;
            if (upBehind) lo = r + mg + 0.04f * scale;
            else if (rearEdge) lo = r + mg - 0.7f * L;
            const float uh = lo <= hi ? Clamp(uHeel, lo, hi) : (upAhead ? hi : lo), cost = fabsf(uh - uHeel) + (lo <= hi ? 0.f : 0.1f);
            const float gl = g[Clamp((int)floorf((uh + L) / kGroundScanStep + 0.5f), k0, k1)];
            if (cost < bestCost && fabsf(uh - uHeel) <= 0.2f * scale && fabsf(gl - gFoot) < 0.4f) {
                bestCost = cost;
                bestShift = uh - uHeel;
                bestG = gl;
                bestBall = rearEdge && !upBehind && uh < r - 0.05f;
            }
        }
        if (!stepped || bestCost > 1e8f) return false;
        oShift = bestShift;
        oLand = bestG;
        oBall = bestBall;
        return true;
    };
    // a step's landing heel fitted so (the tread's height, the move and whether the heel overhangs a nose kept for its
    // landing; none if it cannot be fitted)
    auto fitStep = [&](int s, vec3 h) -> vec3 {
        A.stepFitLand[s] = -9.f;
        float sh = 0.f, land = -9.f;
        bool ball = false;
        if (!fitTread(s, h, sh, land, ball)) return h;
        A.stepFitShift[s] = sh;
        A.stepFitLand[s] = land;
        A.stepFitBall[s] = ball;
        return h + vec3(A.stepScanDir[s].x, A.stepScanDir[s].y, 0.f) * sh;
    };
    if (A.plantOn > 0.f) {""")
# standing: a foot that comes down on stairs does so as a short fitted step
rep("""            } else if (A.stepT[s] < 0.f && !A.planted[s] && nearGround) {
                // standing: a foot that comes down stays down where it is shown
                A.ballProbe[s] = false;
                A.plantSlack[s] = 0.f;
                A.planted[s] = true;
                A.footHold[s] = false;
                vec3 h = shownHeel(s);
                A.plantP[s] = vec3(h.x, h.y, 0.f);
                A.plantYaw[s] = wrapAngle(fyaw[s] + A.corrYaw[s]);
            }""",
"""            } else if (A.stepT[s] < 0.f && !A.planted[s] && nearGround) {
                // standing: a foot that comes down stays down where it is shown - on stairs, the other foot down, it comes
                // down onto a tread, as a short step from there
                const int o = 1 - s;
                const vec3 h = shownHeel(s);
                A.ballProbe[s] = false;
                A.footHold[s] = false;
                if (A.stairSeen < 1.5f && terrain && in.footProbes && A.planted[o] && A.stepT[o] < 0.f) {
                    A.stepT[s] = 0.f;
                    A.stepScanAsked[s] = A.stepScanHave[s] = A.stepFitBall[s] = false;
                    A.stepFitLand[s] = -9.f;
                    A.stepFitShift[s] = 0.f;
                    A.stepFrom[s] = vec3(h.x, h.y, 0.f);
                    A.stepFromYaw[s] = wrapAngle(fyaw[s] + A.corrYaw[s]);
                    A.stepDur[s] = 0.22f;
                    A.stepLift[s] = 0.02f * scale;
                } else {
                    A.plantSlack[s] = 0.f;
                    A.planted[s] = true;
                    A.plantP[s] = vec3(h.x, h.y, 0.f);
                    A.plantYaw[s] = wrapAngle(fyaw[s] + A.corrYaw[s]);
                }
            }""")
# standing: no step that would only land the foot back where it stands
rep("""            if (err[s] > need && A.planted[o] && A.stepT[o] < 0.f && A.stepT[s] < 0.f) {
                vec3 e = A.plantP[s] - heel[s];
                float dist = length(vec2(e.x, e.y)), dy = fabsf(wrapAngle(A.plantYaw[s] - fyaw[s]));
                float turn = fabsf(in.turnRate);
                A.planted[s] = false;
                A.stepT[s] = 0.f;
                A.ballProbe[s] = false;
                A.stepFrom[s] = A.plantP[s];""",
"""            // (on stairs: not a step whose landing, fitted to the tread, is where the foot already stands)
            bool inPlace = false;
            if (err[s] > need && A.stairSeen < 1.5f && A.stepScanHave[s] && A.planted[s]) {
                float sh = 0.f, land = -9.f;
                bool ball = false;
                const vec3 hp(heel[s].x, heel[s].y, 0.f);
                if (fitTread(s, hp, sh, land, ball)) {
                    const vec3 hf = hp + vec3(A.stepScanDir[s].x, A.stepScanDir[s].y, 0.f) * sh, e2 = hf - A.plantP[s];
                    inPlace = length(vec2(e2.x, e2.y)) < 0.04f * scale && fabsf(wrapAngle(A.plantYaw[s] - fyaw[s])) < 0.3f;
                }
            }
            if (err[s] > need && !inPlace && A.planted[o] && A.stepT[o] < 0.f && A.stepT[s] < 0.f) {
                vec3 e = A.plantP[s] - heel[s];
                float dist = length(vec2(e.x, e.y)), dy = fabsf(wrapAngle(A.plantYaw[s] - fyaw[s]));
                float turn = fabsf(in.turnRate);
                A.planted[s] = false;
                A.stepT[s] = 0.f;
                A.ballProbe[s] = false;
                A.stepScanAsked[s] = A.stepScanHave[s] = A.stepFitBall[s] = false;
                A.stepFitLand[s] = -9.f;
                A.stepFitShift[s] = 0.f;
                A.stepFrom[s] = A.plantP[s];""")
rep("""        A.stepT[s] = 0.f;
        A.ballProbe[s] = false;
        A.stepFrom[s] = A.plantP[s];
        A.stepFromYaw[s] = A.plantYaw[s];
        A.stepDur[s] = Clamp(0.15f + 0.22f * dist / scale, 0.15f, 0.26f);""",
"""        A.stepT[s] = 0.f;
        A.ballProbe[s] = false;
        A.stepScanAsked[s] = A.stepScanHave[s] = A.stepFitBall[s] = false;
        A.stepFitLand[s] = -9.f;
        A.stepFitShift[s] = 0.f;
        A.stepFrom[s] = A.plantP[s];
        A.stepFromYaw[s] = A.plantYaw[s];
        A.stepDur[s] = Clamp(0.15f + 0.22f * dist / scale, 0.15f, 0.26f);""")
# a step's landing onto a tread
rep("""                A.stepTo[s] = vec3(h.x, h.y, 0.f);
                A.stepToYaw[s] = wrapAngle(fyaw[s] + lead);""",
"""                A.stepTo[s] = vec3(h.x, h.y, 0.f);
                if (A.stairSeen < 1.5f) A.stepTo[s] = fitStep(s, A.stepTo[s]);   // (on stairs: onto a tread)
                A.stepToYaw[s] = wrapAngle(fyaw[s] + lead);""")
rep("""            if (A.stepT[s] >= 1.f) {
                // (on stairs: down on the ground probed where it lands, whatever of the way down is left)
                if (A.stairSeen < 1.5f && terrain && in.footProbes) (s ? A.footR : A.footL) = A.groundRaw[s];
                A.stepT[s] = -1.f;
                A.planted[s] = true;
                A.footHold[s] = false;
                A.plantSlack[s] = 0.f;""",
"""            if (A.stepT[s] >= 1.f) {
                // (on stairs: down on the tread its landing is fitted to - probed under the ball if the heel overhangs its
                // nose - else on the ground probed where it lands, whatever of the way down is left. A foot moved onto its
                // tread may stray that much further from the pose)
                const bool fitted = A.stepFitLand[s] > -8.f;
                if (fitted) {
                    (s ? A.footR : A.footL) = A.stepFitLand[s];
                    A.ballProbe[s] = A.stepFitBall[s];
                } else if (A.stairSeen < 1.5f && terrain && in.footProbes) {
                    (s ? A.footR : A.footL) = A.groundRaw[s];
                }
                A.stepT[s] = -1.f;
                A.planted[s] = true;
                A.footHold[s] = false;
                A.plantSlack[s] = fitted ? fabsf(A.stepFitShift[s]) : 0.f;""")
# the step's scan asked for
rep("""        A.probeP[s] = vec3(m.x, m.y, 0.f);
""",
"""        A.probeP[s] = vec3(m.x, m.y, 0.f);
        // a step on stairs (quick or standing): the way along the foot where it lands is scanned once, early in the step
        if (A.stepT[s] >= 0.f && A.stepT[s] < 0.6f && A.stairSeen < 1.5f && !A.stepScanAsked[s] && !A.scanWant && terrain && in.footProbes) {
            const vec3 sdir = rotate(qz(A.stepToYaw[s]), vec3(0.f, 1.f, 0.f));
            vec3 slm = A.stepTo[s] + sdir * (0.5f * L);
            slm = rotate(qBack, slm) - d;
            A.scanWant = true;
            A.scanForStep = true;
            A.scanFoot = (u8)s;
            A.scanDir = rotate(qBack, sdir);
            A.scanFrom = vec3(slm.x, slm.y, 0.f) - A.scanDir * kScanBack;
            A.stepScanAsked[s] = true;
        }
""")
# resets
rep("""        stairBall[k] = ballProbe[k] = false;
        scanDone[k] = false;
""",
"""        stairBall[k] = ballProbe[k] = false;
        scanDone[k] = false;
        stepScanAsked[k] = stepScanHave[k] = stepFitBall[k] = false;
        stepFitLand[k] = -9.f;
        stepFitShift[k] = 0.f;
""")
rep("""    scanWant = false;
    stairSeen = 99.f;""",
"""    scanWant = scanForStep = false;
    stairSeen = 99.f;""")
rep("""                stairBall[s] = ballProbe[s] = false;
                pivotW[s] = 0.f;
                stepT[s] = -1.f;""",
"""                stairBall[s] = ballProbe[s] = false;
                stepScanAsked[s] = stepScanHave[s] = stepFitBall[s] = false;
                stepFitLand[s] = -9.f;
                stepFitShift[s] = 0.f;
                pivotW[s] = 0.f;
                stepT[s] = -1.f;""")
open(fn, 'w').write(s)
# the Animator's fields (additive)
h = open(fh).read()
old = """    float rootRatio = 1.f;                    // the share of its gait's travel the body actually covers (held back: below 1)
"""
if h.count(old) != 1: sys.exit('character.h anchor count %d' % h.count(old))
h = h.replace(old, old + """    // (a step on stairs - quick or standing: the ground scanned along the foot where it lands, kept with the world while
    // the step lasts and after, and the tread its landing is fitted to)
    bool scanForStep = false;                 // the scan asked for is a step's
    bool stepScanAsked[2] = {false, false}, stepScanHave[2] = {false, false};
    float stepG[2][kGroundScan] = {};         // its ground, along the way from stepScanFrom
    vec3 stepScanFrom[2] = {vec3(0), vec3(0)}, stepScanDir[2] = {vec3(0, 1, 0), vec3(0, 1, 0)};
    float stepFitShift[2] = {0.f, 0.f};       // the landing moved along the way onto one tread (m)
    float stepFitLand[2] = {-9.f, -9.f};      // that tread's height (kept with the world; -9: none)
    bool stepFitBall[2] = {false, false};     // its heel overhangs that tread's nose (the ground is probed under the ball)
""")
open(fh, 'w').write(h)
