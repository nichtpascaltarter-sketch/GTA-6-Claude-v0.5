#!/usr/bin/env python3
# Planted feet compensated by the root's actual displacement (AnimInput::rootMove) instead of velocity x dt: pushes
# (ped-ped separation, collider push-outs) and snaps no longer drag them. usage: p15_rootmove.py <tree> [--header-only]
import sys, os
tree = sys.argv[1]
header_only = len(sys.argv) > 2 and sys.argv[2] == '--header-only'
ch = os.path.join(tree, 'src/anim/character.h')
an = os.path.join(tree, 'src/anim/animator.cpp')

s = open(ch).read()
old = '''    vec3 passBy = vec3(0);
    vec2 passVel = vec2(0);
    float passWeight = 0;
'''
new = old + '''    // the root's own displacement since the last update, in this update's model space (the game's position change,
    // pushes and all): planted feet keep their places by it (else by speed x dt along localMoveDir)
    vec3 rootMove = vec3(0);
    bool rootMoveValid = false;
'''
assert s.count(old) == 1
s = s.replace(old, new)
open(ch, 'w').write(s)
if header_only:
    print('header ok')
    sys.exit(0)

s = open(an).read()
old = '''    const vec3 d = vec3(md.x, md.y, 0.f) * (spd * dt);'''
new = '''    // (by the root's actual move when the game gives it: a push or a snap carries the body, not the planted feet)
    const vec3 d = in.rootMoveValid ? vec3(in.rootMove.x, in.rootMove.y, 0.f) : vec3(md.x, md.y, 0.f) * (spd * dt);'''
assert s.count(old) == 1
s = s.replace(old, new)
open(an, 'w').write(s)
print('ok')
