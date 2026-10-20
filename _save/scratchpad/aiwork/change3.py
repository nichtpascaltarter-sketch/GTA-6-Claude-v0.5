# AI agent: change 3 - walking turns a gait can follow (the coordinator's item from the animation agent's locotour: quick
# turns at walking pace slid the feet). ai_game.h ai_turn: the body's turn rate by the speed the walker is going at (4 rad/s
# on the spot, 2.4 from 0.4 m/s up), the way it goes eased to the body (an arc through a turn, not a crab across the old
# line), the pace easing off with a sharp turn's size (all but stopped to turn round). Used by pedai.cpp's walkers (in
# place of the 3 rad/s cap on the asked speed), ai.cpp's BRAIN_FOLLOW / BRAIN_GOTO (8 rad/s before) and police.cpp's
# on-foot steps (6-9 rad/s before). Usage: change3.py SRC_GAME_DIR (applied after change2.py)
import re, sys
D = sys.argv[1].rstrip("/")

def load(f):
    return open(D + "/" + f, encoding="utf-8").read()

def save(f, s):
    open(D + "/" + f, "w", encoding="utf-8", newline="").write(s)

# ---- ai_game.h: the helpers
s = load("ai_game.h")
anchor = "\n}  // namespace Game\n"
assert s.count(anchor) == 1 and "ai_turn" not in s
helper = r'''
// Walking turns a gait can follow (pedai.cpp's walkers, ai.cpp's BRAIN_FOLLOW / BRAIN_GOTO, police.cpp's officers on
// foot). People slow for a sharp turn, or take it over a step or two: at 6-9 rad/s (then a 3 rad/s cap on the asked speed)
// the feet slid - the animation agent's locotour had 8 of 19 slides over 20 cm in quick turns at walking pace (1.7-3.2
// rad/s at 0.36-0.98 m/s), the worst a group follower's 148-degree pivot.
namespace ai_turn {

// the body's turn rate at v m/s: 4 rad/s stood or barely moving (the animator's feet step round on the spot), 2.4 rad/s
// from 0.4 m/s up (a right-angle turn over a step or two at a full walk; no quicker in a slow walk, where the feet slid most)
inline float rate(float v) { return Lerp(4.f, 2.4f, Saturate((v - 0.1f) / 0.3f)); }

// The way a walker going at v m/s, its body facing yaw, goes when asked for `desired`: eased to the body's turn - no more
// than 35 degrees off the way the body faces from 0.5 m/s up (an arc through the turn, not a crab across the old line),
// any way at all when barely moving - and the pace easing off with the turn's size: as asked up to 50 degrees off, under
// a tenth of it by 130 (a turn round taken all but on the spot, then off again)
inline vec2 follow(float yaw, vec2 desired, float v) {
    float l = length(desired);
    if (l < 1e-3f) return desired;
    float err = AI::wrapPi(atan2f(-desired.x, desired.y) - yaw);
    float k = Lerp(1.f, 0.08f, Saturate((fabsf(err) - 0.87f) / 1.4f));
    float allow = Lerp(kPi, 0.6f, Saturate((v - 0.15f) / 0.35f));
    float go = yaw + Clamp(err, -allow, allow);
    return vec2(-sinf(go), cosf(go)) * (l * k);
}

// An on-foot step's turn rate (rateMax for anything but a walk) and pace: walking (0.35-2.4 m/s asked, not aiming), the
// rate a walk can follow at the speed it is going at (v); facing the way it goes (faceYaw within 35 degrees of it: not a
// strafe or a step back) and `couple` (not a quick step aside), the way and the pace eased to the body too (follow)
inline float ease(float yaw, float faceYaw, float v, bool aiming, float rateMax, vec2& desired, bool couple = true) {
    float l = length(desired);
    if (aiming || l >= 2.4f) return rateMax;
    if (l > 0.35f && couple && fabsf(AI::wrapPi(faceYaw - atan2f(-desired.x, desired.y))) < 0.6f) desired = follow(yaw, desired, v);
    return Min(rateMax, rate(v));
}

}  // namespace ai_turn
'''
s = s.replace(anchor, helper + anchor, 1)
save("ai_game.h", s)

# ---- pedai.cpp: the walkers' final turn and pace
s = load("pedai.cpp")
old = """    // walking along, the body turns no faster than 3 rad/s, and a sharp turn (over 60 degrees off the way the body faces)
    // is taken at a slower step: at 6 rad/s at a full walk the trailing foot lifted off for a few frames (the animation
    // agent's measurement). On the spot the turn stays quick, and so does a run, aiming, a fight or a start back from a car.
    {
        float spd = length(desired);
        if (spd > 0.35f && spd < 2.4f && !p.aiming && b.type != BRAIN_COMBAT && pa.flinchT <= 0.f) {
            turnRate = Min(turnRate, 3.f);
            float mYaw = atan2f(-desired.x, desired.y);
            float tYaw = faceSet ? faceYaw : mYaw;
            if (fabsf(pedai_detail::wrapA(tYaw - mYaw)) < 0.6f) {   // (facing the way they go: not a strafe or a step back)
                float err = fabsf(pedai_detail::wrapA(mYaw - p.yaw));
                if (err > 1.05f) desired *= Max(0.3f, 1.f - (err - 1.05f) / 1.5f);
            }
        }
    }
    // face & move
    if (faceSet) turnTo(p, faceYaw, turnRate, dt);
    else if (length2(desired) > 0.04f) turnTo(p, atan2f(-desired.x, desired.y), turnRate, dt);
    movePed(p, desired, dt, false);"""
new = """    // walking along, the body turns at a rate a walk can follow - by the speed it is going at: 2.4 rad/s from 0.4 m/s up, a
    // right-angle turn over a step or two, 4 rad/s on the spot (ai_turn) - and the way it goes turns with it: an arc
    // through the turn, not a crab across the old line, the pace easing off with a sharp turn's size, all but stopped to
    // turn round. (The 3 rad/s cap on the asked speed before still let the feet slide.) A run, a fight, aiming and a start
    // back from a car keep their quick turns; a strafe or a step back (facing elsewhere than the way they go) and a quick
    // step out of a runner's way the rate cap only. The body turns for the way asked, the step goes the eased way.
    bool turning = faceSet || length2(desired) > 0.04f;
    float wantYaw = faceSet ? faceYaw : atan2f(-desired.x, desired.y);
    if (b.type != BRAIN_COMBAT && pa.flinchT <= 0.f)
        turnRate = ai_turn::ease(p.yaw, wantYaw, length(p.vel.xy()), p.aiming, turnRate, desired, pa.sidestepT <= 0.f);
    // face & move
    if (turning) turnTo(p, wantYaw, turnRate, dt);
    movePed(p, desired, dt, false);"""
assert s.count(old) == 1, "pedai final"
s = s.replace(old, new, 1)
save("pedai.cpp", s)

# ---- ai.cpp: BRAIN_FOLLOW and BRAIN_GOTO
s = load("ai.cpp")
old = """            faceTowards(p, desired, 8.f, dt);
            movePed(p, desired, dt, false);
            break;
        }"""
assert s.count(old) == 2, "ai.cpp follow/goto (%d)" % s.count(old)
new = """            {
                // (a walk's turn and pace - ai_turn: the body turns for the way asked, the step goes the eased way)
                vec2 face = desired;
                float turnR = ai_turn::ease(p.yaw, atan2f(-desired.x, desired.y), length(p.vel.xy()), p.aiming, 8.f, desired);
                faceTowards(p, face, turnR, dt);
            }
            movePed(p, desired, dt, false);
            break;
        }"""
s = s.replace(old, new)
save("ai.cpp", s)

# ---- police.cpp: the on-foot steps (all but the stand-and-hold one, which moves to a spot it already faces)
s = load("police.cpp")
assert "turnR" not in s
pat = re.compile(r"(\n[ ]+)float (\w+) = AI::wrapPi\(faceYaw - p\.yaw\);\n[ ]+p\.yaw = AI::wrapPi\(p\.yaw \+ Clamp\(\2, -(\d+)\.f \* dt, \3\.f \* dt\)\);\n([ ]+)(g\.)?movePed\(p, desired, dt, false\);")
def sub(m):
    ind = m.group(1)
    return (ind + "float turnR = ai_turn::ease(p.yaw, faceYaw, length(p.vel.xy()), p.aiming, " + m.group(3) + ".f, desired);   // (a walk's turn and pace: ai_turn)" +
            ind + "float " + m.group(2) + " = AI::wrapPi(faceYaw - p.yaw);" +
            ind + "p.yaw = AI::wrapPi(p.yaw + Clamp(" + m.group(2) + ", -turnR * dt, turnR * dt));\n" +
            m.group(4) + (m.group(5) or "") + "movePed(p, desired, dt, false);")
s, n = pat.subn(sub, s)
assert n == 9, "police sites (%d)" % n
save("police.cpp", s)
print("ok: ai_game.h, pedai.cpp, ai.cpp (2), police.cpp (%d)" % n)
