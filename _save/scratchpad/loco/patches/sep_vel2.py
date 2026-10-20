#!/usr/bin/env python3
# The ped-ped separation (GameWorld::updatePeds) also removes the closing part of the pair's relative velocity, split
# with the position push's weights - as movePed does against a static collider - between two people the game moves (the
# player's own pushes through a crowd stay as they were). usage: sep_vel2.py <peds.cpp>
import sys
p = sys.argv[1]
s = open(p).read()
old = '''                float wa = a.isPlayer ? 0.25f : 0.5f, wb = b.isPlayer ? 0.25f : 0.5f;
                a.pos = a.pos - dvec3(n.x * push * wa * 2.f, n.y * push * wa * 2.f, 0);
                b.pos = b.pos + dvec3(n.x * push * wb * 2.f, n.y * push * wb * 2.f, 0);
'''
new = '''                float wa = a.isPlayer ? 0.25f : 0.5f, wb = b.isPlayer ? 0.25f : 0.5f;
                a.pos = a.pos - dvec3(n.x * push * wa * 2.f, n.y * push * wa * 2.f, 0);
                b.pos = b.pos + dvec3(n.x * push * wb * 2.f, n.y * push * wb * 2.f, 0);
                // ... and between two people the game moves, the closing part of their relative velocity goes too, shared the
                // same way (as movePed does against a static collider): someone walking into another's back carries the speed
                // they actually make, which their gait (animatePed) and the stuck test of the AI's walk (aiWalkRound) then
                // see. (Not with the player: a push through a crowd, or one taken from it, feels as it did)
                const float vc = (b.vel.x - a.vel.x) * n.x + (b.vel.y - a.vel.y) * n.y;
                if (vc < 0.f && !a.isPlayer && !b.isPlayer) {
                    a.vel = a.vel + vec3(n.x, n.y, 0.f) * (vc * wa);
                    b.vel = b.vel - vec3(n.x, n.y, 0.f) * (vc * wb);
                }
'''
assert s.count(old) == 1, 'separation block not found'
s = s.replace(old, new)
open(p, 'w').write(s)
print('patched', p)
