# AI agent: change 2 - BRAIN_GOTO walkers ease off behind somebody slower on the line they walk (their pace, a little under
# it when close), as the sidewalk walkers' social force does (pednav.cpp), instead of pressing into their back; somebody
# stood still there: a stop behind them, and aiWalkRound's held-up step aside takes them round. Usage: change2.py AI_CPP
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()

helper_anchor = "vec2 aiWalkRound(GameWorld& g, int id, vec2 goal, float want, float dt) {"
helper = """namespace ai_pace {

// The pace to keep behind somebody slower on the line being walked (BRAIN_GOTO): theirs along it - a little under it at arm's
// length, to open the gap again, a little over it further back - not pressing into their back (the sidewalk walkers' social
// force, pednav.cpp, eases them the same way). Somebody stood still there: a stop behind them, and aiWalkRound's held-up step
// aside then takes the walker round. Beside the line (more than 0.6 m off it): room to pass, no easing.
float behind(const GameWorld& g, int id, vec2 dir, float spd) {
    const Ped& p = g.peds[id];
    vec2 pos = p.pos.toVec3().xy();
    thread_local std::vector<int> around;
    around.clear();
    g.pedsNear(pos, 1.9f, around);
    float cap = spd;
    for (int j : around) {
        if (j == id || j < 0 || j >= (int)g.peds.size()) continue;
        const Ped& q = g.peds[j];
        if (!q.used || (q.state != PS_ONFOOT && q.state != PS_GETUP)) continue;
        vec2 rel = q.pos.toVec3().xy() - pos;
        float along = dot(rel, dir);
        if (along < 0.35f || along > 1.8f || fabsf(dot(rel, AI::rightOf(dir))) > 0.6f) continue;
        float theirs = Max(dot(q.vel.xy(), dir), 0.f);
        if (theirs >= spd) continue;
        float k = Clamp((along - 0.55f) / 1.f, 0.f, 1.f);   // (0 at arm's length, 1 from 1.55 m)
        cap = Min(cap, theirs * Lerp(0.6f, 1.f, k) + 0.3f * k);
    }
    return Max(cap, 0.f);
}

}  // namespace ai_pace

"""
assert s.count(helper_anchor) == 1, "helper anchor"
s = s.replace(helper_anchor, helper + helper_anchor, 1)

old = """                float spd = Min(b.speed, dist * 2.f + 0.5f);
                vec2 to = aiWalkRound(*this, id, goal, spd, dt) - p.pos.toVec3().xy();
                float ls = length(to);
                if (ls > 1e-3f) desired = to / ls * spd;"""
new = """                float spd = Min(b.speed, dist * 2.f + 0.5f);
                vec2 to = aiWalkRound(*this, id, goal, spd, dt) - p.pos.toVec3().xy();
                float ls = length(to);
                if (ls > 1e-3f) desired = to / ls * ai_pace::behind(*this, id, to / ls, spd);   // (behind somebody slower: their pace)"""
assert s.count(old) == 1, "goto site"
s = s.replace(old, new, 1)
open(p, "w", encoding="utf-8", newline="").write(s)
print("ok", p)
