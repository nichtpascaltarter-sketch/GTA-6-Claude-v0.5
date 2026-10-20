#!/usr/bin/env python3
# curves2.py <animator.cpp in> <animator.cpp out> <character.h in> <character.h out>   (on c15b: animator 16e6ee2a,
# character.h 3d37b201)
#  - the inner foot's lateral shift going down round a tight bend follows the turn rate smoothed over about 0.6 s (a
#    stair's bend is a steady turn; a walker's weave down a straight flight is not, and it fired the shift there), and
#    only while the turn now goes the same way
#  - stairLat cleared with the other stair fields on a reset and when distant peds drop their foot work
import sys

def sub(s, old, new, count=1):
    n = s.count(old)
    if n != count:
        raise SystemExit('expected %d, found %d: %r' % (count, n, old[:120]))
    return s.replace(old, new)

src, dst, hsrc, hdst = sys.argv[1:5]
s = open(src).read()
h = open(hsrc).read()

h = sub(h, """    vec3 stairLat[2] = {vec3(0), vec3(0)};    // a swing's landing moved out from a tight bend's centre going down (kept with the world)
""", """    vec3 stairLat[2] = {vec3(0), vec3(0)};    // a swing's landing moved out from a tight bend's centre going down (kept with the world)
    float turnRateS = 0.f;                    // the turn rate smoothed over about 0.6 s: a stair's steady bend, not a weave (rad/s)
""")

s = sub(s, """static void footPlanting(Animator& A, const AnimInput& in, float dt, Pose& p, bool planting, bool terrain, float duty, float cycle) {
    const Skeleton& sk = *A.skel;
""", """static void footPlanting(Animator& A, const AnimInput& in, float dt, Pose& p, bool planting, bool terrain, float duty, float cycle) {
    const Skeleton& sk = *A.skel;
    // (a stair's bend - a spiral's, winders' - is a steady turn; a walker's weave down a straight flight is not)
    A.turnRateS += (in.turnRate - A.turnRateS) * (1.f - expf(-dt / 0.6f));
""")

s = sub(s, """            // (down round a tight bend on stairs - a spiral, winders - the inner foot lands further out than its gait would
            // put it, where the wedge treads are deeper: 3 cm for each 1/m of the bend's curvature, at most 7 cm)
            vec3 latNew(0.f);
            if (A.stairSeen < 1.5f && A.rootVz < -0.1f && (in.turnRate > 0.f ? s == 0 : s == 1)) {
                const float curv = fabsf(in.turnRate) / Max(spd, 0.3f);
                const vec3 ltw = rotate(qz(turnOver(sT)), vec3(-md.y, md.x, 0.f));
                latNew = ltw * ((in.turnRate > 0.f ? -1.f : 1.f) * Min(0.03f * curv, 0.07f) * scale);
            }
""", """            // (down round a tight bend on stairs - a spiral, winders - the inner foot lands further out than its gait would
            // put it, where the wedge treads are deeper: 3 cm for each 1/m of the bend's steady curvature, at most 7 cm)
            vec3 latNew(0.f);
            if (A.stairSeen < 1.5f && A.rootVz < -0.1f && A.turnRateS * in.turnRate > 0.f && (A.turnRateS > 0.f ? s == 0 : s == 1)) {
                const float curv = fabsf(A.turnRateS) / Max(spd, 0.3f);
                const vec3 ltw = rotate(qz(turnOver(sT)), vec3(-md.y, md.x, 0.f));
                latNew = ltw * ((A.turnRateS > 0.f ? -1.f : 1.f) * Min(0.03f * curv, 0.07f) * scale);
            }
""")

s = sub(s, """        stairLand[k] = -9.f;
        stairRear[k] = 9.f;
        stairBall[k] = ballProbe[k] = false;
""", """        stairLand[k] = -9.f;
        stairRear[k] = 9.f;
        stairLat[k] = vec3(0);
        stairBall[k] = ballProbe[k] = false;
""")
s = sub(s, """    plantOn = bodyLag = headLead = accS = accV = prevSpeed = stepShift = legSink = 0.f;
""", """    plantOn = bodyLag = headLead = accS = accV = prevSpeed = stepShift = legSink = turnRateS = 0.f;
""")
s = sub(s, """                stairLand[s] = -9.f;
                stairRear[s] = 9.f;
                stairBall[s] = ballProbe[s] = false;
""", """                stairLand[s] = -9.f;
                stairRear[s] = 9.f;
                stairLat[s] = vec3(0);
                stairBall[s] = ballProbe[s] = false;
""")
open(dst, 'w').write(s)
open(hdst, 'w').write(h)
print('curves2 applied')
