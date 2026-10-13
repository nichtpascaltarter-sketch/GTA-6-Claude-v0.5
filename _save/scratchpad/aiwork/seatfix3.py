# AI agent: an officer walking round the car to a far door (police.cpp FT_RETURN, the seat's door) holds the car too -
# on nt_ai100b the driver, in first, drove off while the partner went round to the passenger door (the stranding Codex
# guessed at: with the seat claims the second one now always takes the other door). At most the 8 s door walk.
# Usage: seatfix3.py POLICE_CPP
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
old = """                    if (length(tod) > 0.7f && pa.doorWalkT < 8.f) {
                        float spd = Min(1.8f, length(tod) * 2.f + 0.4f);
                        vec2 st = aiWalkRound(*this, id, door, spd, dt) - pos;
                        desired = (length(st) > 1e-3f ? normalize(st) : normalize(tod)) * spd;
                        faceYaw = atan2f(-desired.x, desired.y);
                    } else {"""
new = """                    if (length(tod) > 0.7f && pa.doorWalkT < 8.f) {
                        float spd = Min(1.8f, length(tod) * 2.f + 0.4f);
                        vec2 st = aiWalkRound(*this, id, door, spd, dt) - pos;
                        desired = (length(st) > 1e-3f ? normalize(st) : normalize(tod)) * spd;
                        faceYaw = atan2f(-desired.x, desired.y);
                        // (the car waits for them on the way round to its door too - the one in first at the wheel drove
                        //  off from a partner walking round to the passenger door on nt_ai100b)
                        vehAI(hv).escortHold = Max(vehAI(hv).escortHold, time + 1.0);
                    } else {"""
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)
open(p, "w", encoding="utf-8", newline="").write(s)
print("ok", p)
