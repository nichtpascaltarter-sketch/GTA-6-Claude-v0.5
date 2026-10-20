import sys
# test-only: LOCO_STOPS=1 makes the locostairs walkers stop on the flights - while on one, a 2 s stop after every 2.5 s
# of walking (as the harness's stairs17.stops) - and counts a standing walker's feet by the way it is heading
fn = sys.argv[1]
s = open(fn).read()
def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit('count %d for: %s' % (s.count(old), old[:80]))
    s = s.replace(old, new)
rep("""        int up = 0;   // heading up to the top (1) or down to the foot (0)
    };""", """        int up = 0;   // heading up to the top (1) or down to the foot (0)
        float walkT = 0.f, pauseUntil = -1.f;   // (LOCO_STOPS: walking time on the flights since the last stop; the stop's end)
        dvec3 pauseP;
    };""")
rep("""        vec2 v(p.vel.x, p.vel.y);
        return dot(v, lsDir) < 0.f ? 0 : 1;   // 0 going up, 1 going down""", """        vec2 v(p.vel.x, p.vel.y);
        if (length(v) < 0.3f)
            for (const LsPed& L : lsPeds)
                if (L.uid == p.uid) return L.up ? 0 : 1;   // (standing: by the way it is heading)
        return dot(v, lsDir) < 0.f ? 0 : 1;   // 0 going up, 1 going down""")
rep("""            if (p.state == PS_ONFOOT && p.health > 0.f) lsSend(L);
        }""", """            static const bool kStops = getenv("LOCO_STOPS") != nullptr;
            if (kStops && p.state == PS_ONFOOT && p.health > 0.f && lsT0 > 0.f) {
                if (L.pauseUntil > tourT) {
                    p.brain.type = BRAIN_GOTO;
                    p.brain.goal = L.pauseP;
                    continue;
                }
                vec2 d = p.pos.toVec3().xy() - lsTopC.xy();
                float a = dot(d, lsDir);
                if (a > 0.5f && a < lsLen - 0.5f && fabsf(dot(d, lsRight)) < 1.f) L.walkT += dt;
                if (L.walkT > 2.5f) {
                    L.walkT = 0.f;
                    L.pauseUntil = tourT + 2.f;
                    L.pauseP = p.pos;
                    p.brain.type = BRAIN_GOTO;
                    p.brain.goal = L.pauseP;
                    continue;
                }
            }
            if (p.state == PS_ONFOOT && p.health > 0.f) lsSend(L);
        }""")
open(fn, 'w').write(s)
