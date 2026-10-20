#!/usr/bin/env python3
# stairpace.py <animator.cpp in> <animator.cpp out> <character.h in> <character.h out>
# Stairs at a walking pace and turns onto flights (on animator 002915d0 = snapshot 57, character.h 1628ef2e):
#  - below the slow walk (the walk blended with the stand) the swing keeps the walk's clearance in the tests that decide
#    when it touches down, grazes the ground, lets go of its toe-off hold, fades its correction and stays planted after
#    its toe-off - at a steady pace only (setting off and stopping keep their timing)
#  - landings predicted along the turning way (turn rate), scanned along the landing foot's own axis, re-scanned when the
#    landing drifts across the scanned line or turns from it (swings and steps); the tread move goes along that line
#  - the landing probe under the toes; going up, a foot about to lift probes its landing during its last stance frames
#  - going down, a tread too short for both margins splits the shortfall between the heel and the ball
#  - a swing's tread fit holds only where it comes down near its fitted landing (a stop or a hold well short: the ground
#    under it); on stairs a swing keeps above the ground it left through the first part of the swing
#  - a swing coming down short of its fit, its heel well over the nose behind that tread, stands on its forefoot
import sys

def sub(s, old, new, count=1):
    n = s.count(old)
    if n != count:
        raise SystemExit('expected %d, found %d: %r' % (count, n, old[:120]))
    return s.replace(old, new)

src, dst, hsrc, hdst = sys.argv[1:5]
s = open(src).read()

# 1. the walk-blend clearance and the late-stance landing probe, per foot, once the swing phases are known
s = sub(s, """            swingU[s] = ph > duty ? (ph - duty) / Max(1.f - duty, 0.05f) : 0.f;
        }
    const Bone ends[2] = {B_FOOT_L, B_FOOT_R};""", """            swingU[s] = ph > duty ? (ph - duty) / Max(1.f - duty, 0.05f) : 0.f;
        }
    // the clearance a swing keeps while the walk is blended with the stand below the slow walk (the walk's own, which the
    // blend would take away; the lift further down shows it): the tests on the pose's foot height below count it, so a
    // slow walk's swing lets go of the ground, fades its correction and touches down as a walk's does - where its landing
    // was predicted and fitted to a tread - not as the blend's low swing would. At a steady pace only: setting off and
    // coming to a stop keep their timing. Its landing (touching down, grazing the ground) is the pose's own from late in
    // the swing
    float blendLift[2] = {0.f, 0.f}, blendLand[2] = {0.f, 0.f};
    {
        // (the pace asked for is the pace walked - and has been for a moment: a follower stopping and going keeps the
        // blend's timing)
        const float steadyNow = 1.f - sstep(0.05f, 0.2f, fabsf(Max(in.speed, 0.f) - A.speedS));
        A.blendSteady += (steadyNow - A.blendSteady) * (1.f - expf(-dt * (steadyNow < A.blendSteady ? 12.f : 10.f)));
        const float steady = Min(steadyNow, A.blendSteady);
        if (walking && A.moveW < 0.999f && steady > 0.f)
            for (int s = 0; s < 2; s++)
                if (swingU[s] > 0.f) {
                    blendLift[s] = steady * (1.f - A.moveW) * 0.07f * (A.footHeel + A.footBall) / 0.197f * sinf(kPi * swingU[s]);
                    blendLand[s] = blendLift[s] * (1.f - sstep(0.7f, 0.85f, swingU[s]));
                }
    }
    // up a flight, a foot about to lift (its stance nearly done, or done and the pose still has it down; the root or the
    // other foot already a step above it) probes where it will land as well: its swing knows that ground from the start,
    // clearing the next nose
    bool lateSt[2] = {false, false};
    for (int s = 0; s < 2; s++) {
        float phL = A.phase - (s ? 0.5f : 0.f);
        phL -= floorf(phL);
        const float fs = s ? A.footR : A.footL, fo = s ? A.footL : A.footR;
        lateSt[s] = walking && cycle > 0.f && A.stepT[s] < 0.f && (A.planted[s] || A.footHold[s]) && A.stairSeen < 1.5f &&
                    (fs < -0.05f || (A.planted[1 - s] && fo > fs + 0.05f)) && (swingU[s] > 0.f || phL > duty - 0.08f);
    }
    const Bone ends[2] = {B_FOOT_L, B_FOOT_R};""")

# 2. the root's vertical move only off probes under a planted foot (not its landing probe)
s = sub(s, """                if (A.planted[s] && A.plantedPrev[s] && A.stepT[s] < 0.f) {""",
        """                if (A.planted[s] && A.plantedPrev[s] && A.stepT[s] < 0.f && !A.probeAhead[s]) {""")
s = sub(s, """            if (A.stairLand[s] > -8.f) A.stairLand[s] -= dzRoot;
            if (A.stepScanHave[s])""", """            if (A.stairLand[s] > -8.f) A.stairLand[s] -= dzRoot;
            if (A.liftGround[s] > -8.f) A.liftGround[s] -= dzRoot;
            if (A.stepScanHave[s])""")
s = sub(s, """                if (swingU[s] <= 0.f) A.groundAhead[s] = *fz[s];""", """                if (swingU[s] <= 0.f && !lateSt[s]) A.groundAhead[s] = *fz[s];""")
s = sub(s, """            A.groundRaw[s] = gIn[s];
            A.plantedPrev[s] = A.planted[s] && A.stepT[s] < 0.f;""", """            A.groundRaw[s] = gIn[s];
            A.plantedPrev[s] = A.planted[s] && A.stepT[s] < 0.f && !A.probeAhead[s];   // (probed under it both updates)""")
s = sub(s, """            A.footHold[s] = A.ballProbe[s] = false;
            A.pivotW[s] = 0.f;
        }
    }
    // root motion of this update""", """            A.footHold[s] = A.ballProbe[s] = false;
            A.pivotW[s] = 0.f;
            A.liftGround[s] = -9.f;
        }
    }
    // root motion of this update""")
s = sub(s, """        A.scanLand[s] = rotate(qBack, A.scanLand[s]) - d;
        A.stepScanFrom[s] = rotate(qBack, A.stepScanFrom[s]) - d;""", """        A.scanLand[s] = rotate(qBack, A.scanLand[s]) - d;
        A.stairDir[s] = rotate(qBack, A.stairDir[s]);
        A.stepScanFrom[s] = rotate(qBack, A.stepScanFrom[s]) - d;""")

# 3. landings along the turning way; a fit holds near its landing
s = sub(s, """    // displayed heel / yaw of a foot (animation + current correction)
    auto shownHeel = [&](int s) {
        vec3 pa = pivotA(s);
        return pa + A.plantCorr[s] + rotate(qz(A.corrYaw[s]), heel[s] - pa);
    };""", """    // the body's turn over the next sT metres of its way at its turn rate now (at most 1.2 rad), and where a point carried
    // with it - across, where p is now; along, `lead` ahead of the body then - will be once the body has gone that far along
    // its turning way: a swing's landing on a curving way (a spiral stair, a turn onto a flight) is where the turn takes it
    auto turnOver = [&](float sT) { return Clamp(in.turnRate * sT / Max(spd, 0.3f), -1.2f, 1.2f); };
    auto landAhead = [&](vec3 p, float sT, float lead) {
        const vec3 f(md.x, md.y, 0.f), lt(-md.y, md.x, 0.f);
        const float psi = turnOver(sT);
        vec3 D = f * sT;
        if (fabsf(psi) > 1e-3f) {
            const float rho = sT / psi;
            D = f * (rho * sinf(psi)) + lt * (rho * (1.f - cosf(psi)));
        }
        vec3 r = D + rotate(qz(psi), lt * dot(vec2(p.x, p.y), vec2(lt.x, lt.y)) + f * lead);
        r.z = p.z;
        return r;
    };
    // displayed heel / yaw of a foot (animation + current correction)
    auto shownHeel = [&](int s) {
        vec3 pa = pivotA(s);
        return pa + A.plantCorr[s] + rotate(qz(A.corrYaw[s]), heel[s] - pa);
    };
    // a swing's tread fit holds where it comes down near the landing it was fitted for (along the line scanned) - not for
    // one that comes down far short of it, held back or stopping on the flight: that lands on the ground under it
    auto atFit = [&](int s) {
        const vec3 dir = A.stairDir[s];
        const vec3 hf = A.scanLand[s] + dir * (A.stairShift[s] - 0.5f * (A.footHeel + A.footBall));
        const vec3 h = shownHeel(s);
        return fabsf(dot(vec2(h.x - hf.x, h.y - hf.y), vec2(dir.x, dir.y))) < 0.3f * (A.footHeel + A.footBall) / 0.197f;
    };""")

# 4. down a flight: a tread too short for both margins splits the shortfall (heel off the riser behind, ball off the nose)
s = sub(s, """            // (a tread shorter than the foot: no toes in a riser, and only if nothing else will do)
            const float uh = lo <= hi ? Clamp(uHeel, lo, hi) : (upAhead ? hi : lo), cost = fabsf(uh - uHeel) + (lo <= hi ? 0.f : 0.1f);""",
        """            // (a tread shorter than the foot: no toes in a riser, and only if nothing else will do; down a flight, the
            // heel's and the ball's margins share what is missing)
            const float uh = lo <= hi ? Clamp(uHeel, lo, hi) : (upAhead ? hi : (upBehind && frontEdge ? 0.5f * (lo + hi) : lo)),
                        cost = fabsf(uh - uHeel) + (lo <= hi ? 0.f : 0.1f);""")
s = sub(s, """            else if (rearEdge) lo = r + mg - 0.7f * L;
            const float uh = lo <= hi ? Clamp(uHeel, lo, hi) : (upAhead ? hi : lo), cost = fabsf(uh - uHeel) + (lo <= hi ? 0.f : 0.1f);""",
        """            else if (rearEdge) lo = r + mg - 0.7f * L;
            const float uh = lo <= hi ? Clamp(uHeel, lo, hi) : (upAhead ? hi : (upBehind && frontEdge ? 0.5f * (lo + hi) : lo)),
                        cost = fabsf(uh - uHeel) + (lo <= hi ? 0.f : 0.1f);""")

# 5. gait contacts with the walk-blend clearance
s = sub(s, """                bool touching = Min(Min(heel[s].z, ball[s].z), toeZ[s]) < 0.004f * scale;
                // (a foot still on the ground just after the gait's toe-off stays planted until it actually lifts:
                // blended gaits, e.g. a diagonal walk, lift a little later than their mixed duty)
                bool contact = (ph > 0.004f && ph < duty - 0.01f && nearGround) || (A.planted[s] && ph <= 0.004f) ||
                               (swingU[s] > 0.7f && touching) || (A.planted[s] && touching && swingU[s] < 0.25f);""",
        """                const float lowZ = Min(Min(heel[s].z, ball[s].z), toeZ[s]);
                // (a foot still on the ground just after the gait's toe-off stays planted until it actually lifts:
                // blended gaits, e.g. a diagonal walk, lift a little later than their mixed duty)
                bool contact = (ph > 0.004f && ph < duty - 0.01f && nearGround) || (A.planted[s] && ph <= 0.004f) ||
                               (swingU[s] > 0.7f && lowZ + blendLand[s] < 0.004f * scale) ||
                               (A.planted[s] && lowZ + blendLift[s] < 0.004f * scale && swingU[s] < 0.25f);""")
s = sub(s, """                    if (!A.footHold[s] && A.stairLand[s] > -8.f) {
                        // (down on the tread its swing was fitted to: at that tread's height from the start, not the
                        // lagging probe's)""", """                    if (!A.footHold[s] && A.stairLand[s] > -8.f && atFit(s)) {
                        // (down on the tread its swing was fitted to: at that tread's height from the start, not the
                        // lagging probe's)""")
s = sub(s, """                        // (on stairs without a tread fitted: at the higher of the near probe's ground - which runs ahead down
                        // a step, or lags behind up one - and the landing probe's under the forefoot)
                        if (A.stairSeen < 1.5f) (s ? A.footR : A.footL) = Max(s ? A.footR : A.footL, A.groundAhead[s]);""",
        """                        // (on stairs without a tread fitted: at the higher of the near probe's ground - which runs ahead down
                        // a step, or lags behind up one - and the landing probe's under the forefoot; far short of a fitted
                        // landing, the near probe's)
                        if (A.stairSeen < 1.5f && A.stairLand[s] < -8.f) (s ? A.footR : A.footL) = Max(s ? A.footR : A.footL, A.groundAhead[s]);""")

# 6. the toe-off hold, the graze hold and the swing's fade with it; the tread move along the line scanned
s = sub(s, """            if (A.footHold[s] && Min(Min(heel[s].z, ball[s].z), toeZ[s]) + A.pinZ[s] > 0.012f * scale) {""",
        """            if (A.footHold[s] && Min(Min(heel[s].z, ball[s].z), toeZ[s]) + A.pinZ[s] + blendLift[s] > 0.012f * scale) {""")
s = sub(s, """            if (walking && late && Min(Min(heel[s].z, ball[s].z), toeZ[s]) + A.pinZ[s] < 0.006f * scale) {
                A.ballProbe[s] = A.stairLand[s] > -8.f && A.stairBall[s];
                if (A.stairLand[s] > -8.f) (s ? A.footR : A.footL) = A.stairLand[s];
                A.plantSlack[s] = A.stairLand[s] > -8.f ? fabsf(A.stairShift[s]) : 0.f;""",
        """            if (walking && late && Min(Min(heel[s].z, ball[s].z), toeZ[s]) + A.pinZ[s] + blendLand[s] < 0.006f * scale) {
                const bool fit = A.stairLand[s] > -8.f && atFit(s);
                A.ballProbe[s] = fit && A.stairBall[s];
                if (fit) (s ? A.footR : A.footL) = A.stairLand[s];
                A.plantSlack[s] = fit ? fabsf(A.stairShift[s]) : 0.f;""")
s = sub(s, """            } else if (Min(heel[s].z, ball[s].z) > 0.012f * scale || A.moveW < 0.3f) {""",
        """            } else if (Min(heel[s].z, ball[s].z) + blendLift[s] > 0.012f * scale || A.moveW < 0.3f) {""")
s = sub(s, """                const vec3 to = vec3(md.x, md.y, 0.f) * A.stairShift[s];""", """                const vec3 to = A.stairDir[s] * A.stairShift[s];""")

# 7. a swing on stairs keeps above the ground it left at first
s = sub(s, """    float offs[2] = {A.footL, A.footR};
    for (int s = 0; s < 2; s++)
        if (swingU[s] > 0.f && !A.planted[s] && !A.footHold[s] && A.stepT[s] < 0.f)
            offs[s] = Max(Max(offs[s], A.groundAhead[s]), A.stairLand[s] - 0.3f * (1.f - sstep(0.45f, 0.75f, swingU[s])));""",
        """    float offs[2] = {A.footL, A.footR};
    for (int s = 0; s < 2; s++) {
        if (A.planted[s] || A.footHold[s] || A.stepT[s] >= 0.f) A.liftGround[s] = offs[s];
        if (swingU[s] > 0.f && !A.planted[s] && !A.footHold[s] && A.stepT[s] < 0.f) {
            offs[s] = Max(Max(offs[s], A.groundAhead[s]), A.stairLand[s] - 0.3f * (1.f - sstep(0.45f, 0.75f, swingU[s])));
            // (on stairs, through the first part of its swing no lower than the ground it left: its toes clear the tread
            // ahead on the way up - the top step onto a landing - and its heel the nose behind on the way down)
            if (A.stairSeen < 1.5f && A.liftGround[s] > -8.f) offs[s] = Max(offs[s], A.liftGround[s] - 0.3f * sstep(0.35f, 0.6f, swingU[s]));
        }
    }""")

# 8. the landing probe: also before the lift going up; along the turning way; under the toes
s = sub(s, """        const bool swinging = swingU[s] > 0.f && cycle > 0.f && !A.planted[s] && !A.footHold[s] && A.stepT[s] < 0.f;
        A.probeAhead[s] = swinging && !A.probeAhead[s];""", """        const bool swinging = swingU[s] > 0.f && cycle > 0.f && !A.planted[s] && !A.footHold[s] && A.stepT[s] < 0.f;
        float phP = A.phase - (s ? 0.5f : 0.f);
        phP -= floorf(phP);
        A.probeAhead[s] = (swinging || lateSt[s]) && !A.probeAhead[s];""")
s = sub(s, """        // the forefoot rather than the middle, so a landing that would straddle an edge shows)
        if (A.probeAhead[s])
            m = m + vec3(md.x, md.y, 0.f) * (Clamp(cycle * ((1.f - swingU[s]) * (1.f - duty) * A.rootRatio + A.strikeLead), 0.05f, 1.5f) + A.stairShift[s] +
                                             0.4f * L - dot(vec2(m.x, m.y), vec2(md.x, md.y)));
        else if""", """        // the toes rather than the middle, so a landing that would straddle an edge or put its toes into a riser shows; along
        // the turning way)
        if (A.probeAhead[s]) {
            const float sT = Clamp(cycle * (swinging ? (1.f - swingU[s]) * (1.f - duty) : 1.f - phP) * A.rootRatio, 0.f, 1.2f);
            m = landAhead(m, sT, cycle * A.strikeLead + A.stairShift[s] + 0.65f * L);
        } else if""")

# 9. a turning step re-scanned once its landing leaves the scanned line or turns from it
s = sub(s, """            offScan = uH < -0.05f || uH + L > (kGroundScan - 0.5f) * kGroundScanStep - 0.05f;
        }""", """            offScan = uH < -0.05f || uH + L > (kGroundScan - 0.5f) * kGroundScanStep - 0.05f;
            // (a turning step: its landing gone off the scanned line, or turned from it - the scan no longer runs along the foot)
            const float vH = dot(vec2(A.stepTo[s].x - A.stepScanFrom[s].x, A.stepTo[s].y - A.stepScanFrom[s].y), vec2(-sd0.y, sd0.x));
            const vec3 sdNow = rotate(qz(A.stepToYaw[s]), vec3(0.f, 1.f, 0.f));
            offScan = offScan || fabsf(vH) > 0.08f * scale || dot(vec2(sdNow.x, sdNow.y), vec2(sd0.x, sd0.y)) < 0.94f;
        }""")

# 10. a swing's scan: along the turning way to its landing, along the foot as it will land; again when it drifts or turns
s = sub(s, """            vec3 lm = pa + A.plantCorr[s] * w + rotate(qz(A.corrYaw[s] * w), mid - pa);
            const vec2 mdv(md.x, md.y);
            lm = lm + vec3(mdv.x, mdv.y, 0.f) * (cycle * ((1.f - swingU[s]) * (1.f - duty) * A.rootRatio + A.strikeLead) - dot(vec2(lm.x, lm.y), mdv));""",
        """            vec3 lm = pa + A.plantCorr[s] * w + rotate(qz(A.corrYaw[s] * w), mid - pa);
            const float sT = Clamp(cycle * (1.f - swingU[s]) * (1.f - duty) * A.rootRatio, 0.f, 1.2f);
            lm = landAhead(lm, sT, cycle * A.strikeLead);
            // (scanned along the foot as it will land - the pose's yaw now, turned with the body till then - so that its
            // heel, ball and toe tip lie on the line scanned even where it lands turned from the way, onto a flight from a
            // landing)
            const vec3 sdw = rotate(qz(turnOver(sT) + fyaw[s]), vec3(0.f, 1.f, 0.f));
            const vec2 mdv(sdw.x, sdw.y);""")
s = sub(s, """            const bool again = A.scanDone[s] && A.stairSeen < 1.5f && fabsf(dot(vec2(lm.x, lm.y) - vec2(A.scanLand[s].x, A.scanLand[s].y), mdv)) > 0.03f;""",
        """            // (again: the landing gone 3 cm along the scanned line or 5 cm across it, or the foot at it turned 15 degrees from it)
            const vec2 dl(lm.x - A.scanLand[s].x, lm.y - A.scanLand[s].y);
            const bool again = A.scanDone[s] && A.stairSeen < 1.5f &&
                               (fabsf(dot(dl, mdv)) > 0.03f || fabsf(dot(dl, vec2(-mdv.y, mdv.x))) > 0.05f * scale ||
                                dot(mdv, vec2(A.stairDir[s].x, A.stairDir[s].y)) < 0.966f);""")
s = sub(s, """                A.scanDir = rotate(qBack, vec3(md.x, md.y, 0.f));
                A.scanFrom = vec3(lm.x, lm.y, 0.f) - A.scanDir * kScanBack;""", """                A.scanDir = rotate(qBack, sdw);
                A.scanFrom = vec3(lm.x, lm.y, 0.f) - A.scanDir * kScanBack;
                A.stairDir[s] = sdw;""")

# 11. a swing coming down short of its tread fit (a slow walk touching down early, a graze hold) hangs its heel further
# over the nose behind that tread than the fit had it: well over it, it stands on its forefoot as a landing fitted so does
s = sub(s, """        return fabsf(dot(vec2(h.x - hf.x, h.y - hf.y), vec2(dir.x, dir.y))) < 0.3f * (A.footHeel + A.footBall) / 0.197f;
    };""", """        return fabsf(dot(vec2(h.x - hf.x, h.y - hf.y), vec2(dir.x, dir.y))) < 0.3f * (A.footHeel + A.footBall) / 0.197f;
    };
    // (a swing coming down short of its tread fit - touching down early at a slow walk, held where it grazed the ground -
    // hangs its heel further over the nose behind that tread than the fit had it: well over it, it stands on its forefoot,
    // the ground probed under the ball, as a landing fitted so does)
    auto overNose = [&](int s) {
        if (A.stairRear[s] > 8.f) return false;
        const vec3 dir = A.stairDir[s];
        const vec3 hf = A.scanLand[s] + dir * (A.stairShift[s] - 0.5f * (A.footHeel + A.footBall));
        const vec3 h = shownHeel(s);
        return A.stairRear[s] + dot(vec2(h.x - hf.x, h.y - hf.y), vec2(dir.x, dir.y)) < -0.05f;
    };""")
s = sub(s, """        bool stepped = false, bestBall = false;
        float bestCost = 1e9f, bestShift = 0.f, bestG = -9.f;""", """        bool stepped = false, bestBall = false;
        float bestCost = 1e9f, bestShift = 0.f, bestG = -9.f, bestRear = 9.f;""")
s = sub(s, """                bestBall = rearEdge && !upBehind && uh < r - 0.05f;   // (the heel well over the nose)
""", """                bestBall = rearEdge && !upBehind && uh < r - 0.05f;   // (the heel well over the nose)
                bestRear = rearEdge && !upBehind ? uh - r : 9.f;      // (how far ahead of a nose lower behind)
""")
s = sub(s, """        A.stairBall[s] = stepped && bestBall;
""", """        A.stairBall[s] = stepped && bestBall;
        A.stairRear[s] = stepped ? bestRear : 9.f;
""")
s = sub(s, """                        A.ballProbe[s] = A.stairBall[s];
""", """                        A.ballProbe[s] = A.stairBall[s] || overNose(s);
""")
s = sub(s, """                A.ballProbe[s] = fit && A.stairBall[s];
""", """                A.ballProbe[s] = fit && (A.stairBall[s] || overNose(s));
""")
s = sub(s, """        stairLand[k] = -9.f;
        stairBall[k] = ballProbe[k] = false;
""", """        stairLand[k] = -9.f;
        stairRear[k] = 9.f;
        stairBall[k] = ballProbe[k] = false;
""")
s = sub(s, """                stairLand[s] = -9.f;
                stairBall[s] = ballProbe[s] = false;
""", """                stairLand[s] = -9.f;
                stairRear[s] = 9.f;
                stairBall[s] = ballProbe[s] = false;
""")
open(dst, 'w').write(s)

h = open(hsrc).read()
h = sub(h, """    float stairLand[2] = {-9.f, -9.f};        // that tread's height (kept with the world; -9: none): the swing clears it
""", """    float stairLand[2] = {-9.f, -9.f};        // that tread's height (kept with the world; -9: none): the swing clears it
    vec3 stairDir[2] = {vec3(0, 1, 0), vec3(0, 1, 0)};   // the line that fit was scanned along (kept with the world): the move goes along it
    float liftGround[2] = {-9.f, -9.f};       // the ground a foot last stood on (kept with the world): its swing keeps above it at first
    float blendSteady = 0.f;                  // a slow walk's pace steady for a moment (0..1): its swing keeps the walk's clearance
    float stairRear[2] = {9.f, 9.f};          // how far that fit's heel is ahead of the nose behind its tread, one lower (m, along stairDir; 9: none)
""")
open(hdst, 'w').write(h)
print('ok')
