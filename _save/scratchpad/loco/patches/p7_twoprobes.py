import sys
p = sys.argv[1]
s = open(p).read()
def rep(old, new, cnt=1):
    global s
    n = s.count(old)
    if n != cnt:
        raise SystemExit("count %d != %d for: %s" % (n, cnt, old[:80]))
    s = s.replace(old, new)
rep('''    // ground under the feet and the slope plane. The heights are kept with the world: the root's vertical motion this
    // update (read off the ground under a foot that stayed planted, which does not move) comes out of them first, so a
    // planted foot keeps its height exactly (no lag on a slope, a curb or a stair) and only a swinging foot's new ground
    // (its probe ahead) is eased into''', '''    // ground under the feet and the slope plane. The heights are kept with the world: the root's vertical motion this
    // update (read off the ground under a foot that stayed planted, which does not move) comes out of them first, so a
    // planted foot keeps its height exactly (no lag on a slope, a curb or a stair). A swinging foot's probe alternates
    // between just ahead of it and where it will land; it swings over the higher of the two (up onto a curb or a stair
    // early enough to clear its edge, down off one only once past the edge)''')
rep('''        float* fz[2] = {&A.footL, &A.footR};
        for (int s = 0; s < 2; s++) {
            // (a swinging foot - its probe where it will land - gets there by the end of its swing: down to lower ground
            // evenly over what is left of it, so it clears the edge it is leaving or follows a slope down; up onto higher
            // ground, a curb or a stair, in the first part of it)
            float k = held[s] ? 1.f : 1.f - expf(-dt * 14.f);
            if (!held[s] && swingU[s] > 0.f && cycle > 0.f) {
                const float tRem = (1.f - swingU[s]) * (1.f - duty) * cycle / Max(Max(0.f, in.speed), 0.3f);
                k = Saturate(dt / Max((gIn[s] < *fz[s] - dzRoot ? 1.f : 0.4f) * tRem, dt));
            }
            *fz[s] += (gIn[s] - (*fz[s] - dzRoot)) * k - dzRoot;
            A.groundRaw[s] = gIn[s];''', '''        const float kg = 1.f - expf(-dt * 14.f);
        float* fz[2] = {&A.footL, &A.footR};
        for (int s = 0; s < 2; s++) {
            A.groundAhead[s] -= dzRoot;
            if (A.probeAhead[s]) {
                A.groundAhead[s] += (gIn[s] - A.groundAhead[s]) * kg;
                *fz[s] -= dzRoot;
            } else {
                *fz[s] += (gIn[s] - (*fz[s] - dzRoot)) * (held[s] ? 1.f : kg) - dzRoot;
                if (swingU[s] <= 0.f) A.groundAhead[s] = *fz[s];
            }
            A.groundRaw[s] = gIn[s];''')
rep('''    // terrain heights under the feet (probed there, or extrapolated along the slope from probes below the hips)
    float offs[2] = {A.footL, A.footR};''', '''    // terrain heights under the feet (probed there, or extrapolated along the slope from probes below the hips); a
    // swinging foot over the higher of the ground near it and where it lands
    float offs[2] = {A.footL, A.footR};
    for (int s = 0; s < 2; s++)
        if (swingU[s] > 0.f && !A.planted[s] && !A.footHold[s] && A.stepT[s] < 0.f) offs[s] = Max(offs[s], A.groundAhead[s]);''')
rep('''        // (a swinging foot's probe: where it will land, about a stride's remaining part ahead)
        if (!A.planted[s] && !A.footHold[s] && A.stepT[s] < 0.f)
            m = m + vec3(md.x, md.y, 0.f) * (swingU[s] > 0.f && cycle > 0.f ? Clamp((1.f - swingU[s]) * cycle, 0.05f, 1.2f) : Min(0.35f, spd * 0.2f));''', '''        // (a swinging foot's probe: every other update where it will land - the rest of a stride ahead - else just ahead)
        const bool swinging = swingU[s] > 0.f && cycle > 0.f && !A.planted[s] && !A.footHold[s] && A.stepT[s] < 0.f;
        A.probeAhead[s] = swinging && !A.probeAhead[s];
        if (A.probeAhead[s]) m = m + vec3(md.x, md.y, 0.f) * Clamp((1.f - swingU[s]) * cycle, 0.05f, 1.2f);
        else if (!A.planted[s] && !A.footHold[s] && A.stepT[s] < 0.f) m = m + vec3(md.x, md.y, 0.f) * Min(swinging ? 0.15f : 0.35f, spd * 0.2f);''')
open(p, 'w').write(s)
print("ok")
