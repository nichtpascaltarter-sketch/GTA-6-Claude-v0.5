// Pedestrian brains: sidewalk wandering (pednav.cpp) with destinations and street crossings, scenarios at benches,
// bus stops, walls and on the beach, groups walking and chatting together, joggers, taxi hailing and bus riding;
// reactions to gunfire/explosions/bodies/fights (flee, cower, dive away from speeding cars, hands up at gunpoint,
// bystanders filming, witnesses phoning the police), gang members defending their territory, carjack victims.
#include "gameworld.h"

namespace Game {

// greetings between two people (population.cpp): the clip, stepping in, starting it on both, stepping back
namespace pop_detail {
int greetPick(const GameWorld& g, int a, int b, u32 h, bool formal);
void greetBegin(GameWorld& g, int a, int b, int clip);
bool greetReady(GameWorld& g, int a, int b);
float greetStart(GameWorld& g, int a, int b, int clip);
void greetPart(GameWorld& g, int a, int b, float gap);
}  // namespace pop_detail

namespace pedai_detail {


inline float wrapA(float a) {
    // remainder instead of repeated subtraction: a huge or infinite angle must not spin forever
    if (a >= -kPi && a <= kPi) return a;
    a = remainderf(a, kTwoPi);
    return a == a ? a : 0.f;
}

inline void turnTo(Ped& p, float yaw, float rate, float dt) {
    float d = wrapA(yaw - p.yaw);
    float step = Clamp(d, -rate * dt, rate * dt);
    p.yaw = wrapA(p.yaw + step);
    p.turnRate = step / Max(dt, 1e-4f);
}

inline float yawTo(vec2 from, vec2 to) {
    vec2 d = to - from;
    return atan2f(-d.x, d.y);
}

// Gang territories: Cuervos hold Calle Luna and the Flats, the Saints the port and its approaches.
Faction territoryOwner(World::Region r) {
    if (r == World::REG_CALLE_LUNA || r == World::REG_FLATS) return FAC_GANG_CUERVOS;
    if (r == World::REG_PORT) return FAC_GANG_SAINTS;
    return FAC_CIVILIAN;
}

bool isGang(Faction f) { return f == FAC_GANG_CUERVOS || f == FAC_GANG_SAINTS; }

// A fleeing ped spreads panic to the people around it (limited hand-offs so a crowd calms down again)
void spreadPanic(GameWorld& g, const Ped& p, dvec3 origin, u8 depth) {
    for (Stimulus& s : g.ai.stimuli)
        if (s.kind == STIM_PANIC && g.time - s.time < 1.5f && length(rel(s.pos, p.pos)) < 6.f) {
            s.time = (float)g.time;
            s.depth = Min(s.depth, depth);
            return;
        }
    Stimulus s;
    s.pos = p.pos;
    s.origin = origin;
    s.kind = STIM_PANIC;
    s.depth = depth;
    s.radius = 10.f;
    s.time = (float)g.time;
    g.ai.stimuli.push_back(s);
    if (g.ai.stimuli.size() > 32) g.ai.stimuli.erase(g.ai.stimuli.begin());
}

// A shop window to stop at: on the street facade of a store (a row of shops; the ground floor of a mid-rise, an art
// deco block or a tower in the shopping districts) on the side of the sidewalk p is on - the facade at most 4.5 m to the
// side of p (never across the street), the spot 0.55 m in front of the glass across from p, clear of the building's
// corners and of anything built in front of it. `face` is the way to look (at the glass).
bool shopWindowNear(const GameWorld& g, vec2 p, vec2& at, vec2& face) {
    const World::BuildingSet* bs = g.buildings ? g.buildings : World::gBuildings;
    if (!bs || bs->buildings.empty()) return false;
    thread_local std::vector<int> nb;
    nb.clear();
    bs->buildingsNear(p, 30.f, nb);
    float best = 1e9f;
    for (int i : nb) {
        const World::Building& b = bs->buildings[i];
        u8 st = b.style;
        if (st != World::BS_SHOPS && st != World::BS_MIDRISE && st != World::BS_DECO && st != World::BS_TOWER) continue;
        if (b.hx < 3.f) continue;
        vec2 f0 = b.c + b.front * b.hy;               // the middle of the street facade
        float off = dot(p - f0, b.front);             // how far in front of it p is
        if (off < 0.5f || off > 4.5f) continue;
        float along = Clamp(dot(p - f0, b.ax), -b.hx + 1.2f, b.hx - 1.2f);
        vec2 spot = f0 + b.ax * along + b.front * 0.55f;
        if (length(spot - p) > 6.f || bs->pointInBuilding(spot, 0.25f)) continue;
        float d = off + fabsf(dot(p - f0, b.ax) - along);
        if (d < best) {
            best = d;
            at = spot;
            face = -b.front;
        }
    }
    return best < 1e8f;
}

// Down hurt (stance 24: on the back, knees up, writhing, a hand on the wound): after a beating, a fall or a knock from
// a car at very low health. They call for help; a passer-by or two may stop (one kneels beside them, another stands by
// on the phone to the emergency line - ACT_AID); the ambulance an incident brings (population.cpp) has a medic see to
// them for a while, then they get up (the get-up from the back) and go with the crew; with nobody coming they get up
// on their own after a couple of minutes and limp away.
void startHurt(GameWorld& g, int id) {
    Ped& p = g.peds[id];
    PedAI& pa = g.pedAI(id);
    pa.activity = ACT_HURT;
    pa.hurtCare = 0.f;
    pa.anchor = p.pos.toVec3().xy();
    pa.anchorYaw = p.yaw;
    pa.stance = 24;
    pa.clip = -1;
    pa.leader = -1;
    pa.greetWith = -1;
    pa.targetVeh = -1;
    pa.actTimer = 100.f + hashToFloat(hash32(p.uid * 7u + 3u)) * 60.f;
    p.brain.type = BRAIN_WANDER;
    p.vel = vec3(0.f, 0.f, p.vel.z);
    g.aiSay(id, BK_HURT, 0.8f, true);
    // a passer-by or two stop to help
    std::vector<int> around;
    g.pedsNear(pa.anchor, 16.f, around);
    int helpers = 0;
    for (int o : around) {
        if (helpers >= 2) break;
        if (o == id || o >= (int)g.ai.ped.size()) continue;
        Ped& q = g.peds[o];
        if (!q.used || q.isPlayer || q.persistent || q.faction != FAC_CIVILIAN || q.state != PS_ONFOOT || q.brain.type != BRAIN_WANDER) continue;
        PedAI& qa = g.pedAI(o);
        if (qa.activity != ACT_WALK || qa.leader >= 0 || qa.role == PR_DRUNK || hash32(q.uid * 31u + p.uid) % 5u >= 2u) continue;
        vec2 to = q.pos.toVec3().xy() - pa.anchor;
        vec2 dir = length2(to) > 1e-4f ? normalize(to) : vec2(1.f, 0.f);
        qa.activity = ACT_AID;
        qa.aidPed = id;
        qa.clip = -1;
        qa.actTimer = 30.f + hashToFloat(hash32(q.uid + 11u)) * 25.f;
        if (helpers == 0) {
            qa.anchor = pa.anchor + dir * 0.85f;   // kneeling beside them
            qa.stance = 0;
        } else {
            qa.anchor = pa.anchor + dir * 2.4f;    // standing by, on the phone to the emergency line
            qa.stance = 8;
        }
        qa.anchorYaw = AI::dirYaw(-dir);
        helpers++;
    }
}

}  // namespace pedai_detail

using namespace pedai_detail;

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::aiStimulus(dvec3 pos, int kind, int source, float radius, bool byPlayer) {
    // merge with a recent stimulus of the same kind nearby
    for (Stimulus& s : ai.stimuli)
        if (s.kind == kind && time - s.time < 1.5f && length(rel(s.pos, pos)) < 8.f) {
            s.time = (float)time;
            s.radius = Max(s.radius, radius);
            s.player = s.player || byPlayer;
            if (source >= 0) s.source = source;
            return;
        }
    Stimulus s;
    s.pos = pos;
    s.origin = pos;
    s.kind = (u8)kind;
    s.source = source;
    s.radius = radius;
    s.time = (float)time;
    s.player = byPlayer;
    ai.stimuli.push_back(s);
    if (ai.stimuli.size() > 24) ai.stimuli.erase(ai.stimuli.begin());
}

void GameWorld::aiUpdateThreats(float dt) {
    for (size_t i = 0; i < ai.stimuli.size();) {
        u8 k = ai.stimuli[i].kind;
        float life = k == STIM_BODY ? 40.f : (k == STIM_FIGHT ? 4.f : (k == STIM_PANIC ? 3.f : 6.f));
        if (time - ai.stimuli[i].time > life) ai.stimuli.erase(ai.stimuli.begin() + i);
        else i++;
    }
    Ped* pl = playerPed();
    if (!pl) return;
    // drivers near gunfire / explosions floor it (or bail out when boxed in); aimed at: flee or give up the car
    for (const Stimulus& s : ai.stimuli) {
        if (time - s.time > 0.25f || (s.kind != STIM_GUNFIRE && s.kind != STIM_EXPLOSION)) continue;
        for (int vi = 0; vi < (int)vehicles.size(); vi++) {
            Vehicle& v = vehicles[vi];
            if (!v.used || v.scripted || v.faction == FAC_POLICE) continue;
            int drv = v.seats[0];
            if (drv < 0 || peds[drv].isPlayer || peds[drv].brain.type != BRAIN_DRIVER) continue;
            if (length(rel(v.sim.body.pos, s.pos)) > s.radius * 0.8f) continue;
            peds[drv].brain.type = BRAIN_FLEE;
            peds[drv].brain.goal = s.pos;
            peds[drv].brain.target = s.source;
            peds[drv].brain.timer = 0.f;
            vehAI(vi).fleeTimer = 0.f;
        }
    }
    if (pl->aiming && pl->state == PS_ONFOOT && pl->weapon != WPN_FISTS) {
        vec3 eye = pedHeadPos(*pl);
        for (int vi = 0; vi < (int)vehicles.size(); vi++) {
            Vehicle& v = vehicles[vi];
            if (!v.used || v.scripted || v.faction == FAC_POLICE) continue;
            int drv = v.seats[0];
            if (drv < 0 || peds[drv].isPlayer || (peds[drv].brain.type != BRAIN_DRIVER && peds[drv].brain.type != BRAIN_FLEE)) continue;
            vec3 to = v.sim.body.pos.toVec3() + vec3(0, 0, 1.f) - eye;
            float along = dot(to, pl->aimDir);
            if (along < 0.f || along > 18.f || length(to - pl->aimDir * along) > 2.2f) continue;
            PedAI& da = pedAI(drv);
            if (v.sim.speed() < 2.f && da.temper != 2) {
                // stopped and staring down a barrel: get out, hands up, then run
                removePedFromVehicle(drv, true);
                traffic.detach(vi);
                vehAI(vi).managed = false;
                peds[drv].brain.type = BRAIN_WANDER;
                da.activity = ACT_HANDS_UP;
                da.actTimer = 3.f;
                aiSay(drv, BK_HANDS_UP, 1.f, true);
            } else if (peds[drv].brain.type != BRAIN_FLEE) {
                peds[drv].brain.type = BRAIN_FLEE;
                peds[drv].brain.target = player;
                peds[drv].brain.timer = 0.f;
                aiSay(drv, BK_PANIC, 0.6f);
            }
        }
    }
    // the player leaning on the horn next to people
    {
        int pv = playerVehicle();
        if (pv >= 0 && vehicles[pv].hornOn) aiStimulus(vehicles[pv].sim.body.pos, STIM_HORN, player, 14.f, true);
    }
    // bodies on the ground are scary for a while
    static float bodyScan = 0.f;
    bodyScan -= dt;
    if (bodyScan <= 0.f) {
        bodyScan = 1.f;
        for (int i = 0; i < (int)peds.size(); i++) {
            const Ped& p = peds[i];
            if (!p.used || p.state != PS_DEAD || p.stateTime > 60.f || p.stateTime < 0.5f) continue;
            if (length(rel(p.pos, pl->pos)) > 120.f) continue;
            aiStimulus(p.pos, STIM_BODY, p.lastAttacker, 18.f, p.lastAttacker == player);
        }
        // the player walking around with a gun drawn
        if (pl->state == PS_ONFOOT && pl->weapon != WPN_FISTS && weaponInfo(pl->weapon).clipSize > 0)
            aiStimulus(pl->pos, STIM_ARMED, player, pl->aiming ? 16.f : 9.f, true);
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Civilian / gang brains (police on foot: police.cpp)
void GameWorld::aiCivilianBrain(int id, float dt) {
    Ped& p = peds[id];
    Brain& b = p.brain;
    PedAI& pa = pedAI(id);
    vec2 pos = p.pos.toVec3().xy();
    Ped* pl = playerPed();
    vec2 ppos = pl ? pl->pos.toVec3().xy() : vec2(1e9f);
    float plDist = pl ? length(ppos - pos) : 1e9f;
    pa.think -= dt;
    pa.actTimer -= dt;
    if (pa.greetT > 0.f) pa.greetT = Max(pa.greetT - dt, 0.f);
    if (pa.greetWith >= 0 && pa.activity != ACT_VENUE && pa.activity != ACT_MEET) {   // (parted: fled, knocked down, gone off)
        pa.greetWith = -1;
        pa.greetT = 0.f;
    }
    pa.diveCooldown -= dt;
    pa.shoutTimer -= dt;
    // just back on their feet after a knock-down (a beating, a fall, a car) at very low health: down again, hurt
    if (pa.knockedDown) {
        pa.knockedDown = false;
        if (p.faction == FAC_CIVILIAN && !p.persistent && p.state == PS_ONFOOT && p.health > 0.f && p.health < p.maxHealth * 0.28f &&
            pa.activity != ACT_HURT && pa.activity != ACT_CUFFED && hash32(p.uid * 13u + 7u) % 4u != 0u)
            startHurt(*this, id);
    }
    bool gang = isGang(p.faction);
    int selfBody = id < (int)ai.pedBody.size() ? ai.pedBody[id] : -1;
    // ---------------------------------------------------------------- perception (staggered; not while down hurt or in cuffs)
    if (pa.think <= 0.f && pa.activity != ACT_HURT && pa.activity != ACT_CUFFED) {
        pa.think = 0.25f + hashToFloat(hash32(p.uid + (u32)(time * 10.0))) * 0.25f;
        bool calm = b.type == BRAIN_WANDER || b.type == BRAIN_SCENARIO;
        // stimuli
        for (const Stimulus& s : ai.stimuli) {
            float d = length(rel(s.pos, p.pos));
            if (d > s.radius || s.source == id) continue;
            // (one of an ambient event's own people: not scared off by its own raised voices and fists)
            if (pa.eventId >= 0 && s.source >= 0 && s.source < (int)ai.ped.size() && ai.ped[s.source].eventId == pa.eventId &&
                (s.kind == STIM_FIGHT || s.kind == STIM_ARREST))
                continue;
            bool fresh = time - s.time < 0.6f;
            // a witness on the phone to the police keeps talking (backing away from the scene) unless something
            // blows up next to them: re-reacting to the very gunfire being reported would drop the call
            if (pa.activity == ACT_CALL_POLICE && !(s.kind == STIM_EXPLOSION && d < 15.f)) continue;
            if (p.faction == FAC_POLICE) {
                // an officer on a foot beat runs toward trouble: an NPC culprit is pursued (the player's crimes go
                // through the wanted level), other commotion is checked out
                if (!calm) continue;
                int src = s.source;
                bool npcCulprit = src >= 0 && src < (int)peds.size() && src != player && peds[src].used && peds[src].health > 0.f &&
                                  peds[src].faction != FAC_POLICE && peds[src].state != PS_DEAD;
                if (npcCulprit && (s.kind == STIM_GUNFIRE || s.kind == STIM_EXPLOSION || s.kind == STIM_FIGHT || s.kind == STIM_ARMED)) {
                    b.type = BRAIN_COMBAT;
                    b.target = src;
                    b.timer = 0.f;
                    pa.activity = ACT_WALK;
                    pa.leader = -1;
                    if (s.kind != STIM_FIGHT && p.ammo[WPN_PISTOL] + p.clip[WPN_PISTOL] > 0) p.weapon = WPN_PISTOL;   // sidearm out
                    aiSay(id, BK_COP_FREEZE, 1.f, true);
                    break;
                }
                if (s.kind == STIM_ARMED && s.player && pa.shoutTimer <= 0.f && d < 14.f) {
                    aiSay(id, BK_COP_FREEZE, 0.6f, true);   // "drop the weapon" - the wanted system decides the rest
                    pa.shoutTimer = 8.f;
                }
                bool look = s.kind == STIM_GUNFIRE || s.kind == STIM_EXPLOSION || s.kind == STIM_FIGHT || s.kind == STIM_BODY || s.kind == STIM_CRASH ||
                            s.kind == STIM_PANIC;
                if (look && (pa.activity == ACT_WALK || pa.activity == ACT_GROUP) && pa.leader < 0) {
                    pa.activity = ACT_INSPECT;
                    pa.threatPos = (s.kind == STIM_PANIC ? s.origin : s.pos).toVec3().xy();
                    pa.actTimer = 6.f + hashToFloat(hash32(p.uid + (u32)(s.time * 3.f))) * 4.f;
                    pa.walk.hurry = s.kind == STIM_BODY || s.kind == STIM_CRASH ? 1.4f : 2.2f;
                }
                continue;
            }
            switch (s.kind) {
                case STIM_GUNFIRE:
                case STIM_EXPLOSION: {
                    if (gang && s.player && pl && calm) {
                        // gang members answer gunfire on their turf
                        if (territoryOwner(map->regionAt(pos.x, pos.y)) == p.faction || d < 25.f) {
                            b.type = BRAIN_COMBAT;
                            b.target = player;
                            aiSay(id, BK_GANG_ATTACK, 0.6f);
                            break;
                        }
                    }
                    if (gang && b.type == BRAIN_COMBAT) break;
                    // a mixed crowd: most run, some hit the deck, the bold at a safe distance get their phones out
                    float rg = hashToFloat(hash32(p.uid * 29u + (u32)(s.time * 2.f)));
                    if (calm && s.kind == STIM_GUNFIRE && !gang && pa.temper == 2 && d > 22.f && rg < 0.45f && pa.activity != ACT_FILM) {
                        pa.activity = ACT_FILM;
                        ai.stats.filming++;
                        pa.actTimer = 8.f + rg * 12.f;
                        pa.anchor = pos;
                        pa.threatPos = s.pos.toVec3().xy();
                        aiSay(id, BK_FILMING, 0.4f);
                        break;
                    }
                    if (b.type != BRAIN_FLEE && b.type != BRAIN_COWER) {
                        bool deck = (pa.temper == 0 && d < 14.f && hashToFloat(hash32(p.uid + 5u)) < 0.45f) || (pa.temper == 1 && rg < 0.18f);
                        if (deck) {
                            b.type = BRAIN_COWER;
                            aiSay(id, BK_COWER, 0.5f);
                        } else {
                            b.type = BRAIN_FLEE;
                            aiSay(id, fresh ? BK_PANIC : BK_FLEE, 0.35f);
                        }
                        b.timer = 0.f;
                        b.target = s.source;
                        b.goal = s.pos;
                        p.animIn.stance = 0;
                        pa.activity = ACT_WALK;
                        pa.panicDepth = 0;
                        pa.panicEmit = 0.3f + hashToFloat(hash32(p.uid + 23u)) * 0.5f;
                    } else if (b.type == BRAIN_FLEE) {
                        b.timer = Min(b.timer, 4.f);   // keep running
                        b.goal = s.pos;
                    }
                    break;
                }
                case STIM_FIGHT: {
                    if (!calm || gang) break;
                    // bold bystanders film, the rest back off
                    if (pa.temper == 2 && d > 5.f && d < 22.f && pa.activity != ACT_FILM) {
                        pa.activity = ACT_FILM;
                        ai.stats.filming++;
                        pa.actTimer = 8.f + hashToFloat(hash32(p.uid + 17u)) * 8.f;
                        pa.anchor = pos;
                        pa.threatPos = s.pos.toVec3().xy();
                        aiSay(id, BK_FILMING, 0.4f);
                    } else if (d < 8.f && pa.activity != ACT_FILM) {
                        b.type = BRAIN_FLEE;
                        b.goal = s.pos;
                        b.target = -1;
                        b.timer = 8.f;   // short retreat
                    }
                    break;
                }
                case STIM_PANIC: {
                    // people running past: the timid run too, others hurry away and look back, the bold get their phones out
                    if (!calm || gang || s.depth >= 3 || s.source == id) break;
                    vec2 from = s.origin.toVec3().xy();
                    float dOrigin = length(from - pos);
                    u32 hp = hash32(p.uid * 7u + (u32)(s.time * 4.f));
                    float r = hashToFloat(hp);
                    if (pa.temper == 0 || r < 0.35f) {
                        b.type = BRAIN_FLEE;
                        b.goal = s.origin;
                        b.target = -1;
                        b.timer = 5.f;
                        pa.activity = ACT_WALK;
                        pa.panicDepth = (u8)(s.depth + 1);
                        pa.panicEmit = 0.6f + r;
                        ai.stats.panicSpread++;
                        aiSay(id, BK_PANIC, 0.25f);
                    } else if (pa.temper == 2 && dOrigin < 60.f && dOrigin > 12.f && pa.activity != ACT_FILM) {
                        pa.activity = ACT_FILM;
                        ai.stats.filming++;
                        pa.actTimer = 6.f + r * 6.f;
                        pa.threatPos = from;
                        aiSay(id, BK_FILMING, 0.3f);
                    } else if (pa.activity == ACT_WALK) {
                        pa.activity = ACT_INSPECT;
                        pa.threatPos = from;
                        pa.actTimer = 1.5f + r * 1.5f;
                        pa.walk.hurry = 1.6f;
                    }
                    break;
                }
                case STIM_HORN: {
                    // honked at: jump, glare at the car, the bold shout back
                    if (!calm || d > s.radius || pa.activity == ACT_INSPECT || time - s.time > 0.8f) break;
                    u32 hh = hash32(p.uid * 11u + (u32)(s.time * 2.f));
                    pa.activity = ACT_INSPECT;
                    pa.threatPos = s.pos.toVec3().xy();
                    pa.actTimer = 1.2f + hashToFloat(hh);
                    if (d < 7.f && p.pendingAction < 0 && hh % 3 == 0) p.pendingAction = Anim::CLIP_STAGGER;
                    aiSay(id, pa.temper == 2 ? BK_INSULT : BK_BUMP, pa.temper == 0 ? 0.15f : 0.45f);
                    break;
                }
                case STIM_CRASH: {
                    // rubbernecking: look over, the bold film it, a few shout
                    if (!calm || gang || d < 3.f) break;
                    u32 hc = hash32(p.uid * 13u + (u32)(s.time * 4.f));
                    float r = hashToFloat(hc);
                    if (pa.temper == 2 && d > 7.f && r < 0.5f && pa.activity != ACT_FILM) {
                        pa.activity = ACT_FILM;
                        ai.stats.filming++;
                        pa.actTimer = 6.f + r * 8.f;
                        pa.threatPos = s.pos.toVec3().xy();
                        aiSay(id, BK_FILMING, 0.3f);
                    } else if (pa.activity == ACT_WALK || pa.activity == ACT_SCENARIO) {
                        pa.activity = ACT_INSPECT;
                        pa.threatPos = s.pos.toVec3().xy();
                        pa.actTimer = 2.5f + r * 3.f;
                        if (r < 0.3f) aiSay(id, BK_CRASH, 0.5f);
                    }
                    break;
                }
                case STIM_ARREST: {
                    // the police at work (someone at gunpoint, hands up, on the ground, in cuffs): the bold get their
                    // phones out, others stop a few metres back for a look and a remark - once per scene, and nobody
                    // busy with something of their own (a venue, a queue, a meet)
                    if (!calm || gang || d < 5.f || time - pa.sceneT < 45.0) break;
                    if (pa.activity != ACT_WALK && pa.activity != ACT_SCENARIO && pa.activity != ACT_GROUP) break;
                    pa.sceneT = (float)time;
                    u32 ha = hash32(p.uid * 37u + (u32)s.source * 11u);
                    float r = hashToFloat(ha);
                    if (pa.temper == 2 && d > 7.f && r < 0.6f) {
                        pa.activity = ACT_FILM;
                        ai.stats.filming++;
                        pa.actTimer = 10.f + r * 18.f;
                        pa.anchor = pos;
                        pa.threatPos = s.pos.toVec3().xy();
                        aiSay(id, (ha >> 9) & 1 ? BK_ONLOOKER : BK_FILMING, 0.4f);
                    } else if (pa.temper != 0 && r < 0.65f && pa.leader < 0 && pa.activity == ACT_WALK) {
                        pa.activity = ACT_INSPECT;
                        pa.threatPos = s.pos.toVec3().xy();
                        pa.actTimer = 3.f + r * 6.f;
                        pa.walk.hurry = 1.f;
                        if (r < 0.3f) aiSay(id, BK_ONLOOKER, 0.5f);
                    }
                    break;
                }
                case STIM_BODY: {
                    if (!calm || d > 16.f) break;
                    if (!lineOfSight(p.pos + dvec3(0, 0, 1.6), s.pos + dvec3(0, 0, 0.5), id, -1)) break;
                    aiSay(id, BK_PANIC, 0.5f);
                    b.type = BRAIN_FLEE;
                    b.goal = s.pos;
                    b.target = -1;
                    b.timer = 5.f;
                    pa.activity = ACT_WALK;
                    pa.panicDepth = 1;   // a body scares, but spreads less than gunfire
                    pa.panicEmit = 0.8f;
                    break;
                }
                case STIM_ARMED: {
                    if (gang || !calm || s.source != player || !pl) break;
                    if (pl->aiming) {
                        // at gunpoint?
                        vec3 eye = pedHeadPos(*pl);
                        vec3 to = pedChestPos(p) - eye;
                        float along = dot(to, pl->aimDir);
                        float off = length(to - pl->aimDir * along);
                        if (along > 0.f && along < 25.f && off < 1.2f) {
                            if (pa.temper == 0) {
                                b.type = BRAIN_COWER;
                                aiSay(id, BK_COWER, 0.7f);
                            } else if (pa.activity != ACT_HANDS_UP) {
                                pa.activity = ACT_HANDS_UP;
                                pa.actTimer = 3.f;
                                aiSay(id, BK_HANDS_UP, 0.8f);
                            } else {
                                pa.actTimer = 3.f;
                            }
                            break;
                        }
                    }
                    if (d < 7.f && pa.activity == ACT_WALK && pa.shoutTimer <= 0.f) {
                        aiSay(id, BK_GUN_SEEN, 0.25f);
                        pa.shoutTimer = 20.f;
                        pa.walk.hurry = 1.5f;
                    }
                    break;
                }
                default: break;
            }
        }
        // a flashy car rolling by slowly: people turn to look, point, the bold film it and shout something
        if (!gang && pl && calm && pl->state == PS_INVEHICLE && pl->vehicle >= 0 && plDist < 14.f && pa.leader < 0 &&
            (pa.activity == ACT_WALK || pa.activity == ACT_SCENARIO) && p.faction == FAC_CIVILIAN && pa.barkCooldown <= 0.f) {
            const Vehicle& pv = vehicles[pl->vehicle];
            Vehicles::VehicleClass cls = vassets[pv.model].spec.cls;
            bool flashy = cls == Vehicles::VC_SUPER || cls == Vehicles::VC_SPORTS || cls == Vehicles::VC_MUSCLE;
            u32 hn = hash32(p.uid * 131u + (u32)(time * 0.25));
            if (flashy && pv.sim.speed() < 11.f && hashToFloat(hn) < 0.12f) {
                pa.activity = ACT_WATCH;
                pa.anchor = pos;
                pa.anchorYaw = yawTo(pos, ppos);
                pa.stance = pa.temper == 2 ? 8 : 17;   // phone up / pointing
                pa.clip = -1;
                pa.actTimer = 3.f + hashToFloat(hash32(hn)) * 3.f;
                aiSay(id, BK_NICE_CAR, 0.7f);
            }
        }
        // gang turf: an armed player hanging around gets confronted, then attacked
        if (gang && pl && b.type != BRAIN_COMBAT && b.type != BRAIN_FLEE && pl->state == PS_ONFOOT && plDist < 14.f) {
            bool onTurf = territoryOwner(map->regionAt(pos.x, pos.y)) == p.faction;
            bool armed = pl->weapon != WPN_FISTS && weaponInfo(pl->weapon).clipSize > 0;
            if (onTurf && (armed || pa.provoked)) {
                pa.linger += 0.4f;
                if (pa.activity != ACT_CONFRONT && pa.linger > 3.f) {
                    pa.activity = ACT_CONFRONT;
                    pa.actTimer = 7.f;
                    aiSay(id, BK_GANG_WARN, 1.f, true);
                }
            } else {
                pa.linger = Max(0.f, pa.linger - 0.4f);
            }
        }
        // bumped into by the player
        if (pl && plDist < 0.9f && length(pl->vel.xy()) > 1.2f && pl->state == PS_ONFOOT && calm) {
            aiSay(id, pa.temper == 2 ? BK_INSULT : BK_BUMP, 0.6f);
            if (gang) pa.provoked = true;
        }
        // attacked by the player: gang members fight back, civilians flee
        if (p.lastAttacker == player && time - p.lastDamageTime < 1.0 && b.type != BRAIN_COMBAT) {
            if (gang || (pa.temper == 2 && hashToFloat(hash32(p.uid * 3u)) < 0.4f)) {
                b.type = BRAIN_COMBAT;
                b.target = player;
                aiSay(id, gang ? BK_GANG_ATTACK : BK_INSULT, 0.8f);
                if (gang) {
                    // the whole crew joins
                    std::vector<int> crew;
                    pedsNear(pos, 35.f, crew);
                    for (int c : crew)
                        if (c != id && peds[c].faction == p.faction && peds[c].health > 0.f && peds[c].state == PS_ONFOOT && peds[c].brain.type != BRAIN_COMBAT &&
                            !peds[c].persistent) {
                            peds[c].brain.type = BRAIN_COMBAT;
                            peds[c].brain.target = player;
                        }
                }
            } else if (b.type != BRAIN_FLEE) {
                b.type = BRAIN_FLEE;
                b.target = player;
                b.timer = 0.f;
                aiSay(id, BK_PANIC, 0.7f);
            }
        }
    }
    // ---------------------------------------------------------------- dive away from vehicles about to hit us
    if (pa.diveCooldown <= 0.f && p.state == PS_ONFOOT && selfBody >= 0) {
        float r = 14.f;
        int danger = -1;
        vec2 dangerDir;
        traffic.hash.query(traffic.bodies, pos - vec2(r), pos + vec2(r), [&](int bi) {
            const AI::Body& vb = traffic.bodies[bi];
            if (vb.kind != AI::BK_CAR || vb.speed < 6.f || danger >= 0) return;
            if (fabsf(vb.z - (float)p.pos.z) > 2.5f) return;
            vec2 rp = pos - vb.pos;
            float along = dot(rp, vb.fwd);
            float lat = dot(rp, AI::rightOf(vb.fwd));
            if (along < vb.halfLen || fabsf(lat) > vb.halfWid + 1.1f) return;
            float ttc = (along - vb.halfLen) / Max(vb.speed, 0.1f);
            if (ttc < 1.1f) {
                danger = bi;
                dangerDir = AI::rightOf(vb.fwd) * (lat >= 0.f ? 1.f : -1.f);
            }
        });
        if (danger >= 0) {
            pa.diveCooldown = 3.f;
            const AI::Body& vb = traffic.bodies[danger];
            aiSay(id, BK_DIVE, 0.8f, true);
            if (vb.speed > 11.f && pa.temper != 0) {
                // leap out of the way (ragdoll dive), get up afterwards
                knockDown(id, vec3(dangerDir * 380.f, 140.f));   // (no bracing: a car is coming)
                return;
            }
            p.vel = vec3(dangerDir * 5.5f, p.vel.z);
            if (b.type == BRAIN_WANDER || b.type == BRAIN_SCENARIO) {
                b.type = BRAIN_FLEE;
                b.goal = dvec3(vec3(vb.pos, (float)p.pos.z));
                b.target = -1;
                b.timer = 10.f;
            }
        }
    }
    // ---------------------------------------------------------------- behaviors
    vec2 desired(0, 0);
    float faceYaw = p.yaw;
    bool faceSet = false;
    float turnRate = 6.f;
    int stance = 0;
    switch (b.type) {
        case BRAIN_SCENARIO:
            // legacy scenario brain (missions/story): stand with the given stance, optionally resume wandering
            stance = b.scenario >= 0 ? b.scenario : 0;
            if (b.sub == 1 && b.timer > 10.f) {
                b.type = BRAIN_WANDER;
                b.edge = -1;
                stance = 0;
            }
            break;
        case BRAIN_COWER: {
            stance = 4;
            vec3 from = b.target >= 0 && peds[b.target].used ? peds[b.target].pos.toVec3() : b.goal.toVec3();
            faceYaw = yawTo(pos, from.xy());
            faceSet = true;
            if (b.timer > 7.f + hashToFloat(hash32(p.uid)) * 8.f) {
                b.type = BRAIN_FLEE;
                b.timer = 0.f;
            }
            break;
        }
        case BRAIN_FLEE: {
            vec3 from = b.target >= 0 && b.target < (int)peds.size() && peds[b.target].used ? peds[b.target].pos.toVec3() : b.goal.toVec3();
            vec2 away = pos - from.xy();
            float d = length(away);
            away = d > 1e-3f ? away / d : AI::yawDir(p.yaw);
            // prefer running along the street (sidewalks) instead of into walls: blend with the sidewalk direction
            if (pa.navOk && pa.walk.link >= 0) {
                vec2 t = laneGraph.walkTangent(pa.walk.link, pa.walk.x, pa.walk.fromA);
                if (dot(t, away) < 0.f) t = -t;
                away = normalize(away * 0.6f + t * 0.4f);
            }
            float wob = sinf((float)time * 1.3f + (float)(p.uid % 97)) * 0.35f;
            vec2 dir = normalize(away + vec2(-away.y, away.x) * wob);
            float spd = b.timer < 9.f ? 6.1f : 3.4f;
            // running people make others around them panic too (a few hand-offs, then the crowd settles)
            pa.panicEmit -= dt;
            if (pa.panicEmit <= 0.f && b.timer < 8.f && pa.panicDepth < 3) {
                pa.panicEmit = 1.5f;
                spreadPanic(*this, p, dvec3(from), pa.panicDepth);
            }
            // avoid running into traffic lanes when a car is coming
            desired = dir * spd;
            faceYaw = atan2f(-dir.x, dir.y);
            faceSet = true;
            turnRate = 9.f;
            if ((int)(b.timer * 2.f) % 9 == 3 && pa.barkCooldown <= 0.f) aiSay(id, BK_FLEE, 0.15f);
            if (b.timer > 14.f && d > 55.f) {
                b.type = BRAIN_WANDER;
                b.edge = -1;
                pa.navOk = false;
                pa.activity = ACT_WALK;
                b.timer = 0.f;
            }
            break;
        }
        case BRAIN_COMBAT: {
            if (!p.target_is_valid(*this)) {
                b.type = BRAIN_WANDER;
                b.edge = -1;
                pa.navOk = false;
                p.aiming = false;
                if (gang && pl && plDist < 40.f) aiSay(id, BK_GANG_TAUNT, 0.5f);
                break;
            }
            Ped& t = peds[b.target];
            vec2 tp = t.pos.toVec3().xy();
            vec2 to = tp - pos;
            float dist = length(to);
            const WeaponInfo& wi = weaponInfo(p.weapon);
            bool ranged = wi.clipSize > 0;
            float prefer = ranged ? Clamp(wi.range * 0.35f, 7.f, 24.f) : 1.1f;
            if (b.thinkTimer <= 0.f) {
                b.thinkTimer = 0.3f;
                b.alerted = lineOfSight(p.pos + dvec3(0, 0, 1.6), t.pos + dvec3(0, 0, 1.3), id, t.vehicle);
            }
            bool los = b.alerted;
            if (dist > prefer * 1.25f || !los) desired = to / Max(dist, 1e-3f) * (dist > 20.f ? 5.5f : 3.4f);
            else if (ranged && dist < prefer * 0.5f) desired = -to / Max(dist, 1e-3f) * 2.4f;
            else if (ranged) {
                vec2 side(-to.y, to.x);
                side = normalize(side) * (((((u32)(time * 0.45)) + p.uid) & 1) ? 1.7f : -1.7f);
                desired = side;
            }
            faceYaw = yawTo(pos, tp);
            faceSet = true;
            turnRate = 9.f;
            p.aiming = false;
            p.firing = false;
            if (ranged) {
                p.aiming = dist < wi.range && los;
                if (p.aiming && p.fireTimer <= 0.f && p.reloadTimer <= 0.f) {
                    float burst = fmodf((float)time * (0.6f + b.aggression * 0.6f) + p.uid * 0.37f, 2.f);
                    if (burst < 1.1f) {
                        quat q = quatAxisAngle(vec3(0, 0, 1), p.yaw);
                        vec3 hand = p.pos.toVec3() + rotate(q, p.bones[Anim::B_HAND_R].c[3].xyz());
                        vec3 aimAt = pedChestPos(t) + vec3(0, 0, (hash32(p.uid + (u32)(time * 7)) % 100) * 0.004f - 0.2f);
                        vec3 d = normalize(aimAt - hand);
                        p.aimDir = d;
                        fireWeapon(id, dvec3(hand + d * 0.3f), d);
                    }
                }
                if (p.clip[p.weapon] <= 0 && p.ammo[p.weapon] <= 0) p.ammo[p.weapon] = wi.clipSize * 3;
            } else if (dist < 1.4f && p.meleeTimer <= 0.f) {
                vec3 f(-sinf(p.yaw), cosf(p.yaw), 0);
                fireWeapon(id, p.pos + dvec3(0, 0, 1.2), f);
                p.meleeTimer = wi.fireInterval + 0.3f;
                aiStimulus(p.pos, STIM_FIGHT, id, 22.f, b.target == player);
            }
            if (dist > 90.f && !gang) {
                b.type = BRAIN_WANDER;
                b.edge = -1;
                pa.navOk = false;
            }
            // unarmed civilians give up quickly
            if (!gang && !ranged && b.timer > 12.f) {
                b.type = BRAIN_FLEE;
                b.timer = 4.f;
            }
            break;
        }
        default: {
            // ---------------- BRAIN_WANDER and its activities
            b.type = BRAIN_WANDER;
            if (!pa.navOk || b.edge == -1) {
                if (pedNav.place(pa.walk, pos, p.uid * 2654435761u + 7u, 80.f)) {
                    pa.navOk = true;
                    b.edge = pa.walk.link;
                    if (pa.role == PR_JOGGER) pa.walk.speed = 2.8f + hashToFloat(hash32(p.uid)) * 0.6f;
                    else if (pa.role == PR_BUSINESS) pa.walk.speed = 1.45f;
                    else if (pa.role == PR_DRUNK) pa.walk.speed = 0.9f;
                } else {
                    pa.navOk = false;
                    b.edge = -2;
                }
            }
            switch (pa.activity) {
                case ACT_WALK:
                case ACT_JOG: {
                    if (pa.leader >= 0) {
                        // group member: keep a slot beside/behind the leader
                        bool ok = pa.leader < (int)peds.size() && peds[pa.leader].used && peds[pa.leader].uid == pa.leaderUid && peds[pa.leader].health > 0.f &&
                                  peds[pa.leader].brain.type == BRAIN_WANDER && peds[pa.leader].state == PS_ONFOOT;
                        if (!ok) {
                            pa.leader = -1;
                            pa.navOk = false;
                            b.edge = -1;
                            break;
                        }
                        Ped& L = peds[pa.leader];
                        vec2 lf = AI::yawDir(L.yaw), lr = AI::rightOf(lf);
                        vec2 slot = L.pos.toVec3().xy() + lr * pa.slot.x + lf * pa.slot.y;
                        vec2 to = slot - pos;
                        float d = length(to);
                        float lspd = length(L.vel.xy());
                        if (d > 0.25f) desired = to / d * Min(lspd + d * 1.2f, 2.8f);
                        if (lspd < 0.3f && d < 1.2f) {
                            // leader stopped: face the group and chat
                            faceYaw = yawTo(pos, L.pos.toVec3().xy());
                            faceSet = true;
                            stance = 7;
                        } else if (length(desired) > 0.2f) {
                            faceYaw = atan2f(-desired.x, desired.y);
                            faceSet = true;
                            if ((p.uid + (u32)(time * 0.1)) % 2 == 0) stance = 7;   // chatting on the way
                        }
                        break;
                    }
                    if (!pa.navOk) {
                        // no sidewalk nearby (beach, parks): idle about
                        stance = 0;
                        if (pa.actTimer <= 0.f) {
                            pa.actTimer = 6.f + hashToFloat(hash32(p.uid + (u32)time)) * 8.f;
                            pa.anchor = pos + AI::yawDir(hashToFloat(hash32(p.uid * 7u + (u32)time)) * kTwoPi) * 6.f;
                        }
                        vec2 to = pa.anchor - pos;
                        if (length(to) > 0.8f) {
                            desired = normalize(to) * 1.1f;
                            faceYaw = atan2f(-desired.x, desired.y);
                            faceSet = true;
                        }
                        break;
                    }
                    float fy = p.yaw;
                    bool raining = env && env->rain > 0.35f;
                    if (raining && pa.role != PR_JOGGER) pa.walk.hurry = Max(pa.walk.hurry, 1.3f);   // hurrying through the rain
                    desired = pedNav.step(pa.walk, pos, dt, selfBody, &fy);
                    // on the phone or smoking on the move now and then (the upper body keeps it up while walking)
                    pa.walkStanceTimer -= dt;
                    if (pa.walkStanceTimer <= 0.f) {
                        u32 hw = hash32(p.uid * 97u + (u32)(time * 0.5));
                        float q = hashToFloat(hw);
                        bool calls = pa.role == PR_BUSINESS ? q < 0.45f : q < 0.18f;
                        pa.walkStance = pa.role == PR_JOGGER || pa.role == PR_COP ? 0 : (calls ? 8 : (q > 0.95f && pa.role != PR_BUSINESS ? 10 : 0));
                        pa.walkStanceTimer = pa.walkStance ? 15.f + hashToFloat(hash32(hw)) * 40.f : 20.f + hashToFloat(hash32(hw)) * 50.f;
                    }
                    stance = pa.walkStance;
                    if (stance == 8 && pa.barkCooldown <= 0.f && plDist < 6.f) aiSay(id, BK_PHONE_CHAT, 0.25f);
                    if (pa.role == PR_DRUNK) {
                        float sway = sinf((float)time * 1.7f + p.uid) * 0.6f + sinf((float)time * 0.63f + p.uid * 3u) * 0.4f;
                        vec2 side = AI::rightOf(length2(desired) > 0.01f ? normalize(desired) : AI::yawDir(p.yaw));
                        desired += side * sway * 0.7f;
                        if (pa.barkCooldown <= 0.f && plDist < 12.f) aiSay(id, BK_DRUNK, 0.2f);
                    }
                    faceYaw = fy;
                    faceSet = true;
                    if (pa.walk.state == AI::WS_WAIT_CROSS && pa.role == PR_JOGGER && p.pendingAction < 0 && p.anim.actionDone()) p.pendingAction = Anim::CLIP_JOG_IDLE;
                    // tourists stop every so often to take a picture (the group waits around them)
                    if (pa.role == PR_TOURIST && pa.walk.state == AI::WS_WALK) {
                        pa.clipTimer -= dt;
                        if (pa.clipTimer <= 0.f) {
                            u32 ht = hash32(p.uid * 29u + (u32)time);
                            pa.clipTimer = 18.f + hashToFloat(ht) * 25.f;
                            if (pa.clipTimer > 0.f && hashToFloat(hash32(ht)) < 0.7f) {
                                pa.activity = ACT_SCENARIO;
                                pa.anchor = pos;
                                pa.anchorYaw = p.yaw + (hashToFloat(hash32(ht * 3u)) - 0.5f) * 2.4f;
                                pa.stance = 8;
                                pa.clip = -1;
                                pa.actTimer = 4.f + hashToFloat(hash32(ht * 5u)) * 4.f;
                                if (plDist < 15.f) aiSay(id, BK_TOURIST, 0.3f);
                                break;
                            }
                        }
                    }
                    // occasional stops: scenario, phone call, bench, bus stop
                    if (pa.actTimer <= 0.f) {
                        u32 h = hash32(p.uid * 31u + (u32)(time * 3.0));
                        pa.actTimer = 12.f + hashToFloat(h) * 25.f;
                        float r = hashToFloat(hash32(h));
                        if (pa.role != PR_JOGGER && pa.walk.state == AI::WS_WALK && r < 0.35f) {
                            // nearby bench / bus stop?
                            std::vector<int> spots;
                            laneGraph.spotsNear(pos, 12.f, spots);
                            int pick = -1;
                            for (int si : spots) {
                                // someone already there?
                                bool taken = false;
                                for (int k = 0; k < (int)ai.ped.size() && k < (int)peds.size() && !taken; k++)
                                    if (k != id && peds[k].used && ai.ped[k].uid == peds[k].uid && ai.ped[k].activity != ACT_WALK &&
                                        length(ai.ped[k].anchor - laneGraph.spots[si].pos.xy()) < 1.0f)
                                        taken = true;
                                if (!taken) {
                                    pick = si;
                                    break;
                                }
                            }
                            if (pick >= 0 && raining && laneGraph.spots[pick].kind == AI::SP_BENCH) pick = -1;   // nobody sits in the rain
                            if (pick >= 0 && r < 0.2f) {
                                const AI::ScenarioPoint& sp = laneGraph.spots[pick];
                                pa.activity = sp.kind == AI::SP_BUS_STOP ? ACT_WAIT_BUS : ACT_SCENARIO;
                                pa.anchor = sp.pos.xy();
                                pa.anchorYaw = atan2f(-sp.face.x, sp.face.y);
                                pa.stance = sp.kind == AI::SP_BENCH ? 6 : (sp.kind == AI::SP_BUS_STOP ? 23 : 0);
                                pa.clip = -1;   // (stance 6 is the looping sit)
                                pa.actTimer = 20.f + hashToFloat(hash32(h * 3u)) * 40.f;
                            } else if (r < 0.28f && pa.role != PR_DRUNK) {
                                // stop against the building side of the sidewalk: phone, smoke, lean
                                const AI::WalkLink& L = laneGraph.walkLinks[pa.walk.link];
                                if (L.kind == AI::WL_SIDEWALK) {
                                    float side = pa.walk.fromA == (L.sb >= L.sa) ? 1.f : -1.f;   // + = right of walking dir
                                    float latE = L.lat;
                                    float outward = latE > 0.f ? 1.f : -1.f;               // building side in edge frame
                                    float lat = outward * L.halfWidth * side;
                                    vec3 a = laneGraph.walkPos(pa.walk.link, pa.walk.x, lat * 1.0f, pa.walk.fromA);
                                    pa.anchor = a.xy();
                                    vec2 streetDir = normalize(laneGraph.walkPos(pa.walk.link, pa.walk.x, -lat, pa.walk.fromA).xy() - a.xy());
                                    pa.anchorYaw = atan2f(-streetDir.x, streetDir.y);
                                    pa.activity = ACT_SCENARIO;
                                    float q = hashToFloat(hash32(h * 5u));
                                    World::Region reg = map->regionAt(pos.x, pos.y);
                                    bool night = env->timeOfDay > 20.f || env->timeOfDay < 4.f;
                                    if (raining) {
                                        pa.stance = q < 0.5f ? 8 : 0;   // sheltering under the facade, waiting it out
                                        pa.clip = -1;
                                    } else if (night && (reg == World::REG_BEACH || reg == World::REG_CALLE_LUNA) && q < 0.35f) {
                                        pa.stance = 9;   // dancing outside the clubs
                                        pa.clip = -1;
                                    } else if (q < 0.4f) {
                                        pa.stance = 8;   // phone call
                                        pa.clip = -1;
                                    } else if (q < 0.7f) {
                                        pa.stance = 10;   // smoking (looping stance)
                                        pa.clip = -1;
                                    } else {
                                        pa.stance = 11;   // leaning on the wall
                                        pa.clip = -1;
                                    }
                                    pa.actTimer = 12.f + hashToFloat(hash32(h * 7u)) * 25.f;
                                }
                            } else if (r < 0.35f && pa.role != PR_DRUNK && !raining && pa.leader < 0) {
                                // window shopping in the shopping streets: a stop at a store window on the building side, a
                                // good look at what is in it (a tourist points something out), then on
                                World::Region reg = map->regionAt(pos.x, pos.y);
                                bool shops = reg == World::REG_DOWNTOWN || reg == World::REG_MIDTOWN || reg == World::REG_NORTH_CITY ||
                                             reg == World::REG_CALLE_LUNA || reg == World::REG_BEACH || reg == World::REG_KEY_CORAL;
                                bool open = env->timeOfDay > 8.5f && env->timeOfDay < 21.5f;
                                vec2 at, face;
                                if (shops && open && laneGraph.walkLinks[pa.walk.link].kind == AI::WL_SIDEWALK && shopWindowNear(*this, pos, at, face)) {
                                    pa.activity = ACT_SCENARIO;
                                    pa.browsing = true;
                                    pa.anchor = at;
                                    pa.anchorYaw = atan2f(-face.x, face.y);
                                    pa.stance = 0;
                                    pa.clip = pa.role == PR_TOURIST && hash32(h * 13u) % 2u == 0u ? (int)Anim::CLIP_POINT : -1;   // (once)
                                    pa.actTimer = 6.f + hashToFloat(hash32(h * 11u)) * 10.f;
                                }
                            }
                        } else if (r >= 0.47f && r < 0.51f && pa.leader < 0 && p.faction == FAC_CIVILIAN && pa.walk.state == AI::WS_WALK &&
                                   pa.walk.link >= 0 && laneGraph.walkLinks[pa.walk.link].kind == AI::WL_SIDEWALK && pa.eventId < 0) {
                            // jaywalking: straight across a quiet, narrow street to the sidewalk opposite
                            const AI::WalkLink& L = laneGraph.walkLinks[pa.walk.link];
                            const World::RoadEdge& e = roads->edges[L.edge];
                            float along = L.length > 1e-3f ? Clamp(pa.walk.x / L.length, 0.f, 1.f) : 0.f;
                            float s = pa.walk.fromA ? Lerp(L.sa, L.sb, along) : Lerp(L.sb, L.sa, along);
                            bool narrow = e.halfWidth < 7.5f && e.cls != World::RC_HIGHWAY && e.cls != World::RC_RAMP && e.cls != World::RC_BOULEVARD;
                            if (narrow && s > 12.f && s < e.length - 12.f) {
                                vec3 c3 = e.posAt(s);
                                vec3 t3 = e.tangentAt(s);
                                vec2 rt = AI::rightOf(normalize(t3.xy() + vec2(1e-5f, 0.f)));
                                vec2 target = c3.xy() - rt * L.lat;   // the mirrored sidewalk
                                // nothing coming within 45 m either way along the street
                                bool clear = true;
                                traffic.hash.query(traffic.bodies, c3.xy() - vec2(45.f), c3.xy() + vec2(45.f), [&](int bi) {
                                    const AI::Body& ob = traffic.bodies[bi];
                                    if (ob.kind == AI::BK_PED) return;
                                    if (fabsf(dot(ob.pos - c3.xy(), rt)) < e.halfWidth + 1.f) clear = false;
                                });
                                if (clear && length(target - pos) > 4.f) {
                                    pa.activity = ACT_CROSS;
                                    b.type = BRAIN_GOTO;
                                    b.goal = dvec3(vec3(target, groundHeight(target.x, target.y, c3.z + 1.5f)));
                                    b.speed = 1.7f;   // a brisk walk across
                                    b.timer = 0.f;
                                    break;
                                }
                            }
                        } else if (r < 0.47f && pa.leader < 0 && p.faction == FAC_CIVILIAN && pa.walk.state == AI::WS_WALK && pa.role != PR_JOGGER &&
                                   pa.role != PR_DRUNK && pa.eventId < 0) {
                            // errands: head into a shop / lobby / front door close by (and out of the simulation)
                            vec3 door;
                            if (aiBuildingDoorNear(*this, pos, 14.f, h, door)) {
                                pa.activity = ACT_ENTER_VEH;   // walk to a point and vanish (population.cpp)
                                pa.targetVeh = -1;
                                b.type = BRAIN_GOTO;
                                b.goal = dvec3(door);
                                b.speed = Max(pa.walk.speed, 1.2f);
                                b.timer = 0.f;
                                break;
                            }
                        }
                    }
                    break;
                }
                case ACT_SCENARIO:
                case ACT_WAIT_BUS:
                case ACT_HAIL_TAXI:
                case ACT_WATCH:
                case ACT_QUEUE:
                case ACT_VENUE:
                case ACT_MEET:
                case ACT_AID:
                case ACT_EVENT: {
                    if (pa.activity == ACT_VENUE && aiVenueStep(*this, id, dt)) break;
                    if (pa.activity == ACT_AID) {
                        // helping someone down hurt: done when they are up (or gone), and stepping back for the medics
                        int v = pa.aidPed;
                        bool ok = v >= 0 && v < (int)peds.size() && v < (int)ai.ped.size() && peds[v].used && ai.ped[v].uid == peds[v].uid &&
                                  ai.ped[v].activity == ACT_HURT && ai.ped[v].hurtCare >= 0.f;
                        if (!ok) pa.actTimer = Min(pa.actTimer, 0.f);
                        else if (ai.ped[v].targetVeh >= 0) pa.actTimer = Min(pa.actTimer, 1.f);   // (a medic is there)
                        if (pa.actTimer <= 0.f) {
                            pa.aidPed = -1;
                            pa.navOk = false;
                            b.edge = -1;
                        }
                    }
                    vec2 to = pa.anchor - pos;
                    float d = length(to);
                    // a pair at the curb stepping in for a greeting (or back after it) goes the last few centimetres at a
                    // careful step, facing the other
                    bool paired = (pa.activity == ACT_VENUE || pa.activity == ACT_MEET) && pa.greetWith >= 0;
                    if (pa.greetT > 0.f) {
                        // in a greeting (a hug, a handshake): stood still, facing the partner, while the clip plays
                        faceYaw = pa.anchorYaw;
                        faceSet = true;
                        stance = 0;
                    } else if (d > (paired ? 0.05f : 0.35f)) {
                        // (runners on a track at a steady run, strollers at an easy pace)
                        float vmax = pa.activity == ACT_VENUE && pa.venueMode == VM_JOG ? 3.1f + hashToFloat(hash32(p.uid)) * 0.6f
                                   : (pa.activity == ACT_VENUE && pa.venueMode == VM_STROLL ? 1.05f + hashToFloat(hash32(p.uid)) * 0.25f : 1.4f);
                        if (pa.activity == ACT_EVENT && pa.homeVeh >= 0 && p.faction == FAC_MEDIC && d > 4.f) vmax = 2.8f;   // (a crew runs to a scene)
                        desired = to / d * (paired ? Min(1.3f, d * 2.5f + 0.12f) : Min(vmax, d * 2.f + 0.3f));
                        faceYaw = paired && d < 0.9f ? pa.anchorYaw : atan2f(-desired.x, desired.y);
                        faceSet = true;
                        if (paired) stance = pa.stance;
                    } else {
                        faceYaw = pa.anchorYaw;
                        faceSet = true;
                        stance = pa.stance;
                        if (pa.activity == ACT_AID) {
                            if (pa.stance == 0) p.animIn.crouch = true;   // kneeling beside them
                            if (pa.barkCooldown <= 0.f && plDist < 25.f) aiSay(id, BK_SAMARITAN, 0.35f);
                        }
                        if (pa.clip >= 0 && p.pendingAction < 0 && p.anim.actionDone()) {
                            p.pendingAction = pa.clip;
                            if (pa.activity == ACT_SCENARIO && pa.clip == Anim::CLIP_POINT) pa.clip = -1;   // (a point at a shop window: once)
                        }
                        if (pa.activity == ACT_EVENT && pa.aimAt >= 0) {
                            // holding someone at gunpoint (mugger)
                            const Ped* v = pa.aimAt < (int)peds.size() && peds[pa.aimAt].used && peds[pa.aimAt].health > 0.f ? &peds[pa.aimAt] : nullptr;
                            if (v && weaponInfo(p.weapon).clipSize > 0) {
                                faceYaw = yawTo(pos, v->pos.toVec3().xy());
                                p.aiming = true;
                                p.aimDir = normalize(pedChestPos(*v) - pedHeadPos(p));
                            } else {
                                pa.aimAt = -1;
                            }
                        }
                        if (pa.activity == ACT_HAIL_TAXI && pa.targetVeh < 0 && pa.clipTimer <= 0.f) {
                            p.pendingAction = Anim::CLIP_WAVE;
                            pa.clipTimer = 3.f + hashToFloat(hash32(p.uid + (u32)time)) * 3.f;
                        }
                        if (pa.activity == ACT_WATCH && pa.clipTimer <= 0.f) {
                            if (hash32(p.uid + (u32)time) % 3 == 0) p.pendingAction = Anim::CLIP_CHEER;
                            pa.clipTimer = 5.f + hashToFloat(hash32(p.uid * 3u + (u32)time)) * 6.f;
                            if (pa.eventId >= 0) aiSay(id, BK_MUSIC_PRAISE, 0.15f);
                        }
                        if (pa.stance == 8 && pa.barkCooldown <= 0.f && plDist < 8.f) aiSay(id, BK_PHONE_CHAT, 0.3f);
                    }
                    pa.clipTimer -= dt;
                    // taxi assigned: walk to the rear door and get in
                    if (pa.activity == ACT_HAIL_TAXI && pa.targetVeh >= 0) {
                        int tv = pa.targetVeh;
                        if (tv >= (int)vehicles.size() || !vehicles[tv].used) {
                            pa.targetVeh = -1;
                            break;
                        }
                        Vehicle& v = vehicles[tv];
                        if (v.sim.speed() < 0.5f) {
                            vec3 door = v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, vec3(vassets[v.model].spec.boxHalf.x + 0.5f, -0.6f, 0.f));
                            vec2 tod = door.xy() - pos;
                            float dd = length(tod);
                            stance = 0;
                            if (dd > 0.8f) {
                                desired = tod / dd * 1.6f;
                                faceYaw = atan2f(-desired.x, desired.y);
                            } else {
                                int seat = freeSeat(tv, false);
                                if (seat > 0) {
                                    warpPedIntoVehicle(id, tv, seat);
                                    VehAI& va = vehAI(tv);
                                    va.fare = id;
                                    // ride somewhere 400-1500 m away
                                    u32 h = hash32(p.uid * 77u + (u32)time);
                                    float ang = hashToFloat(h) * kTwoPi, r = 400.f + hashToFloat(hash32(h)) * 1100.f;
                                    vec2 dest = pos + vec2(cosf(ang), sinf(ang)) * r;
                                    float u;
                                    int ln = laneGraph.nearestLane(dest, vec2(0), 300.f, &u);
                                    va.dest = ln >= 0 ? laneGraph.lanePos(ln, u).xy() : dest;
                                    if (AI::Driver* dr = traffic.get(tv)) {
                                        dr->hasDest = false;
                                        dr->stopPath = -1;
                                        dr->mode = AI::DM_NORMAL;
                                    }
                                    va.task = 0;
                                    va.scene = -1;
                                    pa.activity = ACT_WALK;
                                    pa.targetVeh = -1;
                                    return;
                                }
                                pa.targetVeh = -1;
                            }
                        }
                    }
                    if (pa.activity == ACT_QUEUE && pa.clipTimer <= 0.f) {
                        // shuffling in line: glance around, check the phone, talk to the next in line
                        u32 hq = hash32(p.uid * 5u + (u32)time);
                        pa.clipTimer = 6.f + hashToFloat(hq) * 10.f;
                        if (hq % 4 == 0 && p.pendingAction < 0) p.pendingAction = Anim::CLIP_IDLE_LOOK;
                    }
                    if (pa.actTimer <= 0.f && pa.activity != ACT_EVENT && pa.activity != ACT_HAIL_TAXI && pa.activity != ACT_QUEUE && pa.activity != ACT_VENUE &&
                        pa.activity != ACT_MEET) {
                        pa.activity = ACT_WALK;
                        pa.browsing = false;
                        pa.clip = -1;
                        pa.stance = 0;
                        pa.actTimer = 15.f + hashToFloat(hash32(p.uid + (u32)time)) * 20.f;
                    }
                    if (pa.activity == ACT_HAIL_TAXI && pa.actTimer <= -90.f) {
                        pa.activity = ACT_WALK;   // gave up waiting
                        pa.actTimer = 20.f;
                    }
                    if (pa.activity == ACT_EVENT && pa.homeVeh >= 0) {
                        // emergency crew: attend the scene, then return to the vehicle
                        if (pa.actTimer <= 0.f) {
                            int hv = pa.homeVeh;
                            if (hv < (int)vehicles.size() && vehicles[hv].used) {
                                vec3 vp = vehicles[hv].sim.body.pos.toVec3();
                                vec2 tov = vp.xy() - pos;
                                if (length(tov) > 3.f) {
                                    desired = normalize(tov) * 2.2f;
                                    faceYaw = atan2f(-desired.x, desired.y);
                                    faceSet = true;
                                    stance = 0;
                                } else {
                                    int seat = freeSeat(hv, false);
                                    if (seat > 0) warpPedIntoVehicle(id, hv, seat);
                                    pa.activity = ACT_WALK;
                                    pa.homeVeh = -1;
                                    return;
                                }
                            } else {
                                pa.activity = ACT_WALK;
                                pa.homeVeh = -1;
                            }
                        } else if (d < 1.5f) {
                            p.animIn.crouch = true;   // kneeling next to the casualty
                            if (pa.barkCooldown <= 0.f && plDist < 15.f) aiSay(id, BK_MEDIC, 0.3f);
                        }
                    }
                    break;
                }
                case ACT_ENTER_VEH: {
                    // boarding a bus: walk to the front door, then leave the simulation (on board)
                    int tv = pa.targetVeh;
                    if (tv < 0 || tv >= (int)vehicles.size() || !vehicles[tv].used || vehicles[tv].sim.speed() > 1.f) {
                        pa.activity = ACT_WALK;
                        pa.targetVeh = -1;
                        break;
                    }
                    Vehicle& v = vehicles[tv];
                    const Vehicles::VehicleModel& spec = vassets[v.model].spec;
                    vec3 door = v.sim.body.pos.toVec3() + rotate(v.sim.body.rot, vec3(spec.boxHalf.x + 0.5f, spec.boxHalf.y * 0.7f, 0.f));
                    vec2 tod = door.xy() - pos;
                    float dd = length(tod);
                    if (dd > 0.9f) {
                        desired = tod / dd * 1.7f;
                        faceYaw = atan2f(-desired.x, desired.y);
                        faceSet = true;
                    } else if (!p.persistent) {
                        despawnPed(id);
                        return;
                    }
                    break;
                }
                case ACT_HURT: {
                    // down hurt (startHurt): on the back, writhing, calling for help; up once a medic has seen to them
                    // for a while (the medic stays until then), or after a couple of minutes on their own - the get-up from
                    // the back, then off: to the ambulance with the crew, or away, limping
                    faceYaw = pa.anchorYaw;
                    faceSet = true;
                    if (pa.hurtCare >= 0.f) {
                        stance = 24;
                        int medic = -1;
                        std::vector<int> close;
                        pedsNear(pos, 2.4f, close);
                        for (int o : close)
                            if (o != id && peds[o].faction == FAC_MEDIC && peds[o].state == PS_ONFOOT && o < (int)ai.ped.size() && ai.ped[o].uid == peds[o].uid)
                                medic = o;
                        if (medic >= 0) {
                            pa.hurtCare += dt;
                            pa.targetVeh = ai.ped[medic].homeVeh;
                            ai.ped[medic].actTimer = Max(ai.ped[medic].actTimer, 3.f);   // (seeing to them until they are up)
                        } else if (pa.barkCooldown <= 0.f && plDist < 25.f) {
                            aiSay(id, BK_HURT, 0.3f);
                        }
                        if (pa.hurtCare > 7.f || pa.actTimer <= 0.f) {
                            pa.hurtCare = -1.f;
                            pa.clipTimer = 1.8f;
                            p.pendingAction = Anim::CLIP_GET_UP_BACK;
                            p.health = Max(p.health, p.maxHealth * (medic >= 0 ? 0.45f : 0.32f));
                            p.legInjury = medic >= 0 ? 9.f : Max(p.legInjury, 20.f);   // (patched up: a short limp; on their own, a long one)
                        }
                    } else {
                        // getting up
                        stance = 0;
                        pa.clipTimer -= dt;
                        if (pa.clipTimer <= 0.f) {
                            int amb = pa.targetVeh;
                            bool toAmb = amb >= 0 && amb < (int)vehicles.size() && vehicles[amb].used && vehicles[amb].sim.speed() < 1.f &&
                                         length(vehicles[amb].sim.body.pos.toVec3().xy() - pos) < 45.f;
                            pa.hurtCare = 0.f;
                            pa.stance = 0;
                            if (toAmb) {
                                pa.activity = ACT_ENTER_VEH;   // into the ambulance: off to the hospital
                                pa.targetVeh = amb;
                            } else {
                                pa.activity = ACT_WALK;
                                pa.targetVeh = -1;
                                pa.navOk = false;
                                b.edge = -1;
                                pa.actTimer = 40.f;
                            }
                        }
                    }
                    break;
                }
                case ACT_INSPECT: {
                    // stop and look at what happened, then carry on (a little faster)
                    faceYaw = yawTo(pos, pa.threatPos);
                    faceSet = true;
                    if (pa.actTimer <= 0.f) {
                        pa.activity = ACT_WALK;
                        pa.actTimer = 15.f + hashToFloat(hash32(p.uid + (u32)time)) * 15.f;
                    }
                    break;
                }
                case ACT_ROADRAGE: {
                    // the player crashed into our car: storm over, shout, maybe throw a punch, then drive off
                    int hv = pa.homeVeh;
                    bool carOk = hv >= 0 && hv < (int)vehicles.size() && vehicles[hv].used && !vehicles[hv].exploded && vehicles[hv].seats[0] < 0;
                    bool plOk = pl && pl->health > 0.f;
                    // the player stayed in the (stopped) car: storm up to the driver's window instead
                    int plCar = plOk && pl->state == PS_INVEHICLE && pl->vehicle >= 0 && vehicles[pl->vehicle].used ? pl->vehicle : -1;
                    bool carStill = plCar >= 0 && vehicles[plCar].sim.speed() < 2.5f;
                    if (pa.actTimer > 0.f && plOk && plDist < 30.f && (pl->state == PS_ONFOOT || carStill)) {
                        vec2 goal = ppos;
                        float reach = 2.2f;
                        if (plCar >= 0) {
                            const Vehicle& pv = vehicles[plCar];
                            const Vehicles::VehicleModel& ps = vassets[pv.model].spec;
                            float side = !ps.seats.empty() && ps.seats[0].pos.x > 0.f ? 1.f : -1.f;
                            goal = (pv.sim.body.pos.toVec3() + rotate(pv.sim.body.rot, vec3(side * (ps.boxHalf.x + 0.55f), 0.2f, 0.f))).xy();
                            reach = 0.5f;
                        }
                        vec2 to = goal - pos;
                        float d = length(to);
                        if (d > reach) desired = to / Max(d, 1e-3f) * (d > 8.f ? 3.2f : Clamp(d * 1.5f, 0.6f, 1.5f));
                        faceYaw = yawTo(pos, ppos);
                        faceSet = true;
                        if (pa.shoutTimer <= 0.f && d < 12.f) {
                            aiSay(id, (hash32(p.uid + (u32)time) % 3) == 0 ? BK_CRASH : BK_ROAD_RAGE, 1.f, true);
                            if (p.pendingAction < 0) p.pendingAction = Anim::CLIP_POINT;
                            pa.shoutTimer = 3.f + hashToFloat(hash32(p.uid * 3u + (u32)time)) * 2.f;
                        }
                        bool armed = pl->weapon != WPN_FISTS && weaponInfo(pl->weapon).clipSize > 0;
                        if (d < reach + 0.5f && !armed) pa.linger += dt;
                        if (plCar >= 0) {
                            // at the window: the bold pound on the glass every couple of seconds
                            if (pa.temper == 2 && pa.linger > 1.5f && d < 1.2f && p.pendingAction < 0 && fmodf(pa.linger, 2.2f) < dt) {
                                p.pendingAction = (hash32(p.uid + (u32)(pa.linger * 3.f)) & 1) ? Anim::CLIP_PUNCH_R : Anim::CLIP_PUNCH_L;
                                vehicles[plCar].hornOn = false;
                                aiSay(id, BK_ROAD_RAGE, 0.6f, true);
                            }
                            if (pa.linger > 9.f) pa.actTimer = Min(pa.actTimer, 0.5f);   // said their piece
                            break;
                        }
                        // standing face to face with an unarmed player for a while: it comes to blows
                        if (pa.linger > 3.5f && pa.temper == 2) {
                            b.type = BRAIN_COMBAT;
                            b.target = player;
                            b.timer = 0.f;
                            pa.activity = ACT_WALK;
                            pa.linger = 0.f;
                        }
                        break;
                    }
                    // back to the car and away
                    if (!carOk) {
                        pa.activity = ACT_WALK;
                        pa.homeVeh = -1;
                        break;
                    }
                    vec3 cp = vehicles[hv].sim.body.pos.toVec3();
                    vec2 toc = cp.xy() - pos;
                    if (length(toc) > 3.f) {
                        desired = normalize(toc) * 2.2f;
                        faceYaw = atan2f(-desired.x, desired.y);
                        faceSet = true;
                    } else {
                        warpPedIntoVehicle(id, hv, 0);
                        b.type = BRAIN_DRIVER;
                        pa.activity = ACT_WALK;
                        pa.homeVeh = -1;
                        pa.linger = 0.f;
                        vehicles[hv].parked = false;
                        attachTraffic(hv);
                        vehAI(hv).rage = 0;
                        return;
                    }
                    break;
                }
                case ACT_ERRAND: {
                    // delivery: to the door, a while there, back to the van and away
                    int hv = pa.homeVeh;
                    bool carOk = hv >= 0 && hv < (int)vehicles.size() && vehicles[hv].used && !vehicles[hv].exploded && vehicles[hv].seats[0] < 0;
                    if (!carOk || pa.actTimer <= 0.f) {
                        pa.activity = ACT_WALK;
                        pa.homeVeh = -1;
                        pa.navOk = false;
                        if (carOk) vehAI(hv).errand = 0;
                        break;
                    }
                    VehAI& hva = vehAI(hv);
                    if (hva.errand == 2) {
                        vec2 to = pa.anchor - pos;
                        float d = length(to);
                        if (d > 0.7f) {
                            desired = to / d * Min(1.5f, d * 2.f + 0.3f);
                            faceYaw = atan2f(-desired.x, desired.y);
                        } else {
                            faceYaw = yawTo(pos, pa.anchor + (pa.anchor - vehicles[hv].sim.body.pos.toVec3().xy()));   // facing the door
                            pa.clipTimer -= dt;
                            if (pa.clipTimer <= 0.f) hva.errand = 3;   // signed for: head back
                        }
                        faceSet = true;
                        break;
                    }
                    // back to the driver's door
                    const Vehicle& hvv = vehicles[hv];
                    const Vehicles::VehicleModel& hs = vassets[hvv.model].spec;
                    float side = !hs.seats.empty() && hs.seats[0].pos.x > 0.f ? 1.f : -1.f;
                    vec2 door = (hvv.sim.body.pos.toVec3() + rotate(hvv.sim.body.rot, vec3(side * (hs.boxHalf.x + 0.5f), 0.3f, 0.f))).xy();
                    vec2 to = door - pos;
                    float d = length(to);
                    if (d > 0.8f) {
                        desired = to / d * Min(1.6f, d * 2.f + 0.3f);
                        faceYaw = atan2f(-desired.x, desired.y);
                        faceSet = true;
                        break;
                    }
                    warpPedIntoVehicle(id, hv, 0);
                    b.type = BRAIN_DRIVER;
                    pa.activity = ACT_WALK;
                    pa.homeVeh = -1;
                    vehicles[hv].parked = false;
                    hva.errand = 0;
                    hva.errandTimer = -60.f;   // no new stop for a while
                    attachTraffic(hv);
                    return;
                }
                case ACT_LEAVE_CAR: {
                    // parked and out: round the back of the car (not through it) toward the building, then inside
                    int hv = pa.homeVeh;
                    vec2 target = pa.anchor;
                    vec2 goal = target;
                    if (hv >= 0 && hv < (int)vehicles.size() && vehicles[hv].used) {
                        const Vehicle& hvv = vehicles[hv];
                        const Vehicles::VehicleModel& hs = vassets[hvv.model].spec;
                        vec2 cc = hvv.sim.body.pos.toVec3().xy();
                        vec2 cfw = hvv.sim.forward().xy();
                        cfw = length2(cfw) > 1e-6f ? normalize(cfw) : vec2(0, 1);
                        vec2 crt = AI::rightOf(cfw);
                        vec2 lp = pos - cc;
                        float ly = dot(lp, cfw), lx = dot(lp, crt);
                        float targetSide = dot(target - cc, crt) >= 0.f ? 1.f : -1.f, pedSide = lx >= 0.f ? 1.f : -1.f;
                        if (pedSide != targetSide && fabsf(lx) > hs.boxHalf.x * 0.5f) {
                            // round the back (or the front, if the next car stands right behind), then across
                            float endSign = aiCarEndToWalkRound(*this, hv, ly > hs.boxHalf.y * 0.5f ? 1.f : -1.f);
                            float side = fabsf(ly) < hs.boxHalf.y + 0.6f || ly * endSign < 0.f ? pedSide : targetSide;
                            goal = cc + cfw * (endSign * (hs.boxHalf.y + 0.8f)) + crt * (side * (hs.boxHalf.x + 0.6f));
                        }
                    }
                    if (goal.x == target.x && goal.y == target.y) {
                        // clear of the car: off to the door and out of the simulation (population.cpp)
                        pa.activity = ACT_ENTER_VEH;
                        pa.targetVeh = -1;
                        pa.homeVeh = -1;
                        b.type = BRAIN_GOTO;
                        b.goal = dvec3(vec3(target, groundHeight(target.x, target.y, (float)p.pos.z + 1.5f)));
                        b.speed = 1.4f;
                        b.timer = 0.f;
                        break;
                    }
                    if (pa.actTimer <= 0.f) {
                        pa.activity = ACT_WALK;
                        pa.homeVeh = -1;
                        pa.navOk = false;
                        break;
                    }
                    vec2 tg = goal - pos;
                    float dg = Max(length(tg), 1e-3f);
                    desired = tg / dg * Min(1.4f, dg * 2.f + 0.4f);
                    faceYaw = atan2f(-desired.x, desired.y);
                    faceSet = true;
                    break;
                }
                case ACT_DRIVE_OFF: {
                    // owner of a car parked at the curb: round to the driver's door, open it, get in; the car then
                    // blinks, waits for a gap and pulls out (traffic.cpp)
                    int hv = pa.homeVeh;
                    bool carOk = hv >= 0 && hv < (int)vehicles.size() && vehicles[hv].used && !vehicles[hv].exploded && !vehicles[hv].sim.wrecked &&
                                 vehicles[hv].seats[0] < 0 && vehicles[hv].parked && !vehicles[hv].playerUsed;
                    if (!carOk || pa.actTimer <= 0.f) {
                        pa.activity = ACT_WALK;
                        pa.homeVeh = -1;
                        pa.navOk = false;
                        break;
                    }
                    const Vehicle& hvv = vehicles[hv];
                    const Vehicles::VehicleModel& hs = vassets[hvv.model].spec;
                    bool left = !hs.seats.empty() ? hs.seats[0].exitLeft : true;
                    float sy = !hs.seats.empty() ? hs.seats[0].pos.y : 0.f;
                    vec2 door = (hvv.sim.body.pos.toVec3() + rotate(hvv.sim.body.rot, vec3(left ? -(hs.boxHalf.x + 0.4f) : hs.boxHalf.x + 0.4f, sy - 0.2f, 0.f))).xy();
                    vec3 cf = hvv.sim.forward();
                    float doorYaw = atan2f(-cf.x, cf.y) + (left ? -kPi * 0.5f : kPi * 0.5f);
                    if (pa.clipTimer < 0.f) {
                        vec2 to = door - pos;
                        float d = length(to);
                        if (d > 0.5f) {
                            // walk round the car rather than through it: aim for the door via the nearer end
                            vec2 cc = hvv.sim.body.pos.toVec3().xy();
                            vec2 cfw = normalize(cf.xy() + vec2(1e-4f, 0.f));
                            vec2 lp = pos - cc;
                            float ly = dot(lp, cfw), lx = dot(lp, AI::rightOf(cfw));
                            float doorSide = left ? -1.f : 1.f, pedSide = lx >= 0.f ? 1.f : -1.f, endSign = ly >= 0.f ? 1.f : -1.f;
                            vec2 goal = door;
                            if (pedSide != doorSide && fabsf(lx) > hs.boxHalf.x * 0.5f) {
                                // on the far side: along the car to its nearer end (unless another car stands there),
                                // then across in front of / behind it
                                endSign = aiCarEndToWalkRound(*this, hv, endSign);
                                float side = fabsf(ly) < hs.boxHalf.y + 0.6f || ly * endSign < 0.f ? pedSide : doorSide;
                                goal = cc + cfw * (endSign * (hs.boxHalf.y + 0.8f)) + AI::rightOf(cfw) * (side * (hs.boxHalf.x + 0.6f));
                            }
                            vec2 tg = goal - pos;
                            float dg = Max(length(tg), 1e-3f);
                            desired = tg / dg * Min(1.5f, d * 2.f + 0.4f);
                            faceYaw = atan2f(-desired.x, desired.y);
                        } else {
                            // at the door: open it and climb in
                            pa.clipTimer = 1.05f;
                            p.yaw = doorYaw;
                            if (p.pendingAction < 0) p.pendingAction = left ? Anim::CLIP_ENTER_CAR_L : Anim::CLIP_ENTER_CAR_R;
#ifdef HAVE_AUDIO
                            Audio::play(Audio::SFX_CAR_DOOR_OPEN, vec3(door, (float)p.pos.z + 0.8f), 0.6f);
#endif
                            faceYaw = doorYaw;
                        }
                        faceSet = true;
                        break;
                    }
                    faceYaw = doorYaw;
                    faceSet = true;
                    pa.clipTimer -= dt;
                    if (pa.clipTimer > 0.f) break;
                    Vehicle& car = vehicles[hv];
                    warpPedIntoVehicle(id, hv, 0);
                    b.type = BRAIN_DRIVER;
                    pa.activity = ACT_WALK;
                    pa.homeVeh = -1;
                    pa.clipTimer = 0.f;
                    car.sim.engineOn = true;
                    float tod = env ? env->timeOfDay : 12.f;
                    car.lightsOn = tod > 19.2f || tod < 6.6f || (env && env->rain > 0.5f);
                    VehAI& cva = vehAI(hv);
                    cva.pullOut = 1;
                    cva.pullTimer = 0.f;
                    cva.role = VR_TRAFFIC;
                    ai.stats.departures++;
                    return;
                }
                case ACT_CALL_POLICE: {
                    // walk away from the scene a bit, then talk on the phone
                    vec2 away = pos - pa.threatPos;
                    float d = length(away);
                    stance = 8;   // phone to the ear, also while backing off
                    if (d < 18.f && pa.actTimer > 4.f) {
                        desired = (d > 1e-3f ? away / d : AI::yawDir(p.yaw)) * (d < 10.f ? 3.2f : 2.4f);
                        faceYaw = atan2f(-desired.x, desired.y);
                    } else {
                        faceYaw = yawTo(pos, pa.threatPos) + kPi;
                    }
                    faceSet = true;
                    if (pa.actTimer <= 0.f) pa.activity = ACT_WALK;
                    break;
                }
                case ACT_FILM: {
                    vec2 to = pa.threatPos - pos;
                    float d = length(to);
                    if (d < 6.f) desired = -to / Max(d, 1e-3f) * 1.2f;
                    faceYaw = yawTo(pos, pa.threatPos);
                    faceSet = true;
                    stance = 8;   // phone held up
                    // follow the action (a fight moving about, an arrest walked to the car)
                    for (const Stimulus& s : ai.stimuli)
                        if ((s.kind == STIM_FIGHT || s.kind == STIM_ARREST) && length(s.pos.toVec3().xy() - pa.threatPos) < 15.f) pa.threatPos = s.pos.toVec3().xy();
                    if (pa.actTimer <= 0.f) pa.activity = ACT_WALK;
                    break;
                }
                case ACT_CUFFED: {
                    // arrested (police.cpp police_escort): walked to the patrol car a step ahead of the officer holding
                    // the arm, round to the back door, in; while the car is on its way, sitting on the kerb with the
                    // officer over them. The officer called away or down (or nobody came): off and running
                    int cop = b.target;
                    bool copOk = cop >= 0 && cop < (int)peds.size() && cop < (int)ai.ped.size() && peds[cop].used && peds[cop].health > 0.f &&
                                 ai.ped[cop].uid == peds[cop].uid && ai.ped[cop].escortPed == id && peds[cop].brain.type == BRAIN_GOTO &&
                                 peds[cop].brain.target == -3;
                    if (!copOk || pa.actTimer <= 0.f) {
                        pa.activity = ACT_WALK;
                        pa.clipTimer = 0.f;
                        b.type = BRAIN_FLEE;
                        b.target = -1;
                        b.goal = p.pos;
                        b.timer = 0.f;
                        break;
                    }
                    p.weapon = WPN_FISTS;
                    stance = 25;   // (in cuffs: wrists crossed at the small of the back, head down)
                    const PedAI& ca = ai.ped[cop];
                    vec2 cpos = peds[cop].pos.toVec3().xy();
                    float copD = length(cpos - pos);
                    int car = ca.escortCar;
                    bool carOk = car >= 0 && car < (int)vehicles.size() && vehicles[car].used && vehicles[car].uid == ca.escortCarUid && !vehicles[car].exploded &&
                                 vehicles[car].sim.speed() < 0.6f &&
                                 (car >= (int)ai.veh.size() || ai.veh[car].task != PT_TRANSPORT || ai.veh[car].transportState == 2);
                    // the seat: the back where there is one (taken meanwhile: another)
                    int seat = -1;
                    if (carOk) {
                        const Vehicle& cv = vehicles[car];
                        int n = Min((int)vassets[cv.model].spec.seats.size(), 8);
                        const int order[3] = {3, 2, 1};
                        for (int k : order)
                            if (seat < 0 && k < n && cv.seats[k] < 0) seat = k;
                        if (seat < 0) carOk = false;
                    }
                    if (!carOk) {
                        // waiting for the car: down on the kerb once the officer is next to them
                        pa.clipTimer = -1.f;
                        if (copD < 2.6f && length(p.vel.xy()) < 0.3f) stance = 21;
                        faceYaw = p.yaw;
                        faceSet = true;
                        break;
                    }
                    const Vehicle& cv = vehicles[car];
                    const Vehicles::VehicleModel& cs = vassets[cv.model].spec;
                    bool left = cs.seats[seat].exitLeft;
                    float sy = cs.seats[seat].pos.y;
                    vec2 door = (cv.sim.body.pos.toVec3() + rotate(cv.sim.body.rot, vec3(left ? -(cs.boxHalf.x + 0.42f) : cs.boxHalf.x + 0.42f, sy - 0.15f, 0.f))).xy();
                    vec3 cf = cv.sim.forward();
                    float doorYaw = atan2f(-cf.x, cf.y) + (left ? -kPi * 0.5f : kPi * 0.5f);
                    if (pa.clipTimer < 0.f) {
                        vec2 to = door - pos;
                        float d = length(to);
                        if (d > 0.5f) {
                            // round the car rather than through it (as ACT_DRIVE_OFF), at the officer's pace: never
                            // pulling away from the hand on the arm
                            vec2 cc = cv.sim.body.pos.toVec3().xy();
                            vec2 cfw = normalize(cf.xy() + vec2(1e-4f, 0.f));
                            vec2 lp = pos - cc;
                            float ly = dot(lp, cfw), lx = dot(lp, AI::rightOf(cfw));
                            float doorSide = left ? -1.f : 1.f, pedSide = lx >= 0.f ? 1.f : -1.f, endSign = ly >= 0.f ? 1.f : -1.f;
                            vec2 goal = door;
                            if (pedSide != doorSide && fabsf(lx) > cs.boxHalf.x * 0.5f) {
                                endSign = aiCarEndToWalkRound(*this, car, endSign);
                                float side = fabsf(ly) < cs.boxHalf.y + 0.6f || ly * endSign < 0.f ? pedSide : doorSide;
                                goal = cc + cfw * (endSign * (cs.boxHalf.y + 0.8f)) + AI::rightOf(cfw) * (side * (cs.boxHalf.x + 0.6f));
                            }
                            vec2 tg = goal - pos;
                            float dg = Max(length(tg), 1e-3f);
                            // (at the officer's pace; the last few metres round the car regardless - the officer may be
                            //  a step behind, held up by the bodywork)
                            bool atCar = length(cv.sim.body.pos.toVec3().xy() - pos) < cs.boxHalf.y + 2.5f;
                            float spd = copD < 2.2f || atCar ? Min(1.15f, d * 2.f + 0.3f) : (copD < 3.5f ? 0.5f : 0.f);
                            desired = tg / dg * spd;
                            faceYaw = atan2f(-tg.x, tg.y);
                        } else if (copD < 6.f) {
                            // at the door: in (the officer's hand on the head)
                            pa.clipTimer = 1.05f;
                            p.yaw = doorYaw;
                            if (p.pendingAction < 0) p.pendingAction = left ? Anim::CLIP_ENTER_CAR_L : Anim::CLIP_ENTER_CAR_R;
#ifdef HAVE_AUDIO
                            Audio::play(Audio::SFX_CAR_DOOR_OPEN, vec3(door, (float)p.pos.z + 0.8f), 0.6f);
#endif
                            faceYaw = doorYaw;
                        } else {
                            faceYaw = yawTo(pos, cpos);
                        }
                        faceSet = true;
                    } else {
                        faceYaw = doorYaw;
                        faceSet = true;
                        stance = 0;   // (the climb in: the clip has the body)
                        pa.clipTimer -= dt;
                        if (pa.clipTimer <= 0.f) {
                            warpPedIntoVehicle(id, car, seat);
                            b.type = BRAIN_PASSENGER;
                            b.target = -1;
                            pa.activity = ACT_WALK;
                            pa.clipTimer = 0.f;
                            ai.stats.custody++;
                            return;
                        }
                    }
                    if (pa.shoutTimer <= 0.f && plDist < 35.f) {
                        aiSay(id, BK_SUSPECT, 0.55f);
                        pa.shoutTimer = 5.f + hashToFloat(hash32(p.uid + (u32)time)) * 6.f;
                    }
                    break;
                }
                case ACT_HANDS_UP: {
                    stance = 5;
                    // (a suspect who gave up faces the officer on them; anyone else, the gun on them)
                    if (b.target >= 0 && b.target < (int)peds.size() && peds[b.target].used && peds[b.target].faction == FAC_POLICE) {
                        faceYaw = yawTo(pos, peds[b.target].pos.toVec3().xy());
                        faceSet = true;
                    } else if (pl) {
                        faceYaw = yawTo(pos, ppos);
                        faceSet = true;
                    }
                    if (pa.actTimer <= 0.f) {
                        // the gun is lowered: run
                        pa.activity = ACT_WALK;
                        b.type = BRAIN_FLEE;
                        b.target = player;
                        b.timer = 0.f;
                    }
                    break;
                }
                case ACT_CONFRONT: {
                    if (!pl) {
                        pa.activity = ACT_WALK;
                        break;
                    }
                    vec2 to = ppos - pos;
                    float d = length(to);
                    if (d > 3.f) desired = to / d * 1.8f;
                    faceYaw = yawTo(pos, ppos);
                    faceSet = true;
                    bool armed = pl->weapon != WPN_FISTS && weaponInfo(pl->weapon).clipSize > 0;
                    if (pl->aiming || (pa.actTimer <= 0.f && (armed || pa.provoked) && d < 16.f)) {
                        b.type = BRAIN_COMBAT;
                        b.target = player;
                        aiSay(id, BK_GANG_ATTACK, 1.f, true);
                        std::vector<int> crew;
                        pedsNear(pos, 30.f, crew);
                        for (int c : crew)
                            if (c != id && peds[c].faction == p.faction && peds[c].health > 0.f && peds[c].state == PS_ONFOOT && !peds[c].persistent) {
                                peds[c].brain.type = BRAIN_COMBAT;
                                peds[c].brain.target = player;
                            }
                        pa.activity = ACT_WALK;
                    } else if (d > 22.f || (!armed && !pa.provoked)) {
                        aiSay(id, BK_GANG_TAUNT, 0.6f);
                        pa.activity = ACT_WALK;
                        pa.linger = 0.f;
                        pa.provoked = false;
                    }
                    break;
                }
                default:
                    pa.activity = ACT_WALK;
                    break;
            }
            break;
        }
    }
    if (b.type != BRAIN_COMBAT && !(b.type == BRAIN_WANDER && pa.activity == ACT_EVENT && pa.aimAt >= 0)) {
        p.aiming = false;
        p.firing = false;
    }
    if (stance != 6 && pa.activity != ACT_EVENT && !(pa.activity == ACT_AID && pa.stance == 0)) p.animIn.crouch = false;
    // squared up in a fist/knife fight: melee.cpp owns the guard / block stances (19, 20)
    bool meleeEngaged = p.meleeTarget >= 0 && p.meleeTarget < (int)peds.size() && peds[p.meleeTarget].used && peds[p.meleeTarget].health > 0.f &&
                        length(rel(peds[p.meleeTarget].pos, p.pos)) < 4.f && (p.animIn.stance == 19 || p.animIn.stance == 20);
    if (!meleeEngaged) p.animIn.stance = stance;
    // hurt (a limp on a wounded leg, the hunch at low health - the animator's legHurt / wounded): about 1 m/s on a
    // stroll, a desperate hobble when running for it
    {
        float hurt = Max(p.legInjury > 0.f ? Saturate(p.legInjury / 8.f) : 0.f, Saturate((0.5f - p.health / Max(p.maxHealth, 1.f)) * 2.5f));
        float l = length(desired);
        if (hurt > 0.f && l > 1e-3f) {
            float cap = Lerp(l, b.type == BRAIN_FLEE ? 1.7f : 1.f, Saturate(hurt * 1.5f));
            if (l > cap) desired = desired * (cap / l);
        }
    }
    if (pa.activity == ACT_HURT) desired = vec2(0.f);
    // face & move
    if (faceSet) turnTo(p, faceYaw, turnRate, dt);
    else if (length2(desired) > 0.04f) turnTo(p, atan2f(-desired.x, desired.y), turnRate, dt);
    movePed(p, desired, dt, false);
    pa.lastPos = pos;
}

// ------------------------------------------------------------------------------------------------------------------
// Acquaintances running into each other on the sidewalk, now and then round the player: two people strolling alone
// who know each other (a few pairs in a hundred, from both uids) come face to face and stop - "hey, look who it is!" -
// step in for a hug, a kiss on the cheek or (two in suits) a handshake, stand talking a while, taking turns, then say
// goodbye and go on their ways. Days and evenings, not in the rain; one or two at a time, with a pause between.
namespace pedai_meet {

// someone strolling alone along a sidewalk or a path with nothing else on their mind (not a jogger, a drunk, an officer,
// not on the phone, not in a group, not crossing, not headed in through a door)
bool meetable(GameWorld& g, int i) {
    const Ped& p = g.peds[i];
    if (!p.used || p.isPlayer || p.persistent || p.state != PS_ONFOOT || p.ragdoll || p.health <= 0.f || p.charIndex < 0) return false;
    if (p.faction != FAC_CIVILIAN || p.brain.type != BRAIN_WANDER || p.pendingAction >= 0 || !p.anim.actionDone()) return false;
    if (i >= (int)g.ai.ped.size() || g.ai.ped[i].uid != p.uid) return false;
    const PedAI& q = g.ai.ped[i];
    if (q.activity != ACT_WALK || q.leader >= 0 || !q.navOk || q.eventId >= 0 || q.greetWith >= 0 || q.goInside || q.walkStance == 8) return false;
    if (q.role == PR_JOGGER || q.role == PR_DRUNK || q.role == PR_COP) return false;
    if (q.walk.state != AI::WS_WALK || q.walk.link < 0 || q.walk.link >= (int)g.laneGraph.walkLinks.size()) return false;
    u8 k = g.laneGraph.walkLinks[q.walk.link].kind;
    return k == AI::WL_SIDEWALK || k == AI::WL_PATH;
}

// still in the meeting (not knocked down, fled, despawned)
bool inMeet(GameWorld& g, int i, u32 uid, int other) {
    if (i < 0 || i >= (int)g.peds.size() || i >= (int)g.ai.ped.size()) return false;
    const Ped& p = g.peds[i];
    const PedAI& q = g.ai.ped[i];
    return p.used && p.uid == uid && q.uid == uid && p.state == PS_ONFOOT && !p.ragdoll && p.health > 0.f && p.brain.type == BRAIN_WANDER &&
           q.activity == ACT_MEET && q.greetWith == other;
}

// back to the stroll (the walker picks up where it left off)
void walkOn(GameWorld& g, int i) {
    PedAI& q = g.pedAI(i);
    if (q.activity == ACT_MEET) q.activity = ACT_WALK;
    q.greetWith = -1;
    q.greetT = 0.f;
    q.stance = 0;
    q.clip = -1;
    q.actTimer = 25.f + hashToFloat(hash32(q.uid * 13u + 5u)) * 20.f;   // (no other stop straight away)
}

}  // namespace pedai_meet

void GameWorld::aiStreetMeets(float dt) {
    using namespace pedai_meet;
    ai.meetGap -= dt;
    // the meetings under way
    for (size_t k = 0; k < ai.meets.size();) {
        AIState::StreetMeet& m = ai.meets[k];
        bool okA = inMeet(*this, m.a, m.ua, m.b), okB = inMeet(*this, m.b, m.ub, m.a);
        if (!okA || !okB) {
            // one of them was called away (a scare, a bump, gone): the other goes on alone
            if (okA) walkOn(*this, m.a);
            if (okB) walkOn(*this, m.b);
            LOG("street meet: peds %d and %d broken off in phase %d", m.a, m.b, (int)m.phase);
            ai.meets.erase(ai.meets.begin() + k);
            continue;
        }
        PedAI& qa = pedAI(m.a);
        PedAI& qb = pedAI(m.b);
        qa.actTimer = qb.actTimer = Max(qa.actTimer, 30.f);   // (the stroll's own stops wait)
        m.t -= dt;
        m.sayT -= dt;
        bool done = false;
        switch (m.phase) {
            case 0:   // stepping in
                if (pop_detail::greetReady(*this, m.a, m.b) || m.t <= 0.f) {
                    m.t = pop_detail::greetStart(*this, m.a, m.b, m.clip);
                    m.phase = 1;
                }
                break;
            case 1:   // the greeting
                if (qa.greetT <= 0.f && qb.greetT <= 0.f) {
                    pop_detail::greetPart(*this, m.a, m.b, 0.95f);   // a step back to talk
                    qa.stance = qb.stance = 7;
                    m.phase = 2;
                    m.t = 10.f + hashToFloat(hash32(m.ua * 3u + m.ub)) * 12.f;
                    m.sayT = 0.5f;
                    m.turn = 1;   // (the other answers first)
                }
                break;
            case 2:   // talking, taking turns
                if (m.sayT <= 0.f) {
                    int who = m.turn ? m.b : m.a;
                    aiSay(who, m.lines == 0 ? BK_REUNION : BK_SMALLTALK, 1.f);
                    if (peds[who].speechCooldown > 0.f) {
                        m.sayT = peds[who].speechCooldown + 0.3f + hashToFloat(hash32(m.ua + m.lines * 31u)) * 0.9f;
                        m.turn ^= 1;
                        m.lines++;
                    } else {
                        m.sayT = 0.7f;   // (someone else nearby has the floor: in a moment)
                    }
                }
                if (m.t <= 0.f) {
                    aiSay(m.a, BK_PARTING, 1.f, true);
                    m.phase = 3;
                    m.t = 1.8f;
                    m.sayT = 0.9f;
                }
                break;
            default:   // the goodbye (the other answers it), then off
                if (m.sayT <= 0.f && m.sayT > -dt * 1.5f) aiSay(m.b, BK_PARTING, 1.f, true);
                if (m.t <= 0.f) {
                    walkOn(*this, m.a);
                    walkOn(*this, m.b);
                    done = true;
                    LOG("street meet: peds %d and %d part after %d lines", m.a, m.b, (int)m.lines);
                }
                break;
        }
        if (done) {
            ai.meets.erase(ai.meets.begin() + k);
            continue;
        }
        k++;
    }
    // new ones: round the player, now and then
    ai.meetScan -= dt;
    Ped* pl = playerPed();
    if (!pl || ai.meetScan > 0.f) return;
    ai.meetScan = 0.4f;
    if (ai.meetGap > 0.f || ai.meets.size() >= 2 || !env) return;
    if (env->timeOfDay < 7.f || env->timeOfDay > 22.5f || env->rain > 0.3f) return;
    std::vector<int> around, cand;
    pedsNear(pl->pos.toVec3().xy(), 70.f, around);
    for (int i : around)
        if (meetable(*this, i) && length(peds[i].vel.xy()) > 0.6f) cand.push_back(i);
    for (int a : cand) {
        const Ped& A = peds[a];
        vec2 pa = A.pos.toVec3().xy();
        vec2 fa = normalize(A.vel.xy());
        for (int b : cand) {
            if (b == a) continue;
            const Ped& B = peds[b];
            vec2 d = B.pos.toVec3().xy() - pa;
            float dist = length(d);
            if (dist < 1.6f || dist > 5.f || fabsf(B.pos.z - A.pos.z) > 1.2) continue;
            // coming face to face: the other ahead, nearly in line, walking the other way
            if (dot(d, fa) < dist * 0.8f || fabsf(cross(fa, d)) > 2.2f || dot(normalize(B.vel.xy()), fa) > -0.7f) continue;
            // who knows whom: a few pairs in a hundred
            u32 lo = Min(A.uid, B.uid), hi = Max(A.uid, B.uid);
            if (hashToFloat(hash32(lo * 2654435761u ^ (hi + 0x6d2b79f5u))) > 0.035f * ai.meetBoost) continue;
            PedAI& qa = pedAI(a);
            PedAI& qb = pedAI(b);
            bool formal = qa.role == PR_BUSINESS && qb.role == PR_BUSINESS;
            int clip = pop_detail::greetPick(*this, a, b, hash32(lo * 7919u + hi), formal);
            if (clip < 0) continue;
            for (int i : {a, b}) {
                PedAI& q = pedAI(i);
                q.activity = ACT_MEET;
                q.clip = -1;
                q.walkStance = 0;
                q.actTimer = 60.f;
            }
            pop_detail::greetBegin(*this, a, b, clip);
            AIState::StreetMeet m;
            m.a = a;
            m.b = b;
            m.ua = A.uid;
            m.ub = B.uid;
            m.clip = (i8)clip;
            m.t = 1.5f + dist / 1.1f;   // (the step in: a moment to close the gap)
            ai.meets.push_back(m);
            ai.meetsStarted++;
            ai.meetGap = (35.f + hashToFloat(hash32(A.uid + 77u)) * 45.f) / Max(ai.meetBoost, 1.f);
            aiSay(a, BK_REUNION, 1.f, true);
            LOG("street meet %d: peds %d and %d (%s) at %.0f %.0f, %.1f m apart", ai.meetsStarted, a, b, Anim::clipInfo((Anim::Clip)clip).name, pa.x, pa.y, dist);
            return;
        }
    }
}

}  // namespace Game
