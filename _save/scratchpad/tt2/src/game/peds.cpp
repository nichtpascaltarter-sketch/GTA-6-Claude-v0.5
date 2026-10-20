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
        p.moveMode = 0;
        p.pos = dvec3(p.traverseTo);
        p.groundZ = p.traverseTo.z;
        p.airTime = 0.f;
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
    } else if (p.state == PS_ONFOOT || p.state == PS_SWIM) {
        if (in.stance == 1 || in.stance == 2 || in.stance == 3) in.stance = 0;
    }
    in.action = -1;
    if (p.pendingAction >= 0) {
        in.action = p.pendingAction;
        p.pendingAction = -1;
    }
    // foot IK: probe ground under both feet (only for nearby peds)
    in.groundOffsetL = in.groundOffsetR = 0.f;
    if (p.visibleDist < 30.f && p.grounded && p.state == PS_ONFOOT) {
        quat q = yawQuat(p.yaw);
        vec3 base = p.pos.toVec3();
        vec3 fl = base + rotate(q, vec3(-0.11f, 0, 0)), fr = base + rotate(q, vec3(0.11f, 0, 0));
        float gl = groundHeight(fl.x, fl.y, base.z + 0.3f), gr = groundHeight(fr.x, fr.y, base.z + 0.3f);
        in.groundOffsetL = Clamp(gl - base.z, -0.3f, 0.3f);
        in.groundOffsetR = Clamp(gr - base.z, -0.3f, 0.3f);
    }
    p.anim.update(in, dt);
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
    if (p.reloadTimer > 0.f) {
        p.reloadTimer -= dt;
        if (p.reloadTimer <= 0.f) {
            const WeaponInfo& wi = weaponInfo(p.weapon);
            int need = wi.clipSize - p.clip[p.weapon];
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
    // footsteps (nearby)
    if (p.state == PS_ONFOOT && p.grounded && p.visibleDist < 25.f) {
        float spd = length(vec2(p.vel.x, p.vel.y));
        if (spd > 0.6f) {
            float stride = spd < 2.f ? 0.75f : (spd < 5.f ? 1.2f : 1.7f);
            p.stepPhase += spd * dt / stride;
            if (p.stepPhase >= 1.f) {
                p.stepPhase -= 1.f;
#ifdef HAVE_AUDIO
                Phys::GroundHit g = Phys::gCollision->ground((float)p.pos.x, (float)p.pos.y, (float)p.pos.z + 0.2f);
                Audio::Sfx s = Audio::SFX_STEP_CONCRETE;
                switch (g.surface) {
                    case Phys::SURF_GRASS: s = Audio::SFX_STEP_GRASS; break;
                    case Phys::SURF_DIRT: case Phys::SURF_MUD: s = Audio::SFX_STEP_GRAVEL; break;
                    case Phys::SURF_SAND: s = Audio::SFX_STEP_SAND; break;
                    case Phys::SURF_WOOD: s = Audio::SFX_STEP_WOOD; break;
                    case Phys::SURF_METAL: s = Audio::SFX_STEP_METAL; break;
                    default: break;
                }
                if (g.water) s = Audio::SFX_STEP_WATER;
                Audio::play(s, p.pos.toVec3(), (p.isPlayer ? 0.55f : 0.35f) * Saturate(spd / 3.f + 0.4f));
#endif
            }
        }
    }
}

}  // namespace Game
