// Procedural body mesh: the skin surface is a quad-dominant ring/grid topology whose vertices are placed by ray
// casting against a signed distance model of the body (smooth unions of ellipsoids and round cones placed on the
// bind-pose skeleton and scaled by weight/muscle/gender). Each part (torso, legs, arms, neck, head) sees its own
// subset of primitives so limbs never web together; the two junction types are handled topologically:
//  - legs: "pants" topology (each leg's top loop = half of the torso's bottom ring + a shared crotch seam),
//  - arms: a rectangular hole in the torso's side (armhole) whose boundary loop is bridged to the arm tube by
//    transition rings that are projected onto the union of torso and arm surfaces (smooth armpit/shoulder).
// Skin weights come from the parametrization (distance along the limb, height on the torso) with wide smooth
// blend zones at every joint.
#include "anim_internal.h"

namespace Anim {
namespace detail {

// ------------------------------------------------------------------------------------------------
// Signed distance model of torso, limbs and neck (head primitives are added by face.cpp)

static void addBodyPrims(BuildCtx& c) {
    const BodyDims& D = *c.D;
    Sdf& S = c.sdf;
    const vec3* J = D.J;
    const float s = D.s, fem = D.fem, lift = D.lift;
    const float wc = D.weight - 0.5f, mc = D.muscle - 0.4f;
    const u32 T = MK_TORSO, LL = MK_LEG_L, LR = MK_LEG_R;
    const float zH = D.zHip;
    const float zC = J[B_CHEST].z;

    // ---- pelvis, glutes, abdomen
    S.ellipsoid(vec3(0, -0.008f * s, zH + 0.028f * s), vec3(D.hipHalfW * 0.94f, D.hipDepth, 0.125f * s), T | LL | LR, 0.035f * s);
    for (int side = 0; side < 2; side++) {
        float sx = side ? 1.f : -1.f;
        float g = D.glute;
        vec3 gc(sx * (0.058f + 0.008f * (g - 1.f)) * s, -D.hipDepth * 0.5f - 0.016f * s * g, zH - 0.028f * s);
        vec3 gr = vec3(0.071f, 0.060f, 0.085f) * (s * sqrtf(g));
        S.ellipsoid(gc, gr, T | (side ? LR : LL), 0.03f * s, vec3(1, 0, 0), vec3(0, cosf(0.25f), -sinf(0.25f)));
    }
    S.ellipsoid(vec3(0, 0.004f * s, zH + 0.095f * s), vec3(D.waistHalfW * 1.03f, D.waistDepth * 0.94f, 0.10f * s), T, 0.045f * s);
    S.ellipsoid(vec3(0, -0.004f * s, D.zWaist), vec3(D.waistHalfW, D.waistDepth, 0.12f * s), T, 0.045f * s);
    if (D.belly > 0.01f)
        S.ellipsoid(vec3(0, D.waistDepth * 0.28f + 0.03f * s * D.belly, D.zWaist - 0.045f * s),
                    vec3(D.waistHalfW * 0.86f, D.waistDepth * (0.6f + 0.36f * D.belly), (0.10f + 0.035f * D.belly) * s), T, 0.05f * s);
    if (D.weight > 0.55f)
        for (int side = 0; side < 2; side++) {
            float sx = side ? 1.f : -1.f;
            float k = (D.weight - 0.55f) * 2.2f;
            S.ellipsoid(vec3(sx * D.waistHalfW * 0.82f, -0.02f * s, zH + 0.075f * s), vec3(0.05f, 0.065f, 0.05f) * (s * k), T, 0.04f * s);
        }
    // ---- ribcage and chest
    S.ellipsoid(vec3(0, -0.012f * s, zC - 0.03f * s), vec3(D.chestHalfW * 0.97f, D.chestDepth * 0.96f, 0.158f * s), T, 0.04f * s);
    S.ellipsoid(vec3(0, -0.006f * s, zC + 0.062f * s), vec3(D.chestHalfW * 1.04f, D.chestDepth * 0.88f, 0.095f * s), T, 0.035f * s);
    if (D.pecs > 0.05f)
        for (int side = 0; side < 2; side++) {
            float sx = side ? 1.f : -1.f;
            vec3 pc(sx * 0.058f * s, D.chestDepth * 0.66f - 0.012f * s, zC + 0.032f * s);
            vec3 pr(0.066f * s, (0.022f + 0.018f * D.pecs) * s, 0.05f * s);
            S.ellipsoid(pc, pr, T, 0.03f * s, vec3(cosf(0.3f), 0, sx * sinf(0.3f) * 0.3f + 0.25f), vec3(0, 1, 0));
        }
    if (D.bust > 0.02f)
        for (int side = 0; side < 2; side++) {
            float sx = side ? 1.f : -1.f;
            float rb = (0.043f + 0.03f * D.bust) * s;
            vec3 bc(sx * 0.074f * s, D.chestDepth * 0.62f + rb * 0.25f, zC - 0.008f * s - rb * 0.25f);
            vec3 ax = normalize(vec3(cosf(0.25f), sx * -sinf(0.25f), 0));
            S.ellipsoid(bc, vec3(rb * 1.02f, rb * 0.9f, rb * 0.92f), T, 0.035f * s, ax, vec3(sx * sinf(0.25f), cosf(0.25f), 0));
        }
    for (int side = 0; side < 2; side++) {
        float sx = side ? 1.f : -1.f;
        // shoulder blades and lats
        S.ellipsoid(vec3(sx * 0.07f * s, -D.chestDepth * 0.7f, zC + 0.035f * s), vec3(0.068f, 0.032f, 0.085f) * s, T, 0.035f * s);
        float lat = 0.75f + 0.6f * Max(0.f, mc);
        S.ellipsoid(vec3(sx * D.chestHalfW * 0.72f, -0.03f * s, zC - 0.04f * s), vec3(0.045f * lat, 0.065f, 0.10f) * s, T, 0.04f * s);
    }
    // ---- shoulder girdle: trapezius, clavicles, deltoids
    for (int side = 0; side < 2; side++) {
        float sx = side ? 1.f : -1.f;
        int ua = side ? B_UPPERARM_R : B_UPPERARM_L;
        int clav = side ? B_CLAVICLE_R : B_CLAVICLE_L;
        u32 AM = side ? MK_ARM_R : MK_ARM_L;
        vec3 gh = J[ua];
        float tr = D.trap;
        S.cone(vec3(sx * 0.01f * s, J[B_NECK].y + 0.004f * s, J[B_NECK].z + 0.004f * s),
               vec3(gh.x - sx * 0.012f * s, gh.y - 0.004f * s, gh.z + 0.006f * s), (0.040f + 0.012f * tr) * s, 0.027f * s,
               T | MK_NECK, 0.03f * s);
        S.cone(J[clav] + vec3(0, 0.022f * s, 0.008f * s), gh + vec3(-sx * 0.02f * s, 0.022f * s, 0.028f * s), 0.011f * s, 0.012f * s, T, 0.02f * s);
        vec3 ad = D.armDir[side];
        vec3 dc = gh + vec3(sx * 0.008f * s, 0.004f * s, -0.004f * s) + ad * 0.026f * s;
        float rd = D.rShoulder;
        S.ellipsoid(dc, vec3(rd * 1.0f, rd * 1.08f, rd * 1.12f), AM, 0.025f * s, vec3(0, 1, 0), vec3(-ad.x, 0, -ad.z));
    }
    // ---- neck base
    S.cone(vec3(0, J[B_NECK].y + 0.02f * s, J[B_NECK].z - 0.05f * s), vec3(0, J[B_NECK].y + 0.03f * s, J[B_NECK].z + 0.07f * s),
           D.neckR * 1.25f, D.neckR * 1.02f, T | MK_NECK, 0.03f * s);

    // ---- legs
    for (int side = 0; side < 2; side++) {
        float sx = side ? 1.f : -1.f;
        u32 LM = side ? LR : LL;
        u32 FM = side ? MK_FOOT_R : MK_FOOT_L;
        int th = side ? B_THIGH_R : B_THIGH_L, kn = side ? B_CALF_R : B_CALF_L, an = side ? B_FOOT_R : B_FOOT_L;
        vec3 hip = J[th], knee = J[kn], ank = J[an];
        vec3 ld = normalize(knee - hip);
        float TL = length(knee - hip), SL = length(ank - knee);
        S.cone(hip + vec3(0, 0, 0.03f * s), knee, D.rThigh, D.rKnee * 1.0f, T | LM, 0.03f * s, 1.f, 0.93f, vec3(1, 0, 0));
        vec3 ax(1, 0, 0), ay = normalize(cross(ld, ax));
        (void)ay;
        auto along = [&](float t) { return lerp(hip, knee, t); };
        float mq = 0.9f + 0.4f * D.muscle;
        S.ellipsoid(along(0.42f) + vec3(sx * 0.004f * s, D.rThigh * 0.34f, 0), vec3(D.rThigh * 0.68f, D.rThigh * 0.52f, TL * 0.34f) * mq, LM, 0.03f * s,
                    vec3(1, 0, 0), vec3(0, 1, 0));
        S.ellipsoid(along(0.2f) + vec3(-sx * D.rThigh * 0.34f, 0.004f * s, 0), vec3(D.rThigh * 0.6f, D.rThigh * 0.72f, TL * 0.3f), LM, 0.03f * s);
        float saddle = Saturate(0.3f + 0.3f * fem + 1.1f * wc);
        S.ellipsoid(along(0.16f) + vec3(sx * D.rThigh * 0.38f, -0.01f * s, 0), vec3(D.rThigh * 0.55f, D.rThigh * 0.62f, TL * 0.24f) * (0.7f + 0.3f * saddle),
                    T | LM, 0.035f * s);
        S.ellipsoid(along(0.48f) + vec3(0, -D.rThigh * 0.34f, 0), vec3(D.rThigh * 0.64f, D.rThigh * 0.5f, TL * 0.32f), LM, 0.03f * s);
        // knee
        S.ellipsoid(knee + vec3(0, D.rKnee * 0.74f, 0.012f * s), vec3(0.025f, 0.016f, 0.028f) * s, LM, 0.016f * s);
        S.ellipsoid(knee + vec3(0, -0.004f * s, -0.01f * s), vec3(D.rKnee * 0.98f, D.rKnee * 0.9f, 0.05f * s), LM, 0.02f * s);
        // shin & calf
        S.cone(knee, ank + vec3(0, 0, 0.012f * s), D.rKnee * 0.9f, D.rAnkle, LM, 0.03f * s, 1.f, 0.95f, vec3(1, 0, 0));
        float mc2 = 0.85f + 0.35f * D.muscle + 0.2f * wc;
        vec3 calfC = lerp(knee, ank, 0.27f);
        S.ellipsoid(calfC + vec3(-sx * 0.008f * s, -D.rCalf * 0.3f, -0.01f * s), vec3(D.rCalf * 0.62f, D.rCalf * 0.72f, SL * 0.2f) * mc2, LM, 0.03f * s);
        S.ellipsoid(calfC + vec3(sx * 0.012f * s, -D.rCalf * 0.3f, 0.01f * s), vec3(D.rCalf * 0.55f, D.rCalf * 0.66f, SL * 0.18f) * mc2, LM, 0.03f * s);
        // achilles and ankle
        S.cone(lerp(knee, ank, 0.62f) + vec3(0, -D.rAnkle * 0.6f, 0), ank + vec3(0, -D.heelBack * 0.55f, -0.02f * s), 0.016f * s, 0.011f * s, LM | FM, 0.02f * s);
        S.ellipsoid(ank + vec3(sx * 0.02f * s, -0.004f * s, -0.008f * s), vec3(0.012f, 0.014f, 0.016f) * s, LM | FM, 0.012f * s);
        S.ellipsoid(ank + vec3(-sx * 0.019f * s, 0.002f * s, 0.002f * s), vec3(0.012f, 0.014f, 0.016f) * s, LM | FM, 0.012f * s);
        // foot
        float fw = D.footW;
        float fy = ank.y;
        S.ellipsoid(vec3(ank.x, fy - D.heelBack * 0.55f, lift + 0.033f * s), vec3(0.029f * s, 0.037f * s, 0.036f * s), LM | FM, 0.02f * s);
        S.ellipsoid(vec3(ank.x - sx * 0.002f * s, fy + D.ballFwd * 0.35f, lift + 0.03f * s), vec3(fw * 0.42f, D.ballFwd * 0.62f, 0.034f * s), LM | FM, 0.025f * s);
        S.ellipsoid(ank + vec3(0, 0.03f * s, -0.018f * s), vec3(0.031f, 0.05f, 0.03f) * s, LM | FM, 0.025f * s);
        S.ellipsoid(vec3(ank.x - sx * 0.003f * s, fy + D.ballFwd, lift + 0.02f * s), vec3(fw * 0.5f, 0.036f * s, 0.021f * s), LM | FM, 0.02f * s,
                    vec3(cosf(0.18f), sx * 0.18f, 0), vec3(-sx * 0.18f, 1, 0));
        S.ellipsoid(vec3(ank.x - sx * 0.006f * s, fy + (D.ballFwd + D.toeFwd) * 0.5f + 0.004f * s, lift + 0.013f * s),
                    vec3(fw * 0.43f, (D.toeFwd - D.ballFwd) * 0.62f, 0.0135f * s), LM | FM, 0.015f * s, vec3(cosf(0.22f), sx * 0.22f, 0),
                    vec3(-sx * 0.22f, 1, 0));
    }
    // ---- arms and hands
    for (int side = 0; side < 2; side++) {
        float sx = side ? 1.f : -1.f;
        u32 AM = side ? MK_ARM_R : MK_ARM_L, HM = side ? MK_HAND_R : MK_HAND_L;
        int ua = side ? B_UPPERARM_R : B_UPPERARM_L, fa = side ? B_FOREARM_R : B_FOREARM_L, hd = side ? B_HAND_R : B_HAND_L;
        vec3 gh = J[ua], el = J[fa], wr = J[hd];
        vec3 ad = D.armDir[side];
        vec3 fr(0, 1, 0);                          // front of the arm
        vec3 up = normalize(cross(ad, fr)) * sx;   // up-lateral side (back of the hand)
        (void)up;
        float mu = 0.85f + 0.45f * D.muscle;
        S.cone(gh + ad * 0.01f * s, el, D.rUpperArm * 1.02f, D.rElbow, AM, 0.03f * s, 1.f, 0.96f, fr);
        S.ellipsoid(lerp(gh, el, 0.55f) + fr * D.rUpperArm * 0.32f, vec3(D.rUpperArm * 0.6f, D.rUpperArm * 0.62f, D.upperArm * 0.25f) * mu, AM,
                    0.025f * s, fr, vec3(ad.z, 0, -ad.x));
        S.ellipsoid(lerp(gh, el, 0.40f) - fr * D.rUpperArm * 0.32f, vec3(D.rUpperArm * 0.62f, D.rUpperArm * 0.66f, D.upperArm * 0.3f) * mu, AM,
                    0.025f * s, fr, vec3(ad.z, 0, -ad.x));
        S.ellipsoid(el - fr * D.rElbow * 0.55f, vec3(0.016f, 0.016f, 0.02f) * s, AM, 0.015f * s);
        S.cone(el, wr, D.rForearm, D.rWrist, AM, 0.03f * s, 1.f, 0.78f, fr);
        vec3 pn = D.palmN[side];
        S.ellipsoid(lerp(el, wr, 0.24f) - pn * D.rForearm * 0.12f, vec3(D.rForearm * 0.95f, D.rForearm * 0.8f, D.forearm * 0.27f) * (0.9f + 0.2f * D.muscle),
                    AM, 0.03f * s, fr, -pn);
        // palm: flattened ellipsoid (width along +Y, thickness along palm normal) ending at the knuckles (its rounded
        // end follows the arc of the knuckle line; the fingers' bases carry the knuckles and the finger webs)
        vec3 pc = wr + ad * (D.palmLen * 0.49f);
        S.ellipsoid(pc, vec3(D.handW * 0.5f, D.handT * 0.5f, D.palmLen * 0.52f), AM | HM, 0.016f * s, fr, -pn);
        // metacarpal heads: square the palm's end off along the knuckle line (the knuckles of a fist, the finger webs
        // between them)
        for (int f = 0; f < 4; f++) {
            vec3 mcp = J[phalanxBone(side == 1, f, 0)];
            S.ellipsoid(mcp - ad * (0.005f * s), vec3(D.handW * kFingerDefs[f].rad * 1.06f, D.handT * 0.34f, 0.0105f * s), AM | HM, 0.009f * s, fr, -pn);
        }
        S.ellipsoid(wr + ad * (D.palmLen * 0.12f), vec3(D.rWrist * 1.05f, D.rWrist * 0.72f, 0.022f * s), AM | HM, 0.02f * s, fr, -pn);
        // thenar (ball of the thumb) along the thumb's metacarpal, hypothenar pad on the little finger side, and the
        // distal palm pads under the knuckles
        {
            vec3 cmc = J[side ? B_THUMB_R : B_THUMB_L], mcpT = J[phalanxBone(side == 1, 4, 1)];
            vec3 th = lerp(cmc, mcpT, 0.42f) - pn * (D.handT * 0.08f) - fr * (D.handW * 0.03f);
            vec3 tax = normalize(mcpT - cmc);
            S.ellipsoid(th, vec3(0.0165f, 0.0125f, 0.029f) * s, AM | HM, 0.013f * s, normalize(cross(tax, pn)), pn);
        }
        S.ellipsoid(wr + ad * (D.palmLen * 0.42f) - fr * (D.handW * 0.3f) + pn * (D.handT * 0.2f), vec3(0.013f, 0.011f, 0.03f) * s, AM | HM,
                    0.014f * s, fr, -pn);
        S.ellipsoid(wr + ad * (D.palmLen * 0.86f) + pn * (D.handT * 0.26f), vec3(D.handW * 0.43f, D.handT * 0.3f, 0.012f * s), AM | HM,
                    0.012f * s, fr, -pn);
    }
    // ---- neck
    {
        vec3 nb(0, J[B_NECK].y + 0.03f * s, J[B_NECK].z - 0.02f * s);
        vec3 nt(0, J[B_HEAD].y + 0.012f * D.headS, J[B_HEAD].z - 0.012f * D.headS);
        S.cone(nb, nt, D.neckR * 1.06f, D.neckR * 0.94f, MK_NECK | MK_HEAD, 0.03f * s, 1.f, 0.96f, vec3(1, 0, 0));
        for (int side = 0; side < 2; side++) {
            float sx = side ? 1.f : -1.f;
            vec3 a = J[B_HEAD] + vec3(sx * 0.046f, -0.012f, 0.004f) * D.headS;
            vec3 b(sx * 0.016f * s, J[B_NECK].y + 0.075f * s, J[B_NECK].z - 0.012f * s);
            S.cone(a, b, 0.0135f * s, 0.012f * s, MK_NECK | MK_HEAD, 0.02f * s);
        }
        if (fem < 0.5f)
            S.ellipsoid(vec3(0, lerp(nb, nt, 0.55f).y + D.neckR * 0.9f, lerp(nb, nt, 0.55f).z), vec3(0.012f, 0.012f, 0.017f) * s, MK_NECK | MK_HEAD, 0.015f * s);
    }
    // flat soles
    S.plane(vec3(0, 0, lift), vec3(0, 0, 1), LL | LR | MK_FOOT_L | MK_FOOT_R);
    (void)mc;
}

// ------------------------------------------------------------------------------------------------
// Skin weights by region

static SkinW torsoWeights(const BodyDims& D, vec3 p) {
    const vec3* J = D.J;
    const float s = D.s;
    float z = p.z, ax = fabsf(p.x);
    int side = p.x < 0.f ? 0 : 1;
    int o = side ? 4 : 0;
    float z1 = J[B_SPINE1].z, z2 = J[B_SPINE2].z, z3 = J[B_CHEST].z;
    float a1 = sstep(z1 - 0.07f * s, z1 + 0.035f * s, z);
    float a2 = sstep(z2 - 0.05f * s, z2 + 0.045f * s, z);
    float a3 = sstep(z3 - 0.05f * s, z3 + 0.05f * s, z);
    float lat = Saturate(ax / (0.8f * D.hipHalfW));
    float th = sstep(D.zHip + 0.06f * s, D.zCrotch - 0.08f * s, z) * Lerp(0.5f, 1.f, lat);
    float sh = sstep(D.zArmpit - 0.07f * s, D.zAcromion, z) * sstep(0.05f * s, 0.17f * s, ax);
    float clav = sh * 0.7f;
    float ua = sstep(D.zArmpit - 0.06f * s, D.zArmpit + 0.03f * s, z) * sstep(0.13f * s, 0.21f * s, ax) * 0.6f;
    float nk = sstep(D.zNeckFront - 0.015f * s, D.zNeckBack + 0.02f * s, z) * (1.f - sstep(0.06f * s, 0.12f * s, ax)) * 0.35f;
    float sum = th + clav + ua + nk;
    if (sum > 1.f) { th /= sum; clav /= sum; ua /= sum; nk /= sum; sum = 1.f; }
    float rest = 1.f - sum;
    WAcc acc;
    acc.add(B_PELVIS, (1.f - a1) * rest);
    acc.add(B_SPINE1, a1 * (1.f - a2) * rest);
    acc.add(B_SPINE2, a2 * (1.f - a3) * rest);
    acc.add(B_CHEST, a3 * rest);
    acc.add(B_THIGH_L + o, th);
    acc.add(B_CLAVICLE_L + o, clav);
    acc.add(B_UPPERARM_L + o, ua);
    acc.add(B_NECK, nk);
    return acc.finish();
}

SkinW torsoSkinWeights(const BodyDims& D, vec3 p) { return torsoWeights(D, p); }

struct LegInfo {
    float kneeA, ankleA, ballA;
};

static SkinW legWeights(const BodyDims& D, int side, float along, vec3 p, const LegInfo& li) {
    const float s = D.s;
    int o = side ? 4 : 0;
    float lat = Saturate(fabsf(p.x) / (0.8f * D.hipHalfW));
    float thw = sstep(D.zHip + 0.06f * s, D.zCrotch - 0.08f * s, p.z) * Lerp(0.5f, 1.f, lat);
    thw = Max(thw, sstep(D.zCrotch - 0.01f * s, D.zCrotch - 0.12f * s, p.z));
    float tK = sstep(li.kneeA - 0.05f * s, li.kneeA + 0.045f * s, along);
    float tA = sstep(li.ankleA - 0.035f * s, li.ankleA + 0.03f * s, along);
    float tB = sstep(li.ballA - 0.016f * s, li.ballA + 0.02f * s, along);
    WAcc acc;
    acc.add(B_PELVIS, 1.f - thw);
    acc.add(B_THIGH_L + o, thw * (1.f - tK));
    acc.add(B_CALF_L + o, thw * tK * (1.f - tA));
    acc.add(B_FOOT_L + o, thw * tA * (1.f - tB));
    acc.add(B_TOE_L + o, thw * tB);
    return acc.finish();
}

// Along the arm: clavicle / chest -> upper arm -> forearm -> forearm roll (ramping in over the forearm's upper half,
// so the forearm takes the hand's twist gradually) -> hand at the wrist.
static SkinW armWeights(const BodyDims& D, int side, float along) {
    const float s = D.s;
    int o = side ? 4 : 0;
    float eA = D.upperArm, wA = D.upperArm + D.forearm;
    float tU = sstep(-0.03f * s, 0.075f * s, along);
    float tE = sstep(eA - 0.045f * s, eA + 0.04f * s, along);
    float tR = lstep(eA + 0.01f * s, eA + D.forearm * 0.62f, along);
    float tW = sstep(wA - 0.03f * s, wA + 0.02f * s, along);
    WAcc acc;
    acc.add(B_CLAVICLE_L + o, (1.f - tU) * 0.75f);
    acc.add(B_CHEST, (1.f - tU) * 0.25f);
    acc.add(B_UPPERARM_L + o, tU * (1.f - tE));
    acc.add(B_FOREARM_L + o, tE * (1.f - tR) * (1.f - tW));
    acc.add(side ? B_FOREARM_ROLL_R : B_FOREARM_ROLL_L, tE * tR * (1.f - tW));
    acc.add(B_HAND_L + o, tW);
    return acc.finish();
}

// Share of the thumb's metacarpal bone in the palm skin over the thenar eminence (the ball of the thumb follows the
// thumb when it opposes).
static float thenarWeight(const BodyDims& D, int side, vec3 p) {
    const vec3 a = D.J[side ? B_THUMB_R : B_THUMB_L], b = D.J[phalanxBone(side == 1, 4, 1)];
    vec3 ab = b - a;
    float t = Saturate(dot(p - a, ab) / Max(length2(ab), 1e-8f));
    float d = length(p - (a + ab * t));
    return 0.55f * (1.f - sstep(0.012f * D.s, 0.03f * D.s, d)) * sstep(0.f, 0.35f, t);
}

// ------------------------------------------------------------------------------------------------
// Paths with parallel-transported frames (for limbs)

struct PathS {
    vec3 p, t, f, s;
    float along;
};

// Catmull-Rom sampling of control points, then parallel transport of the frame (f = front, s = lateral side).
static void buildPath(const std::vector<vec3>& cp, vec3 frontHint, bool mirror, float step, std::vector<PathS>& out) {
    std::vector<vec3> pts;
    int n = (int)cp.size();
    for (int i = 0; i + 1 < n; i++) {
        vec3 p0 = cp[i > 0 ? i - 1 : 0], p1 = cp[i], p2 = cp[i + 1], p3 = cp[i + 2 < n ? i + 2 : n - 1];
        if (i == 0) p0 = p1 - (p2 - p1);
        if (i + 2 >= n) p3 = p2 + (p2 - p1);
        float segL = length(p2 - p1);
        int k = Max(2, (int)(segL / step));
        for (int j = 0; j < k; j++) {
            float t = (float)j / k, t2 = t * t, t3 = t2 * t;
            vec3 q = (p1 * 2.f + (p2 - p0) * t + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * t3) * 0.5f;
            pts.push_back(q);
        }
    }
    pts.push_back(cp[n - 1]);
    out.clear();
    float acc = 0.f;
    vec3 f = frontHint;
    for (size_t i = 0; i < pts.size(); i++) {
        vec3 t = i + 1 < pts.size() ? normalize(pts[i + 1] - pts[i]) : normalize(pts[i] - pts[i - 1]);
        if (i > 0) {
            acc += length(pts[i] - pts[i - 1]);
            // parallel transport: rotate previous frame by the tangent change
            quat q = quatFromTo(out.back().t, t);
            f = rotate(q, out.back().f);
        }
        f = normalize(f - t * dot(f, t));
        vec3 sd = cross(t, f);
        if (mirror) sd = -sd;
        PathS ps;
        ps.p = pts[i];
        ps.t = t;
        ps.f = f;
        ps.s = sd;
        ps.along = acc;
        out.push_back(ps);
    }
}

static PathS pathAt(const std::vector<PathS>& ps, float along) {
    if (along <= ps.front().along) return ps.front();
    if (along >= ps.back().along) return ps.back();
    size_t lo = 0, hi = ps.size() - 1;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (ps[mid].along <= along) lo = mid;
        else hi = mid;
    }
    const PathS &a = ps[lo], &b = ps[hi];
    float t = (along - a.along) / Max(b.along - a.along, 1e-6f);
    PathS r;
    r.p = lerp(a.p, b.p, t);
    r.t = normalize(lerp(a.t, b.t, t));
    r.f = normalize(lerp(a.f, b.f, t));
    r.s = normalize(lerp(a.s, b.s, t));
    r.along = along;
    return r;
}

static float closestAlong(const std::vector<PathS>& ps, vec3 p) {
    float best = 1e9f, a = 0.f;
    for (const PathS& q : ps) {
        float d = length2(q.p - p);
        if (d < best) { best = d; a = q.along; }
    }
    return a;
}

// ------------------------------------------------------------------------------------------------
// Torso grid

struct TorsoGrid {
    int N = 32, R = 0;
    int rowArmpit = 0, rowShoulder = 0;
    int holeC0[2] = {0, 0}, holeC1[2] = {0, 0};   // vertex column ranges (inclusive), increasing index order
    std::vector<u32> vi;
    std::vector<float> theta;
    vec3 capC;
    std::vector<u32> seam;   // crotch seam: back, mid, front
    u32 at(int j, int i) const { return vi[(size_t)j * N + (size_t)(((i % N) + N) % N)]; }
    bool inHoleInterior(int j, int i) const {
        if (j <= rowArmpit || j >= rowShoulder) return false;
        i = ((i % N) + N) % N;
        for (int sd = 0; sd < 2; sd++)
            if (i > holeC0[sd] && i < holeC1[sd]) return true;
        return false;
    }
    bool cellInHole(int j, int i) const {
        if (j < rowArmpit || j >= rowShoulder) return false;
        i = ((i % N) + N) % N;
        for (int sd = 0; sd < 2; sd++)
            if (i >= holeC0[sd] && i < holeC1[sd]) return true;
        return false;
    }
};

static BVert skinVert(const BuildCtx& c, vec3 p, u8 part, u8 side, const SkinW& sw, float pa, float pb, vec2 uv) {
    BVert v;
    v.p = p;
    v.n = vec3(0, 0, 1);
    v.t = vec3(1, 0, 0);
    v.uv = uv;
    v.col = c.skin;
    v.mat = MAT_SKIN;
    v.part = part;
    v.side = side;
    v.sw = sw;
    v.pa = pa;
    v.pb = pb;
    return v;
}

static float torsoAxisY(const BodyDims& D, float z) {
    const vec3* J = D.J;
    const float s = D.s;
    float y0 = -0.012f * s, y1 = J[B_SPINE1].y + 0.03f * s, y2 = J[B_CHEST].y + 0.032f * s, y3 = J[B_NECK].y + 0.036f * s;
    if (z <= D.zHip) return y0;
    if (z <= D.zWaist) return Lerp(y0, y1, lstep(D.zHip, D.zWaist, z));
    if (z <= J[B_CHEST].z) return Lerp(y1, y2, lstep(D.zWaist, J[B_CHEST].z, z));
    return Lerp(y2, y3, lstep(J[B_CHEST].z, D.zAcromion, z));
}

static void buildTorso(BuildCtx& c, TorsoGrid& T) {
    const BodyDims& D = *c.D;
    const vec3* J = D.J;
    const float s = D.s;
    MeshB& m = c.m;
    const int N = T.N;
    T.theta.resize(N);
    for (int i = 0; i < N; i++) T.theta[i] = kTwoPi * i / N;
    // rows: horizontal from crotch to armpit, then a "cap" of rays from the armpit center up to the neck base
    std::vector<float> zs;
    int nLow = Max(16, (int)lrintf((D.zArmpit - D.zCrotch) / (0.022f * s)));
    for (int k = 0; k <= nLow; k++) zs.push_back(Lerp(D.zCrotch, D.zArmpit, (float)k / nLow));
    const float capFr[] = {0.2f, 0.4f, 0.58f, 0.78f, 1.0f};
    const int nCap = 5;
    T.rowArmpit = nLow;
    T.rowShoulder = nLow + 3;
    T.R = nLow + 1 + nCap;
    T.vi.assign((size_t)T.R * N, 0xffffffffu);
    T.capC = vec3(0, torsoAxisY(D, D.zArmpit), D.zArmpit);
    // armhole columns
    int q = N / 4;
    T.holeC0[1] = q - 2;
    T.holeC1[1] = q + 2;
    T.holeC0[0] = 3 * q - 2;
    T.holeC1[0] = 3 * q + 2;
    // neck base target ring
    float nbR = D.neckR * 1.18f, nbY = J[B_NECK].y + 0.036f * s;
    auto neckBase = [&](float th) {
        float zf = Lerp(D.zNeckFront, D.zNeckBack, 0.5f - 0.5f * cosf(th)) + 0.016f * s * Sq(sinf(th));
        return vec3(nbR * sinf(th), nbY + nbR * 0.92f * cosf(th), zf);
    };
    const u32 mask = MK_TORSO;
    for (int j = 0; j < T.R; j++) {
        for (int i = 0; i < N; i++) {
            if (T.inHoleInterior(j, i)) continue;
            float th = T.theta[i];
            vec3 dh(sinf(th), cosf(th), 0.f);
            vec3 o, dir;
            if (j <= nLow) {
                float z = zs[j];
                o = vec3(0, torsoAxisY(D, z), z);
                dir = dh;
            } else {
                float fr = capFr[j - nLow - 1];
                o = T.capC;
                vec3 tn = normalize(neckBase(th) - o);
                dir = normalize(lerp(dh, tn, fr));
            }
            float t = c.sdf.castOut(o, dir, mask, 0.45f * s);
            vec3 p = o + dir * t;
            BVert v = skinVert(c, p, PART_TORSO, p.x < 0.f ? 0 : 1, torsoWeights(D, p), p.z, th, vec2(uWrap(th, kPi, 0.16f * s), p.z));
            v.uPer = kTwoPi * 0.16f * s;
            v.pc = j <= nLow ? 0.8f * (float)j / nLow : 0.8f + 0.2f * capFr[j - nLow - 1];
            v.t = vec3(cosf(th), -sinf(th), 0.f);
            v.axisPt = o;
            T.vi[(size_t)j * N + i] = m.add(v);
        }
    }
    // faces
    for (int j = 0; j + 1 < T.R; j++)
        for (int i = 0; i < N; i++) {
            if (T.cellInHole(j, i)) continue;
            u32 a0 = T.at(j, i), a1 = T.at(j, i + 1), b0 = T.at(j + 1, i), b1 = T.at(j + 1, i + 1);
            m.quad(a0, b0, b1, a1);
        }
    // crotch seam (back, mid, front) at the midline under the bottom ring
    vec3 fc = m.v[T.at(0, 0)].p, bc = m.v[T.at(0, N / 2)].p;
    const u32 seamMask = MK_TORSO | MK_LEG_L | MK_LEG_R;
    for (int k = 0; k < 3; k++) {
        float f = (k + 1) / 4.f;   // from back to front
        vec3 p = lerp(bc, fc, f);
        p.z = D.zCrotch - (0.012f + 0.012f * sinf(f * kPi)) * s;
        p.x = 0.f;
        p = c.sdf.project(p, seamMask, 6);
        p.x = 0.f;
        WAcc acc;
        acc.add(B_PELVIS, 0.6f);
        acc.add(B_THIGH_L, 0.2f);
        acc.add(B_THIGH_R, 0.2f);
        BVert v = skinVert(c, p, PART_TORSO, 0, acc.finish(), p.z, kPi, vec2(0, p.z));
        v.pc = -0.02f;
        v.t = vec3(0, 1, 0);
        v.axisPt = vec3(0, p.y, p.z + 0.05f * s);
        T.seam.push_back(m.add(v));
    }
    // torso top ring for the neck
    c.torsoTop.clear();
    for (int i = 0; i < N; i++) c.torsoTop.push_back(T.at(T.R - 1, i));
}

// Sample a closed loop (vertex positions with azimuths increasing around the loop) at azimuth `a`, wrap aware.
static vec3 sampleLoopAt(const std::vector<vec3>& pos, std::vector<float> ang, float a) {
    int k = (int)ang.size();
    // unwrap to a monotonic sequence starting in (-pi, pi]
    if (ang[0] > kPi) ang[0] -= kTwoPi;
    for (int i = 1; i < k; i++)
        while (ang[i] < ang[i - 1]) ang[i] += kTwoPi;
    while (a < ang[0]) a += kTwoPi;
    while (a >= ang[0] + kTwoPi) a -= kTwoPi;
    int i0 = k - 1;
    for (int i = 0; i + 1 < k; i++)
        if (a >= ang[i] && a < ang[i + 1]) { i0 = i; break; }
    int i1 = (i0 + 1) % k;
    float a0 = ang[i0], a1 = i1 == 0 ? ang[0] + kTwoPi : ang[i1];
    float t = Saturate((a - a0) / Max(a1 - a0, 1e-6f));
    return lerp(pos[i0], pos[i1], t);
}

// Sort loop vertices by angle around an axis frame.
static void sortLoopByAngle(const MeshB& m, std::vector<u32>& loop, vec3 o, vec3 f, vec3 sd) {
    std::vector<std::pair<float, u32>> a;
    for (u32 v : loop) {
        vec3 d = m.v[v].p - o;
        float ang = atan2f(dot(d, sd), dot(d, f));
        if (ang < 0.f) ang += kTwoPi;
        a.push_back(std::make_pair(ang, v));
    }
    std::sort(a.begin(), a.end(), [](const std::pair<float, u32>& x, const std::pair<float, u32>& y) { return x.first < y.first; });
    loop.clear();
    for (auto& e : a) loop.push_back(e.second);
}

// Resample a closed loop (sorted by angle around o/f/sd) at uniform angles.
static void resampleLoopAngles(const MeshB& m, const std::vector<u32>& loop, vec3 o, vec3 f, vec3 sd, int n, std::vector<vec3>& out) {
    int k = (int)loop.size();
    std::vector<float> ang(k);
    std::vector<vec3> pos(k);
    for (int i = 0; i < k; i++) {
        vec3 d = m.v[loop[i]].p - o;
        float a = atan2f(dot(d, sd), dot(d, f));
        if (a < 0.f) a += kTwoPi;
        ang[i] = a;
        pos[i] = m.v[loop[i]].p;
    }
    out.resize(n);
    for (int j = 0; j < n; j++) out[j] = sampleLoopAt(pos, ang, kTwoPi * j / n);
}

// Bridge a junction loop on the torso to a limb ring by interpolated rings projected on the union surface.
// Returns the vertex indices of the last transition ring (uniform angles, n verts).
static void bridgeJunction(BuildCtx& c, const std::vector<u32>& loopSorted, const PathS& ringFrame, const std::vector<vec3>& ringPos,
                           int nTrans, u32 projMask, bool flip, std::vector<u32>& lastRing,
                           const std::function<SkinW(vec3, float)>& weightFn, u8 part, u8 side, float along0, float along1) {
    MeshB& m = c.m;
    int n = (int)ringPos.size();
    std::vector<vec3> loopRes;
    resampleLoopAngles(m, loopSorted, ringFrame.p, ringFrame.f, ringFrame.s, n, loopRes);
    vec3 loopC(0);
    for (u32 li : loopSorted) loopC += m.v[li].p;
    loopC /= (float)Max((size_t)1, loopSorted.size());
    std::vector<u32> prev = loopSorted;
    bool first = true;
    for (int r = 1; r <= nTrans; r++) {
        float t = (float)r / (nTrans + 1);
        std::vector<u32> ring(n);
        for (int k = 0; k < n; k++) {
            vec3 p = lerp(loopRes[k], ringPos[k], t);
            p = c.sdf.project(p, projMask, 5);
            float along = Lerp(along0, along1, t);
            float th = kTwoPi * k / n;
            BVert v = skinVert(c, p, part, side, weightFn(p, t), along, th, vec2(uWrap(th, 1.5f * kPi, 0.06f * c.D->s), along));
            v.uPer = kTwoPi * 0.06f * c.D->s;
            v.pc = along / (c.D->thigh + c.D->shin + c.D->footLen);
            v.t = normalize(ringFrame.f * -sinf(th) + ringFrame.s * cosf(th));
            v.axisPt = lerp(loopC, ringFrame.p, t);
            ring[k] = m.add(v);
        }
        if (first) stitchLoops(m, prev, ring, true, flip);
        else
            for (int k = 0; k < n; k++) m.quadMirror(flip, prev[k], ring[k], ring[(k + 1) % n], prev[(k + 1) % n]);
        prev = ring;
        first = false;
    }
    lastRing = prev;
}

// Generic limb ring caster.
static void castLimbRing(BuildCtx& c, const PathS& ps, int n, u32 mask, float tMax, std::vector<vec3>& out) {
    out.resize(n);
    for (int k = 0; k < n; k++) {
        float th = kTwoPi * k / n;
        vec3 dir = ps.f * cosf(th) + ps.s * sinf(th);
        float t = c.sdf.castOut(ps.p, dir, mask, tMax);
        out[k] = ps.p + dir * t;
    }
}

// Determine winding for a tube whose rings advance along t with angles increasing from f towards s.
static bool tubeFlip(const PathS& ps) {
    // quad (a0, b0, b1, a1) normal = cross(T, U) where U = around direction at angle 0 = s
    vec3 nrm = cross(ps.t, ps.s);
    return dot(nrm, ps.f) < 0.f;
}

// ------------------------------------------------------------------------------------------------
// Legs

static void buildLeg(BuildCtx& c, TorsoGrid& T, int side) {
    const BodyDims& D = *c.D;
    const vec3* J = D.J;
    const float s = D.s, lift = D.lift;
    MeshB& m = c.m;
    const int o = side ? 4 : 0;
    const u32 LM = side ? MK_LEG_R : MK_LEG_L;
    const u32 FM = side ? MK_FOOT_R : MK_FOOT_L;
    vec3 hip = J[B_THIGH_L + o], knee = J[B_CALF_L + o], ank = J[B_FOOT_L + o];
    // path: hip -> knee -> down the shin, then a circular arc (radius Rb, centred in front of the shin) around the
    // heel into the foot axis, then along the foot to the toes.
    const float sx = side ? 1.f : -1.f;
    const float Rb = 0.05f * s;
    const float footAxisZ = lift + 0.036f * s;
    vec3 arcC(ank.x, ank.y + Rb, footAxisZ + Rb);
    std::vector<vec3> cp;
    cp.push_back(hip);
    cp.push_back(lerp(hip, knee, 0.5f));
    cp.push_back(knee);
    vec3 arcStart(ank.x, ank.y, arcC.z);
    cp.push_back(lerp(knee, arcStart, 0.5f));
    cp.push_back(lerp(knee, arcStart, 0.85f));
    for (int k = 0; k <= 6; k++) {
        float ph = kHalfPi * k / 6.f;
        cp.push_back(vec3(ank.x, arcC.y - Rb * cosf(ph), arcC.z - Rb * sinf(ph)));
    }
    cp.push_back(vec3(ank.x - sx * 0.001f * s, ank.y + D.ballFwd * 0.62f, Lerp(footAxisZ, lift + 0.021f * s, 0.55f)));
    cp.push_back(vec3(ank.x - sx * 0.003f * s, ank.y + D.ballFwd, lift + 0.021f * s));
    cp.push_back(vec3(ank.x - sx * 0.006f * s, ank.y + D.toeFwd - 0.014f * s, lift + 0.015f * s));
    std::vector<PathS> path;
    buildPath(cp, vec3(0, 1, 0), side == 0, 0.003f, path);
    LegInfo li;
    li.kneeA = closestAlong(path, knee);
    li.ankleA = closestAlong(path, ank);
    li.ballA = closestAlong(path, cp[cp.size() - 2]);
    float arcA0 = closestAlong(path, arcStart);
    float arcLen = kHalfPi * Rb;
    float footMaskA = arcA0 + arcLen * 0.4f;
    float endA = path.back().along;
    // ring positions along the path
    std::vector<float> rings;
    float a0 = (hip.z - D.zCrotch) + 0.05f * s;
    float a = a0;
    while (a < li.kneeA - 0.085f * s) { rings.push_back(a); a += 0.027f * s; }
    // dense around the knee (bending, trouser folds) and over the lower shin (hem stacking)
    for (float k = -0.084f; k <= 0.0841f; k += 0.0105f) rings.push_back(li.kneeA + k * s);
    a = li.kneeA + 0.11f * s;
    float stackA = arcA0 - 0.13f * s;
    while (a < stackA - 0.01f * s) { rings.push_back(a); a += 0.03f * s; }
    a = Max(a, stackA);
    while (a < arcA0 - 0.03f * s) { rings.push_back(a); a += 0.011f * s; }
    for (int k = 0; k <= 6; k++) rings.push_back(arcA0 - 0.015f * s + (arcLen + 0.02f * s) * k / 6.f);
    a = arcA0 + arcLen + 0.028f * s;
    while (a < endA - 0.006f * s) { rings.push_back(a); a += 0.022f * s; }
    const int NL = 18;
    const bool flip = tubeFlip(path[0]);
    // junction loop: half the torso bottom ring + crotch seam
    std::vector<u32> loop;
    int N = T.N;
    if (side == 1) for (int i = 0; i <= N / 2; i++) loop.push_back(T.at(0, i));
    else for (int i = N / 2; i <= N; i++) loop.push_back(T.at(0, i));
    for (u32 v : T.seam) loop.push_back(v);
    PathS r0 = pathAt(path, rings[0]);
    sortLoopByAngle(m, loop, r0.p, r0.f, r0.s);
    std::vector<vec3> ringPos;
    castLimbRing(c, r0, NL, LM | FM, 0.3f * s, ringPos);
    std::vector<u32> prev;
    auto wfn = [&](vec3 p, float t) {
        (void)t;
        return legWeights(D, side, closestAlong(path, p), p, li);
    };
    bridgeJunction(c, loop, r0, ringPos, 2, LM | MK_TORSO, flip, prev, wfn, PART_LEG, (u8)side, hip.z - D.zCrotch, rings[0]);
    // regular rings
    for (size_t r = 0; r < rings.size(); r++) {
        PathS ps = pathAt(path, rings[r]);
        u32 rm = ps.along > footMaskA ? FM : (LM | FM);
        if (r > 0) castLimbRing(c, ps, NL, rm, 0.3f * s, ringPos);
        std::vector<u32> ring(NL);
        for (int k = 0; k < NL; k++) {
            float th = kTwoPi * k / NL;
            vec3 p = ringPos[k];
            BVert v = skinVert(c, p, PART_LEG, (u8)side, legWeights(D, side, ps.along, p, li), ps.along, th,
                               vec2(uWrap(th, 1.5f * kPi, 0.06f * s), ps.along));
            v.uPer = kTwoPi * 0.06f * s;
            v.pc = ps.along / endA;
            v.t = normalize(ps.f * -sinf(th) + ps.s * cosf(th));
            v.axisPt = ps.p;
            if (ps.along > arcA0 + arcLen * 0.5f && th > kPi * 0.6f && th < kPi * 1.4f) v.flags |= BuildCtx::F_SOLE;
            ring[k] = m.add(v);
        }
        for (int k = 0; k < NL; k++) m.quadMirror(flip, prev[k], ring[k], ring[(k + 1) % NL], prev[(k + 1) % NL]);
        prev = ring;
    }
    // toe tip cap
    PathS pe = path.back();
    float tt = c.sdf.castOut(pe.p, pe.t, FM, 0.1f * s);
    BVert tip = skinVert(c, pe.p + pe.t * tt, PART_LEG, (u8)side, legWeights(D, side, endA, pe.p, li), endA + tt, 0.f, vec2(0, endA + tt));
    tip.pc = 1.f;
    tip.axisPt = pe.p;
    u32 ti = m.add(tip);
    for (int k = 0; k < NL; k++) m.triMirror(flip, prev[k], ti, prev[(k + 1) % NL]);
}

// ------------------------------------------------------------------------------------------------
// Arms and hands

static void buildArm(BuildCtx& c, TorsoGrid& T, int side) {
    const BodyDims& D = *c.D;
    const vec3* J = D.J;
    const float s = D.s;
    MeshB& m = c.m;
    const int o = side ? 4 : 0;
    const u32 AM = side ? MK_ARM_R : MK_ARM_L, HM = side ? MK_HAND_R : MK_HAND_L;
    vec3 gh = J[B_UPPERARM_L + o], el = J[B_FOREARM_L + o], wr = J[B_HAND_L + o], kn = J[side ? B_FINGERS_R : B_FINGERS_L];
    std::vector<vec3> cp = {gh, el, wr, kn};
    std::vector<PathS> path;
    buildPath(cp, vec3(0, 1, 0), side == 0, 0.004f, path);
    float eA = D.upperArm, wA = D.upperArm + D.forearm, endA = wA + D.palmLen - 0.004f * s;
    std::vector<float> rings;
    float a = 0.085f * s;
    while (a < eA - 0.075f * s) { rings.push_back(a); a += 0.026f * s; }
    // dense around the elbow (bending, sleeve folds)
    for (float k = -0.072f; k <= 0.0721f; k += 0.009f) rings.push_back(eA + k * s);
    a = eA + 0.098f * s;
    while (a < wA - 0.035f * s) { rings.push_back(a); a += 0.026f * s; }
    for (float k = -0.035f; k <= 0.0151f; k += 0.017f) rings.push_back(wA + k * s);
    a = wA + 0.032f * s;
    while (a < endA - 0.004f * s) { rings.push_back(a); a += 0.0125f * s; }
    rings.push_back(endA);
    const int NA = 16;
    const bool flip = tubeFlip(path[0]);
    // armhole loop from the torso, in perimeter order starting at the front-middle (arm angle 0), going up over
    // the shoulder (90), down the back (180) and under the armpit (270).
    std::vector<u32> loop;
    {
        int j0 = T.rowArmpit, j1 = T.rowShoulder, jm = (j0 + j1) / 2;
        int cf = side ? T.holeC0[1] : T.holeC1[0];
        int cb = side ? T.holeC1[1] : T.holeC0[0];
        int dc = side ? 1 : -1;
        for (int j = jm; j <= j1; j++) loop.push_back(T.at(j, cf));
        for (int i = cf + dc; i != cb + dc; i += dc) loop.push_back(T.at(j1, i));
        for (int j = j1 - 1; j >= j0; j--) loop.push_back(T.at(j, cb));
        for (int i = cb - dc; i != cf - dc; i -= dc) loop.push_back(T.at(j0, i));
        for (int j = j0 + 1; j < jm; j++) loop.push_back(T.at(j, cf));
    }
    PathS r0 = pathAt(path, rings[0]);
    std::vector<vec3> ringPos;
    u32 mask = AM | HM;
    castLimbRing(c, r0, NA, mask, 0.25f * s, ringPos);
    std::vector<u32> prev;
    {
        // resample the loop uniformly by arc length (topological angle correspondence)
        int nl = (int)loop.size();
        std::vector<float> cum(nl + 1, 0.f);
        for (int i = 0; i < nl; i++) cum[i + 1] = cum[i] + length(m.v[loop[(i + 1) % nl]].p - m.v[loop[i]].p);
        std::vector<vec3> loopRes(NA);
        for (int k = 0; k < NA; k++) {
            float target = cum[nl] * k / NA;
            int i = 0;
            while (i < nl - 1 && cum[i + 1] < target) i++;
            float t = Saturate((target - cum[i]) / Max(cum[i + 1] - cum[i], 1e-6f));
            loopRes[k] = lerp(m.v[loop[i]].p, m.v[loop[(i + 1) % nl]].p, t);
        }
        vec3 loopC(0);
        for (u32 li : loop) loopC += m.v[li].p;
        loopC /= (float)nl;
        const int NT = 3;
        prev = loop;
        for (int r = 1; r <= NT; r++) {
            float t = (float)r / (NT + 1);
            std::vector<u32> ring(NA);
            for (int k = 0; k < NA; k++) {
                vec3 p = lerp(loopRes[k], ringPos[k], t);
                p = c.sdf.project(p, AM | MK_TORSO, 6);
                float th = kTwoPi * k / NA;
                float al = rings[0] * t;
                SkinW sw = lerpSkin(torsoWeights(D, p), armWeights(D, side, al), sstep(t));
                BVert v = skinVert(c, p, PART_ARM, (u8)side, sw, al, th, vec2(uWrap(th, 1.5f * kPi, 0.045f * s), al));
                v.uPer = kTwoPi * 0.045f * s;
                v.pc = al / endA;
                v.t = normalize(r0.f * -sinf(th) + r0.s * cosf(th));
                v.axisPt = lerp(loopC, r0.p, t);
                ring[k] = m.add(v);
            }
            if (r == 1) stitchLoops(m, prev, ring, true, flip);
            else
                for (int k = 0; k < NA; k++) m.quadMirror(flip, prev[k], ring[k], ring[(k + 1) % NA], prev[(k + 1) % NA]);
            prev = ring;
        }
    }
    for (size_t r = 0; r < rings.size(); r++) {
        PathS ps = pathAt(path, rings[r]);
        if (r > 0) castLimbRing(c, ps, NA, mask, 0.25f * s, ringPos);
        std::vector<u32> ring(NA);
        bool hand = ps.along > wA - 0.01f * s;
        for (int k = 0; k < NA; k++) {
            float th = kTwoPi * k / NA;
            vec3 p = ringPos[k];
            SkinW sw = armWeights(D, side, ps.along);
            if (hand) {
                float wt = thenarWeight(D, side, p);
                if (wt > 0.f) sw = lerpSkin(sw, skin1(phalanxBone(side == 1, 4, 0)), wt);
            }
            BVert v = skinVert(c, p, hand ? PART_HAND : PART_ARM, (u8)side, sw, ps.along, th, vec2(uWrap(th, 1.5f * kPi, 0.045f * s), ps.along));
            v.uPer = kTwoPi * 0.045f * s;
            v.pc = ps.along / endA;
            v.t = normalize(ps.f * -sinf(th) + ps.s * cosf(th));
            v.axisPt = ps.p;
            // palm side (towards the palm normal = -s)
            if (ps.along > wA - 0.02f * s && cosf(th - kPi * 1.5f) > 0.3f) {
                v.flags |= BuildCtx::F_PALM;
                v.col = lerp(c.skin, c.palmCol, sstep(0.3f, 0.8f, cosf(th - kPi * 1.5f)));
            }
            ring[k] = m.add(v);
        }
        for (int k = 0; k < NA; k++) m.quadMirror(flip, prev[k], ring[k], ring[(k + 1) % NA], prev[(k + 1) % NA]);
        prev = ring;
    }
    // knuckle cap
    PathS pe = path.back();
    float tt = c.sdf.castOut(pe.p - pe.t * 0.01f * s, pe.t, mask, 0.06f * s);
    vec3 tipP = pe.p - pe.t * 0.01f * s + pe.t * tt;
    BVert tip = skinVert(c, tipP, PART_HAND, (u8)side, armWeights(D, side, endA), endA, 0.f, vec2(0, endA));
    tip.pc = 1.f;
    tip.axisPt = pe.p;
    u32 ti = m.add(tip);
    for (int k = 0; k < NA; k++) m.triMirror(flip, prev[k], ti, prev[(k + 1) % NA]);
}

// Fingers and thumbs: one tube per digit along its joint polyline (a base inside the palm / thenar, the knuckle or
// the thumb's CMC joint, the middle and end joints, a rounded tip), elliptical in section with palmar pads, dorsal
// knuckle bumps, joint creases (geometry plus the skin shader's crease channel) and a separate nail plate; skinned
// per phalanx (hand -> proximal -> middle -> distal) with blend zones centred on the joints. The dorsal side hands
// over to the next bone a little later than the palmar side, so a flexed knuckle keeps its bony corner while the
// palm side folds.
struct Digit {
    vec3 P[5];      // base, joint 1 (knuckle; thumb: CMC), joint 2, joint 3, tip
    float S[5];     // arc length of each point from the base
    int bone[4];    // skin bone of each segment
    vec3 back;      // dorsal (nail side) direction
    float r0;       // radius at joint 1
    bool thumb;
};

// Centre point and tangent of a digit's axis at arc length u (extrapolated beyond the base and the tip).
static void digitAxis(const Digit& g, float u, vec3& p, vec3& t) {
    int i = 0;
    while (i < 3 && u > g.S[i + 1]) i++;
    vec3 d = normalize(g.P[i + 1] - g.P[i]);
    p = g.P[i] + d * (u - g.S[i]);
    t = d;
    for (int j = 1; j <= 3; j++) {
        float x = (u - g.S[j]) / 0.0035f;
        if (fabsf(x) < 1.f) t = normalize(lerp(normalize(g.P[j] - g.P[j - 1]), normalize(g.P[j + 1] - g.P[j]), sstep(0.5f + 0.5f * x)));
    }
}

// Section half-axes at arc length u without the tip rounding: a = half width, bd / bp = dorsal / palmar half
// thickness.
static void digitSection(const Digit& g, float u, float s, float& a, float& bd, float& bp) {
    const float S1 = g.S[1], S2 = g.S[2], S3 = g.S[3], S4 = g.S[4];
    const float l1 = S2 - S1, l2 = S3 - S2, l3 = S4 - S3;
    float R;
    if (!g.thumb) R = g.r0 * (u < S3 ? Lerp(1.f, 0.875f, sstep(S1, S3, u)) : Lerp(0.875f, 0.85f, sstep(S3, S4, u)));
    else R = g.r0 * (u < S2 ? Lerp(0.95f, 1.04f, sstep(S1, S2, u)) : (u < S3 ? Lerp(1.04f, 0.95f, sstep(S2, S3, u)) : 0.94f));   // the thenar carries the base
    float knuckle = 0.07f * bump(u, S2, 0.0035f * s) + 0.05f * bump(u, S3, 0.003f * s);
    if (!g.thumb) knuckle += 0.14f * bump(u, S1 - 0.0015f * s, 0.0055f * s);
    float pads = 0.08f * bump(u, S2 + 0.5f * l2, 0.3f * l2) + 0.11f * bump(u, S3 + 0.52f * l3, 0.28f * l3);
    if (!g.thumb) pads += 0.07f * bump(u, S1 + 0.62f * l1, 0.25f * l1);
    float creases = 0.075f * bump(u, S2, 0.0022f * s) + 0.06f * bump(u, S3 - 0.0008f * s, 0.002f * s);
    a = R * 1.05f * (1.f + 0.035f * bump(u, S2, 0.004f * s) + 0.03f * bump(u, S3, 0.003f * s));
    bd = R * 0.86f * (1.f + knuckle);
    bp = R * 0.95f * (1.f + pads - creases);
    (void)l1;
}

static float digitCapLen(const Digit& g, float s) {
    float a, bd, bp;
    digitSection(g, g.S[4], s, a, bd, bp);
    return 1.1f * a;
}

// Surface point of a digit at arc length u and section angle th (0 = dorsal), tip rounding included; `n` receives the
// section normal (for offsets), `cap` whether the point lies on the rounded tip.
static vec3 digitPoint(const Digit& g, float u, float th, float s, vec3* nOut = nullptr, bool round = true) {
    vec3 p, t;
    digitAxis(g, u, p, t);
    vec3 dor = normalize(g.back - t * dot(g.back, t)), lat = cross(t, dor);
    float a, bd, bp;
    digitSection(g, Min(u, g.S[4]), s, a, bd, bp);
    const float capL = digitCapLen(g, s), c0 = g.S[4] - capL;
    if (round && u > c0) {
        // fingertip: the pad curls up to meet the nail line, which stays nearly straight until the free edge; seen
        // from above the tip is rounded
        float pr = Saturate((u - c0) / capL), pr2 = pr * pr;
        p += dor * (a * 0.2f * pr2);
        a *= sqrtf(Max(0.f, 1.f - pr2 * pr2 * sqrtf(pr)));
        bd *= sqrtf(Max(0.f, 1.f - pr2 * pr2));
        bp *= sqrtf(Max(0.f, 1.f - pr2));
    }
    float cd = cosf(th), sl = sinf(th);
    float b = cd > 0.f ? bd : bp;
    if (nOut) *nOut = normalize(dor * (cd / Max(b, 1e-5f)) + lat * (sl / Max(a, 1e-5f)));
    return p + dor * (cd * b) + lat * (sl * a);
}

static void buildFingers(BuildCtx& c, int side) {
    const BodyDims& D = *c.D;
    const CharacterDesc& d = *c.d;
    const vec3* J = D.J;
    const float s = D.s;
    MeshB& m = c.m;
    const bool right = side == 1;
    const int hb = right ? B_HAND_R : B_HAND_L;
    const vec3 pn = D.palmN[side];
    const int NF = 10;
    const float lum = dot(c.skin, vec3(0.3f, 0.59f, 0.11f));
    const float fair = sstep(0.1f, 0.45f, lum);
    // knuckles: darker on dark skin, redder on fair skin; wrinkles deepen with age
    const vec3 knuckleTint = lerp(vec3(0.8f, 0.77f, 0.77f), vec3(1.f, 0.9f, 0.89f), fair);
    const float wrinkle = 0.7f + 0.8f * D.age;
    // nails: the plate shows the pink bed on fair skin, a lighter beige-pink than the skin on dark skin; some women
    // wear them longer and polished (one colour on all ten)
    Rng nr(hash32(d.seed * 0x3C6EF372u + 0x1Bu));
    const bool polish = D.fem > 0.5f && nr.chance(0.38f);
    const float nailExt = D.fem > 0.5f ? (polish ? nr.range(0.0012f, 0.0035f) : nr.range(0.f, 0.0012f)) : nr.range(0.f, 0.0006f);
    static const vec3 kPolish[6] = {vec3(0.42f, 0.02f, 0.03f), vec3(0.62f, 0.2f, 0.28f), vec3(0.58f, 0.4f, 0.34f),
                                    vec3(0.2f, 0.02f, 0.05f), vec3(0.04f, 0.035f, 0.04f), vec3(0.7f, 0.08f, 0.1f)};
    const vec3 polishCol = kPolish[nr.irange(0, 5)];
    vec3 nailCol = lerp(vmax(mulColor(c.skin, vec3(1.6f, 1.45f, 1.45f)), vec3(0.3f, 0.2f, 0.17f)), vec3(0.7f, 0.49f, 0.46f), fair);
    nailCol = lerp(nailCol, vec3(0.62f, 0.55f, 0.43f), 0.25f * sstep(0.5f, 1.f, D.age));
    const vec3 edgeCol = lerp(vec3(0.78f, 0.74f, 0.68f), nailCol, 0.25f);
    const vec3 bedCol = lerp(c.skin, lerp(c.skin * 1.3f, vec3(0.62f, 0.42f, 0.4f), fair), 0.55f);
    for (int f = 0; f < 5; f++) {
        Digit g;
        g.thumb = f == 4;
        const int b0 = phalanxBone(right, f, 0);
        if (!g.thumb) {
            g.P[1] = J[b0];
            g.P[2] = J[b0 + 1];
            g.P[3] = J[b0 + 2];
            g.P[4] = D.fingTip[side][f];
            vec3 fdir = normalize(cross(pn, D.fingAx[side][f]));   // straight finger direction (flexion axis x palm)
            g.P[0] = g.P[1] - fdir * (0.013f * s);
            g.bone[0] = hb;
            g.bone[1] = b0;
            g.bone[2] = b0 + 1;
            g.bone[3] = b0 + 2;
            g.back = -pn;
        } else {
            g.P[1] = J[b0];
            g.P[2] = J[b0 + 1];
            g.P[3] = J[b0 + 2];
            g.P[4] = D.fingTip[side][4];
            g.P[0] = g.P[1] - normalize(g.P[2] - g.P[1]) * (0.006f * s);
            g.bone[0] = hb;
            g.bone[1] = b0;
            g.bone[2] = b0 + 1;
            g.bone[3] = b0 + 2;
            g.back = -thumbPadDir(pn);
        }
        g.S[0] = 0.f;
        for (int i = 1; i < 5; i++) g.S[i] = g.S[i - 1] + length(g.P[i] - g.P[i - 1]);
        g.r0 = D.fingR[side][f];
        const float S1 = g.S[1], S2 = g.S[2], S3 = g.S[3], S4 = g.S[4];
        const float l1 = S2 - S1, l2 = S3 - S2, l3 = S4 - S3;
        const float capL = digitCapLen(g, s), c0 = S4 - capL;
        std::vector<float> us;
        if (!g.thumb) {
            const float u[] = {0.f, S1 - 0.0022f * s, S1 + 0.0028f * s, S1 + 0.009f * s, S1 + 0.5f * l1, S2 - 0.0038f * s, S2,
                               S2 + 0.0038f * s, S2 + 0.5f * l2, S3 - 0.0028f * s, S3 + 0.0028f * s, S3 + 0.42f * l3};
            us.assign(u, u + sizeof(u) / sizeof(u[0]));
        } else {
            const float u[] = {0.f, S1 + 0.3f * l1, S1 + 0.62f * l1, S2 - 0.0045f * s, S2, S2 + 0.0045f * s, S2 + 0.5f * l2, S3 - 0.003f * s,
                               S3 + 0.003f * s, S3 + 0.42f * l3};
            us.assign(u, u + sizeof(u) / sizeof(u[0]));
        }
        while (!us.empty() && us.back() > c0 - 0.002f * s) us.pop_back();
        us.push_back(c0);
        us.push_back(c0 + 0.5f * capL);
        us.push_back(c0 + 0.82f * capL);
        // crease channel phase: global along the digit (linear in u, so it interpolates exactly between rings); line
        // pairs straddle the middle joint (thumb: the end joint)
        const float crSp = 0.0022f * s, crRef = g.thumb ? S3 : S2;
        std::vector<u32> prev;
        bool flip = false;
        for (size_t r = 0; r < us.size(); r++) {
            const float u = us[r];
            vec3 ap, at;
            digitAxis(g, u, ap, at);
            vec3 dor = normalize(g.back - at * dot(g.back, at)), lat = cross(at, dor);
            std::vector<u32> ring(NF);
            for (int k = 0; k < NF; k++) {
                float th = kTwoPi * k / NF;
                float cd = cosf(th);
                vec3 p = digitPoint(g, u, th, s);
                // skin weights per phalanx
                float dn = 0.5f + 0.5f * cd;
                WAcc acc;
                if (!g.thumb) {
                    float w1 = sstep(S1 + Lerp(-0.006f, -0.001f, dn) * s, S1 + Lerp(0.005f, 0.009f, dn) * s, u);
                    float w2 = sstep(S2 - 0.0035f * s, S2 + 0.0035f * s, u), w3 = sstep(S3 - 0.003f * s, S3 + 0.003f * s, u);
                    acc.add(g.bone[0], 1.f - w1);
                    acc.add(g.bone[1], w1 * (1.f - w2));
                    acc.add(g.bone[2], w2 * (1.f - w3));
                    acc.add(g.bone[3], w3);
                } else {
                    float wm = Lerp(0.5f, 1.f, sstep(0.f, S1 + 0.5f * l1, u));
                    float w2 = sstep(S2 - 0.005f * s, S2 + 0.005f * s, u), w3 = sstep(S3 - 0.0035f * s, S3 + 0.0035f * s, u);
                    acc.add(g.bone[0], (1.f - wm) * (1.f - w2));
                    acc.add(g.bone[1], wm * (1.f - w2));
                    acc.add(g.bone[2], w2 * (1.f - w3));
                    acc.add(g.bone[3], w3);
                }
                BVert v = skinVert(c, p, g.thumb ? PART_THUMB : PART_FINGER, (u8)side, acc.finish(), u, th, vec2(0.f));
                v.pc = u / S4;
                v.t = at;
                v.axisPt = ap;
                // colour: lighter palmar side, tinted knuckles, the nail bed under the plate, pinker finger pads
                float palmar = sstep(-0.15f, -0.7f, cd), dors = sstep(0.3f, 0.9f, cd);
                vec3 col = lerp(c.skin, c.palmCol, 0.75f * palmar);
                float kn = g.thumb ? 0.7f * bump(u, S3, 0.004f * s) + 0.5f * bump(u, S2, 0.005f * s)
                                   : bump(u, S1 - 0.001f * s, 0.006f * s) + 0.8f * bump(u, S2, 0.0045f * s) + 0.55f * bump(u, S3, 0.0035f * s);
                col = mulColor(col, lerp(vec3(1.f), knuckleTint, Saturate(kn) * dors));
                if (u > S3 + 0.3f * l3) col = lerp(col, bedCol, dors * sstep(S3 + 0.3f * l3, S3 + 0.45f * l3, u));
                col = lerp(col, mulColor(col, vec3(1.05f, 0.94f, 0.94f)), palmar * sstep(S3, S3 + 0.3f * l3, u));
                v.col = col;
                if (cd < -0.35f) v.flags |= BuildCtx::F_PALM;
                // creases: palmar flexion lines at the joints (and the finger's base line), dorsal knuckle wrinkles
                auto win = [&](float x, float w) { return 1.f - sstep(0.f, w, fabsf(x)); };
                float depth;
                if (!g.thumb)
                    depth = palmar * (0.34f * win(u - S2, 0.0026f * s) + 0.28f * win(u - S3 + 0.0008f * s, 0.0022f * s) +
                                      0.24f * win(u - (S1 + 0.5f * l1), 0.003f * s)) +
                            dors * wrinkle * (0.2f * win(u - S2, 0.0045f * s) + 0.13f * win(u - S3, 0.0035f * s) + 0.1f * win(u - S1, 0.004f * s));
                else
                    depth = palmar * (0.34f * win(u - S3, 0.003f * s) + 0.25f * win(u - S2, 0.0045f * s)) +
                            dors * wrinkle * (0.18f * win(u - S3, 0.004f * s) + 0.12f * win(u - S2, 0.0045f * s));
                v.uv = vec2((u - crRef) / crSp, depth);
                ring[k] = m.add(v);
            }
            if (r == 0) {
                flip = dot(cross(at, lat), dor) < 0.f;
                // close the base inside the palm (a posed hand must never show an open tube end)
                BVert bc = m.v[ring[0]];
                bc.p = ap - at * (0.002f * s);
                WAcc acc;
                for (int k = 0; k < NF; k++)
                    for (int q = 0; q < 4; q++) acc.add(m.v[ring[k]].sw.b[q], m.v[ring[k]].sw.w[q]);
                bc.sw = acc.finish();
                u32 ci = m.add(bc);
                for (int k = 0; k < NF; k++) m.triMirror(flip, ring[k], ring[(k + 1) % NF], ci);
            } else
                for (int k = 0; k < NF; k++) m.quadMirror(flip, prev[k], ring[k], ring[(k + 1) % NF], prev[(k + 1) % NF]);
            prev = ring;
        }
        {
            // rounded tip: pole at the dorsal front where the nail line ends
            vec3 p = digitPoint(g, S4, 0.f, s);
            vec3 ap, at;
            digitAxis(g, S4, ap, at);
            WAcc acc;
            acc.add(g.bone[3], 1.f);
            BVert v = skinVert(c, p, g.thumb ? PART_THUMB : PART_FINGER, (u8)side, acc.finish(), S4, 0.f, vec2((S4 - crRef) / crSp, 0.f));
            v.pc = 1.f;
            v.t = at;
            v.axisPt = ap;
            v.col = lerp(c.skin, bedCol, 0.5f);
            u32 ti = m.add(v);
            for (int k = 0; k < NF; k++) m.triMirror(flip, prev[k], ti, prev[(k + 1) % NF]);
        }
        // nail plate: from the proximal fold to the free edge at the fingertip (longer nails run on past it), 0.3 mm
        // above the distal surface, its base and sides tucked under the skin folds; a thin rim shows its thickness
        {
            const float u0 = S4 - (g.thumb ? 0.62f : 0.6f) * l3, uE = c0 + 0.85f * capL;
            // odd column count: a column on the centre line (the plate's chords stay above the skin); a second row just
            // past the fold makes the plate emerge along a clean line whatever the skin's facets underneath
            const int NU = 5, NV = 5;
            const float vRow[NV] = {0.f, 0.08f, 0.38f, 0.76f, 1.f};
            const float thMax = g.thumb ? 0.95f : 0.9f;
            const float ext = (g.thumb ? 0.6f : 1.f) * nailExt * s;
            u32 grid[NV][NU];
            vec3 rowDor[NV];
            for (int j = 0; j < NV; j++) {
                float uu = Lerp(u0, uE, vRow[j]);
                vec3 ap, at;
                digitAxis(g, uu, ap, at);
                rowDor[j] = normalize(g.back - at * dot(g.back, at));
                for (int i = 0; i < NU; i++) {
                    float x = -1.f + 2.f * (float)i / (NU - 1);
                    float th = x * thMax * (j == 0 ? 0.86f : 1.f);
                    float ua = uu + (j == 0 ? 0.0011f * s * x * x : 0.f);   // rounded proximal corners
                    vec3 n;
                    vec3 sp = digitPoint(g, ua, th, s, &n);
                    float x4 = x * x * x * x;
                    // tucked under the proximal and lateral folds, 0.3-0.4 mm proud over the bed, the free edge clear of
                    // the fingertip across its whole width
                    float side = Lerp(0.0009f, 0.00025f, sstep(0.5f, 0.95f, vRow[j]));
                    float h = (j == 0 ? -0.0005f : 0.00038f * (1.f - 0.45f * x * x)) * s - side * s * x4;
                    vec3 pos = sp + n * h;
                    if (j == NV - 1 && ext > 0.f) {
                        // long nails: continue the plate's own curve past the tip
                        vec3 pv = m.v[grid[j - 1][i]].p;
                        pos += normalize(pos - pv) * ext;
                    }
                    WAcc acc;
                    acc.add(g.bone[3], 1.f);
                    BVert v = skinVert(c, pos, PART_ACC, (u8)side, acc.finish(), ua, th, vec2(0.f));
                    v.flags |= BuildCtx::F_NAIL;
                    v.t = at;
                    v.axisPt = ap;
                    vec3 col = nailCol;
                    if (j == 0) col = lerp(col, vec3(0.8f, 0.72f, 0.68f), g.thumb ? 0.4f : 0.22f);   // lunula
                    if (j == NV - 1) col = lerp(nailCol, edgeCol, 0.4f + 0.6f * sstep(0.f, 0.0018f * s, ext));
                    if (polish) col = polishCol;
                    v.col = col;
                    grid[j][i] = m.add(v);
                }
            }
            for (int j = 0; j + 1 < NV; j++)
                for (int i = 0; i + 1 < NU; i++) m.quad(grid[j][i], grid[j][i + 1], grid[j + 1][i + 1], grid[j + 1][i]);
            // free-edge rim (the nail's thickness)
            u32 rim[NU];
            for (int i = 0; i < NU; i++) {
                BVert v = m.v[grid[NV - 1][i]];
                v.p = v.p - rowDor[NV - 1] * (0.00055f * s);
                v.col = polish ? polishCol * 0.8f : edgeCol * 0.85f;
                rim[i] = m.add(v);
            }
            for (int i = 0; i + 1 < NU; i++) m.quad(grid[NV - 1][i], grid[NV - 1][i + 1], rim[i + 1], rim[i]);
        }
    }
}

// ------------------------------------------------------------------------------------------------
// Neck: rings between the torso top ring and the head grid row 0.

static void buildNeck(BuildCtx& c) {
    const BodyDims& D = *c.D;
    MeshB& m = c.m;
    const HeadInfo& H = c.head;
    int n = H.cols;
    std::vector<u32> top(n);
    for (int k = 0; k < n; k++) top[k] = H.grid[k];
    vec3 cb(0), ct(0);
    for (u32 v : c.torsoTop) cb += m.v[v].p;
    cb /= (float)c.torsoTop.size();
    for (u32 v : top) ct += m.v[v].p;
    ct /= (float)n;
    vec3 ax = normalize(ct - cb);
    vec3 f = normalize(vec3(0, 1, 0) - ax * ax.y);
    vec3 sd = cross(f, ax);   // right side (+X)
    auto azim = [&](vec3 p, vec3 o) {
        vec3 d = p - o;
        float a = atan2f(dot(d, sd), dot(d, f));
        return a < 0.f ? a + kTwoPi : a;
    };
    // resample the torso top ring at the azimuths of the head's row 0
    int nb = (int)c.torsoTop.size();
    std::vector<float> ab(nb);
    std::vector<vec3> pb(nb);
    for (int i = 0; i < nb; i++) {
        ab[i] = azim(m.v[c.torsoTop[i]].p, cb);
        pb[i] = m.v[c.torsoTop[i]].p;
    }
    std::vector<vec3> bot(n);
    for (int k = 0; k < n; k++) bot[k] = sampleLoopAt(pb, ab, azim(m.v[top[k]].p, ct));
    const int NR = 3;
    std::vector<u32> prev;
    bool flip = dot(cross(ax, sd), f) < 0.f;
    for (int r = 1; r <= NR; r++) {
        float u = (float)r / (NR + 1);
        std::vector<u32> ring(n);
        for (int k = 0; k < n; k++) {
            vec3 pl = lerp(bot[k], m.v[top[k]].p, u);
            vec3 o = lerp(cb, ct, u);
            vec3 dir = normalize(pl - o);
            float tc = c.sdf.castOut(o, dir, MK_NECK, 0.2f * D.s);
            vec3 p = o + dir * tc;
            WAcc acc;
            float wc = 1.f - sstep(-0.1f, 0.5f, u), wh = sstep(0.55f, 1.05f, u);
            acc.add(B_CHEST, wc);
            acc.add(B_HEAD, wh);
            acc.add(B_NECK, Max(0.f, 1.f - wc - wh));
            float th = azim(p, o);
            BVert v = skinVert(c, p, PART_NECK, p.x < 0.f ? 0 : 1, acc.finish(), u, th, vec2(uWrap(th, kPi, 0.06f * D.s), p.z));
            v.uPer = kTwoPi * 0.06f * D.s;
            v.pc = 1.f + 0.2f * u;
            v.t = normalize(cross(ax, p - o));
            v.axisPt = o;
            ring[k] = m.add(v);
        }
        if (r == 1) stitchLoops(m, c.torsoTop, ring, true, flip);
        else
            for (int k = 0; k < n; k++) m.quadMirror(flip, prev[k], ring[k], ring[(k + 1) % n], prev[(k + 1) % n]);
        prev = ring;
    }
    for (int k = 0; k < n; k++) m.quadMirror(flip, prev[k], top[k], top[(k + 1) % n], prev[(k + 1) % n]);
}

// ------------------------------------------------------------------------------------------------

void buildBody(BuildCtx& c) {
    addBodyPrims(c);
    addHeadPrims(c);
    TorsoGrid T;
    buildTorso(c, T);
    buildLeg(c, T, 0);
    buildLeg(c, T, 1);
    buildArm(c, T, 0);
    buildArm(c, T, 1);
    buildHeadGrid(c);
    buildNeck(c);
    c.surfaceIdxEnd = c.m.idx.size();
    c.m.computeNormals(0, c.surfaceIdxEnd);
    {
        // head grid: analytic normals from the face field (the grid's thin triangles near the eye corners and the
        // nose would otherwise put triangulation creases into the shading); blended in above the neck junction
        const HeadInfo& H = c.head;
        for (int j = 2; j < H.rows; j++)
            for (int k = 0; k < H.cols; k++) {
                BVert& v = c.m.v[H.grid[(size_t)j * H.cols + k]];
                vec3 g = c.sdf.grad(v.p, MK_HEAD);
                if (length2(g) < 1e-10f) continue;
                vec3 gn = normalize(g);
                if (dot(gn, v.n) < 0.2f) continue;   // off the field (a grid vertex that did not land on it): keep
                v.n = normalize(lerp(v.n, gn, j >= 4 ? 1.f : 0.5f));
            }
    }
    size_t fingerIdx0 = c.m.idx.size();
    buildFingers(c, 0);
    buildFingers(c, 1);
    c.m.computeNormals(fingerIdx0, c.m.idx.size());
    buildFaceDetails(c);
    for (BVert& v : c.m.v) v.bp = v.p;
}

}  // namespace detail
}  // namespace Anim
