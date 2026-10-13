// Skeleton construction: human proportions from height/gender/weight/muscle/age.
//
// Bind pose: "A-pose". Arms hang 45-50 degrees from vertical in the frontal plane (palms facing down/inwards,
// thumbs forward, elbows pointing backwards), legs straight with feet slightly apart, head looking forward.
// Every bone's bind rotation is the identity, i.e. all bone frames are aligned with model axes in the bind pose
// (+X right, +Y forward, +Z up); bindLocalPos holds the joint offsets. Pose rotations are therefore expressed
// relative to model-aligned parent frames, which keeps clip authoring intuitive and skeleton independent.
#include "anim_internal.h"

namespace Anim {
namespace detail {

float shoeLift(int shoes) {
    switch (shoes) {
        case SHOE_SNEAKER: return 0.026f;
        case SHOE_DRESS: return 0.02f;
        case SHOE_BOOT: return 0.032f;
        case SHOE_SANDAL: return 0.014f;
        case SHOE_BARE: return 0.f;
        case SHOE_FLATS: return 0.012f;
        case SHOE_RUNNER: return 0.03f;
        default: return 0.02f;
    }
}

void computeDims(const CharacterDesc& d, BodyDims& D) {
    Rng r(hash32(d.seed * 0x9E3779B1u + 0x51u), 0x2545F4914F6CDD1DULL);
    const float fem = d.gender == FEMALE ? 1.f : 0.f;
    const float H = Clamp(d.height, 1.40f, 2.10f);
    const float w = Saturate(d.weight), m = Saturate(d.muscle), a = Saturate(d.age);
    const float wc = w - 0.5f, mc = m - 0.4f;
    D.H = H;
    D.fem = fem;
    D.weight = w;
    D.muscle = m;
    D.age = a;
    D.s = H / 1.78f;
    const float s = D.s;
    D.lift = shoeLift(d.shoes);
    const float lift = D.lift;
    // Per-seed proportion variation (fixed draw order: deterministic)
    float legVar = 1.f + 0.016f * r.range(-1.f, 1.f);
    float shVar = 1.f + 0.03f * r.range(-1.f, 1.f);
    float armVar = 1.f + 0.015f * r.range(-1.f, 1.f);
    D.headS = powf(H / 1.75f, 0.35f) * Lerp(1.f, 0.945f, fem) * (1.f + 0.022f * r.range(-1.f, 1.f));
    const float hs = D.headS;
    const float kyph = sstep(0.6f, 1.0f, a);   // elderly stoop

    // ---- heights (barefoot)
    float zHeadJ = H - 0.172f * hs - 0.02f * kyph;
    float zNeck = H * Lerp(0.8175f, 0.82f, fem) - 0.012f * kyph;
    float zGH = H * Lerp(0.800f, 0.803f, fem) - 0.012f * kyph;
    float zChest = H * 0.727f - 0.006f * kyph;
    float zSpine2 = H * 0.662f;
    float zSpine1 = H * 0.600f;
    float zPelvis = H * 0.550f * legVar;
    float zHip = H * Lerp(0.507f, 0.511f, fem) * legVar;
    float zKnee = H * 0.284f * legVar;
    float zAnkle = Max(H * 0.039f, 0.062f);
    D.zCrotch = zHip - Lerp(0.072f, 0.068f, fem) * s + lift;
    D.zHip = zHip + lift;
    D.zWaist = zSpine1 + 0.02f * s + lift;
    D.zNavel = H * 0.595f + lift;
    D.zChestLine = zChest + 0.005f * s + lift;
    D.zArmpit = zGH - 0.075f * s + lift;
    D.zAcromion = zGH + 0.03f * s + lift;
    D.zNeckFront = zNeck - 0.01f * s + lift;
    D.zNeckBack = zNeck + 0.02f * s + lift;

    // ---- widths
    float GHhalf = (H * Lerp(0.104f, 0.0985f, fem) + 0.014f * mc + 0.006f * wc) * shVar;
    float hipJHalf = H * Lerp(0.0505f, 0.0575f, fem) + 0.006f * wc;
    float kneeHalf = hipJHalf * Lerp(0.93f, 0.84f, fem) + 0.008f * wc;
    float ankleHalf = hipJHalf * Lerp(1.0f, 0.93f, fem) + 0.004f * wc;

    // ---- limb lengths
    D.upperArm = H * 0.186f * armVar;
    D.forearm = H * Lerp(0.146f, 0.142f, fem) * armVar;
    float handLen = H * Lerp(0.108f, 0.104f, fem);
    D.palmLen = handLen * 0.53f;
    D.fingerLen = handLen * 0.47f;
    D.thumbLen = handLen * 0.34f;
    D.handW = H * Lerp(0.0485f, 0.046f, fem) * (1.f + 0.05f * mc + 0.03f * wc);
    D.handT = D.handW * 0.34f;
    D.footLen = H * Lerp(0.152f, 0.147f, fem);
    D.footW = D.footLen * Lerp(0.37f, 0.36f, fem) * (1.f + 0.05f * wc);
    D.heelBack = D.footLen * 0.21f;
    D.ballFwd = D.footLen * 0.52f;
    D.toeFwd = D.footLen * 0.79f;
    D.armAngle = (46.f + 6.f * Max(0.f, wc) * 2.f + 3.f * Max(0.f, mc)) * kDegToRad;

    // ---- torso shape
    D.hipHalfW = H * Lerp(0.0925f, 0.1035f, fem) * (1.f + 0.30f * wc);
    D.hipDepth = H * Lerp(0.056f, 0.058f, fem) * (1.f + 0.32f * wc);
    D.waistHalfW = H * Lerp(0.0800f, 0.0715f, fem) * (1.f + 0.62f * wc + 0.1f * a);
    D.waistDepth = H * Lerp(0.0575f, 0.053f, fem) * (1.f + 0.65f * wc + 0.1f * a);
    D.chestHalfW = H * Lerp(0.0865f, 0.079f, fem) * (1.f + 0.24f * wc + 0.14f * mc);
    D.chestDepth = H * Lerp(0.0625f, 0.058f, fem) * (1.f + 0.30f * wc + 0.12f * mc);
    D.glute = Lerp(1.f, 1.22f, fem) * (1.f + 0.55f * wc) * (1.f + 0.1f * r.range(-1.f, 1.f));
    D.bust = fem * (0.35f + 0.65f * r.f()) * (1.f + 0.9f * wc);
    D.belly = Saturate(Max(0.f, wc) * 1.8f + a * 0.35f * (1.f - fem * 0.5f) - 0.1f);
    D.trap = Lerp(1.f, 0.6f, fem) * (0.85f + 0.9f * mc);
    D.pecs = (1.f - fem) * Saturate(0.45f + 1.2f * mc + 0.3f * wc);
    D.neckR = Lerp(0.058f, 0.0495f, fem) * s * (1.f + 0.18f * wc + 0.15f * mc);

    // ---- limb radii
    D.rShoulder = 0.052f * s * (1.f + 0.35f * mc + 0.18f * wc) * Lerp(1.f, 0.86f, fem);
    D.rUpperArm = 0.0425f * s * (1.f + 0.30f * mc + 0.30f * wc) * Lerp(1.f, 0.9f, fem);
    D.rElbow = 0.035f * s * (1.f + 0.12f * mc + 0.18f * wc) * Lerp(1.f, 0.88f, fem);
    D.rForearm = 0.0405f * s * (1.f + 0.25f * mc + 0.18f * wc) * Lerp(1.f, 0.86f, fem);
    D.rWrist = 0.0275f * s * (1.f + 0.06f * mc + 0.08f * wc) * Lerp(1.f, 0.88f, fem);
    D.rThigh = 0.086f * s * (1.f + 0.36f * wc + 0.14f * mc) * Lerp(1.f, 1.07f, fem);
    D.rKnee = 0.053f * s * (1.f + 0.2f * wc) * Lerp(1.f, 0.97f, fem);
    D.rCalf = 0.056f * s * (1.f + 0.22f * wc + 0.2f * mc) * Lerp(1.f, 0.95f, fem);
    D.rAnkle = 0.033f * s * (1.f + 0.1f * wc) * Lerp(1.f, 0.9f, fem);
    D.shoulderHalfW = GHhalf + D.rShoulder;

    // ---- face variation
    auto g = [&]() { return r.range(-1.f, 1.f); };
    D.faceW = 1.f + 0.05f * g() + 0.04f * wc;
    D.jawW = Lerp(1.f, 0.9f, fem) * (1.f + 0.07f * g() + 0.08f * wc);
    D.chinP = 1.f + 0.3f * g();
    D.chinH = 1.f + 0.08f * g();
    D.noseL = 1.f + 0.09f * g();
    D.noseW = Lerp(1.f, 0.86f, fem) * (1.f + 0.12f * g());
    D.noseP = Lerp(1.f, 0.88f, fem) * (1.f + 0.12f * g());
    D.noseBridge = 1.f + 0.35f * g();
    D.lipFull = Lerp(1.f, 1.05f, fem) * (1.f + 0.18f * g());
    D.lipW = 1.f + 0.07f * g();
    D.eyeSize = Lerp(1.f, 1.04f, fem) * (1.f + 0.05f * g());
    D.eyeTilt = 0.06f * g();
    D.eyeSpace = 1.f + 0.04f * g();
    D.browH = 1.f + 0.12f * g();
    D.browRidge = Lerp(1.f, 0.35f, fem) * (1.f + 0.3f * g());
    D.cheekB = 1.f + 0.25f * g();
    D.earSize = (1.f + 0.07f * g()) * (1.f + 0.08f * a);
    D.earOut = 1.f + 0.35f * g();
    D.foreheadSlope = 0.5f + 0.5f * g();
    D.lidFold = r.f();
    D.headLen = 1.f + 0.035f * g();

    // ---- joints (model space, bind pose, raised by the shoe sole)
    vec3* J = D.J;
    J[B_ROOT] = vec3(0, 0, 0);
    J[B_PELVIS] = vec3(0, -0.012f * s, zPelvis + lift);
    J[B_SPINE1] = vec3(0, -0.030f * s, zSpine1 + lift);
    J[B_SPINE2] = vec3(0, -0.040f * s + 0.012f * kyph, zSpine2 + lift);
    J[B_CHEST] = vec3(0, -0.042f * s + 0.025f * kyph, zChest + lift);
    J[B_NECK] = vec3(0, -0.046f * s + 0.045f * kyph, zNeck + lift);
    J[B_HEAD] = vec3(0, -0.012f * s + 0.065f * kyph, zHeadJ + lift);
    float ang = D.armAngle;
    for (int side = 0; side < 2; side++) {
        float sx = side == 0 ? -1.f : 1.f;
        int clav = side == 0 ? B_CLAVICLE_L : B_CLAVICLE_R;
        int ua = side == 0 ? B_UPPERARM_L : B_UPPERARM_R;
        int fa = side == 0 ? B_FOREARM_L : B_FOREARM_R;
        int hand = side == 0 ? B_HAND_L : B_HAND_R;
        int fing = side == 0 ? B_FINGERS_L : B_FINGERS_R;
        int thumb = side == 0 ? B_THUMB_L : B_THUMB_R;
        int th = side == 0 ? B_THIGH_L : B_THIGH_R;
        int calf = side == 0 ? B_CALF_L : B_CALF_R;
        int foot = side == 0 ? B_FOOT_L : B_FOOT_R;
        int toe = side == 0 ? B_TOE_L : B_TOE_R;
        vec3 dir = vec3(sx * sinf(ang), 0.f, -cosf(ang));
        D.armDir[side] = dir;
        D.palmN[side] = vec3(-sx * cosf(ang), 0.f, -sinf(ang));
        D.thumbDir[side] = normalize(dir * 0.62f + vec3(0, 1, 0) * 0.66f + D.palmN[side] * 0.42f);
        J[clav] = vec3(sx * 0.024f * s, 0.030f * s + 0.03f * kyph, zNeck - 0.024f * s + lift);
        J[ua] = vec3(sx * GHhalf, -0.012f * s + 0.022f * kyph, zGH + lift);
        J[fa] = J[ua] + dir * D.upperArm;
        J[hand] = J[fa] + dir * D.forearm;
        J[fing] = J[hand] + dir * D.palmLen;
        J[thumb] = J[hand] + dir * (0.016f * s) + vec3(0, 1, 0) * (0.019f * s) + D.palmN[side] * (0.009f * s);
        J[th] = vec3(sx * hipJHalf, 0.006f * s, zHip + lift);
        J[calf] = vec3(sx * kneeHalf, 0.012f * s, zKnee + lift);
        J[foot] = vec3(sx * ankleHalf, -0.004f * s, zAnkle + lift);
        J[toe] = vec3(sx * ankleHalf, J[foot].y + D.ballFwd, 0.021f * s + lift);
        D.legDir[side] = normalize(J[calf] - J[th]);
    }
    D.thigh = length(J[B_CALF_L] - J[B_THIGH_L]);
    D.shin = length(J[B_FOOT_L] - J[B_CALF_L]);
    J[B_JAW] = J[B_HEAD] + vec3(0, 0.010f, 0.020f) * hs;
    J[B_EYE_L] = J[B_HEAD] + vec3(-0.0315f * D.eyeSpace, 0.0705f, 0.058f) * hs;
    J[B_EYE_R] = J[B_HEAD] + vec3(0.0315f * D.eyeSpace, 0.0705f, 0.058f) * hs;
}

static const int kParent[B_COUNT] = {
    -1,                                   // ROOT
    B_ROOT,                               // PELVIS
    B_PELVIS, B_SPINE1, B_SPINE2,         // SPINE1, SPINE2, CHEST
    B_CHEST, B_NECK,                      // NECK, HEAD
    B_CHEST, B_CLAVICLE_L, B_UPPERARM_L, B_FOREARM_L,
    B_CHEST, B_CLAVICLE_R, B_UPPERARM_R, B_FOREARM_R,
    B_PELVIS, B_THIGH_L, B_CALF_L, B_FOOT_L,
    B_PELVIS, B_THIGH_R, B_CALF_R, B_FOOT_R,
    B_HAND_L, B_HAND_L, B_HAND_R, B_HAND_R,
    B_HEAD, B_HEAD, B_HEAD,
};

}  // namespace detail

void buildSkeleton(const CharacterDesc& d, Skeleton& out) {
    using namespace detail;
    BodyDims D;
    computeDims(d, D);
    const vec3* J = D.J;
    for (int b = 0; b < B_COUNT; b++) {
        out.parent[b] = kParent[b];
        vec3 pp = kParent[b] >= 0 ? J[kParent[b]] : vec3(0);
        out.bindLocalPos[b] = J[b] - pp;
        out.bindLocalRot[b] = quat();
        out.invBindModel[b] = mat4Translation(-J[b]);
    }
    auto len = [&](int a, int b) { return length(J[b] - J[a]); };
    const float s = D.s, hs = D.headS;
    out.boneLength[B_ROOT] = J[B_PELVIS].z;
    out.boneLength[B_PELVIS] = len(B_PELVIS, B_SPINE1);
    out.boneLength[B_SPINE1] = len(B_SPINE1, B_SPINE2);
    out.boneLength[B_SPINE2] = len(B_SPINE2, B_CHEST);
    out.boneLength[B_CHEST] = len(B_CHEST, B_NECK);
    out.boneLength[B_NECK] = len(B_NECK, B_HEAD);
    out.boneLength[B_HEAD] = 0.2f * hs;
    for (int side = 0; side < 2; side++) {
        int o = side == 0 ? 0 : 4;
        out.boneLength[B_CLAVICLE_L + o] = len(B_CLAVICLE_L + o, B_UPPERARM_L + o);
        out.boneLength[B_UPPERARM_L + o] = len(B_UPPERARM_L + o, B_FOREARM_L + o);
        out.boneLength[B_FOREARM_L + o] = len(B_FOREARM_L + o, B_HAND_L + o);
        out.boneLength[B_HAND_L + o] = D.palmLen;
        out.boneLength[B_THIGH_L + o] = len(B_THIGH_L + o, B_CALF_L + o);
        out.boneLength[B_CALF_L + o] = len(B_CALF_L + o, B_FOOT_L + o);
        out.boneLength[B_FOOT_L + o] = len(B_FOOT_L + o, B_TOE_L + o);
        out.boneLength[B_TOE_L + o] = D.toeFwd - D.ballFwd;
        int f = side == 0 ? 0 : 2;
        out.boneLength[B_FINGERS_L + f] = D.fingerLen;
        out.boneLength[B_THUMB_L + f] = D.thumbLen;
    }
    out.boneLength[B_JAW] = 0.095f * hs;
    out.boneLength[B_EYE_L] = out.boneLength[B_EYE_R] = 0.024f * hs;

    out.boneRadius[B_ROOT] = 0.05f * s;
    out.boneRadius[B_PELVIS] = D.hipHalfW * 0.9f;
    out.boneRadius[B_SPINE1] = D.waistHalfW * 0.92f;
    out.boneRadius[B_SPINE2] = (D.waistHalfW + D.chestHalfW) * 0.46f;
    out.boneRadius[B_CHEST] = D.chestHalfW * 0.98f;
    out.boneRadius[B_NECK] = D.neckR;
    out.boneRadius[B_HEAD] = 0.098f * hs;
    for (int side = 0; side < 2; side++) {
        int o = side == 0 ? 0 : 4;
        out.boneRadius[B_CLAVICLE_L + o] = 0.045f * s;
        out.boneRadius[B_UPPERARM_L + o] = D.rUpperArm;
        out.boneRadius[B_FOREARM_L + o] = (D.rForearm + D.rWrist) * 0.5f;
        out.boneRadius[B_HAND_L + o] = D.handT * 0.8f;
        out.boneRadius[B_THIGH_L + o] = D.rThigh * 0.85f;
        out.boneRadius[B_CALF_L + o] = (D.rCalf + D.rAnkle) * 0.52f;
        out.boneRadius[B_FOOT_L + o] = D.footW * 0.4f;
        out.boneRadius[B_TOE_L + o] = D.footW * 0.33f;
        int f = side == 0 ? 0 : 2;
        out.boneRadius[B_FINGERS_L + f] = 0.0095f * s;
        out.boneRadius[B_THUMB_L + f] = 0.011f * s;
    }
    out.boneRadius[B_JAW] = 0.045f * hs;
    out.boneRadius[B_EYE_L] = out.boneRadius[B_EYE_R] = 0.012f * hs;
}

}  // namespace Anim
