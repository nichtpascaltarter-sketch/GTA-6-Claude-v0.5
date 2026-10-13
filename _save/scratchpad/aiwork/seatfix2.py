# AI agent: officers back to their car (police.cpp FT_RETURN): near enough by the car's bodywork, not only its middle -
# behind a long car its middle is over 3 m off, and an officer stood pressed against the tail, the car waiting on them.
# Usage: seatfix2.py POLICE_CPP
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """            vec3 vp3 = vehicles[hv].sim.body.pos.toVec3();
            vec2 tov = vp3.xy() - pos;
            float dv = length(tov);
            if (dv > 3.f) {
                pa.doorWalkT = 0.f;"""
new = """            vec3 vp3 = vehicles[hv].sim.body.pos.toVec3();
            vec2 tov = vp3.xy() - pos;
            float dv = length(tov);
            // (near enough by its bodywork too, not only by its middle: stood at the tail of a long car the middle is over
            //  3 m off - on nt_ai100 an officer stayed pressed against the back of an estate car, and the car waited on them)
            const Vehicles::VehicleModel& rs = vassets[vehicles[hv].model].spec;
            vec2 rf = normalize(vehicles[hv].sim.forward().xy() + vec2(1e-4f, 0.f));
            vec2 rl = pos - (vp3 + rotate(vehicles[hv].sim.body.rot, rs.boxCenter)).xy();
            float ox = Max(fabsf(dot(rl, AI::rightOf(rf))) - rs.boxHalf.x, 0.f), oy = Max(fabsf(dot(rl, rf)) - rs.boxHalf.y, 0.f);
            if (dv > 3.f && ox * ox + oy * oy > 1.2f * 1.2f) {
                pa.doorWalkT = 0.f;"""
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)
open(p, "w", encoding="utf-8", newline="").write(s)
print("ok", p)
