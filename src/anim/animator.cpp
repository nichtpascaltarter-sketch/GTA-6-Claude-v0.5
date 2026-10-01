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
// Standing habits (Animator::fidgetMask bits): held postures, then fidgets.
enum {
    FG_PHONE = 0, FG_CROSSARMS, FG_POCKETS, FG_HIP, FG_BEHIND, FG_CLASP, FG_POSTURES,
    FG_WATCH = FG_POSTURES, FG_SCRATCH, FG_TUG, FG_CHIN, FG_YAWN, FG_ARMS, FG_TAP, FG_ROCK, FG_STRETCH, FG_COUNT
};
static const int kPostureClip[FG_POSTURES] = {IC_IDLE_PHONE, IC_IDLE_CROSSARMS, IC_IDLE_POCKETS, IC_IDLE_HIP, IC_IDLE_BEHIND, IC_IDLE_CLASP};
static const int kFidgetClip[FG_COUNT - FG_POSTURES] = {IC_FIDGET_WATCH, IC_FIDGET_SCRATCH, IC_FIDGET_TUG, IC_FIDGET_CHIN, IC_FIDGET_YAWN,
                                                       IC_FIDGET_ARMS,  IC_FIDGET_TAP,     IC_FIDGET_ROCK, IC_IDLE_STRETCH};
// Arms a posture holds (bit 0 left, bit 1 right): fidgets leave them alone (a hand behind the back or in a pocket
// cannot come out through the body).
static const u8 kPostureArms[FG_POSTURES] = {3, 3, 3, 2, 3, 3};
// Arms a fidget takes over (bit 0 left, bit 1 right; bit 2 the clavicles as offsets) and its legs (bit 0 / 1).
static const u8 kFidgetArms[FG_COUNT - FG_POSTURES] = {1, 2, 3, 3, 2, 3, 0, 0, 4};
static const u8 kFidgetLegs[FG_COUNT - FG_POSTURES] = {0, 0, 0, 0, 0, 0, 2, 3, 0};

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

// blendPoses over the controller bones only: the derived bones (forearm roll, phalanges) take their rotations from
// the controllers, so their entries are just carried along (from a).
static void blendCtl(const Pose& a, const Pose& b, float w, Pose& out) {
    if (w <= 0.f) {
        if (&out != &a) out = a;
        return;
    }
    if (w >= 1.f) {
        if (&out != &b) out = b;
        return;
    }
    for (int i = 0; i < B_FIRST_DERIVED; i++) out.rot[i] = nlerp(a.rot[i], b.rot[i], w);
    if (&out != &a)
        for (int i = B_FIRST_DERIVED; i < B_COUNT; i++) out.rot[i] = a.rot[i];
    out.rootOffset = lerp(a.rootOffset, b.rootOffset, w);
}

// Gait clips mixed in step. Every gait clip strikes the left heel at phase 0 and the right at 0.5 and lifts each foot
// its duty later; mixed as they are, clips of different duty (the walk bands, a walk with a strafe, a jog with a run)
// lift a foot at different moments, so the mix half-lifts a foot that planting (reading the contacts from the mixed
// duty) still holds down, and it pops up at the release. Each clip's time is warped so that its contacts fall where the
// mix's do: one warp for the whole pose while the clip and the mix both have double support (walks) or both a flight
// (runs) - toe-offs at the same place in each half cycle - else, a walk mixed with a run, each leg on its own (the body
// keeps the plain phase).
static float gaitWarp(float ph, float d, float D) {
    const float e = D > 0.5f ? D - 0.5f : D, ec = d > 0.5f ? d - 0.5f : d;   // a toe-off within its half cycle
    const float h = ph < 0.5f ? 0.f : 0.5f, q = ph - h;
    return h + (q < e ? q * (ec / e) : ec + (q - e) * ((0.5f - ec) / (0.5f - e)));
}
static float footWarp(float ph, float d, float D, int s) {
    const float o = s ? 0.5f : 0.f;
    float q = ph - o;
    q -= floorf(q);
    float w = (q < D ? q * (d / D) : d + (q - D) * ((1.f - d) / (1.f - D))) + o;
    return w - floorf(w);
}
static void sampleGait(const Skeleton& sk, int id, float phase, float D, Pose& out, u32 seed) {
    const float dur = clipInfoId(id).duration, d = clipDuty(id);
    if (d < 0.05f || D < 0.05f || D > 0.95f || fabsf(d - D) < 1e-3f) {
        sampleClipId(sk, id, phase * dur, out, seed);
    } else if ((d - 0.5f) * (D - 0.5f) > 0.f && fabsf(d - 0.5f) > 0.01f && fabsf(D - 0.5f) > 0.01f) {
        sampleClipId(sk, id, gaitWarp(phase, d, D) * dur, out, seed);
    } else {
        sampleClipId(sk, id, phase * dur, out, seed);
        for (int s = 0; s < 2; s++) sampleClipLeg(sk, id, footWarp(phase, d, D, s) * dur, s, out);
    }
}

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

// Hands on the steering wheel for this skeleton: the vehicle's rim (AnimInput::wheelC / wheelN / wheelR) or, when
// the game gives none, a car's typical one (radius 0.185 m, centred 0.5 m ahead of and 0.4 m above the seat hip point,
// tilted towards the driver), turned by `steer` radians (+ = right; the game draws the rim turned by the same angle,
// Animator::wheelTurn).
static void driveHands(const Skeleton& sk, Pose& p, float steer, const AnimInput& in) {
    const vec3 wn0(0.f, -0.912f, 0.411f);   // the typical rim's column axis (the driving clip's hands are posed for it)
    vec3 wc(0.f, 0.5f, 0.9f), wn = wn0;
    float R = 0.185f;
    if (in.wheelR > 0.f && length2(in.wheelN) > 1e-6f) {
        wc = in.wheelC;
        wn = normalize(in.wheelN);
        R = in.wheelR;
    }
    const vec3 wx(1.f, 0.f, 0.f);
    vec3 wy = cross(wn, wx);
    wy = length2(wy) > 1e-8f ? normalize(wy) : vec3(0.f, 0.f, 1.f);
    // hands turn with the rim, and with its tilt when it differs from the typical one
    quat rotW = qaa(wn, -steer) * quatFromTo(wn0, wn);
    const Bone ups[2] = {B_UPPERARM_L, B_UPPERARM_R}, lows[2] = {B_FOREARM_L, B_FOREARM_R}, ends[2] = {B_HAND_L, B_HAND_R};
    for (int s = 0; s < 2; s++) {
        float sx = s ? 1.f : -1.f;
        float a = sx * 1.1f + steer;
        vec3 radial = wx * sinf(a) + wy * cosf(a);
        vec3 target = wc + radial * (R + 0.03f) + wn * 0.028f - wy * 0.0235f;   // the wrist behind and outside the rim
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

// Two-bone IK of a leg (thigh, calf, foot) whose parent (pelvis) model transform is known. The knee bends about its
// hinge (the thigh's local X axis: clips flex the calf about it), by exactly the angle that puts the ankle at the
// target's distance from the hip (half-angle form: precise for a nearly straight leg), then the leg is aimed at the
// target and swung about the hip -> target line so the knee points towards the pole. The foot keeps its model rotation.
static void legIK(const Skeleton& sk, Pose& p, int up, int lo, int end, quat Qp, vec3 Pp, vec3 target, vec3 pole) {
    vec3 A = Pp + rotate(Qp, sk.bindLocalPos[up]);
    quat Qa = Qp * p.rot[up];
    vec3 B = A + rotate(Qa, sk.bindLocalPos[lo]);
    quat Qb = Qa * p.rot[lo];
    vec3 C = B + rotate(Qb, sk.bindLocalPos[end]);
    quat Qc = Qb * p.rot[end];
    vec3 AT = target - A;
    float dT = length(AT);
    vec3 a = B - A, c = C - B;
    float la = length(a), lb = length(c);
    if (dT < 1e-5f || la < 1e-5f || lb < 1e-5f) return;
    // knee: the calf turns about the hinge so that |a + R c| = d
    vec3 m = normalize(rotate(Qa, vec3(1, 0, 0)));
    vec3 aP = m * dot(a, m), cP = m * dot(c, m), aQ = a - aP, cQ = c - cP;
    float na = length(aQ), nc = length(cQ);
    float reach = sqrtf(length2(aP + cP) + (na + nc) * (na + nc));
    float d = Clamp(dT, fabsf(la - lb) + 1e-4f, reach * 0.9995f);
    quat r1;
    if (na > 1e-5f && nc > 1e-5f) {
        float q = d * d - length2(aP + cP);   // |a_perp + R c_perp|^2 wanted
        float h = Saturate(((na + nc) * (na + nc) - q) / (4.f * na * nc));
        float want = -2.f * asinf(sqrtf(h));   // flexion bends the calf back: negative about +X
        float cur = atan2f(dot(cross(aQ, cQ), m), dot(aQ, cQ));
        r1 = quatAxisAngle(m, want - cur);
    }
    vec3 C1 = B + rotate(r1, c);
    // aim the chain at the target, then swing the knee towards the pole about the hip -> target axis
    vec3 u = AT / dT;
    quat r2 = quatFromTo(normalize(C1 - A), u);
    vec3 kb = rotate(r2, B - A), kp = pole - A;
    kb = kb - u * dot(kb, u);
    kp = kp - u * dot(kp, u);
    quat r3;
    if (length2(kb) > 1e-10f && length2(kp) > 1e-10f) {
        kb = normalize(kb);
        kp = normalize(kp);
        r3 = quatAxisAngle(u, atan2f(dot(cross(kb, kp), u), dot(kb, kp)));
    }
    quat rr = r3 * r2;
    quat Qa2 = normalize(rr * Qa), Qb2 = normalize(rr * r1 * Qb);
    p.rot[up] = normalize(conj(Qp) * Qa2);
    p.rot[lo] = normalize(conj(Qa2) * Qb2);
    p.rot[end] = normalize(conj(Qb2) * Qc);
}

// ------------------------------------------------------------------------------------------------
// Feet on the ground
//
// A foot in contact stays where it is in the world while the ped moves and turns: its footprint (heel point and yaw)
// is carried back through the root motion (speed along the move direction, turn rate) and the animated foot is moved
// onto it by a rigid correction about its contact pivot (the heel, rolling onto the ball as the heel lifts, so heel
// strike, foot flat and toe-off keep their roll), then the leg is IK'd. Contacts come from the gait phase while
// walking (touch-down at the heel strike, release at toe-off, after which the correction fades out early in the
// swing). While standing both feet stay down, and a foot the pose has moved away from (turning on the spot, feet
// coming together after a stop, a changed stance) steps over in a low arc, one foot at a time, with the pelvis
// shifting over the standing foot. Ground offsets under each foot lift / lower it (the pelvis drops to reach the
// lower one) and feet on the ground tilt with the slope.
static void footPlanting(Animator& A, const AnimInput& in, float dt, Pose& p, bool planting, bool terrain, float duty) {
    const Skeleton& sk = *A.skel;
    const Bone ends[2] = {B_FOOT_L, B_FOOT_R};
    const Bone ups[2] = {B_THIGH_L, B_THIGH_R}, lows[2] = {B_CALF_L, B_CALF_R};
    // ground under the feet (smoothed) and the slope plane
    float gl = terrain ? Clamp(in.groundOffsetL, -0.35f, 0.35f) : 0.f;
    float gr = terrain ? Clamp(in.groundOffsetR, -0.35f, 0.35f) : 0.f;
    float kg = 1.f - expf(-dt * 14.f);
    A.footL += (gl - A.footL) * kg;
    A.footR += (gr - A.footR) * kg;
    vec3 gn = in.groundNormal;
    float gnl = length(gn);
    gn = gnl > 1e-4f && gn.z > 0.3f ? gn / gnl : vec3(0, 0, 1);
    A.slopeN = lerp(A.slopeN, vec2(gn.x, gn.y), 1.f - expf(-dt * 8.f));
    A.slopeS += ((terrain ? 1.f : 0.f) - A.slopeS) * (1.f - expf(-dt * 8.f));
    float nz = sqrtf(Max(0.1f, 1.f - length2(A.slopeN)));
    float slopeY = -A.slopeN.y / nz * A.slopeS, slopeX = -A.slopeN.x / nz * A.slopeS;   // dz/dy and dz/dx of the ground
    // planting weight (a linear ramp: a weight short of 1 would leak part of a large correction); everything resets
    // once it is off
    A.plantOn = approach(A.plantOn, planting ? 1.f : 0.f, dt * (planting ? 4.f : 8.f));
    if (!planting && A.plantOn < 0.02f) {
        A.plantOn = 0.f;
        A.stepReq = -1;
        for (int s = 0; s < 2; s++) {
            A.planted[s] = false;
            A.stepT[s] = -1.f;
            A.plantCorr[s] = vec3(0);
            A.corrYaw[s] = 0.f;
            A.pinZ[s] = A.plantAge[s] = 0.f;
        }
    }
    // root motion of this update, in the new model space: world-fixed points move back by it and turn against it
    vec2 md = in.localMoveDir;
    float mdl = length(md);
    md = mdl > 1e-3f ? md / mdl : vec2(0, 1);
    const float spd = Max(0.f, in.speed);
    const vec3 d = vec3(md.x, md.y, 0.f) * (spd * dt);
    const float dpsi = in.turnRate * dt;
    const quat qBack = qz(-dpsi);
    for (int s = 0; s < 2; s++) {
        A.plantP[s] = rotate(qBack, A.plantP[s]) - d;
        A.plantYaw[s] = wrapAngle(A.plantYaw[s] - dpsi);
        A.stepFrom[s] = rotate(qBack, A.stepFrom[s]) - d;
        A.stepFromYaw[s] = wrapAngle(A.stepFromYaw[s] - dpsi);
    }
    // the animated feet: ankle, rotation, heel point, contact pivot (heel .. ball by wBall), yaw
    const float L = A.footHeel + A.footBall;   // heel -> ball on the ground
    const float scale = L / 0.197f;            // foot size relative to the male reference
    vec3 fp[2], heel[2], ball[2];
    quat fq[2];
    float fyaw[2], wBall[2];
    // pelvis model transform (root -> pelvis), the legs from it
    const quat qr = p.rot[B_ROOT];
    const vec3 pr = sk.bindLocalPos[B_ROOT];
    const quat qp = qr * p.rot[B_PELVIS];
    const vec3 pp0 = pr + rotate(qr, sk.bindLocalPos[B_PELVIS] + p.rootOffset);
    quat qcalf[2];
    vec3 knee[2];
    for (int s = 0; s < 2; s++) {
        vec3 hip = pp0 + rotate(qp, sk.bindLocalPos[ups[s]]);
        quat qt = qp * p.rot[ups[s]];
        knee[s] = hip + rotate(qt, sk.bindLocalPos[lows[s]]);
        qcalf[s] = qt * p.rot[lows[s]];
        fp[s] = knee[s] + rotate(qcalf[s], sk.bindLocalPos[ends[s]]);
        fq[s] = qcalf[s] * p.rot[ends[s]];
        vec3 F = rotate(fq[s], vec3(0, 1, 0));
        fyaw[s] = atan2f(-F.x, F.y);
        float pitch = asinf(Clamp(F.z, -1.f, 1.f));
        heel[s] = fp[s] + rotate(fq[s], vec3(0.f, -A.footHeel, -A.footAnkleH));
        ball[s] = fp[s] + rotate(fq[s], vec3(0.f, A.footBall, -A.footAnkleH));
        wBall[s] = Saturate((-pitch - 0.02f) / 0.07f);
    }
    auto pivotA = [&](int s) { return lerp(heel[s], ball[s], wBall[s]); };
    // planted pivot: the same point of the sole on a footprint (heel point h, yaw y)
    auto pivotP = [&](int s, vec3 h, float y) { return h + rotate(qz(y), vec3(0.f, wBall[s] * L, 0.f)); };
    // displayed heel / yaw of a foot (animation + current correction)
    auto shownHeel = [&](int s) {
        vec3 pa = pivotA(s);
        return pa + A.plantCorr[s] + rotate(qz(A.corrYaw[s]), heel[s] - pa);
    };
    const bool walking = A.moveW > 0.3f && duty > 0.05f;
    if (A.plantOn > 0.f) {
        for (int s = 0; s < 2; s++) {
            bool nearGround = pivotA(s).z < 0.05f * scale;
            if (walking) {
                // gait contacts: plant at the heel strike (where the foot is shown; early when a late swing already
                // meets the ground, e.g. a slow walk blended with the idle, or ground higher than the pose expects),
                // release at toe-off
                float ph = A.phase - (s ? 0.5f : 0.f);
                ph -= floorf(ph);
                float swingU = ph > duty ? (ph - duty) / Max(1.f - duty, 0.05f) : 0.f;
                bool touching = Min(heel[s].z, ball[s].z) < 0.004f * scale;
                // (a foot still on the ground just after the gait's toe-off stays planted until it actually lifts:
                // blended gaits, e.g. a diagonal walk, lift a little later than their mixed duty)
                bool contact = (ph > 0.004f && ph < duty - 0.01f && nearGround) || (A.planted[s] && ph <= 0.004f) ||
                               (swingU > 0.7f && touching) || (A.planted[s] && touching && swingU < 0.25f);
                A.stepT[s] = -1.f;   // a pending step gives way to the gait (its offset fades out)
                if (contact && !A.planted[s]) {
                    A.planted[s] = true;
                    vec3 h = shownHeel(s);
                    A.plantP[s] = vec3(h.x, h.y, 0.f);
                    A.plantYaw[s] = wrapAngle(fyaw[s] + A.corrYaw[s]);
                    A.footEvents |= 1u << s;
                } else if (!contact && A.planted[s]) {
                    A.planted[s] = false;
                }
            } else if (A.stepT[s] < 0.f && !A.planted[s] && nearGround) {
                // standing: a foot that comes down stays down where it is shown
                A.planted[s] = true;
                vec3 h = shownHeel(s);
                A.plantP[s] = vec3(h.x, h.y, 0.f);
                A.plantYaw[s] = wrapAngle(fyaw[s] + A.corrYaw[s]);
            }
        }
        // standing: step the foot that the pose has moved furthest from its footprint (one at a time)
        if (!walking && planting) {
            float err[2] = {0.f, 0.f};
            for (int s = 0; s < 2; s++)
                if (A.planted[s]) {
                    vec3 e = A.plantP[s] - heel[s];
                    err[s] = Max(length(vec2(e.x, e.y)) / (0.085f * scale), fabsf(wrapAngle(A.plantYaw[s] - fyaw[s])) / 0.3f);
                }
            int s = err[0] >= err[1] ? 0 : 1, o = 1 - s;
            float need = 1.f;
            if (A.stepReq >= 0) {
                // settling after a weight shift: the unloaded foot moves to where the pose now has it (a smaller
                // error than the one that forces a step), if it is far enough off to bother
                if (A.stepReq < 2) s = A.stepReq, o = 1 - s;
                need = 0.3f;
                if (err[s] <= need || !A.planted[s]) A.stepReq = -1;
            }
            if (err[s] > need && A.planted[o] && A.stepT[o] < 0.f && A.stepT[s] < 0.f) {
                vec3 e = A.plantP[s] - heel[s];
                float dist = length(vec2(e.x, e.y)), dy = fabsf(wrapAngle(A.plantYaw[s] - fyaw[s]));
                float turn = fabsf(in.turnRate);
                A.planted[s] = false;
                A.stepT[s] = 0.f;
                A.stepFrom[s] = A.plantP[s];
                A.stepFromYaw[s] = A.plantYaw[s];
                // quicker steps while turning faster; a settling foot slides over low
                A.stepDur[s] = Clamp(0.26f + 0.6f * dist / scale + 0.12f * dy - 0.04f * turn, 0.22f, 0.48f);
                A.stepLift[s] = need < 1.f ? Clamp(0.016f + 0.1f * dist / scale, 0.016f, 0.035f) * scale
                                           : Clamp(0.03f + 0.15f * dist / scale + 0.02f * dy, 0.03f, 0.075f) * scale;
                A.stepReq = -1;
            }
        } else {
            A.stepReq = -1;
        }
    }
    // corrections: planted feet sit on their footprints, stepping feet travel to the pose's footprint, free feet let
    // their last correction fade
    float lift[2] = {0.f, 0.f};
    float shiftT = 0.f;
    for (int s = 0; s < 2; s++) {
        vec3 pa = pivotA(s);
        if (A.stepT[s] >= 0.f) {
            A.stepT[s] += dt / A.stepDur[s];
            float u = Min(A.stepT[s], 1.f), e = u * u * (3.f - 2.f * u);
            // the foot heads for the pose's footprint, which stops moving for the last quarter (it lands still)
            if (u < 0.75f) {
                // turning: land ahead of the turn (about the root), so the foot stays down longer before its next step
                float lead = Clamp(in.turnRate * 0.28f, -0.45f, 0.45f);
                vec3 h = rotate(qz(lead), vec3(heel[s].x, heel[s].y, 0.f));
                A.stepTo[s] = vec3(h.x, h.y, 0.f);
                A.stepToYaw[s] = wrapAngle(fyaw[s] + lead);
            } else {
                A.stepTo[s] = rotate(qBack, A.stepTo[s]) - d;
                A.stepToYaw[s] = wrapAngle(A.stepToYaw[s] - dpsi);
            }
            vec3 h = lerp(A.stepFrom[s], A.stepTo[s], e);
            float y = A.stepFromYaw[s] + wrapAngle(A.stepToYaw[s] - A.stepFromYaw[s]) * e;
            vec3 pp = pivotP(s, h, y);
            A.plantCorr[s] = vec3(pp.x - pa.x, pp.y - pa.y, 0.f);
            A.corrYaw[s] = wrapAngle(y - fyaw[s]);
            lift[s] = A.stepLift[s] * sinf(kPi * u);
            shiftT = (s ? -1.f : 1.f) * 0.03f * scale * sinf(kPi * Min(1.f, u * 1.3f));   // weight over the other foot
            if (A.stepT[s] >= 1.f) {
                A.stepT[s] = -1.f;
                A.planted[s] = true;
                A.plantP[s] = A.stepTo[s];
                A.plantYaw[s] = A.stepToYaw[s];
                A.footEvents |= 1u << s;
            }
        } else if (A.planted[s]) {
            vec3 pp = pivotP(s, A.plantP[s], A.plantYaw[s]);
            A.plantCorr[s] = vec3(pp.x - pa.x, pp.y - pa.y, 0.f);
            A.corrYaw[s] = wrapAngle(A.plantYaw[s] - fyaw[s]);
            // too far from the pose (a fast turn, a shove): let go (standing feet wait longer for their step)
            float lim = walking ? 0.3f : 0.38f, limY = walking ? 0.75f : 1.15f;
            if (length2(A.plantCorr[s]) > lim * lim * scale * scale || fabsf(A.corrYaw[s]) > limY) A.planted[s] = false;
        } else if (Min(heel[s].z, ball[s].z) > 0.012f * scale || A.moveW < 0.3f) {
            // released: the correction fades once the foot is off the ground (a toe still on it would slide)
            float k = expf(-dt / 0.07f);
            A.plantCorr[s] = A.plantCorr[s] * k;
            A.corrYaw[s] *= k;
        }
    }
    A.stepShift += (shiftT - A.stepShift) * (1.f - expf(-dt * 10.f));
    // terrain heights under the feet (probed there, or extrapolated along the slope from probes below the hips)
    float offs[2] = {A.footL, A.footR};
    if (!in.footProbes)
        for (int s = 0; s < 2; s++) offs[s] += Clamp(fp[s].y * slopeY, -0.3f, 0.3f);
    float drop = Min(0.f, Min(offs[0], offs[1]));
    const float w = A.plantOn;
    // the sole on the ground: a planted foot's pivot sits exactly on it (proportions that differ from the clips'
    // reference leave it a few mm off), no foot sinks into it
    float pin[2] = {0.f, 0.f};
    for (int s = 0; s < 2; s++) {
        // toe tip on the sole under the (bent) toe bone
        const int tb = s ? B_TOE_R : B_TOE_L;
        vec3 toeJ = fp[s] + rotate(fq[s], sk.bindLocalPos[tb]);
        vec3 toe = toeJ + rotate(fq[s] * p.rot[tb], vec3(0.f, sk.boneLength[tb], -(A.footAnkleH + sk.bindLocalPos[tb].z)));
        float low = Min(Min(heel[s].z, ball[s].z), toe.z);
        float pz = pivotA(s).z;
        // (proportions and mixed clips leave the pose's sole a centimetre or two off the ground at times: a planted
        // foot is pulled onto it over its first 80 ms, and a lifted one lets go over 50 ms, not in a frame)
        if (A.planted[s]) {
            A.plantAge[s] += dt;
            float hold = Max(-pz, -low);
            A.pinZ[s] = A.plantAge[s] < 0.08f ? Lerp(A.pinZ[s], hold, Saturate(dt / Max(0.08f - A.plantAge[s] + dt, dt))) : hold;
        } else {
            A.plantAge[s] = 0.f;
            A.pinZ[s] *= expf(-dt / 0.05f);
        }
        pin[s] = Max(A.pinZ[s], -low) * w;
    }
    bool any = fabsf(drop) > 1e-4f || fabsf(slopeY) > 0.01f || fabsf(slopeX) > 0.01f || fabsf(A.stepShift) > 1e-4f;
    for (int s = 0; s < 2; s++)
        any = any || offs[s] - drop > 0.002f || length2(A.plantCorr[s]) * w * w > 1e-6f || fabsf(A.corrYaw[s]) * w > 0.002f || lift[s] > 1e-3f ||
              fabsf(pin[s]) > 5e-4f;
    // probe points for the next update: under the shown foot, ahead of a swinging one
    for (int s = 0; s < 2; s++) {
        vec3 pa = pivotA(s), mid = (heel[s] + ball[s]) * 0.5f;
        vec3 m = pa + A.plantCorr[s] * w + rotate(qz(A.corrYaw[s] * w), mid - pa);
        if (!A.planted[s] && A.stepT[s] < 0.f) m = m + vec3(md.x, md.y, 0.f) * Min(0.35f, spd * 0.2f);
        A.probeP[s] = vec3(m.x, m.y, 0.f);
    }
    // (a planted foot always goes through the IK: switching it off when the correction happens to be tiny would let
    // the leg's own solution differ from the IK's for a frame)
    if (!any && A.legSink < 1e-4f && !A.planted[0] && !A.planted[1]) return;
    p.rootOffset.z += drop;
    p.rootOffset.x += A.stepShift * w;
    vec3 targets[2];
    for (int s = 0; s < 2; s++) {
        vec3 pa = pivotA(s);
        targets[s] = pa + A.plantCorr[s] * w + rotate(qz(A.corrYaw[s] * w), fp[s] - pa);
        targets[s].z = fp[s].z + offs[s] + lift[s] * w + pin[s];
    }
    // a foot kept where the pose no longer has it (the body turned or shifted over it) may be out of reach of a
    // nearly straight leg: the pelvis comes down instead of the foot being dragged (at once, back up smoothly)
    vec3 pp = pr + rotate(qr, sk.bindLocalPos[B_PELVIS] + p.rootOffset);
    {
        float sink = 0.f;
        for (int s = 0; s < 2; s++) {
            vec3 hp = pp + rotate(qp, sk.bindLocalPos[ups[s]]);
            vec3 dd = targets[s] - hp;
            float L = (length(sk.bindLocalPos[lows[s]]) + length(sk.bindLocalPos[ends[s]])) * 0.994f;
            float h2 = L * L - dd.x * dd.x - dd.y * dd.y;
            sink = Max(sink, h2 > 0.f ? -dd.z - sqrtf(h2) : 0.08f * scale);
        }
        sink = Min(sink, 0.08f * scale) * w;
        A.legSink = sink > A.legSink ? sink : A.legSink + (sink - A.legSink) * (1.f - expf(-dt * 6.f));
        p.rootOffset.z -= A.legSink;
        pp = pp - rotate(qr, vec3(0.f, 0.f, A.legSink));
    }
    quat tilt = qx(atanf(slopeY)) * qy(-atanf(slopeX));
    for (int s = 0; s < 2; s++) {
        vec3 pa = pivotA(s);
        quat rc = qz(A.corrYaw[s] * w);
        vec3 target = targets[s];
        float grounded = 1.f - sstep(0.02f, 0.08f, pa.z);
        quat want = rc * fq[s];
        if (grounded > 0.f && (fabsf(slopeY) > 0.01f || fabsf(slopeX) > 0.01f)) want = nlerp(want, tilt * want, grounded);
        // the knee and the calf's forward axis give the pole
        vec3 pole = knee[s] + rotate(qcalf[s], vec3(0.f, 0.4f, 0.f));
        p.rot[ends[s]] = normalize(conj(qcalf[s]) * want);   // the IK keeps the foot's model rotation
        legIK(sk, p, ups[s], lows[s], ends[s], qp, pp, target, pole);
    }
}

// ------------------------------------------------------------------------------------------------
// Standing life

// Bones a standing posture / fidget offsets from the plain standing pose (Animator::restUp, same order).
static const u8 kUpperAdd[8] = {B_SPINE1, B_SPINE2, B_CHEST, B_NECK, B_HEAD, B_JAW, B_EYE_L, B_EYE_R};
static const u8 kArmChain[2][3] = {{B_UPPERARM_L, B_FOREARM_L, B_HAND_L}, {B_UPPERARM_R, B_FOREARM_R, B_HAND_R}};

static float quatAngleBetween(quat a, quat b) { return 2.f * acosf(Min(1.f, fabsf(dot(a, b)))); }

// A posture or fidget (clips authored from the plain standing pose) over the standing base: the arms in `arms`
// (bit 0 left, bit 1 right) as posed, hanging from the base's chest; while such an arm is still near the plain
// standing pose (a fidget starting or ending), the base's own arm offset (the weight shift's, a wide body's
// clearance) rides along, so the arm leaves and rejoins the base without a jump; `clear` swings the posed arms out
// (a posture coming or going: hands pass round the hips, not through them); bit 2: the clavicles as offsets (a shrug
// over whatever the arms do); the spine, neck, head, jaw and eyes as offsets, so the base's weight shift and
// counter-tilts stay; the legs in `legs` (bit 0 left, bit 1 right) as posed, with the pelvis offset (a tapping foot,
// rising onto the toes; planted feet keep their footprints).
static void layerStanding(const Animator& A, Pose& base, const Pose& layer, float w, int arms, int legs, float clear = 0.f, float back = 0.f) {
    if (w <= 1e-4f) return;
    static const u8 kArmRest[2][3] = {{B_CLAVICLE_L, B_FINGERS_L, B_THUMB_L}, {B_CLAVICLE_R, B_FINGERS_R, B_THUMB_R}};
    static const u8 kLeg[2][4] = {{B_THIGH_L, B_CALF_L, B_FOOT_L, B_TOE_L}, {B_THIGH_R, B_CALF_R, B_FOOT_R, B_TOE_R}};
    for (int s = 0; s < 2; s++) {
        if (arms & (1 << s)) {
            float away = Saturate(quatAngleBetween(layer.rot[kArmChain[s][0]], A.restArm[s][0]) / 0.3f +
                                  quatAngleBetween(layer.rot[kArmChain[s][1]], A.restArm[s][1]) / 0.5f);
            for (int j = 0; j < 3; j++) {
                int b = kArmChain[s][j];
                quat q = layer.rot[b];
                if (away < 1.f) q = normalize(q * nlerp(quat(), conj(A.restArm[s][j]) * base.rot[b], 1.f - away));
                base.rot[b] = nlerp(base.rot[b], q, w);
            }
            for (u8 b : kArmRest[s]) base.rot[b] = nlerp(base.rot[b], layer.rot[b], w);
            if (clear > 1e-4f || back > 1e-4f) {
                int ub = kArmChain[s][0];
                base.rot[ub] = normalize(qy(s ? -clear : clear) * qx(-back) * base.rot[ub]);
            }
        } else if (arms & 4) {
            int c = s ? B_CLAVICLE_R : B_CLAVICLE_L;
            base.rot[c] = normalize(base.rot[c] * nlerp(quat(), layer.rot[c], w));   // rest clavicles: identity
        }
        if (legs & (1 << s))
            for (u8 b : kLeg[s]) base.rot[b] = nlerp(base.rot[b], layer.rot[b], w);
    }
    for (int i = 0; i < 8; i++) {
        int b = kUpperAdd[i];
        base.rot[b] = normalize(base.rot[b] * nlerp(quat(), conj(A.restUp[i]) * layer.rot[b], w));
    }
    if (legs) base.rootOffset = base.rootOffset + (layer.rootOffset - A.restRoot) * w;
}

// Move a hand by `delta` (model space, scaled by w) with the arm's two-bone IK; the hand keeps its model rotation and
// the elbow its bend direction.
static void nudgeHand(const Skeleton& sk, Pose& p, int s, vec3 delta, float w, vec3 poleShift = vec3(0)) {
    if (w <= 1e-3f || length2(delta) * w * w < 1e-6f) return;
    const Bone up = s ? B_UPPERARM_R : B_UPPERARM_L, lo = s ? B_FOREARM_R : B_FOREARM_L, hb = s ? B_HAND_R : B_HAND_L;
    quat qu, qf, qh;
    vec3 pu, pf, ph;
    boneModel(sk, p, up, qu, pu);
    boneModel(sk, p, lo, qf, pf);
    boneModel(sk, p, hb, qh, ph);
    vec3 bend = pf - (pu + ph) * 0.5f;
    vec3 pole = pf + (length2(bend) > 1e-6f ? normalize(bend) : vec3(s ? 1.f : -1.f, -0.3f, 0.f)) * 0.3f + poleShift * w;
    solveTwoBoneIK(sk, p, up, lo, hb, ph + delta * w, pole, 1.f);
    boneModel(sk, p, lo, qf, pf);
    p.rot[hb] = normalize(conj(qf) * qh);
}

// Arm poses for the prop in hand (AnimInput::carry): the clip per arm (-1 none) and how much of the arm it takes while
// walking (hanging loads let part of the swing through; a held cup, case or umbrella keeps the arm still).
static void carryArms(const Animator& A, const AnimInput& in, int clip[2], float take[2]) {
    clip[0] = clip[1] = -1;
    take[0] = take[1] = 1.f;
    switch (in.carry) {
        case 1: clip[1] = IC_CARRY_CASE; break;
        case 2: case 4: clip[0] = IC_CARRY_HANG_L; take[0] = 0.55f; break;
        case 3:
            // the cup goes to the other hand while the phone is up or a phone / cigarette stance has the right hand (the
            // game draws it from the same rule)
            if (A.phoneW > 0.3f || A.browseW > 0.3f || rightHandBusy(in.stance)) clip[0] = IC_CARRY_CUP_L;
            else clip[1] = IC_CARRY_CUP_R;
            take[0] = take[1] = 0.92f;
            break;
        case 5:
            clip[1] = in.carryOpen ? IC_CARRY_UMBRELLA : IC_CARRY_HANG_R;
            take[1] = in.carryOpen ? 1.f : 0.55f;
            break;
        case 6: clip[1] = IC_CARRY_ROD; break;
        case 8: clip[1] = IC_CARRY_BOARD; take[1] = 0.9f; break;
        default: break;
    }
}

// Pick an index by weight (r in 0..1); -1 when every weight is 0.
static int pickWeighted(const float* w, int n, float r) {
    float tot = 0.f;
    for (int i = 0; i < n; i++) tot += Max(w[i], 0.f);
    if (tot <= 0.f) return -1;
    r *= tot;
    int last = -1;
    for (int i = 0; i < n; i++) {
        if (w[i] <= 0.f) continue;
        last = i;
        r -= w[i];
        if (r <= 0.f) return i;
    }
    return last;
}

// Weight from leg to leg while standing still: on the left or right leg (now and then both) for a while on each
// person's own timing, moving over in about a second (a critically damped spring, per-person rate). `want` >= 0
// holds a side for a posture / fidget (0 left, 0.5 both, 1 right). When most of the weight has moved, the unloaded
// foot is asked to settle into its new place (a small step, footPlanting).
static void weightShift(Animator& A, bool still, float want, float dt) {
    auto shiftTo = [&](float t) {
        A.standTarget = t;
        A.settleT = 2.5f / A.standK;   // ~70 % of the way
        A.stepReq = -1;
    };
    if (want >= 0.f) {
        if (fabsf(A.standTarget - want) > 0.01f) shiftTo(want);
        A.standNext = Max(A.standNext, 3.f);
    } else if (still) {
        A.standNext -= dt;
        if (A.standNext <= 0.f) {
            u32 h = hash32(A.seed * 0x3C6EF372u + (u32)(A.time * 5.f));
            float r = hashToFloat(h);
            bool centred = fabsf(A.standTarget - 0.5f) < 0.01f;
            shiftTo(centred ? (r < 0.5f ? 0.f : 1.f) : (r < 0.8f ? 1.f - A.standTarget : 0.5f));
            A.standNext = (4.f + 9.f * hashToFloat(hash32(h + 1u))) / Max(A.fidgetRate, 0.3f);
        }
    } else {
        A.standNext = Max(A.standNext, 1.5f);
    }
    // exact step of the spring (stable for any dt)
    float x = A.standW - A.standTarget, v = A.standV, k = A.standK, e = expf(-k * dt);
    float c = v + k * x;
    A.standW = A.standTarget + (x + c * dt) * e;
    A.standV = (v - k * c * dt) * e;
    if (A.settleT >= 0.f) {
        A.settleT -= dt;
        if (A.settleT < 0.f) {
            A.settleT = -1.f;
            if (still) A.stepReq = A.standTarget > 0.75f ? 0 : (A.standTarget < 0.25f ? 1 : 2);
        }
    }
}

// Breathing on the upper body at the person's own rate (12-18 breaths a minute at rest), faster and deeper for a
// while after running: the chest lifts, the shoulders rise (the arms keep hanging), the head stays level.
// `amount` scales the visible motion (the timing always runs).
static void breathe(Animator& A, Pose& p, float dt, float speed, float amount) {
    float run = Saturate((speed - 2.2f) / 4.5f);
    A.exertion += (run - A.exertion) * (1.f - expf(-dt / (run > A.exertion ? 18.f : 35.f)));
    A.breathPh += dt * A.breathRate * (1.f + 1.4f * A.exertion);
    A.breathPh -= floorf(A.breathPh);
    const float ph = A.breathPh;
    // inhale over 40 % of the cycle, a slower exhale, a short pause at the bottom
    A.breath = ph < 0.4f ? sstep(0.f, 0.4f, ph) : 1.f - sstep(0.4f, 0.92f, ph);
    float amp = amount * (1.f + 2.2f * A.exertion);
    if (amp < 1e-3f) return;
    float c = (A.breath - 0.5f) * amp;   // about the mean: the posture itself does not change
    rotateLocal(p, B_SPINE2, qx(0.006f * c));
    rotateLocal(p, B_CHEST, qx(0.016f * c));
    rotateLocal(p, B_NECK, qx(-0.012f * c));
    rotateLocal(p, B_HEAD, qx(-0.01f * c));
    for (int s = 0; s < 2; s++) {
        float d = (s ? -0.022f : 0.022f) * c * (1.f + 0.6f * A.exertion);
        int cb = s ? B_CLAVICLE_R : B_CLAVICLE_L, ub = s ? B_UPPERARM_R : B_UPPERARM_L;
        p.rot[cb] = normalize(p.rot[cb] * qy(d));
        p.rot[ub] = normalize(qy(-d) * p.rot[ub]);
    }
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
    hipTurn = 0.f;
    hipBack = false;
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
    // per-person motion from the seed alone (setCharacter refines it from the character)
    {
        u32 h = hash32(variationSeed * 0x2545F491u + 0x6Bu);
        float r = hashToFloat(h);
        gaitStyle = r < 0.4f ? GS_NEUTRAL : (r < 0.65f ? GS_RELAXED : (r < 0.85f ? GS_HURRIED : GS_TIRED));
        armSwingK = 0.82f + 0.36f * hashToFloat(hash32(h + 1u));
        postureLean = (hashToFloat(hash32(h + 2u)) - 0.5f) * 0.05f;
        headPitchAdd = (hashToFloat(hash32(h + 3u)) - 0.5f) * 0.08f;
        cadenceK = 0.96f + 0.08f * hashToFloat(hash32(h + 4u));
        energy = 0.3f + 0.4f * hashToFloat(hash32(h + 5u));
        lookiness = 0.3f + 0.5f * hashToFloat(hash32(h + 6u));
        fidgetRate = 0.7f + 0.6f * hashToFloat(hash32(h + 7u));
        fidgetMask = 0xffffffffu;
        armOut = heavyK = athleticK = 0.f;
        // standing: which leg the weight is on and when it next moves, how quickly; breathing rate and phase
        float rs = hashToFloat(hash32(h + 8u));
        standW = standTarget = rs < 0.42f ? 0.f : (rs < 0.84f ? 1.f : 0.5f);
        standV = 0.f;
        standNext = 1.f + 9.f * hashToFloat(hash32(h + 9u));
        standK = 4.5f + 2.5f * energy;
        settleT = -1.f;
        stepReq = -1;
        breathRate = 0.2f + 0.1f * hashToFloat(hash32(h + 10u));
        breathPh = hashToFloat(hash32(h + 11u));
        breath = exertion = 0.f;
        fidgetVar = -1;
        fidgetCount = 0;
        fidgetT = fidgetDur = fidgetW = 0.f;
        fidgetNext = 3.f + 12.f * hashToFloat(hash32(h + 12u));
        carryClip[0] = carryClip[1] = -1;
        carryW[0] = carryW[1] = 0.f;
        bagSwing[0] = bagSwing[1] = 1.f;
    }
    for (int k = 0; k < 2; k++) {
        plantP[k] = plantCorr[k] = stepFrom[k] = vec3(0);
        probeP[k] = vec3(k ? 0.11f : -0.11f, 0.f, 0.f);
        plantYaw[k] = corrYaw[k] = stepFromYaw[k] = stepLift[k] = stepToYaw[k] = 0.f;
        stepTo[k] = vec3(0);
        stepT[k] = -1.f;
        stepDur[k] = 0.35f;
        planted[k] = false;
        armRest[k] = quat();
    }
    plantOn = bodyLag = headLead = accS = accV = prevSpeed = stepShift = legSink = 0.f;
    footEvents = 0;
    if (s) {
        legScale = skeletonLegScale(*s);
        styleF = skeletonStyle(*s);
        sampleClip(*s, CLIP_IDLE, time, pose, seed);
        // foot geometry: heel 0.21, ball 0.52 of the foot length from the ankle (skeleton.cpp), ankle height = bind z
        footBall = Max(0.05f, s->bindLocalPos[B_TOE_L].y);
        footHeel = footBall * (0.21f / 0.52f);
        footAnkleH = s->bindLocalPos[B_ROOT].z + s->bindLocalPos[B_PELVIS].z + s->bindLocalPos[B_THIGH_L].z + s->bindLocalPos[B_CALF_L].z +
                     s->bindLocalPos[B_FOOT_L].z;
        Pose rest;
        sampleClip(*s, CLIP_IDLE, 0.f, rest, 0u);
        armRest[0] = rest.rot[B_UPPERARM_L];
        armRest[1] = rest.rot[B_UPPERARM_R];
        armOut = skeletonArmClearance(*s);
        // the plain standing pose the postures and fidgets are layered against (their clips start from it)
        sampleClipId(*s, IC_FIDGET_WATCH, 0.f, rest, 0u);
        for (int i = 0; i < 8; i++) restUp[i] = rest.rot[kUpperAdd[i]];
        restRoot = rest.rootOffset;
        for (int k = 0; k < 2; k++)
            for (int j = 0; j < 3; j++) restArm[k][j] = rest.rot[kArmChain[k][j]];
    } else {
        for (int b = 0; b < B_COUNT; b++) pose.rot[b] = quat();
        pose.rootOffset = vec3(0);
        for (int i = 0; i < 8; i++) restUp[i] = quat();
        restRoot = vec3(0);
        for (int k = 0; k < 2; k++)
            for (int j = 0; j < 3; j++) restArm[k][j] = quat();
    }
    snap = pose;
}

// Walking style and body language from the character: age, build, sex and role, plus a per-person roll.
void Animator::setCharacter(const CharacterDesc& d) {
    using namespace detail;
    u32 h = hash32(d.seed * 0x9E3779B1u + 0x51A7u);
    auto rnd = [&](u32 k) { return hashToFloat(hash32(h + k * 0x85EBCA6Bu)); };
    const float age = Clamp(d.age, 0.f, 1.f), wt = Clamp(d.weight, 0.f, 1.f);
    const bool fem = d.gender == FEMALE;
    // walking style: the old shuffle more and more from about 60, else by role and a personal roll
    float r = rnd(1);
    int st = GS_NEUTRAL;
    if (rnd(2) < sstep(0.66f, 0.9f, age)) st = GS_ELDERLY;
    else {
        float young = 1.f - sstep(0.12f, 0.5f, age);   // under ~25 .. over ~50
        // cumulative weights: neutral, relaxed, hurried, swagger, tired
        float w[5] = {0.34f, 0.24f, 0.16f, (fem ? 0.05f : 0.11f) * (0.5f + young), 0.07f + 0.1f * sstep(0.35f, 0.7f, age) + 0.12f * sstep(0.6f, 0.95f, wt)};
        switch (d.role) {
            case 1: w[0] = 0.5f; w[1] = 0.1f; w[2] = 0.12f; w[3] = 0.25f; w[4] = 0.05f; break;   // police
            case 2: w[0] = 0.15f; w[1] = 0.22f; w[2] = 0.05f; w[3] = 0.55f; w[4] = 0.03f; break; // gang
            case 3: w[0] = 0.3f; w[1] = 0.05f; w[2] = 0.5f; w[3] = 0.12f; w[4] = 0.05f; break;   // business
            case 4: w[0] = 0.25f; w[1] = 0.55f; w[2] = 0.03f; w[3] = 0.14f; w[4] = 0.05f; break; // beach
            case 5: w[0] = 0.4f; w[1] = 0.08f; w[2] = 0.1f; w[3] = 0.1f; w[4] = 0.32f; break;    // worker
            case 6: w[0] = 0.45f; w[1] = 0.05f; w[2] = 0.4f; w[3] = 0.05f; w[4] = 0.05f; break;  // medic
            default: break;
        }
        float tot = w[0] + w[1] + w[2] + w[3] + w[4], acc = 0.f;
        static const int kSt[5] = {GS_NEUTRAL, GS_RELAXED, GS_HURRIED, GS_SWAGGER, GS_TIRED};
        for (int k = 0; k < 5; k++) {
            acc += w[k] / tot;
            if (r <= acc || k == 4) {
                st = kSt[k];
                break;
            }
        }
    }
    gaitStyle = st;
    // energy: young and light people move with more spring, older and heavier ones less
    energy = Clamp(0.55f + 0.35f * (rnd(3) - 0.5f) - 0.35f * age - 0.2f * Max(0.f, wt - 0.5f), 0.05f, 1.f);
    armSwingK = Clamp(0.78f + 0.4f * rnd(4) + 0.12f * (energy - 0.5f), 0.7f, 1.25f);
    // posture: the skeleton already stoops with age (skeleton.cpp's kyphosis from ~55): only a little on top
    postureLean = (rnd(5) - 0.5f) * 0.05f + 0.04f * sstep(0.45f, 1.f, age) * (1.f - 0.6f * sstep(0.6f, 1.f, age)) + 0.02f * Max(0.f, wt - 0.6f);
    // build: heavy bodies walk on a wider base with more side-to-side sway, athletic ones with more spring
    heavyK = sstep(0.62f, 0.9f, wt);
    athleticK = sstep(0.6f, 0.8f, Clamp(d.muscle, 0.f, 1.f)) * (1.f - sstep(0.5f, 0.62f, wt));
    armSwingK = Clamp(armSwingK * (1.f + 0.1f * athleticK - 0.1f * heavyK), 0.65f, 1.3f);
    headPitchAdd = (rnd(6) - 0.5f) * 0.08f;
    cadenceK = 0.96f + 0.08f * rnd(7);
    // how much they look around (young and relaxed people more, the hurried less) and how fidgety they are
    lookiness = Clamp(0.25f + 0.5f * rnd(8) + (st == GS_RELAXED ? 0.15f : 0.f) - (st == GS_HURRIED ? 0.15f : 0.f), 0.05f, 1.f);
    fidgetRate = Clamp(0.6f + 0.8f * rnd(9) + 0.3f * (energy - 0.5f), 0.4f, 1.5f);
    // standing habits: a few postures and fidgets per person, likelier by age, sex, role, style and clothes (bits:
    // FG_* order below; the postures and fidgets in update)
    const float old = sstep(0.5f, 0.8f, age), young = 1.f - sstep(0.1f, 0.4f, age);
    {
        const bool pockets = d.bottom == BOT_JEANS || d.bottom == BOT_SHORTS || d.bottom == BOT_CARGO || d.bottom == BOT_SLACKS ||
                             d.bottom == BOT_POLICE || d.bottom == BOT_BAGGY || d.bottom == BOT_WORK;
        const bool hem = d.top != TOP_NONE && d.top != TOP_BIKINI && d.top != TOP_ONEPIECE;
        const bool watch = (d.extras & ACC_EXPLICIT) ? (d.extras & ACC_WATCH) != 0 : rnd(40) < 0.5f;
        const float biz = d.role == 3 ? 1.f : 0.f, cop = d.role == 1 ? 1.f : 0.f;
        float p[FG_COUNT];
        p[FG_PHONE] = 0.45f + 0.35f * young - 0.35f * old;
        p[FG_CROSSARMS] = 0.4f + 0.2f * cop;
        p[FG_POCKETS] = pockets ? 0.4f + (fem ? 0.f : 0.15f) : 0.f;
        p[FG_HIP] = 0.15f + (fem ? 0.3f : 0.f);
        p[FG_BEHIND] = 0.08f + 0.5f * old + 0.35f * cop;
        p[FG_CLASP] = 0.12f + (fem ? 0.18f : 0.f) + 0.2f * old + 0.2f * biz;
        p[FG_WATCH] = watch ? 0.45f + 0.3f * biz : 0.f;
        p[FG_SCRATCH] = 0.35f;
        p[FG_TUG] = hem ? 0.2f + 0.25f * heavyK + (fem ? 0.1f : 0.f) : 0.f;
        p[FG_CHIN] = 0.25f + 0.1f * old;
        p[FG_YAWN] = 0.12f + (st == GS_TIRED ? 0.45f : 0.f) + 0.1f * old;
        p[FG_ARMS] = 0.15f + 0.2f * young - 0.12f * old;
        p[FG_TAP] = 0.2f + 0.3f * energy + (st == GS_HURRIED ? 0.2f : 0.f);
        p[FG_ROCK] = 0.15f + (fem ? 0.f : 0.1f) + 0.1f * cop;
        p[FG_STRETCH] = 0.3f + 0.1f * old;
        u32 mask = 0;
        for (int k = 0; k < FG_COUNT; k++)
            if (rnd(20 + (u32)k) < p[k]) mask |= 1u << k;
        // at least one posture and two fidgets
        if (!(mask & ((1u << FG_POSTURES) - 1u))) mask |= 1u << (p[FG_POCKETS] > 0.f && rnd(60) < 0.5f ? FG_POCKETS : FG_CROSSARMS);
        auto fidgets = [&]() {
            int n = 0;
            for (int k = FG_POSTURES; k < FG_COUNT; k++) n += (mask >> k) & 1u;
            return n;
        };
        for (u32 tries = 0; tries < 6 && fidgets() < 2; tries++) {
            int k = FG_POSTURES + (int)(hash32(h + 70u + tries) % (u32)(FG_COUNT - FG_POSTURES));
            if (p[k] > 0.f) mask |= 1u << k;
        }
        if (fidgets() == 0) mask |= 1u << FG_SCRATCH;
        fidgetMask = mask;
    }
    // this body's skin where posed hands rest on it (the clips have the reference body's): rays out from inside its
    // signed distance model (torso and legs) at the heights the clips use
    for (int i = 0; i < 3; i++) skinP[i] = vec3(0);
    if (skel) {
        BodyDims D;
        computeDims(d, D);
        BuildCtx bc;
        bc.d = &d;
        bc.D = &D;
        bc.sk = skel;
        addBodyPrims(bc);
        const u32 mk = MK_TORSO | MK_LEG_L | MK_LEG_R;
        const float s = D.s;
        const vec3 from[3] = {vec3(0.f, D.J[B_THIGH_R].y + 0.015f * s, D.J[B_THIGH_R].z + 0.1f * s), vec3(0.f, 0.f, D.zHip + 0.07f * s),
                              vec3(0.06f * s, 0.f, D.zWaist + 0.05f * s)};
        const vec3 dir[3] = {vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 1, 0)};
        for (int i = 0; i < 3; i++) skinP[i] = from[i] + dir[i] * bc.sdf.castOut(from[i], dir[i], mk, 0.5f) - D.J[B_PELVIS];
    }
    // a tote bag on one shoulder: that arm swings less (a crossbody bag a little less)
    bagSwing[0] = bagSwing[1] = 1.f;
    if (d.bag == BAG_TOTE || d.bag == BAG_CROSSBODY) bagSwing[bagSide(d) & 1] = d.bag == BAG_TOTE ? 0.65f : 0.85f;
    // breathing: 12-18 a minute, a little quicker for heavy and older people; weight shifts: quicker when energetic
    breathRate = Clamp(0.2f + 0.08f * rnd(10) + 0.02f * heavyK + 0.02f * old, 0.2f, 0.3f);
    standK = 4.2f + 2.8f * energy;
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
    // the smoothed direction turns towards the input round the circle (a straight lerp of an exact reversal, e.g.
    // backing off while aiming, would shrink to nothing and, renormalized, never flip: the feet would slide)
    {
        float aCur = atan2f(dirS.x, dirS.y), da = wrapAngle(atan2f(md.x, md.y) - aCur);
        if (fabsf(da) > 3.1f) da = 3.1f;   // straight back: turn one way
        aCur += da * kFast;
        dirS = vec2(sinf(aCur), cosf(aCur));
    }
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
    float locoDuty = 0.f;
    if (vehicleStance) {
        sampleClip(sk, kStanceClip[stance], stanceTime, base, seed);
        moveW = 0.f;
    } else {
        // ---- locomotion (forward gait bands by speed): idle, this person's walk style (slow / normal / brisk), the
        //      easy jog, jog, run and sprint; strides scale with the leg length, cadence varies a little per person
        const float v = speedS;
        float rate;   // cycles per second
        const int kBands = 8;
        const int gs = Clamp(gaitStyle, 0, GS_COUNT - 1);
        const int bands[kBands] = {CLIP_IDLE, gaitClip(gs, 0), gaitClip(gs, 1), gaitClip(gs, 2), IC_JOG_SLOW, CLIP_JOG, CLIP_RUN, CLIP_SPRINT};
        float bandSpeed[kBands], bandStride[kBands], bandDuty[kBands];
        for (int i = 0; i < kBands; i++) {
            const ClipInfo& bi = clipInfoId(bands[i]);
            bandSpeed[i] = i ? bi.speed : 0.f;
            bandStride[i] = bi.speed * bi.duration;
            bandDuty[i] = clipDuty(bands[i]);
        }
        int b1 = 1;
        while (b1 < kBands - 1 && v > bandSpeed[b1]) b1++;
        int b0 = b1 - 1;
        float wb = b0 == 0 ? 1.f : Saturate((v - bandSpeed[b0]) / Max(bandSpeed[b1] - bandSpeed[b0], 1e-3f));
        if (v > bandSpeed[kBands - 1]) wb = 1.f;
        float strideF = b0 == 0 ? bandStride[1] : Lerp(bandStride[b0], bandStride[b1], wb);
        // below the slow walk: the slow walk at its own cadence, blended with idle (shorter steps, same foot speed)
        float walkW = b0 == 0 ? Saturate(v / bandSpeed[1]) : 1.f;
        float rateF = (b0 == 0 ? bandSpeed[1] : v) / Max(strideF, 0.1f);
        float dutyF = b0 == 0 ? bandDuty[1] : Lerp(bandDuty[b0], bandDuty[b1], wb);
        // directional weights (forward, back, left, right), velocity-matched: the clips share one phase rate, each
        // moves its planted foot a stride per cycle along its own axis, so each axis's share of the rate (cycles it
        // needs for its component of the velocity) is its weight, and the rate is their sum (a diagonal runs the phase
        // faster than either clip alone; a plain L1 mix would leave the blended foot short of the ground speed)
        float cf = dirS.y, sf = dirS.x;
        float cw = crouchBlend;
        // ---- the legs turned into the travel direction: moving diagonally or across, the hips - and with them the
        //      forward gait - turn up to 46-57 deg towards it while the trunk keeps facing ahead (the aim; turned back
        //      after the layers below), so the legs walk and run along the direction and the strafe / backward clips
        //      only cover what is left. Well behind (beyond 117 deg; forwards again within 100) the legs back-pedal
        //      along it instead.
        {
            float th = atan2f(dirS.x, dirS.y);   // + = to the right
            if (hipBack ? fabsf(th) < 1.75f : fabsf(th) > 2.05f) hipBack = !hipBack;
            float rel = hipBack ? wrapAngle(th - kPi) : th;
            float maxT = Lerp(0.8f, 1.f, Saturate((v - 1.6f) / 1.4f));
            bool can = !stanceIsGuard(stance) && !in.swimming && !in.inAir;
            float target = can ? Clamp(rel, -maxT, maxT) * Saturate(v / 0.6f) * (1.f - cw) : 0.f;
            hipTurn += (target - hipTurn) * (1.f - expf(-dt * 7.f));
            if (fabsf(hipTurn) < 1e-4f) hipTurn = 0.f;
            cf = cosf(th - hipTurn);
            sf = sinf(th - hipTurn);
        }
        float rateB = Max(v, 0.85f) / stride(CLIP_WALK_BACK), rateS = Max(v, 0.85f) / stride(CLIP_STRAFE_L);
        float rateC = Max(v, 0.6f) / stride(CLIP_CROUCH_WALK);
        float wF = Max(0.f, cf) * rateF * cadenceK, wBk = Max(0.f, -cf) * rateB, wR = Max(0.f, sf) * rateS, wL = Max(0.f, -sf) * rateS;
        float wsum = Max(wF + wBk + wR + wL, 1e-4f);
        rate = wsum * (1.f - cw) + rateC * cw;
        wF /= wsum; wBk /= wsum; wR /= wsum; wL /= wsum;
        rate = Min(rate, 2.4f) / ls;
        if (in.swimming) rate = Max(v, 0.5f) / stride(CLIP_SWIM) / ls;
        moveW = walkW;
        // stance fraction of the blended gait (foot planting reads the contacts from the phase)
        locoDuty = (wF * dutyF + wBk * clipDuty(CLIP_WALK_BACK) + (wL + wR) * clipDuty(CLIP_STRAFE_L)) * (1.f - cw) + clipDuty(CLIP_CROUCH_WALK) * cw;
        // turning on the spot: planted feet step round (foot planting below); distant peds without it side-step
        float turnStep = cheap ? (1.f - Saturate(v / 0.4f)) * Saturate((fabsf(in.turnRate) - 0.8f) / 1.5f) * (1.f - crouchBlend) *
                                     (in.swimming || in.inAir || stance != 0 ? 0.f : 1.f)
                               : 0.f;
        if (v > 0.02f || in.swimming) phase += dt * rate;
        phase += dt * turnStep * 1.3f / ls;
        phase -= floorf(phase);

        // standing: the weight on one leg or the other (each person's own timing), moving over through both
        Pose idle;
        const bool needIdle = walkW < 0.999f || cw > 0.001f;
        // (chatting, on the phone or smoking the weight moves from leg to leg too: those clips give the upper body)
        const bool chatStance = stance == 7 || stance == 8 || stance == 10;
        const bool standStill = (stance == 0 || stance == 23 || chatStance) && speedS < 0.05f && !in.aiming && !in.crouch && !in.inAir &&
                                !in.swimming && (action < 0 || actionFinished || actionUpper);
        weightShift(*this, standStill,
                    idleVar == IC_IDLE_HIP ? 1.f : (fidgetVar == IC_FIDGET_TAP ? 0.f : (fidgetVar == IC_FIDGET_ROCK ? 0.5f : -1.f)), dt);
        if (needIdle) {
            float sw = Saturate(standW);
            if (sw < 0.004f) sampleClipId(sk, IC_STAND_L, time, idle, seed);
            else if (sw > 0.996f) sampleClipId(sk, IC_STAND_R, time, idle, seed);
            else {
                sampleClipId(sk, IC_STAND_L, time, tmp, seed);
                sampleClipId(sk, IC_STAND_R, time, tmp2, seed);
                blendCtl(tmp, tmp2, sw, idle);
            }
        }
        if (cw < 0.999f) {
            Pose fwd;
            // (the clips in step with the mix: sampleGait)
            const float D = locoDuty;
            if (b0 == 0) sampleGait(sk, bands[1], phase, D, fwd, seed);
            else if (wb < 0.002f || wb > 0.998f) {
                int bb = wb < 0.5f ? bands[b0] : bands[b1];
                sampleGait(sk, bb, phase, D, fwd, seed);
            } else {
                sampleGait(sk, bands[b0], phase, D, tmp, seed);
                sampleGait(sk, bands[b1], phase, D, tmp2, seed);
                blendCtl(tmp, tmp2, wb, fwd);
            }
            Pose mv = fwd;
            float acc = wF;
            if (wBk > 0.001f) {
                sampleGait(sk, CLIP_WALK_BACK, phase, D, tmp, seed);
                acc += wBk;
                blendCtl(mv, tmp, wBk / acc, mv);
            }
            if (wL > 0.001f) {
                sampleGait(sk, CLIP_STRAFE_L, phase, D, tmp, seed);
                acc += wL;
                blendCtl(mv, tmp, wL / acc, mv);
            }
            if (wR > 0.001f) {
                sampleGait(sk, CLIP_STRAFE_R, phase, D, tmp, seed);
                acc += wR;
                blendCtl(mv, tmp, wR / acc, mv);
            }
            if (needIdle) blendCtl(idle, mv, walkW, base);
            else base = mv;
        } else {
            base = idle;
        }
        if (turnStep > 0.01f) {
            Clip sc = in.turnRate > 0.f ? CLIP_STRAFE_L : CLIP_STRAFE_R;
            sampleClip(sk, sc, phase * clipInfo(sc).duration, tmp, seed);
            blendCtl(base, tmp, turnStep * 0.45f, base);
            moveW = Max(moveW, turnStep);
        }
        if (cw > 0.001f) {
            Pose ci, cwk, cp;
            sampleClip(sk, CLIP_CROUCH_IDLE, time, ci, seed);
            sampleGait(sk, CLIP_CROUCH_WALK, phase, locoDuty, cwk, seed);
            blendCtl(ci, cwk, Saturate(v / 0.5f), cp);
            blendCtl(base, cp, cw, base);
        }
        // ---- the person in the gait: arm swing amplitude, arms clear of a wide body, trunk and head carriage
        {
            for (int s = 0; s < 2; s++) {
                // the fore-aft swing only (about the shoulder's lateral axis): the gait's abduction keeps the hand
                // clear of the hip whatever the amplitude (a shoulder bag's side swings less)
                float ks = 1.f + (armSwingK * bagSwing[s] - 1.f) * moveW * (1.f - cw);
                if (fabsf(ks - 1.f) <= 1e-3f) continue;
                int ub = s ? B_UPPERARM_R : B_UPPERARM_L;
                quat D = base.rot[ub] * conj(armRest[s]);   // from the hanging arm, in the clavicle's frame
                float swing = 2.f * atan2f(D.x, D.w);
                base.rot[ub] = normalize(qx(swing * (ks - 1.f)) * base.rot[ub]);
            }
            if (armOut > 1e-4f) {
                base.rot[B_UPPERARM_L] = normalize(qy(armOut) * base.rot[B_UPPERARM_L]);
                base.rot[B_UPPERARM_R] = normalize(qy(-armOut) * base.rot[B_UPPERARM_R]);
            }
            if (fabsf(postureLean) > 1e-4f) rotateLocal(base, B_SPINE2, qx(-postureLean));
            if (fabsf(headPitchAdd) > 1e-4f) rotateLocal(base, B_HEAD, qx(-headPitchAdd));
            // build: a heavy body's thighs apart (wider base) and more lateral sway, an athletic one bouncier
            float gw = moveW * (1.f - cw);
            if (heavyK > 1e-3f) {
                float a = 0.035f * heavyK;
                base.rot[B_THIGH_L] = normalize(qy(a) * base.rot[B_THIGH_L]);
                base.rot[B_THIGH_R] = normalize(qy(-a) * base.rot[B_THIGH_R]);
                base.rootOffset.x *= 1.f + 0.5f * heavyK * gw;
            }
            if (athleticK > 1e-3f) base.rootOffset.z *= 1.f + 0.25f * athleticK * gw;
        }
        // ---- standing around (stance 0 / 23, still): held postures (arms crossed, hands in the pockets / behind the
        //      back / clasped in front, a hand on the hip, a look at the phone) and, in their own slot, fidgets (the
        //      watch, a scratch at the head, tugging the top straight, a hand to the chin, a yawn, a stretch, a neck
        //      roll, a tapping foot, rocking onto the toes), from each person's habits (fidgetMask) at their own rate,
        //      more often in a queue. They layer over the weight shift (layerStanding); the hand on the hip brings its
        //      own (the weight on the right leg), a tapping foot / rocking wait for the weight to move first.
        {
            int cClip[2];
            float cTake[2];
            carryArms(*this, in, cClip, cTake);
            const int busyArms = (cClip[0] >= 0 ? 1 : 0) | (cClip[1] >= 0 ? 2 : 0);   // hands holding a prop
            const bool canVary = standStill && !chatStance && in.weaponKind != 2 && !in.phoneCall;
            // listeners keep a listening posture going; speakers only shift onto a hip now and then
            if (in.listening && idleVar < 0 && canVary) idleNext = Min(idleNext, 1.2f);
            if (in.speaking && idleVar >= 0 && idleVar != IC_IDLE_HIP) idleVarDur = Min(idleVarDur, idleVarT + 0.3f);
            if (idleVar >= 0) {
                // a prop taken in hand ends a posture that needs that hand
                int pk = 0;
                while (pk < FG_POSTURES - 1 && kPostureClip[pk] != idleVar) pk++;
                if (kPostureArms[pk] & busyArms) idleVarDur = Min(idleVarDur, idleVarT + 0.25f);
            }
            if (!canVary) {
                if (idleVar >= 0) idleVarDur = Min(idleVarDur, idleVarT + 0.25f);   // fade out now
                if (idleVar < 0) idleNext = Max(idleNext, 2.f);
            }
            if (idleVar < 0 && canVary) {
                idleNext -= dt;
                // not while a fidget has the arms (the posture's arms would come in from wherever the fidget has them)
                int fkA = 0;
                while (fidgetVar >= 0 && fkA < FG_COUNT - FG_POSTURES - 1 && kFidgetClip[fkA] != fidgetVar) fkA++;
                if (fidgetVar >= 0 && (kFidgetArms[fkA] & 3)) idleNext = Max(idleNext, 0.3f);
                if (idleNext <= 0.f) {
                    u32 h = hash32(seed * 0x9E3779B1u + (u32)idleCount * 0x85EBCA6Bu);
                    // phone, crossed arms, pockets, hip, behind the back, clasped: standing around / queueing / listening
                    static const float kWN[FG_POSTURES] = {0.17f, 0.21f, 0.21f, 0.15f, 0.11f, 0.13f};
                    static const float kWQ[FG_POSTURES] = {0.34f, 0.21f, 0.18f, 0.11f, 0.07f, 0.09f};
                    static const float kWL[FG_POSTURES] = {0.f, 0.4f, 0.2f, 0.25f, 0.1f, 0.15f};
                    const float* kw = in.listening ? kWL : (stance == 23 ? kWQ : kWN);
                    float w[FG_POSTURES];
                    for (int k = 0; k < FG_POSTURES; k++) w[k] = (fidgetMask >> k) & 1u && !(kPostureArms[k] & busyArms) ? kw[k] : 0.f;
                    float r = hashToFloat(h);
                    int k = pickWeighted(w, FG_POSTURES, r);
                    if (k < 0 && in.listening && !busyArms) k = pickWeighted(kWL, FG_POSTURES, r);   // a listener still takes one
                    if (in.speaking) k = (busyArms & 2) ? -1 : FG_HIP;
                    idleCount++;
                    if (k >= 0) {
                        idleVar = kPostureClip[k];
                        idleVarT = 0.f;
                        idleVarDur = 5.f + 6.f * hashToFloat(hash32(h + 3u)) + (in.listening ? 4.f : 0.f);
                    } else {
                        idleNext = 6.f + 8.f * hashToFloat(hash32(h + 5u));
                    }
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
                float w = idleVarW * (1.f - moveW);
                if (idleVar == IC_IDLE_HIP) blendCtl(base, tmp, w, base);
                else {
                    // coming or going the arms swing out (and back, for hands going behind the back) round the hips
                    float c4 = 4.f * idleVarW * (1.f - idleVarW) * (1.f - moveW);
                    layerStanding(*this, base, tmp, w, 3, 0, 0.3f * c4, idleVar == IC_IDLE_BEHIND ? 0.4f * c4 : 0.f);
                }
            }
            // fidgets (inside a posture too: the arms they need leave it for a moment)
            const bool canFidget = canVary && !in.speaking && idleVar != IC_IDLE_PHONE;
            if (fidgetVar >= 0) {
                int fk0 = 0;
                while (fk0 < FG_COUNT - FG_POSTURES - 1 && kFidgetClip[fk0] != fidgetVar) fk0++;
                if (!canFidget || (kFidgetArms[fk0] & 3 & busyArms)) fidgetDur = Min(fidgetDur, Max(fidgetT, 0.f) + 0.25f);
            }
            if (fidgetVar < 0 && canFidget) {
                fidgetNext -= dt * (in.listening ? 0.35f : 1.f) * (stance == 23 ? 1.4f : 1.f);
                if (fidgetNext <= 0.f) {
                    u32 h = hash32(seed * 0x2C1B3C6Du + (u32)fidgetCount * 0x297A2D39u);
                    const int nF = FG_COUNT - FG_POSTURES;
                    // watch, scratch, tug, chin, yawn, arms, tap, rock, neck roll
                    static const float kW[FG_COUNT - FG_POSTURES] = {0.14f, 0.14f, 0.1f, 0.1f, 0.07f, 0.07f, 0.14f, 0.09f, 0.14f};
                    float w[FG_COUNT - FG_POSTURES];
                    for (int k = 0; k < nF; k++) w[k] = (fidgetMask >> (FG_POSTURES + k)) & 1u ? kW[k] : 0.f;
                    // the feet only near the camera (distant peds have no foot planting), not with the weight held on
                    // the right for a hand on the hip; no arm the posture holds
                    if (cheap || idleVar == IC_IDLE_HIP) w[FG_TAP - FG_POSTURES] = w[FG_ROCK - FG_POSTURES] = 0.f;
                    int heldArms = busyArms;
                    if (idleVar >= 0) {
                        int pk = 0;
                        while (pk < FG_POSTURES - 1 && kPostureClip[pk] != idleVar) pk++;
                        heldArms |= kPostureArms[pk];
                    }
                    for (int f = 0; f < nF; f++)
                        if (kFidgetArms[f] & 3 & heldArms) w[f] = 0.f;
                    int k = pickWeighted(w, nF, hashToFloat(h));
                    fidgetCount++;
                    fidgetNext = (8.f + 16.f * hashToFloat(hash32(h + 1u))) / Max(fidgetRate, 0.3f);
                    if (k >= 0) {
                        fidgetVar = kFidgetClip[k];
                        const ClipInfo& fi = clipInfoId(fidgetVar);
                        fidgetDur = fidgetVar == IC_FIDGET_TAP ? 1.8f + 2.6f * hashToFloat(hash32(h + 2u)) : fi.duration;
                        // tapping (the right foot) waits for the weight on the left leg, rocking for both
                        float off = fidgetVar == IC_FIDGET_TAP ? standW : (fidgetVar == IC_FIDGET_ROCK ? fabsf(standW - 0.5f) : 0.f);
                        fidgetT = off > 0.15f ? -1.1f : 0.f;
                    }
                }
            }
            float ft = 0.f;
            if (fidgetVar >= 0) {
                fidgetT += dt;
                ft = fidgetT >= 0.f && fidgetT < fidgetDur - 0.25f ? 1.f : 0.f;
                if (fidgetT >= fidgetDur && fidgetW < 0.01f) fidgetVar = -1;
            }
            fidgetW = approach(fidgetW, ft, dt * 5.f);
            if (fidgetVar >= 0 && fidgetW > 0.001f) {
                int k = 0;
                while (k < FG_COUNT - FG_POSTURES - 1 && kFidgetClip[k] != fidgetVar) k++;
                const ClipInfo& fi = clipInfoId(fidgetVar);
                float t = fi.loop ? Max(fidgetT, 0.f) : Clamp(fidgetT, 0.f, fi.duration - 0.001f);
                sampleClipId(sk, fidgetVar, t, tmp, seed);
                layerStanding(*this, base, tmp, sstep(0.f, 1.f, fidgetW) * (1.f - moveW), kFidgetArms[k], kFidgetLegs[k]);
            }
            // posed hands on this person's own body (the clips have the reference body's): the hand on the hip onto the
            // flank, hands behind the back onto the small of the back; clasped hands, the forearm under a hand at the
            // chin and hands at the hem clear of the belly
            if (!cheap && (idleVarW > 0.01f || fidgetW > 0.01f)) {
                quat qp;
                vec3 pp;
                boneModel(sk, base, B_PELVIS, qp, pp);
                const vec3 X = rotate(qp, vec3(1, 0, 0)), Y = rotate(qp, vec3(0, 1, 0));
                int fk = 0;
                while (fidgetVar >= 0 && fk < FG_COUNT - FG_POSTURES - 1 && kFidgetClip[fk] != fidgetVar) fk++;
                const float wf = fidgetVar >= 0 ? sstep(0.f, 1.f, fidgetW) * (1.f - moveW) : 0.f;
                for (int sd = 0; sd < 2; sd++) {
                    // where the palm is and how far it sits from the skin point along the axis
                    auto gap = [&](int i, vec3 ax) {
                        quat qh;
                        vec3 ph;
                        boneModel(sk, base, sd ? B_HAND_R : B_HAND_L, qh, ph);
                        vec3 palm = ph + rotate(qh, sk.bindLocalPos[sd ? B_FINGERS_R : B_FINGERS_L]) * 0.45f;
                        return dot(palm - (pp + rotate(qp, skinP[i])), ax) - sk.boneRadius[sd ? B_HAND_R : B_HAND_L] * 0.75f;
                    };
                    // a fidget that has taken this arm leaves the posture's contact
                    float wp = idleVar >= 0 ? idleVarW * (1.f - moveW) * (fidgetVar >= 0 && (kFidgetArms[fk] & (1 << sd)) ? 1.f - wf : 1.f) : 0.f;
                    if (wp > 0.01f) {
                        if (idleVar == IC_IDLE_HIP && sd == 1) nudgeHand(sk, base, sd, X * Clamp(0.004f - gap(0, X), -0.06f, 0.06f), wp);
                        else if (idleVar == IC_IDLE_BEHIND) nudgeHand(sk, base, sd, Y * -Clamp(0.006f - gap(1, -Y), -0.04f, 0.06f), wp);
                        else if (idleVar == IC_IDLE_CLASP) nudgeHand(sk, base, sd, Y * Clamp(0.008f - gap(2, Y), 0.f, 0.08f), wp);
                    }
                    if (wf > 0.01f && ((fidgetVar == IC_FIDGET_CHIN && sd == 0) || fidgetVar == IC_FIDGET_TUG))
                        nudgeHand(sk, base, sd, Y * Clamp(0.006f - gap(2, Y), 0.f, 0.08f), wf);
                }
            }
        }
        if (stance >= 4 && stance != 23) {
            int sc = stanceClip;
            float sOff = stanceIsGuard(stance) ? 0.f : hashToFloat(seed * 7u + 3u) * clipInfoId(sc).duration;
            float tScale = stance == 9 ? 0.9f + 0.22f * hashToFloat(hash32(seed + 404u)) : 1.f;   // dance tempo per ped
            sampleClipId(sk, sc, stanceTime * tScale + sOff, tmp, seed);
            float still = stanceLocksLegs(stance) ? 1.f : 1.f - Saturate((speedS - 0.25f) / 0.6f);
            if (chatStance) {
                blendUpperBody(base, tmp, 1.f, base);   // the legs keep the base's weight shifts (or walk)
            } else if (stanceUpperWhileMoving(stance)) {
                blendUpperBody(base, tmp, 1.f, tmp2);
                blendCtl(tmp2, tmp, still, base);
            } else {
                blendCtl(base, tmp, still, base);
            }
            if (stanceLocksLegs(stance)) moveW = 0.f;
        }
        // ---- airborne
        if (airBlend > 0.001f) {
            sampleClip(sk, CLIP_FALL, airT, tmp, seed);
            blendCtl(base, tmp, airBlend, base);
            // a long drop: the arms wheel and the legs pedal, more the longer it lasts
            float flail = sstep(0.7f, 1.6f, airT) * airBlend;
            if (flail > 1e-3f) {
                float ph = airT * (8.f + 2.f * hashToFloat(seed * 5u + 1u));
                for (int s = 0; s < 2; s++) {
                    float sx = s ? 1.f : -1.f, o = s * 2.1f;
                    rotateLocal(base, s ? B_UPPERARM_R : B_UPPERARM_L, qx(0.7f * flail * sinf(ph + o)) * qy(sx * 0.35f * flail * sinf(ph * 0.7f + o)));
                    rotateLocal(base, s ? B_FOREARM_R : B_FOREARM_L, qx(0.3f * flail * sinf(ph * 1.3f + o)));
                    rotateLocal(base, s ? B_THIGH_R : B_THIGH_L, qx(0.35f * flail * sinf(ph * 0.8f + o + 1.f)));
                    rotateLocal(base, s ? B_CALF_R : B_CALF_L, qx(-0.35f * flail * (0.5f + 0.5f * sinf(ph * 0.8f + o + 2.3f))));
                }
                rotateLocal(base, B_SPINE2, qx(-0.1f * flail * sinf(ph * 0.5f)));
            }
        }
        // ---- swimming
        if (swimBlend > 0.001f) {
            Pose si, sw;
            sampleClip(sk, CLIP_SWIM_IDLE, time, si, seed);
            sampleClip(sk, CLIP_SWIM, phase * clipInfo(CLIP_SWIM).duration, sw, seed);
            blendCtl(si, sw, Saturate((v - 0.2f) / 0.6f), tmp);
            blendCtl(base, tmp, swimBlend, base);
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
            if (full > 0.001f) blendCtl(base, tmp, full, base);
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
        // a prop in hand: the holding arm posed for it (a new prop waits for the old pose to fade out)
        {
            int cClip[2];
            float cTake[2];
            carryArms(*this, in, cClip, cTake);
            if (stance == 12 || stance == 21 || stance == 22) cClip[0] = cClip[1] = -1;   // lying down: nothing held up
            static const u8 kArmB[2][4] = {{B_CLAVICLE_L, B_UPPERARM_L, B_FOREARM_L, B_HAND_L}, {B_CLAVICLE_R, B_UPPERARM_R, B_FOREARM_R, B_HAND_R}};
            static const u8 kHandB[2][2] = {{B_FINGERS_L, B_THUMB_L}, {B_FINGERS_R, B_THUMB_R}};
            for (int sd = 0; sd < 2; sd++) {
                if (cClip[sd] != carryClip[sd] && carryW[sd] < 0.02f) carryClip[sd] = cClip[sd];
                float target = cClip[sd] >= 0 && cClip[sd] == carryClip[sd] ? 1.f : 0.f;
                carryW[sd] = approach(carryW[sd], target, dt * 4.f);
                if (carryClip[sd] < 0 || carryW[sd] <= 0.001f) continue;
                sampleClipId(sk, carryClip[sd], time, tmp, seed);
                float e = sstep(0.f, 1.f, carryW[sd]);
                float take = Lerp(1.f, cTake[sd] > 0.f ? cTake[sd] : 1.f, moveW);   // hanging loads swing a little
                for (u8 b : kArmB[sd]) base.rot[b] = nlerp(base.rot[b], tmp.rot[b], e * take);
                for (u8 b : kHandB[sd]) base.rot[b] = nlerp(base.rot[b], tmp.rot[b], e);
            }
        }
    }

    // ---------------------------------------------------------------- breathing (the timing runs for everyone; the
    //                                                                  scenario clips breathe on their own, softer here)
    {
        bool ownClip = !vehicleStance && stance >= 4 && stance != 18 && stance != 19 && stance != 20 && stance != 23;
        float amt = cheap || swimBlend > 0.5f ? 0.f : (vehicleStance ? 0.5f : (ownClip ? 0.4f : 1.f)) * (1.f - 0.6f * moveW);
        breathe(*this, base, dt, speedS, amt);
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

    // ---------------------------------------------------------------- hips turned into the travel direction (the
    //                                                                  trunk turned back: it keeps facing ahead)
    if (fabsf(hipTurn) > 1e-4f && !vehicleStance && (action < 0 || actionFinished || actionUpper)) {
        const quat qh = qz(-hipTurn);
        outp.rot[B_PELVIS] = normalize(qh * outp.rot[B_PELVIS]);
        outp.rootOffset = rotate(qh, outp.rootOffset);
        // spread over the spine (the lower back twists least)
        static const u8 kChain[3] = {B_SPINE1, B_SPINE2, B_CHEST};
        static const float kShare[3] = {0.3f, 0.35f, 0.35f};
        for (int k = 0; k < 3; k++) {
            quat qp;
            vec3 pp;
            boneModel(sk, outp, sk.parent[kChain[k]], qp, pp);
            outp.rot[kChain[k]] = normalize(conj(qp) * qz(hipTurn * kShare[k]) * qp * outp.rot[kChain[k]]);
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

    // ---------------------------------------------------------------- greetings: onto the real partner
    // The clips fit a partner of this body at pairDistance, posed the same (mirrored): the partner's chest / head
    // (AnimInput::grabTarget) against that standard gives the fit-up, weighed by how far into the contact the clip is.
    if (!cheap && action >= 0 && !actionFinished && (action == CLIP_HUG || action == CLIP_HANDSHAKE || action == CLIP_CHEEK_KISS) &&
        in.grabWeight > 0.f) {
        float k = pairReach(action, actionTime) * Clamp(in.grabWeight, 0.f, 1.f);
        if (k > 0.001f) {
            const float d = pairDistance((Clip)action, sk, sk);
            quat q;
            vec3 me;
            boneModel(sk, outp, action == CLIP_CHEEK_KISS ? B_HEAD : B_CHEST, q, me);
            vec3 dl = in.grabTarget - vec3(-me.x, d - me.y, me.z);   // the partner against the standard one
            if (length2(dl) > 0.36f) dl = normalize(dl) * 0.6f;
            if (action == CLIP_HUG) {
                // both hands stay on the partner's back. A much taller partner is held with both arms under its arms (the
                // high hand drops to the middle of the back), a much shorter one with both arms over its shoulders (the
                // low hand rises); over a shorter partner's shoulder the elbow comes down too
                const float s = length(sk.bindLocalPos[B_FOREARM_R]) / 0.3f;   // arm size relative to the reference
                float under = sstep(0.06f, 0.16f, dl.z), over = sstep(0.06f, 0.16f, -dl.z);
                // a bigger partner is also deeper: its back further behind its chest (by the chest heights' ratio)
                float deeper = (Clamp(in.grabTarget.z / Max(me.z, 0.5f), 0.75f, 1.35f) - 1.f) * 0.14f;
                vec3 dh = dl + vec3(0.f, deeper, 0.f);
                nudgeHand(sk, outp, 0, dh + vec3(0.f, 0.f, 0.2f * s * over), k);
                nudgeHand(sk, outp, 1, dh - vec3(0.f, 0.f, 0.2f * s * under), k, vec3(0.f, 0.f, Min(dl.z, 0.f) * 1.5f - 0.3f * under));
            } else if (action == CLIP_HANDSHAKE) {
                // the hands meet half way between the two chests, at the height of both
                nudgeHand(sk, outp, 1, vec3(dl.x, dl.y, dl.z * 0.83f) * 0.5f, k);
            } else {
                // each partner covers half of the difference: the taller bows the head and neck down, the shorter lifts
                // them (the upper body takes the rest of a big difference), leaning further in or less, and sideways
                vec3 h = dl * 0.5f;
                float down = Clamp(-h.z / 0.2f, -0.35f, 0.6f), fwd = Clamp(h.y / 0.55f, -0.2f, 0.3f), side = Clamp(h.x / 0.55f, -0.2f, 0.2f);
                float bow = Max(0.f, down - 0.35f);   // beyond what the neck takes comfortably
                rotateLocal(outp, B_SPINE2, qx(-(fwd + bow) * k) * qy(side * 0.6f * k));
                rotateLocal(outp, B_NECK, qx(-Min(down, 0.35f) * 0.6f * k) * qy(side * 0.4f * k));
                rotateLocal(outp, B_HEAD, qx(-Min(down, 0.35f) * 0.4f * k));
                nudgeHand(sk, outp, 1, dl, k);
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
        if (!cheap) driveHands(sk, outp, wheelTurn(), in);
    } else {
        steerS = 0.f;
    }

    // ---------------------------------------------------------------- lean into turns
    const bool upright = !vehicleStance && (action < 0 || actionUpper) && swimBlend < 0.5f && airBlend < 0.5f;
    if (!vehicleStance && fabsf(leanS) > 1e-4f && (action < 0 || actionUpper)) {
        outp.rot[B_PELVIS] = normalize(qy(leanS * 0.5f) * outp.rot[B_PELVIS]);
        rotateLocal(outp, B_SPINE2, qy(leanS * 0.3f));
        rotateLocal(outp, B_NECK, qy(-leanS * 0.5f));
    }

    // ---------------------------------------------------------------- start / stop lean, head leading into turns,
    //                                                                  body following a turn on the spot
    footEvents = 0;
    if (!cheap) {
        // acceleration: the body leans into a start and back against a stop, then settles (damped spring)
        float acc = dt > 1e-4f ? Clamp((spdIn - prevSpeed) / dt, -12.f, 12.f) : 0.f;
        float target = upright && !in.aiming && crouchBlend < 0.5f ? Clamp(acc * 0.012f, -0.1f, 0.12f) : 0.f;
        accV += (55.f * (target - accS) - 8.5f * accV) * dt;
        accS = Clamp(accS + accV * dt, -0.15f, 0.18f);
        if (fabsf(accS) > 1e-4f) {
            rotateLocal(outp, B_SPINE1, qx(-accS * 0.45f));
            rotateLocal(outp, B_SPINE2, qx(-accS * 0.45f));
            rotateLocal(outp, B_HEAD, qx(accS * 0.6f));
        }
        // standing turns: the body keeps its heading and catches up while the planted feet step round
        bool lagOK = upright && plantOn > 0.5f && moveW < 0.3f && !in.aiming && stance != 19 && stance != 20;
        if (lagOK) bodyLag = Clamp(bodyLag - in.turnRate * dt, -1.2f, 1.2f);
        bodyLag *= expf(-dt * (lagOK ? 4.f : 14.f));
        if (fabsf(bodyLag) > 1e-4f) outp.rot[B_ROOT] = normalize(qz(bodyLag) * outp.rot[B_ROOT]);
        // the head turns into a turn before the body (and faces the new heading while the body lags)
        float leadT = upright && !in.aiming ? Clamp(in.turnRate * 0.2f, -0.45f, 0.45f) : 0.f;
        headLead += (leadT - headLead) * (1.f - expf(-dt * 7.f));
        float hy = Clamp(headLead - bodyLag * 0.8f, -0.95f, 0.95f);
        if (fabsf(hy) > 1e-4f) {
            rotateLocal(outp, B_NECK, qz(hy * 0.4f));
            rotateLocal(outp, B_HEAD, qz(hy * 0.6f));
        }
        prevSpeed = spdIn;
    } else {
        accS = accV = bodyLag = headLead = 0.f;
        prevSpeed = spdIn;
    }

    // ---------------------------------------------------------------- feet: planted on the ground, stepping, terrain
    {
        bool plantStance = stance == 0 || stance == 5 || stance == 7 || stance == 8 || stance == 10 || stance == 14 || stance == 15 ||
                           stance == 17 || stance == 23;
        bool planting = !cheap && footIK && plantStance && (action < 0 || actionFinished || actionUpper) && swimBlend < 0.01f;
        if (!cheap) footPlanting(*this, in, dt, outp, planting, footIK, locoDuty);
        else if (plantOn > 0.f || planted[0] || planted[1]) {
            // distant (LOD2) peds: no foot work; start from free feet when they come close again
            plantOn = legSink = stepShift = 0.f;
            for (int s = 0; s < 2; s++) {
                planted[s] = false;
                stepT[s] = -1.f;
                plantCorr[s] = vec3(0);
                corrYaw[s] = 0.f;
                pinZ[s] = plantAge[s] = 0.f;
            }
        }
    }

    // ---------------------------------------------------------------- crossfade from captured pose
    if (snapW > 0.f) {
        snapW = Max(0.f, snapW - dt * snapRate);
        float w = snapW * snapW * (3.f - 2.f * snapW);
        blendCtl(outp, snap, w, pose);
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
    // ---- gaze. A target direction (the game's look-at point, else a glance of the person's own, else straight on):
    //      the eyes go to it at once (a saccade), the head follows on a spring and the eyes hold the target as it
    //      arrives; small shifts are left to the eyes, the chest joins in for big turns; within the neck's and the eyes'
    //      limits. A big shift often comes with a blink.
    const bool alert = !dead && !out;
    float lwT = alert ? Clamp(in.lookWeight, 0.f, 1.f) : 0.f;
    lookW += (lwT - lookW) * (1.f - expf(-dt * 4.f));
    float tgtY = 0.f, tgtP = 0.f;
    bool haveTarget = false;
    // shaking hands: eyes on the partner's face (its chest from AnimInput::grabTarget, the face above it)
    const bool shake = alert && action == CLIP_HANDSHAKE && !actionFinished && in.grabWeight > 0.f && lwT <= 0.15f;
    if (lwT > 0.15f || shake) {
        quat qh;
        vec3 ph;
        boneModel(sk, pose, B_HEAD, qh, ph);
        vec3 fwdH = rotate(qh, vec3(0, 1, 0));
        vec3 eyesP = ph + rotate(qh, vec3(0.f, 0.07f, 0.06f));
        vec3 d = (shake ? in.grabTarget + vec3(0.f, 0.f, 0.33f) : in.lookAt) - eyesP;
        float dl = length(d);
        if (dl > 0.05f) {
            d = d / dl;
            tgtY = wrapAngle(atan2f(-d.x, d.y) - atan2f(-fwdH.x, fwdH.y));
            tgtP = asinf(Clamp(d.z, -1.f, 1.f)) - asinf(Clamp(fwdH.z, -1.f, 1.f));
            haveTarget = true;
        }
    }
    // glances of one's own when nothing else holds the eyes: sideways at shop windows and people, down at the path
    // while walking, now and then up; the curious more often (lookiness)
    const bool free = alert && !haveTarget && lookW < 0.1f && (action < 0 || actionFinished) && aimBlend < 0.1f && !stanceIsGuard(stance) &&
                      stance != 4 && stance != 5 && swimBlend < 0.5f;
    glanceNext -= dt;
    if (glanceT >= 0.f) {
        glanceT += dt;
        if (glanceT > glanceDur || !free) glanceT = -1.f;
    }
    if (glanceT < 0.f && glanceNext <= 0.f && free) {
        u32 h = hash32(seed * 0x61C88647u + (u32)(time * 3.f));
        float r = hashToFloat(h), r2 = hashToFloat(hash32(h + 1u)), r3 = hashToFloat(hash32(h + 2u));
        if (moveW > 0.4f && r < 0.45f) {   // down at the way ahead
            glanceYaw = (r2 - 0.5f) * 0.3f;
            glancePitch = -0.2f - 0.15f * r3;
        } else if (r > 0.9f) {             // up
            glanceYaw = (r2 - 0.5f) * 0.8f;
            glancePitch = 0.15f + 0.15f * r3;
        } else {                           // to one side
            glanceYaw = (r2 < 0.5f ? -1.f : 1.f) * (0.35f + 0.7f * r3);
            glancePitch = (hashToFloat(hash32(h + 3u)) - 0.6f) * 0.2f;
        }
        glanceT = 0.f;
        glanceDur = 0.6f + 1.6f * hashToFloat(hash32(h + 4u));
        glanceNext = glanceDur + (2.f + 7.f * hashToFloat(hash32(h + 5u))) / (0.35f + lookiness);
    }
    if (!haveTarget && glanceT >= 0.f) {
        tgtY = glanceYaw;
        tgtP = glancePitch;
    }
    // a big shift of the target: often a blink with it
    if ((fabsf(tgtY - tgtYawPrev) > 0.5f || fabsf(tgtP - tgtPitchPrev) > 0.35f) && blinkT < 0.f &&
        hashToFloat(hash32(seed * 97u + (u32)(time * 50.f))) < 0.5f) {
        blinkT = 0.f;
        blinkNext = Max(blinkNext, 1.2f);
    }
    tgtYawPrev = tgtY;
    tgtPitchPrev = tgtP;
    // the head's share: nothing for the first ~7 degrees (the eyes alone), most of the rest; its limits
    auto headShare = [](float a, float lo, float hi) {
        float m = Max(0.f, fabsf(a) - 0.12f) * 0.85f;
        return Clamp(a < 0.f ? -m : m, lo, hi);
    };
    float hyT = headShare(tgtY, -1.35f, 1.35f), hpT = headShare(tgtP, -0.45f, 0.35f);
    // critically damped spring, exact step (the head lags the eyes by a few hundred ms)
    auto spring = [&](float& x, float& v, float target, float k) {
        float e = expf(-k * dt), xx = x - target, c = v + k * xx;
        x = target + (xx + c * dt) * e;
        v = (v - k * c * dt) * e;
    };
    spring(headYawS, headYawV, alert ? hyT : 0.f, 9.f);
    spring(headPitchS, headPitchV, alert ? hpT : 0.f, 9.f);
    // the chest turns along for big head turns
    float chestY = Clamp((fabsf(headYawS) - 0.8f) * 0.45f, 0.f, 0.35f) * (headYawS < 0.f ? -1.f : 1.f);
    if (fabsf(chestY) > 1e-4f) {
        rotateLocal(pose, B_SPINE2, qz(chestY * 0.4f));
        rotateLocal(pose, B_CHEST, qz(chestY * 0.6f));
    }
    float hy = headYawS - chestY, hp = headPitchS;
    if (fabsf(hy) + fabsf(hp) > 1e-4f) {
        pose.rot[B_NECK] = normalize(pose.rot[B_NECK] * qz(hy * 0.4f) * qx(hp * 0.35f));
        pose.rot[B_HEAD] = normalize(pose.rot[B_HEAD] * qz(hy * 0.6f) * qx(hp * 0.65f));
    }
    // the eyes: on the target where the head has not got to yet (fast, limited)
    float ke = 1.f - expf(-dt / 0.025f);
    eyeYawS += ((alert ? Clamp(tgtY - headYawS, -0.55f, 0.55f) : 0.f) - eyeYawS) * ke;
    eyePitchS += ((alert ? Clamp(tgtP - headPitchS, -0.35f, 0.3f) : 0.f) - eyePitchS) * ke;
    float eyeYaw = eyeYawS, eyePitch = eyePitchS;
    // idle gaze: small saccades between fixations
    gazeNext -= dt;
    if (gazeNext <= 0.f) {
        u32 h = hash32(seed * 747796405u + (u32)(time * 7.f) * 2891336453u);
        // (smaller while the eyes hold a target)
        gazeTarget = vec2((hashToFloat(h) - 0.5f) * 0.3f, (hashToFloat(hash32(h)) - 0.5f) * 0.12f) *
                     (1.f - 0.6f * Max(lookW, glanceT >= 0.f ? 1.f : 0.f));
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
    // a kiss on the cheek: lips pushed forward while the cheeks touch
    if (action == CLIP_CHEEK_KISS && !actionFinished && !dead && !out) {
        float kw = pairReach(action, actionTime);
        for (int i = 0; i < 6; i++) target[i] = Lerp(target[i], kVisemeShape[14][i], kw * 0.85f);
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
    // out of breath after running: the mouth opens with each breath
    float jawE = exprS[0] * ek + (dead || out ? 0.f : exertion * (0.04f + 0.12f * breath));
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
