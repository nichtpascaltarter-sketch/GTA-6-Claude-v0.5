// Procedural animation clips.
//
// Every clip is authored as a function of time producing a "rig pose" of intuitive controls: pelvis offset/rotation,
// spine/neck/head bend-twist-lean (distributed over the vertebrae), clavicle shrug, arms as FK (direction + elbow pole +
// flexion + forearm twist) or IK (hand target + elbow pole + optional hand orientation), legs as IK (ankle target, knee
// direction, foot orientation, toe bend) or FK (hip/knee/ankle angles), fingers, jaw and eyes. Keyframed clips use
// time-aware Catmull-Rom (Hermite) interpolation of rig poses; when neighbouring keys drive a limb differently
// (FK vs IK) the limb is converted to the IK representation of its evaluated pose first, so the interpolation is
// continuous. Rig poses are converted to local bone rotations on a reference skeleton (two-bone IK, forearm twist
// distribution) and baked at 30 Hz into a global library built once (thread-safe function-local static).
//
// Locomotion comes from a gait generator: per-foot stance/swing phases with heel strike, foot-flat and toe-off roll
// (the planted foot moves exactly at ground speed), swing arcs with toe clearance, pelvis bob/sway/rotation/list, spine
// counter-rotation, arm swing opposite to the legs and head stabilization. Male and female variants are baked for
// locomotion and idles; sampleClip picks the style from the skeleton proportions.
//
// Conventions (model space, character faces +Y, origin on the ground, see also character.cpp):
//  - Locomotion clips are in place; ClipInfo::speed is the matching ground speed. The left heel strikes at phase 0
//    in every gait clip so they can be blended at the same normalized phase.
//  - SIT_DRIVE / SIT_PASSENGER / RIDE_BIKE / SIT_BENCH: hip joints (their midpoint) exactly 0.5 m above the origin,
//    whatever the character size (the game places the ped origin 0.5 m below VehicleModel::seats[].pos). Driving:
//    hands on a steering wheel rim (r 0.185 m) centred 0.5 m ahead of the hips and 0.4 m above them (the car
//    interior layout); the Animator re-targets both hands onto that rim with IK for every skeleton and turns them
//    with the steering input (AnimInput::localMoveDir.x while in stance 1).
//  - SWIM / SWIM_IDLE: the water surface is 1.32 m above the origin (the game keeps swimmers there).
//  - VAULT and CLIMB are in place: the game moves the ped origin along the traversal path.
//  - ENTER_CAR_L starts standing at the door facing the car (the seat ahead, the car's front to the left) and ends
//    lowering into the seat at t = 1.1 s, turned 90 degrees left (the game seats the ped at 1.05 s and the animator
//    crossfades into SIT_DRIVE). EXIT_CAR_L starts from the seated pose 0.8 m to the right (inside the car, facing
//    the car's front) and ends standing at the origin. The _R variants are mirrored (passenger side).
//  - DEATH_FRONT (hit from the front) falls backwards and ends lying on the back, head towards -Y;
//    DEATH_BACK ends lying face down, head towards +Y.
//  - GET_UP_BACK starts lying on the back with the head towards -Y (feet forward, pelvis above the origin);
//    GET_UP_FRONT starts lying face down with the head towards +Y (pelvis above the origin). Both end standing at
//    the origin facing +Y.
#include "anim_internal.h"

namespace Anim {
namespace detail {

void boneModel(const Skeleton& sk, const Pose& pose, int bone, quat& q, vec3& t);

static const float kSeatHipZ = 0.5f;   // game: seated ped origin is 0.5 m below the seat hip point
static const float kWaterZ = 1.32f;    // game: swimming ped origin is 1.32 m below the water surface

// ------------------------------------------------------------------------------------------------
// Rig controls

struct ArmCtl {
    vec3 dir = vec3(0, 0.03f, -1);   // FK: upper arm direction in the chest frame
    vec3 pole = vec3(0, -1, 0);      // FK: direction the elbow points (chest frame)
    float elbow = 0.18f;             // FK: flexion (rad)
    float twist = 0.f;               // forearm pronation (rad, + = palm turns backwards)
    float wristFlex = 0.f, wristDev = 0.f;
    float fingers = 0.35f, thumb = 0.2f;
    float clavUp = 0.f, clavFwd = 0.f;
    bool ik = false;                 // hand (wrist joint) target in model space instead of FK
    vec3 target, ikPole = vec3(0, -1, 0);
    bool orient = false;             // desired hand orientation (model space)
    quat handRot;
};

struct LegCtl {
    bool ik = true;
    vec3 ankle;                      // IK: ankle target (model space)
    float pitch = 0.f, yaw = 0.f, roll = 0.f;   // IK: foot orientation (model space, + pitch = toes up)
    bool footQ = false;              // IK: use footRot instead of the angles
    quat footRot;
    float toe = 0.f;                 // toe bend relative to the foot (+ = toes up)
    vec3 knee = vec3(0, 1, 0);       // IK: knee direction
    float hipFlex = 0.f, hipAbd = 0.f, hipTwist = 0.f, kneeFlex = 0.f, ankleFlex = 0.f;   // FK (twist + = toes out)
};

struct Rig {
    vec3 pelvis;                     // pelvis joint offset from bind (model)
    float pelvisYaw = 0.f, pelvisPitch = 0.f, pelvisRoll = 0.f, pelvisTwist = 0.f;
    float spinePitch = 0.f, spineYaw = 0.f, spineRoll = 0.f;
    float neckPitch = 0.f, neckYaw = 0.f, neckRoll = 0.f;
    float headPitch = 0.f, headYaw = 0.f, headRoll = 0.f;
    ArmCtl arm[2];
    LegCtl leg[2];
    float jaw = 0.f;
    vec2 eyes;
};

struct AuthorCtx {
    Skeleton sk;
    BodyDims D;
    float footH;         // ankle joint height above the ground (bind)
    float legLen;
    vec3 hip[2], ankle[2], gh[2];
    vec3 pelvisBind, hipMidLocal;
    float heelBack, ballFwd, toeFwd;
    float fem = 0.f;     // style (0 male .. 1 female)
    vec3 chestP, headP;
    float shoulderZ;
    // locomotion strides are given for the male reference (ClipInfo::speed * duration); the female reference covers
    // the same stride per leg length (its gaits are authored at speed * strideScale)
    float strideScale = 1.f;
    float thighLen = 0.43f, shinLen = 0.43f;   // hip -> knee, knee -> ankle
    // skin of the reference body (bind pose) where hands rest on it: the right flank above the hip joint, the small of
    // the back, the front of the belly a little left of the middle (a forearm across the waist, clasped hands)
    vec3 skinFlankR, skinBack, skinBelly;
    vec3 skinUpperBack, skinWaistBack;   // the back at the chest joint's height / at the waist (partner's hands in a hug)
};

static inline quat eulerZXY(float yaw, float pitchFwd, float roll) { return qz(yaw) * qx(-pitchFwd) * qy(roll); }
static inline quat pelvisRot(const Rig& r) { return eulerZXY(r.pelvisYaw, r.pelvisPitch, r.pelvisRoll) * qz(r.pelvisTwist); }
static inline quat footEuler(const LegCtl& l) { return qz(l.yaw) * qx(l.pitch) * qy(l.roll); }
static inline vec3 nrmOr(vec3 v, vec3 fb) {
    float l = length(v);
    return l > 1e-6f ? v / l : fb;
}
static inline quat qAccum(quat acc, quat q, float w, quat ref) {
    if (dot(q, ref) < 0.f) q = quat(-q.x, -q.y, -q.z, -q.w);
    return quat(acc.x + q.x * w, acc.y + q.y * w, acc.z + q.z * w, acc.w + q.w * w);
}
// Rotation angle of q around a unit axis (swing-twist decomposition).
static inline float twistAngle(quat q, vec3 axis) {
    float p = q.x * axis.x + q.y * axis.y + q.z * axis.z, w = q.w;
    if (w < 0.f) {
        p = -p;
        w = -w;
    }
    return 2.f * atan2f(p, w);
}

// ------------------------------------------------------------------------------------------------
// Rig -> Pose

// twistRef (optional, per arm): forearm twist winding to stay closest to (the previous baked frame), instead of the
// authored ArmCtl::twist; twistOut receives the solved twist of IK arms.
static void rigToPose(const AuthorCtx& A, const Rig& r, Pose& out, const float* twistRef = nullptr, float* twistOut = nullptr) {
    const Skeleton& sk = A.sk;
    for (int b = 0; b < B_COUNT; b++) out.rot[b] = quat();
    out.rootOffset = r.pelvis;
    out.rot[B_PELVIS] = pelvisRot(r);
    const float sw[3] = {0.3f, 0.3f, 0.4f};
    for (int i = 0; i < 3; i++) out.rot[B_SPINE1 + i] = eulerZXY(r.spineYaw * sw[i], r.spinePitch * sw[i], r.spineRoll * sw[i]);
    out.rot[B_NECK] = eulerZXY(r.neckYaw, r.neckPitch, r.neckRoll);
    out.rot[B_HEAD] = eulerZXY(r.headYaw, r.headPitch, r.headRoll);
    out.rot[B_JAW] = qx(-r.jaw);
    out.rot[B_EYE_L] = out.rot[B_EYE_R] = qz(r.eyes.x) * qx(r.eyes.y);
    // arms (FK part; the clavicle is shared by FK and IK arms)
    for (int s = 0; s < 2; s++) {
        const ArmCtl& a = r.arm[s];
        float sx = s ? 1.f : -1.f;
        int o = s ? 4 : 0, fo = s ? 2 : 0;
        quat clav = qz(sx * a.clavFwd) * qy(-sx * a.clavUp);
        out.rot[B_CLAVICLE_L + o] = clav;
        vec3 bindDir = A.D.armDir[s], bindPole(0, -1, 0);
        vec3 pn = A.D.palmN[s];
        vec3 dn = normalize(a.dir);
        vec3 pl = a.pole - dn * dot(a.pole, dn);   // elbow direction orthogonal to the arm
        if (length2(pl) < 1e-4f) pl = cross(dn, vec3(sx, 0, 0));
        if (length2(pl) < 1e-6f) pl = vec3(0, -1, 0);
        quat rc = quatFromTwoPairs(bindDir, bindPole, dn, normalize(pl));
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
    // FK legs
    for (int s = 0; s < 2; s++) {
        const LegCtl& l = r.leg[s];
        int o = s ? 4 : 0;
        float sx = s ? 1.f : -1.f;
        out.rot[B_TOE_L + o] = qx(l.toe);
        if (!l.ik) {
            out.rot[B_THIGH_L + o] = qz(-sx * l.hipTwist) * qx(l.hipFlex) * qy(-sx * l.hipAbd);
            out.rot[B_CALF_L + o] = qx(-l.kneeFlex);
            out.rot[B_FOOT_L + o] = qx(l.ankleFlex);
        }
    }
    // IK legs: pre-orient the thigh towards the target with the knee towards the pole and pre-bend the knee about
    // its hinge, so the analytic solve only makes small corrections (continuous, no roll flips)
    for (int s = 0; s < 2; s++) {
        const LegCtl& l = r.leg[s];
        if (!l.ik) continue;
        int o = s ? 4 : 0;
        quat qp, qh;
        vec3 pp, hipP;
        boneModel(sk, out, B_PELVIS, qp, pp);
        boneModel(sk, out, B_THIGH_L + o, qh, hipP);
        vec3 d = nrmOr(l.ankle - hipP, vec3(0, 0, -1));
        vec3 kn = l.knee - d * dot(l.knee, d);
        if (length2(kn) < 1e-5f) kn = rotate(qp, vec3(0, 1, 0)) - d * dot(rotate(qp, vec3(0, 1, 0)), d);
        vec3 bindLeg = nrmOr(sk.bindLocalPos[B_CALF_L + o], vec3(0, 0, -1));
        vec3 bindKnee = vec3(0, 1, 0) - bindLeg * bindLeg.y;
        quat Qt = quatFromTwoPairs(bindLeg, normalize(bindKnee), d, nrmOr(kn, vec3(0, 1, 0)));
        out.rot[B_THIGH_L + o] = normalize(conj(qp) * Qt);
        out.rot[B_CALF_L + o] = qx(-0.35f);
        vec3 mid = lerp(hipP, l.ankle, 0.5f);
        vec3 poleP = mid + normalize(l.knee) * 0.5f;
        solveTwoBoneIK(sk, out, (Bone)(B_THIGH_L + o), (Bone)(B_CALF_L + o), (Bone)(B_FOOT_L + o), l.ankle, poleP, 1.f);
        quat qc;
        vec3 tc;
        boneModel(sk, out, B_CALF_L + o, qc, tc);
        quat want = l.footQ ? l.footRot : footEuler(l);
        out.rot[B_FOOT_L + o] = normalize(conj(qc) * want);
    }
    // IK arms
    for (int s = 0; s < 2; s++) {
        const ArmCtl& a = r.arm[s];
        if (!a.ik) continue;
        int o = s ? 4 : 0;
        float sx = s ? 1.f : -1.f;
        quat qu, qcl;
        vec3 sh, pcl;
        boneModel(sk, out, B_CLAVICLE_L + o, qcl, pcl);
        boneModel(sk, out, B_UPPERARM_L + o, qu, sh);
        vec3 ax = A.D.armDir[s];
        // pre-orient the upper arm at the target with the elbow towards the pole, pre-bend the elbow on its hinge
        vec3 d = nrmOr(a.target - sh, ax);
        vec3 pl = a.ikPole - d * dot(a.ikPole, d);
        if (length2(pl) < 1e-5f) pl = cross(d, vec3(sx, 0, 0));
        if (length2(pl) < 1e-8f) pl = vec3(0, -1, 0);
        quat Qu = quatFromTwoPairs(ax, vec3(0, -1, 0), d, normalize(pl));
        out.rot[B_UPPERARM_L + o] = normalize(conj(qcl) * Qu);
        vec3 hinge = normalize(cross(ax, vec3(0, 1, 0)));
        out.rot[B_FOREARM_L + o] = qaa(hinge, 0.35f);
        vec3 poleP = lerp(sh, a.target, 0.5f) + normalize(a.ikPole) * 0.5f;
        solveTwoBoneIK(sk, out, (Bone)(B_UPPERARM_L + o), (Bone)(B_FOREARM_L + o), (Bone)(B_HAND_L + o), a.target, poleP, 1.f);
        quat qf;
        vec3 tf;
        boneModel(sk, out, B_FOREARM_L + o, qf, tf);
        quat want;
        if (a.orient) want = a.handRot;
        else {
            // straight wrist with the authored pronation and flexion
            vec3 flexAx = normalize(cross(ax, A.D.palmN[s]));
            want = qf * qaa(ax, a.twist) * qaa(flexAx, a.wristFlex);
        }
        // distribute the twist between forearm and hand (no candy-wrapping at the wrist); the authored twist picks
        // the winding when the requested orientation is near half a turn
        quat local = normalize(conj(qf) * want);
        float tw = twistAngle(local, ax);
        float ref = twistRef ? twistRef[s] : a.twist;
        while (tw - ref > kPi) tw -= kTwoPi;
        while (tw - ref < -kPi) tw += kTwoPi;
        if (twistOut) twistOut[s] = tw;
        out.rot[B_FOREARM_L + o] = normalize(out.rot[B_FOREARM_L + o] * qaa(ax, tw * 0.5f));
        boneModel(sk, out, B_FOREARM_L + o, qf, tf);
        out.rot[B_HAND_L + o] = normalize(conj(qf) * want);
    }
}

static void boneOf(const AuthorCtx& A, const Rig& r, int bone, vec3& p, quat& q) {
    Pose ps;
    rigToPose(A, r, ps);
    boneModel(A.sk, ps, bone, q, p);
}
static vec3 bonePos(const AuthorCtx& A, const Rig& r, int bone) {
    vec3 p;
    quat q;
    boneOf(A, r, bone, p, q);
    return p;
}

// Set the pelvis offset so that the hip joints' midpoint lands on `hipTarget` (with the rig's pelvis rotation).
static void placeHips(const AuthorCtx& A, Rig& r, vec3 hipTarget) {
    r.pelvis = hipTarget - A.pelvisBind - rotate(pelvisRot(r), A.hipMidLocal);
}

// ------------------------------------------------------------------------------------------------
// Limb representation conversion (FK -> IK) and rig combination

static void armToIK(const AuthorCtx& A, const Pose& p, int s, ArmCtl& a) {
    int o = s ? 4 : 0;
    quat qh, qf, qu;
    vec3 ph, pf, pu;
    boneModel(A.sk, p, B_HAND_L + o, qh, ph);
    boneModel(A.sk, p, B_FOREARM_L + o, qf, pf);
    boneModel(A.sk, p, B_UPPERARM_L + o, qu, pu);
    vec3 pole = pf - (pu + ph) * 0.5f;
    a.ik = true;
    a.target = ph;
    a.ikPole = nrmOr(pole, rotate(qu, vec3(0, -1, 0)));
    a.orient = true;
    a.handRot = qh;
}

static void legToIK(const AuthorCtx& A, const Pose& p, int s, LegCtl& l) {
    int o = s ? 4 : 0;
    quat qf, qk, qt;
    vec3 pf, pk, pt;
    boneModel(A.sk, p, B_FOOT_L + o, qf, pf);
    boneModel(A.sk, p, B_CALF_L + o, qk, pk);
    boneModel(A.sk, p, B_THIGH_L + o, qt, pt);
    vec3 kd = pk - (pt + pf) * 0.5f;
    l.ik = true;
    l.ankle = pf;
    l.knee = nrmOr(kd, rotate(qk, vec3(0, 1, 0)));
    l.footQ = true;
    l.footRot = qf;
}

// Make the limb representations of several rigs agree (converting mismatching limbs to IK of their evaluated pose).
static void unifyRigs(const AuthorCtx& A, Rig* const* r, int n) {
    bool armMix[2] = {false, false}, legMix[2] = {false, false};
    for (int s = 0; s < 2; s++)
        for (int i = 1; i < n; i++) {
            if (r[i]->arm[s].ik != r[0]->arm[s].ik || r[i]->arm[s].orient != r[0]->arm[s].orient) armMix[s] = true;
            if (r[i]->leg[s].ik != r[0]->leg[s].ik) legMix[s] = true;
        }
    if (!armMix[0] && !armMix[1] && !legMix[0] && !legMix[1]) return;
    for (int i = 0; i < n; i++) {
        Pose p;
        rigToPose(A, *r[i], p);
        for (int s = 0; s < 2; s++) {
            if (armMix[s]) armToIK(A, p, s, r[i]->arm[s]);
            if (legMix[s]) legToIK(A, p, s, r[i]->leg[s]);
        }
    }
}

static void combineArm(const ArmCtl* const* a, const float* w, int n, int hk, ArmCtl& o) {
    const ArmCtl& h = *a[hk];
    vec3 dir(0), pole(0), tgt(0), ip(0);
    float el = 0, tw = 0, wf = 0, wd = 0, fi = 0, th = 0, cu = 0, cf = 0;
    quat hr(0, 0, 0, 0);
    for (int i = 0; i < n; i++) {
        const ArmCtl& x = *a[i];
        float k = w[i];
        dir = dir + x.dir * k;
        pole = pole + x.pole * k;
        tgt = tgt + x.target * k;
        ip = ip + x.ikPole * k;
        el += x.elbow * k;
        tw += x.twist * k;
        wf += x.wristFlex * k;
        wd += x.wristDev * k;
        fi += x.fingers * k;
        th += x.thumb * k;
        cu += x.clavUp * k;
        cf += x.clavFwd * k;
        hr = qAccum(hr, x.handRot, k, h.handRot);
    }
    ArmCtl res = h;
    res.dir = nrmOr(dir, h.dir);
    res.pole = nrmOr(pole, h.pole);
    res.target = tgt;
    res.ikPole = nrmOr(ip, h.ikPole);
    res.elbow = el;
    res.twist = tw;
    res.wristFlex = wf;
    res.wristDev = wd;
    res.fingers = fi;
    res.thumb = th;
    res.clavUp = cu;
    res.clavFwd = cf;
    res.handRot = normalize(hr);
    o = res;
}

static void combineLeg(const LegCtl* const* L, const float* w, int n, int hk, LegCtl& o) {
    const LegCtl& h = *L[hk];
    bool anyQ = false;
    for (int i = 0; i < n; i++) anyQ = anyQ || L[i]->footQ;
    vec3 an(0), kn(0);
    float pi = 0, ya = 0, ro = 0, to = 0, hf = 0, ha = 0, ht = 0, kf = 0, af = 0;
    quat ref = h.footQ ? h.footRot : footEuler(h);
    quat fq(0, 0, 0, 0);
    for (int i = 0; i < n; i++) {
        const LegCtl& x = *L[i];
        float k = w[i];
        an = an + x.ankle * k;
        kn = kn + x.knee * k;
        pi += x.pitch * k;
        ya += x.yaw * k;
        ro += x.roll * k;
        to += x.toe * k;
        hf += x.hipFlex * k;
        ha += x.hipAbd * k;
        ht += x.hipTwist * k;
        kf += x.kneeFlex * k;
        af += x.ankleFlex * k;
        if (anyQ) fq = qAccum(fq, x.footQ ? x.footRot : footEuler(x), k, ref);
    }
    LegCtl res = h;
    res.ankle = an;
    res.knee = nrmOr(kn, h.knee);
    res.pitch = pi;
    res.yaw = ya;
    res.roll = ro;
    res.toe = to;
    res.hipFlex = hf;
    res.hipAbd = ha;
    res.hipTwist = ht;
    res.kneeFlex = kf;
    res.ankleFlex = af;
    res.footQ = anyQ;
    if (anyQ) res.footRot = normalize(fq);
    o = res;
}

// Weighted combination of rigs (weights sum to 1; negative weights allowed for spline interpolation).
static void combineRigs(const Rig* const* r, const float* w, int n, Rig& o) {
    int hk = 0;
    for (int i = 1; i < n; i++)
        if (w[i] > w[hk]) hk = i;
    Rig res = *r[hk];
    res.pelvis = vec3(0);
    res.pelvisYaw = res.pelvisPitch = res.pelvisRoll = res.pelvisTwist = 0.f;
    res.spinePitch = res.spineYaw = res.spineRoll = 0.f;
    res.neckPitch = res.neckYaw = res.neckRoll = 0.f;
    res.headPitch = res.headYaw = res.headRoll = 0.f;
    res.jaw = 0.f;
    res.eyes = vec2(0);
    for (int i = 0; i < n; i++) {
        const Rig& x = *r[i];
        float k = w[i];
        res.pelvis = res.pelvis + x.pelvis * k;
        res.pelvisYaw += x.pelvisYaw * k;
        res.pelvisPitch += x.pelvisPitch * k;
        res.pelvisRoll += x.pelvisRoll * k;
        res.pelvisTwist += x.pelvisTwist * k;
        res.spinePitch += x.spinePitch * k;
        res.spineYaw += x.spineYaw * k;
        res.spineRoll += x.spineRoll * k;
        res.neckPitch += x.neckPitch * k;
        res.neckYaw += x.neckYaw * k;
        res.neckRoll += x.neckRoll * k;
        res.headPitch += x.headPitch * k;
        res.headYaw += x.headYaw * k;
        res.headRoll += x.headRoll * k;
        res.jaw += x.jaw * k;
        res.eyes = res.eyes + x.eyes * k;
    }
    for (int s = 0; s < 2; s++) {
        const ArmCtl* ap[4];
        const LegCtl* lp[4];
        for (int i = 0; i < n && i < 4; i++) {
            ap[i] = &r[i]->arm[s];
            lp[i] = &r[i]->leg[s];
        }
        combineArm(ap, w, n, hk, res.arm[s]);
        combineLeg(lp, w, n, hk, res.leg[s]);
    }
    o = res;
}

// Mirror left/right (for the passenger-side car clips).
static Rig mirrorRig(const Rig& a) {
    Rig o = a;
    auto mx = [](vec3 v) { return vec3(-v.x, v.y, v.z); };
    auto mq = [](quat q) { return quat(q.x, -q.y, -q.z, q.w); };
    o.pelvis = mx(a.pelvis);
    o.pelvisYaw = -a.pelvisYaw;
    o.pelvisRoll = -a.pelvisRoll;
    o.pelvisTwist = -a.pelvisTwist;
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
        da.handRot = mq(sa.handRot);
        const LegCtl& sl = a.leg[1 - s];
        LegCtl& dl = o.leg[s];
        dl = sl;
        dl.ankle = mx(sl.ankle);
        dl.yaw = -sl.yaw;
        dl.roll = -sl.roll;
        dl.knee = mx(sl.knee);
        dl.footRot = mq(sl.footRot);
    }
    return o;
}

// ------------------------------------------------------------------------------------------------
// Keyframes: time-aware Catmull-Rom through rig poses

struct Key {
    float t;
    Rig r;
};

static void sampleKeys(const AuthorCtx& A, const std::vector<Key>& keys, float t, bool loop, float dur, Rig& out, float tension = 0.f) {
    int n = (int)keys.size();
    if (n == 1) {
        out = keys[0].r;
        return;
    }
    int i0, i1, ip, in;
    float t0, t1, tp, tn;
    bool hasPrev, hasNext;
    if (loop) {
        t = t - floorf(t / dur) * dur;
        float tt = t < keys[0].t ? t + dur : t;
        int i = n - 1;
        for (int k = 0; k < n; k++) {
            float ka = keys[k].t, kb = k + 1 < n ? keys[k + 1].t : keys[0].t + dur;
            if (tt >= ka && tt <= kb) {
                i = k;
                break;
            }
        }
        i0 = i;
        i1 = (i + 1) % n;
        ip = (i + n - 1) % n;
        in = (i + 2) % n;
        t0 = keys[i0].t;
        t1 = keys[i1].t + (i + 1 >= n ? dur : 0.f);
        tp = keys[ip].t - (i == 0 ? dur : 0.f);
        tn = keys[in].t + (i + 2 >= n ? dur : 0.f);
        hasPrev = hasNext = true;
        t = tt;
    } else {
        if (t <= keys[0].t) {
            out = keys[0].r;
            return;
        }
        if (t >= keys[n - 1].t) {
            out = keys[n - 1].r;
            return;
        }
        int i = 0;
        while (i + 2 < n && t > keys[i + 1].t) i++;
        i0 = i;
        i1 = i + 1;
        hasPrev = i > 0;
        hasNext = i + 2 < n;
        ip = hasPrev ? i - 1 : i;
        in = hasNext ? i + 2 : i + 1;
        t0 = keys[i0].t;
        t1 = keys[i1].t;
        tp = keys[ip].t;
        tn = keys[in].t;
    }
    float D = Max(t1 - t0, 1e-4f);
    float u = Saturate((t - t0) / D);
    float u2 = u * u, u3 = u2 * u;
    float h00 = 2.f * u3 - 3.f * u2 + 1.f, h10 = u3 - 2.f * u2 + u, h01 = -2.f * u3 + 3.f * u2, h11 = u3 - u2;
    float a = hasPrev ? (1.f - tension) / Max(t1 - tp, 1e-4f) : 0.f;
    float b = hasNext ? (1.f - tension) / Max(tn - t0, 1e-4f) : 0.f;
    float w[4] = {-h10 * D * a, h00 - h11 * D * b, h01 + h10 * D * a, h11 * D * b};
    Rig r0 = keys[ip].r, r1 = keys[i0].r, r2 = keys[i1].r, r3 = keys[in].r;
    Rig* rr[4] = {&r0, &r1, &r2, &r3};
    unifyRigs(A, rr, 4);
    const Rig* cr[4] = {&r0, &r1, &r2, &r3};
    combineRigs(cr, w, 4, out);
}

// ------------------------------------------------------------------------------------------------
// Pose building helpers

static float easeInOut(float t) {
    t = Saturate(t);
    return t * t * (3.f - 2.f * t);
}
static float smoothPulse(float t) {
    t = Saturate(t);
    return sinf(kPi * t);
}

static void armFK(ArmCtl& a, int side, float fwdSwing, float abd, float elbow, float twist, float fingers) {
    float sx = side ? 1.f : -1.f;
    float ca = cosf(abd);
    a.ik = false;
    a.orient = false;
    a.dir = normalize(vec3(sx * sinf(abd), sinf(fwdSwing) * ca, -cosf(fwdSwing) * ca));
    a.pole = normalize(vec3(sx * 0.35f, -cosf(fwdSwing), -sinf(fwdSwing)));
    a.elbow = elbow;
    a.twist = twist;
    a.fingers = fingers;
    a.thumb = fingers * 0.7f;
}

static void armIK(ArmCtl& a, vec3 target, vec3 pole, float fingers) {
    a.ik = true;
    a.orient = false;
    a.target = target;
    a.ikPole = normalize(pole);
    a.fingers = fingers;
    a.thumb = fingers * 0.8f;
}

// Hand orientation from the direction the hand points (wrist -> knuckles) and the palm normal.
static quat handFrame(const AuthorCtx& A, int s, vec3 fingerDir, vec3 palmDir) {
    return quatFromTwoPairs(A.D.armDir[s], A.D.palmN[s], normalize(fingerDir), normalize(palmDir));
}

static void standPose(const AuthorCtx& A, Rig& r) {
    r = Rig();
    for (int s = 0; s < 2; s++) {
        float sx = s ? 1.f : -1.f;
        LegCtl& l = r.leg[s];
        l.ik = true;
        l.ankle = A.ankle[s] + vec3(-sx * 0.01f * A.fem, 0.f, 0.f);
        l.yaw = -sx * 0.1f;
        l.knee = normalize(vec3(sx * 0.12f, 1.f, 0.f));
        armFK(r.arm[s], s, 0.03f, 0.1f + 0.03f * (1.f - A.fem), 0.2f, 0.15f, 0.38f);
    }
}

static void setFootFlat(const AuthorCtx& A, LegCtl& l, vec3 groundPt, float yaw) {
    l.ik = true;
    l.footQ = false;
    l.ankle = vec3(groundPt.x, groundPt.y, A.footH + groundPt.z);
    l.pitch = 0.f;
    l.roll = 0.f;
    l.yaw = yaw;
    l.toe = 0.f;
}

// Ankle position of a foot pitched by `pitch` whose heel (pivot 0) or ball (pivot 1) touches the ground at (x, y).
static vec3 ankleFromPivot(const AuthorCtx& A, float x, float y, float pitch, int pivot) {
    vec3 v = pivot == 0 ? vec3(0, A.heelBack, A.footH) : vec3(0, -A.ballFwd, A.footH);
    return vec3(x, y, 0.f) + rotate(qx(pitch), v);
}

// Foot on its ball at (x, y) with the heel raised by `pitch` (< 0) and the toes flat on the ground.
static void setFootToes(const AuthorCtx& A, LegCtl& l, float x, float y, float pitch, float yaw) {
    l.ik = true;
    l.footQ = false;
    l.pitch = pitch;
    l.yaw = yaw;
    l.roll = 0.f;
    l.toe = -pitch;
    l.ankle = ankleFromPivot(A, x, y, pitch, 1);
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
    float shift = 0.f;   // stance centre behind the hips along the travel direction (m, reference size)
    // walking style (WalkStyle): foot progression angle, where in the swing the foot is highest (the knee folds
    // early in a walk), trunk flexion, head carried forward / gaze lowered, shoulders rounded / raised, forward arm
    // swing towards the midline, wrist flexion
    float toeOut = 0.08f;
    float swingPeak = 0.5f;
    float postLean = 0.f;
    float neckFwd = 0.f, headDown = 0.f;
    float clavFwd = 0.f, clavUp = 0.f;
    float armCross = 0.f;
    float wristFlex = 0.f;
    // walking pelvis height from the stance legs: extra stance knee flexion (rad) and the loading-response dip scale
    float kneeFlex = 0.f, bobK = 1.f;
    // running: anterior pelvic tilt (rad; the lower back arches over it, the trunk keeps its lean), so the hip joint's
    // extension at toe-off stays anatomical (about 15-20 degrees) while the thigh trails as far behind
    float tilt = 0.f;
    // walking: the pelvis height over the cycle (kPelvisTab steps from phase 0, walkPelvis), else from this phase alone
    const float* zTab = nullptr;
};
static const int kPelvisTab = 96;

// Ankle position of a foot with the given pitch and yaw whose heel (pivot 0) or ball (pivot 1) touches the ground at p.
static vec3 ankleFromPivotYaw(const AuthorCtx& A, vec3 p, float pitch, float yaw, int pivot) {
    vec3 v = pivot == 0 ? vec3(0, A.heelBack, A.footH) : vec3(0, -A.ballFwd, A.footH);
    return vec3(p.x, p.y, 0.f) + rotate(qz(yaw) * qx(pitch), v);
}

static void gaitPose(const AuthorCtx& A, const GaitP& g, float phase, Rig& r) {
    standPose(A, r);
    const float T = g.T;
    const float v = g.speed * A.strideScale;
    const float Sst = v * g.duty * T;   // distance the body travels during one stance
    vec3 dir3(g.dir.x, g.dir.y, 0.f);
    bool fwd = g.dir.y > 0.5f, back = g.dir.y < -0.5f, lateral = fabsf(g.dir.x) > 0.5f;
    const float heelToBall = A.heelBack + A.ballFwd;
    // swing height profile: sin(pi u^k) peaks at u = swingPeak
    const float swingK = logf(0.5f) / logf(Clamp(g.swingPeak, 0.2f, 0.8f));
    vec3 ankles[2];
    float stanceU[2] = {-1.f, -1.f};   // stance progress 0..1 of a leg on the ground
    float swingU[2] = {-1.f, -1.f};    // swing progress 0..1 of a leg in the air
    for (int s = 0; s < 2; s++) {
        float sx = s ? 1.f : -1.f;
        float p = phase - (s ? 0.5f : 0.f);
        p -= floorf(p);
        float xc = sx * g.footSpread * A.D.s;
        LegCtl& l = r.leg[s];
        l.ik = true;
        const float fy = -sx * (lateral ? 0.02f : g.toeOut);
        l.yaw = fy;
        l.knee = normalize(vec3(sx * (0.1f + 0.5f * Max(0.f, g.toeOut - 0.08f)), 1.f, 0.f));   // knees follow the toes out
        const vec3 ballOff = rotate(qz(fy), vec3(0, heelToBall, 0));   // heel -> ball contact along the foot
        if (p < g.duty) {
            // stance: the contact point slides backwards (relative to the body) at the ground speed
            float u = p / g.duty;
            stanceU[s] = u;
            float along = Sst * 0.5f - u * Sst - g.shift * A.D.s;
            float pitch = 0.f, toe = 0.f;
            int pivot = 0;
            vec3 pc = vec3(xc, 0.f, 0.f) + dir3 * along;   // heel contact point
            if (fwd) {
                // heel strike -> foot flat -> heel off (rolling over the ball)
                float uFlat = g.run ? 0.1f : 0.16f, uHeelOff = g.run ? 0.5f : 0.62f;
                if (u < uFlat) {
                    pitch = g.strikePitch * (1.f - easeInOut(u / uFlat));
                } else if (u >= uHeelOff) {
                    pitch = g.toeOffPitch * easeInOut((u - uHeelOff) / (1.f - uHeelOff));
                    pivot = 1;
                    toe = -pitch;
                    pc = pc + ballOff;
                }
            } else if (back && u < 0.15f) {
                // backwards: toe first contact, then heel down
                pitch = -0.25f * (1.f - easeInOut(u / 0.15f));
                pivot = 1;
                toe = -pitch;
                pc = pc + ballOff;
            }
            l.ankle = ankleFromPivotYaw(A, pc, pitch, fy, pivot);
            l.pitch = pitch;
            l.toe = toe;
        } else {
            // swing: from the toe-off pose to the next contact pose
            float u = (p - g.duty) / (1.f - g.duty);
            swingU[s] = u;
            float e = easeInOut(u);
            float pStart = fwd ? g.toeOffPitch : 0.f;
            float pEnd = fwd ? g.strikePitch : (back ? -0.25f : 0.f);
            vec3 ps = vec3(xc, 0.f, 0.f) + dir3 * (-Sst * 0.5f - g.shift * A.D.s);
            vec3 pe = vec3(xc, 0.f, 0.f) + dir3 * (Sst * 0.5f - g.shift * A.D.s);
            if (fwd) ps = ps + ballOff;
            if (back) pe = pe + ballOff;
            vec3 a0 = ankleFromPivotYaw(A, ps, pStart, fy, fwd ? 1 : 0);
            vec3 a1 = ankleFromPivotYaw(A, pe, pEnd, fy, back ? 1 : 0);
            // horizontally a Hermite curve that leaves the ground and lands at ground speed (the foot is still in the
            // world at toe-off and at the heel strike), vertically eased
            const float u2 = u * u, u3 = u2 * u;
            const float h00 = 2.f * u3 - 3.f * u2 + 1.f, h10 = u3 - 2.f * u2 + u, h01 = 3.f * u2 - 2.f * u3, h11 = u3 - u2;
            const vec3 m0 = dir3 * (-v * (1.f - g.duty) * T);   // d(position)/du of a point at rest on the ground
            // a runner's foot is thrown up off the toes and paws back into the next contact at part of the ground speed
            const float k0 = g.run ? 0.35f : 1.f, k1 = g.run ? 0.6f : 1.f;
            vec3 ank = a0 * h00 + m0 * (h10 * k0 + h11 * k1) + a1 * h01;
            ank.z = Lerp(a0.z, a1.z, e);
            // (eased in over the first eighth: the foot leaves its toes with the speed the roll-off gave it, rather
            // than at once)
            float h = g.lift * A.D.s * powf(smoothPulse(powf(u, swingK)), 0.8f) * (g.run ? 1.f : easeInOut(u * 8.f));
            if (g.run) h += g.kick * A.D.s * smoothPulse(Saturate(u * 1.6f)) * 0.9f;
            ank.z += h;
            if (g.run && fwd) ank = ank - dir3 * (g.kick * A.D.s * 0.15f * smoothPulse(Saturate(u * 1.6f)));   // heel kick (the knee folds)
            if (lateral) ank = ank + vec3(0, 0.04f * A.D.s * smoothPulse(u), 0);
            float pm = fwd ? Lerp(pStart, pEnd, easeInOut(Saturate((u - 0.1f) / 0.8f))) : Lerp(pStart, pEnd, e);
            if (fwd && !g.run) pm += 0.12f * smoothPulse(u);   // toes up during mid swing
            l.pitch = pm;
            l.toe = fwd ? Max(0.f, -pm) * (1.f - easeInOut(u * 2.f)) : 0.f;
            if (back && u > 0.8f) l.toe = -pm * easeInOut((u - 0.8f) / 0.2f);
            l.ankle = ank;
        }
        ankles[s] = l.ankle;
    }
    // pelvis
    float c2 = cosf(kTwoPi * phase * 2.f);
    float bobPhase = g.run ? cosf(kTwoPi * (phase - g.duty * 0.5f) * 2.f) : c2;
    float z = -g.drop - g.bob * (0.5f + 0.5f * bobPhase);
    float s1 = sinf(kTwoPi * phase), c1 = cosf(kTwoPi * phase);
    float fem = A.fem;
    r.pelvis = vec3(0, 0, z * A.D.s);
    r.pelvis.x += -g.sway * A.D.s * (1.f + 0.6f * fem) * s1 * (lateral ? 0.4f : 1.f);
    r.pelvisYaw = (fwd || back ? -1.f : 0.3f) * g.yawA * (1.f + 0.5f * fem) * c1 * (back ? -1.f : 1.f);
    r.pelvisRoll = g.rollA * (1.f + 0.8f * fem) * s1;
    r.pelvisPitch = g.lean * 0.4f + g.postLean * 0.25f + g.tilt;
    if (fwd && !g.run) {
        // walking: the pelvis rides on the stance legs, whose knees follow a normative stance flexion (about 5 deg at
        // the heel strike, a ~15 deg loading dip at a fifth of the stance, ~5 deg at mid-stance: an inverted
        // pendulum, highest over the stance foot); a leg rolling off its toes no longer holds the pelvis up once the
        // other foot is down (until then the foot stays on its ball, the heel rising), and a leg about to land takes
        // the pelvis down to its reach by the heel strike (the falling step into double support, the heel meeting
        // the ground on time)
        const float la = A.thighLen, lb = A.shinLen;
        quat pr = pelvisRot(r);
        float best = 1e9f;
        for (int s = 0; s < 2; s++) {
            float u = stanceU[s], bonus;
            if (u >= 0.f) {
                float uo = stanceU[1 - s];
                bonus = 0.12f * A.D.s * sstep(0.72f, 0.92f, u) * (uo >= 0.f ? sstep(0.f, 0.12f, uo) : 0.f);
            } else {
                if (swingU[s] < 0.75f) continue;
                bonus = 0.12f * A.D.s * (1.f - sstep(0.75f, 1.f, swingU[s]));
                u = 0.f;
            }
            float dk = (u - 0.2f) / 0.11f;
            float th = 0.08f + g.kneeFlex + 0.2f * g.bobK * expf(-dk * dk);
            float dLeg = sqrtf(la * la + lb * lb + 2.f * la * lb * cosf(th));
            vec3 hip = A.pelvisBind + vec3(r.pelvis.x, r.pelvis.y, 0.f) + rotate(pr, A.hip[s] - A.pelvisBind);
            vec3 d = ankles[s] - hip;
            float hz = ankles[s].z + sqrtf(Max(0.f, dLeg * dLeg - d.x * d.x - d.y * d.y)) - hip.z;
            best = sminf(best, hz + bonus, 0.02f * A.D.s);
        }
        if (g.zTab) {
            float f = phase * kPelvisTab;
            int i0 = (int)floorf(f);
            f -= (float)i0;
            i0 = ((i0 % kPelvisTab) + kPelvisTab) % kPelvisTab;
            float z = Lerp(g.zTab[i0], g.zTab[(i0 + 1) % kPelvisTab], f);
            r.pelvis.z = best < 1e8f ? Min(z, best) : z;
        } else if (best < 1e8f) {
            r.pelvis.z = best;
        }
    } else {
        // lower the pelvis where a leg would overstretch (limited: beyond that the foot simply leaves the ground a
        // bit early/late, which reads better than a crouching gait)
        float nominalZ = r.pelvis.z;
        for (int s = 0; s < 2; s++) {
            vec3 hip = A.hip[s] + vec3(r.pelvis.x, r.pelvis.y, 0.f);
            vec3 d = ankles[s] - hip;
            float L = A.legLen * 0.99f;
            float dz = sqrtf(Max(0.f, L * L - d.x * d.x - d.y * d.y));
            float maxZ = ankles[s].z + dz - A.hip[s].z;
            if (r.pelvis.z > maxZ) r.pelvis.z = maxZ;
        }
        r.pelvis.z = Max(r.pelvis.z, nominalZ - (g.run ? 0.03f : 0.05f) * A.D.s);
    }
    r.spinePitch = g.lean * 0.6f + g.postLean * 0.75f + (g.run ? 0.03f * c2 : 0.01f * c2) - g.tilt * 0.85f;
    r.spineYaw = -r.pelvisYaw + g.chestYaw * c1 * (fwd ? 1.f : (back ? -1.f : 0.2f));
    r.spineRoll = -r.pelvisRoll * 0.7f;
    // head stabilization: keep facing forward and level (a stooped trunk carries the head forward, the neck extends
    // to look ahead)
    r.neckYaw = -(r.pelvisYaw + r.spineYaw) * 0.45f;
    r.headYaw = -(r.pelvisYaw + r.spineYaw) * 0.5f;
    r.headPitch = -(r.pelvisPitch + r.spinePitch) * 0.55f * g.headBob + 0.03f - g.neckFwd * 1.15f + g.headDown;
    r.neckPitch = -(r.pelvisPitch + r.spinePitch) * 0.2f + g.neckFwd;
    r.headRoll = -(r.pelvisRoll + r.spineRoll) * 0.7f;
    // arms swing opposite to the same-side leg
    for (int s = 0; s < 2; s++) {
        float sx = s ? 1.f : -1.f;
        float w = (s ? 1.f : -1.f) * c1;   // + = forward
        if (back) w = -w;
        if (lateral) w *= 0.3f;
        float swing = g.armSwing * w + (g.run ? 0.1f : 0.02f) - g.postLean * 0.6f;   // a stooped trunk: the arms still hang
        float elbow = g.elbow + g.elbowSwing * Max(0.f, w);
        // the hands pass beside the thighs and hips, not through them: a little more abduction on the forward swing
        // of a walk, elbows out on the back swing of a run
        float abd = g.run ? g.armAbd + 0.08f + 0.07f * fem + 0.12f * Max(0.f, -w) : g.armAbd * (1.f - 0.25f * fem) + 0.05f * Max(0.f, w);
        armFK(r.arm[s], s, swing, abd, elbow, g.run ? 0.35f : 0.15f, g.fist);
        // elbow pointing back (a little out): the forearm flexes forward, the hand only slightly towards the midline
        r.arm[s].pole = g.run ? normalize(vec3(sx * 0.28f, -1.f, 0.2f)) : normalize(vec3(sx * 0.12f, -cosf(swing), -sinf(swing)));
        r.arm[s].dir = normalize(r.arm[s].dir + vec3(-sx * (0.05f * fem + g.armCross + (g.run ? 0.06f : 0.f)) * Max(0.f, w), 0, 0));
        r.arm[s].clavFwd = 0.04f * w + g.clavFwd;
        r.arm[s].clavUp = (g.run ? 0.02f : 0.f) + g.clavUp;
        r.arm[s].wristFlex = g.wristFlex;
    }
}

// ------------------------------------------------------------------------------------------------
// Walking styles
//
// Step length and cadence follow the walk ratio (step length / cadence, nearly constant for a person over 0.8-2 m/s:
// Sekiya & Nagasaki 1998): step = sqrt(60 v WR), cadence = sqrt(60 v / WR) steps per minute. The neutral ratio gives
// the male reference 92 / 109 / 127 steps per minute with 0.65 / 0.77 / 0.92 m steps at 1.0 / 1.4 / 1.95 m/s.
static const float kWalkRatio = 0.007058f;   // m per (step/min), male reference (1.78 m)
struct WalkStyle {
    float wr, dutyAdd;                    // walk ratio multiplier (< 1: short quick steps), longer double support
    float lift, strike, toeOff;           // swing clearance, heel strike and toe-off pitch multipliers
    float bob, sway, yaw, roll, chest;    // pelvis bob / sway / rotation / list and shoulder counter-rotation multipliers
    float arm, elbowAdd, armOut, fist;    // arm swing multiplier, elbow flexion added (rad), extra abduction, hand curl
    float lean, neckFwd, headDown;        // trunk flexion, head carried forward, gaze lowered (rad)
    float clavFwd, clavUp;                // shoulders rounded forward / raised (rad)
    float toeOut, spread, kneeBend;       // foot progression angle (rad), base width multiplier, pelvis drop (m)
    float armCross;                       // forward swing towards the midline
};
static const WalkStyle kWalkStyles[GS_COUNT] = {
    // wr     duty+   lift   strk   toe   bob    sway   yaw    roll  chest  arm    elb+    out    fist   lean    neck    down    clvF    clvU    toeO   sprd   knee    cross
    {1.00f, 0.f,    1.f,   1.f,   1.f,  1.f,   1.f,   1.f,   1.f,  1.f,   1.f,   0.f,    0.f,   0.34f, 0.f,    0.f,    0.f,    0.f,    0.f,    0.08f, 1.f,   0.f,    0.f},  // neutral
    {1.04f, 0.f,    0.9f,  0.9f,  0.9f, 0.9f,  1.15f, 1.1f,  1.1f, 1.1f,  0.85f, -0.05f, 0.05f, 0.26f, -0.02f, 0.f,    -0.03f, 0.f,    0.f,    0.13f, 1.05f, 0.f,    0.f},  // relaxed stroll
    {0.97f, -0.01f, 1.05f, 1.1f,  1.1f, 0.8f,  0.7f,  1.15f, 0.9f, 0.9f,  0.9f,  0.4f,   0.04f, 0.42f, 0.07f,  0.06f,  0.03f,  0.03f,  0.03f,  0.06f, 0.9f,  0.f,    0.05f},  // hurried commuter
    {0.8f,  0.045f, 0.5f,  0.4f,  0.5f, 0.55f, 1.3f,  0.6f,  0.8f, 0.5f,  0.45f, 0.15f, 0.16f,  0.32f, 0.1f , 0.08f ,  0.02f,  0.12f,  0.02f,  0.18f, 1.2f,  0.03f,  0.f},  // elderly shuffle
    {1.12f, 0.f,    1.05f, 1.05f, 1.f,  1.25f, 1.3f,  1.25f, 1.3f, 1.9f,  1.3f,  0.1f,   0.12f, 0.4f,  -0.05f, -0.02f, -0.05f, -0.07f, 0.f,    0.2f,  1.2f,  0.005f, 0.f},  // confident swagger
    {0.88f, 0.02f,  0.62f, 0.6f,  0.7f, 0.75f, 1.2f,  0.8f,  1.1f, 0.7f,  0.5f,  -0.05f, 0.02f, 0.22f, 0.09f,  0.04f,  0.14f,  0.07f,  -0.04f, 0.1f,  1.05f, 0.012f, 0.f},  // tired drag
};

// Cycle time of a walk band for a style (male reference): stride = 2 * step.
static float walkCycle(int style, int band) {
    float v = kGaitBandSpeed[band];
    float step = sqrtf(60.f * v * kWalkRatio * kWalkStyles[style].wr);
    return 2.f * step / v;
}

static void walkGait(int style, int band, GaitP& g) {
    const WalkStyle& ws = kWalkStyles[Clamp(style, 0, GS_COUNT - 1)];
    band = Clamp(band, 0, kGaitBands - 1);
    // neutral values at slow / normal / brisk
    static const float duty[3] = {0.645f, 0.62f, 0.595f}, lift[3] = {0.085f, 0.1f, 0.115f}, bob[3] = {0.028f, 0.035f, 0.042f};
    static const float sway[3] = {0.026f, 0.022f, 0.018f}, yawA[3] = {0.055f, 0.07f, 0.09f}, rollA[3] = {0.045f, 0.05f, 0.055f};
    static const float lean[3] = {0.025f, 0.04f, 0.065f}, chest[3] = {0.08f, 0.1f, 0.13f}, arm[3] = {0.22f, 0.3f, 0.42f};
    static const float elbow[3] = {0.22f, 0.25f, 0.4f}, elbowSw[3] = {0.15f, 0.2f, 0.3f}, strike[3] = {0.24f, 0.3f, 0.36f};
    static const float toeOff[3] = {-0.7f, -0.82f, -0.92f}, shift[3] = {0.09f, 0.11f, 0.13f};
    g = GaitP();
    g.speed = kGaitBandSpeed[band];
    g.T = walkCycle(style, band);
    g.duty = duty[band] + ws.dutyAdd;
    g.lift = lift[band] * ws.lift;
    g.bob = bob[band] * ws.bob;
    g.drop = 0.012f + ws.kneeBend;
    g.sway = sway[band] * ws.sway;
    g.yawA = yawA[band] * ws.yaw;
    g.rollA = rollA[band] * ws.roll;
    g.lean = lean[band];
    g.chestYaw = chest[band] * ws.chest;
    g.armSwing = arm[band] * ws.arm;
    g.armAbd = 0.15f + ws.armOut;
    g.elbow = Max(0.08f, elbow[band] + ws.elbowAdd);
    g.elbowSwing = elbowSw[band] * Lerp(1.f, ws.arm, 0.5f);
    g.fist = ws.fist + 0.04f * (float)band;
    g.footSpread = 0.1f * ws.spread;
    g.strikePitch = strike[band] * ws.strike;
    g.toeOffPitch = toeOff[band] * ws.toeOff;
    g.shift = shift[band];
    g.toeOut = ws.toeOut;
    g.swingPeak = 0.22f;
    g.kneeFlex = ws.kneeBend * 4.f;
    g.bobK = ws.bob;
    g.postLean = ws.lean;
    g.neckFwd = ws.neckFwd;
    g.headDown = ws.headDown;
    g.clavFwd = ws.clavFwd;
    g.clavUp = ws.clavUp;
    g.armCross = ws.armCross;
}

// ------------------------------------------------------------------------------------------------
// Clip table

static const ClipInfo kClipInfo[CLIP_COUNT] = {
    {"idle", 4.0f, true, 0.f},          {"idle_look", 6.0f, true, 0.f},      {"walk", 1.1f, true, 1.4f},
    {"jog", 0.74f, true, 3.0f},         {"run", 0.66f, true, 5.0f},          {"sprint", 0.6f, true, 7.0f},
    {"walk_back", 0.85f, true, 1.2f},   {"strafe_l", 0.9f, true, 1.3f},      {"strafe_r", 0.9f, true, 1.3f},
    {"crouch_idle", 3.0f, true, 0.f},   {"crouch_walk", 1.3f, true, 1.0f},   {"jump_start", 0.35f, false, 0.f},
    {"fall", 1.0f, true, 0.f},          {"land", 0.55f, false, 0.f},         {"aim_pistol", 2.0f, true, 0.f},
    {"aim_rifle", 2.0f, true, 0.f},     {"fire_pistol", 0.3f, false, 0.f},   {"fire_rifle", 0.12f, false, 0.f},
    {"reload", 1.9f, false, 0.f},       {"throw", 1.2f, false, 0.f},         {"punch_l", 0.5f, false, 0.f},
    {"punch_r", 0.62f, false, 0.f},     {"kick", 0.9f, false, 0.f},          {"block", 1.5f, true, 0.f},
    {"hit_front", 0.55f, false, 0.f},   {"hit_back", 0.55f, false, 0.f},     {"stagger", 1.3f, false, 0.f},
    {"death_front", 1.6f, false, 0.f},  {"death_back", 1.6f, false, 0.f},    {"sit_drive", 3.0f, true, 0.f},
    {"sit_passenger", 4.0f, true, 0.f}, {"ride_bike", 2.0f, true, 0.f},      {"enter_car_l", 1.1f, false, 0.f},
    {"exit_car_l", 1.0f, false, 0.f},   {"enter_car_r", 1.1f, false, 0.f},   {"exit_car_r", 1.0f, false, 0.f},
    {"swim_idle", 2.4f, true, 0.f},     {"swim", 1.4f, true, 1.2f},          {"climb", 1.2f, false, 0.f},
    {"vault", 0.7f, false, 0.f},        {"cower", 2.0f, true, 0.f},          {"hands_up", 3.0f, true, 0.f},
    {"flee", 0.64f, true, 5.5f},        {"talk", 5.0f, true, 0.f},           {"talk_phone", 6.0f, true, 0.f},
    {"sit_bench", 5.0f, true, 0.f},     {"smoke", 8.0f, true, 0.f},          {"dance", 2.0f, true, 0.f},
    {"wave", 1.6f, true, 0.f},          {"point", 2.5f, true, 0.f},          {"cheer", 1.6f, true, 0.f},
    {"lean_wall", 5.0f, true, 0.f},     {"sunbathe", 6.0f, true, 0.f},       {"jog_idle", 0.8f, true, 0.f},
    {"get_up_front", 1.7f, false, 0.f}, {"get_up_back", 1.6f, false, 0.f},   {"hook", 0.95f, false, 0.f},
    {"uppercut", 0.8f, false, 0.f},     {"bat_swing", 1.05f, false, 0.f},    {"bat_overhead", 1.2f, false, 0.f},
    {"knife_slash", 0.55f, false, 0.f}, {"knife_stab", 0.75f, false, 0.f},   {"dodge_back", 0.5f, false, 0.f},
    {"dodge_l", 0.5f, false, 0.f},      {"dodge_r", 0.5f, false, 0.f},       {"hit_head", 0.45f, false, 0.f},
    {"hit_body", 0.9f, false, 0.f},     {"knockout", 1.5f, false, 0.f},      {"takedown_attacker", 3.0f, false, 0.f},
    {"takedown_victim", 3.0f, false, 0.f}, {"counter", 0.8f, false, 0.f},
    {"hug", 3.2f, false, 0.f},          {"handshake", 2.4f, false, 0.f},     {"cheek_kiss", 2.0f, false, 0.f},
};

// Internal clips (see anim_internal.h) used by the animator: ids CLIP_COUNT + i.
static const int kExtraCount = IC_END - CLIP_COUNT;
static const ClipInfo kExtraInfo[IC_GAIT_FIRST - CLIP_COUNT] = {
    {"rifle_carry", 2.0f, true, 0.f}, {"guard", 1.2f, true, 0.f},     {"guard_knife", 1.4f, true, 0.f},
    {"guard_bat", 1.6f, true, 0.f},   {"block_bat", 1.5f, true, 0.f},  {"idle_crossarms", 5.0f, true, 0.f},
    {"idle_pockets", 5.0f, true, 0.f}, {"idle_hip", 6.0f, true, 0.f},  {"idle_phone", 6.0f, true, 0.f},
    {"idle_stretch", 4.0f, false, 0.f}, {"dance2", 2.0f, true, 0.f},    {"dance3", 1.0f, true, 0.f},
    {"dance4", 2.0f, true, 0.f},      {"sit_ground", 6.0f, true, 0.f}, {"lie_front", 6.0f, true, 0.f},
    {"jog_slow", 0.774f, true, 2.4f},
    {"stand_l", 7.0f, true, 0.f},      {"stand_r", 7.0f, true, 0.f},      {"idle_behind", 6.0f, true, 0.f},
    {"idle_clasp", 6.0f, true, 0.f},   {"fidget_watch", 2.6f, false, 0.f}, {"fidget_scratch", 2.8f, false, 0.f},
    {"fidget_tug", 1.8f, false, 0.f},  {"fidget_chin", 3.2f, false, 0.f},  {"fidget_yawn", 3.0f, false, 0.f},
    {"fidget_arms", 3.4f, false, 0.f}, {"fidget_tap", 3.0f, true, 0.f},    {"fidget_rock", 3.75f, true, 0.f},
    {"carry_case", 2.0f, true, 0.f},   {"carry_hang_l", 2.0f, true, 0.f},  {"carry_hang_r", 2.0f, true, 0.f},
    {"carry_cup_r", 2.0f, true, 0.f},  {"carry_cup_l", 2.0f, true, 0.f},   {"carry_umbrella", 2.0f, true, 0.f},
    {"carry_rod", 2.0f, true, 0.f},    {"carry_board", 2.0f, true, 0.f},
};
// Walk style clips: cycle time from the style's walk ratio at the band speed.
static const ClipInfo* gaitInfoTable() {
    static const char* const kNames[GS_COUNT * kGaitBands] = {
        "walk_neutral_slow", "walk_neutral",  "walk_neutral_brisk", "walk_relaxed_slow", "walk_relaxed",  "walk_relaxed_brisk",
        "walk_hurried_slow", "walk_hurried",  "walk_hurried_brisk", "walk_elderly_slow", "walk_elderly",  "walk_elderly_brisk",
        "walk_swagger_slow", "walk_swagger",  "walk_swagger_brisk", "walk_tired_slow",   "walk_tired",    "walk_tired_brisk",
    };
    static const ClipInfo* tbl = []() {
        static ClipInfo t[GS_COUNT * kGaitBands];
        for (int s = 0; s < GS_COUNT; s++)
            for (int b = 0; b < kGaitBands; b++) t[s * kGaitBands + b] = {kNames[s * kGaitBands + b], walkCycle(s, b), true, kGaitBandSpeed[b]};
        return t;
    }();
    return tbl;
}
static const ClipInfo& infoOf(int id) {
    if (id < CLIP_COUNT) return kClipInfo[id];
    if (id < IC_GAIT_FIRST) return kExtraInfo[id - CLIP_COUNT];
    return gaitInfoTable()[Min(id, (int)IC_GAIT_LAST) - IC_GAIT_FIRST];
}

// ------------------------------------------------------------------------------------------------
// Common poses

// Fist at a wrist target with a straight wrist (the hand continues the solved forearm) and the palm turned towards
// palmDir (orthogonalized).
static void fistArm(const AuthorCtx& A, Rig& r, int sd, vec3 wrist, vec3 pole, vec3 palmDir) {
    armIK(r.arm[sd], wrist, pole, 0.95f);
    r.arm[sd].orient = false;
    Pose ps;
    rigToPose(A, r, ps);
    quat qf, qh;
    vec3 pf, ph;
    int o = sd ? 4 : 0;
    boneModel(A.sk, ps, B_FOREARM_L + o, qf, pf);
    boneModel(A.sk, ps, B_HAND_L + o, qh, ph);
    vec3 F = nrmOr(ph - pf, vec3(0, 0, 1));
    r.arm[sd].orient = true;
    r.arm[sd].handRot = handFrame(A, sd, F, nrmOr(palmDir - F * dot(palmDir, F), vec3(sd ? -1.f : 1.f, 0.f, 0.f)));
}

static void guardPose(const AuthorCtx& A, Rig& r) {
    standPose(A, r);
    const float s = A.D.s;
    // fighting stance: left foot forward, knees bent, chin down, fists up in front of the cheeks (lead hand forward),
    // forearms near vertical, elbows in, palms facing each other
    setFootFlat(A, r.leg[0], vec3(A.ankle[0].x - 0.02f * s, 0.14f * s, 0.f), 0.15f);
    setFootFlat(A, r.leg[1], vec3(A.ankle[1].x + 0.04f * s, -0.12f * s, 0.f), -0.45f);
    r.pelvis = vec3(0, 0, -0.06f * s);
    r.pelvisYaw = -0.25f;
    r.spineYaw = 0.1f;
    r.spinePitch = 0.12f;
    r.headPitch = 0.1f;
    r.headYaw = 0.12f;
    r.arm[0].clavFwd = r.arm[1].clavFwd = 0.08f;
    fistArm(A, r, 0, vec3(-0.1f, 0.38f, 1.38f) * s, vec3(-0.45f, -0.1f, -1.f), vec3(1.f, 0.f, 0.f));
    fistArm(A, r, 1, vec3(0.11f, 0.28f, 1.4f) * s, vec3(0.45f, -0.2f, -1.f), vec3(-1.f, 0.f, 0.f));
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

// Lying on the back (supine: head towards -Y) or face down (prone: head towards +Y); pelvis above (0, pelvisY).
static void lyingPose(const AuthorCtx& A, Rig& r, bool onBack, float variant, float pelvisY) {
    r = Rig();
    const float s = A.D.s;
    if (onBack) {
        r.pelvisPitch = -kHalfPi;
        r.pelvis = vec3(0, pelvisY, 0.105f * s) - A.pelvisBind;
        r.spinePitch = 0.1f;
        r.neckPitch = 0.05f;
        r.headPitch = 0.05f;
        r.headYaw = 0.4f * (variant - 0.5f) * 2.f;
        r.headRoll = 0.15f * (variant - 0.5f);
    } else {
        r.pelvisPitch = kHalfPi;
        r.pelvis = vec3(0, pelvisY, 0.1f * s) - A.pelvisBind;
        r.spinePitch = -0.05f;
        float side = variant > 0.5f ? 1.f : -1.f;
        r.headYaw = side * 1.25f;   // cheek on the ground
        r.headPitch = -0.3f;
        r.neckPitch = -0.3f;
        r.neckYaw = side * 0.2f;
    }
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        LegCtl& l = r.leg[sd];
        l.ik = false;
        ArmCtl& a = r.arm[sd];
        a.ik = false;
        // limp hands lying on the ground: nearly flat (the jointed fingers would dig into it)
        a.fingers = 0.12f;
        a.thumb = 0.05f;
        if (onBack) {
            l.hipFlex = 0.06f + 0.12f * variant * sd;
            l.hipAbd = 0.14f + 0.06f * sd;
            l.hipTwist = 0.45f;
            l.kneeFlex = 0.12f + 0.3f * variant * sd;
            l.ankleFlex = -0.45f;
            // arms on the ground beside the body
            a.dir = normalize(vec3(sx * (0.45f + 0.3f * variant * sd), -0.1f, -0.85f));
            a.pole = normalize(vec3(sx * 0.3f, -1.f, 0.f));
            a.elbow = 0.25f + 0.35f * sd * variant;
            a.twist = -0.9f;
        } else {
            // legs flat, tops of the feet on the ground
            l.hipFlex = 0.04f;
            l.hipAbd = 0.12f + 0.05f * sd;
            l.hipTwist = 0.15f;
            l.kneeFlex = 0.03f + (sd ? 0.08f * variant : 0.f);
            l.ankleFlex = -1.4f;
            bool up = (sd == 1) == (variant > 0.5f);
            if (up) {
                // arm up beside the head, forearm on the ground
                a.dir = normalize(vec3(sx * 0.75f, 0.12f, 0.6f));
                a.pole = normalize(vec3(sx * 0.6f, 0.2f, -0.8f));
                a.elbow = 1.5f;
                a.twist = 0.4f;
            } else {
                a.dir = normalize(vec3(sx * 0.3f, -0.02f, -0.95f));
                a.pole = normalize(vec3(sx * 0.3f, -1.f, 0.f));
                a.elbow = 0.15f;
                a.twist = 0.9f;
                // the fingers lie along the ground instead of pointing into it (the same twist turns the two palms
                // differently: the left one flexes less)
                a.wristFlex = sd ? -0.3f : -0.1f;
            }
        }
    }
}

// Sitting with the hip joints at (0, 0, hipZ), feet at forward distance feetY on a floor at floorZ.
static void seatedPose(const AuthorCtx& A, Rig& r, float hipZ, float feetY, float floorZ, float recline) {
    standPose(A, r);
    const float s = A.D.s;
    r.pelvisPitch = -0.25f - recline * 0.5f;     // posterior pelvic tilt when sitting
    r.spinePitch = 0.22f - recline * 0.5f;
    r.headPitch = 0.05f + recline * 0.6f;
    placeHips(A, r, vec3(0, 0, hipZ));
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        setFootFlat(A, r.leg[sd], vec3(A.hip[sd].x + sx * 0.05f * s, feetY, floorZ), -sx * 0.12f);
        r.leg[sd].knee = normalize(vec3(sx * 0.15f, 0.35f, 1.f));
    }
}

// ------------------------------------------------------------------------------------------------
// Individual clips

static void clipIdle(const AuthorCtx& A, float t, float dur, Rig& r, bool look) {
    standPose(A, r);
    const float s = A.D.s;
    float w = sinf(kTwoPi * t / dur);                 // weight shift left/right
    float br = sinf(kTwoPi * t / (dur * 0.5f));      // breathing
    float fem = A.fem;
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
        armFK(a, sd, 0.03f + 0.025f * sinf(kTwoPi * t / dur + sd), 0.1f + 0.02f * (1.f - fem), 0.2f + 0.05f * sd, 0.15f, 0.38f);
        a.clavUp = 0.02f * br;
        float unweighted = Saturate(sx * w);
        r.leg[sd].ankle = r.leg[sd].ankle + vec3(0, 0.02f * unweighted * s, 0.f);
        r.leg[sd].yaw += -sx * 0.1f * unweighted;
    }
    if (look) {
        float u = t / dur;
        float yaw = 0.9f * sstep(0.12f, 0.22f, u) * (1.f - sstep(0.35f, 0.45f, u)) - 0.8f * sstep(0.55f, 0.65f, u) * (1.f - sstep(0.8f, 0.9f, u));
        r.headYaw = yaw * 0.6f;
        r.neckYaw = yaw * 0.35f;
        r.spineYaw = yaw * 0.12f;
        r.eyes = vec2(yaw * 0.25f, 0.f);
        r.headPitch += -0.05f * sstep(0.55f, 0.65f, u) * (1.f - sstep(0.8f, 0.9f, u));
    }
}

// Gait parameters of a locomotion clip (the public gaits, the easy jog and the walk styles); false for other clips.
static bool gaitParams(int id, GaitP& g) {
    if (id >= IC_GAIT_FIRST && id <= IC_GAIT_LAST) {
        int k = id - IC_GAIT_FIRST;
        walkGait(k / kGaitBands, k % kGaitBands, g);
        return true;
    }
    g = GaitP();
    const ClipInfo& ci = infoOf(id);
    g.T = ci.duration;
    g.speed = ci.speed;
    g.shift = 0.13f;
    switch (id) {
        case CLIP_WALK:
            walkGait(GS_NEUTRAL, 1, g);
            g.T = ci.duration;
            break;
        case IC_JOG_SLOW:
            g.shift = 0.17f;
            g.duty = 0.42f; g.lift = 0.09f; g.kick = 0.06f; g.bob = 0.028f; g.drop = 0.03f; g.sway = 0.014f; g.yawA = 0.09f; g.rollA = 0.045f;
            g.lean = 0.09f; g.chestYaw = 0.14f; g.armSwing = 0.45f; g.elbow = 1.25f; g.elbowSwing = 0.12f; g.fist = 0.65f; g.footSpread = 0.08f;
            g.strikePitch = 0.18f; g.toeOffPitch = -0.55f; g.run = true; g.tilt = 0.1f;
            break;
        case CLIP_JOG:
            g.shift = 0.2f;
            g.duty = 0.38f; g.lift = 0.1f; g.kick = 0.1f; g.bob = 0.03f; g.drop = 0.035f; g.sway = 0.012f; g.yawA = 0.1f; g.rollA = 0.04f;
            g.lean = 0.12f; g.chestYaw = 0.16f; g.armSwing = 0.55f; g.elbow = 1.35f; g.elbowSwing = 0.15f; g.fist = 0.75f; g.footSpread = 0.07f;
            g.strikePitch = 0.15f; g.toeOffPitch = -0.6f; g.run = true; g.tilt = 0.14f;
            break;
        case CLIP_RUN:
            g.shift = 0.24f;
            g.duty = 0.28f; g.lift = 0.13f; g.kick = 0.2f; g.bob = 0.035f; g.drop = 0.045f; g.sway = 0.01f; g.yawA = 0.12f; g.rollA = 0.04f;
            g.lean = 0.2f; g.chestYaw = 0.2f; g.armSwing = 0.8f; g.elbow = 1.45f; g.elbowSwing = 0.2f; g.fist = 0.85f; g.footSpread = 0.06f;
            g.strikePitch = 0.1f; g.toeOffPitch = -0.7f; g.run = true; g.tilt = 0.22f;
            break;
        case CLIP_SPRINT:
            g.shift = 0.25f;
            g.duty = 0.22f; g.lift = 0.16f; g.kick = 0.3f; g.bob = 0.035f; g.drop = 0.05f; g.sway = 0.008f; g.yawA = 0.13f; g.rollA = 0.03f;
            g.lean = 0.3f; g.chestYaw = 0.22f; g.armSwing = 1.1f; g.elbow = 1.5f; g.elbowSwing = 0.25f; g.fist = 0.9f; g.footSpread = 0.05f;
            g.strikePitch = 0.05f; g.toeOffPitch = -0.8f; g.run = true; g.tilt = 0.26f;
            break;
        case CLIP_FLEE:
            g.shift = 0.24f;
            g.duty = 0.3f; g.lift = 0.12f; g.kick = 0.18f; g.bob = 0.035f; g.drop = 0.045f; g.lean = 0.12f; g.chestYaw = 0.25f; g.armSwing = 1.0f;
            g.elbow = 0.9f; g.elbowSwing = 0.5f; g.fist = 0.3f; g.footSpread = 0.07f; g.strikePitch = 0.1f; g.toeOffPitch = -0.7f; g.run = true;
            g.armAbd = 0.35f;
            break;
        case CLIP_WALK_BACK:
            // shorter, quicker steps than forwards, on softer knees (the stance foot still reaches the ground at both
            // ends of its stance)
            g.shift = 0.02f;
            g.dir = vec2(0, -1); g.duty = 0.65f; g.lift = 0.05f; g.bob = 0.025f; g.armSwing = 0.18f; g.lean = 0.08f; g.yawA = 0.05f;
            g.drop = 0.03f;
            break;
        case CLIP_STRAFE_L: case CLIP_STRAFE_R:
            g.shift = 0.f;
            g.dir = vec2(id == CLIP_STRAFE_L ? -1.f : 1.f, 0.f); g.duty = 0.58f; g.lift = 0.06f; g.bob = 0.02f; g.sway = 0.0f;
            g.armSwing = 0.12f; g.footSpread = 0.13f; g.yawA = 0.03f; g.rollA = 0.03f;
            break;
        case CLIP_CROUCH_WALK:
            g.shift = 0.02f;
            g.duty = 0.66f; g.lift = 0.06f; g.bob = 0.02f; g.drop = 0.36f; g.sway = 0.03f; g.lean = 0.45f; g.armSwing = 0.15f; g.elbow = 0.9f;
            g.footSpread = 0.13f; g.strikePitch = 0.15f; g.toeOffPitch = -0.35f; g.headBob = 1.4f;
            break;
        default: return false;
    }
    return true;
}

// A walk's pelvis height over the cycle: where the stance legs hold it (gaitPose), taken smoothly below that - the
// lowest height within reach a little either side, averaged - so the body sinks into each double support ahead of the
// heel strike instead of dropping onto the leading leg at it, and every planted foot still reaches the ground.
static void walkPelvis(const AuthorCtx& A, const GaitP& g, float* tab) {
    const int N = kPelvisTab, R = 3;   // averaging over two passes of +-R steps, so the lowest is taken over +-2R
    float raw[kPelvisTab], lo[kPelvisTab], tmp[kPelvisTab];
    GaitP g0 = g;
    g0.zTab = nullptr;
    for (int i = 0; i < N; i++) {
        Rig r;
        gaitPose(A, g0, (float)i / N, r);
        raw[i] = r.pelvis.z;
    }
    for (int i = 0; i < N; i++) {
        float m = raw[i];
        for (int k = -2 * R; k <= 2 * R; k++) m = Min(m, raw[(i + k + N) % N]);
        lo[i] = m;
    }
    for (int pass = 0; pass < 2; pass++) {
        const float* src = pass ? tmp : lo;
        float* dst = pass ? tab : tmp;
        for (int i = 0; i < N; i++) {
            float sum = 0.f;
            for (int k = -R; k <= R; k++) sum += src[(i + k + N) % N];
            dst[i] = sum / (2 * R + 1);
        }
    }
}

static void clipLocomotion(const AuthorCtx& A, int c, float t, Rig& r) {
    GaitP g;
    gaitParams(c, g);
    float phase = t / infoOf(c).duration;
    // (the clips bake one after another, frame by frame: a walk's table is made at its first frame)
    static const AuthorCtx* tabA = nullptr;
    static int tabC = -1;
    static float tab[kPelvisTab];
    if (g.dir.y > 0.5f && !g.run) {
        if (tabA != &A || tabC != c) {
            walkPelvis(A, g, tab);
            tabA = &A;
            tabC = c;
        }
        g.zTab = tab;
    }
    gaitPose(A, g, phase, r);
    if (c == CLIP_CROUCH_WALK) {
        for (int sd = 0; sd < 2; sd++) r.leg[sd].knee = normalize(vec3((sd ? 1.f : -1.f) * 0.35f, 1.f, 0.f));
        r.headPitch -= 0.35f;
    }
    if (c == CLIP_FLEE) {
        // panic: glance back over the shoulder once per cycle, arms raised
        float look = sstep(0.3f, 0.9f, sinf(kTwoPi * phase));
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
        vec3 knee = bonePos(A, r, sd ? B_CALF_R : B_CALF_L);
        armIK(r.arm[sd], knee + vec3((sd ? 1.f : -1.f) * 0.02f, 0.03f, 0.07f) * A.D.s, vec3((sd ? 1.f : -1.f), -0.5f, -0.5f), 0.5f);
        r.arm[sd].twist = 0.6f;
    }
}

static void clipJump(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    if (c == CLIP_JUMP_START) {
        // the game launches immediately: push-off extension, then the airborne tuck
        Rig a, push, air;
        standPose(A, a);
        a.pelvis = vec3(0, 0, -0.05f * s);
        a.spinePitch = 0.15f;
        for (int sd = 0; sd < 2; sd++) armFK(a.arm[sd], sd, -0.4f, 0.2f, 0.3f, 0.2f, 0.5f);
        standPose(A, push);
        push.pelvis = vec3(0, 0.02f * s, 0.08f * s);
        push.spinePitch = 0.02f;
        push.headPitch = -0.08f;   // chin up with the drive
        for (int sd = 0; sd < 2; sd++) {
            // the arms drive forward and up past the shoulders, elbows bent (not held out straight)
            armFK(push.arm[sd], sd, 1.55f, 0.28f, 0.95f, 0.25f, 0.45f);
            setFootToes(A, push.leg[sd], A.ankle[sd].x, A.ankle[sd].y + A.ballFwd, -0.75f, push.leg[sd].yaw);
            push.leg[sd].ankle.z += 0.02f * s;
        }
        standPose(A, air);
        air.pelvis = vec3(0, 0, 0.06f * s);
        air.spinePitch = 0.08f;
        air.headPitch = -0.05f;
        for (int sd = 0; sd < 2; sd++) {
            LegCtl& l = air.leg[sd];
            l.ik = false;
            l.hipFlex = 0.45f + 0.25f * sd;
            l.kneeFlex = 0.7f + 0.3f * sd;
            l.ankleFlex = -0.35f;
            l.hipAbd = 0.08f;
            armFK(air.arm[sd], sd, 0.6f, 0.45f, 0.6f, 0.2f, 0.4f);
        }
        std::vector<Key> k = {{0.f, a}, {0.1f, push}, {0.35f, air}};
        sampleKeys(A, k, t, false, 0.35f, r);
    } else if (c == CLIP_FALL) {
        // airborne: knees drawn up a little and cycling slowly, the arms up in front and out for balance with the
        // elbows bent, the trunk a little forward, looking down at where the feet will land
        standPose(A, r);
        float w = sinf(kTwoPi * t), w2 = sinf(kTwoPi * t * 2.f + 1.f);
        r.pelvis = vec3(0, 0, 0.05f * s);
        r.spinePitch = 0.12f + 0.03f * w2;
        r.headPitch = 0.12f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            LegCtl& l = r.leg[sd];
            l.ik = false;
            l.hipFlex = 0.45f + 0.2f * sx * w;
            l.kneeFlex = 0.8f - 0.2f * sx * w;
            l.ankleFlex = -0.3f;
            l.hipAbd = 0.1f;
            armFK(r.arm[sd], sd, 0.55f + 0.2f * sx * w, 0.62f + 0.12f * w2, 0.9f + 0.15f * w2, 0.3f, 0.25f);
        }
    } else {   // LAND: absorb the impact and recover
        Rig a, b, m, e;
        standPose(A, a);
        a.pelvis = vec3(0, 0, 0.02f * s);
        for (int sd = 0; sd < 2; sd++) armFK(a.arm[sd], sd, 0.3f, 0.6f, 0.5f, 0.2f, 0.3f);
        crouchPose(A, b, 0.6f);
        b.spinePitch = 0.4f;
        for (int sd = 0; sd < 2; sd++) armFK(b.arm[sd], sd, 0.5f, 0.35f, 0.6f, 0.2f, 0.5f);
        crouchPose(A, m, 0.25f);
        standPose(A, e);
        std::vector<Key> k = {{0.f, a}, {0.1f, b}, {0.3f, m}, {0.55f, e}};
        sampleKeys(A, k, t, false, 0.55f, r);
    }
}

// Weapon aim poses (the animator adds the aim pitch on the spine).
static void aimPistolPose(const AuthorCtx& A, Rig& r) {
    standPose(A, r);
    const float s = A.D.s;
    setFootFlat(A, r.leg[0], vec3(A.ankle[0].x - 0.03f * s, 0.1f * s, 0.f), 0.12f);
    setFootFlat(A, r.leg[1], vec3(A.ankle[1].x + 0.03f * s, -0.07f * s, 0.f), -0.3f);
    r.pelvis = vec3(0, 0, -0.03f * s);
    r.pelvisYaw = -0.08f;
    r.spinePitch = 0.1f;
    r.spineYaw = 0.08f;
    r.headPitch = 0.06f;
    r.arm[0].clavFwd = r.arm[1].clavFwd = 0.1f;
    vec3 shL = bonePos(A, r, B_UPPERARM_L), shR = bonePos(A, r, B_UPPERARM_R);
    vec3 grip(0.03f * s, shR.y + 0.5f * s, shR.z - 0.05f * s);
    armIK(r.arm[1], grip, vec3(1, -0.3f, -1), 0.9f);
    armIK(r.arm[0], grip + vec3(-0.045f, -0.005f, -0.025f) * s, vec3(-1, -0.3f, -1), 0.8f);
    vec3 dR = normalize(grip - shR), dL = normalize(grip - shL);
    r.arm[1].orient = true;
    r.arm[1].handRot = handFrame(A, 1, dR + vec3(0, 0, -0.15f), vec3(-1, 0.1f, -0.1f));
    r.arm[0].orient = true;
    r.arm[0].handRot = handFrame(A, 0, dL + vec3(0.2f, 0, -0.2f), vec3(1, 0, 0.3f));
}

static void aimRiflePose(const AuthorCtx& A, Rig& r) {
    standPose(A, r);
    const float s = A.D.s;
    setFootFlat(A, r.leg[0], vec3(A.ankle[0].x - 0.03f * s, 0.14f * s, 0.f), 0.25f);
    setFootFlat(A, r.leg[1], vec3(A.ankle[1].x + 0.03f * s, -0.1f * s, 0.f), -0.45f);
    r.pelvis = vec3(0, 0, -0.04f * s);
    r.pelvisYaw = -0.3f;
    r.spineYaw = 0.22f;
    r.spinePitch = 0.1f;
    r.headYaw = 0.08f;
    r.headRoll = 0.1f;
    r.headPitch = 0.1f;
    r.arm[1].clavFwd = 0.1f;
    r.arm[0].clavFwd = 0.14f;
    vec3 shR = bonePos(A, r, B_UPPERARM_R);
    vec3 grip(shR.x - 0.1f * s, shR.y + 0.24f * s, shR.z - 0.12f * s);
    vec3 fore(0.0f * s, shR.y + 0.52f * s, shR.z - 0.1f * s);
    armIK(r.arm[1], grip, vec3(1, -0.3f, -0.5f), 0.85f);
    armIK(r.arm[0], fore, vec3(-0.5f, -0.2f, -1.f), 0.75f);
    r.arm[1].orient = true;
    r.arm[1].handRot = handFrame(A, 1, vec3(-0.1f, 0.55f, -0.8f), vec3(-1, 0.1f, 0.f));
    r.arm[0].orient = true;
    r.arm[0].handRot = handFrame(A, 0, vec3(0.4f, 0.9f, 0.05f), vec3(0.3f, 0.f, 1.f));
}

static void clipCombat(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    Rig g;
    guardPose(A, g);
    vec3 shL = bonePos(A, g, B_UPPERARM_L), shR = bonePos(A, g, B_UPPERARM_R);
    switch (c) {
        case CLIP_PUNCH_L: {   // jab
            Rig wind = g, hit = g;
            wind.spineYaw = 0.2f;
            wind.arm[0].target = wind.arm[0].target + vec3(0, -0.04f, 0) * s;
            hit.spineYaw = -0.25f;
            hit.pelvisYaw = -0.35f;
            hit.arm[0].target = vec3(shL.x + 0.2f * s, shL.y + 0.6f * s, shL.z + 0.02f * s);
            hit.arm[0].twist = 1.5f;
            hit.arm[0].handRot = handFrame(A, 0, vec3(0.3f, 1.f, 0.02f), vec3(0.f, 0.f, -1.f));   // palm down at full reach
            hit.leg[0].ankle = hit.leg[0].ankle + vec3(0, 0.05f * s, 0);
            hit.headYaw = 0.2f;
            std::vector<Key> k = {{0.f, g}, {0.08f, wind}, {0.17f, hit}, {0.23f, hit}, {0.5f, g}};
            sampleKeys(A, k, t, false, 0.5f, r);
            break;
        }
        case CLIP_PUNCH_R: {   // cross with hip rotation and heel pivot
            Rig wind = g, hit = g;
            wind.spineYaw = 0.3f;
            wind.pelvisYaw = -0.1f;
            wind.arm[1].target = wind.arm[1].target + vec3(0.02f, -0.05f, 0.f) * s;
            hit.spineYaw = -0.45f;
            hit.pelvisYaw = -0.5f;
            hit.pelvis = hit.pelvis + vec3(0, 0.04f * s, 0);
            hit.arm[1].target = vec3(shR.x - 0.3f * s, shR.y + 0.62f * s, shR.z + 0.0f * s);
            hit.arm[1].twist = 1.6f;
            hit.arm[1].handRot = handFrame(A, 1, vec3(-0.45f, 1.f, 0.f), vec3(0.f, 0.f, -1.f));
            hit.arm[0].target = hit.arm[0].target + vec3(0, -0.06f, -0.03f) * s;
            setFootToes(A, hit.leg[1], g.leg[1].ankle.x, g.leg[1].ankle.y + A.ballFwd, -0.5f, -0.9f);
            hit.headYaw = 0.35f;
            std::vector<Key> k = {{0.f, g}, {0.12f, wind}, {0.23f, hit}, {0.3f, hit}, {0.62f, g}};
            sampleKeys(A, k, t, false, 0.62f, r);
            break;
        }
        case CLIP_KICK: {   // front kick with the right leg
            Rig chamber = g, ext = g, back = g;
            vec3 hip = A.hip[1];
            chamber.leg[1].ik = true;
            chamber.leg[1].ankle = vec3(hip.x, hip.y + 0.25f * s, hip.z - 0.4f * s);
            chamber.leg[1].pitch = -0.6f;
            chamber.leg[1].knee = vec3(0, 0.3f, 1.f);
            chamber.spinePitch = -0.08f;
            chamber.pelvisPitch = -0.15f;
            chamber.pelvis = chamber.pelvis + vec3(0, -0.03f * s, 0.02f * s);
            ext = chamber;
            ext.leg[1].ankle = vec3(hip.x - 0.02f * s, hip.y + 0.72f * s, hip.z - 0.12f * s);
            ext.leg[1].pitch = -0.9f;
            ext.leg[1].knee = vec3(0, 0.2f, 1.f);
            ext.pelvis = ext.pelvis + vec3(0, -0.05f * s, 0);
            ext.spinePitch = -0.25f;
            ext.arm[1].target = ext.arm[1].target + vec3(0.08f, -0.1f, -0.05f) * s;
            back = chamber;
            std::vector<Key> k = {{0.f, g}, {0.2f, chamber}, {0.35f, ext}, {0.45f, ext}, {0.62f, back}, {0.9f, g}};
            sampleKeys(A, k, t, false, 0.9f, r);
            break;
        }
        case CLIP_BLOCK: {   // guard up covering the face
            r = g;
            r.headPitch = 0.25f;
            r.spinePitch = 0.2f;
            vec3 hp = bonePos(A, r, B_HEAD);
            // forearms up in front of the face, fists at the forehead, palms towards the face
            fistArm(A, r, 0, hp + vec3(-0.075f, 0.17f, -0.08f) * s, vec3(-0.3f, 0.1f, -1.f), vec3(0.3f, -1.f, 0.f));
            fistArm(A, r, 1, hp + vec3(0.075f, 0.16f, -0.09f) * s, vec3(0.3f, 0.1f, -1.f), vec3(-0.3f, -1.f, 0.f));
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
        float dir = c == CLIP_HIT_FRONT ? -1.f : 1.f;   // front hits push the torso backwards
        Rig h = a, st = a;
        h.spinePitch = 0.3f * dir;
        h.neckPitch = 0.2f * dir;
        h.headPitch = 0.3f * dir;
        h.pelvis = vec3(0, 0.05f * s * dir, -0.04f * s);
        for (int sd = 0; sd < 2; sd++) armFK(h.arm[sd], sd, dir > 0.f ? -0.4f : 0.6f, 0.35f, 0.7f, 0.2f, 0.6f);
        // catch step and return
        setFootFlat(A, st.leg[0], vec3(A.ankle[0].x, 0.14f * s * dir, 0.f), 0.1f);
        st.pelvis = vec3(0, 0.07f * s * dir, -0.05f * s);
        st.spinePitch = 0.1f * dir;
        Rig lift = st;
        lift.leg[0].ankle = lift.leg[0].ankle + vec3(0, -0.07f * s * dir, 0.05f * s);
        std::vector<Key> k = {{0.f, a}, {0.07f, h}, {0.24f, st}, {0.38f, lift}, {0.55f, a}};
        sampleKeys(A, k, t, false, 0.55f, r);
    } else {
        // STAGGER: stumble two steps backwards with flailing arms, then step back to the start
        Rig k1 = a, k2, k3, k4, k5, k6;
        k1.spinePitch = -0.3f;
        k1.headPitch = -0.3f;
        k1.pelvis = vec3(0, -0.06f * s, -0.03f * s);
        for (int sd = 0; sd < 2; sd++) armFK(k1.arm[sd], sd, 0.8f, 0.7f, 0.5f, 0.2f, 0.3f);
        k1.leg[1].ankle = k1.leg[1].ankle + vec3(0, -0.1f * s, 0.07f * s);
        k2 = k1;
        setFootFlat(A, k2.leg[1], vec3(A.ankle[1].x, -0.24f * s, 0.f), -0.2f);
        k2.pelvis = vec3(0.02f * s, -0.17f * s, -0.05f * s);
        k3 = k2;
        k3.leg[0].ankle = vec3(A.ankle[0].x, -0.2f * s, A.footH + 0.07f * s);
        k3.pelvis = vec3(-0.01f * s, -0.27f * s, -0.04f * s);
        for (int sd = 0; sd < 2; sd++) armFK(k3.arm[sd], sd, 0.3f, 1.1f, 0.4f, 0.2f, 0.3f);
        k4 = k3;
        setFootFlat(A, k4.leg[0], vec3(A.ankle[0].x, -0.36f * s, 0.f), 0.15f);
        k4.pelvis = vec3(0, -0.3f * s, -0.06f * s);
        k4.spinePitch = 0.1f;
        k5 = k4;
        k5.leg[1].ankle = vec3(A.ankle[1].x, -0.12f * s, A.footH + 0.06f * s);
        k5.pelvis = vec3(0, -0.15f * s, -0.03f * s);
        for (int sd = 0; sd < 2; sd++) armFK(k5.arm[sd], sd, 0.2f, 0.4f, 0.4f, 0.2f, 0.4f);
        k6 = k5;
        setFootFlat(A, k6.leg[1], vec3(A.ankle[1].x, 0.f, 0.f), -0.1f);
        k6.leg[0].ankle = vec3(A.ankle[0].x, -0.15f * s, A.footH + 0.05f * s);
        k6.pelvis = vec3(0, -0.06f * s, -0.02f * s);
        std::vector<Key> k = {{0.f, a}, {0.12f, k1}, {0.3f, k2}, {0.48f, k3}, {0.65f, k4}, {0.85f, k5}, {1.02f, k6}, {1.3f, a}};
        sampleKeys(A, k, t, false, 1.3f, r);
    }
}

static void clipDeath(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    bool front = c == CLIP_DEATH_FRONT;   // hit from the front: falls on the back
    Rig a, hit, buckle, impact, down, end;
    standPose(A, a);
    hit = a;
    float dir = front ? -1.f : 1.f;
    hit.spinePitch = 0.35f * dir;
    hit.headPitch = 0.4f * dir;
    hit.pelvis = vec3(0, -0.06f * s * dir, -0.04f * s);
    for (int sd = 0; sd < 2; sd++) armFK(hit.arm[sd], sd, front ? 0.9f : -0.4f, 0.5f, 0.6f, 0.3f, 0.4f);
    buckle = hit;
    if (front) {
        // knees give, falling backwards onto the buttocks
        buckle.pelvis = vec3(0, -0.12f * s, -0.4f * s);
        buckle.pelvisPitch = -0.35f;
        buckle.spinePitch = -0.2f;
        buckle.headPitch = 0.2f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            setFootFlat(A, buckle.leg[sd], vec3(A.ankle[sd].x + sx * 0.03f * s, 0.05f * s, 0.f), -sx * 0.2f);
            buckle.leg[sd].knee = normalize(vec3(sx * 0.3f, 1.f, 0.3f));
            armFK(buckle.arm[sd], sd, 0.5f, 0.7f, 0.5f, 0.3f, 0.3f);
        }
        impact = Rig();
        impact.pelvisPitch = -1.0f;
        placeHips(A, impact, vec3(0, -0.2f * s, 0.1f * s));
        impact.spinePitch = -0.2f;
        impact.headPitch = 0.35f;
        for (int sd = 0; sd < 2; sd++) {
            LegCtl& l = impact.leg[sd];
            l.ik = false;
            l.hipFlex = 0.9f;
            l.kneeFlex = 1.0f + 0.2f * sd;
            l.hipAbd = 0.15f;
            l.ankleFlex = -0.2f;
            ArmCtl& am = impact.arm[sd];
            am.ik = false;
            am.dir = normalize(vec3((sd ? 1.f : -1.f) * 0.7f, 0.2f, -0.5f));
            am.pole = normalize(vec3((sd ? 1.f : -1.f) * 0.3f, -1.f, -0.2f));
            am.elbow = 0.4f;
        }
        lyingPose(A, end, true, 0.3f, -0.32f * s);
    } else {
        // knees drop to the ground, torso pitches forward, hands reach out to break the fall
        buckle.pelvisPitch = 0.3f;
        buckle.spinePitch = 0.3f;
        placeHips(A, buckle, vec3(0, 0.08f * s, 0.46f * s));
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            LegCtl& l = buckle.leg[sd];
            l.ik = true;
            l.footQ = false;
            l.ankle = vec3(A.hip[sd].x + sx * 0.03f * s, -0.3f * s, 0.075f * s);
            l.pitch = -1.25f;
            l.yaw = -sx * 0.1f;
            l.toe = 1.1f;
            l.knee = normalize(vec3(sx * 0.15f, 0.6f, -1.f));
            armFK(buckle.arm[sd], sd, 0.6f, 0.3f, 0.4f, 0.3f, 0.3f);
        }
        impact = buckle;
        impact.pelvisPitch = 0.9f;
        impact.spinePitch = 0.35f;
        impact.headPitch = -0.35f;
        placeHips(A, impact, vec3(0, 0.2f * s, 0.34f * s));
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            impact.leg[sd].ankle = vec3(A.hip[sd].x + sx * 0.03f * s, -0.22f * s, 0.07f * s);
            impact.leg[sd].pitch = -1.4f;
            impact.leg[sd].toe = 0.4f;
            armIK(impact.arm[sd], vec3(sx * 0.3f * s, 0.85f * s, 0.05f * s), vec3(sx * 0.6f, -1.f, 0.2f), 0.3f);
        }
        lyingPose(A, end, false, 0.7f, 0.36f * s);
    }
    down = end;
    down.pelvis.z += 0.02f * s;
    down.spinePitch += front ? 0.08f : -0.05f;
    down.headPitch += front ? 0.1f : -0.08f;
    std::vector<Key> k = {{0.f, a}, {0.1f, hit}, {0.42f, buckle}, {0.72f, impact}, {1.0f, down}, {1.6f, end}};
    sampleKeys(A, k, t, false, 1.6f, r, 0.3f);
}

// Kneeling on the right knee with the left foot planted in front.
static void kneelPose(const AuthorCtx& A, Rig& r, float frontFootY, float lean) {
    standPose(A, r);
    const float s = A.D.s;
    r.pelvisPitch = 0.15f + lean * 0.3f;
    r.spinePitch = lean;
    r.headPitch = -lean * 0.5f;
    placeHips(A, r, vec3(0, -0.05f * s, A.legLen * 0.52f + 0.02f * s));
    setFootFlat(A, r.leg[0], vec3(A.ankle[0].x - 0.02f * s, frontFootY, 0.f), 0.1f);
    r.leg[0].knee = normalize(vec3(-0.2f, 1.f, 0.4f));
    LegCtl& l = r.leg[1];
    l.ik = true;
    l.footQ = false;
    l.ankle = vec3(A.ankle[1].x + 0.02f * s, -0.38f * s, 0.075f * s);
    l.pitch = -1.25f;
    l.toe = 1.1f;
    l.yaw = -0.1f;
    l.knee = normalize(vec3(0.1f, 0.2f, -1.f));
}

static void clipGetUp(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    Rig end;
    standPose(A, end);
    if (c == CLIP_GET_UP_BACK) {
        Rig lie, sit, tuck, squat, rise;
        lyingPose(A, lie, true, 0.5f, 0.f);
        // sit up: pelvis on the ground, torso up, heels drawn in, hands behind pushing
        sit = Rig();
        sit.pelvisPitch = -0.75f;
        placeHips(A, sit, vec3(0, 0.02f * s, 0.1f * s));
        sit.spinePitch = 0.6f;
        sit.headPitch = 0.1f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            setFootFlat(A, sit.leg[sd], vec3(A.hip[sd].x + sx * 0.06f * s, 0.42f * s, 0.f), -sx * 0.25f);
            sit.leg[sd].knee = normalize(vec3(sx * 0.25f, 0.4f, 1.f));
            armFK(sit.arm[sd], sd, -0.55f, 0.35f, 0.15f, 0.2f, 0.3f);
        }
        // weight forward onto the feet, one hand on the ground
        tuck = sit;
        tuck.pelvisPitch = -0.2f;
        tuck.spinePitch = 0.9f;
        placeHips(A, tuck, vec3(0, 0.08f * s, 0.2f * s));
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            setFootFlat(A, tuck.leg[sd], vec3(A.hip[sd].x + sx * 0.08f * s, 0.26f * s, 0.f), -sx * 0.25f);
            tuck.leg[sd].knee = normalize(vec3(sx * 0.4f, 0.8f, 0.6f));
            armFK(tuck.arm[sd], sd, 0.5f, 0.25f, 0.3f, 0.2f, 0.4f);
        }
        armIK(tuck.arm[0], vec3(-0.22f * s, 0.2f * s, 0.02f * s), vec3(-1, -0.5f, 0), 0.3f);
        squat = end;
        squat.pelvisPitch = 0.35f;
        squat.spinePitch = 0.55f;
        squat.headPitch = -0.3f;
        placeHips(A, squat, vec3(0, 0.02f * s, 0.42f * s));
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            setFootFlat(A, squat.leg[sd], vec3(A.ankle[sd].x + sx * 0.05f * s, 0.12f * s, 0.f), -sx * 0.25f);
            squat.leg[sd].knee = normalize(vec3(sx * 0.35f, 1.f, 0.f));
            armFK(squat.arm[sd], sd, 0.6f, 0.3f, 0.5f, 0.2f, 0.4f);
        }
        rise = end;
        rise.pelvis = vec3(0, 0.02f * s, -0.12f * s);
        rise.spinePitch = 0.25f;
        setFootFlat(A, rise.leg[1], vec3(A.ankle[1].x, 0.06f * s, 0.f), -0.15f);
        std::vector<Key> k = {{0.f, lie}, {0.4f, sit}, {0.72f, tuck}, {1.0f, squat}, {1.3f, rise}, {1.6f, end}};
        sampleKeys(A, k, t, false, 1.6f, r);
    } else {
        Rig lie, push, fours, kneel, rise;
        lyingPose(A, lie, false, 0.5f, 0.f);
        // push up: hands under the shoulders, chest lifts
        push = lie;
        push.pelvisPitch = kHalfPi * 0.8f;
        push.headYaw = 0.f;
        push.neckYaw = 0.f;
        push.headPitch = -0.4f;
        placeHips(A, push, vec3(0, -0.02f * s, 0.12f * s));
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            armIK(push.arm[sd], vec3(sx * 0.24f * s, 0.42f * s, 0.03f * s), vec3(sx * 0.5f, -0.5f, 0.3f), 0.2f);
            push.arm[sd].orient = true;
            push.arm[sd].handRot = handFrame(A, sd, vec3(sx * 0.1f, 1.f, 0.f), vec3(0, 0, -1));
        }
        // hands and knees
        fours = Rig();
        fours.pelvisPitch = 1.35f;
        fours.spinePitch = 0.15f;
        fours.headPitch = -0.7f;
        placeHips(A, fours, vec3(0, -0.02f * s, A.D.thigh + 0.07f * s));
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            LegCtl& l = fours.leg[sd];
            l.ik = true;
            l.ankle = vec3(A.hip[sd].x + sx * 0.02f * s, -0.4f * s, 0.075f * s);
            l.pitch = -1.25f;
            l.toe = 1.1f;
            l.knee = normalize(vec3(sx * 0.1f, 0.2f, -1.f));
            armIK(fours.arm[sd], vec3(sx * 0.22f * s, 0.5f * s, 0.02f * s), vec3(sx * 0.3f, -1.f, 0.f), 0.2f);
            fours.arm[sd].orient = true;
            fours.arm[sd].handRot = handFrame(A, sd, vec3(sx * 0.1f, 1.f, 0.f), vec3(0, 0, -1));
        }
        kneelPose(A, kneel, 0.2f * s, 0.55f);
        for (int sd = 0; sd < 2; sd++) armFK(kneel.arm[sd], sd, 0.5f, 0.2f, 0.4f, 0.2f, 0.5f);
        armIK(kneel.arm[0], bonePos(A, kneel, B_CALF_L) + vec3(0, 0.05f, 0.06f) * s, vec3(-1, -0.5f, 0), 0.5f);
        rise = end;
        rise.pelvis = vec3(0, 0.02f * s, -0.14f * s);
        rise.spinePitch = 0.3f;
        setFootFlat(A, rise.leg[0], vec3(A.ankle[0].x, 0.08f * s, 0.f), 0.1f);
        rise.leg[1].ankle = vec3(A.ankle[1].x, -0.1f * s, A.footH + 0.06f * s);
        std::vector<Key> k = {{0.f, lie}, {0.35f, push}, {0.75f, fours}, {1.1f, kneel}, {1.42f, rise}, {1.7f, end}};
        sampleKeys(A, k, t, false, 1.7f, r);
    }
}

// Driving: hips 0.5 m above the origin, hands on a wheel rim centred 0.5 m ahead / 0.4 m above the hips.
static void drivePose(const AuthorCtx& A, Rig& r, float t) {
    const float s = A.D.s;
    seatedPose(A, r, kSeatHipZ, 0.78f * s, kSeatHipZ - 0.3f, -0.05f);
    // lean in towards the wheel (the rim is at about arm's length from an upright seated torso)
    r.spinePitch += 0.2f;
    r.headPitch -= 0.14f;
    placeHips(A, r, vec3(0, 0, kSeatHipZ));
    for (int sd = 0; sd < 2; sd++) {
        r.arm[sd].clavFwd = 0.22f;
        r.leg[sd].pitch = 0.4f;
        r.leg[sd].ankle.z += 0.02f * s;
        r.leg[sd].knee = normalize(vec3((sd ? 1.f : -1.f) * 0.2f, 0.4f, 1.f));
    }
    vec3 wc(0.f, 0.5f * s, kSeatHipZ + 0.4f * s);
    vec3 wx(1, 0, 0), wy = normalize(vec3(0, 0.41f, 0.91f));   // wheel plane axes (tilted towards the driver)
    float steer = 0.05f * sinf(kTwoPi * t / 3.f);
    float R = 0.185f * s;
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        float ang = sx * 1.1f - steer;   // 10 and 2 o'clock
        vec3 rim = wc + wx * (sinf(ang) * R) + wy * (cosf(ang) * R);
        vec3 radial = normalize(rim - wc);
        // wrist slightly behind/outside the rim so the fingers wrap it
        vec3 hp = rim + radial * 0.03f * s - vec3(0, 0.035f, 0.01f) * s;
        armIK(r.arm[sd], hp, vec3(sx * 1.f, -0.4f, -0.9f), 0.85f);
        r.arm[sd].orient = true;
        vec3 tang = normalize(cross(radial, vec3(0, -0.91f, 0.41f)));
        r.arm[sd].handRot = handFrame(A, sd, radial * 0.35f + vec3(0, 0.6f, 0.1f) - tang * (0.3f * sx), vec3(-sx * 0.35f, 0.8f, -0.4f));
    }
    r.headPitch = 0.02f + 0.02f * sinf(kTwoPi * t / 3.f * 2.f);
    r.headYaw = 0.1f * sinf(kTwoPi * t / 3.f);
}

static void passengerPose(const AuthorCtx& A, Rig& r, float t, float dur) {
    const float s = A.D.s;
    seatedPose(A, r, kSeatHipZ, 0.62f * s, kSeatHipZ - 0.3f, 0.2f);
    float w = sinf(kTwoPi * t / dur);
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        vec3 knee = bonePos(A, r, sd ? B_CALF_R : B_CALF_L);
        vec3 th = lerp(bonePos(A, r, sd ? B_THIGH_R : B_THIGH_L), knee, 0.55f) + vec3(sx * 0.01f, 0.f, 0.075f * s);
        armIK(r.arm[sd], th, vec3(sx, -0.6f, -0.2f), 0.4f);
        r.arm[sd].orient = true;
        r.arm[sd].handRot = handFrame(A, sd, vec3(-sx * 0.25f, 1.f, -0.25f), vec3(0, 0.1f, -1.f));
    }
    r.headYaw = 0.5f * sstep(0.3f, 0.5f, 0.5f + 0.5f * w) - 0.15f;
    r.headPitch = 0.05f;
}

static void bikePose(const AuthorCtx& A, Rig& r, float t) {
    const float s = A.D.s;
    standPose(A, r);
    r.pelvisPitch = 0.15f;
    r.spinePitch = 0.4f;
    r.headPitch = -0.45f;
    r.neckPitch = -0.1f;
    placeHips(A, r, vec3(0, 0, kSeatHipZ));
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        // feet on the pegs, knees against the tank
        LegCtl& l = r.leg[sd];
        l.ik = true;
        l.ankle = vec3(sx * 0.2f * s, 0.08f * s, kSeatHipZ - 0.32f * s + A.footH * 0.5f);
        l.pitch = -0.1f;
        l.yaw = -sx * 0.1f;
        l.knee = normalize(vec3(sx * 0.15f, 1.f, 0.5f));
        vec3 bar(sx * 0.3f * s, 0.52f * s, kSeatHipZ + 0.38f * s + 0.006f * sinf(kTwoPi * t / 2.f + sd));
        armIK(r.arm[sd], bar, vec3(sx * 1.f, -0.4f, -0.6f), 0.9f);
        r.arm[sd].orient = true;
        r.arm[sd].handRot = handFrame(A, sd, vec3(-sx * 0.2f, 1.f, -0.3f), vec3(-sx * 0.4f, 0.2f, -1.f));
    }
    r.spineYaw = 0.04f * sinf(kTwoPi * t / 2.f);
}

// Car entry / exit (left side authored; the right side is mirrored). See the conventions at the top.
static void clipCar(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    bool enter = c == CLIP_ENTER_CAR_L || c == CLIP_ENTER_CAR_R;
    bool right = c == CLIP_ENTER_CAR_R || c == CLIP_EXIT_CAR_R;
    std::vector<Key> k;
    if (enter) {
        // facing the car: open the door with the left hand, step in, turn left, lower into the seat
        Rig stand, reach, pull, step, turn, sit;
        standPose(A, stand);
        reach = stand;
        armIK(reach.arm[0], vec3(0.12f * s, 0.4f * s, 0.98f * s), vec3(-1, -0.5f, -1), 0.8f);
        reach.spineYaw = 0.1f;
        reach.spinePitch = 0.1f;
        pull = reach;
        pull.arm[0].target = vec3(-0.28f * s, 0.12f * s, 0.98f * s);
        pull.pelvis = vec3(0.03f * s, -0.03f * s, 0.f);
        pull.spineYaw = 0.25f;
        setFootFlat(A, pull.leg[1], vec3(A.ankle[1].x + 0.06f * s, 0.12f * s, 0.f), -0.3f);
        step = pull;
        step.pelvis = vec3(0.05f * s, 0.15f * s, -0.05f * s);
        step.pelvisYaw = 0.4f;
        step.spineYaw = 0.2f;
        step.spinePitch = 0.25f;
        setFootFlat(A, step.leg[1], vec3(0.1f * s, 0.42f * s, 0.f), 0.6f);
        armIK(step.arm[1], vec3(0.25f * s, 0.45f * s, 1.12f * s), vec3(1, -0.5f, -0.5f), 0.9f);
        turn = step;
        turn.pelvisYaw = 1.1f;
        turn.spinePitch = 0.45f;
        turn.headPitch = 0.25f;
        turn.pelvis = vec3(0, 0.35f * s, -0.22f * s);
        turn.leg[1].yaw = 1.2f;
        turn.arm[0].target = vec3(-0.35f * s, 0.25f * s, 0.95f * s);
        sit = turn;
        sit.pelvisYaw = kHalfPi;
        sit.pelvisPitch = -0.2f;
        sit.spinePitch = 0.35f;
        sit.headPitch = 0.1f;
        placeHips(A, sit, vec3(0.0f, 0.62f * s, kSeatHipZ + 0.03f));
        setFootFlat(A, sit.leg[0], vec3(-0.15f * s, 0.22f * s, 0.f), kHalfPi - 0.3f);
        sit.leg[1].ankle = vec3(-0.45f * s, 0.62f * s, kSeatHipZ - 0.1f);
        sit.leg[1].pitch = 0.2f;
        sit.leg[1].yaw = kHalfPi;
        sit.leg[1].knee = normalize(vec3(-1.f, 0.f, 0.6f));
        sit.arm[0].target = vec3(-0.45f * s, 0.3f * s, 1.0f * s);
        k = {{0.f, stand}, {0.22f, reach}, {0.45f, pull}, {0.68f, step}, {0.88f, turn}, {1.1f, sit}};
    } else {
        // start seated 0.8 m to the right (inside), swing the left leg out, stand up and step to the origin
        const float dx = 0.8f * s;
        Rig seat, swing, out, rise, stand;
        if (right) passengerPose(A, seat, 0.f, 4.f);
        else drivePose(A, seat, 0.f);
        seat.pelvis.x += dx;
        for (int sd = 0; sd < 2; sd++) {
            seat.leg[sd].ankle.x += dx;
            seat.arm[sd].target.x += dx;
        }
        swing = seat;
        swing.pelvisYaw = 0.7f;
        swing.spinePitch = 0.3f;
        swing.headYaw = 0.3f;
        placeHips(A, swing, vec3(dx - 0.05f * s, 0.f, kSeatHipZ));
        setFootFlat(A, swing.leg[0], vec3(dx - 0.5f * s, 0.2f * s, 0.f), 0.6f);
        swing.leg[0].knee = normalize(vec3(-0.6f, 0.5f, 0.6f));
        swing.leg[1].ankle = vec3(dx + 0.05f * s, 0.55f * s, kSeatHipZ - 0.28f);
        armIK(swing.arm[0], vec3(dx - 0.35f * s, 0.35f * s, 1.05f * s), vec3(-1, -0.4f, -0.6f), 0.8f);
        armIK(swing.arm[1], vec3(dx + 0.05f * s, 0.45f * s, 0.95f * s), vec3(1, -0.4f, -0.6f), 0.8f);
        out = swing;
        out.pelvisYaw = 0.5f;
        out.pelvisPitch = 0.2f;
        out.spinePitch = 0.55f;
        out.headPitch = 0.2f;
        placeHips(A, out, vec3(dx * 0.5f, 0.08f * s, 0.7f * s));
        setFootFlat(A, out.leg[1], vec3(dx * 0.5f + 0.12f * s, 0.3f * s, 0.f), 0.2f);
        out.leg[1].knee = normalize(vec3(0.3f, 1.f, 0.2f));
        out.arm[1].target = vec3(dx * 0.7f, 0.3f * s, 1.1f * s);
        rise = out;
        rise.pelvisYaw = 0.15f;
        rise.pelvisPitch = 0.05f;
        rise.spinePitch = 0.15f;
        rise.headPitch = 0.f;
        rise.headYaw = 0.f;
        rise.pelvis = vec3(dx * 0.2f, 0.03f * s, -0.04f * s);
        setFootFlat(A, rise.leg[0], vec3(A.ankle[0].x + dx * 0.1f, 0.05f * s, 0.f), 0.1f);
        rise.leg[1].ankle = vec3(A.ankle[1].x + dx * 0.25f, 0.05f * s, A.footH + 0.08f * s);
        for (int sd = 0; sd < 2; sd++) armFK(rise.arm[sd], sd, 0.15f, 0.2f, 0.4f, 0.2f, 0.4f);
        standPose(A, stand);
        k = {{0.f, seat}, {0.28f, swing}, {0.55f, out}, {0.8f, rise}, {1.0f, stand}};
    }
    sampleKeys(A, k, t, false, enter ? 1.1f : 1.0f, r);
    if (right) r = mirrorRig(r);
}

// Swimming: the water surface is kWaterZ above the origin.
static void clipSwim(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    r = Rig();
    if (c == CLIP_SWIM) {
        // front crawl: horizontal, face down, body roll with the stroke, flutter kick
        float ph = t / 1.4f;
        float s1 = sinf(kTwoPi * ph);
        r.pelvisPitch = kHalfPi * 0.93f;
        r.pelvisTwist = 0.35f * s1;
        r.pelvis = vec3(0, 0, kWaterZ - 0.1f * s) - A.pelvisBind;
        r.spineYaw = 0.25f * s1;
        r.headPitch = -0.45f;
        float breathe = Max(0.f, sinf(kTwoPi * ph - 0.4f));
        r.headYaw = -0.9f * breathe * breathe;   // breathe to the side every stroke cycle
        r.neckPitch = -0.1f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            float p = ph + (sd ? 0.5f : 0.f);
            p -= floorf(p);
            ArmCtl& a = r.arm[sd];
            a.ik = false;
            float th = kTwoPi * p;
            // chest frame when prone: +Z = forward (head), +Y = down into the water, -Y = up out of the water
            float lat = 0.17f + 0.45f * Max(0.f, -sinf(th));
            a.dir = normalize(vec3(sx * lat, sinf(th), cosf(th)));
            a.pole = normalize(vec3(sx * 0.5f, -0.3f + 0.9f * (0.5f - 0.5f * cosf(th)), 0.4f - 0.8f * sinf(th) * 0.5f));
            a.elbow = 0.2f + 0.45f * (0.5f - 0.5f * cosf(2.f * th)) + 0.25f * Max(0.f, sinf(th));
            a.fingers = 0.12f;
            a.thumb = 0.1f;
            a.twist = 0.55f - 0.35f * cosf(th);
            LegCtl& l = r.leg[sd];
            l.ik = false;
            float kk = sinf(kTwoPi * ph * 3.f + (sd ? kPi : 0.f));
            l.hipFlex = 0.1f * kk;
            l.kneeFlex = 0.15f + 0.2f * Max(0.f, kk);
            l.ankleFlex = -1.1f;
            l.hipAbd = 0.04f;
            l.hipTwist = 0.1f;
        }
    } else {
        // treading water: upright, chin at the surface, sculling arms and cycling legs
        float ph = t / 2.4f;
        float c1 = cosf(kTwoPi * ph);
        r.pelvis = vec3(0, 0, kWaterZ + 0.085f * s - A.headP.z + 0.02f * s * c1);
        r.spinePitch = 0.08f;
        r.headPitch = -0.12f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            float p = ph * 2.f + (sd ? 0.5f : 0.f);
            armFK(r.arm[sd], sd, 0.6f + 0.12f * sinf(kTwoPi * p), 0.95f + 0.3f * cosf(kTwoPi * p), 0.7f, 1.3f, 0.15f);
            r.arm[sd].pole = normalize(vec3(sx * 0.3f, -0.4f, -1.f));
            LegCtl& l = r.leg[sd];
            l.ik = false;
            l.hipFlex = 0.5f + 0.3f * sinf(kTwoPi * p);
            l.kneeFlex = 1.0f + 0.4f * cosf(kTwoPi * p);
            l.hipAbd = 0.3f;
            l.ankleFlex = -0.4f;
        }
    }
}

// Climb onto a ledge / vault over an obstacle (in place: the game moves the origin along the path).
static void clipClimbVault(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    if (c == CLIP_CLIMB) {
        // the origin rises by the ledge height over the first ~60%; hands stay on the ledge edge
        Rig reach, hang, pull, knee, stand;
        standPose(A, reach);
        reach.spinePitch = -0.05f;
        reach.headPitch = -0.4f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            armIK(reach.arm[sd], vec3(sx * 0.22f * s, 0.3f * s, 2.0f * s), vec3(sx, -0.2f, -1), 0.85f);
            setFootToes(A, reach.leg[sd], A.ankle[sd].x, A.ankle[sd].y + 0.1f * s, -0.5f, reach.leg[sd].yaw);
        }
        hang = reach;
        hang.pelvis = vec3(0, 0.05f * s, 0.05f * s);
        hang.headPitch = -0.2f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            hang.arm[sd].target = vec3(sx * 0.22f * s, 0.3f * s, 1.55f * s);
            LegCtl& l = hang.leg[sd];
            l.ik = true;
            l.footQ = false;
            l.ankle = vec3(sx * 0.12f * s, 0.2f * s, A.footH + (sd ? 0.35f : 0.12f) * s);
            l.pitch = 0.f;
            l.toe = 0.f;
            l.knee = normalize(vec3(sx * 0.2f, 1.f, 0.f));
        }
        pull = hang;
        pull.pelvis = vec3(0, 0.08f * s, 0.f);
        pull.spinePitch = 0.35f;
        pull.headPitch = 0.1f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            pull.arm[sd].target = vec3(sx * 0.24f * s, 0.32f * s, 0.95f * s);
            pull.arm[sd].ikPole = normalize(vec3(sx * 0.5f, -1.f, 0.2f));
        }
        pull.leg[1].ankle = vec3(0.12f * s, 0.05f * s, A.footH + 0.5f * s);
        pull.leg[1].knee = normalize(vec3(0.1f, 1.f, 0.4f));
        knee = pull;
        knee.pelvis = vec3(0, 0.12f * s, -0.3f * s);
        knee.spinePitch = 0.55f;
        for (int sd = 0; sd < 2; sd++) knee.arm[sd].target = vec3((sd ? 1.f : -1.f) * 0.24f * s, 0.35f * s, 0.35f * s);
        setFootFlat(A, knee.leg[1], vec3(0.12f * s, 0.25f * s, 0.f), -0.1f);
        knee.leg[0].ankle = vec3(-0.1f * s, -0.2f * s, A.footH + 0.12f * s);
        knee.leg[0].pitch = -0.6f;
        standPose(A, stand);
        std::vector<Key> k = {{0.f, reach}, {0.2f, hang}, {0.5f, pull}, {0.8f, knee}, {1.2f, stand}};
        sampleKeys(A, k, t, false, 1.2f, r);
    } else {
        // the origin arcs over the obstacle: plant the hands, tuck the legs high, land and absorb
        Rig a, plant, over, land, e;
        standPose(A, a);
        a.spinePitch = 0.15f;
        for (int sd = 0; sd < 2; sd++) armFK(a.arm[sd], sd, 0.6f, 0.2f, 0.5f, 0.2f, 0.4f);
        plant = a;
        plant.pelvis = vec3(0, 0.05f * s, 0.05f * s);
        plant.spinePitch = 0.55f;
        plant.headPitch = -0.3f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            armIK(plant.arm[sd], vec3(sx * 0.18f * s, 0.45f * s, 0.75f * s), vec3(sx, -1, 0), 0.3f);
            setFootToes(A, plant.leg[sd], A.ankle[sd].x, A.ankle[sd].y + 0.05f * s, -0.6f, plant.leg[sd].yaw);
        }
        over = plant;
        over.pelvis = vec3(0.05f * s, 0.05f * s, 0.32f * s);
        over.pelvisRoll = -0.25f;
        over.spinePitch = 0.5f;
        over.headPitch = -0.2f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            over.arm[sd].target = vec3(sx * 0.2f * s, 0.25f * s, 0.55f * s);
            LegCtl& l = over.leg[sd];
            l.ik = false;
            l.hipFlex = 1.5f;
            l.kneeFlex = 2.0f;
            l.hipAbd = sd ? 0.45f : 0.2f;
            l.ankleFlex = -0.3f;
        }
        crouchPose(A, land, 0.45f);
        land.spinePitch = 0.35f;
        for (int sd = 0; sd < 2; sd++) armFK(land.arm[sd], sd, 0.5f, 0.35f, 0.5f, 0.2f, 0.4f);
        standPose(A, e);
        std::vector<Key> k = {{0.f, a}, {0.14f, plant}, {0.34f, over}, {0.52f, land}, {0.7f, e}};
        sampleKeys(A, k, t, false, 0.7f, r);
    }
}

static void clipWeapon(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    switch (c) {
        case CLIP_AIM_PISTOL: {
            aimPistolPose(A, r);
            float br = sinf(kTwoPi * t / 2.f);
            r.spinePitch += 0.01f * br;
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
            for (int sd = 0; sd < 2; sd++) k.arm[sd].target = k.arm[sd].target + vec3(0, -0.04f, 0.035f) * s;
            k.arm[1].handRot = qaa(vec3(1, 0, 0), 0.35f) * k.arm[1].handRot;
            k.arm[0].handRot = qaa(vec3(1, 0, 0), 0.25f) * k.arm[0].handRot;
            k.headPitch -= 0.03f;
            k.spinePitch -= 0.03f;
            std::vector<Key> ks = {{0.f, a}, {0.04f, k}, {0.3f, a}};
            sampleKeys(A, ks, t, false, 0.3f, r);
            break;
        }
        case CLIP_FIRE_RIFLE: {
            Rig a, k;
            aimRiflePose(A, a);
            k = a;
            for (int sd = 0; sd < 2; sd++) k.arm[sd].target = k.arm[sd].target + vec3(0, -0.03f, 0.012f) * s;
            k.spineYaw += 0.03f;
            k.spinePitch -= 0.02f;
            std::vector<Key> ks = {{0.f, a}, {0.03f, k}, {0.12f, a}};
            sampleKeys(A, ks, t, false, 0.12f, r);
            break;
        }
        case CLIP_RELOAD: {
            // weapon lowered in front, left hand to the belt, magazine in, slap, back up
            Rig base, low, pouch, insert, slap;
            aimRiflePose(A, base);
            low = base;
            vec3 shR = bonePos(A, base, B_UPPERARM_R);
            vec3 w(shR.x - 0.15f * s, shR.y + 0.3f * s, shR.z - 0.3f * s);
            low.arm[1].target = w;
            low.arm[1].handRot = handFrame(A, 1, vec3(-0.3f, 0.6f, -0.6f), vec3(-1, 0.f, 0.2f));
            low.arm[0].target = w + vec3(-0.08f, 0.14f, 0.02f) * s;
            low.headPitch = 0.45f;
            low.spinePitch = 0.18f;
            pouch = low;
            pouch.arm[0].target = vec3(-0.14f * s, 0.1f * s, A.hip[0].z + 0.06f * s);
            pouch.arm[0].ikPole = normalize(vec3(-1, -0.5f, 0));
            pouch.arm[0].handRot = handFrame(A, 0, vec3(0.1f, 0.2f, -1.f), vec3(0.8f, 0.2f, 0.f));
            pouch.headPitch = 0.3f;
            insert = low;
            insert.arm[0].target = w + vec3(-0.02f, 0.06f, -0.08f) * s;
            slap = low;
            slap.arm[0].target = w + vec3(-0.02f, 0.06f, -0.05f) * s;
            std::vector<Key> ks = {{0.f, base}, {0.25f, low}, {0.6f, pouch}, {1.0f, insert}, {1.2f, insert}, {1.35f, slap}, {1.55f, low}, {1.9f, base}};
            sampleKeys(A, ks, t, false, 1.9f, r);
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
            armFK(wind.arm[1], 1, -0.5f, 1.35f, 1.7f, 0.4f, 0.9f);
            wind.arm[1].pole = normalize(vec3(0.3f, -0.4f, -1.f));
            armFK(wind.arm[0], 0, 1.3f, 0.3f, 0.3f, 0.2f, 0.4f);
            rel = wind;
            rel.pelvis = vec3(0, 0.1f * s, -0.05f * s);
            rel.pelvisYaw = 0.3f;
            rel.spineYaw = 0.4f;
            rel.spinePitch = 0.25f;
            rel.headYaw = 0.f;
            armFK(rel.arm[1], 1, 1.9f, 0.25f, 0.3f, 0.6f, 0.2f);
            armFK(rel.arm[0], 0, -0.3f, 0.3f, 0.6f, 0.2f, 0.5f);
            setFootToes(A, rel.leg[1], A.ankle[1].x, A.ankle[1].y + A.ballFwd, -0.6f, -0.3f);
            follow = rel;
            follow.spinePitch = 0.45f;
            follow.spineYaw = 0.55f;
            armFK(follow.arm[1], 1, 0.4f, -0.2f, 0.4f, 0.6f, 0.2f);
            std::vector<Key> ks = {{0.f, a}, {0.4f, wind}, {0.6f, rel}, {0.82f, follow}, {1.2f, a}};
            sampleKeys(A, ks, t, false, 1.2f, r);
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
            vec3 hp;
            quat hq;
            boneOf(A, r, B_HEAD, hp, hq);
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                vec3 tgt = hp + rotate(hq, vec3(sx * 0.07f, 0.02f, 0.1f) * s) + vec3(0, 0, tr);
                armIK(r.arm[sd], tgt, vec3(sx * 0.3f, 1.f, -0.3f), 0.6f);
                r.arm[sd].twist = 0.4f;
            }
            r.pelvis.x += tr * 0.5f;
            break;
        }
        case CLIP_HANDS_UP: {
            standPose(A, r);
            float sw = sinf(kTwoPi * u);
            vec3 hp = bonePos(A, r, B_HEAD);
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                vec3 h(sx * 0.3f * s, hp.y + 0.1f * s, hp.z + 0.14f * s + 0.01f * sw * s);
                armIK(r.arm[sd], h, vec3(sx, -0.3f, -0.5f), 0.05f);
                r.arm[sd].orient = true;
                r.arm[sd].handRot = handFrame(A, sd, vec3(sx * 0.1f, 0.1f, 1), vec3(0, 1, 0));
            }
            r.headPitch = -0.05f;
            r.pelvis.x = 0.01f * sw * s;
            break;
        }
        case CLIP_TALK: case CLIP_TALK_PHONE: {
            clipIdle(A, t, dur, r, false);
            float j = 0.5f + 0.5f * sinf(t * 17.f) * sinf(t * 5.3f + 1.f);
            r.jaw = 0.08f * j * (0.6f + 0.4f * sinf(t * 2.1f));
            r.headPitch += 0.04f * sinf(t * 2.3f);
            r.headRoll += 0.04f * sinf(t * 1.1f);
            if (c == CLIP_TALK) {
                vec3 ch = bonePos(A, r, B_SPINE2);
                for (int sd = 0; sd < 2; sd++) {
                    float sx = sd ? 1.f : -1.f;
                    float g1 = 0.5f + 0.5f * sinf(t * 2.2f + sd * 1.7f), g2 = sinf(t * 3.1f + sd);
                    vec3 h(sx * (0.16f + 0.05f * g2) * s, ch.y + (0.25f + 0.08f * g1) * s, ch.z - (0.08f - 0.07f * g1) * s);
                    armIK(r.arm[sd], h, vec3(sx, -0.6f, -0.6f), 0.3f);
                    r.arm[sd].orient = true;
                    r.arm[sd].handRot = handFrame(A, sd, vec3(sx * 0.3f, 1.f, 0.1f + 0.2f * g2), vec3(-sx * 0.4f, 0.1f, 1.f));
                }
            } else {
                vec3 hp;
                quat hq;
                boneOf(A, r, B_HEAD, hp, hq);
                vec3 ear = hp + rotate(hq, vec3(0.09f, 0.015f, -0.02f) * s);
                armIK(r.arm[1], ear + rotate(hq, vec3(0.02f, -0.03f, -0.07f) * s), vec3(0.6f, -0.2f, -1.f), 0.7f);
                r.arm[1].orient = true;
                r.arm[1].handRot = hq * handFrame(A, 1, vec3(-0.25f, 0.35f, 1.f), vec3(-1, 0.1f, 0.f));
                r.headRoll += 0.1f;
                vec3 hipL = bonePos(A, r, B_THIGH_L);
                armIK(r.arm[0], hipL + vec3(-0.12f, 0.06f, 0.0f) * s, vec3(-1, -0.4f, 0.f), 0.5f);
            }
            break;
        }
        case CLIP_SIT_BENCH: {
            seatedPose(A, r, kSeatHipZ, 0.5f * s, 0.f, -0.02f);
            float w = sinf(kTwoPi * u);
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                vec3 knee = bonePos(A, r, sd ? B_CALF_R : B_CALF_L);
                vec3 th = lerp(bonePos(A, r, sd ? B_THIGH_R : B_THIGH_L), knee, 0.6f) + vec3(sx * 0.01f, 0.f, 0.075f * s);
                armIK(r.arm[sd], th, vec3(sx, -0.5f, -0.3f), 0.4f);
                r.arm[sd].orient = true;
                r.arm[sd].handRot = handFrame(A, sd, vec3(-sx * 0.2f, 1.f, -0.3f), vec3(0, 0.1f, -1.f));
            }
            r.headYaw = 0.5f * sinf(kTwoPi * u) * sstep(0.2f, 0.8f, fabsf(w));
            r.spinePitch += 0.015f * sinf(kTwoPi * u * 2.f);
            break;
        }
        case CLIP_SMOKE: {
            clipIdle(A, t, dur, r, false);
            float lift = sstep(0.08f, 0.2f, u) * (1.f - sstep(0.38f, 0.5f, u));
            vec3 hp;
            quat hq;
            boneOf(A, r, B_HEAD, hp, hq);
            vec3 mouth = hp + rotate(hq, vec3(0.03f, 0.12f, -0.07f) * s);
            vec3 rest = bonePos(A, r, B_SPINE1) + vec3(0.22f, 0.14f, 0.02f) * s;
            vec3 h = lerp(rest, mouth + vec3(0.015f, 0.01f, -0.03f) * s, lift);
            armIK(r.arm[1], h, vec3(1, -0.3f, -0.8f), 0.55f);
            r.arm[1].orient = true;
            r.arm[1].handRot = nlerp(handFrame(A, 1, vec3(-0.3f, 0.9f, 0.1f), vec3(-0.2f, 0.f, 1.f)), handFrame(A, 1, vec3(-0.6f, 0.4f, 0.6f), vec3(-0.5f, -0.8f, 0.f)), lift);
            r.headPitch += -0.25f * sstep(0.45f, 0.52f, u) * (1.f - sstep(0.6f, 0.7f, u));   // exhale upwards
            vec3 hipL = bonePos(A, r, B_THIGH_L);
            armIK(r.arm[0], hipL + vec3(-0.1f, 0.07f, -0.02f) * s, vec3(-1, -0.4f, 0.f), 0.6f);
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
            vec3 sh = bonePos(A, r, B_UPPERARM_R);
            vec3 h(sh.x + 0.16f * s + 0.03f * w * s, sh.y + 0.14f * s, sh.z + 0.28f * s);
            armIK(r.arm[1], h, vec3(1, -0.3f, -0.6f), 0.1f);
            r.arm[1].orient = true;
            r.arm[1].handRot = qaa(vec3(0, 1, 0), 0.35f * w) * handFrame(A, 1, vec3(0.1f, 0.1f, 1), vec3(0, 1, 0));
            r.headYaw = 0.1f;
            r.headRoll = 0.08f;
            break;
        }
        case CLIP_POINT: {
            clipIdle(A, t, 4.f, r, false);
            vec3 sh = bonePos(A, r, B_UPPERARM_R);
            vec3 h(sh.x - 0.05f * s, sh.y + 0.56f * s, sh.z + 0.03f * s);
            armIK(r.arm[1], h, vec3(1, -0.4f, -0.8f), 0.1f);
            r.arm[1].orient = true;
            r.arm[1].handRot = handFrame(A, 1, vec3(-0.05f, 1.f, 0.05f), vec3(-1, 0, -0.2f));
            r.spineYaw = 0.1f;
            r.headYaw = 0.05f;
            break;
        }
        case CLIP_CHEER: {
            standPose(A, r);
            float b = sinf(kTwoPi * u * 2.f);
            float hop = Max(0.f, b);
            r.pelvis = vec3(0, 0, -0.03f * s + 0.05f * hop * s);
            vec3 hp = bonePos(A, r, B_HEAD);
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                vec3 h(sx * 0.33f * s, hp.y + 0.08f * s, hp.z + (0.3f + 0.05f * b) * s);
                armIK(r.arm[sd], h, vec3(sx, -0.2f, -0.3f), 0.95f);
                if (hop > 0.f) {
                    setFootToes(A, r.leg[sd], A.ankle[sd].x, A.ankle[sd].y + A.heelBack + A.ballFwd, -0.4f * hop, r.leg[sd].yaw);
                    r.leg[sd].ankle.z += 0.05f * hop * s;
                }
            }
            r.headPitch = -0.2f;
            break;
        }
        case CLIP_LEAN_WALL: {
            // back against a wall behind (-Y), arms crossed, right foot flat against the wall
            standPose(A, r);
            float br = sinf(kTwoPi * u * 2.f);
            r.pelvis = vec3(0, -0.1f * s, -0.02f * s);
            r.pelvisPitch = -0.08f;
            r.spinePitch = -0.06f + 0.01f * br;
            r.headPitch = 0.05f;
            setFootFlat(A, r.leg[0], vec3(A.ankle[0].x - 0.02f * s, 0.18f * s, 0.f), 0.15f);
            r.leg[1].ankle = vec3(A.ankle[1].x, -0.2f * s, 0.42f * s);
            r.leg[1].pitch = 0.3f;
            r.leg[1].knee = vec3(0.2f, 1.f, 0.f);
            vec3 c2 = bonePos(A, r, B_SPINE2);
            armIK(r.arm[0], c2 + vec3(0.1f, 0.19f, 0.0f) * s, vec3(-1, -0.2f, -1), 0.6f);
            armIK(r.arm[1], c2 + vec3(-0.1f, 0.21f, 0.03f) * s, vec3(1, -0.2f, -1), 0.6f);
            r.arm[0].twist = r.arm[1].twist = 1.0f;
            r.headYaw = 0.4f * sinf(kTwoPi * u) * sstep(0.3f, 0.9f, fabsf(sinf(kTwoPi * u)));
            break;
        }
        case CLIP_SUNBATHE: {
            lyingPose(A, r, true, 0.5f, 0.f);
            float br = sinf(kTwoPi * u * 2.f);
            r.spinePitch += 0.01f * br;
            r.headYaw = 0.1f;
            r.headPitch = 0.25f;
            vec3 hp = bonePos(A, r, B_HEAD);
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                // hands behind the head, elbows out on the towel
                armIK(r.arm[sd], hp + vec3(sx * 0.05f, -0.06f, -0.03f) * s, vec3(sx, -0.5f, -0.2f), 0.3f);
                r.arm[sd].twist = 0.8f;
            }
            LegCtl& l = r.leg[1];
            l.ik = true;
            l.footQ = false;
            vec3 hipR = bonePos(A, r, B_THIGH_R);
            l.ankle = vec3(hipR.x + 0.04f * s, hipR.y + 0.48f * s, A.footH);
            l.pitch = 0.f;
            l.yaw = -0.1f;
            l.knee = normalize(vec3(0.15f, 0.f, 1.f));
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
                setFootToes(A, r.leg[sd], A.ankle[sd].x, A.ankle[sd].y + A.heelBack + A.ballFwd, -0.6f * heel, r.leg[sd].yaw);
                r.leg[sd].ankle.z += 0.03f * heel * s;
                armFK(r.arm[sd], sd, 0.3f + 0.2f * sx * b, 0.15f, 1.4f, 0.4f, 0.7f);
            }
            break;
        }
        default: standPose(A, r); break;
    }
}

// ------------------------------------------------------------------------------------------------
// Melee
//
// Held weapons: the fist's grip centre lies kGripAlong * palmLen along the fingers and kGripPalm * palmLen towards
// the palm from the wrist, and a handle held in it points out of the thumb side (hand-bone +Y). Weapon poses are
// authored as (g = right fist centre, d = handle direction towards the barrel / tip, f = finger direction): the right
// palm then faces d x f, and a left hand below it on the same handle wraps the same way (palm f x d).

static const float kBatGap = 0.095f;   // left fist centre below the right one on a bat handle (m)

// Arm s grips a handle along D (out of its thumb side) with the fist centre at G and the fingers along F.
static void gripArm(const AuthorCtx& A, ArmCtl& a, int s, vec3 G, vec3 D, vec3 F, vec3 pole) {
    D = normalize(D);
    F = nrmOr(F - D * dot(F, D), vec3(0, 0, -1));
    vec3 P = s ? cross(D, F) : cross(F, D);
    float pl = A.D.palmLen;
    armIK(a, G - F * (kGripAlong * pl) - P * (kGripPalm * pl), pole, 0.95f);
    a.thumb = 0.85f;
    a.orient = true;
    a.handRot = handFrame(A, s, F, P);
}

struct WKey {
    float t;
    vec3 g, d, f;          // right fist centre, handle direction (towards the barrel / tip), finger direction
    vec3 pole, poleL;      // elbow pole directions (right, left)
};

// Both hands on a bat: the right fist at w.g, the left one kBatGap below it (towards the knob).
static void batArms(const AuthorCtx& A, Rig& r, const WKey& w) {
    vec3 D = normalize(w.d);
    gripArm(A, r.arm[1], 1, w.g, D, w.f, w.pole);
    gripArm(A, r.arm[0], 0, w.g - D * kBatGap, D, w.f, w.poleL);
}

// Both hands on the bat for the keyed body rig: the fists point along the right shoulder -> grip direction (projected
// off the handle), which keeps the wrists anatomical and the finger direction continuous along the swing.
static void batArmsFromShoulder(const AuthorCtx& A, Rig& r, WKey w) {
    vec3 sh = bonePos(A, r, B_UPPERARM_R) + vec3(0.f, 0.f, -0.12f * A.D.s);
    vec3 D = normalize(w.d), v = w.g - sh;
    vec3 F = v - D * dot(v, D);
    w.f = length2(F) > 1e-6f ? normalize(F) : w.f;
    batArms(A, r, w);
}

// Time-aware Catmull-Rom weights over the keys around t (same scheme as sampleKeys).
static void splineWeights(const float* ts, int n, float t, int idx[4], float w[4]) {
    for (int j = 0; j < 4; j++) {
        idx[j] = 0;
        w[j] = 0.f;
    }
    if (t <= ts[0] || n < 2) {
        w[1] = 1.f;
        return;
    }
    if (t >= ts[n - 1]) {
        idx[1] = n - 1;
        w[1] = 1.f;
        return;
    }
    int i = 0;
    while (i + 2 < n && t > ts[i + 1]) i++;
    bool hasPrev = i > 0, hasNext = i + 2 < n;
    idx[0] = hasPrev ? i - 1 : i;
    idx[1] = i;
    idx[2] = i + 1;
    idx[3] = hasNext ? i + 2 : i + 1;
    float t0 = ts[i], t1 = ts[i + 1], tp = ts[idx[0]], tn = ts[idx[3]];
    float D = Max(t1 - t0, 1e-4f);
    float u = Saturate((t - t0) / D), u2 = u * u, u3 = u2 * u;
    float h00 = 2.f * u3 - 3.f * u2 + 1.f, h10 = u3 - 2.f * u2 + u, h01 = -2.f * u3 + 3.f * u2, h11 = u3 - u2;
    float a = hasPrev ? 1.f / Max(t1 - tp, 1e-4f) : 0.f;
    float b = hasNext ? 1.f / Max(tn - t0, 1e-4f) : 0.f;
    w[0] = -h10 * D * a;
    w[1] = h00 - h11 * D * b;
    w[2] = h01 + h10 * D * a;
    w[3] = h11 * D * b;
}

// Weapon path through keys (positions and directions splined; directions renormalized).
static WKey sampleWeapon(const WKey* k, int n, float t) {
    float ts[16];
    for (int i = 0; i < n && i < 16; i++) ts[i] = k[i].t;
    int idx[4];
    float w[4];
    splineWeights(ts, Min(n, 16), t, idx, w);
    WKey o;
    o.t = t;
    o.g = o.d = o.f = o.pole = o.poleL = vec3(0);
    for (int j = 0; j < 4; j++) {
        const WKey& q = k[idx[j]];
        o.g = o.g + q.g * w[j];
        o.d = o.d + normalize(q.d) * w[j];
        o.f = o.f + normalize(q.f) * w[j];
        o.pole = o.pole + normalize(q.pole) * w[j];
        o.poleL = o.poleL + normalize(q.poleL) * w[j];
    }
    const WKey& h = k[idx[1]];
    o.d = nrmOr(o.d, normalize(h.d));
    o.f = nrmOr(o.f, normalize(h.f));
    o.pole = nrmOr(o.pole, normalize(h.pole));
    o.poleL = nrmOr(o.poleL, normalize(h.poleL));
    return o;
}

// Keep the eyes on the opponent while the pelvis and spine turn.
static void faceTarget(Rig& r) { r.headYaw = -(r.pelvisYaw + r.spineYaw) * 0.8f; }

// Fist (wrist) up in front of the chin for the rig's current torso rotation.
static void guardArm(const AuthorCtx& A, Rig& r, int sd, vec3 off) {
    const float s = A.D.s;
    quat cy = qz(r.pelvisYaw + r.spineYaw);
    float sx = sd ? 1.f : -1.f;
    vec3 hp = bonePos(A, r, B_HEAD);
    fistArm(A, r, sd, hp + rotate(cy, off * s), rotate(cy, vec3(sx * 0.45f, -0.15f, -1.f)), rotate(cy, vec3(-sx, 0.f, 0.f)));
}

// Fighting stances per weapon (stance 19); the strikes start and end in them.
static void knifeGuard(const AuthorCtx& A, Rig& r, WKey& w) {
    guardPose(A, r);
    const float s = A.D.s;
    r.spinePitch = 0.16f;
    r.headPitch = 0.08f;
    w.g = vec3(0.12f, 0.34f, 1.1f) * s;
    w.d = normalize(vec3(-0.15f, 0.62f, 0.77f));
    w.f = normalize(vec3(0.05f, 0.78f, -0.6f));
    w.pole = normalize(vec3(1.f, -0.5f, -1.f));
    w.poleL = normalize(vec3(-1.f, -0.4f, -1.f));
    gripArm(A, r.arm[1], 1, w.g, w.d, w.f, w.pole);
    vec3 shL = bonePos(A, r, B_UPPERARM_L);
    armIK(r.arm[0], shL + vec3(0.16f, 0.33f, -0.03f) * s, w.poleL, 0.35f);
    r.arm[0].twist = 0.6f;
}

static void batGuard(const AuthorCtx& A, Rig& r, WKey& w) {
    guardPose(A, r);
    const float s = A.D.s;
    setFootFlat(A, r.leg[0], vec3(A.ankle[0].x - 0.05f * s, 0.12f * s, 0.f), 0.25f);
    setFootFlat(A, r.leg[1], vec3(A.ankle[1].x + 0.07f * s, -0.12f * s, 0.f), -0.35f);
    r.pelvisYaw = -0.3f;
    r.spineYaw = -0.1f;
    r.spinePitch = 0.1f;
    faceTarget(r);
    w.g = vec3(0.17f, 0.18f, 1.3f) * s;
    w.d = normalize(vec3(0.3f, -0.4f, 0.87f));
    w.f = normalize(vec3(-0.45f, 0.8f, 0.2f));
    w.pole = normalize(vec3(1.f, -0.2f, -1.f));
    w.poleL = normalize(vec3(-0.1f, 0.3f, -1.f));
    batArmsFromShoulder(A, r, w);
}

// Bat held across in front of the face: right hand on the handle, left hand overhand on the barrel.
static void batBlock(const AuthorCtx& A, Rig& r, WKey& w) {
    guardPose(A, r);
    const float s = A.D.s;
    r.spinePitch = 0.18f;
    r.headPitch = 0.12f;
    r.pelvis.z -= 0.02f * s;
    w.g = vec3(0.19f, 0.3f, 1.5f) * s;
    w.d = normalize(vec3(-1.f, 0.08f, 0.05f));
    w.f = vec3(0.f, -0.25f, 1.f);
    w.pole = normalize(vec3(1.f, -0.3f, -1.f));
    w.poleL = normalize(vec3(-1.f, -0.3f, -1.f));
    gripArm(A, r.arm[1], 1, w.g, w.d, w.f, w.pole);
    gripArm(A, r.arm[0], 0, w.g + normalize(w.d) * 0.4f * s, -w.d, w.f, w.poleL);
}

// Looping guards (internal clips): a light bounce and weave on top of the stances.
static void clipGuardLoop(const AuthorCtx& A, int id, float t, Rig& r) {
    const float s = A.D.s;
    float u = t / infoOf(id).duration;
    float bob = 0.5f - 0.5f * cosf(kTwoPi * 2.f * u);
    float sw = sinf(kTwoPi * u);
    WKey w;
    if (id == IC_GUARD_KNIFE) knifeGuard(A, r, w);
    else if (id == IC_GUARD_BAT) batGuard(A, r, w);
    else if (id == IC_BLOCK_BAT) batBlock(A, r, w);
    else guardPose(A, r);
    r.pelvis.z -= 0.012f * s * bob;
    r.pelvis.x += 0.01f * s * sw;
    r.spineRoll -= 0.035f * sw;
    r.headRoll += 0.02f * sw;
    if (id == IC_GUARD) {
        for (int sd = 0; sd < 2; sd++) r.arm[sd].target = r.arm[sd].target + vec3(0.008f * sw, 0.f, -0.006f * bob) * s;
    } else if (id == IC_GUARD_KNIFE) {
        w.g = w.g + vec3(0.025f * sw, 0.015f * bob, 0.012f * sinf(kTwoPi * 2.f * u)) * s;
        gripArm(A, r.arm[1], 1, w.g, w.d, w.f, w.pole);
        r.arm[0].target = r.arm[0].target + vec3(0.01f * sw, 0.f, -0.006f * bob) * s;
    } else if (id == IC_GUARD_BAT) {
        w.d = rotate(qaa(w.f, 0.07f * sw), w.d);
        w.g = w.g + vec3(0.f, 0.f, -0.008f * bob) * s;
        batArmsFromShoulder(A, r, w);
    } else {
        w.g = w.g + vec3(0.f, 0.f, -0.01f * bob) * s;
        gripArm(A, r.arm[1], 1, w.g, w.d, w.f, w.pole);
        gripArm(A, r.arm[0], 0, w.g + normalize(w.d) * 0.4f * s, -w.d, w.f, w.poleL);
    }
}

// Fist strikes from the guard: hook, uppercut, counter (from the block).
static void clipFists(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    Rig g;
    guardPose(A, g);
    switch (c) {
        case CLIP_HOOK: {
            // load onto the rear leg with the shoulders coiled right and the fist drawn wide, then whip the hips and
            // shoulders round: the fist travels a flat arc with the elbow up and lands at jaw height (0.42 s)
            Rig wind = g, hit = g, fol = g, back = g;
            wind.pelvisYaw = -0.45f;
            wind.spineYaw = -0.25f;
            wind.spineRoll = 0.08f;
            wind.pelvis = g.pelvis + vec3(0.03f, -0.04f, -0.02f) * s;
            faceTarget(wind);
            fistArm(A, wind, 1, vec3(0.25f, 0.06f, 1.42f) * s, vec3(1.f, 0.1f, -0.5f), vec3(-1.f, 0.3f, 0.f));
            guardArm(A, wind, 0, vec3(-0.07f, 0.3f, -0.16f));
            hit.pelvisYaw = 0.3f;
            hit.spineYaw = 0.35f;
            hit.spineRoll = -0.1f;
            hit.spinePitch = 0.15f;
            hit.pelvis = g.pelvis + vec3(-0.03f, 0.06f, -0.03f) * s;
            setFootToes(A, hit.leg[1], g.leg[1].ankle.x - 0.02f * s, g.leg[1].ankle.y + A.ballFwd, -0.55f, -1.0f);
            faceTarget(hit);
            armIK(hit.arm[1], vec3(0.05f, 0.52f, 1.44f) * s, vec3(1.f, -0.1f, 0.25f), 0.95f);
            hit.arm[1].orient = true;
            hit.arm[1].handRot = handFrame(A, 1, vec3(-0.85f, 0.5f, 0.05f), vec3(0.1f, 0.f, -1.f));
            guardArm(A, hit, 0, vec3(-0.05f, 0.18f, -0.14f));
            fol = hit;
            fol.pelvisYaw = 0.42f;
            fol.spineYaw = 0.5f;
            faceTarget(fol);
            armIK(fol.arm[1], vec3(-0.18f, 0.45f, 1.42f) * s, vec3(0.8f, 0.3f, 0.2f), 0.95f);
            fol.arm[1].handRot = handFrame(A, 1, vec3(-0.9f, 0.1f, -0.1f), vec3(0.1f, 0.f, -1.f));
            guardArm(A, fol, 0, vec3(-0.05f, 0.18f, -0.14f));
            back = g;
            back.pelvisYaw = 0.f;
            back.spineYaw = 0.2f;
            back.pelvis = g.pelvis + vec3(0.f, 0.03f, -0.01f) * s;
            faceTarget(back);
            guardArm(A, back, 1, vec3(0.1f, 0.2f, -0.2f));
            std::vector<Key> k = {{0.f, g}, {0.25f, wind}, {0.42f, hit}, {0.52f, fol}, {0.7f, back}, {0.95f, g}};
            sampleKeys(A, k, t, false, 0.95f, r);
            break;
        }
        case CLIP_UPPERCUT: {
            // dip and drop the right shoulder, then drive up through the legs: the fist rises vertically in front of
            // the chest and lands under the chin (0.3 s)
            Rig dip = g, hit = g, fol = g;
            dip.pelvis = g.pelvis + vec3(0.03f, 0.f, -0.07f) * s;
            dip.pelvisYaw = -0.4f;
            dip.spineYaw = -0.2f;
            dip.spineRoll = 0.12f;
            dip.spinePitch = 0.2f;
            faceTarget(dip);
            armIK(dip.arm[1], vec3(0.2f, 0.2f, 1.06f) * s, vec3(0.6f, -0.7f, -0.4f), 0.95f);
            dip.arm[1].orient = true;
            dip.arm[1].handRot = handFrame(A, 1, vec3(-0.1f, 0.45f, 0.9f), vec3(-0.2f, -1.f, 0.3f));
            guardArm(A, dip, 0, vec3(-0.07f, 0.28f, -0.17f));
            hit.pelvis = g.pelvis + vec3(-0.01f, 0.05f, 0.015f) * s;
            hit.pelvisYaw = 0.12f;
            hit.spineYaw = 0.3f;
            hit.spineRoll = -0.06f;
            hit.spinePitch = -0.02f;
            setFootToes(A, hit.leg[1], g.leg[1].ankle.x - 0.02f * s, g.leg[1].ankle.y + A.ballFwd, -0.5f, -0.8f);
            faceTarget(hit);
            armIK(hit.arm[1], vec3(0.04f, 0.4f, 1.47f) * s, vec3(0.4f, -0.3f, -1.f), 0.95f);
            hit.arm[1].orient = true;
            hit.arm[1].handRot = handFrame(A, 1, vec3(-0.15f, 0.25f, 1.f), vec3(0.f, -1.f, 0.15f));
            guardArm(A, hit, 0, vec3(-0.05f, 0.17f, -0.14f));
            fol = hit;
            fol.spinePitch = -0.1f;
            fol.pelvis = hit.pelvis + vec3(0.f, 0.01f, 0.015f) * s;
            armIK(fol.arm[1], vec3(0.03f, 0.37f, 1.58f) * s, vec3(0.4f, -0.3f, -1.f), 0.95f);
            std::vector<Key> k = {{0.f, g}, {0.16f, dip}, {0.3f, hit}, {0.4f, fol}, {0.8f, g}};
            sampleKeys(A, k, t, false, 0.8f, r);
            break;
        }
        default: {   // CLIP_COUNTER: from the blocking guard, sweep the attack aside and shove with both hands (0.32 s)
            Rig blk, parry, shove, hold;
            clipCombat(A, CLIP_BLOCK, 0.f, blk);
            parry = blk;
            parry.spineYaw = 0.25f;
            parry.pelvisYaw = -0.1f;
            faceTarget(parry);
            vec3 hp = bonePos(A, parry, B_HEAD);
            armIK(parry.arm[0], hp + vec3(-0.26f, 0.2f, -0.06f) * s, vec3(-1.f, -0.2f, -0.6f), 0.4f);
            parry.arm[0].twist = 0.3f;
            armIK(parry.arm[1], hp + vec3(0.12f, 0.08f, -0.3f) * s, vec3(1.f, -0.3f, -1.f), 0.5f);
            shove = g;
            shove.pelvis = g.pelvis + vec3(0.f, 0.1f, -0.02f) * s;
            shove.pelvisYaw = -0.05f;
            shove.spineYaw = 0.05f;
            shove.spinePitch = 0.22f;
            faceTarget(shove);
            setFootFlat(A, shove.leg[0], vec3(A.ankle[0].x - 0.02f * s, 0.26f * s, 0.f), 0.12f);
            setFootToes(A, shove.leg[1], g.leg[1].ankle.x, g.leg[1].ankle.y + A.ballFwd + 0.04f * s, -0.45f, -0.4f);
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                armIK(shove.arm[sd], vec3(sx * 0.13f, 0.6f, 1.3f) * s, vec3(sx, -0.2f, -1.f), 0.15f);
                shove.arm[sd].orient = true;
                shove.arm[sd].handRot = handFrame(A, sd, vec3(sx * -0.1f, 0.3f, 1.f), vec3(0.f, 1.f, -0.2f));
            }
            hold = shove;
            hold.pelvis = shove.pelvis + vec3(0.f, 0.01f, 0.f) * s;
            std::vector<Key> k = {{0.f, blk}, {0.13f, parry}, {0.32f, shove}, {0.42f, hold}, {0.8f, g}};
            sampleKeys(A, k, t, false, 0.8f, r);
            break;
        }
    }
}

// Knife strikes from the knife guard: a quick horizontal slash (0.2 s) and a lunging stab (0.32 s).
static void clipKnife(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    Rig g;
    WKey wg;
    knifeGuard(A, g, wg);
    wg.t = 0.f;
    const vec3 poleL = wg.poleL;
    if (c == CLIP_KNIFE_SLASH) {
        Rig wind = g, cut = g, fol = g;
        wind.spineYaw = -0.3f;
        wind.pelvisYaw = -0.4f;
        faceTarget(wind);
        cut.spineYaw = 0.25f;
        cut.pelvisYaw = -0.1f;
        cut.pelvis = g.pelvis + vec3(0.f, 0.04f, -0.01f) * s;
        faceTarget(cut);
        fol.spineYaw = 0.5f;
        fol.pelvisYaw = 0.1f;
        fol.spinePitch = 0.22f;
        fol.pelvis = g.pelvis + vec3(-0.02f, 0.05f, -0.02f) * s;
        faceTarget(fol);
        std::vector<Key> k = {{0.f, g}, {0.1f, wind}, {0.2f, cut}, {0.3f, fol}, {0.55f, g}};
        sampleKeys(A, k, t, false, 0.55f, r);
        const WKey wk[] = {
            wg,
            {0.1f, vec3(0.34f, 0.12f, 1.36f) * s, vec3(0.15f, -0.3f, 0.94f), vec3(-0.35f, 0.9f, 0.2f), vec3(1.f, -0.6f, -0.3f), poleL},
            {0.2f, vec3(0.02f, 0.56f, 1.3f) * s, vec3(-1.f, 0.15f, 0.05f), vec3(0.15f, 1.f, -0.1f), vec3(1.f, -0.3f, -0.2f), poleL},
            {0.3f, vec3(-0.3f, 0.36f, 1.18f) * s, vec3(-0.45f, -0.85f, 0.05f), vec3(-0.85f, 0.45f, -0.15f), vec3(0.5f, 0.3f, -0.8f), poleL},
            {0.55f, wg.g, wg.d, wg.f, wg.pole, poleL},
        };
        WKey w = sampleWeapon(wk, 5, t);
        gripArm(A, r.arm[1], 1, w.g, w.d, w.f, w.pole);
    } else {
        Rig cock = g, lunge = g, hold = g;
        cock.spineYaw = -0.3f;
        cock.pelvisYaw = -0.4f;
        cock.pelvis = g.pelvis + vec3(0.02f, -0.05f, -0.02f) * s;
        faceTarget(cock);
        lunge.pelvisYaw = -0.05f;
        lunge.spineYaw = 0.1f;
        lunge.spinePitch = 0.3f;
        lunge.pelvis = g.pelvis + vec3(0.f, 0.2f, -0.06f) * s;
        faceTarget(lunge);
        setFootFlat(A, lunge.leg[0], vec3(A.ankle[0].x - 0.02f * s, 0.34f * s, 0.f), 0.12f);
        setFootToes(A, lunge.leg[1], g.leg[1].ankle.x, g.leg[1].ankle.y + A.ballFwd + 0.06f * s, -0.4f, -0.35f);
        armIK(lunge.arm[0], bonePos(A, lunge, B_UPPERARM_L) + vec3(0.1f, 0.26f, -0.12f) * s, poleL, 0.4f);   // free hand up
        hold = lunge;
        hold.pelvis = lunge.pelvis + vec3(0.f, 0.01f, 0.f) * s;
        std::vector<Key> k = {{0.f, g}, {0.18f, cock}, {0.32f, lunge}, {0.42f, hold}, {0.75f, g}};
        sampleKeys(A, k, t, false, 0.75f, r);
        const WKey wk[] = {
            wg,
            {0.18f, vec3(0.22f, -0.02f, 1.08f) * s, vec3(0.f, 0.8f, 0.6f), vec3(0.f, 0.6f, -0.8f), vec3(1.f, -0.8f, -0.4f), poleL},
            {0.32f, vec3(0.05f, 0.78f, 1.18f) * s, vec3(0.f, 0.82f, 0.57f), vec3(0.f, 0.57f, -0.82f), vec3(0.8f, -0.4f, -0.6f), poleL},
            {0.42f, vec3(0.05f, 0.79f, 1.18f) * s, vec3(0.f, 0.82f, 0.57f), vec3(0.f, 0.57f, -0.82f), vec3(0.8f, -0.4f, -0.6f), poleL},
            {0.75f, wg.g, wg.d, wg.f, wg.pole, poleL},
        };
        WKey w = sampleWeapon(wk, 5, t);
        gripArm(A, r.arm[1], 1, w.g, w.d, w.f, w.pole);
    }
}

// Two-handed bat swings from the bat guard. The body is keyed; the bat follows its own spline and both hands are
// placed on it every frame.
static void clipBat(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    Rig g;
    WKey wg;
    batGuard(A, g, wg);
    wg.t = 0.f;
    if (c == CLIP_BAT_SWING) {
        // right to left, flat at chest height: coil, stride, hips then shoulders, contact 0.5 s, wrap round
        Rig coil = g, launch = g, hit = g, fol = g, fin = g;
        coil.pelvisYaw = -0.55f;
        coil.spineYaw = -0.35f;
        coil.pelvis = g.pelvis + vec3(0.04f, -0.05f, -0.01f) * s;
        faceTarget(coil);
        launch = coil;
        launch.pelvisYaw = -0.2f;
        launch.spineYaw = -0.3f;
        launch.pelvis = g.pelvis + vec3(0.f, 0.04f, -0.05f) * s;
        setFootFlat(A, launch.leg[0], vec3(A.ankle[0].x - 0.08f * s, 0.24f * s, 0.f), 0.3f);
        faceTarget(launch);
        hit = launch;
        hit.pelvisYaw = 0.35f;
        hit.spineYaw = 0.1f;
        hit.spinePitch = 0.18f;
        hit.pelvis = g.pelvis + vec3(-0.03f, 0.07f, -0.06f) * s;
        setFootToes(A, hit.leg[1], g.leg[1].ankle.x - 0.02f * s, g.leg[1].ankle.y + A.ballFwd, -0.6f, -1.1f);
        faceTarget(hit);
        fol = hit;
        fol.pelvisYaw = 0.6f;
        fol.spineYaw = 0.45f;
        fol.spinePitch = 0.1f;
        faceTarget(fol);
        fin = fol;
        fin.pelvisYaw = 0.5f;
        fin.spineYaw = 0.35f;
        fin.spinePitch = 0.05f;
        faceTarget(fin);
        std::vector<Key> k = {{0.f, g}, {0.3f, coil}, {0.43f, launch}, {0.5f, hit}, {0.62f, fol}, {0.78f, fin}, {1.05f, g}};
        sampleKeys(A, k, t, false, 1.05f, r);
        const WKey wk[] = {
            wg,
            {0.3f, vec3(0.28f, -0.02f, 1.38f) * s, vec3(-0.25f, -0.75f, 0.6f), vec3(0.2f, 0.85f, 0.45f), vec3(1.f, -0.4f, -0.6f), vec3(0.2f, 0.4f, -1.f)},
            {0.43f, vec3(0.24f, 0.24f, 1.2f) * s, vec3(0.45f, -0.75f, 0.45f), vec3(-0.6f, 0.4f, -0.2f), vec3(1.f, -0.2f, -0.8f), vec3(-0.2f, 0.2f, -1.f)},
            {0.5f, vec3(0.f, 0.46f, 1.12f) * s, vec3(0.05f, 1.f, 0.08f), vec3(-1.f, 0.f, -0.35f), vec3(1.f, -0.3f, -0.5f), vec3(-0.5f, -0.2f, -1.f)},
            {0.62f, vec3(-0.34f, 0.3f, 1.2f) * s, vec3(-0.8f, 0.3f, 0.25f), vec3(-0.2f, -1.f, -0.2f), vec3(0.3f, 0.3f, -1.f), vec3(-1.f, -0.5f, -0.5f)},
            {0.78f, vec3(-0.3f, 0.02f, 1.36f) * s, vec3(-0.35f, -0.65f, 0.65f), vec3(0.3f, -0.8f, 0.3f), vec3(0.3f, 0.5f, -1.f), vec3(-1.f, -0.2f, -0.6f)},
            {1.05f, wg.g, wg.d, wg.f, wg.pole, wg.poleL},
        };
        batArmsFromShoulder(A, r, sampleWeapon(wk, 7, t));
    } else {
        // overhead: raise the bat behind the head, step in and chop down through head height (0.62 s)
        Rig raise = g, chop = g, hit = g, fol = g;
        raise.pelvisYaw = -0.2f;
        raise.spineYaw = 0.f;
        raise.spinePitch = -0.15f;
        raise.pelvis = g.pelvis + vec3(0.f, -0.04f, 0.02f) * s;
        faceTarget(raise);
        raise.headPitch = -0.1f;
        chop = raise;
        chop.spinePitch = 0.05f;
        chop.pelvis = g.pelvis + vec3(0.f, 0.06f, -0.03f) * s;
        setFootFlat(A, chop.leg[0], vec3(A.ankle[0].x - 0.05f * s, 0.3f * s, 0.f), 0.15f);
        hit = chop;
        hit.pelvisYaw = 0.f;
        hit.spinePitch = 0.35f;
        hit.pelvis = g.pelvis + vec3(0.f, 0.1f, -0.07f) * s;
        setFootToes(A, hit.leg[1], g.leg[1].ankle.x - 0.02f * s, g.leg[1].ankle.y + A.ballFwd + 0.02f * s, -0.45f, -0.6f);
        faceTarget(hit);
        hit.headPitch = 0.1f;
        fol = hit;
        fol.spinePitch = 0.55f;
        fol.pelvis = g.pelvis + vec3(0.f, 0.1f, -0.1f) * s;
        std::vector<Key> k = {{0.f, g}, {0.38f, raise}, {0.55f, chop}, {0.62f, hit}, {0.76f, fol}, {1.2f, g}};
        sampleKeys(A, k, t, false, 1.2f, r);
        const WKey wk[] = {
            wg,
            {0.38f, vec3(0.1f, -0.02f, 1.78f) * s, vec3(0.05f, -0.85f, -0.3f), vec3(0.f, 0.3f, 1.f), vec3(1.f, 0.3f, 0.2f), vec3(-1.f, 0.3f, 0.2f)},
            {0.55f, vec3(0.05f, 0.3f, 1.62f) * s, vec3(0.f, 0.25f, 1.f), vec3(0.f, 1.f, -0.25f), vec3(1.f, -0.2f, -0.4f), vec3(-1.f, -0.2f, -0.4f)},
            {0.62f, vec3(0.03f, 0.46f, 1.28f) * s, vec3(0.f, 0.86f, 0.5f), vec3(0.f, 0.5f, -0.86f), vec3(0.8f, -0.4f, -0.6f), vec3(-0.8f, -0.4f, -0.6f)},
            {0.76f, vec3(0.02f, 0.4f, 0.98f) * s, vec3(0.f, 0.7f, -0.7f), vec3(0.f, -0.7f, -0.7f), vec3(0.7f, -0.5f, -0.6f), vec3(-0.7f, -0.5f, -0.6f)},
            {1.2f, wg.g, wg.d, wg.f, wg.pole, wg.poleL},
        };
        batArmsFromShoulder(A, r, sampleWeapon(wk, 6, t));
    }
}

// Root motion of the reference skeleton (the pose is baked in place relative to it; see clipRootMotion).
static vec3 rootMotionRef(int c, float t) {
    auto smoother = [](float x) {
        x = Saturate(x);
        return x * x * x * (x * (x * 6.f - 15.f) + 10.f);
    };
    switch (c) {
        case CLIP_DODGE_BACK: return vec3(0.f, -1.2f * smoother((t - 0.03f) / 0.4f), 0.f);
        case CLIP_DODGE_L: return vec3(-1.2f * smoother((t - 0.03f) / 0.4f), 0.f, 0.f);
        case CLIP_DODGE_R: return vec3(1.2f * smoother((t - 0.03f) / 0.4f), 0.f, 0.f);
        case CLIP_KNOCKOUT: return vec3(0.f, 0.38f * smoother((t - 0.45f) / 0.6f), 0.f);
        case CLIP_TAKEDOWN_VICTIM: return vec3(0.f, 0.4f * smoother((t - 2.45f) / 0.4f), 0.f);
        case CLIP_TAKEDOWN_ATTACKER: return vec3(0.f, -0.35f * smoother((t - 2.5f) / 0.45f), 0.f);   // steps back off the body
        default: return vec3(0.f);
    }
}

// Dodges from the guard (1.2 m, authored in the start frame; the root follows rootMotionRef).
static void clipDodge(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    Rig g;
    guardPose(A, g);
    vec3 m = rootMotionRef(c, 0.5f) * s;   // total displacement
    auto moved = [&](const Rig& a, vec3 d) {
        Rig o = a;
        o.pelvis = o.pelvis + d;
        for (int sd = 0; sd < 2; sd++) {
            o.leg[sd].ankle = o.leg[sd].ankle + d;
            o.arm[sd].target = o.arm[sd].target + d;
        }
        return o;
    };
    Rig end = moved(g, m);
    if (c == CLIP_DODGE_BACK) {
        // push off the front foot, the rear foot reaches back, glide, the front foot follows
        Rig push = g, reach, land, follow;
        push.pelvis = g.pelvis + vec3(0.f, -0.03f, -0.05f) * s;
        push.spinePitch = 0.22f;
        setFootToes(A, push.leg[1], g.leg[1].ankle.x, g.leg[1].ankle.y + A.ballFwd, -0.3f, -0.45f);
        reach = moved(g, rootMotionRef(c, 0.2f) * s);
        reach.pelvis.z -= 0.04f * s;
        reach.spinePitch = 0.25f;
        reach.leg[0].ankle = vec3(g.leg[0].ankle.x, g.leg[0].ankle.y - 0.3f * s, A.footH + 0.05f * s);   // just pushed off
        reach.leg[0].pitch = -0.35f;
        reach.leg[1].ankle = vec3(g.leg[1].ankle.x, -0.62f * s, A.footH + 0.08f * s);
        reach.leg[1].pitch = -0.2f;
        land = moved(g, rootMotionRef(c, 0.3f) * s);
        land.pelvis.z -= 0.06f * s;
        land.spinePitch = 0.2f;
        setFootFlat(A, land.leg[1], vec3(g.leg[1].ankle.x, g.leg[1].ankle.y + m.y, 0.f), -0.45f);
        land.leg[0].ankle = vec3(g.leg[0].ankle.x, -0.5f * s, A.footH + 0.07f * s);
        land.leg[0].pitch = 0.1f;
        follow = moved(g, rootMotionRef(c, 0.42f) * s);
        follow.pelvis.z -= 0.035f * s;
        follow.spinePitch = 0.15f;
        std::vector<Key> k = {{0.f, g}, {0.07f, push}, {0.2f, reach}, {0.3f, land}, {0.42f, follow}, {0.5f, end}};
        sampleKeys(A, k, t, false, 0.5f, r);
    } else {
        // side step: the leading foot reaches out, the body glides low, the trailing foot closes up
        bool left = c == CLIP_DODGE_L;
        int lead = left ? 0 : 1, trail = 1 - lead;
        float dx = left ? -1.f : 1.f;
        Rig push = g, reach, land, follow;
        push.pelvis = g.pelvis + vec3(-dx * 0.03f, 0.f, -0.05f) * s;
        push.pelvisRoll = dx * 0.08f;
        push.spineRoll = -dx * 0.1f;
        reach = moved(g, rootMotionRef(c, 0.2f) * s);
        reach.pelvis.z -= 0.05f * s;
        reach.pelvisRoll = -dx * 0.06f;
        reach.spineRoll = dx * 0.12f;
        reach.leg[lead].ankle = vec3(g.leg[lead].ankle.x + dx * 0.6f * s, g.leg[lead].ankle.y, A.footH + 0.07f * s);
        reach.leg[trail].ankle = vec3(g.leg[trail].ankle.x + dx * 0.25f * s, g.leg[trail].ankle.y, A.footH + 0.05f * s);   // pushed off
        reach.leg[trail].pitch = -0.3f;
        land = moved(g, rootMotionRef(c, 0.3f) * s);
        land.pelvis.z -= 0.07f * s;
        land.spineRoll = dx * 0.08f;
        setFootFlat(A, land.leg[lead], vec3(g.leg[lead].ankle.x + m.x, g.leg[lead].ankle.y, 0.f), g.leg[lead].yaw);
        land.leg[trail].ankle = vec3(g.leg[trail].ankle.x + dx * 0.55f * s, g.leg[trail].ankle.y, A.footH + 0.08f * s);
        follow = moved(g, rootMotionRef(c, 0.42f) * s);
        follow.pelvis.z -= 0.035f * s;
        std::vector<Key> k = {{0.f, g}, {0.07f, push}, {0.2f, reach}, {0.3f, land}, {0.42f, follow}, {0.5f, end}};
        sampleKeys(A, k, t, false, 0.5f, r);
    }
}

// Melee hit reactions (from the guard), knockout.
static void clipMeleeHit(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    Rig g;
    guardPose(A, g);
    if (c == CLIP_HIT_HEAD) {
        // the face snaps back and aside, the guard drifts open, then everything comes back
        Rig snap = g, rec = g;
        snap.headPitch = -0.42f;
        snap.neckPitch = -0.2f;
        snap.headYaw = g.headYaw + 0.32f;
        snap.headRoll = -0.15f;
        snap.spinePitch = -0.06f;
        snap.pelvis = g.pelvis + vec3(0.f, -0.03f, -0.01f) * s;
        for (int sd = 0; sd < 2; sd++) snap.arm[sd].target = g.arm[sd].target + vec3(sd ? 0.05f : -0.05f, -0.03f, -0.06f) * s;
        rec.headPitch = -0.12f;
        rec.headYaw = g.headYaw + 0.1f;
        rec.pelvis = g.pelvis + vec3(0.f, -0.02f, -0.02f) * s;
        std::vector<Key> k = {{0.f, g}, {0.06f, snap}, {0.2f, rec}, {0.45f, g}};
        sampleKeys(A, k, t, false, 0.45f, r);
    } else if (c == CLIP_HIT_BODY) {
        // doubled over a gut punch, hands to the belly, straighten up into the guard
        Rig fold = g, hold, rise = g;
        fold.spinePitch = 0.62f;
        fold.pelvisPitch = 0.15f;
        fold.neckPitch = 0.15f;
        fold.headPitch = -0.2f;
        fold.pelvisYaw = -0.1f;
        fold.spineYaw = 0.f;
        fold.headYaw = 0.05f;
        fold.pelvis = g.pelvis + vec3(0.f, -0.08f, -0.09f) * s;
        vec3 belly = bonePos(A, fold, B_SPINE1) + vec3(0.f, 0.16f, -0.02f) * s;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            armIK(fold.arm[sd], belly + vec3(sx * 0.07f, 0.f, 0.01f * sd) * s, vec3(sx, -0.4f, -0.5f), 0.55f);
            fold.arm[sd].twist = 0.4f;
        }
        hold = fold;
        hold.spinePitch = 0.55f;
        hold.pelvis = fold.pelvis + vec3(0.f, 0.01f, 0.01f) * s;
        rise.spinePitch = 0.3f;
        rise.pelvis = g.pelvis + vec3(0.f, -0.04f, -0.04f) * s;
        std::vector<Key> k = {{0.f, g}, {0.12f, fold}, {0.42f, hold}, {0.65f, rise}, {0.9f, g}};
        sampleKeys(A, k, t, false, 0.9f, r);
    } else {
        // KNOCKOUT: the head snaps, the knees give, limp onto the knees and forward onto the face. Ends like the start
        // of GET_UP_FRONT relative to the root (which moves forward, rootMotionRef).
        Rig hit = g, buckle, kneel, impact, end;
        hit.headPitch = -0.45f;
        hit.neckPitch = -0.2f;
        hit.headYaw = g.headYaw + 0.35f;
        hit.spinePitch = -0.05f;
        for (int sd = 0; sd < 2; sd++) armFK(hit.arm[sd], sd, 0.5f, 0.35f, 1.1f, 0.4f, 0.4f);
        buckle = hit;
        buckle.pelvis = g.pelvis + vec3(0.f, 0.02f, -0.3f) * s;
        buckle.pelvisPitch = 0.2f;
        buckle.spinePitch = 0.25f;
        buckle.headPitch = 0.2f;
        buckle.headYaw = 0.2f;
        buckle.headRoll = 0.25f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            buckle.leg[sd].knee = normalize(vec3(sx * 0.25f, 1.f, 0.1f));
            armFK(buckle.arm[sd], sd, 0.15f, 0.2f, 0.3f, 0.3f, 0.3f);
        }
        kneel = Rig();
        kneel.pelvisPitch = 0.35f;
        kneel.spinePitch = 0.4f;
        kneel.headPitch = 0.35f;
        kneel.headRoll = 0.3f;
        placeHips(A, kneel, vec3(0.f, 0.05f * s, 0.47f * s));
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            LegCtl& l = kneel.leg[sd];
            l.ik = true;
            l.footQ = false;
            l.ankle = vec3(A.hip[sd].x + sx * 0.04f * s, -0.33f * s, 0.075f * s);
            l.pitch = -1.25f;
            l.yaw = -sx * 0.1f;
            l.toe = 1.1f;
            l.knee = normalize(vec3(sx * 0.15f, 0.6f, -1.f));
            armFK(kneel.arm[sd], sd, 0.05f, 0.15f, 0.25f, 0.3f, 0.3f);
        }
        lyingPose(A, end, false, 0.5f, 0.38f * s);
        impact = end;
        impact.pelvis.z += 0.05f * s;
        impact.spinePitch += 0.12f;
        impact.headPitch += 0.15f;
        std::vector<Key> k = {{0.f, g}, {0.08f, hit}, {0.36f, buckle}, {0.62f, kneel}, {0.92f, impact}, {1.5f, end}};
        sampleKeys(A, k, t, false, 1.5f, r, 0.2f);
    }
}

// Synced rear choke. The victim clip is authored in the victim's frame; the attacker stands 0.55 m behind it (same
// facing) and its hands are placed on the victim's neck and head as evaluated at the same time.
static const float kTakedownGap = 0.55f;
static const float kTakedownGrab = 0.35f, kTakedownRelease = 2.45f;

static void takedownVictim(const AuthorCtx& A, float t, Rig& r) {
    const float s = A.D.s;
    Rig a, grab, pull1, pull2, weak, limp, drop, end;
    standPose(A, a);
    grab = a;
    grab.pelvis = vec3(0.f, -0.1f, -0.02f) * s;
    grab.spinePitch = -0.18f;
    grab.neckPitch = -0.12f;
    grab.headPitch = -0.2f;
    grab.pelvisPitch = -0.08f;
    setFootToes(A, grab.leg[0], A.ankle[0].x, 0.05f * s + A.ballFwd, -0.4f, 0.1f);
    vec3 throat(0.f, 0.08f * s, 1.44f * s);
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        armIK(grab.arm[sd], throat + vec3(sx * 0.06f, 0.05f, 0.02f) * s, vec3(sx, -0.5f, -0.6f), 0.8f);
        grab.arm[sd].twist = 0.9f;
    }
    pull1 = grab;
    pull1.pelvis = vec3(0.03f, -0.12f, -0.04f) * s;
    pull1.pelvisRoll = 0.08f;
    pull1.spineRoll = -0.1f;
    pull1.headRoll = 0.12f;
    pull1.leg[1].ankle = vec3(A.ankle[1].x + 0.03f * s, 0.12f * s, A.footH + 0.12f * s);   // kicks out
    pull1.leg[1].pitch = 0.4f;
    for (int sd = 0; sd < 2; sd++) pull1.arm[sd].target = grab.arm[sd].target + vec3(0.f, 0.02f, -0.04f) * s;
    pull2 = grab;
    pull2.pelvis = vec3(-0.03f, -0.14f, -0.07f) * s;
    pull2.pelvisRoll = -0.1f;
    pull2.spineRoll = 0.12f;
    pull2.headRoll = -0.12f;
    setFootToes(A, pull2.leg[1], A.ankle[1].x + 0.03f * s, 0.1f * s + A.ballFwd, -0.5f, -0.1f);
    pull2.leg[0].ankle = vec3(A.ankle[0].x - 0.02f * s, 0.16f * s, A.footH + 0.1f * s);
    pull2.leg[0].pitch = 0.35f;
    weak = grab;
    weak.pelvis = vec3(0.f, -0.14f, -0.2f) * s;
    weak.spinePitch = -0.05f;
    weak.headPitch = 0.1f;
    weak.headRoll = 0.2f;
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        setFootFlat(A, weak.leg[sd], vec3(A.ankle[sd].x + sx * 0.03f * s, 0.12f * s, 0.f), -sx * 0.3f);
        weak.leg[sd].knee = normalize(vec3(sx * 0.3f, 1.f, 0.f));
        armFK(weak.arm[sd], sd, 0.35f, 0.25f, 0.9f, 0.4f, 0.4f);
    }
    limp = Rig();
    limp.pelvisPitch = 0.1f;
    limp.spinePitch = -0.1f;
    limp.headPitch = 0.3f;
    limp.headRoll = 0.3f;
    placeHips(A, limp, vec3(0.f, -0.12f * s, 0.46f * s));
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        LegCtl& l = limp.leg[sd];
        l.ik = true;
        l.footQ = false;
        l.ankle = vec3(A.hip[sd].x + sx * 0.05f * s, -0.5f * s, 0.075f * s);
        l.pitch = -1.25f;
        l.yaw = -sx * 0.15f;
        l.toe = 1.1f;
        l.knee = normalize(vec3(sx * 0.2f, 0.7f, -1.f));
        armFK(limp.arm[sd], sd, 0.02f, 0.12f, 0.25f, 0.3f, 0.3f);
    }
    lyingPose(A, end, false, 0.5f, 0.4f * s);
    drop = end;
    drop.pelvis.z += 0.06f * s;
    drop.spinePitch += 0.15f;
    drop.headPitch += 0.2f;
    std::vector<Key> k = {{0.f, a},        {kTakedownGrab, grab}, {0.8f, pull1}, {1.25f, pull2}, {1.65f, pull1},
                          {1.95f, weak},   {2.3f, limp},          {kTakedownRelease, limp},     {2.72f, drop}, {3.0f, end}};
    sampleKeys(A, k, t, false, 3.0f, r, 0.2f);
}

static void takedownAttacker(const AuthorCtx& A, float t, Rig& r) {
    const float s = A.D.s;
    const vec3 off(0.f, kTakedownGap * s, 0.f);
    Rig a, reach, hold, lean, lower, rel;
    standPose(A, a);
    reach = a;
    reach.pelvis = vec3(0.f, 0.1f, -0.03f) * s;
    reach.spinePitch = 0.2f;
    setFootFlat(A, reach.leg[0], vec3(A.ankle[0].x, 0.22f * s, 0.f), 0.05f);
    armFK(reach.arm[1], 1, 1.25f, 0.3f, 1.3f, 0.6f, 0.5f);   // arms coming up round the victim
    armFK(reach.arm[0], 0, 1.35f, 0.2f, 1.5f, 0.4f, 0.4f);
    hold = a;
    hold.pelvis = vec3(0.02f, 0.14f, -0.08f) * s;
    hold.spinePitch = 0.08f;
    hold.headPitch = 0.1f;
    hold.headYaw = -0.25f;
    setFootFlat(A, hold.leg[0], vec3(A.ankle[0].x - 0.03f * s, 0.24f * s, 0.f), 0.1f);
    setFootFlat(A, hold.leg[1], vec3(A.ankle[1].x + 0.06f * s, -0.12f * s, 0.f), -0.3f);
    for (int sd = 0; sd < 2; sd++) hold.leg[sd].knee = normalize(vec3((sd ? 1.f : -1.f) * 0.25f, 1.f, 0.f));
    lean = hold;
    lean.pelvis = vec3(0.02f, 0.08f, -0.1f) * s;
    lean.spinePitch = -0.06f;
    lower = hold;
    lower.pelvis = vec3(0.04f, 0.1f, -0.38f) * s;
    lower.spinePitch = 0.35f;
    lower.headPitch = 0.3f;
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        setFootFlat(A, lower.leg[sd], vec3(A.ankle[sd].x + sx * 0.08f * s, (sd ? -0.12f : 0.26f) * s, 0.f), -sx * 0.25f);
        lower.leg[sd].knee = normalize(vec3(sx * 0.4f, 1.f, 0.f));
    }
    rel = lower;
    rel.pelvis = vec3(0.02f, -0.1f, -0.22f) * s;
    rel.spinePitch = 0.25f;
    rel.leg[1].ankle = vec3(A.ankle[1].x + 0.06f * s, -0.4f * s, A.footH + 0.06f * s);   // stepping back
    for (int sd = 0; sd < 2; sd++) armFK(rel.arm[sd], sd, 0.45f, 0.3f, 0.7f, 0.3f, 0.4f);
    Rig end = a;   // standing 0.35 m further back (root motion)
    vec3 back = rootMotionRef(CLIP_TAKEDOWN_ATTACKER, 3.f) * s;
    end.pelvis = end.pelvis + back;
    for (int sd = 0; sd < 2; sd++) end.leg[sd].ankle = end.leg[sd].ankle + back;
    std::vector<Key> k = {{0.f, a},       {0.22f, reach},   {kTakedownGrab, hold}, {0.9f, lean},       {1.4f, hold},
                          {1.8f, lean},   {2.3f, lower},    {kTakedownRelease, lower}, {2.7f, rel},    {3.0f, end}};
    sampleKeys(A, k, t, false, 3.0f, r, 0.2f);
    // hands on the victim: the right forearm across the throat (hand towards its left shoulder), the left hand
    // behind its head; blended in over the reach and out at the release
    float w = sstep(0.12f, kTakedownGrab, t) * (1.f - sstep(kTakedownRelease, kTakedownRelease + 0.18f, t));
    if (w > 0.f) {
        Rig v;
        takedownVictim(A, t, v);
        vec3 vp;
        quat vq;
        boneOf(A, v, B_NECK, vp, vq);
        vec3 neck = vp + off, fwdN = rotate(vq, vec3(0, 1, 0)), upN = rotate(vq, vec3(0, 0, 1));
        vec3 hp;
        quat hq;
        boneOf(A, v, B_HEAD, hp, hq);
        hp = hp + off;
        vec3 throat = neck + fwdN * 0.07f * s + upN * 0.04f * s;
        Rig h = r;
        armIK(h.arm[1], throat + vec3(-0.11f, -0.01f, -0.03f) * s, vec3(0.3f, 1.f, -0.2f), 0.7f);
        h.arm[1].orient = true;
        h.arm[1].handRot = handFrame(A, 1, vec3(-1.f, -0.1f, -0.1f), vec3(0.f, -0.8f, -0.5f));
        vec3 back = hp - rotate(hq, vec3(0, 1, 0)) * 0.1f * s + rotate(hq, vec3(0, 0, 1)) * 0.02f * s;
        armIK(h.arm[0], back + vec3(-0.02f, -0.07f, -0.02f) * s, vec3(-1.f, -0.2f, -0.5f), 0.3f);
        h.arm[0].orient = true;
        h.arm[0].handRot = handFrame(A, 0, vec3(0.2f, 0.4f, 0.9f), vec3(0.f, 1.f, -0.3f));
        Rig* rr[2] = {&r, &h};
        unifyRigs(A, rr, 2);
        const Rig* cr[2] = {&r, &h};
        float ww[2] = {1.f - w, w};
        Rig o;
        combineRigs(cr, ww, 2, o);
        r = o;
    }
}

// Two-handed bat grip used by the animator to keep the left hand exactly on the handle (see anim_internal.h).
float batGrip(int clip, float t, float& dist, bool& reversed) {
    (void)t;
    dist = -kBatGap;
    reversed = false;
    switch (clip) {
        case CLIP_BAT_SWING: case CLIP_BAT_OVERHEAD: case IC_GUARD_BAT: return 1.f;
        case IC_BLOCK_BAT:
            dist = 0.4f;
            reversed = true;
            return 1.f;
        default: return 0.f;
    }
}

// ------------------------------------------------------------------------------------------------
// Ambient variety: idle variations, dance styles, ground poses (internal clips, see anim_internal.h)

static void clipAmbient(const AuthorCtx& A, int id, float t, Rig& r) {
    const float s = A.D.s;
    const float dur = infoOf(id).duration, u = t / dur;
    const float br = sinf(kTwoPi * u * (dur / 2.f));   // ~2 s breathing
    const float sw = sinf(kTwoPi * u);                   // slow sway over the loop
    switch (id) {
        case IC_IDLE_CROSSARMS: {
            standPose(A, r);
            r.spinePitch = -0.02f;
            r.headRoll = 0.04f * sw;
            r.headPitch = 0.04f;
            vec3 c = bonePos(A, r, B_CHEST);
            // forearms folded across the chest: the left hand tucked under the right upper arm, the right over the left
            armIK(r.arm[0], c + vec3(0.13f, 0.15f, -0.06f) * s, vec3(-1.f, -0.3f, -0.6f), 0.55f);
            armIK(r.arm[1], c + vec3(-0.12f, 0.19f, -0.02f) * s, vec3(1.f, -0.3f, -0.6f), 0.4f);
            r.arm[0].orient = r.arm[1].orient = true;
            r.arm[0].handRot = handFrame(A, 0, vec3(1.f, 0.1f, 0.05f), vec3(0.f, -1.f, 0.1f));
            r.arm[1].handRot = handFrame(A, 1, vec3(-1.f, 0.05f, -0.1f), vec3(0.f, -1.f, -0.2f));
            r.arm[0].clavFwd = r.arm[1].clavFwd = 0.06f;
            break;
        }
        case IC_IDLE_POCKETS: {
            standPose(A, r);
            r.headYaw = 0.1f * sw;
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                vec3 hip = A.hip[sd];
                // hands slid into the front pockets, thumbs out, elbows relaxed back
                armIK(r.arm[sd], vec3(hip.x + sx * 0.035f * s, hip.y + 0.07f * s, hip.z + 0.02f * s), vec3(sx * 0.6f, -1.f, 0.1f), 0.5f);
                r.arm[sd].orient = true;
                r.arm[sd].handRot = handFrame(A, sd, vec3(-sx * 0.2f, 0.3f, -1.f), vec3(-sx * 0.8f, -0.2f, 0.f));
                r.arm[sd].thumb = 0.1f;
            }
            break;
        }
        case IC_IDLE_HIP: {
            // weight on the right leg, hip out, the left knee relaxed forward, right hand on the hip
            standPose(A, r);
            r.pelvis = vec3(0.035f * s, 0.f, -0.012f * s);
            r.pelvisRoll = -0.07f;
            r.pelvisYaw = 0.05f * sw;
            r.spineRoll = 0.09f;
            r.headRoll = -0.04f + 0.03f * sw;
            setFootFlat(A, r.leg[1], vec3(A.ankle[1].x + 0.01f * s, 0.f, 0.f), 0.25f);
            setFootToes(A, r.leg[0], A.ankle[0].x - 0.02f * s, 0.07f * s + A.ballFwd, -0.25f, -0.2f);
            r.leg[0].knee = normalize(vec3(0.3f, 1.f, 0.f));
            // the palm on the flank above the hip bone (the skin carried by the posed pelvis), fingers forward and down
            vec3 pp;
            quat pq;
            boneOf(A, r, B_PELVIS, pp, pq);
            vec3 flank = pp + rotate(pq, A.skinFlankR - A.pelvisBind);
            vec3 fing = normalize(vec3(-0.35f, 0.5f, -0.8f)), palmN = normalize(vec3(-1.f, 0.f, 0.2f));
            vec3 palmC = flank - rotate(pq, palmN) * (0.5f * A.D.handT + 0.004f);
            armIK(r.arm[1], palmC - fing * (0.45f * A.D.palmLen), vec3(1.f, -0.4f, 0.f), 0.3f);
            r.arm[1].orient = true;
            r.arm[1].handRot = handFrame(A, 1, fing, palmN);
            armFK(r.arm[0], 0, 0.05f + 0.02f * sw, 0.12f, 0.25f, 0.2f, 0.4f);
            break;
        }
        case IC_IDLE_PHONE: {
            // glancing at a phone held low in both hands, thumb scrolling
            standPose(A, r);
            r.spinePitch = 0.1f;
            r.neckPitch = 0.2f;
            r.headPitch = 0.3f + 0.03f * sinf(kTwoPi * u * 3.f);
            vec3 c = bonePos(A, r, B_CHEST);
            vec3 ph = c + vec3(0.02f * s, 0.27f * s, -0.19f * s);
            float scroll = sinf(kTwoPi * u * 6.f);
            armIK(r.arm[1], ph + vec3(0.045f, -0.06f, -0.02f) * s, vec3(1.f, -0.5f, -0.6f), 0.55f);
            armIK(r.arm[0], ph + vec3(-0.045f, -0.06f, -0.025f) * s, vec3(-1.f, -0.5f, -0.6f), 0.6f);
            r.arm[1].orient = r.arm[0].orient = true;
            r.arm[1].handRot = handFrame(A, 1, vec3(-0.3f, 0.8f, 0.4f), vec3(-0.5f, 0.1f, 0.85f));
            r.arm[0].handRot = handFrame(A, 0, vec3(0.3f, 0.8f, 0.4f), vec3(0.5f, 0.1f, 0.85f));
            r.arm[1].thumb = 0.3f + 0.25f * scroll;
            break;
        }
        case IC_IDLE_STRETCH: {
            // neck roll and a shoulder shrug (a fidget: from and back to the plain standing pose)
            standPose(A, r);
            float a = kTwoPi * u, e = sstep(0.f, 0.12f, u) * (1.f - sstep(0.5f, 0.62f, u));
            r.headRoll = 0.22f * sinf(a) * e;
            r.headPitch = (0.1f + 0.15f * cosf(a)) * e;
            r.neckRoll = 0.1f * sinf(a) * e;
            float shrug = sstep(0.55f, 0.7f, u) * (1.f - sstep(0.8f, 0.95f, u));
            r.arm[0].clavUp = r.arm[1].clavUp = 0.25f * shrug;
            r.spinePitch = -0.05f * shrug;
            break;
        }
        case IC_DANCE2: {
            // side-step groove: steps left and right on the beat, arms swinging across
            standPose(A, r);
            float beat = t * 2.f;
            float step = sinf(kPi * beat);          // one side per beat
            float bounce = fabsf(sinf(kPi * beat));
            r.pelvis = vec3(0.07f * step * s, 0.f, -0.04f * s - 0.03f * s * bounce);
            r.pelvisRoll = -0.1f * step;
            r.spineRoll = 0.12f * step;
            r.spineYaw = 0.15f * step;
            r.headRoll = -0.06f * step;
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                float lift = Max(0.f, sx * step);   // the leg on the far side lifts as the weight moves
                r.leg[sd].ankle = vec3(A.ankle[sd].x + sx * 0.08f * s + 0.05f * step * s, 0.f, A.footH + 0.05f * s * lift * lift);
                r.leg[sd].knee = normalize(vec3(sx * 0.25f, 1.f, 0.f));
                armFK(r.arm[sd], sd, 0.5f + 0.3f * sx * step, 0.25f, 1.5f, 0.5f, 0.6f);
            }
            break;
        }
        case IC_DANCE3: {
            // club bounce with hands up, fast (one beat per 0.5 s)
            standPose(A, r);
            float b = sinf(kTwoPi * t * 2.f);
            float bb = 0.5f + 0.5f * b;
            r.pelvis = vec3(0.f, 0.f, -0.05f * s - 0.05f * s * bb);
            r.spinePitch = 0.08f * bb;
            r.headPitch = 0.15f * bb - 0.05f;
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                setFootFlat(A, r.leg[sd], vec3(A.ankle[sd].x + sx * 0.06f * s, 0.f, 0.f), -sx * 0.15f);
                r.leg[sd].knee = normalize(vec3(sx * 0.3f, 1.f, 0.f));
                vec3 sh = A.gh[sd];
                armIK(r.arm[sd], sh + vec3(sx * 0.12f, 0.12f, 0.42f + 0.05f * b) * s, vec3(sx, -0.3f, -0.5f), 0.5f + 0.4f * bb);
                r.arm[sd].orient = true;
                r.arm[sd].handRot = handFrame(A, sd, vec3(sx * 0.1f, 0.2f, 1.f), vec3(0.f, 1.f, 0.f));
            }
            break;
        }
        case IC_DANCE4: {
            // slow hip sway, arms low and loose
            standPose(A, r);
            float half = sinf(kTwoPi * t * 0.5f);
            float b = sinf(kTwoPi * t);
            r.pelvis = vec3(0.05f * half * s, 0.f, -0.03f * s - 0.015f * s * (0.5f + 0.5f * b));
            r.pelvisRoll = 0.14f * half;
            r.pelvisYaw = 0.1f * b;
            r.spineRoll = -0.12f * half;
            r.headRoll = 0.07f * half;
            r.headYaw = -0.08f * b;
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                setFootFlat(A, r.leg[sd], vec3(A.ankle[sd].x + sx * 0.04f * s, 0.f, 0.f), -sx * 0.18f);
                r.leg[sd].knee = normalize(vec3(sx * 0.3f, 1.f, 0.f));
                armFK(r.arm[sd], sd, 0.3f + 0.2f * sinf(kTwoPi * t * 0.5f + sd * 1.3f), 0.25f, 0.9f + 0.3f * b, 0.4f, 0.5f);
            }
            break;
        }
        case IC_SIT_GROUND: {
            // on a towel: pelvis on the ground, knees up, leaning back on straight arms
            r = Rig();
            r.pelvisPitch = -0.55f;
            placeHips(A, r, vec3(0.f, 0.f, 0.1f * s));
            r.spinePitch = 0.25f + 0.01f * br;
            r.headPitch = -0.15f + 0.05f * sw;
            r.headYaw = 0.2f * sinf(kTwoPi * u * 0.5f);
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                setFootFlat(A, r.leg[sd], vec3(A.hip[sd].x + sx * 0.08f * s, 0.46f * s, 0.f), -sx * 0.2f);
                r.leg[sd].knee = normalize(vec3(sx * 0.3f, 0.3f, 1.f));
                armIK(r.arm[sd], vec3(sx * 0.2f * s, -0.26f * s, 0.035f * s), vec3(sx * 0.4f, 0.8f, 0.f), 0.2f);
                r.arm[sd].orient = true;
                r.arm[sd].handRot = handFrame(A, sd, vec3(sx * 0.2f, -1.f, -0.1f), vec3(0.f, 0.f, -1.f));
            }
            break;
        }
        default: {   // IC_LIE_FRONT: face down on a towel, head turned on the folded forearms
            lyingPose(A, r, false, 0.5f, 0.f);
            r.headYaw = 1.0f;
            r.headPitch = -0.1f;
            r.neckPitch = -0.15f;
            r.spinePitch += 0.008f * br;
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                ArmCtl& a = r.arm[sd];
                a.ik = false;
                a.dir = normalize(vec3(sx * 0.8f, 0.45f, 0.2f));
                a.pole = normalize(vec3(sx * 0.3f, 0.2f, -1.f));
                a.elbow = 1.9f;
                a.twist = 0.9f;
                a.fingers = 0.4f;
            }
            break;
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Standing: weight on one leg, held postures and fidgets (internal clips, see anim_internal.h). They carry no
// breathing (the animator breathes at each person's own rate); fidgets start and end in the plain standing pose.

// Weight on one leg (side 0 left, 1 right): the pelvis over the standing foot, the free hip dropped and a little
// forward, the free knee eased with its foot forward and turned out, the shoulders counter-tilted, arms hanging.
static void standWeight(const AuthorCtx& A, float t, float dur, Rig& r, int side) {
    standPose(A, r);
    const float s = A.D.s, fem = A.fem;
    const float sx = side ? 1.f : -1.f;   // the standing side
    const float sw = sinf(kTwoPi * t / dur), sw2 = sinf(kTwoPi * t / dur * 2.f + 1.3f);
    r.pelvis = vec3(sx * (0.03f + 0.01f * fem) * s + 0.003f * s * sw, 0.f, -0.008f * s);
    r.pelvisRoll = -sx * (0.05f + 0.035f * fem);
    r.pelvisYaw = -sx * (0.04f + 0.01f * sw2);
    r.spineRoll = -r.pelvisRoll * 1.15f;
    r.spineYaw = -r.pelvisYaw * 0.6f;
    r.spinePitch = 0.01f;
    r.headRoll = -(r.pelvisRoll + r.spineRoll) * 0.8f + 0.012f * sw;
    r.headYaw = 0.03f * sw2;
    r.headPitch = 0.02f;
    const int st = side, fr = 1 - side;
    setFootFlat(A, r.leg[st], vec3(A.ankle[st].x + sx * 0.008f * s, -0.01f * s, 0.f), -sx * 0.1f);
    r.leg[st].knee = normalize(vec3(sx * 0.1f, 1.f, 0.f));
    setFootFlat(A, r.leg[fr], vec3(A.ankle[fr].x - sx * 0.03f * s, 0.055f * s, 0.f), sx * 0.24f);
    r.leg[fr].knee = normalize(vec3(-sx * 0.35f, 1.f, 0.f));
    for (int sd = 0; sd < 2; sd++) {
        // the arm on the standing side swings a little clear of the hip pushed out under it
        float out = (sd == st ? 0.04f : 0.f) + 0.12f + 0.02f * (1.f - fem);
        armFK(r.arm[sd], sd, 0.03f + 0.02f * sinf(kTwoPi * t / dur + sd * 1.3f), out, 0.2f + 0.05f * sd, 0.15f, 0.36f);
    }
}

// Upper-body fidget envelope: rises over `in` s, holds, returns over `outT` s before the end.
static float fidgetEnv(float t, float dur, float in, float outT) { return sstep(0.f, in, t) * (1.f - sstep(dur - outT, dur, t)); }

static void clipStanding(const AuthorCtx& A, int id, float t, Rig& r) {
    const float s = A.D.s;
    const float dur = infoOf(id).duration;
    switch (id) {
        case IC_STAND_L: case IC_STAND_R: standWeight(A, t, dur, r, id == IC_STAND_R ? 1 : 0); break;
        case IC_IDLE_BEHIND: {
            // hands clasped behind the back (the left hand holding the right wrist), chest open
            standPose(A, r);
            float sw = sinf(kTwoPi * t / dur);
            r.pelvis = vec3(0.005f * s * sw, 0.f, -0.006f * s);
            r.spinePitch = -0.035f;
            r.headPitch = 0.02f;
            r.headYaw = 0.12f * sinf(kTwoPi * t / dur + 0.7f);
            float zb = A.D.zHip + 0.07f * s, yb = -(A.D.waistDepth + 0.055f * s);
            armIK(r.arm[1], vec3(0.03f * s, yb, zb), vec3(1.f, -0.6f, 0.1f), 0.45f);
            armIK(r.arm[0], vec3(-0.04f * s, yb - 0.005f * s, zb - 0.015f * s), vec3(-1.f, -0.6f, 0.1f), 0.7f);
            r.arm[1].orient = r.arm[0].orient = true;
            r.arm[1].handRot = handFrame(A, 1, vec3(-0.45f, -0.15f, -0.9f), vec3(0.f, -1.f, 0.25f));
            r.arm[0].handRot = handFrame(A, 0, vec3(0.85f, -0.1f, -0.5f), vec3(0.35f, 0.25f, 0.5f));
            r.arm[0].clavFwd = r.arm[1].clavFwd = -0.05f;
            break;
        }
        case IC_IDLE_CLASP: {
            // hands loosely clasped in front of the lower belly
            standPose(A, r);
            float sw = sinf(kTwoPi * t / dur);
            r.pelvis = vec3(0.005f * s * sw, 0.f, -0.006f * s);
            r.spinePitch = 0.02f;
            r.headPitch = 0.04f;
            float zc = A.D.zHip + 0.01f * s, yc = A.D.waistDepth + 0.085f * s;
            armIK(r.arm[1], vec3(0.045f * s, yc, zc + 0.01f * s), vec3(1.f, -0.5f, -0.3f), 0.55f);
            armIK(r.arm[0], vec3(-0.045f * s, yc - 0.01f * s, zc), vec3(-1.f, -0.5f, -0.3f), 0.55f);
            r.arm[1].orient = r.arm[0].orient = true;
            r.arm[1].handRot = handFrame(A, 1, vec3(-0.75f, 0.35f, -0.55f), vec3(-0.55f, -0.25f, 0.3f));
            r.arm[0].handRot = handFrame(A, 0, vec3(0.75f, 0.35f, -0.5f), vec3(0.55f, -0.2f, 0.35f));
            break;
        }
        case IC_FIDGET_WATCH: {
            // raise the left wrist, turn the watch face up and look at it
            standPose(A, r);
            float e = fidgetEnv(t, dur, 0.4f, 0.5f);
            vec3 c = bonePos(A, r, B_CHEST);
            r.headPitch = 0.34f * e;
            r.neckPitch = 0.12f * e;
            r.headYaw = 0.14f * e;
            r.eyes = vec2(0.05f, -0.18f) * e;
            Rig rest = r;
            armFK(rest.arm[0], 0, 0.03f, 0.12f, 0.2f, 0.15f, 0.36f);
            armIK(r.arm[0], c + vec3(-0.03f, 0.27f, -0.13f) * s, vec3(-1.f, -0.3f, -0.4f), 0.45f);
            r.arm[0].orient = true;
            r.arm[0].handRot = handFrame(A, 0, vec3(0.9f, 0.3f, 0.15f), vec3(0.15f, 0.1f, -1.f));   // palm down: the watch face up
            // blend the arm from hanging to the watch pose through the envelope
            Rig* rr[2] = {&rest, &r};
            unifyRigs(A, rr, 2);
            const ArmCtl* ap[2] = {&rest.arm[0], &r.arm[0]};
            float w[2] = {1.f - e, e};
            combineArm(ap, w, 2, e > 0.5f ? 1 : 0, r.arm[0]);
            armFK(r.arm[1], 1, 0.03f, 0.12f, 0.25f, 0.15f, 0.36f);
            break;
        }
        case IC_FIDGET_SCRATCH: {
            // right hand to the back of the head, fingers scratching, head tipped towards it
            standPose(A, r);
            float e = fidgetEnv(t, dur, 0.45f, 0.55f);
            vec3 hp;
            quat hq;
            r.headPitch = 0.12f * e;
            r.headRoll = 0.1f * e;
            r.spineRoll = -0.03f * e;
            boneOf(A, r, B_HEAD, hp, hq);
            float scr = 0.012f * sinf(kTwoPi * t * 5.f) * sstep(0.5f, 0.7f, t) * (1.f - sstep(dur - 0.8f, dur - 0.55f, t));
            Rig rest = r;
            armFK(rest.arm[1], 1, 0.03f, 0.12f, 0.25f, 0.15f, 0.36f);
            armIK(r.arm[1], hp + rotate(hq, vec3(0.07f, -0.06f + scr, 0.02f) * s), vec3(1.f, 0.1f, 0.4f), 0.55f + 0.1f * sinf(kTwoPi * t * 5.f));
            r.arm[1].orient = true;
            r.arm[1].handRot = hq * handFrame(A, 1, vec3(-0.3f, -0.4f, 1.f), vec3(-1.f, 0.2f, 0.f));
            Rig* rr[2] = {&rest, &r};
            unifyRigs(A, rr, 2);
            const vec3 from = rest.arm[1].target, to = r.arm[1].target;
            const ArmCtl* ap[2] = {&rest.arm[1], &r.arm[1]};
            float w[2] = {1.f - e, e};
            combineArm(ap, w, 2, e > 0.5f ? 1 : 0, r.arm[1]);
            // the hand goes up out to the side and in front of the shoulder, then back to the head (a straight line
            // would run through the ribs and the shoulder)
            vec3 via = A.gh[1] + vec3(0.24f, 0.14f, -0.02f) * s;
            r.arm[1].target = from * ((1.f - e) * (1.f - e)) + via * (2.f * (1.f - e) * e) + to * (e * e);
            armFK(r.arm[0], 0, 0.03f, 0.12f, 0.2f, 0.15f, 0.36f);
            break;
        }
        case IC_FIDGET_TUG: {
            // both hands take the hem at the front and tug it down, then smooth it
            standPose(A, r);
            float e = fidgetEnv(t, dur, 0.4f, 0.5f);
            float pull = sstep(0.55f, 0.8f, t) * (1.f - sstep(1.05f, 1.3f, t));
            r.headPitch = 0.22f * e;
            r.spinePitch = 0.05f * e;
            Rig rest = r;
            for (int sd = 0; sd < 2; sd++) armFK(rest.arm[sd], sd, 0.03f, 0.12f, 0.2f + 0.05f * sd, 0.15f, 0.36f);
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                vec3 tg(sx * 0.1f * s, A.D.waistDepth + 0.055f * s, A.D.zWaist - (0.07f + 0.04f * pull) * s);
                armIK(r.arm[sd], tg, vec3(sx, -0.6f, -0.2f), 0.75f);
                r.arm[sd].orient = true;
                r.arm[sd].handRot = handFrame(A, sd, vec3(-sx * 0.3f, 0.35f, -0.9f), vec3(-sx * 0.2f, -1.f, 0.f));
            }
            Rig* rr[2] = {&rest, &r};
            unifyRigs(A, rr, 2);
            for (int sd = 0; sd < 2; sd++) {
                const ArmCtl* ap[2] = {&rest.arm[sd], &r.arm[sd]};
                float w[2] = {1.f - e, e};
                combineArm(ap, w, 2, e > 0.5f ? 1 : 0, r.arm[sd]);
            }
            break;
        }
        case IC_FIDGET_CHIN: {
            // the right hand to the chin, the left forearm across the waist under the right elbow
            standPose(A, r);
            float e = fidgetEnv(t, dur, 0.5f, 0.6f);
            vec3 hp;
            quat hq;
            r.headPitch = 0.06f * e;
            r.headYaw = -0.08f * e;
            boneOf(A, r, B_HEAD, hp, hq);
            Rig rest = r;
            for (int sd = 0; sd < 2; sd++) armFK(rest.arm[sd], sd, 0.03f, 0.12f, 0.2f + 0.05f * sd, 0.15f, 0.36f);
            armIK(r.arm[1], hp + rotate(hq, vec3(0.015f, 0.1f, -0.16f) * s), vec3(0.7f, 0.1f, -1.f), 0.6f);
            r.arm[1].orient = true;
            r.arm[1].handRot = hq * handFrame(A, 1, vec3(-0.25f, 0.45f, 1.f), vec3(-0.9f, -0.1f, -0.3f));
            armIK(r.arm[0], vec3(0.08f * s, A.skinBelly.y + A.D.rWrist + 0.012f, A.D.zWaist + 0.06f * s), vec3(-1.f, -0.4f, -0.3f), 0.5f);
            r.arm[0].orient = true;
            r.arm[0].handRot = handFrame(A, 0, vec3(1.f, 0.2f, 0.1f), vec3(0.f, -0.2f, 1.f));
            Rig* rr[2] = {&rest, &r};
            unifyRigs(A, rr, 2);
            // the hands travel round the front of the belly and chest, not through them
            const vec3 via[2] = {vec3(-0.06f * s, A.skinBelly.y + 0.12f * s, A.D.zWaist - 0.02f * s),
                                 vec3(0.12f * s, A.skinBelly.y + 0.2f * s, A.D.zChestLine - 0.05f * s)};
            for (int sd = 0; sd < 2; sd++) {
                const vec3 from = rest.arm[sd].target, to = r.arm[sd].target;
                const ArmCtl* ap[2] = {&rest.arm[sd], &r.arm[sd]};
                float w[2] = {1.f - e, e};
                combineArm(ap, w, 2, e > 0.5f ? 1 : 0, r.arm[sd]);
                r.arm[sd].target = from * ((1.f - e) * (1.f - e)) + via[sd] * (2.f * (1.f - e) * e) + to * (e * e);
            }
            break;
        }
        case IC_FIDGET_YAWN: {
            // head back, jaw wide, shoulders up, the right hand over the mouth, eyes squeezed
            standPose(A, r);
            float e = fidgetEnv(t, dur, 0.7f, 0.9f);
            float jaw = sstep(0.4f, 1.0f, t) * (1.f - sstep(1.9f, 2.4f, t));
            r.headPitch = -0.22f * e;
            r.neckPitch = -0.08f * e;
            r.spinePitch = -0.05f * e;
            r.jaw = 0.4f * jaw;
            r.eyes = vec2(0.f, -0.5f * jaw);
            vec3 hp;
            quat hq;
            boneOf(A, r, B_HEAD, hp, hq);
            Rig rest = r;
            for (int sd = 0; sd < 2; sd++) armFK(rest.arm[sd], sd, 0.03f, 0.12f, 0.2f + 0.05f * sd, 0.15f, 0.36f);
            float cover = sstep(0.5f, 0.95f, t) * (1.f - sstep(1.9f, 2.4f, t));
            armIK(r.arm[1], hp + rotate(hq, vec3(0.02f, 0.16f, -0.1f) * s), vec3(0.8f, 0.f, -1.f), 0.35f);
            r.arm[1].orient = true;
            r.arm[1].handRot = hq * handFrame(A, 1, vec3(-0.9f, 0.1f, 0.35f), vec3(0.f, -1.f, 0.f));
            armFK(r.arm[0], 0, 0.05f, 0.14f, 0.3f, 0.15f, 0.36f);
            r.arm[0].clavUp = r.arm[1].clavUp = 0.12f * jaw;
            Rig* rr[2] = {&rest, &r};
            unifyRigs(A, rr, 2);
            const ArmCtl* ap[2] = {&rest.arm[1], &r.arm[1]};
            float w[2] = {1.f - cover, cover};
            combineArm(ap, w, 2, cover > 0.5f ? 1 : 0, r.arm[1]);
            break;
        }
        case IC_FIDGET_ARMS: {
            // fingers laced, arms pushed out in front, then up over the head, back arched a little
            standPose(A, r);
            float e = fidgetEnv(t, dur, 0.6f, 0.7f);
            float up = sstep(1.1f, 1.7f, t) * (1.f - sstep(dur - 1.1f, dur - 0.6f, t));
            vec3 sh = (A.gh[0] + A.gh[1]) * 0.5f;
            r.spinePitch = -0.08f * up * e;
            r.headPitch = -0.1f * up * e;
            r.pelvis = vec3(0.f, 0.f, 0.01f * s * up * e);
            Rig rest = r;
            for (int sd = 0; sd < 2; sd++) armFK(rest.arm[sd], sd, 0.03f, 0.12f, 0.2f + 0.05f * sd, 0.15f, 0.36f);
            vec3 front = sh + vec3(0.f, 0.52f, -0.02f) * s, over = sh + vec3(0.f, 0.08f, 0.55f) * s;
            vec3 hands = lerp(front, over, up);
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                armIK(r.arm[sd], hands + vec3(sx * 0.045f * s, -0.03f * s * (1.f - up), 0.f), vec3(sx, -0.4f, -0.5f), 0.55f);
                r.arm[sd].orient = true;
                vec3 fing = lerp(vec3(-sx * 0.6f, 0.75f, 0.f), vec3(-sx * 0.6f, 0.f, 0.8f), up);
                vec3 palm = lerp(vec3(0.f, 1.f, 0.f), vec3(0.f, 0.f, 1.f), up);
                r.arm[sd].handRot = handFrame(A, sd, fing, palm);
                r.arm[sd].clavUp = 0.1f * up;
            }
            Rig* rr[2] = {&rest, &r};
            unifyRigs(A, rr, 2);
            for (int sd = 0; sd < 2; sd++) {
                const ArmCtl* ap[2] = {&rest.arm[sd], &r.arm[sd]};
                float w[2] = {1.f - e, e};
                combineArm(ap, w, 2, e > 0.5f ? 1 : 0, r.arm[sd]);
            }
            break;
        }
        case IC_FIDGET_TAP: {
            // the right forefoot taps (heel down) about 2.3 times a second
            standPose(A, r);
            float tap = Max(0.f, sinf(kTwoPi * t * 2.3f));
            float pitch = 0.3f * tap * tap;
            LegCtl& l = r.leg[1];
            l.ik = true;
            l.footQ = false;
            l.yaw = -0.18f;
            l.pitch = pitch;
            l.toe = 0.f;
            l.ankle = ankleFromPivotYaw(A, vec3(A.ankle[1].x + 0.02f * s, 0.05f * s, 0.f) - rotate(qz(l.yaw), vec3(0.f, A.heelBack, 0.f)), pitch, l.yaw, 0);
            r.leg[1].knee = normalize(vec3(0.3f, 1.f, 0.f));
            r.pelvis = vec3(-0.015f * s, 0.f, -0.006f * s);
            break;
        }
        default: {   // IC_FIDGET_ROCK: up onto the toes and back down, slowly
            standPose(A, r);
            float k = 0.5f - 0.5f * cosf(kTwoPi * t / (dur / 3.f));
            float pitch = -0.28f * k;
            for (int sd = 0; sd < 2; sd++) {
                float sx = sd ? 1.f : -1.f;
                setFootToes(A, r.leg[sd], A.ankle[sd].x, A.ankle[sd].y + A.ballFwd, pitch, -sx * 0.1f);
            }
            placeHips(A, r, vec3(0.f, 0.f, A.hip[0].z + (A.ballFwd * sinf(-pitch)) - 0.004f * s));
            r.spinePitch = -0.02f * k;
            break;
        }
    }
}

// Carrying (arm layers for AnimInput::carry: only the carrying arm is used). The props are drawn from the fist
// (handGrip): a case trails on its wheels behind the hand, bags hang plumb, a cup and an umbrella's shaft stand up
// from it, so the fists sit where those read naturally.
static void clipCarry(const AuthorCtx& A, int id, float t, Rig& r) {
    standPose(A, r);
    const float s = A.D.s;
    const float sw = sinf(kTwoPi * t / 2.f);   // a slight settle of the load
    switch (id) {
        case IC_CARRY_CASE: {
            // the right arm back and nearly straight, the fist behind the hip gripping the bar across (palm back)
            ArmCtl& a = r.arm[1];
            armIK(a, A.gh[1] + vec3(0.06f, -0.24f, -0.54f) * s, vec3(1.f, 0.4f, 0.f), 0.85f);
            a.orient = true;
            a.handRot = handFrame(A, 1, vec3(0.05f, -0.35f, -1.f), vec3(0.1f, -1.f, 0.3f));
            a.thumb = 0.7f;
            break;
        }
        case IC_CARRY_HANG_L: case IC_CARRY_HANG_R: {
            // a bag or briefcase (a furled umbrella) hanging in the fist: the arm straight and a little out from the
            // thigh, the knuckles out
            int sd = id == IC_CARRY_HANG_R ? 1 : 0;
            armFK(r.arm[sd], sd, 0.02f + 0.01f * sw, 0.24f + 0.03f * (1.f - A.fem), 0.12f, 0.2f, 0.88f);
            r.arm[sd].thumb = 0.75f;
            r.arm[sd].clavUp = -0.02f;   // the shoulder drawn down by the weight
            break;
        }
        case IC_CARRY_CUP_R: case IC_CARRY_CUP_L: {
            // a cup held up in front at the waist, the forearm raised, thumb on top (the cup stands in the fist)
            int sd = id == IC_CARRY_CUP_R ? 1 : 0;
            float sx = sd ? 1.f : -1.f;
            ArmCtl& a = r.arm[sd];
            armIK(a, A.gh[sd] + vec3(-sx * 0.05f, 0.26f, -0.3f) * s + vec3(0.f, 0.f, 0.004f * sw), vec3(sx, -0.6f, -0.3f), 0.75f);
            a.orient = true;
            a.handRot = handFrame(A, sd, vec3(-sx * 0.25f, 1.f, 0.15f), vec3(-sx, 0.1f, 0.f));
            a.thumb = 0.55f;
            break;
        }
        case IC_CARRY_UMBRELLA: {
            // the shaft of an open umbrella in the right fist at chest height in front, the canopy above the head
            ArmCtl& a = r.arm[1];
            armIK(a, A.gh[1] + vec3(-0.08f, 0.24f, -0.22f) * s, vec3(1.f, -0.5f, -0.4f), 0.85f);
            a.orient = true;
            a.handRot = handFrame(A, 1, vec3(-0.3f, 1.f, 0.05f), vec3(-1.f, 0.f, 0.1f));
            a.thumb = 0.7f;
            break;
        }
        case IC_CARRY_ROD: {
            // a fishing rod held up and forward in the right fist at the waist, the butt along the forearm
            ArmCtl& a = r.arm[1];
            armIK(a, A.gh[1] + vec3(-0.02f, 0.3f, -0.36f) * s, vec3(1.f, -0.6f, -0.4f), 0.85f);
            a.orient = true;
            a.handRot = handFrame(A, 1, vec3(-0.15f, 0.55f, -0.8f), vec3(-1.f, 0.f, 0.f));
            a.thumb = 0.8f;
            break;
        }
        default: {   // IC_CARRY_BOARD: the right arm out round a surfboard carried on its rail, the hand at the hip
            armFK(r.arm[1], 1, 0.06f, 0.42f, 0.4f, 0.35f, 0.78f);
            r.arm[1].thumb = 0.6f;
            r.arm[1].clavUp = 0.04f;
            break;
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Greetings between two people (public clips, see pairDistance): authored for a partner of the same body facing this
// one at pairDistance; the animator moves the contact hands / the head onto the real partner (AnimInput::grabTarget).

}  // namespace detail

float pairDistance(Clip c, const Skeleton& a, const Skeleton& b) {
    // chest fronts from the torso radius (~0.65 r), arm reach from the upper arm + forearm
    float ra = a.boneRadius[B_CHEST], rb = b.boneRadius[B_CHEST];
    float feet = a.bindLocalPos[B_TOE_R].y + b.bindLocalPos[B_TOE_R].y + 0.07f;   // the balls of the feet clear
    float armA = length(a.bindLocalPos[B_FOREARM_R]) + length(a.bindLocalPos[B_HAND_R]);
    float armB = length(b.bindLocalPos[B_FOREARM_R]) + length(b.bindLocalPos[B_HAND_R]);
    switch (c) {
        // (hug, kiss: standing this far apart the feet are clear of each other's; the clips step in and lean over)
        case CLIP_HUG: return Max(0.65f * (ra + rb) + 0.16f, feet);          // chests together
        case CLIP_HANDSHAKE: return 0.57f * (armA + armB);                    // hands meeting half way, elbows bent
        case CLIP_CHEEK_KISS: return Max(0.65f * (ra + rb) + 0.16f, feet);   // heads side by side
        default: return 0.f;
    }
}

bool greetingFits(Clip c, const CharacterDesc& a, const CharacterDesc& b) {
    using namespace detail;
    auto wide = [](const CharacterDesc& d) { return d.hat == HAT_SUNHAT || d.hat == HAT_FEDORA; };
    auto peaked = [](const CharacterDesc& d) { return d.hat == HAT_CAP || d.hat == HAT_HARDHAT || d.hat == HAT_POLICE; };
    switch (c) {
        case CLIP_CHEEK_KISS: return !wide(a) && !wide(b) && !peaked(a) && !peaked(b);
        case CLIP_HUG:
            // (the heads go to each one's right: a cap's peak turns away from the partner)
            return !(wide(a) && a.height < b.height + 0.15f) && !(wide(b) && b.height < a.height + 0.15f);
        default: return true;
    }
}

namespace detail {

// How far a greeting is into its contact (0..1): arms round the partner (hug), hands gripped (handshake), cheeks
// together (kiss). The animator weighs its partner fit-up by it.
float pairReach(int c, float t) {
    switch (c) {
        case CLIP_HUG: return sstep(0.25f, 0.85f, t) * (1.f - sstep(2.45f, 3.05f, t));
        case CLIP_HANDSHAKE: return sstep(0.1f, 0.6f, t) * (1.f - sstep(1.65f, 2.2f, t));
        case CLIP_CHEEK_KISS: return sstep(0.1f, 0.6f, t) * (1.f - sstep(1.2f, 1.75f, t));
        default: return 0.f;
    }
}

// A point of the partner (same body, facing this one at distance d) in this one's model space.
static vec3 partnerPt(vec3 p, float d) { return vec3(-p.x, d - p.y, p.z); }

// Arm from its hanging pose to `goal` (an IK arm) by weight e, the hand travelling on a curve through `via` (round the
// partner's body, not through it).
static void reachArm(const AuthorCtx& A, Rig& r, int sd, const ArmCtl& goal, vec3 via, float e) {
    Rig rest = r, tgt = r;
    armFK(rest.arm[sd], sd, 0.03f, 0.12f, 0.2f + 0.05f * sd, 0.15f, 0.36f);
    tgt.arm[sd] = goal;
    Rig* rr[2] = {&rest, &tgt};
    unifyRigs(A, rr, 2);
    const vec3 from = rest.arm[sd].target, to = tgt.arm[sd].target;
    const ArmCtl* ap[2] = {&rest.arm[sd], &tgt.arm[sd]};
    float w[2] = {1.f - e, e};
    combineArm(ap, w, 2, e > 0.5f ? 1 : 0, r.arm[sd]);
    r.arm[sd].target = from * ((1.f - e) * (1.f - e)) + via * (2.f * (1.f - e) * e) + to * (e * e);
}

static void clipGreeting(const AuthorCtx& A, Clip c, float t, Rig& r) {
    const float s = A.D.s;
    const float d = pairDistance(c, A.sk, A.sk);
    const float k = pairReach(c, t);
    standPose(A, r);
    switch (c) {
        case CLIP_HUG: {
            // step in with the right foot, the left one back, and lean over the front foot (the partner does the same,
            // so on either side a forward foot faces a foot that stepped back)
            float lean = sstep(0.1f, 0.75f, t) * (1.f - sstep(2.5f, 3.15f, t));
            setFootFlat(A, r.leg[1], vec3(A.ankle[1].x + 0.035f * s * lean, 0.07f * s * lean, 0.f), -0.1f - 0.1f * lean);
            setFootFlat(A, r.leg[0], vec3(A.ankle[0].x - 0.01f * s * lean, -0.1f * s * lean, 0.f), 0.1f + 0.06f * lean);
            r.leg[0].knee = normalize(vec3(-0.12f, 1.f, 0.f));
            r.leg[1].knee = normalize(vec3(0.15f, 1.f, 0.f));
            // (the body a little to the right as well: each head goes over the partner's left shoulder)
            r.pelvis = vec3(0.03f * s * lean, 0.045f * s * lean, -0.015f * s * lean);
            r.spinePitch = 0.13f * lean;
            // together they turn a little from side to side; the head goes to the right of the partner's
            float hold = sstep(0.85f, 1.1f, t) * (1.f - sstep(2.3f, 2.6f, t));
            float sway = sinf(kTwoPi * 0.62f * (t - 0.95f)) * hold;
            r.spineYaw = 0.08f * sway;
            r.pelvisYaw = 0.035f * sway;
            r.neckYaw = -0.28f * lean;
            r.headYaw = -0.42f * lean;
            r.headPitch = -0.05f * lean;
            r.spineRoll = 0.1f * lean;   // the head to the right, clear of the partner's (and a cap's)
            r.neckRoll = 0.12f * lean;
            r.eyes = vec2(-0.15f, -0.25f) * k;   // eyes turned away, lids low
            // pats with the right hand
            float pat = 0.f;
            const float pats[3] = {1.35f, 1.72f, 2.09f};
            for (float tp : pats) pat = Max(pat, sinf(kPi * Saturate((t - tp) / 0.26f)));
            const float gap = 0.5f * A.D.handT + 0.03f;   // the palm on the partner's back (the shoulder blades bulge)
            // the partner leans in the same way: its skin comes towards this one with the pelvis and the lean
            auto leanIn = [&](vec3 p) { return p - vec3(0.f, (0.045f * s + 0.13f * Max(0.f, p.z - A.pelvisBind.z)) * lean, 0.f); };
            // the right hand over the partner's left shoulder onto the upper back, fingers across it
            vec3 ub = leanIn(partnerPt(A.skinUpperBack, d));
            ArmCtl hi;
            armIK(hi, ub + vec3(0.05f * s, gap + 0.008f + 0.045f * s * pat, 0.06f * s), vec3(1.f, 0.f, 0.6f), 0.4f);
            hi.orient = true;
            hi.handRot = handFrame(A, 1, vec3(-0.85f, 0.f, -0.45f), vec3(0.f, -1.f, 0.f));
            reachArm(A, r, 1, hi, vec3(0.36f * s, 0.45f * d, A.shoulderZ + 0.1f * s), k);
            // the left hand under the partner's right arm onto the lower back
            vec3 lb = leanIn(partnerPt(A.skinWaistBack, d));
            ArmCtl lo;
            armIK(lo, lb + vec3(-0.05f * s, gap, 0.02f * s), vec3(-1.f, -0.2f, -0.5f), 0.4f);
            lo.orient = true;
            lo.handRot = handFrame(A, 0, vec3(0.85f, 0.f, -0.35f), vec3(0.f, -1.f, 0.f));
            reachArm(A, r, 0, lo, vec3(-0.38f * s, 0.55f * d, A.D.zWaist + 0.03f * s), k);
            break;
        }
        case CLIP_HANDSHAKE: {
            // the right hands meet half way at about the waist, palms facing, thumbs up; two pumps; a nod
            float pump = sinf(kTwoPi * 2.2f * (t - 0.75f)) * sstep(0.72f, 0.82f, t) * (1.f - sstep(1.3f, 1.45f, t));
            vec3 M(0.f, 0.5f * d, 0.83f * A.chestP.z);
            ArmCtl g;
            armIK(g, M + vec3(0.02f * s, -0.055f * s, 0.028f * s * pump), vec3(1.f, -0.6f, -0.4f), 0.55f);
            g.orient = true;
            g.handRot = handFrame(A, 1, vec3(-0.25f, 1.f, -0.12f), vec3(-1.f, 0.1f, 0.f));
            g.thumb = 0.45f;
            reachArm(A, r, 1, g, M + vec3(0.12f * s, -0.12f * s, -0.12f * s), k);
            r.spinePitch = 0.05f * k;
            r.headPitch = 0.02f * k + 0.1f * sinf(kPi * Saturate((t - 0.75f) / 0.5f));
            break;
        }
        default: {   // CLIP_CHEEK_KISS
            // lean in over the right foot, the left one stepped back (as in the hug), the head to the left so the right
            // cheeks meet; the right hand on the partner's left upper arm
            setFootFlat(A, r.leg[1], vec3(A.ankle[1].x + 0.03f * s * k, 0.06f * s * k, 0.f), -0.1f - 0.08f * k);
            setFootFlat(A, r.leg[0], vec3(A.ankle[0].x - 0.01f * s * k, -0.1f * s * k, 0.f), 0.1f + 0.05f * k);
            r.leg[0].knee = normalize(vec3(-0.12f, 1.f, 0.f));
            r.leg[1].knee = normalize(vec3(0.15f, 1.f, 0.f));
            r.spinePitch = 0.17f * k;
            r.spineRoll = -0.13f * k;
            r.spineYaw = 0.06f * k;
            r.neckPitch = 0.2f * k;
            r.neckYaw = 0.2f * k;
            r.headYaw = 0.22f * k;
            r.headPitch = 0.04f * k;
            r.pelvis = vec3(0.01f * s * k, 0.045f * s * k, -0.01f * s * k);
            r.eyes = vec2(0.1f, -0.3f) * k;
            // outside of the partner's left upper arm (the partner leans in too)
            vec3 arm = partnerPt(A.gh[0], d) + vec3(0.06f * s, -(0.045f * s + 0.17f * (A.gh[0].z - A.pelvisBind.z)) * k, -0.13f * s);
            ArmCtl h;
            armIK(h, arm + vec3(0.f, -0.06f * s, 0.f), vec3(1.f, -0.4f, -0.6f), 0.3f);
            h.orient = true;
            h.handRot = handFrame(A, 1, vec3(-0.15f, 0.5f, -0.85f), vec3(-0.9f, 0.3f, 0.f));
            reachArm(A, r, 1, h, arm + vec3(0.1f * s, -0.15f * s, -0.1f * s), k);
            break;
        }
    }
}

// Rifle held at the low ready while not aiming (arms only; used as an arm layer by the animator).
static void rifleCarryPose(const AuthorCtx& A, float t, Rig& r) {
    standPose(A, r);
    const float s = A.D.s;
    float br = sinf(kTwoPi * t / 2.f);
    vec3 shR = bonePos(A, r, B_UPPERARM_R), shL = bonePos(A, r, B_UPPERARM_L);
    vec3 grip(shR.x - 0.04f * s, shR.y + 0.2f * s, shR.z - 0.46f * s + 0.004f * br);
    vec3 fore(shL.x + 0.2f * s, shL.y + 0.36f * s, shL.z - 0.3f * s + 0.004f * br);
    armIK(r.arm[1], grip, vec3(1, -0.6f, 0.1f), 0.85f);
    armIK(r.arm[0], fore, vec3(-1, -0.5f, -0.4f), 0.75f);
    r.arm[1].orient = true;
    r.arm[1].handRot = handFrame(A, 1, vec3(-0.25f, 0.65f, -0.7f), vec3(-1, 0.f, 0.1f));
    r.arm[0].orient = true;
    r.arm[0].handRot = handFrame(A, 0, vec3(0.5f, 0.8f, -0.2f), vec3(0.3f, 0.f, 1.f));
}

static void authorClip(const AuthorCtx& A, int id, float t, Rig& r) {
    if (id >= CLIP_COUNT) {
        switch (id) {
            case IC_RIFLE_CARRY: rifleCarryPose(A, t, r); break;
            case IC_GUARD: case IC_GUARD_KNIFE: case IC_GUARD_BAT: case IC_BLOCK_BAT: clipGuardLoop(A, id, t, r); break;
            case IC_IDLE_CROSSARMS: case IC_IDLE_POCKETS: case IC_IDLE_HIP: case IC_IDLE_PHONE: case IC_IDLE_STRETCH: case IC_DANCE2:
            case IC_DANCE3: case IC_DANCE4: case IC_SIT_GROUND: case IC_LIE_FRONT: clipAmbient(A, id, t, r); break;
            case IC_JOG_SLOW: clipLocomotion(A, id, t, r); break;
            case IC_STAND_L: case IC_STAND_R: case IC_IDLE_BEHIND: case IC_IDLE_CLASP: case IC_FIDGET_WATCH: case IC_FIDGET_SCRATCH:
            case IC_FIDGET_TUG: case IC_FIDGET_CHIN: case IC_FIDGET_YAWN: case IC_FIDGET_ARMS: case IC_FIDGET_TAP: case IC_FIDGET_ROCK:
                clipStanding(A, id, t, r);
                break;
            case IC_CARRY_CASE: case IC_CARRY_HANG_L: case IC_CARRY_HANG_R: case IC_CARRY_CUP_R: case IC_CARRY_CUP_L: case IC_CARRY_UMBRELLA:
            case IC_CARRY_ROD: case IC_CARRY_BOARD:
                clipCarry(A, id, t, r);
                break;
            default:
                if (id >= IC_GAIT_FIRST && id <= IC_GAIT_LAST) clipLocomotion(A, id, t, r);
                else standPose(A, r);
                break;
        }
        return;
    }
    Clip c = (Clip)id;
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
        case CLIP_DEATH_FRONT: case CLIP_DEATH_BACK: clipDeath(A, c, t, r); break;
        case CLIP_SIT_DRIVE: drivePose(A, r, t); break;
        case CLIP_SIT_PASSENGER: passengerPose(A, r, t, ci.duration); break;
        case CLIP_RIDE_BIKE: bikePose(A, r, t); break;
        case CLIP_ENTER_CAR_L: case CLIP_EXIT_CAR_L: case CLIP_ENTER_CAR_R: case CLIP_EXIT_CAR_R: clipCar(A, c, t, r); break;
        case CLIP_SWIM_IDLE: case CLIP_SWIM: clipSwim(A, c, t, r); break;
        case CLIP_CLIMB: case CLIP_VAULT: clipClimbVault(A, c, t, r); break;
        case CLIP_GET_UP_FRONT: case CLIP_GET_UP_BACK: clipGetUp(A, c, t, r); break;
        case CLIP_HOOK: case CLIP_UPPERCUT: case CLIP_COUNTER: clipFists(A, c, t, r); break;
        case CLIP_HUG: case CLIP_HANDSHAKE: case CLIP_CHEEK_KISS: clipGreeting(A, c, t, r); break;
        case CLIP_KNIFE_SLASH: case CLIP_KNIFE_STAB: clipKnife(A, c, t, r); break;
        case CLIP_BAT_SWING: case CLIP_BAT_OVERHEAD: clipBat(A, c, t, r); break;
        case CLIP_DODGE_BACK: case CLIP_DODGE_L: case CLIP_DODGE_R: clipDodge(A, c, t, r); break;
        case CLIP_HIT_HEAD: case CLIP_HIT_BODY: case CLIP_KNOCKOUT: clipMeleeHit(A, c, t, r); break;
        case CLIP_TAKEDOWN_ATTACKER: takedownAttacker(A, t, r); break;
        case CLIP_TAKEDOWN_VICTIM: takedownVictim(A, t, r); break;
        default: clipSocial(A, c, t, ci.duration, r); break;
    }
}

// ------------------------------------------------------------------------------------------------
// Baking and sampling

enum RootMode : u8 { RM_SCALE = 0, RM_HIP, RM_PELVIS, RM_HEAD };

static RootMode rootMode(int c) {
    switch (c) {
        case CLIP_SIT_DRIVE: case CLIP_SIT_PASSENGER: case CLIP_RIDE_BIKE: case CLIP_SIT_BENCH: return RM_HIP;
        case CLIP_SWIM: return RM_PELVIS;
        case CLIP_SWIM_IDLE: return RM_HEAD;
        default: return RM_SCALE;
    }
}

struct BakedClip {
    int frames = 0;
    float fps = 30.f;
    std::vector<quat> rot;   // frames * B_COUNT
    std::vector<vec3> root;
};

struct ClipLib {
    AuthorCtx ctx[2];                                // male / female reference
    BakedClip clips[2][CLIP_COUNT + kExtraCount];    // [style][clip id]
    float duty[CLIP_COUNT + kExtraCount];            // stance fraction of the locomotion clips (0: not a gait)
};

static void makeAuthorCtx(AuthorCtx& A, bool female) {
    CharacterDesc d;
    d.seed = 12345;
    d.gender = female ? FEMALE : MALE;
    d.height = female ? 1.65f : 1.78f;
    d.weight = 0.45f;
    d.muscle = female ? 0.3f : 0.45f;
    d.age = 0.3f;
    d.shoes = 0;
    computeDims(d, A.D);
    buildSkeleton(d, A.sk);
    const vec3* J = A.D.J;
    A.footH = J[B_FOOT_L].z;
    A.legLen = length(A.sk.bindLocalPos[B_CALF_L]) + length(A.sk.bindLocalPos[B_FOOT_L]);
    for (int s = 0; s < 2; s++) {
        int o = s ? 4 : 0;
        A.hip[s] = J[B_THIGH_L + o];
        A.ankle[s] = J[B_FOOT_L + o];
        A.gh[s] = J[B_UPPERARM_L + o];
    }
    A.pelvisBind = J[B_PELVIS];
    A.hipMidLocal = (A.sk.bindLocalPos[B_THIGH_L] + A.sk.bindLocalPos[B_THIGH_R]) * 0.5f;
    A.heelBack = A.D.heelBack;
    A.ballFwd = J[B_TOE_L].y - J[B_FOOT_L].y;
    A.toeFwd = A.D.toeFwd;
    A.fem = female ? 1.f : 0.f;
    A.chestP = J[B_CHEST];
    A.headP = J[B_HEAD];
    A.shoulderZ = J[B_UPPERARM_R].z;
    A.thighLen = length(A.sk.bindLocalPos[B_CALF_L]);
    A.shinLen = length(A.sk.bindLocalPos[B_FOOT_L]);
    {
        // the body's signed distance model (torso and legs): rays out from inside find the skin
        BuildCtx bc;
        bc.d = &d;
        bc.D = &A.D;
        bc.sk = &A.sk;
        addBodyPrims(bc);
        const u32 mk = MK_TORSO | MK_LEG_L | MK_LEG_R;
        const float s = A.D.s;
        auto skin = [&](vec3 from, vec3 dir) { return from + dir * bc.sdf.castOut(from, dir, mk, 0.5f); };
        A.skinFlankR = skin(vec3(0.f, A.hip[1].y + 0.015f * s, A.hip[1].z + 0.1f * s), vec3(1, 0, 0));
        A.skinBack = skin(vec3(0.f, 0.f, A.D.zHip + 0.07f * s), vec3(0, -1, 0));
        A.skinBelly = skin(vec3(0.06f * s, 0.f, A.D.zWaist + 0.05f * s), vec3(0, 1, 0));
        A.skinUpperBack = skin(vec3(0.f, 0.f, J[B_CHEST].z), vec3(0, -1, 0));
        A.skinWaistBack = skin(vec3(0.f, 0.f, A.D.zWaist), vec3(0, -1, 0));
    }
}

static bool styleDependent(int c) {
    switch (c) {
        case CLIP_IDLE: case CLIP_IDLE_LOOK: case CLIP_WALK: case CLIP_JOG: case CLIP_RUN: case CLIP_SPRINT: case CLIP_WALK_BACK:
        case CLIP_STRAFE_L: case CLIP_STRAFE_R: case CLIP_TALK: case CLIP_TALK_PHONE: case CLIP_SMOKE: case CLIP_DANCE: case CLIP_FLEE:
        case IC_IDLE_CROSSARMS: case IC_IDLE_POCKETS: case IC_IDLE_HIP: case IC_IDLE_PHONE: case IC_IDLE_STRETCH: case IC_DANCE2:
        case IC_DANCE3: case IC_DANCE4: case IC_JOG_SLOW: case IC_STAND_L: case IC_STAND_R: case IC_IDLE_BEHIND: case IC_IDLE_CLASP:
        case IC_FIDGET_WATCH: case IC_FIDGET_SCRATCH: case IC_FIDGET_TUG: case IC_FIDGET_CHIN: case IC_FIDGET_YAWN: case IC_FIDGET_ARMS:
        case IC_FIDGET_TAP: case IC_FIDGET_ROCK: case IC_CARRY_CASE: case IC_CARRY_HANG_L: case IC_CARRY_HANG_R: case IC_CARRY_CUP_R:
        case IC_CARRY_CUP_L: case IC_CARRY_UMBRELLA: case IC_CARRY_ROD: case IC_CARRY_BOARD: case CLIP_HUG: case CLIP_HANDSHAKE:
        case CLIP_CHEEK_KISS:
            return true;
        default: return c >= IC_GAIT_FIRST && c <= IC_GAIT_LAST;
    }
}

static void bakeClip(const AuthorCtx& A, int c, BakedClip& out) {
    const ClipInfo& ci = infoOf(c);
    int n = ci.loop ? Max(2, (int)lrintf(ci.duration * out.fps)) : Max(2, (int)ceilf(ci.duration * out.fps) + 1);
    out.frames = n;
    out.rot.resize((size_t)n * B_COUNT);
    out.root.resize(n);
    Pose p;
    float twPrev[2] = {0.f, 0.f}, twNow[2] = {0.f, 0.f};
    for (int f = 0; f < n; f++) {
        float t = ci.loop ? ci.duration * f / n : Min(ci.duration, f / out.fps);
        Rig r;
        authorClip(A, c, t, r);
        // forearm twist winding follows the previous frame (no half-turn flips when a grip passes the wrap)
        for (int s = 0; s < 2; s++) twNow[s] = r.arm[s].twist;
        if (f > 0)
            for (int s = 0; s < 2; s++) twPrev[s] = r.arm[s].ik ? twPrev[s] : r.arm[s].twist;
        rigToPose(A, r, p, f > 0 ? twPrev : nullptr, twNow);
        for (int s = 0; s < 2; s++) twPrev[s] = twNow[s];
        for (int b = 0; b < B_COUNT; b++) {
            quat q = normalize(p.rot[b]);
            if (f > 0 && dot(q, out.rot[(size_t)(f - 1) * B_COUNT + b]) < 0.f) q = quat(-q.x, -q.y, -q.z, -q.w);
            out.rot[(size_t)f * B_COUNT + b] = q;
        }
        // clips with root motion are stored in place relative to the moving origin
        out.root[f] = p.rootOffset - rootMotionRef(c, t) * (A.D.s);
    }
}

const ClipInfo& clipInfoId(int id) { return infoOf(Clamp(id, 0, CLIP_COUNT + kExtraCount - 1)); }

static ClipLib* buildLib() {
    ClipLib* L = new ClipLib();
    makeAuthorCtx(L->ctx[0], false);
    makeAuthorCtx(L->ctx[1], true);
    L->ctx[1].strideScale = L->ctx[1].legLen / Max(L->ctx[0].legLen, 1e-3f);
    for (int c = 0; c < CLIP_COUNT + kExtraCount; c++) {
        bakeClip(L->ctx[0], c, L->clips[0][c]);
        if (styleDependent(c)) bakeClip(L->ctx[1], c, L->clips[1][c]);
        GaitP g;
        L->duty[c] = gaitParams(c, g) ? g.duty : 0.f;
    }
    return L;
}

const ClipLib& clipLib() {
    static ClipLib* lib = buildLib();
    return *lib;
}

float clipDuty(int id) { return id >= 0 && id < CLIP_COUNT + kExtraCount ? clipLib().duty[id] : 0.f; }

// Femininity of a skeleton's proportions (hip joint spacing relative to shoulder spacing).
float skeletonStyle(const Skeleton& sk) {
    float hip = fabsf(sk.bindLocalPos[B_THIGH_L].x);
    vec3 ua = sk.bindLocalPos[B_CLAVICLE_L] + sk.bindLocalPos[B_UPPERARM_L];
    float sh = fabsf(ua.x);
    float ratio = hip / Max(sh, 1e-3f);
    return Saturate((ratio - 0.52f) / 0.05f);
}

static float skelLegLen(const Skeleton& sk) { return length(sk.bindLocalPos[B_CALF_L]) + length(sk.bindLocalPos[B_FOOT_L]); }

// Leg length of a skeleton relative to the male reference: locomotion strides (ClipInfo::speed * duration, for the
// male reference) scale with it for both styles (the female bakes cover the same stride per leg length).
float skeletonLegScale(const Skeleton& sk) {
    const ClipLib& L = clipLib();
    return skelLegLen(sk) / Max(skelLegLen(L.ctx[0].sk), 1e-3f);
}

// Hanging hands pass the body at hip height, where it is widest at the hips or the outer thighs: an abduction that
// gives a wide body the reference body's clearance there (relative to the shoulder joints the arms hang from).
float skeletonArmClearance(const Skeleton& sk) {
    const ClipLib& L = clipLib();
    const Skeleton& R = L.ctx[skeletonStyle(sk) > 0.5f ? 1 : 0].sk;
    auto excess = [](const Skeleton& s) {
        float hips = s.boneRadius[B_PELVIS] / 0.9f;
        float thighs = fabsf(s.bindLocalPos[B_THIGH_L].x) + s.boneRadius[B_THIGH_L] / 0.85f;
        float shoulder = fabsf((s.bindLocalPos[B_CLAVICLE_L] + s.bindLocalPos[B_UPPERARM_L]).x);
        return Max(hips, thighs) - shoulder;
    };
    float more = excess(sk) - excess(R);
    float arm = length(sk.bindLocalPos[B_FOREARM_L]) + length(sk.bindLocalPos[B_HAND_L]);
    return Clamp(atan2f(Max(0.f, more) * 1.15f, arm * 0.9f), 0.f, 0.28f);
}

// The two baked frames around time t and the weight of the second.
static void bakedFrames(const BakedClip& bc, const ClipInfo& ci, float t, int& i0, int& i1, float& f) {
    if (ci.loop) {
        float tt = t - floorf(t / ci.duration) * ci.duration;
        f = tt / ci.duration * (float)bc.frames;
        i0 = (int)f;
        f -= (float)i0;
        i0 = i0 % bc.frames;
        if (i0 < 0) i0 += bc.frames;
        i1 = (i0 + 1) % bc.frames;
    } else {
        float tt = Clamp(t, 0.f, ci.duration);
        f = tt * bc.fps;
        i0 = Min((int)f, bc.frames - 1);
        f -= (float)i0;
        i1 = Min(i0 + 1, bc.frames - 1);
        if (i0 == bc.frames - 1) f = 0.f;
    }
    f = Saturate(f);
}

static void sampleBaked(const BakedClip& bc, const ClipInfo& ci, float t, Pose& out) {
    float f;
    int i0, i1;
    bakedFrames(bc, ci, t, i0, i1, f);
    const quat* a = &bc.rot[(size_t)i0 * B_COUNT];
    const quat* b = &bc.rot[(size_t)i1 * B_COUNT];
    // the derived bones' rotations come from their controllers (computeMatrices ignores these entries): copied
    for (int k = 0; k < B_FIRST_DERIVED; k++) out.rot[k] = nlerp(a[k], b[k], f);
    for (int k = B_FIRST_DERIVED; k < B_COUNT; k++) out.rot[k] = a[k];
    out.rootOffset = lerp(bc.root[i0], bc.root[i1], f);
}

}  // namespace detail

const ClipInfo& clipInfo(Clip c) {
    int i = Clamp((int)c, 0, CLIP_COUNT - 1);
    return detail::kClipInfo[i];
}

namespace detail {
void sampleClipId(const Skeleton& skel, int ci, float t, Pose& out, u32 variationSeed);
}

void sampleClip(const Skeleton& skel, Clip c, float t, Pose& out, u32 variationSeed) {
    detail::sampleClipId(skel, Clamp((int)c, 0, CLIP_COUNT - 1), t, out, variationSeed);
}

float clipEventTime(Clip c) {
    switch (c) {
        case CLIP_PUNCH_L: return 0.17f;
        case CLIP_PUNCH_R: return 0.23f;
        case CLIP_KICK: return 0.35f;
        case CLIP_THROW: return 0.56f;
        case CLIP_ENTER_CAR_L: case CLIP_ENTER_CAR_R: return 0.22f;
        case CLIP_HOOK: return 0.42f;
        case CLIP_UPPERCUT: return 0.3f;
        case CLIP_BAT_SWING: return 0.5f;
        case CLIP_BAT_OVERHEAD: return 0.62f;
        case CLIP_KNIFE_SLASH: return 0.2f;
        case CLIP_KNIFE_STAB: return 0.32f;
        case CLIP_COUNTER: return 0.32f;
        case CLIP_TAKEDOWN_ATTACKER: case CLIP_TAKEDOWN_VICTIM: return detail::kTakedownGrab;
        case CLIP_HUG: return 0.85f;          // arms closed round the partner
        case CLIP_HANDSHAKE: return 0.6f;     // hands gripped
        case CLIP_CHEEK_KISS: return 0.6f;    // cheeks together
        default: return -1.f;
    }
}

vec3 clipRootMotion(const Skeleton& skel, Clip c, float t) {
    using namespace detail;
    int ci = Clamp((int)c, 0, CLIP_COUNT - 1);
    const ClipInfo& info = kClipInfo[ci];
    const ClipLib& L = clipLib();
    // the clips with root motion are baked on the male reference (not style dependent)
    float ls = skelLegLen(skel) / Max(skelLegLen(L.ctx[0].sk), 1e-3f);
    return rootMotionRef(ci, Clamp(t, 0.f, info.duration)) * (L.ctx[0].D.s * ls);
}

void phoneFrame(const Skeleton& skel, const mat4* modelSpace, vec3& pos, vec3& longAxis, vec3& screen) {
    // palm centres and frames of both hands (bind axes: fingers along B_FINGERS, palm normals as in skeleton.cpp)
    vec3 pc[2], fd[2], pn[2];
    for (int s = 0; s < 2; s++) {
        int hb = s ? B_HAND_R : B_HAND_L, fb = s ? B_FINGERS_R : B_FINGERS_L;
        vec3 fl = skel.bindLocalPos[fb];
        float pl = length(fl);
        vec3 fing = pl > 1e-5f ? fl / pl : vec3(0, 0, -1);
        vec3 palmB = normalize(s ? cross(vec3(0, 1, 0), fing) : cross(fing, vec3(0, 1, 0)));
        const mat4& m = modelSpace[hb];
        vec3 cx = m.c[0].xyz(), cy = m.c[1].xyz(), cz = m.c[2].xyz();
        auto dirOf = [&](vec3 v) { return normalize(cx * v.x + cy * v.y + cz * v.z); };
        fd[s] = dirOf(fing);
        pn[s] = dirOf(palmB);
        pc[s] = m.c[3].xyz() + fd[s] * (0.55f * pl) + pn[s] * (0.16f * pl);
    }
    // the phone lies on the right palm (back against it), shifted a little towards the fingers
    pos = pc[1] + pn[1] * 0.006f + fd[1] * 0.02f;
    longAxis = fd[1];
    screen = pn[1];
    // held between both hands: centre between the palms, screen normal averaged
    float d = length(pc[0] - pc[1]);
    if (d < 0.14f) {
        float w = 1.f - detail::sstep(0.09f, 0.14f, d);
        vec3 mid = (pc[0] + pc[1]) * 0.5f + normalize(pn[0] + pn[1] + vec3(0, 0, 1e-3f)) * 0.006f;
        pos = lerp(pos, mid, 0.5f * w);
        screen = normalize(lerp(screen, normalize(pn[0] + pn[1] + vec3(0, 0, 1e-3f)), 0.5f * w));
    }
    longAxis = normalize(longAxis - screen * dot(longAxis, screen));
}

void handGrip(const Skeleton& skel, const mat4* modelSpace, bool right, vec3& pos, vec3& axis, vec3& palm) {
    int hb = right ? B_HAND_R : B_HAND_L, fb = right ? B_FINGERS_R : B_FINGERS_L;
    vec3 fl = skel.bindLocalPos[fb];
    float pl = length(fl);
    vec3 fing = pl > 1e-5f ? fl / pl : vec3(0, 0, -1);
    // bind palm normals (see skeleton.cpp): right = Y x fingers, left = fingers x Y
    vec3 pn = normalize(right ? cross(vec3(0, 1, 0), fing) : cross(fing, vec3(0, 1, 0)));
    const mat4& m = modelSpace[hb];
    vec3 cx = m.c[0].xyz(), cy = m.c[1].xyz(), cz = m.c[2].xyz();
    auto dirOf = [&](vec3 v) { return normalize(cx * v.x + cy * v.y + cz * v.z); };
    vec3 f = dirOf(fing), p = dirOf(pn);
    axis = dirOf(vec3(0, 1, 0));
    palm = p;
    pos = m.c[3].xyz() + f * (detail::kGripAlong * pl) + p * (detail::kGripPalm * pl);
}

// Sample a public or internal clip id (see IC_*).
void detail::sampleClipId(const Skeleton& skel, int ci, float t, Pose& out, u32 variationSeed) {
    using namespace detail;
    const ClipLib& L = clipLib();
    ci = Clamp(ci, 0, CLIP_COUNT + kExtraCount - 1);
    const ClipInfo& info = infoOf(ci);
    int st = styleDependent(ci) && skeletonStyle(skel) > 0.5f ? 1 : 0;
    sampleBaked(L.clips[st][ci], info, t, out);
    const AuthorCtx& R = L.ctx[st];
    // root offset for this skeleton: scaled by the leg length, or keeping an absolute height
    float ls = skelLegLen(skel) / Max(skelLegLen(R.sk), 1e-3f);
    RootMode rm = rootMode(ci);
    if (rm == RM_SCALE) {
        out.rootOffset = out.rootOffset * ls;
    } else {
        out.rootOffset.x *= ls;
        out.rootOffset.y *= ls;
        float zs = skel.bindLocalPos[B_ROOT].z + skel.bindLocalPos[B_PELVIS].z, zr = R.sk.bindLocalPos[B_ROOT].z + R.sk.bindLocalPos[B_PELVIS].z;
        if (rm == RM_HIP) {
            zs += skel.bindLocalPos[B_THIGH_L].z;
            zr += R.sk.bindLocalPos[B_THIGH_L].z;
        } else if (rm == RM_HEAD) {
            for (int b = B_SPINE1; b <= B_HEAD; b++) {
                zs += skel.bindLocalPos[b].z;
                zr += R.sk.bindLocalPos[b].z;
            }
        }
        out.rootOffset.z += zr - zs;
    }
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

// One leg's bones (thigh, calf, foot, toe; side 0 left, 1 right) of a clip at time t, over those in out: a gait clip
// whose legs are warped in time each on its own (sampleGait in the animator).
void detail::sampleClipLeg(const Skeleton& skel, int ci, float t, int side, Pose& out) {
    using namespace detail;
    const ClipLib& L = clipLib();
    ci = Clamp(ci, 0, CLIP_COUNT + kExtraCount - 1);
    const ClipInfo& info = infoOf(ci);
    int st = styleDependent(ci) && skeletonStyle(skel) > 0.5f ? 1 : 0;
    const BakedClip& bc = L.clips[st][ci];
    float f;
    int i0, i1;
    bakedFrames(bc, info, t, i0, i1, f);
    const quat* a = &bc.rot[(size_t)i0 * B_COUNT];
    const quat* b = &bc.rot[(size_t)i1 * B_COUNT];
    static const int kLeg[2][4] = {{B_THIGH_L, B_CALF_L, B_FOOT_L, B_TOE_L}, {B_THIGH_R, B_CALF_R, B_FOOT_R, B_TOE_R}};
    for (int k : kLeg[side ? 1 : 0]) out.rot[k] = nlerp(a[k], b[k], f);
}

}  // namespace Anim
