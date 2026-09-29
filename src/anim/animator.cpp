// Animation state machine (allocation free). Layers, evaluated every update:
//  1. base: phase-synchronized locomotion (idle / walk / jog / run / sprint forward gaits blended by speed, walk-back and
//     strafes blended by the local move direction, crouch variants) or swimming, falling, a scenario/vehicle stance;
//  2. upper-body layers: aim (pistol / rifle / guard / throwable) with aim pitch on the spine, fire recoil, reload,
//     scenario upper bodies while walking (phone, smoke, hands up...);
//  3. one-shot actions (hits, punches, car entry/exit, jumps, deaths, get-ups...), full body or upper body only when
//     the character keeps moving;
//  4. procedural adjustments: lean into turns and two-bone foot IK against the terrain offsets;
//  5. discontinuities (action start/end, stance change) are hidden with a crossfade from a captured pose.
#include "anim_internal.h"

namespace Anim {
namespace detail {

float skeletonStyle(const Skeleton& sk);
float skeletonLegScale(const Skeleton& sk);

// Stance -> looping clip.
static const Clip kStanceClip[] = {
    CLIP_IDLE,        // 0 normal (locomotion)
    CLIP_SIT_DRIVE,   // 1 driving
    CLIP_SIT_PASSENGER,  // 2 passenger
    CLIP_RIDE_BIKE,   // 3 bike
    CLIP_COWER,       // 4
    CLIP_HANDS_UP,    // 5
    CLIP_SIT_BENCH,   // 6
    CLIP_TALK,        // 7
    CLIP_TALK_PHONE,  // 8
    CLIP_DANCE,       // 9
    CLIP_SMOKE,       // 10
    CLIP_LEAN_WALL,   // 11
    CLIP_SUNBATHE,    // 12
    CLIP_JOG_IDLE,    // 13
    CLIP_IDLE_LOOK,   // 14
    CLIP_WAVE,        // 15
    CLIP_CHEER,       // 16
    CLIP_POINT,       // 17
    CLIP_CROUCH_IDLE, // 18
    CLIP_BLOCK,       // 19 fighting guard
};
static const int kStanceCount = (int)(sizeof(kStanceClip) / sizeof(kStanceClip[0]));

static bool stanceIsVehicle(int s) { return s >= 1 && s <= 3; }
// Scenario stances whose upper body stays on while walking.
static bool stanceUpperWhileMoving(int s) { return s == 5 || s == 7 || s == 8 || s == 10 || s == 15 || s == 17; }
// Scenario stances that keep the character in place (locomotion is ignored).
static bool stanceLocksLegs(int s) { return s == 6 || s == 11 || s == 12; }

static bool actionUpperCapable(int a) {
    switch (a) {
        case CLIP_PUNCH_L: case CLIP_PUNCH_R: case CLIP_THROW: case CLIP_HIT_FRONT: case CLIP_HIT_BACK: case CLIP_FIRE_PISTOL:
        case CLIP_FIRE_RIFLE: case CLIP_RELOAD: case CLIP_WAVE: case CLIP_POINT: case CLIP_HANDS_UP: case CLIP_BLOCK: return true;
        default: return false;
    }
}
static bool actionIsDeath(int a) { return a == CLIP_DEATH_FRONT || a == CLIP_DEATH_BACK; }
static bool actionIsCar(int a) { return a >= CLIP_ENTER_CAR_L && a <= CLIP_EXIT_CAR_R; }
// Actions that the player can cancel by moving (after a fraction of the clip).
static float actionCancelAt(int a) {
    switch (a) {
        case CLIP_LAND: return 0.25f;
        case CLIP_GET_UP_FRONT: case CLIP_GET_UP_BACK: return 0.8f;
        case CLIP_HIT_FRONT: case CLIP_HIT_BACK: return 0.4f;
        case CLIP_STAGGER: return 0.6f;
        case CLIP_EXIT_CAR_L: case CLIP_EXIT_CAR_R: return 0.7f;
        case CLIP_HANDS_UP: case CLIP_CHEER: case CLIP_WAVE: case CLIP_POINT: case CLIP_SIT_BENCH: case CLIP_TALK: return 0.0f;
        default: return 2.f;
    }
}

struct GaitBand {
    Clip c;
    float speed, stride;   // stride = distance per cycle
};

static float stride(Clip c) {
    const ClipInfo& ci = clipInfo(c);
    return ci.speed * ci.duration;
}

static void rotateLocal(Pose& p, int b, quat q) { p.rot[b] = normalize(p.rot[b] * q); }

void sampleClipId(const Skeleton& skel, int ci, float t, Pose& out, u32 variationSeed);
static const int kRifleCarryClip = CLIP_COUNT + 0;   // internal clip id (see clips.cpp IC_RIFLE_CARRY)

// Hands on the steering wheel for this skeleton (absolute car interior geometry: rim radius 0.185 m centred 0.5 m
// ahead of and 0.4 m above the seat hip point, tilted towards the driver), turned by `steer` radians (+ = right).
static void driveHands(const Skeleton& sk, Pose& p, float steer) {
    const vec3 wc(0.f, 0.5f, 0.9f), wx(1.f, 0.f, 0.f), wy(0.f, 0.411f, 0.912f), wn(0.f, -0.912f, 0.411f);
    const float R = 0.185f;
    quat rotW = qaa(wn, -steer);
    const Bone ups[2] = {B_UPPERARM_L, B_UPPERARM_R}, lows[2] = {B_FOREARM_L, B_FOREARM_R}, ends[2] = {B_HAND_L, B_HAND_R};
    for (int s = 0; s < 2; s++) {
        float sx = s ? 1.f : -1.f;
        float a = sx * 1.1f + steer;
        vec3 radial = wx * sinf(a) + wy * cosf(a);
        vec3 target = wc + radial * (R + 0.03f) - vec3(0.f, 0.035f, 0.01f);
        quat qh, qu, qf;
        vec3 ph, pu, pf;
        boneModel(sk, p, ends[s], qh, ph);
        boneModel(sk, p, ups[s], qu, pu);
        vec3 pole = lerp(pu, target, 0.5f) + normalize(vec3(sx, -0.4f, -0.9f)) * 0.5f;
        solveTwoBoneIK(sk, p, ups[s], lows[s], ends[s], target, pole, 1.f);
        boneModel(sk, p, lows[s], qf, pf);
        p.rot[ends[s]] = normalize(conj(qf) * (rotW * qh));
    }
}

// Blend only the arms (clavicles down to the fingers) of `layer` onto `p`.
static void blendArms(Pose& p, const Pose& layer, float w) {
    static const u8 kArmBones[] = {B_CLAVICLE_L, B_UPPERARM_L, B_FOREARM_L, B_HAND_L, B_CLAVICLE_R, B_UPPERARM_R, B_FOREARM_R, B_HAND_R,
                                   B_FINGERS_L,  B_THUMB_L,    B_FINGERS_R, B_THUMB_R};
    for (u8 b : kArmBones) p.rot[b] = nlerp(p.rot[b], layer.rot[b], w);
}

}  // namespace detail

void Animator::init(const Skeleton* s, u32 variationSeed) {
    using namespace detail;
    skel = s;
    seed = variationSeed;
    time = hashToFloat(hash32(variationSeed ^ 0x51ED27u)) * 20.f;
    phase = hashToFloat(hash32(variationSeed + 17u));
    locoBlend = aimBlend = crouchBlend = airBlend = swimBlend = 0.f;
    action = -1;
    actionTime = 0.f;
    actionFinished = true;
    stance = prevStance = 0;
    stanceBlend = 1.f;
    speedS = leanS = reloadW = reloadT = airT = stanceTime = 0.f;
    fireT = 10.f;
    footL = footR = 0.f;
    snapW = 0.f;
    snapRate = 5.f;
    moveW = 0.f;
    dirS = vec2(0, 1);
    actionUpper = wasReloading = false;
    lastInAction = -1;
    extBlend = false;
    if (s) {
        legScale = skeletonLegScale(*s);
        styleF = skeletonStyle(*s);
        sampleClip(*s, CLIP_IDLE, time, pose, seed);
    } else {
        for (int b = 0; b < B_COUNT; b++) pose.rot[b] = quat();
        pose.rootOffset = vec3(0);
    }
    snap = pose;
}

void Animator::update(const AnimInput& in, float dt) {
    using namespace detail;
    if (!skel) return;
    const Skeleton& sk = *skel;
    dt = Clamp(dt, 0.f, 0.25f);
    time += dt;
    const float kFast = 1.f - expf(-dt * 10.f), kMed = 1.f - expf(-dt * 6.f);

    // ---------------------------------------------------------------- input smoothing
    float spdIn = Max(0.f, in.speed);
    speedS += (spdIn - speedS) * kMed;
    if (spdIn < 0.05f && speedS < 0.05f) speedS = 0.f;
    vec2 md = in.localMoveDir;
    float mdl = length(md);
    md = mdl > 1e-3f ? md / mdl : vec2(0, 1);
    dirS = lerp(dirS, md, kFast);
    float dl = length(dirS);
    dirS = dl > 1e-3f ? dirS / dl : vec2(0, 1);
    crouchBlend += ((in.crouch ? 1.f : 0.f) - crouchBlend) * kMed;
    airBlend += ((in.inAir ? 1.f : 0.f) - airBlend) * (1.f - expf(-dt * 8.f));
    swimBlend += ((in.swimming ? 1.f : 0.f) - swimBlend) * (1.f - expf(-dt * 4.f));
    bool aimingNow = in.aiming || (in.firing && (in.weaponKind == 1 || in.weaponKind == 2));
    aimBlend += ((aimingNow ? 1.f : 0.f) - aimBlend) * (1.f - expf(-dt * 12.f));
    airT = in.inAir ? airT + dt : 0.f;
    float leanTarget = Clamp(-in.turnRate * Min(speedS, 7.f) * 0.03f, -0.22f, 0.22f);
    leanS += (leanTarget - leanS) * (1.f - expf(-dt * 5.f));

    // ---------------------------------------------------------------- stance changes
    int st = Clamp(in.stance, 0, kStanceCount - 1);
    if (st != stance) {
        snap = pose;
        snapW = 1.f;
        snapRate = stanceIsVehicle(st) || stanceIsVehicle(stance) ? 4.f : 3.f;
        prevStance = stance;
        stance = st;
        stanceTime = 0.f;
        stanceBlend = 0.f;
        // entering a vehicle seat ends the entry clip. The game re-roots the ped at the seat facing the vehicle's
        // front at that moment, i.e. turned 90 degrees from the entry frame: express the captured pose in the new
        // frame (undo the clip's quarter turn, take the seated hip placement) so the crossfade has no spin or slide.
        if (stanceIsVehicle(st) && action >= 0 && actionIsCar(action)) {
            if (action == CLIP_ENTER_CAR_L || action == CLIP_ENTER_CAR_R) {
                float turn = action == CLIP_ENTER_CAR_L ? -kHalfPi : kHalfPi;
                snap.rot[B_PELVIS] = normalize(qz(turn) * snap.rot[B_PELVIS]);
                Pose seated;
                sampleClip(sk, kStanceClip[st], 0.f, seated, seed);
                snap.rootOffset = seated.rootOffset;
            }
            action = -1;
            actionFinished = true;
        }
    }
    stanceTime += dt;
    stanceBlend = Min(1.f, stanceBlend + dt * 3.f);

    // ---------------------------------------------------------------- action trigger
    bool trigger = in.action >= 0 && in.action < CLIP_COUNT && in.action != lastInAction;
    lastInAction = in.action;
    if (trigger) {
        if (!extBlend) {
            snap = pose;
            snapW = 1.f;
            snapRate = in.action == CLIP_HIT_FRONT || in.action == CLIP_HIT_BACK || in.action == CLIP_FIRE_PISTOL ||
                               in.action == CLIP_FIRE_RIFLE
                           ? 14.f
                           : 7.f;
            // get-ups start from a ragdoll: the previous animated pose is stale, so cut straight to the lying
            // pose (unless the game handed over the ragdoll pose with blendFrom(), which keeps its crossfade)
            if (in.action == CLIP_GET_UP_FRONT || in.action == CLIP_GET_UP_BACK) snapW = 0.f;
        }
        action = in.action;
        actionTime = 0.f;
        actionFinished = false;
        actionUpper = actionUpperCapable(action) && (speedS > 0.7f || in.aiming || stanceIsVehicle(stance));
    }

    // ---------------------------------------------------------------- base pose
    Pose base, tmp, tmp2;
    const float ls = Max(legScale, 0.3f);
    bool vehicleStance = stanceIsVehicle(stance);
    bool footIK = false;
    if (vehicleStance) {
        sampleClip(sk, kStanceClip[stance], stanceTime, base, seed);
        moveW = 0.f;
    } else {
        // ---- locomotion (forward gait bands by speed)
        const float v = speedS;
        float rate;   // cycles per second
        // forward component
        static const Clip bands[5] = {CLIP_IDLE, CLIP_WALK, CLIP_JOG, CLIP_RUN, CLIP_SPRINT};
        float bandSpeed[5];
        for (int i = 0; i < 5; i++) bandSpeed[i] = clipInfo(bands[i]).speed;
        int b1 = 1;
        while (b1 < 4 && v > bandSpeed[b1]) b1++;
        int b0 = b1 - 1;
        float wb = b0 == 0 ? 1.f : Saturate((v - bandSpeed[b0]) / Max(bandSpeed[b1] - bandSpeed[b0], 1e-3f));
        if (v > bandSpeed[4]) wb = 1.f;
        float strideF = b0 == 0 ? stride(CLIP_WALK) : Lerp(stride(bands[b0]), stride(bands[b1]), wb);
        // below walking speed: walk clip slowed down, blended with idle
        float walkW = b0 == 0 ? Saturate(v / 0.9f) : 1.f;
        float rateF = Max(v, b0 == 0 ? 0.85f : 0.f) / Max(strideF, 0.1f);
        // directional weights (forward, back, left, right)
        float cf = dirS.y, sf = dirS.x;
        float wF = Max(0.f, cf), wBk = Max(0.f, -cf), wR = Max(0.f, sf), wL = Max(0.f, -sf);
        float wsum = wF + wBk + wR + wL;
        wF /= wsum; wBk /= wsum; wR /= wsum; wL /= wsum;
        // crouch
        float cw = crouchBlend;
        float rateB = Max(v, 0.85f) / stride(CLIP_WALK_BACK), rateS = Max(v, 0.85f) / stride(CLIP_STRAFE_L);
        float rateC = Max(v, 0.6f) / stride(CLIP_CROUCH_WALK);
        rate = (wF * rateF + wBk * rateB + (wL + wR) * rateS) * (1.f - cw) + rateC * cw;
        rate = Min(rate, 2.4f) / ls;
        if (in.swimming) rate = Max(v, 0.5f) / stride(CLIP_SWIM) / ls;
        moveW = walkW;
        // turning on the spot: step the feet around instead of sliding them (sideways steps towards the turn)
        float turnStep = (1.f - Saturate(v / 0.4f)) * Saturate((fabsf(in.turnRate) - 0.8f) / 1.5f) * (1.f - crouchBlend) *
                         (in.swimming || in.inAir || stance != 0 ? 0.f : 1.f);
        if (v > 0.02f || in.swimming) phase += dt * rate;
        phase += dt * turnStep * 1.3f / ls;
        phase -= floorf(phase);

        // standing locomotion pose
        Pose idle;
        bool needIdle = walkW < 0.999f || cw > 0.001f;
        if (needIdle) sampleClip(sk, CLIP_IDLE, time, idle, seed);
        // now and then an idle look-around (per-character timing)
        if (needIdle && stance == 0 && !in.aiming) {
            const float period = 17.f + 6.f * hashToFloat(seed * 31u + 5u);
            float lp = time / period + hashToFloat(seed * 13u + 1u);
            float fr = lp - floorf(lp);
            float lookDur = clipInfo(CLIP_IDLE_LOOK).duration / period;
            float lw = sstep(0.f, 0.06f, fr) * (1.f - sstep(lookDur - 0.06f, lookDur, fr));
            if (lw > 0.001f) {
                sampleClip(sk, CLIP_IDLE_LOOK, Min(fr * period, clipInfo(CLIP_IDLE_LOOK).duration - 0.01f), tmp, seed);
                blendPoses(idle, tmp, lw, idle);
            }
        }
        if (cw < 0.999f) {
            Pose fwd;
            if (b0 == 0) sampleClip(sk, CLIP_WALK, phase * clipInfo(CLIP_WALK).duration, fwd, seed);
            else {
                sampleClip(sk, bands[b0], phase * clipInfo(bands[b0]).duration, tmp, seed);
                sampleClip(sk, bands[b1], phase * clipInfo(bands[b1]).duration, tmp2, seed);
                blendPoses(tmp, tmp2, wb, fwd);
            }
            Pose mv = fwd;
            float acc = wF;
            if (wBk > 0.001f) {
                sampleClip(sk, CLIP_WALK_BACK, phase * clipInfo(CLIP_WALK_BACK).duration, tmp, seed);
                acc += wBk;
                blendPoses(mv, tmp, wBk / acc, mv);
            }
            if (wL > 0.001f) {
                sampleClip(sk, CLIP_STRAFE_L, phase * clipInfo(CLIP_STRAFE_L).duration, tmp, seed);
                acc += wL;
                blendPoses(mv, tmp, wL / acc, mv);
            }
            if (wR > 0.001f) {
                sampleClip(sk, CLIP_STRAFE_R, phase * clipInfo(CLIP_STRAFE_R).duration, tmp, seed);
                acc += wR;
                blendPoses(mv, tmp, wR / acc, mv);
            }
            if (needIdle) blendPoses(idle, mv, walkW, base);
            else base = mv;
        } else {
            base = idle;
        }
        if (turnStep > 0.01f) {
            Clip sc = in.turnRate > 0.f ? CLIP_STRAFE_L : CLIP_STRAFE_R;
            sampleClip(sk, sc, phase * clipInfo(sc).duration, tmp, seed);
            blendPoses(base, tmp, turnStep * 0.45f, base);
            moveW = Max(moveW, turnStep);
        }
        if (cw > 0.001f) {
            Pose ci, cwk, cp;
            sampleClip(sk, CLIP_CROUCH_IDLE, time, ci, seed);
            sampleClip(sk, CLIP_CROUCH_WALK, phase * clipInfo(CLIP_CROUCH_WALK).duration, cwk, seed);
            blendPoses(ci, cwk, Saturate(v / 0.5f), cp);
            blendPoses(base, cp, cw, base);
        }
        // ---- scenario stances
        if (stance >= 4) {
            Clip sc = kStanceClip[stance];
            float sOff = hashToFloat(seed * 7u + 3u) * clipInfo(sc).duration;
            sampleClip(sk, sc, stanceTime + sOff, tmp, seed);
            float still = stanceLocksLegs(stance) ? 1.f : 1.f - Saturate((speedS - 0.25f) / 0.6f);
            if (stanceUpperWhileMoving(stance)) {
                blendUpperBody(base, tmp, 1.f, tmp2);
                blendPoses(tmp2, tmp, still, base);
            } else {
                blendPoses(base, tmp, still, base);
            }
            if (stanceLocksLegs(stance)) moveW = 0.f;
        }
        // ---- airborne
        if (airBlend > 0.001f) {
            sampleClip(sk, CLIP_FALL, airT, tmp, seed);
            blendPoses(base, tmp, airBlend, base);
        }
        // ---- swimming
        if (swimBlend > 0.001f) {
            Pose si, sw;
            sampleClip(sk, CLIP_SWIM_IDLE, time, si, seed);
            sampleClip(sk, CLIP_SWIM, phase * clipInfo(CLIP_SWIM).duration, sw, seed);
            blendPoses(si, sw, Saturate((v - 0.2f) / 0.6f), tmp);
            blendPoses(base, tmp, swimBlend, base);
        }
        footIK = !in.inAir && !in.swimming && stance != 6 && stance != 11 && stance != 12;
    }

    // ---------------------------------------------------------------- upper body layers
    if (!vehicleStance && swimBlend < 0.5f) {
        if (aimBlend > 0.001f) {
            Clip ac = in.weaponKind == 1 ? CLIP_AIM_PISTOL : in.weaponKind == 2 ? CLIP_AIM_RIFLE : in.weaponKind == 4 ? CLIP_THROW : CLIP_BLOCK;
            float at = ac == CLIP_THROW ? 0.4f : time;
            if (in.firing && (in.weaponKind == 1 || in.weaponKind == 2)) {
                Clip fc = in.weaponKind == 1 ? CLIP_FIRE_PISTOL : CLIP_FIRE_RIFLE;
                float fd = clipInfo(fc).duration;
                if (fireT >= fd) fireT = 0.f;
                sampleClip(sk, fc, fireT, tmp, seed);
                fireT += dt;
            } else {
                fireT = 10.f;
                sampleClip(sk, ac, at, tmp, seed);
            }
            // standing still: take the aiming legs (shooting stance) too
            float full = aimBlend * (1.f - moveW) * (1.f - crouchBlend) * (1.f - airBlend);
            if (full > 0.001f) blendPoses(base, tmp, full, base);
            blendUpperBody(base, tmp, aimBlend, base);
            // aim pitch on the spine (+ = up)
            float pitch = Clamp(in.aimPitch, -1.2f, 1.2f) * aimBlend;
            if (fabsf(pitch) > 1e-4f) {
                rotateLocal(base, B_SPINE1, qx(pitch * 0.2f));
                rotateLocal(base, B_SPINE2, qx(pitch * 0.35f));
                rotateLocal(base, B_CHEST, qx(pitch * 0.45f));
            }
        } else {
            fireT = 10.f;
        }
        // long guns are carried at the low ready while not aiming (arm layer; the spine keeps its gait motion)
        if (in.weaponKind == 2 && aimBlend < 0.999f && !in.swimming) {
            sampleClipId(sk, kRifleCarryClip, time, tmp, seed);
            blendArms(base, tmp, 1.f - aimBlend);
        }
        // reload layer
        if (in.reloading && !wasReloading) reloadT = 0.f;
        wasReloading = in.reloading;
        reloadW += ((in.reloading ? 1.f : 0.f) - reloadW) * kFast;
        if (in.reloading) reloadT += dt;
        if (reloadW > 0.001f && (in.weaponKind == 1 || in.weaponKind == 2)) {
            sampleClip(sk, CLIP_RELOAD, Min(reloadT, clipInfo(CLIP_RELOAD).duration), tmp, seed);
            blendUpperBody(base, tmp, reloadW, base);
        }
    }

    // ---------------------------------------------------------------- one-shot action
    Pose outp = base;
    if (action >= 0) {
        const ClipInfo& ai = clipInfo((Clip)action);
        // cancel by moving (get up, land, hits...) or by starting to fall
        bool cancel = false;
        if (!actionFinished && !actionIsDeath(action)) {
            float ca = actionCancelAt(action) * ai.duration;
            if (actionTime >= ca && speedS > 1.0f && !actionUpper) cancel = true;
            if (in.swimming && action != CLIP_SWIM) cancel = true;
            if (action == CLIP_LAND && speedS > 2.5f) cancel = true;   // running landings just keep running
        }
        if (!actionFinished) actionTime += dt;
        if (!actionFinished && (actionTime >= ai.duration || cancel)) {
            actionFinished = true;
            if (!actionIsDeath(action)) {
                // hand back to the base layers with a crossfade from the last displayed pose
                snap = pose;
                snapW = 1.f;
                snapRate = action == CLIP_JUMP_START ? 8.f : 4.f;
                action = -1;
            }
        }
        if (action >= 0) {
            sampleClip(sk, (Clip)action, Min(actionTime, ai.duration), tmp, seed);
            if (actionUpper) blendUpperBody(outp, tmp, 1.f, outp);
            else {
                outp = tmp;
                footIK = footIK && (action == CLIP_LAND || action == CLIP_PUNCH_L || action == CLIP_PUNCH_R || action == CLIP_HIT_FRONT ||
                                    action == CLIP_HIT_BACK || action == CLIP_THROW);
            }
        }
    }

    // ---------------------------------------------------------------- hands on the steering wheel (exact for any size)
    // While driving, AnimInput::localMoveDir.x carries the steering input (-1 left .. +1 right; 0 when unset).
    if (stance == 1 && (action < 0 || actionUpper)) {
        float steerIn = Clamp(in.localMoveDir.x, -1.f, 1.f);
        steerS += (steerIn - steerS) * (1.f - expf(-dt * 8.f));
        driveHands(sk, outp, steerS * 1.2f);
    } else {
        steerS = 0.f;
    }

    // ---------------------------------------------------------------- lean into turns
    if (!vehicleStance && fabsf(leanS) > 1e-4f && (action < 0 || actionUpper)) {
        outp.rot[B_PELVIS] = normalize(qy(leanS * 0.5f) * outp.rot[B_PELVIS]);
        rotateLocal(outp, B_SPINE2, qy(leanS * 0.3f));
        rotateLocal(outp, B_NECK, qy(-leanS * 0.5f));
    }

    // ---------------------------------------------------------------- foot IK
    float gl = footIK ? Clamp(in.groundOffsetL, -0.35f, 0.35f) : 0.f;
    float gr = footIK ? Clamp(in.groundOffsetR, -0.35f, 0.35f) : 0.f;
    footL += (gl - footL) * (1.f - expf(-dt * 14.f));
    footR += (gr - footR) * (1.f - expf(-dt * 14.f));
    if (fabsf(footL) > 0.003f || fabsf(footR) > 0.003f) {
        float drop = Min(0.f, Min(footL, footR));
        outp.rootOffset.z += drop;
        const Bone ups[2] = {B_THIGH_L, B_THIGH_R}, lows[2] = {B_CALF_L, B_CALF_R}, ends[2] = {B_FOOT_L, B_FOOT_R};
        float offs[2] = {footL - drop, footR - drop};
        for (int s = 0; s < 2; s++) {
            if (offs[s] < 0.002f) continue;
            quat qa, qk;
            vec3 pa, pk;
            boneModel(sk, outp, ends[s], qa, pa);
            boneModel(sk, outp, lows[s], qk, pk);
            vec3 target = pa + vec3(0, 0, offs[s]);
            vec3 pole = pk + rotate(qk, vec3(0, 0.4f, 0));
            solveTwoBoneIK(sk, outp, ups[s], lows[s], ends[s], target, pole, 1.f);
        }
    }

    // ---------------------------------------------------------------- crossfade from captured pose
    if (snapW > 0.f) {
        snapW = Max(0.f, snapW - dt * snapRate);
        float w = snapW * snapW * (3.f - 2.f * snapW);
        blendPoses(outp, snap, w, pose);
    } else {
        pose = outp;
    }
    locoBlend = moveW;
    extBlend = false;
}

void Animator::blendFrom(const Pose& from, float seconds) {
    snap = from;
    pose = from;
    snapW = 1.f;
    snapRate = 1.f / Max(seconds, 0.02f);
    extBlend = true;   // an action started by the next update keeps this crossfade
}

}  // namespace Anim
