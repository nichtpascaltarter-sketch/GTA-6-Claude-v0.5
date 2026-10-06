#!/usr/bin/env python3
# Stairs (and curbs): a foot about to land where the ground steps asks the game for a ground scan along the way ahead
# (AnimInput::groundScan, at Animator::groundScanFrom/Dir); the landing is moved along the way to fit one tread - toes
# short of a riser up ahead, the heel clear of a riser behind, the ball always on the tread.
# usage: p16_stairs.py <tree> [--header-only]
import sys, os
tree = sys.argv[1]
header_only = len(sys.argv) > 2 and sys.argv[2] == '--header-only'
ch = os.path.join(tree, 'src/anim/character.h')
an = os.path.join(tree, 'src/anim/animator.cpp')

s = open(ch).read()
def rep(a, b):
    global s
    assert s.count(a) == 1, a
    s = s.replace(a, b)
rep('''// High level animation state machine driven by gameplay each frame.
struct AnimInput {''', '''// A ground scan along the way ahead of a foot about to land where the ground steps (stairs, a curb): this many
// heights, this far apart (m) - AnimInput::groundScan, Animator::wantsGroundScan.
constexpr int kGroundScan = 12;
constexpr float kGroundScanStep = 0.05f;

// High level animation state machine driven by gameplay each frame.
struct AnimInput {''')
rep('''    vec3 rootMove = vec3(0);
    bool rootMoveValid = false;
''', '''    vec3 rootMove = vec3(0);
    bool rootMoveValid = false;
    // the ground along the line Animator::groundScanFrom/Dir asked for (kGroundScan heights kGroundScanStep apart,
    // relative to the root like groundOffsetL/R): a foot landing on stairs or at a curb comes down on one tread
    float groundScan[kGroundScan] = {};
    bool groundScanValid = false;
''')
rep('''    float passW = 0.f, passYaw = 0.f, passShift = 0.f;   // passing someone close, smoothed: weight, shoulder turn (rad),
                                                         // side-step (m)
''', '''    float passW = 0.f, passYaw = 0.f, passShift = 0.f;   // passing someone close, smoothed: weight, shoulder turn (rad),
                                                         // side-step (m)
    vec3 scanFrom, scanDir;                   // a ground scan asked for (model space): its start and direction
    u8 scanFoot = 0;                          // the landing foot it is for
    bool scanWant = false;                    // asked for before the next update
    bool scanDone[2] = {false, false};        // this swing's landing has been fitted to the ground ahead
    float stairShift[2] = {0.f, 0.f};         // the landing moved along the way onto one tread (m)
    float stairSeen = 99.f;                   // s since steps were last found ahead (scans keep coming meanwhile)
''')
rep('''    vec3 footProbe(int side) const { return probeP[side & 1]; }
''', '''    vec3 footProbe(int side) const { return probeP[side & 1]; }
    // Model-space line the game should sample the ground along before the next update (AnimInput::groundScan): from
    // groundScanFrom() along groundScanDir() (a unit vector), kGroundScan heights kGroundScanStep apart. Asked for once a
    // swing by a foot about to land where the ground steps (stairs, a curb).
    bool wantsGroundScan() const { return scanWant; }
    vec3 groundScanFrom() const { return scanFrom; }
    vec3 groundScanDir() const { return scanDir; }
''')
open(ch, 'w').write(s)
if header_only:
    print('header ok')
    sys.exit(0)

s = open(an).read()
# 1. the scan's answer: the landing fitted onto one tread
rep('''    auto shownHeel = [&](int s) {
        vec3 pa = pivotA(s);
        return pa + A.plantCorr[s] + rotate(qz(A.corrYaw[s]), heel[s] - pa);
    };''', '''    auto shownHeel = [&](int s) {
        vec3 pa = pivotA(s);
        return pa + A.plantCorr[s] + rotate(qz(A.corrYaw[s]), heel[s] - pa);
    };
    // the ground scan asked for last update (the way ahead of a foot about to land where the ground steps): the treads
    // along it, and where the landing heel goes so that the foot stands on the one under its ball - the toes short of a
    // riser up ahead, the heel clear of a riser behind (overhanging a nose or an edge is fine), the ball on the tread
    const float kScanBack = 0.25f;   // (the scan starts this far behind the foot's middle where it was to land)
    A.stairSeen += dt;
    if (A.scanWant && in.groundScanValid && walking) {
        const int s = A.scanFoot & 1;
        const float* g = in.groundScan;
        const float toeLen = sk.boneLength[s ? B_TOE_R : B_TOE_L], footLen = L + toeLen;
        const float uHeel = kScanBack - 0.5f * L, uBall = kScanBack + 0.5f * L;
        const int kb = Clamp((int)floorf(uBall / kGroundScanStep + 0.5f), 0, kGroundScan - 1);
        int k0 = kb, k1 = kb;
        while (k0 > 0 && fabsf(g[k0 - 1] - g[kb]) < 0.03f) k0--;
        while (k1 < kGroundScan - 1 && fabsf(g[k1 + 1] - g[kb]) < 0.03f) k1++;
        const bool rearEdge = k0 > 0, frontEdge = k1 < kGroundScan - 1;
        const float r = (k0 - 0.5f) * kGroundScanStep, f = (k1 + 0.5f) * kGroundScanStep;
        const bool upAhead = frontEdge && g[k1 + 1] > g[kb] + 0.03f, upBehind = rearEdge && g[k0 - 1] > g[kb] + 0.03f;
        const bool stepped = (rearEdge && fabsf(g[k0 - 1] - g[kb]) > 0.06f && fabsf(g[k0 - 1] - g[kb]) < 0.4f) ||
                             (frontEdge && fabsf(g[k1 + 1] - g[kb]) > 0.06f && fabsf(g[k1 + 1] - g[kb]) < 0.4f);
        float lo = -1e9f, hi = 1e9f, uh = uHeel;
        const float mg = 0.025f * scale;
        if (upAhead) hi = f - mg - footLen;
        else if (frontEdge) hi = f - mg - L;
        if (upBehind) lo = r + mg;
        else if (rearEdge) lo = r + mg - L;
        uh = lo <= hi ? Clamp(uHeel, lo, hi) : (upAhead ? hi : lo);   // (a tread shorter than the foot: no toes in a riser)
        A.stairShift[s] = stepped ? Clamp(uh - uHeel, -0.2f * scale, 0.2f * scale) : 0.f;
        if (stepped) A.stairSeen = 0.f;
        A.scanDone[s] = true;
    }
    A.scanWant = false;''')
# 2. a foot down (planted, held, stepping): its swing's fitting is over
rep('''        if (A.stepT[s] >= 0.f) {
            A.stepT[s] += dt / A.stepDur[s];''', '''        if (A.stepT[s] >= 0.f || A.planted[s] || A.footHold[s]) {
            A.stairShift[s] = 0.f;
            A.scanDone[s] = false;
        }
        if (A.stepT[s] >= 0.f) {
            A.stepT[s] += dt / A.stepDur[s];''')
# 3. a free foot: the correction eases to the fitted landing (else fades)
rep('''            } else if (Min(heel[s].z, ball[s].z) > 0.012f * scale || A.moveW < 0.3f) {
                float k = expf(-dt / 0.07f);
                A.plantCorr[s] = A.plantCorr[s] * k;
                A.corrYaw[s] *= k;
            }
            A.pivotW[s] = approach(A.pivotW[s], 0.f, dt * 4.f);''', '''            } else if (Min(heel[s].z, ball[s].z) > 0.012f * scale || A.moveW < 0.3f) {
                float k = expf(-dt / 0.07f);
                const vec3 to = vec3(md.x, md.y, 0.f) * A.stairShift[s];
                A.plantCorr[s] = to + (A.plantCorr[s] - to) * k;
                A.corrYaw[s] *= k;
            }
            A.pivotW[s] = approach(A.pivotW[s], 0.f, dt * 4.f);''')
# 4. the scan request: once a swing, by a foot heading for where the ground steps (or on stairs)
rep('''        m = rotate(qBack, m) - d;
        A.probeP[s] = vec3(m.x, m.y, 0.f);''', '''        m = rotate(qBack, m) - d;
        A.probeP[s] = vec3(m.x, m.y, 0.f);
        // a swing heading where the ground steps (its landing ground off the ground under it, or stairs just now): the
        // way ahead of where it lands is scanned once, early enough to fit the landing onto one tread
        if (swinging && terrain && in.footProbes && !A.scanDone[s] && !A.scanWant && swingU[s] > 0.12f && swingU[s] < 0.6f &&
            (A.stairSeen < 1.5f || fabsf(A.groundAhead[s] - (s ? A.footR : A.footL)) > 0.04f)) {
            vec3 lm = pa + A.plantCorr[s] * w + rotate(qz(A.corrYaw[s] * w), mid - pa) +
                      vec3(md.x, md.y, 0.f) * Clamp((1.f - swingU[s]) * cycle, 0.05f, 1.2f);
            lm = rotate(qBack, lm) - d;
            A.scanWant = true;
            A.scanFoot = (u8)s;
            A.scanDir = rotate(qBack, vec3(md.x, md.y, 0.f));
            A.scanFrom = vec3(lm.x, lm.y, 0.f) - A.scanDir * kScanBack;
        }''')
# 5. resets
rep('''        groundRaw[k] = groundAhead[k] = pivotW[k] = 0.f;''', '''        groundRaw[k] = groundAhead[k] = pivotW[k] = stairShift[k] = 0.f;
        scanDone[k] = false;''')
rep('''    rootVz = passW = passYaw = passShift = 0.f;''', '''    rootVz = passW = passYaw = passShift = 0.f;
    scanWant = false;
    stairSeen = 99.f;''')
rep('''                planted[s] = plantedPrev[s] = footHold[s] = probeAhead[s] = false;''', '''                planted[s] = plantedPrev[s] = footHold[s] = probeAhead[s] = scanDone[s] = false;
                stairShift[s] = 0.f;''')
open(an, 'w').write(s)
print('ok')
