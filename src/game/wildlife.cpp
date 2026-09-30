// Wildlife simulation: the animals living around the player. Groups (flocks, pods, herds, packs, shoals) spawn out of
// view by region, coast, water depth and time of day, run species behaviours (boids flocking with obstacle and ground
// avoidance, perching and landing, wading and striking, soaring in thermals, basking and lurking alligators, dolphin
// breathing arcs and bow riding, shoaling fish, dogs walked on leashes by pedestrians, strays, cats, raccoons, grazing
// herds), react to the player, vehicles, gunfire and explosions, can be run over or shot, play their sounds at their
// positions and are submitted to the dynamic renderer: individual skinned draws up close, and flocks / shoals as
// batched skinned meshes (many animals per draw) further away. Models and animation: animal_models.cpp.
#include "gameworld.h"
#include "wildlife.h"

namespace Game {

namespace Wildlife {
void update(GameWorld& g, float dt);
void submitRender(GameWorld& g);
// Bullet test (combat.cpp, per pellet): returns true if an animal was hit before any other obstacle (the pellet stops).
bool bulletHit(GameWorld& g, int shooter, dvec3 from, vec3 dir, float range, float damage);
int liveCount();
}  // namespace Wildlife

namespace wild_detail {

using namespace Fauna;

// ------------------------------------------------------------------------------------------------------------------
enum GroupType : u8 {
    GT_GULLS = 0, GT_PELICANS, GT_PIGEONS, GT_WADERS, GT_VULTURES, GT_PARROTS,
    GT_GATOR, GT_IGUANA, GT_DOLPHINS, GT_MANATEE, GT_TURTLE, GT_FISH,
    GT_DOG_LEASH, GT_DOG_STRAY, GT_CAT, GT_RACCOON, GT_DEER, GT_CATTLE, GT_HORSES,
    GT_SHOREBIRDS, GT_GRACKLES, GT_FRIGATES, GT_CORMORANTS, GT_CEGRETS,
    GT_COUNT
};

enum AnimalState : u8 {
    ST_IDLE = 0,    // standing / perched / floating still / basking
    ST_WALK,        // moving on the ground
    ST_FLY,         // flying (flock boids, path, soaring)
    ST_TAKEOFF,
    ST_LAND,        // approaching a landing spot
    ST_SWIM,        // on or in the water
    ST_DIVE,        // plunge dive (birds), breathing arc / leap (dolphins)
    ST_FLEE,
    ST_ATTACK,      // lunge, chase, bite, drag
    ST_REST,        // sitting, lying
    ST_FEED,        // grazing, pecking, probing, striking prey
    ST_ALERT,       // head up, watching a threat
    ST_FALL,        // shot out of the sky / thrown by a vehicle
    ST_DEAD
};

struct Animal {
    bool used = false;
    u8 sp = 0, var = 0, state = ST_IDLE;
    int group = -1;
    int slot = 0;             // batch slot within the group (stable while alive)
    u32 uid = 0;
    vec3 pos, vel;
    float yaw = 0.f, pitch = 0.f, roll = 0.f;
    float scale = 1.f;
    float health = 10.f;
    float t = 0.f;            // time in the current state
    float timer = 0.f;        // generic countdown
    float think = 0.f;        // decision timer
    float life = 0.f;
    vec3 goal;
    int perch = -1;
    float groundZ = 0.f, groundT = 0.f;
    vec3 groundN = vec3(0, 0, 1);
    bool inWater = false;
    float waterZ = -1000.f;
    float speedWant = 0.f;
    float standH = 0.f;       // height of the model origin above the ground when standing (birds: body centre)
    float cooldown = 0.f, soundT = 0.f;
    int target = -1;          // ped (bite / chase / owner)
    u32 targetUid = 0;
    float aggression = 0.f;   // strays, gators
    vec3 spin;                // tumbling after death
    float deadT = 0.f;
    bool drowned = false;
    float accDt = 0.f;        // throttled updates for distant animals
    float aux = 0.f, aux2 = 0.f;   // species extras: cormorant wing drying / dive time, cattle egret riding / hop flight
    float visDist = 1e9f;
    int poseFrame = -1000;    // frame of the cached pose (distant animals re-pose at a lower rate)
    mat4 skin[kMaxBones];     // cached skinning matrices (model space)
    // animation inputs (by body plan)
    BirdAnim ba;
    QuadAnim qa;
    ReptileAnim ra;
    SwimAnim sa;
};

struct Perch {
    vec3 pos;
    u8 kind;                  // 0 ground/sand, 1 post top (one bird), 2 water, 3 railing / roof edge, 4 tree canopy
    int taken = -1;
};

struct Group {
    bool used = false;
    u8 type = 0;
    u8 sp = 0;
    u32 uid = 0;
    vec3 anchor;              // home
    vec3 target;              // current flock goal / thermal centre / path waypoint
    vec3 center;              // centroid (render batch origin)
    float radius = 40.f;
    float height = 15.f;      // cruising height above the ground / water
    u8 mode = 0;
    float modeT = 0.f, timer = 0.f;
    float alarm = 0.f;        // > 0 while fleeing a scare
    vec3 threat;
    std::vector<int> members;
    std::vector<Perch> perches;
    int owner = -1;           // leashed dog: the pedestrian walking it
    u32 ownerUid = 0;
    int leashHand = 0;        // 0 left, 1 right
    float soundT = 0.f;
    int variant = 0;
    float heading = 0.f;
    float ageOut = 0.f;       // time spent far / unseen (despawn hysteresis)
    int boat = -1;            // dolphins: vehicle being bow-ridden
    float boatT = 0.f;
    int attackVictim = -1;    // stray dog attacking a pedestrian
    u32 attackVictimUid = 0;
    float attackT = 0.f;
    vec3 hand;                // leashed dog: the owner's hand holding the leash (world)
    bool hasHand = false;
    float accDt = 0.f;        // distant groups update at a lower rate
    bool frozen = false;      // test line-ups: animation only, no behaviour
    vec2 shoreN = vec2(0.f, 1.f);   // sanderlings: unit normal pointing up the beach at the anchor (on the water's edge)
    int host = -1;            // cattle egrets: the cattle herd they follow
    u32 hostUid = 0;
};

struct Threat {
    vec3 pos, vel;
    float radius;             // scare radius
    u8 kind;                  // 0 person on foot, 1 vehicle, 2 gunfire, 3 explosion
    bool player;
    int ped = -1, veh = -1;
    float speed = 0.f;
};

struct SpeciesAssets {
    ModelData md[kMaxVariants];
    Render::Model* lod[kMaxVariants][2] = {};
    Render::Model* batch[kMaxVariants][2] = {};
    int variants = 0;
};

struct WildState {
    SpeciesAssets sp[SP_COUNT];
    bool assetsReady = false, assetsStarted = false;
    JobCounter jobs;
    std::vector<Animal> animals;
    std::vector<Group> groups;
    u32 nextUid = 1;
    float spawnTimer = 0.f;
    int spawnCursor = 0;
    Rng rng = Rng(0xF4A7A11ull, 0x51u);
    double time = 0.0;
    // frame context
    vec3 cam, camFwd, player, playerVel;
    bool playerOnFoot = false, playerSwim = false, playerAiming = false, playerRunning = false;
    int playerVeh = -1;
    float tod = 12.f, rain = 0.f;
    std::vector<Threat> threats;
    std::vector<int> pedsNear;          // peds within 150 m of the camera (this frame)
    std::vector<int> vehNear;           // moving vehicles within 200 m
    float lastStimTime = -1e9f;
    std::vector<mat4> arena;            // bone matrices submitted this frame
    // statistics
    float msUpdate = 0.f, msRender = 0.f;
    int drawn = 0, drawCalls = 0;
    float logT = 0.f;
    int frame = 0;
    bool spawning = true;
    // test automation (--wildlifetest)
    int testScene = -1, testPhase = 0, testFrame = 0;
};
WildState gW;

// ------------------------------------------------------------------------------------------------------------------
// Small helpers
inline float frand() { return gW.rng.f(); }
inline float frange(float a, float b) { return gW.rng.range(a, b); }
inline float wrapA(float a) { return wrapAngle(a); }
inline float yawOf(vec2 d) { return atan2f(-d.x, d.y); }
inline vec2 dirOf(float yaw) { return vec2(-sinf(yaw), cosf(yaw)); }
inline float approachAngle(float cur, float target, float maxStep) {
    float d = wrapA(target - cur);
    return wrapA(cur + Clamp(d, -maxStep, maxStep));
}
inline bool isBird(int sp) { return speciesInfo(sp).plan == PLAN_BIRD; }
inline bool isWaderSp(int sp) { return sp == SP_HERON || sp == SP_EGRET || sp == SP_SPOONBILL || sp == SP_FLAMINGO || sp == SP_IBIS; }

void sfx(Audio::Sfx id, vec3 pos, float vol = 1.f, float pitch = 1.f) {
#ifdef HAVE_AUDIO
    Audio::play(id, pos, vol, pitch * (0.94f + 0.12f * frand()));
#else
    (void)id; (void)pos; (void)vol; (void)pitch;
#endif
}

// Water surface (with waves) or kNoWater-ish
inline bool waterAt(float x, float y, float& z) { return Phys::waterSurface(x, y, z); }
inline float waterDepth(const World::WorldMap& m, float x, float y) {
    float w = m.waterAt(x, y);
    if (w <= World::kNoWater + 1.f) return -100.f;
    return w - m.heightAt(x, y);
}

// Ground under a point: collision world (roofs, decks, props) near the player, terrain elsewhere.
float groundAt(const GameWorld& g, float x, float y, float zRef, vec3* nrm = nullptr) {
    float d2 = length2(vec2(x, y) - gW.player.xy());
    if (d2 < 380.f * 380.f && Phys::gCollision) {
        Phys::GroundHit h = Phys::gCollision->ground(x, y, zRef, 0.6f);
        if (nrm) *nrm = h.normal;
        return h.z;
    }
    if (nrm) *nrm = g.map->normalAt(x, y);
    return g.map->heightAt(x, y);
}

bool onRoadOrBuilding(const GameWorld& g, vec2 p, float roadMargin, float bldMargin) {
    if (g.roads && g.roads->nearRoad(p, roadMargin)) return true;
    if (g.buildings && g.buildings->pointInBuilding(p, bldMargin)) return true;
    return false;
}

inline bool urbanRegion(World::Region r) {
    switch (r) {
        case World::REG_DOWNTOWN: case World::REG_FINANCIAL: case World::REG_MIDTOWN: case World::REG_NORTH_CITY: case World::REG_CALLE_LUNA:
        case World::REG_BEACH: case World::REG_PORT: case World::REG_AIRPORT: case World::REG_FLATS: case World::REG_FORT_CASTELL:
        case World::REG_KEY_TOWN:
            return true;
        default: return false;
    }
}
inline bool suburbRegion(World::Region r) {
    switch (r) {
        case World::REG_GROVE: case World::REG_SUBURBS: case World::REG_KEY_CORAL: case World::REG_BAY_ISLAND: case World::REG_LAKE_TOWN:
        case World::REG_HARLOW: case World::REG_GULF_TOWN:
            return true;
        default: return false;
    }
}
inline bool wildRegion(World::Region r) {
    switch (r) {
        case World::REG_SAWGRASS: case World::REG_FARMLAND: case World::REG_RIDGE: case World::REG_REDLAND: case World::REG_KEYS:
            return true;
        default: return false;
    }
}

// Model-space rotation of an animal (yaw about Z, pitch about X, roll about Y)
inline quat animalQuat(const Animal& a) { return quatAxisAngle(vec3(0, 0, 1), a.yaw) * quatAxisAngle(vec3(1, 0, 0), a.pitch) * quatAxisAngle(vec3(0, 1, 0), a.roll); }

const ModelData& modelOf(const Animal& a) { return gW.sp[a.sp].md[Min((int)a.var, Max(gW.sp[a.sp].variants - 1, 0))]; }

// Scale from model space to the world, including the species length variation baked per animal
inline vec3 worldPoint(const Animal& a, vec3 local) { return a.pos + rotate(animalQuat(a), local * a.scale); }

int allocAnimal() {
    for (int i = 0; i < (int)gW.animals.size(); i++)
        if (!gW.animals[i].used) return i;
    if (gW.animals.size() >= 900) return -1;
    gW.animals.emplace_back();
    return (int)gW.animals.size() - 1;
}

int allocGroup() {
    for (int i = 0; i < (int)gW.groups.size(); i++)
        if (!gW.groups[i].used) return i;
    if (gW.groups.size() >= 96) return -1;
    gW.groups.emplace_back();
    return (int)gW.groups.size() - 1;
}

int newGroup(GroupType t, int sp, vec3 anchor) {
    int gi = allocGroup();
    if (gi < 0) return -1;
    Group& G = gW.groups[gi];
    G = Group();
    G.used = true;
    G.type = t;
    G.sp = (u8)sp;
    G.uid = gW.nextUid++;
    G.anchor = G.target = G.center = anchor;
    G.heading = frand() * kTwoPi;
    G.soundT = frange(1.f, 6.f);
    return gi;
}

int newAnimal(int gi, int sp, int var, vec3 pos, float yaw) {
    int ai = allocAnimal();
    if (ai < 0) return -1;
    Animal& a = gW.animals[ai];
    a = Animal();
    a.used = true;
    a.sp = (u8)sp;
    a.var = (u8)Clamp(var, 0, Max(speciesInfo(sp).variants - 1, 0));
    a.uid = gW.nextUid++;
    a.group = gi;
    a.pos = pos;
    a.yaw = yaw;
    a.health = speciesInfo(sp).hp;
    a.scale = 1.f;
    a.think = frand() * 0.5f;
    a.soundT = frange(2.f, 12.f);
    a.ba.flap = frand() * kTwoPi;
    a.qa.phase = frand();
    a.sa.phase = frand() * kTwoPi;
    a.ba.t = a.qa.t = a.ra.t = a.sa.t = frand() * 100.f;
    if (gi >= 0) {
        Group& G = gW.groups[gi];
        a.slot = (int)G.members.size();
        G.members.push_back(ai);
    }
    return ai;
}

void releaseGroup(int gi) {
    Group& G = gW.groups[gi];
    for (int ai : G.members)
        if (ai >= 0 && ai < (int)gW.animals.size()) gW.animals[ai].used = false;
    G.members.clear();
    G.used = false;
}

}  // namespace wild_detail

// ==================================================================================================================
// Assets, frame context, threats, damage
namespace wild_detail {

void startAssets() {
    gW.assetsStarted = true;
    for (int sp = 0; sp < SP_COUNT; sp++) {
        gW.sp[sp].variants = Min(speciesInfo(sp).variants, kMaxVariants);
        for (int v = 0; v < gW.sp[sp].variants; v++) {
            ModelData* md = &gW.sp[sp].md[v];
            int s2 = sp, v2 = v;
            Jobs::submit([md, s2, v2] { buildModel(s2, v2, *md); }, kJobNormal, &gW.jobs);
        }
    }
}

void finishAssets(GameWorld& g) {
    Render::DynamicRenderer* dyn = g.renderer->dynamic;
    size_t tris = 0;
    for (int sp = 0; sp < SP_COUNT; sp++)
        for (int v = 0; v < gW.sp[sp].variants; v++) {
            ModelData& md = gW.sp[sp].md[v];
            for (int l = 0; l < 2; l++) {
                if (!md.lod[l].indices.empty()) {
                    gW.sp[sp].lod[v][l] = dyn->createSkinnedModel(md.lod[l]);
                    tris += md.lod[l].indices.size() / 3;
                }
                if (!md.batch[l].indices.empty()) {
                    // one batch draw covers a whole flock / shoal: generous bounds around the batch origin
                    md.batch[l].bounds = AABB(vec3(-170.f, -170.f, -90.f), vec3(170.f, 170.f, 170.f));
                    gW.sp[sp].batch[v][l] = dyn->createSkinnedModel(md.batch[l]);
                }
                md.lod[l] = SkinnedMeshData();
                md.batch[l] = SkinnedMeshData();
            }
        }
    gW.assetsReady = true;
    gW.arena.reserve(12000);
    LOG("Wildlife: %d species models ready (%zu k triangles in the detailed LODs)", (int)SP_COUNT, tris / 1000);
}

void addThreat(vec3 pos, vec3 vel, float radius, u8 kind, bool player, int ped, int veh) {
    Threat t;
    t.pos = pos;
    t.vel = vel;
    t.radius = radius;
    t.kind = kind;
    t.player = player;
    t.ped = ped;
    t.veh = veh;
    t.speed = length(vel.xy());
    gW.threats.push_back(t);
}

void hurtAnimal(GameWorld& g, int ai, float dmg, vec3 dir, int attacker, bool vehicleHit);

void gatherContext(GameWorld& g) {
    gW.cam = g.rig.cam.pos.toVec3();
    gW.camFwd = g.rig.cam.forward();
    gW.tod = g.env ? g.env->timeOfDay : 12.f;
    gW.rain = g.env ? g.env->rain : 0.f;
    gW.threats.clear();
    gW.pedsNear.clear();
    gW.vehNear.clear();
    Ped* pl = g.playerPed();
    gW.playerVeh = g.playerVehicle();
    if (pl) {
        gW.player = gW.playerVeh >= 0 ? g.vehicles[gW.playerVeh].sim.body.pos.toVec3() : pl->pos.toVec3();
        gW.playerVel = gW.playerVeh >= 0 ? g.vehicles[gW.playerVeh].sim.body.vel : pl->vel;
        gW.playerOnFoot = gW.playerVeh < 0 && (pl->state == PS_ONFOOT || pl->state == PS_GETUP);
        gW.playerSwim = pl->state == PS_SWIM;
        gW.playerAiming = pl->aiming && pl->weapon != WPN_FISTS;
        gW.playerRunning = gW.playerOnFoot && length(pl->vel.xy()) > 4.2f;
    } else {
        gW.player = gW.cam;
        gW.playerVel = vec3(0.f);
        gW.playerOnFoot = gW.playerSwim = gW.playerAiming = gW.playerRunning = false;
    }
    vec3 c = gW.cam;
    for (int i = 0; i < (int)g.peds.size(); i++) {
        const Ped& p = g.peds[i];
        if (!p.used || p.vehicle >= 0) continue;
        vec3 pp = p.pos.toVec3();
        if (length2(pp.xy() - c.xy()) > 160.f * 160.f) continue;
        gW.pedsNear.push_back(i);
        if (p.health <= 0.f || p.state == PS_DEAD) continue;
        float sp = length(p.vel.xy());
        // people scare wildlife in proportion to their speed; the player a bit more (and when aiming)
        float r = (p.isPlayer ? 8.f : 3.f) + sp * (p.isPlayer ? 1.6f : 1.2f) + (p.isPlayer && p.aiming ? 4.f : 0.f);
        addThreat(pp, p.vel, r, 0, p.isPlayer, i, -1);
        if (p.firing) addThreat(pp, vec3(0.f), 95.f, 2, p.isPlayer, i, -1);
    }
    for (int i = 0; i < (int)g.vehicles.size(); i++) {
        const Vehicle& v = g.vehicles[i];
        if (!v.used) continue;
        vec3 vp = v.sim.body.pos.toVec3();
        if (length2(vp.xy() - c.xy()) > 230.f * 230.f) continue;
        float sp = length(v.sim.body.vel);
        if (sp < 1.2f && !(v.hornOn)) continue;
        gW.vehNear.push_back(i);
        bool pl2 = gW.playerVeh == i;
        float r = 6.f + sp * 1.3f + (v.hornOn ? 25.f : 0.f) + (g.isAircraft(i) ? 40.f : 0.f);
        addThreat(vp, v.sim.body.vel, r, 1, pl2, -1, i);
    }
    // gunfire / explosions reported to the AI since the last frame
    float newest = gW.lastStimTime;
    for (const Stimulus& s : g.ai.stimuli) {
        if (s.time <= gW.lastStimTime) continue;
        newest = Max(newest, s.time);
        if (s.kind == STIM_GUNFIRE) addThreat(s.pos.toVec3(), vec3(0.f), 95.f, 2, s.player, s.source, -1);
        else if (s.kind == STIM_EXPLOSION) {
            vec3 ep = s.pos.toVec3();
            addThreat(ep, vec3(0.f), 160.f, 3, s.player, s.source, -1);
            // blast: kills and throws animals nearby
            for (int ai = 0; ai < (int)gW.animals.size(); ai++) {
                Animal& a = gW.animals[ai];
                if (!a.used || a.state == ST_DEAD) continue;
                vec3 d = a.pos - ep;
                float dist = length(d);
                if (dist > 12.f) continue;
                float f = Saturate(1.f - dist / 12.f);
                vec3 dir = dist > 0.1f ? d / dist : vec3(0, 0, 1);
                hurtAnimal(g, ai, 400.f * f * f + 5.f, normalize(dir + vec3(0, 0, 0.7f)) * (6.f + 14.f * f), s.player ? g.player : -1, false);
            }
        } else if (s.kind == STIM_HORN || s.kind == STIM_PANIC) {
            addThreat(s.pos.toVec3(), vec3(0.f), s.kind == STIM_HORN ? 30.f : 12.f, 0, s.player, s.source, -1);
        }
    }
    gW.lastStimTime = newest;
}

// Nearest threat that scares an animal at p with the given sensitivity multiplier; returns distance ratio (< 1 scared)
float scareLevel(vec3 p, float sensitivity, vec3* from = nullptr, bool* gunfire = nullptr, const Threat** which = nullptr) {
    float best = 1e9f;
    for (const Threat& t : gW.threats) {
        float r = t.radius * sensitivity;
        if (t.kind >= 2) r = t.radius * Max(sensitivity, 0.8f);
        float d = length(p - t.pos);
        float ratio = d / Max(r, 0.1f);
        if (ratio < best) {
            best = ratio;
            if (from) *from = t.pos;
            if (gunfire) *gunfire = t.kind >= 2;
            if (which) *which = &t;
        }
    }
    return best;
}

// Colour of the feathers a hit bird sheds
vec3 featherTint(int sp) {
    switch (sp) {
        case SP_PIGEON: return vec3(0.55f, 0.57f, 0.62f);
        case SP_VULTURE: case SP_PELICAN: return vec3(0.27f, 0.24f, 0.21f);
        case SP_SPOONBILL: case SP_FLAMINGO: return vec3(0.95f, 0.55f, 0.6f);
        case SP_PARROT: return vec3(0.35f, 0.75f, 0.3f);
        case SP_GRACKLE: case SP_FRIGATE: case SP_CORMORANT: return vec3(0.08f, 0.08f, 0.09f);
        case SP_SANDPIPER: return vec3(0.8f, 0.8f, 0.78f);
        default: return vec3(0.95f);
    }
}

void killAnimal(GameWorld& g, int ai, vec3 impulse, int attacker, bool vehicleHit) {
    Animal& a = gW.animals[ai];
    if (a.state == ST_DEAD || a.state == ST_FALL) return;
    const SpeciesInfo& si = speciesInfo(a.sp);
    a.health = 0.f;
    a.deadT = 0.f;
    a.vel = a.vel * 0.3f + impulse;
    a.spin = vec3(frange(-6.f, 6.f), frange(-6.f, 6.f), frange(-4.f, 4.f)) * (length(impulse) > 4.f ? 1.f : 0.25f);
    a.state = ST_FALL;
    a.t = 0.f;
    vec3 fxp = a.pos + vec3(0, 0, si.plan == PLAN_BIRD ? 0.f : si.height * 0.4f * a.scale);
    if (si.plan == PLAN_BIRD) {
        // puff of feathers
        spawnFx(FX_LEAVES, dvec3(fxp), vec3(0, 0, 1.f), 8, 0.35f * Max(a.scale, 0.6f), featherTint(a.sp));
    }
    if (si.plan == PLAN_QUAD || si.plan == PLAN_REPTILE || a.sp == SP_DOLPHIN || a.sp == SP_MANATEE) spawnFx(FX_BLOOD, dvec3(fxp), normalize(impulse + vec3(0, 0, 0.3f)), 4, 1.f);
    if (a.sp == SP_DOG) sfx(Audio::SFX_DOG_YELP, fxp, 1.f);
    else if (a.sp == SP_COW) sfx(Audio::SFX_COW_MOO, fxp, 0.9f, 0.8f);
    else if (a.sp == SP_HORSE) sfx(Audio::SFX_HORSE_NEIGH, fxp, 0.9f, 1.1f);
    else if (a.sp == SP_CAT) sfx(Audio::SFX_CAT_MEOW, fxp, 0.9f, 1.3f);
    if (vehicleHit && (si.plan == PLAN_QUAD || si.plan == PLAN_REPTILE)) sfx(Audio::SFX_BODY_FALL, fxp, 1.f, si.mass > 100.f ? 0.8f : 1.2f);
    if (attacker >= 0 && attacker == g.player) {
        g.pinfo.killMarker = true;
        g.pinfo.hitMarker = 1.f;
    }
    (void)g;
}

// Every wound makes the animal react (flee, turn on the attacker); death when health runs out.
void hurtAnimal(GameWorld& g, int ai, float dmg, vec3 dir, int attacker, bool vehicleHit) {
    Animal& a = gW.animals[ai];
    if (!a.used || a.state == ST_DEAD || a.state == ST_FALL) return;
    a.health -= dmg;
    if (a.health <= 0.f) {
        killAnimal(g, ai, dir, attacker, vehicleHit);
        return;
    }
    a.cooldown = 0.f;
    a.target = attacker;
    a.targetUid = attacker >= 0 && attacker < (int)g.peds.size() ? g.peds[attacker].uid : 0;
    if (a.group >= 0) {
        Group& G = gW.groups[a.group];
        G.alarm = 12.f;
        if (attacker >= 0 && attacker < (int)g.peds.size()) G.threat = g.peds[attacker].pos.toVec3();
        else G.threat = a.pos - dir;
    }
    if (a.sp == SP_DOG) sfx(Audio::SFX_DOG_YELP, a.pos + vec3(0, 0, 0.5f), 0.9f);
    if (a.sp == SP_GATOR) sfx(Audio::SFX_GATOR_HISS, a.pos + vec3(0, 0, 0.4f), 1.f);
    a.vel += dir * 0.3f;
}

}  // namespace wild_detail

// ==================================================================================================================
// Locomotion
namespace wild_detail {

struct FlyParams {
    float cruise, minSpd, maxSpd, accel, flapFreq, glide;   // glide: 0 always flapping .. 1 soaring
};
const FlyParams& flyParams(int sp) {
    static const FlyParams k[] = {
        {9.f, 5.5f, 15.f, 7.f, 2.8f, 0.6f},    // gull
        {10.f, 6.5f, 14.f, 5.f, 2.1f, 0.55f},  // pelican
        {11.f, 6.f, 17.f, 10.f, 6.5f, 0.2f},   // pigeon
        {8.5f, 5.f, 12.f, 4.f, 2.2f, 0.35f},   // heron
        {9.f, 5.f, 12.f, 4.5f, 2.6f, 0.35f},   // egret
        {10.f, 6.f, 13.f, 5.f, 3.0f, 0.3f},    // spoonbill
        {11.f, 7.f, 14.f, 5.f, 2.6f, 0.3f},    // flamingo
        {10.f, 6.f, 14.f, 6.f, 3.6f, 0.45f},   // ibis
        {10.f, 7.f, 15.f, 3.f, 1.8f, 0.95f},   // vulture
        {12.f, 7.f, 17.f, 10.f, 8.f, 0.1f},    // parakeet
    };
    static const FlyParams k2[] = {
        {9.f, 5.f, 14.f, 12.f, 9.f, 0.2f},      // sanderling: fast twinkling beats low over the surf
        {10.f, 5.5f, 15.f, 9.f, 5.f, 0.25f},    // grackle
        {9.f, 6.f, 16.f, 3.f, 1.4f, 0.97f},     // frigatebird: hangs in the wind, hardly ever flaps
        {12.f, 8.f, 16.f, 6.f, 4.2f, 0.1f},     // cormorant: steady beats low over the water
        {10.f, 6.f, 13.f, 6.f, 3.2f, 0.3f},     // cattle egret
    };
    if (sp >= (int)SP_SANDPIPER) return k2[Clamp(sp - (int)SP_SANDPIPER, 0, (int)ARRAY_COUNT(k2) - 1)];
    return k[Clamp(sp, 0, (int)SP_PARROT)];
}

// Integrates a flying bird: steering acceleration, speed limits, ground / roof / water floor, banking and wing beats.
void flyStep(GameWorld& g, Animal& a, vec3 steer, float dt, const FlyParams& fp, float minClear, float minSpeed) {
    float sl = length(steer);
    if (sl > fp.accel) steer *= fp.accel / sl;
    a.vel += steer * dt;
    float sp = length(a.vel);
    if (sp > fp.maxSpd) a.vel *= fp.maxSpd / sp;
    else if (sp < minSpeed) a.vel = (sp > 0.1f ? a.vel / sp : vec3(dirOf(a.yaw), 0.f)) * minSpeed;
    sp = length(a.vel);
    a.groundT -= dt;
    if (a.groundT <= 0.f) {
        a.groundT = 0.22f + frand() * 0.1f;
        vec3 ahead = a.pos + a.vel * 1.3f;
        float gz = groundAt(g, ahead.x, ahead.y, ahead.z + 80.f);
        float wz;
        a.inWater = waterAt(ahead.x, ahead.y, wz);
        if (a.inWater) {
            gz = Max(gz, wz);
            a.waterZ = wz;
        }
        float top;
        if (g.buildings && g.buildings->pointInBuilding(ahead.xy(), 3.f, &top)) gz = Max(gz, top);
        a.groundZ = gz;
    }
    float floorZ = a.groundZ + minClear;
    if (a.pos.z < floorZ) a.vel.z += Min((floorZ - a.pos.z) * 3.f, 8.f) * dt;
    a.pos += a.vel * dt;
    if (a.pos.z < a.groundZ + 0.25f) {
        a.pos.z = a.groundZ + 0.25f;
        a.vel.z = Max(a.vel.z, 0.f);
    }
    // orientation follows the flight path; banking from the turn rate
    vec2 h = a.vel.xy();
    float hs = length(h);
    float prevYaw = a.yaw;
    if (hs > 0.3f) a.yaw = approachAngle(a.yaw, yawOf(h), 6.f * dt);
    float yawRate = wrapA(a.yaw - prevYaw) / Max(dt, 1e-3f);
    float pitchT = Clamp(atan2f(a.vel.z, Max(hs, 0.5f)) * 0.8f, -0.9f, 0.6f);
    a.pitch = Lerp(a.pitch, pitchT, expDecay(5.f, dt));
    float bankT = Clamp(atanf(yawRate * sp / 9.81f), -1.05f, 1.05f);
    a.roll = Lerp(a.roll, -bankT, expDecay(3.5f, dt));
    // wing beats: climbing or slow -> flap, otherwise alternate flapping and gliding
    float want;
    if (a.vel.z > 0.7f || sp < fp.cruise * 0.85f) want = 1.f;
    else {
        float cyc = sinf(a.life * (0.45f + 0.2f * hashToFloat(a.uid)) + (float)(a.uid & 63));
        want = cyc > fp.glide * 2.f - 1.f ? 0.9f : 0.f;
        if (a.vel.z < -0.8f) want *= 0.3f;
    }
    a.ba.flapAmp = Lerp(a.ba.flapAmp, want, expDecay(2.5f, dt));
    a.ba.flap += dt * kTwoPi * fp.flapFreq * (0.85f + 0.3f * a.ba.flapAmp);
    a.ba.soar = Lerp(a.ba.soar, fp.glide > 0.9f ? 1.f : 0.f, expDecay(1.f, dt));
    a.ba.fold = Max(0.f, a.ba.fold - dt * 4.f);
    a.ba.legs = Max(0.f, a.ba.legs - dt * 2.f);
    a.ba.flare = Max(0.f, a.ba.flare - dt * 2.f);
    a.ba.sit = Max(0.f, a.ba.sit - dt * 4.f);
    a.ba.walkAmt = 0.f;
}

// Moves a land animal: acceleration-limited velocity along its facing, collision push-out against static geometry,
// water / building avoidance, ground following. Returns false when blocked.
bool groundStep(GameWorld& g, Animal& a, vec2 wantVel, float dt, float accel, float turnRate, float radius, int waterMode) {
    vec2 v = a.vel.xy();
    vec2 dv = wantVel - v;
    float l = length(dv);
    if (l > accel * dt) dv *= accel * dt / l;
    v += dv;
    float sp = length(v);
    if (sp > 0.05f) a.yaw = approachAngle(a.yaw, yawOf(v), turnRate * dt);
    vec2 fwd = dirOf(a.yaw);
    // animals walk where they face (no strafing): turn first when the goal is behind
    float along = sp > 1e-4f ? dot(v / sp, fwd) : 0.f;
    v = fwd * (sp * Max(along, 0.f));
    vec2 np = a.pos.xy() + v * dt;
    bool blocked = false;
    if (waterMode == 0 || waterMode == 1) {
        float d = waterDepth(*g.map, np.x + fwd.x * radius, np.y + fwd.y * radius);
        if (d > (waterMode == 0 ? 0.12f : 0.7f)) blocked = true;
    }
    if (!blocked && length2(np - gW.player.xy()) > 380.f * 380.f && g.buildings && g.buildings->pointInBuilding(np, radius)) blocked = true;
    if (blocked) {
        v = vec2(0.f);
        a.timer = Min(a.timer, 0.f);
    } else {
        a.pos.x = np.x;
        a.pos.y = np.y;
    }
    // push out of static colliders (buildings, benches, poles, cars do not collide)
    if (Phys::gCollision && length2(a.pos.xy() - gW.player.xy()) < 380.f * 380.f && radius > 0.06f) {
        vec3 push, nrm;
        if (Phys::gCollision->capsuleOverlap(a.pos + vec3(0, 0, 0.1f), radius, 0.5f, push, nrm)) {
            a.pos.x += push.x;
            a.pos.y += push.y;
            v -= nrm.xy() * dot(v, nrm.xy());
        }
    }
    a.vel = vec3(v, 0.f);
    a.groundT -= dt;
    if (a.groundT <= 0.f) {
        a.groundT = 0.1f + frand() * 0.05f;
        a.groundZ = groundAt(g, a.pos.x, a.pos.y, a.pos.z + 1.2f, &a.groundN);
        float wz;
        a.inWater = waterAt(a.pos.x, a.pos.y, wz) && wz > a.groundZ;
        a.waterZ = a.inWater ? wz : -1000.f;
    }
    float k = expDecay(14.f, dt);
    float zWant = a.groundZ + a.standH;
    a.pos.z = Lerp(a.pos.z, zWant, k);
    if (fabsf(a.pos.z - zWant) > 2.f) a.pos.z = zWant;
    // body follows the ground slope
    vec3 n = a.groundN;
    vec3 f3(fwd, 0.f), r3(fwd.y, -fwd.x, 0.f);
    float pitchT = Clamp(-atan2f(dot(n, f3), Max(n.z, 0.2f)), -0.5f, 0.5f);
    float rollT = Clamp(atan2f(dot(n, r3), Max(n.z, 0.2f)), -0.4f, 0.4f);
    a.pitch = Lerp(a.pitch, pitchT, expDecay(6.f, dt));
    a.roll = Lerp(a.roll, rollT, expDecay(6.f, dt));
    return !blocked;
}

// 3D swimming below the surface. depth = wanted depth below the surface (m); stays above the bottom.
void swimStep(GameWorld& g, Animal& a, vec3 wantVel, float dt, float accel, float turnRate, float depth, float bodyR) {
    vec3 dv = wantVel - a.vel;
    float l = length(dv);
    if (l > accel * dt) dv *= accel * dt / l;
    a.vel += dv;
    vec3 np = a.pos + a.vel * dt;
    float wz;
    bool water = waterAt(np.x, np.y, wz);
    float bottom = g.map->heightAt(np.x, np.y);
    if (!water || wz - bottom < bodyR * 2.2f) {
        // shore or shallows ahead: turn back towards deeper water
        a.vel.x = -a.vel.x * 0.5f;
        a.vel.y = -a.vel.y * 0.5f;
        a.timer = Min(a.timer, 0.f);
        np = a.pos;
        water = waterAt(np.x, np.y, wz);
        bottom = g.map->heightAt(np.x, np.y);
    }
    a.pos = np;
    if (water) {
        a.waterZ = wz;
        a.inWater = true;
        float zWant = wz - depth;
        float lo = bottom + bodyR * 1.1f, hi = wz - bodyR * 0.2f;
        if (hi < lo) zWant = (lo + hi) * 0.5f;
        a.pos.z = Clamp(a.pos.z, Min(lo, hi), Max(lo, hi) + 3.f);
        (void)zWant;
    }
    vec2 h = a.vel.xy();
    float hs = length(h);
    float prevYaw = a.yaw;
    if (hs > 0.05f) a.yaw = approachAngle(a.yaw, yawOf(h), turnRate * dt);
    a.sa.turn = Lerp(a.sa.turn, wrapA(a.yaw - prevYaw) / Max(dt, 1e-3f), expDecay(5.f, dt));
    a.pitch = Lerp(a.pitch, Clamp(atan2f(a.vel.z, Max(hs, 0.3f)), -0.8f, 0.8f), expDecay(4.f, dt));
    a.roll = Lerp(a.roll, -Clamp(a.sa.turn * 0.25f, -0.5f, 0.5f), expDecay(3.f, dt));
}

// Landing spots around a group anchor: pole and post tops, lifeguard towers, low roof edges, pier railings, tree
// crowns, open ground and the water surface.
void findPerches(GameWorld& g, Group& G, float radius, int want, bool ground, bool water, bool posts, bool roofs, bool trees) {
    G.perches.clear();
    vec2 c = G.anchor.xy();
    if (Phys::gCollision && length2(c - gW.player.xy()) < 380.f * 380.f && (posts || roofs || trees)) {
        std::vector<int> ids;
        Phys::gCollision->collidersNear(c, radius, ids);
        for (int id : ids) {
            if ((int)G.perches.size() >= want) break;
            const Phys::Collider& col = Phys::gCollision->collider(id);
            if (col.owner < 0) continue;
            if (col.kind == Phys::COL_CYLINDER) {
                bool tree = col.surface == Phys::SURF_WOOD;
                if (tree && trees && col.he.z > 3.f) G.perches.push_back({col.c + vec3(frange(-0.8f, 0.8f), frange(-0.8f, 0.8f), col.he.z * 1.02f + 1.2f), 4});
                else if (!tree && posts && col.he.z > 2.5f) G.perches.push_back({col.c + vec3(0, 0, col.he.z + 0.08f), 1});
            } else if (col.kind == Phys::COL_BOX) {
                float top = col.c.z + col.he.z;
                bool building = (col.flags & 2) != 0;
                if (building && roofs && top < G.anchor.z + 45.f && top > G.anchor.z + 3.f) {
                    vec2 ax = col.ax, ay = perp(col.ax);
                    for (int k = 0; k < 2 && (int)G.perches.size() < want; k++) {
                        float u = frange(-0.9f, 0.9f), side = frand() < 0.5f ? -1.f : 1.f;
                        vec2 p = frand() < 0.5f ? col.c.xy() + ax * (u * col.he.x) + ay * (side * col.he.y * 0.98f)
                                                : col.c.xy() + ay * (u * col.he.y) + ax * (side * col.he.x * 0.98f);
                        G.perches.push_back({vec3(p, top + 0.02f), 3});
                    }
                } else if (!building && posts && col.he.z > 1.5f) {
                    G.perches.push_back({vec3(col.c.xy(), top + 0.02f), 1});   // lifeguard towers, shelters
                }
            }
        }
    }
    // pier decks: birds line the railings
    if (roofs && World::gSites) {
        for (const World::Pad& pd : World::gSites->pads) {
            if (pd.kind != World::PAD_DECK || (int)G.perches.size() >= want) continue;
            if (length(pd.c - c) > radius + pd.hx) continue;
            vec2 ay = perp(pd.ax);
            for (int k = 0; k < 6 && (int)G.perches.size() < want; k++) {
                float u = frange(-pd.hx, pd.hx);
                vec2 p = pd.c + pd.ax * u;
                if (length(p - c) > radius) continue;
                float side = frand() < 0.5f ? -1.f : 1.f;
                G.perches.push_back({vec3(p + ay * (side * pd.hy * 0.97f), pd.heightAt(p) + 1.05f), 3});
            }
        }
    }
    int tries = 0;
    while ((int)G.perches.size() < want && tries++ < want * 4) {
        vec2 p = c + gW.rng.inCircle() * radius;
        float wz;
        bool w = waterAt(p.x, p.y, wz);
        float gz = groundAt(g, p.x, p.y, G.anchor.z + 30.f);
        if (w && wz > gz + 0.05f) {
            if (water) G.perches.push_back({vec3(p, wz), 2});
        } else if (ground && !onRoadOrBuilding(g, p, 0.8f, 0.5f)) {
            G.perches.push_back({vec3(p, gz), 0});
        }
    }
}

int pickPerch(Group& G, vec3 from, int self, bool preferNear) {
    int best = -1;
    float bestScore = 1e9f;
    for (int i = 0; i < (int)G.perches.size(); i++) {
        Perch& pc = G.perches[i];
        if (pc.taken >= 0 && pc.taken != self && (pc.kind == 1 || pc.kind == 4)) continue;
        float d = length(pc.pos - from);
        float score = preferNear ? d + frand() * 25.f : frand() * 100.f;
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    if (best >= 0) G.perches[best].taken = self;
    return best;
}

void releasePerch(Animal& a) {
    if (a.group >= 0 && a.perch >= 0) {
        Group& G = gW.groups[a.group];
        if (a.perch < (int)G.perches.size() && G.perches[a.perch].taken == (int)(&a - &gW.animals[0])) G.perches[a.perch].taken = -1;
    }
    a.perch = -1;
}

}  // namespace wild_detail

// ==================================================================================================================
// Birds
namespace wild_detail {

struct BoidScratch {
    std::vector<vec3> pos, vel;
    std::vector<int> idx;
};
BoidScratch gBoid;

inline float birdHash(const Animal& a, u32 salt) { return hashToFloat(hash32(a.uid * 2654435761u + salt)); }

// Height of a bird's body centre (its model origin) above the surface it stands on / floats on
float birdStandHeight(const Animal& a, bool floating) {
    const fauna_detail::BirdSpec& B = fauna_detail::birdSpec(a.sp);
    return (floating ? B.bodyHB * 0.45f : modelOf(a).legLen) * a.scale;
}

// Ballistic fall of a shot / struck animal (birds tumble, others slide and roll), then it lies dead.
void fallStep(GameWorld& g, Animal& a, float dt) {
    const SpeciesInfo& si = speciesInfo(a.sp);
    a.deadT += dt;
    if (a.state == ST_FALL) {
        float drag = si.plan == PLAN_BIRD ? 0.6f : 0.15f;
        a.vel.z -= 9.81f * dt;
        a.vel *= 1.f - Min(drag * dt, 0.5f);
        a.pos += a.vel * dt;
        a.yaw += a.spin.z * dt;
        a.pitch += a.spin.x * dt * 0.5f;
        a.roll += a.spin.y * dt * 0.5f;
        float wz;
        bool water = waterAt(a.pos.x, a.pos.y, wz);
        float gz = groundAt(g, a.pos.x, a.pos.y, a.pos.z + 1.f);
        float floorZ = water ? Max(wz, gz) : gz;
        bool aquatic = si.plan == PLAN_CETACEAN || si.plan == PLAN_FISH || si.plan == PLAN_TURTLE || a.sp == SP_GATOR;
        if (aquatic && water && a.pos.z < wz) floorZ = a.pos.z;   // already in the water
        if (a.pos.z <= floorZ || (aquatic && water)) {
            if (si.plan == PLAN_BIRD && water && a.deadT > 0.05f) {
                spawnFx(FX_WATER_SPLASH, dvec3(vec3(a.pos.x, a.pos.y, wz)), vec3(0, 0, 1), 3, 0.4f);
                sfx(Audio::SFX_SPLASH_SMALL, a.pos, 0.6f);
            }
            a.pos.z = floorZ;
            a.inWater = water && wz > gz;
            a.waterZ = water ? wz : -1000.f;
            a.state = ST_DEAD;
            a.t = 0.f;
            a.vel = vec3(0.f);
            if (si.plan == PLAN_BIRD) {
                a.roll = (birdHash(a, 3) < 0.5f ? -1.f : 1.f) * 1.35f;
                a.pitch = 0.1f;
            } else if (si.plan == PLAN_QUAD) {
                a.roll = 0.f;   // the dead pose lies on its side
                a.pitch = 0.f;
            } else {
                a.roll = a.inWater ? kPi : 0.f;   // belly up in the water
                a.pitch = 0.f;
            }
        }
    }
    if (a.state == ST_DEAD) {
        a.t += dt;
        if (a.inWater) {   // float and drift
            float wz;
            if (waterAt(a.pos.x, a.pos.y, wz)) a.pos.z = Lerp(a.pos.z, wz - (si.plan == PLAN_BIRD ? 0.02f : 0.12f * a.scale), expDecay(3.f, dt));
        }
    }
    a.ba.dead = a.qa.dead = a.ra.dead = a.sa.dead = Min(1.f, a.deadT * 3.f);
    a.ba.fold = Lerp(a.ba.fold, 0.35f, expDecay(2.f, dt));
    a.ba.flapAmp = 0.f;
    a.ba.legs = 0.3f;
}

// Common perched behaviour: look around, preen, walk a few steps, float on the water.
void perchedAnim(Animal& a, float dt, bool floating) {
    a.ba.fold = Min(1.f, a.ba.fold + dt * 3.f);
    a.ba.flapAmp = Max(0.f, a.ba.flapAmp - dt * 3.f);
    a.ba.legs = floating ? Max(0.f, a.ba.legs - dt * 3.f) : Min(1.f, a.ba.legs + dt * 3.f);
    a.ba.sit = floating ? Min(1.f, a.ba.sit + dt * 2.f) : Max(0.f, a.ba.sit - dt * 2.f);
    a.ba.flare = Max(0.f, a.ba.flare - dt * 3.f);
    a.ba.tail = Max(0.f, a.ba.tail - dt * 2.f);
    a.ba.soar = 0.f;
    a.ba.dive = 0.f;
    // head: glances with pauses
    float look = sinf(a.life * 0.7f + birdHash(a, 1) * 10.f) + 0.5f * sinf(a.life * 1.9f + birdHash(a, 2) * 7.f);
    float glance = fabsf(look) > 0.9f ? look * 0.6f : 0.f;
    a.ba.headYaw = Lerp(a.ba.headYaw, glance, expDecay(8.f, dt));
    a.ba.mouth = Max(0.f, a.ba.mouth - dt * 3.f);
    a.pitch = Lerp(a.pitch, 0.f, expDecay(4.f, dt));
    a.roll = Lerp(a.roll, 0.f, expDecay(4.f, dt));
}

struct FlockCfg {
    float sep, view;
    float orbitR0, orbitR1, h0, h1;
    float fly0, fly1, rest0, rest1;
    float fleeDist;
    float diveRate;          // plunges per second while over water
    bool walks, swims, groupFlush;
    float walkSpeed;
};

void startTakeoff(Animal& a, float delay) {
    a.state = ST_TAKEOFF;
    a.t = -delay;
    releasePerch(a);
}

struct FlockCtx {
    FlockCfg cfg;
    const FlyParams* fp;
    BoidParams bp;
    float baseZ;
};

void gatherFlyingBoids(Group& G) {
    gBoid.pos.clear();
    gBoid.vel.clear();
    gBoid.idx.clear();
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used || (a.state != ST_FLY && a.state != ST_TAKEOFF && a.state != ST_LAND)) continue;
        gBoid.idx.push_back(ai);
        gBoid.pos.push_back(a.pos);
        gBoid.vel.push_back(a.vel);
    }
}

// One flock bird: perched / walking / floating, taking off, circling with the flock, landing, plunge-diving.
void flockBirdStep(GameWorld& g, int gi, int ai, float dt, const FlockCtx& fc) {
    Group& G = gW.groups[gi];
    const FlockCfg& cfg = fc.cfg;
    const FlyParams& fp = *fc.fp;
    Animal& a = gW.animals[ai];
    a.life += dt;
    a.t += dt;
    a.standH = modelOf(a).legLen * a.scale;
    if (a.state == ST_FALL || a.state == ST_DEAD) {
        fallStep(g, a, dt);
        return;
    }
    a.think -= dt;
    bool floating = a.state == ST_SWIM || (a.perch >= 0 && a.perch < (int)G.perches.size() && G.perches[a.perch].kind == 2);
    switch (a.state) {
        case ST_IDLE: case ST_WALK: case ST_SWIM: {
            if (G.sp == SP_CORMORANT && a.aux2 > 0.f) {   // swimming under water after a surface dive
                a.aux2 -= dt;
                float wz;
                if (!waterAt(a.pos.x, a.pos.y, wz)) wz = a.pos.z;
                a.yaw = wrapA(a.yaw + 0.5f * sinf(a.life * 0.6f + birdHash(a, 51) * 6.f) * dt);
                vec2 np = a.pos.xy() + dirOf(a.yaw) * (1.3f * dt);
                if (waterDepth(*g.map, np.x, np.y) > 1.2f) {
                    a.pos.x = np.x;
                    a.pos.y = np.y;
                } else {
                    a.yaw = wrapA(a.yaw + 2.f * dt);
                }
                a.pos.z = Lerp(a.pos.z, wz - 1.1f, expDecay(4.f, dt));
                a.ba.fold = 1.f;
                a.ba.legs = 0.f;
                a.ba.sit = 1.f;
                a.pitch = Lerp(a.pitch, -0.15f, expDecay(3.f, dt));
                if (a.aux2 <= 0.f) {   // back up, sometimes with a fish
                    a.pos.z = wz + birdStandHeight(a, true);
                    a.pitch = 0.f;
                    a.ba.mouth = frand() < 0.3f ? 1.f : 0.f;
                    a.t = 0.f;
                    spawnFx(FX_WATER_SPLASH, dvec3(vec3(a.pos.x, a.pos.y, wz)), vec3(0, 0, 1), 2, 0.25f);
                }
                return;
            }
            if (a.think <= 0.f) {
                a.think = 0.15f + frand() * 0.15f;
                bool gun = false;
                vec3 from;
                float sc = scareLevel(a.pos, cfg.fleeDist / 8.f, &from, &gun);
                if (sc < 1.f || G.alarm > 10.f) {
                    G.alarm = Max(G.alarm, gun ? 20.f : 6.f);
                    G.threat = from;
                    if (cfg.groupFlush) {
                        for (int aj : G.members) {
                            Animal& b = gW.animals[aj];
                            if (b.used && (b.state == ST_IDLE || b.state == ST_WALK || b.state == ST_SWIM)) startTakeoff(b, frand() * 0.35f);
                        }
                        sfx(Audio::SFX_WING_FLAP, a.pos, 1.f);
                        G.mode = 1;
                        G.modeT = 0.f;
                    } else {
                        startTakeoff(a, frand() * 0.2f);
                        if (G.sp == SP_GULL && frand() < 0.4f) sfx(Audio::SFX_SEAGULL, a.pos, 0.9f);
                        if (G.sp == SP_GRACKLE && frand() < 0.3f) sfx(Audio::SFX_PARROT_SQUAWK, a.pos, 0.45f, 1.4f);
                        if (G.sp == SP_PELICAN) sfx(Audio::SFX_WING_FLAP, a.pos, 0.8f, 0.7f);
                    }
                    return;
                }
            }
            a.timer -= dt;
            if (a.timer <= 0.f && !cfg.groupFlush) {
                startTakeoff(a, 0.f);
                return;
            }
            if (floating) {
                float wz;
                if (waterAt(a.pos.x, a.pos.y, wz)) a.pos.z = Lerp(a.pos.z, wz + birdStandHeight(a, true), expDecay(6.f, dt));
                vec2 drift = g.env ? g.env->windDir * (0.15f * g.env->wind) : vec2(0.f);
                a.pos += vec3(drift * dt, 0.f);
                perchedAnim(a, dt, true);
                if (G.sp == SP_CORMORANT && a.t > 3.f && G.alarm <= 0.f && frand() < dt * 0.06f && waterDepth(*g.map, a.pos.x, a.pos.y) > 1.5f) {
                    a.aux2 = frange(8.f, 22.f);   // a surface dive: a little forward leap and gone
                    a.t = 0.f;
                    spawnFx(FX_WATER_SPLASH, dvec3(a.pos), vec3(0, 0, 1), 2, 0.25f);
                }
            } else if (a.state == ST_WALK) {
                vec2 d = a.goal.xy() - a.pos.xy();
                float dist = length(d);
                vec2 want = dist > 0.15f ? d / dist * cfg.walkSpeed : vec2(0.f);
                groundStep(g, a, want, dt, 3.f, 7.f, 0.06f, 0);
                perchedAnim(a, dt, false);
                a.ba.walkAmt = Min(1.f, a.ba.walkAmt + dt * 4.f);
                a.ba.walk += dt * kTwoPi * (G.sp == SP_PIGEON ? 3.2f : (G.sp == SP_GRACKLE ? 2.7f : 2.2f));
                if (dist < 0.2f || a.t > 6.f) {
                    a.state = ST_IDLE;
                    a.t = 0.f;
                }
            } else {
                perchedAnim(a, dt, false);
                a.ba.walkAmt = Max(0.f, a.ba.walkAmt - dt * 4.f);
                bool onPost = a.perch >= 0 && a.perch < (int)G.perches.size() && G.perches[a.perch].kind != 0;
                bool ground = G.sp == SP_PIGEON || G.sp == SP_GRACKLE;   // feeding on the ground around the flock's spot
                if (!onPost && cfg.walks && a.t > 1.f && frand() < dt * 0.25f) {   // pecking about, short walks
                    a.state = ST_WALK;
                    a.t = 0.f;
                    vec2 r = gW.rng.inCircle() * (G.sp == SP_PIGEON ? 2.5f : (G.sp == SP_GRACKLE ? 3.f : 4.f));
                    vec2 c = (ground ? G.anchor.xy() * 0.3f + a.pos.xy() * 0.7f : a.pos.xy()) + r;
                    a.goal = vec3(c, a.pos.z);
                }
                if (G.sp == SP_CORMORANT && onPost) {   // perched cormorants hang their wings out to dry
                    if (a.aux > 0.f) {
                        a.aux -= dt;
                        a.timer = Max(a.timer, 5.f);
                        a.ba.fold = Max(0.f, a.ba.fold - dt * 6.f);
                        a.ba.flap = kHalfPi + 0.1f * sinf(a.life * 1.6f);
                        a.ba.flapAmp = 0.85f;
                        a.ba.headYaw = 0.4f * sinf(a.life * 0.3f);
                    } else if (a.t > 5.f && frand() < dt * 0.03f) {
                        a.aux = frange(20.f, 60.f);
                    }
                }
                float pk = ground ? (sinf(a.life * (G.sp == SP_GRACKLE ? 2.4f : 3.1f) + birdHash(a, 5) * 9.f) > 0.55f ? 1.f : 0.f) : 0.f;
                a.ba.peck = Lerp(a.ba.peck, pk, expDecay(10.f, dt));
                if (!onPost)   // side-step people walking through
                    for (int pi : gW.pedsNear) {
                        const Ped& p = g.peds[pi];
                        vec2 d = a.pos.xy() - p.pos.toVec3().xy();
                        if (length2(d) < 1.3f * 1.3f) {
                            a.state = ST_WALK;
                            a.t = 0.f;
                            a.goal = vec3(a.pos.xy() + normalize(d) * 1.8f, a.pos.z);
                            break;
                        }
                    }
            }
            return;
        }
        case ST_TAKEOFF: {
            if (a.t < 0.f) {   // staggered flock takeoff
                perchedAnim(a, dt, floating);
                return;
            }
            vec3 away(dirOf(a.yaw + birdHash(a, 9) - 0.5f), 0.f);
            if (G.alarm > 0.f) {
                vec2 aw = a.pos.xy() - G.threat.xy();
                if (length2(aw) > 0.01f) away = vec3(normalize(aw), 0.f);
            }
            vec3 wantV = away * fp.cruise * 0.7f + vec3(0, 0, 3.2f);
            a.vel = lerp(a.vel, wantV, expDecay(3.5f, dt));
            a.pos += a.vel * dt;
            if (length2(a.vel.xy()) > 1e-4f) a.yaw = approachAngle(a.yaw, yawOf(a.vel.xy()), 8.f * dt);
            a.pitch = Lerp(a.pitch, 0.35f, expDecay(5.f, dt));
            a.ba.fold = Max(0.f, a.ba.fold - dt * 5.f);
            a.ba.legs = Max(0.f, a.ba.legs - dt * 1.5f);
            a.ba.sit = Max(0.f, a.ba.sit - dt * 5.f);
            a.ba.flapAmp = Lerp(a.ba.flapAmp, 1.3f, expDecay(8.f, dt));
            a.ba.flap += dt * kTwoPi * fp.flapFreq * 1.3f;
            a.ba.tail = 0.6f;
            a.ba.peck = 0.f;
            if (a.t > 0.9f) {
                if (floating) spawnFx(FX_WATER_SPLASH, dvec3(a.pos), vec3(0, 0, 1), 2, 0.3f);
                a.state = ST_FLY;
                a.t = 0.f;
                a.timer = frange(cfg.fly0, cfg.fly1);
                a.groundT = 0.f;
            }
            return;
        }
        case ST_FLY: {
            int bi = -1;
            for (int q = 0; q < (int)gBoid.idx.size(); q++)
                if (gBoid.idx[q] == ai) {
                    bi = q;
                    break;
                }
            vec3 steer(0.f);
            if (bi >= 0) steer += boidSteer(gBoid.pos.data(), gBoid.vel.data(), (int)gBoid.pos.size(), bi, fc.bp) * 1.6f;
            // orbit the target: each bird keeps its own radius / height, the whole flock turns the same way
            float R = Lerp(cfg.orbitR0, cfg.orbitR1, birdHash(a, 11));
            float H = Lerp(cfg.h0, cfg.h1, birdHash(a, 12)) + (G.alarm > 0.f ? 10.f : 0.f);
            vec2 rel = a.pos.xy() - G.target.xy();
            float ang = atan2f(rel.y, rel.x) + 0.45f;
            vec3 want = vec3(G.target.xy() + vec2(cosf(ang), sinf(ang)) * R, fc.baseZ + H + 3.f * sinf(a.life * 0.21f + birdHash(a, 13) * 6.f));
            steer += (normalize(want - a.pos) * fp.cruise - a.vel) * 1.2f;
            for (const Threat& t : gW.threats) {   // keep clear of people and vehicles
                vec3 d = a.pos - t.pos;
                float dl = length(d);
                if (dl < 7.f && dl > 0.01f) steer += d / dl * (7.f - dl) * 1.5f;
            }
            flyStep(g, a, steer, dt, fp, 4.f, fp.minSpd);
            a.ba.neck = G.sp == SP_PELICAN ? -1.f : 0.f;
            a.ba.headYaw = Lerp(a.ba.headYaw, Clamp(a.roll * -0.8f, -0.6f, 0.6f), expDecay(3.f, dt));
            a.timer -= dt;
            if (cfg.diveRate > 0.f && a.inWater && G.alarm <= 0.f && a.pos.z - a.groundZ > 6.f && frand() < cfg.diveRate * dt) {
                a.state = ST_DIVE;
                a.t = 0.f;
                if (G.sp == SP_GULL && frand() < 0.5f) sfx(Audio::SFX_SEAGULL, a.pos, 0.8f);
                return;
            }
            if (cfg.groupFlush && G.mode == 1 && G.modeT > a.timer + 2.f && G.alarm <= 0.f) G.mode = 2;
            bool mayLand = G.alarm <= 0.f && a.timer <= 0.f && (!cfg.groupFlush || G.mode == 2);
            if (mayLand) {
                a.perch = pickPerch(G, a.pos, ai, true);
                if (a.perch >= 0) {
                    a.state = ST_LAND;
                    a.t = 0.f;
                    a.goal = G.perches[a.perch].pos + vec3(0, 0, birdStandHeight(a, G.perches[a.perch].kind == 2));
                } else {
                    a.timer = frange(5.f, 15.f);
                }
            }
            return;
        }
        case ST_LAND: {
            vec3 to = a.goal - a.pos;
            float dist = length(to);
            float spd = Min(fp.cruise, dist * 0.9f + 1.2f);
            vec3 wantV = dist > 0.01f ? to / dist * spd : vec3(0.f);
            FlyParams lp = fp;
            lp.accel = fp.accel * 1.8f;
            flyStep(g, a, (wantV - a.vel) * 3.5f, dt, lp, dist > 12.f ? 2.f : 0.f, 0.4f);
            float fl = Saturate(1.f - dist / 5.f);
            a.ba.flare = fl;
            a.ba.tail = fl;
            a.ba.legs = fl;
            if (fl > 0.2f) a.ba.flapAmp = Lerp(a.ba.flapAmp, 1.1f, expDecay(6.f, dt));
            a.pitch = Lerp(a.pitch, 0.f, fl);
            if (dist < 0.45f || a.t > 20.f) {
                a.pos = a.goal;
                a.vel = vec3(0.f);
                bool water = a.perch >= 0 && a.perch < (int)G.perches.size() && G.perches[a.perch].kind == 2;
                if (water) spawnFx(FX_WATER_SPLASH, dvec3(a.pos), vec3(0, 0, 1), 2, 0.3f);
                a.state = water ? ST_SWIM : ST_IDLE;
                a.t = 0.f;
                a.timer = frange(cfg.rest0, cfg.rest1);
                a.pitch = a.roll = 0.f;
                a.groundZ = a.pos.z;
            }
            return;
        }
        case ST_DIVE: {
            a.ba.dive = Min(1.f, a.ba.dive + dt * 3.f);
            a.ba.flapAmp = 0.f;
            a.vel = lerp(a.vel, vec3(dirOf(a.yaw) * 5.f, -14.f), expDecay(3.f, dt));
            a.pos += a.vel * dt;
            a.pitch = Lerp(a.pitch, -1.25f, expDecay(5.f, dt));
            a.roll = Lerp(a.roll, 0.f, expDecay(5.f, dt));
            float wz;
            if (!waterAt(a.pos.x, a.pos.y, wz)) {
                a.state = ST_FLY;
                a.ba.dive = 0.f;
                return;
            }
            if (a.pos.z <= wz + birdStandHeight(a, true)) {
                bool big = G.sp == SP_PELICAN;
                spawnFx(FX_WATER_SPLASH, dvec3(vec3(a.pos.x, a.pos.y, wz)), vec3(0, 0, 1), big ? 8 : 4, big ? 1.1f : 0.6f);
                sfx(big ? Audio::SFX_SPLASH_BIG : Audio::SFX_SPLASH_SMALL, a.pos, 0.8f);
                a.pos.z = wz + birdStandHeight(a, true);
                a.vel = vec3(0.f);
                a.pitch = 0.f;
                a.ba.dive = 0.f;
                a.state = ST_SWIM;
                a.perch = -1;
                a.t = 0.f;
                a.timer = frange(2.f, 5.f);
                a.ba.mouth = 1.f;
                a.ba.fold = 1.f;
            }
            return;
        }
        default: a.state = ST_FLY; return;
    }
}

// Gulls, pigeons and similar flock birds
void updateFlockBirds(GameWorld& g, int gi, float dt, const FlockCfg& cfg) {
    Group& G = gW.groups[gi];
    FlockCtx fc;
    fc.cfg = cfg;
    fc.fp = &flyParams(G.sp);
    fc.bp.sepDist = cfg.sep;
    fc.bp.viewDist = cfg.view;
    fc.bp.wSep = 3.f;
    fc.bp.wAli = 0.8f;
    fc.bp.wCoh = 0.12f;
    fc.baseZ = G.anchor.z;
    gatherFlyingBoids(G);
    G.alarm = Max(0.f, G.alarm - dt);
    G.modeT += dt;
    vec3 tgt = G.anchor;
    if (G.alarm > 0.f) {   // the flock drifts away from the threat for a while
        vec2 away = G.anchor.xy() - G.threat.xy();
        away = length2(away) > 1.f ? normalize(away) : dirOf(G.heading);
        tgt = G.anchor + vec3(away * 45.f, 12.f);
    } else if (G.sp == SP_GULL) {
        // a boat under way near the flock's spot (the player's too): the gulls trail it, hanging low over the wake,
        // and drift back home once it has gone
        int best = -1;
        float bd = 170.f;
        for (int vi : gW.vehNear) {
            if (!g.isBoat(vi) || !g.vehicles[vi].used) continue;
            const Vehicle& v = g.vehicles[vi];
            float d = length(v.sim.body.pos.toVec3().xy() - G.anchor.xy());
            if (length(v.sim.body.vel.xy()) > 3.f && d < bd) {
                bd = d;
                best = vi;
            }
        }
        if (best >= 0) {
            const Vehicle& v = g.vehicles[best];
            vec2 vd = normalize(v.sim.body.vel.xy());
            tgt = vec3(v.sim.body.pos.toVec3().xy() - vd * 10.f, G.anchor.z);
            fc.cfg.orbitR0 = 4.f;
            fc.cfg.orbitR1 = 14.f;
            fc.cfg.h0 = 4.f;
            fc.cfg.h1 = 12.f;
            for (int ai : G.members) {
                Animal& a = gW.animals[ai];
                if (!a.used) continue;
                if ((a.state == ST_IDLE || a.state == ST_SWIM) && frand() < dt * 0.4f) startTakeoff(a, 0.f);   // the perched ones join in
                if (a.state == ST_FLY) a.timer = Max(a.timer, 3.f);
            }
        }
    }
    G.target = lerp(G.target, tgt, expDecay(0.3f, dt));
    for (int ai : G.members)
        if (gW.animals[ai].used) flockBirdStep(g, gi, ai, dt, fc);
    // pigeons: circle for a while after a flush, then all come back down together
    if (cfg.groupFlush) {
        if (G.mode == 0 && G.modeT > G.timer) {   // now and then the flock takes a lap on its own
            G.timer = frange(60.f, 150.f);
            G.modeT = 0.f;
            G.mode = 1;
            for (int aj : G.members) {
                Animal& b = gW.animals[aj];
                if (b.used && (b.state == ST_IDLE || b.state == ST_WALK)) startTakeoff(b, frand() * 0.35f);
            }
        }
        if (G.mode == 2) {
            bool allDown = true;
            for (int ai : G.members) {
                const Animal& a = gW.animals[ai];
                if (a.used && (a.state == ST_FLY || a.state == ST_LAND || a.state == ST_TAKEOFF)) allDown = false;
            }
            if (allDown) {
                G.mode = 0;
                G.modeT = 0.f;
            }
        }
    }
    G.soundT -= dt;
    if (G.soundT <= 0.f && !G.members.empty()) {
        G.soundT = G.sp == SP_PIGEON ? frange(2.5f, 7.f) : frange(1.5f, 5.f);
        int ai = G.members[(size_t)(gW.rng.next() % G.members.size())];
        const Animal& a = gW.animals[ai];
        if (a.used && a.state != ST_DEAD && a.state != ST_FALL && length2(a.pos - gW.cam) < 140.f * 140.f) {
            if (G.sp == SP_GULL) sfx(Audio::SFX_SEAGULL, a.pos, 0.75f);
            else if (G.sp == SP_PIGEON && (a.state == ST_IDLE || a.state == ST_WALK)) sfx(Audio::SFX_PIGEON_COO, a.pos, 0.6f);
            else if (G.sp == SP_GRACKLE && frand() < 0.6f) sfx(Audio::SFX_PARROT_SQUAWK, a.pos, 0.4f, frange(1.3f, 1.6f));   // creaky, rattling calls
        }
    }
}

// Pelicans: a line gliding low over the water along the coast, a flap wave running down the line; they rest on posts
// or the water, and plunge for fish.
void updatePelicans(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    const FlyParams& fp = flyParams(SP_PELICAN);
    FlockCtx fc;
    fc.cfg = {3.f, 10.f, 20.f, 40.f, 3.f, 8.f, 1e9f, 1e9f, 1e9f, 1e9f, 10.f, 0.f, false, true, false, 0.6f};
    fc.fp = &fp;
    fc.bp.sepDist = 3.f;
    fc.bp.viewDist = 10.f;
    fc.baseZ = G.anchor.z;
    G.modeT += dt;
    G.alarm = Max(0.f, G.alarm - dt);
    // mode 0: patrolling the shore, mode 1: resting at the anchor
    if (G.mode == 0 && G.modeT > G.timer) {
        G.mode = 1;
        G.modeT = 0.f;
        G.timer = frange(30.f, 80.f);
        for (int ai : G.members) {
            Animal& a = gW.animals[ai];
            if (!a.used || a.state != ST_FLY) continue;
            a.perch = pickPerch(G, a.pos, ai, true);
            if (a.perch >= 0) {
                a.state = ST_LAND;
                a.goal = G.perches[a.perch].pos + vec3(0, 0, birdStandHeight(a, G.perches[a.perch].kind == 2));
                a.t = 0.f;
            }
        }
    } else if (G.mode == 1 && (G.modeT > G.timer || G.alarm > 0.f)) {
        G.mode = 0;
        G.modeT = 0.f;
        G.timer = frange(40.f, 100.f);
        for (int ai : G.members) {
            Animal& a = gW.animals[ai];
            if (a.used && (a.state == ST_IDLE || a.state == ST_SWIM)) startTakeoff(a, frand() * 1.5f);
        }
    }
    Animal* leader = nullptr;
    for (int ai : G.members)
        if (gW.animals[ai].used && gW.animals[ai].state == ST_FLY) {
            leader = &gW.animals[ai];
            break;
        }
    // leader waypoints along the coastline over shallow water
    if (leader && (length(leader->pos.xy() - G.target.xy()) < 25.f || length(G.target - G.anchor) < 1.f)) {
        bool found = false;
        for (int tries = 0; tries < 12 && !found; tries++) {
            float h = G.heading + frange(-0.5f, 0.5f);
            vec2 p = leader->pos.xy() + dirOf(h) * frange(90.f, 160.f);
            float sd = g.map->coastDistance(p.x, p.y);
            float wz;
            if (sd < -10.f && sd > -160.f && waterAt(p.x, p.y, wz) && length(p - G.anchor.xy()) < 700.f) {
                G.heading = h;
                G.target = vec3(p, wz);
                found = true;
            }
        }
        if (!found) {
            G.heading += kPi * 0.8f;   // turn back along the coast
            G.target = G.anchor + vec3(dirOf(G.heading) * 30.f, 0.f);
        }
    }
    gatherFlyingBoids(G);
    int rank = 0;
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        if (a.state != ST_FLY || G.mode == 1) {
            if (a.state == ST_FLY && G.mode == 1) a.timer = 0.f;   // land as soon as a perch is free
            flockBirdStep(g, gi, ai, dt, fc);
            continue;
        }
        a.life += dt;
        a.t += dt;
        vec3 goal;
        if (&a == leader) goal = G.target + vec3(0, 0, 3.f);
        else {
            vec3 ld = normalize(vec3(leader->vel.xy(), 0.f) + vec3(0.001f, 0, 0));
            vec3 side = vec3(ld.y, -ld.x, 0.f);
            goal = leader->pos - ld * (4.5f * (float)rank) + side * (1.1f * (float)rank);
        }
        vec3 to = goal - a.pos;
        vec3 steer = (normalize(to) * Min(fp.maxSpd, fp.cruise + length(to) * 0.2f) - a.vel) * 1.4f;
        flyStep(g, a, steer, dt, fp, 2.5f, fp.minSpd);
        float cyc = fmodf(a.life * 0.33f - (float)rank * 0.12f + 10.f, 1.f);
        a.ba.flapAmp = Lerp(a.ba.flapAmp, cyc < 0.35f ? 0.8f : 0.f, expDecay(4.f, dt));
        a.ba.neck = -1.f;
        if (a.inWater && frand() < dt * 0.015f && G.alarm <= 0.f) {
            a.state = ST_DIVE;
            a.t = 0.f;
        }
        rank++;
    }
}

}  // namespace wild_detail

// ==================================================================================================================
// Waders, vultures, parakeets
namespace wild_detail {

// A shallow-water spot for waders near `around` (bottom depth range), or false.
bool findShallowSpot(GameWorld& g, vec2 around, float r0, float r1, float minD, float maxD, vec3& out) {
    for (int tries = 0; tries < 24; tries++) {
        float ang = frand() * kTwoPi, r = frange(r0, r1);
        vec2 p = around + vec2(cosf(ang), sinf(ang)) * r;
        float d = waterDepth(*g.map, p.x, p.y);
        if (d < minD || d > maxD) continue;
        if (g.roads && g.roads->nearRoad(p, 8.f)) continue;
        out = vec3(p, g.map->heightAt(p.x, p.y));
        return true;
    }
    return false;
}

void updateWaders(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    int sp = G.sp;
    const FlyParams& fp = flyParams(sp);
    float flee = sp == SP_HERON ? 26.f : (sp == SP_FLAMINGO ? 30.f : (sp == SP_IBIS ? 12.f : 20.f));
    bool striker = sp == SP_HERON || sp == SP_EGRET;
    float maxDepth = sp == SP_FLAMINGO ? 0.55f : (striker ? 0.4f : 0.28f);
    G.alarm = Max(0.f, G.alarm - dt);
    G.modeT += dt;
    // relocation: the group moves to another feeding spot after a scare or now and then
    bool relocate = false;
    if (G.mode == 0 && G.alarm > 0.f) relocate = true;
    if (G.mode == 0 && G.modeT > G.timer) relocate = true;
    if (relocate) {
        vec3 spot;
        vec2 from = G.anchor.xy();
        if (G.alarm > 0.f) from += normalize(G.anchor.xy() - G.threat.xy() + vec2(0.01f, 0.f)) * 120.f;
        if (findShallowSpot(g, from, 40.f, 180.f, 0.03f, maxDepth, spot)) {
            G.target = spot;
            G.mode = 1;
            G.modeT = 0.f;
            for (int ai : G.members) {
                Animal& a = gW.animals[ai];
                if (a.used && a.state != ST_DEAD && a.state != ST_FALL && a.state != ST_FLY && a.state != ST_TAKEOFF) startTakeoff(a, frand() * 0.8f);
            }
            if (sp == SP_HERON || sp == SP_EGRET) sfx(Audio::SFX_HERON_CALL, G.anchor + vec3(0, 0, 1.f), 1.f);
        } else {
            G.modeT = 0.f;
            G.timer = frange(30.f, 60.f);
        }
    }
    int k = 0;
    int landed = 0, alive = 0;
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        a.life += dt;
        a.t += dt;
        a.standH = modelOf(a).legLen * a.scale;
        if (a.state == ST_FALL || a.state == ST_DEAD) {
            fallStep(g, a, dt);
            continue;
        }
        alive++;
        a.think -= dt;
        switch (a.state) {
            case ST_IDLE: case ST_WALK: case ST_FEED: case ST_ALERT: {
                landed++;
                if (a.think <= 0.f) {
                    a.think = 0.2f + frand() * 0.2f;
                    bool gun = false;
                    vec3 from;
                    float sc = scareLevel(a.pos, flee / 8.f, &from, &gun);
                    if (sc < 0.75f || gun) {
                        G.alarm = gun ? 25.f : 10.f;
                        G.threat = from;
                        break;
                    } else if (sc < 1.3f && a.state != ST_ALERT) {
                        a.state = ST_ALERT;
                        a.t = 0.f;
                        a.goal = from;
                    }
                }
                perchedAnim(a, dt, false);
                if (a.state == ST_ALERT) {
                    vec2 d = a.goal.xy() - a.pos.xy();
                    float want = wrapA(yawOf(d) - a.yaw);
                    a.ba.headYaw = Lerp(a.ba.headYaw, Clamp(want, -1.4f, 1.4f), expDecay(5.f, dt));
                    a.ba.neck = Lerp(a.ba.neck, 0.35f, expDecay(4.f, dt));
                    a.ba.peck = Max(0.f, a.ba.peck - dt * 3.f);
                    if (a.t > 5.f) {
                        a.state = ST_IDLE;
                        a.t = 0.f;
                    }
                    break;
                }
                if (a.state == ST_IDLE) {
                    a.ba.neck = Lerp(a.ba.neck, 0.f, expDecay(3.f, dt));
                    a.ba.peck = Lerp(a.ba.peck, 0.f, expDecay(3.f, dt));
                    if (a.t > frange(3.f, 9.f)) {
                        vec3 spot;
                        if (findShallowSpot(g, a.pos.xy(), 1.5f, 7.f, 0.02f, maxDepth, spot)) {
                            a.goal = spot;
                            a.state = ST_WALK;
                            a.t = 0.f;
                        } else a.t = 0.f;
                    }
                } else if (a.state == ST_WALK) {
                    vec2 d = a.goal.xy() - a.pos.xy();
                    float dist = length(d);
                    float spd = striker ? 0.22f : (sp == SP_IBIS ? 0.38f : 0.3f);
                    groundStep(g, a, dist > 0.2f ? d / dist * spd : vec2(0.f), dt, 1.5f, 1.5f, 0.1f, 1);
                    a.ba.walkAmt = Min(1.f, a.ba.walkAmt + dt * 2.f);
                    a.ba.walk += dt * kTwoPi * (striker ? 0.7f : 1.1f);
                    if (!striker) {   // sweeping / probing bills
                        a.ba.peck = Lerp(a.ba.peck, sp == SP_IBIS ? (sinf(a.life * 5.f) > 0.f ? 1.f : 0.6f) : 0.85f, expDecay(4.f, dt));
                        a.ba.headYaw = sp == SP_IBIS ? 0.f : 0.5f * sinf(a.life * 2.2f);
                    } else {
                        a.ba.neck = Lerp(a.ba.neck, -0.15f, expDecay(2.f, dt));   // stalking crouch
                    }
                    if (dist < 0.25f || a.t > 20.f) {
                        a.state = striker && frand() < 0.5f ? ST_FEED : ST_IDLE;
                        a.t = 0.f;
                    }
                } else if (a.state == ST_FEED) {   // the strike: a fast stab into the water and back
                    float s = a.t < 1.2f ? 0.f : (a.t < 1.45f ? (a.t - 1.2f) / 0.25f : Max(0.f, 1.f - (a.t - 1.45f) / 0.5f));
                    a.ba.neck = Lerp(-0.2f, 1.f, s);
                    a.ba.peck = s;
                    a.ba.walkAmt = 0.f;
                    if (a.t > 1.44f && a.t - dt <= 1.44f) {
                        float wz;
                        vec3 tip = worldPoint(a, modelOf(a).headTip);
                        if (waterAt(tip.x, tip.y, wz)) spawnFx(FX_WATER_SPLASH, dvec3(vec3(tip.x, tip.y, wz)), vec3(0, 0, 1), 1, 0.2f);
                    }
                    if (a.t > 2.3f) {
                        a.state = ST_IDLE;
                        a.t = 0.f;
                    }
                }
                break;
            }
            case ST_TAKEOFF: {
                if (a.t < 0.f) break;
                vec2 dir2 = G.mode == 1 ? G.target.xy() - a.pos.xy() : dirOf(a.yaw);
                if (G.alarm > 0.f) dir2 = a.pos.xy() - G.threat.xy();
                dir2 = normalize(dir2 + vec2(0.001f, 0.f));
                a.vel = lerp(a.vel, vec3(dir2 * 3.5f, 2.6f), expDecay(3.f, dt));
                a.pos += a.vel * dt;
                a.yaw = approachAngle(a.yaw, yawOf(dir2), 4.f * dt);
                a.ba.fold = Max(0.f, a.ba.fold - dt * 4.f);
                a.ba.legs = Max(0.f, a.ba.legs - dt * 0.8f);
                a.ba.flapAmp = Lerp(a.ba.flapAmp, 1.25f, expDecay(6.f, dt));
                a.ba.flap += dt * kTwoPi * fp.flapFreq * 1.2f;
                a.ba.neck = Lerp(a.ba.neck, 0.f, expDecay(3.f, dt));
                a.ba.peck = 0.f;
                if (a.t > 1.3f) {
                    a.state = ST_FLY;
                    a.t = 0.f;
                    a.groundT = 0.f;
                }
                break;
            }
            case ST_FLY: {
                vec3 slot = G.target + vec3(frange(-0.2f, 0.2f), 0, 0) + vec3(dirOf(birdHash(a, 7) * kTwoPi) * (3.f * (float)k), 0.f);
                vec3 to = slot + vec3(0, 0, 14.f) - a.pos;
                float dh = length(to.xy());
                if (dh < 40.f) to.z = slot.z + 2.f + dh * 0.3f - a.pos.z;   // descend on approach
                vec3 steer = (normalize(to) * fp.cruise - a.vel) * 1.2f;
                flyStep(g, a, steer, dt, fp, 3.f, fp.minSpd);
                a.ba.neck = striker ? -1.f : 0.3f;   // herons fly with the neck folded, the others stretched out
                if (dh < 18.f) {
                    a.state = ST_LAND;
                    a.t = 0.f;
                    a.goal = slot + vec3(0, 0, a.standH);
                    float gz = g.map->heightAt(a.goal.x, a.goal.y);
                    a.goal.z = gz + a.standH;
                }
                break;
            }
            case ST_LAND: {
                vec3 to = a.goal - a.pos;
                float dist = length(to);
                vec3 wantV = dist > 0.01f ? to / dist * Min(fp.cruise, dist * 0.8f + 1.f) : vec3(0.f);
                FlyParams lp = fp;
                lp.accel *= 1.6f;
                flyStep(g, a, (wantV - a.vel) * 3.f, dt, lp, 0.f, 0.3f);
                float fl = Saturate(1.f - dist / 6.f);
                a.ba.flare = fl;
                a.ba.legs = fl;
                a.ba.tail = fl;
                a.ba.neck = Lerp(a.ba.neck, 0.f, fl);
                if (dist < 0.4f || a.t > 15.f) {
                    a.pos = a.goal;
                    a.vel = vec3(0.f);
                    a.pitch = a.roll = 0.f;
                    a.state = ST_IDLE;
                    a.t = 0.f;
                    a.groundZ = a.goal.z - a.standH;
                }
                break;
            }
            default: a.state = ST_IDLE; break;
        }
        k++;
    }
    if (G.mode == 1 && landed == alive && alive > 0 && G.modeT > 3.f) {
        G.mode = 0;
        G.modeT = 0.f;
        G.timer = frange(60.f, 180.f);
        G.anchor = G.target;
    }
}

// Vultures: soaring circles in a thermal, then a long glide to the next one.
void updateVultures(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    const FlyParams& fp = flyParams(G.sp);
    bool frigate = G.sp == SP_FRIGATE;   // frigatebirds hang over the beach and the bay instead of open country
    G.modeT += dt;
    vec2 wind = g.env ? g.env->windDir * (0.6f + g.env->wind) : vec2(0.5f, 0.3f);
    if (G.mode == 0) {
        G.target += vec3(wind * dt, 0.f);
        if (G.modeT > G.timer) {
            G.mode = 1;
            G.modeT = 0.f;
            vec2 nt = G.anchor.xy();
            for (int tries = 0; tries < 10; tries++) {
                float ang = frand() * kTwoPi;
                nt = G.anchor.xy() + vec2(cosf(ang), sinf(ang)) * frange(150.f, 450.f);
                float sd = g.map->coastDistance(nt.x, nt.y);
                if (!frigate || (sd > -300.f && sd < 60.f)) break;
            }
            G.threat = vec3(nt, g.map->heightAt(nt.x, nt.y));   // next thermal
        }
    } else {
        G.target = lerp(G.target, G.threat, Saturate(dt * 0.04f));
        if (length(G.target.xy() - G.threat.xy()) < 40.f || G.modeT > 60.f) {
            G.mode = 0;
            G.modeT = 0.f;
            G.timer = frange(60.f, 150.f);
        }
    }
    float dirSign = (G.uid & 1) ? 1.f : -1.f;
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        a.life += dt;
        if (a.state == ST_FALL || a.state == ST_DEAD) {
            fallStep(g, a, dt);
            continue;
        }
        a.state = ST_FLY;
        float R = frigate ? Lerp(22.f, 55.f, birdHash(a, 21)) : Lerp(28.f, 65.f, birdHash(a, 21));
        float alt = frigate ? Lerp(30.f, 95.f, birdHash(a, 22)) + 12.f * sinf(a.life * 0.03f + birdHash(a, 23) * 6.f)
                            : Lerp(55.f, 150.f, birdHash(a, 22)) + 20.f * sinf(a.life * 0.02f + birdHash(a, 23) * 6.f);
        float ground = Max(g.map->heightAt(G.target.x, G.target.y), 0.f);
        vec2 rel = a.pos.xy() - G.target.xy();
        float ang = atan2f(rel.y, rel.x) + dirSign * 0.35f;
        vec3 want = vec3(G.target.xy() + vec2(cosf(ang), sinf(ang)) * R, ground + alt);
        if (G.mode == 1) want = vec3(G.target.xy() + vec2(cosf(birdHash(a, 24) * kTwoPi), sinf(birdHash(a, 24) * kTwoPi)) * 15.f, ground + alt);
        vec3 steer = (normalize(want - a.pos) * fp.cruise - a.vel) * 0.9f;
        flyStep(g, a, steer, dt, fp, 25.f, fp.minSpd);
        a.ba.soar = 1.f;
        float every = frigate ? 41.f : 23.f;
        if (a.life - floorf(a.life / every) * every < 1.2f) a.ba.flapAmp = 0.5f;   // an occasional lazy flap
        if (frigate) a.ba.tail = 0.15f + 0.45f * (0.5f + 0.5f * sinf(a.life * 0.3f + birdHash(a, 25) * 6.f));   // the forked tail scissors as it steers
        // scatter upwards from gunfire
        bool gun = false;
        vec3 from;
        if (scareLevel(a.pos, 1.f, &from, &gun) < 1.f && gun) G.target += vec3(normalize(G.target.xy() - from.xy() + vec2(0.01f, 0.f)) * 40.f * dt, 0.f);
    }
}

// ---- Sanderlings ----------------------------------------------------------------------------------------------------
// The water's edge near p: follows the coast-distance gradient to where the sand meets the sea. edge = that point
// (terrain height), inlandN = unit normal pointing up the beach. False without a clear, gently sloping edge of open sand.
bool findShoreEdge(GameWorld& g, vec2 p, vec3& edge, vec2& inlandN) {
    const World::WorldMap& m = *g.map;
    const float e = 3.f;
    vec2 grad(m.coastDistance(p.x + e, p.y) - m.coastDistance(p.x - e, p.y), m.coastDistance(p.x, p.y + e) - m.coastDistance(p.x, p.y - e));
    if (length2(grad) < 1e-4f) return false;
    vec2 n = normalize(grad);
    float sWet = 1e9f, sDry = 1e9f;   // a wet sample seaward and the first dry one up the beach
    for (float s = -30.f; s <= 30.f; s += 2.f) {
        vec2 q = p + n * s;
        if (waterDepth(m, q.x, q.y) > 0.02f) sWet = s;
        else if (sWet < 1e8f) {
            sDry = s;
            break;
        }
    }
    if (sWet > 1e8f || sDry > 1e8f) return false;
    for (int it = 0; it < 8; it++) {
        float mid = 0.5f * (sWet + sDry);
        vec2 q = p + n * mid;
        if (waterDepth(m, q.x, q.y) > 0.02f) sWet = mid;
        else sDry = mid;
    }
    vec2 q = p + n * sDry;
    vec2 up = q + n * 6.f;
    float rise = m.heightAt(up.x, up.y) - m.heightAt(q.x, q.y);
    if (rise > 1.8f || rise < 0.02f) return false;   // seawalls and flat mud are not beaches
    if (onRoadOrBuilding(g, q, 6.f, 4.f)) return false;
    edge = vec3(q, m.heightAt(q.x, q.y));
    inlandN = n;
    return true;
}

// How far up the beach the water reaches at this moment (m from the still-water edge): a slow surge up the sand, a
// quicker backwash, the waves arriving at an angle along the beach.
float swashLine(const Group& G, float along, float t) {
    float ph = t * (kTwoPi / 7.5f) + along * 0.11f + (float)(G.uid % 13u);
    float s = sinf(ph);
    return 0.4f + 1.2f * (s >= 0.f ? powf(s, 0.8f) : -powf(-s, 1.4f));
}

// Sanderlings: a tight little flock running up and down with the swash at the water's edge, probing the wet sand as
// the water drains away, sprinting ahead of every surge; the flock works its way along the beach and, when flushed,
// flies low over the surf to settle further along the shore.
void updateShorebirds(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    const FlyParams& fp = flyParams(G.sp);
    float now = (float)gW.time;
    G.modeT += dt;
    G.alarm = Max(0.f, G.alarm - dt);
    if (G.mode == 0) {   // drifting along the beach while feeding (heading holds the drift direction, +-1)
        vec2 T(G.shoreN.y, -G.shoreN.x);
        G.anchor += vec3(T * (G.heading * 0.12f * dt), 0.f);
        G.timer -= dt;
        if (G.timer <= 0.f) {
            G.timer = 4.f;
            vec3 e;
            vec2 n;
            if (findShoreEdge(g, G.anchor.xy(), e, n)) {
                G.anchor = e;
                G.shoreN = n;
            } else {
                G.heading = -G.heading;   // end of the sand: work back the other way
            }
        }
    }
    gatherFlyingBoids(G);
    BoidParams bp;
    bp.sepDist = 0.7f;
    bp.viewDist = 5.f;
    bool flush = false;
    vec3 from = G.threat;
    int airborne = 0;
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        a.life += dt;
        a.t += dt;
        a.standH = modelOf(a).legLen * a.scale;
        if (a.state == ST_FALL || a.state == ST_DEAD) {
            fallStep(g, a, dt);
            continue;
        }
        vec2 N = G.shoreN, T(N.y, -N.x);
        float along = (birdHash(a, 41) - 0.5f) * 9.f + 2.f * sinf(a.life * 0.05f + birdHash(a, 42) * 6.f);
        float behind = 0.25f + birdHash(a, 43) * 1.1f;
        switch (a.state) {
            case ST_IDLE: case ST_WALK: {
                a.think -= dt;
                if (a.think <= 0.f && G.mode == 0) {
                    a.think = 0.2f + frand() * 0.2f;
                    bool gun = false;
                    vec3 fr;
                    if (scareLevel(a.pos, 1.1f, &fr, &gun) < 1.f) {
                        flush = true;
                        from = fr;
                        if (gun) G.alarm = Max(G.alarm, 8.f);
                    }
                }
                float swash = swashLine(G, along, now), swashNext = swashLine(G, along, now + 0.3f);
                vec2 goal = G.anchor.xy() + T * along + N * (swash + behind);
                vec2 d = goal - a.pos.xy();
                float dist = length(d);
                float spd = dist > 0.2f ? Min(2.6f, dist * 2.5f) : 0.f;
                groundStep(g, a, dist > 0.01f ? d / dist * spd : vec2(0.f), dt, 16.f, 16.f, 0.04f, 1);
                float sp = length(a.vel.xy());
                a.state = sp > 0.1f ? ST_WALK : ST_IDLE;
                perchedAnim(a, dt, false);
                a.ba.walkAmt = Lerp(a.ba.walkAmt, Saturate(sp / 0.4f), expDecay(12.f, dt));
                a.ba.walk += dt * kTwoPi * (2.f + sp * 5.5f);   // the legs blur when they sprint
                bool draining = swashNext < swash;               // probe the wet sand behind the backwash
                float probe = sp < 0.15f && draining && sinf(a.life * 13.f + birdHash(a, 44) * 9.f) > 0.1f ? 1.f : 0.f;
                a.ba.peck = Lerp(a.ba.peck, probe, expDecay(18.f, dt));
                if (sp < 0.1f) a.yaw = approachAngle(a.yaw, yawOf(-N) + (birdHash(a, 45) - 0.5f), 4.f * dt);
                break;
            }
            case ST_TAKEOFF: case ST_FLY: {
                airborne++;
                if (a.t < 0.f) {   // staggered start
                    perchedAnim(a, dt, false);
                    break;
                }
                if (a.state == ST_TAKEOFF && a.t > 0.35f) a.state = ST_FLY;
                // low over the surf towards the new stretch of beach, a tight twinkling flock
                vec3 goal = vec3(G.target.xy() + T * (along * 0.6f) - N * 2.5f, G.target.z + 1.4f + 0.5f * sinf(a.life * 1.3f + birdHash(a, 46) * 6.f));
                vec3 steer = (normalize(goal - a.pos + vec3(0.001f, 0, 0)) * fp.cruise - a.vel) * 2.f;
                for (int q = 0; q < (int)gBoid.idx.size(); q++)
                    if (gBoid.idx[q] == ai) {
                        steer += boidSteer(gBoid.pos.data(), gBoid.vel.data(), (int)gBoid.pos.size(), q, bp) * 2.f;
                        break;
                    }
                flyStep(g, a, steer, dt, fp, 0.8f, fp.minSpd);
                a.ba.tail = 0.f;
                if (length(goal.xy() - a.pos.xy()) < 6.f) {
                    a.state = ST_LAND;
                    a.t = 0.f;
                }
                break;
            }
            case ST_LAND: {
                airborne++;
                vec2 gp = G.target.xy() + T * along + N * (swashLine(G, along, now) + behind);
                vec3 goal(gp, groundAt(g, gp.x, gp.y, G.target.z + 2.f) + a.standH);
                vec3 to = goal - a.pos;
                float dist = length(to);
                vec3 wantV = dist > 0.01f ? to / dist * Min(fp.cruise, dist * 1.5f + 0.8f) : vec3(0.f);
                FlyParams lp = fp;
                lp.accel *= 2.f;
                flyStep(g, a, (wantV - a.vel) * 4.f, dt, lp, 0.f, 0.3f);
                float fl = Saturate(1.f - dist / 3.f);
                a.ba.flare = fl;
                a.ba.tail = fl;
                a.ba.legs = fl;
                if (dist < 0.3f || a.t > 8.f) {
                    a.pos = goal;
                    a.vel = vec3(0.f);
                    a.state = ST_WALK;
                    a.t = 0.f;
                    a.pitch = a.roll = 0.f;
                }
                break;
            }
            default: a.state = ST_WALK; break;
        }
    }
    if (flush && G.mode == 0) {
        // settle 35-90 m further along the beach, away from the disturbance (the other way if there is no beach)
        vec2 N = G.shoreN, T(N.y, -N.x);
        float dir = dot(G.anchor.xy() - from.xy(), T) >= 0.f ? 1.f : -1.f;
        vec3 e = G.anchor;
        vec2 n = N;
        bool ok = false;
        for (int k = 0; k < 4 && !ok; k++) {
            float dd = (k < 2 ? dir : -dir) * frange(35.f, 90.f);
            ok = findShoreEdge(g, G.anchor.xy() + T * dd, e, n);
        }
        if (!ok) {   // nowhere else on this beach: a loop out over the surf and back to the same spot
            e = G.anchor;
            n = N;
        }
        G.target = e;
        G.anchor = e;
        G.shoreN = n;
        G.heading = dir;
        G.mode = 1;
        G.modeT = 0.f;
        for (int aj : G.members) {
            Animal& b = gW.animals[aj];
            if (b.used && (b.state == ST_IDLE || b.state == ST_WALK)) {
                b.state = ST_TAKEOFF;
                b.t = -frand() * 0.25f;
            }
        }
        sfx(Audio::SFX_WING_FLAP, G.members.empty() ? G.anchor : gW.animals[G.members[0]].pos, 0.6f, 1.6f);
        if (frand() < 0.7f) sfx(Audio::SFX_PARROT_SQUAWK, G.members.empty() ? G.anchor : gW.animals[G.members[0]].pos, 0.25f, 2.1f);   // soft twick calls
    } else if (G.mode == 1 && airborne == 0 && G.modeT > 1.f) {
        G.mode = 0;
        G.timer = 0.f;
    }
}

// ---- Cattle egrets ---------------------------------------------------------------------------------------------------
// Small white herons that keep company with grazing cattle: they walk at the cows' front feet snapping up the insects
// the cattle stir up, now and then ride on a back, hop along in short flights when the herd moves on, and flush
// together when something scares them, circling before they settle back among the herd.
const FlockCfg kCattleEgretCfg = {1.5f, 8.f, 10.f, 22.f, 5.f, 12.f, 6.f, 14.f, 1e9f, 1e9f, 6.f, 0.f, false, false, true, 0.6f};

void updateCattleEgrets(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    const FlyParams& fp = flyParams(G.sp);
    const Group* H = nullptr;
    if (G.host >= 0 && G.host < (int)gW.groups.size() && gW.groups[G.host].used && gW.groups[G.host].uid == G.hostUid) H = &gW.groups[G.host];
    G.alarm = Max(0.f, G.alarm - dt);
    G.modeT += dt;
    if (H) {   // follow the herd: its centroid is the flock's home, landing spots lie in front of the cows
        vec3 c(0.f);
        int n = 0;
        for (int ci : H->members) {
            const Animal& cw = gW.animals[ci];
            if (cw.used && cw.state != ST_DEAD && cw.state != ST_FALL) {
                c += cw.pos;
                n++;
            }
        }
        if (n) G.anchor = c / (float)n;
    } else if (!g.inCameraView(G.anchor + vec3(0, 0, 1.f), 8.f)) {   // the herd is gone: leave once nobody is looking
        releaseGroup(gi);
        return;
    }
    G.target = G.anchor;
    G.timer -= dt;
    if (G.timer <= 0.f) {
        G.timer = 2.f;
        G.perches.clear();
        if (H)
            for (int ci : H->members) {
                const Animal& cw = gW.animals[ci];
                if (!cw.used || cw.state == ST_DEAD) continue;
                vec2 f = dirOf(cw.yaw);
                vec2 p = cw.pos.xy() + f * (1.4f * cw.scale + frand()) + vec2(f.y, -f.x) * frange(-1.2f, 1.2f);
                G.perches.push_back({vec3(p, groundAt(g, p.x, p.y, cw.pos.z + 2.f)), 0});
            }
        if (G.perches.empty()) {
            vec2 p = G.anchor.xy() + gW.rng.inCircle() * 6.f;
            G.perches.push_back({vec3(p, groundAt(g, p.x, p.y, G.anchor.z + 2.f)), 0});
        }
    }
    FlockCtx fc;
    fc.cfg = kCattleEgretCfg;
    fc.fp = &fp;
    fc.bp.sepDist = 1.5f;
    fc.bp.viewDist = 8.f;
    fc.baseZ = G.anchor.z;
    gatherFlyingBoids(G);
    int flying = 0;
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        a.standH = modelOf(a).legLen * a.scale;
        if (a.state == ST_FALL || a.state == ST_DEAD) {
            a.life += dt;
            fallStep(g, a, dt);
            continue;
        }
        bool inAir = a.state == ST_TAKEOFF || a.state == ST_FLY || a.state == ST_LAND;
        if (inAir && a.aux2 < 0.5f) {   // flushed: circling with the flock (shared flock code)
            flying++;
            flockBirdStep(g, gi, ai, dt, fc);
            continue;
        }
        a.life += dt;
        a.t += dt;
        // the cow this egret keeps company with
        const Animal* cow = nullptr;
        if (H && a.target >= 0 && a.target < (int)gW.animals.size()) {
            const Animal& c = gW.animals[a.target];
            if (c.used && c.uid == a.targetUid && c.group == G.host && c.state != ST_DEAD && c.state != ST_FALL) cow = &c;
        }
        if (!cow && H && !H->members.empty()) {
            int ci = H->members[(size_t)(gW.rng.next() % H->members.size())];
            const Animal& c = gW.animals[ci];
            if (c.used && c.state != ST_DEAD && c.state != ST_FALL) {
                a.target = ci;
                a.targetUid = c.uid;
                cow = &c;
                a.aux = frand() < 0.15f ? 1.f : 0.f;   // some want a ride
            }
        }
        bool fleeingCow = cow && (cow->state == ST_FLEE || length(cow->vel.xy()) > 2.5f);
        if (a.aux > 0.5f && (fleeingCow || G.alarm > 0.f)) a.aux = 0.f;
        vec3 goal = a.pos;
        if (cow) {
            vec2 f = dirOf(cow->yaw), side(f.y, -f.x);
            if (a.aux > 0.5f) {   // on the back, between the withers and the hips
                vec2 p = cow->pos.xy() + f * ((birdHash(a, 32) - 0.6f) * 0.5f * cow->scale);
                goal = vec3(p, cow->pos.z + modelOf(*cow).legLen * cow->scale * 0.97f + a.standH);
            } else {
                float len = speciesInfo(SP_COW).length * cow->scale;
                float sgn = birdHash(a, 31) < 0.5f ? -1.f : 1.f;
                vec2 p = cow->pos.xy() + f * (len * 0.45f + 0.4f + 0.6f * birdHash(a, 33)) + side * (sgn * (0.5f + 0.8f * birdHash(a, 34)));
                goal = vec3(p, groundAt(g, p.x, p.y, cow->pos.z + 2.f) + a.standH);
            }
        }
        if (inAir) {   // a short hop to catch up with the cow (or onto its back)
            flying++;
            a.goal = goal;
            if (a.state == ST_TAKEOFF && a.t > 0.25f) a.state = ST_FLY;
            vec3 to = goal - a.pos;
            float d = length(to);
            vec3 wantV = d > 0.01f ? to / d * Min(fp.cruise * 0.7f, d * 1.6f + 0.8f) : vec3(0.f);
            if (d > 3.f) wantV.z += 1.2f;   // a low arc
            FlyParams lp = fp;
            lp.accel *= 2.f;
            flyStep(g, a, (wantV - a.vel) * 3.f, dt, lp, 0.f, 0.3f);
            float fl = Saturate(1.f - d / 2.5f);
            a.ba.flare = fl;
            a.ba.legs = fl;
            a.ba.tail = fl;
            if (fl > 0.2f) a.ba.flapAmp = Lerp(a.ba.flapAmp, 1.1f, expDecay(6.f, dt));
            if (d < 0.3f || a.t > 10.f) {
                a.pos = goal;
                a.vel = vec3(0.f);
                a.state = ST_IDLE;
                a.t = 0.f;
                a.aux2 = 0.f;
                a.pitch = a.roll = 0.f;
                if (a.aux > 0.5f) a.aux = 2.f;   // settled on the back
            }
            continue;
        }
        // on the ground (or riding)
        if (a.perch >= 0) releasePerch(a);
        a.think -= dt;
        if (a.think <= 0.f) {
            a.think = 0.2f + frand() * 0.2f;
            bool gun = false;
            vec3 fr;
            if (scareLevel(a.pos, 0.9f, &fr, &gun) < 1.f) {   // the whole group flushes and circles
                G.alarm = gun ? 15.f : 6.f;
                G.threat = fr;
                G.mode = 1;
                G.modeT = 0.f;
                for (int aj : G.members) {
                    Animal& b = gW.animals[aj];
                    if (b.used && (b.state == ST_IDLE || b.state == ST_WALK)) {
                        b.aux2 = 0.f;
                        if (b.aux > 0.5f) b.aux = 0.f;
                        startTakeoff(b, frand() * 0.4f);
                    }
                }
                sfx(Audio::SFX_WING_FLAP, a.pos, 0.8f);
                continue;
            }
        }
        if (a.aux > 1.5f && cow) {   // riding: carried along on the back
            a.pos = goal;
            a.vel = cow->vel;
            a.yaw = approachAngle(a.yaw, cow->yaw + (birdHash(a, 35) - 0.5f) * 1.2f, 2.f * dt);
            a.state = ST_IDLE;
            perchedAnim(a, dt, false);
            a.ba.neck = Lerp(a.ba.neck, -0.45f, expDecay(3.f, dt));
            if (frand() < dt * 0.02f) a.aux = 0.f;   // hop down after a while
            continue;
        }
        vec2 d = goal.xy() - a.pos.xy();
        float dist = length(d);
        bool hop = dist > 7.f || (a.aux > 0.5f && dist < 3.f);   // too far behind, or right next to the cow it wants to ride
        if (hop && cow) {
            a.aux2 = 1.f;
            a.state = ST_TAKEOFF;
            a.t = 0.f;
            a.vel = vec3(0.f, 0.f, 1.5f);
            continue;
        }
        float spd = dist > 0.3f ? Min(0.9f, dist * 1.2f) : 0.f;
        groundStep(g, a, dist > 0.01f ? d / dist * spd : vec2(0.f), dt, 3.f, 6.f, 0.06f, 0);
        float sp = length(a.vel.xy());
        a.state = sp > 0.08f ? ST_WALK : ST_IDLE;
        perchedAnim(a, dt, false);
        a.ba.walkAmt = Lerp(a.ba.walkAmt, Saturate(sp / 0.4f), expDecay(8.f, dt));
        a.ba.walk += dt * kTwoPi * (1.2f + sp * 2.f);
        float strike = sinf(a.life * 2.3f + birdHash(a, 36) * 9.f) > 0.82f ? 1.f : 0.f;   // quick jabs at insects
        a.ba.peck = Lerp(a.ba.peck, strike, expDecay(14.f, dt));
        a.ba.neck = Lerp(a.ba.neck, -0.35f * (1.f - a.ba.peck), expDecay(3.f, dt));   // the hunched, short-necked look
        if (sp < 0.08f && cow) a.yaw = approachAngle(a.yaw, yawOf(cow->pos.xy() - a.pos.xy()), 3.f * dt);
    }
    if (G.mode == 2 && flying == 0) {
        G.mode = 0;
        G.modeT = 0.f;
    }
}

// Parakeets: a loud flock flying from one tree crown to the next, resting hidden in the fronds for a while.
void updateParrots(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    const FlyParams& fp = flyParams(SP_PARROT);
    G.modeT += dt;
    G.alarm = Max(0.f, G.alarm - dt);
    gatherFlyingBoids(G);
    BoidParams bp;
    bp.sepDist = 1.3f;
    bp.viewDist = 9.f;
    bp.wSep = 3.f;
    bp.wAli = 1.2f;
    bp.wCoh = 0.4f;
    auto pickTree = [&]() {
        std::vector<int> cands;
        for (int i = 0; i < (int)G.perches.size(); i++) {
            float d = length(G.perches[i].pos.xy() - G.target.xy());
            if (G.perches[i].kind == 4 && d > 30.f) cands.push_back(i);
        }
        if (cands.empty()) return false;
        G.target = G.perches[cands[gW.rng.next() % cands.size()]].pos;
        return true;
    };
    if (G.mode == 1 && (G.modeT > G.timer || G.alarm > 0.f)) {   // leave the tree
        G.mode = 0;
        G.modeT = 0.f;
        if (!pickTree()) G.target = G.anchor + vec3(gW.rng.inCircle() * 80.f, 20.f);
        for (int ai : G.members) {
            Animal& a = gW.animals[ai];
            if (a.used && a.state == ST_IDLE) startTakeoff(a, frand() * 0.4f);
        }
        sfx(Audio::SFX_PARROT_SQUAWK, G.target, 1.f);
    }
    vec3 centroid(0.f);
    int nf = 0;
    for (int ai : G.members) {
        const Animal& a = gW.animals[ai];
        if (a.used && a.state == ST_FLY) {
            centroid += a.pos;
            nf++;
        }
    }
    if (nf) centroid = centroid / (float)nf;
    bool arrive = G.mode == 0 && nf > 0 && length(centroid - G.target) < 12.f;
    int slot = 0;
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        a.life += dt;
        a.t += dt;
        a.standH = modelOf(a).legLen * a.scale;
        if (a.state == ST_FALL || a.state == ST_DEAD) {
            fallStep(g, a, dt);
            continue;
        }
        switch (a.state) {
            case ST_IDLE:
                perchedAnim(a, dt, false);
                a.ba.headYaw = 0.8f * sinf(a.life * 1.7f + birdHash(a, 3) * 5.f);
                a.think -= dt;
                if (a.think <= 0.f) {
                    a.think = 0.3f;
                    bool gun = false;
                    vec3 from;
                    if (scareLevel(a.pos, 1.2f, &from, &gun) < 1.f) {
                        G.alarm = 8.f;
                        G.threat = from;
                    }
                }
                break;
            case ST_TAKEOFF:
                if (a.t < 0.f) break;
                a.vel = lerp(a.vel, vec3(normalize(G.target.xy() - a.pos.xy() + vec2(0.01f, 0.f)) * 6.f, 3.f), expDecay(4.f, dt));
                a.pos += a.vel * dt;
                a.yaw = approachAngle(a.yaw, yawOf(a.vel.xy()), 8.f * dt);
                a.ba.fold = Max(0.f, a.ba.fold - dt * 6.f);
                a.ba.legs = Max(0.f, a.ba.legs - dt * 3.f);
                a.ba.flapAmp = 1.2f;
                a.ba.flap += dt * kTwoPi * fp.flapFreq;
                if (a.t > 0.5f) {
                    a.state = ST_FLY;
                    a.t = 0.f;
                }
                break;
            case ST_FLY: {
                int bi = -1;
                for (int q = 0; q < (int)gBoid.idx.size(); q++)
                    if (gBoid.idx[q] == ai) bi = q;
                vec3 steer = bi >= 0 ? boidSteer(gBoid.pos.data(), gBoid.vel.data(), (int)gBoid.pos.size(), bi, bp) * 1.5f : vec3(0.f);
                vec3 to = G.target + vec3(0, 0, 4.f) - a.pos;
                steer += (normalize(to) * fp.cruise - a.vel) * 1.3f;
                flyStep(g, a, steer, dt, fp, 3.f, fp.minSpd);
                if (arrive) {
                    a.state = ST_LAND;
                    a.t = 0.f;
                    float ang = birdHash(a, 31) * kTwoPi;
                    a.goal = G.target + vec3(cosf(ang) * 1.8f, sinf(ang) * 1.8f, frange(-1.2f, 0.4f) + a.standH);
                }
                break;
            }
            case ST_LAND: {
                vec3 to = a.goal - a.pos;
                float dist = length(to);
                FlyParams lp = fp;
                lp.accel *= 2.f;
                flyStep(g, a, ((dist > 0.01f ? to / dist : vec3(0.f)) * Min(fp.cruise, dist + 1.f) - a.vel) * 4.f, dt, lp, 0.f, 0.3f);
                a.ba.flare = Saturate(1.f - dist / 3.f);
                a.ba.legs = a.ba.flare;
                if (dist < 0.3f || a.t > 8.f) {
                    a.pos = a.goal;
                    a.vel = vec3(0.f);
                    a.state = ST_IDLE;
                    a.t = 0.f;
                    a.pitch = a.roll = 0.f;
                }
                break;
            }
            default: a.state = ST_FLY; break;
        }
        slot++;
    }
    if (G.mode == 0) {
        bool allPerched = true;
        for (int ai : G.members) {
            const Animal& a = gW.animals[ai];
            if (a.used && a.state != ST_IDLE && a.state != ST_DEAD && a.state != ST_FALL) allPerched = false;
        }
        if (allPerched) {
            G.mode = 1;
            G.modeT = 0.f;
            G.timer = frange(15.f, 40.f);
        }
    }
    G.soundT -= dt;
    if (G.soundT <= 0.f && !G.members.empty()) {
        G.soundT = G.mode == 0 ? frange(0.4f, 1.2f) : frange(1.5f, 4.f);
        int ai = G.members[(size_t)(gW.rng.next() % G.members.size())];
        const Animal& a = gW.animals[ai];
        if (a.used && a.state != ST_DEAD && length2(a.pos - gW.cam) < 150.f * 150.f) sfx(Audio::SFX_PARROT_SQUAWK, a.pos, 0.8f);
    }
}

}  // namespace wild_detail

// ==================================================================================================================
// Reptiles and marine life
namespace wild_detail {

// Nearest open water (depth >= minDepth) from a land point, along 12 rays.
bool findWater(GameWorld& g, vec2 from, float maxDist, float minDepth, vec3& out) {
    float best = 1e9f;
    for (int k = 0; k < 12; k++) {
        vec2 d = dirOf((float)k / 12.f * kTwoPi);
        for (float r = 1.f; r <= maxDist; r += 1.f) {
            vec2 p = from + d * r;
            if (waterDepth(*g.map, p.x, p.y) >= minDepth) {
                if (r < best) {
                    best = r;
                    out = vec3(p + d * 1.5f, g.map->waterAt(p.x, p.y));
                }
                break;
            }
        }
    }
    return best < 1e8f;
}

// A basking spot on a bank next to the water (away from roads and buildings).
bool findBank(GameWorld& g, vec2 from, float maxDist, vec3& out) {
    int start = (int)(gW.rng.next() % 12);
    for (int kk = 0; kk < 12; kk++) {
        vec2 d = dirOf((float)((kk + start) % 12) / 12.f * kTwoPi);
        for (float r = 2.f; r <= maxDist; r += 1.f) {
            vec2 p = from + d * r;
            if (waterDepth(*g.map, p.x, p.y) < -0.05f) {
                vec2 q = p + d * 1.2f;
                if (waterDepth(*g.map, q.x, q.y) > -0.05f) break;
                if (onRoadOrBuilding(g, q, 12.f, 3.f)) break;
                if (g.map->normalAt(q.x, q.y).z < 0.85f) break;
                out = vec3(q, groundAt(g, q.x, q.y, g.map->heightAt(q.x, q.y) + 2.f));
                return true;
            }
        }
    }
    return false;
}

inline bool pedOk(const GameWorld& g, int id, u32 uid) {
    return id >= 0 && id < (int)g.peds.size() && g.peds[id].used && g.peds[id].uid == uid && g.peds[id].health > 0.f;
}

vec3 pedBoneWorld(const GameWorld& g, const Ped& p, int bone) {
    vec3 b = p.bones[bone].c[3].xyz();
    if (p.ragdoll) return b;
    return p.pos.toVec3() + rotate(quatAxisAngle(vec3(0, 0, 1), p.yaw), b);
}

void gatorBite(GameWorld& g, Animal& a, int ped, bool drag) {
    Ped& p = g.peds[ped];
    vec3 dir = p.pos.toVec3() - a.pos;
    dir.z = 0.f;
    dir = length2(dir) > 1e-4f ? normalize(dir) : vec3(dirOf(a.yaw), 0.f);
    sfx(Audio::SFX_JAW_SNAP, worldPoint(a, modelOf(a).mouth), 1.f);
    if (drag) return;
    g.damagePed(ped, p.isPlayer ? 34.f : 60.f, DMG_MELEE, -1, dir);
    spawnFx(FX_BLOOD, dvec3(g.pedChestPos(p) - vec3(0, 0, 0.6f)), dir, 4, 1.f);
    if (g.peds[ped].used && g.peds[ped].health > 0.f && g.peds[ped].state == PS_ONFOOT) g.knockDown(ped, dir * 320.f + vec3(0, 0, 60.f));
    if (p.isPlayer) {
        g.rumble(0.9f, 0.8f);
        g.rig.shake = Max(g.rig.shake, 0.6f);
    }
}

// Alligators: bask on banks (jaws agape), cruise with only the eyes and back above the water, hiss and lunge at people
// who come too close, drag swimmers under with a death roll, slide into the water from vehicles and gunfire.
void updateGators(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        a.life += dt;
        a.t += dt;
        a.ra.t += dt;
        a.cooldown -= dt;
        const ModelData& md = modelOf(a);
        float L = speciesInfo(a.sp).length * a.scale;
        if (a.state == ST_FALL || a.state == ST_DEAD) {
            fallStep(g, a, dt);
            a.ra.roll = 0.f;
            continue;
        }
        float depth = waterDepth(*g.map, a.pos.x, a.pos.y);
        bool inWater = depth > 0.45f * a.scale;
        vec3 head = worldPoint(a, md.headTip);
        Ped* pl = g.playerPed();
        // ---- perception
        a.think -= dt;
        if (a.think <= 0.f && a.state != ST_ATTACK) {
            a.think = 0.25f + frand() * 0.15f;
            bool gun = false;
            const Threat* th = nullptr;
            vec3 from;
            float sc = scareLevel(a.pos, 1.f, &from, &gun, &th);
            bool vehicle = th && th->kind == 1 && th->speed > 2.f;
            if ((gun && sc < 0.5f) || (vehicle && sc < 0.6f) || a.health < speciesInfo(a.sp).hp * 0.5f) {
                if (a.state != ST_FLEE) {
                    a.state = ST_FLEE;
                    a.t = 0.f;
                    a.goal = from;
                    if (!inWater) sfx(Audio::SFX_GATOR_HISS, head, 0.9f);
                }
            } else if (pl && pl->health > 0.f && a.cooldown <= 0.f) {
                vec3 pp = pl->pos.toVec3();
                float dHead = length(pp - head);
                bool playerSwim = pl->state == PS_SWIM;
                bool onFoot = gW.playerOnFoot;
                vec2 toP = pp.xy() - a.pos.xy();
                float facing = cosf(wrapA(yawOf(toP) - a.yaw));
                if ((onFoot || playerSwim) && dHead < (playerSwim ? 2.2f : 3.2f) * Max(a.scale, 0.7f) && facing > 0.3f) {
                    a.state = ST_ATTACK;
                    a.t = 0.f;
                    a.target = g.player;
                    a.targetUid = pl->uid;
                    a.goal = pp;
                } else if ((onFoot || playerSwim) && dHead < 9.f && a.state != ST_ALERT && a.state != ST_FLEE) {
                    a.state = ST_ALERT;
                    a.t = 0.f;
                    a.goal = pp;
                    if (!inWater || playerSwim) sfx(Audio::SFX_GATOR_HISS, head, 1.f);
                } else if (playerSwim && dHead < 16.f && inWater && a.state != ST_FLEE) {
                    a.state = ST_WALK;   // stalking a swimmer
                    a.goal = pp;
                    a.t = 0.f;
                }
            }
        }
        // ---- behaviour
        float speed = 0.f;
        switch (a.state) {
            case ST_REST: case ST_IDLE: {   // basking on land / lurking in the water
                a.ra.jaw = Lerp(a.ra.jaw, (!inWater && sinf(a.life * 0.07f + (float)(a.uid & 31)) > 0.2f) ? 0.55f : 0.f, expDecay(1.f, dt));
                a.ra.hiss = Max(0.f, a.ra.hiss - dt);
                a.timer -= dt;
                if (a.timer <= 0.f) {
                    vec3 spot;
                    if (inWater && findBank(g, a.pos.xy(), 25.f, spot) && length(spot.xy() - G.anchor.xy()) < 60.f) {
                        a.goal = spot;
                        a.state = ST_WALK;
                    } else if (!inWater && findWater(g, a.pos.xy(), 20.f, 0.9f, spot)) {
                        a.goal = spot + vec3(normalize(spot.xy() - a.pos.xy()) * frange(4.f, 12.f), 0.f);
                        a.state = ST_SWIM;
                    } else a.timer = frange(20.f, 40.f);
                    a.t = 0.f;
                }
                if (frand() < dt / 200.f && length2(a.pos - gW.cam) < 150.f * 150.f) sfx(Audio::SFX_GATOR_BELLOW, head, 1.f);
                break;
            }
            case ST_SWIM: case ST_WALK: {   // travel to a.goal (swimming or walking)
                vec2 d = a.goal.xy() - a.pos.xy();
                float dist = length(d);
                speed = inWater ? 0.9f : 0.55f;
                if (a.state == ST_WALK && pl && pl->state == PS_SWIM && a.target < 0) speed = 1.2f;
                if (dist < 1.f || a.t > 60.f) {
                    a.state = inWater ? ST_IDLE : ST_REST;
                    a.timer = inWater ? frange(30.f, 90.f) : frange(60.f, 160.f);
                    a.t = 0.f;
                    speed = 0.f;
                } else if (inWater) {
                    vec3 want = vec3(d / dist * speed, 0.f);
                    swimStep(g, a, want, dt, 1.5f, 1.2f, 0.f, 0.3f * a.scale);
                } else {
                    groundStep(g, a, d / dist * speed, dt, 2.f, 1.5f, 0.35f * a.scale, 2);
                }
                break;
            }
            case ST_ALERT: {   // face the intruder, hiss with the jaws open
                vec2 d = a.goal.xy() - a.pos.xy();
                a.yaw = approachAngle(a.yaw, yawOf(d), 1.6f * dt);
                a.ra.hiss = Min(1.f, a.ra.hiss + dt * 3.f);
                a.ra.jaw = Lerp(a.ra.jaw, 0.75f, expDecay(4.f, dt));
                if (a.t > 4.f) {
                    a.state = inWater ? ST_IDLE : ST_REST;
                    a.timer = frange(10.f, 30.f);
                    a.t = 0.f;
                }
                break;
            }
            case ST_ATTACK: {   // lunge, snap; swimmers are dragged under with a death roll
                bool ok = pedOk(g, a.target, a.targetUid);
                if (!ok) {
                    a.state = ST_IDLE;
                    break;
                }
                Ped& p = g.peds[a.target];
                vec3 pp = p.pos.toVec3();
                vec2 d = pp.xy() - a.pos.xy();
                float dist = length(d);
                if (a.t < 0.6f) {   // lunge
                    a.yaw = approachAngle(a.yaw, yawOf(d), 4.f * dt);
                    speed = Lerp(0.f, 4.5f, Saturate(a.t * 5.f));
                    a.ra.jaw = Lerp(a.ra.jaw, 1.f, expDecay(10.f, dt));
                    if (inWater) swimStep(g, a, vec3(dirOf(a.yaw) * speed, 0.f), dt, 12.f, 3.f, 0.f, 0.3f * a.scale);
                    else groundStep(g, a, dirOf(a.yaw) * speed, dt, 14.f, 4.f, 0.35f * a.scale, 2);
                    float dh = length(pp + vec3(0, 0, 0.5f) - worldPoint(a, md.headTip));
                    if (dh < 1.25f * Max(a.scale, 0.8f)) {
                        a.ra.jaw = 0.f;
                        if (p.state == PS_SWIM) {
                            a.state = ST_ATTACK;
                            a.t = 0.61f;   // drag phase
                            gatorBite(g, a, a.target, true);
                            a.timer = 2.4f;
                        } else {
                            gatorBite(g, a, a.target, false);
                            a.cooldown = 4.f;
                            a.state = ST_ALERT;
                            a.t = 1.f;
                        }
                    } else if (a.t + dt >= 0.6f) {
                        sfx(Audio::SFX_JAW_SNAP, worldPoint(a, md.mouth), 0.9f);
                        a.ra.jaw = 0.f;
                        a.cooldown = 3.f;
                        a.state = ST_ALERT;
                        a.t = 1.5f;
                    }
                } else {   // drag: the swimmer is pulled along and rolled for ~2 seconds
                    a.timer -= dt;
                    a.ra.roll += dt * 9.f;
                    a.ra.jaw = 0.15f;
                    vec3 away = normalize(vec3(a.pos.xy() - G.anchor.xy() + vec2(0.01f, 0.f), 0.f));
                    swimStep(g, a, away * 1.2f, dt, 3.f, 1.f, 0.6f, 0.3f * a.scale);
                    vec3 grip = worldPoint(a, md.headTip);
                    p.pos = dvec3(lerp(pp, vec3(grip.x, grip.y, pp.z), Saturate(dt * 6.f)));
                    p.vel = a.vel;
                    if (p.isPlayer) {
                        g.rumble(0.7f, 0.5f);
                        g.rig.shake = Max(g.rig.shake, 0.45f);
                    }
                    g.damagePed(a.target, 13.f * dt, DMG_MELEE, -1, away);
                    if (frand() < dt * 4.f) spawnFx(FX_WATER_SPLASH, dvec3(grip), vec3(0, 0, 1), 3, 0.8f);
                    if (a.timer <= 0.f || !inWater) {
                        a.ra.roll = 0.f;
                        a.cooldown = 8.f;
                        a.state = ST_FLEE;
                        a.t = 0.f;
                        a.goal = pp;
                    }
                }
                break;
            }
            case ST_FLEE: {   // into the water and away, then submerge
                if (!inWater) {
                    vec3 w;
                    if (a.t < 0.1f && findWater(g, a.pos.xy(), 25.f, 0.9f, w)) a.goal = w;
                    vec2 d = a.goal.xy() - a.pos.xy();
                    float dist = length(d);
                    speed = 2.6f;
                    if (dist > 0.3f) groundStep(g, a, d / dist * speed, dt, 8.f, 2.5f, 0.35f * a.scale, 2);
                    float depth2 = waterDepth(*g.map, a.pos.x, a.pos.y);
                    if (depth2 > 0.3f && a.t > 0.3f) {
                        spawnFx(FX_WATER_SPLASH, dvec3(a.pos), vec3(0, 0, 1), 8, 1.3f);
                        sfx(Audio::SFX_SPLASH_BIG, a.pos, 1.f, 0.8f);
                    }
                    if (a.t > 12.f) {
                        a.state = ST_REST;
                        a.timer = 20.f;
                    }
                } else {
                    vec2 away = a.pos.xy() - a.goal.xy();
                    away = length2(away) > 0.01f ? normalize(away) : dirOf(a.yaw);
                    swimStep(g, a, vec3(away * 1.6f, 0.f), dt, 3.f, 1.2f, 1.2f, 0.3f * a.scale);
                    speed = 1.6f;
                    if (a.t > 8.f) {
                        a.state = ST_IDLE;
                        a.timer = frange(20.f, 50.f);
                        a.t = 0.f;
                    }
                }
                break;
            }
            default: a.state = inWater ? ST_IDLE : ST_REST; break;
        }
        // water: float with eyes and back at the surface (lower when submerging after a scare)
        float wz;
        if (waterAt(a.pos.x, a.pos.y, wz) && waterDepth(*g.map, a.pos.x, a.pos.y) > 0.45f * a.scale) {
            float sink = (a.state == ST_FLEE && a.t > 2.f) ? 0.5f : 0.f;
            float zWant = wz - (0.4f + sink) * a.scale;
            float bottom = g.map->heightAt(a.pos.x, a.pos.y);
            a.pos.z = Lerp(a.pos.z, Max(zWant, bottom), expDecay(3.f, dt));
            a.ra.swim = Min(1.f, a.ra.swim + dt * 2.f);
            a.ra.swimPhase += dt * (1.2f + speed * 2.f);
            a.pitch = Lerp(a.pitch, 0.f, expDecay(3.f, dt));
            a.roll = Lerp(a.roll, 0.f, expDecay(3.f, dt));
        } else {
            a.ra.swim = Max(0.f, a.ra.swim - dt * 2.f);
            if (a.state == ST_REST || a.state == ST_IDLE) {
                a.groundT -= dt;
                if (a.groundT <= 0.f) {
                    a.groundT = 0.5f;
                    a.groundZ = groundAt(g, a.pos.x, a.pos.y, a.pos.z + 1.f, &a.groundN);
                }
                a.pos.z = Lerp(a.pos.z, a.groundZ, expDecay(6.f, dt));
            }
        }
        a.ra.speed = Lerp(a.ra.speed, a.ra.swim > 0.5f ? 0.f : speed, expDecay(6.f, dt));
        a.ra.phase += dt * reptileCycleRate(md, a.ra.speed * (1.f / Max(a.scale, 0.3f)));
        a.ra.lift = Lerp(a.ra.lift, (a.state == ST_WALK || a.state == ST_FLEE || a.state == ST_ATTACK) && a.ra.swim < 0.5f ? 1.f : 0.f, expDecay(2.f, dt));
        a.ra.turn = 0.f;
        (void)L;
    }
}

// Iguanas: basking on seawalls; they sprint to the edge and drop into the water when someone comes close.
void updateIguanas(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        a.life += dt;
        a.t += dt;
        a.ra.t += dt;
        if (a.state == ST_FALL || a.state == ST_DEAD) {
            fallStep(g, a, dt);
            continue;
        }
        float speed = 0.f;
        a.think -= dt;
        if (a.think <= 0.f && a.state != ST_FLEE && a.state != ST_SWIM) {
            a.think = 0.3f;
            vec3 from;
            if (scareLevel(a.pos, 0.6f, &from) < 1.f) {
                a.state = ST_FLEE;
                a.t = 0.f;
                vec3 w;
                a.goal = findWater(g, a.pos.xy(), 15.f, 0.4f, w) ? w : a.pos + vec3(normalize(a.pos.xy() - from.xy() + vec2(0.01f, 0.f)) * 10.f, 0.f);
            }
        }
        switch (a.state) {
            case ST_FLEE: {
                vec2 d = a.goal.xy() - a.pos.xy();
                float dist = length(d);
                speed = 3.2f;
                if (dist > 0.2f) groundStep(g, a, d / dist * speed, dt, 20.f, 8.f, 0.05f, 2);
                if (waterDepth(*g.map, a.pos.x, a.pos.y) > 0.25f) {
                    spawnFx(FX_WATER_SPLASH, dvec3(a.pos), vec3(0, 0, 1), 3, 0.5f);
                    sfx(Audio::SFX_SPLASH_SMALL, a.pos, 0.8f);
                    a.state = ST_SWIM;
                    a.t = 0.f;
                    a.goal = a.pos + vec3(normalize(d + vec2(0.01f, 0.f)) * 12.f, 0.f);
                }
                if (a.t > 8.f) a.state = ST_REST;
                break;
            }
            case ST_SWIM: {
                vec2 d = a.goal.xy() - a.pos.xy();
                swimStep(g, a, vec3(normalize(d + vec2(0.01f, 0.f)) * 0.8f, 0.f), dt, 2.f, 2.f, 0.1f, 0.05f);
                float wz;
                if (waterAt(a.pos.x, a.pos.y, wz)) a.pos.z = Lerp(a.pos.z, wz - 0.06f, expDecay(3.f, dt));
                a.ra.swim = 1.f;
                a.ra.swimPhase += dt * 6.f;
                break;
            }
            default: {   // basking: the odd head bob
                a.state = ST_REST;
                a.ra.headPitch = (fmodf(a.life, 9.f) < 0.8f) ? 0.25f * sinf(a.life * 14.f) : 0.f;
                a.groundT -= dt;
                if (a.groundT <= 0.f) {
                    a.groundT = 0.6f;
                    a.groundZ = groundAt(g, a.pos.x, a.pos.y, a.pos.z + 0.8f, &a.groundN);
                }
                a.pos.z = Lerp(a.pos.z, a.groundZ, expDecay(6.f, dt));
                break;
            }
        }
        a.ra.speed = Lerp(a.ra.speed, speed, expDecay(8.f, dt));
        a.ra.phase += dt * reptileCycleRate(modelOf(a), a.ra.speed);
        a.ra.lift = a.state == ST_FLEE ? 1.f : 0.3f;
    }
}

// Dolphin pods: travel through deep water, surfacing in arcs to breathe, leaping, and riding the bow wave of boats.
void updateDolphins(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    G.modeT += dt;
    // bow riding: join a fast boat nearby
    if (G.boat >= 0) {
        bool ok = G.boat < (int)g.vehicles.size() && g.vehicles[G.boat].used && g.isBoat(G.boat);
        G.boatT -= dt;
        float bs = ok ? length(g.vehicles[G.boat].sim.body.vel.xy()) : 0.f;
        if (!ok || G.boatT <= 0.f || bs < 3.5f || bs > 16.f) G.boat = -1;
    } else if (G.modeT > 5.f) {
        for (int vi : gW.vehNear) {
            if (!g.isBoat(vi)) continue;
            const Vehicle& v = g.vehicles[vi];
            float bs = length(v.sim.body.vel.xy());
            if (bs < 5.f || bs > 15.f) continue;
            if (length(v.sim.body.pos.toVec3().xy() - G.center.xy()) < 110.f) {
                G.boat = vi;
                G.boatT = frange(35.f, 70.f);
                G.modeT = 0.f;
                break;
            }
        }
    }
    vec3 podGoal;
    float podSpeed = 2.4f;
    vec2 podDir;
    if (G.boat >= 0) {
        const Vehicle& v = g.vehicles[G.boat];
        const VehicleAsset& va = g.vassets[v.model];
        vec3 f = v.sim.body.rotMat() * vec3(0, 1, 0);
        podDir = normalize(f.xy() + vec2(0.0001f, 0.f));
        podGoal = v.sim.body.pos.toVec3() + vec3(podDir * (va.spec.boxHalf.y + 3.f), 0.f);
        podSpeed = length(v.sim.body.vel.xy());
    } else {
        if (length(G.center.xy() - G.target.xy()) < 30.f || length(G.target - G.anchor) < 1.f) {
            for (int tries = 0; tries < 16; tries++) {
                float h = G.heading + frange(-0.8f, 0.8f);
                vec2 p = G.center.xy() + dirOf(h) * frange(120.f, 260.f);
                if (waterDepth(*g.map, p.x, p.y) > 5.f && length(p - gW.player.xy()) < 700.f) {
                    G.heading = h;
                    G.target = vec3(p, 0.f);
                    break;
                }
                if (tries == 15) {
                    G.heading += kPi;
                    G.target = G.anchor;
                }
            }
        }
        podDir = normalize(G.target.xy() - G.center.xy() + vec2(0.0001f, 0.f));
        podGoal = G.center + vec3(podDir * 6.f, 0.f);
    }
    vec3 cen(0.f);
    int n = 0;
    int k = 0;
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        a.life += dt;
        a.t += dt;
        a.sa.t += dt;
        if (a.state == ST_FALL || a.state == ST_DEAD) {
            fallStep(g, a, dt);
            continue;
        }
        // formation slot around the pod goal
        vec2 side(podDir.y, -podDir.x);
        float ring = (float)(k + 1);
        vec2 off = side * ((k & 1) ? 1.f : -1.f) * (1.8f + 1.4f * (float)(k / 2)) - podDir * (2.f * (float)(k / 2)) +
                   vec2(sinf(a.life * 0.3f + ring), cosf(a.life * 0.23f + ring)) * 1.2f;
        vec3 slot = podGoal + vec3(off, 0.f);
        vec2 d = slot.xy() - a.pos.xy();
        float wantSp = Clamp(podSpeed + dot(d, podDir) * 0.35f, 0.5f, 13.f);
        vec2 hv = normalize(d + podDir * 3.f + vec2(0.0001f, 0.f)) * wantSp;
        a.vel.x = Lerp(a.vel.x, hv.x, expDecay(1.2f, dt));
        a.vel.y = Lerp(a.vel.y, hv.y, expDecay(1.2f, dt));
        a.pos.x += a.vel.x * dt;
        a.pos.y += a.vel.y * dt;
        float wz;
        if (!waterAt(a.pos.x, a.pos.y, wz)) wz = 0.f;
        float bottom = g.map->heightAt(a.pos.x, a.pos.y);
        float yawPrev = a.yaw;
        if (length2(a.vel.xy()) > 0.01f) a.yaw = approachAngle(a.yaw, yawOf(a.vel.xy()), 1.8f * dt);
        a.sa.turn = Lerp(a.sa.turn, wrapA(a.yaw - yawPrev) / Max(dt, 1e-3f), expDecay(4.f, dt));
        float hs = length(a.vel.xy());
        // breathing: submerged cruise, then a surfacing arc (or a leap)
        a.timer -= dt;
        float depth = 1.6f + 0.6f * sinf(a.life * 0.37f + ring);
        if (a.state != ST_DIVE && a.timer <= 0.f) {
            a.state = ST_DIVE;
            a.t = 0.f;
            bool leap = frand() < (G.boat >= 0 ? 0.4f : 0.15f) && hs > 2.f;
            a.goal = vec3(leap ? frange(1.2f, 2.4f) : 0.12f, leap ? frange(1.5f, 1.9f) : frange(1.2f, 1.6f), leap ? 1.f : 0.f);   // height, duration, leap
            a.speedWant = depth;
        }
        float zWant = wz - depth;
        if (a.state == ST_DIVE) {
            float T = a.goal.y;
            float u = a.t / T;
            float D = a.speedWant, H = a.goal.x;
            float arc = sinf(kPi * Saturate(u));
            zWant = wz - D + (D + H) * arc;
            float dz = (D + H) * kPi / T * cosf(kPi * Saturate(u));
            a.pitch = Clamp(atan2f(dz, Max(hs, 1.f)), -1.2f, 1.2f);
            if (a.t - dt < T * 0.45f && a.t >= T * 0.45f && length2(a.pos - gW.cam) < 200.f * 200.f) {
                sfx(Audio::SFX_ANIMAL_BLOW, a.pos, 0.8f);
                if (frand() < 0.35f) sfx(Audio::SFX_DOLPHIN_CALL, a.pos, 0.7f);
            }
            if (a.goal.z > 0.5f && a.t - dt < T * 0.82f && a.t >= T * 0.82f) {
                spawnFx(FX_WATER_SPLASH, dvec3(vec3(a.pos.x, a.pos.y, wz)), vec3(0, 0, 1), 10, 1.4f);
                sfx(Audio::SFX_SPLASH_BIG, a.pos, 0.8f, 1.2f);
            }
            if (a.goal.z < 0.5f && fabsf(u - 0.5f) < 0.2f && frand() < dt * 6.f) spawnFx(FX_WAKE_SPRAY, dvec3(vec3(a.pos.x, a.pos.y, wz)), vec3(a.vel.xy() * 0.2f, 0.6f), 1, 0.5f);
            if (u >= 1.f) {
                a.state = ST_SWIM;
                a.timer = G.boat >= 0 ? frange(1.5f, 4.f) : frange(4.f, 12.f);
            }
            a.pos.z = zWant;
        } else {
            a.state = ST_SWIM;
            a.pos.z = Lerp(a.pos.z, zWant, expDecay(1.5f, dt));
            a.pitch = Lerp(a.pitch, Clamp((zWant - a.pos.z) * 0.3f, -0.4f, 0.4f), expDecay(3.f, dt));
        }
        a.pos.z = Max(a.pos.z, bottom + 0.4f);
        a.sa.phase += dt * (2.f + hs * 0.55f);
        a.sa.amp = Clamp(0.5f + hs * 0.12f, 0.4f, 1.3f);
        a.sa.pitch = a.state == ST_DIVE ? -a.pitch * 0.6f : 0.f;
        a.roll = Lerp(a.roll, -Clamp(a.sa.turn * 0.4f, -0.6f, 0.6f), expDecay(2.f, dt));
        cen += a.pos;
        n++;
        k++;
    }
    if (n) G.center = lerp(G.center, cen / (float)n, 1.f);
}

// Manatees and sea turtles: slow grazers near the surface, coming up to breathe now and then.
void updateSlowSwimmers(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    bool manatee = G.sp == SP_MANATEE;
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        a.life += dt;
        a.t += dt;
        a.sa.t += dt;
        if (a.state == ST_FALL || a.state == ST_DEAD) {
            fallStep(g, a, dt);
            continue;
        }
        a.think -= dt;
        if (a.think <= 0.f || length(a.goal.xy() - a.pos.xy()) < 2.f) {
            a.think = frange(8.f, 20.f);
            for (int t = 0; t < 8; t++) {
                vec2 p = G.anchor.xy() + gW.rng.inCircle() * 30.f;
                if (waterDepth(*g.map, p.x, p.y) > (manatee ? 1.3f : 2.f)) {
                    a.goal = vec3(p, 0.f);
                    break;
                }
            }
        }
        // flee from boats passing close
        float spd = manatee ? 0.45f : 0.6f;
        vec2 dir = normalize(a.goal.xy() - a.pos.xy() + vec2(0.0001f, 0.f));
        for (int vi : gW.vehNear) {
            if (!g.isBoat(vi)) continue;
            vec2 d = a.pos.xy() - g.vehicles[vi].sim.body.pos.toVec3().xy();
            if (length2(d) < 15.f * 15.f) {
                dir = normalize(d + vec2(0.0001f, 0.f));
                spd = manatee ? 1.2f : 1.8f;
            }
        }
        // breathing cycle
        a.timer -= dt;
        float wz;
        if (!waterAt(a.pos.x, a.pos.y, wz)) wz = 0.f;
        float depth = manatee ? 0.9f : 2.2f;
        bool surface = a.timer < 4.f;
        if (a.timer <= 0.f) a.timer = frange(20.f, 45.f);
        if (surface) depth = manatee ? 0.12f : 0.25f;
        if (surface && a.timer > 3.9f && a.timer - dt <= 3.9f && manatee && length2(a.pos - gW.cam) < 120.f * 120.f) sfx(Audio::SFX_ANIMAL_BLOW, a.pos, 0.7f, 0.7f);
        swimStep(g, a, vec3(dir * spd, 0.f), dt, 0.6f, 0.5f, depth, manatee ? 0.35f : 0.15f);
        a.pos.z = Lerp(a.pos.z, Max(wz - depth, g.map->heightAt(a.pos.x, a.pos.y) + 0.4f), expDecay(0.8f, dt));
        a.pitch = Lerp(a.pitch, surface ? 0.15f : 0.f, expDecay(1.f, dt));
        a.sa.phase += dt * (manatee ? 1.3f : 1.6f);
        a.sa.amp = manatee ? 0.6f : 0.9f;
        a.state = ST_SWIM;
    }
}

// Fish shoals: 3D boids around a reef / pier, keeping between the bottom and the surface, scattering from swimmers,
// boats and blasts.
void updateFish(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    bool tarpon = G.variant == 3;
    // shoal wanders around the anchor
    G.modeT += dt;
    if (G.modeT > G.timer) {
        G.modeT = 0.f;
        G.timer = frange(6.f, 14.f);
        for (int t = 0; t < 6; t++) {
            vec2 p = G.anchor.xy() + gW.rng.inCircle() * G.radius;
            float d = waterDepth(*g.map, p.x, p.y);
            if (d > 1.5f) {
                float wz = g.map->waterAt(p.x, p.y);
                G.target = vec3(p, wz - Min(d * 0.5f, 3.f));
                break;
            }
        }
    }
    gBoid.pos.clear();
    gBoid.vel.clear();
    gBoid.idx.clear();
    for (int ai : G.members) {
        const Animal& a = gW.animals[ai];
        if (a.used && a.state != ST_DEAD && a.state != ST_FALL) {
            gBoid.idx.push_back(ai);
            gBoid.pos.push_back(a.pos);
            gBoid.vel.push_back(a.vel);
        }
    }
    BoidParams bp;
    bp.sepDist = tarpon ? 1.6f : 0.45f;
    bp.viewDist = tarpon ? 6.f : 2.6f;
    bp.wSep = 4.f;
    bp.wAli = 1.3f;
    bp.wCoh = 0.6f;
    // threats: the swimming player / underwater camera, boats
    vec3 scare(0.f);
    bool anyScare = false;
    if (gW.playerSwim) {
        scare = gW.player;
        anyScare = true;
    }
    float cwz;
    if (waterAt(gW.cam.x, gW.cam.y, cwz) && gW.cam.z < cwz) {
        if (!anyScare) scare = gW.cam;
        anyScare = true;
    }
    for (int q = 0; q < (int)gBoid.idx.size(); q++) {
        Animal& a = gW.animals[gBoid.idx[q]];
        a.life += dt;
        a.sa.t += dt;
        if (a.state == ST_DIVE) {   // a mullet leaping clear of the water, tail still beating
            a.vel.z -= 9.81f * dt;
            a.pos += a.vel * dt;
            a.pitch = Clamp(atan2f(a.vel.z, Max(length(a.vel.xy()), 0.2f)), -1.3f, 1.3f);
            a.sa.phase += dt * 14.f;
            a.sa.amp = 1.3f;
            float wz2;
            if (waterAt(a.pos.x, a.pos.y, wz2) && a.pos.z < wz2 - 0.15f && a.vel.z < 0.f) {
                spawnFx(FX_WATER_SPLASH, dvec3(vec3(a.pos.x, a.pos.y, wz2)), vec3(0, 0, 1), 3, 0.35f);
                if (length2(a.pos - gW.cam) < 60.f * 60.f) sfx(Audio::SFX_SPLASH_SMALL, a.pos, 0.5f, 1.3f);
                a.state = ST_SWIM;
                a.vel *= 0.3f;
            }
            continue;
        }
        float wzs;
        if (G.variant == 2 && !anyScare && waterAt(a.pos.x, a.pos.y, wzs) && a.pos.z > wzs - 1.6f && frand() < dt * 0.012f) {
            a.state = ST_DIVE;   // mullet jump: out of the water and back in a metre or two on
            vec2 h = length2(a.vel.xy()) > 1e-4f ? normalize(a.vel.xy()) : dirOf(a.yaw);
            a.vel = vec3(h * frange(1.5f, 2.8f), frange(3.f, 4.2f));
            a.pos.z = wzs - 0.1f;
            spawnFx(FX_WATER_SPLASH, dvec3(vec3(a.pos.x, a.pos.y, wzs)), vec3(0, 0, 1), 2, 0.25f);
            continue;
        }
        vec3 steer = boidSteer(gBoid.pos.data(), gBoid.vel.data(), (int)gBoid.pos.size(), q, bp);
        vec3 toT = G.target - a.pos;
        steer += toT * (0.12f / Max(1.f, length(toT) * 0.2f));
        float cruise = tarpon ? 1.1f : 0.7f;
        if (anyScare) {
            vec3 d = a.pos - scare;
            float dl = length(d);
            if (dl < 6.f && dl > 0.01f) {
                steer += d / dl * (6.f - dl) * 3.f;
                cruise = 3.f;
            }
        }
        for (int vi : gW.vehNear)
            if (g.isBoat(vi)) {
                vec3 d = a.pos - g.vehicles[vi].sim.body.pos.toVec3();
                float dl = length(d);
                if (dl < 7.f && dl > 0.01f) {
                    steer += d / dl * (7.f - dl) * 2.f;
                    cruise = 2.5f;
                }
            }
        float sp = length(a.vel);
        steer += (sp > 0.01f ? a.vel / sp : vec3(dirOf(a.yaw), 0.f)) * (cruise - sp) * 1.5f;
        float sl = length(steer);
        if (sl > 6.f) steer *= 6.f / sl;
        a.vel += steer * dt;
        a.vel.z *= 0.9f;
        vec3 np = a.pos + a.vel * dt;
        float wz = a.pos.z + 1.f;
        if (!waterAt(np.x, np.y, wz) || waterDepth(*g.map, np.x, np.y) < 0.9f) {   // stay in the water
            a.vel.x = -a.vel.x;
            a.vel.y = -a.vel.y;
            np = a.pos;
            waterAt(np.x, np.y, wz);
        }
        float bottom = g.map->heightAt(np.x, np.y);
        np.z = Clamp(np.z, bottom + 0.35f, wz - 0.45f);
        a.pos = np;
        float yp = a.yaw;
        if (length2(a.vel.xy()) > 1e-4f) a.yaw = approachAngle(a.yaw, yawOf(a.vel.xy()), 5.f * dt);
        a.sa.turn = wrapA(a.yaw - yp) / Max(dt, 1e-3f);
        a.pitch = Clamp(atan2f(a.vel.z, Max(length(a.vel.xy()), 0.2f)), -0.5f, 0.5f);
        a.sa.phase += dt * (4.f + sp * 7.f) * (tarpon ? 0.5f : 1.f);
        a.sa.amp = Clamp(0.5f + sp * 0.5f, 0.5f, 1.4f);
    }
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (a.used && (a.state == ST_DEAD || a.state == ST_FALL)) {   // dead fish float up
            float wz;
            if (waterAt(a.pos.x, a.pos.y, wz)) a.pos.z = Lerp(a.pos.z, wz - 0.05f, expDecay(0.5f, dt));
            a.state = ST_DEAD;
            a.roll = Lerp(a.roll, kPi, expDecay(1.f, dt));
            a.deadT += dt;
            a.sa.dead = 1.f;
        }
    }
}

}  // namespace wild_detail

// ==================================================================================================================
// Mammals
namespace wild_detail {

struct QuadSpeeds {
    float walk, trot, run, radius, alert, flee;   // m/s; collision radius; alert / flight distances (m)
};
const QuadSpeeds& quadSpeeds(int sp) {
    static const QuadSpeeds kDog = {1.25f, 2.9f, 8.f, 0.22f, 14.f, 5.f};
    static const QuadSpeeds kCat = {0.6f, 1.5f, 6.f, 0.12f, 8.f, 4.5f};
    static const QuadSpeeds kRac = {0.6f, 1.4f, 4.f, 0.17f, 10.f, 6.f};
    static const QuadSpeeds kDeer = {1.1f, 3.f, 12.f, 0.35f, 65.f, 38.f};
    static const QuadSpeeds kCow = {1.f, 2.3f, 5.5f, 0.6f, 25.f, 12.f};
    static const QuadSpeeds kHorse = {1.5f, 4.f, 12.f, 0.55f, 35.f, 16.f};
    switch (sp) {
        case SP_CAT: return kCat;
        case SP_RACCOON: return kRac;
        case SP_DEER: return kDeer;
        case SP_COW: return kCow;
        case SP_HORSE: return kHorse;
        default: return kDog;
    }
}

// Ground locomotion + gait bookkeeping for a quadruped
void quadMove(GameWorld& g, Animal& a, vec2 wantVel, float dt, float accel, float turnRate, int waterMode) {
    const QuadSpeeds& qs = quadSpeeds(a.sp);
    float yawPrev = a.yaw;
    groundStep(g, a, wantVel, dt, accel, turnRate, qs.radius * a.scale, waterMode);
    const ModelData& md = modelOf(a);
    float sp = length(a.vel.xy()) / Max(a.scale, 0.2f);
    a.qa.speed = Lerp(a.qa.speed, sp, expDecay(10.f, dt));
    float h = md.legLen;
    float fr = a.qa.speed / sqrtf(9.81f * Max(h, 0.1f));
    float gt = fr < 0.42f ? 0.f : (fr < 1.05f ? 1.f : (fr < 1.6f ? 2.f : 3.f));
    if (a.sp == SP_COW && gt > 2.f) gt = 2.f;
    a.qa.gait = approach(a.qa.gait, gt, dt * 2.5f);
    a.qa.phase += dt * quadCycleRate(md, a.qa.speed, a.qa.gait);
    a.qa.phase -= floorf(a.qa.phase);
    a.qa.turn = Lerp(a.qa.turn, wrapA(a.yaw - yawPrev) / Max(dt, 1e-3f), expDecay(6.f, dt));
}

void quadIdle(Animal& a, float dt) {
    a.qa.speed = Lerp(a.qa.speed, 0.f, expDecay(8.f, dt));
    a.qa.turn = Lerp(a.qa.turn, 0.f, expDecay(8.f, dt));
    a.vel = vec3(0.f);
}

void quadFaceTowards(Animal& a, vec3 p, float rate, float dt) {
    vec2 d = p.xy() - a.pos.xy();
    if (length2(d) > 1e-4f) a.yaw = approachAngle(a.yaw, yawOf(d), rate * dt);
}

// Head look at a world point (limited), relative to the body
void quadLookAt(Animal& a, vec3 p, float dt) {
    vec2 d = p.xy() - a.pos.xy();
    float want = length2(d) > 1e-4f ? Clamp(wrapA(yawOf(d) - a.yaw), -1.3f, 1.3f) : 0.f;
    a.qa.lookYaw = Lerp(a.qa.lookYaw, want, expDecay(5.f, dt));
}

void bark(Animal& a, float vol = 1.f) {
    vec3 head = worldPoint(a, modelOf(a).headTip);
    sfx(Audio::SFX_DOG_BARK, head, vol, a.scale < 0.7f ? 1.35f : (a.var == 4 ? 0.9f : 1.f));
    a.qa.mouth = 1.f;
}

// Bite a pedestrian (dogs)
void dogBite(GameWorld& g, Animal& a, int ped) {
    Ped& p = g.peds[ped];
    vec3 dir = p.pos.toVec3() - a.pos;
    dir.z = 0.f;
    dir = length2(dir) > 1e-4f ? normalize(dir) : vec3(dirOf(a.yaw), 0.f);
    float dmg = a.scale < 0.7f ? 3.f : (p.isPlayer ? 7.f : 9.f);
    g.damagePed(ped, dmg, DMG_MELEE, -1, dir);
    sfx(Audio::SFX_DOG_GROWL, worldPoint(a, modelOf(a).headTip), 1.f);
    sfx(Audio::SFX_PUNCH, p.pos.toVec3() + vec3(0, 0, 0.5f), 0.4f, 1.4f);
    spawnFx(FX_BLOOD, dvec3(p.pos.toVec3() + vec3(0, 0, 0.45f)), -dir, 2, 0.6f);
    a.qa.mouth = 1.f;
    if (p.isPlayer) g.rumble(0.35f, 0.4f);
}

// Dogs: leashed pets following their owner, and stray packs roaming the Flats.
void updateDogs(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    bool leashed = G.type == GT_DOG_LEASH;
    bool ownerOk = leashed && pedOk(g, G.owner, G.ownerUid) && g.peds[G.owner].vehicle < 0;
    const Ped* owner = ownerOk ? &g.peds[G.owner] : nullptr;
    if (owner && (owner->state == PS_ONFOOT || owner->state == PS_GETUP)) {
        G.hand = pedBoneWorld(g, *owner, G.leashHand ? Anim::B_HAND_R : Anim::B_HAND_L);
        G.hasHand = true;
    } else if (!owner) {
        if (G.hasHand && leashed) {   // the owner is gone: the dog runs loose
            for (int ai : G.members) {
                Animal& a = gW.animals[ai];
                if (a.used && a.state != ST_DEAD && a.state != ST_FALL) {
                    a.state = ST_ALERT;
                    a.t = 0.f;
                    a.goal = G.hand;
                }
            }
        }
        G.hasHand = false;
    }
    Ped* pl = g.playerPed();
    G.attackT += dt;
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        a.life += dt;
        a.t += dt;
        a.qa.t += dt;
        a.cooldown -= dt;
        a.qa.mouth = Max(0.f, a.qa.mouth - dt * 3.f);
        if (a.state == ST_FALL || a.state == ST_DEAD) {
            fallStep(g, a, dt);
            continue;
        }
        const QuadSpeeds& qs = quadSpeeds(a.sp);
        const ModelData& md = modelOf(a);
        vec3 head = worldPoint(a, md.headTip);
        a.think -= dt;
        // ---------------------------------------------------------------- leashed: walk with the owner
        if (owner && a.state != ST_ATTACK && a.state != ST_FLEE && a.state != ST_ALERT) {
            vec3 op = owner->pos.toVec3();
            vec2 of = dirOf(owner->yaw), orr(of.y, -of.x);
            float oSpeed = length(owner->vel.xy());
            bool fleeing = owner->brain.type == BRAIN_FLEE;
            float side = G.leashHand ? 1.f : -1.f;
            float sniff = sinf(a.life * 0.23f + (float)(a.uid & 15));
            vec2 want = op.xy() + orr * (0.7f * side + 0.35f * sniff) + of * (oSpeed > 0.3f ? 0.6f + 0.3f * sinf(a.life * 0.17f) : 0.25f);
            vec2 d = want - a.pos.xy();
            float dist = length(d);
            float spd = dist < 0.3f ? 0.f : Clamp(dist * 2.3f, 0.f, fleeing ? qs.run : 5.5f);
            if (oSpeed < 0.25f) {
                a.timer += dt;
                spd = dist > 0.9f ? Min(spd, 1.2f) : 0.f;
            } else a.timer = 0.f;
            if (spd > 0.05f) quadMove(g, a, d / Max(dist, 1e-3f) * spd, dt, 8.f, 5.f, 1);
            else {
                quadIdle(a, dt);
                quadFaceTowards(a, op + vec3(of * 2.f, 0.f), 2.f, dt);
            }
            a.qa.sit = Lerp(a.qa.sit, a.timer > 3.f ? 1.f : 0.f, expDecay(3.f, dt));
            a.qa.headDown = Lerp(a.qa.headDown, oSpeed > 0.3f && sniff > 0.6f ? 0.7f : 0.f, expDecay(3.f, dt));
            a.qa.tailWag = 0.5f + 0.3f * sinf(a.life * 0.5f);
            a.qa.alert = 0.3f;
            // keep the collar within the leash's reach
            if (G.hasHand) {
                vec3 collar = worldPoint(a, md.collar);
                vec3 dh = collar - G.hand;
                float L = length(dh);
                const float rope = 1.75f;
                if (L > rope) {
                    vec3 corr = -dh / L * (L - rope);
                    a.pos.x += corr.x;
                    a.pos.y += corr.y;
                }
            }
            // the owner under attack by the player: the dog breaks free and defends
            if (pl && owner->lastAttacker == g.player && g.time - owner->lastDamageTime < 5.0 && a.scale > 0.6f) {
                a.state = ST_ATTACK;
                a.t = 0.f;
                a.target = g.player;
                a.targetUid = pl->uid;
                G.owner = -1;
                G.hasHand = false;
                bark(a);
            }
            // barks at the player running past or pointing a gun
            if (pl && a.think <= 0.f) {
                a.think = 0.4f;
                float dp = length(pl->pos.toVec3() - a.pos);
                if (a.cooldown <= 0.f && ((dp < 5.f && gW.playerRunning) || (dp < 10.f && gW.playerAiming))) {
                    bark(a, 0.9f);
                    a.cooldown = frange(2.f, 5.f);
                    quadLookAt(a, pl->pos.toVec3(), 1.f);
                }
            }
            continue;
        }
        // ---------------------------------------------------------------- strays and loose dogs
        if (a.think <= 0.f && a.state != ST_ATTACK) {
            a.think = 0.25f + frand() * 0.1f;
            bool gun = false;
            const Threat* th = nullptr;
            vec3 from;
            float sc = scareLevel(a.pos, 1.f, &from, &gun, &th);
            bool hurt = a.health < speciesInfo(a.sp).hp * 0.75f;
            bool aimedAt = false;
            if (pl && gW.playerAiming) {
                vec3 to = a.pos + vec3(0, 0, 0.4f) - g.pedHeadPos(*pl);
                float along = dot(to, pl->aimDir);
                aimedAt = along > 0.f && along < 25.f && length(to - pl->aimDir * along) < 1.2f;
            }
            if ((gun && sc < 0.4f) || hurt || aimedAt || (th && th->kind == 1 && th->speed > 9.f && sc < 0.5f)) {
                if (a.state != ST_FLEE) {
                    a.state = ST_FLEE;
                    a.t = 0.f;
                    a.goal = from;
                    if (hurt || aimedAt) sfx(Audio::SFX_DOG_YELP, head, 0.8f);
                }
            } else if (pl && pl->health > 0.f && a.state != ST_FLEE) {
                vec3 pp = pl->pos.toVec3();
                float dp = length(pp - a.pos);
                bool inCar = gW.playerVeh >= 0;
                float carSpeed = inCar ? length(gW.playerVel.xy()) : 0.f;
                if (!inCar && dp < 7.f && a.aggression > 0.62f && a.cooldown <= 0.f) {
                    a.state = ST_ATTACK;
                    a.t = 0.f;
                    a.target = g.player;
                    a.targetUid = pl->uid;
                } else if ((!inCar && dp < qs.alert) || (inCar && carSpeed < 9.f && dp < 16.f)) {
                    if (a.state != ST_ALERT) {
                        a.state = ST_ALERT;
                        a.t = 0.f;
                    }
                    a.goal = pp;
                    if (gW.playerRunning || (inCar && carSpeed > 3.f)) {   // chase whatever runs
                        a.state = ST_ATTACK;
                        a.t = 0.f;
                        a.target = g.player;
                        a.targetUid = pl->uid;
                        a.timer = 5.f;   // barking chase only
                    }
                }
            }
            // an aggressive stray goes for a pedestrian now and then: a street event
            if (!leashed && a.state == ST_WALK && a.aggression > 0.7f && G.attackT > 90.f && frand() < 0.08f) {
                for (int pi : gW.pedsNear) {
                    const Ped& p = g.peds[pi];
                    if (p.isPlayer || p.faction != FAC_CIVILIAN || p.state != PS_ONFOOT || p.health <= 0.f || p.persistent) continue;
                    if (length(p.pos.toVec3() - a.pos) > 25.f) continue;
                    a.state = ST_ATTACK;
                    a.t = 0.f;
                    a.target = pi;
                    a.targetUid = p.uid;
                    a.timer = 0.f;
                    G.attackT = 0.f;
                    G.attackVictim = pi;
                    G.attackVictimUid = p.uid;
                    LOG("Wildlife: stray dog attacks a pedestrian at %.0f %.0f", a.pos.x, a.pos.y);
                    break;
                }
            }
        }
        switch (a.state) {
            case ST_ATTACK: {
                bool ok = pedOk(g, a.target, a.targetUid);
                if (!ok || a.t > 14.f) {
                    a.state = ST_WALK;
                    a.t = 0.f;
                    break;
                }
                Ped& p = g.peds[a.target];
                vec3 pp = p.pos.toVec3();
                vec2 d = pp.xy() - a.pos.xy();
                float dist = length(d);
                bool barkOnly = a.timer > 0.f;
                if (barkOnly) {
                    a.timer -= dt;
                    if (a.timer <= 0.f || dist > 30.f) {
                        a.state = ST_ALERT;
                        a.t = 0.f;
                        a.timer = 0.f;
                        break;
                    }
                }
                float stopAt = barkOnly ? 2.5f : 0.9f;
                float spd = dist > stopAt ? qs.run * (barkOnly ? 0.8f : 1.f) : 0.f;
                if (spd > 0.f) quadMove(g, a, d / dist * spd, dt, 14.f, 7.f, 0);
                else {
                    quadIdle(a, dt);
                    quadFaceTowards(a, pp, 6.f, dt);
                }
                a.qa.crouch = 0.3f;
                a.qa.alert = 1.f;
                a.qa.tailWag = 0.f;
                if (a.cooldown <= 0.f) {
                    if (!barkOnly && dist < 1.3f && p.vehicle < 0) {
                        dogBite(g, a, a.target);
                        a.cooldown = 1.3f;
                        if (!p.isPlayer) {   // the victim panics; bystanders react
                            Ped& vp = g.peds[a.target];
                            vp.brain.type = BRAIN_FLEE;
                            vp.brain.goal = dvec3(a.pos);
                            vp.brain.target = -1;
                            vp.brain.timer = 0.f;
                            g.aiSay(a.target, frand() < 0.5f ? BK_HELP : BK_PANIC, 1.f, true);
                            g.aiStimulus(dvec3(a.pos), STIM_FIGHT, -1, 22.f, false);
                        }
                    } else {
                        bark(a, 1.f);
                        a.cooldown = frange(0.6f, 1.2f);
                    }
                }
                if (!p.isPlayer && dist > 25.f) {
                    a.state = ST_WALK;
                    a.t = 0.f;
                }
                break;
            }
            case ST_ALERT: {   // stand ground and bark
                quadIdle(a, dt);
                quadFaceTowards(a, a.goal, 4.f, dt);
                a.qa.crouch = Lerp(a.qa.crouch, 0.35f, expDecay(3.f, dt));
                a.qa.alert = 1.f;
                a.qa.tailWag = 0.f;
                if (a.cooldown <= 0.f && length(a.goal - a.pos) < 18.f) {
                    bark(a, 1.f);
                    a.cooldown = frange(0.7f, 1.6f);
                }
                if (a.t > 6.f) {
                    a.state = owner ? ST_WALK : ST_WALK;
                    a.t = 0.f;
                    a.qa.crouch = 0.f;
                }
                break;
            }
            case ST_FLEE: {
                vec2 away = a.pos.xy() - a.goal.xy();
                away = length2(away) > 0.01f ? normalize(away) : dirOf(a.yaw);
                quadMove(g, a, away * qs.run, dt, 12.f, 5.f, 0);
                a.qa.crouch = 0.4f;
                a.qa.tailWag = 0.f;
                if (a.t > 7.f) {
                    a.state = ST_WALK;
                    a.t = 0.f;
                    a.qa.crouch = 0.f;
                    if (!leashed) G.anchor = a.pos;
                }
                break;
            }
            case ST_REST: {
                quadIdle(a, dt);
                a.qa.lie = Lerp(a.qa.lie, 1.f, expDecay(1.5f, dt));
                a.qa.tailWag = 0.f;
                if (a.t > a.timer) {
                    a.state = ST_WALK;
                    a.t = 0.f;
                }
                break;
            }
            default: {   // wander around the pack anchor, sniffing
                a.state = ST_WALK;
                a.qa.lie = Lerp(a.qa.lie, 0.f, expDecay(3.f, dt));
                a.qa.sit = Lerp(a.qa.sit, 0.f, expDecay(3.f, dt));
                a.qa.crouch = Lerp(a.qa.crouch, 0.f, expDecay(3.f, dt));
                a.qa.alert = Lerp(a.qa.alert, 0.f, expDecay(1.f, dt));
                if (length(a.goal.xy() - a.pos.xy()) < 1.f || a.t > 12.f) {
                    a.t = 0.f;
                    if (frand() < 0.25f) {
                        a.state = ST_REST;
                        a.timer = frange(8.f, 25.f);
                        break;
                    }
                    for (int k2 = 0; k2 < 6; k2++) {
                        vec2 p = G.anchor.xy() + gW.rng.inCircle() * 30.f;
                        if (!onRoadOrBuilding(g, p, 0.f, 1.f) || k2 == 5) {
                            a.goal = vec3(p, a.pos.z);
                            break;
                        }
                    }
                }
                vec2 d = a.goal.xy() - a.pos.xy();
                float dist = length(d);
                bool trot = (a.uid & 3) == 0;
                quadMove(g, a, dist > 0.5f ? d / dist * (trot ? qs.trot : qs.walk) : vec2(0.f), dt, 4.f, 3.f, 0);
                a.qa.headDown = Lerp(a.qa.headDown, sinf(a.life * 0.4f) > 0.3f ? 0.8f : 0.f, expDecay(2.f, dt));
                a.qa.tailWag = 0.25f;
                break;
            }
        }
    }
    // a rescued pedestrian thanks the player
    if (G.attackVictim >= 0) {
        bool dogsAttacking = false;
        for (int ai : G.members) {
            const Animal& a = gW.animals[ai];
            if (a.used && a.state == ST_ATTACK && a.target == G.attackVictim) dogsAttacking = true;
        }
        if (!dogsAttacking) {
            if (pedOk(g, G.attackVictim, G.attackVictimUid) && pl && length(g.peds[G.attackVictim].pos.toVec3() - pl->pos.toVec3()) < 20.f) {
                bool dogDown = false;
                for (int ai : G.members)
                    if (gW.animals[ai].used && (gW.animals[ai].state == ST_DEAD || gW.animals[ai].state == ST_FLEE)) dogDown = true;
                if (dogDown) g.aiSay(G.attackVictim, BK_THANKS, 1.f, true);
            }
            G.attackVictim = -1;
        }
    }
}

// Cats (porches, garden walls, parked cars) and raccoons (dumpsters at night): idle, prowl, bolt when approached.
void updateCritters(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    bool cat = G.sp == SP_CAT;
    const QuadSpeeds& qs = quadSpeeds(G.sp);
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        a.life += dt;
        a.t += dt;
        a.qa.t += dt;
        a.qa.mouth = Max(0.f, a.qa.mouth - dt * 2.f);
        if (a.state == ST_FALL || a.state == ST_DEAD) {
            fallStep(g, a, dt);
            continue;
        }
        a.think -= dt;
        if (a.think <= 0.f && a.state != ST_FLEE) {
            a.think = 0.25f;
            vec3 from;
            float sc = scareLevel(a.pos, cat ? 0.55f : 0.8f, &from);
            // cats also run from dogs
            if (cat)
                for (const Group& D : gW.groups)
                    if (D.used && (D.type == GT_DOG_LEASH || D.type == GT_DOG_STRAY))
                        for (int dj : D.members) {
                            const Animal& d = gW.animals[dj];
                            if (d.used && d.state != ST_DEAD && length2(d.pos - a.pos) < 8.f * 8.f) {
                                sc = 0.f;
                                from = d.pos;
                            }
                        }
            if (sc < 1.f) {
                a.state = ST_FLEE;
                a.t = 0.f;
                a.goal = from;
                if (cat && frand() < 0.4f) sfx(Audio::SFX_CAT_MEOW, a.pos + vec3(0, 0, 0.25f), 0.7f, 1.4f);
            }
        }
        switch (a.state) {
            case ST_FLEE: {
                vec2 away = a.pos.xy() - a.goal.xy();
                away = length2(away) > 0.01f ? normalize(away) : dirOf(a.yaw);
                quadMove(g, a, away * qs.run, dt, 16.f, 7.f, 0);
                a.qa.sit = a.qa.lie = 0.f;
                a.qa.rear = 0.f;
                a.qa.crouch = 0.35f;
                if (a.t > 4.f) {
                    a.state = ST_WALK;
                    a.t = 0.f;
                    a.qa.crouch = 0.f;
                    G.anchor = a.pos;
                    a.goal = a.pos;
                }
                break;
            }
            case ST_WALK: {
                vec2 d = a.goal.xy() - a.pos.xy();
                float dist = length(d);
                quadMove(g, a, dist > 0.3f ? d / dist * qs.walk : vec2(0.f), dt, 3.f, 3.f, 0);
                a.qa.sit = Lerp(a.qa.sit, 0.f, expDecay(4.f, dt));
                a.qa.lie = Lerp(a.qa.lie, 0.f, expDecay(4.f, dt));
                a.qa.rear = Lerp(a.qa.rear, 0.f, expDecay(4.f, dt));
                a.qa.headDown = Lerp(a.qa.headDown, cat ? 0.f : 0.7f, expDecay(2.f, dt));
                if (dist < 0.4f || a.t > 10.f) {
                    a.state = cat ? ST_REST : ST_FEED;
                    a.t = 0.f;
                    a.timer = frange(6.f, 20.f);
                }
                break;
            }
            case ST_FEED: {   // raccoon rummaging at the dumpster, up on its hind legs
                quadIdle(a, dt);
                quadFaceTowards(a, G.target, 3.f, dt);
                a.qa.rear = Lerp(a.qa.rear, sinf(a.life * 0.5f) > -0.2f ? 0.75f : 0.f, expDecay(2.f, dt));
                a.qa.headDown = Lerp(a.qa.headDown, 0.3f, expDecay(2.f, dt));
                if (frand() < dt * 0.3f && length2(a.pos - gW.cam) < 60.f * 60.f) sfx(Audio::SFX_RACCOON_CHITTER, a.pos + vec3(0, 0, 0.3f), 0.7f);
                if (a.t > a.timer) {
                    a.state = ST_WALK;
                    a.t = 0.f;
                    a.goal = G.target + vec3(gW.rng.inCircle() * 3.f, 0.f);
                }
                break;
            }
            default: {   // cats: sit / loaf, groom, now and then stroll a few meters
                a.state = ST_REST;
                quadIdle(a, dt);
                bool loaf = (a.uid & 1) != 0;
                a.qa.sit = Lerp(a.qa.sit, loaf ? 0.f : 1.f, expDecay(2.f, dt));
                a.qa.lie = Lerp(a.qa.lie, loaf ? 1.f : 0.f, expDecay(2.f, dt));
                a.qa.lookYaw = 0.9f * sinf(a.life * 0.3f + (float)(a.uid & 7));
                a.qa.tailWag = 0.3f;
                if (cat && frand() < dt * 0.03f && (gW.tod > 19.f || gW.tod < 6.f)) sfx(Audio::SFX_CAT_MEOW, a.pos + vec3(0, 0, 0.25f), 0.6f);
                if (a.t > a.timer + 10.f) {
                    a.state = ST_WALK;
                    a.t = 0.f;
                    a.goal = G.anchor + vec3(gW.rng.inCircle() * (cat ? 4.f : 6.f), 0.f);
                }
                break;
            }
        }
        a.qa.alert = Lerp(a.qa.alert, a.state == ST_FLEE ? 1.f : 0.3f, expDecay(2.f, dt));
    }
}

// Grazing herds: deer in the Cypress Ridge woods, cattle and horses on the farmland pastures.
void updateHerd(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    int sp = G.sp;
    const QuadSpeeds& qs = quadSpeeds(sp);
    G.alarm = Max(0.f, G.alarm - dt);
    G.modeT += dt;
    // the herd drifts slowly across its pasture
    if (G.modeT > G.timer && G.alarm <= 0.f) {
        G.modeT = 0.f;
        G.timer = frange(40.f, 90.f);
        for (int t = 0; t < 8; t++) {
            vec2 p = G.anchor.xy() + gW.rng.inCircle() * G.radius * 0.6f;
            if (!onRoadOrBuilding(g, p, sp == SP_DEER ? 6.f : 14.f, 4.f) && waterDepth(*g.map, p.x, p.y) < 0.f) {
                G.target = vec3(p, 0.f);
                break;
            }
        }
    }
    int k = 0;
    for (int ai : G.members) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        a.life += dt;
        a.t += dt;
        a.qa.t += dt;
        a.qa.mouth = Max(0.f, a.qa.mouth - dt);
        if (a.state == ST_FALL || a.state == ST_DEAD) {
            fallStep(g, a, dt);
            continue;
        }
        a.think -= dt;
        if (a.think <= 0.f) {
            a.think = 0.3f + frand() * 0.2f;
            bool gun = false;
            vec3 from;
            float sc = scareLevel(a.pos, qs.alert / 10.f, &from, &gun);
            float fleeRatio = qs.flee / qs.alert;
            if (gun && sc < 1.5f) sc = 0.f;
            if (sc < fleeRatio || G.alarm > 5.f) {
                if (G.alarm <= 5.f) {
                    G.alarm = gun ? 14.f : 9.f;
                    G.threat = from;
                    if (sp == SP_DEER) sfx(Audio::SFX_DEER_SNORT, a.pos + vec3(0, 0, 1.f), 1.f);
                    if (sp == SP_HORSE) sfx(Audio::SFX_HORSE_NEIGH, a.pos + vec3(0, 0, 1.5f), 0.9f);
                }
                if (a.state != ST_FLEE) {
                    a.state = ST_FLEE;
                    a.t = 0.f;
                }
            } else if (sc < 1.f && a.state != ST_FLEE && a.state != ST_ALERT) {
                a.state = ST_ALERT;
                a.t = 0.f;
                a.goal = from;
            }
        }
        switch (a.state) {
            case ST_FLEE: {
                vec2 away = a.pos.xy() - G.threat.xy();
                away = length2(away) > 0.01f ? normalize(away) : dirOf(a.yaw);
                vec2 toHerd = G.center.xy() - a.pos.xy();
                vec2 dir = normalize(away * 2.f + (length2(toHerd) > 25.f ? normalize(toHerd) * 0.6f : vec2(0.f)) + dirOf(a.yaw) * 0.3f);
                float spd = qs.run * (sp == SP_COW ? 0.7f : 1.f) * (0.85f + 0.15f * hashToFloat(a.uid));
                quadMove(g, a, dir * spd, dt, sp == SP_DEER ? 14.f : 6.f, 2.5f, 1);
                a.qa.headDown = Lerp(a.qa.headDown, 0.f, expDecay(5.f, dt));
                a.qa.lie = 0.f;
                a.qa.alert = 1.f;
                if (G.alarm <= 0.f && a.t > 6.f) {
                    a.state = ST_WALK;
                    a.t = 0.f;
                    G.anchor = G.center;
                    G.target = G.center;
                }
                break;
            }
            case ST_ALERT: {
                quadIdle(a, dt);
                quadLookAt(a, a.goal, dt);
                a.qa.headDown = Lerp(a.qa.headDown, 0.f, expDecay(4.f, dt));
                a.qa.alert = Lerp(a.qa.alert, 1.f, expDecay(4.f, dt));
                if (a.t > 6.f) {
                    a.state = ST_FEED;
                    a.t = 0.f;
                }
                break;
            }
            case ST_REST: {
                quadIdle(a, dt);
                a.qa.lie = Lerp(a.qa.lie, 1.f, expDecay(0.8f, dt));
                a.qa.headDown = Lerp(a.qa.headDown, 0.1f, expDecay(1.f, dt));
                if (a.t > a.timer) {
                    a.state = ST_FEED;
                    a.t = 0.f;
                }
                break;
            }
            case ST_WALK: {
                vec2 d = a.goal.xy() - a.pos.xy();
                float dist = length(d);
                a.qa.lie = Lerp(a.qa.lie, 0.f, expDecay(2.f, dt));
                a.qa.headDown = Lerp(a.qa.headDown, 0.f, expDecay(2.f, dt));
                a.qa.alert = Lerp(a.qa.alert, 0.f, expDecay(1.f, dt));
                quadMove(g, a, dist > 0.5f ? d / dist * qs.walk : vec2(0.f), dt, 2.f, 1.2f, 0);
                if (dist < 0.8f || a.t > 25.f) {
                    a.state = ST_FEED;
                    a.t = 0.f;
                }
                break;
            }
            default: {   // grazing: head down, a slow step now and then
                a.state = ST_FEED;
                a.qa.lie = Lerp(a.qa.lie, 0.f, expDecay(2.f, dt));
                a.qa.alert = Lerp(a.qa.alert, 0.f, expDecay(1.f, dt));
                a.qa.headDown = Lerp(a.qa.headDown, 1.f, expDecay(1.5f, dt));
                bool step = fmodf(a.life + (float)(a.uid & 7), 7.f) < 1.2f;
                vec2 fwd = dirOf(a.yaw);
                if (step) quadMove(g, a, fwd * 0.35f, dt, 1.f, 0.6f, 0);
                else quadIdle(a, dt);
                a.qa.mouth = (fmodf(a.life * 1.6f, 1.f) < 0.3f) ? 0.25f : 0.f;   // chewing
                if (a.t > frange(15.f, 40.f)) {
                    a.t = 0.f;
                    float r = frand();
                    if (r < (sp == SP_COW ? 0.2f : 0.06f) && (gW.tod > 11.f || gW.tod < 6.f)) {
                        a.state = ST_REST;
                        a.timer = frange(30.f, 90.f);
                    } else {
                        a.state = ST_WALK;
                        float ang = (float)k * 2.39996f;
                        a.goal = G.target + vec3(vec2(cosf(ang), sinf(ang)) * sqrtf((float)k + 1.f) * (sp == SP_DEER ? 2.5f : 4.f), 0.f) +
                                 vec3(gW.rng.inCircle() * 3.f, 0.f);
                    }
                }
                if (frand() < dt / 60.f && length2(a.pos - gW.cam) < 150.f * 150.f) {
                    if (sp == SP_COW) sfx(Audio::SFX_COW_MOO, a.pos + vec3(0, 0, 1.2f), 1.f, 0.9f + 0.2f * hashToFloat(a.uid));
                    if (sp == SP_HORSE) sfx(Audio::SFX_HORSE_NEIGH, a.pos + vec3(0, 0, 1.5f), 0.6f);
                }
                break;
            }
        }
        a.qa.tailWag = sp == SP_DEER ? 0.f : 0.3f;
        k++;
    }
    vec3 cen(0.f);
    int n = 0;
    for (int ai : G.members)
        if (gW.animals[ai].used && gW.animals[ai].state != ST_DEAD) {
            cen += gW.animals[ai].pos;
            n++;
        }
    if (n) G.center = cen / (float)n;
}

}  // namespace wild_detail

// ==================================================================================================================
// Spawning
namespace wild_detail {

struct TypeRule {
    float r0, r1, despawn;
    int maxGroups;
};
const TypeRule kRules[GT_COUNT] = {
    {70.f, 350.f, 650.f, 4},    // gulls
    {80.f, 400.f, 750.f, 2},    // pelicans
    {35.f, 170.f, 280.f, 3},    // pigeons
    {45.f, 260.f, 450.f, 6},    // waders
    {150.f, 600.f, 1200.f, 1},  // vultures
    {60.f, 260.f, 480.f, 2},    // parakeets
    {40.f, 220.f, 330.f, 5},    // alligators
    {25.f, 110.f, 180.f, 4},    // iguanas
    {100.f, 450.f, 850.f, 1},   // dolphins
    {30.f, 200.f, 320.f, 2},    // manatees
    {30.f, 200.f, 320.f, 2},    // sea turtles
    {8.f, 70.f, 120.f, 4},      // fish shoals
    {25.f, 110.f, 200.f, 3},    // leashed dogs
    {45.f, 160.f, 260.f, 2},    // stray dogs
    {25.f, 110.f, 180.f, 3},    // cats
    {25.f, 100.f, 170.f, 3},    // raccoons
    {70.f, 240.f, 380.f, 2},    // deer
    {70.f, 280.f, 470.f, 2},    // cattle
    {70.f, 280.f, 470.f, 1},    // horses
    {40.f, 220.f, 380.f, 3},    // sanderlings
    {35.f, 170.f, 280.f, 2},    // grackles
    {150.f, 650.f, 1300.f, 1},  // frigatebirds
    {60.f, 300.f, 550.f, 2},    // cormorants
    {70.f, 280.f, 470.f, 2},    // cattle egrets (with a cattle herd)
};

inline bool dayTime(float a = 7.f, float b = 19.f) { return gW.tod >= a && gW.tod <= b; }

// A site element of the given kinds near p (piers, marinas, docks...), or null
const World::SiteElem* siteNear(vec2 p, float r, std::initializer_list<int> kinds) {
    if (!World::gSites) return nullptr;
    const World::SiteElem* best = nullptr;
    float bd = r;
    for (const World::SiteElem& e : World::gSites->elems) {
        bool match = false;
        for (int k : kinds) match |= (int)e.kind == k;
        if (!match) continue;
        float d = length(e.c - p);
        if (d < bd) {
            bd = d;
            best = &e;
        }
    }
    return best;
}

float spawnHeight(GameWorld& g, vec2 p) {
    float wz;
    float gz = groundAt(g, p.x, p.y, g.map->heightAt(p.x, p.y) + 40.f);
    if (waterAt(p.x, p.y, wz)) gz = Max(gz, wz);
    return gz;
}

// ---- group spawners (also used by the test scenes) ----------------------------------------------------------------
int spawnFlock(GameWorld& g, GroupType t, int sp, vec3 anchor, int count, bool airborne) {
    int gi = newGroup(t, sp, anchor);
    if (gi < 0) return -1;
    Group& G = gW.groups[gi];
    bool pigeons = sp == SP_PIGEON, grackles = sp == SP_GRACKLE, cormorants = sp == SP_CORMORANT;
    G.radius = pigeons ? 12.f : (grackles ? 16.f : (cormorants ? 40.f : 60.f));
    if (pigeons) findPerches(g, G, 10.f, count + 6, true, false, false, false, false);
    else if (grackles) findPerches(g, G, 18.f, count + 8, true, false, true, true, true);        // lots, lamps, roofs, palms
    else if (cormorants) findPerches(g, G, 45.f, count + 6, false, true, true, true, false);     // pilings, rails, the water
    else findPerches(g, G, 55.f, count + 8, true, true, true, true, false);
    G.timer = frange(60.f, 150.f);
    for (int k = 0; k < count; k++) {
        int var = 0;
        if (sp == SP_GULL) var = frand() < 0.4f ? 1 : 0;
        if (sp == SP_PIGEON) var = frand() < 0.6f ? 0 : (frand() < 0.65f ? 1 : 2);
        if (sp == SP_GRACKLE) var = frand() < 0.55f ? 0 : 1;   // glossy males, smaller brown females
        int ai = newAnimal(gi, sp, var, anchor, frand() * kTwoPi);
        if (ai < 0) break;
        Animal& a = gW.animals[ai];
        a.scale = (sp == SP_GULL && var == 1 ? 0.78f : (sp == SP_GRACKLE && var == 1 ? 0.8f : 1.f)) * frange(0.92f, 1.08f);
        bool fly = airborne ? frand() < 0.65f : false;
        if (!fly) {
            a.perch = pickPerch(G, anchor, ai, false);
            if (a.perch >= 0) {
                const Perch& pc = G.perches[a.perch];
                a.pos = pc.pos + vec3(0, 0, birdStandHeight(a, pc.kind == 2));
                a.state = pc.kind == 2 ? ST_SWIM : ST_IDLE;
                a.timer = frange(10.f, 60.f);
                a.ba.fold = 1.f;
                a.ba.legs = pc.kind == 2 ? 0.f : 1.f;
                a.ba.sit = pc.kind == 2 ? 1.f : 0.f;
                continue;
            }
        }
        vec2 r = gW.rng.inCircle() * 40.f;
        a.pos = anchor + vec3(r, frange(8.f, 25.f));
        a.state = ST_FLY;
        a.vel = vec3(dirOf(frand() * kTwoPi) * 8.f, 0.f);
        a.timer = frange(10.f, 60.f);
    }
    return gi;
}

int spawnPelicanLine(GameWorld& g, vec3 anchor, int count) {
    int gi = newGroup(GT_PELICANS, SP_PELICAN, anchor);
    if (gi < 0) return -1;
    Group& G = gW.groups[gi];
    findPerches(g, G, 60.f, count + 6, false, true, true, true, false);
    G.timer = frange(40.f, 100.f);
    vec2 dir = dirOf(G.heading);
    for (int k = 0; k < count; k++) {
        int ai = newAnimal(gi, SP_PELICAN, 0, anchor, G.heading);
        if (ai < 0) break;
        Animal& a = gW.animals[ai];
        a.scale = frange(0.92f, 1.08f);
        a.pos = anchor + vec3(-dir * (4.5f * (float)k) + vec2(dir.y, -dir.x) * (1.1f * (float)k), 3.f);
        a.vel = vec3(dir * 9.f, 0.f);
        a.state = ST_FLY;
    }
    return gi;
}

int spawnWaders(GameWorld& g, int sp, vec3 spot, int count) {
    int gi = newGroup(GT_WADERS, sp, spot);
    if (gi < 0) return -1;
    Group& G = gW.groups[gi];
    G.timer = frange(60.f, 180.f);
    for (int k = 0; k < count; k++) {
        vec3 p = spot;
        if (k > 0) {
            vec3 s2;
            if (findShallowSpot(g, spot.xy(), 1.5f, 6.f + (float)count, 0.02f, 0.5f, s2)) p = s2;
        }
        int ai = newAnimal(gi, sp, 0, p, frand() * kTwoPi);
        if (ai < 0) break;
        Animal& a = gW.animals[ai];
        a.scale = frange(0.9f, 1.08f);
        a.standH = modelOf(a).legLen * a.scale;
        a.pos = vec3(p.xy(), g.map->heightAt(p.x, p.y) + a.standH);
        a.state = ST_IDLE;
        a.ba.fold = 1.f;
        a.ba.legs = 1.f;
    }
    return gi;
}

// Soaring birds circling in thermals: vultures over open country, frigatebirds over the coast
int spawnSoarers(GameWorld& g, GroupType t, int sp, vec3 anchor, int count) {
    int gi = newGroup(t, sp, anchor);
    if (gi < 0) return -1;
    Group& G = gW.groups[gi];
    G.timer = frange(60.f, 150.f);
    int var = sp == SP_VULTURE && frand() < 0.35f ? 1 : 0;
    for (int k = 0; k < count; k++) {
        if (sp == SP_FRIGATE) var = frand() < 0.5f ? 0 : 1;
        int ai = newAnimal(gi, sp, var, anchor, frand() * kTwoPi);
        if (ai < 0) break;
        Animal& a = gW.animals[ai];
        float ang = frand() * kTwoPi;
        a.pos = anchor + vec3(cosf(ang) * 45.f, sinf(ang) * 45.f, sp == SP_FRIGATE ? frange(30.f, 90.f) : frange(60.f, 150.f));
        a.vel = vec3(-sinf(ang) * 10.f, cosf(ang) * 10.f, 0.f);
        a.state = ST_FLY;
        a.scale = frange(0.95f, 1.05f);
    }
    return gi;
}

int spawnVultures(GameWorld& g, vec3 anchor, int count) { return spawnSoarers(g, GT_VULTURES, SP_VULTURE, anchor, count); }

int spawnShorebirds(GameWorld& g, vec3 edge, vec2 inlandN, int count) {
    int gi = newGroup(GT_SHOREBIRDS, SP_SANDPIPER, edge);
    if (gi < 0) return -1;
    Group& G = gW.groups[gi];
    G.shoreN = inlandN;
    G.heading = frand() < 0.5f ? -1.f : 1.f;   // drift direction along the beach
    G.timer = 4.f;
    vec2 T(inlandN.y, -inlandN.x);
    for (int k = 0; k < count; k++) {
        int ai = newAnimal(gi, SP_SANDPIPER, 0, edge, yawOf(-inlandN) + frange(-0.8f, 0.8f));
        if (ai < 0) break;
        Animal& a = gW.animals[ai];
        a.scale = frange(0.92f, 1.08f);
        a.standH = modelOf(a).legLen * a.scale;
        vec2 p = edge.xy() + T * ((birdHash(a, 41) - 0.5f) * 9.f) + inlandN * (0.6f + birdHash(a, 43) * 1.1f);
        a.pos = vec3(p, groundAt(g, p.x, p.y, edge.z + 2.f) + a.standH);
        a.state = ST_WALK;
        a.ba.fold = 1.f;
        a.ba.legs = 1.f;
    }
    return gi;
}

int spawnCattleEgrets(GameWorld& g, int herd, int count) {
    if (herd < 0 || herd >= (int)gW.groups.size() || !gW.groups[herd].used || gW.groups[herd].members.empty()) return -1;
    u32 hostUid = gW.groups[herd].uid;
    std::vector<int> cows = gW.groups[herd].members;   // copies: allocating groups / animals may move the arrays
    vec3 c0 = gW.animals[cows[0]].pos;
    int gi = newGroup(GT_CEGRETS, SP_CEGRET, c0);
    if (gi < 0) return -1;
    gW.groups[gi].host = herd;
    gW.groups[gi].hostUid = hostUid;
    for (int k = 0; k < count; k++) {
        int ci = cows[(size_t)k % cows.size()];
        vec3 cp = gW.animals[ci].pos;
        float cyaw = gW.animals[ci].yaw, cs = gW.animals[ci].scale;
        u32 cuid = gW.animals[ci].uid;
        vec2 f = dirOf(cyaw);
        vec2 p = cp.xy() + f * (1.4f * cs + frand()) + vec2(f.y, -f.x) * frange(-1.f, 1.f);
        int ai = newAnimal(gi, SP_CEGRET, 0, vec3(p, cp.z), yawOf(cp.xy() - p));
        if (ai < 0) break;
        Animal& a = gW.animals[ai];
        a.scale = frange(0.92f, 1.06f);
        a.standH = modelOf(a).legLen * a.scale;
        a.pos.z = groundAt(g, p.x, p.y, cp.z + 2.f) + a.standH;
        a.state = ST_IDLE;
        a.target = ci;
        a.targetUid = cuid;
        a.aux = frand() < 0.15f ? 1.f : 0.f;
        a.ba.fold = 1.f;
        a.ba.legs = 1.f;
    }
    return gi;
}

int spawnParrots(GameWorld& g, vec3 anchor, int count) {
    int gi = newGroup(GT_PARROTS, SP_PARROT, anchor);
    if (gi < 0) return -1;
    Group& G = gW.groups[gi];
    findPerches(g, G, 150.f, 24, false, false, false, false, true);
    int trees = 0;
    for (const Perch& p : G.perches) trees += p.kind == 4;
    if (trees < 2) {
        releaseGroup(gi);
        return -1;
    }
    int var = frand() < 0.3f ? 1 : 0;
    G.target = G.perches[gW.rng.next() % G.perches.size()].pos;
    for (int k = 0; k < count; k++) {
        int ai = newAnimal(gi, SP_PARROT, var, anchor, frand() * kTwoPi);
        if (ai < 0) break;
        Animal& a = gW.animals[ai];
        a.pos = anchor + vec3(gW.rng.inCircle() * 6.f, frange(12.f, 18.f));
        a.vel = vec3(dirOf(frand() * kTwoPi) * 10.f, 0.f);
        a.state = ST_FLY;
        a.scale = frange(0.95f, 1.05f);
    }
    return gi;
}

int spawnSingle(GameWorld& g, GroupType t, int sp, int var, vec3 pos, float yaw, float scale, u8 state) {
    int gi = newGroup(t, sp, pos);
    if (gi < 0) return -1;
    int ai = newAnimal(gi, sp, var, pos, yaw);
    if (ai < 0) {
        releaseGroup(gi);
        return -1;
    }
    Animal& a = gW.animals[ai];
    a.scale = scale;
    a.state = state;
    a.timer = frange(30.f, 90.f);
    a.goal = pos;
    a.aggression = frand();
    return gi;
}

int spawnGator(GameWorld& g, vec3 pos, bool basking) {
    int gi = spawnSingle(g, GT_GATOR, SP_GATOR, 0, pos, frand() * kTwoPi, frange(0.55f, 1.15f), basking ? ST_REST : ST_IDLE);
    if (gi >= 0) {
        Animal& a = gW.animals[gW.groups[gi].members[0]];
        a.timer = frange(30.f, 120.f);
        if (basking) {   // face the water
            vec3 w;
            if (findWater(g, pos.xy(), 15.f, 0.5f, w)) a.yaw = yawOf(w.xy() - pos.xy()) + frange(-0.6f, 0.6f) + (frand() < 0.4f ? kPi : 0.f);
        }
    }
    return gi;
}

int spawnHerd(GameWorld& g, GroupType t, int sp, vec3 anchor, int count) {
    int gi = newGroup(t, sp, anchor);
    if (gi < 0) return -1;
    Group& G = gW.groups[gi];
    G.radius = sp == SP_DEER ? 40.f : (sp == SP_COW ? 60.f : 45.f);
    G.timer = frange(30.f, 80.f);
    int herdVar = (int)(gW.rng.next() % (u32)speciesInfo(sp).variants);
    for (int k = 0; k < count; k++) {
        float ang = (float)k * 2.39996f;
        vec2 p = anchor.xy() + vec2(cosf(ang), sinf(ang)) * sqrtf((float)k + 1.f) * (sp == SP_DEER ? 2.5f : 4.5f);
        int var = sp == SP_DEER ? (k == 0 && frand() < 0.6f ? 1 : 0) : (frand() < 0.7f ? herdVar : (int)(gW.rng.next() % (u32)speciesInfo(sp).variants));
        int ai = newAnimal(gi, sp, var, vec3(p, groundAt(g, p.x, p.y, anchor.z + 5.f)), frand() * kTwoPi);
        if (ai < 0) break;
        Animal& a = gW.animals[ai];
        a.scale = sp == SP_DEER ? (var == 1 ? frange(1.f, 1.1f) : frange(0.88f, 1.f)) : frange(0.9f, 1.08f);
        a.state = ST_FEED;
        a.t = frand() * 20.f;
        a.groundT = 0.f;
    }
    return gi;
}

int spawnDogPack(GameWorld& g, vec3 anchor, int count) {
    int gi = newGroup(GT_DOG_STRAY, SP_DOG, anchor);
    if (gi < 0) return -1;
    for (int k = 0; k < count; k++) {
        int var = k == 0 ? 4 : (int)(gW.rng.next() % 5);
        if (var == 3 && frand() < 0.5f) var = 4;
        vec2 p = anchor.xy() + gW.rng.inCircle() * 4.f;
        int ai = newAnimal(gi, SP_DOG, var, vec3(p, anchor.z), frand() * kTwoPi);
        if (ai < 0) break;
        Animal& a = gW.animals[ai];
        a.scale = frange(0.9f, 1.05f);
        a.state = ST_WALK;
        a.goal = a.pos;
        a.aggression = frand();
    }
    return gi;
}

int attachLeashDog(GameWorld& g, int ped) {
    const Ped& p = g.peds[ped];
    vec3 pp = p.pos.toVec3();
    int gi = newGroup(GT_DOG_LEASH, SP_DOG, pp);
    if (gi < 0) return -1;
    Group& G = gW.groups[gi];
    G.owner = ped;
    G.ownerUid = p.uid;
    G.leashHand = frand() < 0.5f ? 1 : 0;
    int var = (int)(gW.rng.next() % 4);   // pets: labs, shepherd, terrier
    vec2 of = dirOf(p.yaw), orr(of.y, -of.x);
    vec2 dp = pp.xy() + orr * (G.leashHand ? 0.7f : -0.7f) + of * 0.5f;
    int ai = newAnimal(gi, SP_DOG, var, vec3(dp, pp.z), p.yaw);
    if (ai < 0) {
        releaseGroup(gi);
        return -1;
    }
    Animal& a = gW.animals[ai];
    a.scale = frange(0.9f, 1.05f);
    a.state = ST_WALK;
    a.aggression = 0.f;
    return gi;
}

int spawnShoal(GameWorld& g, vec3 anchor, int var, int count) {
    int gi = newGroup(GT_FISH, SP_FISH, anchor);
    if (gi < 0) return -1;
    Group& G = gW.groups[gi];
    G.variant = var;
    G.radius = var == 3 ? 12.f : 8.f;
    G.timer = 5.f;
    for (int k = 0; k < count; k++) {
        vec3 p = anchor + vec3(gW.rng.inCircle() * 2.5f, frange(-0.8f, 0.8f));
        int ai = newAnimal(gi, SP_FISH, var, p, frand() * kTwoPi);
        if (ai < 0) break;
        Animal& a = gW.animals[ai];
        a.scale = frange(0.8f, 1.15f);
        a.vel = vec3(dirOf(a.yaw) * 0.6f, 0.f);
        a.state = ST_SWIM;
    }
    return gi;
}

int spawnPod(GameWorld& g, vec3 anchor, int count) {
    int gi = newGroup(GT_DOLPHINS, SP_DOLPHIN, anchor);
    if (gi < 0) return -1;
    Group& G = gW.groups[gi];
    G.center = anchor;
    for (int k = 0; k < count; k++) {
        vec3 p = anchor + vec3(gW.rng.inCircle() * 6.f, -1.5f);
        int ai = newAnimal(gi, SP_DOLPHIN, 0, p, G.heading);
        if (ai < 0) break;
        Animal& a = gW.animals[ai];
        a.scale = frange(0.85f, 1.1f);
        a.state = ST_SWIM;
        a.timer = frange(0.5f, 8.f);
    }
    return gi;
}

// Picks a place for a new group of type t around the focus; returns the group or -1.
int trySpawn(GameWorld& g, GroupType t, bool warm) {
    const TypeRule& R = kRules[t];
    vec2 focus = gW.cam.xy();
    World::Region here = g.map->regionAt(focus.x, focus.y);
    bool rainy = gW.rain > 0.5f;
    // quick prerequisites by time / weather
    switch (t) {
        case GT_PIGEONS: if (!dayTime(6.5f, 19.5f) || rainy) return -1; break;
        case GT_VULTURES: if (!dayTime(8.5f, 18.f) || gW.rain > 0.2f) return -1; break;
        case GT_PARROTS: if (!dayTime(7.f, 19.f) || rainy) return -1; break;
        case GT_IGUANA: if (!dayTime(8.5f, 18.5f) || gW.rain > 0.2f) return -1; break;
        case GT_RACCOON: if (dayTime(5.5f, 20.5f)) return -1; break;
        case GT_DOG_LEASH: if (!dayTime(6.5f, 21.5f) || rainy) return -1; break;
        case GT_WADERS: if (!dayTime(6.f, 20.f)) return -1; break;
        case GT_GRACKLES: if (!dayTime(6.5f, 19.5f) || rainy) return -1; break;
        case GT_FRIGATES: if (!dayTime(8.f, 18.5f) || gW.rain > 0.3f) return -1; break;
        case GT_CORMORANTS: if (!dayTime(6.f, 20.f)) return -1; break;
        case GT_CEGRETS: if (!dayTime(6.5f, 19.5f)) return -1; break;
        case GT_FISH: {
            float wz;
            bool nearWater = false;
            for (int k = 0; k < 6 && !nearWater; k++) {
                vec2 p = focus + gW.rng.inCircle() * 40.f;
                if (waterAt(p.x, p.y, wz) && gW.cam.z - wz < 45.f) nearWater = true;
            }
            if (!nearWater) return -1;
            break;
        }
        default: break;
    }
    (void)here;
    for (int attempt = 0; attempt < 10; attempt++) {
        float ang = frand() * kTwoPi, r = frange(R.r0, R.r1);
        vec2 p = focus + vec2(cosf(ang), sinf(ang)) * r;
        if (fabsf(p.x) > World::kWorldHalf - 100.f || fabsf(p.y) > World::kWorldHalf - 100.f) continue;
        World::Region reg = g.map->regionAt(p.x, p.y);
        float sd = g.map->coastDistance(p.x, p.y);
        bool seen = !warm && g.inCameraView(vec3(p, spawnHeight(g, p) + 1.f), 4.f);
        switch (t) {
            case GT_GULLS: {
                if (sd < -160.f || sd > 50.f) break;
                const World::SiteElem* site = siteNear(p, 250.f, {World::SK_BEACH_PIER, World::SK_MARINA, World::SK_DOCK, World::SK_QUAY, World::SK_FISH_SHACK, World::SK_RIVER_MARINA});
                vec2 a = site && frand() < 0.7f ? site->c + gW.rng.inCircle() * 25.f : p;
                if (seen && length(a - focus) < 150.f) break;
                int n = (int)frange(10.f, gW.tod > 20.f || gW.tod < 6.f ? 14.f : 26.f);
                return spawnFlock(g, GT_GULLS, SP_GULL, vec3(a, spawnHeight(g, a)), n, dayTime(6.f, 20.f));
            }
            case GT_PELICANS: {
                if (sd < -140.f || sd > 5.f || waterDepth(*g.map, p.x, p.y) < 0.8f) break;
                if (seen && r < 150.f) break;
                return spawnPelicanLine(g, vec3(p, spawnHeight(g, p)), (int)frange(3.f, 7.99f));
            }
            case GT_PIGEONS: {
                const World::SiteElem* plaza = siteNear(focus, 350.f, {World::SK_FOUNTAIN, World::SK_PARK, World::SK_CITY_HALL});
                vec2 a = p;
                if (plaza && frand() < 0.6f) a = plaza->c + gW.rng.inCircle() * Min(plaza->hx, 40.f);
                World::Region ra = g.map->regionAt(a.x, a.y);
                if (!urbanRegion(ra) && !plaza) break;
                float rz;
                if (g.roads->surfaceHeight(a, &rz) || g.buildings->pointInBuilding(a, 1.f) || waterDepth(*g.map, a.x, a.y) > 0.f) break;
                if (!plaza && !g.roads->nearRoad(a, 12.f)) break;
                if (seen && length(a - focus) < 90.f) break;
                return spawnFlock(g, GT_PIGEONS, SP_PIGEON, vec3(a, groundAt(g, a.x, a.y, g.map->heightAt(a.x, a.y) + 3.f)), (int)frange(12.f, 30.f), false);
            }
            case GT_WADERS: {
                bool ok = reg == World::REG_SAWGRASS || reg == World::REG_KEYS || reg == World::REG_LAKE_TOWN || reg == World::REG_REDLAND ||
                          reg == World::REG_FARMLAND || reg == World::REG_GULF_TOWN || reg == World::REG_KEY_CORAL || reg == World::REG_RIDGE;
                if (!ok) break;
                vec3 spot;
                if (!findShallowSpot(g, p, 0.f, 40.f, 0.03f, 0.4f, spot)) break;
                if (seen && length(spot.xy() - focus) < 160.f) break;
                float rr = frand();
                int sp = SP_HERON, n = 1;
                bool south = reg == World::REG_KEYS || (reg == World::REG_SAWGRASS && p.y < -3000.f);
                if (south && rr < 0.35f) { sp = SP_FLAMINGO; n = (int)frange(6.f, 14.f); }
                else if (rr < 0.3f) { sp = SP_HERON; n = frand() < 0.2f ? 2 : 1; }
                else if (rr < 0.55f) { sp = SP_EGRET; n = (int)frange(1.f, 3.99f); }
                else if (rr < 0.75f) { sp = SP_SPOONBILL; n = (int)frange(3.f, 7.99f); }
                else { sp = SP_IBIS; n = (int)frange(5.f, 11.99f); }
                return spawnWaders(g, sp, spot, n);
            }
            case GT_VULTURES: {
                if (!wildRegion(reg) && reg != World::REG_HARLOW) break;
                return spawnVultures(g, vec3(p, g.map->heightAt(p.x, p.y)), (int)frange(4.f, 9.99f));
            }
            case GT_PARROTS: {
                if (!(suburbRegion(reg) || reg == World::REG_CALLE_LUNA || reg == World::REG_MIDTOWN || reg == World::REG_NORTH_CITY || reg == World::REG_BEACH)) break;
                if (length(p - gW.player.xy()) > 330.f) break;
                if (seen && r < 150.f) break;
                return spawnParrots(g, vec3(p, spawnHeight(g, p)), (int)frange(8.f, 18.f));
            }
            case GT_GATOR: {
                if (!(reg == World::REG_SAWGRASS || reg == World::REG_REDLAND || reg == World::REG_FARMLAND || reg == World::REG_RIDGE)) break;
                if (waterDepth(*g.map, p.x, p.y) < 0.9f) break;
                if (g.roads->nearRoad(p, 30.f) || g.buildings->pointInBuilding(p, 25.f)) break;
                vec3 bank;
                bool bask = dayTime(8.f, 17.f) && frand() < 0.6f && findBank(g, p, 18.f, bank);
                vec3 pos = bask ? bank : vec3(p, g.map->waterAt(p.x, p.y) - 0.4f);
                if (seen && length(pos.xy() - focus) < 120.f) break;
                if (bask && g.roads->nearRoad(bank.xy(), 25.f)) break;
                return spawnGator(g, pos, bask);
            }
            case GT_IGUANA: {
                if (!(suburbRegion(reg) || reg == World::REG_BEACH || reg == World::REG_KEYS || reg == World::REG_KEY_TOWN || reg == World::REG_CALLE_LUNA)) break;
                if (waterDepth(*g.map, p.x, p.y) > -0.05f) break;
                vec3 w;
                if (!findWater(g, p, 4.f, 0.4f, w)) break;
                if (onRoadOrBuilding(g, p, 1.5f, 1.f) || seen) break;
                int gi = spawnSingle(g, GT_IGUANA, SP_IGUANA, frand() < 0.3f ? 1 : 0, vec3(p, groundAt(g, p.x, p.y, g.map->heightAt(p.x, p.y) + 2.f)),
                                     yawOf(w.xy() - p) + kPi + frange(-1.f, 1.f), frange(0.75f, 1.1f), ST_REST);
                return gi;
            }
            case GT_DOLPHINS: {
                if (waterDepth(*g.map, p.x, p.y) < 5.f || sd > -60.f) break;
                return spawnPod(g, vec3(p, g.map->waterAt(p.x, p.y) - 1.5f), (int)frange(3.f, 7.99f));
            }
            case GT_MANATEE: case GT_TURTLE: {
                bool manatee = t == GT_MANATEE;
                float d = waterDepth(*g.map, p.x, p.y);
                if (manatee ? (d < 1.5f || d > 6.f || sd < -90.f) : (d < 2.f || d > 16.f)) break;
                if (!manatee && !(reg == World::REG_KEYS || reg == World::REG_KEY_CORAL || reg == World::REG_BEACH || reg == World::REG_KEY_TOWN || reg == World::REG_OCEAN)) break;
                if (manatee && reg == World::REG_OCEAN && sd < -40.f) break;
                int gi = spawnSingle(g, manatee ? GT_MANATEE : GT_TURTLE, manatee ? SP_MANATEE : SP_TURTLE, (int)(gW.rng.next() & 1),
                                     vec3(p, g.map->waterAt(p.x, p.y) - (manatee ? 0.9f : 2.f)), frand() * kTwoPi, frange(0.85f, 1.1f), ST_SWIM);
                if (gi >= 0 && manatee && frand() < 0.35f) {   // a mother with her calf
                    int ai = newAnimal(gi, SP_MANATEE, 0, gW.animals[gW.groups[gi].members[0]].pos + vec3(1.5f, -1.f, 0.2f), 0.f);
                    if (ai >= 0) {
                        gW.animals[ai].scale = 0.55f;
                        gW.animals[ai].state = ST_SWIM;
                    }
                }
                return gi;
            }
            case GT_FISH: {
                vec2 q = focus + gW.rng.inCircle() * R.r1;
                float d = waterDepth(*g.map, q.x, q.y);
                if (d < 1.6f || d > 14.f) break;
                World::Region rq = g.map->regionAt(q.x, q.y);
                bool reef = rq == World::REG_KEYS || rq == World::REG_KEY_CORAL || rq == World::REG_KEY_TOWN;
                bool structure = siteNear(q, 80.f, {World::SK_BEACH_PIER, World::SK_DOCK, World::SK_MARINA, World::SK_RIVER_MARINA}) != nullptr;
                int var = 2;
                float rr = frand();
                if (structure && rr < 0.12f) var = 3;
                else if (reef || structure) var = rr < 0.55f ? 0 : 1;
                float wz = g.map->waterAt(q.x, q.y);
                int n = var == 3 ? (int)frange(3.f, 6.99f) : (int)frange(18.f, 48.f);
                return spawnShoal(g, vec3(q, wz - Min(d * 0.5f, 3.f)), var, n);
            }
            case GT_DOG_LEASH: {
                // a pedestrian out walking their dog
                for (int pi : gW.pedsNear) {
                    const Ped& pd = g.peds[pi];
                    if (pd.isPlayer || pd.persistent || pd.faction != FAC_CIVILIAN || pd.state != PS_ONFOOT || pd.brain.type != BRAIN_WANDER || pd.health <= 0.f) continue;
                    if (pi >= (int)g.ai.ped.size() || g.ai.ped[pi].uid != pd.uid) continue;
                    const PedAI& pa = g.ai.ped[pi];
                    if (pa.activity != ACT_WALK || pa.eventId >= 0 || pa.leader >= 0) continue;
                    vec3 pp = pd.pos.toVec3();
                    float dist = length(pp.xy() - gW.player.xy());
                    if (dist < R.r0 || dist > R.r1) continue;
                    World::Region rp = g.map->regionAt(pp.x, pp.y);
                    if (!(urbanRegion(rp) || suburbRegion(rp)) || rp == World::REG_PORT || rp == World::REG_AIRPORT) continue;
                    if (!warm && g.inCameraView(pp + vec3(0, 0, 1.f), 2.f) && dist < 70.f) continue;
                    bool taken = false;
                    for (const Group& G2 : gW.groups)
                        if (G2.used && G2.type == GT_DOG_LEASH && G2.owner == pi) taken = true;
                    if (taken || frand() > 0.35f) continue;
                    return attachLeashDog(g, pi);
                }
                return -1;
            }
            case GT_DOG_STRAY: {
                bool ok = reg == World::REG_FLATS || reg == World::REG_GULF_TOWN || reg == World::REG_HARLOW || reg == World::REG_REDLAND ||
                          (reg == World::REG_CALLE_LUNA && (gW.tod > 21.f || gW.tod < 5.f));
                if (!ok) break;
                float rz;
                if (g.roads->surfaceHeight(p, &rz) || g.buildings->pointInBuilding(p, 1.5f) || waterDepth(*g.map, p.x, p.y) > -0.1f) break;
                if (!g.roads->nearRoad(p, 30.f) || seen) break;
                return spawnDogPack(g, vec3(p, groundAt(g, p.x, p.y, g.map->heightAt(p.x, p.y) + 3.f)), (int)frange(1.f, 3.99f));
            }
            case GT_CAT: {
                if (!(suburbRegion(reg) || reg == World::REG_CALLE_LUNA || reg == World::REG_NORTH_CITY || reg == World::REG_KEY_TOWN || reg == World::REG_BEACH)) break;
                std::vector<int> bl;
                g.buildings->buildingsNear(p, 50.f, bl);
                for (int bi : bl) {
                    const World::Building& b = g.buildings->buildings[bi];
                    if (b.style != World::BS_HOUSE && b.style != World::BS_VILLA && b.style != World::BS_SHACK && b.style != World::BS_MOTEL) continue;
                    vec2 porch = b.c + b.front * (b.hy + 1.1f) + b.ax * frange(-b.hx * 0.6f, b.hx * 0.6f);
                    if (onRoadOrBuilding(g, porch, 1.5f, 0.3f)) continue;
                    vec3 cp = vec3(porch, groundAt(g, porch.x, porch.y, b.baseZ + 2.f));
                    if (!warm && g.inCameraView(cp + vec3(0, 0, 0.3f), 1.f) && length(cp.xy() - focus) < 60.f) continue;
                    int var = frand() < 0.4f ? 0 : (frand() < 0.5f ? 1 : 2);
                    return spawnSingle(g, GT_CAT, SP_CAT, var, cp, yawOf(b.front) + frange(-0.8f, 0.8f), frange(0.9f, 1.1f), ST_REST);
                }
                break;
            }
            case GT_RACCOON: {
                if (!(urbanRegion(reg) || suburbRegion(reg))) break;
                if (!Phys::gCollision || length(p - gW.player.xy()) > 300.f) break;
                std::vector<int> ids;
                Phys::gCollision->collidersNear(p, 40.f, ids);
                for (int id : ids) {
                    const Phys::Collider& c = Phys::gCollision->collider(id);
                    if (c.owner < 0 || c.kind != Phys::COL_BOX || !(c.flags & 1)) continue;
                    bool dumpster = fabsf(c.he.x - 0.9f) < 0.05f && fabsf(c.he.y - 0.6f) < 0.05f;
                    bool bin = fabsf(c.he.x - 0.3f) < 0.05f && fabsf(c.he.z - 0.5f) < 0.05f;
                    if (!dumpster && !bin) continue;
                    vec2 side = perp(c.ax);
                    vec2 at = c.c.xy() + side * (c.he.y + 0.35f) * (frand() < 0.5f ? -1.f : 1.f);
                    vec3 rp = vec3(at, c.c.z - c.he.z);
                    if (!warm && g.inCameraView(rp + vec3(0, 0, 0.3f), 1.f) && length(at - focus) < 50.f) continue;
                    int n = dumpster ? (int)frange(1.f, 3.99f) : 1;
                    int gi = newGroup(GT_RACCOON, SP_RACCOON, rp);
                    if (gi < 0) return -1;
                    gW.groups[gi].target = vec3(c.c.xy(), rp.z);
                    for (int k = 0; k < n; k++) {
                        int ai = newAnimal(gi, SP_RACCOON, 0, rp + vec3(gW.rng.inCircle() * 1.2f, 0.f), yawOf(c.c.xy() - at));
                        if (ai >= 0) {
                            gW.animals[ai].state = ST_FEED;
                            gW.animals[ai].timer = frange(5.f, 20.f);
                            gW.animals[ai].scale = frange(0.85f, 1.1f);
                        }
                    }
                    return gi;
                }
                break;
            }
            case GT_SHOREBIRDS: {
                bool ok = reg == World::REG_BEACH || reg == World::REG_KEYS || reg == World::REG_KEY_CORAL || reg == World::REG_KEY_TOWN || reg == World::REG_GULF_TOWN;
                if (!ok || sd < -20.f || sd > 40.f) break;
                vec3 edge;
                vec2 n;
                if (!findShoreEdge(g, p, edge, n)) break;
                if (seen && length(edge.xy() - focus) < 120.f) break;
                if (siteNear(edge.xy(), 25.f, {World::SK_BEACH_PIER, World::SK_MARINA, World::SK_DOCK, World::SK_QUAY})) break;
                return spawnShorebirds(g, edge, n, (int)frange(6.f, 18.99f));
            }
            case GT_GRACKLES: {
                if (!(urbanRegion(reg) || suburbRegion(reg) || reg == World::REG_BEACH || reg == World::REG_KEY_TOWN)) break;
                float rz;
                if (g.roads->surfaceHeight(p, &rz) || g.buildings->pointInBuilding(p, 1.f) || waterDepth(*g.map, p.x, p.y) > 0.f) break;
                if (!g.roads->nearRoad(p, 15.f)) break;   // parking lots and verges
                if (seen && r < 90.f) break;
                return spawnFlock(g, GT_GRACKLES, SP_GRACKLE, vec3(p, groundAt(g, p.x, p.y, g.map->heightAt(p.x, p.y) + 3.f)), (int)frange(5.f, 14.99f), false);
            }
            case GT_FRIGATES: {
                if (sd < -350.f || sd > 80.f) break;
                return spawnSoarers(g, GT_FRIGATES, SP_FRIGATE, vec3(p, 0.f), (int)frange(1.f, 5.99f));
            }
            case GT_CORMORANTS: {
                const World::SiteElem* site = siteNear(p, 220.f, {World::SK_MARINA, World::SK_DOCK, World::SK_QUAY, World::SK_RIVER_MARINA, World::SK_BEACH_PIER});
                vec2 a;
                if (site) a = site->c + gW.rng.inCircle() * 20.f;
                else if (reg == World::REG_SAWGRASS && waterDepth(*g.map, p.x, p.y) > 1.2f) a = p;
                else break;
                if (seen && length(a - focus) < 120.f) break;
                return spawnFlock(g, GT_CORMORANTS, SP_CORMORANT, vec3(a, spawnHeight(g, a)), (int)frange(2.f, 7.99f), false);
            }
            case GT_CEGRETS: {   // join a cattle herd that has no egrets yet
                for (int hi = 0; hi < (int)gW.groups.size(); hi++) {
                    const Group& H = gW.groups[hi];
                    if (!H.used || H.type != GT_CATTLE || H.members.empty()) continue;
                    bool has = false;
                    for (const Group& E : gW.groups) has |= E.used && E.type == GT_CEGRETS && E.host == hi && E.hostUid == H.uid;
                    if (has || frand() < 0.3f) continue;
                    if (!warm && g.inCameraView(H.center + vec3(0, 0, 1.f), 6.f) && length(H.center.xy() - focus) < 80.f) continue;
                    return spawnCattleEgrets(g, hi, (int)frange(2.f, 6.99f));
                }
                return -1;
            }
            case GT_DEER: case GT_CATTLE: case GT_HORSES: {
                int sp = t == GT_DEER ? SP_DEER : (t == GT_CATTLE ? SP_COW : SP_HORSE);
                bool ok;
                if (t == GT_DEER) ok = reg == World::REG_RIDGE || ((reg == World::REG_REDLAND || reg == World::REG_FARMLAND) && (gW.tod < 8.5f || gW.tod > 18.f));
                else if (t == GT_CATTLE) ok = reg == World::REG_FARMLAND || reg == World::REG_REDLAND;
                else ok = reg == World::REG_FARMLAND || reg == World::REG_HARLOW || reg == World::REG_REDLAND;
                if (!ok) break;
                if (waterDepth(*g.map, p.x, p.y) > -0.2f || g.map->normalAt(p.x, p.y).z < (sp == SP_DEER ? 0.85f : 0.95f)) break;
                if (onRoadOrBuilding(g, p, sp == SP_DEER ? 12.f : 22.f, 18.f) || seen) break;
                int n = sp == SP_DEER ? (int)frange(2.f, 6.99f) : (sp == SP_COW ? (int)frange(5.f, 12.99f) : (int)frange(2.f, 4.99f));
                return spawnHerd(g, t, sp, vec3(p, groundAt(g, p.x, p.y, g.map->heightAt(p.x, p.y) + 3.f)), n);
            }
            default: break;
        }
    }
    return -1;
}

void despawnFar(GameWorld& g, float dt) {
    for (int gi = 0; gi < (int)gW.groups.size(); gi++) {
        Group& G = gW.groups[gi];
        if (!G.used) continue;
        // reference point: the centroid of the members
        vec3 c(0.f);
        int n = 0, dead = 0;
        for (int ai : G.members) {
            const Animal& a = gW.animals[ai];
            if (!a.used) continue;
            c += a.pos;
            n++;
            if (a.state == ST_DEAD) dead++;
        }
        if (n == 0) {
            releaseGroup(gi);
            continue;
        }
        c = c / (float)n;
        if (G.type != GT_DOLPHINS) G.center = c;
        float d = length(c.xy() - gW.cam.xy());
        bool seen = g.inCameraView(c + vec3(0, 0, 1.f), 3.f);
        float limit = kRules[G.type].despawn;
        bool drop = d > limit;
        if (dead == n && d > 50.f && !seen) G.ageOut += dt;
        else if (G.type == GT_DOG_LEASH && !pedOk(g, G.owner, G.ownerUid) && !seen && d > 40.f) G.ageOut += dt;
        else if (G.type == GT_FISH && d > 60.f) G.ageOut += dt;
        else G.ageOut = 0.f;
        if (G.ageOut > (dead == n ? 30.f : 5.f)) drop = true;
        if (drop) releaseGroup(gi);
    }
}

void spawner(GameWorld& g, float dt) {
    gW.spawnTimer -= dt;
    bool warm = g.populationWarmup > 0.f;
    if (gW.spawnTimer > 0.f && !warm) return;
    gW.spawnTimer = 0.35f;
    int total = 0;
    for (const Animal& a : gW.animals) total += a.used;
    int types = warm ? (int)GT_COUNT : 2;
    for (int k = 0; k < types; k++) {
        GroupType t = (GroupType)(gW.spawnCursor++ % (int)GT_COUNT);
        int have = 0;
        for (const Group& G : gW.groups)
            if (G.used && G.type == t) have++;
        if (have >= kRules[t].maxGroups || total > 520) continue;
        int gi = trySpawn(g, t, warm);
        if (gi >= 0) total += (int)gW.groups[gi].members.size();
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Vehicles running animals over (and big animals denting the vehicles)
void vehicleImpacts(GameWorld& g) {
    for (int vi : gW.vehNear) {
        Vehicle& v = g.vehicles[vi];
        if (!v.used || (g.isAircraft(vi) && v.sim.agl > 2.5f)) continue;
        vec3 vel = v.sim.body.vel;
        float sp = length(vel);
        if (sp < 2.f) continue;
        const Vehicles::VehicleModel& spec = g.vassets[v.model].spec;
        mat3 R = v.sim.body.rotMat();
        mat3 Rt = transpose(R);
        vec3 c = v.sim.body.pos.toVec3() + R * spec.boxCenter;
        float reach = length(spec.boxHalf) + 2.5f;
        bool boat = g.isBoat(vi);
        for (int ai = 0; ai < (int)gW.animals.size(); ai++) {
            Animal& a = gW.animals[ai];
            if (!a.used || a.state == ST_DEAD || a.state == ST_FALL) continue;
            const SpeciesInfo& si = speciesInfo(a.sp);
            bool aquatic = si.plan == PLAN_CETACEAN || si.plan == PLAN_FISH || si.plan == PLAN_TURTLE;
            if (aquatic != boat && !(boat && (a.sp == SP_GATOR || isBird(a.sp)))) continue;
            if (isBird(a.sp) && (a.state == ST_FLY || a.state == ST_TAKEOFF) && a.pos.z - a.groundZ > 1.5f) continue;
            vec3 body = a.pos + vec3(0, 0, isBird(a.sp) ? 0.f : si.height * 0.45f * a.scale);
            vec3 d = body - c;
            if (length2(d) > reach * reach) continue;
            vec3 l = Rt * d;
            float r = (si.plan == PLAN_BIRD ? 0.15f : si.length * 0.25f) * a.scale;
            if (fabsf(l.x) > spec.boxHalf.x + r || fabsf(l.y) > spec.boxHalf.y + r || fabsf(l.z) > spec.boxHalf.z + si.height * 0.5f * a.scale) continue;
            vec3 rv = vel - a.vel;
            float rs = length(rv);
            if (rs < 2.f) {   // slow push: move the animal aside
                vec3 side = R * vec3(l.x >= 0.f ? 1.f : -1.f, 0.f, 0.f);
                a.pos += side * (0.6f * g.dtLast * 3.f);
                continue;
            }
            vec3 dir = normalize(rv + vec3(0, 0, rs * 0.3f));
            float mass = si.mass * a.scale * a.scale * a.scale;
            bool big = mass > 60.f;
            float killSpeed = big ? 9.f : 3.5f;
            vec3 imp = dir * rs * (big ? 0.45f : 1.1f);
            if (rs > killSpeed) killAnimal(g, ai, imp, v.seats[0], true);
            else hurtAnimal(g, ai, rs * (big ? 6.f : 12.f), imp, v.seats[0], true);
            if (big) {
                // the car takes the hit too
                vec3 pr = body - v.sim.body.pos.toVec3();
                vec3 j = -normalize(vec3(rv.xy(), 0.f)) * Min(mass, 500.f) * rs * 0.55f;
                v.sim.body.applyImpulse(j, pr);
                v.sim.sleeping = false;
                g.damageVehicle(vi, Min(mass, 500.f) * rs * 0.012f, -1, pr, j);
                sfx(Audio::SFX_CAR_CRASH_LIGHT, body, Saturate(rs / 12.f) + 0.3f);
                if (vi == gW.playerVeh) {
                    g.rumble(0.8f, 0.6f);
                    g.rig.shake = Max(g.rig.shake, Saturate(rs / 20.f));
                }
            } else {
                sfx(Audio::SFX_BODY_FALL, body, 0.5f, 1.4f);
            }
            // a dog run over while its owner watches
            if (a.group >= 0 && gW.groups[a.group].type == GT_DOG_LEASH) {
                Group& G = gW.groups[a.group];
                if (pedOk(g, G.owner, G.ownerUid)) {
                    Ped& o = g.peds[G.owner];
                    o.brain.type = BRAIN_FLEE;
                    o.brain.goal = v.sim.body.pos;
                    o.brain.timer = 3.f;
                    g.aiSay(G.owner, BK_PANIC, 1.f, true);
                }
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Hit volumes: a capsule along the body (flying birds: a sphere covering the wings)
void hitCapsule(const Animal& a, vec3& A, vec3& B, float& r) {
    const SpeciesInfo& si = speciesInfo(a.sp);
    float s = a.scale;
    if (si.plan == PLAN_BIRD) {
        bool flying = a.state == ST_FLY || a.state == ST_TAKEOFF || a.state == ST_LAND || a.state == ST_DIVE;
        A = worldPoint(a, vec3(0, si.length * 0.3f, 0.f));
        B = worldPoint(a, vec3(0, -si.length * 0.25f, 0.f));
        r = flying ? si.wingspan * 0.28f * s : si.length * 0.22f * s;
        return;
    }
    float zc = (si.plan == PLAN_QUAD) ? si.height * 0.62f : (si.plan == PLAN_REPTILE ? 0.3f : 0.f);
    A = worldPoint(a, vec3(0, si.length * 0.36f, zc));
    B = worldPoint(a, vec3(0, -si.length * (si.plan == PLAN_REPTILE ? 0.25f : 0.34f), zc));
    r = (si.plan == PLAN_QUAD ? Max(si.height * 0.3f, 0.12f) : (si.plan == PLAN_REPTILE ? 0.22f : si.height * 0.5f)) * s;
}

// Closest approach between the ray o + d t (t in [0, tMax]) and segment AB: returns t, sets the distance
float raySegment(vec3 o, vec3 d, float tMax, vec3 A, vec3 B, float& dist) {
    vec3 u = B - A, w = o - A;
    float a = dot(d, d), b = dot(d, u), c = dot(u, u), dd = dot(d, w), e = dot(u, w);
    float den = a * c - b * b;
    float t, s;
    if (den < 1e-8f) {
        s = 0.f;
        t = -dd / Max(a, 1e-8f);
    } else {
        t = (b * e - c * dd) / den;
        s = (a * e - b * dd) / den;
    }
    s = Saturate(s);
    t = Clamp(dot(A + u * s - o, d) / Max(a, 1e-8f), 0.f, tMax);
    vec3 pr = o + d * t, ps = A + u * Saturate(dot(pr - A, u) / Max(c, 1e-8f));
    dist = length(pr - ps);
    return t;
}

}  // namespace wild_detail

// ==================================================================================================================
// Rendering
namespace wild_detail {

struct LodDist {
    float lod0, lod1, draw, shadow;
};
LodDist lodDist(int sp) {
    const SpeciesInfo& si = speciesInfo(sp);
    switch (si.plan) {
        case PLAN_BIRD: {
            float s = Max(si.wingspan, 0.5f);
            float draw = sp == SP_VULTURE || sp == SP_FRIGATE ? 1500.f
                                                               : (sp == SP_PIGEON || sp == SP_SANDPIPER || sp == SP_GRACKLE ? 380.f : (sp == SP_PARROT ? 420.f : 950.f));
            return {Clamp(s * 22.f, 14.f, 45.f), Clamp(s * 120.f, 90.f, 260.f), draw, 30.f};
        }
        case PLAN_QUAD: return {si.length * 12.f + 15.f, 0.f, sp == SP_CAT || sp == SP_RACCOON ? 150.f : (sp == SP_DOG ? 230.f : 500.f), 60.f};
        case PLAN_REPTILE: return {sp == SP_GATOR ? 45.f : 20.f, 0.f, sp == SP_GATOR ? 320.f : 140.f, 60.f};
        case PLAN_FISH: return {25.f, 0.f, 90.f, 0.f};
        default: return {40.f, 0.f, sp == SP_DOLPHIN ? 500.f : 300.f, 40.f};
    }
}

void poseAnimal(GameWorld& g, Animal& a, const Group* G, bool force) {
    int interval = a.visDist < 45.f ? 1 : (a.visDist < 160.f ? 2 : (a.visDist < 420.f ? 4 : 8));
    if (G && G->type == GT_DOG_LEASH && a.visDist < 80.f) interval = 1;
    if (!force && gW.frame - a.poseFrame < interval) return;
    a.poseFrame = gW.frame;
    const ModelData& md = modelOf(a);
    Pose P;
    Frames F;
    switch (speciesInfo(a.sp).plan) {
        case PLAN_BIRD: animateBird(md, a.ba, P); break;
        case PLAN_QUAD: animateQuad(md, a.qa, P); break;
        case PLAN_REPTILE: animateReptile(md, a.ra, P); break;
        default: animateSwimmer(md, a.sa, P); break;
    }
    poseSkeleton(md.skel, P, a.skin, &F);
    if (a.sp == SP_DOG) {
        bool held = G && G->type == GT_DOG_LEASH && G->hasHand && a.state != ST_DEAD && a.state != ST_FALL;
        vec3 hand(0.f);
        if (held) hand = rotate(conj(animalQuat(a)), G->hand - a.pos) * (1.f / Max(a.scale, 0.1f));
        leashMatrices(md, F, hand, 1.75f / Max(a.scale, 0.1f), !held, a.skin);
    }
}

void submitAll(GameWorld& g) {
    Render::DynamicRenderer* dyn = g.renderer->dynamic;
    vec3 cam = g.rig.cam.pos.toVec3();
    vec3 camF = g.rig.cam.forward();
    gW.arena.clear();
    const size_t cap = gW.arena.capacity();
    gW.drawn = 0;
    gW.drawCalls = 0;
    float camWater;
    bool camNearWater = waterAt(cam.x, cam.y, camWater) && cam.z - camWater < 35.f;
    // per animal: LOD choice (0 individual detailed, 1 individual reduced, 2 batch mid, 3 batch far, -1 hidden)
    static std::vector<signed char> lod;
    lod.assign(gW.animals.size(), -1);
    for (int ai = 0; ai < (int)gW.animals.size(); ai++) {
        Animal& a = gW.animals[ai];
        if (!a.used) continue;
        const SpeciesInfo& si = speciesInfo(a.sp);
        vec3 to = a.pos - cam;
        float d = length(to);
        a.visDist = d;
        LodDist L = lodDist(a.sp);
        if (d > L.draw) continue;
        float size = Max(si.plan == PLAN_BIRD ? si.wingspan : si.length, 0.3f) * a.scale;
        if (dot(to, camF) < -size * 1.5f && d > size * 3.f) continue;   // behind the camera
        if (si.plan == PLAN_FISH && !camNearWater) continue;
        bool batchable = si.plan == PLAN_BIRD || si.plan == PLAN_FISH;
        if (batchable) lod[ai] = (d < L.lod0 && si.plan == PLAN_BIRD) ? 0 : (d < (si.plan == PLAN_FISH ? L.lod0 : L.lod1) ? 2 : 3);
        else lod[ai] = d < L.lod0 ? 0 : 1;
    }
    // individual draws
    for (int ai = 0; ai < (int)gW.animals.size(); ai++) {
        if (lod[ai] != 0 && lod[ai] != 1) continue;
        Animal& a = gW.animals[ai];
        const Group* G = a.group >= 0 ? &gW.groups[a.group] : nullptr;
        const ModelData& md = modelOf(a);
        Render::Model* model = gW.sp[a.sp].lod[a.var][lod[ai]];
        if (!model) model = gW.sp[a.sp].lod[a.var][0];
        if (!model || gW.arena.size() + (size_t)md.skel.n > cap) continue;
        poseAnimal(g, a, G, false);
        size_t start = gW.arena.size();
        gW.arena.insert(gW.arena.end(), a.skin, a.skin + md.skel.n);
        vec3 rp = a.pos + a.vel * (G ? G->accDt : 0.f);
        Render::DrawItem d;
        d.model = model;
        d.pos = dvec3(rp);
        d.rot = mat3FromQuat(animalQuat(a));
        d.scale = vec3(a.scale);
        d.bones = &gW.arena[start];
        d.boneCount = md.skel.n;
        d.id = 0xA000000000ull | (u64)a.uid;
        d.castShadow = a.visDist < lodDist(a.sp).shadow;
        d.wetExposed = 1.f;
        dyn->submit(d);
        gW.drawn++;
        gW.drawCalls++;
    }
    // batched draws: one per group / variant / LOD; slot = member index, other slots collapsed
    for (int gi = 0; gi < (int)gW.groups.size(); gi++) {
        Group& G = gW.groups[gi];
        if (!G.used || G.members.empty()) continue;
        int sp = G.sp;
        const SpeciesInfo& si = speciesInfo(sp);
        if (si.plan != PLAN_BIRD && si.plan != PLAN_FISH) continue;
        for (int var = 0; var < gW.sp[sp].variants; var++)
            for (int bl = 0; bl < 2; bl++) {
                int want = bl == 0 ? 2 : 3;
                vec3 origin(0.f);
                int n = 0;
                for (int ai : G.members)
                    if (lod[ai] == want && gW.animals[ai].var == var) {
                        origin += gW.animals[ai].pos;
                        n++;
                    }
                if (!n) continue;
                const ModelData& md = gW.sp[sp].md[var];
                Render::Model* model = gW.sp[sp].batch[var][bl];
                int slots = Min((int)G.members.size(), md.batchCap);
                size_t need = (size_t)slots * (size_t)md.batchN;
                if (!model || gW.arena.size() + need > cap) continue;
                origin = origin / (float)n;
                size_t start = gW.arena.size();
                gW.arena.resize(start + need);
                for (int k = 0; k < slots; k++) {
                    Animal& a = gW.animals[G.members[k]];
                    mat4* B = &gW.arena[start + (size_t)k * md.batchN];
                    vec3 rel = a.pos + a.vel * G.accDt - origin;
                    if (!a.used || lod[G.members[k]] != want || a.var != var || length2(rel) > 160.f * 160.f) {
                        for (int j = 0; j < md.batchN; j++) B[j] = mat4(vec4(0.f), vec4(0.f), vec4(0.f), vec4(rel, 1.f));
                        continue;
                    }
                    poseAnimal(g, a, &G, false);
                    mat3 R = mat3FromQuat(animalQuat(a));
                    float s = a.scale;
                    mat4 M(vec4(R.c[0] * s, 0.f), vec4(R.c[1] * s, 0.f), vec4(R.c[2] * s, 0.f), vec4(rel, 1.f));
                    for (int j = 0; j < md.batchN; j++) B[j] = M * a.skin[md.batchBones[j]];
                    gW.drawn++;
                }
                Render::DrawItem d;
                d.model = model;
                d.pos = dvec3(origin);
                d.bones = &gW.arena[start];
                d.boneCount = (int)need;
                d.id = 0xB000000000ull | ((u64)G.uid << 4) | ((u64)var << 1) | (u64)bl;
                d.castShadow = bl == 0 && length(origin - cam) < 40.f && si.plan == PLAN_BIRD;
                d.wetExposed = 1.f;
                dyn->submit(d);
                gW.drawCalls++;
            }
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Group dispatch
const FlockCfg kGullCfg = {3.f, 14.f, 18.f, 55.f, 8.f, 30.f, 20.f, 70.f, 15.f, 60.f, 8.f, 1.f / 45.f, true, true, false, 0.7f};
const FlockCfg kPigeonCfg = {1.1f, 8.f, 10.f, 22.f, 7.f, 15.f, 8.f, 14.f, 1e9f, 1e9f, 3.5f, 0.f, true, false, true, 0.45f};
const FlockCfg kGrackleCfg = {1.2f, 8.f, 8.f, 20.f, 6.f, 14.f, 5.f, 15.f, 20.f, 90.f, 4.f, 0.f, true, false, false, 0.8f};
const FlockCfg kCormorantCfg = {2.5f, 10.f, 20.f, 60.f, 3.f, 10.f, 15.f, 40.f, 60.f, 240.f, 10.f, 0.f, false, true, false, 0.5f};

void frozenStep(Animal& a, float dt) {
    a.life += dt;
    a.ba.t += dt;
    a.qa.t += dt;
    a.ra.t += dt;
    a.sa.t += dt;
    a.sa.phase += dt * 3.f;
    a.ra.swimPhase += dt * 2.f;
}

void updateGroup(GameWorld& g, int gi, float dt) {
    Group& G = gW.groups[gi];
    if (G.frozen) {
        for (int ai : G.members)
            if (gW.animals[ai].used) frozenStep(gW.animals[ai], dt);
        return;
    }
    switch (G.type) {
        case GT_GULLS: updateFlockBirds(g, gi, dt, kGullCfg); break;
        case GT_PIGEONS: updateFlockBirds(g, gi, dt, kPigeonCfg); break;
        case GT_GRACKLES: updateFlockBirds(g, gi, dt, kGrackleCfg); break;
        case GT_CORMORANTS: updateFlockBirds(g, gi, dt, kCormorantCfg); break;
        case GT_SHOREBIRDS: updateShorebirds(g, gi, dt); break;
        case GT_CEGRETS: updateCattleEgrets(g, gi, dt); break;
        case GT_FRIGATES: updateVultures(g, gi, dt); break;
        case GT_PELICANS: updatePelicans(g, gi, dt); break;
        case GT_WADERS: updateWaders(g, gi, dt); break;
        case GT_VULTURES: updateVultures(g, gi, dt); break;
        case GT_PARROTS: updateParrots(g, gi, dt); break;
        case GT_GATOR: updateGators(g, gi, dt); break;
        case GT_IGUANA: updateIguanas(g, gi, dt); break;
        case GT_DOLPHINS: updateDolphins(g, gi, dt); break;
        case GT_MANATEE: case GT_TURTLE: updateSlowSwimmers(g, gi, dt); break;
        case GT_FISH: updateFish(g, gi, dt); break;
        case GT_DOG_LEASH: case GT_DOG_STRAY: updateDogs(g, gi, dt); break;
        case GT_CAT: case GT_RACCOON: updateCritters(g, gi, dt); break;
        default: updateHerd(g, gi, dt); break;
    }
}

void updateAllGroups(GameWorld& g, float dt) {
    for (int gi = 0; gi < (int)gW.groups.size(); gi++) {
        Group& G = gW.groups[gi];
        if (!G.used) continue;
        float d = length(G.center - gW.cam);
        float interval = d < 220.f ? 0.f : (d < 500.f ? 1.f / 15.f : 1.f / 8.f);
        G.accDt += dt;
        if (G.accDt < interval) continue;
        float step = Min(G.accDt, 0.25f);
        G.accDt = 0.f;
        updateGroup(g, gi, step);
    }
}

}  // namespace wild_detail

// ==================================================================================================================
// Test scenes (--play --wildlifetest [--shotdir DIR]): stages animals in front of a fixed camera around the map and
// saves screenshots (wild_*.bmp), then quits.
namespace wild_detail {

struct TestCam {
    vec3 pos;
    float yaw = 0.f, pitch = 0.f, fov = 55.f * kDegToRad;
};
TestCam gTestCam;
vec3 gTestSpot;   // scene anchor found in stage A
vec2 gTestN;      // beach normal (sanderlings)

void clearAll() {
    for (int gi = 0; gi < (int)gW.groups.size(); gi++)
        if (gW.groups[gi].used) releaseGroup(gi);
}

void aimCam(vec3 eye, vec3 target, float fovDeg) {
    vec3 d = target - eye;
    gTestCam.pos = eye;
    gTestCam.yaw = atan2f(-d.x, d.y);
    gTestCam.pitch = atan2f(d.z, Max(length(d.xy()), 1e-3f));
    gTestCam.fov = fovDeg * kDegToRad;
}

void teleportPlayer(GameWorld& g, vec2 p) {
    Ped* pl = g.playerPed();
    if (!pl) return;
    if (pl->vehicle >= 0) g.removePedFromVehicle(g.player, false);
    float z = Max(g.groundHeight(p.x, p.y, g.map->heightAt(p.x, p.y) + 30.f), g.map->heightAt(p.x, p.y));
    pl->pos = dvec3(p.x, p.y, z + 0.05f);
    pl->vel = vec3(0.f);
}

float gz(GameWorld& g, vec2 p) { return groundAt(g, p.x, p.y, g.map->heightAt(p.x, p.y) + 20.f); }

// Search helpers for scene locations
bool findPoint(GameWorld& g, vec2 around, float radius, const std::function<bool(vec2)>& ok, vec2& out) {
    Rng r(hash32((u32)(around.x * 13.f) ^ (u32)(around.y * 7.f)));
    for (int i = 0; i < 4000; i++) {
        float ang = r.f() * kTwoPi, rr = sqrtf(r.f()) * radius;
        vec2 p = around + vec2(cosf(ang), sinf(ang)) * rr;
        if (ok(p)) {
            out = p;
            return true;
        }
    }
    return false;
}

void frozenLineup(GameWorld& g, const std::vector<std::pair<int, int>>& list, vec2 start, vec2 step, float yaw) {
    for (size_t i = 0; i < list.size(); i++) {
        vec2 p = start + step * (float)i;
        int sp = list[i].first, var = list[i].second;
        int gi = spawnSingle(g, GT_CAT, sp, var, vec3(p, gz(g, p)), yaw, 1.f, ST_IDLE);
        if (gi < 0) continue;
        Group& G = gW.groups[gi];
        G.type = isBird(sp) ? GT_WADERS : (speciesInfo(sp).plan == PLAN_REPTILE ? GT_GATOR : GT_CAT);
        G.sp = (u8)sp;
        G.frozen = true;
        Animal& a = gW.animals[G.members[0]];
        if (isBird(sp)) {
            a.ba.fold = 1.f;
            a.ba.legs = 1.f;
            a.standH = modelOf(a).legLen;
            a.pos.z += a.standH;
        }
        if (sp == SP_GATOR) a.ra.jaw = 0.45f;
        if (sp == SP_IGUANA) a.ra.lift = 0.4f;
    }
}

const char* kSceneNames[] = {"wild_gulls_pier", "wild_gulls_close", "wild_pelicans", "wild_pigeons", "wild_pigeons_flush", "wild_sawgrass",
                             "wild_gator_bank", "wild_gator_water", "wild_dolphins", "wild_fish", "wild_dogs", "wild_farm", "wild_deer",
                             "wild_night", "wild_vultures", "wild_lineup_bigbirds", "wild_lineup_smallbirds", "wild_lineup_big",
                             "wild_lineup_small", "wild_gulls_overhead", "wild_shorebirds", "wild_grackles", "wild_cormorants",
                             "wild_frigates", "wild_lineup_newbirds"};
const int kSceneCount = (int)ARRAY_COUNT(kSceneNames);

// --wildscene a,b,c: only the scenes whose names contain one of the comma separated fragments
bool sceneWanted(int s) {
    const char* only = Platform::argValue("wildscene");
    if (!only) return true;
    std::string list = only;
    size_t a = 0;
    while (a <= list.size()) {
        size_t b = list.find(',', a);
        if (b == std::string::npos) b = list.size();
        std::string frag = list.substr(a, b - a);
        if (!frag.empty() && strstr(kSceneNames[s], frag.c_str())) return true;
        a = b + 1;
    }
    return false;
}

// Stage A: time, player and camera placement. Returns false to skip the scene.
bool sceneStageA(GameWorld& g, int s) {
    auto setTod = [&](float t) {
        if (g.env) g.env->timeOfDay = t;
        gW.tod = t;
    };
    switch (s) {
        case 0: setTod(10.f); teleportPlayer(g, vec2(5500.f, 1248.f)); aimCam(vec3(5480.f, 1214.f, 12.f), vec3(5560.f, 1252.f, 7.f), 60.f); return true;
        case 1: setTod(16.5f); teleportPlayer(g, vec2(5520.f, 1250.f)); aimCam(vec3(5524.f, 1245.f, 7.3f), vec3(5530.5f, 1242.3f, 6.75f), 50.f); return true;
        case 2: setTod(11.f); teleportPlayer(g, vec2(5620.f, 1248.f)); aimCam(vec3(5630.f, 1243.f, 8.5f), vec3(5700.f, 1215.f, 2.f), 55.f); return true;
        case 3: case 4: {
            setTod(13.f);
            teleportPlayer(g, vec2(3000.f, 238.f));
            float z = g.map->heightAt(3000.f, 250.f);
            aimCam(vec3(2987.f, 243.f, z + 1.7f), vec3(3001.f, 256.f, z + 0.1f), 55.f);
            return true;
        }
        case 5: case 6: case 7: {
            setTod(s == 5 ? 8.5f : (s == 6 ? 14.5f : 12.f));
            vec2 p;
            float dMin = s == 5 ? 0.05f : 1.2f, dMax = s == 5 ? 0.3f : 5.f;
            if (!findPoint(g, vec2(-5060.f, 420.f), 900.f, [&](vec2 q) {
                    float d = waterDepth(*g.map, q.x, q.y);
                    return d > dMin && d < dMax && !g.roads->nearRoad(q, 20.f);
                }, p))
                return false;
            if (s == 6) {
                vec3 bank;
                if (!findBank(g, p, 30.f, bank)) return false;
                p = bank.xy();
            }
            gTestSpot = vec3(p, g.map->heightAt(p.x, p.y));
            vec2 off = s == 5 ? vec2(-13.f, -9.f) : (s == 6 ? vec2(-3.2f, -3.2f) : vec2(-6.f, -5.f));
            teleportPlayer(g, p + off * 1.5f);
            float wl = g.map->waterAt(p.x, p.y);
            float eyeZ = s == 6 ? gTestSpot.z + 1.2f : Max(wl, gTestSpot.z) + (s == 5 ? 2.2f : 1.3f);
            aimCam(vec3(p + off, eyeZ), vec3(p, Max(wl, gTestSpot.z) + (s == 5 ? 0.7f : 0.2f)), s == 6 ? 50.f : 55.f);
            return true;
        }
        case 8: case 9: {
            setTod(s == 8 ? 11.f : 12.5f);
            vec2 p;
            vec2 around = s == 8 ? vec2(6100.f, 1300.f) : vec2(5620.f, 1235.f);
            if (!findPoint(g, around, s == 8 ? 700.f : 200.f, [&](vec2 q) {
                    float d = waterDepth(*g.map, q.x, q.y);
                    return s == 8 ? d > 8.f : (d > 3.f && d < 7.f);
                }, p))
                return false;
            float wl = g.map->waterAt(p.x, p.y);
            gTestSpot = vec3(p, wl);
            teleportPlayer(g, p + vec2(-10.f, -10.f));
            if (s == 8) aimCam(vec3(p + vec2(-22.f, -26.f), wl + 3.5f), vec3(p, wl + 0.5f), 55.f);
            else aimCam(vec3(p + vec2(0.f, -5.f), wl - 1.3f), vec3(p + vec2(0.f, 1.f), wl - 1.9f), 65.f);
            return true;
        }
        case 10: {
            setTod(17.5f);
            Ped* pl = g.playerPed();
            if (!pl) return false;
            vec2 dry;
            if (findPoint(g, vec2(3000.f, 300.f), 250.f, [&](vec2 q) {
                    return waterDepth(*g.map, q.x, q.y) < -0.5f && !g.roads->nearRoad(q, 2.f) && g.roads->nearRoad(q, 12.f) && !g.buildings->pointInBuilding(q, 6.f);
                }, dry))
                teleportPlayer(g, dry);
            gTestSpot = pl->pos.toVec3();
            vec2 f = dirOf(pl->yaw);
            aimCam(gTestSpot + vec3(-f * 1.5f + vec2(f.y, -f.x) * 4.5f, 1.6f), gTestSpot + vec3(f * 3.f, 0.4f), 55.f);
            return true;
        }
        case 11: case 12: case 14: {
            setTod(s == 11 ? 9.f : (s == 12 ? 7.5f : 12.5f));
            vec2 around = s == 12 ? vec2(-6891.f, 7235.f) : vec2(-1100.f, 7000.f);
            World::Region want = s == 12 ? World::REG_RIDGE : World::REG_FARMLAND;
            vec2 p;
            if (!findPoint(g, around, 2500.f, [&](vec2 q) {
                    if (g.map->regionAt(q.x, q.y) != want || waterDepth(*g.map, q.x, q.y) > -0.3f) return false;
                    if (g.map->normalAt(q.x, q.y).z < (s == 12 ? 0.93f : 0.98f)) return false;
                    return !g.roads->nearRoad(q, 45.f) && !g.buildings->pointInBuilding(q, 40.f);
                }, p))
                return false;
            gTestSpot = vec3(p, g.map->heightAt(p.x, p.y));
            teleportPlayer(g, p + vec2(-18.f, -16.f));
            if (s == 14) aimCam(gTestSpot + vec3(0, 0, 1.7f), gTestSpot + vec3(30.f, 80.f, 55.f), 65.f);
            else aimCam(gTestSpot + vec3(s == 11 ? -17.f : -12.f, s == 11 ? -15.f : -10.f, 2.2f), gTestSpot + vec3(0, 0, 0.9f), 55.f);
            return true;
        }
        case 13: {
            setTod(23.3f);
            Ped* pl = g.playerPed();
            if (!pl) return false;
            teleportPlayer(g, vec2(3000.f, 300.f));
            gTestSpot = pl->pos.toVec3();
            aimCam(gTestSpot + vec3(0, -4.f, 1.7f), gTestSpot, 55.f);
            return true;
        }
        case 15: case 16: case 17: case 18: case 24: {
            setTod(11.f);
            teleportPlayer(g, vec2(3000.f, 300.f));
            float z = g.map->heightAt(3000.f, 318.f);
            if (s == 15 || s == 24) aimCam(vec3(3000.f, 306.5f, z + 1.3f), vec3(3000.f, 318.f, z + 0.55f), 60.f);
            else if (s == 16) aimCam(vec3(3000.f, 313.8f, z + 0.55f), vec3(3000.f, 318.f, z + 0.18f), 55.f);
            else if (s == 17) aimCam(vec3(3000.f, 296.f, z + 3.2f), vec3(3000.f, 318.f, z + 0.8f), 60.f);
            else aimCam(vec3(3000.f, 311.5f, z + 1.1f), vec3(3000.f, 318.f, z + 0.25f), 58.f);
            return true;
        }
        case 20: {   // sanderlings at the water's edge
            setTod(9.5f);
            vec2 p, n;
            vec3 edge;
            bool found = false;
            for (int tries = 0; tries < 30 && !found; tries++) {
                if (!findPoint(g, vec2(5250.f + 97.f * (float)tries, 900.f - 53.f * (float)tries), 500.f, [&](vec2 q) {
                        float sd = g.map->coastDistance(q.x, q.y);
                        return sd > -5.f && sd < 12.f && g.map->regionAt(q.x, q.y) == World::REG_BEACH;
                    }, p))
                    continue;
                found = findShoreEdge(g, p, edge, n);
            }
            if (!found) return false;
            gTestSpot = edge;
            gTestN = n;
            vec2 T(n.y, -n.x);
            teleportPlayer(g, edge.xy() + n * 25.f);
            aimCam(edge + vec3(T * 7.f + n * 3.5f, 1.1f), edge + vec3(n * 0.8f, 0.1f), 50.f);
            return true;
        }
        case 21: {   // grackles strutting on the plaza
            setTod(12.5f);
            teleportPlayer(g, vec2(3000.f, 238.f));
            float z = g.map->heightAt(3000.f, 250.f);
            aimCam(vec3(2992.f, 248.f, z + 1.4f), vec3(3000.f, 256.f, z + 0.2f), 50.f);
            return true;
        }
        case 22: {   // cormorants on the pilings of a marina
            setTod(15.5f);
            const World::SiteElem* site = siteNear(vec2(5600.f, 1240.f), 8000.f, {World::SK_MARINA, World::SK_RIVER_MARINA, World::SK_DOCK});
            if (!site) return false;
            gTestSpot = vec3(site->c, spawnHeight(g, site->c));
            teleportPlayer(g, site->c + vec2(0.f, -20.f));
            aimCam(gTestSpot + vec3(-14.f, -14.f, 5.f), gTestSpot + vec3(0, 0, 1.f), 55.f);
            return true;
        }
        case 23: {   // frigatebirds hanging over the beach
            setTod(13.f);
            vec2 p;
            if (!findPoint(g, vec2(5250.f, 900.f), 400.f, [&](vec2 q) {
                    float sd = g.map->coastDistance(q.x, q.y);
                    return sd > 2.f && sd < 12.f && g.map->regionAt(q.x, q.y) == World::REG_BEACH && !g.roads->nearRoad(q, 30.f);
                }, p))
                return false;
            gTestSpot = vec3(p, g.map->heightAt(p.x, p.y));
            teleportPlayer(g, p);
            aimCam(gTestSpot + vec3(0, 0, 1.6f), gTestSpot + vec3(-10.f, 40.f, 45.f), 60.f);
            return true;
        }
        case 19: {
            setTod(17.2f);
            vec2 p;
            if (!findPoint(g, vec2(5250.f, 900.f), 400.f, [&](vec2 q) {
                    float sd = g.map->coastDistance(q.x, q.y);
                    return sd > 2.f && sd < 12.f && g.map->regionAt(q.x, q.y) == World::REG_BEACH && !g.roads->nearRoad(q, 30.f);
                }, p))
                return false;
            gTestSpot = vec3(p, g.map->heightAt(p.x, p.y));
            teleportPlayer(g, p);
            aimCam(gTestSpot + vec3(0, 0, 1.6f), gTestSpot + vec3(6.f, 25.f, 9.f), 65.f);
            return true;
        }
        default: return false;
    }
}

// After the fast-forward: frame the animals that moved (flocks, pods) from at most maxDist away
void trackCam(GameWorld& g, float maxDist, float minHeight, bool move = true) {
    // the densest cluster: the animal with the most others within 12 m, and the centroid of that neighbourhood
    int best = -1, bestN = -1;
    for (int i = 0; i < (int)gW.animals.size(); i++) {
        if (!gW.animals[i].used) continue;
        int n = 0;
        for (const Animal& b : gW.animals) n += b.used && length2(b.pos - gW.animals[i].pos) < 144.f;
        if (n > bestN) {
            bestN = n;
            best = i;
        }
    }
    if (best < 0) return;
    vec3 c(0.f);
    int n = 0;
    for (const Animal& b : gW.animals)
        if (b.used && length2(b.pos - gW.animals[best].pos) < 144.f) {
            c += b.pos;
            n++;
        }
    c = c / (float)Max(n, 1);
    vec3 away = gTestCam.pos - c;
    float d = length(away);
    vec3 eye = gTestCam.pos;
    if (d > maxDist && move) eye = c + away * (maxDist / d);
    float floorZ = Max(g.map->heightAt(eye.x, eye.y), g.map->waterAt(eye.x, eye.y));
    eye.z = Max(eye.z, floorZ + minHeight);
    aimCam(eye, c, gTestCam.fov * kRadToDeg);
}

// Stage B (after streaming): the animals. Returns the number of fast-forward steps (1/30 s) to simulate.
int sceneStageB(GameWorld& g, int s) {
    switch (s) {
        case 0: spawnFlock(g, GT_GULLS, SP_GULL, vec3(5550.f, 1250.f, 5.6f), 24, true); return 180;
        case 1: {
            int gi = newGroup(GT_GULLS, SP_GULL, vec3(5531.f, 1242.3f, 6.65f));
            Group& G = gW.groups[gi];
            for (int k = 0; k < 5; k++) {
                vec2 q(5527.8f + 1.35f * (float)k, 1242.25f - 0.6f * (float)(k & 1));
                G.perches.push_back({vec3(q, gz(g, q)), 0});
            }
            for (int k = 0; k < 5; k++) {
                int ai = newAnimal(gi, SP_GULL, k == 2 ? 1 : 0, G.perches[k].pos, (k & 1) ? 1.9f : -1.4f);
                Animal& a = gW.animals[ai];
                a.perch = k;
                G.perches[k].taken = ai;
                a.scale = k == 2 ? 0.8f : frange(0.95f, 1.05f);
                a.pos.z += birdStandHeight(a, false);
                a.state = ST_IDLE;
                a.timer = 1000.f;
                a.ba.fold = a.ba.legs = 1.f;
            }
            return 20;
        }
        case 2: {
            int gi = spawnPelicanLine(g, vec3(5760.f, 1175.f, 0.5f), 5);
            if (gi >= 0) {
                Group& G = gW.groups[gi];
                G.heading = yawOf(vec2(5660.f, 1230.f) - vec2(5760.f, 1175.f));
                G.target = vec3(5600.f, 1262.f, 0.5f);
                G.timer = 1000.f;
                vec2 d = dirOf(G.heading);
                int k = 0;
                for (int ai : G.members) {
                    Animal& a = gW.animals[ai];
                    a.pos = vec3(vec2(5760.f, 1175.f) - d * (4.5f * (float)k) + vec2(d.y, -d.x) * (1.1f * (float)k), 3.5f);
                    a.vel = vec3(d * 9.f, 0.f);
                    a.yaw = G.heading;
                    k++;
                }
            }
            return 60;
        }
        case 3: case 4: {
            float z = gz(g, vec2(3001.f, 257.f));
            spawnFlock(g, GT_PIGEONS, SP_PIGEON, vec3(3001.f, 257.f, z), 26, false);
            if (s == 4) addThreat(gW.cam, vec3(0.f), 95.f, 2, true, g.player, -1);
            return s == 3 ? 90 : 30;
        }
        case 5: {
            vec3 spot = gTestSpot, s2;
            spawnWaders(g, SP_HERON, spot, 1);
            if (findShallowSpot(g, spot.xy() + vec2(5.f, 3.f), 0.f, 6.f, 0.02f, 0.35f, s2)) spawnWaders(g, SP_EGRET, s2, 2);
            if (findShallowSpot(g, spot.xy() + vec2(-4.f, 7.f), 0.f, 8.f, 0.02f, 0.3f, s2)) spawnWaders(g, SP_SPOONBILL, s2, 4);
            if (findShallowSpot(g, spot.xy() + vec2(8.f, -3.f), 0.f, 8.f, 0.02f, 0.3f, s2)) spawnWaders(g, SP_IBIS, s2, 5);
            if (findShallowSpot(g, spot.xy() + vec2(-9.f, -2.f), 0.f, 8.f, 0.02f, 0.45f, s2)) spawnWaders(g, SP_FLAMINGO, s2, 5);
            return 90;
        }
        case 6: {
            int gi = spawnGator(g, vec3(gTestSpot.xy(), gz(g, gTestSpot.xy())), true);
            if (gi >= 0) {
                Animal& a = gW.animals[gW.groups[gi].members[0]];
                a.scale = 1.f;
                a.ra.jaw = 0.6f;
                a.yaw = yawOf(vec2(1.f, -0.35f));
                a.timer = 1000.f;
            }
            return 15;
        }
        case 7: {
            int gi = spawnGator(g, vec3(gTestSpot.xy(), g.map->waterAt(gTestSpot.x, gTestSpot.y) - 0.4f), false);
            if (gi >= 0) {
                Animal& a = gW.animals[gW.groups[gi].members[0]];
                a.scale = 1.05f;
                a.yaw = yawOf(vec2(-1.f, -0.3f));
                a.timer = 1000.f;
            }
            return 30;
        }
        case 8: {
            int gi = spawnPod(g, gTestSpot + vec3(0, 0, -1.5f), 6);
            if (gi >= 0) {
                Group& G = gW.groups[gi];
                G.heading = yawOf(vec2(1.f, 0.35f));
                G.target = gTestSpot + vec3(dirOf(G.heading) * 200.f, 0.f);
            }
            return 60;
        }
        case 9: {
            vec2 c = gTestSpot.xy() + vec2(0.f, 2.f);
            float wl = g.map->waterAt(c.x, c.y);
            spawnShoal(g, vec3(c, wl - 1.9f), 0, 36);
            spawnShoal(g, vec3(c + vec2(3.f, 3.f), wl - 1.6f), 1, 20);
            spawnShoal(g, vec3(c + vec2(-4.f, 7.f), wl - 2.2f), 3, 4);
            return 60;
        }
        case 10: {
            Ped* pl = g.playerPed();
            vec2 f = dirOf(pl->yaw);
            vec2 pp = gTestSpot.xy() + f * 3.f;
            int ci = g.randomCivilianChar(0xD06u, 0);
            int ped = g.spawnPed(ci, dvec3(vec3(pp, gTestSpot.z)), pl->yaw + kPi, FAC_CIVILIAN);
            if (ped >= 0) {
                g.peds[ped].brain.type = BRAIN_WANDER;
                attachLeashDog(g, ped);
            }
            spawnDogPack(g, vec3(gTestSpot.xy() + f * 9.f + vec2(f.y, -f.x) * 5.f, gTestSpot.z), 2);
            return 45;
        }
        case 11: {
            int herd = spawnHerd(g, GT_CATTLE, SP_COW, gTestSpot, 8);
            if (herd >= 0) spawnCattleEgrets(g, herd, 5);
            spawnHerd(g, GT_HORSES, SP_HORSE, gTestSpot + vec3(10.f, -8.f, 0.f), 3);
            return 90;
        }
        case 12: spawnHerd(g, GT_DEER, SP_DEER, gTestSpot, 5); return 60;
        case 13: {
            // a dumpster near the player for the raccoons, and a cat
            if (!Phys::gCollision) return 0;
            std::vector<int> ids;
            Phys::gCollision->collidersNear(gTestSpot.xy(), 250.f, ids);
            for (int id : ids) {
                const Phys::Collider& c = Phys::gCollision->collider(id);
                if (c.owner < 0 || c.kind != Phys::COL_BOX || fabsf(c.he.x - 0.9f) > 0.05f || fabsf(c.he.y - 0.6f) > 0.05f) continue;
                vec2 side = perp(c.ax);
                vec3 base = vec3(c.c.xy() + side * (c.he.y + 0.4f), c.c.z - c.he.z);
                int gi = newGroup(GT_RACCOON, SP_RACCOON, base);
                gW.groups[gi].target = vec3(c.c.xy(), base.z);
                for (int k = 0; k < 2; k++) {
                    int ai = newAnimal(gi, SP_RACCOON, 0, base + vec3(c.ax * (-0.5f + 1.1f * (float)k), 0.f), yawOf(-side));
                    gW.animals[ai].state = k == 0 ? ST_FEED : ST_WALK;
                    gW.animals[ai].goal = base + vec3(c.ax * 2.f, 0.f);
                    gW.animals[ai].timer = 100.f;
                }
                spawnSingle(g, GT_CAT, SP_CAT, 0, base + vec3(side * 2.5f + c.ax * 2.f, 0.f), yawOf(-side), 1.f, ST_REST);
                aimCam(base + vec3(side * 3.8f + c.ax * 1.5f, 1.5f), base + vec3(c.ax * 0.6f, 0.35f), 55.f);
                return 20;
            }
            return 0;
        }
        case 14: {
            int gi = spawnVultures(g, gTestSpot + vec3(30.f, 80.f, 0.f), 7);
            if (gi >= 0)
                for (int ai : gW.groups[gi].members) gW.animals[ai].pos.z = gTestSpot.z + frange(35.f, 70.f);
            return 150;
        }
        case 15:
            frozenLineup(g, {{SP_PELICAN, 0}, {SP_HERON, 0}, {SP_EGRET, 0}, {SP_SPOONBILL, 0}, {SP_FLAMINGO, 0}, {SP_IBIS, 0}, {SP_VULTURE, 0}, {SP_VULTURE, 1}},
                         vec2(2994.4f, 318.f), vec2(1.6f, 0.f), kPi);
            return 5;
        case 16:
            frozenLineup(g, {{SP_GULL, 0}, {SP_GULL, 1}, {SP_PIGEON, 0}, {SP_PIGEON, 1}, {SP_PIGEON, 2}, {SP_PARROT, 0}, {SP_PARROT, 1}},
                         vec2(2998.2f, 318.f), vec2(0.6f, 0.f), kPi);
            return 5;
        case 17:
            frozenLineup(g, {{SP_COW, 0}, {SP_COW, 1}, {SP_COW, 2}, {SP_HORSE, 0}, {SP_HORSE, 1}, {SP_HORSE, 2}, {SP_DEER, 0}, {SP_DEER, 1}},
                         vec2(2990.2f, 318.f), vec2(2.8f, 0.f), kPi * 0.5f);
            return 5;
        case 18:
            frozenLineup(g, {{SP_DOG, 0}, {SP_DOG, 1}, {SP_DOG, 2}, {SP_DOG, 3}, {SP_DOG, 4}, {SP_CAT, 0}, {SP_CAT, 1}, {SP_CAT, 2}, {SP_RACCOON, 0}, {SP_IGUANA, 0}},
                         vec2(2996.f, 318.f), vec2(0.9f, 0.f), kPi * 0.5f);
            return 5;
        case 20: spawnShorebirds(g, gTestSpot, gTestN, 14); return 90;
        case 21: {
            vec2 q(3000.f, 256.f);
            spawnFlock(g, GT_GRACKLES, SP_GRACKLE, vec3(q, gz(g, q)), 10, false);
            return 60;
        }
        case 22: {
            int gi = spawnFlock(g, GT_CORMORANTS, SP_CORMORANT, gTestSpot, 7, false);
            if (gi >= 0)
                for (int ai : gW.groups[gi].members)
                    if (gW.animals[ai].state == ST_IDLE) gW.animals[ai].aux = 40.f;   // wings out to dry
            return 45;
        }
        case 23: spawnSoarers(g, GT_FRIGATES, SP_FRIGATE, gTestSpot + vec3(-10.f, 60.f, 0.f), 5); return 120;
        case 24:
            frozenLineup(g, {{SP_SANDPIPER, 0}, {SP_GRACKLE, 0}, {SP_GRACKLE, 1}, {SP_CEGRET, 0}, {SP_CORMORANT, 0}, {SP_FRIGATE, 0}, {SP_FRIGATE, 1}},
                         vec2(2997.f, 318.f), vec2(1.0f, 0.f), kPi);
            return 5;
        case 19: {
            int gi = spawnFlock(g, GT_GULLS, SP_GULL, gTestSpot + vec3(4.f, 22.f, -6.f), 20, true);
            if (gi >= 0)
                for (int ai : gW.groups[gi].members) {
                    Animal& a = gW.animals[ai];
                    if (a.state != ST_FLY) {
                        a.state = ST_FLY;
                        a.pos = gTestSpot + vec3(gW.rng.inCircle() * 20.f + vec2(4.f, 22.f), frange(4.f, 12.f));
                    }
                    a.timer = 1000.f;
                }
            return 120;
        }
        default: return 0;
    }
}

void testUpdate(GameWorld& g) {
    if (gW.testScene < 0) return;
    g.hudVisible = false;
    g.hidePlayerModel = true;
    g.populationOff = gW.testScene != 10;
    gW.spawning = false;
    int& ph = gW.testPhase;
    int& fr = gW.testFrame;
    int s = gW.testScene;
    fr++;
    if (ph == 0) {
        clearAll();
        if (!sceneStageA(g, s)) {
            LOG("Wildlife test: scene %s skipped (no location)", kSceneNames[s]);
            ph = 5;
        } else {
            ph = 1;
            fr = 0;
        }
    } else if (ph == 1) {
        int pending = g.renderer->world ? g.renderer->world->pendingCount() : 0;
        if ((pending == 0 && fr > 20) || fr > 500) {
            gatherContext(g);
            int steps = sceneStageB(g, s);
            for (int k = 0; k < steps; k++) {
                updateAllGroups(g, 1.f / 30.f);
                if (s == 4) addThreat(gW.cam, vec3(0.f), 95.f, 2, true, g.player, -1);
            }
            if (s == 0) trackCam(g, 38.f, 2.f);
            if (s == 2) trackCam(g, 30.f, 3.f);
            if (s == 14) trackCam(g, 70.f, 2.f);
            if (s == 22) trackCam(g, 22.f, 2.f);
            if (s == 23) trackCam(g, 0.f, 1.5f, false);
            if (s == 8)   // a couple of dolphins mid-leap for the shot
                for (const Group& G : gW.groups)
                    if (G.used && G.type == GT_DOLPHINS)
                        for (int k = 0; k < (int)G.members.size() && k < 3; k++) {
                            Animal& a = gW.animals[G.members[k]];
                            a.state = ST_DIVE;
                            a.goal = vec3(k == 0 ? 1.8f : 0.12f, 1.7f, k == 0 ? 1.f : 0.f);
                            a.speedWant = 1.6f;
                            a.t = k == 0 ? 0.55f : 0.5f + 0.1f * (float)k;
                        }
            if (s == 8) updateAllGroups(g, 1.f / 30.f);   // place the leapers (the shot holds the moment)
            ph = 2;
            fr = 0;
        }
    } else if (ph == 2) {
        int pending = g.renderer->world ? g.renderer->world->pendingCount() : 0;
        if ((pending == 0 && fr > 12) || fr > 300) {
            std::string dir = Platform::argValue("shotdir") ? Platform::argValue("shotdir") : "Z:\\tmp\\";
            g.requestScreenshot = dir + kSceneNames[s] + ".bmp";
            int n = 0;
            for (const Animal& a : gW.animals) n += a.used;
            LOG("Wildlife test: %s: %d animals, %d drawn in %d draws, update %.3f ms, render %.3f ms", kSceneNames[s], n, gW.drawn, gW.drawCalls,
                gW.msUpdate, gW.msRender);
            ph = 3;
            fr = 0;
        }
    } else if (ph == 3) {
        if (g.requestScreenshot.empty()) ph = 5;
    }
    if (ph == 5) {
        gW.testScene++;
        ph = 0;
        fr = 0;
        while (gW.testScene < kSceneCount && !sceneWanted(gW.testScene)) gW.testScene++;
        if (gW.testScene >= kSceneCount) {
            LOG("Wildlife test: done");
            PostQuitMessage(0);
            gW.testScene = -2;
        }
    }
    if (gW.testScene >= 0) {
        g.rig.cam.pos = dvec3(gTestCam.pos);
        g.rig.cam.yaw = gTestCam.yaw;
        g.rig.cam.pitch = gTestCam.pitch;
        g.rig.cam.roll = 0.f;
        g.rig.cam.fovY = gTestCam.fov;
    }
}

}  // namespace wild_detail

// ==================================================================================================================
// Public API
namespace Wildlife {

void update(GameWorld& g, float dt) {
    using namespace wild_detail;
    static bool init = false;
    if (!init) {
        init = true;
        if (Platform::hasArg("wildlifetest")) {
            gW.testScene = 0;
            while (gW.testScene < kSceneCount && !sceneWanted(gW.testScene)) gW.testScene++;
            if (gW.testScene >= kSceneCount) gW.testScene = -1;
        }
    }
    if (Platform::hasArg("nowildlife") || !g.renderer || !g.map) return;
    if (!gW.assetsStarted) startAssets();
    if (!gW.assetsReady) {
        if (!gW.jobs.done()) return;
        finishAssets(g);
    }
    double t0 = TimeSeconds();
    gW.frame++;
    gW.time += dt;
    gatherContext(g);
    if (gW.testScene < 0 && gW.spawning) {
        spawner(g, dt);
        despawnFar(g, dt);
    }
    if (gW.testScene < 0 || gW.testPhase != 2) {   // test shots: hold the staged moment while streaming settles
        updateAllGroups(g, dt);
        vehicleImpacts(g);
    }
    gW.msUpdate = Lerp(gW.msUpdate, (float)((TimeSeconds() - t0) * 1000.0), 0.05f);
    gW.logT += dt;
    if (gW.logT > 30.f) {
        gW.logT = 0.f;
        int n = 0, groups = 0, birds = 0;
        for (const Animal& a : gW.animals) {
            n += a.used;
            birds += a.used && isBird(a.sp);
        }
        for (const Group& G : gW.groups) groups += G.used;
        LOG("Wildlife: %d animals (%d birds) in %d groups, %d drawn in %d draws | update %.3f ms, render %.3f ms", n, birds, groups, gW.drawn,
            gW.drawCalls, gW.msUpdate, gW.msRender);
    }
    if (gW.testScene >= 0) testUpdate(g);
}

void submitRender(GameWorld& g) {
    using namespace wild_detail;
    if (!gW.assetsReady || !g.renderer || !g.renderer->dynamic) return;
    double t0 = TimeSeconds();
    submitAll(g);
    gW.msRender = Lerp(gW.msRender, (float)((TimeSeconds() - t0) * 1000.0), 0.05f);
}

bool bulletHit(GameWorld& g, int shooter, dvec3 from, vec3 dir, float range, float damage) {
    using namespace wild_detail;
    if (!gW.assetsReady) return false;
    vec3 o = from.toVec3();
    float bestT = range;
    int best = -1;
    for (int ai = 0; ai < (int)gW.animals.size(); ai++) {
        const Animal& a = gW.animals[ai];
        if (!a.used || a.state == ST_DEAD) continue;
        vec3 to = a.pos - o;
        float t = dot(to, dir);
        if (t < 0.f || t > bestT + 3.f) continue;
        float size = Max(speciesInfo(a.sp).length, speciesInfo(a.sp).wingspan) * a.scale;
        if (length2(to - dir * t) > (size + 1.f) * (size + 1.f)) continue;
        vec3 A, B;
        float r;
        hitCapsule(a, A, B, r);
        float dist;
        float th = raySegment(o, dir, bestT, A, B, dist);
        if (dist < r && th < bestT) {
            bestT = th;
            best = ai;
        }
    }
    if (best < 0) return false;
    WorldHit h;
    int ignoreVeh = shooter >= 0 && shooter < (int)g.peds.size() ? g.peds[shooter].vehicle : -1;
    if (g.raycast(from, dir, bestT, h, shooter, ignoreVeh) && h.t < bestT - 0.05f) return false;   // something in front
    Animal& a = gW.animals[best];
    vec3 hp = o + dir * bestT;
    spawnTracer(from, dvec3(hp));
    const SpeciesInfo& si = speciesInfo(a.sp);
    if (si.plan == PLAN_BIRD) {
        spawnFx(FX_LEAVES, dvec3(hp), -dir, 6, 0.3f, featherTint(a.sp));
    } else {
        spawnFx(FX_BLOOD, dvec3(hp), -dir, 3, 1.f);
        WorldHit h2;
        if (si.mass > 20.f && g.raycast(dvec3(hp), vec3(0, 0, -1), 3.f, h2, -1, -1, false, false)) spawnDecal(DECAL_BLOOD, h2.pos, h2.normal, 0.4f);
    }
    sfx(Audio::SFX_IMPACT_FLESH, hp, 0.7f);
    if (shooter >= 0 && shooter == g.player) g.pinfo.hitMarker = 1.f;
    // shotgun / rifle rounds throw small animals
    hurtAnimal(g, best, damage * (si.plan == PLAN_BIRD ? 3.f : 1.f), dir * (si.mass < 10.f ? 4.f : 1.5f), shooter, false);
    return true;
}

int liveCount() {
    int n = 0;
    for (const wild_detail::Animal& a : wild_detail::gW.animals) n += a.used;
    return n;
}

int sightings(const Render::Camera& cam, float maxDist, Sighting* out, int maxOut, float aspect, float minSize) {
    using namespace wild_detail;
    if (!out || maxOut <= 0) return 0;
    vec3 o = cam.pos.toVec3();
    vec3 f = cam.forward(), r = cam.right(), u = cross(r, f);
    float tanV = tanf(Clamp(cam.fovY, 0.02f, 2.8f) * 0.5f);
    aspect = Max(aspect, 0.1f);
    float cw;
    bool camUnder = waterAt(o.x, o.y, cw) && o.z < cw;
    Sighting found[SP_COUNT];
    for (int k = 0; k < SP_COUNT; k++) found[k] = {k, 0, 0.f, 1.f, 0.f};
    int rays = 0;
    for (const Animal& a : gW.animals) {
        if (!a.used || a.state == ST_DEAD || a.state == ST_FALL) continue;
        const SpeciesInfo& si = speciesInfo(a.sp);
        bool grounded = si.plan == PLAN_QUAD || si.plan == PLAN_REPTILE;
        bool flying = si.plan == PLAN_BIRD && (a.state == ST_FLY || a.state == ST_TAKEOFF || a.state == ST_LAND);
        float extent = Max(si.length, flying ? si.wingspan : si.height) * a.scale;
        vec3 c = a.pos + vec3(0.f, 0.f, grounded ? si.height * 0.5f * a.scale : 0.f);
        float wz;
        if (waterAt(c.x, c.y, wz)) {
            if (!camUnder && c.z < wz - 1.2f) continue;   // too deep to see from above the surface
            if (camUnder && c.z > wz + 0.3f) continue;    // above the surface, seen from below it
        }
        vec3 d = c - o;
        float len = length(d);
        if (len > maxDist || len < 0.3f) continue;
        float z = dot(d, f);
        if (z < 0.3f) continue;
        float x = dot(d, r) / (z * tanV * aspect), y = dot(d, u) / (z * tanV);
        if (fabsf(x) > 1.f || fabsf(y) > 1.f) continue;
        float size = extent / (2.f * z * tanV);
        if (size < minSize) continue;
        if (Phys::gCollision && rays < 64) {   // hidden behind walls, trees, terrain?
            rays++;
            Phys::RayHit rh;
            float clear = len - extent * 0.5f;
            if (clear > 0.2f && Phys::gCollision->raycast(o, d / len, clear, rh, true)) continue;
        }
        Sighting& S = found[a.sp];
        S.count++;
        if (size > S.size) {
            S.size = size;
            S.centre = Max(fabsf(x), fabsf(y));
            S.dist = len;
        }
    }
    Sighting list[SP_COUNT];
    int n = 0;
    for (int k = 0; k < SP_COUNT; k++)
        if (found[k].count > 0) list[n++] = found[k];
    std::sort(list, list + n, [](const Sighting& a, const Sighting& b) { return a.size > b.size; });
    n = Min(n, maxOut);
    for (int k = 0; k < n; k++) out[k] = list[k];
    return n;
}

int visibleSpecies(const Render::Camera& cam, float maxDist, int* outSpecies, int maxOut) {
    if (!outSpecies || maxOut <= 0) return 0;
    Sighting list[Fauna::SP_COUNT];
    int n = sightings(cam, maxDist, list, Min(maxOut, (int)Fauna::SP_COUNT));
    for (int k = 0; k < n; k++) outSpecies[k] = list[k].species;
    return n;
}

const char* speciesName(int species) { return species >= 0 && species < Fauna::SP_COUNT ? Fauna::speciesInfo(species).name : ""; }

int speciesCount() { return Fauna::SP_COUNT; }

}  // namespace Wildlife
}  // namespace Game
