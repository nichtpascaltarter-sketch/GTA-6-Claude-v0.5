import sys
p = sys.argv[1]
s = open(p).read()
def rep(old, new, cnt=1):
    global s
    n = s.count(old)
    if n != cnt:
        raise SystemExit("count %d != %d for: %s" % (n, cnt, old[:80]))
    s = s.replace(old, new)
rep('''static void footPlanting(Animator& A, const AnimInput& in, float dt, Pose& p, bool planting, bool terrain, float duty) {
    const Skeleton& sk = *A.skel;''', '''static void footPlanting(Animator& A, const AnimInput& in, float dt, Pose& p, bool planting, bool terrain, float duty, float cycle) {
    const Skeleton& sk = *A.skel;
    const bool walking = A.moveW > 0.3f && duty > 0.05f;
    // how far through its swing each foot is (0 in stance or standing)
    float swingU[2] = {0.f, 0.f};
    if (walking)
        for (int s = 0; s < 2; s++) {
            float ph = A.phase - (s ? 0.5f : 0.f);
            ph -= floorf(ph);
            swingU[s] = ph > duty ? (ph - duty) / Max(1.f - duty, 0.05f) : 0.f;
        }''')
rep('''        const float kg = 1.f - expf(-dt * 14.f);
        float* fz[2] = {&A.footL, &A.footR};
        for (int s = 0; s < 2; s++) {
            *fz[s] += (gIn[s] - (*fz[s] - dzRoot)) * (held[s] ? 1.f : kg) - dzRoot;''', '''        float* fz[2] = {&A.footL, &A.footR};
        for (int s = 0; s < 2; s++) {
            // (a swinging foot rises to higher ground ahead - a curb, a stair - at once, but goes down to lower ground only
            // late in its swing: it clears the edge it is leaving)
            const bool down = gIn[s] < *fz[s] - dzRoot;
            const float k = held[s] ? 1.f : 1.f - expf(-dt * (down ? (swingU[s] > 0.f && swingU[s] < 0.55f ? 2.f : 14.f) : 20.f));
            *fz[s] += (gIn[s] - (*fz[s] - dzRoot)) * k - dzRoot;''')
rep('''    const bool walking = A.moveW > 0.3f && duty > 0.05f;
    if (A.plantOn > 0.f) {''', '''    if (A.plantOn > 0.f) {''')
rep('''        if (!A.planted[s] && !A.footHold[s] && A.stepT[s] < 0.f) m = m + vec3(md.x, md.y, 0.f) * Min(0.35f, spd * 0.2f);''', '''        // (a swinging foot's probe: where it will land, about a stride's remaining part ahead)
        if (!A.planted[s] && !A.footHold[s] && A.stepT[s] < 0.f)
            m = m + vec3(md.x, md.y, 0.f) * (swingU[s] > 0.f && cycle > 0.f ? Clamp((1.f - swingU[s]) * cycle, 0.05f, 1.2f) : Min(0.35f, spd * 0.2f));''')
rep('''    float locoDuty = 0.f;''', '''    float locoDuty = 0.f, locoCycle = 0.f;''')
rep('''        rate = Min(rate, 2.4f) / ls;''', '''        rate = Min(rate, 2.4f) / ls;
        locoCycle = rate > 1e-3f ? v / rate : 0.f;   // ground covered per gait cycle (a foot's swing)''')
rep('''        if (!cheap) footPlanting(*this, in, dt, outp, planting, footIK, locoDuty);''', '''        if (!cheap) footPlanting(*this, in, dt, outp, planting, footIK, locoDuty, locoCycle);''')
open(p, 'w').write(s)
print("ok")
