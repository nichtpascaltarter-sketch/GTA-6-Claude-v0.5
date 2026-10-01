// Ambient population around the player: pedestrians on sidewalks, traffic on lanes, parked cars, police patrols
// and wanted-level dispatch. Density depends on district and time of day; spawning happens out of view.
#include "gameworld.h"

namespace Game {

namespace pop_detail {

struct Density {
    float peds, traffic, parked;
};

Density densityFor(World::Region r) {
    switch (r) {
        case World::REG_DOWNTOWN: return {42, 30, 14};
        case World::REG_FINANCIAL: return {36, 28, 10};
        case World::REG_MIDTOWN: return {34, 24, 16};
        case World::REG_NORTH_CITY: return {26, 22, 16};
        case World::REG_CALLE_LUNA: return {34, 20, 18};
        case World::REG_BEACH: return {40, 20, 16};
        case World::REG_BAY_ISLAND: return {6, 6, 8};
        case World::REG_KEY_CORAL: return {12, 10, 10};
        case World::REG_PORT: return {6, 14, 8};
        case World::REG_GROVE: return {16, 14, 14};
        case World::REG_AIRPORT: return {10, 16, 12};
        case World::REG_FLATS: return {16, 18, 14};
        case World::REG_SUBURBS: return {12, 14, 16};
        case World::REG_REDLAND: return {6, 8, 6};
        case World::REG_SAWGRASS: return {1, 5, 1};
        case World::REG_GULF_TOWN: return {10, 6, 6};
        case World::REG_FARMLAND: return {2, 6, 3};
        case World::REG_LAKE_TOWN: return {14, 10, 10};
        case World::REG_HARLOW: return {10, 8, 8};
        case World::REG_RIDGE: return {2, 6, 2};
        case World::REG_FORT_CASTELL: return {14, 12, 12};
        case World::REG_KEYS: return {4, 6, 4};
        case World::REG_KEY_TOWN: return {18, 10, 10};
        default: return {0, 0, 0};
    }
}

float timeFactorPeds(float tod) {
    if (tod < 5.f) return 0.25f;
    if (tod < 7.f) return 0.25f + (tod - 5.f) * 0.35f;
    if (tod < 20.f) return 1.f;
    if (tod < 23.f) return 1.f - (tod - 20.f) * 0.2f;
    return 0.35f;
}
float timeFactorTraffic(float tod) {
    if (tod < 5.f) return 0.35f;
    if (tod < 7.f) return 0.35f + (tod - 5.f) * 0.33f;
    if (tod < 22.f) return 1.f;
    return 0.6f;
}

float gSpawnTimer = 0.f;
float gDispatchTimer = 0.f;
u32 gSpawnCounter = 1;

bool inView(const Render::Camera& cam, vec3 p, float margin) {
    vec3 d = rel(p, cam.pos);
    float l = length(d);
    if (l < 1e-3f) return true;
    float cs = dot(d / l, cam.forward());
    return cs > cosf(cam.fovY * 0.5f * 1.9f + margin);
}

}  // namespace pop_detail

using namespace pop_detail;

void GameWorld::updatePopulation(float dt) {
    Ped* pl = playerPed();
    if (!pl || populationOff) return;
    vec3 pp = pl->pos.toVec3();
    int pv = playerVehicle();
    float speed = pv >= 0 ? vehicles[pv].sim.speed() : 0.f;
    // Streaming center leads the player when driving fast
    vec3 center = pp + (pv >= 0 ? vehicles[pv].sim.body.vel * 2.f : vec3(0));
    World::Region reg = map->regionAt(center.x, center.y);
    Density den = densityFor(reg);
    float tod = env->timeOfDay;
    int wantPeds = (int)(den.peds * timeFactorPeds(tod) * (env->rain > 0.4f ? 0.45f : 1.f) * pedDensityScale);
    int wantTraffic = (int)(den.traffic * timeFactorTraffic(tod) * trafficDensityScale);
    int wantParked = (int)(den.parked * trafficDensityScale);
    int nPeds = 0, nTraffic = 0, nParked = 0;
    const float pedDespawn = 150.f, carDespawn = 340.f;
    for (int i = 0; i < (int)peds.size(); i++) {
        Ped& p = peds[i];
        if (!p.used || p.isPlayer || p.persistent) continue;
        if (p.vehicle >= 0) continue;  // counted with vehicles
        float d = length(p.pos.toVec3() - pp);
        bool dead = p.state == PS_DEAD;
        if (d > pedDespawn || (dead && p.stateTime > 60.f && d > 40.f) || (d > 70.f && !inView(rig.cam, p.pos.toVec3(), 0.1f) && nPeds > wantPeds + 4)) {
            despawnPed(i);
            continue;
        }
        if (!dead) nPeds++;
    }
    for (int i = 0; i < (int)vehicles.size(); i++) {
        Vehicle& v = vehicles[i];
        if (!v.used || v.persistent) continue;
        if (i == pv) continue;
        float d = length(v.sim.body.pos.toVec3() - pp);
        bool occupiedByPlayer = false;
        for (int s = 0; s < 8; s++)
            if (v.seats[s] >= 0 && peds[v.seats[s]].isPlayer) occupiedByPlayer = true;
        if (occupiedByPlayer) continue;
        float limit = v.playerUsed ? 600.f : (v.parked ? 220.f : carDespawn);
        if (d > limit || (v.exploded && v.wreckTime > 90.f && d > 60.f && !inView(rig.cam, v.sim.body.pos.toVec3(), 0.1f))) {
            despawnVehicle(i, true);
            continue;
        }
        if (v.parked) nParked++;
        else if (v.seats[0] >= 0 && peds[v.seats[0]].brain.type == BRAIN_DRIVER) nTraffic++;
    }
    gSpawnTimer -= dt;
    if (gSpawnTimer > 0.f) return;
    gSpawnTimer = 0.12f;
    // ---- spawn one of each category per tick when below target
    std::vector<int> cand;
    auto pickEdge = [&](float rMin, float rMax, bool needSidewalk, float& sOut, int& sideOut, vec3& posOut) -> int {
        u32 h = hash32(gSpawnCounter++ * 2654435761u);
        float ang = hashToFloat(h) * kTwoPi;
        float r = rMin + hashToFloat(hash32(h)) * (rMax - rMin);
        vec2 probe = center.xy() + vec2(cosf(ang), sinf(ang)) * r;
        float s = 0, side = 0;
        int e = roads->nearestEdge(probe, 40.f, &s, nullptr, &side);
        if (e < 0) return -1;
        const World::RoadEdge& ed = roads->edges[e];
        if (needSidewalk && ed.sidewalk <= 0.f) return -1;
        if (s < ed.cut0 + 2.f || s > ed.length - ed.cut1 - 2.f) return -1;
        sOut = s;
        sideOut = side >= 0.f ? 1 : -1;
        vec3 c = ed.posAt(s);
        posOut = c;
        return e;
    };
    // pedestrians
    if (nPeds < wantPeds && !chars.empty()) {
        float s;
        int side;
        vec3 c;
        int e = pickEdge(40.f, 115.f, true, s, side, c);
        if (e >= 0) {
            const World::RoadEdge& ed = roads->edges[e];
            vec3 t = ed.tangentAt(s);
            vec2 n = normalize(vec2(-t.y, t.x));
            vec2 p2 = c.xy() + n * ((ed.halfWidth + ed.sidewalk * 0.55f) * (float)side);
            vec3 sp(p2, 0.f);
            float gz = groundHeight(sp.x, sp.y, c.z + 1.f);
            sp.z = gz;
            vec3 push, nrm;
            bool blocked = Phys::gCollision->capsuleOverlap(sp, 0.3f, 1.7f, push, nrm);
            if (!blocked && !(inView(rig.cam, sp, 0.f) && length(sp - pp) < 70.f)) {
                u32 h = hash32(gSpawnCounter * 7919u);
                int role = 0;
                if (reg == World::REG_BEACH || reg == World::REG_KEY_CORAL || reg == World::REG_KEYS) role = (h % 3 == 0) ? 4 : 0;
                else if (reg == World::REG_FINANCIAL || reg == World::REG_DOWNTOWN) role = (h % 4 == 0) ? 3 : 0;
                else if (reg == World::REG_PORT || reg == World::REG_FLATS || reg == World::REG_FORT_CASTELL) role = (h % 3 == 0) ? 5 : 0;
                if ((reg == World::REG_CALLE_LUNA || reg == World::REG_FLATS) && h % 11 == 0) role = 2;
                int ci = randomCivilianChar(h >> 3, role);
                int id = spawnPed(ci, dvec3(sp), hashToFloat(h) * kTwoPi, role == 2 ? (h & 1 ? FAC_GANG_CUERVOS : FAC_GANG_SAINTS) : FAC_CIVILIAN);
                if (id >= 0) {
                    Ped& p = peds[id];
                    p.brain.type = BRAIN_WANDER;
                    p.brain.edge = e;
                    p.brain.edgeS = s;
                    p.brain.side = (float)side;
                    p.brain.edgeDir = (h >> 5) & 1 ? 1 : -1;
                    p.brain.speed = 1.15f + ((h >> 9) % 40) * 0.01f;
                    p.brain.bravery = hashToFloat(h >> 2);
                    p.brain.aggression = role == 2 ? 0.8f : 0.2f;
                    p.brain.thinkTimer = 4.f + (h % 10);
                    if (role == 2) {
                        giveWeapon(id, (h >> 7) % 3 == 0 ? WPN_SMG : WPN_PISTOL, 60);
                        p.weapon = (h >> 7) % 3 == 0 ? WPN_SMG : WPN_PISTOL;
                        p.brain.accuracy = 0.35f;
                    }
                    // standing scenarios (talking, phone, smoking) for some
                    if ((h >> 12) % 6 == 0) {
                        p.brain.type = BRAIN_SCENARIO;
                        int sc[4] = {7, 8, 0, 7};
                        p.brain.scenario = sc[(h >> 15) & 3];
                        p.brain.sub = (h >> 17) & 1;
                    }
                }
            }
        }
    }
    // traffic
    if (nTraffic < wantTraffic && !vassets.empty()) {
        float s;
        int side;
        vec3 c;
        float rMin = speed > 20.f ? 160.f : 90.f;
        int e = pickEdge(rMin, 260.f, false, s, side, c);
        if (e >= 0) {
            const World::RoadEdge& ed = roads->edges[e];
            bool ok = ed.cls != World::RC_DIRT && !(ed.flags & World::RF_UNPAVED);
            int dir = side;
            if (ed.flags & World::RF_ONEWAY) dir = 1;
            int lanes = dir > 0 ? ed.lanesF : ed.lanesB;
            if (lanes <= 0) ok = false;
            vec3 t = ed.tangentAt(s) * (float)dir;
            const World::RoadClassInfo& info = World::roadInfo(ed.cls);
            u32 h = hash32(gSpawnCounter * 104729u);
            int lane = (int)(h % (u32)Max(lanes, 1));
            float off = info.median * 0.5f + info.laneWidth * (lane + 0.5f);
            if (ed.flags & World::RF_ONEWAY) off = -ed.halfWidth + info.laneWidth * (lane + 0.5f) + info.shoulder;
            vec2 p2 = c.xy() + vec2(t.y, -t.x) * off;
            vec3 sp(p2, c.z + 0.3f);
            if (ok && inView(rig.cam, sp, 0.f) && length(sp - pp) < 150.f) ok = false;
            // keep clear of other vehicles
            if (ok)
                for (auto& o : vehicles)
                    if (o.used && length(o.sim.body.pos.toVec3() - sp) < 9.f) {
                        ok = false;
                        break;
                    }
            if (ok) {
                // model choice by weight + district
                float total = 0.f;
                for (auto& a : vassets) total += trafficWeight(a.spec, reg);
                float pick = hashToFloat(hash32(h)) * total;
                int model = -1;
                for (int i = 0; i < (int)vassets.size(); i++) {
                    pick -= trafficWeight(vassets[i].spec, reg);
                    if (pick <= 0.f) {
                        model = i;
                        break;
                    }
                }
                if (model >= 0) {
                    float yaw = atan2f(-t.x, t.y);
                    bool cop = vassets[model].spec.cls == Vehicles::VC_POLICE;
                    int vid = spawnVehicle(model, dvec3(sp), yaw, true, cop ? FAC_POLICE : FAC_CIVILIAN);
                    if (vid >= 0) {
                        Vehicle& v = vehicles[vid];
                        v.laneEdge = e;
                        v.laneDir = dir;
                        v.laneIndex = lane;
                        v.laneS = s;
                        v.faction = cop ? FAC_POLICE : FAC_CIVILIAN;
                        float cruise = Min(info.speed, v.cruiseSpeed * (ed.cls == World::RC_HIGHWAY ? 2.2f : 1.f));
                        v.sim.body.vel = vec3(t.x, t.y, 0) * (cruise * 0.8f);
                        int drv = v.seats[0];
                        if (drv >= 0) {
                            peds[drv].brain.type = BRAIN_DRIVER;
                            if (cop) {
                                giveWeapon(drv, WPN_PISTOL, 90);
                                peds[drv].weapon = WPN_PISTOL;
                                peds[drv].brain.accuracy = 0.6f;
                            }
                        }
                    }
                }
            }
        }
    }
    // parked cars along curbs
    if (nParked < wantParked && !vassets.empty()) {
        float s;
        int side;
        vec3 c;
        int e = pickEdge(50.f, 170.f, true, s, side, c);
        if (e >= 0) {
            const World::RoadEdge& ed = roads->edges[e];
            const World::RoadClassInfo& info = World::roadInfo(ed.cls);
            if ((ed.cls == World::RC_STREET || ed.cls == World::RC_LANE || ed.cls == World::RC_AVENUE) && info.shoulder > 1.5f) {
                vec3 t = ed.tangentAt(s) * (float)side;
                vec2 p2 = c.xy() + vec2(t.y, -t.x) * (ed.halfWidth - info.shoulder * 0.5f);
                vec3 sp(p2, c.z + 0.3f);
                bool ok = !(inView(rig.cam, sp, 0.f) && length(sp - pp) < 90.f);
                for (auto& o : vehicles)
                    if (ok && o.used && length(o.sim.body.pos.toVec3() - sp) < 7.f) ok = false;
                if (ok) {
                    u32 h = hash32(gSpawnCounter * 15485863u);
                    float total = 0.f;
                    for (auto& a : vassets) total += parkedWeight(a.spec);
                    float pick = hashToFloat(h) * total;
                    for (int i = 0; i < (int)vassets.size(); i++) {
                        pick -= parkedWeight(vassets[i].spec);
                        if (pick <= 0.f) {
                            int vid = spawnVehicle(i, dvec3(sp), atan2f(-t.x, t.y), false);
                            if (vid >= 0) {
                                vehicles[vid].parked = true;
                                vehicles[vid].sim.engineOn = false;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    updateDispatch(dt);
}

float GameWorld::trafficWeight(const Vehicles::VehicleModel& m, World::Region reg) const {
    using namespace Vehicles;
    float w = m.spawnWeight;
    switch (m.cls) {
        case VC_BOAT: case VC_JETSKI: case VC_AIRBOAT: case VC_PLANE: case VC_HELI: return 0.f;
        case VC_AMBULANCE: case VC_FIRETRUCK: return w * 0.1f;
        case VC_POLICE: return w * 0.35f;
        case VC_TAXI: return (reg == World::REG_DOWNTOWN || reg == World::REG_FINANCIAL || reg == World::REG_BEACH || reg == World::REG_MIDTOWN) ? w * 1.5f : w * 0.1f;
        case VC_BUS: return (reg == World::REG_DOWNTOWN || reg == World::REG_MIDTOWN || reg == World::REG_BEACH) ? w * 0.6f : w * 0.1f;
        case VC_TRUCK: case VC_SERVICE: return (reg == World::REG_PORT || reg == World::REG_FLATS || reg == World::REG_FORT_CASTELL) ? w * 2.f : w * 0.6f;
        case VC_PICKUP: return (reg == World::REG_FARMLAND || reg == World::REG_RIDGE || reg == World::REG_HARLOW || reg == World::REG_SAWGRASS) ? w * 3.f : w;
        case VC_SUPER: case VC_SPORTS: return (reg == World::REG_BEACH || reg == World::REG_BAY_ISLAND || reg == World::REG_KEY_CORAL || reg == World::REG_FINANCIAL) ? w * 2.5f : w * 0.6f;
        default: return w;
    }
}

float GameWorld::parkedWeight(const Vehicles::VehicleModel& m) const {
    using namespace Vehicles;
    switch (m.cls) {
        case VC_BOAT: case VC_JETSKI: case VC_AIRBOAT: case VC_PLANE: case VC_HELI: case VC_BUS: case VC_FIRETRUCK: case VC_AMBULANCE: case VC_POLICE: return 0.f;
        default: return m.spawnWeight;
    }
}

// Police response to the wanted level: patrol cars converge on the player, officers engage on foot.
void GameWorld::updateDispatch(float dt) {
    Ped* pl = playerPed();
    if (!pl) return;
    int wanted = pinfo.wanted;
    // assign existing police to the chase
    int active = 0;
    for (int i = 0; i < (int)peds.size(); i++) {
        Ped& p = peds[i];
        if (!p.used || p.faction != FAC_POLICE || p.health <= 0.f) continue;
        if (wanted > 0) {
            if (p.brain.type != BRAIN_COMBAT) {
                p.brain.type = BRAIN_COMBAT;
                p.brain.target = player;
                p.brain.timer = 0.f;
                if (p.weapon == WPN_FISTS) {
                    giveWeapon(i, wanted >= 3 ? WPN_RIFLE : WPN_PISTOL, 120);
                    p.weapon = wanted >= 3 ? WPN_RIFLE : WPN_PISTOL;
                }
            }
            active++;
            if (p.vehicle >= 0 && p.seat == 0) vehicles[p.vehicle].sirenOn = true;
            // officers in cars close to an on-foot player get out
            if (p.state == PS_INVEHICLE && pl->state != PS_INVEHICLE && p.vehicle >= 0) {
                float d = length(rel(pl->pos, p.pos));
                if (d < 28.f && vehicles[p.vehicle].sim.speed() < 3.f) removePedFromVehicle(i, true);
            }
        } else if (p.brain.type == BRAIN_COMBAT && p.brain.target == player) {
            p.brain.type = p.state == PS_INVEHICLE && p.seat == 0 ? BRAIN_DRIVER : BRAIN_WANDER;
            p.brain.target = -1;
            p.brain.edge = -1;
            p.aiming = false;
            if (p.vehicle >= 0) vehicles[p.vehicle].sirenOn = false;
        }
    }
    if (wanted <= 0) return;
    gDispatchTimer -= dt;
    int wantUnits = 2 + wanted * 3;
    if (active >= wantUnits || gDispatchTimer > 0.f) return;
    gDispatchTimer = Max(2.5f, 9.f - wanted * 1.4f);
    int policeModel = findVehicleModel(Vehicles::VC_POLICE, gSpawnCounter++);
    if (policeModel < 0) return;
    // spawn on a road 120-220 m away, out of view
    vec3 pp = pl->pos.toVec3();
    for (int attempt = 0; attempt < 6; attempt++) {
        u32 h = hash32(gSpawnCounter++ * 2246822519u);
        float ang = hashToFloat(h) * kTwoPi;
        float r = 120.f + hashToFloat(hash32(h)) * 100.f;
        vec2 probe = pp.xy() + vec2(cosf(ang), sinf(ang)) * r;
        float s = 0, side = 0;
        int e = roads->nearestEdge(probe, 50.f, &s, nullptr, &side);
        if (e < 0) continue;
        const World::RoadEdge& ed = roads->edges[e];
        vec3 c = ed.posAt(s);
        if (inView(rig.cam, c, 0.f) && length(c - pp) < 160.f) continue;
        vec3 t = ed.tangentAt(s);
        vec2 toP = pp.xy() - c.xy();
        float dir = dot(vec2(t.x, t.y), toP) >= 0.f ? 1.f : -1.f;
        vec3 td = t * dir;
        int vid = spawnVehicle(policeModel, dvec3(c + vec3(td.y, -td.x, 0) * 2.f + vec3(0, 0, 0.3f)), atan2f(-td.x, td.y), true, FAC_POLICE);
        if (vid < 0) return;
        Vehicle& v = vehicles[vid];
        v.faction = FAC_POLICE;
        v.sirenOn = true;
        int drv = v.seats[0];
        if (drv >= 0) {
            giveWeapon(drv, wanted >= 3 ? WPN_RIFLE : WPN_PISTOL, 150);
            peds[drv].weapon = wanted >= 3 ? WPN_RIFLE : WPN_PISTOL;
            peds[drv].brain.type = BRAIN_COMBAT;
            peds[drv].brain.target = player;
            peds[drv].brain.accuracy = 0.45f + wanted * 0.07f;
        }
        // partner
        int ci = randomCivilianChar(h >> 5, 1);
        int partner = spawnPed(ci, v.sim.body.pos, 0.f, FAC_POLICE);
        if (partner >= 0) {
            warpPedIntoVehicle(partner, vid, 1);
            giveWeapon(partner, WPN_PISTOL, 120);
            peds[partner].weapon = WPN_PISTOL;
            peds[partner].brain.type = BRAIN_COMBAT;
            peds[partner].brain.target = player;
            peds[partner].brain.accuracy = 0.45f;
        }
        v.sim.body.vel = td * 12.f;
        break;
    }
}

}  // namespace Game
