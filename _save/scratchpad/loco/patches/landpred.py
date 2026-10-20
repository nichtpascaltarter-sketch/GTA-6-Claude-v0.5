import sys
# landing predictions: (1) the strike lead read at a plant made after the clip's strike gets the body's travel since added
# back; (2) a body held back (pressed against someone, its velocity and gait unchanged) predicts only the share of the
# gait's travel it actually covers; (3) down a flight, the landing heel keeps 4 cm more off the riser behind. The toe
# clearance's half-update term is kept, re-explained. usage: landpred.py <animator.cpp> <character.h>
an, ch = sys.argv[1], sys.argv[2]
def patch(fn, pairs):
    s = open(fn).read()
    for old, new in pairs:
        if s.count(old) != 1: sys.exit('count %d in %s for: %s' % (s.count(old), fn, old[:90]))
        s = s.replace(old, new)
    open(fn, 'w').write(s)
patch(ch, [
("""    float strikeLead = 0.3f;                  // where the pose's heel strikes land ahead of the body, in strides (learned)
""",
"""    float strikeLead = 0.3f;                  // where the pose's heel strikes land ahead of the body, in strides (learned)
    float rootRatio = 1.f;                    // the share of its gait's travel the body actually covers (held back: below 1)
"""),
])
patch(an, [
# (3) + the toe line's comment
("""            // (the toes come forward 3-5 cm over the stance, rolling over the ball; and the heel strike is seen up to an update
            // late, the foot coming down that much further on)
            if (upAhead) hi = f - mg - footLen - 0.055f * scale - 0.5f * Max(in.speed, 0.f) * dt;
            else if (frontEdge) hi = f - mg - L;
            if (upBehind) lo = r + mg;""",
"""            // (the toes come forward 3-5 cm over the stance, rolling over the ball; and the landing is fitted again at most
            // once an update, so a coarse update leaves it further off when the walk changes pace. Down a flight, a landing
            // that falls short puts the heel on the step behind, one that goes long only takes the toes further over the
            // nose: the heel keeps 4 cm more off the riser behind)
            if (upAhead) hi = f - mg - footLen - 0.055f * scale - 0.5f * Max(in.speed, 0.f) * dt;
            else if (frontEdge) hi = f - mg - L;
            if (upBehind) lo = r + mg + 0.04f * scale;"""),
# (1)
("""                    // (where the pose lands its heel strikes ahead of the body, in strides, the swing still to go taken off:
                    // the landing predictions use it)
                    if (!A.footHold[s] && cycle > 0.3f && A.moveW > 0.9f && fabsf(in.turnRate) < 0.5f) {
                        const vec3 mp = (heel[s] + ball[s]) * 0.5f;
                        const float obs = dot(vec2(mp.x, mp.y), md) / cycle - (swingU[s] > 0.f ? (1.f - swingU[s]) * (1.f - duty) : 0.f);""",
"""                    // (where the pose lands its heel strikes ahead of the body, in strides: the swing still to go taken off,
                    // or - planted an update or two after the strike, the pose's foot gone back by the body's travel since -
                    // that travel added back. The landing predictions use it)
                    if (!A.footHold[s] && cycle > 0.3f && A.moveW > 0.9f && fabsf(in.turnRate) < 0.5f) {
                        const vec3 mp = (heel[s] + ball[s]) * 0.5f;
                        const float obs = dot(vec2(mp.x, mp.y), md) / cycle - (swingU[s] > 0.f ? (1.f - swingU[s]) * (1.f - duty) : -ph);"""),
# (2) the ratio
("""    const float dpsi = in.turnRate * dt;
    const quat qBack = qz(-dpsi);
    for (int s = 0; s < 2; s++) {
        A.plantP[s] = rotate(qBack, A.plantP[s]) - d;""",
"""    const float dpsi = in.turnRate * dt;
    const quat qBack = qz(-dpsi);
    // (a body held back - pressed against someone, its velocity and so its gait unchanged - covers less ground than its
    // gait: the landing predictions take only that share of the gait's travel to the strike)
    {
        float r = 1.f;
        if (in.rootMoveValid && walking && spd > 0.3f && dt > 1e-4f) r = Clamp(dot(vec2(in.rootMove.x, in.rootMove.y), md) / (spd * dt), 0.f, 1.f);
        A.rootRatio += (r - A.rootRatio) * (1.f - expf(-dt / 0.05f));
    }
    for (int s = 0; s < 2; s++) {
        A.plantP[s] = rotate(qBack, A.plantP[s]) - d;"""),
("""            m = m + vec3(md.x, md.y, 0.f) * (Clamp(cycle * ((1.f - swingU[s]) * (1.f - duty) + A.strikeLead), 0.05f, 1.5f) + A.stairShift[s] +""",
"""            m = m + vec3(md.x, md.y, 0.f) * (Clamp(cycle * ((1.f - swingU[s]) * (1.f - duty) * A.rootRatio + A.strikeLead), 0.05f, 1.5f) + A.stairShift[s] +"""),
("""            lm = lm + vec3(mdv.x, mdv.y, 0.f) * (cycle * ((1.f - swingU[s]) * (1.f - duty) + A.strikeLead) - dot(vec2(lm.x, lm.y), mdv));""",
"""            lm = lm + vec3(mdv.x, mdv.y, 0.f) * (cycle * ((1.f - swingU[s]) * (1.f - duty) * A.rootRatio + A.strikeLead) - dot(vec2(lm.x, lm.y), mdv));"""),
("""    strikeLead = 0.3f;
    footEvents = 0;""",
"""    strikeLead = 0.3f;
    rootRatio = 1.f;
    footEvents = 0;"""),
])
print('patched', an, ch)
