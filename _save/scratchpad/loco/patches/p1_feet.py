import sys
p = sys.argv[1]
s = open(p).read()
def rep(old, new, cnt=1):
    global s
    n = s.count(old)
    if n != cnt:
        raise SystemExit("count %d != %d for: %s" % (n, cnt, old[:80]))
    s = s.replace(old, new)

# 1. world-anchored ground heights
rep('''    // ground under the feet (smoothed) and the slope plane
    float gl = terrain ? Clamp(in.groundOffsetL, -0.35f, 0.35f) : 0.f;
    float gr = terrain ? Clamp(in.groundOffsetR, -0.35f, 0.35f) : 0.f;
    float kg = 1.f - expf(-dt * 14.f);
    A.footL += (gl - A.footL) * kg;
    A.footR += (gr - A.footR) * kg;
''', '''    // ground under the feet and the slope plane. The heights are kept with the world: the root's vertical motion this
    // update (read off the ground under a foot that stayed planted, which does not move) comes out of them first, so a
    // planted foot keeps its height exactly (no lag on a slope, a curb or a stair) and only a swinging foot's new ground
    // (its probe ahead) is eased into
    const float gIn[2] = {terrain ? Clamp(in.groundOffsetL, -0.35f, 0.35f) : 0.f, terrain ? Clamp(in.groundOffsetR, -0.35f, 0.35f) : 0.f};
    {
        float dzRoot = 0.f, sum = 0.f;
        bool held[2] = {false, false};   // probed under a foot planted for the last two updates
        int n = 0;
        if (terrain && in.footProbes) {
            for (int s = 0; s < 2; s++)
                if (A.planted[s] && A.plantedPrev[s] && A.stepT[s] < 0.f) {
                    held[s] = true;
                    sum += A.groundRaw[s] - gIn[s];
                    n++;
                }
            if (n) {
                dzRoot = Clamp(sum / (float)n, -0.25f, 0.25f);
                if (dt > 1e-4f) A.rootVz += (dzRoot / dt - A.rootVz) * (1.f - expf(-dt * 12.f));
            } else {
                dzRoot = Clamp(A.rootVz * dt, -0.05f, 0.05f);   // both feet off the ground (a run's flight): as it was going
            }
        } else {
            A.rootVz = 0.f;
        }
        const float kg = 1.f - expf(-dt * 14.f);
        float* fz[2] = {&A.footL, &A.footR};
        for (int s = 0; s < 2; s++) {
            *fz[s] += (gIn[s] - (*fz[s] - dzRoot)) * (held[s] ? 1.f : kg) - dzRoot;
            A.groundRaw[s] = gIn[s];
            A.plantedPrev[s] = A.planted[s] && A.stepT[s] < 0.f;
        }
    }
''')
rep('''            A.pinZ[s] = A.plantAge[s] = 0.f;
        }
    }
    // root motion of this update''', '''            A.pinZ[s] = A.plantAge[s] = 0.f;
            A.footHold[s] = false;
            A.pivotW[s] = 0.f;
        }
    }
    // root motion of this update''')
# walking contacts: steps under way finish first; toe-off holds the foot until it lifts
rep('''            if (walking) {
                // gait contacts: plant at the heel strike''', '''            if (walking) {
                // (a step under way - a turn step, or one settling the feet as the walk begins - lands first)
                if (A.stepT[s] >= 0.f) continue;
                // gait contacts: plant at the heel strike''')
rep('''                A.stepT[s] = -1.f;   // a pending step gives way to the gait (its offset fades out)
                if (contact && !A.planted[s]) {
                    A.planted[s] = true;
                    vec3 h = shownHeel(s);''', '''                if (contact && !A.planted[s]) {
                    A.planted[s] = true;
                    A.footHold[s] = false;
                    vec3 h = shownHeel(s);''')
rep('''                } else if (!contact && A.planted[s]) {
                    A.planted[s] = false;
                }''', '''                } else if (!contact && A.planted[s]) {
                    // toe-off: held where it is until the pose has lifted it clear (a foot never slides off)
                    A.planted[s] = false;
                    A.footHold[s] = true;
                }''')
rep('''                // standing: a foot that comes down stays down where it is shown
                A.planted[s] = true;
                vec3 h = shownHeel(s);''', '''                // standing: a foot that comes down stays down where it is shown
                A.planted[s] = true;
                A.footHold[s] = false;
                vec3 h = shownHeel(s);''')
# corrections: step start helper, pivot, hold
rep('''    float lift[2] = {0.f, 0.f};
    float shiftT = 0.f;
    for (int s = 0; s < 2; s++) {
        vec3 pa = pivotA(s);
        if (A.stepT[s] >= 0.f) {''', '''    float lift[2] = {0.f, 0.f};
    float shiftT = 0.f;
    // a quick step from where the foot is to where the pose has it (walking: a foot the turn or the pose has left behind
    // beyond the leg's comfortable reach lifts and steps over instead of being dragged)
    auto quickStep = [&](int s) {
        vec3 e = A.plantP[s] - heel[s];
        float dist = length(vec2(e.x, e.y));
        A.planted[s] = false;
        A.footHold[s] = false;
        A.stepT[s] = 0.f;
        A.stepFrom[s] = A.plantP[s];
        A.stepFromYaw[s] = A.plantYaw[s];
        A.stepDur[s] = Clamp(0.17f + 0.3f * dist / scale, 0.17f, 0.3f);
        A.stepLift[s] = Clamp(0.025f + 0.1f * dist / scale, 0.025f, 0.06f) * scale;
    };
    for (int s = 0; s < 2; s++) {
        vec3 pa = pivotA(s);
        if (A.stepT[s] >= 0.f) {''')
rep('''            if (A.stepT[s] >= 1.f) {
                A.stepT[s] = -1.f;
                A.planted[s] = true;''', '''            if (A.stepT[s] >= 1.f) {
                A.stepT[s] = -1.f;
                A.planted[s] = true;
                A.footHold[s] = false;''')
rep('''        } else if (A.planted[s]) {
            vec3 pp = pivotP(s, A.plantP[s], A.plantYaw[s]);
            A.plantCorr[s] = vec3(pp.x - pa.x, pp.y - pa.y, 0.f);
            A.corrYaw[s] = wrapAngle(A.plantYaw[s] - fyaw[s]);
            // too far from the pose (a fast turn, a shove): let go (standing feet wait longer for their step)
            float lim = walking ? 0.3f : (A.staggerT >= 0.f ? 0.55f : 0.38f), limY = walking ? 0.75f : 1.15f;
            if (length2(A.plantCorr[s]) > lim * lim * scale * scale || fabsf(A.corrYaw[s]) > limY) A.planted[s] = false;
        } else if (Min(heel[s].z, ball[s].z) > 0.012f * scale || A.moveW < 0.3f) {''', '''        } else if (A.planted[s]) {
            // twisted further than the leg turns comfortably (a quick turn while walking): the foot pivots on its ball,
            // which stays where it is, the heel coming up a little
            const float tw = wrapAngle(A.plantYaw[s] - fyaw[s]), allow = walking ? 0.45f : 1.f;
            bool piv = false;
            if (fabsf(tw) > allow) {
                const float ex = tw - Clamp(tw, -allow, allow);
                const vec3 bw = A.plantP[s] + rotate(qz(A.plantYaw[s]), vec3(0.f, L, 0.f));
                A.plantYaw[s] = wrapAngle(A.plantYaw[s] - ex);
                A.plantP[s] = bw - rotate(qz(A.plantYaw[s]), vec3(0.f, L, 0.f));
                piv = true;
            }
            A.pivotW[s] = approach(A.pivotW[s], piv ? 1.f : 0.f, dt * (piv ? 10.f : 4.f));
            vec3 pp = pivotP(s, A.plantP[s], A.plantYaw[s]);
            A.plantCorr[s] = vec3(pp.x - pa.x, pp.y - pa.y, 0.f);
            A.corrYaw[s] = wrapAngle(A.plantYaw[s] - fyaw[s]);
            // too far from the pose (a fast turn, a shove): walking, a quick step over (the other foot down); standing, let
            // go for its step (standing feet wait longer)
            float lim = walking ? 0.3f : (A.staggerT >= 0.f ? 0.55f : 0.38f), limY = walking ? 0.75f : 1.15f;
            if (length2(A.plantCorr[s]) > lim * lim * scale * scale || fabsf(A.corrYaw[s]) > limY) {
                if (walking && A.stepT[1 - s] < 0.f) quickStep(s);
                else A.planted[s] = false;
            }
        } else if (A.footHold[s]) {
            // let go at toe-off, the pose still has it on the ground: it stays where it is until the pose lifts it clear
            // (or the leg would stretch too far: then it steps over)
            vec3 pp = pivotP(s, A.plantP[s], A.plantYaw[s]);
            A.plantCorr[s] = vec3(pp.x - pa.x, pp.y - pa.y, 0.f);
            A.corrYaw[s] = wrapAngle(A.plantYaw[s] - fyaw[s]);
            A.pivotW[s] = approach(A.pivotW[s], 0.f, dt * 4.f);
            if (Min(heel[s].z, ball[s].z) > 0.012f * scale) A.footHold[s] = false;
            else if (length2(A.plantCorr[s]) > 0.3f * 0.3f * scale * scale || fabsf(A.corrYaw[s]) > 0.75f) {
                if (A.stepT[1 - s] < 0.f) quickStep(s);
                else A.footHold[s] = false;
            }
        } else if (Min(heel[s].z, ball[s].z) > 0.012f * scale || A.moveW < 0.3f) {''')
# the pivot weight fades on free feet too
rep('''            float k = expf(-dt / 0.07f);
            A.plantCorr[s] = A.plantCorr[s] * k;
            A.corrYaw[s] *= k;
        }
    }''', '''            float k = expf(-dt / 0.07f);
            A.plantCorr[s] = A.plantCorr[s] * k;
            A.corrYaw[s] *= k;
            A.pivotW[s] = approach(A.pivotW[s], 0.f, dt * 4.f);
        }
    }''')
# any: pivot
rep('''              fabsf(pin[s]) > 5e-4f;''', '''              fabsf(pin[s]) > 5e-4f || A.pivotW[s] > 1e-3f;''')
# probes: held feet probe under themselves; carried back by one update of root motion
rep('''        if (!A.planted[s] && A.stepT[s] < 0.f) m = m + vec3(md.x, md.y, 0.f) * Min(0.35f, spd * 0.2f);
        A.probeP[s] = vec3(m.x, m.y, 0.f);''', '''        if (!A.planted[s] && !A.footHold[s] && A.stepT[s] < 0.f) m = m + vec3(md.x, md.y, 0.f) * Min(0.35f, spd * 0.2f);
        // (the game probes after its next move: carried back by one update's root motion, a planted foot's probe stays
        // under it)
        m = rotate(qBack, m) - d;
        A.probeP[s] = vec3(m.x, m.y, 0.f);''')
# IK: heel raised about the ball while pivoting
rep('''        quat want = rc * fq[s];
        if (grounded > 0.f && (fabsf(slopeY) > 0.01f || fabsf(slopeX) > 0.01f)) want = nlerp(want, tilt * want, grounded);''', '''        quat want = rc * fq[s];
        if (grounded > 0.f && (fabsf(slopeY) > 0.01f || fabsf(slopeX) > 0.01f)) want = nlerp(want, tilt * want, grounded);
        // pivoting on the ball: the heel comes up about it
        if (A.pivotW[s] * w > 1e-3f) {
            const quat rp = quatAxisAngle(rotate(want, vec3(1.f, 0.f, 0.f)), -0.2f * A.pivotW[s] * w);
            const vec3 bp = target + rotate(want, vec3(0.f, A.footBall, -A.footAnkleH));
            target = bp + rotate(rp, target - bp);
            want = normalize(rp * want);
        }''')
open(p, 'w').write(s)
print("ok")
