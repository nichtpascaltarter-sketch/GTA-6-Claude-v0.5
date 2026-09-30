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
        case SHOE_LOAFER: return 0.018f;
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
    D.handLen = handLen;
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

    // ---- face variation: wide per-seed ranges (a street crowd must not look like siblings), ancestry / sex / age
    // tendencies on top. Draws with the main stream keep their order; the extra shape parameters use a second stream.
    auto g = [&]() { return r.range(-1.f, 1.f); };
    // gaussian-ish draw in -1..1 (sum of two uniforms: extremes are rarer)
    auto g2 = [&]() { return 0.5f * (r.range(-1.f, 1.f) + r.range(-1.f, 1.f)) * 1.4f; };
    D.faceW = 1.f + 0.085f * g() + 0.04f * wc;
    D.jawW = Lerp(1.f, 0.92f, fem) * (1.f + 0.13f * g() + 0.09f * wc);
    D.chinP = 1.f + 0.35f * g();
    // women: shorter lower face, smaller nose, fuller lips, slightly larger eyes; noses and ears keep growing with age
    D.chinH = (1.f + 0.12f * g()) * Lerp(1.f, 0.94f, fem);
    D.noseL = (1.f + 0.15f * g()) * Lerp(1.f, 0.93f, fem) * (1.f + 0.05f * a);
    D.noseW = Lerp(1.f, 0.84f, fem) * (1.f + 0.16f * g()) * (1.f + 0.04f * a);
    D.noseP = Lerp(1.f, 0.86f, fem) * (1.f + 0.15f * g());
    D.noseBridge = 1.f + 0.45f * g();
    D.lipFull = Lerp(1.f, 1.1f, fem) * (1.f + 0.22f * g());
    D.lipW = 1.f + 0.12f * g();
    D.eyeSize = Lerp(1.f, 1.05f, fem) * (1.f + 0.08f * g());
    D.eyeTilt = 0.08f * g();
    D.eyeSpace = 1.f + 0.07f * g();
    D.browH = 1.f + 0.18f * g() + 0.05f * fem;
    D.browRidge = Lerp(1.f, 0.35f, fem) * (1.f + 0.35f * g());
    D.cheekB = 1.f + 0.38f * g();
    D.earSize = (1.f + 0.08f * g()) * (1.f + 0.08f * a);
    D.earOut = 1.f + 0.35f * g();
    D.foreheadSlope = 0.5f + 0.5f * g();
    D.lidFold = r.f();
    D.headLen = 1.f + 0.05f * g();
    // face height below the eyes (midface + lower face together), philtrum length, forehead height (hairline offset,
    // degrees), cheekbone height, brow shape, lip thickness ratio and nose profile / tip type
    D.faceH = 1.f + 0.06f * g2() + 0.02f * (1.f - fem);
    // (women have a shorter upper lip; it lengthens with age)
    D.philtrum = 1.f + 0.16f * g2() - 0.18f * fem + 0.12f * sstep(0.3f, 1.f, a);
    D.foreheadH = 3.2f * g2();
    D.cheekH = 0.0028f * g2();
    D.browArch = Saturate(0.5f + 0.5f * g()) * 1.2f + 0.4f;
    D.browThick = 0.75f + 0.5f * r.f();
    D.browTilt = 1.6f * g2();
    D.lipRatio = 0.9f + 0.28f * g2();
    {
        float t = r.f();   // nose type: straight, convex (hump / hooked), concave (scooped), bulbous
        D.noseScoop = 0.f;
        D.noseBulb = 1.f + 0.12f * g2();
        if (t < 0.18f) D.noseBulb = r.range(1.18f, 1.4f);
        else if (t < 0.34f) D.noseScoop = r.range(0.5f, 1.f) * Lerp(0.7f, 1.1f, fem);
    }
    {
        // separate stream: the draws above keep their values for existing seeds
        Rng q(hash32(d.seed * 0x2C1B3C6Du + 0x297A2D39u));
        auto h = [&]() { return q.range(-1.f, 1.f); };
        D.jawFlare = 0.5f + 0.5f * h();
        D.chinSquare = Saturate(Lerp(0.55f, 0.2f, fem) + 0.35f * h());
        D.chinCleft = (fem < 0.5f && q.chance(0.18f)) ? q.range(0.4f, 1.f) : 0.f;
        D.noseHump = D.noseScoop > 0.f ? 0.f : Saturate(0.3f * (1.f - fem) + 0.45f * h() + 0.2f * a);
        D.noseTipUp = 0.7f * h() + 0.25f * fem + 0.5f * D.noseScoop - 0.5f * sstep(0.6f, 1.f, D.noseHump);   // hooked with a big hump
        D.lipBow = 0.5f + 0.5f * h();
        D.asymEye = 0.0007f * h();
        D.asymBrow = 0.0012f * h();
        D.asymMouth = 0.0008f * h();
        D.asymNose = 0.0009f * h();
        D.asymChin = 0.0012f * h();
        D.asymEar = 0.12f * h();
        // ancestry: resolved from the skin tone when not given (dark -> African, light -> European, else Latin)
        int anc = d.ancestry;
        if (anc < 0 || anc > 4) {
            float lum = dot(saturate(d.skinTone), vec3(0.3f, 0.59f, 0.11f));
            anc = lum < 0.13f ? 1 : (lum > 0.42f ? 2 : 0);
        }
        D.ancestry = anc;
        // eyelids: crease 4.5-6 mm above the lashes on young European faces (higher on women), lower on African and
        // Latin faces, often absent (monolid) or low with an epicanthic fold on East Asian faces; hooding grows with age
        float creaseMm = Lerp(5.0f, 5.8f, fem) + 0.6f * h();
        float epi = 0.f;
        D.hood = Saturate(0.15f + 0.25f * q.f() + 0.55f * sstep(0.35f, 0.95f, a));
        D.apertureH = 1.f + 0.11f * h() + 0.04f * fem - 0.08f * sstep(0.5f, 1.f, a);
        D.mouthCornerUp = 0.0014f * h() - 0.0008f * sstep(0.4f, 1.f, a);   // up-turned (smiling) .. down-turned mouth
        D.eyeDepth = 0.0014f * h();                                          // protruding (+) .. deep-set (-) eyes
        switch (anc) {
            case 1:   // African / Caribbean: broader, lower-bridged nose, fuller lips, lower crease
                D.noseW *= 1.13f;
                D.noseBridge -= 0.25f;
                D.noseP *= 0.96f;
                D.lipFull *= 1.17f;
                D.lipW *= 1.04f;
                creaseMm -= 0.8f;
                D.browRidge *= 1.05f;
                break;
            case 2:   // European: narrower, higher-bridged nose, thinner lips, higher crease, deeper-set eyes
                D.noseW *= 0.95f;
                D.noseBridge += 0.18f;
                D.noseP *= 1.04f;
                D.lipFull *= 0.93f;
                creaseMm += 0.6f;
                break;
            case 3: {   // East Asian: low nasal bridge, monolid or low crease, epicanthic fold, fuller cheekbones
                D.noseBridge -= 0.4f;
                D.noseW *= 1.03f;
                D.noseP *= 0.93f;
                D.browRidge *= 0.7f;
                D.cheekB += 0.18f;
                D.faceW *= 1.025f;
                bool mono = q.chance(0.55f);
                creaseMm = mono ? 0.f : q.range(1.8f, 3.2f);
                epi = q.range(0.5f, 1.f);
                D.hood = Saturate(D.hood + 0.2f);
                D.eyeTilt += 0.04f;
                break;
            }
            case 4:   // mixed / other
                D.noseBridge -= 0.08f;
                D.lipFull *= 1.04f;
                break;
            default:   // Latin American / Mediterranean
                D.noseW *= 1.03f;
                D.lipFull *= 1.04f;
                creaseMm -= 0.3f;
                break;
        }
        // face shape archetype: correlated proportions on top of the independent variation (round, square, heart,
        // long, diamond or plain oval), so a crowd reads as different people from a distance
        {
            Rng fsr(hash32(d.seed * 0x61C88647u + 0x1234567u));
            float k = fsr.range(0.6f, 1.f);
            switch ((int)(fsr.f() * 6.f)) {
                case 1:   // round: wide and short, soft jaw, full cheeks
                    D.faceW *= 1.f + 0.06f * k; D.faceH *= 1.f - 0.05f * k; D.chinH *= 1.f - 0.07f * k; D.jawW *= 1.f + 0.03f * k;
                    D.chinSquare *= 0.5f; D.cheekB += 0.15f * k; break;
                case 2:   // square: broad angular jaw, square chin
                    D.jawW *= 1.f + 0.1f * k; D.jawFlare = Lerp(D.jawFlare, 1.f, 0.7f * k); D.chinSquare = Lerp(D.chinSquare, 1.f, 0.7f * k);
                    D.faceH *= 1.f - 0.02f * k; break;
                case 3:   // heart: wide cheekbones and forehead, narrow jaw, pointed chin
                    D.jawW *= 1.f - 0.09f * k; D.cheekB += 0.25f * k; D.chinSquare *= 0.3f; D.chinP *= 1.f + 0.08f * k; D.faceW *= 1.f + 0.02f * k; break;
                case 4:   // long: tall narrow face, long chin
                    D.faceH *= 1.f + 0.07f * k; D.faceW *= 1.f - 0.04f * k; D.chinH *= 1.f + 0.08f * k; D.noseL *= 1.f + 0.05f * k; break;
                case 5:   // diamond: prominent cheekbones, narrow forehead and jaw
                    D.cheekB += 0.3f * k; D.jawW *= 1.f - 0.06f * k; D.cheekH += 0.0015f * k; D.faceW *= 1.f + 0.02f * k; break;
                default: break;   // oval
            }
        }
        // caricature guard: the independent draws and the archetype can stack into a face far longer and narrower (or
        // shorter and wider) than people are; the height-to-width ratio is softly held in a band, the correction split
        // between height and width so the face keeps its size. Sex dimorphism survives the draws the same way: a
        // woman's jaw stays narrower than her cheekbones and her chin shorter than a man's average (soft limits keep
        // some spread above them)
        {
            float asp = D.faceH / D.faceW, t = asp;
            if (asp > 1.07f) t = 1.07f + 0.35f * (asp - 1.07f);
            else if (asp < 0.93f) t = 0.93f - 0.35f * (0.93f - asp);
            float f = sqrtf(t / asp);
            D.faceH *= f;
            D.faceW /= f;
        }
        {
            // (a jaw wider than the cheekbones on anyone reads as a caricature: the square archetype on top of a wide
            // draw is held too)
            float jr = D.jawW / D.faceW, jHi = fem > 0.5f ? 0.97f : 1.1f;
            if (jr > jHi) D.jawW = D.faceW * (jHi + (fem > 0.5f ? 0.25f : 0.3f) * (jr - jHi));
        }
        if (fem > 0.5f && D.chinH > 0.98f) D.chinH = 0.98f + 0.4f * (D.chinH - 0.98f);
        D.creaseDeg = creaseMm / 1.47f;
        D.creaseDepth = creaseMm > 0.f ? (0.00055f + 0.00045f * q.f()) * (1.f + 0.6f * sstep(0.4f, 1.f, a)) : 0.f;
        D.epicanthic = epi;
        D.lipBorder = Saturate(0.6f + 0.3f * h() - 0.35f * sstep(0.5f, 1.f, a));   // the border blurs with age
        D.cornerDepth = 0.0006f + 0.0004f * q.f() + 0.0006f * sstep(0.4f, 1.f, a);
        D.earAngle = (17.f + 4.f * h()) * kDegToRad * (0.8f + 0.2f * D.earOut);
        // lips thin with age
        D.lipFull *= 1.f - 0.18f * sstep(0.45f, 1.f, a);
    }

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
        // derived bones: the forearm roll half way down the forearm, the finger phalanges (MCP / PIP / DIP joints
        // along each finger, bind pose with a slight rest curl in the flexion plane) and the thumb's MCP / IP joints
        J[side == 0 ? B_FOREARM_ROLL_L : B_FOREARM_ROLL_R] = J[fa] + dir * (D.forearm * 0.5f);
        {
            const vec3 pn = D.palmN[side], wy(0, 1, 0);
            for (int f = 0; f < 4; f++) {
                const FingerDef& fd = kFingerDefs[f];
                vec3 fdir = normalize(dir * cosf(fd.splay) + wy * sinf(fd.splay));
                vec3 ax = normalize(cross(fdir, pn));   // + rotates the finger towards the palm (flexion)
                float L = D.fingerLen * fd.len, l1 = L * fd.f1, l2 = L * fd.f2, l3 = L - l1 - l2;
                float c0 = kFingerRestCurl[0], c1 = c0 + kFingerRestCurl[1], c2 = c1 + kFingerRestCurl[2];
                int b0 = phalanxBone(side == 1, f, 0);
                J[b0] = J[hand] + dir * (D.palmLen * fd.along) + wy * (D.handW * fd.lat) - pn * (D.handT * 0.06f);
                J[b0 + 1] = J[b0] + rotate(qaa(ax, c0), fdir) * l1;
                J[b0 + 2] = J[b0 + 1] + rotate(qaa(ax, c1), fdir) * l2;
                D.fingTip[side][f] = J[b0 + 2] + rotate(qaa(ax, c2), fdir) * l3;
                D.fingAx[side][f] = ax;
                D.fingR[side][f] = D.handW * fd.rad;
            }
            // thumb: metacarpal from the CMC joint along thumbDir, then the phalanges lie along the index finger's side
            // (pad towards it)
            vec3 td = normalize(dir * 0.64f + wy * 0.7f + pn * 0.26f);   // nearer the palm's plane than thumbDir
            vec3 d1 = normalize(dir * 0.93f + wy * 0.3f - pn * 0.04f);
            vec3 ax = normalize(cross(d1, thumbPadDir(pn)));
            int t0 = phalanxBone(side == 1, 4, 0);
            J[t0] = J[thumb];
            J[t0 + 1] = J[t0] + td * (kThumbMeta * handLen);
            J[t0 + 2] = J[t0 + 1] + d1 * (kThumbProx * handLen);
            D.fingTip[side][4] = J[t0 + 2] + rotate(qaa(ax, kThumbRestIP), d1) * (kThumbDist * handLen);
            D.fingAx[side][4] = ax;
            D.fingR[side][4] = D.handW * 0.118f;
        }
        J[th] = vec3(sx * hipJHalf, 0.006f * s, zHip + lift);
        J[calf] = vec3(sx * kneeHalf, 0.012f * s, zKnee + lift);
        J[foot] = vec3(sx * ankleHalf, -0.004f * s, zAnkle + lift);
        J[toe] = vec3(sx * ankleHalf, J[foot].y + D.ballFwd, 0.021f * s + lift);
        D.legDir[side] = normalize(J[calf] - J[th]);
    }
    D.thigh = length(J[B_CALF_L] - J[B_THIGH_L]);
    D.shin = length(J[B_FOOT_L] - J[B_CALF_L]);
    J[B_JAW] = J[B_HEAD] + vec3(0, 0.010f, 0.020f) * hs;
    J[B_EYE_L] = J[B_HEAD] + vec3(-0.0315f * D.eyeSpace, 0.0705f + D.eyeDepth, 0.058f) * hs;
    J[B_EYE_R] = J[B_HEAD] + vec3(0.0315f * D.eyeSpace, 0.0705f + D.eyeDepth, 0.058f + D.asymEye) * hs;
    // speech bones (pivots, head space as in face.cpp's landmarks): the upper lip hangs from above/behind it (pitch
    // forward = protrude), the lower lip rides the jaw from below/behind (pitch back = tuck), the corners swing about a
    // point behind the mouth (yaw = narrow/spread, pitch = up/down), the tongue from the floor of the mouth
    J[B_LIP_UPPER] = J[B_HEAD] + faceMap(D, vec3(0.f, 0.093f, 0.022f)) * hs;    // straight above the lip: pitch pushes it forward
    J[B_LIP_LOWER] = J[B_HEAD] + faceMap(D, vec3(0.f, 0.09f, -0.058f)) * hs;    // straight below the lip
    J[B_LIP_CORNER_L] = J[B_HEAD] + faceMap(D, vec3(-0.004f * D.lipW * D.faceW, 0.065f, -0.02f)) * hs;
    J[B_LIP_CORNER_R] = J[B_HEAD] + faceMap(D, vec3(0.004f * D.lipW * D.faceW, 0.065f, -0.02f)) * hs;
    J[B_TONGUE] = J[B_HEAD] + faceMap(D, vec3(0.f, 0.052f, -0.036f)) * hs;
    // brows pivot 6 cm behind the brow line (pitch = raise along the forehead, roll = knit / lift the inner end)
    J[B_BROW_L] = J[B_HEAD] + vec3(-0.031f * D.faceW, 0.022f, 0.06f) * hs;
    J[B_BROW_R] = J[B_HEAD] + vec3(0.031f * D.faceW, 0.022f, 0.06f) * hs;
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
    B_HEAD, B_JAW, B_HEAD, B_HEAD, B_JAW,  // LIP_UPPER, LIP_LOWER, LIP_CORNER_L/R, TONGUE
    B_HEAD, B_HEAD,                        // BROW_L/R
    B_FOREARM_L, B_FOREARM_R,              // FOREARM_ROLL_L/R
    B_HAND_L, B_INDEX1_L, B_INDEX2_L, B_HAND_L, B_MIDDLE1_L, B_MIDDLE2_L,
    B_HAND_L, B_RING1_L, B_RING2_L, B_HAND_L, B_PINKY1_L, B_PINKY2_L, B_HAND_L, B_THUMB1_L, B_THUMB2_L,
    B_HAND_R, B_INDEX1_R, B_INDEX2_R, B_HAND_R, B_MIDDLE1_R, B_MIDDLE2_R,
    B_HAND_R, B_RING1_R, B_RING2_R, B_HAND_R, B_PINKY1_R, B_PINKY2_R, B_HAND_R, B_THUMB1_R, B_THUMB2_R,
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
    out.boneLength[B_LIP_UPPER] = out.boneLength[B_LIP_LOWER] = 0.02f * hs;
    out.boneLength[B_LIP_CORNER_L] = out.boneLength[B_LIP_CORNER_R] = 0.03f * hs;
    out.boneLength[B_TONGUE] = 0.04f * hs;
    out.boneLength[B_BROW_L] = out.boneLength[B_BROW_R] = 0.06f * hs;
    for (int side = 0; side < 2; side++) {
        int roll = side ? B_FOREARM_ROLL_R : B_FOREARM_ROLL_L;
        out.boneLength[roll] = D.forearm * 0.5f;
        out.boneRadius[roll] = (D.rForearm + D.rWrist) * 0.5f;
        for (int f = 0; f < 5; f++)
            for (int j = 0; j < 3; j++) {
                int b = phalanxBone(side == 1, f, j);
                vec3 end = j < 2 ? J[b + 1] : D.fingTip[side][f];
                out.boneLength[b] = length(end - J[b]);
                out.boneRadius[b] = D.fingR[side][f] * (f == 4 && j == 0 ? 1.2f : 1.f - 0.08f * (float)j);
            }
    }

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
    for (int b = B_LIP_UPPER; b <= B_BROW_R; b++) out.boneRadius[b] = 0.008f * hs;
    initDerivedRig(out);
}

}  // namespace Anim
