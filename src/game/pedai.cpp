// Pedestrian brains: sidewalk wandering (pednav.cpp) with destinations and street crossings, scenarios at benches,
// bus stops, walls and on the beach, groups walking and chatting together, joggers, taxi hailing and bus riding;
// reactions to gunfire/explosions/bodies/fights (flee, cower, dive away from speeding cars, hands up at gunpoint,
// bystanders filming, witnesses phoning the police), gang members defending their territory, carjack victims.
#include "gameworld.h"

namespace Game {

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
    pa.diveCooldown -= dt;
    pa.shoutTimer -= dt;
    bool gang = isGang(p.faction);
    int selfBody = id < (int)ai.pedBody.size() ? ai.pedBody[id] : -1;
    // ---------------------------------------------------------------- perception (staggered)
    if (pa.think <= 0.f) {
        pa.think = 0.25f + hashToFloat(hash32(p.uid + (u32)(time * 10.0))) * 0.25f;
        bool calm = b.type == BRAIN_WANDER || b.type == BRAIN_SCENARIO;
        // stimuli
        for (const Stimulus& s : ai.stimuli) {
            float d = length(rel(s.pos, p.pos));
            if (d > s.radius || s.source == id) continue;
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
                knockDown(id, vec3(dangerDir * 380.f, 140.f));
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
                case ACT_EVENT: {
                    vec2 to = pa.anchor - pos;
                    float d = length(to);
                    if (d > 0.35f) {
                        desired = to / d * Min(1.4f, d * 2.f + 0.3f);
                        faceYaw = atan2f(-desired.x, desired.y);
                        faceSet = true;
                    } else {
                        faceYaw = pa.anchorYaw;
                        faceSet = true;
                        stance = pa.stance;
                        if (pa.clip >= 0 && p.pendingAction < 0 && p.anim.actionDone()) p.pendingAction = pa.clip;
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
                    if (pa.actTimer <= 0.f && pa.activity != ACT_EVENT && pa.activity != ACT_HAIL_TAXI && pa.activity != ACT_QUEUE) {
                        pa.activity = ACT_WALK;
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
                    // follow the action
                    for (const Stimulus& s : ai.stimuli)
                        if (s.kind == STIM_FIGHT && length(s.pos.toVec3().xy() - pa.threatPos) < 15.f) pa.threatPos = s.pos.toVec3().xy();
                    if (pa.actTimer <= 0.f) pa.activity = ACT_WALK;
                    break;
                }
                case ACT_HANDS_UP: {
                    stance = 5;
                    if (pl) {
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
    if (stance != 6 && pa.activity != ACT_EVENT) p.animIn.crouch = false;
    // squared up in a fist/knife fight: melee.cpp owns the guard / block stances (19, 20)
    bool meleeEngaged = p.meleeTarget >= 0 && p.meleeTarget < (int)peds.size() && peds[p.meleeTarget].used && peds[p.meleeTarget].health > 0.f &&
                        length(rel(peds[p.meleeTarget].pos, p.pos)) < 4.f && (p.animIn.stance == 19 || p.animIn.stance == 20);
    if (!meleeEngaged) p.animIn.stance = stance;
    // face & move
    if (faceSet) turnTo(p, faceYaw, turnRate, dt);
    else if (length2(desired) > 0.04f) turnTo(p, atan2f(-desired.x, desired.y), turnRate, dt);
    movePed(p, desired, dt, false);
    pa.lastPos = pos;
}

}  // namespace Game
