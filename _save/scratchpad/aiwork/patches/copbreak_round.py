# coffee break: the walk to the standing spot goes round the car's end too (the driver gets out on the street side)
p='/home/user/GTA-6-Claude-v0.5/src/game/pedai.cpp'
s=open(p).read()
a="""                    VehAI& hva = vehAI(hv);
                    const Vehicle& hvv = vehicles[hv];
                    vec2 cpos = hvv.sim.body.pos.toVec3().xy();
                    if (hva.copBreak == 2 && pa.actTimer > 0.f) {
                        vec2 to = pa.anchor - pos;
                        float d = length(to);
                        if (d > 0.4f) {
                            desired = to / d * Min(1.3f, d * 1.5f + 0.3f);
                            faceYaw = atan2f(-desired.x, desired.y);
                        } else {"""
b="""                    VehAI& hva = vehAI(hv);
                    const Vehicle& hvv = vehicles[hv];
                    const Vehicles::VehicleModel& hs = vassets[hvv.model].spec;
                    vec2 cpos = hvv.sim.body.pos.toVec3().xy();
                    // (to a point on the far side of the car: round its end, not through the bodywork)
                    auto roundCar = [&](vec2 target) {
                        vec2 cfw = normalize(hvv.sim.forward().xy() + vec2(1e-4f, 0.f)), crt = AI::rightOf(cfw);
                        vec2 lp = pos - cpos;
                        float lx = dot(lp, crt), ly = dot(lp, cfw), dx = dot(target - cpos, crt);
                        if (lx * dx >= 0.f || fabsf(lx) <= hs.boxHalf.x * 0.5f) return target;
                        float endSign = aiCarEndToWalkRound(*this, hv, ly >= 0.f ? 1.f : -1.f);
                        float side = fabsf(ly) < hs.boxHalf.y + 0.6f || ly * endSign < 0.f ? (lx >= 0.f ? 1.f : -1.f) : (dx >= 0.f ? 1.f : -1.f);
                        return cpos + cfw * (endSign * (hs.boxHalf.y + 0.8f)) + crt * (side * (hs.boxHalf.x + 0.6f));
                    };
                    if (hva.copBreak == 2 && pa.actTimer > 0.f) {
                        vec2 to = roundCar(pa.anchor) - pos;
                        float d = length(pa.anchor - pos), dg = length(to);
                        if (d > 0.4f) {
                            desired = to / Max(dg, 1e-3f) * Min(1.3f, dg * 1.5f + 0.3f);
                            faceYaw = atan2f(-desired.x, desired.y);
                        } else {"""
assert s.count(a)==1; s=s.replace(a,b)
a="""                    // back in through the door they got out of (another free one if that seat was taken meanwhile)
                    const Vehicles::VehicleModel& hs = vassets[hvv.model].spec;
                    int ns = Min((int)hs.seats.size(), 8);"""
b="""                    // back in through the door they got out of (another free one if that seat was taken meanwhile)
                    int ns = Min((int)hs.seats.size(), 8);"""
assert s.count(a)==1; s=s.replace(a,b)
a="""                    vec2 goal = door;
                    {
                        // (round the car's end to a door on the street side, not through the bodywork)
                        vec2 cfw = normalize(hvv.sim.forward().xy() + vec2(1e-4f, 0.f)), crt = AI::rightOf(cfw);
                        vec2 lp = pos - cpos;
                        float lx = dot(lp, crt), ly = dot(lp, cfw), dx = dot(door - cpos, crt);
                        if (lx * dx < 0.f && fabsf(lx) > hs.boxHalf.x * 0.5f) {
                            float endSign = aiCarEndToWalkRound(*this, hv, ly >= 0.f ? 1.f : -1.f);
                            float side = fabsf(ly) < hs.boxHalf.y + 0.6f || ly * endSign < 0.f ? (lx >= 0.f ? 1.f : -1.f) : (dx >= 0.f ? 1.f : -1.f);
                            goal = cpos + cfw * (endSign * (hs.boxHalf.y + 0.8f)) + crt * (side * (hs.boxHalf.x + 0.6f));
                        }
                    }
                    vec2 tg = goal - pos;"""
b="""                    vec2 goal = roundCar(door);   // (round the car's end to a door on the street side)
                    vec2 tg = goal - pos;"""
assert s.count(a)==1; s=s.replace(a,b)
open(p,'w').write(s)
print("applied copbreak_round")
