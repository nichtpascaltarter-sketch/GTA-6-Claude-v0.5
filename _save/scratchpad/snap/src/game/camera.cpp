// Third-person camera rig: on-foot orbit, over-the-shoulder aim, vehicle chase (cars/bikes/boats/aircraft),
// death orbit, scripted cutscene shots. Collision-aware with smoothing, shake and recoil.
#include "gameworld.h"

namespace Game {

namespace cam_detail {

vec3 dirFromAngles(float yaw, float pitch) { return vec3(-sinf(yaw) * cosf(pitch), cosf(yaw) * cosf(pitch), sinf(pitch)); }

float wrapAngle(float a) {
    while (a > kPi) a -= kTwoPi;
    while (a < -kPi) a += kTwoPi;
    return a;
}

float smoothNoise(float t, float seed) {
    float i = floorf(t), f = t - i;
    float a = hashToFloat(hash32((u32)(i * 1.f) * 2654435761u + (u32)seed)) * 2.f - 1.f;
    float b = hashToFloat(hash32((u32)(i + 1.f) * 2654435761u + (u32)seed)) * 2.f - 1.f;
    f = f * f * (3.f - 2.f * f);
    return Lerp(a, b, f);
}

}  // namespace cam_detail

using namespace cam_detail;

void GameWorld::updateCamera(float dt) {
    CameraRig& r = rig;
    Ped* pp = playerPed();
    const Controls& c = ctl;
    float rdt = Min(dt, 0.05f);
    if (r.mode == CAM_SCRIPTED || r.scriptActive) {
        vec3 d = rel(r.scriptTarget, r.scriptPos);
        float len = length(d);
        if (len > 1e-3f) d = d / len;
        r.cam.pos = r.scriptPos;
        r.cam.yaw = atan2f(-d.x, d.y);
        r.cam.pitch = asinf(Clamp(d.z, -1.f, 1.f));
        r.cam.roll = 0.f;
        r.cam.fovY = r.scriptFov * kDegToRad;
        r.yaw = r.cam.yaw;
        r.pitch = r.cam.pitch;
        return;
    }
    if (!pp) return;
    Ped& p = *pp;
    int veh = p.vehicle;
    bool inVeh = veh >= 0 && p.state == PS_INVEHICLE;
    bool dead = p.state == PS_DEAD || pinfo.deathTimer > 0.f;
    // --- manual look
    if (playerControl && !pinfo.weaponWheel) {
        float sens = (p.aiming ? 0.55f : 1.f);
        r.yaw -= c.look.x * sens;
        r.pitch += c.look.y * sens;
    }
    if (c.lookActive) r.noInputTime = 0.f;
    else r.noInputTime += rdt;
    r.recoil = Max(0.f, r.recoil - rdt * 0.6f);
    dvec3 pivot;
    float wantDist = 3.6f;
    float wantFov = 60.f;
    vec3 shoulder(0, 0, 0);
    float minPitch = -1.25f, maxPitch = 0.95f;
    if (dead) {
        r.mode = CAM_DEATH;
        pivot = p.pos + dvec3(0, 0, 0.6);
        r.yaw += rdt * 0.25f;
        r.pitch = Lerp(r.pitch, -0.55f, rdt * 1.5f);
        wantDist = 4.5f;
        wantFov = 50.f;
    } else if (inVeh) {
        r.mode = CAM_VEHICLE;
        const Vehicle& v = vehicles[veh];
        const Vehicles::VehicleModel& spec = vassets[v.model].spec;
        bool air = isAircraft(veh), boat = isBoat(veh), bike = isBike(veh);
        float len = spec.boxHalf.y * 2.f;
        float height = spec.boxCenter.z + spec.boxHalf.z;
        vec3 f = v.sim.forward();
        vec3 vel = v.sim.body.vel;
        float spd = length(vel);
        r.velSmooth = lerp(r.velSmooth, vel, Saturate(rdt * 3.f));
        vec3 followDir = f;
        if (!air && spd > 3.f && dot(normalize(r.velSmooth), f) > -0.2f) followDir = normalize(lerp(f, normalize(r.velSmooth), 0.5f));
        if (!air && v.sim.forwardSpeed() < -2.f) followDir = f;  // reversing: stay behind the car
        float targetYaw = atan2f(-followDir.x, followDir.y);
        float targetPitch = air ? Clamp(asinf(Clamp(f.z, -1.f, 1.f)) * 0.6f - 0.12f, -0.9f, 0.6f) : -0.16f;
        // auto-follow after a short delay without manual look input
        float follow = r.noInputTime > 1.2f ? (air ? 3.f : 2.4f) : 0.f;
        if (spd < 1.5f && !air) follow *= 0.2f;
        r.yaw += wrapAngle(targetYaw - r.yaw) * Saturate(rdt * follow);
        r.pitch += (targetPitch - r.pitch) * Saturate(rdt * follow * 0.8f);
        if (c.lookBehind.down) r.lookBehind = 1.f;
        else r.lookBehind = 0.f;
        float view = (float)r.vehicleView;
        float baseDist = len * (view == 0 ? 0.95f : 1.25f) + (bike ? 2.8f : 3.2f);
        if (air) baseDist = len * 1.1f + 6.f;
        if (boat) baseDist += 1.5f;
        wantDist = baseDist + Saturate(spd / 40.f) * 1.2f;
        pivot = v.sim.body.pos + dvec3(0, 0, height * 0.75f + (bike ? 0.9f : 0.6f));
        wantFov = 60.f + Saturate((spd - 15.f) / 45.f) * 12.f;
        if (r.vehicleView == 2) {
            // hood / cockpit view
            vec3 seat = spec.seats.empty() ? vec3(0, 0, 1.2f) : spec.seats[0].pos;
            vec3 eye = rotate(v.sim.body.rot, seat + vec3(0, 0.1f, 0.62f));
            r.cam.pos = v.sim.body.pos + eye;
            vec3 fwd = v.sim.forward();
            float yawV = atan2f(-fwd.x, fwd.y), pitchV = asinf(Clamp(fwd.z, -1.f, 1.f));
            if (r.noInputTime < 1.2f) {
                r.cam.yaw = r.yaw;
                r.cam.pitch = r.pitch;
            } else {
                r.yaw = yawV;
                r.pitch = pitchV;
                r.cam.yaw = yawV;
                r.cam.pitch = pitchV;
            }
            vec3 rgt = v.sim.right();
            r.cam.roll = air ? asinf(Clamp(rgt.z, -1.f, 1.f)) : 0.f;
            r.fov = Lerp(r.fov, (65.f + Saturate(spd / 60.f) * 10.f) * kDegToRad, Saturate(rdt * 4.f));
            r.cam.fovY = r.fov;
            if (c.camMode.released) r.vehicleView = 0;
            return;
        }
        // hold the camera button for cinematic roadside shots; a short press cycles the views
        if (c.camMode.down) r.camHold += rdt;
        if (c.camMode.released) {
            if (!r.cineUsed) r.vehicleView = (r.vehicleView + 1) % 3;
            r.camHold = 0.f;
            r.cineUsed = false;
            r.cineActive = false;
        }
        if (r.camHold > 0.35f) {
            r.cineUsed = true;
            dvec3 vp = v.sim.body.pos;
            vec3 toCam = rel(r.cinePos, vp);
            vec3 vfwd = spd > 2.f ? normalize(vel) : f;
            // pick a new vantage point ahead of the vehicle when it has passed the current one
            if (!r.cineActive || dot(toCam, vfwd) < -25.f || length(toCam) > 140.f) {
                u32 hsh = hash32((u32)(time * 3.0) + v.uid);
                float side = (hsh & 1) ? 1.f : -1.f;
                vec3 rightV = normalize(cross(vfwd, vec3(0, 0, 1)));
                float ahead = 25.f + Min(spd, 40.f) * 1.2f;
                vec3 cp = vp.toVec3() + vfwd * ahead + rightV * (side * (6.f + (hsh >> 4) % 6)) ;
                float gz = groundHeight(cp.x, cp.y, cp.z + 20.f);
                cp.z = Max(cp.z, gz) + (air ? 6.f : 1.2f + ((hsh >> 8) % 3));
                if (air) cp = vp.toVec3() - vfwd * 18.f + rightV * (side * 12.f) + vec3(0, 0, 4.f);
                r.cinePos = dvec3(cp);
                r.cineActive = true;
                r.cut = true;
            }
            vec3 d = rel(vp + dvec3(0, 0, 0.8), r.cinePos);
            float dl = length(d);
            if (dl > 1e-3f) d = d / dl;
            r.cam.pos = r.cinePos;
            r.cam.yaw = atan2f(-d.x, d.y);
            r.cam.pitch = asinf(Clamp(d.z, -1.f, 1.f));
            r.cam.roll = 0.f;
            r.fov = Lerp(r.fov, Clamp(900.f / Max(dl, 5.f), 18.f, 55.f) * kDegToRad, Saturate(rdt * 3.f));
            r.cam.fovY = r.fov;
            return;
        }
        minPitch = -1.2f;
        maxPitch = 0.8f;
    } else {
        // on foot
        bool aimMode = p.aiming;
        r.mode = aimMode ? CAM_AIM : CAM_ONFOOT;
        r.aimBlend = Lerp(r.aimBlend, aimMode ? 1.f : 0.f, Saturate(rdt * 10.f));
        float spd = length(vec2(p.vel.x, p.vel.y));
        // gentle auto-follow behind the ped while moving without camera input
        if (!aimMode && spd > 1.f && r.noInputTime > 2.f) {
            float targetYaw = p.yaw;
            r.yaw += wrapAngle(targetYaw - r.yaw) * Saturate(rdt * 0.9f);
        }
        float swim = p.state == PS_SWIM ? 1.f : 0.f;
        pivot = p.pos + dvec3(0, 0, Lerp(1.55f, 1.62f, r.aimBlend) - swim * 0.9f);
        wantDist = Lerp(3.7f + Saturate(spd / 7.f) * 0.6f, 1.55f, r.aimBlend);
        shoulder = vec3(Lerp(0.f, 0.52f, r.aimBlend), 0, 0);
        wantFov = Lerp(58.f + Saturate((spd - 5.f) / 3.f) * 5.f, 44.f, r.aimBlend);
        if (p.weapon == WPN_SNIPER && aimMode) wantFov = 16.f;
        if (p.state == PS_RAGDOLL || p.state == PS_GETUP) wantDist = 4.2f;
        if (p.moveMode == 4) {
            wantDist = 9.f;
            pivot = p.pos + dvec3(0, 0, 3.0);
        } else if (!p.grounded && p.hasParachute && p.airTime > 0.5f) {
            wantDist = 6.5f;
            wantFov = 68.f;
        }
    }
    r.pitch = Clamp(r.pitch, minPitch, maxPitch);
    r.yaw = wrapAngle(r.yaw);
    float camYaw = r.yaw + (r.lookBehind > 0.5f ? kPi : 0.f);
    float camPitch = r.pitch + r.recoil;
    // shake
    r.shake = Max(0.f, r.shake - rdt * 0.9f);
    float sh = r.shake * r.shake;
    float tt = (float)time * 18.f;
    camYaw += smoothNoise(tt, 11.f) * sh * 0.05f;
    camPitch += smoothNoise(tt, 37.f) * sh * 0.05f;
    vec3 fwd = dirFromAngles(camYaw, camPitch);
    vec3 right = normalize(cross(fwd, vec3(0, 0, 1)));
    // smoothed pivot (critically damped follow) for vehicles; direct for on-foot
    vec3 pivotRel = rel(pivot, r.pivotWorld);
    if (r.cut || length(pivotRel) > 30.f) {
        r.pivotWorld = pivot;
        r.pivotSmooth = vec3(0);
        r.curDist = wantDist;
        r.cut = false;
        pivotRel = vec3(0);
    }
    float follow = inVeh ? 14.f : 30.f;
    r.pivotWorld = r.pivotWorld + pivotRel * Saturate(rdt * follow);
    dvec3 piv = r.pivotWorld + right * shoulder.x;
    // camera collision: sphere-ish cast via several rays
    float maxD = wantDist;
    {
        vec3 back = -fwd;
        float best = maxD;
        vec3 offs[5] = {vec3(0), right * 0.2f, right * -0.2f, vec3(0, 0, 0.2f), vec3(0, 0, -0.15f)};
        for (auto& o : offs) {
            WorldHit h;
            int ignoreVeh = inVeh ? veh : -1;
            if (raycast(piv + o, back, maxD + 0.3f, h, player, ignoreVeh, false, true)) best = Min(best, h.t - 0.3f);
        }
        maxD = Max(best, 0.35f);
    }
    if (maxD < r.curDist) r.curDist = maxD;  // snap in
    else r.curDist = Lerp(r.curDist, maxD, Saturate(rdt * 2.5f));
    dvec3 camPos = piv - fwd * r.curDist;
    // keep above ground / water
    float gz = groundHeight((float)camPos.x, (float)camPos.y, (float)camPos.z + 0.5f);
    if (camPos.z < gz + 0.35) camPos.z = gz + 0.35;
    r.cam.pos = camPos;
    r.cam.yaw = camYaw;
    r.cam.pitch = camPitch;
    r.cam.roll = 0.f;
    r.fovTarget = wantFov * kDegToRad;
    r.fov = Lerp(r.fov, r.fovTarget, Saturate(rdt * 6.f));
    r.cam.fovY = r.fov;
}

}  // namespace Game
