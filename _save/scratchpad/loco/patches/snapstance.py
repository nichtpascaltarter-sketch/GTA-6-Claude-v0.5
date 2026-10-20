import sys
# 1. A stance change between stances whose legs are the walk's or the stand's (plain 0, chatting 7, on the phone 8,
#    smoking 10, waiting 23) crossfades the upper body only. The crossfade from the captured pose ran after the feet were
#    put on the ground and took the legs and pelvis too: planted feet were dragged with the body's move and turn since
#    the capture, for a third of a second. pedai flips a walking follower between 0 and 7 every few frames (the locotour's
#    ped 13: 27 flips, its 128 cm slide), and a walker's phone or cigarette ending (8 / 10 -> 0) did the same.
# 2. A foot held on the ground since its toe-off that the gait plants again (its next stance come round before the pose
#    lifted it clear: a slow walk, a turn) or that comes to a stand keeps its footprint; it was re-planted at the shown
#    heel - this update's pose with the last update's correction - which jumps by the pose's own move in one update.
# usage: snapstance.py <animator.cpp> <character.h>
fa, fh = sys.argv[1], sys.argv[2]
def patch(fn, pairs):
    s = open(fn).read()
    for old, new in pairs:
        if s.count(old) != 1: sys.exit('%s: count %d for: %s' % (fn, s.count(old), old[:90]))
        s = s.replace(old, new)
    open(fn, 'w').write(s)
patch(fh, [("""    float footL = 0, footR = 0, snapW = 0, snapRate = 5.f, moveW = 0, legScale = 1.f, styleF = 0, steerS = 0;""",
"""    float footL = 0, footR = 0, snapW = 0, snapRate = 5.f, moveW = 0, legScale = 1.f, styleF = 0, steerS = 0;
    bool snapUpper = false;   // the crossfade from snap takes the upper body only (a stance change that leaves the legs to the walk or the stand)""")])
patch(fa, [
("""        // weapon switch inside a fighting guard
        snap = pose;
        snapW = 1.f;
        snapRate = 6.f;""",
"""        // weapon switch inside a fighting guard
        snap = pose;
        snapW = 1.f;
        snapRate = 6.f;
        snapUpper = false;"""),
("""    if (st != stance) {
        snap = pose;
        snapW = 1.f;
        snapRate = stanceIsVehicle(st) || stanceIsVehicle(stance) ? 4.f : (stanceIsGuard(st) && stanceIsGuard(stance) ? 12.f : 3.f);""",
"""    if (st != stance) {
        snap = pose;
        snapW = 1.f;
        snapRate = stanceIsVehicle(st) || stanceIsVehicle(stance) ? 4.f : (stanceIsGuard(st) && stanceIsGuard(stance) ? 12.f : 3.f);
        // (between stances whose legs are the walk's or the stand's - plain, chatting, on the phone, smoking, waiting -
        // only the upper body crossfades: the legs are the same either way, and a crossfade of theirs from the captured
        // pose, after the feet are put on the ground, drags planted feet with the body's move and turn since)
        auto baseLegs = [](int s) { return s == 0 || s == 7 || s == 8 || s == 10 || s == 23; };
        snapUpper = baseLegs(st) && baseLegs(stance);"""),
("""        if (!extBlend) {
            snap = pose;
            snapW = 1.f;""",
"""        if (!extBlend) {
            snap = pose;
            snapW = 1.f;
            snapUpper = false;"""),
("""                // hand back to the base layers with a crossfade from the last displayed pose
                snap = pose;
                snapW = 1.f;""",
"""                // hand back to the base layers with a crossfade from the last displayed pose
                snap = pose;
                snapW = 1.f;
                snapUpper = false;"""),
("""        float w = snapW * snapW * (3.f - 2.f * snapW);
        blendCtl(outp, snap, w, pose);""",
"""        float w = snapW * snapW * (3.f - 2.f * snapW);
        if (snapUpper) blendUpperBody(outp, snap, w, pose);
        else blendCtl(outp, snap, w, pose);"""),
("""                    A.planted[s] = true;
                    A.footHold[s] = false;
                    vec3 h = shownHeel(s);
                    A.plantP[s] = vec3(h.x, h.y, 0.f);
                    A.plantYaw[s] = wrapAngle(fyaw[s] + A.corrYaw[s]);
                    A.footEvents |= 1u << s;""",
"""                    // (a foot held since its toe-off stays on its footprint: the shown heel is this update's pose with the
                    // last update's correction, off by the pose's own move since)
                    const bool held = A.footHold[s];
                    A.planted[s] = true;
                    A.footHold[s] = false;
                    if (!held) {
                        vec3 h = shownHeel(s);
                        A.plantP[s] = vec3(h.x, h.y, 0.f);
                        A.plantYaw[s] = wrapAngle(fyaw[s] + A.corrYaw[s]);
                    }
                    A.footEvents |= 1u << s;"""),
("""                const int o = 1 - s;
                const vec3 h = shownHeel(s);
                A.ballProbe[s] = false;
                A.footHold[s] = false;
                if (A.stairSeen < 1.5f && terrain && in.footProbes && A.planted[o] && A.stepT[o] < 0.f) {
                    A.stepT[s] = 0.f;
                    A.stepScanAsked[s] = A.stepScanHave[s] = A.stepFitBall[s] = false;
                    A.stepFitLand[s] = -9.f;
                    A.stepFitShift[s] = 0.f;
                    A.stepFrom[s] = vec3(h.x, h.y, 0.f);
                    A.stepFromYaw[s] = wrapAngle(fyaw[s] + A.corrYaw[s]);""",
"""                const int o = 1 - s;
                // (a foot held since its toe-off stands where it is held)
                const bool held = A.footHold[s];
                const vec3 h = held ? A.plantP[s] : shownHeel(s);
                const float hy = held ? A.plantYaw[s] : wrapAngle(fyaw[s] + A.corrYaw[s]);
                A.ballProbe[s] = false;
                A.footHold[s] = false;
                if (A.stairSeen < 1.5f && terrain && in.footProbes && A.planted[o] && A.stepT[o] < 0.f) {
                    A.stepT[s] = 0.f;
                    A.stepScanAsked[s] = A.stepScanHave[s] = A.stepFitBall[s] = false;
                    A.stepFitLand[s] = -9.f;
                    A.stepFitShift[s] = 0.f;
                    A.stepFrom[s] = vec3(h.x, h.y, 0.f);
                    A.stepFromYaw[s] = hy;"""),
("""                } else {
                    A.plantSlack[s] = 0.f;
                    A.planted[s] = true;
                    A.plantP[s] = vec3(h.x, h.y, 0.f);
                    A.plantYaw[s] = wrapAngle(fyaw[s] + A.corrYaw[s]);
                }""",
"""                } else {
                    A.plantSlack[s] = 0.f;
                    A.planted[s] = true;
                    A.plantP[s] = vec3(h.x, h.y, 0.f);
                    A.plantYaw[s] = hy;
                }"""),
])
print('ok')
