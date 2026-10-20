// Player controller: on-foot locomotion (walk/jog/sprint/jump/crouch/swim), aiming with soft lock-on, weapons and
// weapon wheel, entering/jacking/exiting vehicles, driving controls for every vehicle class, death and respawn.
#include "gameworld.h"

namespace Game {

namespace player_detail {

float wrapA(float a) {
    while (a > kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}

struct EnterState {
    float timer = 0.f;
    bool animStarted = false;
    vec3 doorPos;
};
EnterState gEnter;
float gSteerSmooth = 0.f;
int gLockTarget = -1;
float gLockTime = 0.f;

// Hospitals / respawn points (on land near roads, spread across the map)
const vec2 kRespawnPoints[] = {vec2(1650, 1050), vec2(3650, 3200), vec2(-2400, 1800), vec2(5200, -900), vec2(-6400, 5200),
                               vec2(900, 6800), vec2(-3900, -3100), vec2(7400, 4800)};

}  // namespace player_detail

using namespace player_detail;

void GameWorld::updatePlayer(float dt) {
    Ped* pp = playerPed();
    if (!pp) return;
    Ped& p = *pp;
    pinfo.hitMarker = Max(0.f, pinfo.hitMarker - dt * 3.f);
    for (size_t i = 0; i < pinfo.damageDirTimes.size();) {
        pinfo.damageDirTimes[i] += dt;
        if (pinfo.damageDirTimes[i] > 1.5f) {
            pinfo.damageDirTimes.erase(pinfo.damageDirTimes.begin() + i);
            pinfo.damageDirs.erase(pinfo.damageDirs.begin() + i);
        } else i++;
    }
    // health regeneration up to half (GTA-style)
    if (p.health > 0.f && p.health < p.maxHealth * 0.5f && time - p.lastDamageTime > 6.0) p.health = Min(p.maxHealth * 0.5f, p.health + dt * 2.f);
    // busted: hands up, then released at the police station via the respawn flow
    if (pinfo.busted) {
        pinfo.deathTimer += dt;
        p.animIn.stance = 5;
        movePed(p, vec2(0, 0), dt, false);
        return;
    }
    // death
    if (p.health <= 0.f || p.state == PS_DEAD) {
        if (pinfo.deathTimer <= 0.f) {
            pinfo.deathTimer = 0.001f;
            pinfo.deaths++;
#ifdef HAVE_AUDIO
            Audio::play2D(Audio::SFX_WASTED, 0.9f);
            Audio::setSlowMotion(0.5f);
#endif
            timeScale = 0.4f;
        }
        pinfo.deathTimer += dt / Max(timeScale, 0.1f);
        return;
    }
    if (!playerControl) {
        p.aiming = p.firing = false;
        if (p.state == PS_ONFOOT || p.state == PS_SWIM) movePed(p, vec2(0, 0), dt, false);
        return;
    }
    switch (p.state) {
        case PS_ONFOOT:
        case PS_SWIM:
            updatePlayerOnFoot(p, dt);
            break;
        case PS_ENTERING: {
            gEnter.timer += dt;
            int v = p.targetVehicle;
            if (v < 0 || !vehicles[v].used) {
                p.state = PS_ONFOOT;
                break;
            }
            Vehicle& veh = vehicles[v];
            const Vehicles::VehicleModel& spec = vassets[veh.model].spec;
            int seat = p.targetSeat;
            bool left = seat < (int)spec.seats.size() ? spec.seats[seat].exitLeft : true;
            vec3 sp = seat < (int)spec.seats.size() ? spec.seats[seat].pos : vec3(0);
            bool quick = isBike(v) || isBoat(v);
            vec3 local(left ? -(spec.boxHalf.x + 0.35f) : (spec.boxHalf.x + 0.35f), sp.y - 0.2f, 0.f);
            if (isBike(v)) local = vec3(left ? -0.6f : 0.6f, sp.y, 0.f);
            vec3 door = veh.sim.body.pos.toVec3() + rotate(veh.sim.body.rot, local);
            vec3 d = door - p.pos.toVec3();
            d.z = 0;
            float dist = length(d);
            if (!gEnter.animStarted) {
                if (dist > 0.45f && gEnter.timer < 2.5f) {
                    vec2 dir = vec2(d.x, d.y) / Max(dist, 1e-3f);
                    float spd = Min(3.5f, dist * 3.f + 0.8f);
                    movePed(p, dir * spd, dt, false);
                    float ty = atan2f(-dir.x, dir.y);
                    p.yaw += wrapA(ty - p.yaw) * Saturate(dt * 10.f);
                } else {
                    gEnter.animStarted = true;
                    gEnter.timer = 0.f;
                    vec3 f = veh.sim.forward();
                    p.yaw = atan2f(-f.x, f.y) + (left ? -kPi * 0.5f : kPi * 0.5f);
                    p.vel = vec3(0);
                    // jack the current occupant
                    int occ = veh.seats[seat];
                    if (occ >= 0 && occ != player) {
                        removePedFromVehicle(occ, true);
                        Ped& o = peds[occ];
                        o.brain.type = hash32(o.uid) % 4 == 0 && o.faction != FAC_CIVILIAN ? BRAIN_COMBAT : BRAIN_FLEE;
                        o.brain.target = player;
                        o.brain.timer = 0.f;
                        knockDown(occ, rotate(veh.sim.body.rot, vec3(left ? -1.f : 1.f, 0, 0.3f)) * 160.f);
                        pinfo.vehiclesStolen++;
                        reportCrime(9, veh.sim.body.pos, occ);
                        socialReport(UI::TE_CAR_STOLEN, veh.sim.body.pos, spec.name.c_str());
                    } else if (!veh.playerUsed && veh.parked) {
                        pinfo.vehiclesStolen++;
                        socialReport(UI::TE_CAR_STOLEN, veh.sim.body.pos, spec.name.c_str());
                        if (hash32(veh.uid) % 5 == 0) veh.alarm = true;
                    }
                    if (!quick) p.pendingAction = left ? Anim::CLIP_ENTER_CAR_L : Anim::CLIP_ENTER_CAR_R;
#ifdef HAVE_AUDIO
                    if (!quick) Audio::play(Audio::SFX_CAR_DOOR_OPEN, door, 0.7f);
#endif
                }
                // abort with movement input
                if (length(ctl.move) > 0.6f && gEnter.timer > 0.4f && !gEnter.animStarted) p.state = PS_ONFOOT;
            } else {
                float need = quick ? 0.35f : 1.05f;
                if (gEnter.timer >= need) {
                    warpPedIntoVehicle(player, v, seat);
                    pinfo.lastVehicle = v;
                    veh.playerUsed = true;
                    veh.alarm = veh.alarm && !quick;
#ifdef HAVE_AUDIO
                    if (!quick) Audio::play(Audio::SFX_CAR_DOOR_CLOSE, door, 0.7f);
                    if (!veh.sim.engineOn || veh.parked) Audio::play(Audio::SFX_ENGINE_START, door, 0.7f);
                    Audio::setRadioStation(isBike(v) || isBoat(v) || isAircraft(v) ? -1 : veh.radio);
                    Audio::setRadioInterior(1.f);
#endif
                    veh.parked = false;
                }
            }
            break;
        }
        case PS_INVEHICLE:
            updatePlayerVehicle(p, dt);
            break;
        case PS_EXITING:
            if (p.stateTime > 0.9f) {
                p.state = PS_ONFOOT;
                p.stateTime = 0.f;
            }
            movePed(p, vec2(0, 0), dt, false);
            break;
        default:
            break;
    }
}

void GameWorld::updatePlayerOnFoot(Ped& p, float dt) {
    const Controls& c = ctl;
    bool swimming = p.state == PS_SWIM;
    if (p.takedownT >= 0.f) {   // mid-takedown: the clip drives the body
        p.aiming = p.firing = false;
        movePed(p, vec2(0.f, 0.f), dt, false);
        return;
    }
    if (p.moveMode == 2 || p.moveMode == 3) {
        updateTraverse(p, dt);
        return;
    }
    if (p.moveMode == 4) {
        updateParachute(p, dt);
        return;
    }
    // skydiving: long free fall with a parachute on the back
    if (!p.grounded && p.state == PS_ONFOOT && p.hasParachute && p.airTime > 0.5f && (float)p.pos.z - p.groundZ > 20.f) {
        if (hudHelpTimer <= 0.f) help("Press ~i:SPACE|X~ to deploy the ~p~parachute~s~.", 1.f);
        // tracking: steer the fall with the movement input
        vec2 cf(-sinf(rig.yaw), cosf(rig.yaw)), cr(cosf(rig.yaw), sinf(rig.yaw));
        vec2 track = (cr * c.move.x + cf * c.move.y) * 28.f;
        p.vel.x += (track.x - p.vel.x) * Saturate(dt * 0.8f);
        p.vel.y += (track.y - p.vel.y) * Saturate(dt * 0.8f);
        p.vel.z = Max(p.vel.z, -52.f);  // terminal velocity
        if (length(track) > 1.f) p.yaw = atan2f(-track.x, track.y);
        if (c.jump.pressed) {
            p.moveMode = 4;
            p.chuteOpen = 0.f;
#ifdef HAVE_AUDIO
            Audio::play(Audio::SFX_BODY_FALL, p.pos.toVec3() + vec3(0, 0, 3.f), 0.6f, 0.6f);
#endif
            return;
        }
    }
    if (swimming && p.moveMode == 1) p.moveMode = 0;
    // ----- weapon wheel
    if (c.weaponWheel.down && !swimming) {
        if (!pinfo.weaponWheel) {
            pinfo.weaponWheel = true;
            pinfo.wheelSel = weaponInfo(p.weapon).slot;
        }
        timeScale = 0.3f;
        vec2 sel = c.usingPad ? Platform::input().pad.rightStick : vec2(Platform::input().mouseDelta.x, -Platform::input().mouseDelta.y) * 0.05f;
        static vec2 accum;
        if (c.usingPad) accum = sel;
        else accum = (accum + sel) * 0.9f;
        if (length(accum) > 0.5f) {
            float ang = atan2f(accum.x, accum.y);  // 0 = up, clockwise
            if (ang < 0) ang += kTwoPi;
            pinfo.wheelSel = (int)((ang + kPi / 8.f) / (kPi / 4.f)) % 8;
        }
    } else if (pinfo.weaponWheel) {
        pinfo.weaponWheel = false;
        timeScale = 1.f;
        // equip the best weapon in the selected slot
        for (int w = WPN_COUNT - 1; w >= 0; w--)
            if (p.hasWeapon[w] && weaponInfo((WeaponType)w).slot == pinfo.wheelSel) {
                if (p.weapon != (WeaponType)w) {
                    p.weapon = (WeaponType)w;
#ifdef HAVE_AUDIO
                    Audio::play2D(Audio::SFX_WEAPON_SWITCH, 0.5f);
#endif
                }
                break;
            }
    }
    if (c.weaponScroll != 0 && !pinfo.weaponWheel) {
        int w = p.weapon;
        for (int k = 0; k < WPN_COUNT; k++) {
            w = (w + c.weaponScroll + WPN_COUNT) % WPN_COUNT;
            if (p.hasWeapon[w] && (weaponInfo((WeaponType)w).clipSize == 0 || p.ammo[w] > 0)) break;
        }
        if (w != p.weapon) {
            p.weapon = (WeaponType)w;
            p.reloadTimer = 0.f;
#ifdef HAVE_AUDIO
            Audio::play2D(Audio::SFX_WEAPON_SWITCH, 0.5f);
#endif
        }
    }
    if (c.weaponSlot >= 0) {
        for (int w = WPN_COUNT - 1; w >= 0; w--)
            if (p.hasWeapon[w] && weaponInfo((WeaponType)w).slot == c.weaponSlot) {
                p.weapon = (WeaponType)w;
                break;
            }
    }
    const WeaponInfo& wi = weaponInfo(p.weapon);
    // ----- melee lock-on (hold aim with fists / knife / bat): face the opponent and strafe in a guard; block with
    // cover, dodge with jump, light attacks with attack, heavy attacks with reload
    bool meleeLock = false;
    if (wi.clipSize == 0 && !swimming && p.state == PS_ONFOOT && !pinfo.weaponWheel && c.aim.down) {
        int t = p.meleeTarget;
        bool valid = t >= 0 && t < (int)peds.size() && peds[t].used && peds[t].health > 0.f && peds[t].state == PS_ONFOOT && !peds[t].ragdoll &&
                     length(rel(peds[t].pos, p.pos)) < 12.f;
        if (!valid || c.aim.pressed) {
            vec3 cf = rig.cam.forward();
            vec2 cf2(cf.x, cf.y);
            int nt = length(cf2) > 1e-3f ? meleeAutoTarget(p, 9.f, 0.25f, normalize(cf2)) : -1;
            if (nt >= 0) t = nt;
            else if (!valid) t = -1;
        }
        p.meleeTarget = t;
        meleeLock = t >= 0;
    }
    if (meleeLock) {
        vec3 d = rel(peds[p.meleeTarget].pos, p.pos);
        float ty = atan2f(-d.x, d.y);
        float k = Saturate(dt * 4.f);
        if (length(c.look) > 0.02f) k *= 0.25f;   // the player can still look around
        rig.yaw += wrapA(ty - rig.yaw) * k;
        if (p.meleeMove < 0 && p.dodgeT < 0.f) p.yaw = wrapA(p.yaw + wrapA(ty - p.yaw) * Saturate(dt * 12.f));
        if (p.animIn.stance == 0) p.animIn.stance = 19;   // fighting guard
        meleeBlock(player, c.cover.down);
        if (c.jump.pressed) {
            vec2 cfw(-sinf(rig.yaw), cosf(rig.yaw)), crt(cosf(rig.yaw), sinf(rig.yaw));
            vec2 wd = crt * c.move.x + cfw * c.move.y;
            meleeDodge(player, length(wd) > 0.3f ? normalize(wd) : -normalize(vec2(d.x, d.y) + vec2(1e-4f, 0.f)));
        }
    } else {
        if (p.blocking) meleeBlock(player, false);
        if (p.animIn.stance == 19 || p.animIn.stance == 20) p.animIn.stance = 0;
    }
    // ----- aiming
    bool canAim = !swimming && p.state == PS_ONFOOT && !pinfo.weaponWheel;
    bool wantAim = canAim && c.aim.down && (wi.clipSize > 0 || p.weapon == WPN_GRENADE || p.weapon == WPN_MOLOTOV);
    if (wantAim && !p.aiming) {
        // soft lock-on (controller) / snap assist
        gLockTarget = -1;
        if (c.usingPad) {
            vec3 cf = rig.cam.forward();
            float best = 0.86f;   // generous cone (~30 deg) like console aim assist
            vec3 eye = rig.cam.pos.toVec3();
            for (int i = 0; i < (int)peds.size(); i++) {
                const Ped& o = peds[i];
                if (!o.used || i == player || o.health <= 0.f || o.state == PS_INVEHICLE || o.state == PS_RAGDOLL || o.state == PS_DEAD ||
                    o.state == PS_GETUP)
                    continue;
                vec3 tp = pedChestPos(o);
                vec3 d = tp - eye;
                float dist = length(d);
                if (dist > 70.f || dist < 1.f) continue;
                float cs = dot(d / dist, cf);
                bool hostile = o.faction == FAC_ENEMY || o.faction == FAC_POLICE || (o.brain.type == BRAIN_COMBAT && o.brain.target == player);
                float score = cs + (hostile ? 0.08f : -0.04f);
                if (score > best && lineOfSight(rig.cam.pos, dvec3(tp), player, -1)) {
                    best = score;
                    gLockTarget = i;
                }
            }
        }
        gLockTime = 0.f;
    }
    p.aiming = wantAim;
    if (gLockTarget >= 0 && (!peds[gLockTarget].used || peds[gLockTarget].health <= 0.f || peds[gLockTarget].state == PS_RAGDOLL ||
                             peds[gLockTarget].state == PS_DEAD))
        gLockTarget = -1;   // target went down: release the lock
    if (p.aiming && gLockTarget >= 0) {
        gLockTime += dt;
        vec3 tp = pedChestPos(peds[gLockTarget]);
        vec3 d = tp - rig.cam.pos.toVec3();
        float ty = atan2f(-d.x, d.y), tpch = atan2f(d.z, length(vec2(d.x, d.y)));
        float k = gLockTime < 0.2f ? Saturate(dt * 14.f) : Saturate(dt * 3.f);
        if (length(c.look) > 0.02f) k *= 0.2f;  // player overrides
        rig.yaw += wrapA(ty - rig.yaw) * k;
        rig.pitch += (tpch - rig.pitch) * k;
    }
    // aim target point from the camera through the reticle
    vec3 camF = rig.cam.forward();
    WorldHit aimHit;
    dvec3 aimPoint = rig.cam.pos + camF * 250.f;
    if (raycast(rig.cam.pos + camF * 1.2f, camF, 250.f, aimHit, player, -1)) aimPoint = aimHit.pos;
    pinfo.hitMarker = pinfo.hitMarker;  // decays in updatePlayer
    // muzzle position: right hand (fallback: chest + forward)
    quat qy = quatAxisAngle(vec3(0, 0, 1), p.yaw);
    vec3 hand = p.pos.toVec3() + rotate(qy, p.bones[Anim::B_HAND_R].c[3].xyz());
    vec3 muzzle = hand + normalize(rel(aimPoint, dvec3(hand))) * (p.weapon >= WPN_SMG && p.weapon <= WPN_RPG ? 0.5f : 0.22f);
    dvec3 fpMuzzle;
    if (fpWeaponMuzzle(fpMuzzle)) muzzle = fpMuzzle.toVec3();   // first person: the gun held in front of the eyes
    vec3 aimDir = normalize(rel(aimPoint, dvec3(muzzle)));
    p.aimDir = aimDir;
    p.aimPitch = asinf(Clamp(camF.z, -1.f, 1.f));
    // ----- fire / reload
    bool fireInput = wi.automatic ? c.attack.down : c.attack.pressed;
    p.firing = false;
    if (!pinfo.weaponWheel && !swimming && fireInput) {
        if (wi.clipSize == 0) {
            if (p.meleeMove >= 0) {
                meleeStart(player, false);   // chains the combo
            } else if (p.dodgeT < 0.f && p.meleeStagger <= 0.f) {
                if (!meleeLock) p.yaw = atan2f(-camF.x, camF.y);   // swing where the camera looks
                if (!stealthTakedown(p) && !(c.sprint.down && p.weapon == WPN_FISTS && !meleeLock && sprintKick(p))) meleeStart(player, false);
            }
        } else {
            // hip fire turns the ped toward the aim direction first
            float ty = atan2f(-aimDir.x, aimDir.y);
            float diff = wrapA(ty - p.yaw);
            p.yaw += diff * Saturate(dt * 20.f);
            if (fabsf(diff) < 0.5f) {
                if (p.weapon == WPN_GRENADE || p.weapon == WPN_MOLOTOV) fireWeapon(player, dvec3(p.pos.toVec3() + vec3(0, 0, 1.6f) + camF * 0.4f), camF);
                else fireWeapon(player, dvec3(muzzle), aimDir);
            }
        }
    }
    if (c.reload.pressed && wi.clipSize == 0 && !pinfo.weaponWheel && !swimming) {
        if (!meleeLock && p.meleeMove < 0) p.yaw = atan2f(-camF.x, camF.y);
        meleeStart(player, true);   // heavy attack
    }
    if (c.reload.pressed && wi.clipSize > 0 && p.clip[p.weapon] < clipCapacity(p, p.weapon) && p.ammo[p.weapon] > p.clip[p.weapon] && p.reloadTimer <= 0.f) {
        p.reloadTimer = wi.reloadTime;
#ifdef HAVE_AUDIO
        Audio::play(Audio::SFX_RELOAD, hand, 0.6f);
#endif
    }
    // weapon flashlight on / off (H / D-pad down, as for vehicle lights)
    if (c.lights.pressed && (weaponComps(p, p.weapon) & WC_FLASHLIGHT)) pinfo.flashlightOn = !pinfo.flashlightOn;
    // ----- movement
    float cy = rig.yaw;
    vec2 fwd(-sinf(cy), cosf(cy)), right(cosf(cy), sinf(cy));
    vec2 mv = c.move;
    if (pinfo.weaponWheel) mv = vec2(0, 0);
    vec2 dir = right * mv.x + fwd * mv.y;
    float mag = Min(length(mv), 1.f);
    if (c.walkToggle.pressed) pinfo.walkMode = !pinfo.walkMode;
    bool sprint = c.sprint.down && mag > 0.3f && !p.aiming && !meleeLock && pinfo.stamina > 0.02f;
    float speed;
    if (swimming) speed = sprint ? 2.6f : 1.4f;
    else if (p.aiming) speed = mag > 0.7f && !pinfo.walkMode ? 2.9f : 1.6f;
    else if (sprint) speed = 6.9f;
    else if (pinfo.walkMode || mag < 0.5f) speed = 1.55f * Max(mag, 0.3f) / 0.5f;
    else speed = 4.1f;
    speed = Min(speed, 6.9f);
    if (p.animIn.crouch && !swimming) speed = Min(speed, 1.9f);
    if (meleeLock) speed = Min(speed, mag > 0.7f ? 2.4f : 1.5f);   // footwork around the opponent
    if (p.meleeMove >= 0 || p.meleeStagger > 0.f) speed *= 0.2f;    // planted while swinging / reeling
    if (mag < 0.1f) speed = 0.f;
    if (sprint && mag > 0.1f) pinfo.stamina = Max(0.f, pinfo.stamina - dt * 0.07f);
    else pinfo.stamina = Min(1.f, pinfo.stamina + dt * 0.12f);
    vec2 desired = mag > 0.1f ? normalize(dir) * speed : vec2(0, 0);
    // first person: the body faces the view; backwards and sideways the legs can only walk / jog
    bool firstPerson = rig.fpActive && !swimming;
    if (firstPerson && mag > 0.1f) {
        float fwdAmt = dot(normalize(dir), fwd);
        if (fwdAmt < -0.3f) desired = desired * (Min(speed, 2.2f) / Max(speed, 1e-3f));
        else if (fwdAmt < 0.5f) desired = desired * (Min(speed, 3.6f) / Max(speed, 1e-3f));
    }
    // facing
    float prevYaw = p.yaw;
    if (meleeLock || p.meleeMove >= 0 || p.dodgeT >= 0.f) {
        // melee.cpp / the lock-on above steer the facing
    } else if (firstPerson) {
        p.yaw += wrapA(cy - p.yaw) * Saturate(dt * 18.f);
    } else if (p.aiming || (p.firing && wi.clipSize > 0)) {
        float ty = atan2f(-camF.x, camF.y);
        p.yaw += wrapA(ty - p.yaw) * Saturate(dt * 16.f);
    } else if (mag > 0.1f) {
        float ty = atan2f(-dir.x, dir.y);
        float turn = sprint ? 7.f : 10.f;
        float d = wrapA(ty - p.yaw);
        float step = Clamp(d, -turn * dt * 1.5f, turn * dt * 1.5f);
        p.yaw += (fabsf(d) > 2.6f && speed < 5.f) ? d * Saturate(dt * 12.f) : step;
        // slow down while turning sharply
        if (fabsf(d) > 1.2f && p.grounded) desired = desired * 0.45f;
    }
    p.yaw = wrapA(p.yaw);
    p.turnRate = wrapA(p.yaw - prevYaw) / Max(dt, 1e-4f);
    // ----- cover
    vec3 camF3 = rig.cam.forward();
    if (c.cover.pressed && !swimming && !meleeLock) {
        if (p.moveMode == 1) {
            p.moveMode = 0;
            p.animIn.crouch = false;
        } else {
            vec3 probeDir = mag > 0.1f ? vec3(normalize(dir), 0) : normalize(vec3(camF3.x, camF3.y, 0));
            float top, thick;
            vec3 hit, nrm;
            if (probeObstacle(p, probeDir, 2.2f, top, thick, hit, nrm)) {
                float h = top - (float)p.pos.z;
                if (h > 0.75f) {
                    vec3 n = normalize(vec3(nrm.x, nrm.y, 0));
                    vec3 cp = hit + n * 0.36f;
                    cp.z = groundHeight(cp.x, cp.y, (float)p.pos.z + 0.4f);
                    p.pos = dvec3(cp);
                    p.coverNormal = n;
                    p.coverLow = h < 1.4f;
                    p.moveMode = 1;
                    p.vel = vec3(0);
                    p.yaw = atan2f(n.x, -n.y);  // face the cover surface
                }
            }
        }
    }
    if (p.moveMode == 1) {
        vec3 n = p.coverNormal;
        vec3 tangent = normalize(cross(vec3(0, 0, 1), n));
        float along = dot(vec3(dir, 0), tangent) * Min(mag, 1.f);
        float away = dot(vec3(dir, 0), n) * Min(mag, 1.f);
        // crouch behind low cover, pop up while aiming
        p.animIn.crouch = p.coverLow && !p.aiming;
        if (away > 0.75f && !p.aiming) {
            p.moveMode = 0;
            p.animIn.crouch = false;
        } else {
            vec3 cand = p.pos.toVec3() + tangent * (along * 2.2f * dt);
            // stay only while there is still cover behind the candidate position
            WorldHit wh;
            bool still = raycast(dvec3(cand + vec3(0, 0, p.coverLow ? 0.6f : 1.2f)), -n, 0.9f, wh, player, -1, false, true);
            if (still && fabsf(along) > 0.05f) p.pos = dvec3(cand.x, cand.y, (double)groundHeight(cand.x, cand.y, cand.z + 0.4f));
            if (!p.aiming) p.yaw = atan2f(n.x, -n.y);
            else {
                float ty = atan2f(-camF3.x, camF3.y);
                p.yaw += wrapA(ty - p.yaw) * Saturate(dt * 16.f);
            }
            p.vel = vec3(0);
            p.animIn.stance = 0;
            return;
        }
    }
    if (!swimming) {
        if (c.crouchHold) {   // settings: crouch while held
            if (c.crouch.pressed) p.animIn.crouch = true;
            if (c.crouch.released) p.animIn.crouch = false;
        } else if (c.crouch.pressed) {
            p.animIn.crouch = !p.animIn.crouch;
        }
    }
    // ----- diving
    if (swimming) {
        if (c.crouch.down) p.diveDepth = Min(p.diveDepth + dt * 1.6f, 9.f);
        else if (c.jump.down) p.diveDepth = Max(p.diveDepth - dt * 2.2f, 0.f);
        if (p.diveDepth > 0.6f) {
            pinfo.breath = Max(0.f, pinfo.breath - dt / 28.f);
            if (pinfo.breath <= 0.f) damagePed(player, 12.f * dt, DMG_DROWN, -1, vec3(0, 0, 1));
        }
    } else {
        p.diveDepth = 0.f;
    }
    bool jump = c.jump.pressed && p.grounded && !p.aiming && !swimming && !meleeLock && p.dodgeT < 0.f;
    // jumping at a low wall / car / fence vaults or climbs it instead
    if (jump && mag > 0.2f && tryTraverse(p, vec3(normalize(dir), 0))) return;
    if (jump && tryTraverse(p, vec3(-sinf(p.yaw), cosf(p.yaw), 0))) return;
    if (jump) p.animIn.crouch = false;
    movePed(p, desired, dt, jump);
    if (jump) p.pendingAction = Anim::CLIP_JUMP_START;
    pinfo.distanceWalked += length(vec2(p.vel.x, p.vel.y)) * dt;
    if (p.diveDepth <= 0.6f) pinfo.breath = Min(1.f, pinfo.breath + dt * 0.3f);
    // ----- enter vehicle
    if ((c.enter.pressed || c.special.pressed) && !swimming) {
        bool asPassenger = c.special.pressed && !c.enter.pressed;
        std::vector<int> list;
        vehiclesNear(p.pos.toVec3().xy(), 7.f, list);
        int best = -1;
        float bestD = 1e9f;
        for (int vi : list) {
            Vehicle& v = vehicles[vi];
            if (v.locked || v.exploded || v.sim.wrecked) continue;
            if (asPassenger && freeSeat(vi, false) < 0) continue;
            const Vehicles::VehicleModel& spec = vassets[v.model].spec;
            mat3 R = v.sim.body.rotMat();
            vec3 lc = transpose(R) * (p.pos.toVec3() - v.sim.body.pos.toVec3());
            float dx = Max(fabsf(lc.x) - spec.boxHalf.x, 0.f), dy = Max(fabsf(lc.y - spec.boxCenter.y) - spec.boxHalf.y, 0.f);
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 2.2f && fabsf(lc.z) < 3.f && d < bestD) {
                bestD = d;
                best = vi;
            }
        }
        if (best >= 0) {
            int seat = asPassenger ? freeSeat(best, false) : 0;
            p.targetVehicle = best;
            p.targetSeat = seat;
            p.state = PS_ENTERING;
            p.stateTime = 0.f;
            p.aiming = false;
            gEnter = EnterState();
        }
    }
}

// Silent takedown of an unaware ped from behind while sneaking (crouched) with fists or a knife.
bool GameWorld::stealthTakedown(Ped& p) {
    if (!p.animIn.crouch || (p.weapon != WPN_FISTS && p.weapon != WPN_KNIFE)) return false;
    vec3 f(-sinf(p.yaw), cosf(p.yaw), 0.f);
    std::vector<int> list;
    pedsNear(p.pos.toVec3().xy(), 2.f, list);
    for (int o : list) {
        if (o == player) continue;
        Ped& t = peds[o];
        if (t.health <= 0.f || t.state != PS_ONFOOT || t.brain.alerted || t.isPlayer) continue;
        vec3 d = t.pos.toVec3() - p.pos.toVec3();
        d.z = 0;
        float dist = length(d);
        if (dist > 1.6f || dist < 0.2f || dot(d / dist, f) < 0.7f) continue;
        vec3 tf(-sinf(t.yaw), cosf(t.yaw), 0.f);
        if (dot(tf, d / dist) < 0.3f) continue;  // must be approached from behind
        // synced rear choke (the victim is released dead at the end of the grab, see melee.cpp)
        return startTakedown(player, o);
    }
    return false;
}

// Running kick: heavier melee hit with a good chance to knock the target over.
bool GameWorld::sprintKick(Ped& p) {
    vec3 f(-sinf(p.yaw), cosf(p.yaw), 0.f);
    std::vector<int> list;
    pedsNear(p.pos.toVec3().xy(), 2.4f, list);
    for (int o : list) {
        if (o == player) continue;
        Ped& t = peds[o];
        if (t.health <= 0.f || t.state != PS_ONFOOT) continue;
        vec3 d = t.pos.toVec3() - p.pos.toVec3();
        d.z = 0;
        float dist = length(d);
        if (dist > 2.1f || dist < 0.2f || dot(d / dist, f) < 0.6f) continue;
        p.pendingAction = Anim::CLIP_KICK;
        damagePed(o, 28.f, DMG_MELEE, player, f);
        if (peds[o].used && peds[o].health > 0.f) knockDown(o, f * 320.f + vec3(0, 0, 80.f));
#ifdef HAVE_AUDIO
        Audio::play(Audio::SFX_KICK, pedChestPos(t), 0.9f);
#endif
        return true;
    }
    return false;
}

void GameWorld::updatePlayerVehicle(Ped& p, float dt) {
    const Controls& c = ctl;
    int vi = p.vehicle;
    if (vi < 0) return;
    Vehicle& v = vehicles[vi];
    const Vehicles::VehicleModel& spec = vassets[v.model].spec;
    bool air = isAircraft(vi);
    Vehicles::VehicleControls& vc = v.ctl;
    vc = Vehicles::VehicleControls();
    if (p.seat == 0 && !pinfo.weaponWheel) {
        vc.throttle = c.accel;
        vc.brake = c.brake;
        // keyboard steering is ramped; analog passes through
        float target = c.steer;
        if (!c.usingPad) {
            float rate = fabsf(target) > fabsf(gSteerSmooth) ? 3.2f : 6.f;
            gSteerSmooth += Clamp(target - gSteerSmooth, -rate * dt, rate * dt);
            vc.steer = gSteerSmooth;
        } else {
            gSteerSmooth = target;
            vc.steer = target;
        }
        vc.handbrake = c.handbrake.down;
        if (air) {
            vc.pitch = c.pitch;
            vc.roll = c.roll;
            vc.yaw = c.yaw;
            vc.lift = c.lift;
            vc.throttle = spec.cls == Vehicles::VC_PLANE ? c.accel : 0.f;
            vc.brake = spec.cls == Vehicles::VC_PLANE ? c.brake : 0.f;
        } else {
            // bikes: rider lean (wheelie/stoppie); cars: air control (the sim ignores it on the ground)
            float kb = (Platform::input().down(KEY_DOWN) ? 1.f : 0.f) - (Platform::input().down(KEY_UP) ? 1.f : 0.f);
            vc.pitch = Clamp(kb - Platform::input().pad.leftStick.y, -1.f, 1.f);
            if (!isBike(vi) && !isBoat(vi)) vc.roll = 0.f;
        }
        v.hornOn = c.horn.down && spec.sirenMode < 0;
        if (c.horn.pressed && spec.sirenMode >= 0) v.sirenOn = !v.sirenOn;
        if (c.lights.pressed) v.lightsOn = !v.lightsOn;
        v.indicator = 0;
    }
    // radio
    if (!isBike(vi) && !isBoat(vi) && !air) {
        int n = 0;
#ifdef HAVE_AUDIO
        n = Audio::radioStationCount();
#endif
        if (n > 0 && (c.radioNext.pressed || c.radioPrev.pressed)) {
            int st = v.radio;
            st = c.radioNext.pressed ? st + 1 : st - 1;
            if (st >= n) st = -1;
            if (st < -1) st = n - 1;
            v.radio = st;
#ifdef HAVE_AUDIO
            Audio::setRadioStation(st);
#endif
            pinfo.radioStation = st;
            hudRadioTimer = 3.f;
        }
    }
    // drive-by (keyboard + mouse): aim with RMB, fire with LMB
    p.aiming = false;
    p.firing = false;
    if (c.aim.down && !c.usingPad && (p.weapon == WPN_PISTOL || p.weapon == WPN_REVOLVER || p.weapon == WPN_SMG) && !air) {
        p.aiming = true;
        vec3 camF = rig.cam.forward();
        WorldHit h;
        dvec3 target = rig.cam.pos + camF * 150.f;
        if (raycast(rig.cam.pos + camF * 2.f, camF, 150.f, h, player, vi)) target = h.pos;
        vec3 head = pedHeadPos(p);
        vec3 muzzle = head + v.sim.right() * (spec.seats.empty() || spec.seats[0].exitLeft ? -0.55f : 0.55f) - vec3(0, 0, 0.2f);
        vec3 d = normalize(rel(target, dvec3(muzzle)));
        bool fire = weaponInfo(p.weapon).automatic ? c.attack.down : c.attack.pressed;
        if (fire) fireWeapon(player, dvec3(muzzle), d);
    }
    pinfo.distanceDriven += v.sim.speed() * dt;
    // exit
    if (c.enter.pressed) {
        float spd = v.sim.speed();
        bool bail = spd > 7.f && !air && !isBoat(vi);
        v.hornOn = false;
        vc = Vehicles::VehicleControls();
        if (air && spd > 5.f) bail = true;
#ifdef HAVE_AUDIO
        Audio::setRadioStation(-1);
#endif
        float agl = air ? v.sim.agl : 0.f;
        removePedFromVehicle(player, !bail);
        if (air && agl > 18.f) {
            // jump out of an aircraft: free fall with a parachute
            p.hasParachute = true;
            p.state = PS_ONFOOT;
            p.grounded = false;
            p.airTime = 0.f;
            p.fallStartZ = (float)p.pos.z;
            p.vel = v.sim.body.vel + v.sim.right() * (spec.seats.empty() || spec.seats[0].exitLeft ? -2.f : 2.f);
            v.idleTime = 0.f;
            return;
        }
        if (bail) {
            // dive out: ragdoll with the vehicle's velocity
            knockDown(player, v.sim.body.vel * 20.f + v.sim.right() * (spec.seats.empty() || spec.seats[0].exitLeft ? -150.f : 150.f));
        } else {
            p.state = PS_EXITING;
            p.stateTime = 0.f;
#ifdef HAVE_AUDIO
            if (!isBike(vi) && !isBoat(vi)) Audio::play(Audio::SFX_CAR_DOOR_CLOSE, p.pos.toVec3(), 0.6f);
#endif
        }
        v.idleTime = 0.f;
    }
}

void GameWorld::updateParachute(Ped& p, float dt) {
    const Controls& c = ctl;
    p.chuteOpen = Min(1.f, p.chuteOpen + dt * 1.4f);
    float open = p.chuteOpen * p.chuteOpen;
    p.yaw -= c.move.x * 1.05f * dt * open;
    bool flare = c.sprint.down || c.move.y < -0.5f;
    float fwdSpeed = flare ? 4.5f : 9.5f + Max(c.move.y, 0.f) * 4.f;
    float sink = flare ? 2.6f : 5.2f + Max(c.move.y, 0.f) * 2.f;
    vec3 f(-sinf(p.yaw), cosf(p.yaw), 0.f);
    vec3 target = f * (fwdSpeed * open) + vec3(0, 0, -Lerp(45.f, sink, open));
    target.x += env->windDir.x * env->wind * 3.f;
    target.y += env->windDir.y * env->wind * 3.f;
    p.vel += (target - p.vel) * Saturate(dt * (1.2f + open * 1.5f));
    vec3 np = p.pos.toVec3() + p.vel * dt;
    vec3 push, n;
    if (Phys::gCollision->capsuleOverlap(np, 0.35f, 1.8f, push, n)) np += push;
    float gz = groundHeight(np.x, np.y, np.z + 0.5f);
    float wz;
    bool water = Phys::waterSurface(np.x, np.y, wz) && wz > gz;
    float surface = water ? wz - 1.2f : gz;
    p.pos = dvec3(np);
    p.grounded = false;
    p.airTime += dt;
    if (np.z <= surface + 0.05f) {
        float impact = -p.vel.z;
        p.moveMode = 0;
        p.hasParachute = false;
        p.chuteOpen = 0.f;
        p.pos.z = surface;
        p.grounded = !water;
        p.airTime = 0.f;
        p.fallStartZ = surface;
        p.vel = vec3(p.vel.x, p.vel.y, 0.f) * 0.3f;
        if (!water && impact > 8.f) knockDown(player, vec3(f * 90.f));
        else if (!water) p.pendingAction = Anim::CLIP_LAND;
    }
}

// Focus: Mari slows time while aiming on foot, Dex while driving. The meter drains while active and recharges
// slowly (faster with kills / near misses in the future). Toggled with Caps Lock or by clicking both sticks.
void GameWorld::updateFocus(float realDt) {
    Ped* pp = playerPed();
    if (!pp || pinfo.deathTimer > 0.f || pinfo.busted) {
        if (focusActiveApplied) {
            focusActiveApplied = false;
            pinfo.focusActive = false;
        }
        return;
    }
    Ped& p = *pp;
    bool eligible = protagonistIndex == 0 ? (p.state == PS_ONFOOT && p.aiming) : (p.state == PS_INVEHICLE && p.seat == 0);
    if (ctl.focus.pressed && playerControl) {
        if (!pinfo.focusActive && pinfo.focus > 0.15f && eligible) pinfo.focusActive = true;
        else pinfo.focusActive = false;
    }
    if (pinfo.focusActive && (!eligible || pinfo.focus <= 0.f)) pinfo.focusActive = false;
    if (pinfo.focusActive) pinfo.focus = Max(0.f, pinfo.focus - realDt / 9.f);
    else pinfo.focus = Min(1.f, pinfo.focus + realDt / 90.f);
    if (pinfo.focusActive != focusActiveApplied) {
        focusActiveApplied = pinfo.focusActive;
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_UI_SELECT, 0.4f, pinfo.focusActive ? 0.6f : 0.9f);
#endif
    }
    if (!pinfo.weaponWheel) {
        float target = pinfo.focusActive ? (protagonistIndex == 0 ? 0.35f : 0.5f) : 1.f;
        timeScale = Lerp(timeScale, target, Saturate(realDt * 6.f));
        if (fabsf(timeScale - 1.f) < 0.01f) timeScale = 1.f;
#ifdef HAVE_AUDIO
        Audio::setSlowMotion(timeScale);
#endif
    }
}

void GameWorld::tutorialHint(int id, const char* text) {
    if (pinfo.hintsShown & (1u << id)) return;
    if (hudHelpTimer > 0.f) return;   // wait until the current help box is gone
    pinfo.hintsShown |= 1u << id;
    help(text, 7.f);
}

// Contextual one-time hints for new players.
void GameWorld::updateTutorialHints(float dt) {
    (void)dt;
    Ped* pp = playerPed();
    if (!pp || !playerControl || mInCutscene() || pinfo.deathTimer > 0.f) return;
    Ped& p = *pp;
    if (p.state == PS_ONFOOT) {
        std::vector<int> list;
        vehiclesNear(p.pos.toVec3().xy(), 4.5f, list);
        for (int vi : list)
            if (!vehicles[vi].locked && !vehicles[vi].exploded) {
                tutorialHint(0, "Press ~i:F|Y~ to enter a vehicle. Hold ~i:G|RS~ nearby to ride as a passenger.");
                break;
            }
        if (pinfo.playTime > 20.0) tutorialHint(1, "Hold ~i:SHIFT|A~ to sprint. Press ~i:SPACE|X~ at low walls to vault or climb.");
        if (p.weapon != WPN_FISTS && pinfo.playTime > 40.0) tutorialHint(2, "Aim with ~i:RMB|LT~ and fire with ~i:LMB|RT~. Hold ~i:TAB|LB~ for the weapon wheel.");
        if (pinfo.wanted > 0) tutorialHint(3, "Press ~i:Q|RB~ near a wall or car to take cover. Break line of sight and leave the red search area to lose the police.");
        if (pinfo.playTime > 90.0) tutorialHint(4, "Press ~i:M|START~ to open the map and set a waypoint. Crouch with ~i:CTRL|LS~ to sneak up for silent takedowns.");
        if (pinfo.playTime > 150.0) tutorialHint(5, "Use Focus with ~k:CAPS~ or by clicking both sticks (~p:LS~ + ~p:RS~): Mari slows time while aiming, Dex while driving.");
    } else if (p.state == PS_INVEHICLE) {
        int pv = p.vehicle;
        if (!isBike(pv) && !isBoat(pv) && !isAircraft(pv)) tutorialHint(6, "Change the radio with ~i:PGUP|RIGHT~. Headlights ~i:H|DOWN~, horn ~i:E|LS~. Hold ~i:V|BACK~ for the cinematic camera.");
        if (isAircraft(pv)) tutorialHint(7, "Throttle/collective with ~i:W|RT~ and ~i:S|LT~, pitch and roll with ~i:ARROWS|LS~, yaw with ~i:A|LB~ / ~i:D|RB~. Bail out at altitude to skydive.");
        if (isBoat(pv)) tutorialHint(8, "Boats plane at speed - ease off the throttle in tight turns.");
    }
}

void GameWorld_respawnPlayer(GameWorld& g) {
    Ped* pp = g.playerPed();
    if (!pp) return;
    Ped& p = *pp;
    vec2 best = kRespawnPoints[0];
    float bd = 1e30f;
    for (vec2 r : kRespawnPoints) {
        float d = length(r - p.pos.toVec3().xy());
        if (d < bd) {
            bd = d;
            best = r;
        }
    }
    // snap to the nearest sidewalk/road
    float s = 0, side = 0;
    int e = World::gRoads->nearestEdge(best, 400.f, &s, nullptr, &side);
    vec3 pos(best, 0.f);
    if (e >= 0) {
        const World::RoadEdge& ed = World::gRoads->edges[e];
        vec3 c = ed.posAt(s);
        vec3 t = ed.tangentAt(s);
        vec3 n = normalize(vec3(-t.y, t.x, 0));
        pos = c + n * (ed.halfWidth + ed.sidewalk * 0.5f) * (side >= 0 ? 1.f : -1.f);
    }
    if (p.vehicle >= 0) g.removePedFromVehicle(g.player, false);
    freeRagdoll(p.ragdoll);
    p.pos = dvec3(pos.x, pos.y, g.groundHeight(pos.x, pos.y, pos.z + 50.f));
    p.health = p.maxHealth;
    p.armor = 0.f;
    p.state = PS_ONFOOT;
    p.stateTime = 0.f;
    p.vel = vec3(0);
    p.grounded = true;
    bool wasBusted = g.pinfo.busted;
    if (wasBusted) {
        // lose non-melee weapons and some cash, like a real booking
        for (int w = WPN_PISTOL; w < WPN_COUNT; w++) {
            p.hasWeapon[w] = false;
            p.ammo[w] = p.clip[w] = 0;
        }
        p.weapon = WPN_FISTS;
    }
    p.animIn.stance = 0;
    g.playerControl = true;
    g.pinfo.deathTimer = 0.f;
    g.pinfo.wanted = 0;
    g.pinfo.wantedHeat = 0.f;
    g.pinfo.busted = false;
    long long fee = Min<long long>(g.pinfo.money, Max<long long>(250, g.pinfo.money / 10));
    g.pinfo.money -= fee;
    g.timeScale = 1.f;
    g.rig.cut = true;
    g.populationWarmup = 1.5f;
#ifdef HAVE_AUDIO
    Audio::setSlowMotion(1.f);
#endif
}

}  // namespace Game
