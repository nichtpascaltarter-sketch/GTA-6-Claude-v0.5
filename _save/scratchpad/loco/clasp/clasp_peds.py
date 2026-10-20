import sys, os
# the couple block (peds.cpp): each one's point now just past the middle (the grip centres a hand's half thickness past
# the middle, palms meeting there) rather than a fist's width past it. CLASP_PAST env overrides (test builds).
fn = sys.argv[1]
past = os.environ.get('CLASP_PAST', '0.05')
s = open(fn).read()
def rep(old, new):
    global s
    if s.count(old) != 1: sys.exit('count %d for: %s' % (s.count(old), old[:80]))
    s = s.replace(old, new)
rep("""    // a couple walking hand in hand (pedai.cpp: PedAI::handWith on both, renewed every frame by the companion): each
    // one's inner hand on the point between them, a little above where it hangs (an arm at full stretch is let go of:
    // the animator's reach), the animator picking the hand on that side. The animator closes a fist round a handle a
    // little short of the point, so each one's point is a fist's width past the middle: the two hands in one clasp.""",
"""    // a couple walking hand in hand (pedai.cpp: PedAI::handWith on both, renewed every frame by the companion): each
    // one's inner hand on the point between them, a little above where it hangs (an arm at full stretch is let go of:
    // the animator's reach), the animator picking the hand on that side. The animator closes the hand round a handle a
    // little short of the point, and the clasp's handle is the other one's hand: each one's point is that much past
    // the middle, the palms meeting there.""")
rep("""            vec2 m = vec2(j.x, j.y) * 0.5f + normalize(vec2(j.x, j.y) + vec2(1e-4f, 0.f)) * 0.08f;""",
    """            vec2 m = vec2(j.x, j.y) * 0.5f + normalize(vec2(j.x, j.y) + vec2(1e-4f, 0.f)) * %sf;""" % past)
open(fn, 'w').write(s)
print('patched', fn, 'past', past)
