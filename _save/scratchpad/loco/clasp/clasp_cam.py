import sys
# test only (app.cpp, --autoplay couple): the camera close on the two joined hands, from behind and from in front by turns
fn = sys.argv[1]
s = open(fn).read()
old = """                vec3 cam = mid + vec3(-f * 3.0f + side * 1.1f, 1.7f);
                if (!game.lineOfSight(dvec3(cam), dvec3(mid + vec3(0.f, 0.f, 1.f)), -1, -1)) cam = mid + vec3(-f * 3.0f - side * 1.1f, 1.7f);
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(cam);
                game.rig.scriptTarget = dvec3(mid + vec3(0.f, 0.f, 0.95f));
                game.rig.scriptFov = 45.f;"""
new = """                // (test: close on the joined hands, from behind and in front by turns)
                const vec3 hj = (handW(A, !lr) + handW(B, lr)) * 0.5f;
                const bool front = (shots & 1) != 0;
                vec3 cam = hj + vec3((front ? f : -f) * 0.85f + side * 0.3f, 0.25f);
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(cam);
                game.rig.scriptTarget = dvec3(hj);
                game.rig.scriptFov = 40.f;"""
if s.count(old) != 1: sys.exit('count %d' % s.count(old))
s = s.replace(old, new)
open(fn, 'w').write(s)
print('patched', fn)
