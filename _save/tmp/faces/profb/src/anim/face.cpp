// Head and face. The head surface is a (theta, phi) grid of rays cast from a point inside the skull against a
// signed distance face model (cranium, brow ridge, cheekbones, jaw, chin, nose, lips, eye sockets with lids).
// Rows and columns are warped per row so that feature contours are grid lines: two rows follow the upper and
// lower eyelid margins (the cells between them are removed to open the eye fissure, revealing the eyeball),
// and two coincident rows form the lip line (a zero-width mouth slit that opens when the jaw rotates).
// Ears, eyeballs (iris/pupil by geometry + vertex colors), eyebrows, lashes, teeth and the mouth cavity are
// separate small meshes.
#include "anim_internal.h"

namespace Anim {
namespace detail {

// Head space (unscaled, face height mapped: see faceMap) to model space.
static inline vec3 headToModel(const BuildCtx& c, vec3 hp) { return c.D->J[B_HEAD] + faceMap(*c.D, hp) * c.D->headS; }

// Face landmark positions in head space (meters, unscaled), adjusted by per-character face params.
struct FaceLm {
    vec3 eye[2];
    float eyeR;
    vec3 noseTip, nasion, subnasale, ala[2];
    vec3 stomion, mouthCorner[2];
    vec3 chin, menton;
    vec3 ear[2];
};

static void faceLandmarks(const BuildCtx& c, FaceLm& L) {
    const BodyDims& D = *c.D;
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        L.eye[sd] = (D.J[B_EYE_L + sd] - D.J[B_HEAD]) / D.headS;
        L.ala[sd] = vec3(sx * 0.0116f * D.noseW + 0.4f * D.asymNose, 0.0908f + 0.003f * (D.noseP - 1.f), 0.0108f - 0.004f * (D.noseL - 1.f));
        L.mouthCorner[sd] = vec3(sx * 0.0252f * D.lipW * D.faceW, 0.0845f, -0.0185f + D.mouthCornerUp + (sd ? D.asymMouth : 0.f));
        L.ear[sd] = vec3(sx * 0.0695f * D.faceW, -0.01f, 0.036f);
    }
    L.eyeR = 0.0119f * D.eyeSize;
    L.nasion = vec3(0.2f * D.asymNose, 0.0872f + 0.0045f * (D.noseBridge - 1.f), 0.0615f);
    L.noseTip = vec3(D.asymNose, 0.1128f + 0.009f * (D.noseP - 1.f), 0.0182f - 0.011f * (D.noseL - 1.f) + 0.003f * D.noseTipUp);
    L.subnasale = vec3(0.5f * D.asymNose, 0.0955f, 0.0048f - 0.006f * (D.noseL - 1.f));
    L.stomion = vec3(0, 0.0955f, -0.0195f);
    L.chin = vec3(D.asymChin, 0.0968f + 0.006f * (D.chinP - 1.f), -0.051f * D.chinH);     // soft tissue pogonion
    L.menton = vec3(0.8f * D.asymChin, 0.074f + 0.004f * (D.chinP - 1.f), -0.0655f * D.chinH);
}

// The face is sculpted from overlapping smooth primitives, back to front: skull, brow ridge, orbits, maxilla and
// cheekbones, mandible (angles, body, chin), cheek soft tissue (malar fat, the pad lateral to the nasolabial fold,
// jowls), the mouth (muzzle, philtrum columns, upper lip with its tubercle, two-lobed lower lip), the nose (dorsum
// with an optional hump, domed tip, columella, alae) and the lids. Blend radii set how soft each junction is: the
// nasolabial fold, the alar crease and the lid crease come from tight blends.
void addHeadPrims(BuildCtx& c) {
    const BodyDims& D = *c.D;
    Sdf& S = c.sdf;
    FaceLm L;
    faceLandmarks(c, L);
    const float hs = D.headS;
    auto P = [&](float x, float y, float z) { return headToModel(c, vec3(x, y, z)); };
    auto Pv = [&](vec3 v) { return headToModel(c, v); };
    auto R = [&](float r) { return r * hs; };
    const u32 HM = MK_HEAD;
    const float fem = D.fem, wc = D.weight - 0.5f, age = Saturate(D.age), mus = Saturate(D.muscle);
    const float lean = Saturate(-wc * 2.2f), full = Saturate(wc * 2.f), youth = 1.f - age;
    const float fw = D.faceW, jw = D.jawW;
    // ---- skull: cranium and forehead
    S.ellipsoid(P(0, -0.013f, 0.074f), vec3(0.0752f * fw, 0.098f * D.headLen, 0.1f) * hs, HM, R(0.01f));
    float fs = D.foreheadSlope;
    S.ellipsoid(P(0, 0.034f - 0.004f * fs, 0.088f), vec3(0.061f, 0.055f, 0.062f) * hs, HM, R(0.03f));
    // ---- brow ridge (glabella + supraorbital arches wrapping round to the temples)
    float br = (0.0085f + 0.004f * D.browRidge);
    S.ellipsoid(P(0, 0.0775f + 0.002f * D.browRidge, 0.0765f), vec3(0.015f, br * 1.0f, br * 1.05f) * hs, HM, R(0.018f));
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f, bz = sd ? D.asymBrow : 0.f;
        S.cone(P(sx * 0.011f, 0.0775f + 0.002f * D.browRidge, 0.0775f + bz), P(sx * 0.046f, 0.064f, 0.0785f + bz), R(br), R(br * 0.78f), HM, R(0.018f));
    }
    // ---- mid face (maxilla + cheeks) and cheekbones
    S.ellipsoid(P(0, 0.028f, 0.012f), vec3(0.0615f * fw, 0.062f, 0.062f) * hs, HM, R(0.02f));
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        // malar eminence below and lateral to the orbit
        S.ellipsoid(P(sx * 0.0445f * fw, 0.0565f, 0.034f + D.cheekH), vec3(0.0185f, 0.0165f, 0.0125f) * (hs * (0.85f + 0.2f * D.cheekB)), HM, R(0.022f));
        // zygomatic arch towards the ear
        S.cone(P(sx * 0.052f * fw, 0.045f, 0.036f), P(sx * 0.064f * fw, 0.0f, 0.03f), R(0.008f), R(0.006f), HM, R(0.014f));
    }
    // ---- mandible: base mass, angles, body along the jawline, chin
    S.ellipsoid(P(0, 0.029f, -0.027f * D.chinH), vec3(0.0525f * jw, 0.058f, 0.041f * D.chinH) * hs, HM, R(0.024f));
    const float gx = 0.0495f * jw * (0.96f + 0.08f * D.jawFlare);
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        vec3 gon(sx * gx, 0.004f, -0.031f + 0.004f * (1.f - D.jawFlare));
        S.ellipsoid(Pv(gon + vec3(0, 0, 0.001f)), vec3(0.012f, 0.019f, 0.016f) * (hs * Lerp(1.f, 0.62f, fem) * (0.9f + 0.2f * D.jawFlare)), HM, R(0.02f));
        // jawline: from the angle along the body of the mandible (U-shaped dental arch) to the corner of the chin
        vec3 cc(L.chin.x + sx * 0.0125f * (1.f + 0.45f * D.chinSquare), L.chin.y - 0.0145f, L.chin.z - 0.001f);
        vec3 mb(sx * 0.037f * jw, 0.054f, -0.041f * D.chinH);
        float jr = 0.0098f * Lerp(1.f, 0.8f, fem);
        S.cone(Pv(gon + vec3(-sx * 0.004f, 0.012f, -0.002f)), Pv(mb), R(jr), R(jr * 0.98f), HM, R(0.012f));
        S.cone(Pv(mb), Pv(cc), R(jr * 0.98f), R(0.0094f * (0.9f + 0.2f * D.chinSquare)), HM, R(0.012f));
        // masseter
        S.ellipsoid(P(sx * 0.046f * jw, 0.018f, -0.008f), vec3(0.0105f, 0.02f, 0.025f) * (hs * (0.8f + 0.35f * mus) * Lerp(1.f, 0.78f, fem)), HM, R(0.02f));
    }
    // chin (mental protuberance): rounder / narrower for women, broad and square for some men
    {
        float cw = 0.0162f * (1.f + 0.4f * D.chinSquare) * (1.f + 0.25f * (jw - 1.f)) * Lerp(1.f, 0.86f, fem);
        S.ellipsoid(Pv(L.chin + vec3(0, -0.0125f, -0.0005f)), vec3(cw, 0.0125f, 0.0122f) * hs, HM, R(0.012f));
        if (D.chinCleft > 0.f) {
            Prim& q = S.prims[S.cone(Pv(L.chin + vec3(0, 0.002f, 0.006f)), Pv(L.chin + vec3(0, 0.0015f, -0.008f)), R(0.0021f * D.chinCleft), R(0.0019f * D.chinCleft), HM, R(0.003f))];
            q.op = OP_SUB;
        }
    }
    // under the chin into the neck (+ submental fullness on heavier faces)
    S.ellipsoid(P(0, 0.028f, -0.057f * D.chinH), vec3(0.036f, 0.045f, 0.022f) * hs, HM | MK_NECK, R(0.022f));
    if (full > 0.05f) S.ellipsoid(P(0, 0.046f, -0.062f * D.chinH), vec3(0.028f, 0.026f, 0.013f) * (hs * (0.4f + 0.6f * full)), HM | MK_NECK, R(0.02f));
    // a very heavy face carries a double chin: a soft roll under the jaw line, wider than the chin
    if (D.fat > 0.25f)
        S.ellipsoid(P(0, 0.05f, -0.071f * D.chinH), vec3(0.036f + 0.008f * D.fat, 0.028f, 0.014f + 0.007f * D.fat) * (hs * (0.5f + 0.5f * D.fat)), HM | MK_NECK,
                    R(0.018f));
    // ---- cheek soft tissue
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        // malar fat ("apple" of the cheek): fuller on young, female and heavier faces
        float mf = Saturate(0.35f + 0.35f * youth + 0.35f * full + 0.2f * fem - 0.25f * lean * (1.f - 0.5f * youth));
        S.ellipsoid(P(sx * 0.0345f, 0.0625f, 0.019f + 0.7f * D.cheekH), vec3(0.019f, 0.0125f, 0.017f) * (hs * (0.55f + 0.5f * mf)), HM, R(0.022f));
        // pad lateral to the nasolabial fold (ala -> past the mouth corner); its tight medial blend is the fold
        float nl = 0.45f + 0.5f * age + 0.25f * full;
        vec3 a(sx * 0.0235f, 0.0842f, 0.0085f), b(sx * 0.0335f, 0.0732f, -0.0235f);
        S.cone(Pv(a), Pv(b), R(0.0062f * nl), R(0.0072f * nl), HM, R(0.009f));
        // lower cheek over the buccal fat and the teeth (keeps young / fuller faces from looking gaunt below the
        // cheekbones)
        // (thin young faces keep much of it: the hollow below the cheekbones comes with age more than with leanness)
        float bf = Saturate(0.35f + 0.45f * youth + 0.6f * full + 0.2f * fem - 0.45f * lean * (1.f - 0.5f * youth));
        S.ellipsoid(P(sx * 0.0425f * jw, 0.05f, -0.013f), vec3(0.013f, 0.02f, 0.022f) * (hs * (0.5f + 0.5f * bf)), HM, R(0.02f));
        // jowls (age / weight)
        float jl = Saturate(0.9f * age + 0.6f * full - 0.35f + 0.55f * D.fat);
        if (jl > 0.02f) S.ellipsoid(P(sx * 0.0385f * jw, 0.061f, -0.037f), vec3(0.012f, 0.012f, 0.012f) * (hs * (0.6f + 0.5f * jl)), HM, R(0.016f));
    }
    // ---- mouth: muzzle over the dental arch and the skin of the upper lip
    S.ellipsoid(P(0, 0.066f, -0.012f), vec3(0.034f * D.lipW, 0.0305f, 0.029f) * hs, HM, R(0.02f));
    S.ellipsoid(Pv(L.subnasale + vec3(0, -0.0098f, -0.0112f)), vec3(0.0175f * D.lipW, 0.0105f, 0.0115f) * hs, HM, R(0.008f));
    // lips: upper (tubercle + cupid's bow) and lower (two lobes), curved chains towards the corners
    float lf = D.lipFull;
    float ulR = 0.0051f * lf * D.lipRatio, llR = 0.0066f * lf;
    vec3 ulC(0, L.stomion.y + 0.0004f, L.stomion.z + 0.006f), llC(0, L.stomion.y - 0.0024f, L.stomion.z - 0.0072f);
    S.ellipsoid(Pv(ulC + vec3(0, 0.0004f, -0.0018f)), vec3(0.0062f * D.lipW, 0.0041f * lf, 0.0038f * lf) * hs, HM, R(0.003f));
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        vec3 mc = L.mouthCorner[sd];
        // philtrum column: nose base down to the peak of the cupid's bow
        vec3 pk(sx * 0.0054f * D.lipW, ulC.y + 0.0004f, ulC.z + ulR * 0.92f + 0.0007f * D.lipBow);
        S.cone(Pv(L.subnasale + vec3(sx * 0.0033f, -0.0016f, -0.0016f)), Pv(pk), R(0.0017f), R(0.0021f), HM, R(0.0034f));
        vec3 ulM(sx * 0.0115f * D.lipW, ulC.y - 0.0014f, ulC.z + 0.0003f), llM(sx * 0.0115f * D.lipW, llC.y - 0.0012f, llC.z - 0.0002f);
        S.cone(Pv(ulC), Pv(ulM), R(ulR), R(ulR * 0.95f), HM, R(0.003f));
        S.cone(Pv(ulM), Pv(mc + vec3(-sx * 0.002f, 0.0f, 0.0015f)), R(ulR * 0.95f), R(0.0022f), HM, R(0.003f));
        S.ellipsoid(Pv(vec3(sx * 0.0062f * D.lipW, llC.y + 0.0002f, llC.z)), vec3(0.0092f * D.lipW, llR * 0.96f, llR * 0.9f) * hs, HM, R(0.003f));
        S.cone(Pv(llM), Pv(mc + vec3(-sx * 0.002f, 0.0f, -0.0015f)), R(llR * 0.85f), R(0.0022f), HM, R(0.003f));
    }
    // ---- nose: dorsum (bony bridge + cartilage, optional hump) ending short of the domed tip lobule, columella, alae
    {
        float nw = D.noseW;
        vec3 N = L.nasion, T = L.noseTip, Sn = L.subnasale;
        vec3 d2 = T + vec3(0, -0.0105f, 0.0098f);   // supratip: the dorsum blends into the lobule here
        // deepen the nasion (the brow ridge and mid-face blends fill it in)
        {
            Prim& q = S.prims[S.ellipsoid(Pv(N + vec3(0, 0.0096f, -0.001f)), vec3(0.0095f, 0.0065f, 0.0085f) * hs, HM, R(0.013f))];
            q.op = OP_SUB;
        }
        // nasal pyramid: the sidewalls from the face up to the ridge (the ridge cone alone would float in front of
        // the face and leave a pocket behind the tip)
        S.cone(Pv(N + vec3(0, -0.0085f, -0.004f)), Pv(T + vec3(0, -0.0175f, 0.0015f)), R(0.006f), R(0.0095f * nw), HM, R(0.008f), 1.2f, 1.f, vec3(1, 0, 0));
        S.cone(Pv(N + vec3(0, -0.0042f, -0.0015f)), Pv(d2), R(0.0048f * (0.85f + 0.15f * nw)), R(0.0056f * nw), HM, R(0.009f), 0.9f, 1.f, vec3(1, 0, 0));
        if (D.noseHump > 0.05f)
            S.ellipsoid(Pv(lerp(N, d2, 0.45f) + vec3(0, 0.0008f, 0)), vec3(0.0042f, 0.0032f, 0.0068f) * (hs * D.noseHump), HM, R(0.005f));
        const float bulb = D.noseBulb;
        S.ellipsoid(Pv(T + vec3(0, -0.0064f * bulb, -0.0012f)), vec3(0.0084f * nw, 0.0064f, 0.0074f) * (hs * bulb), HM, R(0.007f));
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            S.ellipsoid(Pv(T + vec3(sx * 0.0033f * bulb, -0.0053f * bulb, 0.0002f)), vec3(0.0046f, 0.0046f, 0.0050f) * (hs * bulb), HM, R(0.0045f));
        }
        if (D.noseScoop > 0.f) {
            // concave (scooped) dorsum: carve the middle of the ridge
            Prim& q = S.prims[S.ellipsoid(Pv(lerp(N, d2, 0.55f) + vec3(0, 0.0052f, 0.0004f)), vec3(0.0065f, 0.0034f, 0.0105f) * (hs * D.noseScoop), HM,
                                          R(0.006f))];
            q.op = OP_SUB;
        }
        S.cone(Pv(vec3(T.x, T.y - 0.0078f, Sn.z + 0.0068f)), Pv(Sn + vec3(0, 0.0012f, 0.0032f)), R(0.0028f), R(0.0031f), HM, R(0.004f), 0.8f, 1.f, vec3(1, 0, 0));
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            vec3 A = L.ala[sd];
            // lateral crus: ala to tip
            S.cone(Pv(A + vec3(-sx * 0.0026f, 0.0045f, 0.0018f)), Pv(T + vec3(sx * 0.0045f, -0.0064f, -0.0006f)), R(0.0046f), R(0.0042f), HM, R(0.006f), 0.8f, 1.f, vec3(1, 0, 0));
            S.ellipsoid(Pv(A + vec3(0, 0.0005f, 0.0005f)), vec3(0.0052f, 0.0078f, 0.006f) * hs, HM, R(0.0045f));
        }
    }
    // ---- eyes: sockets (carved), then lids wrapped around the eyeballs, bags under older eyes
    for (int sd = 0; sd < 2; sd++) {
        vec3 e = L.eye[sd];
        Prim& q = S.prims[S.ellipsoid(Pv(e + vec3(0, 0.0055f, 0.0018f)), vec3(0.0195f, 0.0145f, 0.0135f) * hs, HM, R(0.01f))];
        q.op = OP_SUB;
    }
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        vec3 e = L.eye[sd];
        float er = L.eyeR;
        S.ellipsoid(Pv(e), vec3(er + 0.0009f) * hs, HM, R(0.003f));
        // upper lid (fuller for hooded / monolid eyes), thick margin
        float up = 0.0011f + 0.0016f * D.hood;
        // (narrower than the eyeball across, so the lids end at the eye corners instead of meeting in a crease beyond them)
        S.ellipsoid(Pv(e + vec3(0, -0.0006f, 0.0032f)), vec3(er + 0.0002f, er + up + 0.0003f, er * 0.8f) * hs, HM, R(0.0045f));
        S.ellipsoid(Pv(e + vec3(0, -0.001f, -0.0035f)), vec3(er - 0.0004f, er + 0.0011f, er * 0.62f) * hs, HM, R(0.0055f));
        float bag = Saturate(1.2f * age - 0.35f + 0.3f * full);
        if (bag > 0.02f) S.ellipsoid(Pv(e + vec3(sx * 0.002f, 0.0052f, -0.0128f)), vec3(0.0105f, 0.0042f, 0.0042f) * (hs * (0.6f + 0.4f * bag)), HM, R(0.005f));
        // epicanthic fold: skin from the upper lid draped over the inner corner
        if (D.epicanthic > 0.f)
            S.ellipsoid(Pv(e + vec3(-sx * 0.0112f, 0.0068f, 0.0012f)), vec3(0.0038f, 0.0026f, 0.0048f) * (hs * (0.6f + 0.4f * D.epicanthic)), HM,
                        R(0.003f), vec3(0.8f * sx, 0.6f, 0.f), vec3(0, 0, 1));
    }
    // ear roots (the ears themselves are separate meshes)
    for (int sd = 0; sd < 2; sd++) S.ellipsoid(Pv(L.ear[sd] + vec3(0, 0.002f, 0)), vec3(0.008f, 0.016f, 0.021f) * hs, HM, R(0.008f));
}

// ------------------------------------------------------------------------------------------------
// Head grid

struct HeadLayout {
    float thI, thO, phI, phO;       // eye fissure corners (theta/phi, right side; mirrored)
    float Hh, Hl;                   // lid heights at the center
    float thMC, phS, phMC;          // mouth corner theta, stomion phi, corner phi
    float thAla;
};

static void angOf(vec3 d, float& th, float& ph) {
    th = atan2f(d.x, d.y);
    ph = atan2f(d.z, sqrtf(d.x * d.x + d.y * d.y));
}

// Fissure centerline and lid offsets for |theta|
static void eyeRows(const HeadLayout& L, float at, float& phC, float& hHi, float& hLo, float& inSpan) {
    float u = (at - L.thI) / (L.thO - L.thI);
    if (u >= 0.f && u <= 1.f) {
        phC = Lerp(L.phI, L.phO, u);
        float sHi = powf(Max(0.f, sinf(kPi * powf(u, 0.9f))), 0.72f);
        float sLo = powf(Max(0.f, sinf(kPi * powf(u, 1.12f))), 0.85f);
        hHi = L.Hh * sHi;
        hLo = L.Hl * sLo;
        inSpan = 1.f;
    } else {
        float d = u < 0.f ? (L.thI - at) : (at - L.thO);
        phC = u < 0.f ? L.phI : L.phO;
        float g = Min(d * 0.2f, 1.0f * kDegToRad);
        hHi = g;
        hLo = g;
        inSpan = 0.f;
    }
}

static float lipLine(const HeadLayout& L, float at, float& gap, float& pinch) {
    float u = at / L.thMC;
    if (u <= 1.f) {
        gap = 0.f;
        pinch = 1.f - 0.72f * u * u;
        return L.phS - (L.phS - L.phMC) * u * u;
    }
    float d = at - L.thMC;
    gap = Min(d * 0.22f, 1.0f * kDegToRad);
    pinch = 0.28f + 0.72f * sstep(0.f, 11.f * kDegToRad, d);
    return L.phMC - d * 0.08f;
}

static void mapLandmarks(const BodyDims& D, FaceLm& L) {
    for (int sd = 0; sd < 2; sd++) {
        L.ala[sd] = faceMap(D, L.ala[sd]);
        L.mouthCorner[sd] = faceMap(D, L.mouthCorner[sd]);
    }
    L.nasion = faceMap(D, L.nasion);
    L.noseTip = faceMap(D, L.noseTip);
    L.subnasale = faceMap(D, L.subnasale);
    L.stomion = faceMap(D, L.stomion);
    L.chin = faceMap(D, L.chin);
    L.menton = faceMap(D, L.menton);
}

void buildHeadGrid(BuildCtx& c) {
    const BodyDims& D = *c.D;
    MeshB& m = c.m;
    HeadInfo& H = c.head;
    FaceLm Lm;
    faceLandmarks(c, Lm);
    FaceLm Lf = Lm;   // landmarks where they end up (face height mapped), for comparisons with mesh positions
    mapLandmarks(D, Lf);
    const float hs = D.headS;
    H.origin = D.J[B_HEAD];
    H.C = headToModel(c, vec3(0, 0.006f, 0.048f));
    const vec3 C = H.C;
    const u32 HM = MK_HEAD;
    // ---- feature angles as seen from C
    HeadLayout L;
    {
        vec3 e = headToModel(c, Lm.eye[1]);
        float er = Lm.eyeR * hs;
        // fissure ~28 mm wide and ~9-10 mm high at the pupil (the upper lid covers the top 1-2 mm of the iris, the lower
        // lid meets its bottom); an epicanthic fold hides the inner corner, hooding lowers the upper margin
        vec3 inner = e + vec3(-0.0138f * D.eyeSize + 0.0019f * D.epicanthic, 0.0076f, -0.0012f + 0.0006f * D.epicanthic) * hs;
        vec3 outer = e + vec3(0.0143f * D.eyeSize, 0.0016f, 0.0010f + 0.03f * D.eyeTilt) * hs;
        float th, ph;
        angOf(inner - C, L.thI, L.phI);
        angOf(outer - C, L.thO, L.phO);
        float hTop = (0.41f - 0.08f * D.hood) * D.apertureH, hBot = 0.48f * D.apertureH;
        vec3 top = e + vec3(0.001f, er * sqrtf(Max(0.f, 1.f - hTop * hTop)), er * hTop);
        vec3 bot = e + vec3(0.0015f, er * sqrtf(Max(0.f, 1.f - hBot * hBot)), -er * hBot);
        angOf(top - C, th, ph);
        float phCmid = 0.5f * (L.phI + L.phO);
        L.Hh = ph - phCmid;
        angOf(bot - C, th, ph);
        L.Hl = phCmid - ph;
        float phs;
        angOf(headToModel(c, Lm.mouthCorner[1]) - C, L.thMC, L.phMC);
        angOf(headToModel(c, Lm.stomion) - C, th, phs);
        L.phS = phs;
        angOf(headToModel(c, Lm.ala[1]) - C, L.thAla, ph);
        H.thetaEye = 0.5f * (L.thI + L.thO);
        H.thetaMouth = L.thMC;
        H.phiMouth = L.phS;
    }
    const float deg = kDegToRad;
    // ---- column layouts (right half, KH columns strictly between 0 and pi). Feature families put columns on the
    // eye corners, the mouth corner and the nose wing; rows blend between families.
    const int KH = 28;
    const int NC = 2 * KH + 2;
    H.cols = NC;
    std::vector<float> colUniform(KH), colEye(KH), colMouth(KH), colNose(KH), colFace(KH);
    for (int k = 0; k < KH; k++) colUniform[k] = kPi * (k + 1) / (KH + 1);
    for (int k = 0; k < KH; k++) colFace[k] = kPi * powf((k + 1.f) / (KH + 1), 1.3f);
    auto featCols = [&](std::vector<float>& out, const float* stops, const int* counts, int nseg, float pw) {
        // piecewise uniform up to the last stop, then a power ramp towards the back of the head
        int n = 0;
        float a = 0.f;
        for (int sgi = 0; sgi < nseg; sgi++) {
            for (int k = 1; k <= counts[sgi]; k++) out[n++] = a + (stops[sgi] - a) * k / counts[sgi];
            a = stops[sgi];
        }
        int rest = KH - n;
        for (int k = 1; k <= rest; k++) out[n++] = a + (kPi - a) * powf((float)k / (rest + 1), pw);
    };
    {
        const float st[2] = {L.thI, L.thO};
        const int cn[2] = {4, 11};
        featCols(colEye, st, cn, 2, 1.3f);
    }
    {
        const float st[1] = {L.thMC};
        const int cn[1] = {9};
        featCols(colMouth, st, cn, 1, 1.38f);
    }
    {
        const float st[1] = {L.thAla};
        const int cn[1] = {6};
        featCols(colNose, st, cn, 1, 1.4f);
    }
    auto fullCols = [&](const std::vector<float>& a, const std::vector<float>& b, float t, std::vector<float>& out) {
        out.resize(NC);
        out[0] = 0.f;
        for (int k = 0; k < KH; k++) {
            float v = Lerp(a[k], b[k], t);
            out[1 + k] = v;
            out[NC - 1 - k] = kTwoPi - v;
        }
        out[KH + 1] = kPi;
    };
    // ---- rows (bottom to top). phi: front elevation (degrees) away from the features; feature rows follow the lip
    // line / lid margins at an offset (degrees, scaled by the lip pinch / lid span) and relax to evenly spaced values
    // between the bracketing plain rows away from the mouth and eyes. Each row also carries its share of the speech
    // (lip) bones, the eye bones (upper lid) and the brow bones.
    enum { RK_PLAIN, RK_LIP, RK_MOUTHLO, RK_MOUTHHI, RK_LIDLO, RK_EYELO, RK_EYEHI, RK_LIDUP };
    struct RowDef {
        float phi;
        int kind;
        int colA, colB;   // column family: 0 uniform 1 face 2 mouth 3 nose 4 eye
        float colT;
        float off;        // feature offset (degrees)
        float lipW, lidW, browW;
    };
    RowDef rows[] = {
        {-68.0f, RK_PLAIN, 0, 1, 0.3f, 0, 0, 0, 0},      {-65.0f, RK_PLAIN, 0, 1, 0.6f, 0, 0, 0, 0},
        {-62.0f, RK_PLAIN, 1, 2, 0.15f, 0, 0, 0, 0},     {-59.2f, RK_PLAIN, 1, 2, 0.35f, 0, 0, 0, 0},
        {-56.4f, RK_PLAIN, 1, 2, 0.55f, 0, 0, 0, 0},     {-53.8f, RK_PLAIN, 1, 2, 0.75f, 0, 0, 0, 0},
        {-51.3f, RK_PLAIN, 1, 2, 0.9f, 0, 0, 0, 0},      {-48.9f, RK_PLAIN, 2, 2, 0.f, 0, 0.1f, 0, 0},
        {-46.6f, RK_PLAIN, 2, 2, 0.f, 0, 0.3f, 0, 0},
        // lower lip: skin below the vermilion, the vermilion border (two close rows: skin / lip side), lip body, wet
        // edge
        {0.f, RK_LIP, 2, 2, 0.f, -7.2f, 0.55f, 0, 0},    {0.f, RK_LIP, 2, 2, 0.f, -5.9f, 0.72f, 0, 0},
        {0.f, RK_LIP, 2, 2, 0.f, -5.1f, 0.82f, 0, 0},    {0.f, RK_LIP, 2, 2, 0.f, -3.3f, 0.95f, 0, 0},
        {0.f, RK_LIP, 2, 2, 0.f, -1.6f, 1.f, 0, 0},
        {0.f, RK_MOUTHLO, 2, 2, 0.f, 0.f, 1.f, 0, 0},
        {0.f, RK_MOUTHHI, 2, 2, 0.f, 0.f, 1.f, 0, 0},
        // upper lip: wet edge, lip body, the vermilion border (lip side / skin side: the white roll), then the
        // philtrum up to the nose
        {0.f, RK_LIP, 2, 2, 0.f, 1.2f, 1.f, 0, 0},       {0.f, RK_LIP, 2, 2, 0.f, 2.5f, 0.95f, 0, 0},
        {0.f, RK_LIP, 2, 2, 0.f, 3.6f, 0.88f, 0, 0},     {0.f, RK_LIP, 2, 2, 0.f, 4.4f, 0.8f, 0, 0},
        {-31.2f, RK_PLAIN, 2, 3, 0.2f, 0, 0.55f, 0, 0},  {-29.3f, RK_PLAIN, 2, 3, 0.4f, 0, 0.25f, 0, 0},
        {-27.5f, RK_PLAIN, 2, 3, 0.6f, 0, 0.08f, 0, 0},  {-25.6f, RK_PLAIN, 2, 3, 0.8f, 0, 0, 0, 0},
        // nose and cheeks
        {-23.6f, RK_PLAIN, 3, 3, 0.f, 0, 0, 0, 0},       {-21.5f, RK_PLAIN, 3, 3, 0.f, 0, 0, 0, 0},
        {-19.3f, RK_PLAIN, 3, 3, 0.f, 0, 0, 0, 0},       {-17.1f, RK_PLAIN, 3, 3, 0.f, 0, 0, 0, 0},
        {-14.8f, RK_PLAIN, 3, 4, 0.15f, 0, 0, 0, 0},     {-12.4f, RK_PLAIN, 3, 4, 0.3f, 0, 0, 0, 0},
        {-9.9f, RK_PLAIN, 3, 4, 0.45f, 0, 0, 0, 0},      {-7.4f, RK_PLAIN, 3, 4, 0.6f, 0, 0, 0, 0},
        {-4.9f, RK_PLAIN, 3, 4, 0.75f, 0, 0, 0, 0},      {-2.5f, RK_PLAIN, 3, 4, 0.9f, 0, 0, 0, 0},
        // lower lid: lid-cheek junction, lid bulge, margin outer edge, margin
        {0.f, RK_LIDLO, 4, 4, 0.f, 3.6f, 0, 0, 0},       {0.f, RK_LIDLO, 4, 4, 0.f, 2.2f, 0, 0, 0},
        {0.f, RK_LIDLO, 4, 4, 0.f, 1.0f, 0, 0, 0},       {0.f, RK_EYELO, 4, 4, 0.f, 0.f, 0, 0, 0},
        // upper lid: margin, lash line, tarsal plate, crease, fold
        {0.f, RK_EYEHI, 4, 4, 0.f, 0.f, 0, 1.f, 0},      {0.f, RK_LIDUP, 4, 4, 0.f, 0.9f, 0, 1.f, 0},
        {0.f, RK_LIDUP, 4, 4, 0.f, 2.0f, 0, 0.83f, 0},   {0.f, RK_LIDUP, 4, 4, 0.f, 3.1f, 0, 0.52f, 0},
        {0.f, RK_LIDUP, 4, 4, 0.f, 4.3f, 0, 0.15f, 0.3f},
        // brows, forehead, scalp
        {16.2f, RK_PLAIN, 4, 4, 0.f, 0, 0, 0, 0.75f},    {18.4f, RK_PLAIN, 4, 1, 0.2f, 0, 0, 0, 0.95f},
        {20.8f, RK_PLAIN, 4, 1, 0.4f, 0, 0, 0, 1.f},     {23.5f, RK_PLAIN, 4, 1, 0.6f, 0, 0, 0, 0.9f},
        {26.7f, RK_PLAIN, 4, 1, 0.8f, 0, 0, 0, 0.7f},    {30.5f, RK_PLAIN, 1, 1, 0.f, 0, 0, 0, 0.45f},
        {35.2f, RK_PLAIN, 1, 0, 0.2f, 0, 0, 0, 0.22f},   {40.6f, RK_PLAIN, 1, 0, 0.4f, 0, 0, 0, 0.08f},
        {46.5f, RK_PLAIN, 1, 0, 0.6f, 0, 0, 0, 0},       {53.0f, RK_PLAIN, 1, 0, 0.8f, 0, 0, 0, 0},
        {60.0f, RK_PLAIN, 0, 0, 0.f, 0, 0, 0, 0},        {67.5f, RK_PLAIN, 0, 0, 0.f, 0, 0, 0, 0},
        {75.0f, RK_PLAIN, 0, 0, 0.f, 0, 0, 0, 0},        {82.5f, RK_PLAIN, 0, 0, 0.f, 0, 0, 0, 0},
    };
    const int NRD = (int)(sizeof(rows) / sizeof(rows[0]));
    const int NR = NRD + 1;   // + row 0 (neck ring)
    H.rows = NR;
    // per-character upper lid rows: lash line, tarsal plate, the crease and the skin fold above it (a monolid keeps
    // evenly spaced rows and no crease)
    const bool hasCrease = D.creaseDeg > 0.f && D.creaseDepth > 0.f;
    const float creaseOff = hasCrease ? Clamp(D.creaseDeg, 2.3f, 4.6f) : 3.1f;
    const float foldOff = creaseOff + 1.15f;
    {
        const float upOff[4] = {0.85f, 0.85f + (creaseOff - 0.85f) * 0.55f, creaseOff, foldOff};
        int n = 0;
        for (int r = 0; r < NRD && n < 4; r++)
            if (rows[r].kind == RK_LIDUP) rows[r].off = upOff[n++];
    }
    // named rows (grid row j = index into rows[] + 1)
    H.rowLipLo = H.rowLipHi = H.rowLidLo = H.rowLidHi = -1;
    H.rowNoseBase = H.rowBrow = H.rowHairline = H.rowChin = -1;
    for (int r = 0; r < NRD; r++) {
        int j = r + 1;
        const RowDef& rd = rows[r];
        if (rd.kind == RK_MOUTHLO) H.rowMouthLo = j;
        if (rd.kind == RK_MOUTHHI) H.rowMouthHi = j;
        if (rd.kind == RK_EYELO) H.rowEyeLo = j;
        if (rd.kind == RK_EYEHI) H.rowEyeHi = j;
        if (rd.kind == RK_LIP && H.rowLipLo < 0) H.rowLipLo = j;
        if (rd.kind == RK_LIP) H.rowLipHi = j;
        if (rd.kind == RK_LIDLO && H.rowLidLo < 0) H.rowLidLo = j;
        if (rd.kind == RK_LIDUP) H.rowLidHi = j;
        if (rd.kind == RK_PLAIN && H.rowChin < 0 && rd.phi >= -60.f) H.rowChin = j;
        if (rd.kind == RK_PLAIN && H.rowNoseBase < 0 && rd.phi >= -26.f) H.rowNoseBase = j;
        if (rd.kind == RK_PLAIN && H.rowBrow < 0 && rd.phi >= 18.f) H.rowBrow = j;
        if (rd.kind == RK_PLAIN && H.rowHairline < 0 && rd.phi >= 44.f) H.rowHairline = j;
    }
    // bracketing plain rows of every feature row (index into rows[])
    std::vector<int> plainBelow(NRD, 0), plainAbove(NRD, 0);
    for (int r = 0; r < NRD; r++) {
        int a = r, b = r;
        while (a > 0 && rows[a].kind != RK_PLAIN) a--;
        while (b < NRD - 1 && rows[b].kind != RK_PLAIN) b++;
        plainBelow[r] = a;
        plainAbove[r] = b;
    }
    const std::vector<float>* colLists[5] = {&colUniform, &colFace, &colMouth, &colNose, &colEye};
    H.grid.assign((size_t)NR * NC, 0);
    // ---- row 0: ring on the neck just below the jaw/skull
    vec3 n0c = headToModel(c, vec3(0, -0.017f, -0.052f));
    const float tilt = 32.f * deg;
    vec3 nAx(0, sinf(tilt), cosf(tilt));
    vec3 nF(0, cosf(tilt), -sinf(tilt));
    std::vector<float> row0Phi(NC), row0Th(NC);
    std::vector<float> col0;
    fullCols(colUniform, colUniform, 0.f, col0);
    for (int k = 0; k < NC; k++) {
        float th = col0[k];
        vec3 dir = nF * cosf(th) + vec3(1, 0, 0) * sinf(th);
        float t = c.sdf.castOut(n0c, dir, MK_NECK | MK_HEAD, 0.2f * hs);
        vec3 p = n0c + dir * t;
        float pth, pph;
        angOf(p - C, pth, pph);
        row0Phi[k] = pph;
        if (pth < 0.f) pth += kTwoPi;
        if (k == 0 && pth > kPi) pth -= kTwoPi;
        row0Th[k] = pth;
        WAcc acc;
        acc.add(B_HEAD, 0.8f);
        acc.add(B_NECK, 0.2f);
        BVert v;
        v.p = p;
        v.col = c.skin;
        v.mat = MAT_SKIN;
        v.part = PART_HEAD;
        v.side = p.x < 0.f ? 0 : 1;
        v.sw = acc.finish();
        v.pa = th;
        v.pb = pph;
        v.uv = vec2(uWrap(th, kPi, 0.07f * hs), p.z);
        v.uPer = kTwoPi * 0.07f * hs;
        v.pc = 1.2f;
        v.t = normalize(cross(nAx, dir));
        v.axisPt = n0c;
        H.grid[k] = m.add(v);
    }
    c.neckTopFirst = H.grid[0];
    // ---- rows 1..NRD
    const float backTop = 84.f * deg;
    std::vector<float> cols;
    for (int r = 0; r < NRD; r++) {
        const RowDef& rd = rows[r];
        int j = r + 1;
        fullCols(*colLists[rd.colA], *colLists[rd.colB], rd.colT, cols);
        float rowFrac = (float)j / NRD;   // for back layout
        for (int k = 0; k < NC; k++) {
            float th = cols[k];
            if (j <= 3) {
                // align the first rows with the actual azimuths of the neck ring (avoids twisted quads)
                float a0 = row0Th[k];
                while (a0 - th > kPi) a0 -= kTwoPi;
                while (th - a0 > kPi) a0 += kTwoPi;
                th = Lerp(a0, th, (float)j / 4.f);
                if (th < 0.f) th += kTwoPi;
                if (th >= kTwoPi) th -= kTwoPi;
            }
            float ath = th > kPi ? kTwoPi - th : th;
            // front (face) phi: feature rows follow the eye/lip contours near the features and relax to evenly
            // spaced "plain" values (between the bracketing plain rows) away from them.
            float phF = rd.phi * deg;
            float phC, hHi, hLo, inSpan, gap, pinch;
            eyeRows(L, ath, phC, hHi, hLo, inSpan);
            float phM = lipLine(L, ath, gap, pinch);
            float dEye = ath < L.thI ? L.thI - ath : (ath > L.thO ? ath - L.thO : 0.f);
            float wEye = 1.f - sstep(0.f, 12.f * deg, dEye);
            float wLip = 1.f - sstep(0.f, 13.f * deg, ath - L.thMC);
            if (rd.kind != RK_PLAIN) {
                bool lip = rd.kind <= RK_MOUTHHI;
                int first = plainBelow[r], last = plainAbove[r];
                float plain = Lerp(rows[first].phi, rows[last].phi, (float)(r - first) / (last - first)) * deg;
                float feat = phF;
                switch (rd.kind) {
                    case RK_EYELO: feat = phC - hLo; break;
                    case RK_EYEHI: feat = phC + hHi; break;
                    case RK_LIDLO: feat = phC - hLo - rd.off * deg * (0.79f + 0.21f * inSpan); break;
                    case RK_LIDUP: feat = phC + hHi + rd.off * deg * (0.7f + 0.3f * inSpan); break;
                    case RK_MOUTHLO: feat = phM - gap; break;
                    case RK_MOUTHHI: feat = phM + gap; break;
                    case RK_LIP: feat = rd.off < 0.f ? phM - gap + rd.off * deg * pinch : phM + gap + rd.off * deg * pinch; break;
                    default: break;
                }
                phF = Lerp(plain, feat, lip ? wLip : wEye);
            }
            // under-chin rows start from the neck ring
            float ph0 = row0Phi[k];
            if (j <= 2) {
                float target = rows[2].phi * deg;
                phF = Lerp(ph0, target, (float)j / 3.f);
            }
            // back layout: spread rows evenly from the neck ring up to the crown
            float phB = Lerp(ph0, backTop, powf(rowFrac, 0.92f));
            float w = sstep(118.f * deg, 58.f * deg, ath);
            float ph = Lerp(phB, phF, w);
            // keep rows above the neck ring
            ph = Max(ph, ph0 + 0.6f * deg * (float)j);
            vec3 dir(cosf(ph) * sinf(th), cosf(ph) * cosf(th), sinf(ph));
            float t = c.sdf.castOut(C, dir, HM, 0.22f * hs);
            // relief finer than the face field resolves, along the ray: the upper lid crease (valley) with the skin fold
            // over it, the lower lid's pretarsal roll and its junction with the cheek, the white roll along the
            // vermilion border and the dimpled mouth corners
            float relief = 0.f, shadeK = 1.f;
            {
                float ue = (ath - L.thI) / Max(L.thO - L.thI, 1e-4f);   // 0 inner .. 1 outer eye corner
                float wLid = sstep(-0.12f, 0.12f, ue) * (1.f - sstep(0.88f, 1.15f, ue));
                if (rd.kind == RK_LIDUP && hasCrease && rd.off == creaseOff) {
                    relief = -D.creaseDepth * wLid * (0.8f + 0.3f * ue);
                    shadeK = 1.f - 0.12f * wLid;
                }
                if (rd.kind == RK_LIDUP && rd.off == foldOff) relief = (0.00015f + 0.00055f * D.hood) * wLid;
                if (rd.kind == RK_LIDLO && rd.off < 1.5f) relief = 0.0003f * wLid;
                if (rd.kind == RK_LIDLO && rd.off > 3.f) {
                    relief = -0.0002f * wLid;
                    shadeK = 1.f - 0.06f * wLid;
                }
                float um = ath / Max(L.thMC, 1e-4f);                   // 0 centre .. 1 mouth corner
                bool lipRow = rd.kind == RK_LIP || rd.kind == RK_MOUTHLO || rd.kind == RK_MOUTHHI;
                if (rd.kind == RK_LIP && (rd.off > 4.f || rd.off < -5.5f)) {
                    float wb = 1.f - sstep(0.75f, 1.02f, um);
                    relief = D.lipBorder * 0.00045f * wb;
                    shadeK = 1.f + 0.05f * D.lipBorder * wb;
                }
                if (lipRow && fabsf(rd.off) < 5.5f) {
                    float cw = bump(um, 1.f, 0.12f);
                    relief -= D.cornerDepth * cw;
                    shadeK *= 1.f - 0.22f * cw;
                }
            }
            vec3 p = C + dir * (t + relief * hs);
            BVert v;
            v.p = p;
            v.col = c.skin;
            v.mat = MAT_SKIN;
            v.part = PART_HEAD;
            v.side = p.x < 0.f ? 0 : 1;
            v.pa = th;
            v.pb = ph;
            v.uv = vec2(uWrap(th, kPi, 0.07f * hs), ph * 0.09f * hs);
            v.uPer = kTwoPi * 0.07f * hs;
            v.pc = 1.2f + (float)j / NRD;
            v.t = vec3(cosf(th), -sinf(th), 0.f);
            v.axisPt = C;
            // ---- weights: jaw for the lower face
            vec3 hp = (p - D.J[B_HEAD]) / hs;
            float front = sstep(95.f * deg, 50.f * deg, ath);
            float below = (j < H.rowMouthHi) ? 1.f : 0.f;
            float jawW = below * front;
            if (j < H.rowMouthLo) jawW = front * sstep(-0.004f, -0.012f, hp.z - Lf.stomion.z + 0.01f * (1.f - front));
            if (j <= H.rowMouthLo) jawW = Max(jawW, front * sstep(80.f * deg, 30.f * deg, ath));
            jawW *= sstep(-0.02f, 0.03f, hp.y);   // towards the ear the jaw influence fades
            if (j <= 2) jawW *= 0.6f;
            {
                // the commissures stay closed when the jaw drops: towards the mouth corners both sides of the slit
                // meet at half the jaw's weight, so the opening is lens shaped instead of splitting past the lips
                float uc = ath / Max(L.thMC, 1e-3f);
                float nearSlit = j < H.rowMouthHi ? 1.f - sstep(0.f, 2.5f, (float)(H.rowMouthLo - j)) : 1.f - sstep(0.f, 2.5f, (float)(j - H.rowMouthHi));
                float ck = sstep(0.68f, 1.02f, uc) * (1.f - sstep(1.3f, 1.8f, uc)) * nearSlit * front;
                if (j < H.rowMouthHi) jawW = Lerp(jawW, 0.5f * jawW, ck);
                else jawW = Max(jawW, 0.5f * ck);
            }
            WAcc acc;
            acc.add(B_JAW, jawW);
            acc.add(B_HEAD, 1.f - jawW);
            v.sw = acc.finish();
            // ---- base colours: vermilion, nostril shade (the regional skin tones are painted by paintFaceSkin)
            vec3 col = c.skin;
            if (rd.kind == RK_LIP || rd.kind == RK_MOUTHLO || rd.kind == RK_MOUTHHI) {
                float u = ath / L.thMC;
                float lipMask = 1.f - sstep(0.85f, 1.12f, u);
                float ao = fabsf(rd.off);
                if (rd.kind == RK_LIP) lipMask *= rd.off < 0.f ? 1.f - sstep(5.2f, 5.8f, ao) : 1.f - sstep(3.7f, 4.3f, ao);
                if (lipMask > 0.f) {
                    float inner = rd.kind != RK_LIP ? 1.f : 1.f - sstep(1.1f, 3.3f, ao);   // 1 at the mouth line
                    col = lerp(col, lerp(c.lipCol, c.lipInner, inner * (rd.off < 0.f ? 0.85f : 0.65f)), lipMask);
                    // the seam between the closed lips and the wet inner edge read darker
                    float inMouth = 1.f - sstep(0.8f, 1.02f, u);
                    if (rd.kind != RK_LIP) col = col * Lerp(1.f, 0.62f, inMouth);
                    else if (ao < 1.7f) col = col * Lerp(1.f, 0.88f, inMouth);
                    v.flags |= BuildCtx::F_LIP;
                }
            }
            {
                // nostrils: dark ovals on the underside of the nose either side of the columella, converging towards the
                // tip (a shade: the grid has no holes), with a soft darker rim inside the alae
                vec3 hq = hp;
                if (hq.z < Lf.noseTip.z + 0.002f && hq.z > Lf.subnasale.z - 0.004f && hq.y > Lf.subnasale.y - 0.002f &&
                    hq.y < Lf.noseTip.y + 0.004f && fabsf(hq.x) < 0.02f) {
                    vec3 gn = normalize(c.sdf.grad(p, HM));
                    float down = sstep(-0.15f, -0.55f, gn.z);
                    float best = 0.f;
                    for (int sd = 0; sd < 2; sd++) {
                        float sx = sd ? 1.f : -1.f;
                        vec2 nc(Lf.noseTip.x + sx * 0.0057f * D.noseW, Lerp(Lf.subnasale.y, Lf.noseTip.y, 0.4f));
                        float ang = sx * 0.42f;   // long axis leans in towards the tip
                        vec2 dq(hq.x - nc.x, hq.y - nc.y);
                        vec2 lq(dq.x * cosf(ang) - dq.y * sinf(ang), dq.x * sinf(ang) + dq.y * cosf(ang));
                        float e = Sq(lq.x / (0.0031f * D.noseW)) + Sq(lq.y / 0.0058f);
                        best = Max(best, 1.f - sstep(0.55f, 1.25f, e));
                    }
                    col = col * Lerp(1.f, 0.16f, best * down);
                    col = col * Lerp(1.f, 0.8f, down * (1.f - best) * sstep(0.004f, 0.009f, fabsf(hq.x)));
                }
            }
            col = col * shadeK;
            if (j >= H.rowMouthHi + 2 && j <= H.rowHairline) v.flags |= BuildCtx::F_FACE;
            if (ath < 110.f * deg && j < H.rowLidLo && j >= 1) v.flags |= BuildCtx::F_BEARD;
            if (j > H.rowLidHi) v.flags |= BuildCtx::F_SCALP;
            v.col = col;
            H.grid[(size_t)j * NC + k] = m.add(v);
        }
    }
    // ---- rays from C graze the underside of the nose (and the lower lip / chin junction): the rows there bunch up on
    // the lip and leave one long stretched band from the nose tip back to the subnasale. Resample those columns
    // along the surface (arc length through the row polyline, projected back onto the face) so the columella, the
    // nostril rims and the lip below get their share of the rows. Feature rows at the ends stay fixed.
    {
        auto rowAt = [&](float phiDeg) {
            for (int r = 0; r < NRD; r++)
                if (rows[r].kind == RK_PLAIN && rows[r].phi >= phiDeg) return r + 1;
            return NRD;
        };
        struct Span {
            int j0, j1;
            float thMax, uni;
        };
        const Span spans[2] = {{H.rowLipHi, rowAt(-10.f), 30.f * deg, 0.75f}, {rowAt(-54.f), H.rowLipLo, L.thMC * 1.25f, 0.5f}};
        std::vector<vec3> P, Q;
        std::vector<float> sArc, s0;
        for (const Span& sp : spans) {
            int n = sp.j1 - sp.j0 + 1;
            if (n < 3) continue;
            for (int k = 0; k < NC; k++) {
                float th0 = m.v[H.grid[(size_t)sp.j0 * NC + k]].pa;
                float at0 = th0 > kPi ? kTwoPi - th0 : th0;
                if (at0 > sp.thMax) continue;
                float fade = 1.f - sstep(sp.thMax * 0.7f, sp.thMax, at0);
                P.resize(n);
                for (int i = 0; i < n; i++) P[i] = m.v[H.grid[(size_t)(sp.j0 + i) * NC + k]].p;
                for (int it = 0; it < 3; it++) {
                    sArc.assign(n, 0.f);
                    for (int i = 1; i < n; i++) sArc[i] = sArc[i - 1] + length(P[i] - P[i - 1]);
                    if (it == 0) s0 = sArc;
                    float total = sArc[n - 1];
                    if (total < 1e-5f) break;
                    Q = P;
                    for (int i = 1; i + 1 < n; i++) {
                        float target = Lerp(s0[i] * total / Max(s0[n - 1], 1e-6f), total * i / (n - 1), sp.uni * fade);
                        int seg = 0;
                        while (seg + 2 < n && sArc[seg + 1] < target) seg++;
                        float f = Saturate((target - sArc[seg]) / Max(sArc[seg + 1] - sArc[seg], 1e-7f));
                        Q[i] = c.sdf.project(lerp(P[seg], P[seg + 1], f), HM, 6);
                    }
                    P = Q;
                }
                for (int i = 1; i + 1 < n; i++) {
                    BVert& v = m.v[H.grid[(size_t)(sp.j0 + i) * NC + k]];
                    v.p = P[i];
                    float th, ph;
                    angOf(v.p - C, th, ph);
                    if (th < 0.f) th += kTwoPi;
                    v.pa = th;
                    v.pb = ph;
                }
            }
        }
    }
    // pole
    {
        vec3 dir = normalize(vec3(0, -0.08f, 1.f));
        float t = c.sdf.castOut(C, dir, HM, 0.25f * hs);
        BVert v;
        v.p = C + dir * t;
        v.col = c.skin;
        v.mat = MAT_SKIN;
        v.part = PART_HEAD;
        v.sw = skin1(B_HEAD);
        v.pa = 0.f;
        v.pb = kHalfPi;
        v.pc = 2.2f;
        v.uv = vec2(0, 0.14f * hs);
        v.t = vec3(1, 0, 0);
        v.flags = BuildCtx::F_SCALP;
        v.axisPt = C;
        u32 pole = m.add(v);
        for (int k = 0; k < NC; k++) m.tri(H.grid[(size_t)(NR - 1) * NC + k], pole, H.grid[(size_t)(NR - 1) * NC + (k + 1) % NC]);
    }
    // ---- faces (skip the eye fissures and the mouth slit)
    for (int j = 0; j + 1 < NR; j++)
        for (int k = 0; k < NC; k++) {
            int k1 = (k + 1) % NC;
            u32 a0 = H.grid[(size_t)j * NC + k], a1 = H.grid[(size_t)j * NC + k1];
            u32 b0 = H.grid[(size_t)(j + 1) * NC + k], b1 = H.grid[(size_t)(j + 1) * NC + k1];
            if (j == H.rowEyeLo) {
                float t0 = m.v[a0].pa, t1 = m.v[a1].pa;
                float a0t = t0 > kPi ? kTwoPi - t0 : t0, a1t = t1 > kPi ? kTwoPi - t1 : t1;
                if (a0t >= L.thI - 1e-4f && a0t <= L.thO + 1e-4f && a1t >= L.thI - 1e-4f && a1t <= L.thO + 1e-4f) continue;
            }
            if (j == H.rowMouthLo) {
                float t0 = m.v[a0].pa, t1 = m.v[a1].pa;
                float a0t = t0 > kPi ? kTwoPi - t0 : t0, a1t = t1 > kPi ? kTwoPi - t1 : t1;
                if (a0t <= L.thMC + 1e-4f && a1t <= L.thMC + 1e-4f) continue;
            }
            // split each quad along the diagonal that keeps the surface convex-ish (follows the shorter diagonal)
            float d0 = length2(m.v[a0].p - m.v[b1].p), d1 = length2(m.v[b0].p - m.v[a1].p);
            if (d0 <= d1) m.quad(a0, b0, b1, a1);
            else {
                m.tri(a0, b0, a1);
                m.tri(b0, b1, a1);
            }
        }
    // Speech, eye and brow bones share the face: every row carries its weights for them (RowDef lipW / lidW /
    // browW), faded across the face by theta so the skin stretches smoothly into the head / jaw weights.
    //  - lips: the lower lip rows on B_LIP_LOWER (a child of the jaw), the upper lip rows on B_LIP_UPPER, the corners
    //    and the skin just outside them on B_LIP_CORNER_*;
    //  - upper lids ride on the eye bones (the skeleton has no lid bones): the margin rotates about the eyeball centre
    //    with the eye, so it follows vertical gaze like a real lid and the animator blinks by pitching the eye down;
    //    full weight across the middle of the fissure, nothing at the corners, the crease and fold follow partially;
    //  - forehead skin under the brows rides on the brow bones (raised / knitted brows move the skin with them).
    {
        const float halfSpan = 0.5f * (L.thO - L.thI);
        const float thIn = H.thetaEye - 22.f * deg, thOut = H.thetaEye + 26.f * deg;
        for (int r = 0; r < NRD; r++) {
            const RowDef& rd = rows[r];
            if (rd.lipW <= 0.f && rd.lidW <= 0.f && rd.browW <= 0.f) continue;
            int j = r + 1;
            bool upperLip = j >= H.rowMouthHi;
            for (int k = 0; k < NC; k++) {
                BVert& v = m.v[H.grid[(size_t)j * NC + k]];
                float th = v.pa;
                bool right = th < kPi;
                float at = right ? th : kTwoPi - th;
                if (rd.lipW > 0.f) {
                    float u = at / Max(L.thMC, 1e-3f);   // 0 centre .. 1 mouth corner
                    float wLip = 1.f - sstep(0.5f, 1.05f, u);
                    float wCor = sstep(0.4f, 0.95f, u) * (1.f - sstep(1.25f, 2.1f, u)) * (upperLip ? 0.85f : 0.8f);
                    float sum = wLip + wCor;
                    if (sum > 1.f) {
                        wLip /= sum;
                        wCor /= sum;
                    }
                    wLip *= rd.lipW;
                    wCor *= rd.lipW;
                    if (wLip + wCor > 1e-3f) {
                        WAcc acc;
                        float keep = 1.f - wLip - wCor;
                        for (int q = 0; q < 4; q++) acc.add(v.sw.b[q], v.sw.w[q] * keep);
                        acc.add(upperLip ? B_LIP_UPPER : B_LIP_LOWER, wLip);
                        acc.add(right ? B_LIP_CORNER_R : B_LIP_CORNER_L, wCor);
                        v.sw = acc.finish();
                    }
                }
                if (rd.lidW > 0.f) {
                    float dd = fabsf(at - H.thetaEye);
                    float w = rd.lidW * (1.f - sstep(0.62f * halfSpan, 1.02f * halfSpan, dd));
                    if (w > 0.f) {
                        WAcc acc;
                        for (int q = 0; q < 4; q++) acc.add(v.sw.b[q], v.sw.w[q] * (1.f - w));
                        acc.add(right ? B_EYE_R : B_EYE_L, w);
                        v.sw = acc.finish();
                    }
                }
                if (rd.browW > 0.f) {
                    float w = rd.browW * sstep(thIn - 10.f * deg, thIn + 4.f * deg, at) * (1.f - sstep(thOut - 6.f * deg, thOut + 10.f * deg, at));
                    if (w > 1e-3f) {
                        WAcc acc;
                        for (int q = 0; q < 4; q++) acc.add(v.sw.b[q], v.sw.w[q] * (1.f - w));
                        acc.add(right ? B_BROW_R : B_BROW_L, w);
                        v.sw = acc.finish();
                    }
                }
            }
        }
    }
    H.eyeC[0] = D.J[B_EYE_L];
    H.eyeC[1] = D.J[B_EYE_R];
    H.eyeR = Lm.eyeR * hs;
    for (int sd = 0; sd < 2; sd++) {
        H.earPos[sd] = headToModel(c, Lm.ear[sd]);
        H.lipCorner[sd] = headToModel(c, Lm.mouthCorner[sd]);
    }
}

// ------------------------------------------------------------------------------------------------
// Face details

// Eyeball: sclera sphere with a spherical corneal cap bulging over the iris (cornea radius 0.66 of the eye, meeting
// the sclera at the limbus, 29 degrees from the axis). uv = (phase, polar angle) * 0.01 radians (the renderer's eye
// shader keys iris fibres, the limbal ring, veins and the wet cornea on the polar angle). Vertex colours: pupil,
// iris (darker pupillary ring, lighter collarette, darkening towards the limbus), a soft grey-blue limbus, and a warm
// off-white sclera, pinker towards the corners and the back.
static void addEyeball(BuildCtx& c, int sd, vec3 irisCol) {
    MeshB& m = c.m;
    const HeadInfo& H = c.head;
    vec3 ctr = H.eyeC[sd];
    float r = H.eyeR;
    const int NS = 22;
    const float polar[] = {0.f, 4.f, 8.f, 11.f, 14.5f, 18.f, 22.f, 25.5f, 28.f, 29.5f, 31.5f, 35.f, 41.f, 55.f, 75.f, 100.f, 122.f};
    const int NP = (int)(sizeof(polar) / sizeof(polar[0]));
    const float limbus = 29.f * kDegToRad, rc = 0.66f;
    const float dc = cosf(limbus) - sqrtf(rc * rc - Sq(sinf(limbus)));   // cornea sphere centre along the axis (units of r)
    std::vector<u32> prev;
    // eyes look slightly outward-forward in bind
    vec3 fw = normalize(vec3((sd ? 1.f : -1.f) * 0.04f, 1.f, 0.f));
    vec3 ex, ez;
    ez = vec3(0, 0, 1);
    ex = normalize(cross(fw, ez));
    ez = cross(ex, fw);
    Rng rr(hash32(c.d->seed * 1741u + 5u));
    vec3 irisIn = lerp(irisCol, vec3(0.2f, 0.14f, 0.06f), 0.35f * rr.f());   // hazel-ish centre on some eyes
    vec3 scl = vec3(0.74f, 0.70f, 0.66f);
    for (int pi = 0; pi < NP; pi++) {
        float a = polar[pi] * kDegToRad;
        int n = pi == 0 ? 1 : NS;
        std::vector<u32> ring(n);
        float ca = cosf(a);
        float tr = a < limbus ? ca * dc + sqrtf(Max(0.f, Sq(ca * dc) - dc * dc + rc * rc)) : 1.f;   // radial distance (units of r)
        for (int k = 0; k < n; k++) {
            float ph = kTwoPi * k / NS;
            vec3 dir = fw * cosf(a) + (ex * cosf(ph) + ez * sinf(ph)) * sinf(a);
            vec3 p = ctr + dir * (r * tr);
            float pd = polar[pi];
            vec3 col;
            if (pd < 11.5f) col = vec3(0.01f, 0.009f, 0.009f);
            else if (pd < 29.f) {
                float t = (pd - 11.5f) / 17.5f;
                col = lerp(irisIn * 0.7f, irisCol * 1.1f, sstep(0.f, 0.35f, t));
                col = col * (1.f + 0.25f * bump(pd, 16.5f, 1.6f));        // collarette
                col = col * Lerp(1.f, 0.4f, sstep(22.f, 28.5f, pd));      // limbal darkening
            } else {
                float lim = 1.f - sstep(29.f, 34.f, pd);
                col = lerp(scl, vec3(0.42f, 0.42f, 0.46f), 0.55f * lim);
                float corner = Sq(cosf(ph));   // nasal / temporal sclera shows more vessels
                col = lerp(col, mulColor(col, vec3(1.02f, 0.86f, 0.84f)), sstep(38.f, 60.f, pd) * (0.4f + 0.6f * corner));
                if (pd > 70.f) col = vec3(0.6f, 0.45f, 0.42f);
            }
            BVert v;
            v.p = p;
            v.n = normalize(p - (a < limbus ? ctr + fw * (r * dc) : ctr));
            v.t = ex * -sinf(ph) + ez * cosf(ph);
            v.uv = vec2(ph * 0.01f, a * 0.01f);
            v.col = col;
            v.mat = MAT_EYE;
            v.part = PART_EYE;
            v.side = (u8)sd;
            v.sw = skin1(sd ? B_EYE_R : B_EYE_L);
            ring[k] = m.add(v);
        }
        if (pi == 1) {
            for (int k = 0; k < NS; k++) m.tri(prev[0], ring[(k + 1) % NS], ring[k]);
        } else if (pi > 1) {
            for (int k = 0; k < NS; k++) m.quad(prev[k], prev[(k + 1) % NS], ring[(k + 1) % NS], ring[k]);
        }
        prev = ring;
    }
}

// Winding helper: make triangle (a,b,c) face `out`.
static void triFacing(MeshB& m, u32 a, u32 b, u32 c, vec3 out) {
    vec3 n = cross(m.v[b].p - m.v[a].p, m.v[c].p - m.v[a].p);
    if (dot(n, out) < 0.f) m.tri(a, c, b);
    else m.tri(a, b, c);
}

// Ear: a relief over a polar grid in the ear plane (angle 0 = front edge towards the face, 90 = top, 180 = back,
// 270 = lobe; radius 0 = canal .. 1 = outline). The front surface carries the anatomy as heights along the ear's
// outward axis: canal and concha bowl (split by the crus of the helix), tragus and antitragus flaps round the
// intertragic notch, the Y-shaped antihelix with the triangular fossa between its crura, the scapha groove and the
// helix rim rolling over it; the lobe stays soft and thick. The rim wraps round to the back of the auricle and into
// the head.
static void addEar(BuildCtx& c, int sd) {
    const BodyDims& D = *c.D;
    MeshB& m = c.m;
    const float hs = D.headS * D.earSize;
    const float sx = sd ? 1.f : -1.f;
    vec3 root = c.head.earPos[sd];
    // snap root onto the head surface
    {
        vec3 dir = normalize(root - c.head.C);
        float t = c.sdf.castOut(c.head.C, dir, MK_HEAD, 0.2f);
        root = c.head.C + dir * t;
    }
    // ear frame from the head surface at the root: the auricle lies in the local tangent plane (long axis leaning back
    // ~15 degrees), rolled out about its front attachment line by the cephaloauricular angle, so the helix stands
    // 12-18 mm off the head at its top back instead of sticking out sideways
    vec3 nS = c.sdf.grad(root, MK_HEAD);
    nS = length2(nS) > 1e-12f ? normalize(nS) : vec3(sx, 0, 0);
    nS = normalize(lerp(nS, vec3(sx, 0.f, 0.f), 0.35f));
    vec3 back0 = vec3(0, -1, 0) - nS * dot(vec3(0, -1, 0), nS);
    back0 = normalize(back0);
    vec3 up0 = normalize(cross(nS, back0));
    if (up0.z < 0.f) up0 = -up0;
    const float tiltBack = 0.26f;
    vec3 up = normalize(up0 * cosf(tiltBack) + back0 * sinf(tiltBack));
    vec3 back = normalize(back0 - up * dot(back0, up));
    const float ang = D.earAngle * (sd ? 1.f + D.asymEar : 1.f);
    vec3 out = normalize(nS * cosf(ang) - back * sinf(ang));
    back = normalize(back * cosf(ang) + nS * sinf(ang));
    const int NE = 22;
    const float h = 0.062f * hs, w = 0.034f * hs;
    const vec2 cen(0.46f * w, 0.1f * h);   // canal (centre of the polar grid) behind / above the root
    auto outline = [&](float a) {
        float cu = cosf(a), su = sinf(a);
        float rx = 0.5f * w * (1.f + 0.12f * su), ry = 0.5f * h;
        if (su < 0.f) rx *= 1.f + 0.18f * su;   // narrower lobe
        return vec2(-cu * rx, su * ry);           // offset from cen
    };
    const float eo = D.earOut * (sd ? 1.f + D.asymEar : 1.f);
    // relief (meters along `out`, before the ear's own protrusion) at (angle a, radius s)
    auto relief = [&](float a, float s) {
        float ad = wrapAngle(a) * kRadToDeg;            // -180..180 (0 front, 90 top, -90 lobe)
        float adp = ad < 0.f ? ad + 360.f : ad;         // 0..360
        float lobe = bump(adp, 285.f, 32.f) * sstep(0.45f, 0.7f, s);
        float rimZone = sstep(15.f, 35.f, adp) * (1.f - sstep(240.f, 262.f, adp));
        float hlx = 0.0042f * bump(s, 0.975f, 0.045f) * rimZone;                    // helix rim
        float scapha = -0.0022f * bump(s, 0.86f, 0.05f) * rimZone;
        float antiArc = sstep(55.f, 85.f, adp) * (1.f - sstep(215.f, 245.f, adp));
        float anti = 0.0034f * bump(s, 0.7f, 0.07f) * antiArc;                     // antihelix body
        float supCrus = 0.0026f * bump(s, 0.74f, 0.06f) * bump(adp, 75.f, 22.f);   // superior crus towards the top
        float infCrus = 0.0024f * bump(s, 0.56f, 0.06f) * bump(adp, 55.f, 18.f);   // inferior crus forward
        float fossa = -0.0018f * bump(s, 0.66f, 0.06f) * bump(adp, 62.f, 12.f);     // triangular fossa
        float concha = -0.0055f * (1.f - sstep(0.3f, 0.58f, s)) * (1.f - lobe);
        float crusHelix = 0.0032f * bump(adp, 28.f, 12.f) * sstep(0.28f, 0.5f, s) * (1.f - sstep(0.85f, 0.98f, s));
        float tragus = 0.0048f * bump(ad, -12.f, 20.f) * bump(s, 0.42f, 0.1f);
        float antitragus = 0.0036f * bump(adp, 238.f, 16.f) * bump(s, 0.54f, 0.07f);
        float notch = -0.002f * bump(adp, 270.f, 14.f) * bump(s, 0.45f, 0.08f);
        float canal = -0.009f * (1.f - sstep(0.04f, 0.16f, s));
        float lobeBody = 0.0022f * lobe;
        return 1.3f * (hlx + scapha + anti + supCrus + infCrus + fossa + concha + crusHelix + tragus + antitragus + notch + canal + lobeBody) *
               D.headS;
    };
    // the auricle's own thickness off its plane (the stand-off comes from the frame's angle): a little fuller at the
    // back of the rim, the lobe flatter
    auto protrude = [&](float a, float s) {
        float frontness = sstep(0.2f, 1.f, cosf(a));
        float lobe = sstep(0.2f, 1.f, -sinf(a));
        return (0.0012f + s * Lerp(0.0012f, 0.0026f, 1.f - frontness) * (1.f - 0.4f * lobe)) * D.headS;
    };
    (void)eo;
    const float fr[] = {0.06f, 0.18f, 0.31f, 0.43f, 0.55f, 0.65f, 0.73f, 0.81f, 0.87f, 0.93f, 0.985f};
    const int NFr = (int)(sizeof(fr) / sizeof(fr[0]));
    std::vector<std::vector<u32>> rings;
    vec3 earCol = lerp(c.skin, mulColor(c.skin, vec3(1.1f, 0.85f, 0.82f)), 0.35f);
    size_t i0 = m.idx.size();
    auto addRing = [&](float sRad, float offMode, float colMul, float depthExtra) {
        std::vector<u32> ring(NE);
        for (int k = 0; k < NE; k++) {
            float a = kTwoPi * k / NE;
            vec2 q = cen + outline(a) * sRad;
            float o;
            if (offMode == 0.f) o = protrude(a, Min(sRad, 1.f)) + relief(a, sRad);
            else o = offMode * D.headS;
            BVert v;
            v.p = root + back * q.x + up * q.y + out * (o + depthExtra);
            float rl = relief(a, Min(sRad, 1.f)) / D.headS;
            v.col = earCol * colMul * (offMode == 0.f ? Saturate(0.86f + 22.f * rl) : 1.f);
            v.mat = MAT_SKIN;
            v.part = PART_EAR;
            v.side = (u8)sd;
            v.sw = skin1(B_HEAD);
            v.uv = vec2(q.x * 3.f, q.y * 3.f);
            v.t = up;
            ring[k] = m.add(v);
        }
        rings.push_back(ring);
    };
    // canal cap centre, front surface rings, the rim, the back of the auricle, the root inside the head
    for (int i = 0; i < NFr; i++) addRing(fr[i], 0.f, 1.f, 0.f);
    const int iRimFront = NFr - 1;
    // rim: the helix rolls over the edge; back rings step towards the head
    {
        std::vector<u32> rim(NE), back1(NE), back2(NE), rootR(NE);
        for (int k = 0; k < NE; k++) {
            float a = kTwoPi * k / NE;
            float oF = protrude(a, 1.f) + relief(a, 1.f);
            float thick = (0.0034f + 0.0022f * sstep(0.2f, 1.f, -sinf(a))) * D.headS;
            vec2 q1 = cen + outline(a) * 1.02f, q2 = cen + outline(a) * 0.95f, q3 = cen + outline(a) * 0.8f, q4 = cen + outline(a) * 0.62f;
            auto mk = [&](vec2 q, float o, float cm) {
                BVert v;
                v.p = root + back * q.x + up * q.y + out * o;
                v.col = earCol * cm;
                v.mat = MAT_SKIN;
                v.part = PART_EAR;
                v.side = (u8)sd;
                v.sw = skin1(B_HEAD);
                v.uv = vec2(q.x * 3.f, q.y * 3.f);
                v.t = up;
                return m.add(v);
            };
            rim[k] = mk(q1, oF - thick * 0.45f, 1.02f);
            back1[k] = mk(q2, oF - thick, 0.95f);
            // the back of the auricle curves in to meet the head (postauricular groove): the root ring sits on the
            // skull, 2 mm under the skin, and the ring before it halfway between
            u32 rr = mk(q4, 0.f, 0.86f);
            vec3 onHead = c.sdf.project(m.v[rr].p, MK_HEAD, 6);
            vec3 nh = c.sdf.grad(onHead, MK_HEAD);
            nh = length2(nh) > 1e-12f ? normalize(nh) : out;
            m.v[rr].p = onHead - nh * (0.002f * D.headS);
            rootR[k] = rr;
            back2[k] = mk(q3, 0.f, 0.9f);
            m.v[back2[k]].p = lerp(m.v[back1[k]].p, onHead + nh * (0.0015f * D.headS), 0.55f);
        }
        rings.push_back(rim);
        rings.push_back(back1);
        rings.push_back(back2);
        rings.push_back(rootR);
    }
    const int nR = (int)rings.size();
    // triangles: front surface faces `out`, the rim faces away from the canal, the back faces the head
    for (int r = 1; r < nR; r++)
        for (int k = 0; k < NE; k++) {
            u32 a0 = rings[r - 1][k], a1 = rings[r - 1][(k + 1) % NE], b0 = rings[r][k], b1 = rings[r][(k + 1) % NE];
            vec3 cq = (m.v[a0].p + m.v[a1].p + m.v[b0].p + m.v[b1].p) * 0.25f;
            vec3 radial = normalize(cq - (root + back * cen.x + up * cen.y));
            vec3 f = r <= iRimFront ? out : (r == iRimFront + 1 ? normalize(radial + out * 0.6f) : (r == iRimFront + 2 ? radial : -out * 0.6f + radial * 0.4f));
            triFacing(m, a0, b0, b1, f);
            triFacing(m, a0, b1, a1, f);
        }
    // cap the canal
    {
        BVert v = m.v[rings[0][0]];
        v.p = root + back * cen.x + up * cen.y + out * (protrude(0.f, 0.f) - 0.011f * D.headS);
        v.col = earCol * 0.45f;
        u32 ctr = m.add(v);
        for (int k = 0; k < NE; k++) triFacing(m, rings[0][k], ctr, rings[0][(k + 1) % NE], out);
    }
    m.computeNormals(i0, m.idx.size());
}

// Eyebrow: rows of short strand cards lying on the brow ridge (CARD_BROW), combed like real brow hair: the medial
// head points up, the body sweeps up and out, the tail runs out and slightly down. The skin under the brow takes a
// tint of the brow colour so the alpha-tested cards read full. Skinned to the brow bone (raise / knit).
static void addBrow(BuildCtx& c, int sd, vec3 col) {
    const BodyDims& D = *c.D;
    MeshB& m = c.m;
    const HeadInfo& H = c.head;
    const float sx = sd ? 1.f : -1.f;
    const float deg = kDegToRad;
    Rng r(hash32(c.d->seed * 7717u + 31u + (u32)sd));
    float thick = Lerp(1.f, 0.72f, D.fem) * D.browThick;
    float thIn = H.thetaEye - 14.5f * deg, thOut = H.thetaEye + 19.5f * deg;
    // the brow lies on the supraorbital ridge: its lower edge ~1.5 cm above the eye centre (about 1 cm above the lid
    // margin), a little higher on women (browH)
    float phBase = 18.4f * deg + 1.2f * deg * (D.browH - 1.f) * 5.f + (sd ? D.asymBrow / 0.09f : 0.f);
    // brow shape in (lateral angle, elevation): lower edge + height along u (0 medial head .. 1 tail)
    auto lower = [&](float u) {
        float arch = (Lerp(1.2f, 2.6f, D.fem) * D.browArch * sinf(kPi * powf(u, 0.8f)) - 0.8f * u + D.browTilt * u * u) * deg;
        float hgt = Lerp(4.2f, 1.5f, powf(u, 1.1f)) * deg * thick;
        return phBase + arch - 0.45f * hgt;
    };
    auto height = [&](float u) { return Lerp(4.2f, 1.5f, powf(u, 1.1f)) * deg * thick; };
    auto surf = [&](float at, float ph, vec3& p, vec3& n) {
        // outermost surface along the ray (from outside: the ray from the grid centre first leaves the solid inside
        // the carved eye socket under the brow ridge)
        vec3 dir(cosf(ph) * sinf(at) * sx, cosf(ph) * cosf(at), sinf(ph));
        float t = 0.2f * D.headS;
        float f = c.sdf.eval(H.C + dir * t, MK_HEAD);
        for (int it = 0; it < 200 && f > 0.f; it++) {
            float tn = t - Max(f * 0.8f, 0.0003f);
            float fn = c.sdf.eval(H.C + dir * tn, MK_HEAD);
            if (fn <= 0.f) {
                float lo = tn, hi = t;   // inside at lo, outside at hi
                for (int b = 0; b < 12; b++) {
                    float mid = 0.5f * (lo + hi);
                    if (c.sdf.eval(H.C + dir * mid, MK_HEAD) <= 0.f) lo = mid;
                    else hi = mid;
                }
                t = 0.5f * (lo + hi);
                break;
            }
            t = tn;
            f = fn;
        }
        p = H.C + dir * t;
        vec3 g = c.sdf.grad(p, MK_HEAD);
        n = length2(g) > 1e-12f ? normalize(g) : dir;
    };
    const SkinW sw = skin1(sd ? B_BROW_R : B_BROW_L);
    vec3 colRoot = col * 0.7f, colTip = col * 1.05f;
    CardPt pts[3];
    const int rowsN = 3;
    for (int row = 0; row < rowsN; row++) {
        int n = row == 2 ? 11 : 17;
        for (int i = 0; i < n; i++) {
            float u = Saturate(((float)i + 0.5f + 0.35f * (r.f() - 0.5f)) / n);
            float at = Lerp(thIn, thOut, u);
            float v = row == 0 ? 0.2f : (row == 1 ? 0.55f : 0.8f);
            float ph = lower(u) + height(u) * (v + 0.12f * (r.f() - 0.5f)) - 0.35f * height(u);
            // growth direction in (lateral, up) angle space
            float up = Lerp(0.8f, -0.25f, sstep(0.f, 0.85f, u)) + 0.15f * (r.f() - 0.5f);
            float out = Lerp(0.45f, 1.f, sstep(0.f, 0.4f, u));
            float len = Lerp(0.0036f, 0.0068f, sstep(0.f, 0.35f, u)) * Lerp(1.f, 0.85f, sstep(0.6f, 1.f, u)) * D.headS * (0.85f + 0.3f * r.f()) *
                        (row == 2 ? 0.8f : 1.f);
            float wdt = Lerp(0.0036f, 0.0026f, u) * D.headS * (0.9f + 0.4f * thick);
            vec3 p0, n0;
            surf(at, ph, p0, n0);
            float R = length(p0 - H.C);
            vec2 g2 = normalize(vec2(out, up));
            int np = 0;
            for (int k = 0; k < 3; k++) {
                float f = (float)k / 2.f;
                float a2 = at + g2.x * len * f / (R * cosf(ph));
                float p2 = ph + g2.y * len * f / R;
                vec3 pk, nk;
                surf(a2, p2, pk, nk);
                float lift = (0.00035f + 0.0006f * f) * D.headS;
                pts[np].p = pk + nk * lift;
                pts[np].n = nk;
                pts[np].w = wdt * (1.f - 0.4f * f);
                pts[np].sw = sw;
                np++;
            }
            emitCard(m, pts, np, CARD_BROW, r.next(), colRoot, colTip, Lerp(1.f, 0.85f, u) * (row == 2 ? 0.8f : 1.f), PART_FACEDETAIL, nullptr);
        }
    }
    // tint the skin under the brow (the cards are sparse at their tips)
    for (int j = 0; j < H.rows; j++)
        for (int k = 0; k < H.cols; k++) {
            BVert& v = m.v[H.grid[(size_t)j * H.cols + k]];
            float th = v.pa;
            bool right = th < kPi;
            if ((sd == 1) != right) continue;
            float at = right ? th : kTwoPi - th;
            float u = (at - thIn) / (thOut - thIn);
            if (u < -0.08f || u > 1.08f) continue;
            float uc = Saturate(u);
            float lo = lower(uc), hi = lo + height(uc);
            float inside = Min(Min(v.pb - lo, hi - v.pb) / (0.6f * deg), Min(u + 0.04f, 1.04f - u) * 12.f);
            float w = sstep(-0.6f, 1.f, inside) * 0.58f;
            if (w > 0.f) v.col = lerp(v.col, mulColor(v.col, col * 2.f) * 0.5f + col * 0.5f, w);
        }
}

// Upper eyelashes and a lid-margin tuck strip around each eye opening.
static void addLidDetails(BuildCtx& c, int sd, vec3 lashCol) {
    const BodyDims& D = *c.D;
    MeshB& m = c.m;
    const HeadInfo& H = c.head;
    int NC = H.cols;
    // collect fissure boundary vertices from the grid rows
    std::vector<u32> up, lo;
    for (int k = 0; k < NC; k++) {
        u32 vu = H.grid[(size_t)H.rowEyeHi * NC + k], vl = H.grid[(size_t)H.rowEyeLo * NC + k];
        float th = m.v[vu].pa;
        bool right = th < kPi;
        if ((sd == 1) != right) continue;
        float at = right ? th : kTwoPi - th;
        if (fabsf(at - H.thetaEye) > 12.5f * kDegToRad) continue;
        up.push_back(vu);
        lo.push_back(vl);
    }
    vec3 ec = H.eyeC[sd];
    float er = H.eyeR;
    // tuck strips: from the margin towards the eyeball surface
    auto tuck = [&](const std::vector<u32>& ring, bool upper) {
        std::vector<u32> inner;
        for (u32 vi : ring) {
            BVert v = m.v[vi];
            vec3 d = normalize(v.p - ec);
            v.p = ec + d * (er * 0.985f);
            v.col = mulColor(c.skin, vec3(0.85f, 0.55f, 0.5f));
            v.flags = 0;
            v.part = PART_FACEDETAIL;   // the tear film: glossy where the lid meets the eye (applySkinChannels)
            inner.push_back(m.add(v));
        }
        for (size_t i = 0; i + 1 < ring.size(); i++) {
            vec3 cen = (m.v[ring[i]].p + m.v[ring[i + 1]].p) * 0.5f;
            vec3 f = normalize(ec + vec3(0, 0.02f, 0) - cen);   // facing into the fissure / forwards
            f = normalize(f + vec3(0, 0.6f, 0));
            (void)upper;
            triFacing(m, ring[i], ring[i + 1], inner[i + 1], f);
            triFacing(m, ring[i], inner[i + 1], inner[i], f);
        }
    };
    size_t i0 = m.idx.size();
    tuck(up, true);
    tuck(lo, false);
    m.computeNormals(i0, m.idx.size());
    // lashes: strand cards along the lid margins (CARD_LASH), each covering ~2 margin vertices; upper lashes curl
    // up and out, the lower ones are short and sparse and point down; skinned like the margin (the upper lid rides on
    // the eye bone)
    Rng r(hash32(c.d->seed * 3301u + 7u + (u32)sd));
    for (int lid = 0; lid < 2; lid++) {
        const std::vector<u32>& ring = lid == 0 ? up : lo;
        if (ring.size() < 3) continue;
        float len = Lerp(0.0028f, 0.0045f, D.fem) * D.headS * (lid == 0 ? 1.f : 0.42f);
        for (size_t i = 0; i + 1 < ring.size(); i++) {
            if (lid == 1 && (i & 1)) continue;
            const BVert& a0 = m.v[ring[i]];
            const BVert& a1 = m.v[ring[i + 1]];
            vec3 root = (a0.p + a1.p) * 0.5f;
            float at = a0.pa > kPi ? kTwoPi - a0.pa : a0.pa;
            float u = Saturate((at - (H.thetaEye - 11.f * kDegToRad)) / (22.f * kDegToRad));
            float l = len * (0.35f + 0.65f * sinf(kPi * Saturate(u * 0.9f + 0.05f))) * (0.85f + 0.3f * r.f());
            vec3 d = normalize(root - ec);
            vec3 along = normalize(a1.p - a0.p);
            vec3 dirL = lid == 0 ? normalize(d * 0.7f + vec3(0, 0.55f, 0.35f)) : normalize(d * 0.8f + vec3(0, 0.45f, -0.4f));
            vec3 curl = lid == 0 ? vec3(0, 0, 1) : vec3(0, 0, -1);
            vec3 nrm = normalize(cross(along, dirL));
            if (nrm.y < 0.f) nrm = -nrm;
            CardPt pts[3];
            for (int k = 0; k < 3; k++) {
                float f = (float)k / 2.f;
                vec3 dk = normalize(dirL + curl * (0.55f * f * f));
                pts[k].p = (k == 0 ? root + d * 0.0002f : pts[k - 1].p + dk * (l * 0.5f));
                pts[k].n = nrm;
                pts[k].w = (length(a1.p - a0.p) * 1.7f + 0.0006f) * (1.f - 0.25f * f);
                pts[k].sw = lerpSkin(a0.sw, a1.sw, 0.5f);
            }
            emitCard(m, pts, 3, CARD_LASH, r.next(), lashCol, lashCol * 1.3f, lid == 0 ? 1.f : 0.6f, PART_FACEDETAIL, nullptr);
        }
    }
}

// Teeth rows and a dark mouth cavity behind the lips.
static void addMouth(BuildCtx& c) {
    const BodyDims& D = *c.D;
    MeshB& m = c.m;
    FaceLm Lm;
    faceLandmarks(c, Lm);
    const float hs = D.headS;
    size_t i0 = m.idx.size();
    // inner lips (vestibule): from each lip's edge a short skirt curls up (upper lip) / down (lower lip) in front of
    // the teeth, so an open mouth shows the wet lip lining instead of a paper-thin edge; it closes at the corners.
    // The lining copies the lip edge's weights (lips follow the speech bones) and blends into the head / jaw deeper in.
    {
        const HeadInfo& H = c.head;
        const int NC = H.cols;
        std::vector<int> order;   // slit columns from the left corner round the front to the right corner
        for (int k = NC - 1; k > NC / 2; k--) {
            float th = m.v[H.grid[(size_t)H.rowMouthHi * NC + k]].pa;
            if (kTwoPi - th <= H.thetaMouth + 1e-4f) order.insert(order.begin(), k);
        }
        for (int k = 0; k < NC / 2; k++) {
            float th = m.v[H.grid[(size_t)H.rowMouthHi * NC + k]].pa;
            if (th <= H.thetaMouth + 1e-4f) order.push_back(k);
        }
        vec3 axis = headToModel(c, vec3(0, 0.07f, 0.f));
        for (int lip = 0; lip < 2; lip++) {   // 0 lower, 1 upper
            int j = lip ? H.rowMouthHi : H.rowMouthLo;
            float up = lip ? 1.f : -1.f;
            std::vector<u32> L0, L1, L2;
            for (int k : order) {
                const BVert src = m.v[H.grid[(size_t)j * NC + k]];
                float th = src.pa;
                float at = th > kPi ? kTwoPi - th : th;
                float f = 1.f - powf(Saturate(at / Max(H.thetaMouth, 1e-3f)), 4.f);
                vec3 in = vec3(axis.x - src.p.x, axis.y - src.p.y, 0.f);
                in = length2(in) > 1e-10f ? normalize(in) : vec3(0, -1, 0);
                BVert v = src;
                v.part = PART_MOUTH;
                v.flags = 0;
                v.col = c.lipCol * 0.85f;
                v.n = normalize(vec3(0, 0.6f, -up));
                L0.push_back(m.add(v));
                v.p = src.p + (in * 0.0025f + vec3(0, 0, up * 0.0035f)) * (hs * f);
                v.col = c.lipCol * 0.55f + vec3(0.07f, 0.012f, 0.012f);
                L1.push_back(m.add(v));
                v.p = src.p + (in * 0.0055f + vec3(0, 0, up * 0.0105f)) * (hs * f);
                v.col = vec3(0.16f, 0.045f, 0.045f);
                v.sw = lerpSkin(src.sw, skin1(lip ? B_HEAD : B_JAW), 0.6f);
                L2.push_back(m.add(v));
            }
            vec3 face = normalize(vec3(0, 1.f, -up * 0.9f));
            for (size_t q = 0; q + 1 < L0.size(); q++) {
                triFacing(m, L0[q], L0[q + 1], L1[q + 1], face);
                triFacing(m, L0[q], L1[q + 1], L1[q], face);
                triFacing(m, L1[q], L1[q + 1], L2[q + 1], face);
                triFacing(m, L1[q], L2[q + 1], L2[q], face);
            }
        }
    }
    // cavity: half ellipsoid facing inwards (wide enough to back the mouth corners when they spread)
    {
        // its front rim sits back inside the cheeks (it must not show past the lip corners)
        vec3 cen = headToModel(c, vec3(0, 0.078f, -0.02f));
        vec3 rr = vec3(0.032f, 0.031f, 0.016f) * hs;
        const int NU = 10, NV = 6;
        std::vector<u32> g((NU + 1) * (NV + 1));
        for (int i = 0; i <= NU; i++)
            for (int j = 0; j <= NV; j++) {
                float a = kPi * i / NU;            // around (0..pi from +x to -x through -y back)
                float b = -kHalfPi + kPi * j / NV;  // elevation
                vec3 d(cosf(a) * cosf(b), -sinf(a) * cosf(b) * 1.f, sinf(b));
                BVert v;
                v.p = cen + vec3(d.x * rr.x, d.y * rr.y, d.z * rr.z);
                v.n = -d;
                v.col = vec3(0.06f, 0.015f, 0.015f);
                v.mat = MAT_SKIN;
                v.part = PART_MOUTH;
                WAcc acc;
                acc.add(B_JAW, 0.5f - 0.5f * sinf(b));
                acc.add(B_HEAD, 0.5f + 0.5f * sinf(b));
                v.sw = acc.finish();
                g[i * (NV + 1) + j] = m.add(v);
            }
        for (int i = 0; i < NU; i++)
            for (int j = 0; j < NV; j++) {
                u32 a = g[i * (NV + 1) + j], b = g[(i + 1) * (NV + 1) + j], cc = g[(i + 1) * (NV + 1) + j + 1], d = g[i * (NV + 1) + j + 1];
                triFacing(m, a, b, cc, normalize(cen - m.v[a].p));
                triFacing(m, a, cc, d, normalize(cen - m.v[a].p));
            }
    }
    // teeth: upper and lower arch strips with individual crowns (incisors ~8 mm, then narrower towards the canines and
    // premolars): a rounded incisal edge per tooth, the labial face bulging between the gaps, darker interproximal lines
    // and the gum margin in pink
    for (int row = 0; row < 2; row++) {
        const int NU = 40;
        const float up = row == 0 ? 1.f : -1.f;
        std::vector<u32> gum, top, bot;
        float zRoot = row == 0 ? -0.0105f : -0.0285f, zEdge = row == 0 ? -0.0198f : -0.0205f;
        const vec3 gumCol = lerp(c.lipCol, vec3(0.62f, 0.3f, 0.3f), 0.6f);
        for (int i = 0; i <= NU; i++) {
            float u = (float)i / NU * 2.f - 1.f;
            float au = fabsf(u);
            float tp = row == 0 ? (au < 0.4f ? au / 0.4f * 2.f : 2.f + (au - 0.4f) / 0.6f * 3.f) : au * 5.f;   // tooth index
            float ft = tp - floorf(tp);                       // 0 / 1 at a gap .. 0.5 mid-tooth
            float mid = sinf(kPi * ft);                       // 0 at the gaps, 1 mid-tooth
            float x = u * 0.02f;
            float y = 0.0905f - 0.018f * u * u + 0.00035f * mid;   // labial bulge of each crown
            // the incisal edge is straight across each crown and rounds off at its corners
            float edge = zEdge + up * 0.0005f * (1.f - sstep(0.f, 0.24f, Min(ft, 1.f - ft)));
            vec3 nrm = normalize(vec3(u * 0.8f + 0.25f * cosf(kPi * ft) * (u < 0.f ? -1.f : 1.f), 1.f, 0.f));
            float tint = 1.f + 0.05f * (hashToFloat(hash32((u32)floorf(tp) * 977u + (u32)row * 31u + (u < 0.f ? 7u : 0u))) - 0.5f);
            vec3 toothCol = vec3(0.68f, 0.65f, 0.56f) * (1.f - 0.4f * au) * (row == 0 ? 1.f : 0.85f) * tint *
                            (0.66f + 0.34f * sstep(0.f, 0.3f, mid));
            for (int e = 0; e < 3; e++) {
                BVert v;
                float z = e == 0 ? zRoot + up * 0.0025f : (e == 1 ? zRoot : edge);
                v.p = headToModel(c, vec3(x, y - (e == 0 ? 0.0006f : 0.f), z));
                v.n = nrm;
                v.col = e == 0 ? gumCol : (e == 1 ? lerp(toothCol, gumCol, 0.25f) : toothCol * 0.97f);
                v.mat = e == 0 ? MAT_SKIN : MAT_EYE;
                v.part = PART_MOUTH;
                v.sw = skin1(row == 0 ? B_HEAD : B_JAW);
                (e == 0 ? gum : (e == 1 ? top : bot)).push_back(m.add(v));
            }
        }
        for (int i = 0; i < NU; i++) {
            vec3 f = m.v[top[i]].n;
            triFacing(m, top[i], top[i + 1], bot[i + 1], f);
            triFacing(m, top[i], bot[i + 1], bot[i], f);
            triFacing(m, gum[i], gum[i + 1], top[i + 1], f);
            triFacing(m, gum[i], top[i + 1], top[i], f);
        }
    }
    // tongue: a domed blade on the floor of the mouth (tip just behind the lower incisors, below their edge), on
    // B_TONGUE which pitches it up behind / between the teeth for TH, DD, nn
    {
        const int NU = 7, NV = 6;
        std::vector<u32> g((NU + 1) * (NV + 1));
        for (int i = 0; i <= NU; i++) {
            float u = (float)i / NU;   // 0 back .. 1 tip
            float y = Lerp(0.05f, 0.0872f, u);
            float half = 0.0165f * sqrtf(Max(0.f, 1.f - powf(u, 3.f))) + 0.0015f;
            for (int j = 0; j <= NV; j++) {
                float vv = (float)j / NV * 2.f - 1.f;
                float z = -0.0262f + 0.0045f * (1.f - u) - 0.0045f * vv * vv - 0.0015f * u * u;
                BVert v;
                v.p = headToModel(c, vec3(vv * half, y, z));
                v.n = normalize(vec3(vv * 0.6f, 0.15f * u, 1.f));
                v.col = lerp(vec3(0.3f, 0.07f, 0.07f), vec3(0.62f, 0.25f, 0.24f), 0.35f + 0.65f * u) * (1.f - 0.25f * vv * vv);
                v.mat = MAT_SKIN;
                v.part = PART_MOUTH;
                v.sw = skin1(B_TONGUE);
                g[i * (NV + 1) + j] = m.add(v);
            }
        }
        for (int i = 0; i < NU; i++)
            for (int j = 0; j < NV; j++) {
                u32 a = g[i * (NV + 1) + j], b = g[(i + 1) * (NV + 1) + j], cc = g[(i + 1) * (NV + 1) + j + 1], d = g[i * (NV + 1) + j + 1];
                triFacing(m, a, b, cc, vec3(0, 0, 1));
                triFacing(m, a, cc, d, vec3(0, 0, 1));
            }
    }
    (void)i0;
    (void)Lm;
}

// Smooth 3D value noise in [0, 1] (skin blotches; cell size 1 / freq).
static float skinNoise3(vec3 p, float freq, u32 seed) {
    vec3 q = p * freq;
    float fx = floorf(q.x), fy = floorf(q.y), fz = floorf(q.z);
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    float tx = sstep(q.x - fx), ty = sstep(q.y - fy), tz = sstep(q.z - fz);
    auto h = [&](int x, int y, int z) { return hashToFloat(hash32((u32)x * 73856093u ^ (u32)y * 19349663u ^ (u32)z * 83492791u ^ seed)); };
    float a = Lerp(Lerp(h(ix, iy, iz), h(ix + 1, iy, iz), tx), Lerp(h(ix, iy + 1, iz), h(ix + 1, iy + 1, iz), tx), ty);
    float b = Lerp(Lerp(h(ix, iy, iz + 1), h(ix + 1, iy, iz + 1), tx), Lerp(h(ix, iy + 1, iz + 1), h(ix + 1, iy + 1, iz + 1), tx), ty);
    return Lerp(a, b, tz);
}

// Regional skin colour in the vertex colours: blush over the cheeks, nose and ears (hemoglobin shows through light
// skin; darker skin only warms slightly), darker and cooler orbits (upper lid crease, tear trough, inner corners), a
// crisper vermilion, freckles on some fair young faces, age spots on some older ones, and palms / soles lighter than
// the back of the hand / foot on darker skin (palms are painted with the fingers; the soles here).
static float paintSkinDetail(BuildCtx& c) {
    const CharacterDesc& d = *c.d;
    const BodyDims& D = *c.D;
    FaceLm Lm;
    faceLandmarks(c, Lm);
    mapLandmarks(D, Lm);
    const float hs = D.headS;
    const float lum = dot(c.skin, vec3(0.3f, 0.59f, 0.11f));
    const float fair = sstep(0.08f, 0.45f, lum);    // 0 deep .. 1 fair
    Rng r(hash32(d.seed * 0x27d4eb2du + 0x165667b1u));
    const float blushAmt = Lerp(0.3f, 1.f, fair) * r.range(0.55f, 1.45f) * Lerp(1.f, 1.12f, D.fem);   // pale .. ruddy people
    const float orbAmt = Lerp(0.8f, 0.55f, fair) * r.range(0.7f, 1.2f) * (0.8f + 0.5f * D.age);
    const float freckles = (fair > 0.55f && d.age < 0.5f && r.chance(0.28f)) ? r.range(0.35f, 1.f) : 0.f;
    const float ageSpots = (fair > 0.3f && d.age > 0.55f && r.chance(0.55f)) ? sstep(0.55f, 0.95f, d.age) * r.range(0.5f, 1.f) : 0.f;
    // fold darkening (separate stream: the draws above keep their values)
    Rng rf(hash32(d.seed * 0x632BE5ABu + 0x9u));
    const float foldNL = (0.12f + 0.4f * sstep(0.2f, 0.9f, d.age) + 0.12f * Saturate(D.weight - 0.5f)) * rf.range(0.7f, 1.2f);
    const float foldMar = 0.45f * sstep(0.5f, 0.95f, d.age) * rf.range(0.6f, 1.2f);
    const vec3 red(1.1f, 0.84f, 0.82f);
    const vec3 warm = vec3(1.05f, 0.95f, 0.9f);
    const u32 seed = hash32(d.seed * 131u + 71u);
    // sun exposure: face, ears, neck, forearms and the backs of the hands a little darker and warmer than the
    // covered body on light and medium skin (varies per person: outdoor workers more)
    const float sun = Lerp(0.02f, 0.1f, fair) * r.range(0.3f, 1.3f) * (d.role == 5 || d.role == 4 ? 1.5f : 1.f);
    const vec3 sunMul(1.f - 0.6f * sun, 1.f - 1.1f * sun, 1.f - 1.5f * sun);
    for (BVert& v : c.m.v) {
        if (v.mat != MAT_SKIN) continue;
        if (v.flags & BuildCtx::F_SOLE) {
            v.col = lerp(v.col, c.palmCol, 0.8f);
            continue;
        }
        {
            // uneven pigmentation and redness at the centimetre scale (skin is never one flat colour); the renderer's
            // mottling adds the finer scale
            float n1 = skinNoise3(v.p, 28.f, seed ^ 0x51u), n2 = skinNoise3(v.p, 61.f, seed ^ 0xA7u);
            float val = 1.f + 0.07f * (n1 - 0.5f) + 0.04f * (n2 - 0.5f);
            vec3 hue = lerp(vec3(1.02f, 0.99f, 0.97f), vec3(0.985f, 1.005f, 1.02f), skinNoise3(v.p, 17.f, seed ^ 0x3Bu));
            v.col = mulColor(v.col, hue) * val;
            float exp = 0.f;
            if (v.part == PART_HEAD || v.part == PART_EAR || v.part == PART_NECK) exp = 1.f;
            else if (v.part == PART_ARM) exp = sstep(0.35f, 0.6f, v.pc);   // forearms
            else if (v.part == PART_HAND || v.part == PART_FINGER || v.part == PART_THUMB) exp = (v.flags & BuildCtx::F_PALM) ? 0.2f : 0.9f;
            if (exp > 0.f) v.col = lerp(v.col, mulColor(v.col, sunMul), exp);
        }
        if (v.part != PART_HEAD && v.part != PART_EAR) continue;
        vec3 hp = (v.p - D.J[B_HEAD]) / hs;
        vec3 col = v.col;
        float blush = 0.f;
        if (v.part == PART_EAR) blush = 0.55f;
        for (int sd = 0; sd < 2; sd++) {
            float sx = sd ? 1.f : -1.f;
            vec3 ck(sx * 0.037f, 0.074f, 0.014f);
            vec3 dq = (hp - ck);
            blush = Max(blush, 0.8f * expf(-(Sq(dq.x / 0.017f) + Sq(dq.y / 0.03f) + Sq(dq.z / 0.016f))));
            vec3 da = hp - Lm.ala[sd];
            blush = Max(blush, 0.7f * expf(-length2(da) / Sq(0.0075f)));
        }
        {
            vec3 dt = hp - (Lm.noseTip + vec3(0, -0.005f, -0.002f));
            blush = Max(blush, 0.75f * expf(-length2(dt) / Sq(0.0095f)));
            vec3 dc = hp - (Lm.chin + vec3(0, -0.004f, 0.002f));
            blush = Max(blush, 0.3f * expf(-length2(dc) / Sq(0.012f)));
        }
        if (blush > 0.f) col = lerp(col, mulColor(col, lerp(warm, red, fair)), Saturate(blush * blushAmt));
        // orbits: darker / cooler around the eye opening, deepest at the inner corner and the tear trough
        if (v.part == PART_HEAD) {
            float orb = 0.f;
            for (int sd = 0; sd < 2; sd++) {
                vec3 de = hp - Lm.eye[sd];
                float rr = length(de);
                float ring = sstep(Lm.eyeR + 0.0005f, Lm.eyeR + 0.004f, rr) * (1.f - sstep(Lm.eyeR + 0.008f, Lm.eyeR + 0.016f, rr));
                float below = sstep(0.002f, -0.008f, de.z);
                float inner = sstep(0.004f, -0.012f, de.x * (sd ? 1.f : -1.f));
                orb = Max(orb, ring * (0.45f + 0.35f * below + 0.35f * inner));
            }
            if (orb > 0.f) col = lerp(col, mulColor(col, Lerp(1.f, 0.72f, orbAmt) * lerp(vec3(0.93f, 0.9f, 0.96f), vec3(1.f), 1.f - fair)), Saturate(orb));
            // folds that must read at conversation distance (the shader's crease channel only resolves up close): the
            // nasolabial fold from beside the nose wing to beside the mouth corner (faint on the young, deep with
            // age) and, later in life, the marionette lines down from the corners
            if (hp.y > 0.05f) {
                auto segD = [&](vec3 a, vec3 b) {
                    vec3 ab = b - a;
                    float t = Saturate(dot(hp - a, ab) / Max(dot(ab, ab), 1e-10f));
                    return length(hp - (a + ab * t));
                };
                float fold = 0.f;
                for (int sd = 0; sd < 2; sd++) {
                    float sx = sd ? 1.f : -1.f;
                    vec3 nlA = Lm.ala[sd] + vec3(sx * 0.0045f, -0.004f, 0.001f), nlB = Lm.mouthCorner[sd] + vec3(sx * 0.0065f, -0.004f, -0.005f);
                    fold = Max(fold, foldNL * expf(-Sq(segD(nlA, nlB) / 0.0022f)));
                    vec3 mA = Lm.mouthCorner[sd] + vec3(sx * 0.0015f, -0.002f, -0.003f), mB = Lm.mouthCorner[sd] + vec3(sx * 0.0045f, -0.005f, -0.021f);
                    fold = Max(fold, foldMar * expf(-Sq(segD(mA, mB) / 0.0018f)));
                }
                if (fold > 0.f) col = lerp(col, mulColor(col, vec3(0.84f, 0.78f, 0.78f)), Saturate(fold));
            }
            // vermilion: the lip rows already carry lipCol; darken the corners and the wet line a touch
            if (v.flags & BuildCtx::F_LIP) {
                float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
                float cor = sstep(0.6f, 1.05f, at / Max(c.head.thetaMouth, 1e-3f));
                col = col * (1.f - 0.18f * cor);
            }
            // freckles over the nose and cheeks, age spots on the forehead, temples and cheeks
            if (freckles > 0.f && (v.flags & BuildCtx::F_FACE)) {
                // the grid is too coarse for single freckles: a soft tan dusting with some speckle, plus the density
                // in the vertex alpha (1 - density) for the renderer's procedural spots
                float zone = expf(-(Sq(hp.x / 0.045f) + Sq((hp.z - 0.035f) / 0.03f))) * sstep(0.05f, 0.08f, hp.y) * freckles;
                float n = skinNoise3(hp, 900.f, seed);
                col = lerp(col, mulColor(col, vec3(0.9f, 0.82f, 0.74f)), zone * (0.25f + 0.35f * sstep(0.45f, 0.75f, n)));
            }
            if (ageSpots > 0.f && hp.z > -0.01f) {
                float nz = skinNoise3(hp, 110.f, seed);
                float spot = sstep(0.77f, 0.84f, nz) * ageSpots * sstep(0.02f, 0.06f, hp.y + 0.03f);
                if (spot > 0.f) col = lerp(col, mulColor(col, vec3(0.84f, 0.74f, 0.64f)), spot * 0.45f);
            }
        }
        v.col = col;
    }
    return freckles;
}

// Small raised skin spots, LOD0 only (tiny PART_ACC components are dropped from LOD1): moles by seed on the face and
// neck, freckle specks over the nose and cheeks of freckled faces, a few reddish blemishes on some young faces. Each
// spot is a low dome on the head surface, skinned like the nearest head grid vertex.
static void addSkinSpots(BuildCtx& c, float freckles) {
    const CharacterDesc& d = *c.d;
    const BodyDims& D = *c.D;
    const HeadInfo& H = c.head;
    MeshB& m = c.m;
    const float hs = D.headS;
    Rng r(hash32(d.seed * 0x9E3779B9u + 0x7F4A7C15u));
    FaceLm Lm;
    faceLandmarks(c, Lm);
    mapLandmarks(D, Lm);
    auto nearestGrid = [&](vec3 p) {
        u32 best = H.grid[0];
        float bd = 1e9f;
        for (u32 vi : H.grid) {
            float dd = length2(m.v[vi].p - p);
            if (dd < bd) {
                bd = dd;
                best = vi;
            }
        }
        return best;
    };
    auto spot = [&](float th, float ph, float rad, vec3 col, float dome) {
        vec3 dir(cosf(ph) * sinf(th), cosf(ph) * cosf(th), sinf(ph));
        float t = c.sdf.castOut(H.C, dir, MK_HEAD, 0.25f * hs);
        vec3 p = H.C + dir * t;
        vec3 n = c.sdf.grad(p, MK_HEAD);
        n = length2(n) > 1e-12f ? normalize(n) : dir;
        // keep clear of the eyes, the mouth slit and the nostrils
        vec3 hp = (p - D.J[B_HEAD]) / hs;
        for (int sd = 0; sd < 2; sd++)
            if (length(hp - Lm.eye[sd]) < Lm.eyeR + 0.006f) return;
        if (length(hp - Lm.stomion) < 0.012f || (fabsf(hp.x) < 0.012f && hp.z < Lm.noseTip.z + 0.004f && hp.z > Lm.subnasale.z - 0.004f)) return;
        const BVert& g = m.v[nearestGrid(p)];
        if (g.flags & BuildCtx::F_LIP) return;
        vec3 u = normalize(anyPerp(n)), w = cross(n, u);
        const int NS = 6;
        BVert v;
        v.mat = MAT_SKIN;
        v.part = PART_ACC;
        v.side = g.side;
        v.sw = g.sw;
        v.col = col;
        v.t = u;
        v.n = n;
        v.p = p + n * ((0.00025f + dome) * hs);
        u32 ci = m.add(v);
        u32 first = (u32)m.v.size();
        for (int k = 0; k < NS; k++) {
            float a = kTwoPi * k / NS + r.f() * 0.4f;
            vec3 dd = u * cosf(a) + w * sinf(a);
            v.p = p + dd * (rad * hs * (0.85f + 0.3f * r.f())) + n * (0.00008f * hs);
            v.n = normalize(n + dd * 0.35f);
            v.col = lerp(col, g.col, 0.35f);
            m.add(v);
        }
        for (int k = 0; k < NS; k++) {
            u32 a = first + k, b = first + (k + 1) % NS;
            vec3 fn = cross(m.v[a].p - m.v[ci].p, m.v[b].p - m.v[ci].p);
            if (dot(fn, n) >= 0.f) m.tri(ci, a, b);
            else m.tri(ci, b, a);
        }
    };
    const float deg = kDegToRad;
    // moles: 0-3 on the face, darker and more raised on fair skin, some flat
    {
        float x = r.f();
        int n = x < 0.04f ? 3 : (x < 0.13f ? 2 : (x < 0.4f ? 1 : 0));
        for (int i = 0; i < n; i++) {
            float th = r.range(-75.f, 75.f) * deg, ph = r.range(-45.f, 28.f) * deg;
            vec3 col = lerp(mulColor(c.skin, vec3(0.45f, 0.38f, 0.34f)), vec3(0.06f, 0.035f, 0.025f), r.range(0.2f, 0.6f));
            spot(th < 0.f ? th + kTwoPi : th, ph, r.range(0.0011f, 0.0022f), col, r.chance(0.6f) ? r.range(0.0002f, 0.0006f) : 0.f);
        }
    }
    // freckle specks over the nose and the upper cheeks
    if (freckles > 0.f) {
        int n = (int)(freckles * r.range(50.f, 110.f));
        for (int i = 0; i < n; i++) {
            float th = r.range(-55.f, 55.f) * deg, ph = r.range(-22.f, 8.f) * deg;
            float zone = expf(-Sq(th / (38.f * deg)) - Sq((ph + 7.f * deg) / (10.f * deg)));
            if (r.f() > zone) continue;
            vec3 col = mulColor(c.skin, vec3(0.8f, 0.66f, 0.55f) * r.range(0.9f, 1.05f));
            spot(th < 0.f ? th + kTwoPi : th, ph, r.range(0.0005f, 0.0011f), col, 0.f);
        }
    }
    // blemishes on some young faces (forehead, cheeks, chin)
    if (d.age < 0.3f && r.chance(0.25f)) {
        int n = r.irange(2, 6);
        for (int i = 0; i < n; i++) {
            float th = r.range(-60.f, 60.f) * deg, ph = r.pick(std::vector<float>{r.range(-50.f, -35.f), r.range(-15.f, 0.f), r.range(22.f, 40.f)}) * deg;
            vec3 col = lerp(c.skin, mulColor(c.skin, vec3(1.15f, 0.68f, 0.66f)), 0.7f);
            spot(th < 0.f ? th + kTwoPi : th, ph, r.range(0.0009f, 0.0017f), col, 0.0002f);
        }
    }
}

// Skin detail channels for the renderer's skin shader (MAT_SKIN vertices of the final mesh, after the outfit has
// copied the skin's uvs): colour alpha = 1 - gloss (lips, mouth lining and the lids' wet margins, nails, the oily
// T-zone), uv = (crease phase, crease depth in mm) for the age lines: forehead lines, frown lines between the brows,
// crow's feet, fine lines under the eyes and above the upper lip, neck rings (the fingers' joint creases and knuckle
// wrinkles come from buildFingers). Crease centres sit at frac(phase) = 0.5.
void applySkinChannels(const BuildCtx& c, MeshB& fin) {
    const CharacterDesc& d = *c.d;
    const BodyDims& D = *c.D;
    FaceLm Lm;
    faceLandmarks(c, Lm);
    mapLandmarks(D, Lm);
    const float hs = D.headS, a = Saturate(D.age);
    Rng r(hash32(d.seed * 0x85EBCA77u + 0xC2B2AE3Du));
    const float kFore = sstep(0.22f, 0.85f, a) * r.range(0.6f, 1.25f) * (1.f + 0.4f * Saturate(1.f - 2.f * D.weight));
    const float kFrown = sstep(0.3f, 0.8f, a) * (r.chance(0.6f) ? r.range(0.5f, 1.2f) : 0.f);
    const float kCrow = sstep(0.28f, 0.8f, a) * r.range(0.6f, 1.2f) * (0.8f + 0.4f * sstep(0.1f, 0.4f, dot(c.skin, vec3(0.3f, 0.59f, 0.11f))));
    const float kUnder = sstep(0.4f, 0.9f, a) * r.range(0.5f, 1.1f);
    const float kLip = sstep(0.55f, 1.f, a) * r.range(0.4f, 1.1f) * Lerp(1.f, 1.3f, D.fem);
    const float kNeck = sstep(0.4f, 1.f, a) * r.range(0.5f, 1.1f);
    const float browZ = 0.083f + 0.012f * (D.browH - 1.f) * 5.f * 0.2f;
    const float foreheadTop = 0.13f + 0.002f * D.foreheadH;
    const float waveS = r.range(0.f, 6.28f);
    for (BVert& v : fin.v) {
        if (v.mat != MAT_SKIN) continue;
        // gloss
        float gloss = 0.f;
        if (v.flags & BuildCtx::F_LIP) gloss = 0.85f;
        if (v.part == PART_MOUTH) gloss = 0.8f;
        if (v.part == PART_FACEDETAIL) gloss = 0.75f;   // lid margins' inner edge (tuck strips): the tear film's wet line
        if (v.flags & BuildCtx::F_NAIL) gloss = 0.62f;
        vec3 hp = (v.p - D.J[B_HEAD]) / hs;
        if (v.part == PART_HEAD && !(v.flags & BuildCtx::F_LIP)) {
            float nose = expf(-Sq(hp.x / 0.012f) - Sq((hp.z - Lm.noseTip.z - 0.012f) / 0.02f)) * sstep(0.09f, 0.1f, hp.y);
            float fore = expf(-Sq(hp.x / 0.025f) - Sq((hp.z - 0.1f) / 0.018f)) * sstep(0.07f, 0.085f, hp.y);
            gloss = Max(gloss, 0.22f * Max(nose, fore * 0.7f));
        }
        v.alpha = 1.f - Saturate(gloss);
        // fingers and thumbs carry their joint creases from buildFingers
        if (v.part == PART_FINGER || v.part == PART_THUMB) {
            v.uPer = 0.f;
            continue;
        }
        // wrinkles (head and neck skin only; everything else no creases)
        float phase = 0.f, depth = 0.f;
        if (v.part == PART_HEAD || v.part == PART_NECK) {
            auto take = [&](float ph, float dp) {
                if (dp > depth) {
                    depth = dp;
                    phase = ph;
                }
            };
            if (v.part == PART_HEAD && hp.y > 0.02f) {
                // forehead: horizontal lines ~9 mm apart between the brows and the hairline, gently wavy, fading at
                // the temples
                float w = sstep(browZ + 0.004f, browZ + 0.012f, hp.z) * (1.f - sstep(foreheadTop - 0.02f, foreheadTop, hp.z)) *
                          (1.f - sstep(0.035f, 0.052f, fabsf(hp.x)));
                if (w > 0.f && kFore > 0.f) take((hp.z - browZ) / 0.009f + 0.12f * sinf(hp.x * 90.f + waveS), 0.35f * kFore * w);
                // frown lines: a vertical crease each side of the glabella
                float wg = sstep(Lm.nasion.z + 0.002f, Lm.nasion.z + 0.008f, hp.z) * (1.f - sstep(browZ + 0.006f, browZ + 0.016f, hp.z)) *
                           (1.f - sstep(0.009f, 0.013f, fabsf(hp.x)));
                if (wg > 0.f && kFrown > 0.f) take((fabsf(hp.x) - 0.0055f) / 0.012f + 0.5f + 0.15f * sinf(hp.z * 300.f), 0.22f * kFrown * wg);
                for (int sd = 0; sd < 2; sd++) {
                    float sx = sd ? 1.f : -1.f;
                    vec3 e = Lm.eye[sd];
                    // crow's feet: lines radiating from the outer eye corner, 8 degrees apart
                    vec3 oc = e + vec3(sx * 0.0145f, -0.004f, 0.001f);
                    float dx = (hp.x - oc.x) * sx, dz = hp.z - oc.z;
                    float rr = sqrtf(dx * dx + dz * dz);
                    if (dx > -0.002f && rr < 0.026f) {
                        float ang = atan2f(dz, Max(dx, 1e-4f));
                        float wc = sstep(0.004f, 0.009f, rr) * (1.f - sstep(0.017f, 0.025f, rr)) * sstep(-1.0f, -0.6f, ang) * (1.f - sstep(0.5f, 0.9f, ang));
                        if (wc > 0.f && kCrow > 0.f) take(ang / (8.f * kDegToRad) + 0.5f, 0.15f * kCrow * wc);
                    }
                    // under the eye: fine arcs below the lower lid
                    float de = e.z - hp.z;
                    float wu = sstep(0.006f, 0.009f, de) * (1.f - sstep(0.013f, 0.017f, de)) * (1.f - sstep(0.008f, 0.013f, fabsf(hp.x - e.x)));
                    if (wu > 0.f && kUnder > 0.f) take((de - 0.006f) / 0.0028f, 0.08f * kUnder * wu);
                }
                // above the upper lip: fine vertical lines
                float wl = sstep(Lm.stomion.z + 0.006f, Lm.stomion.z + 0.009f, hp.z) * (1.f - sstep(Lm.subnasale.z - 0.004f, Lm.subnasale.z, hp.z)) *
                           (1.f - sstep(0.013f, 0.02f, fabsf(hp.x)));
                if (wl > 0.f && kLip > 0.f) take(hp.x / 0.0024f, 0.08f * kLip * wl);
            }
            // neck: horizontal rings on the front and sides
            if (v.part == PART_NECK || (v.part == PART_HEAD && hp.z < -0.05f && hp.y > 0.f)) {
                float wn = v.part == PART_NECK ? 1.f : sstep(-0.05f, -0.07f, hp.z);
                vec3 lp = v.p;
                float front = Saturate(lp.y - D.J[B_NECK].y + 0.02f);
                if (kNeck > 0.f) take(lp.z / 0.013f, 0.2f * kNeck * wn * sstep(0.f, 0.03f, front));
            }
        }
        v.uv = vec2(depth > 0.f ? phase : 0.f, depth);
        v.uPer = 0.f;
    }
}

double gProf[16];
void buildFaceDetails(BuildCtx& c) {
    double t00 = TimeSeconds();
    const CharacterDesc& d = *c.d;
    const BodyDims& D = *c.D;
    // iris color from seed and skin tone (darker skin -> mostly brown)
    Rng r(hash32(d.seed * 0x85ebca6bu + 0x33u));
    float lum = dot(c.skin, vec3(0.3f, 0.59f, 0.11f));
    vec3 iris;
    float pick = r.f();
    float lightChance = sstep(0.12f, 0.45f, lum);
    if (pick < 0.55f + 0.4f * (1.f - lightChance)) iris = lerp(vec3(0.05f, 0.025f, 0.012f), vec3(0.14f, 0.07f, 0.03f), r.f());
    else if (pick < 0.8f) iris = lerp(vec3(0.16f, 0.13f, 0.05f), vec3(0.2f, 0.17f, 0.07f), r.f());   // hazel/green
    else iris = lerp(vec3(0.12f, 0.2f, 0.3f), vec3(0.2f, 0.3f, 0.38f), r.f());                     // blue/gray
    addEyeball(c, 0, iris);
    addEyeball(c, 1, iris);
    double t01 = TimeSeconds(); gProf[0] += t01 - t00;
    addEar(c, 0);
    addEar(c, 1);
    double t02 = TimeSeconds(); gProf[1] += t02 - t01;
    vec3 browCol = d.hairColor * 0.8f;
    if (d.age > 0.75f) browCol = lerp(browCol, vec3(0.45f, 0.43f, 0.4f), 0.5f);
    // brows stay darker than the skin they sit on (a dark complexion with brown-dyed hair still has dark brows; fair
    // brows on pale skin keep a little contrast), so they read at a distance
    browCol = vmin(browCol, d.skinTone * 0.42f);
    double t03 = TimeSeconds();
    addBrow(c, 0, browCol);
    addBrow(c, 1, browCol);
    double t04 = TimeSeconds(); gProf[2] += t04 - t03;
    vec3 lash = d.hairColor * 0.35f + vec3(0.004f);
    addLidDetails(c, 0, lash);
    addLidDetails(c, 1, lash);
    double t05 = TimeSeconds(); gProf[3] += t05 - t04;
    addMouth(c);
    double t06 = TimeSeconds(); gProf[12] += t06 - t05;
    float freckles = paintSkinDetail(c);
    addSkinSpots(c, freckles);
    gProf[4] += TimeSeconds() - t06;
    (void)D;
}

}  // namespace detail
}  // namespace Anim
