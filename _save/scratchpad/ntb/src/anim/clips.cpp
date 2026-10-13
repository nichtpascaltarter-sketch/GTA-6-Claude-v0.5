// Procedural animation clips.
//
// Every clip is authored as a function of time producing a "rig pose" of intuitive controls (pelvis offset and
// rotation, spine/neck/head bend-twist-lean distributed over the vertebrae, clavicle shrug, arm direction + elbow pole
// + elbow flexion or hand IK targets, foot IK targets with heel-to-toe roll, finger curl, jaw, eyes). Rig poses are
// converted to local bone rotations on a reference skeleton (analytic two-bone IK for legs and IK arms) and baked
// at 30 Hz into a global library built once (thread-safe static). Locomotion comes from a gait generator driven by
// speed-derived stride length/frequency: stance/swing phases per foot with heel strike, foot-flat and toe-off roll,
// pelvis bob/sway/rotation/list, spine counter-rotation, arm swing opposite to the legs and head stabilization.
// Male and female variants are baked for locomotion and idles; sampleClip picks the style from the skeleton.
//
// Conventions for placing clips in the world (model space, character faces +Y, origin on the ground):
//  - Locomotion clips are in place (no root translation); ClipInfo::speed is the matching ground speed.
//  - SIT_DRIVE / SIT_PASSENGER: origin = floor under the seat, hip joints ~0.45 m above it, feet forward on the floor.
//    Use clipHipPosition() to align the hips with a vehicle SeatSpec::pos.
//  - RIDE_BIKE: motorcycle posture, hips ~0.78 m above the origin (bike ground point), hands on bars, feet on pegs.
//  - ENTER_CAR_L starts standing beside the car's left door (car to the character's right) and ends seated 0.55 m to
//    the right (+X) in the SIT_DRIVE pose; EXIT_CAR_L is the reverse (starts in SIT_DRIVE, ends standing 0.55 m to the
//    left). The _R variants are mirrored (passenger side, ending/starting in SIT_PASSENGER).
//  - SWIM / SWIM_IDLE: origin = water surface; the body floats with the back (swim) or chin (idle) at the surface.
//  - DEATH_FRONT (hit from the front) ends lying on the back; DEATH_BACK ends face down. GET_UP_BACK starts from
//    lying on the back, GET_UP_FRONT from lying face down; both end standing at the origin.
//  - VAULT moves the body 1.1 m forward during the clip (root motion is in Pose::rootOffset).
#include "anim_internal.h"

namespace Anim {

void boneModelPublic(const Skeleton& sk, const Pose& pose, int bone, quat& q, vec3& t);

namespace detail {

void boneModel(const Skeleton& sk, const Pose& pose, int bone, quat& q, vec3& t);

// ------------------------------------------------------------------------------------------------
// Rig controls

struct ArmCtl {
    vec3 dir = vec3(0, 0.03f, -1);   // upper arm direction in the chest frame (model aligned in bind)
    vec3 pole = vec3(0, -1, 0);      // direction the elbow points
    float elbow = 0.18f;             // flexion (rad)
    float twist = 0.f;               // forearm pronation (rad, + palm turns backwards/down)
    float wristFlex = 0.f, wristDev = 0.f;
    float fingers = 0.35f, thumb = 0.2f;
    float clavUp = 0.f, clavFwd = 0.f;
    bool ik = false;                 // hand target (model space) instead of FK
    vec3 target, ikPole;
    bool orient = false;             // desired hand orientation (model space)
    quat handRot;
};

struct LegCtl {
    bool ik = true;
    vec3 ankle;                      // ankle target (model space)
    float pitch = 0.f, yaw = 0.f, roll = 0.f;   // foot orientation in model space (+pitch = toes up)
    float toe = 0.f;                 // toe bend relative to the foot (+ = toes up)
    vec3 knee = vec3(0, 1, 0);       // knee direction
    // FK alternative (ik = false)
    float hipFlex = 0.f, hipAbd = 0.f, hipTwist = 0.f, kneeFlex = 0.f, ankleFlex = 0.f;
};

struct Rig {
    vec3 pelvis;                     // pelvis offset from bind (model)
    float pelvisYaw = 0.f, pelvisPitch = 0.f, pelvisRoll = 0.f;
    float spinePitch = 0.f, spineYaw = 0.f, spineRoll = 0.f;
    float neckPitch = 0.f, neckYaw = 0.f, neckRoll = 0.f;
    float headPitch = 0.f, headYaw = 0.f, headRoll = 0.f;
    ArmCtl arm[2];
    LegCtl leg[2];
    float jaw = 0.f;
    vec2 eyes;
    quat root;
};

struct AuthorCtx {
    Skeleton sk;
    BodyDims D;
    float footH;         // ankle joint height above the ground (bind)
    float legLen;
    vec3 hip[2], ankle[2], gh[2];
    float heelBack, ballFwd, toeFwd;
    float fem = 0.f;     // style (0 male .. 1 female)
    vec3 chestP, headP;
    float shoulderZ;
};

static float lerpA(float a, float b, float t) { return a + (b - a) * t; }

static void lerpArm(const ArmCtl& a, const ArmCtl& b, float t, ArmCtl& o) {
    o.dir = normalize(lerp(a.dir, b.dir, t));
    o.pole = normalize(lerp(a.pole, b.pole, t));
    o.elbow = lerpA(a.elbow, b.elbow, t);
    o.twist = lerpA(a.twist, b.twist, t);
    o.wristFlex = lerpA(a.wristFlex, b.wristFlex, t);
    o.wristDev = lerpA(a.wristDev, b.wristDev, t);
    o.fingers = lerpA(a.fingers, b.fingers, t);
    o.thumb = lerpA(a.thumb, b.thumb, t);
    o.clavUp = lerpA(a.clavUp, b.clavUp, t);
    o.clavFwd = lerpA(a.clavFwd, b.clavFwd, t);
    o.ik = t < 0.5f ? a.ik : b.ik;
    if (a.ik && b.ik) {
        o.target = lerp(a.target, b.target, t);
        o.ikPole = normalize(lerp(a.ikPole, b.ikPole, t));
    } else {
        o.target = b.ik ? b.target : a.target;
        o.ikPole = b.ik ? b.ikPole : a.ikPole;
    }
    o.orient = t < 0.5f ? a.orient : b.orient;
    o.handRot = nlerp(a.handRot, b.handRot, t);
}

static void lerpLeg(const LegCtl& a, const LegCtl& b, float t, LegCtl& o) {
    o.ik = t < 0.5f ? a.ik : b.ik;
    o.ankle = lerp(a.ankle, b.ankle, t);
    o.pitch = lerpA(a.pitch, b.pitch, t);
    o.yaw = lerpA(a.yaw, b.yaw, t);
    o.roll = lerpA(a.roll, b.roll, t);
    o.toe = lerpA(a.toe, b.toe, t);
    o.knee = normalize(lerp(a.knee, b.knee, t));
    o.hipFlex = lerpA(a.hipFlex, b.hipFlex, t);
    o.hipAbd = lerpA(a.hipAbd, b.hipAbd, t);
    o.hipTwist = lerpA(a.hipTwist, b.hipTwist, t);
    o.kneeFlex = lerpA(a.kneeFlex, b.kneeFlex, t);
    o.ankleFlex = lerpA(a.ankleFlex, b.ankleFlex, t);
}

static Rig lerpRig(const Rig& a, const Rig& b, float t) {
    Rig o;
    o.pelvis = lerp(a.pelvis, b.pelvis, t);
    o.pelvisYaw = lerpA(a.pelvisYaw, b.pelvisYaw, t);
    o.pelvisPitch = lerpA(a.pelvisPitch, b.pelvisPitch, t);
    o.pelvisRoll = lerpA(a.pelvisRoll, b.pelvisRoll, t);
    o.spinePitch = lerpA(a.spinePitch, b.spinePitch, t);
    o.spineYaw = lerpA(a.spineYaw, b.spineYaw, t);
    o.spineRoll = lerpA(a.spineRoll, b.spineRoll, t);
    o.neckPitch = lerpA(a.neckPitch, b.neckPitch, t);
    o.neckYaw = lerpA(a.neckYaw, b.neckYaw, t);
    o.neckRoll = lerpA(a.neckRoll, b.neckRoll, t);
    o.headPitch = lerpA(a.headPitch, b.headPitch, t);
    o.headYaw = lerpA(a.headYaw, b.headYaw, t);
    o.headRoll = lerpA(a.headRoll, b.headRoll, t);
    for (int s = 0; s < 2; s++) {
        lerpArm(a.arm[s], b.arm[s], t, o.arm[s]);
        lerpLeg(a.leg[s], b.leg[s], t, o.leg[s]);
    }
    o.jaw = lerpA(a.jaw, b.jaw, t);
    o.eyes = lerp(a.eyes, b.eyes, t);
    o.root = nlerp(a.root, b.root, t);
    return o;
}

// Mirror left/right (for _R variants of car entry/exit etc.)
static Rig mirrorRig(const Rig& a) {
    Rig o = a;
    auto mx = [](vec3 v) { return vec3(-v.x, v.y, v.z); };
    o.pelvis = mx(a.pelvis);
    o.pelvisYaw = -a.pelvisYaw;
    o.pelvisRoll = -a.pelvisRoll;
    o.spineYaw = -a.spineYaw;
    o.spineRoll = -a.spineRoll;
    o.neckYaw = -a.neckYaw;
    o.neckRoll = -a.neckRoll;
    o.headYaw = -a.headYaw;
    o.headRoll = -a.headRoll;
    o.eyes.x = -a.eyes.x;
    for (int s = 0; s < 2; s++) {
        const ArmCtl& sa = a.arm[1 - s];
        ArmCtl& da = o.arm[s];
        da = sa;
        da.dir = mx(sa.dir);
        da.pole = mx(sa.pole);
        da.target = mx(sa.target);
        da.ikPole = mx(sa.ikPole);
        da.handRot = quat(sa.handRot.x, -sa.handRot.y, -sa.handRot.z, sa.handRot.w);
        const LegCtl& sl = a.leg[1 - s];
        LegCtl& dl = o.leg[s];
        dl = sl;
        dl.ankle = mx(sl.ankle);
        dl.yaw = -sl.yaw;
        dl.roll = -sl.roll;
        dl.knee = mx(sl.knee);
        dl.hipAbd = sl.hipAbd;
        dl.hipTwist = -sl.hipTwist;
    }
    o.root = quat(a.root.x, -a.root.y, -a.root.z, a.root.w);
    return o;
}

// ------------------------------------------------------------------------------------------------
// Rig -> Pose

static inline quat eulerZXY(float yaw, float pitchFwd, float roll) { return qz(yaw) * qx(-pitchFwd) * qy(roll); }

static void rigToPose(const AuthorCtx& A, const Rig& r, Pose& out) {
    const Skeleton& sk = A.sk;
    for (int b = 0; b < B_COUNT; b++) out.rot[b] = quat();
    out.rootOffset = r.pelvis;
    out.rot[B_ROOT] = r.root;
    out.rot[B_PELVIS] = eulerZXY(r.pelvisYaw, r.pelvisPitch, r.pelvisRoll);
    const float sw[3] = {0.3f, 0.3f, 0.4f};
    for (int i = 0; i < 3; i++) out.rot[B_SPINE1 + i] = eulerZXY(r.spineYaw * sw[i], r.spinePitch * sw[i], r.spineRoll * sw[i]);
    out.rot[B_NECK] = eulerZXY(r.neckYaw, r.neckPitch, r.neckRoll);
    out.rot[B_HEAD] = eulerZXY(r.headYaw, r.headPitch, r.headRoll);
    out.rot[B_JAW] = qx(-r.jaw);
    out.rot[B_EYE_L] = out.rot[B_EYE_R] = qz(r.eyes.x) * qx(r.eyes.y);
    // arms
    for (int s = 0; s < 2; s++) {
        const ArmCtl& a = r.arm[s];
        float sx = s ? 1.f : -1.f;
        int o = s ? 4 : 0, fo = s ? 2 : 0;
        quat clav = qz(sx * a.clavFwd) * qy(-sx * a.clavUp);
        out.rot[B_CLAVICLE_L + o] = clav;
        vec3 bindDir = A.D.armDir[s], bindPole(0, -1, 0);
        vec3 pn = A.D.palmN[s];
        quat rc = quatFromTwoPairs(bindDir, bindPole, normalize(a.dir), normalize(a.pole));
        out.rot[B_UPPERARM_L + o] = normalize(conj(clav) * rc);
        vec3 hinge = normalize(cross(bindDir, -bindPole));
        out.rot[B_FOREARM_L + o] = qaa(hinge, a.elbow) * qaa(bindDir, a.twist * 0.5f);
        vec3 flexAx = normalize(cross(bindDir, pn));
        out.rot[B_HAND_L + o] = qaa(bindDir, a.twist * 0.5f) * qaa(flexAx, a.wristFlex) * qaa(pn, a.wristDev);
        out.rot[B_FINGERS_L + fo] = qaa(flexAx, a.fingers * 1.45f);
        vec3 td = A.D.thumbDir[s];
        vec3 tAx = normalize(cross(td, pn));
        out.rot[B_THUMB_L + fo] = qaa(tAx, a.thumb * 0.9f);
    }
    // legs
    for (int s = 0; s < 2; s++) {
        const LegCtl& l = r.leg[s];
        int o = s ? 4 : 0;
        float sx = s ? 1.f : -1.f;
        if (!l.ik) {
            out.rot[B_THIGH_L + o] = qz(sx * l.hipTwist) * qx(l.hipFlex) * qy(sx * l.hipAbd);
            out.rot[B_CALF_L + o] = qx(-l.kneeFlex);
            out.rot[B_FOOT_L + o] = qx(l.ankleFlex);
            out.rot[B_TOE_L + o] = qx(l.toe);
        }
    }
    // IK legs
    for (int s = 0; s < 2; s++) {
        const LegCtl& l = r.leg[s];
        if (!l.ik) continue;
        int o = s ? 4 : 0;
        quat qp;
        vec3 hipP;
        boneModel(sk, out, B_THIGH_L + o, qp, hipP);
        vec3 mid = lerp(hipP, l.ankle, 0.5f);
        vec3 poleP = mid + normalize(l.knee) * 0.5f;
        solveTwoBoneIK(sk, out, (Bone)(B_THIGH_L + o), (Bone)(B_CALF_L + o), (Bone)(B_FOOT_L + o), l.ankle, poleP, 1.f);
        quat qc;
        vec3 tc;
        boneModel(sk, out, B_CALF_L + o, qc, tc);
        quat want = qz(l.yaw) * qx(l.pitch) * qy(l.roll);
        out.rot[B_FOOT_L + o] = normalize(conj(qc) * want);
        out.rot[B_TOE_L + o] = qx(l.toe);
    }
    // IK arms
    for (int s = 0; s < 2; s++) {
        const ArmCtl& a = r.arm[s];
        if (!a.ik) continue;
        int o = s ? 4 : 0;
        quat qu;
        vec3 sh;
        boneModel(sk, out, B_UPPERARM_L + o, qu, sh);
        vec3 poleP = lerp(sh, a.target, 0.5f) + normalize(a.ikPole) * 0.5f;
        solveTwoBoneIK(sk, out, (Bone)(B_UPPERARM_L + o), (Bone)(B_FOREARM_L + o), (Bone)(B_HAND_L + o), a.target, poleP, 1.f);
        if (a.orient) {
            quat qf;
            vec3 tf;
            boneModel(sk, out, B_FOREARM_L + o, qf, tf);
            out.rot[B_HAND_L + o] = normalize(conj(qf) * a.handRot);
        } else {
            // keep the wrist straight, apply the pronation share on the hand
            out.rot[B_HAND_L + o] = qaa(A.D.armDir[s], a.twist * 0.5f) * qaa(normalize(cross(A.D.armDir[s], A.D.palmN[s])), a.wristFlex);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Pose building helpers

static void standPose(const AuthorCtx& A, Rig& r) {
    r = Rig();
    for (int s = 0; s < 2; s++) {
        float sx = s ? 1.f : -1.f;
        LegCtl& l = r.leg[s];
        l.ik = true;
        float spread = Lerp(0.0f, -0.012f, A.fem);
        l.ankle = A.ankle[s] + vec3(sx * spread, 0.f, 0.f);
        l.yaw = -sx * 0.1f;
        l.knee = normalize(vec3(sx * 0.12f, 1.f, 0.f));
        ArmCtl& a = r.arm[s];
        a.dir = normalize(vec3(sx * (0.12f + 0.03f * (1.f - A.fem)), 0.03f, -1.f));
        a.pole = normalize(vec3(sx * 0.35f, -1.f, 0.f));
        a.elbow = 0.2f;
        a.fingers = 0.38f;
        a.thumb = 0.25f;
        a.twist = 0.1f;
    }
}

// Standing on both feet at given positions with the pelvis offset (for weight shift etc.)
static void setFootFlat(const AuthorCtx& A, LegCtl& l, vec3 groundPt, float yaw) {
    l.ik = true;
    l.ankle = vec3(groundPt.x, groundPt.y, A.footH + groundPt.z);
    l.pitch = 0.f;
    l.yaw = yaw;
    l.toe = 0.f;
}

// Ankle position for a foot with pitch `pitch`, pivoting on the heel (pivot = 0) or the ball (pivot = 1), where the
// pivot point touches the ground at (x, y, 0).
static vec3 ankleFromPivot(const AuthorCtx& A, float x, float y, float pitch, int pivot) {
    // vectors from the pivot to the ankle in the foot frame (y forward, z up)
    vec3 v = pivot == 0 ? vec3(0, A.heelBack * 0.9f, A.footH) : vec3(0, -A.ballFwd, A.footH - 0.012f * A.D.s);
    vec3 rv = rotate(qx(pitch), v);
    return vec3(x, y, 0.f) + rv;
}

static void armFK(ArmCtl& a, int side, float fwdSwing, float abd, float elbow, float twist, float fingers) {
    float sx = side ? 1.f : -1.f;
    float ca = cosf(abd);
    a.ik = false;
    a.dir = normalize(vec3(sx * sinf(abd), sinf(fwdSwing) * ca, -cosf(fwdSwing) * ca));
    // elbow points backwards and a bit out
    a.pole = normalize(vec3(sx * 0.35f, -cosf(fwdSwing), -sinf(fwdSwing)));
    a.elbow = elbow;
    a.twist = twist;
    a.fingers = fingers;
    a.thumb = fingers * 0.7f;
}

static void armIK(ArmCtl& a, vec3 target, vec3 pole, float fingers) {
    a.ik = true;
    a.target = target;
    a.ikPole = normalize(pole);
    a.fingers = fingers;
    a.thumb = fingers * 0.8f;
}

// ------------------------------------------------------------------------------------------------
// Gait generator

struct GaitP {
    float T = 1.1f;
    float speed = 1.4f;
    vec2 dir = vec2(0, 1);
    float duty = 0.62f;
    float lift = 0.07f;
    float kick = 0.f;
    float bob = 0.035f;
    float drop = 0.012f;
    float sway = 0.022f;
    float yawA = 0.07f, rollA = 0.05f;
    float lean = 0.04f;
    float chestYaw = 0.1f;
    float armSwing = 0.3f, armAbd = 0.12f, elbow = 0.25f, elbowSwing = 0.2f, fist = 0.4f;
    float footSpread = 0.1f;
    float strikePitch = 0.3f, toeOffPitch = -0.55f;
    bool run = false;
    float headBob = 1.f;
};

static float easeInOut(float t) { t = Saturate(t); return t * t * (3.f - 2.f * t); }
static float smoothPulse(float t) { t = Saturate(t); return sinf(kPi * t); }

static void gaitPose(const AuthorCtx& A, const GaitP& g, float phase, Rig& r) {
    standPose(A, r);
    const float T = g.T;
    const float v = g.speed;
    const float Sst = v * g.duty * T;   // distance the body travels during one stance
    vec3 dir3(g.dir.x, g.dir.y, 0.f);
    vec3 side3(g.dir.y, -g.dir.x, 0.f);   // perpendicular
    bool fwd = g.dir.y > 0.5f, back = g.dir.y < -0.5f, lateral = fabsf(g.dir.x) > 0.5f;
    float lowest = 1e9f;
    vec3 ankles[2];
    for (int s = 0; s < 2; s++) {
        float sx = s ? 1.f : -1.f;
        float p = phase - (s ? 0.5f : 0.f);
        p -= floorf(p);
        float xc = sx * g.footSpread;
        LegCtl& l = r.leg[s];
        l.ik = true;
        l.yaw = -sx * (lateral ? 0.02f : 0.08f);
        l.knee = normalize(vec3(sx * 0.1f, 1.f, 0.f));
        float along;       // position along travel direction of the contact/pivot
        vec3 ank;
        if (p < g.duty) {
            // stance: contact slides backwards (relative to the body) at the ground speed
            float u = p / g.duty;
            along = Sst * 0.5f - u * Sst;
            float pitch = 0.f, toe = 0.f;
            int pivot = 0;
            if (fwd) {
                // heel strike -> foot flat -> heel off (toe pivot)
                float uFlat = g.run ? 0.08f : 0.16f, uHeelOff = g.run ? 0.45f : 0.62f;
                if (u < uFlat) {
                    pitch = g.strikePitch * (1.f - easeInOut(u / uFlat));
                    pivot = 0;
                } else if (u < uHeelOff) {
                    pitch = 0.f;
                    pivot = 0;
                } else {
                    float k = (u - uHeelOff) / (1.f - uHeelOff);
                    pitch = g.toeOffPitch * easeInOut(k);
                    pivot = 1;
                    toe = -pitch;   // keep toes flat on the ground
                }
                // pivot contact point: heel point or ball point along the travel direction
                vec3 pc = vec3(xc, 0.f, 0.f) + dir3 * along;
                ank = ankleFromPivot(A, pc.x, pc.y, pitch, pivot);
            } else {
                // backwards / sideways: toe first contact, then flat
                if (back && u < 0.15f) pitch = -0.25f * (1.f - easeInOut(u / 0.15f));
                if (back && u > 0.75f) pitch = 0.1f * easeInOut((u - 0.75f) / 0.25f);
                vec3 pc = vec3(xc, 0.f, 0.f) + dir3 * along;
                ank = ankleFromPivot(A, pc.x, pc.y, pitch, back && u < 0.15f ? 1 : 0);
                if (back && u < 0.15f) toe = -pitch;
            }
            l.pitch = pitch;
            l.toe = toe;
            l.ankle = ank;
        } else {
            // swing: from toe-off pose to the next contact pose
            float u = (p - g.duty) / (1.f - g.duty);
            float startAlong = -Sst * 0.5f, endAlong = Sst * 0.5f;
            float e = easeInOut(u);
            along = Lerp(startAlong, endAlong, e);
            float pStart = fwd ? g.toeOffPitch : 0.f;
            float pEnd = fwd ? g.strikePitch : (back ? -0.25f : 0.f);
            vec3 a0, a1;
            {
                vec3 pc = vec3(xc, 0.f, 0.f) + dir3 * startAlong;
                a0 = ankleFromPivot(A, pc.x, pc.y, pStart, fwd ? 1 : 0);
                vec3 pe = vec3(xc, 0.f, 0.f) + dir3 * endAlong;
                a1 = ankleFromPivot(A, pe.x, pe.y, pEnd, fwd ? 0 : (back ? 1 : 0));
            }
            ank = lerp(a0, a1, e);
            float h = g.lift * powf(smoothPulse(u), 0.8f);
            if (g.run) h += g.kick * smoothPulse(Saturate(u * 1.6f)) * 0.9f;
            ank.z += h;
            if (g.run && fwd) ank -= dir3 * (g.kick * 0.6f * smoothPulse(Saturate(u * 1.3f)));   // heel kick behind
            if (lateral) ank += vec3(0, 1, 0) * (0.07f * smoothPulse(u));                        // swing passes in front
            float pm = fwd ? Lerp(pStart, pEnd, easeInOut(Saturate((u - 0.1f) / 0.8f))) : Lerp(pStart, pEnd, e);
            if (fwd && !g.run) pm += 0.12f * smoothPulse(u);   // toes up during mid swing
            l.pitch = pm;
            l.toe = fwd ? Max(0.f, -pm) * (1.f - u) : 0.f;
            l.ankle = ank;
        }
        ankles[s] = l.ankle;
        lowest = Min(lowest, l.ankle.z);
    }
    // pelvis
    float c2 = cosf(kTwoPi * phase * 2.f);
    float bobPhase = g.run ? cosf(kTwoPi * (phase - g.duty * 0.5f) * 2.f) : c2;
    float z = -g.drop - g.bob * (0.5f + 0.5f * bobPhase);
    float s1 = sinf(kTwoPi * phase), c1 = cosf(kTwoPi * phase);
    float fem = A.fem;
    r.pelvis = vec3(0, 0, z) + side3 * 0.f;
    r.pelvis.x += -g.sway * (1.f + 0.6f * fem) * s1 * (lateral ? 0.4f : 1.f);
    // reach limit: keep the stance legs slightly bent
    for (int it = 0; it < 2; it++) {
        for (int s = 0; s < 2; s++) {
            vec3 hip = A.hip[s] + r.pelvis;
            vec3 d = ankles[s] - hip;
            float L = A.legLen * 0.985f;
            float dz = sqrtf(Max(0.f, L * L - d.x * d.x - d.y * d.y));
            float maxZ = ankles[s].z + dz - A.hip[s].z;
            if (r.pelvis.z > maxZ) r.pelvis.z = maxZ;
        }
    }
    r.pelvisYaw = (fwd || back ? -1.f : 0.3f) * g.yawA * (1.f + 0.5f * fem) * c1 * (back ? -1.f : 1.f);
    r.pelvisRoll = g.rollA * (1.f + 0.8f * fem) * s1;
    r.pelvisPitch = g.lean * 0.4f;
    r.spinePitch = g.lean * 0.6f + (g.run ? 0.03f * c2 : 0.01f * c2);
    r.spineYaw = -r.pelvisYaw + g.chestYaw * c1 * (fwd ? 1.f : (back ? -1.f : 0.2f));
    r.spineRoll = -r.pelvisRoll * 0.7f;
    // head stabilization: keep facing forward and level
    r.neckYaw = -(r.pelvisYaw + r.spineYaw) * 0.45f;
    r.headYaw = -(r.pelvisYaw + r.spineYaw) * 0.5f;
    r.headPitch = -(r.pelvisPitch + r.spinePitch) * 0.55f * g.headBob + 0.03f;
    r.neckPitch = -(r.pelvisPitch + r.spinePitch) * 0.2f;
    r.headRoll = -(r.pelvisRoll + r.spineRoll) * 0.7f;
    // arms swing opposite to the same-side leg
    for (int s = 0; s < 2; s++) {
        float sx = s ? 1.f : -1.f;
        float w = (s ? 1.f : -1.f) * c1;   // + = forward
        if (back) w = -w;
        if (lateral) w *= 0.3f;
        float swing = g.armSwing * w + (g.run ? 0.1f : 0.02f);
        float elbow = g.elbow + g.elbowSwing * Max(0.f, w);
        float abd = g.armAbd * (1.f - 0.4f * fem) + (g.run ? 0.05f : 0.f);
        armFK(r.arm[s], s, swing, abd, elbow, g.run ? 0.35f : 0.12f, g.fist);
        if (g.run) r.arm[s].pole = normalize(vec3(sx * 0.5f, -1.f, 0.2f));
        // cross-body swing for running and women
        r.arm[s].dir = normalize(r.arm[s].dir + vec3(-sx * (0.1f * fem + (g.run ? 0.12f : 0.f)) * Max(0.f, w), 0, 0));
        r.arm[s].clavFwd = 0.04f * w;
        r.arm[s].clavUp = g.run ? 0.02f : 0.f;
    }
}

// ------------------------------------------------------------------------------------------------
// Clip table

static const ClipInfo kClipInfo[CLIP_COUNT] = {
    {"idle", 4.0f, true, 0.f},          {"idle_look", 6.0f, true, 0.f},      {"walk", 1.1f, true, 1.4f},
    {"jog", 0.74f, true, 3.0f},         {"run", 0.66f, true, 5.0f},          {"sprint", 0.6f, true, 7.0f},
    {"walk_back", 1.15f, true, 1.2f},   {"strafe_l", 0.9f, true, 1.3f},      {"strafe_r", 0.9f, true, 1.3f},
    {"crouch_idle", 3.0f, true, 0.f},   {"crouch_walk", 1.3f, true, 1.0f},   {"jump_start", 0.35f, false, 0.f},
    {"fall", 1.0f, true, 0.f},          {"land", 0.55f, false, 0.f},         {"aim_pistol", 2.0f, true, 0.f},
    {"aim_rifle", 2.0f, true, 0.f},     {"fire_pistol", 0.3f, false, 0.f},   {"fire_rifle", 0.12f, false, 0.f},
    {"reload", 1.9f, false, 0.f},       {"throw", 1.2f, false, 0.f},         {"punch_l", 0.5f, false, 0.f},
    {"punch_r", 0.62f, false, 0.f},     {"kick", 0.9f, false, 0.f},          {"block", 1.5f, true, 0.f},
    {"hit_front", 0.55f, false, 0.f},   {"hit_back", 0.55f, false, 0.f},     {"stagger", 1.2f, false, 0.f},
    {"death_front", 1.6f, false, 0.f},  {"death_back", 1.6f, false, 0.f},    {"sit_drive", 3.0f, true, 0.f},
    {"sit_passenger", 4.0f, true, 0.f}, {"ride_bike", 2.0f, true, 0.f},      {"enter_car_l", 1.8f, false, 0.f},
    {"exit_car_l", 1.6f, false, 0.f},   {"enter_car_r", 1.8f, false, 0.f},   {"exit_car_r", 1.6f, false, 0.f},
    {"swim_idle", 2.4f, true, 0.f},     {"swim", 1.4f, true, 1.2f},          {"climb", 1.2f, true, 0.f},
    {"vault", 0.9f, false, 0.f},        {"cower", 2.0f, true, 0.f},          {"hands_up", 3.0f, true, 0.f},
    {"flee", 0.64f, true, 5.5f},        {"talk", 5.0f, true, 0.f},           {"talk_phone", 6.0f, true, 0.f},
    {"sit_bench", 5.0f, true, 0.f},     {"smoke", 8.0f, true, 0.f},          {"dance", 2.0f, true, 0.f},
    {"wave", 1.6f, true, 0.f},          {"point", 2.5f, true, 0.f},          {"cheer", 1.6f, true, 0.f},
    {"lean_wall", 5.0f, true, 0.f},     {"sunbathe", 6.0f, true, 0.f},       {"jog_idle", 0.8f, true, 0.f},
    {"get_up_front", 2.2f, false, 0.f}, {"get_up_back", 2.0f, false, 0.f},
};

// ------------------------------------------------------------------------------------------------
// Keyframe helper

struct Key {
    float t;
    Rig r;
};

static Rig sampleKeys(const std::vector<Key>& keys, float t, bool loop, float duration) {
    if (keys.size() == 1) return keys[0].r;
    if (loop) {
        t = t - floorf(t / duration) * duration;
        // treat the sequence as cyclic: last key -> first key at duration
        for (size_t i = 0; i < keys.size(); i++) {
            float t0 = keys[i].t, t1 = i + 1 < keys.size() ? keys[i + 1].t : duration + keys[0].t;
            float tt = t < keys[0].t ? t + duration : t;
            if (tt >= t0 && tt <= t1) {
                const Rig& b = i + 1 < keys.size() ? keys[i + 1].r : keys[0].r;
                return lerpRig(keys[i].r, b, easeInOut((tt - t0) / Max(t1 - t0, 1e-4f)));
            }
        }
        return keys[0].r;
    }
    if (t <= keys[0].t) return keys[0].r;
    for (size_t i = 0; i + 1 < keys.size(); i++)
        if (t <= keys[i + 1].t) return lerpRig(keys[i].r, keys[i + 1].r, easeInOut((t - keys[i].t) / Max(keys[i + 1].t - keys[i].t, 1e-4f)));
    return keys.back().r;
}

// ------------------------------------------------------------------------------------------------
// Common poses

static void guardPose(const AuthorCtx& A, Rig& r) {
    standPose(A, r);
    // fighting stance: left foot forward, knees bent, fists up
    setFootFlat(A, r.leg[0], vec3(A.ankle[0].x - 0.02f, 0.14f * A.D.s, 0.f), 0.15f);
    setFootFlat(A, r.leg[1], vec3(A.ankle[1].x + 0.04f, -0.12f * A.D.s, 0.f), -0.45f);
    r.pelvis = vec3(0, 0, -0.06f * A.D.s);
    r.pelvisYaw = -0.25f;
    r.spineYaw = 0.1f;
    r.spinePitch = 0.12f;
    r.headPitch = 0.05f;
    r.headYaw = 0.12f;
    vec3 c = A.chestP;
    armIK(r.arm[0], vec3(-0.07f, c.y + 0.32f, A.shoulderZ + 0.02f), vec3(-1, -0.3f, -1.f), 0.95f);
    armIK(r.arm[1], vec3(0.05f, c.y + 0.24f, A.shoulderZ + 0.05f), vec3(1, -0.3f, -1.f), 0.95f);
    r.arm[0].twist = r.arm[1].twist = 0.9f;
}

static void crouchPose(const AuthorCtx& A, Rig& r, float depth) {
    standPose(A, r);
    const float s = A.D.s;
    r.pelvis = vec3(0, -0.1f * depth * s, -0.42f * depth * s);
    r.pelvisPitch = 0.25f * depth;
    r.spinePitch = 0.35f * depth;
    r.headPitch = -0.35f * depth;
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        setFootFlat(A, r.leg[sd], vec3(A.ankle[sd].x + sx * 0.04f * s, (sd ? -0.08f : 0.08f) * s * depth, 0.f), -sx * 0.2f);
        r.leg[sd].knee = normalize(vec3(sx * 0.35f, 1.f, 0.f));
        armFK(r.arm[sd], sd, 0.35f * depth, 0.15f, 0.8f * depth + 0.2f, 0.4f, 0.5f);
    }
}

// Lying on the back (supine) or face down (prone). Body extends along -Y (back) / +Y (front) from the pelvis.
static void lyingPose(const AuthorCtx& A, Rig& r, bool onBack, float variant) {
    r = Rig();
    const float s = A.D.s;
    float pz = A.D.hipDepth * 0.95f;
    const vec3 pelvisBind = A.sk.bindLocalPos[B_PELVIS];
    if (onBack) {
        r.pelvisPitch = -kHalfPi;           // tip backwards: head towards -Y, face up
        r.pelvis = vec3(0, -0.3f * s, pz - pelvisBind.z);
        r.headYaw = 0.35f * (variant - 0.5f) * 2.f;
        r.headPitch = -0.12f;
        r.spinePitch = 0.03f;
    } else {
        r.pelvisPitch = kHalfPi;            // tip forwards: head towards +Y, face down
        r.pelvis = vec3(0, 0.3f * s, pz - pelvisBind.z);
        r.headYaw = (variant > 0.5f ? 1.f : -1.f) * 1.2f;   // cheek on the ground
        r.headPitch = -0.35f;
        r.neckPitch = -0.2f;
        r.spinePitch = -0.06f;
    }
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        LegCtl& l = r.leg[sd];
        l.ik = false;
        l.hipFlex = onBack ? 0.08f + 0.1f * variant * sd : -0.05f;
        l.hipAbd = 0.12f + 0.08f * sd;
        l.kneeFlex = onBack ? 0.15f + 0.25f * variant * (1 - sd) : 0.1f;
        l.ankleFlex = onBack ? -0.55f : -1.1f;
        ArmCtl& a = r.arm[sd];
        a.ik = false;
        // arms relative to the chest frame
        if (onBack) {
            a.dir = normalize(vec3(sx * 0.9f, 0.1f + 0.3f * variant * sd, -0.5f));
            a.pole = vec3(0, 0, -1);
        } else {
            a.dir = normalize(vec3(sx * 0.7f, 0.4f * (sd ? variant : 1.f - variant), -0.7f));
            a.pole = vec3(0, -1, 0);
        }
        a.elbow = 0.4f + 0.3f * sd;
        a.fingers = 0.4f;
        a.twist = 0.3f;
    }
}

static void seatedPose(const AuthorCtx& A, Rig& r, float hipH, float feetY, float recline) {
    standPose(A, r);
    const float s = A.D.s;
    float hipBindZ = A.hip[0].z;
    r.pelvis = vec3(0, -0.02f * s, hipH - hipBindZ);
    r.pelvisPitch = -0.28f - recline * 0.5f;     // posterior tilt when sitting
    r.spinePitch = 0.18f - recline * 0.5f;
    r.headPitch = 0.05f + recline * 0.4f;
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        setFootFlat(A, r.leg[sd], vec3(A.ankle[sd].x + sx * 0.05f * s, feetY, 0.f), -sx * 0.12f);
        r.leg[sd].knee = normalize(vec3(sx * 0.15f, 0.4f, 1.f));
    }
}

// ------------------------------------------------------------------------------------------------
// Individual clips

static void clipIdle(const AuthorCtx& A, float t, float dur, Rig& r, bool look) {
    standPose(A, r);
    const float s = A.D.s;
    float w = sinf(kTwoPi * t / dur);                 // weight shift left/right
    float br = sinf(kTwoPi * t / (dur * 0.5f));      // breathing (2 breaths per cycle)
    float fem = A.fem;
    // weight on one leg: pelvis shifts over it, the free hip drops, knee relaxes
    r.pelvis = vec3(-0.022f * w * s, 0.f, -0.008f * s - 0.006f * s * fabsf(w));
    r.pelvisRoll = 0.04f * w * (1.f + 0.8f * fem);
    r.pelvisYaw = 0.03f * w;
    r.spineRoll = -0.05f * w;
    r.spinePitch = 0.015f * br;
    r.headRoll = 0.02f * w;
    r.headPitch = 0.02f + 0.01f * sinf(kTwoPi * t / dur * 3.f);
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        ArmCtl& a = r.arm[sd];
        a.clavUp = 0.02f * br;
        armFK(a, sd, 0.03f + 0.025f * sinf(kTwoPi * t / dur + sd), 0.1f + 0.02f * (1.f - fem), 0.2f + 0.05f * sd, 0.1f, 0.38f);
        // relaxed leg forward slightly when unweighted
        float unweighted = Saturate(sx * w);
        r.leg[sd].ankle += vec3(0, 0.02f * unweighted * s, 0.f);
        r.leg[sd].yaw += -sx * 0.1f * unweighted;
    }
    if (look) {
        float u = t / dur;
        float yaw = 0.f;
        yaw += 0.9f * sstep(0.12f, 0.22f, u) * (1.f - sstep(0.35f, 0.45f, u));
        yaw -= 0.8f * sstep(0.55f, 0.65f, u) * (1.f - sstep(0.8f, 0.9f, u));
        r.headYaw = yaw * 0.6f;
        r.neckYaw = yaw * 0.35f;
        r.spineYaw = yaw * 0.12f;
        r.eyes = vec2(yaw * 0.25f, 0.f);
        r.headPitch += -0.05f * sstep(0.55f, 0.65f, u);
    }
}

static void clipLocomotion(const AuthorCtx& A, Clip c, float t, Rig& r) {
    GaitP g;
    const ClipInfo& ci = kClipInfo[c];
    g.T = ci.duration;
    g.speed = ci.speed;
    const float s = A.D.s;
    float phase = t / ci.duration;
    switch (c) {
        case CLIP_WALK: break;
        case CLIP_JOG:
            g.duty = 0.4f; g.lift = 0.1f; g.kick = 0.1f; g.bob = 0.05f; g.drop = 0.04f; g.sway = 0.012f; g.yawA = 0.1f; g.rollA = 0.04f;
            g.lean = 0.12f; g.chestYaw = 0.16f; g.armSwing = 0.55f; g.elbow = 1.35f; g.elbowSwing = 0.15f; g.fist = 0.75f; g.footSpread = 0.07f;
            g.strikePitch = 0.15f; g.toeOffPitch = -0.6f; g.run = true;
            break;
        case CLIP_RUN:
            g.duty = 0.35f; g.lift = 0.13f; g.kick = 0.2f; g.bob = 0.06f; g.drop = 0.05f; g.sway = 0.01f; g.yawA = 0.12f; g.rollA = 0.04f;
            g.lean = 0.2f; g.chestYaw = 0.2f; g.armSwing = 0.8f; g.elbow = 1.45f; g.elbowSwing = 0.2f; g.fist = 0.85f; g.footSpread = 0.06f;
            g.strikePitch = 0.1f; g.toeOffPitch = -0.7f; g.run = true;
            break;
        case CLIP_SPRINT:
            g.duty = 0.28f; g.lift = 0.16f; g.kick = 0.3f; g.bob = 0.06f; g.drop = 0.06f; g.sway = 0.008f; g.yawA = 0.13f; g.rollA = 0.03f;
            g.lean = 0.32f; g.chestYaw = 0.22f; g.armSwing = 1.1f; g.elbow = 1.5f; g.elbowSwing = 0.25f; g.fist = 0.9f; g.footSpread = 0.05f;
            g.strikePitch = 0.05f; g.toeOffPitch = -0.8f; g.run = true;
            break;
        case CLIP_FLEE:
            g.duty = 0.35f; g.lift = 0.12f; g.kick = 0.18f; g.bob = 0.06f; g.drop = 0.05f; g.lean = 0.12f; g.chestYaw = 0.25f; g.armSwing = 1.0f;
            g.elbow = 0.9f; g.elbowSwing = 0.5f; g.fist = 0.3f; g.footSpread = 0.07f; g.strikePitch = 0.1f; g.toeOffPitch = -0.7f; g.run = true;
            g.armAbd = 0.35f;
            break;
        case CLIP_WALK_BACK:
            g.dir = vec2(0, -1); g.duty = 0.65f; g.lift = 0.05f; g.bob = 0.025f; g.armSwing = 0.18f; g.lean = 0.08f; g.yawA = 0.05f;
            break;
        case CLIP_STRAFE_L: case CLIP_STRAFE_R:
            g.dir = vec2(c == CLIP_STRAFE_L ? -1.f : 1.f, 0.f); g.duty = 0.58f; g.lift = 0.06f; g.bob = 0.02f; g.sway = 0.0f;
            g.armSwing = 0.12f; g.footSpread = 0.13f; g.yawA = 0.03f; g.rollA = 0.03f;
            break;
        case CLIP_CROUCH_WALK:
            g.duty = 0.66f; g.lift = 0.06f; g.bob = 0.02f; g.drop = 0.36f * s; g.sway = 0.03f; g.lean = 0.45f; g.armSwing = 0.15f; g.elbow = 0.9f;
            g.footSpread = 0.13f; g.strikePitch = 0.15f; g.toeOffPitch = -0.35f; g.headBob = 1.4f;
            break;
        default: break;
    }
    gaitPose(A, g, phase, r);
    if (c == CLIP_CROUCH_WALK) {
        for (int sd = 0; sd < 2; sd++) r.leg[sd].knee = normalize(vec3((sd ? 1.f : -1.f) * 0.35f, 1.f, 0.f));
        r.headPitch -= 0.35f;
    }
    if (c == CLIP_FLEE) {
        // panic: look back over the shoulder periodically, arms raised
        float look = sinf(kTwoPi * phase) > 0.6f ? 1.f : 0.f;
        r.headYaw += 0.9f * look;
        r.neckYaw += 0.4f * look;
        for (int sd = 0; sd < 2; sd++) r.arm[sd].dir = normalize(r.arm[sd].dir + vec3(0, 0.1f, 0.35f));
    }
}

static void clipCrouchIdle(const AuthorCtx& A, float t, float dur, Rig& r) {
    crouchPose(A, r, 1.f);
    float br = sinf(kTwoPi * t / (dur * 0.5f));
    r.spinePitch += 0.02f * br;
    r.headYaw = 0.25f * sinf(kTwoPi * t / dur);
    for (int sd = 0; sd < 2; sd++) {
        vec3 knee = A.hip[sd] + r.pelvis + vec3(0, 0.35f * A.D.s, -0.15f * A.D.s);
        armIK(r.arm[sd], knee + vec3(0, 0.02f, 0.04f), vec3((sd ? 1.f : -1.f), -0.5f, -0.5f), 0.5f);
    }
}

static void clipJump(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    if (c == CLIP_JUMP_START) {
        Rig a, b, e;
        standPose(A, a);
        crouchPose(A, b, 0.35f);
        for (int sd = 0; sd < 2; sd++) armFK(b.arm[sd], sd, -0.5f, 0.2f, 0.3f, 0.2f, 0.5f);
        standPose(A, e);
        e.pelvis = vec3(0, 0.02f * s, 0.12f * s);
        e.spinePitch = -0.05f;
        for (int sd = 0; sd < 2; sd++) {
            armFK(e.arm[sd], sd, 1.2f, 0.25f, 0.5f, 0.2f, 0.5f);
            e.leg[sd].pitch = -0.7f;
            e.leg[sd].ankle = ankleFromPivot(A, A.ankle[sd].x, A.ankle[sd].y + 0.03f, -0.7f, 1) + vec3(0, 0, 0.1f * s);
            e.leg[sd].toe = 0.2f;
        }
        std::vector<Key> k = {{0.f, a}, {0.15f, b}, {0.35f, e}};
        r = sampleKeys(k, t, false, 0.35f);
    } else if (c == CLIP_FALL) {
        standPose(A, r);
        float w = sinf(kTwoPi * t);
        r.pelvis = vec3(0, 0, 0.05f * s);
        r.spinePitch = 0.1f;
        r.headPitch = -0.15f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            LegCtl& l = r.leg[sd];
            l.ik = false;
            l.hipFlex = 0.35f + 0.25f * sx * w;
            l.kneeFlex = 0.6f - 0.2f * sx * w;
            l.ankleFlex = -0.3f;
            l.hipAbd = 0.1f;
            armFK(r.arm[sd], sd, 0.3f + 0.2f * sx * w, 0.9f + 0.15f * w, 0.5f, 0.2f, 0.3f);
        }
    } else {   // LAND
        Rig a, b, e;
        standPose(A, a);
        a.pelvis = vec3(0, 0, 0.02f * s);
        for (int sd = 0; sd < 2; sd++) armFK(a.arm[sd], sd, 0.3f, 0.6f, 0.5f, 0.2f, 0.3f);
        crouchPose(A, b, 0.55f);
        for (int sd = 0; sd < 2; sd++) armFK(b.arm[sd], sd, 0.6f, 0.35f, 0.6f, 0.2f, 0.5f);
        standPose(A, e);
        std::vector<Key> k = {{0.f, a}, {0.12f, b}, {0.55f, e}};
        r = sampleKeys(k, t, false, 0.55f);
    }
}

// Weapon holding: aim poses (pitch is applied by the animator on the spine).
static void aimPistolPose(const AuthorCtx& A, Rig& r) {
    standPose(A, r);
    const float s = A.D.s;
    setFootFlat(A, r.leg[0], vec3(A.ankle[0].x - 0.02f, 0.1f * s, 0.f), 0.1f);
    setFootFlat(A, r.leg[1], vec3(A.ankle[1].x + 0.03f, -0.07f * s, 0.f), -0.3f);
    r.pelvis = vec3(0, 0, -0.03f * s);
    r.pelvisYaw = -0.1f;
    r.spinePitch = 0.08f;
    r.spineYaw = 0.1f;
    r.headPitch = 0.08f;
    vec3 grip(0.04f * s, A.chestP.y + 0.48f * s, A.shoulderZ - 0.02f * s);
    armIK(r.arm[1], grip, vec3(1, -0.2f, -1), 0.9f);
    armIK(r.arm[0], grip + vec3(-0.035f * s, -0.01f * s, -0.015f * s), vec3(-1, -0.2f, -1), 0.8f);
    // right hand orientation: fingers wrap the grip, barrel forward
    r.arm[1].orient = true;
    r.arm[1].handRot = quatFromTwoPairs(A.D.armDir[1], A.D.palmN[1], normalize(vec3(0.15f, 0.55f, -0.8f)), normalize(vec3(-1, 0.1f, -0.2f)));
    r.arm[0].orient = true;
    r.arm[0].handRot = quatFromTwoPairs(A.D.armDir[0], A.D.palmN[0], normalize(vec3(0.5f, 0.6f, -0.5f)), normalize(vec3(1, -0.1f, 0.3f)));
}

static void aimRiflePose(const AuthorCtx& A, Rig& r) {
    standPose(A, r);
    const float s = A.D.s;
    setFootFlat(A, r.leg[0], vec3(A.ankle[0].x - 0.03f, 0.14f * s, 0.f), 0.25f);
    setFootFlat(A, r.leg[1], vec3(A.ankle[1].x + 0.03f, -0.1f * s, 0.f), -0.45f);
    r.pelvis = vec3(0, 0, -0.04f * s);
    r.pelvisYaw = -0.3f;
    r.spineYaw = 0.22f;
    r.spinePitch = 0.1f;
    r.headYaw = 0.08f;
    r.headRoll = 0.12f;
    r.headPitch = 0.12f;
    vec3 sh = A.gh[1];
    vec3 grip(sh.x - 0.05f * s, A.chestP.y + 0.2f * s, A.shoulderZ - 0.08f * s);
    vec3 fore(-0.04f * s, A.chestP.y + 0.47f * s, A.shoulderZ - 0.1f * s);
    armIK(r.arm[1], grip, vec3(1, -0.2f, -0.6f), 0.85f);
    armIK(r.arm[0], fore, vec3(-0.5f, -0.3f, -1.f), 0.75f);
    r.arm[1].orient = true;
    r.arm[1].handRot = quatFromTwoPairs(A.D.armDir[1], A.D.palmN[1], normalize(vec3(0.05f, 0.6f, -0.8f)), normalize(vec3(-1, 0.1f, 0.f)));
    r.arm[0].twist = -0.6f;
    r.arm[1].clavFwd = 0.1f;
    r.arm[0].clavFwd = 0.12f;
}

static void clipCombat(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    Rig g;
    guardPose(A, g);
    vec3 cp = A.chestP;
    switch (c) {
        case CLIP_PUNCH_L: {
            Rig wind = g, hit = g;
            wind.spineYaw = 0.2f;
            hit.spineYaw = -0.25f;
            hit.pelvisYaw = -0.35f;
            hit.arm[0].target = vec3(-0.02f * s, cp.y + 0.62f * s, A.shoulderZ + 0.0f);
            hit.arm[0].twist = 1.4f;
            hit.leg[0].ankle += vec3(0, 0.05f * s, 0);
            hit.headYaw = 0.2f;
            std::vector<Key> k = {{0.f, g}, {0.08f, wind}, {0.18f, hit}, {0.24f, hit}, {0.5f, g}};
            r = sampleKeys(k, t, false, 0.5f);
            break;
        }
        case CLIP_PUNCH_R: {
            Rig wind = g, hit = g;
            wind.spineYaw = 0.3f;
            wind.pelvisYaw = -0.1f;
            wind.arm[1].target += vec3(0.02f, -0.05f, 0.f) * s;
            hit.spineYaw = -0.45f;
            hit.pelvisYaw = -0.5f;
            hit.pelvis += vec3(0, 0.04f * s, 0);
            hit.arm[1].target = vec3(-0.04f * s, cp.y + 0.64f * s, A.shoulderZ + 0.0f);
            hit.arm[1].twist = 1.5f;
            hit.arm[0].target += vec3(0, -0.06f, -0.03f) * s;
            hit.leg[1].pitch = -0.6f;
            hit.leg[1].ankle = ankleFromPivot(A, g.leg[1].ankle.x, g.leg[1].ankle.y + 0.1f * s, -0.6f, 1);
            hit.leg[1].toe = 0.6f;
            hit.headYaw = 0.35f;
            std::vector<Key> k = {{0.f, g}, {0.12f, wind}, {0.24f, hit}, {0.31f, hit}, {0.62f, g}};
            r = sampleKeys(k, t, false, 0.62f);
            break;
        }
        case CLIP_KICK: {
            Rig chamber = g, ext = g, back = g;
            chamber.leg[1].ik = true;
            vec3 hip = A.hip[1];
            chamber.leg[1].ankle = vec3(hip.x, hip.y + 0.25f * s, hip.z - 0.38f * s);
            chamber.leg[1].pitch = -0.6f;
            chamber.leg[1].knee = vec3(0, 0.3f, 1.f);
            chamber.spinePitch = -0.1f;
            chamber.pelvisPitch = -0.15f;
            ext = chamber;
            ext.leg[1].ankle = vec3(hip.x - 0.02f * s, hip.y + 0.72f * s, hip.z - 0.1f * s);
            ext.leg[1].pitch = -0.9f;
            ext.leg[1].knee = vec3(0, 0.2f, 1.f);
            ext.pelvis += vec3(0, -0.05f * s, 0);
            ext.spinePitch = -0.25f;
            ext.arm[1].target += vec3(0.08f, -0.1f, -0.05f) * s;
            back = chamber;
            std::vector<Key> k = {{0.f, g}, {0.2f, chamber}, {0.35f, ext}, {0.45f, ext}, {0.62f, back}, {0.9f, g}};
            r = sampleKeys(k, t, false, 0.9f);
            break;
        }
        case CLIP_BLOCK: {
            r = g;
            vec3 face(0, cp.y + 0.22f * s, A.headP.z - 0.02f * s);
            armIK(r.arm[0], face + vec3(-0.06f, 0.02f, 0.02f) * s, vec3(-0.3f, 0.f, -1.f), 0.95f);
            armIK(r.arm[1], face + vec3(0.06f, 0.0f, 0.0f) * s, vec3(0.3f, 0.f, -1.f), 0.95f);
            r.arm[0].twist = r.arm[1].twist = 0.3f;
            r.headPitch = 0.25f;
            r.spinePitch = 0.2f;
            r.pelvis.z -= 0.02f * s * (1.f + sinf(kTwoPi * t / 1.5f));
            break;
        }
        default: r = g; break;
    }
}

static void clipHit(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    Rig a;
    standPose(A, a);
    if (c == CLIP_HIT_FRONT || c == CLIP_HIT_BACK) {
        float dir = c == CLIP_HIT_FRONT ? -1.f : 1.f;   // torso snaps backwards for front hits
        Rig h = a;
        h.spinePitch = 0.28f * dir;
        h.neckPitch = 0.2f * dir;
        h.headPitch = 0.25f * dir;
        h.pelvis = vec3(0, 0.05f * s * dir, -0.04f * s);
        for (int sd = 0; sd < 2; sd++) armFK(h.arm[sd], sd, dir > 0.f ? -0.4f : 0.6f, 0.35f, 0.7f, 0.2f, 0.6f);
        Rig st = a;
        setFootFlat(A, st.leg[0], vec3(A.ankle[0].x, 0.16f * s * dir, 0.f), 0.1f);
        st.pelvis = vec3(0, 0.08f * s * dir, -0.05f * s);
        st.spinePitch = 0.1f * dir;
        std::vector<Key> k = {{0.f, a}, {0.08f, h}, {0.28f, st}, {0.55f, a}};
        r = sampleKeys(k, t, false, 0.55f);
    } else {   // STAGGER backwards with flailing arms
        Rig k1 = a, k2 = a, k3 = a;
        k1.spinePitch = -0.3f;
        k1.headPitch = -0.3f;
        for (int sd = 0; sd < 2; sd++) armFK(k1.arm[sd], sd, 0.8f, 0.7f, 0.5f, 0.2f, 0.3f);
        setFootFlat(A, k1.leg[1], vec3(A.ankle[1].x, -0.22f * s, 0.f), -0.2f);
        k1.pelvis = vec3(0, -0.1f * s, -0.04f * s);
        k2 = k1;
        setFootFlat(A, k2.leg[0], vec3(A.ankle[0].x, -0.42f * s, 0.f), 0.2f);
        k2.pelvis = vec3(0.03f * s, -0.33f * s, -0.06f * s);
        for (int sd = 0; sd < 2; sd++) armFK(k2.arm[sd], sd, 0.2f, 1.1f, 0.4f, 0.2f, 0.3f);
        k3 = a;
        for (int sd = 0; sd < 2; sd++) {
            setFootFlat(A, k3.leg[sd], vec3(A.ankle[sd].x, -0.45f * s, 0.f), (sd ? -0.1f : 0.1f));
        }
        k3.pelvis = vec3(0, -0.45f * s, -0.01f * s);
        std::vector<Key> k = {{0.f, a}, {0.25f, k1}, {0.6f, k2}, {1.2f, k3}};
        r = sampleKeys(k, t, false, 1.2f);
    }
}

static void clipDeath(const AuthorCtx& A, Clip c, float t, Rig& r, u32 var) {
    const float s = A.D.s;
    bool front = c == CLIP_DEATH_FRONT;   // hit from the front: falls on the back
    float v = (var & 1) ? 0.3f : 0.7f;
    Rig a, hit, knees, ground, end;
    standPose(A, a);
    hit = a;
    float dir = front ? -1.f : 1.f;
    hit.spinePitch = 0.35f * dir;
    hit.headPitch = 0.4f * dir;
    hit.pelvis = vec3(0, 0.06f * s * -dir, -0.05f * s);
    for (int sd = 0; sd < 2; sd++) armFK(hit.arm[sd], sd, front ? 0.9f : -0.3f, 0.5f, 0.6f, 0.3f, 0.4f);
    knees = hit;
    knees.pelvis = vec3(0, 0.1f * s * -dir, -0.45f * s);
    knees.pelvisPitch = front ? -0.5f : 0.6f;
    knees.spinePitch = front ? -0.2f : 0.3f;
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        setFootFlat(A, knees.leg[sd], vec3(A.ankle[sd].x + sx * 0.03f, (front ? 0.08f : -0.05f) * s, 0.f), -sx * 0.2f);
        knees.leg[sd].knee = normalize(vec3(sx * 0.3f, 1.f, 0.3f));
        armFK(knees.arm[sd], sd, front ? 0.4f : 0.9f, 0.6f, 0.4f, 0.3f, 0.3f);
    }
    lyingPose(A, end, front, v);
    ground = lerpRig(knees, end, 0.6f);
    ground.pelvis = lerp(knees.pelvis, end.pelvis, 0.8f);
    Rig settle = end;
    settle.pelvis.z += 0.02f * s;
    settle.headPitch += front ? -0.1f : 0.05f;
    std::vector<Key> k = {{0.f, a}, {0.1f, hit}, {0.45f, knees}, {0.85f, ground}, {1.15f, settle}, {1.6f, end}};
    r = sampleKeys(k, t, false, 1.6f);
}

static void clipGetUp(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    Rig end;
    standPose(A, end);
    if (c == CLIP_GET_UP_BACK) {
        Rig lie, sit, kneel, rise;
        lyingPose(A, lie, true, 0.5f);
        // sit up: pelvis on the ground, torso upright, legs forward
        sit = Rig();
        sit.pelvis = vec3(0, -0.15f * s, A.D.hipDepth * 0.9f - A.sk.bindLocalPos[B_PELVIS].z);
        sit.pelvisPitch = -0.9f;
        sit.spinePitch = 0.5f;
        sit.headPitch = 0.2f;
        for (int sd = 0; sd < 2; sd++) {
            LegCtl& l = sit.leg[sd];
            l.ik = false;
            l.hipFlex = 0.9f;
            l.kneeFlex = 1.2f;
            l.ankleFlex = -0.2f;
            l.hipAbd = 0.15f;
            armFK(sit.arm[sd], sd, -0.6f, 0.35f, 0.2f, 0.2f, 0.4f);
        }
        kneel = end;
        kneel.pelvis = vec3(0, -0.05f * s, -0.5f * s);
        kneel.pelvisPitch = 0.2f;
        kneel.spinePitch = 0.5f;
        setFootFlat(A, kneel.leg[0], vec3(A.ankle[0].x, 0.2f * s, 0.f), 0.f);
        kneel.leg[1].ankle = vec3(A.ankle[1].x, -0.35f * s, 0.07f * s);
        kneel.leg[1].pitch = -1.2f;
        kneel.leg[1].toe = 1.0f;
        for (int sd = 0; sd < 2; sd++) armFK(kneel.arm[sd], sd, 0.5f, 0.2f, 0.4f, 0.2f, 0.5f);
        rise = end;
        rise.pelvis = vec3(0, 0.02f * s, -0.15f * s);
        rise.spinePitch = 0.3f;
        std::vector<Key> k = {{0.f, lie}, {0.6f, sit}, {1.15f, kneel}, {1.6f, rise}, {2.0f, end}};
        r = sampleKeys(k, t, false, 2.0f);
    } else {
        Rig lie, push, fours, kneel, rise;
        lyingPose(A, lie, false, 0.5f);
        push = lie;
        push.pelvis.z += 0.12f * s;
        push.pelvisPitch = kHalfPi * 0.85f;
        push.headYaw = 0.f;
        push.headPitch = -0.5f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            push.arm[sd].dir = normalize(vec3(sx * 0.3f, -0.1f, -1.f));
            push.arm[sd].pole = vec3(0, -1, 0);
            push.arm[sd].elbow = 0.2f;
        }
        fours = Rig();
        fours.pelvis = vec3(0, -0.05f * s, -0.45f * s);
        fours.pelvisPitch = 1.2f;
        fours.spinePitch = 0.2f;
        fours.headPitch = -0.6f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            LegCtl& l = fours.leg[sd];
            l.ik = false;
            l.hipFlex = 1.5f;
            l.kneeFlex = 1.9f;
            l.ankleFlex = -1.0f;
            l.hipAbd = 0.12f;
            fours.arm[sd].dir = normalize(vec3(sx * 0.25f, 0.1f, -1.f));
            fours.arm[sd].pole = vec3(0, -1, 0);
            fours.arm[sd].elbow = 0.1f;
            fours.arm[sd].ik = false;
        }
        kneel = end;
        kneel.pelvis = vec3(0, -0.05f * s, -0.5f * s);
        kneel.pelvisPitch = 0.25f;
        kneel.spinePitch = 0.5f;
        setFootFlat(A, kneel.leg[0], vec3(A.ankle[0].x, 0.2f * s, 0.f), 0.f);
        kneel.leg[1].ankle = vec3(A.ankle[1].x, -0.35f * s, 0.07f * s);
        kneel.leg[1].pitch = -1.2f;
        kneel.leg[1].toe = 1.0f;
        for (int sd = 0; sd < 2; sd++) armFK(kneel.arm[sd], sd, 0.6f, 0.2f, 0.4f, 0.2f, 0.5f);
        rise = end;
        rise.pelvis = vec3(0, 0.02f * s, -0.15f * s);
        rise.spinePitch = 0.3f;
        std::vector<Key> k = {{0.f, lie}, {0.45f, push}, {0.95f, fours}, {1.45f, kneel}, {1.85f, rise}, {2.2f, end}};
        r = sampleKeys(k, t, false, 2.2f);
    }
}

static void drivePose(const AuthorCtx& A, Rig& r, float t) {
    const float s = A.D.s;
    seatedPose(A, r, 0.45f * s, 0.6f * s, 0.25f);
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        r.leg[sd].ankle.z += 0.02f * s;
        r.leg[sd].pitch = 0.35f;
    }
    // steering wheel ~0.35 m in front of the chest, 0.25 m below shoulder height, tilted towards the driver
    vec3 wc(0.f, A.chestP.y + 0.35f * s, A.shoulderZ - 0.25f * s + r.pelvis.z * 0.9f);
    float steer = 0.05f * sinf(kTwoPi * t / 3.f);
    float R = 0.18f * s;
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        float ang = sx * 1.05f + steer;   // 10 and 2 o'clock
        vec3 hp = wc + vec3(sinf(ang) * R, -0.06f * cosf(ang), cosf(ang) * R * 0.9f);
        armIK(r.arm[sd], hp, vec3(sx * 1.f, -0.3f, -1.f), 0.85f);
        r.arm[sd].twist = 0.6f;
    }
    r.headPitch = -0.05f + 0.02f * sinf(kTwoPi * t / 3.f * 2.f);
    r.headYaw = 0.1f * sinf(kTwoPi * t / 3.f);
}

static void passengerPose(const AuthorCtx& A, Rig& r, float t, float dur) {
    const float s = A.D.s;
    seatedPose(A, r, 0.45f * s, 0.55f * s, 0.3f);
    float w = sinf(kTwoPi * t / dur);
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        vec3 thigh = A.hip[sd] + r.pelvis + vec3(sx * 0.02f, 0.28f * s, 0.07f * s);
        armIK(r.arm[sd], thigh, vec3(sx, -0.5f, -0.3f), 0.4f);
        r.arm[sd].twist = 0.8f;
    }
    r.headYaw = 0.5f * sstep(0.3f, 0.5f, 0.5f + 0.5f * w) - 0.15f;
    r.headPitch = 0.05f;
}

static void bikePose(const AuthorCtx& A, Rig& r, float t) {
    const float s = A.D.s;
    seatedPose(A, r, 0.78f * s, 0.12f * s, 0.f);
    r.pelvisPitch = 0.1f;
    r.spinePitch = 0.45f;
    r.headPitch = -0.45f;
    r.pelvis.y -= 0.06f * s;
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        // feet on pegs, knees gripping the tank
        r.leg[sd].ankle = vec3(sx * 0.2f * s, 0.05f * s, 0.38f * s + A.footH);
        r.leg[sd].pitch = -0.1f;
        r.leg[sd].knee = normalize(vec3(sx * 0.2f, 1.f, 0.4f));
        vec3 bar(sx * 0.32f * s, A.chestP.y + 0.52f * s, 1.02f * s);
        armIK(r.arm[sd], bar + vec3(0, 0, 0.01f * sinf(kTwoPi * t / 2.f + sd)), vec3(sx * 1.f, -0.4f, -0.6f), 0.9f);
        r.arm[sd].twist = 1.0f;
    }
    r.spineYaw = 0.04f * sinf(kTwoPi * t / 2.f);
}

static void clipCar(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    bool enter = c == CLIP_ENTER_CAR_L || c == CLIP_ENTER_CAR_R;
    bool right = c == CLIP_ENTER_CAR_R || c == CLIP_EXIT_CAR_R;
    const float dx = 0.55f * s;
    Rig stand, reach, open, turn, duck, seat;
    standPose(A, stand);
    // left-side driver entry authored; right side is mirrored
    reach = stand;
    armIK(reach.arm[1], vec3(0.35f * s, 0.2f * s, 1.0f * s), vec3(1, -0.5f, -1), 0.9f);
    reach.spineYaw = -0.2f;
    reach.headYaw = -0.3f;
    open = reach;
    open.arm[1].target = vec3(0.2f * s, -0.05f * s, 1.02f * s);
    open.pelvis = vec3(-0.04f * s, -0.05f * s, 0.f);
    setFootFlat(A, open.leg[0], vec3(A.ankle[0].x - 0.05f, -0.12f * s, 0.f), 0.3f);
    turn = stand;
    turn.pelvis = vec3(0.2f * s, 0.05f * s, -0.18f * s);
    turn.pelvisYaw = -0.9f;
    turn.spineYaw = 0.3f;
    turn.spinePitch = 0.35f;
    turn.headPitch = 0.3f;
    setFootFlat(A, turn.leg[1], vec3(0.38f * s, 0.1f * s, 0.f), -0.9f);
    setFootFlat(A, turn.leg[0], vec3(A.ankle[0].x, 0.f, 0.f), -0.2f);
    armIK(turn.arm[1], vec3(0.55f * s, 0.25f * s, 0.95f * s), vec3(1, -0.5f, -1), 0.9f);
    armIK(turn.arm[0], vec3(0.1f * s, 0.25f * s, 1.25f * s), vec3(-1, -0.5f, -1), 0.9f);
    drivePose(A, seat, 0.f);
    seat.pelvis.x += dx;
    for (int sd = 0; sd < 2; sd++) {
        seat.leg[sd].ankle.x += dx;
        seat.arm[sd].target.x += dx;
    }
    duck = seat;
    duck.pelvisYaw = -0.6f;
    duck.spinePitch = 0.6f;
    duck.headPitch = 0.4f;
    duck.pelvis.x -= 0.1f * s;
    duck.pelvis.z += 0.05f * s;
    setFootFlat(A, duck.leg[0], vec3(A.ankle[0].x + 0.1f * s, 0.05f * s, 0.f), -0.4f);
    duck.leg[1].ankle = vec3(dx + 0.05f * s, 0.35f * s, A.footH + 0.12f * s);
    duck.arm[0].target = vec3(0.3f * s, 0.2f * s, 1.25f * s);
    std::vector<Key> k;
    if (enter) k = {{0.f, stand}, {0.3f, reach}, {0.55f, open}, {0.85f, turn}, {1.25f, duck}, {1.8f, seat}};
    else {
        // exit: start seated at the origin, end standing 0.55 m to the left
        Rig seat0, duck0, turn0, stand0;
        drivePose(A, seat0, 0.f);
        auto shiftX = [&](Rig x, float d) {
            x.pelvis.x += d;
            for (int sd = 0; sd < 2; sd++) {
                x.leg[sd].ankle.x += d;
                x.arm[sd].target.x += d;
            }
            return x;
        };
        duck0 = shiftX(duck, -dx);
        turn0 = shiftX(turn, -dx);
        stand0 = shiftX(stand, -dx);
        Rig step = shiftX(open, -dx);
        k = {{0.f, seat0}, {0.4f, duck0}, {0.8f, turn0}, {1.15f, step}, {1.6f, stand0}};
    }
    float dur = enter ? 1.8f : 1.6f;
    r = sampleKeys(k, t, false, dur);
    if (right) {
        // passenger side: mirrored; end/start in the passenger pose
        Rig m = mirrorRig(r);
        if (enter && t > 1.25f) {
            Rig p;
            passengerPose(A, p, 0.f, 4.f);
            p.pelvis.x -= dx;
            for (int sd = 0; sd < 2; sd++) {
                p.leg[sd].ankle.x -= dx;
                p.arm[sd].target.x -= dx;
            }
            m = lerpRig(m, p, easeInOut((t - 1.25f) / 0.55f));
        }
        if (!enter && t < 0.4f) {
            Rig p;
            passengerPose(A, p, 0.f, 4.f);
            m = lerpRig(p, m, easeInOut(t / 0.4f));
        }
        r = m;
    }
}

static void clipSwim(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    r = Rig();
    const vec3 pb = A.sk.bindLocalPos[B_PELVIS];
    if (c == CLIP_SWIM) {
        float ph = t / 1.4f;
        float c1 = cosf(kTwoPi * ph), s1 = sinf(kTwoPi * ph);
        // horizontal, face down; body rolls with the stroke
        r.pelvisPitch = kHalfPi * 0.92f;
        r.pelvis = vec3(0, -0.45f * s, -pb.z - 0.12f * s);
        r.pelvisRoll = 0.0f;
        r.spineYaw = 0.f;
        r.root = quat();
        r.spineRoll = 0.f;
        // roll about the body axis = yaw of the pelvis frame after the pitch; approximate with spine twist
        r.spineYaw = 0.45f * s1;
        r.pelvisYaw = 0.2f * s1;
        r.headPitch = -0.55f;
        r.headYaw = -0.6f * Max(0.f, s1 - 0.6f) / 0.4f;   // breathe to one side
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            float p = ph + (sd ? 0.5f : 0.f);
            p -= floorf(p);
            // freestyle: pull (0..0.5) under the body, recovery (0.5..1) over the water
            ArmCtl& a = r.arm[sd];
            a.ik = false;
            float ang = kTwoPi * p;
            // arm direction in the chest frame (chest faces down: +Y of chest frame points to the head)
            vec3 d = normalize(vec3(sx * (0.25f + 0.25f * Max(0.f, sinf(ang))), cosf(ang) * 0.9f + 0.1f, -sinf(ang)));
            a.dir = d;
            a.pole = normalize(vec3(sx * 0.6f, 0.f, 1.f));
            a.elbow = p > 0.5f ? 0.7f * sinf(kTwoPi * (p - 0.5f)) : 0.5f * sinf(kTwoPi * p);
            a.fingers = 0.15f;
            a.twist = 0.6f;
            // flutter kick
            LegCtl& l = r.leg[sd];
            l.ik = false;
            float k = sinf(kTwoPi * ph * 3.f + (sd ? kPi : 0.f));
            l.hipFlex = 0.1f * k;
            l.kneeFlex = 0.2f + 0.15f * Max(0.f, k);
            l.ankleFlex = -1.1f;
            l.hipAbd = 0.04f;
        }
    } else {
        // treading water: upright, head above the surface
        float ph = t / 2.4f;
        float c1 = cosf(kTwoPi * ph);
        r.pelvis = vec3(0, 0, -A.headP.z + 0.08f * s + 0.02f * s * c1);
        r.spinePitch = 0.1f;
        r.headPitch = -0.1f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            float p = ph * 2.f + (sd ? 0.5f : 0.f);
            armFK(r.arm[sd], sd, 0.6f + 0.1f * sinf(kTwoPi * p), 0.9f + 0.3f * cosf(kTwoPi * p), 0.6f, 0.9f * sx * 0.f + 1.2f, 0.2f);
            LegCtl& l = r.leg[sd];
            l.ik = false;
            l.hipFlex = 0.5f + 0.3f * sinf(kTwoPi * p);
            l.kneeFlex = 1.0f + 0.4f * cosf(kTwoPi * p);
            l.hipAbd = 0.3f;
            l.ankleFlex = -0.4f;
        }
    }
}

static void clipClimbVault(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    if (c == CLIP_CLIMB) {
        // ladder / wall climb cycle, wall in front at y ~ 0.28
        standPose(A, r);
        float ph = t / 1.2f;
        r.pelvis = vec3(0, 0.08f * s, 0.f);
        r.spinePitch = 0.1f;
        r.headPitch = -0.35f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            float p = ph + (sd ? 0.5f : 0.f);
            p -= floorf(p);
            float up = p < 0.5f ? easeInOut(p * 2.f) : 1.f - easeInOut((p - 0.5f) * 2.f);
            vec3 hand(sx * 0.2f * s, 0.3f * s, (1.55f + 0.3f * up) * s);
            armIK(r.arm[sd], hand, vec3(sx, -0.3f, -1), 0.9f);
            float fu = 1.f - up;
            LegCtl& l = r.leg[sd];
            l.ankle = vec3(sx * 0.13f * s, 0.18f * s, A.footH + (0.05f + 0.35f * fu) * s);
            l.pitch = 0.f;
            l.knee = vec3(sx * 0.2f, 1.f, 0.f);
        }
    } else {
        // vault over a low obstacle ~0.5 m ahead: hands plant, body lifts and swings over, lands 1.1 m ahead
        Rig a, plant, over, land, e;
        standPose(A, a);
        plant = a;
        plant.pelvis = vec3(0, 0.25f * s, -0.12f * s);
        plant.spinePitch = 0.6f;
        for (int sd = 0; sd < 2; sd++) armIK(plant.arm[sd], vec3((sd ? 1.f : -1.f) * 0.15f * s, 0.5f * s, 0.95f * s), vec3((sd ? 1.f : -1.f), -1, 0), 0.9f);
        over = plant;
        over.pelvis = vec3(0.1f * s, 0.55f * s, 0.18f * s);
        over.pelvisRoll = -0.4f;
        over.spinePitch = 0.4f;
        for (int sd = 0; sd < 2; sd++) {
            LegCtl& l = over.leg[sd];
            l.ik = false;
            l.hipFlex = 1.2f;
            l.kneeFlex = 1.4f;
            l.hipAbd = 0.5f * (sd ? 1.f : 0.3f);
        }
        land = a;
        land.pelvis = vec3(0, 1.1f * s, -0.2f * s);
        for (int sd = 0; sd < 2; sd++) setFootFlat(A, land.leg[sd], vec3(A.ankle[sd].x, 1.1f * s + (sd ? 0.05f : -0.05f) * s, 0.f), 0.f);
        land.spinePitch = 0.25f;
        e = land;
        e.pelvis = vec3(0, 1.1f * s, 0.f);
        e.spinePitch = 0.f;
        std::vector<Key> k = {{0.f, a}, {0.22f, plant}, {0.45f, over}, {0.7f, land}, {0.9f, e}};
        r = sampleKeys(k, t, false, 0.9f);
    }
}

static void clipWeapon(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    switch (c) {
        case CLIP_AIM_PISTOL: {
            aimPistolPose(A, r);
            float br = sinf(kTwoPi * t / 2.f);
            r.spinePitch += 0.01f * br;
            r.arm[0].target.z += 0.004f * br;
            r.arm[1].target.z += 0.004f * br;
            break;
        }
        case CLIP_AIM_RIFLE: {
            aimRiflePose(A, r);
            float br = sinf(kTwoPi * t / 2.f);
            r.spinePitch += 0.01f * br;
            break;
        }
        case CLIP_FIRE_PISTOL: {
            Rig a, k;
            aimPistolPose(A, a);
            k = a;
            for (int sd = 0; sd < 2; sd++) k.arm[sd].target += vec3(0, -0.04f, 0.035f) * s;
            k.arm[1].handRot = qaa(vec3(1, 0, 0), 0.3f) * k.arm[1].handRot;
            k.headPitch -= 0.03f;
            k.spinePitch -= 0.03f;
            std::vector<Key> ks = {{0.f, a}, {0.04f, k}, {0.3f, a}};
            r = sampleKeys(ks, t, false, 0.3f);
            break;
        }
        case CLIP_FIRE_RIFLE: {
            Rig a, k;
            aimRiflePose(A, a);
            k = a;
            for (int sd = 0; sd < 2; sd++) k.arm[sd].target += vec3(0, -0.03f, 0.012f) * s;
            k.spineYaw += 0.03f;
            k.spinePitch -= 0.02f;
            std::vector<Key> ks = {{0.f, a}, {0.03f, k}, {0.12f, a}};
            r = sampleKeys(ks, t, false, 0.12f);
            break;
        }
        case CLIP_RELOAD: {
            // weapon lowered in front, left hand to the belt pouch, insert magazine, slap
            Rig base, pouch, insert, slap, back;
            aimRiflePose(A, base);
            Rig low = base;
            vec3 w(0.03f * s, A.chestP.y + 0.32f * s, A.shoulderZ - 0.28f * s);
            low.arm[1].target = w;
            low.arm[1].orient = false;
            low.arm[0].target = w + vec3(-0.08f, 0.14f, 0.02f) * s;
            low.headPitch = 0.45f;
            low.spinePitch = 0.18f;
            pouch = low;
            pouch.arm[0].target = vec3(-0.12f * s, 0.12f * s, A.hip[0].z + 0.08f * s);
            pouch.arm[0].ikPole = vec3(-1, -0.5f, 0);
            pouch.headPitch = 0.3f;
            insert = low;
            insert.arm[0].target = w + vec3(-0.02f, 0.06f, -0.08f) * s;
            slap = low;
            slap.arm[0].target = w + vec3(-0.02f, 0.06f, -0.05f) * s;
            back = base;
            std::vector<Key> ks = {{0.f, base}, {0.25f, low}, {0.6f, pouch}, {1.0f, insert}, {1.2f, insert}, {1.35f, slap}, {1.55f, low}, {1.9f, back}};
            r = sampleKeys(ks, t, false, 1.9f);
            break;
        }
        case CLIP_THROW: {
            Rig a, wind, rel, follow;
            standPose(A, a);
            wind = a;
            setFootFlat(A, wind.leg[0], vec3(A.ankle[0].x, 0.22f * s, 0.f), 0.2f);
            wind.pelvis = vec3(0, -0.06f * s, -0.04f * s);
            wind.pelvisYaw = -0.6f;
            wind.spineYaw = -0.5f;
            wind.spinePitch = -0.15f;
            wind.headYaw = 0.9f;
            armFK(wind.arm[1], 1, -0.6f, 1.2f, 1.6f, 0.4f, 0.9f);
            wind.arm[1].pole = vec3(0.3f, -0.3f, -1.f);
            armFK(wind.arm[0], 0, 1.3f, 0.3f, 0.3f, 0.2f, 0.4f);
            rel = wind;
            rel.pelvis = vec3(0, 0.1f * s, -0.05f * s);
            rel.pelvisYaw = 0.3f;
            rel.spineYaw = 0.4f;
            rel.spinePitch = 0.25f;
            rel.headYaw = 0.f;
            armFK(rel.arm[1], 1, 1.9f, 0.25f, 0.3f, 0.6f, 0.2f);
            armFK(rel.arm[0], 0, -0.3f, 0.3f, 0.6f, 0.2f, 0.5f);
            rel.leg[1].pitch = -0.6f;
            rel.leg[1].ankle = ankleFromPivot(A, A.ankle[1].x, -0.02f, -0.6f, 1);
            rel.leg[1].toe = 0.6f;
            follow = rel;
            follow.spinePitch = 0.45f;
            follow.spineYaw = 0.55f;
            armFK(follow.arm[1], 1, 0.4f, -0.2f, 0.4f, 0.6f, 0.2f);
            std::vector<Key> ks = {{0.f, a}, {0.4f, wind}, {0.62f, rel}, {0.85f, follow}, {1.2f, a}};
            r = sampleKeys(ks, t, false, 1.2f);
            break;
        }
        default: standPose(A, r); break;
    }
}

static void clipSocial(const AuthorCtx& A, Clip c, float t, float dur, Rig& r) {
    const float s = A.D.s;
    const float u = t / dur;
    switch (c) {
        case CLIP_COWER: {
            crouchPose(A, r, 1.15f);
            r.spinePitch = 0.7f;
            r.headPitch = 0.4f;
            r.neckPitch = 0.2f;
            float tr = 0.012f * sinf(kTwoPi * t * 7.f);
            vec3 hb = A.headP + r.pelvis + vec3(0, 0.12f * s, 0.05f * s);
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                armIK(r.arm[sd], hb + vec3(sx * 0.06f * s, -0.02f * s, 0.07f * s + tr), vec3(sx * 0.2f, 1.f, 0.f), 0.6f);
            }
            r.pelvis.x += tr * 0.5f;
            break;
        }
        case CLIP_HANDS_UP: {
            standPose(A, r);
            float sw = sinf(kTwoPi * u);
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                vec3 h(sx * 0.3f * s, A.chestP.y + 0.08f * s, A.headP.z + 0.12f * s + 0.01f * sw);
                armIK(r.arm[sd], h, vec3(sx, 0.f, -0.5f), 0.05f);
                r.arm[sd].twist = -0.3f;
                r.arm[sd].orient = true;
                r.arm[sd].handRot = quatFromTwoPairs(A.D.armDir[sd], A.D.palmN[sd], vec3(0, 0.1f, 1), vec3(0, 1, 0));
            }
            r.headPitch = -0.05f;
            r.pelvis.x = 0.01f * sw * s;
            break;
        }
        case CLIP_TALK: case CLIP_TALK_PHONE: {
            clipIdle(A, t, dur, r, false);
            // speech jaw motion
            float j = 0.5f + 0.5f * sinf(t * 17.f) * sinf(t * 5.3f + 1.f);
            r.jaw = 0.08f * j * (0.6f + 0.4f * sinf(t * 2.1f));
            r.headPitch += 0.04f * sinf(t * 2.3f);
            r.headRoll += 0.04f * sinf(t * 1.1f);
            if (c == CLIP_TALK) {
                for (int sd = 0; sd < 2; sd++) {
                    float sx = sd ? 1.f : -1.f;
                    float g1 = 0.5f + 0.5f * sinf(t * 2.2f + sd * 1.7f), g2 = sinf(t * 3.1f + sd);
                    vec3 h(sx * (0.14f + 0.05f * g2) * s, A.chestP.y + (0.22f + 0.08f * g1) * s, A.chestP.z - (0.12f - 0.06f * g1) * s);
                    armIK(r.arm[sd], h, vec3(sx, -0.6f, -0.6f), 0.3f);
                    r.arm[sd].twist = 1.2f;
                }
            } else {
                vec3 ear = A.headP + vec3(0.07f * s, 0.02f * s, -0.03f * s);
                armIK(r.arm[1], ear, vec3(0.5f, -0.2f, -1.f), 0.8f);
                r.arm[1].orient = true;
                r.arm[1].handRot = quatFromTwoPairs(A.D.armDir[1], A.D.palmN[1], normalize(vec3(-0.3f, 0.2f, 1.f)), vec3(-1, 0, 0));
                r.headRoll += 0.12f;
                vec3 hip = A.hip[0] + r.pelvis + vec3(-0.14f * s, 0.04f * s, 0.1f * s);
                armIK(r.arm[0], hip, vec3(-1, -0.4f, 0.f), 0.5f);
            }
            break;
        }
        case CLIP_SIT_BENCH: {
            seatedPose(A, r, 0.5f * s, 0.42f * s, 0.2f);
            float w = sinf(kTwoPi * u);
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                vec3 th = A.hip[sd] + r.pelvis + vec3(sx * 0.03f, 0.25f * s, 0.08f * s);
                armIK(r.arm[sd], th, vec3(sx, -0.5f, -0.3f), 0.4f);
                r.arm[sd].twist = 0.8f;
            }
            r.headYaw = 0.5f * sinf(kTwoPi * u) * sstep(0.2f, 0.8f, fabsf(w));
            r.spinePitch += 0.015f * sinf(kTwoPi * u * 2.f);
            break;
        }
        case CLIP_SMOKE: {
            clipIdle(A, t, dur, r, false);
            float lift = sstep(0.08f, 0.2f, u) * (1.f - sstep(0.38f, 0.5f, u));
            vec3 mouth = A.headP + vec3(0.02f * s, 0.1f * s, -0.07f * s);
            vec3 rest(0.2f * s, 0.12f * s, A.hip[1].z + 0.03f * s);
            vec3 h = lerp(rest, mouth, lift);
            armIK(r.arm[1], h, vec3(1, -0.3f, -0.8f), 0.55f);
            r.arm[1].twist = 1.0f;
            r.headPitch += -0.25f * sstep(0.45f, 0.52f, u) * (1.f - sstep(0.6f, 0.7f, u));   // exhale upwards
            vec3 pocket = A.hip[0] + r.pelvis + vec3(-0.1f * s, 0.06f * s, -0.02f * s);
            armIK(r.arm[0], pocket, vec3(-1, -0.4f, 0.f), 0.6f);
            break;
        }
        case CLIP_DANCE: {
            standPose(A, r);
            float beat = t * 2.f;   // 120 bpm
            float b = sinf(kTwoPi * beat);
            float half = sinf(kTwoPi * beat * 0.5f);
            r.pelvis = vec3(0.05f * half * s, 0.f, -0.05f * s - 0.035f * s * (0.5f + 0.5f * b));
            r.pelvisRoll = 0.12f * half;
            r.pelvisYaw = 0.15f * sinf(kTwoPi * beat * 0.25f);
            r.spineRoll = -0.1f * half;
            r.spineYaw = -0.2f * sinf(kTwoPi * beat * 0.25f);
            r.headRoll = 0.08f * half;
            r.headPitch = 0.08f * b;
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                setFootFlat(A, r.leg[sd], vec3(A.ankle[sd].x + sx * 0.05f * s, 0.f, 0.f), -sx * 0.2f);
                r.leg[sd].knee = normalize(vec3(sx * 0.3f, 1.f, 0.f));
                float a = sinf(kTwoPi * beat * 0.5f + sd * kPi);
                armFK(r.arm[sd], sd, 0.6f + 0.5f * a, 0.4f + 0.2f * a, 1.3f, 0.6f, 0.7f);
            }
            break;
        }
        case CLIP_WAVE: {
            clipIdle(A, t, 4.f, r, false);
            float w = sinf(kTwoPi * u * 2.f);
            vec3 sh = A.gh[1];
            vec3 h(sh.x + 0.18f * s + 0.08f * w * s, A.chestP.y + 0.12f * s, A.headP.z + 0.1f * s);
            armIK(r.arm[1], h, vec3(1, -0.3f, -0.6f), 0.1f);
            r.arm[1].orient = true;
            r.arm[1].handRot = qaa(vec3(0, 1, 0), 0.4f * w) * quatFromTwoPairs(A.D.armDir[1], A.D.palmN[1], vec3(0, 0.1f, 1), vec3(0, 1, 0));
            r.headYaw = 0.1f;
            r.headRoll = 0.08f;
            break;
        }
        case CLIP_POINT: {
            clipIdle(A, t, 4.f, r, false);
            vec3 sh = A.gh[1];
            vec3 h(sh.x - 0.05f * s, A.chestP.y + 0.62f * s, A.shoulderZ + 0.03f * s);
            armIK(r.arm[1], h, vec3(1, -0.4f, -0.8f), 0.55f);
            r.arm[1].orient = true;
            r.arm[1].handRot = quatFromTwoPairs(A.D.armDir[1], A.D.palmN[1], vec3(-0.05f, 1.f, 0.05f), vec3(-1, 0, -0.2f));
            r.spineYaw = 0.1f;
            r.headYaw = 0.05f;
            break;
        }
        case CLIP_CHEER: {
            standPose(A, r);
            float b = sinf(kTwoPi * u * 2.f);
            float hop = Max(0.f, b);
            r.pelvis = vec3(0, 0, -0.03f * s + 0.05f * hop * s);
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                vec3 h(sx * 0.35f * s, A.chestP.y + 0.08f * s, A.headP.z + (0.3f + 0.06f * b) * s);
                armIK(r.arm[sd], h, vec3(sx, -0.2f, -0.3f), 0.95f);
                if (hop > 0.f) {
                    r.leg[sd].pitch = -0.4f * hop;
                    r.leg[sd].ankle = ankleFromPivot(A, A.ankle[sd].x, A.ankle[sd].y, -0.4f * hop, 1) + vec3(0, 0, 0.05f * hop * s);
                    r.leg[sd].toe = 0.4f * hop;
                }
            }
            r.headPitch = -0.2f;
            break;
        }
        case CLIP_LEAN_WALL: {
            standPose(A, r);
            float br = sinf(kTwoPi * u * 2.f);
            r.pelvis = vec3(0, -0.1f * s, -0.02f * s);
            r.pelvisPitch = -0.08f;
            r.spinePitch = -0.06f + 0.01f * br;
            r.headPitch = 0.05f;
            setFootFlat(A, r.leg[0], vec3(A.ankle[0].x - 0.02f, 0.18f * s, 0.f), 0.15f);
            // right foot flat against the wall behind
            r.leg[1].ankle = vec3(A.ankle[1].x, -0.2f * s, 0.42f * s);
            r.leg[1].pitch = 0.3f;
            r.leg[1].knee = vec3(0.2f, 1.f, 0.f);
            // arms crossed over the chest
            vec3 c = A.chestP + r.pelvis;
            armIK(r.arm[0], c + vec3(0.1f * s, 0.2f * s, -0.07f * s), vec3(-1, -0.2f, -1), 0.6f);
            armIK(r.arm[1], c + vec3(-0.1f * s, 0.21f * s, -0.04f * s), vec3(1, -0.2f, -1), 0.6f);
            r.arm[0].twist = r.arm[1].twist = 1.0f;
            r.headYaw = 0.4f * sinf(kTwoPi * u) * sstep(0.3f, 0.9f, fabsf(sinf(kTwoPi * u)));
            break;
        }
        case CLIP_SUNBATHE: {
            lyingPose(A, r, true, 0.5f);
            float br = sinf(kTwoPi * u * 2.f);
            r.spinePitch += 0.01f * br;
            r.headYaw = 0.1f;
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                // hands behind the head
                r.arm[sd].dir = normalize(vec3(sx * 0.6f, 0.f, 0.8f));
                r.arm[sd].pole = normalize(vec3(sx * 0.3f, -1.f, 0.f));
                r.arm[sd].elbow = 2.2f;
                r.arm[sd].fingers = 0.3f;
            }
            r.leg[1].hipFlex = 0.9f;
            r.leg[1].kneeFlex = 1.6f;
            r.leg[1].ankleFlex = 0.3f;
            break;
        }
        case CLIP_JOG_IDLE: {
            standPose(A, r);
            float b = sinf(kTwoPi * u);
            r.pelvis = vec3(0, 0, -0.02f * s - 0.02f * s * fabsf(b));
            r.spinePitch = 0.06f;
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                float heel = Max(0.f, sx * b);
                r.leg[sd].pitch = -0.6f * heel;
                r.leg[sd].ankle = ankleFromPivot(A, A.ankle[sd].x, A.ankle[sd].y, -0.6f * heel, 1) + vec3(0, 0, 0.03f * heel * s);
                r.leg[sd].toe = 0.6f * heel;
                armFK(r.arm[sd], sd, 0.3f + 0.2f * sx * b, 0.15f, 1.4f, 0.4f, 0.7f);
            }
            break;
        }
        default: standPose(A, r); break;
    }
}

static void authorClip(const AuthorCtx& A, Clip c, float t, Rig& r, u32 var) {
    const ClipInfo& ci = kClipInfo[c];
    switch (c) {
        case CLIP_IDLE: clipIdle(A, t, ci.duration, r, false); break;
        case CLIP_IDLE_LOOK: clipIdle(A, t, ci.duration, r, true); break;
        case CLIP_WALK: case CLIP_JOG: case CLIP_RUN: case CLIP_SPRINT: case CLIP_WALK_BACK: case CLIP_STRAFE_L: case CLIP_STRAFE_R:
        case CLIP_CROUCH_WALK: case CLIP_FLEE: clipLocomotion(A, c, t, r); break;
        case CLIP_CROUCH_IDLE: clipCrouchIdle(A, t, ci.duration, r); break;
        case CLIP_JUMP_START: case CLIP_FALL: case CLIP_LAND: clipJump(A, c, t, r); break;
        case CLIP_AIM_PISTOL: case CLIP_AIM_RIFLE: case CLIP_FIRE_PISTOL: case CLIP_FIRE_RIFLE: case CLIP_RELOAD: case CLIP_THROW:
            clipWeapon(A, c, t, r); break;
        case CLIP_PUNCH_L: case CLIP_PUNCH_R: case CLIP_KICK: case CLIP_BLOCK: clipCombat(A, c, t, r); break;
        case CLIP_HIT_FRONT: case CLIP_HIT_BACK: case CLIP_STAGGER: clipHit(A, c, t, r); break;
        case CLIP_DEATH_FRONT: case CLIP_DEATH_BACK: clipDeath(A, c, t, r, var); break;
        case CLIP_SIT_DRIVE: drivePose(A, r, t); break;
        case CLIP_SIT_PASSENGER: passengerPose(A, r, t, ci.duration); break;
        case CLIP_RIDE_BIKE: bikePose(A, r, t); break;
        case CLIP_ENTER_CAR_L: case CLIP_EXIT_CAR_L: case CLIP_ENTER_CAR_R: case CLIP_EXIT_CAR_R: clipCar(A, c, t, r); break;
        case CLIP_SWIM_IDLE: case CLIP_SWIM: clipSwim(A, c, t, r); break;
        case CLIP_CLIMB: case CLIP_VAULT: clipClimbVault(A, c, t, r); break;
        case CLIP_GET_UP_FRONT: case CLIP_GET_UP_BACK: clipGetUp(A, c, t, r); break;
        default: clipSocial(A, c, t, ci.duration, r); break;
    }
}

// ------------------------------------------------------------------------------------------------
// Baking

struct BakedClip {
    int frames = 0;
    float fps = 30.f;
    std::vector<quat> rot;   // frames * B_COUNT
    std::vector<vec3> root;
};

struct ClipLib {
    AuthorCtx ctx[2];                  // male / female reference
    BakedClip clips[2][CLIP_COUNT];    // [style][clip]
    float refLegLen = 1.f;
    float refArmLen = 1.f;
};

static void makeAuthorCtx(AuthorCtx& A, bool female) {
    CharacterDesc d;
    d.seed = 12345;
    d.gender = female ? FEMALE : MALE;
    d.height = female ? 1.65f : 1.78f;
    d.weight = 0.45f;
    d.muscle = female ? 0.3f : 0.45f;
    d.age = 0.3f;
    d.shoes = SHOE_SNEAKER;
    computeDims(d, A.D);
    buildSkeleton(d, A.sk);
    const vec3* J = A.D.J;
    A.footH = J[B_FOOT_L].z;
    A.legLen = A.D.thigh + A.D.shin;
    for (int s = 0; s < 2; s++) {
        int o = s ? 4 : 0;
        A.hip[s] = J[B_THIGH_L + o];
        A.ankle[s] = J[B_FOOT_L + o];
        A.gh[s] = J[B_UPPERARM_L + o];
    }
    A.heelBack = A.D.heelBack;
    A.ballFwd = A.D.ballFwd;
    A.toeFwd = A.D.toeFwd;
    A.fem = female ? 1.f : 0.f;
    A.chestP = J[B_CHEST];
    A.headP = J[B_HEAD];
    A.shoulderZ = J[B_UPPERARM_R].z;
}

static bool styleDependent(Clip c) {
    switch (c) {
        case CLIP_IDLE: case CLIP_IDLE_LOOK: case CLIP_WALK: case CLIP_JOG: case CLIP_RUN: case CLIP_SPRINT: case CLIP_WALK_BACK:
        case CLIP_TALK: case CLIP_TALK_PHONE: case CLIP_SMOKE: case CLIP_DANCE: case CLIP_FLEE: return true;
        default: return false;
    }
}

static void bakeClip(const AuthorCtx& A, Clip c, BakedClip& out) {
    const ClipInfo& ci = kClipInfo[c];
    int n = ci.loop ? Max(2, (int)lrintf(ci.duration * out.fps)) : Max(2, (int)ceilf(ci.duration * out.fps) + 1);
    out.frames = n;
    out.rot.resize((size_t)n * B_COUNT);
    out.root.resize(n);
    Pose p;
    for (int f = 0; f < n; f++) {
        float t = ci.loop ? ci.duration * f / n : Min(ci.duration, f / out.fps);
        Rig r;
        authorClip(A, c, t, r, 0);
        rigToPose(A, r, p);
        for (int b = 0; b < B_COUNT; b++) {
            quat q = normalize(p.rot[b]);
            // keep hemisphere continuity for clean interpolation
            if (f > 0 && dot(q, out.rot[(size_t)(f - 1) * B_COUNT + b]) < 0.f) q = quat(-q.x, -q.y, -q.z, -q.w);
            out.rot[(size_t)f * B_COUNT + b] = q;
        }
        out.root[f] = p.rootOffset;
    }
}

static ClipLib* buildLib() {
    ClipLib* L = new ClipLib();
    makeAuthorCtx(L->ctx[0], false);
    makeAuthorCtx(L->ctx[1], true);
    L->refLegLen = L->ctx[0].legLen;
    for (int c = 0; c < CLIP_COUNT; c++) {
        bakeClip(L->ctx[0], (Clip)c, L->clips[0][c]);
        if (styleDependent((Clip)c)) bakeClip(L->ctx[1], (Clip)c, L->clips[1][c]);
    }
    return L;
}

const ClipLib& clipLib() {
    static ClipLib* lib = buildLib();
    return *lib;
}

// Femininity of a skeleton's proportions (hip joint spacing relative to shoulder spacing).
float skeletonStyle(const Skeleton& sk) {
    float hip = fabsf(sk.bindLocalPos[B_THIGH_L].x);
    vec3 ua = sk.bindLocalPos[B_CLAVICLE_L] + sk.bindLocalPos[B_UPPERARM_L];
    float sh = fabsf(ua.x);
    float ratio = hip / Max(sh, 1e-3f);
    return Saturate((ratio - 0.52f) / 0.05f);
}

float skeletonLegScale(const Skeleton& sk) {
    const ClipLib& L = clipLib();
    float leg = sk.boneLength[B_THIGH_L] + sk.boneLength[B_CALF_L];
    return leg / Max(L.refLegLen, 1e-3f);
}

static void sampleBaked(const BakedClip& bc, const ClipInfo& ci, float t, Pose& out) {
    float f;
    int i0, i1;
    if (ci.loop) {
        float tt = t - floorf(t / ci.duration) * ci.duration;
        f = tt * bc.fps;
        i0 = (int)f;
        f -= i0;
        i0 = i0 % bc.frames;
        i1 = (i0 + 1) % bc.frames;
    } else {
        float tt = Clamp(t, 0.f, ci.duration);
        f = tt * bc.fps;
        i0 = Min((int)f, bc.frames - 1);
        f -= i0;
        i1 = Min(i0 + 1, bc.frames - 1);
    }
    const quat* a = &bc.rot[(size_t)i0 * B_COUNT];
    const quat* b = &bc.rot[(size_t)i1 * B_COUNT];
    for (int k = 0; k < B_COUNT; k++) out.rot[k] = nlerp(a[k], b[k], f);
    out.rootOffset = lerp(bc.root[i0], bc.root[i1], f);
}

}  // namespace detail

const ClipInfo& clipInfo(Clip c) {
    int i = Clamp((int)c, 0, CLIP_COUNT - 1);
    return detail::kClipInfo[i];
}

void sampleClip(const Skeleton& skel, Clip c, float t, Pose& out, u32 variationSeed) {
    using namespace detail;
    const ClipLib& L = clipLib();
    int ci = Clamp((int)c, 0, CLIP_COUNT - 1);
    const ClipInfo& info = kClipInfo[ci];
    float fem = styleDependent((Clip)ci) ? skeletonStyle(skel) : 0.f;
    if (fem > 0.5f) sampleBaked(L.clips[1][ci], info, t, out);
    else sampleBaked(L.clips[0][ci], info, t, out);
    // scale root motion to this skeleton's leg length
    out.rootOffset *= skeletonLegScale(skel);
    // per-character posture variation
    if (variationSeed) {
        u32 h = hash32(variationSeed * 0x9E3779B1u + 77u);
        float a = hashToFloat(h) - 0.5f, b = hashToFloat(hash32(h)) - 0.5f, cc = hashToFloat(hash32(h + 1u)) - 0.5f;
        out.rot[B_HEAD] = normalize(out.rot[B_HEAD] * qy(0.06f * a) * qx(-0.05f * b));
        out.rot[B_SPINE2] = normalize(out.rot[B_SPINE2] * qx(-0.05f * cc));
        out.rot[B_CLAVICLE_L] = normalize(qy(0.03f * b) * out.rot[B_CLAVICLE_L]);
        out.rot[B_CLAVICLE_R] = normalize(qy(-0.03f * b) * out.rot[B_CLAVICLE_R]);
    }
}

vec3 clipHipPosition(const Skeleton& skel, Clip c, float t) {
    Pose p;
    sampleClip(skel, c, t, p, 0);
    quat q;
    vec3 tp;
    detail::boneModel(skel, p, B_PELVIS, q, tp);
    vec3 l, r;
    quat ql, qr;
    detail::boneModel(skel, p, B_THIGH_L, ql, l);
    detail::boneModel(skel, p, B_THIGH_R, qr, r);
    return (l + r) * 0.5f;
}

}  // namespace Anim
