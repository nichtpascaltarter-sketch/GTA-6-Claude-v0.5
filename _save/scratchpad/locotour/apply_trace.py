import sys
# test-only: LOCO_TRACEPED="uid,uid,..." logs every measured frame of those peds in the locotour: the body's move and
# turn, the walk's weight and phase, and per foot its state (planted, held, stepping), correction, pivot and sole heights
fn = sys.argv[1]
s = open(fn).read()
def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit('count %d for: %s' % (s.count(old), old[:80]))
    s = s.replace(old, new)
rep("""            L.yawPrev = p.yaw;
            L.seen = true;
            for (int s = 0; s < 2; s++) {
                vec3 P[3];
                locoFootPoints(p, s, P);
                bool down[3];
                float low = 1e9f;
                for (int k = 0; k < 3; k++) {
                    float gz = game.groundHeight(P[k].x, P[k].y, P[k].z - 0.25f);
                    float h = gz > -1e8f ? P[k].z - gz : 1.f;
                    down[k] = h < kDown;
                    low = Min(low, h);
                }""",
"""            L.yawPrev = p.yaw;
            L.seen = true;
            static std::vector<u32> traceIds;
            static bool traceInit = false;
            if (!traceInit) {
                traceInit = true;
                if (const char* e = getenv("LOCO_TRACEPED"))
                    for (const char* q = e; *q;) {
                        traceIds.push_back((u32)strtoul(q, nullptr, 10));
                        while (*q && *q != ',') q++;
                        if (*q == ',') q++;
                    }
            }
            const bool trace = std::find(traceIds.begin(), traceIds.end(), p.uid) != traceIds.end();
            std::string tl;
            for (int s = 0; s < 2; s++) {
                vec3 P[3];
                locoFootPoints(p, s, P);
                bool down[3];
                float low = 1e9f, hh[3];
                for (int k = 0; k < 3; k++) {
                    float gz = game.groundHeight(P[k].x, P[k].y, P[k].z - 0.25f);
                    float h = gz > -1e8f ? P[k].z - gz : 1.f;
                    hh[k] = h;
                    down[k] = h < kDown;
                    low = Min(low, h);
                }
                if (trace)
                    tl += StrFormat(" | %d pl%d hd%d st%.2f pc%.3f cy%5.2f pv%.2f age%.2f h%5.1f b%5.1f t%5.1f x%.3f y%.3f", s, (int)p.anim.planted[s],
                                    (int)p.anim.footHold[s], p.anim.stepT[s], length(p.anim.plantCorr[s]), p.anim.corrYaw[s], p.anim.pivotW[s],
                                    p.anim.plantAge[s], hh[0] * 100.f, hh[1] * 100.f, hh[2] * 100.f, P[0].x, P[0].y);""")
rep("""                for (int k = 0; k < 3; k++) F.prev[k] = P[k];
                F.prevYaw = fyaw;
            }""",
"""                for (int k = 0; k < 3; k++) F.prev[k] = P[k];
                F.prevYaw = fyaw;
            }
            if (trace) {
                vec2 fw(-sinf(p.yaw), cosf(p.yaw)), rt(cosf(p.yaw), sinf(p.yaw)), v(p.vel.x, p.vel.y);
                LOG("locotour trace %u t%.3f pos %.2f %.2f v %.2f (f%.2f s%.2f) yaw %.1f tr%5.2f in %.2f md %.2f,%.2f mw %.2f ph %.2f stance %d act %d%s", p.uid,
                    tourT, p.pos.x, p.pos.y, length(v), dot(v, fw), dot(v, rt), p.yaw * 57.3f, p.animIn.turnRate, p.animIn.speed, p.animIn.localMoveDir.x,
                    p.animIn.localMoveDir.y, p.anim.moveW, p.anim.phase, p.animIn.stance, (int)game.pedAI(i).activity, tl.c_str());
            }""")
open(fn, 'w').write(s)
