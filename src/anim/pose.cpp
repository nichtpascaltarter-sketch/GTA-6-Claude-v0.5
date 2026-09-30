// Runtime pose math: model/skinning matrices, pose blending, two-bone IK. Allocation free.
#include "anim_internal.h"

namespace Anim {

namespace detail {

// Per-bone weight used by blendUpperBody (0 = keep base, 1 = take layer).
static const float kUpperMask[B_COUNT] = {
    0.f,                 // ROOT
    0.f,                 // PELVIS
    0.55f, 0.85f, 1.f,   // SPINE1, SPINE2, CHEST
    1.f, 1.f,            // NECK, HEAD
    1.f, 1.f, 1.f, 1.f,  // arm L
    1.f, 1.f, 1.f, 1.f,  // arm R
    0.f, 0.f, 0.f, 0.f,  // leg L
    0.f, 0.f, 0.f, 0.f,  // leg R
    1.f, 1.f, 1.f, 1.f,  // fingers/thumbs
    1.f, 1.f, 1.f,       // jaw, eyes
    1.f, 1.f, 1.f, 1.f, 1.f,   // lips, tongue
    1.f, 1.f,                  // brows
    // derived bones (roll, phalanges): computed from their controllers, the entries are never read
    1.f, 1.f,
    1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
    1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f,
};

// Rotation angle of q about a unit axis (swing-twist decomposition), in [-pi, pi].
static FORCEINLINE float twistAbout(quat q, vec3 a) {
    float p = q.x * a.x + q.y * a.y + q.z * a.z, w = q.w;
    if (w < 0.f) {
        p = -p;
        w = -w;
    }
    return 2.f * atan2f(p, w);
}

// Piecewise-linear key table lookup: y at x for keys (xs[i], ys[i]), extrapolated below the first key by its slope
// (hyperextension) and held above the last one.
static FORCEINLINE float keyed(const float* xs, const float* ys, int n, float x) {
    if (x <= xs[0]) return ys[0] + (x - xs[0]) * (ys[1] - ys[0]) / (xs[1] - xs[0]);
    for (int i = 1; i < n; i++)
        if (x <= xs[i]) return Lerp(ys[i - 1], ys[i], (x - xs[i - 1]) / (xs[i] - xs[i - 1]));
    return ys[n - 1];
}

// Local rotations of the derived bones (character.h), out[b - B_FIRST_DERIVED]:
//  - forearm roll = half of the hand's twist about the forearm axis;
//  - finger phalanges from the curl controller's angle about the fingers' flexion axis (the clips' and holdGrip's
//    convention: fingers 0..1 -> 1.45 * fingers rad), per finger and joint from a key table on c = fingers: open at 0,
//    a relaxed cascade at 0.35 (the ulnar fingers curl more), wrapped round a ~3 cm handle at holdGrip's fist centre
//    at 0.9 (the short pinky and index curl less there) and a closed fist at 1. The flexion axes lean so curling
//    fingers converge towards the thumb's base;
//  - thumb phalanges: aimed at key directions in the hand frame on the thumb controller (B_THUMB's angle about its
//    opposition axis, thumb 0..1 -> 0.9 rad): open, relaxed beside the index finger, along a handle's far side above
//    the index finger (grips, 0.55), across the closed fingers (fist, 1).
static void derivedLocals(const Skeleton& sk, const Pose& pose, quat* out) {
    const vec3 Y(0, 1, 0);
    // finger joint flexion keys (rad, added to the bind rest curl): [finger][joint][key] at c = kC[key]
    static const float kC[4] = {0.f, 0.35f, 0.9f, 1.f};
    static const float kFlex[4][3][4] = {
        {{0.f, 0.42f, 1.2f, 1.45f}, {0.f, 0.4f, 0.92f, 1.5f}, {0.f, 0.28f, 0.81f, 1.05f}},    // index
        {{0.f, 0.48f, 1.3f, 1.5f}, {0.f, 0.44f, 1.0f, 1.6f}, {0.f, 0.3f, 0.9f, 1.1f}},       // middle
        {{0.f, 0.5f, 1.24f, 1.52f}, {0.f, 0.5f, 0.83f, 1.62f}, {0.f, 0.34f, 0.97f, 1.1f}},   // ring
        {{0.f, 0.52f, 0.9f, 1.55f}, {0.f, 0.56f, 0.66f, 1.62f}, {0.f, 0.4f, 0.8f, 1.1f}},    // pinky
    };
    static const float kLean[4] = {0.0f, 0.04f, 0.09f, 0.15f};   // flexed fingers lean towards the thumb's base
    // thumb key directions in the hand frame (fingers, thumb side, palm) for the metacarpal, the proximal and the distal
    // phalanx at thumb = kCt (a zero vector = the bind direction)
    static const float kCt[4] = {0.f, 0.2f, 0.55f, 1.f};
    static const float kTM[4][3] = {{0.f, 0.f, 0.f}, {0.62f, 0.62f, 0.48f}, {0.59f, 0.46f, 0.66f}, {0.55f, 0.13f, 0.82f}};
    static const float kT1[4][3] = {{0.f, 0.f, 0.f}, {0.95f, 0.12f, 0.05f}, {0.93f, 0.1f, 0.35f}, {0.93f, -0.3f, 0.2f}};
    static const float kT2[4][3] = {{0.f, 0.f, 0.f}, {0.97f, 0.f, 0.12f}, {0.97f, 0.f, 0.25f}, {0.87f, -0.49f, 0.f}};
    for (int side = 0; side < 2; side++) {
        const bool right = side == 1;
        const int hb = right ? B_HAND_R : B_HAND_L, fb = right ? B_FINGERS_R : B_FINGERS_L, tb = right ? B_THUMB_R : B_THUMB_L;
        vec3 fax = normalize(sk.bindLocalPos[hb]);
        out[(right ? B_FOREARM_ROLL_R : B_FOREARM_ROLL_L) - B_FIRST_DERIVED] = qaa(fax, 0.5f * twistAbout(pose.rot[hb], fax));
        vec3 fing = normalize(sk.bindLocalPos[fb]);
        vec3 pn = normalize(right ? cross(Y, fing) : cross(fing, Y));
        float c = twistAbout(pose.rot[fb], normalize(cross(fing, pn))) * (1.f / 1.45f);
        vec3 td = normalize(fing * 0.62f + Y * 0.66f + pn * 0.42f);
        float ct = Saturate(twistAbout(pose.rot[tb], normalize(cross(td, pn))) * (1.f / 0.9f));
        for (int f = 0; f < 4; f++) {
            const int b0 = phalanxBone(right, f, 0);
            vec3 d1 = normalize(sk.bindLocalPos[b0 + 1]);
            vec3 ax = normalize(cross(d1, pn));
            float lean = (right ? 1.f : -1.f) * kLean[f];
            vec3 u = normalize(ax * cosf(lean) + d1 * sinf(lean));
            float mcp = keyed(kC, kFlex[f][0], 4, Max(c, -0.3f));
            float pip = keyed(kC, kFlex[f][1], 4, Max(c, -0.04f));
            float dip = keyed(kC, kFlex[f][2], 4, Max(c, -0.04f));
            out[b0 - B_FIRST_DERIVED] = qaa(u, mcp);
            out[b0 + 1 - B_FIRST_DERIVED] = qaa(u, pip);
            out[b0 + 2 - B_FIRST_DERIVED] = qaa(u, dip);
        }
        // thumb: interpolate the key directions, then turn each bone (after its parents' rotations) onto them
        const int t0 = phalanxBone(right, 4, 0);
        vec3 bm = normalize(sk.bindLocalPos[t0 + 1]), b1 = normalize(sk.bindLocalPos[t0 + 2]);
        vec3 b2 = rotate(qaa(normalize(cross(b1, thumbPadDir(pn))), kThumbRestIP), b1);
        int k = ct < kCt[1] ? 0 : (ct < kCt[2] ? 1 : 2);
        float w = Saturate((ct - kCt[k]) / (kCt[k + 1] - kCt[k]));
        auto keyDir = [&](const float (*tab)[3], vec3 rest) {
            vec3 a = fing * tab[k][0] + Y * tab[k][1] + pn * tab[k][2], b = fing * tab[k + 1][0] + Y * tab[k + 1][1] + pn * tab[k + 1][2];
            return normalize(lerp(length2(a) > 1e-8f ? normalize(a) : rest, length2(b) > 1e-8f ? normalize(b) : rest, w));
        };
        quat qM = quatFromTo(bm, keyDir(kTM, bm));
        quat l1 = normalize(conj(qM) * quatFromTo(rotate(qM, b1), keyDir(kT1, b1)) * qM);
        quat q12 = normalize(qM * l1);
        quat l2 = normalize(conj(q12) * quatFromTo(rotate(q12, b2), keyDir(kT2, b2)) * q12);
        out[t0 - B_FIRST_DERIVED] = qM;
        out[t0 + 1 - B_FIRST_DERIVED] = l1;
        out[t0 + 2 - B_FIRST_DERIVED] = l2;
    }
}

static FORCEINLINE quat localRot(const Pose& pose, const quat* derived, int b) {
    return b >= B_FIRST_DERIVED ? derived[b - B_FIRST_DERIVED] : pose.rot[b];
}

// Model-space rotation/translation of a bone for a pose (walks up the parent chain).
void boneModel(const Skeleton& sk, const Pose& pose, int bone, quat& q, vec3& t) {
    int chain[16];
    int n = 0;
    for (int b = bone; b >= 0 && n < 16; b = sk.parent[b]) chain[n++] = b;
    quat der[B_COUNT - B_FIRST_DERIVED];
    if (bone >= B_FIRST_DERIVED) derivedLocals(sk, pose, der);
    q = quat();
    t = vec3(0);
    for (int i = n - 1; i >= 0; i--) {
        int b = chain[i];
        vec3 lp = sk.bindLocalPos[b];
        if (b == B_PELVIS) lp += pose.rootOffset;
        t = t + rotate(q, lp);
        q = q * localRot(pose, der, b);
    }
}

static FORCEINLINE mat4 matFromQT(quat q, vec3 t) {
    mat3 r = mat3FromQuat(q);
    return mat4(vec4(r.c[0], 0), vec4(r.c[1], 0), vec4(r.c[2], 0), vec4(t, 1));
}

}  // namespace detail

void computeMatrices(const Skeleton& skel, const Pose& pose, mat4* modelSpace, mat4* skinning) {
    quat mq[B_COUNT];
    vec3 mt[B_COUNT];
    quat der[B_COUNT - B_FIRST_DERIVED];
    detail::derivedLocals(skel, pose, der);
    for (int b = 0; b < B_COUNT; b++) {
        int p = skel.parent[b];
        vec3 lp = skel.bindLocalPos[b];
        if (b == B_PELVIS) lp += pose.rootOffset;
        quat lr = detail::localRot(pose, der, b);
        if (p >= 0) {
            mt[b] = mt[p] + rotate(mq[p], lp);
            mq[b] = normalize(mq[p] * lr);
        } else {
            mt[b] = lp;
            mq[b] = normalize(lr);
        }
        mat4 m = detail::matFromQT(mq[b], mt[b]);
        if (modelSpace) modelSpace[b] = m;
        if (skinning) skinning[b] = m * skel.invBindModel[b];
    }
}

void blendPoses(const Pose& a, const Pose& b, float w, Pose& out) {
    if (w <= 0.f) {
        if (&out != &a) out = a;
        return;
    }
    if (w >= 1.f) {
        if (&out != &b) out = b;
        return;
    }
    for (int i = 0; i < B_COUNT; i++) out.rot[i] = nlerp(a.rot[i], b.rot[i], w);
    out.rootOffset = lerp(a.rootOffset, b.rootOffset, w);
}

void blendUpperBody(const Pose& base, const Pose& layer, float w, Pose& out) {
    for (int i = 0; i < B_COUNT; i++) {
        float bw = detail::kUpperMask[i] * w;
        if (bw <= 0.f) out.rot[i] = base.rot[i];
        else if (bw >= 1.f) out.rot[i] = layer.rot[i];
        else out.rot[i] = nlerp(base.rot[i], layer.rot[i], bw);
    }
    out.rootOffset = base.rootOffset;
}

void poseFromModelSpace(const Skeleton& skel, const mat4* modelSpace, Pose& out) {
    quat mq[B_COUNT];
    for (int b = 0; b < B_COUNT; b++) {
        const mat4& m = modelSpace[b];
        mat3 r(normalize(m.c[0].xyz()), normalize(m.c[1].xyz()), normalize(m.c[2].xyz()));
        mq[b] = normalize(quatFromMat3(r));
        int p = skel.parent[b];
        out.rot[b] = p >= 0 ? normalize(conj(mq[p]) * mq[b]) : mq[b];
    }
    // pelvis offset: model position minus the position the (rotated) root chain gives in bind
    vec3 rootT = modelSpace[B_ROOT].c[3].xyz();
    vec3 bindPel = rootT + rotate(mq[B_ROOT], skel.bindLocalPos[B_PELVIS]);
    out.rootOffset = rotate(conj(mq[B_ROOT]), modelSpace[B_PELVIS].c[3].xyz() - bindPel);
}

void solveTwoBoneIK(const Skeleton& skel, Pose& pose, Bone upper, Bone lower, Bone end, vec3 targetModel, vec3 poleModel, float weight) {
    using namespace detail;
    if (weight <= 0.f) return;
    int par = skel.parent[upper];
    quat Qp;
    vec3 Pp;
    if (par >= 0) boneModel(skel, pose, par, Qp, Pp);
    vec3 ulp = skel.bindLocalPos[upper];
    if (upper == B_PELVIS) ulp += pose.rootOffset;
    vec3 A = Pp + rotate(Qp, ulp);
    quat Qa = Qp * pose.rot[upper];
    vec3 B = A + rotate(Qa, skel.bindLocalPos[lower]);
    quat Qb = Qa * pose.rot[lower];
    vec3 C = B + rotate(Qb, skel.bindLocalPos[end]);
    quat Qc = Qb * pose.rot[end];
    float la = length(skel.bindLocalPos[lower]), lb = length(skel.bindLocalPos[end]);
    if (la < 1e-5f || lb < 1e-5f) return;
    vec3 AT = targetModel - A;
    float dT = length(AT);
    if (dT < 1e-5f) return;
    float dMin = fabsf(la - lb) + 1e-4f, dMax = (la + lb) * 0.9995f;
    float d = Clamp(dT, dMin, dMax);
    // 1) knee/elbow angle
    vec3 BA = A - B, BC = C - B;
    vec3 m = cross(BA, BC);
    if (length2(m) < 1e-10f * la * lb) m = cross(BC, poleModel - B);
    if (length2(m) < 1e-12f) m = anyPerp(BC);
    m = normalize(m);
    float curB = acosf(Clamp(dot(normalize(BA), normalize(BC)), -1.f, 1.f));
    float desB = acosf(Clamp((la * la + lb * lb - d * d) / (2.f * la * lb), -1.f, 1.f));
    quat r1 = quatAxisAngle(m, desB - curB);
    vec3 C1 = B + rotate(r1, BC);
    // 2) aim the chain at the target
    quat r2 = quatFromTo(normalize(C1 - A), AT / dT);
    // 3) swing the knee towards the pole around the A->T axis
    vec3 u = AT / dT;
    vec3 kb = rotate(r2, B - A);
    vec3 kp = poleModel - A;
    kb = kb - u * dot(kb, u);
    kp = kp - u * dot(kp, u);
    quat r3;
    if (length2(kb) > 1e-10f && length2(kp) > 1e-10f) {
        kb = normalize(kb);
        kp = normalize(kp);
        float ang = atan2f(dot(cross(kb, kp), u), dot(kb, kp));
        r3 = quatAxisAngle(u, ang);
    }
    quat rr = r3 * r2;
    quat Qa2 = normalize(rr * Qa);
    quat Qb2 = normalize(rr * r1 * Qb);
    quat la2 = normalize(conj(Qp) * Qa2);
    quat lb2 = normalize(conj(Qa2) * Qb2);
    quat lc2 = normalize(conj(Qb2) * Qc);
    if (weight >= 1.f) {
        pose.rot[upper] = la2;
        pose.rot[lower] = lb2;
        pose.rot[end] = lc2;
    } else {
        pose.rot[upper] = nlerp(pose.rot[upper], la2, weight);
        pose.rot[lower] = nlerp(pose.rot[lower], lb2, weight);
        pose.rot[end] = nlerp(pose.rot[end], lc2, weight);
    }
}

void holdGrip(const Skeleton& skel, Pose& pose, bool right, vec3 pos, vec3 axis, vec3 palm, vec3 pole, float fingers, float thumb,
              float weight) {
    using namespace detail;
    if (weight <= 0.f) return;
    weight = Min(weight, 1.f);
    int hb = right ? B_HAND_R : B_HAND_L, fb = right ? B_FINGERS_R : B_FINGERS_L, tb = right ? B_THUMB_R : B_THUMB_L;
    int lo = right ? B_FOREARM_R : B_FOREARM_L, up = right ? B_UPPERARM_R : B_UPPERARM_L;
    // the hand's grip frame (as handGrip): handle axis +Y, fingers along the bind finger joint, palm normal between
    vec3 fl = skel.bindLocalPos[fb];
    float pl = length(fl);
    vec3 fing = pl > 1e-5f ? fl / pl : vec3(0, 0, -1);
    vec3 pn = normalize(right ? cross(vec3(0, 1, 0), fing) : cross(fing, vec3(0, 1, 0)));
    quat qh = quatFromTwoPairs(vec3(0, 1, 0), pn, axis, palm);
    vec3 wrist = pos - rotate(qh, fing * (kGripAlong * pl) + pn * (kGripPalm * pl));
    // out of the arm's reach: swing the clavicle toward the grip first (up to ~20 degrees, as a shoulder protracts when
    // the arm stretches for a far handguard)
    {
        int cl = skel.parent[up];
        quat qc, qa, qp;
        vec3 pc, pa, pp;
        boneModel(skel, pose, cl, qc, pc);
        boneModel(skel, pose, up, qa, pa);
        float reach = (length(skel.bindLocalPos[lo]) + length(skel.bindLocalPos[hb])) * 0.97f;
        vec3 toT = wrist - pa, arm = pa - pc;
        float over = length(toT) - reach, armL = length(arm);
        vec3 ax = cross(arm, toT);
        if (over > 0.f && armL > 1e-3f && length2(ax) > 1e-10f && skel.parent[cl] >= 0) {
            float ang = Min(over / armL, 0.35f) * weight;
            boneModel(skel, pose, skel.parent[cl], qp, pp);
            pose.rot[cl] = normalize(conj(qp) * quatAxisAngle(normalize(ax), ang) * qc);
        }
    }
    solveTwoBoneIK(skel, pose, (Bone)up, (Bone)lo, (Bone)hb, wrist, pole, weight);
    quat qf;
    vec3 pf;
    boneModel(skel, pose, lo, qf, pf);
    pose.rot[hb] = nlerp(pose.rot[hb], normalize(conj(qf) * qh), weight);
    // curl round the handle (the clips' finger / thumb flexion axes)
    vec3 thumbDir = normalize(fing * 0.62f + vec3(0, 1, 0) * 0.66f + pn * 0.42f);
    pose.rot[fb] = nlerp(pose.rot[fb], qaa(normalize(cross(fing, pn)), fingers * 1.45f), weight);
    pose.rot[tb] = nlerp(pose.rot[tb], qaa(normalize(cross(thumbDir, pn)), thumb * 0.9f), weight);
}

}  // namespace Anim
