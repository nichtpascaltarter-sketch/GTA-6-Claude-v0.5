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
    CLIP_BLOCK,       // 19 fighting guard (the guard of AnimInput::meleeKind, see stanceClipId)
    CLIP_BLOCK,       // 20 blocking guard
    CLIP_IDLE,        // 21 sit on the ground (IC_SIT_GROUND)
    CLIP_IDLE,        // 22 lie face down (IC_LIE_FRONT)
    CLIP_IDLE,        // 23 queue: standing with frequent idle variations
};
static const int kStanceCount = (int)(sizeof(kStanceClip) / sizeof(kStanceClip[0]));

static bool stanceIsVehicle(int s) { return s >= 1 && s <= 3; }
static bool stanceIsGuard(int s) { return s == 19 || s == 20; }
// Clip (public or internal id) that drives a stance; the fighting guards depend on the melee weapon in hand.
static int stanceClipId(int s, int meleeKind) {
    if (s == 19) return meleeKind == 1 ? IC_GUARD_KNIFE : (meleeKind == 2 ? IC_GUARD_BAT : IC_GUARD);
    if (s == 20) return meleeKind == 2 ? (int)IC_BLOCK_BAT : (int)CLIP_BLOCK;
    if (s == 21) return IC_SIT_GROUND;
    if (s == 22) return IC_LIE_FRONT;
    return kStanceClip[s];
}
// Dance style per ped.
static int danceClip(u32 seed) {
    static const int kDances[4] = {CLIP_DANCE, IC_DANCE2, IC_DANCE3, IC_DANCE4};
    return kDances[hash32(seed * 2654435761u + 91u) & 3u];
}
// Scenario stances whose upper body stays on while walking.
static bool stanceUpperWhileMoving(int s) { return s == 5 || s == 7 || s == 8 || s == 10 || s == 15 || s == 17 || s == 19 || s == 20; }
// Scenario stances that keep the character in place (locomotion is ignored).
static bool stanceLocksLegs(int s) { return s == 6 || s == 11 || s == 12 || s == 21 || s == 22; }

static bool actionUpperCapable(int a) {
    switch (a) {
        case CLIP_PUNCH_L: case CLIP_PUNCH_R: case CLIP_THROW: case CLIP_HIT_FRONT: case CLIP_HIT_BACK: case CLIP_FIRE_PISTOL:
        case CLIP_FIRE_RIFLE: case CLIP_RELOAD: case CLIP_WAVE: case CLIP_POINT: case CLIP_HANDS_UP: case CLIP_BLOCK:
        case CLIP_HOOK: case CLIP_UPPERCUT: case CLIP_BAT_SWING: case CLIP_BAT_OVERHEAD: case CLIP_KNIFE_SLASH: case CLIP_KNIFE_STAB:
        case CLIP_HIT_HEAD: case CLIP_HIT_BODY: case CLIP_COUNTER: return true;
        default: return false;
    }
}
static bool actionIsDeath(int a) { return a == CLIP_DEATH_FRONT || a == CLIP_DEATH_BACK; }
// One-shots that end lying on the ground and hold their last frame (until the game starts a get-up or a ragdoll).
static bool actionHoldsEnd(int a) { return actionIsDeath(a) || a == CLIP_KNOCKOUT || a == CLIP_TAKEDOWN_VICTIM; }
// Full-body one-shots that keep the terrain foot IK.
static bool actionKeepsFootIK(int a) {
    switch (a) {
        case CLIP_LAND: case CLIP_PUNCH_L: case CLIP_PUNCH_R: case CLIP_HIT_FRONT: case CLIP_HIT_BACK: case CLIP_THROW: case CLIP_HOOK:
        case CLIP_UPPERCUT: case CLIP_BAT_SWING: case CLIP_BAT_OVERHEAD: case CLIP_KNIFE_SLASH: case CLIP_KNIFE_STAB: case CLIP_DODGE_BACK:
        case CLIP_DODGE_L: case CLIP_DODGE_R: case CLIP_HIT_HEAD: case CLIP_HIT_BODY: case CLIP_COUNTER: return true;
        default: return false;
    }
}
static bool actionIsCar(int a) { return a >= CLIP_ENTER_CAR_L && a <= CLIP_EXIT_CAR_R; }
// Actions that the player can cancel by moving (after a fraction of the clip).
static float actionCancelAt(int a) {
    switch (a) {
        case CLIP_LAND: return 0.25f;
        case CLIP_GET_UP_FRONT: case CLIP_GET_UP_BACK: return 0.8f;
        case CLIP_HIT_FRONT: case CLIP_HIT_BACK: case CLIP_HIT_HEAD: return 0.4f;
        case CLIP_HIT_BODY: return 0.5f;
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

// Yaw of the pelvis (model space, rotation about +Z of its forward axis).
static float pelvisYaw(const Pose& p) {
    vec3 f = rotate(p.rot[B_ROOT] * p.rot[B_PELVIS], vec3(0, 1, 0));
    return atan2f(-f.x, f.y);
}

// Bind finger direction and palm normal of a hand (see skeleton.cpp: the palms face the thighs, thumbs forward).
static void handBindAxes(const Skeleton& sk, int s, vec3& fing, vec3& palm, float& palmLen) {
    vec3 fl = sk.bindLocalPos[s ? B_FINGERS_R : B_FINGERS_L];
    palmLen = length(fl);
    fing = palmLen > 1e-5f ? fl / palmLen : vec3(0, 0, -1);
    palm = normalize(s ? cross(vec3(0, 1, 0), fing) : cross(fing, vec3(0, 1, 0)));
}

// Put the left fist on the handle held in the right fist: `dist` along the handle from the right grip centre
// (- = towards the knob), `reversed` = overhand hold with the left thumb pointing back along the handle.
static void batLeftHand(const Skeleton& sk, Pose& p, float dist, bool reversed, float w) {
    vec3 fR, pR, fL, pL;
    float plR, plL;
    handBindAxes(sk, 1, fR, pR, plR);
    handBindAxes(sk, 0, fL, pL, plL);
    quat qh, qu, qf;
    vec3 ph, pu, pf;
    boneModel(sk, p, B_HAND_R, qh, ph);
    vec3 F = rotate(qh, fR), P = rotate(qh, pR), D = rotate(qh, vec3(0, 1, 0));
    vec3 G = ph + F * (kGripAlong * plR) + P * (kGripPalm * plR) + D * dist;
    vec3 DL = reversed ? -D : D;
    vec3 PL = cross(F, DL);
    quat want = quatFromTwoPairs(fL, pL, F, PL);
    vec3 W = G - F * (kGripAlong * plL) - PL * (kGripPalm * plL);
    // keep the elbow bending the way the clip has it
    boneModel(sk, p, B_UPPERARM_L, qu, pu);
    boneModel(sk, p, B_FOREARM_L, qf, pf);
    quat qhl;
    vec3 phl;
    boneModel(sk, p, B_HAND_L, qhl, phl);
    vec3 bend = pf - (pu + phl) * 0.5f;
    vec3 pole = pf + (length2(bend) > 1e-6f ? normalize(bend) : vec3(-1, -0.3f, -1)) * 0.3f;
    solveTwoBoneIK(sk, p, B_UPPERARM_L, B_FOREARM_L, B_HAND_L, lerp(phl, W, w), pole, 1.f);
    boneModel(sk, p, B_FOREARM_L, qf, pf);
    p.rot[B_HAND_L] = normalize(conj(qf) * nlerp(qhl, want, w));
}

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
    stanceClip = -1;
    gripW = gripD = 0.f;
    actYaw0 = 0.f;
    idleVar = -1;
    idleCount = 0;
    idleVarT = idleVarDur = idleVarW = 0.f;
    idleNext = 3.f + 6.f * hashToFloat(hash32(variationSeed * 31u + 7u));
    gestMode = 0;
    gestT = gestDur = gestR = gestL = palmR = palmL = beatS = phoneW = 0.f;
    tiltS = tiltTarget = tiltNext = autoNod = 0.f;
    browseW = browseL = grabW = 0.f;
    nodNext = 2.f + 2.f * hashToFloat(hash32(variationSeed * 57u + 3u));
    nodPhase = -1.f;
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

void Animator::update(const AnimInput& in, float dt, bool cheap) {
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
    int sClip = st == 9 ? danceClip(seed) : stanceClipId(st, in.meleeKind);
    if (st == stance && sClip != stanceClip && stanceClip >= 0 && !(action >= 0 && !actionFinished && !actionUpper)) {
        // weapon switch inside a fighting guard
        snap = pose;
        snapW = 1.f;
        snapRate = 6.f;
    }
    stanceClip = sClip;
    if (st != stance) {
        snap = pose;
        snapW = 1.f;
        snapRate = stanceIsVehicle(st) || stanceIsVehicle(stance) ? 4.f : (stanceIsGuard(st) && stanceIsGuard(stance) ? 12.f : 3.f);
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
    Pose tmpA;
    bool trigger = in.action >= 0 && in.action < CLIP_COUNT && in.action != lastInAction;
    lastInAction = in.action;
    if (trigger) {
        if (!extBlend) {
            snap = pose;
            snapW = 1.f;
            snapRate = in.action == CLIP_HIT_FRONT || in.action == CLIP_HIT_BACK || in.action == CLIP_FIRE_PISTOL ||
                               in.action == CLIP_FIRE_RIFLE || in.action == CLIP_HIT_HEAD || in.action == CLIP_HIT_BODY
                           ? 14.f
                           : (in.action >= CLIP_DODGE_BACK && in.action <= CLIP_DODGE_R ? 12.f : 7.f);
            // get-ups start from a ragdoll: the previous animated pose is stale, so cut straight to the lying
            // pose (unless the game handed over the ragdoll pose with blendFrom(), which keeps its crossfade)
            if (in.action == CLIP_GET_UP_FRONT || in.action == CLIP_GET_UP_BACK) snapW = 0.f;
        }
        action = in.action;
        actionTime = 0.f;
        actionFinished = false;
        actionUpper = actionUpperCapable(action) && (speedS > 0.7f || in.aiming || stanceIsVehicle(stance));
        if (actionUpper) {
            sampleClip(sk, (Clip)action, 0.f, tmpA, seed);
            actYaw0 = pelvisYaw(tmpA);
        }
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
        // ---- idle variations (crossed arms, hands in pockets, hand on hip, phone check, neck stretch) while
        //      standing around; more often in a queue
        {
            bool canVary = (stance == 0 || stance == 23) && speedS < 0.05f && !in.aiming && !in.crouch && !in.inAir && !in.swimming &&
                           (action < 0 || actionFinished) && in.weaponKind != 2 && !in.phoneCall;
            // listeners keep a listening posture going; speakers only shift onto a hip now and then
            if (in.listening && idleVar < 0 && canVary) idleNext = Min(idleNext, 1.2f);
            if (in.speaking && idleVar >= 0 && idleVar != IC_IDLE_HIP) idleVarDur = Min(idleVarDur, idleVarT + 0.3f);
            if (!canVary) {
                if (idleVar >= 0) idleVarDur = Min(idleVarDur, idleVarT + 0.25f);   // fade out now
                if (idleVar < 0) idleNext = Max(idleNext, 2.f);
            }
            if (idleVar < 0 && canVary) {
                idleNext -= dt;
                if (idleNext <= 0.f) {
                    u32 h = hash32(seed * 0x9E3779B1u + (u32)idleCount * 0x85EBCA6Bu);
                    float r = hashToFloat(h);
                    bool q = stance == 23;
                    static const int kVars[5] = {IC_IDLE_PHONE, IC_IDLE_CROSSARMS, IC_IDLE_POCKETS, IC_IDLE_HIP, IC_IDLE_STRETCH};
                    const float cumN[5] = {0.25f, 0.45f, 0.65f, 0.85f, 1.f}, cumQ[5] = {0.4f, 0.6f, 0.8f, 0.95f, 1.f};
                    int k = 0;
                    while (k < 4 && r > (q ? cumQ[k] : cumN[k])) k++;
                    idleVar = kVars[k];
                    if (in.listening) idleVar = r < 0.45f ? IC_IDLE_CROSSARMS : (r < 0.75f ? IC_IDLE_HIP : IC_IDLE_POCKETS);
                    if (in.speaking) idleVar = IC_IDLE_HIP;
                    idleVarT = 0.f;
                    idleVarDur = idleVar == IC_IDLE_STRETCH ? clipInfoId(IC_IDLE_STRETCH).duration : 5.f + 5.f * hashToFloat(hash32(h + 3u));
                    if (in.listening) idleVarDur += 4.f;
                    idleCount++;
                }
            }
            float target = 0.f;
            if (idleVar >= 0) {
                idleVarT += dt;
                target = idleVarT < idleVarDur - 0.6f ? 1.f : 0.f;
                if (idleVarT >= idleVarDur && idleVarW < 0.01f) {
                    idleVar = -1;
                    u32 h = hash32(seed * 747796405u + (u32)idleCount * 2891336453u);
                    idleNext = stance == 23 ? 2.f + 3.f * hashToFloat(h) : (in.speaking ? 4.f + 5.f * hashToFloat(h) : 6.f + 8.f * hashToFloat(h));
                }
            }
            idleVarW += (target - idleVarW) * (1.f - expf(-dt * (target > idleVarW ? 3.5f : 5.f)));
            if (idleVar >= 0 && idleVarW > 0.001f) {
                sampleClipId(sk, idleVar, idleVarT, tmp, seed);
                blendPoses(base, tmp, idleVarW * (1.f - moveW), base);
            }
        }
        if (stance >= 4 && stance != 23) {
            int sc = stanceClip;
            float sOff = stanceIsGuard(stance) ? 0.f : hashToFloat(seed * 7u + 3u) * clipInfoId(sc).duration;
            float tScale = stance == 9 ? 0.9f + 0.22f * hashToFloat(hash32(seed + 404u)) : 1.f;   // dance tempo per ped
            sampleClipId(sk, sc, stanceTime * tScale + sOff, tmp, seed);
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
        footIK = !in.inAir && !in.swimming && stance != 6 && stance != 11 && stance != 12 && stance != 21 && stance != 22;
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
            sampleClipId(sk, IC_RIFLE_CARRY, time, tmp, seed);
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

    // ---------------------------------------------------------------- conversation: gestures, listener cues, phone
    if (!cheap) conversation(in, dt, base);

    // ---------------------------------------------------------------- one-shot action
    Pose outp = base;
    if (action >= 0) {
        const ClipInfo& ai = clipInfo((Clip)action);
        // cancel by moving (get up, land, hits...) or by starting to fall
        bool cancel = false;
        if (!actionFinished && !actionHoldsEnd(action)) {
            float ca = actionCancelAt(action) * ai.duration;
            if (actionTime >= ca && speedS > 1.0f && !actionUpper) cancel = true;
            if (in.swimming && action != CLIP_SWIM) cancel = true;
            if (action == CLIP_LAND && speedS > 2.5f) cancel = true;   // running landings just keep running
        }
        if (!actionFinished) actionTime += dt;
        if (!actionFinished && (actionTime >= ai.duration || cancel)) {
            actionFinished = true;
            if (!actionHoldsEnd(action)) {
                // hand back to the base layers with a crossfade from the last displayed pose
                snap = pose;
                snapW = 1.f;
                snapRate = action == CLIP_JUMP_START ? 8.f : 4.f;
                action = -1;
            }
        }
        if (action >= 0) {
            sampleClip(sk, (Clip)action, Min(actionTime, ai.duration), tmp, seed);
            if (actionUpper) {
                // upper body over the moving legs; the clip's hip turn (relative to its first frame) goes to the spine
                float dy = wrapAngle(pelvisYaw(tmp) - actYaw0);
                blendUpperBody(outp, tmp, 1.f, outp);
                if (fabsf(dy) > 1e-3f) {
                    quat qp;
                    vec3 pp;
                    boneModel(sk, outp, B_PELVIS, qp, pp);
                    outp.rot[B_SPINE1] = normalize(conj(qp) * qz(dy) * qp * outp.rot[B_SPINE1]);
                }
            } else {
                outp = tmp;
                footIK = footIK && actionKeepsFootIK(action);
            }
        }
    }

    // ---------------------------------------------------------------- takedown: choke arm onto the victim's real neck
    {
        float gwT = 0.f;
        if (action == CLIP_TAKEDOWN_ATTACKER && !actionFinished && in.grabWeight > 0.f)
            gwT = Clamp(in.grabWeight, 0.f, 1.f) * sstep(0.12f, 0.35f, actionTime) * (1.f - sstep(2.45f, 2.63f, actionTime));
        grabW += (gwT - grabW) * (1.f - expf(-dt * 20.f));
        if (grabW > 0.01f && !cheap) {
            // the throat sits a little in front of and above the neck joint (same facing as the attacker)
            vec3 throat = in.grabTarget + vec3(0.f, 0.07f, 0.04f);
            auto forearmMiss = [&]() {
                quat qe, qw;
                vec3 pe, pw;
                boneModel(sk, outp, B_FOREARM_R, qe, pe);
                boneModel(sk, outp, B_HAND_R, qw, pw);
                vec3 seg = pw - pe;
                float u = Saturate(dot(throat - pe, seg) / Max(length2(seg), 1e-6f));
                return throat - (pe + seg * u);   // move the forearm so it crosses the throat
            };
            vec3 delta = forearmMiss();
            // a shorter victim: sink the hips (knees bend, feet stay planted) for most of the height difference
            float dz = Min(0.f, delta.z) * 0.7f * grabW;
            if (dz < -0.005f) {
                const Bone ups[2] = {B_THIGH_L, B_THIGH_R}, lows[2] = {B_CALF_L, B_CALF_R}, ends[2] = {B_FOOT_L, B_FOOT_R};
                quat fq[2];
                vec3 fp[2];
                for (int s = 0; s < 2; s++) boneModel(sk, outp, ends[s], fq[s], fp[s]);
                outp.rootOffset.z += dz;
                for (int s = 0; s < 2; s++) {
                    quat qk, qp;
                    vec3 pk, pp;
                    boneModel(sk, outp, lows[s], qk, pk);
                    boneModel(sk, outp, B_PELVIS, qp, pp);
                    vec3 pole = pk + rotate(qp, vec3((s ? 1.f : -1.f) * 0.2f, 1.f, 0.f)) * 0.4f;
                    solveTwoBoneIK(sk, outp, ups[s], lows[s], ends[s], fp[s], pole, 1.f);
                    boneModel(sk, outp, lows[s], qk, pk);
                    outp.rot[ends[s]] = normalize(conj(qk) * fq[s]);
                }
                delta = forearmMiss();
            }
            if (length2(delta) > 0.16f) delta = normalize(delta) * 0.4f;
            for (int s = 1; s >= 0; s--) {
                int up = s ? B_UPPERARM_R : B_UPPERARM_L, lo = s ? B_FOREARM_R : B_FOREARM_L, hb = s ? B_HAND_R : B_HAND_L;
                quat qu, qf, qh0;
                vec3 pu, pf, ph0;
                boneModel(sk, outp, up, qu, pu);
                boneModel(sk, outp, lo, qf, pf);
                boneModel(sk, outp, hb, qh0, ph0);
                vec3 bend = pf - (pu + ph0) * 0.5f;
                vec3 pole = pf + (length2(bend) > 1e-6f ? normalize(bend) : vec3(0, 1, 0)) * 0.3f;
                solveTwoBoneIK(sk, outp, (Bone)up, (Bone)lo, (Bone)hb, ph0 + delta, pole, grabW);
                boneModel(sk, outp, lo, qf, pf);
                outp.rot[hb] = normalize(conj(qf) * qh0);   // keep the hand's orientation
            }
        }
    }

    // ---------------------------------------------------------------- two-handed bat: left hand on the handle
    {
        float dist = -0.095f, gw = 0.f;
        bool rev = false;
        if (action >= 0 && !actionFinished && (action == CLIP_BAT_SWING || action == CLIP_BAT_OVERHEAD))
            gw = batGrip(action, actionTime, dist, rev);
        else if (stanceIsGuard(stance) && in.meleeKind == 2 && (action < 0 || actionFinished || actionUpper) && !vehicleStance)
            gw = batGrip(stanceClip, stanceTime, dist, rev) * (1.f - swimBlend);
        float k = 1.f - expf(-dt * 14.f);
        gripW += (gw - gripW) * k;
        gripD += (dist - gripD) * k;
        if (gripW > 0.01f && !cheap) batLeftHand(sk, outp, gripD, rev, Min(1.f, gripW * 1.05f));
    }

    // ---------------------------------------------------------------- hands on the steering wheel (exact for any size)
    // While driving, AnimInput::localMoveDir.x carries the steering input (-1 left .. +1 right; 0 when unset).
    if (stance == 1 && (action < 0 || actionUpper)) {
        float steerIn = Clamp(in.localMoveDir.x, -1.f, 1.f);
        steerS += (steerIn - steerS) * (1.f - expf(-dt * 8.f));
        if (!cheap) driveHands(sk, outp, steerS * 1.2f);
    } else {
        steerS = 0.f;
    }

    // ---------------------------------------------------------------- lean into turns
    if (!vehicleStance && fabsf(leanS) > 1e-4f && (action < 0 || actionUpper)) {
        outp.rot[B_PELVIS] = normalize(qy(leanS * 0.5f) * outp.rot[B_PELVIS]);
        rotateLocal(outp, B_SPINE2, qy(leanS * 0.3f));
        rotateLocal(outp, B_NECK, qy(-leanS * 0.5f));
    }

    // ---------------------------------------------------------------- foot IK (terrain probes + slope plane)
    float gl = footIK ? Clamp(in.groundOffsetL, -0.35f, 0.35f) : 0.f;
    float gr = footIK ? Clamp(in.groundOffsetR, -0.35f, 0.35f) : 0.f;
    footL += (gl - footL) * (1.f - expf(-dt * 14.f));
    footR += (gr - footR) * (1.f - expf(-dt * 14.f));
    // slope along the facing (the lateral slope is already in the probes): n = groundNormal (model space)
    vec3 gn = in.groundNormal;
    float gnl = length(gn);
    gn = gnl > 1e-4f && gn.z > 0.3f ? gn / gnl : vec3(0, 0, 1);
    slopeN = lerp(slopeN, vec2(gn.x, gn.y), 1.f - expf(-dt * 8.f));
    slopeS += ((footIK ? 1.f : 0.f) - slopeS) * (1.f - expf(-dt * 8.f));
    float slopeY = -slopeN.y / sqrtf(Max(0.1f, 1.f - length2(slopeN))) * slopeS;   // dz/dy of the ground
    if (!cheap && (fabsf(footL) > 0.003f || fabsf(footR) > 0.003f || fabsf(slopeY) > 0.01f)) {
        const Bone ups[2] = {B_THIGH_L, B_THIGH_R}, lows[2] = {B_CALF_L, B_CALF_R}, ends[2] = {B_FOOT_L, B_FOOT_R};
        float offs[2] = {footL, footR};
        vec3 fp[2];
        quat fq[2];
        for (int s = 0; s < 2; s++) {
            boneModel(sk, outp, ends[s], fq[s], fp[s]);
            offs[s] += Clamp(fp[s].y * slopeY, -0.3f, 0.3f);
        }
        float drop = Min(0.f, Min(offs[0], offs[1]));
        outp.rootOffset.z += drop;
        // tilt the feet with the slope (pitch about the lateral axis)
        quat tilt = qx(atanf(slopeY));
        for (int s = 0; s < 2; s++) {
            float o = offs[s] - drop;
            quat qa, qk;
            vec3 pa, pk;
            boneModel(sk, outp, ends[s], qa, pa);
            boneModel(sk, outp, lows[s], qk, pk);
            if (o > 0.002f) {
                vec3 target = pa + vec3(0, 0, o);
                vec3 pole = pk + rotate(qk, vec3(0, 0.4f, 0));
                solveTwoBoneIK(sk, outp, ups[s], lows[s], ends[s], target, pole, 1.f);
            }
            if (fabsf(slopeY) > 0.01f) {
                // foot model rotation = tilt * current (only for grounded feet: blend out as the foot lifts)
                float restZ = sk.bindLocalPos[B_PELVIS].z + sk.bindLocalPos[ups[s]].z + sk.bindLocalPos[lows[s]].z + sk.bindLocalPos[ends[s]].z;
                float grounded = 1.f - sstep(0.03f, 0.12f, fp[s].z - restZ);
                quat qc;
                vec3 pc;
                boneModel(sk, outp, lows[s], qc, pc);
                quat want = nlerp(qa, tilt * qa, grounded);
                outp.rot[ends[s]] = normalize(conj(qc) * want);
            }
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

    // ---------------------------------------------------------------- face: look-at, gaze, blinks, lip-sync jaw
    if (!cheap) faceOverlay(in, dt);
}

namespace detail {

// Mouth shape per viseme (Oculus order): jaw opening (0..1, used when the game gives no mouthOpen), upper lip pitch
// (+ protrude / - roll in), lower lip pitch (+ tuck back and up / - pout), corner yaw (+ round / - spread), corner
// pitch (+ up), tongue pitch (+ raise).
static const float kVisemeShape[15][6] = {
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f},             // sil
    {0.f, -0.14f, 0.16f, 0.02f, 0.f, 0.f},      // PP  lips pressed together
    {0.12f, 0.06f, 0.42f, -0.04f, 0.02f, 0.f},  // FF  lower lip under the upper teeth
    {0.16f, 0.02f, 0.06f, -0.04f, 0.f, 0.38f},  // TH  tongue tip up between the teeth
    {0.18f, 0.f, 0.04f, -0.06f, 0.f, 0.3f},     // DD
    {0.26f, 0.f, 0.f, -0.03f, 0.f, -0.1f},      // kk
    {0.1f, 0.26f, -0.24f, 0.22f, 0.f, 0.08f},   // CH  lips pushed out
    {0.05f, -0.03f, 0.04f, -0.18f, 0.05f, 0.1f},// SS  teeth together, lips spread
    {0.15f, 0.f, 0.02f, -0.04f, 0.f, 0.26f},    // nn
    {0.15f, 0.16f, -0.12f, 0.18f, 0.f, 0.05f},  // RR
    {0.7f, 0.02f, -0.04f, -0.06f, -0.02f, -0.05f},  // aa
    {0.38f, -0.02f, 0.f, -0.2f, 0.04f, 0.02f},  // E
    {0.24f, -0.05f, 0.02f, -0.28f, 0.06f, 0.04f},   // I
    {0.4f, 0.22f, -0.12f, 0.34f, 0.f, -0.03f},  // O
    {0.08f, 0.36f, -0.18f, 0.55f, 0.f, 0.f},    // U
};

// Pose the speech bones for a mouth shape (see kVisemeShape) and, when jaw >= 0, the jaw.
void applyMouthShape(Pose& p, const float* m, float jaw) {
    p.rot[B_LIP_UPPER] = normalize(p.rot[B_LIP_UPPER] * qx(m[1]));
    p.rot[B_LIP_LOWER] = normalize(p.rot[B_LIP_LOWER] * qx(m[2]));
    p.rot[B_LIP_CORNER_L] = normalize(p.rot[B_LIP_CORNER_L] * qz(-m[3]) * qx(m[4]));
    p.rot[B_LIP_CORNER_R] = normalize(p.rot[B_LIP_CORNER_R] * qz(m[3]) * qx(m[4]));
    p.rot[B_TONGUE] = normalize(p.rot[B_TONGUE] * qx(m[5]));
    if (jaw >= 0.f) p.rot[B_JAW] = qx(-Clamp(jaw, 0.f, 1.f) * 0.3f);
}

// Expressions: jaw, upper lip, lower lip, corner yaw (+ round / - stretch), corner pitch (+ up), lids (eye pitch
// offset: + wide open, - narrowed).
// ... brow raise (-1..1), brow knit (+ inner ends down / - inner ends up).
static const float kExprShape[7][8] = {
    {0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},                   // neutral
    {0.f, -0.02f, 0.f, -0.2f, 0.28f, -0.12f, 0.15f, -0.1f},     // smile (cheeks push the lids up a little)
    {0.f, 0.f, -0.1f, 0.06f, -0.2f, -0.14f, 0.1f, -0.9f},       // sad (inner brows up)
    {0.f, -0.08f, 0.06f, -0.06f, -0.06f, -0.2f, -0.35f, 1.f},   // angry (brows down and knitted)
    {0.18f, -0.03f, -0.04f, -0.18f, -0.08f, 0.16f, 0.8f, -0.5f},// fear
    {0.32f, 0.05f, -0.05f, 0.1f, 0.02f, 0.2f, 1.f, 0.f},        // surprise
    {0.1f, -0.12f, 0.04f, -0.22f, -0.1f, -0.32f, -0.2f, 0.8f},  // pain
};

void visemeShape(int v, float w, float* out) {
    for (int i = 0; i < 6; i++) out[i] = v >= 0 && v < 15 ? kVisemeShape[v][i] * w : 0.f;
}

}  // namespace detail

void Animator::faceOverlay(const AnimInput& in, float dt) {
    using namespace detail;
    const Skeleton& sk = *skel;
    bool dead = action >= 0 && actionFinished && (action == CLIP_DEATH_FRONT || action == CLIP_DEATH_BACK);
    bool out = action >= 0 && (action == CLIP_KNOCKOUT || (action == CLIP_TAKEDOWN_VICTIM && actionTime > 2.1f));
    float eyeYaw = 0.f, eyePitch = 0.f;
    // look-at: neck and head take what they can (limited), the eyes the rest
    float lwT = dead || out ? 0.f : Clamp(in.lookWeight, 0.f, 1.f);
    lookW += (lwT - lookW) * (1.f - expf(-dt * 4.f));
    if (lookW > 0.005f) {
        quat qh;
        vec3 ph;
        boneModel(sk, pose, B_HEAD, qh, ph);
        vec3 fwdH = rotate(qh, vec3(0, 1, 0));
        vec3 eyesP = ph + rotate(qh, vec3(0.f, 0.07f, 0.06f));
        vec3 d = in.lookAt - eyesP;
        float dl = length(d);
        if (dl > 0.05f) {
            d = d / dl;
            float dy = wrapAngle(atan2f(-d.x, d.y) - atan2f(-fwdH.x, fwdH.y));
            float dp = asinf(Clamp(d.z, -1.f, 1.f)) - asinf(Clamp(fwdH.z, -1.f, 1.f));
            float dyH = Clamp(dy, -1.1f, 1.1f), dpH = Clamp(dp, -0.45f, 0.35f);
            float w = lookW;
            pose.rot[B_NECK] = normalize(pose.rot[B_NECK] * qz(dyH * 0.4f * w) * qx(dpH * 0.35f * w));
            pose.rot[B_HEAD] = normalize(pose.rot[B_HEAD] * qz(dyH * 0.6f * w) * qx(dpH * 0.65f * w));
            eyeYaw = Clamp(dy - dyH, -0.45f, 0.45f) * w;
            eyePitch = Clamp(dp - dpH, -0.3f, 0.3f) * w;
        }
    }
    // idle gaze: small saccades between fixations
    gazeNext -= dt;
    if (gazeNext <= 0.f) {
        u32 h = hash32(seed * 747796405u + (u32)(time * 7.f) * 2891336453u);
        gazeTarget = vec2((hashToFloat(h) - 0.5f) * 0.3f, (hashToFloat(hash32(h)) - 0.5f) * 0.12f) * (1.f - 0.6f * lookW);
        gazeNext = 0.5f + 2.5f * hashToFloat(hash32(h + 7u));
    }
    gaze = lerp(gaze, gazeTarget, 1.f - expf(-dt * 35.f));
    // blinks (occasionally double); dead peds keep the lids mostly shut
    blinkNext -= dt;
    if (blinkNext <= 0.f && blinkT < 0.f) {
        u32 h = hash32(seed * 2654435761u + (u32)(time * 13.f));
        blinkT = 0.f;
        blinkNext = hashToFloat(h) < 0.15f ? 0.35f : 1.8f + 4.5f * hashToFloat(hash32(h));
    }
    float blink = 0.f;
    if (blinkT >= 0.f) {
        blinkT += dt;
        float t = blinkT;
        blink = t < 0.06f ? sstep(0.f, 0.06f, t) : (t < 0.09f ? 1.f : 1.f - sstep(0.09f, 0.26f, t));
        if (t > 0.26f) blinkT = -1.f;
    }
    if (dead) blink = 0.78f;
    if (out) blink = Max(blink, action == CLIP_KNOCKOUT ? sstep(0.05f, 0.3f, actionTime) : sstep(2.1f, 2.4f, actionTime));
    // facial expression: explicit, or from what the ped is doing (plus a per-ped resting mood)
    {
        int ex = Clamp(in.expression, -1, 6);
        float ew = Clamp(in.expressionWeight, 0.f, 1.f);
        if (ex < 0) {
            bool acting = action >= 0 && !actionFinished;
            ex = 0;
            ew = 1.f;
            if (dead || out) ew = 0.f;
            else if (acting && (action == CLIP_HIT_FRONT || action == CLIP_HIT_BACK || action == CLIP_HIT_HEAD || action == CLIP_HIT_BODY ||
                                action == CLIP_STAGGER || action == CLIP_TAKEDOWN_VICTIM))
                ex = 6;
            else if (stance == 4 || stance == 5) ex = 4;
            else if (stance == 9 || stance == 15 || stance == 16) ex = 1;
            else if (stance == 19 || stance == 20 || (acting && action >= CLIP_PUNCH_L && action <= CLIP_KICK) ||
                     (acting && action >= CLIP_HOOK && action <= CLIP_KNIFE_STAB) || action == CLIP_COUNTER)
                ex = 3;
            else if (in.aiming) {
                ex = 3;
                ew = 0.5f;
            } else if (stance == 7 || stance == 8) {
                ex = 1;
                ew = 0.3f;
            } else {
                // resting mood: most peds neutral-pleasant, some a little glum
                float mood = hashToFloat(hash32(seed * 2246822519u + 11u));
                ex = mood < 0.55f ? 1 : (mood < 0.8f ? 0 : 2);
                ew = ex == 1 ? 0.18f : 0.25f;
            }
        }
        float ke = 1.f - expf(-dt * 7.f);
        for (int i = 0; i < 8; i++) exprS[i] += (kExprShape[ex][i] * ew - exprS[i]) * ke;
    }
    // speech accents: brow pulses and nods (fast attack / release)
    {
        float kp = 1.f - expf(-dt * 18.f);
        browS += ((dead || out ? 0.f : Clamp(in.brow, -1.f, 1.f)) - browS) * kp;
        nodS += ((dead || out ? 0.f : Max(Clamp(in.nod, 0.f, 1.f), autoNod)) - nodS) * kp;
        if (fabsf(nodS) > 1e-3f) {
            pose.rot[B_NECK] = normalize(pose.rot[B_NECK] * qx(-0.05f * nodS));
            pose.rot[B_HEAD] = normalize(pose.rot[B_HEAD] * qx(-0.13f * nodS));
        }
        // + pulse raises; - pulse lowers a little and knits
        float raise = Clamp(exprS[6] + browS * (browS > 0.f ? 1.f : 0.4f), -1.f, 1.2f);
        float knit = Clamp(exprS[7] + Max(-browS, 0.f), -1.f, 1.f);
        if (fabsf(raise) + fabsf(knit) > 1e-3f) {
            pose.rot[B_BROW_L] = normalize(pose.rot[B_BROW_L] * qx(raise * 0.14f - Max(knit, 0.f) * 0.04f) * qy(knit * 0.18f));
            pose.rot[B_BROW_R] = normalize(pose.rot[B_BROW_R] * qx(raise * 0.14f - Max(knit, 0.f) * 0.04f) * qy(-knit * 0.18f));
        }
    }
    const float kLidClose = 0.66f;   // eye pitch that brings the upper lid down onto the lower one
    for (int s = 0; s < 2; s++) {
        int b = s ? B_EYE_R : B_EYE_L;
        float yaw = Clamp(gaze.x + eyeYaw, -0.5f, 0.5f), pitch = Clamp(gaze.y + eyePitch, -0.35f, 0.35f) + exprS[5];
        pitch = Lerp(pitch, -kLidClose, blink);
        pose.rot[b] = normalize(pose.rot[b] * qz(yaw) * qx(pitch));
    }
    // speech: viseme mouth shapes (lips, corners, tongue); the jaw comes from mouthOpen when the game provides it,
    // else from the visemes (or stays with the clip)
    float target[6] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    bool talking = !dead && !out && (in.viseme >= 0 || in.visemeNext >= 0);
    if (talking) {
        float a[6], b[6];
        float vw = Clamp(in.visemeWeight, 0.f, 1.f), bl = Clamp(in.visemeBlend, 0.f, 1.f);
        visemeShape(in.viseme, vw, a);
        visemeShape(in.visemeNext >= 0 ? in.visemeNext : in.viseme, vw, b);
        for (int i = 0; i < 6; i++) target[i] = Lerp(a[i], b[i], bl);
    }
    float km = 1.f - expf(-dt * 28.f);
    bool any = false;
    for (int i = 0; i < 6; i++) {
        mouth[i] += (target[i] - mouth[i]) * km;
        any = any || fabsf(mouth[i]) > 1e-4f;
    }
    // expressions add on top (half strength on the mouth while talking)
    float ek = talking ? 0.5f : 1.f;
    float comb[6] = {mouth[0], mouth[1] + exprS[1] * ek, mouth[2] + exprS[2] * ek, mouth[3] + exprS[3] * ek, mouth[4] + exprS[4] * ek, mouth[5]};
    any = any || fabsf(exprS[1]) + fabsf(exprS[2]) + fabsf(exprS[3]) + fabsf(exprS[4]) > 1e-4f;
    float jawE = exprS[0] * ek;
    float jaw = in.mouthOpen >= 0.f ? in.mouthOpen : (talking || fabsf(mouth[0]) > 1e-3f ? mouth[0] + jawE : (jawE > 1e-3f ? jawE : -1.f));
    if (any || jaw >= 0.f) applyMouthShape(pose, comb, jaw);
}

// Conversation body language on top of the body layers (before one-shot actions):
//  - speaking: gesture phrases (rest / right palm-up explaining / left / both hands open / beat-ready), hand strokes
//    on the beat pulses, head tilts between phrases (weight shifts come from the idle variations);
//  - listening: occasional nods and tilts (postures come from the idle variations);
//  - phone call: right hand at the ear, head tilted to it; the left hand keeps gesturing while talking.
void Animator::conversation(const AnimInput& in, float dt, Pose& p) {
    using namespace detail;
    const Skeleton& sk = *skel;
    bool busy = (action >= 0 && !actionFinished) || aimBlend > 0.05f || swimBlend > 0.5f || airBlend > 0.5f || stanceIsVehicle(stance) ||
                stanceIsGuard(stance) || stance == 7 || stance == 8 || stance == 12 || stance == 21 || stance == 22 || in.weaponKind == 2;
    const float k5 = 1.f - expf(-dt * 5.f);
    // ---- gesture phrases while speaking
    float amount = in.speaking && !busy ? Clamp(in.gestureAmount, 0.f, 1.5f) : 0.f;
    gestT += dt;
    if (amount > 0.f && gestT >= gestDur) {
        u32 h = hash32(seed * 0x27d4eb2du + (u32)(time * 10.f));
        float r = hashToFloat(h);
        gestMode = r < 0.22f ? 0 : (r < 0.47f ? 1 : (r < 0.62f ? 2 : (r < 0.8f ? 3 : 4)));
        gestT = 0.f;
        gestDur = 1.2f + 2.3f * hashToFloat(hash32(h + 1u));
    }
    float wR = 0.f, wL = 0.f, pR = 0.f, pL = 0.f;
    switch (amount > 0.f ? gestMode : -1) {
        case 1: wR = 1.f; pR = 1.f; wL = 0.15f; break;
        case 2: wL = 1.f; pL = 1.f; wR = 0.15f; break;
        case 3: wR = wL = 1.f; pR = pL = 1.f; break;
        case 4: wR = 1.f; pR = 0.15f; wL = 0.35f; pL = 0.3f; break;
        case 0: wR = 0.3f; break;   // resting hands still twitch on the beats
        default: break;
    }
    beatS += (Clamp(in.beat, 0.f, 1.f) * (amount > 0.f ? 1.f : 0.f) - beatS) * (1.f - expf(-dt * 25.f));
    if (gestMode == 0) wR *= beatS;
    // the phone hand is busy
    phoneW += ((in.phoneCall && !busy ? 1.f : 0.f) - phoneW) * (1.f - expf(-dt * 4.f));
    wR *= (1.f - phoneW);
    float amt = Min(amount, 1.f);
    gestR += (wR * amt - gestR) * k5;
    gestL += (wL * amt - gestL) * k5;
    palmR += (pR - palmR) * k5;
    palmL += (pL - palmL) * k5;
    quat qc, qh;
    vec3 pc, ph;
    if (gestR > 0.01f || gestL > 0.01f || phoneW > 0.01f) {
        boneModel(sk, p, B_CHEST, qc, pc);
        float sc = length(sk.bindLocalPos[B_FOREARM_R]) / 0.331f;   // arm size relative to the reference
        float big = 0.8f + 0.25f * amount;
        for (int s = 0; s < 2; s++) {
            float w = s ? gestR : gestL;
            if (w <= 0.01f) continue;
            float sx = s ? 1.f : -1.f;
            float palm = s ? palmR : palmL;
            float open = gestMode == 3 ? 1.f : 0.f;
            float ph0 = time * 2.4f + (float)s * 1.9f;
            vec3 local = vec3(sx * (0.15f + 0.07f * open) * big, 0.25f + 0.05f * palm, -0.2f + 0.04f * palm) * sc;
            local = local + vec3(0.02f * sinf(ph0), 0.015f * cosf(ph0 * 1.3f), 0.02f * sinf(ph0 * 0.7f)) * (sc * amount);   // phrase drift
            local = local + vec3(0.f, 0.03f, -0.075f) * (beatS * sc * big);                                                   // beat stroke
            vec3 target = pc + rotate(qc, local);
            int up = s ? B_UPPERARM_R : B_UPPERARM_L, lo = s ? B_FOREARM_R : B_FOREARM_L, hb = s ? B_HAND_R : B_HAND_L;
            quat qu, qf, qhd;
            vec3 pu, pf, phd;
            boneModel(sk, p, up, qu, pu);
            vec3 pole = lerp(pu, target, 0.5f) + rotate(qc, normalize(vec3(sx, -0.4f, -1.f))) * 0.4f;
            boneModel(sk, p, hb, qhd, phd);
            solveTwoBoneIK(sk, p, (Bone)up, (Bone)lo, (Bone)hb, target, pole, Min(w, 1.f));
            // hand: palm up (explaining) .. palm to the side (beats), fingers forward and a little out
            vec3 fing, palmB;
            float pl;
            handBindAxes(sk, s, fing, palmB, pl);
            vec3 F = rotate(qc, normalize(vec3(sx * 0.35f, 1.f, 0.05f)));
            vec3 P = rotate(qc, normalize(lerp(vec3(-sx, 0.f, 0.35f), vec3(-sx * 0.25f, 0.f, 1.f), palm)));
            P = normalize(P - F * dot(P, F));
            quat want = quatFromTwoPairs(fing, palmB, F, P);
            boneModel(sk, p, lo, qf, pf);
            p.rot[hb] = normalize(conj(qf) * nlerp(qhd, want, Min(w, 1.f) * 0.85f));
        }
        // ---- phone at the right ear
        if (phoneW > 0.01f) {
            boneModel(sk, p, B_HEAD, qh, ph);
            vec3 target = ph + rotate(qh, vec3(0.085f, 0.035f, -0.035f));
            quat qu, qf, qhd;
            vec3 pu, pf, phd;
            boneModel(sk, p, B_UPPERARM_R, qu, pu);
            boneModel(sk, p, B_HAND_R, qhd, phd);
            vec3 pole = lerp(pu, target, 0.5f) + normalize(vec3(0.7f, 0.2f, -1.f)) * 0.4f;
            solveTwoBoneIK(sk, p, B_UPPERARM_R, B_FOREARM_R, B_HAND_R, target, pole, phoneW);
            vec3 fing, palmB;
            float pl;
            handBindAxes(sk, 1, fing, palmB, pl);
            vec3 F = rotate(qh, normalize(vec3(-0.15f, -0.25f, 1.f))), P = rotate(qh, vec3(-1.f, 0.f, 0.f));
            P = normalize(P - F * dot(P, F));
            boneModel(sk, p, B_FOREARM_R, qf, pf);
            p.rot[B_HAND_R] = normalize(conj(qf) * nlerp(qhd, quatFromTwoPairs(fing, palmB, F, P), phoneW));
        }
    }
    // ---- browsing a phone at chest height: right hand (plus the left one supporting while standing), head down
    {
        float kb = 1.f - expf(-dt * 10.f);
        bool on = in.phoneBrowse && !in.phoneCall && !busy;
        browseW += ((on ? 1.f : 0.f) - browseW) * kb;
        browseL += ((on ? 1.f - Saturate((speedS - 0.3f) / 0.8f) : 0.f) - browseL) * kb;
        if (browseW > 0.01f) {
            boneModel(sk, p, B_CHEST, qc, pc);
            float sc = length(sk.bindLocalPos[B_FOREARM_R]) / 0.331f;
            vec3 phoneC = pc + rotate(qc, vec3(0.02f, 0.3f, -0.21f) * sc);
            for (int s = 1; s >= 0; s--) {
                float w = s ? browseW : browseW * browseL;
                if (w <= 0.01f) continue;
                float sx = s ? 1.f : -1.f;
                int up = s ? B_UPPERARM_R : B_UPPERARM_L, lo = s ? B_FOREARM_R : B_FOREARM_L, hb = s ? B_HAND_R : B_HAND_L;
                vec3 fing, palmB;
                float pl;
                handBindAxes(sk, s, fing, palmB, pl);
                // palm up towards the face, fingers forward (the other hand cradles the phone from its side)
                vec3 F = rotate(qc, normalize(vec3(-sx * 0.25f, 1.f, 0.35f)));
                vec3 P = rotate(qc, normalize(vec3(-sx * 0.35f, -0.45f, 0.85f)));
                P = normalize(P - F * dot(P, F));
                vec3 palmC = phoneC + rotate(qc, vec3(sx * 0.035f, -0.01f, -0.012f)) * sc;
                vec3 wrist = palmC - F * (0.55f * pl) - P * (0.16f * pl);
                quat qu, qf, qhd;
                vec3 pu, pf, phd;
                boneModel(sk, p, up, qu, pu);
                boneModel(sk, p, hb, qhd, phd);
                vec3 pole = lerp(pu, wrist, 0.5f) + rotate(qc, normalize(vec3(sx, -0.5f, -1.f))) * 0.4f;
                solveTwoBoneIK(sk, p, (Bone)up, (Bone)lo, (Bone)hb, wrist, pole, w);
                boneModel(sk, p, lo, qf, pf);
                p.rot[hb] = normalize(conj(qf) * nlerp(qhd, quatFromTwoPairs(fing, palmB, F, P), w));
            }
            // eyes on the screen (less while walking)
            float look = browseW * (0.75f + 0.25f * browseL);
            p.rot[B_NECK] = normalize(p.rot[B_NECK] * qx(-0.18f * look));
            p.rot[B_HEAD] = normalize(p.rot[B_HEAD] * qx(-0.3f * look));
        }
    }
    // ---- head tilts between phrases (speaker and listener), towards the phone on a call
    bool conv = (in.speaking || in.listening) && !busy;
    tiltNext -= dt;
    if (tiltNext <= 0.f) {
        u32 h = hash32(seed * 0x165667b1u + (u32)(time * 4.f));
        tiltTarget = conv ? (hashToFloat(h) - 0.5f) * 0.2f : 0.f;
        tiltNext = 1.4f + 2.2f * hashToFloat(hash32(h + 9u));
    }
    float tiltGoal = (conv ? tiltTarget : 0.f) + 0.1f * phoneW;
    tiltS += (tiltGoal - tiltS) * (1.f - expf(-dt * 2.5f));
    if (fabsf(tiltS) > 1e-3f) p.rot[B_HEAD] = normalize(p.rot[B_HEAD] * qy(tiltS));
    // ---- listener nods now and then (fed into the face overlay's nod)
    autoNod = 0.f;
    if (in.listening && !busy) {
        if (nodPhase < 0.f) {
            nodNext -= dt;
            if (nodNext <= 0.f) {
                nodPhase = 0.f;
                nodNext = 2.2f + 3.f * hashToFloat(hash32(seed * 97u + (u32)(time * 3.f)));
            }
        } else {
            nodPhase += dt;
            autoNod = 0.6f * sinf(kPi * Saturate(nodPhase / 0.55f));
            if (nodPhase > 0.55f) nodPhase = -1.f;
        }
    } else {
        nodPhase = -1.f;
    }
}

void Animator::blendFrom(const Pose& from, float seconds) {
    snap = from;
    pose = from;
    snapW = 1.f;
    snapRate = 1.f / Max(seconds, 0.02f);
    extBlend = true;   // an action started by the next update keeps this crossfade
}

}  // namespace Anim
