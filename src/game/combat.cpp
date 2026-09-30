// Weapons, damage, hit detection (peds as bone capsules, vehicles as oriented boxes, static world), explosions,
// thrown projectiles, fires and pickups.
#include "gameworld.h"

namespace Game {

namespace combat_detail {

// name, hudIcon, animKind, slot, damage, range, interval, clip, reload, spread, pellets, auto, sfx, price, ammoPrice, recoil
const WeaponInfo kWeapons[WPN_COUNT] = {
    {"Fists", 0, 0, 0, 12.f, 1.4f, 0.45f, 0, 0.f, 0.f, 1, false, Audio::SFX_PUNCH, 0, 0, 0.f},
    {"Switchblade", 1, 3, 1, 34.f, 1.6f, 0.55f, 0, 0.f, 0.f, 1, false, Audio::SFX_PUNCH, 400, 0, 0.f},
    {"Baseball Bat", 2, 3, 1, 42.f, 1.9f, 0.8f, 0, 0.f, 0.f, 1, false, Audio::SFX_PUNCH, 250, 0, 0.f},
    {"Vesper 9mm", 3, 1, 2, 26.f, 70.f, 0.2f, 15, 1.35f, 0.035f, 1, false, Audio::SFX_PISTOL, 1200, 60, 0.018f},
    {"Grand Duke .44", 4, 1, 2, 64.f, 80.f, 0.55f, 6, 2.1f, 0.025f, 1, false, Audio::SFX_PISTOL, 2800, 90, 0.05f},
    {"Kestrel SMG", 5, 2, 3, 19.f, 55.f, 0.075f, 30, 1.8f, 0.055f, 1, true, Audio::SFX_SMG, 4200, 120, 0.012f},
    {"Marauder Rifle", 6, 2, 4, 30.f, 150.f, 0.105f, 30, 2.1f, 0.03f, 1, true, Audio::SFX_RIFLE, 7800, 180, 0.016f},
    {"Tidebreaker 12ga", 7, 2, 5, 16.f, 32.f, 0.85f, 8, 2.8f, 0.09f, 8, false, Audio::SFX_SHOTGUN, 3900, 110, 0.06f},
    {"Longreach .308", 8, 2, 6, 140.f, 450.f, 1.25f, 5, 2.8f, 0.004f, 1, false, Audio::SFX_SNIPER, 11500, 260, 0.09f},
    {"Harpoon RPG", 9, 2, 7, 500.f, 300.f, 1.6f, 1, 3.0f, 0.01f, 1, false, Audio::SFX_ROCKET_LAUNCH, 26000, 900, 0.08f},
    {"Grenade", 10, 4, 7, 300.f, 30.f, 1.0f, 1, 0.f, 0.f, 1, false, Audio::SFX_NONE, 350, 350, 0.f},
    {"Molotov", 11, 4, 7, 60.f, 25.f, 1.0f, 1, 0.f, 0.f, 1, false, Audio::SFX_NONE, 200, 200, 0.f},
};

// Bone capsules used for hit detection: (bone a, bone b, radius, damage multiplier)
struct HitCapsule {
    int a, b;
    float r, mult;
};
const HitCapsule kHitCapsules[] = {
    {Anim::B_HEAD, -1, 0.12f, 4.f},
    {Anim::B_NECK, Anim::B_HEAD, 0.07f, 2.5f},
    {Anim::B_PELVIS, Anim::B_CHEST, 0.17f, 1.f},
    {Anim::B_CHEST, Anim::B_NECK, 0.16f, 1.2f},
    {Anim::B_UPPERARM_L, Anim::B_FOREARM_L, 0.06f, 0.6f},
    {Anim::B_FOREARM_L, Anim::B_HAND_L, 0.05f, 0.5f},
    {Anim::B_UPPERARM_R, Anim::B_FOREARM_R, 0.06f, 0.6f},
    {Anim::B_FOREARM_R, Anim::B_HAND_R, 0.05f, 0.5f},
    {Anim::B_THIGH_L, Anim::B_CALF_L, 0.085f, 0.7f},
    {Anim::B_CALF_L, Anim::B_FOOT_L, 0.06f, 0.6f},
    {Anim::B_THIGH_R, Anim::B_CALF_R, 0.085f, 0.7f},
    {Anim::B_CALF_R, Anim::B_FOOT_R, 0.06f, 0.6f},
};

bool rayCapsule(vec3 o, vec3 d, vec3 a, vec3 b, float r, float& t) {
    vec3 ba = b - a, oa = o - a;
    float baba = dot(ba, ba), bard = dot(ba, d), baoa = dot(ba, oa), rdoa = dot(d, oa), oaoa = dot(oa, oa);
    float A = baba - bard * bard;
    float B = baba * rdoa - baoa * bard;
    float C = baba * oaoa - baoa * baoa - r * r * baba;
    float h = B * B - A * C;
    if (A > 1e-8f && h >= 0.f) {
        float tt = (-B - sqrtf(h)) / A;
        float y = baoa + tt * bard;
        if (y > 0.f && y < baba && tt > 0.f) {
            t = tt;
            return true;
        }
        vec3 oc = y <= 0.f ? oa : o - b;
        B = dot(d, oc);
        C = dot(oc, oc) - r * r;
        h = B * B - C;
        if (h > 0.f) {
            tt = -B - sqrtf(h);
            if (tt > 0.f) {
                t = tt;
                return true;
            }
        }
        return false;
    }
    // degenerate (sphere)
    vec3 oc = oa;
    B = dot(d, oc);
    C = dot(oc, oc) - r * r;
    h = B * B - C;
    if (h < 0.f) return false;
    float tt = -B - sqrtf(h);
    if (tt <= 0.f) return false;
    t = tt;
    return true;
}

bool rayObb(vec3 o, vec3 d, vec3 c, const mat3& R, vec3 he, float& t, vec3& nrm) {
    mat3 Rt = transpose(R);
    vec3 lo = Rt * (o - c), ld = Rt * d;
    float tn = -1e30f, tf = 1e30f;
    int axis = 0;
    float sign = 1.f;
    for (int i = 0; i < 3; i++) {
        float oi = i == 0 ? lo.x : (i == 1 ? lo.y : lo.z), di = i == 0 ? ld.x : (i == 1 ? ld.y : ld.z);
        float hi = i == 0 ? he.x : (i == 1 ? he.y : he.z);
        if (fabsf(di) < 1e-9f) {
            if (oi < -hi || oi > hi) return false;
            continue;
        }
        float t1 = (-hi - oi) / di, t2 = (hi - oi) / di;
        float s = -1.f;
        if (t1 > t2) {
            std::swap(t1, t2);
            s = 1.f;
        }
        if (t1 > tn) {
            tn = t1;
            axis = i;
            sign = s;
        }
        tf = Min(tf, t2);
        if (tn > tf) return false;
    }
    if (tf < 0.f) return false;
    t = tn > 0.f ? tn : 0.f;
    vec3 ln(0, 0, 0);
    if (axis == 0) ln.x = sign;
    else if (axis == 1) ln.y = sign;
    else ln.z = sign;
    nrm = R * ln;
    return true;
}

}  // namespace combat_detail

using namespace combat_detail;

const WeaponInfo& weaponInfo(WeaponType w) { return kWeapons[Clamp((int)w, 0, (int)WPN_COUNT - 1)]; }

// ------------------------------------------------------------------------------------------------------------------
bool GameWorld::raycast(dvec3 from, vec3 dir, float maxDist, WorldHit& hit, int ignorePed, int ignoreVeh, bool withPeds,
                        bool withVehicles) const {
    hit = WorldHit();
    hit.t = maxDist;
    bool any = false;
    Phys::RayHit rh;
    vec3 o = from.toVec3();
    if (Phys::gCollision->raycast(o, dir, maxDist, rh, true)) {
        hit.t = rh.t;
        hit.normal = rh.normal;
        hit.collider = rh.collider;
        hit.surface = rh.surface;
        any = true;
    }
    // water surface stops bullets (splash)
    {
        float wz;
        vec3 e = o + dir * hit.t;
        if (dir.z < 0.f && Phys::waterSurface(e.x, e.y, wz) && e.z < wz && o.z > wz) {
            float t = (o.z - wz) / -dir.z;
            if (t < hit.t) {
                hit.t = t;
                hit.normal = vec3(0, 0, 1);
                hit.surface = Phys::SURF_WATER;
                hit.collider = -1;
                any = true;
            }
        }
    }
    if (withVehicles) {
        for (int i = 0; i < (int)vehicles.size(); i++) {
            const Vehicle& v = vehicles[i];
            if (!v.used || i == ignoreVeh) continue;
            const Vehicles::VehicleModel& spec = vassets[v.model].spec;
            mat3 R = v.sim.body.rotMat();
            vec3 c = v.sim.body.pos.toVec3() + R * spec.boxCenter;
            vec3 oc = c - o;
            float along = dot(oc, dir);
            float rad = length(spec.boxHalf);
            if (along < -rad || along > hit.t + rad) continue;
            if (length2(oc - dir * along) > rad * rad) continue;
            float t;
            vec3 n;
            if (rayObb(o, dir, c, R, spec.boxHalf * vec3(0.95f, 0.97f, 0.9f), t, n) && t < hit.t) {
                hit.t = t;
                hit.normal = n;
                hit.vehicle = i;
                hit.ped = -1;
                hit.collider = -1;
                hit.surface = Phys::SURF_METAL;
                any = true;
            }
        }
    }
    if (withPeds) {
        for (int i = 0; i < (int)peds.size(); i++) {
            const Ped& p = peds[i];
            if (!p.used || i == ignorePed) continue;
            if (p.state == PS_INVEHICLE && p.vehicle >= 0 && !isBike(p.vehicle) && p.vehicle == hit.vehicle) {
                // occupants of the vehicle we hit: hit them if the ray passes through the cabin near their seat
            }
            vec3 pp = p.pos.toVec3() + vec3(0, 0, 0.9f);
            vec3 oc = pp - o;
            float along = dot(oc, dir);
            if (along < -1.2f || along > hit.t + 1.2f) continue;
            if (length2(oc - dir * along) > 1.44f) continue;
            // bone capsules
            quat q = quatAxisAngle(vec3(0, 0, 1), p.yaw);
            vec3 base = p.pos.toVec3();
            auto boneWorld = [&](int b) -> vec3 {
                vec3 l = p.bones[b].c[3].xyz();
                return p.ragdoll ? l : base + rotate(q, l);
            };
            for (const HitCapsule& hc : kHitCapsules) {
                vec3 a = boneWorld(hc.a);
                vec3 b = hc.b >= 0 ? boneWorld(hc.b) : a + vec3(0, 0, 0.1f);
                float t;
                if (rayCapsule(o, dir, a, b, hc.r, t) && t < hit.t) {
                    hit.t = t;
                    hit.ped = i;
                    hit.vehicle = -1;
                    hit.bone = hc.a;
                    hit.collider = -1;
                    hit.normal = -dir;
                    hit.surface = 255;
                    any = true;
                }
            }
        }
    }
    if (any) hit.pos = from + dir * hit.t;
    return any;
}

bool GameWorld::lineOfSight(dvec3 a, dvec3 b, int ignorePed, int ignoreVeh) const {
    vec3 d = rel(b, a);
    float len = length(d);
    if (len < 0.01f) return true;
    WorldHit h;
    if (!raycast(a, d / len, len - 0.3f, h, ignorePed, ignoreVeh, false, true)) return true;
    return false;
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::giveWeapon(int pid, WeaponType w, int ammo) {
    if (pid < 0) return;
    Ped& p = peds[pid];
    p.hasWeapon[w] = true;
    const WeaponInfo& wi = weaponInfo(w);
    if (wi.clipSize > 0) {
        p.ammo[w] = Min(p.ammo[w] + ammo, 9999);
        if (p.clip[w] == 0) p.clip[w] = Min(wi.clipSize, p.ammo[w]);
    }
}

void GameWorld::fireWeapon(int pid, dvec3 muzzle, vec3 dir) {
    Ped& p = peds[pid];
    const WeaponInfo& wi = weaponInfo(p.weapon);
    if (p.fireTimer > 0.f || p.reloadTimer > 0.f) return;
    if (wi.clipSize > 0 && p.clip[p.weapon] <= 0) {
        if (p.ammo[p.weapon] > 0) p.reloadTimer = wi.reloadTime;
#ifdef HAVE_AUDIO
        else Audio::play(Audio::SFX_DRY_FIRE, muzzle.toVec3(), 0.6f);
#endif
        p.fireTimer = 0.3f;
        return;
    }
    // NPCs react slower than the player between shots (less for accurate professionals)
    p.fireTimer = wi.fireInterval * (p.isPlayer ? 1.f : 1.7f + (1.f - p.brain.accuracy) * 1.3f);
    p.firing = true;
    if (p.weapon == WPN_GRENADE || p.weapon == WPN_MOLOTOV || p.weapon == WPN_RPG) {
        // projectile weapons
        Projectile pr;
        pr.used = true;
        pr.owner = pid;
        pr.pos = muzzle;
        if (p.weapon == WPN_RPG) {
            pr.type = PROJ_ROCKET;
            pr.vel = dir * 55.f;
            pr.fuse = 6.f;
#ifdef HAVE_AUDIO
            Audio::play(Audio::SFX_ROCKET_LAUNCH, muzzle.toVec3());
#endif
            spawnFx(FX_SMOKE, muzzle - dir * 0.8f, -dir * 2.f, 6, 1.f);
        } else {
            pr.type = p.weapon == WPN_GRENADE ? PROJ_GRENADE : PROJ_MOLOTOV;
            pr.vel = normalize(dir + vec3(0, 0, 0.28f)) * 17.f + p.vel * 0.5f;
            pr.fuse = p.weapon == WPN_GRENADE ? 3.f : 10.f;
            p.pendingAction = Anim::CLIP_THROW;
        }
        bool placed = false;
        for (auto& q : projectiles)
            if (!q.used) {
                q = pr;
                placed = true;
                break;
            }
        if (!placed) projectiles.push_back(pr);
        p.clip[p.weapon]--;
        p.ammo[p.weapon]--;
        if (p.ammo[p.weapon] <= 0 && p.weapon != WPN_RPG) {
            p.hasWeapon[p.weapon] = false;
            p.weapon = WPN_FISTS;
        }
        if (p.isPlayer) pinfo.shotsFired++;
        if (p.isPlayer && time - p.lastGunfireReport > 4.0) {
            p.lastGunfireReport = time;
            reportCrime(1, p.pos, -1);
            socialReport(UI::TE_SHOOTING, p.pos);
        }
        return;
    }
    if (wi.clipSize == 0) {
        // melee: timed attack (melee.cpp); NPCs throw an occasional heavy blow
        meleeStart(pid, !p.isPlayer && hashToFloat(hash32(p.uid * 7u + p.meleeSerial * 13u)) < 0.18f);
        return;
    }
    // hitscan firearms
    p.clip[p.weapon]--;
    p.ammo[p.weapon]--;
    if (p.isPlayer) pinfo.shotsFired++;
    float spread = wi.spread * (p.aiming ? 0.45f : 1.f) * (1.f + p.spreadHeat) * (p.isPlayer ? 1.f : 1.6f - p.brain.accuracy);
    if (p.state == PS_INVEHICLE) spread *= 1.6f;
    p.spreadHeat = Min(p.spreadHeat + (wi.automatic ? 0.25f : 0.6f), 2.5f);
    vec3 up = fabsf(dir.z) < 0.95f ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 rx = normalize(cross(dir, up)), ry = cross(rx, dir);
#ifdef HAVE_AUDIO
    Audio::play((Audio::Sfx)wi.sfx, muzzle.toVec3(), 1.f, 0.96f + (hash32(p.uid + (u32)(time * 1000)) % 8) * 0.01f);
#endif
    spawnFx(FX_MUZZLE_FLASH, muzzle, dir, 1, p.weapon == WPN_SHOTGUN ? 1.5f : 1.f);
    spawnLight(muzzle + dir * 0.3f, vec3(1.f, 0.7f, 0.35f) * 2500.f, 6.f);
    bool hitSomeone = false;
    for (int k = 0; k < wi.pellets; k++) {
        u32 hs = hash32(p.uid * 977u + (u32)(time * 4000.0) + k * 131u);
        float a = hashToFloat(hs) * kTwoPi, r = sqrtf(hashToFloat(hash32(hs))) * spread;
        vec3 d = normalize(dir + rx * (cosf(a) * r) + ry * (sinf(a) * r));
        if (!p.isPlayer && player >= 0 && p.brain.target == player && peds[player].used) {
            // GTA-style hit probability: accuracy, range, a moving or covered target and Focus all reduce it;
            // a missed shot is steered past the target so it still whizzes by
            const Ped& tgt = peds[player];
            vec3 tp = pedChestPos(tgt);
            float dist = length(tp - muzzle.toVec3());
            float spdT = tgt.vehicle >= 0 ? vehicles[tgt.vehicle].sim.speed() : length(vec2(tgt.vel.x, tgt.vel.y));
            float chance = (0.25f + 0.55f * p.brain.accuracy) * (1.15f - Saturate(dist / 70.f)) * (spdT > 2.f ? 0.55f : 1.f) *
                           (tgt.moveMode == 1 ? (tgt.animIn.crouch ? 0.2f : 0.4f) : 1.f) * (pinfo.focusActive ? 0.6f : 1.f);
            if (hashToFloat(hash32(hs ^ 0x5bd1e995u)) > chance) {
                vec3 sideV = normalize(cross(d, vec3(0, 0, 1)));
                float missBy = 0.6f + hashToFloat(hash32(hs + 77u)) * 1.4f;
                vec3 missPoint = tp + sideV * (((hs >> 3) & 1) ? missBy : -missBy) + vec3(0, 0, hashToFloat(hs >> 5) * 0.8f - 0.2f);
                d = normalize(missPoint - muzzle.toVec3());
            }
        }
        WorldHit h;
        int ignoreVeh = p.vehicle;
        if (raycast(muzzle, d, wi.range, h, pid, ignoreVeh)) {
            if (k == 0 || wi.pellets <= 2 || (k & 1)) spawnTracer(muzzle, h.pos);
            if (h.ped >= 0) {
                float dmg = wi.damage;
                float mult = 1.f;
                for (const HitCapsule& hc : kHitCapsules)
                    if (hc.a == h.bone) mult = hc.mult;
                float falloff = Saturate(1.2f - h.t / wi.range);
                addWound(h.ped, h.pos, h.bone, h.bone == Anim::B_HEAD ? 0.045f : 0.06f);
                int victim = h.ped;
                damagePed(victim, dmg * mult * Max(falloff, 0.35f), DMG_BULLET, pid, d, h.bone);
                spawnFx(FX_BLOOD, h.pos, -d, 3, 1.f);
                if (peds[victim].used && peds[victim].health > 0.f && peds[victim].state == PS_ONFOOT && !peds[victim].ragdoll) {
                    Ped& vp = peds[victim];
                    // a point-blank shotgun blast or a rifle round knocks the target off its feet
                    bool bigHit = (p.weapon == WPN_SHOTGUN && h.t < 9.f) || p.weapon == WPN_SNIPER || (p.weapon == WPN_RIFLE && h.t < 4.f);
                    if (bigHit && !vp.isPlayer && hashToFloat(hash32(vp.uid + (u32)(time * 977.0))) < (p.weapon == WPN_SHOTGUN ? 0.75f : 0.5f))
                        knockDown(victim, d * (p.weapon == WPN_SHOTGUN ? 420.f : 300.f) + vec3(0.f, 0.f, 60.f));
                    // leg wounds make the target limp for a while
                    else if (h.bone == Anim::B_THIGH_L || h.bone == Anim::B_THIGH_R || h.bone == Anim::B_CALF_L || h.bone == Anim::B_CALF_R)
                        vp.legInjury = Max(vp.legInjury, vp.isPlayer ? 8.f : 25.f);
                }
#ifdef HAVE_AUDIO
                Audio::play(Audio::SFX_IMPACT_FLESH, h.pos.toVec3(), 0.8f);
#endif
                if (p.isPlayer) {
                    hitSomeone = true;
                    if (h.bone == Anim::B_HEAD && !peds[h.ped].used) pinfo.headshots++;
                }
                // blood decal on the ground/wall behind
                WorldHit h2;
                if (raycast(h.pos, d, 3.f, h2, h.ped, -1, false, false)) spawnDecal(DECAL_BLOOD, h2.pos, h2.normal, 0.5f);
            } else if (h.vehicle >= 0) {
                vec3 pr = rel(h.pos, vehicles[h.vehicle].sim.body.pos);
                damageVehicle(h.vehicle, wi.damage * 0.35f, pid, pr, d * wi.damage * 3.f);
                // shot-out tires
                {
                    Vehicle& hv = vehicles[h.vehicle];
                    const Vehicles::VehicleModel& hs = vassets[hv.model].spec;
                    vec3 local = transpose(hv.sim.body.rotMat()) * pr;
                    for (int w = 0; w < hv.sim.wheelCount && w < (int)hs.wheels.size(); w++) {
                        vec3 wc = hs.wheels[w].pos;
                        if (fabsf(local.x - wc.x) < hs.wheels[w].width * 0.8f + 0.05f && length(vec2(local.y - wc.y, local.z - wc.z)) < hs.wheels[w].radius * 1.05f &&
                            !hv.sim.wheels[w].burst) {
                            hv.sim.wheels[w].burst = true;
                            hv.sim.sleeping = false;
#ifdef HAVE_AUDIO
                            Audio::play(Audio::SFX_TIRE_POP, h.pos.toVec3(), 1.f);
#endif
                            spawnFx(FX_DUST, h.pos, vec3(0, 0, 1.f), 4, 0.4f);
                            break;
                        }
                    }
                }
                // windows: hits above the belt line crack, then shatter the glass
                {
                    Vehicle& gv = vehicles[h.vehicle];
                    const VehicleAsset& ga = vassets[gv.model];
                    vec3 local = transpose(gv.sim.body.rotMat()) * pr;
                    float belt = ga.spec.boxCenter.z + ga.spec.boxHalf.z * 0.15f, roof = ga.spec.boxCenter.z + ga.spec.boxHalf.z * 0.92f;
                    if (!gv.windowsBroken && ga.body && ga.body->glassCount > 0 && local.z > belt && local.z < roof) {
                        spawnFx(FX_GLASS, h.pos, -d, 3, 0.5f);
                        if (++gv.glassHits >= 2) breakVehicleWindows(h.vehicle, d);
                    }
                }
                spawnFx(FX_SPARKS, h.pos, h.normal, 4, 0.6f);
                spawnDecal(DECAL_BULLET_METAL, h.pos, h.normal, 0.06f);
#ifdef HAVE_AUDIO
                Audio::play(Audio::SFX_IMPACT_METAL, h.pos.toVec3(), 0.7f);
#endif
                // occupants can be hit through the glass
                const Vehicle& v = vehicles[h.vehicle];
                for (int s = 0; s < 8; s++) {
                    int occ = v.seats[s];
                    if (occ < 0 || occ == pid) continue;
                    vec3 head = pedHeadPos(peds[occ]) - vec3(0, 0, 0.25f);
                    vec3 oc = head - muzzle.toVec3();
                    float along = dot(oc, d);
                    if (along > 0 && length(oc - d * along) < 0.35f) {
                        damagePed(occ, wi.damage * 0.8f, DMG_BULLET, pid, d, Anim::B_CHEST);
                        if (p.isPlayer) hitSomeone = true;
                        break;
                    }
                }
            } else {
                u8 surf = h.surface;
                if (surf == Phys::SURF_WATER) {
                    spawnFx(FX_WATER_SPLASH, h.pos, vec3(0, 0, 1), 3, 0.5f);
#ifdef HAVE_AUDIO
                    Audio::play(Audio::SFX_IMPACT_WATER, h.pos.toVec3(), 0.6f);
#endif
                } else {
                    bool dirt = surf == Phys::SURF_GRASS || surf == Phys::SURF_DIRT || surf == Phys::SURF_SAND || surf == Phys::SURF_MUD;
                    spawnFx(dirt ? FX_DUST : FX_SPARKS, h.pos, h.normal, dirt ? 3 : 4, 0.5f);
                    spawnDecal(surf == Phys::SURF_METAL ? DECAL_BULLET_METAL : DECAL_BULLET_CONCRETE, h.pos, h.normal, 0.07f);
#ifdef HAVE_AUDIO
                    Audio::Sfx s = dirt ? Audio::SFX_IMPACT_DIRT : (surf == Phys::SURF_METAL ? Audio::SFX_IMPACT_METAL : (surf == Phys::SURF_WOOD ? Audio::SFX_IMPACT_WOOD : Audio::SFX_IMPACT_CONCRETE));
                    Audio::play(s, h.pos.toVec3(), 0.55f);
#endif
                }
            }
        } else {
            spawnTracer(muzzle, muzzle + d * wi.range);
        }
        // bullets whizzing past the player
        if (!p.isPlayer && player >= 0) {
            vec3 pp = pedHeadPos(peds[player]);
            vec3 oc = pp - muzzle.toVec3();
            float along = dot(oc, d);
            if (along > 2.f && along < h.t && length(oc - d * along) < 1.6f) {
#ifdef HAVE_AUDIO
                Audio::play(Audio::SFX_BULLET_WHIZ, muzzle.toVec3() + d * along, 0.7f);
#endif
            }
        }
    }
    if (p.isPlayer) {
        rumble(wi.recoil * 6.f + 0.1f, wi.recoil * 10.f + 0.15f);
        rig.recoil += wi.recoil;
        if (hitSomeone) {
            pinfo.shotsHit++;
            pinfo.hitMarker = 1.f;
        }
    }
    // gunfire is reported at most once every few seconds per shooter (a burst is one incident)
    if (time - p.lastGunfireReport > 4.0) {
        p.lastGunfireReport = time;
        if (p.isPlayer) {
            reportCrime(1, p.pos, -1);
            socialReport(UI::TE_SHOOTING, p.pos);
        }
    }
    if (p.clip[p.weapon] <= 0 && p.ammo[p.weapon] > 0) p.reloadTimer = wi.reloadTime;
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::damagePed(int pid, float amount, DamageType type, int attacker, vec3 dir, int bone) {
    if (pid < 0 || !peds[pid].used) return;
    Ped& p = peds[pid];
    if (p.health <= 0.f || p.invincible) return;
    if (p.isPlayer && attacker >= 0 && attacker < (int)peds.size() && peds[attacker].faction == FAC_FRIEND) return;
    float a = amount;
    if (p.isPlayer && attacker >= 0 && attacker != pid && type != DMG_EXPLOSION) a *= 0.5f;   // protagonists are tougher
    if (p.armor > 0.f && type != DMG_FALL && type != DMG_DROWN) {
        float absorbed = Min(p.armor, a * 0.7f);
        p.armor -= absorbed;
        a -= absorbed;
    }
    p.health -= a;
    p.lastAttacker = attacker;
    p.lastDamageTime = (float)time;
    if (p.isPlayer) {
        rumble(Saturate(a / 40.f), Saturate(a / 25.f));
        fxDamage = Min(1.f, fxDamage + a / 40.f);
        if (a > 30.f) fxChroma = Min(1.f, fxChroma + 0.5f);
    }
    if (p.isPlayer && attacker >= 0 && attacker < (int)peds.size()) {
        vec3 d = rel(peds[attacker].pos, p.pos);
        float ang = atan2f(-d.x, d.y) - rig.yaw;
        pinfo.damageDirs.push_back(ang);
        pinfo.damageDirTimes.push_back(0.f);
        rig.shake = Max(rig.shake, 0.25f);
    }
    if (p.health <= 0.f) {
        killPed(pid, attacker, dir, type);
        return;
    }
    // reactions
    if (p.state == PS_ONFOOT && type != DMG_FALL && p.hitReactTimer <= 0.f) {
        vec3 f(-sinf(p.yaw), cosf(p.yaw), 0);
        p.pendingAction = dot(f, dir) < 0.f ? Anim::CLIP_HIT_FRONT : Anim::CLIP_HIT_BACK;
        p.hitReactTimer = 0.6f;
    }
#ifdef HAVE_AUDIO
    if (p.speechCooldown <= 0.f && type != DMG_FALL) {
        Audio::play(p.female ? Audio::SFX_GRUNT_FEMALE : Audio::SFX_GRUNT_MALE, pedHeadPos(p), 0.8f);
        p.speechCooldown = 0.8f;
    }
#endif
    if (!p.isPlayer) {
        p.brain.alerted = true;
        if (attacker >= 0) p.brain.target = attacker;
    }
    if (attacker >= 0 && attacker < (int)peds.size() && peds[attacker].isPlayer) {
        bool selfDefense = p.faction != FAC_POLICE && p.weapon != WPN_FISTS && p.brain.type == BRAIN_COMBAT && p.brain.target == attacker;
        // one assault report per victim every few seconds (a beating is one incident)
        if (!selfDefense && time - p.lastCrimeReport > 5.0) {
            p.lastCrimeReport = time;
            reportCrime(p.faction == FAC_POLICE ? 5 : 0, p.pos, pid);
        }
    }
}

// Shatters every see-through window of a vehicle (glass burst, sound); the glass pass stops drawing them.
void GameWorld::breakVehicleWindows(int vi, vec3 dir) {
    if (vi < 0 || vi >= (int)vehicles.size() || !vehicles[vi].used) return;
    Vehicle& v = vehicles[vi];
    if (v.windowsBroken) return;
    const VehicleAsset& a = vassets[v.model];
    if (!a.body || a.body->glassCount == 0) return;
    v.windowsBroken = true;
    mat3 R = v.sim.body.rotMat();
    vec3 c = v.sim.body.pos.toVec3() + R * (a.spec.boxCenter + vec3(0.f, 0.f, a.spec.boxHalf.z * 0.5f));
    for (int k = -1; k <= 1; k++)
        spawnFx(FX_GLASS, dvec3(c + R * vec3(0.f, a.spec.boxHalf.y * 0.45f * (float)k, 0.f)), normalize(dir + vec3(0.f, 0.f, 0.4f)), 10, 1.f);
#ifdef HAVE_AUDIO
    Audio::play(Audio::SFX_GLASS_BREAK, c, 1.f);
#endif
}

// Records a wound in bind-pose space so the blood stain follows the animated/ragdolled body.
void GameWorld::addWound(int pid, dvec3 worldPos, int bone, float radius) {
    if (pid < 0 || !peds[pid].used) return;
    Ped& p = peds[pid];
    if (bone < 0 || bone >= Anim::B_COUNT) bone = Anim::B_CHEST;
    vec3 local = rel(worldPos, p.pos);
    if (!p.ragdoll) local = rotate(conj(quatAxisAngle(vec3(0, 0, 1), p.yaw)), local);
    mat4 inv = inverse(p.skin[bone]);
    vec3 bind = transformPoint(inv, local);
    if (!std::isfinite(bind.x) || !std::isfinite(bind.y) || !std::isfinite(bind.z)) return;
    int slot = p.woundNext;
    p.woundNext = (p.woundNext + 1) & 3;
    p.wounds[slot] = vec4(bind, radius);
    p.woundAge[slot] = 0.f;
}

void GameWorld::killPed(int pid, int attacker, vec3 dir, DamageType type) {
    Ped& p = peds[pid];
    p.health = 0.f;
#ifdef HAVE_AUDIO
    if (type != DMG_DROWN) Audio::play(p.female ? Audio::SFX_SCREAM_FEMALE : Audio::SFX_SCREAM_MALE, pedHeadPos(p), 0.6f);
#endif
    if (attacker >= 0 && attacker < (int)peds.size() && peds[attacker].isPlayer && pid != attacker) {
        pinfo.kills++;
        pinfo.killMarker = true;
        if (p.faction == FAC_POLICE) pinfo.copsKilled++;
        bool selfDefense = p.faction != FAC_POLICE && p.weapon != WPN_FISTS && p.brain.type == BRAIN_COMBAT && p.brain.target == attacker;
        if (!selfDefense) reportCrime(p.faction == FAC_POLICE ? 6 : 7, p.pos, pid);
    }
    if (p.state == PS_INVEHICLE) {
        // dies in the seat: slump (vehicle keeps rolling)
        p.state = PS_DEAD;
        p.stateTime = 0.f;
        if (p.vehicle >= 0 && p.seat == 0) {
            vehicles[p.vehicle].ctl = Vehicles::VehicleControls();
            vehicles[p.vehicle].ctl.brake = 0.2f;
        }
        return;
    }
    vec3 imp = dir * (type == DMG_EXPLOSION ? 900.f : (type == DMG_VEHICLE ? 400.f : 90.f));
    knockDown(pid, imp);
    p.state = PS_DEAD;
    p.stateTime = 0.f;
    // drop cash / weapon pickups
    if (!p.isPlayer && !p.persistent) {
        u32 h = hash32(p.uid * 31u);
        Pickup pk;
        pk.used = true;
        pk.pos = p.pos + dvec3(0.4, 0.2, 0.05);
        pk.life = 45.f;
        if (p.faction != FAC_CIVILIAN && p.weapon != WPN_FISTS) {
            pk.type = PICK_WEAPON;
            pk.weapon = p.weapon;
            pk.amount = Max(weaponInfo(p.weapon).clipSize * 2, 1);
        } else {
            pk.type = PICK_MONEY;
            pk.amount = 10 + (int)(h % 140);
        }
        bool placed = false;
        for (auto& q : pickups)
            if (!q.used) {
                q = pk;
                placed = true;
                break;
            }
        if (!placed) pickups.push_back(pk);
    }
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::explode(dvec3 pos, float radius, float damage, int owner) {
    if (Ped* sp = playerPed(); sp && (owner == player || length(rel(pos, sp->pos)) < 250.f)) socialReport(UI::TE_EXPLOSION, pos);
    spawnFx(FX_EXPLOSION, pos, vec3(0, 0, 1), 1, radius / 6.f);
    spawnFx(FX_DARK_SMOKE, pos, vec3(0, 0, 2.f), 10, radius / 5.f);
    spawnFx(FX_DEBRIS, pos, vec3(0, 0, 6.f), 12, 1.f);
    spawnFx(FX_SPARKS, pos, vec3(0, 0, 5.f), 16, 1.f);
    spawnLight(pos + dvec3(0, 0, 1.5), vec3(1.f, 0.55f, 0.2f) * 60000.f, radius * 4.f);
    WorldHit gh;
    if (raycast(pos + dvec3(0, 0, 1), vec3(0, 0, -1), 4.f, gh, -1, -1, false, false)) spawnDecal(DECAL_SCORCH, gh.pos, gh.normal, radius * 0.7f);
#ifdef HAVE_AUDIO
    Audio::play(radius > 5.f ? Audio::SFX_EXPLOSION : Audio::SFX_EXPLOSION_SMALL, pos.toVec3());
#endif
    // camera shake + rumble by distance
    float camD = length(rel(rig.cam.pos, pos));
    rig.shake = Max(rig.shake, Saturate(1.4f - camD / (radius * 8.f)));
    rumble(Saturate(1.2f - camD / (radius * 10.f)), Saturate(1.f - camD / (radius * 6.f)));
    {
        float k = Saturate(1.f - camD / (radius * 12.f));
        if (k > 0.f && dot(normalize(rel(pos, rig.cam.pos)), rig.cam.forward()) > -0.2f) {
            fxFlash = Max(fxFlash, k * 0.55f);
            fxFlashColor = vec3(1.f, 0.62f, 0.3f);
            fxChroma = Max(fxChroma, k * 0.6f);
        }
    }
    std::vector<int> list;
    pedsNear(vec2((float)pos.x, (float)pos.y), radius * 1.6f, list);
    for (int i : list) {
        Ped& p = peds[i];
        vec3 d = rel(p.pos, pos) + vec3(0, 0, 0.8f);
        float dist = length(d);
        float f = Saturate(1.f - dist / (radius * 1.6f));
        if (f <= 0.f) continue;
        vec3 dir = dist > 0.01f ? d / dist : vec3(0, 0, 1);
        if (p.state == PS_INVEHICLE) {
            damagePed(i, damage * f * 0.4f, DMG_EXPLOSION, owner, dir);
            continue;
        }
        damagePed(i, damage * f * f, DMG_EXPLOSION, owner, dir);
        if (peds[i].used && peds[i].health > 0.f) knockDown(i, (dir + vec3(0, 0, 0.6f)) * (700.f * f));
        else if (peds[i].ragdoll) knockDown(i, (dir + vec3(0, 0, 0.6f)) * (900.f * f));
    }
    std::vector<int> vl;
    vehiclesNear(vec2((float)pos.x, (float)pos.y), radius * 1.5f, vl);
    for (int i : vl) {
        Vehicle& v = vehicles[i];
        vec3 d = rel(v.sim.body.pos, pos);
        float dist = length(d);
        float f = Saturate(1.f - dist / (radius * 1.5f));
        vec3 dir = dist > 0.01f ? d / dist : vec3(0, 0, 1);
        vec3 imp = normalize(dir + vec3(0, 0, 0.8f)) * (v.sim.body.mass * 9.f * f);
        v.sim.body.applyImpulse(imp, -dir * 0.5f + vec3(0.3f, 0.1f, 0.f));
        v.sim.sleeping = false;
        damageVehicle(i, damage * f * 2.5f, owner, -dir * 0.8f, imp);
        if (f > 0.55f && !v.exploded) v.fireTimer = Max(v.fireTimer, 6.2f);  // chain reaction shortly after
    }
    // break nearby props
    std::vector<int> cols;
    Phys::gCollision->collidersNear(vec2((float)pos.x, (float)pos.y), radius, cols);
    for (int c : cols) Phys::gCollision->breakCollider(c);
    reportCrime(8, pos, -1);
}

void GameWorld::startFire(dvec3 pos, float radius, float life) {
    Fire f;
    f.used = true;
    f.pos = pos;
    f.radius = radius;
    f.life = life;
    for (auto& q : fires)
        if (!q.used) {
            q = f;
            return;
        }
    fires.push_back(f);
}

void GameWorld::updateProjectiles(float dt) {
    for (auto& pr : projectiles) {
        if (!pr.used) continue;
        pr.fuse -= dt;
        vec3 o = pr.pos.toVec3();
        if (pr.type == PROJ_ROCKET) {
            pr.vel += vec3(0, 0, -1.5f) * dt;
            spawnFx(FX_SMOKE, pr.pos, -pr.vel * 0.05f, 1, 0.5f);
        } else {
            pr.vel += vec3(0, 0, -9.81f) * dt;
        }
        float sp = length(pr.vel);
        vec3 d = sp > 1e-4f ? pr.vel / sp : vec3(0, 0, -1);
        WorldHit h;
        bool hit = raycast(pr.pos, d, sp * dt + 0.05f, h, pr.owner, peds[Max(pr.owner, 0)].vehicle, pr.type == PROJ_ROCKET, true);
        if (hit) {
            if (pr.type == PROJ_ROCKET) {
                explode(h.pos - d * 0.3f, 8.f, 450.f, pr.owner);
                pr.used = false;
                continue;
            }
            if (pr.type == PROJ_MOLOTOV) {
#ifdef HAVE_AUDIO
                Audio::play(Audio::SFX_GLASS_BREAK, h.pos.toVec3());
                Audio::play(Audio::SFX_EXPLOSION_SMALL, h.pos.toVec3(), 0.5f);
#endif
                startFire(h.pos, 3.5f, 9.f);
                pr.used = false;
                continue;
            }
            // grenade bounce
            pr.pos = h.pos + dvec3(h.normal * 0.05f);
            pr.vel = (pr.vel - h.normal * (2.f * dot(pr.vel, h.normal))) * 0.45f;
#ifdef HAVE_AUDIO
            if (sp > 3.f) Audio::play(Audio::SFX_GRENADE_BOUNCE, h.pos.toVec3(), Saturate(sp / 10.f));
#endif
        } else {
            pr.pos = pr.pos + pr.vel * dt;
        }
        if (pr.fuse <= 0.f) {
            if (pr.type == PROJ_GRENADE) explode(pr.pos, 7.f, 380.f, pr.owner);
            else if (pr.type == PROJ_ROCKET) explode(pr.pos, 8.f, 450.f, pr.owner);
            else startFire(pr.pos, 3.f, 8.f);
            pr.used = false;
        }
        (void)o;
    }
}

void GameWorld::updateFires(float dt) {
    for (auto& f : fires) {
        if (!f.used) continue;
        f.life -= dt;
        if (f.life <= 0.f) {
            f.used = false;
#ifdef HAVE_AUDIO
            if (f.snd) {
                Audio::destroyEmitter(f.snd);
                f.snd = 0;
            }
#endif
            continue;
        }
        float k = Saturate(f.life / 3.f);
        spawnFx(FX_FIRE, f.pos, vec3(0, 0, 1.5f), 2, f.radius * 0.5f * k + 0.2f);
        if (hash32((u32)(time * 30.0) + (u32)(size_t)&f) % 4 == 0) spawnFx(FX_DARK_SMOKE, f.pos + dvec3(0, 0, 1.5), vec3(0, 0, 1.5f), 1, 1.f);
        spawnLight(f.pos + dvec3(0, 0, 1.0), vec3(1.f, 0.45f, 0.12f) * (9000.f * k), f.radius * 4.f);
#ifdef HAVE_AUDIO
        if (!f.snd) f.snd = Audio::createEmitter(Audio::EMIT_FIRE);
        Audio::setEmitter(f.snd, f.pos.toVec3(), vec3(0), Saturate(f.radius / 4.f) * k, 0, 0, 0, 1.f);
#endif
        std::vector<int> list;
        pedsNear(vec2((float)f.pos.x, (float)f.pos.y), f.radius, list);
        for (int i : list) {
            Ped& p = peds[i];
            if (fabs(p.pos.z - f.pos.z) > 2.0 || p.state == PS_INVEHICLE) continue;
            damagePed(i, 22.f * dt, DMG_FIRE, -1, vec3(0, 0, 1));
        }
        std::vector<int> vl;
        vehiclesNear(vec2((float)f.pos.x, (float)f.pos.y), f.radius + 1.5f, vl);
        for (int i : vl) damageVehicle(i, 30.f * dt, -1, vec3(0), vec3(0));
    }
}

void GameWorld::updatePickups(float dt) {
    Ped* pl = playerPed();
    for (auto& pk : pickups) {
        if (!pk.used) continue;
        if (pk.timer > 0.f) {
            pk.timer -= dt;
            continue;
        }
        if (pk.life > 0.f) {
            pk.life -= dt;
            if (pk.life <= 0.f) {
                pk.used = false;
                continue;
            }
        }
        if (!pl || pl->state != PS_ONFOOT) continue;
        vec3 d = rel(pl->pos, pk.pos);
        if (d.x * d.x + d.y * d.y > 1.3f * 1.3f || fabsf(d.z) > 1.8f) continue;
        bool taken = true;
        switch (pk.type) {
            case PICK_MONEY:
                pinfo.money += pk.amount;
#ifdef HAVE_AUDIO
                Audio::play2D(Audio::SFX_PICKUP_CASH, 0.8f);
#endif
                break;
            case PICK_HEALTH:
                if (pl->health >= pl->maxHealth) taken = false;
                else {
                    pl->health = Min(pl->maxHealth, pl->health + (float)Max(pk.amount, 50));
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_PICKUP_HEALTH, 0.8f);
#endif
                }
                break;
            case PICK_ARMOR:
                if (pl->armor >= 100.f) taken = false;
                else {
                    pl->armor = Min(100.f, pl->armor + (float)Max(pk.amount, 50));
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_PICKUP_HEALTH, 0.8f);
#endif
                }
                break;
            case PICK_WEAPON:
                giveWeapon(player, pk.weapon, pk.amount);
#ifdef HAVE_AUDIO
                Audio::play2D(Audio::SFX_PICKUP_WEAPON, 0.8f);
#endif
                break;
            case PICK_COLLECTIBLE:
            case PICK_PACKAGE:
#ifdef HAVE_AUDIO
                Audio::play2D(Audio::SFX_PICKUP_COLLECTIBLE, 0.9f);
#endif
                break;
        }
        if (taken) {
            if (pk.respawn > 0.f) pk.timer = pk.respawn;
            else pk.used = false;
            onPickupCollected(pk);
        }
    }
}

}  // namespace Game
