import sys, os
# the hand-in-hand clasp (animator.cpp, the hold block's "hand held along the way" branch): the hand turned about its
# palm normal - the right hand's fingers forward and down, the left's back and down, so the two hands of a couple cross
# palm to palm, each one's fingers round the back of the other's - and closed round the other's hand (thicker than the
# fist's handle allowance). CLASP_ANG / CLASP_CURL env override the angle / curl (test builds).
fn = sys.argv[1]
ang = os.environ.get('CLASP_ANG', '0.6')
curl = os.environ.get('CLASP_CURL', '0.85')
s = open(fn).read()
def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit('count %d for: %s' % (s.count(old), old[:80]))
    s = s.replace(old, new)
rep("""            vec3 axis = vec3(0, 0, r ? 1.f : -1.f);
            const vec3 ga = nrmOr(in.grabAxis, vec3(0, 0, 1));
            if (fabsf(ga.z) < 0.7f) {
                // a hand held along the way: hanging, the fingers down, the palm square to the way and towards the other
                const vec3 side = nrmOr(vec3(ga.y, -ga.x, 0.f), palm);
                palm = dot(side, palm) >= 0.f ? side : -side;
                axis = r ? cross(vec3(0, 0, -1), palm) : cross(palm, vec3(0, 0, -1));
            }
            // (the fist sits on its near side: an arm is thicker than the fingers close round)
            holdGrip(sk, outp, r, in.grabTarget - palm * 0.035f, axis, palm, ps + vec3(r ? 0.35f : -0.35f, -0.1f, -1.f) * 0.5f, 0.7f, 0.6f,
                     holdW);""",
"""            vec3 axis = vec3(0, 0, r ? 1.f : -1.f);
            float curl = 0.7f;
            const vec3 ga = nrmOr(in.grabAxis, vec3(0, 0, 1));
            if (fabsf(ga.z) < 0.7f) {
                // a hand held along the way: hanging, the palm square to the way and towards the other's, the fingers down
                // and turned about the palm - a right hand's forward, a left hand's back - so the two hands of one clasp
                // cross palm to palm, each one's fingers closing round the back of the other's hand
                const vec3 side = nrmOr(vec3(ga.y, -ga.x, 0.f), palm);
                palm = dot(side, palm) >= 0.f ? side : -side;
                axis = rotate(qaa(palm, %sf), r ? cross(vec3(0, 0, -1), palm) : cross(palm, vec3(0, 0, -1)));
                curl = %sf;
            }
            // (the fist sits on its near side: an arm is thicker than the fingers close round)
            holdGrip(sk, outp, r, in.grabTarget - palm * 0.035f, axis, palm, ps + vec3(r ? 0.35f : -0.35f, -0.1f, -1.f) * 0.5f, curl, 0.6f,
                     holdW);""" % (ang, curl))
open(fn, 'w').write(s)
print('patched', fn, 'ang', ang, 'curl', curl)
