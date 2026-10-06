// Pedestrian entities: spawning, kinematic character controller, animation, rendering, vehicle seating.
#include "gameworld.h"

namespace Game {

GameWorld* gGame = nullptr;

namespace ped_detail {

constexpr float kPedRadius = 0.3f;
constexpr float kPedHeight = 1.78f;
constexpr float kStepUp = 0.55f;

float angleDiff(float a, float b) {
    float d = fmodf(a - b + kPi, kTwoPi);
    if (d < 0) d += kTwoPi;
    return d - kPi;
}

quat yawQuat(float yaw) { return quatAxisAngle(vec3(0, 0, 1), yaw); }

#ifdef HAVE_AUDIO
static_assert((int)Audio::FOOT_ASPHALT == (int)Phys::SURF_ASPHALT && (int)Audio::FOOT_WATER == (int)Phys::SURF_WATER &&
                  (int)Audio::FOOT_WOOD == (int)Phys::SURF_WOOD && (int)Audio::FOOT_MUD == (int)Phys::SURF_MUD,
              "Audio::FootSurface follows Phys::SurfaceType");
// Footwear of a ped for its footsteps: dress shoes are heels on women, leather soles on men; flats click like leather.
u8 footwearOf(const GameWorld& g, const Ped& p) {
    if (p.charIndex < 0 || p.charIndex >= (int)g.chars.size()) return Audio::FOOTWEAR_SNEAKER;
    const Anim::CharacterDesc& d = g.chars[(size_t)p.charIndex].desc;
    switch (d.shoes) {
        case Anim::detail::SHOE_DRESS: return d.gender == Anim::FEMALE ? Audio::FOOTWEAR_HEEL : Audio::FOOTWEAR_LEATHER;
        case Anim::detail::SHOE_FLATS: return Audio::FOOTWEAR_LEATHER;
        case Anim::detail::SHOE_BOOT: return Audio::FOOTWEAR_BOOT;
        case Anim::detail::SHOE_SANDAL: return Audio::FOOTWEAR_SANDAL;
        case Anim::detail::SHOE_BARE: return Audio::FOOTWEAR_BARE;
        default: return Audio::FOOTWEAR_SNEAKER;
    }
}
// Body weight relative to an average adult (footstep force and pitch).
float bodyWeightOf(const GameWorld& g, const Ped& p) {
    if (p.charIndex < 0 || p.charIndex >= (int)g.chars.size()) return 1.f;
    const Anim::CharacterDesc& d = g.chars[(size_t)p.charIndex].desc;
    float h = d.height / 1.75f;
    return Clamp(h * h * (0.8f + 0.4f * d.weight + 0.15f * d.muscle) * (d.gender == Anim::FEMALE ? 0.85f : 1.f), 0.5f, 1.8f);
}
// The ground under a foot as an Audio::FootSurface (standing water counts as wading).
u8 footSurfaceOf(const Phys::GroundHit& gh) {
    return gh.water ? (u8)Audio::FOOT_WATER : (u8)Min((int)gh.surface, (int)Audio::FOOT_SURFACE_COUNT - 1);
}
#endif

}  // namespace ped_detail

using namespace ped_detail;

int GameWorld::spawnPed(int charIndex, dvec3 pos, float yaw, Faction f) {
    if (charIndex < 0 || charIndex >= (int)chars.size()) return -1;
    int id = -1;
    for (int i = 0; i < (int)peds.size(); i++)
        if (!peds[i].used) { id = i; break; }
    if (id < 0) {
        id = (int)peds.size();
        peds.emplace_back();
    }
    Ped& p = peds[id];
    p = Ped();
    p.used = true;
    p.uid = nextUid++;
    p.charIndex = charIndex;
    const CharEntry& ce = chars[charIndex];
    p.anim.init(&ce.skel, p.uid * 2654435761u);
    p.anim.setCharacter(ce.desc);   // walking style, posture, fidgets from age / build / role
    p.pos = pos;
    p.yaw = yaw;
    p.faction = f;
    p.female = ce.desc.gender == Anim::FEMALE;
    p.voice.pitch = p.female ? 190.f + (float)(hash32(p.uid) % 50) : 98.f + (float)(hash32(p.uid) % 40);
    p.voice.formantScale = p.female ? 1.14f : 1.f;
    p.voice.speed = 0.95f + (hash32(p.uid * 7) % 20) * 0.01f;
    p.voice.roughness = ce.desc.age * 0.4f;
    p.hasWeapon[WPN_FISTS] = true;
    float gz = groundHeight((float)pos.x, (float)pos.y, (float)pos.z + 1.f);
    if (gz > -1e8f && fabs(gz - pos.z) < 2.5) p.pos.z = gz;
    p.groundZ = (float)p.pos.z;
    for (int b = 0; b < Anim::B_COUNT; b++) {
        p.skin[b] = mat4();
        p.bones[b] = mat4();
    }
    animatePed(p, 0.f);
    return id;
}

void GameWorld::despawnPed(int id) {
    if (id < 0 || id >= (int)peds.size() || !peds[id].used) return;
    Ped& p = peds[id];
    if (p.vehicle >= 0) {
        Vehicle& v = vehicles[p.vehicle];
        for (int& s : v.seats)
            if (s == id) s = -1;
    }
    freeRagdoll(p.ragdoll);
    p.used = false;
    if (id == player) player = -1;
}

float GameWorld::groundHeight(float x, float y, float zRef) const {
    Phys::GroundHit g = Phys::gCollision->ground(x, y, zRef, kStepUp);
    return g.z;
}

void GameWorld::pedsNear(vec2 c, float r, std::vector<int>& out) const {
    out.clear();
    float r2 = r * r;
    for (int i = 0; i < (int)peds.size(); i++) {
        const Ped& p = peds[i];
        if (!p.used) continue;
        float dx = (float)p.pos.x - c.x, dy = (float)p.pos.y - c.y;
        if (dx * dx + dy * dy <= r2) out.push_back(i);
    }
}

vec3 GameWorld::pedHeadPos(const Ped& p) const {
    vec3 h = p.bones[Anim::B_HEAD].c[3].xyz();
    if (p.ragdoll) return h;  // ragdoll bones are stored in world-relative form (see ragdoll.cpp)
    vec3 w = rotate(yawQuat(p.yaw), h);
    return p.pos.toVec3() + w;
}

vec3 GameWorld::pedChestPos(const Ped& p) const {
    vec3 h = p.bones[Anim::B_CHEST].c[3].xyz();
    if (p.ragdoll) return h;
    return p.pos.toVec3() + rotate(yawQuat(p.yaw), h);
}

// ------------------------------------------------------------------------------------------------------------------
// Kinematic capsule controller.
void GameWorld::movePed(Ped& p, vec2 desiredVel, float dt, bool jump) {
    if (p.forcedT > 0.f) {
        // melee dodge / lunge / knock-back: velocity override with an instant response
        p.forcedT -= dt;
        desiredVel = p.forcedVel;
        if (p.grounded) {
            p.vel.x = desiredVel.x;
            p.vel.y = desiredVel.y;
        }
        jump = false;
    }
    if (p.legInjury > 0.f) {
        // limping on a wounded leg
        p.legInjury -= dt;
        float cap = p.isPlayer ? 3.2f : 1.4f;
        float l = length(desiredVel);
        if (l > cap) desiredVel = desiredVel * (cap / l);
    }
    vec2 v(p.vel.x, p.vel.y);
    bool swimming = p.state == PS_SWIM;
    float accel = p.grounded || swimming ? (length(desiredVel) > length(v) ? 11.f : 16.f) : 2.5f;
    vec2 dv = desiredVel - v;
    float dl = length(dv), maxDv = accel * dt;
    if (dl > maxDv) dv = dv * (maxDv / dl);
    v += dv;
    p.vel.x = v.x;
    p.vel.y = v.y;
    if (p.grounded && jump && !swimming) {
        p.vel.z = 4.4f;
        p.grounded = false;
        p.fallStartZ = (float)p.pos.z;
        p.airTime = 0.f;
    }
    if (!p.grounded && !swimming) p.vel.z -= 9.81f * dt;
    vec3 old = p.pos.toVec3();
    vec3 np = old + p.vel * dt;
    // static colliders: two relaxation passes
    for (int it = 0; it < 2; it++) {
        vec3 push, n;
        if (Phys::gCollision->capsuleOverlap(np, kPedRadius, kPedHeight, push, n)) {
            np += vec3(push.x, push.y, 0);
            float vn = dot(p.vel, n);
            if (vn < 0) p.vel -= n * vn;
        }
    }
    // ground / water
    float zRef = p.grounded ? old.z : Max(old.z, np.z);
    Phys::GroundHit g = Phys::gCollision->ground(np.x, np.y, zRef + 0.05f, p.grounded ? kStepUp : 0.3f);
    float wz = 0.f;
    bool water = Phys::waterSurface(np.x, np.y, wz) && wz > g.z;
    if (water && wz - g.z > 1.35f && (np.z < wz - 1.0f || swimming)) {
        // deep water: swim at the surface
        if (!swimming) {
            p.state = PS_SWIM;
            p.stateTime = 0.f;
#ifdef HAVE_AUDIO
            if (p.vel.z < -3.f) Audio::play(Audio::SFX_SPLASH_BIG, np);
            else Audio::play(Audio::SFX_SPLASH_SMALL, np);
#endif
        }
        float target = Max(wz - 1.32f - p.diveDepth, g.z + 0.3f);
        np.z = Lerp(np.z, target, Saturate(dt * (p.diveDepth > 0.f ? 2.5f : 6.f)));
        p.vel.z = 0.f;
        p.grounded = false;
        p.airTime = 0.f;
        p.groundZ = g.z;
    } else {
        if (swimming) {
            p.state = PS_ONFOOT;  // walked out of the water
            p.stateTime = 0.f;
        }
        if (p.grounded) {
            if (g.z > np.z - 0.6f && g.z < np.z + kStepUp + 0.01f) {
                // smooth step up for curbs/stairs, snap down
                np.z = g.z > np.z ? Lerp(np.z, g.z, Saturate(dt * 18.f)) + (g.z - np.z) * 0.35f : g.z;
                if (fabsf(np.z - g.z) < 0.02f) np.z = g.z;
                p.vel.z = 0.f;
                p.airTime = 0.f;
            } else if (g.z <= np.z - 0.6f) {
                p.grounded = false;
                p.fallStartZ = np.z;
                p.airTime = 0.f;
            }
        } else {
            p.airTime += dt;
            if (np.z <= g.z && p.vel.z <= 0.f) {
                float impact = -p.vel.z;
                np.z = g.z;
                p.vel.z = 0.f;
                p.grounded = true;
                float fallH = p.fallStartZ - g.z;
                if (impact > 11.f || fallH > 7.f) {
                    float dmg = Max(0.f, (impact - 10.f) * 14.f) + Max(0.f, (fallH - 6.f) * 9.f);
                    int self = (int)(&p - &peds[0]);
                    damagePed(self, dmg, DMG_FALL, -1, vec3(0, 0, -1));
                    if (p.used && p.health > 0.f && (impact > 13.f || fallH > 9.f)) knockDown(self, vec3(p.vel.x, p.vel.y, 0) * 60.f);
                } else if (impact > 4.f) {
                    p.pendingAction = Anim::CLIP_LAND;
                }
#ifdef HAVE_AUDIO
                if (impact > 2.5f && (p.isPlayer || p.visibleDist < 40.f)) {
                    Audio::Footstep f;
                    f.pos = np;
                    f.event = Audio::FOOT_LAND;
                    f.impact = impact;
                    f.speed = length(vec2(p.vel.x, p.vel.y));
                    f.surface = footSurfaceOf(g);
                    f.footwear = footwearOf(*this, p);
                    f.weight = bodyWeightOf(*this, p);
                    f.wetness = env ? env->wetness : 0.f;
                    f.player = p.isPlayer;
                    Audio::playFootstep(f);
                }
#endif
            }
        }
        p.groundZ = g.z;
    }
    p.pos = dvec3(np);
    if (p.pos.z < -60.0) p.pos.z = -60.0;
}

// ------------------------------------------------------------------------------------------------------------------
// Vaulting and climbing over low obstacles (walls, props, vehicles).
bool GameWorld::probeObstacle(const Ped& p, vec3 dir, float reach, float& topZ, float& thickness, vec3& hitPos, vec3& hitNormal) const {
    vec3 base = p.pos.toVec3();
    float feet = base.z;
    float best = 1e9f;
    bool found = false;
    // sample rays at knee, waist and chest height
    const float heights[3] = {0.45f, 0.95f, 1.45f};
    for (float h : heights) {
        WorldHit wh;
        vec3 o = base + vec3(0, 0, h);
        if (!raycast(dvec3(o), dir, reach, wh, (int)(&p - &peds[0]), -1, false, true)) continue;
        if (wh.t >= best) continue;
        if (fabsf(wh.normal.z) > 0.6f) continue;  // floors/roofs are not obstacles
        best = wh.t;
        hitPos = wh.pos.toVec3();
        hitNormal = wh.normal;
        found = true;
        // obstacle top and thickness
        if (wh.vehicle >= 0) {
            const Vehicle& v = vehicles[wh.vehicle];
            const Vehicles::VehicleModel& spec = vassets[v.model].spec;
            topZ = (float)v.sim.body.pos.z + spec.boxCenter.z + spec.boxHalf.z * 0.9f;
            vec3 f = v.sim.forward(), r = v.sim.right();
            thickness = fabsf(dot(dir, r)) > fabsf(dot(dir, f)) ? spec.boxHalf.x * 2.f : spec.boxHalf.y * 2.f;
        } else if (wh.collider >= 0) {
            const Phys::Collider& c = Phys::gCollision->collider(wh.collider);
            topZ = c.kind == Phys::COL_BOX ? c.c.z + c.he.z : c.c.z + c.he.z;
            if (c.kind == Phys::COL_BOX) {
                vec2 ax = c.ax, ay = perp(c.ax);
                thickness = fabsf(dot(dir.xy(), ax)) > fabsf(dot(dir.xy(), ay)) ? c.he.x * 2.f : c.he.y * 2.f;
            } else {
                thickness = c.he.x * 2.f;
            }
        } else {
            // terrain step / ledge: measure the ground just beyond the hit
            vec3 beyond = hitPos + dir * 0.4f;
            topZ = groundHeight(beyond.x, beyond.y, feet + 3.f);
            thickness = 3.f;
        }
    }
    (void)feet;
    return found;
}

bool GameWorld::tryTraverse(Ped& p, vec3 dir) {
    float top, thick;
    vec3 hit, nrm;
    dir.z = 0;
    dir = normalize(dir);
    if (!probeObstacle(p, dir, 1.1f, top, thick, hit, nrm)) return false;
    float feet = (float)p.pos.z;
    float h = top - feet;
    if (h < 0.4f || h > 2.4f) return false;
    vec3 start = p.pos.toVec3();
    if (h <= 1.25f && thick < 1.6f) {
        // vault over: land on the far side
        vec3 land = hit + dir * (thick + 0.55f);
        land.z = groundHeight(land.x, land.y, top + 0.5f);
        if (land.z > top + 0.3f || land.z < feet - 3.5f) return false;
        vec3 push, n;
        if (Phys::gCollision->capsuleOverlap(land, 0.3f, 1.7f, push, n)) return false;
        p.moveMode = 2;
        p.traverseFrom = start;
        p.traverseMid = vec3(hit.x, hit.y, top + 0.15f) + dir * (thick * 0.5f);
        p.traverseTo = land;
        p.traverseDur = 0.55f + h * 0.15f;
        p.pendingAction = Anim::CLIP_VAULT;
    } else {
        // climb onto the top surface
        vec3 onTop = hit + dir * Min(thick * 0.5f, 0.6f);
        onTop.z = top;
        vec3 push, n;
        if (Phys::gCollision->capsuleOverlap(onTop + vec3(0, 0, 0.05f), 0.28f, 1.7f, push, n) && length(push) > 0.1f) return false;
        p.moveMode = 3;
        p.traverseFrom = start;
        p.traverseMid = vec3(hit.x, hit.y, top + 0.1f) - dir * 0.15f;
        p.traverseTo = onTop;
        p.traverseDur = 0.8f + h * 0.2f;
        p.pendingAction = Anim::CLIP_CLIMB;
    }
    p.traverseT = 0.f;
    p.yaw = atan2f(-dir.x, dir.y);
    p.vel = vec3(0);
    p.aiming = false;
#ifdef HAVE_AUDIO
    if (p.isPlayer || p.visibleDist < 30.f) {
        Audio::playFoley(vec3(hit.x, hit.y, top), Audio::FOLEY_GRAB, p.moveMode == 3 ? 1.f : 0.8f);
        Audio::playFoley(start + vec3(0.f, 0.f, 1.f), Audio::FOLEY_CLOTH, p.moveMode == 3 ? 1.f : 0.8f);
    }
#endif
    return true;
}

void GameWorld::updateTraverse(Ped& p, float dt) {
    p.traverseT += dt;
    float t = Saturate(p.traverseT / Max(p.traverseDur, 0.05f));
    // quadratic Bezier through the apex
    vec3 pos;
    if (p.moveMode == 3) {
        // climb: pull up first, then step forward onto the top
        float tz = Saturate(t / 0.62f), txy = Saturate((t - 0.45f) / 0.55f);
        tz = tz * tz * (3.f - 2.f * tz);
        pos = vec3(Lerp(p.traverseFrom.x, p.traverseTo.x, txy), Lerp(p.traverseFrom.y, p.traverseTo.y, txy), Lerp(p.traverseFrom.z, p.traverseTo.z, tz));
    } else {
        vec3 a = lerp(p.traverseFrom, p.traverseMid, t), b = lerp(p.traverseMid, p.traverseTo, t);
        pos = lerp(a, b, t);
    }
    p.pos = dvec3(pos);
    p.grounded = true;
    p.vel = vec3(0);
    if (t >= 1.f) {
#ifdef HAVE_AUDIO
        if (p.isPlayer || p.visibleDist < 30.f) {  // feet coming down on the far side / on top
            Audio::Footstep f;
            f.pos = p.traverseTo;
            f.event = Audio::FOOT_LAND;
            f.impact = p.moveMode == 2 ? 3.2f : 2.2f;
            f.surface = footSurfaceOf(Phys::gCollision->ground(p.traverseTo.x, p.traverseTo.y, p.traverseTo.z + 0.2f));
            f.footwear = footwearOf(*this, p);
            f.weight = bodyWeightOf(*this, p);
            f.wetness = env ? env->wetness : 0.f;
            f.player = p.isPlayer;
            Audio::playFootstep(f);
        }
#endif
        p.moveMode = 0;
        p.pos = dvec3(p.traverseTo);
        p.groundZ = p.traverseTo.z;
        p.airTime = 0.f;
    }
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::startLipSync(int pid, const char* spokenText, const Audio::VoiceParams& voice) {
    if (pid < 0 || pid >= (int)peds.size() || !peds[pid].used || !spokenText) return;
    Ped& p = peds[pid];
    if (p.visibleDist > 40.f) return;   // nobody sees the mouth from further away
    p.lipKeys.clear();
    Speech::lipSync(spokenText, voice, p.lipKeys);
    p.lipStyles.clear();
    Speech::styleTimeline(spokenText, voice, p.lipStyles);
    p.lipAccents.clear();
    Speech::accentCues(spokenText, voice, p.lipAccents);
    p.lipStart = Platform::timeSeconds();
    p.lipIdx = 0;
    // conversation partners: peds standing within 3.5 m turn to listen for the length of the line (not those busy
    // fleeing / fighting / driving); the speaker addresses the closest one
    float lineLen = p.lipKeys.empty() ? 2.f : p.lipKeys.back().time + p.lipKeys.back().duration;
    int nearest = -1, listeners = 0;
    float best = 1e9f;
    for (int j = 0; j < (int)peds.size() && listeners < 4; j++) {
        Ped& o = peds[j];
        if (j == pid || !o.used || o.state != PS_ONFOOT || o.ragdoll || o.health <= 0.f) continue;
        BrainType bt = o.brain.type;
        if (bt == BRAIN_FLEE || bt == BRAIN_COWER || bt == BRAIN_COMBAT || bt == BRAIN_ARREST) continue;
        float d = length(rel(o.pos, p.pos));
        if (d > 3.5f) continue;
        o.listenUntil = p.lipStart + lineLen + 0.6;
        o.listenTo = pid;
        listeners++;
        if (d < best) best = d, nearest = j;
    }
    if (nearest >= 0) {
        p.lookPed = nearest;
        p.lookT = lineLen + 0.3f;
    }
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::animatePed(Ped& p, float dt) {
    if (p.charIndex < 0) return;
    const CharEntry& ce = chars[p.charIndex];
    if (p.ragdoll) return;  // ragdoll writes bones/skin directly
    Anim::AnimInput& in = p.animIn;
    vec2 hv(p.vel.x, p.vel.y);
    float spd = length(hv);
    in.speed = spd;
    in.turnRate = p.turnRate;
    vec2 fwd(-sinf(p.yaw), cosf(p.yaw)), rightV(cosf(p.yaw), sinf(p.yaw));
    in.localMoveDir = spd > 0.1f ? normalize(vec2(dot(hv, rightV), dot(hv, fwd))) : vec2(0, 1);
    in.inAir = !p.grounded && p.state == PS_ONFOOT && p.airTime > 0.15f;
    in.swimming = p.state == PS_SWIM;
    in.aiming = p.aiming;
    in.firing = p.firing;
    in.reloading = p.reloadTimer > 0.f;
    in.weaponKind = weaponInfo(p.weapon).animKind;
    in.aimPitch = p.aimPitch;
    if (p.state == PS_INVEHICLE && p.vehicle >= 0) {
        in.speed = 0.f;
        in.inAir = false;
        in.stance = isBike(p.vehicle) ? 3 : (p.seat == 0 ? 1 : 2);
        // the driver's hands follow the applied steering (not the direction the car is sliding)
        if (p.seat == 0) in.localMoveDir = vec2(Clamp(vehicles[p.vehicle].sim.steerOut, -1.f, 1.f), 1.f);
        // ... on this vehicle's own rim (the live part gameworld turns by wheelTurn()), given in the ped's model space:
        // origin 0.5 m below the seat's hip point, the vehicle's axes; no rim part = the animator's typical car rim
        in.wheelR = 0.f;
        const Vehicles::VehicleModel& spec = vassets[vehicles[p.vehicle].model].spec;
        if (in.stance == 1 && spec.steerWheelRadius > 0.f && p.seat < (int)spec.seats.size()) {
            in.wheelC = spec.steerWheelPos - (spec.seats[p.seat].pos - vec3(0.f, 0.f, 0.5f));
            in.wheelN = normalize(spec.steerWheelAxis);
            in.wheelR = spec.steerWheelRadius;
        }
    } else if (p.state == PS_ONFOOT || p.state == PS_SWIM) {
        if (in.stance == 1 || in.stance == 2 || in.stance == 3) in.stance = 0;
    }
    in.action = -1;
    if (p.pendingAction >= 0) {
        // during a synced takedown only the takedown clips may start (AI reactions must not break the pair)
        bool takedownClip = p.pendingAction == Anim::CLIP_TAKEDOWN_ATTACKER || p.pendingAction == Anim::CLIP_TAKEDOWN_VICTIM;
        if (p.takedownT < 0.f || takedownClip) in.action = p.pendingAction;
        p.pendingAction = -1;
    }
    in.meleeKind = p.weapon == WPN_KNIFE ? 1 : (p.weapon == WPN_BAT ? 2 : 0);
    // lip sync: jaw opening from the viseme keys of the line being spoken (crossfaded, leading the audio slightly)
    in.mouthOpen = -1.f;
    in.viseme = in.visemeNext = -1;
    in.visemeWeight = in.visemeBlend = 0.f;
    in.expression = -1;          // automatic (pain / fear / anger / mood) unless the line's emotion says otherwise
    in.expressionWeight = 1.f;
    in.brow = in.nod = 0.f;
    in.speaking = in.listening = false;
    in.beat = 0.f;
    in.gestureAmount = 1.f;
    if (p.lipStart >= 0.0 && !p.lipKeys.empty()) {
        const std::vector<Speech::VisemeKey>& K = p.lipKeys;
        float t = (float)(Platform::timeSeconds() - p.lipStart) + 0.03f;
        if (t > K.back().time + K.back().duration) {
            p.lipKeys.clear();
            p.lipStart = -1.0;
        } else if (t >= 0.f) {
            // jaw opening per viseme (Oculus order: sil PP FF TH DD kk CH SS nn RR aa E I O U)
            static const float kJaw[Speech::VISEME_COUNT] = {0.f, 0.f, 0.12f, 0.2f, 0.3f, 0.35f, 0.15f, 0.1f, 0.25f, 0.3f, 1.f, 0.65f, 0.35f, 0.8f, 0.4f};
            int k = p.lipIdx;
            if (k >= (int)K.size() || K[k].time > t) k = 0;
            while (k + 1 < (int)K.size() && K[k + 1].time <= t) k++;
            p.lipIdx = k;
            auto jaw = [&](const Speech::VisemeKey& key) { return kJaw[Min((int)key.viseme, (int)Speech::VISEME_COUNT - 1)] * Saturate(key.weight); };
            float cur = jaw(K[k]), nxt = k + 1 < (int)K.size() ? jaw(K[k + 1]) : 0.f;
            float left = K[k].time + K[k].duration - t;
            float blend = left < 0.05f ? 0.5f * (1.f - left / 0.05f) : 0.f;   // 50 ms crossfade into the next shape
            in.mouthOpen = Saturate(Lerp(cur, nxt, blend));
            // full mouth shapes (lips, corners, tongue) from the same keys
            in.viseme = K[k].viseme;
            in.visemeWeight = Saturate(K[k].weight);
            in.visemeNext = k + 1 < (int)K.size() ? (int)K[k + 1].viseme : 0;
            in.visemeBlend = blend * 2.f;
            in.speaking = true;
            // facial performance: the voiced emotion, plus brow raises / nods on stressed syllables
            for (const Speech::StyleSpan& sp : p.lipStyles)
                if (t >= sp.start && t < sp.end) {
                    switch (sp.style.emotion) {
                        case Speech::EMOTION_HAPPY: in.expression = 1; break;
                        case Speech::EMOTION_SAD: in.expression = 2; break;
                        case Speech::EMOTION_ANGRY: in.expression = 3; break;
                        case Speech::EMOTION_SCARED: in.expression = 4; break;
                        case Speech::EMOTION_SHOUT: in.expression = 3; in.expressionWeight = 0.6f; break;
                        case Speech::EMOTION_CALM: case Speech::EMOTION_WHISPER: in.expression = 0; break;
                        default: break;
                    }
                    switch (sp.style.emotion) {   // how much the hands talk
                        case Speech::EMOTION_SHOUT: in.gestureAmount = 1.35f; break;
                        case Speech::EMOTION_ANGRY: in.gestureAmount = 1.25f; break;
                        case Speech::EMOTION_HAPPY: case Speech::EMOTION_SCARED: in.gestureAmount = 1.1f; break;
                        case Speech::EMOTION_CALM: in.gestureAmount = 0.7f; break;
                        case Speech::EMOTION_SAD: in.gestureAmount = 0.6f; break;
                        case Speech::EMOTION_WHISPER: in.gestureAmount = 0.4f; break;
                        default: break;
                    }
                    if (in.expression >= 0 && sp.style.emotion != Speech::EMOTION_SHOUT) in.expressionWeight = Saturate(sp.style.intensity);
                    break;
                }
            for (const Speech::AccentCue& ac : p.lipAccents) {
                float d = t - ac.time;
                if (d > -0.12f && d < 0.3f) {
                    float env = d < 0.f ? 1.f + d / 0.12f : 1.f - d / 0.3f;   // quick rise, slower fall
                    in.brow = Max(in.brow, env * ac.strength * (ac.nuclear ? 0.8f : 0.5f));
                    in.nod = Max(in.nod, env * ac.strength * (ac.nuclear ? 0.7f : 0.3f));
                }
                // beat gestures: the hand's down-stroke lands slightly ahead of the stressed syllable
                float db = d + 0.09f;
                if (db > -0.12f && db < 0.3f) {
                    float env = db < 0.f ? 1.f + db / 0.12f : 1.f - db / 0.3f;
                    in.beat = Max(in.beat, env * ac.strength * (ac.nuclear ? 1.f : 0.6f));
                }
            }
        }
    }
    // conversation: listeners hold a listening pose and look at the speaker; speakers address their listener;
    // bystanders glance at the player walking past (and the player at people close by)
    double nowT = Platform::timeSeconds();
    int look = -1;
    float lookW = 0.f;
    bool upright = p.state == PS_ONFOOT && !p.aiming && p.health > 0.f;
    if (p.listenUntil > nowT && p.listenTo >= 0 && p.listenTo < (int)peds.size() && peds[p.listenTo].used) {
        if (upright && spd < 0.6f) in.listening = true;
        look = p.listenTo;
        lookW = 0.85f;
    } else {
        p.listenTo = -1;
        if (p.lookT > 0.f) look = p.lookPed, lookW = in.speaking ? 0.8f : 0.6f;
    }
    p.lookT -= dt;
    p.glanceNext -= dt;
    if (p.glanceNext <= 0.f) {
        u32 h = hash32(p.uid * 2654435761u + (u32)(nowT * 7.0));
        p.glanceNext = 1.5f + 3.f * hashToFloat(h);
        if (upright && p.lookT <= 0.f && look < 0 && p.visibleDist < 25.f) {
            int cand = -1;
            if (!p.isPlayer && player >= 0) {
                cand = player;   // NPC: notice the player passing close in front (more often when armed)
                float chance = weaponInfo(peds[player].weapon).animKind != 0 ? 0.7f : 0.35f;
                if (hashToFloat(hash32(h + 1u)) > chance) cand = -1;
            } else if (p.isPlayer) {
                float bestD = 3.5f;   // player: the closest ped standing or walking by in front
                for (int j = 0; j < (int)peds.size(); j++) {
                    const Ped& o = peds[j];
                    if (!o.used || &o == &p || o.state != PS_ONFOOT || o.visibleDist > 10.f) continue;
                    float d = length(rel(o.pos, p.pos));
                    if (d < bestD) bestD = d, cand = j;
                }
                if (hashToFloat(hash32(h + 2u)) > 0.45f) cand = -1;
            }
            if (cand >= 0) {
                vec3 D = rel(peds[cand].pos, p.pos);
                float d = length(vec2(D.x, D.y));
                bool inFront = dot(vec2(D.x, D.y), fwd) > 0.3f * d;
                if (d < (p.isPlayer ? 3.5f : 5.f) && inFront) {
                    p.lookPed = cand;
                    p.lookT = 1.2f + 1.5f * hashToFloat(hash32(h + 3u));
                }
            }
        }
    }
    in.lookWeight = 0.f;
    if (look >= 0 && look < (int)peds.size() && peds[look].used && upright) {
        const Ped& o = peds[look];
        float headZ = o.ragdoll ? 0.3f : o.bones[Anim::B_HEAD].c[3].z;
        vec3 D = rel(o.pos, p.pos) + vec3(0.f, 0.f, headZ);
        in.lookAt = vec3(dot(vec2(D.x, D.y), rightV), dot(vec2(D.x, D.y), fwd), D.z);
        in.lookWeight = in.lookAt.y > -0.2f ? lookW : 0.f;   // never wrench the head round to someone behind
    }
    // passing traffic: when nobody else has its eyes, a ped close by watches a car going past in front now and then
    // (a given ped and car always answer the same, so a car is followed all the way by, on average, a third of them)
    if (in.lookWeight <= 0.f && upright && !p.isPlayer && p.visibleDist < 20.f) {
        float best = 14.f;
        int bv = -1;
        for (int v = 0; v < (int)vehicles.size(); v++) {
            const Vehicle& c = vehicles[v];
            if (!c.used || c.sim.speed() < 5.f || hash32(p.uid * 131u + c.uid * 7u) % 100u >= 35u) continue;
            vec3 D = rel(c.sim.body.pos, p.pos);
            float dd = length(vec2(D.x, D.y));
            if (dd < best && dot(vec2(D.x, D.y), fwd) > -0.2f * dd) best = dd, bv = v;
        }
        if (bv >= 0) {
            vec3 D = rel(vehicles[bv].sim.body.pos, p.pos) + vec3(0.f, 0.f, 1.f);
            in.lookAt = vec3(dot(vec2(D.x, D.y), rightV), dot(vec2(D.x, D.y), fwd), D.z);
            in.lookWeight = 0.5f;
        }
    }
    // phone at the ear (player on a call, NPCs chatting on the phone)
    in.phoneCall = p.phoneCall && upright;
    in.phoneBrowse = p.phoneBrowse && upright && !p.phoneCall;
    // the prop in hand (carry.cpp draws it from the same answer): the holding arm is posed for it
    in.carry = effectiveCarry(p);
    in.carryOpen = in.carry == CARRY_UMBRELLA && umbrellaWeather();
    // synced takedown: the attacker's choke arm finds the victim's actual neck (tall / short pairs still connect)
    in.grabWeight = 0.f;
    if (p.takedownT >= 0.f && !p.takedownVictim && p.takedownPartner >= 0 && p.takedownPartner < (int)peds.size()) {
        const Ped& v = peds[p.takedownPartner];
        if (v.used && !v.ragdoll && v.charIndex >= 0) {
            vec3 neck = rel(v.pos, p.pos) + rotate(yawQuat(v.yaw), v.bones[Anim::B_NECK].c[3].xyz());
            in.grabTarget = vec3(dot(vec2(neck.x, neck.y), rightV), dot(vec2(neck.x, neck.y), fwd), neck.z);
            in.grabWeight = 1.f;
        }
    }
    // a greeting (population.cpp: a hug, a handshake, a kiss on the cheek, started on both together): hands / face onto
    // the partner's real chest / head, so tall / short pairs still meet (the animator reads it only during those clips)
    {
        int self = (int)(&p - peds.data());
        const PedAI* q = self >= 0 && self < (int)ai.ped.size() && ai.ped[self].uid == p.uid ? &ai.ped[self] : nullptr;
        int o = q && q->greetT > 0.f ? q->greetWith : -1;
        if (o >= 0 && o < (int)peds.size() && o < (int)ai.ped.size() && peds[o].used && ai.ped[o].uid == peds[o].uid &&
            ai.ped[o].greetWith == self && !peds[o].ragdoll && peds[o].charIndex >= 0) {
            const Ped& v = peds[o];
            bool kiss = p.anim.action == Anim::CLIP_CHEEK_KISS || in.action == Anim::CLIP_CHEEK_KISS;   // (starting now)
            vec3 j = rel(v.pos, p.pos) + rotate(yawQuat(v.yaw), v.bones[kiss ? Anim::B_HEAD : Anim::B_CHEST].c[3].xyz());
            in.grabTarget = vec3(dot(vec2(j.x, j.y), rightV), dot(vec2(j.x, j.y), fwd), j.z);
            in.grabWeight = 1.f;
        }
    }
    // foot IK: probe the ground where each foot is planted / about to land (Animator::footProbe) and the slope under
    // them (only for nearby peds)
    in.groundOffsetL = in.groundOffsetR = 0.f;
    in.groundNormal = vec3(0, 0, 1);
    in.footProbes = false;
    if (p.visibleDist < 30.f && p.grounded && p.state == PS_ONFOOT) {
        quat q = yawQuat(p.yaw);
        vec3 base = p.pos.toVec3();
        vec3 pl = p.anim.footProbe(0), pr = p.anim.footProbe(1);
        vec3 fl = base + rotate(q, vec3(pl.x, pl.y, 0.f)), fr = base + rotate(q, vec3(pr.x, pr.y, 0.f));
        Phys::GroundHit hl = Phys::gCollision->ground(fl.x, fl.y, base.z + 0.3f, kStepUp);
        Phys::GroundHit hr = Phys::gCollision->ground(fr.x, fr.y, base.z + 0.3f, kStepUp);
        in.groundOffsetL = hl.z > -1e8f ? Clamp(hl.z - base.z, -0.3f, 0.3f) : 0.f;
        in.groundOffsetR = hr.z > -1e8f ? Clamp(hr.z - base.z, -0.3f, 0.3f) : 0.f;
        vec3 n = normalize(hl.normal + hr.normal + vec3(0, 0, 1e-3f));
        in.groundNormal = vec3(dot(vec2(n.x, n.y), rightV), dot(vec2(n.x, n.y), fwd), n.z);
        in.footProbes = true;
    }
    p.anim.update(in, dt, !p.isPlayer && p.visibleDist > 40.f);   // far peds: no IK / face work (LOD2 mesh)
    Anim::computeMatrices(ce.skel, p.anim.pose, p.bones, p.skin);
}

// ------------------------------------------------------------------------------------------------------------------
int GameWorld::freeSeat(int veh, bool driver) const {
    if (veh < 0 || !vehicles[veh].used) return -1;
    const Vehicle& v = vehicles[veh];
    int ns = Min((int)vassets[v.model].spec.seats.size(), 8);
    if (driver) return ns > 0 && v.seats[0] < 0 ? 0 : -1;
    for (int s = 1; s < ns; s++)
        if (v.seats[s] < 0) return s;
    return -1;
}

void GameWorld::warpPedIntoVehicle(int pid, int veh, int seat) {
    if (pid < 0 || veh < 0) return;
    Ped& p = peds[pid];
    Vehicle& v = vehicles[veh];
    if (p.vehicle >= 0) removePedFromVehicle(pid, false);
    if (v.seats[seat] >= 0 && v.seats[seat] != pid) removePedFromVehicle(v.seats[seat], false);
    v.seats[seat] = pid;
    p.vehicle = veh;
    p.seat = seat;
    p.state = PS_INVEHICLE;
    p.stateTime = 0.f;
    p.vel = vec3(0);
    p.aiming = false;
    p.firing = false;
    freeRagdoll(p.ragdoll);
    if (seat == 0) {
        v.sim.engineOn = true;
        v.parked = false;
        v.sim.sleeping = false;
    }
}

void GameWorld::removePedFromVehicle(int pid, bool exitAnim) {
    Ped& p = peds[pid];
    if (p.vehicle < 0) return;
    Vehicle& v = vehicles[p.vehicle];
    const Vehicles::VehicleModel& spec = vassets[v.model].spec;
    int seat = p.seat;
    if (seat >= 0 && seat < 8 && v.seats[seat] == pid) v.seats[seat] = -1;
    // place the ped beside its door
    vec3 sp = seat >= 0 && seat < (int)spec.seats.size() ? spec.seats[seat].pos : vec3(-1, 0, 0.5f);
    bool left = seat >= 0 && seat < (int)spec.seats.size() ? spec.seats[seat].exitLeft : true;
    float side = left ? -1.f : 1.f;
    vec3 local(side * (fabsf(spec.boxHalf.x) + 0.45f), sp.y, 0.f);
    if (isBike(p.vehicle)) local = vec3(side * 0.8f, sp.y, 0.f);
    if (isBoat(p.vehicle)) local = vec3(sp.x, sp.y, 0.3f);
    vec3 wp = rel(v.sim.body.pos, dvec3(0, 0, 0)) + rotate(v.sim.body.rot, local);
    float gz = groundHeight(wp.x, wp.y, wp.z + 1.0f);
    p.pos = dvec3(wp.x, wp.y, Max(gz, (float)v.sim.body.pos.z - 0.3f));
    vec3 f = v.sim.forward();
    p.yaw = atan2f(-f.x, f.y);
    p.vel = v.sim.body.vel;
    p.vel.z = 0;
    p.vehicle = -1;
    p.seat = -1;
    p.state = PS_ONFOOT;
    p.stateTime = 0.f;
    p.grounded = true;
    p.animIn.stance = 0;
    if (exitAnim) p.pendingAction = left ? Anim::CLIP_EXIT_CAR_L : Anim::CLIP_EXIT_CAR_R;
    if (seat == 0 && p.isPlayer) v.playerUsed = true;
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::updatePeds(float dt) {
    // camera distances for LOD
    dvec3 cam = rig.cam.pos;
    for (auto& p : peds)
        if (p.used) p.visibleDist = length(rel(p.pos, cam));
    for (int i = 0; i < (int)peds.size(); i++)
        if (peds[i].used) updatePed(i, dt);
    // ped-ped separation (on foot only)
    for (int i = 0; i < (int)peds.size(); i++) {
        Ped& a = peds[i];
        if (!a.used || (a.state != PS_ONFOOT && a.state != PS_SWIM)) continue;
        for (int j = i + 1; j < (int)peds.size(); j++) {
            Ped& b = peds[j];
            if (!b.used || (b.state != PS_ONFOOT && b.state != PS_SWIM)) continue;
            vec3 d = rel(b.pos, a.pos);
            if (fabsf(d.z) > 1.5f) continue;
            float d2 = d.x * d.x + d.y * d.y;
            float rr = kPedRadius * 2.f;
            if (d2 < rr * rr && d2 > 1e-8f) {
                float dist = sqrtf(d2);
                vec2 n = vec2(d.x, d.y) / dist;
                float push = (rr - dist) * 0.5f;
                float wa = a.isPlayer ? 0.25f : 0.5f, wb = b.isPlayer ? 0.25f : 0.5f;
                a.pos = a.pos - dvec3(n.x * push * wa * 2.f, n.y * push * wa * 2.f, 0);
                b.pos = b.pos + dvec3(n.x * push * wb * 2.f, n.y * push * wb * 2.f, 0);
            }
        }
    }
}

void GameWorld::updatePed(int id, float dt) {
    Ped& p = peds[id];
    p.stateTime += dt;
    // wounds keep bleeding a little (stains spread for a while)
    for (int w = 0; w < 4; w++)
        if (p.wounds[w].w > 0.f && p.woundAge[w] < 12.f) {
            p.woundAge[w] += dt;
            p.wounds[w].w = Min(p.wounds[w].w + dt * 0.006f, 0.16f);
        }
    p.fireTimer = Max(0.f, p.fireTimer - dt);
    p.meleeTimer = Max(0.f, p.meleeTimer - dt);
    p.hitReactTimer = Max(0.f, p.hitReactTimer - dt);
    p.speechCooldown = Max(0.f, p.speechCooldown - dt);
    p.spreadHeat = Max(0.f, p.spreadHeat - dt * 2.5f);
    updateMelee(id, dt);
    if (p.reloadTimer > 0.f) {
        p.reloadTimer -= dt;
        if (p.reloadTimer <= 0.f) {
            int need = clipCapacity(p, p.weapon) - p.clip[p.weapon];
            int avail = p.ammo[p.weapon] - p.clip[p.weapon];
            p.clip[p.weapon] += Min(need, avail);
        }
    }
    switch (p.state) {
        case PS_INVEHICLE: {
            if (p.vehicle < 0 || !vehicles[p.vehicle].used) {
                p.state = PS_ONFOOT;
                p.vehicle = -1;
                break;
            }
            Vehicle& v = vehicles[p.vehicle];
            const Vehicles::VehicleModel& spec = vassets[v.model].spec;
            // sinking vehicle: NPCs bail out and swim, the player runs out of air
            if (!isBoat(p.vehicle) && v.sim.submerged > 0.7f) {
                if (!p.isPlayer) {
                    int vid = p.vehicle;
                    removePedFromVehicle(id, false);
                    p.pos.z = Max(p.pos.z, v.sim.body.pos.z + 0.5);
                    (void)vid;
                    break;
                }
                pinfo.breath = Max(0.f, pinfo.breath - dt / 20.f);
                if (pinfo.breath <= 0.f) damagePed(id, 12.f * dt, DMG_DROWN, -1, vec3(0, 0, 1));
                if (hudHelpTimer <= 0.f) help("The vehicle is sinking! Press ~i:F|Y~ to get out.", 2.f);
            } else if (p.isPlayer) {
                pinfo.breath = Min(1.f, pinfo.breath + dt * 0.3f);
            }
            vec3 sp = p.seat < (int)spec.seats.size() ? spec.seats[p.seat].pos : vec3(0, 0, 0.5f);
            // seat position is the hip; the ped origin is at the feet (~0.45 m below a seated hip)
            vec3 wp = rotate(v.sim.body.rot, sp - vec3(0, 0, 0.5f));
            p.pos = v.sim.body.pos + wp;
            vec3 f = v.sim.forward();
            p.yaw = atan2f(-f.x, f.y);
            p.vel = v.sim.body.vel;
            p.grounded = true;
            break;
        }
        case PS_RAGDOLL:
        case PS_DEAD:
            if (p.ragdoll) GameWorld_updateRagdoll(*this, p, dt);
            break;
        default: break;
    }
    if (!p.isPlayer && p.state != PS_INVEHICLE && p.state != PS_RAGDOLL && p.state != PS_DEAD && p.state != PS_GETUP &&
        p.brain.type == BRAIN_NONE) {
        // idle standing (scripted peds without a brain)
        movePed(p, vec2(0, 0), dt, false);
    }
    if (p.state == PS_GETUP && p.stateTime > 1.4f) {
        p.state = PS_ONFOOT;
        p.stateTime = 0.f;
    }
    // animation update rate LOD
    if (p.ragdoll) return;
    bool doAnim = p.visibleDist < 45.f || ((p.uid + (u32)(time * 60.0)) % (p.visibleDist < 120.f ? 2u : 4u)) == 0;
    if (doAnim) animatePed(p, p.visibleDist < 45.f ? dt : dt * (p.visibleDist < 120.f ? 2.f : 4.f));
    // footsteps (nearby): each foot contact goes to the mixer with the surface, footwear, gait, body weight and wet
    // ground; it builds the layered step (Audio::playFootstep)
    if (p.state == PS_ONFOOT && p.grounded && (p.isPlayer || p.visibleDist < 30.f)) {
        float spd = length(vec2(p.vel.x, p.vel.y));
        // a step sounds when an animated foot comes down (the animator's contacts: the gait's heel strikes, the steps
        // of a turn on the spot or of settling after a stop), under that foot
        u32 ev = doAnim ? p.anim.footEvents : 0u;
        for (int s = 0; s < 2; s++) {
            if (!(ev & (1u << s))) continue;
#ifdef HAVE_AUDIO
            vec3 fm = p.anim.footProbe(s);
            vec3 fpos = p.pos.toVec3() + rotate(yawQuat(p.yaw), vec3(fm.x, fm.y, 0.f));
            Phys::GroundHit g = Phys::gCollision->ground(fpos.x, fpos.y, (float)p.pos.z + 0.2f);
            Audio::Footstep f;
            f.pos = fpos;
            f.speed = Max(spd, 0.4f);
            f.surface = footSurfaceOf(g);
            f.footwear = footwearOf(*this, p);
            f.weight = bodyWeightOf(*this, p);
            f.wetness = env ? env->wetness : 0.f;
            f.player = p.isPlayer;
            Audio::playFootstep(f);
#endif
        }
    }
}

}  // namespace Game
