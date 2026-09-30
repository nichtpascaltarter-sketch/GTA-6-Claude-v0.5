// Enterable interiors, gameplay side (world/interiors.h holds the plan): streaming (room geometry built on worker jobs,
// GPU models, collision added near the player), render submission (room parts with distance LOD, door leaves, local
// lights on their schedules and animations, ambient volumes + daylight portals for the lighting pass), doors (hinged,
// sliding, roll-up; they open for anyone walking up and their collision follows the leaves), NPCs at the scenario
// points (clerks, cooks, patrons, dancers, cops, nurses...), the player-inside queries used by audio/radio/HUD, and the
// --interiortest walk-through (with --play --autoplay interior).
#include "gameworld.h"

namespace Game {

namespace interiors_game {

using World::InteriorDef;

const float kLoadRange = 480.f;       // camera to the interior's cell (LOD0 cells with the facade cut-outs reach 420 m)
const float kUnloadRange = 600.f;
const float kCollisionRange = 130.f;  // player distance: interior collision in the physics world
const float kNpcRange = 75.f, kNpcDespawn = 110.f;
const float kLightRange = 160.f;      // camera distance: local lights
const float kDetailRange = 55.f, kFurnitureRange = 190.f;

struct DoorState {
    float open = 0.f;        // 0 closed .. 1 open (hinged doors: signed, -1 swings toward -n)
    float target = 0.f;
    float hold = 0.f;        // keeps the door open a moment after people passed
    int colKey = 0;
    bool colAdded = false;
    float colOpen = -9.f;    // open amount the current collider was built for
};

struct Loaded {
    int def = -1;
    int state = 0;           // 1 building on a job, 2 cpu mesh ready, 3 GPU models ready
    std::atomic<int> jobDone{0};
    World::InteriorMesh* mesh = nullptr;
    Render::Model* parts[World::IP_COUNT] = {};
    std::vector<Render::Model*> leaves;
    std::vector<int> leafDoor, leafSide;
    std::vector<World::InteriorLight> lights;
    std::vector<World::CollisionBox> col;
    bool colAdded = false;
    std::vector<DoorState> doors;
    std::vector<int> peds;
    std::vector<u32> pedUid;
    std::vector<int> pedScenario;
    bool npcs = false;
    double lastSeen = 0.0;
};

struct State {
    std::vector<Loaded*> slots;   // by interior def index
    int playerInterior = -1, playerRoom = -1;
    float inside = 0.f;
    bool testInit = false;
    int testDef = -1;
    float testTime = 0.f;
    int testStage = 0;
    vec3 testFrom, testTo;
};
State gIS;

int colKeyFor(int def, int sub) { return -(1000000 + def * 256 + sub); }

float cellDistance(vec2 p, dvec3 cam) {
    float cs = World::kCellSize;
    int cx = (int)floorf((p.x + World::kWorldHalf) / cs), cy = (int)floorf((p.y + World::kWorldHalf) / cs);
    vec2 o = World::cellOrigin(cx, cy);
    float qx = Max(Max(o.x - (float)cam.x, 0.f), (float)cam.x - (o.x + cs));
    float qy = Max(Max(o.y - (float)cam.y, 0.f), (float)cam.y - (o.y + cs));
    return sqrtf(qx * qx + qy * qy);
}

// Lights-on factor for a room schedule at a game hour
float lightsOn(u8 schedule, float hour) {
    switch (schedule) {
        case World::LS_ALWAYS: return 1.f;
        case World::LS_EVENING: return (hour > 17.8f || hour < 0.8f || (hour > 5.8f && hour < 7.4f)) ? 1.f : 0.12f;
        case World::LS_BUSINESS: return (hour > 6.5f && hour < 23.5f) ? 1.f : 0.25f;
        case World::LS_NIGHTLIFE: return (hour > 19.f || hour < 4.5f) ? 1.f : 0.35f;
        default: return 1.f;
    }
}

mat3 frameRot(const InteriorDef& d) { return mat3(vec3(d.ax, 0.f), vec3(d.ay, 0.f), vec3(0, 0, 1)); }

// Door leaf placement in model space: pivot, leaf x axis, vertical scale (roll-up)
void leafPose(const World::InteriorDoor& dr, int side, float open, vec3& pivot, vec2& xdir, float& scaleZ) {
    vec3 base = dr.c + vec3(dr.n * dr.depth, 0.f);
    vec3 t3(dr.t, 0.f);
    scaleZ = 1.f;
    float s = open >= 0.f ? 1.f : -1.f;
    float a = fabsf(open) * 95.f * kDegToRad;
    switch (dr.kind) {
        case World::DK_HINGED:
            pivot = base - t3 * (dr.w * 0.5f);
            xdir = dr.t * cosf(a) + dr.n * (sinf(a) * s);
            break;
        case World::DK_HINGED_PAIR:
            if (side == 0) {
                pivot = base - t3 * (dr.w * 0.5f);
                xdir = dr.t * cosf(a) + dr.n * (sinf(a) * s);
            } else {
                pivot = base + t3 * (dr.w * 0.5f);
                xdir = -dr.t * cosf(a) + dr.n * (sinf(a) * s);
            }
            break;
        case World::DK_ROLLUP:
            pivot = base - t3 * (dr.w * 0.5f) + vec3(0, 0, dr.h);
            xdir = dr.t;
            scaleZ = Max(0.03f, 1.f - fabsf(open));
            break;
        default: {   // sliding pair / elevator
            float slide = fabsf(open) * dr.w * 0.5f * 0.96f;
            if (side == 0) {
                pivot = base - t3 * (dr.w * 0.5f + slide);
                xdir = dr.t;
            } else {
                pivot = base + t3 * (dr.w * 0.5f + slide);
                xdir = -dr.t;
            }
            break;
        }
    }
}

float leafWidth(const World::InteriorDoor& dr) {
    bool pair = dr.kind != World::DK_HINGED && dr.kind != World::DK_ROLLUP;
    return pair ? dr.w * 0.5f : dr.w;
}

void freeLoaded(Loaded* L) {
    for (int i = 0; i < World::IP_COUNT; i++)
        if (L->parts[i]) {
            L->parts[i]->release();
            delete L->parts[i];
            L->parts[i] = nullptr;
        }
    for (Render::Model* m : L->leaves)
        if (m) {
            m->release();
            delete m;
        }
    L->leaves.clear();
    delete L->mesh;
    L->mesh = nullptr;
}

void removeCollision(Loaded* L) {
    if (!Phys::gCollision) return;
    if (L->colAdded) {
        Phys::gCollision->removeCell(colKeyFor(L->def, 0));
        L->colAdded = false;
    }
    for (DoorState& ds : L->doors)
        if (ds.colAdded) {
            Phys::gCollision->removeCell(ds.colKey);
            ds.colAdded = false;
            ds.colOpen = -9.f;
        }
}

void despawnNpcs(GameWorld& g, Loaded* L) {
    for (size_t i = 0; i < L->peds.size(); i++) {
        int id = L->peds[i];
        if (id >= 0 && id < (int)g.peds.size() && g.peds[id].used && g.peds[id].uid == L->pedUid[i] && !g.peds[id].isPlayer) g.despawnPed(id);
    }
    L->peds.clear();
    L->pedUid.clear();
    L->pedScenario.clear();
    L->npcs = false;
}

// Streaming: start jobs for interiors near the camera, upload finished meshes, drop far ones
void stream(Render::Renderer& R, GameWorld* g, dvec3 cam) {
    if (!World::gInteriors) return;
    const auto& defs = World::gInteriors->defs;
    if (gIS.slots.size() != defs.size()) gIS.slots.assign(defs.size(), nullptr);
    double now = TimeSeconds();
    int jobsStarted = 0;
    for (size_t i = 0; i < defs.size(); i++) {
        const InteriorDef& d = defs[i];
        float cd = cellDistance(d.center().xy(), cam);
        Loaded*& L = gIS.slots[i];
        if (!L) {
            if (cd > kLoadRange || jobsStarted >= 2) continue;
            L = new Loaded();
            L->def = (int)i;
            L->mesh = new World::InteriorMesh();
            L->state = 1;
            jobsStarted++;
            Loaded* LL = L;
            int di = (int)i;
            Jobs::submit([LL, di] {
                World::buildInterior(di, *LL->mesh);
                LL->jobDone.store(1);
            }, kJobLow);
            continue;
        }
        if (L->state == 1 && L->jobDone.load()) {
            World::InteriorMesh& m = *L->mesh;
            for (int p = 0; p < World::IP_COUNT; p++) L->parts[p] = R.dynamic->createModel(m.parts[p]);
            for (auto& lf : m.leaves) {
                L->leaves.push_back(R.dynamic->createModel(lf.mesh));
                L->leafDoor.push_back(lf.door);
                L->leafSide.push_back(lf.side);
            }
            L->lights = m.lights;
            L->col = m.col;
            L->doors.assign(d.doors.size(), DoorState());
            for (size_t k = 0; k < L->doors.size(); k++) L->doors[k].colKey = colKeyFor((int)i, 1 + (int)k);
            LOG("Interior '%s' built: %d triangles, %zu lights, %zu colliders, %zu door leaves in %.1f ms", d.name.c_str(), m.triangles, m.lights.size(), m.col.size(),
                m.leaves.size(), m.ms);
            delete L->mesh;
            L->mesh = nullptr;
            L->state = 3;
        }
        if (L->state == 3) L->lastSeen = now;
        if (L->state == 3 && cd > kUnloadRange) {
            if (g) despawnNpcs(*g, L);
            removeCollision(L);
            freeLoaded(L);
            delete L;
            L = nullptr;
        }
    }
}

// Collision near the player (walls, counters, furniture) + door leaves
void updateCollision(Loaded* L, const InteriorDef& d, float playerDist) {
    if (!Phys::gCollision || L->state != 3) return;
    bool want = playerDist < kCollisionRange;
    if (want && !L->colAdded) {
        Phys::gCollision->addCell(colKeyFor(L->def, 0), L->col, {});
        L->colAdded = true;
    } else if (!want && L->colAdded) {
        removeCollision(L);
        return;
    }
    if (!want) return;
    for (size_t k = 0; k < d.doors.size(); k++) {
        const World::InteriorDoor& dr = d.doors[k];
        DoorState& ds = L->doors[k];
        if (ds.colAdded && fabsf(ds.colOpen - ds.open) < 0.06f) continue;
        if (ds.colAdded) Phys::gCollision->removeCell(ds.colKey);
        std::vector<World::CollisionBox> boxes;
        int leaves = (dr.kind == World::DK_HINGED || dr.kind == World::DK_ROLLUP) ? 1 : 2;
        bool rollOpen = dr.kind == World::DK_ROLLUP && fabsf(ds.open) > 0.55f;
        for (int s = 0; s < leaves && !rollOpen; s++) {
            vec3 pivot;
            vec2 xd;
            float sz;
            leafPose(dr, s, ds.open, pivot, xd, sz);
            float lw = leafWidth(dr);
            World::CollisionBox cb;
            vec3 cm = pivot + vec3(xd * (lw * 0.5f), dr.kind == World::DK_ROLLUP ? -dr.h * 0.5f : dr.h * 0.5f);
            cb.c = d.toWorld(cm);
            cb.ax = normalize(d.dirToWorld(xd));
            cb.he = vec3(lw * 0.5f, 0.05f, dr.h * 0.5f);
            boxes.push_back(cb);
        }
        Phys::gCollision->addCell(ds.colKey, boxes, {});
        ds.colAdded = true;
        ds.colOpen = ds.open;
    }
}

// Doors open for anyone walking up (player, NPCs) and close again after a moment
void updateDoors(GameWorld& g, Loaded* L, const InteriorDef& d, float dt) {
    if (L->state != 3) return;
    std::vector<int> nearby;
    for (size_t k = 0; k < d.doors.size(); k++) {
        const World::InteriorDoor& dr = d.doors[k];
        DoorState& ds = L->doors[k];
        vec3 cw = d.toWorld(dr.c + vec3(dr.n * dr.depth, 0.f));
        bool sliding = dr.kind == World::DK_SLIDING_PAIR || dr.kind == World::DK_ELEVATOR;
        bool roll = dr.kind == World::DK_ROLLUP;
        float reach = sliding ? 2.3f : (roll ? 7.f : 1.35f);
        g.pedsNear(cw.xy(), reach + 1.f, nearby);
        float want = 0.f;
        vec2 nW = d.dirToWorld(dr.n), tW = d.dirToWorld(dr.t);
        for (int pi : nearby) {
            const Ped& p = g.peds[pi];
            if (p.state == PS_DEAD || p.state == PS_INVEHICLE) continue;
            vec3 pp = p.pos.toVec3();
            if (fabsf(pp.z - cw.z) > 2.f) continue;
            vec2 rel2 = pp.xy() - cw.xy();
            float along = fabsf(dot(rel2, tW)), across = dot(rel2, nW);
            if (along > dr.w * 0.5f + (sliding ? 0.9f : 0.45f) || fabsf(across) > reach) continue;
            // hinged doors swing away from the person pushing them
            want = (sliding || roll) ? 1.f : (across >= 0.f ? -1.f : 1.f);
            if (!sliding && !roll && fabsf(ds.open) > 0.2f) want = ds.open > 0.f ? 1.f : -1.f;   // keep the current swing
            break;
        }
        if (roll) {
            // roll-up doors also open for the player's vehicle
            int pv = g.playerVehicle();
            if (pv >= 0) {
                vec3 vp = g.vehicles[pv].sim.body.pos.toVec3();
                vec2 rel2 = vp.xy() - cw.xy();
                if (fabsf(dot(rel2, tW)) < dr.w * 0.5f + 2.f && fabsf(dot(rel2, nW)) < 14.f) want = 1.f;
            }
        }
        if (want != 0.f) {
            ds.target = want;
            ds.hold = sliding ? 1.2f : (roll ? 6.f : 1.6f);
        } else {
            ds.hold -= dt;
            if (ds.hold <= 0.f) ds.target = 0.f;
        }
        float speed = sliding ? 3.2f : (roll ? 0.55f : 3.8f);
        float prev = ds.open;
        ds.open = approach(ds.open, ds.target, dt * speed * (sliding || roll ? 1.f : (0.35f + 0.65f * (1.f - fabsf(ds.open)))));
#ifdef HAVE_AUDIO
        // shop door chime / hinge creak on opening, latch on closing
        if (fabsf(prev) < 0.01f && fabsf(ds.open) >= 0.01f) {
            if (sliding) Audio::play(Audio::SFX_DOOR_BUZZ, cw + vec3(0, 0, 2.f), 0.18f, 1.6f);
            else if (!roll) Audio::play(Audio::SFX_CAR_DOOR_OPEN, cw + vec3(0, 0, 1.f), 0.35f, 0.72f);
        } else if (!sliding && !roll && fabsf(prev) > 0.02f && fabsf(ds.open) <= 0.02f) {
            Audio::play(Audio::SFX_CAR_DOOR_CLOSE, cw + vec3(0, 0, 1.f), 0.4f, 0.7f);
        }
#endif
        (void)prev;
    }
}

u8 roleToCharRole(u8 role, u8 kind) {
    switch (role) {
        case World::SR_COP: return 1;
        case World::SR_NURSE: case World::SR_DOCTOR: case World::SR_PATIENT: return role == World::SR_PATIENT ? 0 : 6;
        case World::SR_MECHANIC: case World::SR_WORKER: return 5;
        case World::SR_GUARD: case World::SR_BOUNCER: case World::SR_VIP: return 3;
        case World::SR_DANCER: case World::SR_PATRON: return kind == World::IK_CLUB ? 4 : 0;
        default: return 0;
    }
}

bool isNight(float hour) { return hour > 19.5f || hour < 5.5f; }

void updateNpcs(GameWorld& g, Loaded* L, const InteriorDef& d, float playerDist, float hour) {
    if (L->state != 3) return;
    bool want = playerDist < kNpcRange && !g.missionActive();
    if (L->npcs && (playerDist > kNpcDespawn || g.missionActive())) {
        despawnNpcs(g, L);
        return;
    }
    if (!want || L->npcs) return;
    L->npcs = true;
    u32 daySeed = (u32)g.gameDay * 7919u + (u32)(hour / 3.f);
    for (size_t i = 0; i < d.scenarios.size(); i++) {
        const World::InteriorScenario& sc = d.scenarios[i];
        bool night = isNight(hour);
        if ((sc.flags & World::SF_NIGHT) && !night) continue;
        if ((sc.flags & World::SF_DAY) && night) continue;
        u32 h = hash32(d.seed ^ (u32)i * 0x9E3779B9u ^ daySeed);
        if ((sc.flags & World::SF_OPTIONAL) && hashToFloat(h) > 0.6f) continue;
        if (!(sc.flags & World::SF_STAFF) && (d.rooms.empty() ? 1.f : lightsOn(d.rooms[0].schedule, hour)) < 0.5f && hashToFloat(h >> 3) > 0.25f) continue;
        int ci = g.randomCivilianChar(h, roleToCharRole(sc.role, d.kind));
        if (ci < 0) continue;
        vec3 wp = d.toWorld(sc.pos);
        wp.z += sc.lift;
        Faction f = sc.role == World::SR_COP ? FAC_POLICE : FAC_CIVILIAN;
        int id = g.spawnPed(ci, dvec3(wp), d.yawToWorld(sc.yaw), f);
        if (id < 0) continue;
        Ped& p = g.peds[id];
        p.persistent = true;
        p.brain = Brain();
        p.brain.type = BRAIN_SCENARIO;
        p.brain.scenario = sc.stance;
        p.brain.sub = 0;
        p.animIn.stance = sc.stance;
        L->peds.push_back(id);
        L->pedUid.push_back(p.uid);
        L->pedScenario.push_back((int)i);
    }
}

// Keep scenario peds on their spots (seats, stools, behind counters) while they are calm
void pinNpcs(GameWorld& g, Loaded* L, const InteriorDef& d) {
    for (size_t i = 0; i < L->peds.size(); i++) {
        int id = L->peds[i];
        if (id < 0 || id >= (int)g.peds.size()) continue;
        Ped& p = g.peds[id];
        if (!p.used || p.uid != L->pedUid[i] || p.brain.type != BRAIN_SCENARIO || p.state != PS_ONFOOT) continue;
        const World::InteriorScenario& sc = d.scenarios[L->pedScenario[i]];
        vec3 wp = d.toWorld(sc.pos);
        p.pos = dvec3(wp.x, wp.y, wp.z + sc.lift);
        p.vel = vec3(0.f);
        p.yaw = d.yawToWorld(sc.yaw);
        p.grounded = true;
        p.animIn.stance = sc.stance;
    }
}

// Render submission: room parts (distance LOD), door leaves, lights, ambient volumes and daylight portals
// Display vehicle on an interior marker (car lifts, stands): a real vehicle model, parked, no physics
void submitDisplayCar(Render::Renderer& R, const InteriorDef& d, const World::InteriorMarker& mk, u32 key, float dist) {
#ifdef HAVE_VEHICLE_MODELS
    if (!gGame || gGame->vassets.empty()) return;
    const std::vector<VehicleAsset>& va = gGame->vassets;
    bool stripped = mk.kind == World::IM_CAR_STRIPPED;
    u32 h = hash32(d.seed ^ (key * 0x9E3779B9u) ^ 0xCA7u);
    static const Vehicles::VehicleClass kCls[] = {Vehicles::VC_MUSCLE, Vehicles::VC_SPORTS, Vehicles::VC_SEDAN, Vehicles::VC_COUPE, Vehicles::VC_COMPACT};
    int mi = -1;
    for (int t = 0; t < 5 && mi < 0; t++) {
        mi = Vehicles::findModel(kCls[(h + (u32)t) % 5u], (int)((h >> 8) % 3u));
        if (mi < 0) mi = Vehicles::findModel(kCls[(h + (u32)t) % 5u], 0);
    }
    if (mi < 0 || mi >= (int)va.size() || !va[mi].body) return;
    const VehicleAsset& a = va[mi];
    vec3 col = a.spec.paletteColors.empty() ? vec3(0.4f) : a.spec.paletteColors[(h >> 12) % a.spec.paletteColors.size()];
    if (stripped) col *= 0.7f;
    mat3 rot = mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), d.yawToWorld(mk.yaw)));
    dvec3 pos(d.toWorld(mk.pos));
    Render::DrawItem di;
    di.model = a.body;
    di.pos = pos;
    di.rot = rot;
    di.tint0 = vec4(col, stripped ? 0.7f : 0.1f);
    di.tint1 = vec4(col * 0.25f, 0.f);
    di.id = 0x300000000ull | ((u64)key << 4);
    di.castShadow = dist < 70.f;
    di.drawGlass = !stripped;
    di.wetExposed = -1.f;
    R.dynamic->submit(di);
    if (stripped || !a.wheel) return;
    for (size_t w = 0; w < a.spec.wheels.size(); w++) {
        Render::DrawItem wd;
        wd.model = a.wheel;
        wd.pos = pos + dvec3(rot * (a.spec.wheels[w].pos - vec3(0.f, 0.f, 0.04f)));
        mat3 wr = rot;
        if (a.spec.wheels[w].left) wr = wr * mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), kPi));
        wd.rot = wr;
        wd.tint0 = di.tint0;
        wd.tint1 = di.tint1;
        wd.id = 0x300000000ull | ((u64)key << 4) | (u64)(w + 1);
        wd.castShadow = dist < 40.f;
        wd.wetExposed = -1.f;
        R.dynamic->submit(wd);
    }
#else
    (void)R, (void)d, (void)mk, (void)key, (void)dist;
#endif
}

void submitAll(Render::Renderer& R, dvec3 cam, float hour, float gameSeconds) {
    if (!World::gInteriors) return;
    const auto& defs = World::gInteriors->defs;
    struct VolReq {
        float dist;
        int def;
    };
    std::vector<VolReq> vols;
    for (size_t i = 0; i < gIS.slots.size(); i++) {
        Loaded* L = gIS.slots[i];
        if (!L || L->state != 3) continue;
        const InteriorDef& d = defs[i];
        float dist = length(rel(dvec3(d.center()), cam));
        mat3 rot = frameRot(d);
        dvec3 org(d.origin);
        for (int p = 0; p < World::IP_COUNT; p++) {
            if (!L->parts[p] || !L->parts[p]->indexCount) continue;
            if (p == World::IP_DETAIL && dist > kDetailRange + d.radius) continue;
            if (p == World::IP_FURNITURE && dist > kFurnitureRange) continue;
            Render::DrawItem di;
            di.model = L->parts[p];
            di.pos = org;
            di.rot = rot;
            di.wetExposed = d.kind == World::IK_CLUB ? 1.f : -1.f;   // open-air decks get wet in the rain
            di.castShadow = p == World::IP_SHELL ? dist < 200.f : dist < 45.f + d.radius;
            R.dynamic->submit(di);
        }
        for (size_t k = 0; k < L->leaves.size(); k++) {
            if (!L->leaves[k] || dist > kFurnitureRange) continue;
            int di2 = L->leafDoor[k];
            const World::InteriorDoor& dr = d.doors[di2];
            vec3 pivot;
            vec2 xd;
            float sz;
            leafPose(dr, L->leafSide[k], L->doors[di2].open, pivot, xd, sz);
            vec2 xw = normalize(d.dirToWorld(xd));
            Render::DrawItem it;
            it.model = L->leaves[k];
            it.pos = dvec3(d.toWorld(pivot));
            it.rot = mat3(vec3(xw, 0.f), vec3(perp(xw), 0.f), vec3(0, 0, 1));
            it.scale = vec3(1.f, 1.f, sz);
            it.wetExposed = -1.f;
            it.castShadow = dist < 60.f;
            R.dynamic->submit(it);
        }
        if (dist < kFurnitureRange)
            for (size_t m = 0; m < d.markers.size(); m++)
                if (d.markers[m].kind == World::IM_CAR || d.markers[m].kind == World::IM_CAR_STRIPPED) submitDisplayCar(R, d, d.markers[m], (u32)(i * 16 + m), dist);
        // local lights
        if (dist < kLightRange + d.radius) {
            for (const World::InteriorLight& li : L->lights) {
                float on = li.room < d.rooms.size() ? lightsOn(d.rooms[li.room].schedule, hour) : lightsOn(World::LS_EVENING, hour);
                if (on < 0.3f && li.anim != 4) continue;
                Render::DynamicLight dl;
                vec3 dir = li.dir;
                float k = on;
                switch (li.anim) {
                    case 1: k *= (hash32((u32)(gameSeconds * 12.f) + li.phase) % 23u) == 0 ? 0.25f : 1.f; break;   // flicker
                    case 2: {   // club sweep: rotating moving-head
                        float a = gameSeconds * (0.7f + (li.phase % 5) * 0.13f) + li.phase * 0.7f;
                        dir = normalize(vec3(cosf(a) * 0.55f, sinf(a * 1.3f) * 0.55f, -1.f));
                        break;
                    }
                    case 3: k *= fmodf(gameSeconds * 3.f + li.phase * 0.37f, 1.f) < 0.12f ? 1.6f : 0.f; break;   // strobe
                    case 4: k = 0.6f + 0.4f * sinf(gameSeconds * 7.f + li.phase) * sinf(gameSeconds * 3.1f + li.phase * 2.f); break;   // TV
                    case 5: k *= 0.75f + 0.25f * sinf(gameSeconds * 2.f + li.phase); break;   // neon pulse
                    case 6: k *= 0.8f + 0.2f * sinf(gameSeconds * 11.f + li.phase) * sinf(gameSeconds * 4.3f); break;   // fire
                    default: break;
                }
                if (k <= 0.01f) continue;
                vec3 color = li.color;
                if (li.anim == 2) color = hsvToRgb(fmodf(gameSeconds * 0.11f + li.phase * 0.17f, 1.f), 0.8f, 1.f) * (li.color.x + li.color.y + li.color.z) * 0.45f;
                dl.pos = dvec3(d.toWorld(li.pos));
                dl.color = color * k;
                dl.radius = li.radius;
                if (li.cosOuter > -1.5f) {
                    dl.dir = normalize(vec3(d.dirToWorld(dir.xy()), dir.z));
                    dl.spotCos = li.cosOuter;
                    dl.spotInner = li.cosInner;
                } else {
                    dl.dir = vec3(0, 0, -1);
                }
                R.addLight(dl);
            }
        }
        if (dist < 420.f) vols.push_back({dist, (int)i});
    }
    // ambient volumes + portals, nearest interiors first (renderer caps the count)
    std::sort(vols.begin(), vols.end(), [](const VolReq& a, const VolReq& b) { return a.dist < b.dist; });
    for (const VolReq& vr : vols) {
        const InteriorDef& d = defs[vr.def];
        Loaded* L = gIS.slots[vr.def];
        for (size_t ri = 0; ri < d.rooms.size(); ri++) {
            const World::InteriorRoom& rm = d.rooms[ri];
            if (rm.outdoor) continue;
            if ((int)R.interiorVolumes.size() >= Render::Renderer::kMaxInteriorVolumes) break;
            Render::InteriorVolume v;
            vec3 c = (rm.mn + rm.mx) * 0.5f;
            v.center = dvec3(d.toWorld(c));
            v.axis = d.ax;
            v.halfExtents = (rm.mx - rm.mn) * 0.5f;
            v.ambient = rm.lampAmbient * lightsOn(rm.schedule, hour);
            v.skyBounce = rm.dayBounce;
            v.firstPortal = (int)R.interiorPortals.size();
            for (const World::InteriorPortal& pt : d.portals) {
                if (pt.room != (int)ri) continue;
                Render::InteriorPortal rp;
                rp.corner = dvec3(d.toWorld(pt.p0));
                rp.edgeU = vec3(d.dirToWorld(pt.u.xy()), pt.u.z);
                rp.edgeV = vec3(d.dirToWorld(pt.v.xy()), pt.v.z);
                float t = pt.transmission;
                if (pt.door >= 0 && L && pt.door < (int)L->doors.size()) {
                    const World::InteriorDoor& dr = d.doors[pt.door];
                    float closedT = dr.style == 1 ? 0.8f : 0.04f;
                    t = Lerp(closedT, 1.f, Saturate(fabsf(L->doors[pt.door].open)));
                }
                rp.transmission = t;
                R.interiorPortals.push_back(rp);
            }
            v.portalCount = (int)R.interiorPortals.size() - v.firstPortal;
            R.interiorVolumes.push_back(v);
        }
    }
}

// --interiortest <name|index>: walk the player from the street through the door into the interior, looking around
void testDrive(GameWorld& g, float dt) {
    const char* arg = Platform::argValue("interiortest");
    if (!arg || !World::gInteriors || World::gInteriors->defs.empty()) return;
    Ped* pl = g.playerPed();
    if (!pl) return;
    const auto& defs = World::gInteriors->defs;
    if (!gIS.testInit) {
        gIS.testInit = true;
        int idx = World::gInteriors->byName(arg);
        if (idx < 0 && arg[0] >= '0' && arg[0] <= '9') idx = atoi(arg);
        if (idx < 0 || idx >= (int)defs.size()) idx = 0;
        gIS.testDef = idx;
        const InteriorDef& d = defs[idx];
        const World::InteriorMarker* out = d.marker(World::IM_DOOR_OUT);
        vec3 start = out ? d.toWorld(out->pos) : d.toWorld(vec3(0.f, -3.f, 0.f));
        vec3 dirIn = vec3(d.ay, 0.f);
        start = start - dirIn * 4.f;
        pl->pos = dvec3(start.x, start.y, g.groundHeight(start.x, start.y, start.z + 3.f));
        pl->yaw = atan2f(-dirIn.x, dirIn.y);
        pl->vel = vec3(0.f);
        g.rig.yaw = pl->yaw;
        g.rig.cut = true;
        g.populationOff = true;
        gIS.testTime = 0.f;
        LOG("interiortest: '%s' (kind %d), player at %.1f %.1f %.1f", d.name.c_str(), (int)d.kind, start.x, start.y, start.z);
    }
    const InteriorDef& d = defs[gIS.testDef];
    gIS.testTime += dt;
    float t = gIS.testTime;
    Controls& c = g.ctl;
    // walk in through the door (7 s), wander toward the middle of the first room, then look around slowly
    vec3 target = d.rooms.empty() ? d.center() : d.toWorld(vec3((d.rooms[0].mn.x + d.rooms[0].mx.x) * 0.5f, d.rooms[0].mn.y + (d.rooms[0].mx.y - d.rooms[0].mn.y) * 0.4f, 0.f));
    const World::InteriorMarker* entry = d.marker(World::IM_ENTRY);
    vec3 door = entry ? d.toWorld(entry->pos) : d.toWorld(vec3(0.f, 1.f, 0.f));
    vec3 goal = t < 6.f ? door : target;
    vec3 pp = pl->pos.toVec3();
    vec2 to = goal.xy() - pp.xy();
    if (t < 12.f && length(to) > 0.6f) {
        float want = atan2f(-to.x, to.y);
        float dy = wrapAngle(want - g.rig.yaw);
        c.look = vec2(-Clamp(dy, -0.05f, 0.05f), 0.f);
        c.move = vec2(0.f, 0.8f);
    } else {
        c.look = vec2(0.006f, t > 20.f ? 0.0006f : 0.f);
    }
}

}  // namespace interiors_game

// ------------------------------------------------------------------------------------------------------------------
// Public API
namespace Interiors {

using namespace interiors_game;

// Before the player update: the --interiortest walk-through drives the controls
void preUpdate(GameWorld& g, float dt) { testDrive(g, dt); }

// After the peds update: streaming, collision, doors, NPCs, player inside detection
void update(GameWorld& g, float dt) {
    if (!World::gInteriors || !g.renderer) return;
    dvec3 cam = g.rig.cam.pos;
    stream(*g.renderer, &g, cam);
    Ped* pl = g.playerPed();
    vec3 pp = pl ? pl->pos.toVec3() : cam.toVec3();
    float hour = g.env ? g.env->timeOfDay : 12.f;
    const auto& defs = World::gInteriors->defs;
    for (size_t i = 0; i < gIS.slots.size(); i++) {
        Loaded* L = gIS.slots[i];
        if (!L || L->state != 3) continue;
        const InteriorDef& d = defs[i];
        float pd = length(pp.xy() - d.center().xy());
        updateCollision(L, d, pd);
        if (pd < kCollisionRange) updateDoors(g, L, d, dt);
        updateNpcs(g, L, d, pd, hour);
        if (L->npcs) pinNpcs(g, L, d);
    }
    int room = -1;
    gIS.playerInterior = pl && pl->state != PS_INVEHICLE ? World::gInteriors->at(pp + vec3(0, 0, 0.9f), &room) : -1;
    gIS.playerRoom = room;
    gIS.inside = approach(gIS.inside, gIS.playerInterior >= 0 ? 1.f : 0.f, dt * 2.5f);
}

// Render submission in gameplay (GameWorld::submitRender)
void submit(GameWorld& g) {
    if (!g.renderer || !g.env) return;
    submitAll(*g.renderer, g.rig.cam.pos, g.env->timeOfDay, g.env->gameSeconds);
}

// Free camera without a game (screenshots, --shot): streaming + render submission
void submitFreecam(Render::Renderer& R, const Render::Camera& cam, const Render::Environment& env) {
    stream(R, nullptr, cam.pos);
    submitAll(R, cam.pos, env.timeOfDay, env.gameSeconds);
}

bool isInside() { return gIS.playerInterior >= 0; }
int currentInterior() { return gIS.playerInterior; }
int currentRoom() { return gIS.playerRoom; }
int currentKind() { return gIS.playerInterior >= 0 && World::gInteriors ? (int)World::gInteriors->defs[gIS.playerInterior].kind : -1; }
const char* currentName() { return gIS.playerInterior >= 0 && World::gInteriors ? World::gInteriors->defs[gIS.playerInterior].name.c_str() : ""; }
// 0 outdoors .. 1 inside (smoothed over ~0.4 s): audio reverb / radio muffling
float insideAmount() { return gIS.inside; }

}  // namespace Interiors

}  // namespace Game
