import sys
# the strike lead read at a plant an update or two after the clip's heel strike: the body's travel since the strike added
# back (the stance foot has moved back by it), so the landing predictions are not short by it
fn = sys.argv[1]
s = open(fn).read()
def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit('count %d for: %s' % (s.count(old), old[:90]))
    s = s.replace(old, new)
rep("""                    // (where the pose lands its heel strikes ahead of the body, in strides, the swing still to go taken off:
                    // the landing predictions use it)
                    if (!A.footHold[s] && cycle > 0.3f && A.moveW > 0.9f && fabsf(in.turnRate) < 0.5f) {
                        const vec3 mp = (heel[s] + ball[s]) * 0.5f;
                        const float obs = dot(vec2(mp.x, mp.y), md) / cycle - (swingU[s] > 0.f ? (1.f - swingU[s]) * (1.f - duty) : 0.f);""",
"""                    // (where the pose lands its heel strikes ahead of the body, in strides: the swing still to go taken off,
                    // or - planted an update or two after the strike, the pose's foot gone back by the body's travel since -
                    // that travel added back. The landing predictions use it)
                    if (!A.footHold[s] && cycle > 0.3f && A.moveW > 0.9f && fabsf(in.turnRate) < 0.5f) {
                        const vec3 mp = (heel[s] + ball[s]) * 0.5f;
                        const float obs = dot(vec2(mp.x, mp.y), md) / cycle - (swingU[s] > 0.f ? (1.f - swingU[s]) * (1.f - duty) : -ph);""")
open(fn, 'w').write(s)
print('patched', fn)
