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
};

// Model-space rotation/translation of a bone for a pose (walks up the parent chain).
void boneModel(const Skeleton& sk, const Pose& pose, int bone, quat& q, vec3& t) {
    int chain[16];
    int n = 0;
    for (int b = bone; b >= 0 && n < 16; b = sk.parent[b]) chain[n++] = b;
    q = quat();
    t = vec3(0);
    for (int i = n - 1; i >= 0; i--) {
        int b = chain[i];
        vec3 lp = sk.bindLocalPos[b];
        if (b == B_PELVIS) lp += pose.rootOffset;
        t = t + rotate(q, lp);
        q = q * pose.rot[b];
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
    for (int b = 0; b < B_COUNT; b++) {
        int p = skel.parent[b];
        vec3 lp = skel.bindLocalPos[b];
        if (b == B_PELVIS) lp += pose.rootOffset;
        if (p >= 0) {
            mt[b] = mt[p] + rotate(mq[p], lp);
            mq[b] = normalize(mq[p] * pose.rot[b]);
        } else {
            mt[b] = lp;
            mq[b] = normalize(pose.rot[b]);
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
