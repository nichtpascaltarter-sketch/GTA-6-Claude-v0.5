T='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour/'
# v3l (10-20 13:5x): a diagnostic for feet across the end of the wall between the switchback's flights at speed.
# LOCO_SWONLY=1: the player's switchback segments only (run and sprint, up and down, the laps as set). The player's last
# 14 frames per foot on the switchback are kept; when a planted foot first comes 12 cm or more inside a step (or over a
# drop of 40 cm or more under its ball or toe), they are logged: heel / ball / toe across (x) and along (a) the
# switchback and their height over the ground there, the root, the yaw, the speed, the foot's state, and its ground probe
# as the game reads it (where, and the clamped height).
s=open(T+'block_player_v3k.cpp').read()
def sub(s, old, new, n=1):
    c=s.count(old)
    if c!=n: raise SystemExit('expected %d found %d: %r' % (n, c, old[:90]))
    return s.replace(old,new)
s=sub(s, """    int lqDeepLog = 0;
""", """    int lqDeepLog = 0;
    // (v3l) the player's last frames per foot on the switchback, logged when a planted foot first goes inside a step
    struct LqTr {
        float t = 0.f, stepT = 0.f, age = 0.f, yaw = 0.f, v = 0.f, probe = 0.f;
        vec3 P[3];
        float h[3] = {0.f, 0.f, 0.f};
        vec3 root = vec3(0.f);
        vec2 probeXA = vec2(0.f);
        bool planted = false, hold = false, scan = false;
        float stairLand = -9.f;
    };
    LqTr lqTr[2][14];
    int lqTrN[2] = {0, 0};
    bool lqTrBad[2] = {false, false};
    int lqTrDumps = 0;
    vec2 lqXA(vec2 w) const { const vec2 d = w - lqO; return vec2(dot(d, lqX), dot(d, lqA)); }
""")
# LOCO_SWONLY: the switchback segments only, run and sprint
s=sub(s, """            static const bool kChaseOnly = getenv("LOCO_CHASE") && atoi(getenv("LOCO_CHASE")) > 0;
            for (int lap = 0; lap < kLaps; lap++)
                for (int g = kChaseOnly ? 1 : 0; g < 3; g++)
                    for (int stc = 0; stc < (kChaseOnly ? 1 : 2); stc++)
                        for (int up = 1; up >= 0; up--) lqSegs.push_back({stc, up, g});""", """            static const bool kChaseOnly = getenv("LOCO_CHASE") && atoi(getenv("LOCO_CHASE")) > 0;
            static const bool kSwOnly = getenv("LOCO_SWONLY") && atoi(getenv("LOCO_SWONLY")) > 0;   // (v3l: the switchback run and sprinted only)
            for (int lap = 0; lap < kLaps; lap++)
                for (int g = kChaseOnly || kSwOnly ? 1 : 0; g < 3; g++)
                    for (int stc = kSwOnly ? 1 : 0; stc < (kChaseOnly ? 1 : 2); stc++)
                        for (int up = 1; up >= 0; up--) lqSegs.push_back({stc, up, g});""")
# the record and the dump, in the measure
s=sub(s, """            if (p.anim.planted[s]) {
                S.frames++;
                S.lowest = Min(S.lowest, low);""", """            if (bucketStair == 1 && p.isPlayer) {
                // (v3l) this frame kept; a planted foot first inside a step 12 cm or more, or over a drop of 40 cm or
                // more under its ball or toe: its last frames logged
                LqTr& r = lqTr[s][lqTrN[s] % 14];
                lqTrN[s]++;
                const vec3 base = p.pos.toVec3();
                r.t = tourT;
                r.planted = p.anim.planted[s];
                r.hold = p.anim.footHold[s];
                r.stepT = p.anim.stepT[s];
                r.age = p.anim.plantAge[s];
                r.stairLand = p.anim.stairLand[s];
                r.scan = p.anim.wantsGroundScan();
                for (int k = 0; k < 3; k++) r.P[k] = P[k], r.h[k] = hh[k];
                r.root = base;
                r.yaw = p.yaw;
                r.v = length(vec2(p.vel.x, p.vel.y));
                const vec3 pm = p.anim.footProbe(s);
                const float cy = cosf(p.yaw), sy = sinf(p.yaw);
                const vec2 pw(base.x + pm.x * cy - pm.y * sy, base.y + pm.x * sy + pm.y * cy);
                const Phys::GroundHit gh = collision.ground(pw.x, pw.y, base.z + 0.3f, 0.55f);
                r.probe = gh.z > -1e8f ? Clamp(gh.z - base.z, -0.45f, 0.45f) : 0.f;
                r.probeXA = lqXA(pw);
                const bool bad = r.planted && (low < -0.12f || hh[1] > 0.4f || hh[2] > 0.4f);
                if (bad && !lqTrBad[s] && lqTrDumps < 60) {
                    lqTrDumps++;
                    LOG("locoplayer trace %d: switchback %s %s foot %d, seg %d", lqTrDumps, bucketUp ? "down" : "up", gait == 1 ? "run" : "sprint", s, lqSeg);
                    const int n = Min(lqTrN[s], 14);
                    for (int i = n - 1; i >= 0; i--) {
                        const LqTr& q = lqTr[s][(lqTrN[s] - 1 - i) % 14];
                        const vec2 hx = lqXA(q.P[0].xy()), bx = lqXA(q.P[1].xy()), tx = lqXA(q.P[2].xy()), rx = lqXA(q.root.xy());
                        const float yawA = AI::wrapPi(q.yaw - AI::dirYaw(lqA)) * 57.3f;
                        LOG("locoplayer trace %d f-%2d: pl%d hd%d st%5.2f age%.2f land%5.2f scan%d | heel %5.2f %5.2f %+6.1f ball %5.2f %5.2f %+6.1f toe %5.2f %5.2f %+6.1f | root %5.2f %5.2f z%.2f yaw%+4.0f v%.2f | probe %5.2f %5.2f %+.2f",
                            lqTrDumps, i, (int)q.planted, (int)q.hold, q.stepT, q.age, q.stairLand, (int)q.scan, hx.x, hx.y, q.h[0] * 100.f, bx.x, bx.y, q.h[1] * 100.f, tx.x, tx.y,
                            q.h[2] * 100.f, rx.x, rx.y, q.root.z - lqZ0, yawA, q.v, q.probeXA.x, q.probeXA.y, q.probe);
                    }
                }
                lqTrBad[s] = bad;
            }
            if (p.anim.planted[s]) {
                S.frames++;
                S.lowest = Min(S.lowest, low);""")
s=sub(s, """        LOG("locoplayer: switchback at %.1f %.1f %.2f, flights along %.2f %.2f (%d boxes, 17 cm risers)", lqO.x, lqO.y, lqZ0, lqA.x, lqA.y, (int)boxes.size());""",
"""        LOG("locoplayer: switchback at %.1f %.1f %.2f, flights along %.2f %.2f (%d boxes, 17 cm risers; traced)", lqO.x, lqO.y, lqZ0, lqA.x, lqA.y, (int)boxes.size());""")
open(T+'block_player_v3l.cpp','w').write(s)
print('v3l written', len(s))
