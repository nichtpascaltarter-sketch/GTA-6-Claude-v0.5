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

static inline vec3 headToModel(const BuildCtx& c, vec3 hp) { return c.D->J[B_HEAD] + hp * c.D->headS; }

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
        L.ala[sd] = vec3(sx * 0.0128f * D.noseW, 0.094f + 0.003f * (D.noseP - 1.f), 0.0118f - 0.004f * (D.noseL - 1.f));
        L.mouthCorner[sd] = vec3(sx * 0.0245f * D.lipW * D.faceW, 0.0865f, -0.0185f);
        L.ear[sd] = vec3(sx * 0.0695f * D.faceW, -0.01f, 0.036f);
    }
    L.eyeR = 0.0119f * D.eyeSize;
    L.nasion = vec3(0, 0.0845f - 0.004f * (1.f - D.noseBridge), 0.0595f);
    L.noseTip = vec3(0, 0.1105f + 0.009f * (D.noseP - 1.f), 0.019f - 0.012f * (D.noseL - 1.f));
    L.subnasale = vec3(0, 0.0935f, 0.0045f - 0.006f * (D.noseL - 1.f));
    L.stomion = vec3(0, 0.0952f, -0.0195f);
    L.chin = vec3(0, 0.0885f + 0.006f * (D.chinP - 1.f), -0.050f * D.chinH);
    L.menton = vec3(0, 0.071f + 0.004f * (D.chinP - 1.f), -0.0645f * D.chinH);
}

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
    const float fem = D.fem, wc = D.weight - 0.5f;
    // cranium and forehead
    S.ellipsoid(P(0, -0.013f, 0.074f), vec3(0.0752f * D.faceW, 0.098f * D.headLen, 0.1f) * hs, HM, R(0.01f));
    float fs = D.foreheadSlope;
    S.ellipsoid(P(0, 0.034f - 0.004f * fs, 0.088f), vec3(0.061f, 0.055f, 0.062f) * hs, HM, R(0.03f));
    // brow ridge (two segments wrapping around the forehead)
    float br = (0.0085f + 0.004f * D.browRidge);
    S.ellipsoid(P(0, 0.0775f + 0.002f * D.browRidge, 0.0765f), vec3(0.015f, br * 1.0f, br * 1.05f) * hs, HM, R(0.018f));
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        S.cone(P(sx * 0.011f, 0.0775f + 0.002f * D.browRidge, 0.0775f), P(sx * 0.046f, 0.064f, 0.0785f), R(br), R(br * 0.78f), HM, R(0.018f));
    }
    // mid face (maxilla + cheeks) and lower face (mandible) as broad smooth masses
    S.ellipsoid(P(0, 0.028f, 0.012f), vec3(0.0615f * D.faceW, 0.062f, 0.062f) * hs, HM, R(0.02f));
    float jw = D.jawW;
    S.ellipsoid(P(0, 0.029f, -0.027f * D.chinH), vec3(0.0525f * jw, 0.058f, 0.041f * D.chinH) * hs, HM, R(0.024f));
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        S.ellipsoid(P(sx * 0.0495f * D.faceW, 0.053f, 0.035f), vec3(0.02f, 0.016f, 0.0115f) * (hs * (0.85f + 0.2f * D.cheekB)), HM, R(0.022f));
        // zygomatic arch towards the ear
        S.cone(P(sx * 0.052f * D.faceW, 0.045f, 0.036f), P(sx * 0.064f * D.faceW, 0.0f, 0.03f), R(0.008f), R(0.006f), HM, R(0.014f));
        // jaw angle
        S.ellipsoid(P(sx * 0.049f * jw, 0.004f, -0.03f), vec3(0.012f, 0.019f, 0.016f) * (hs * Lerp(1.f, 0.7f, fem)), HM, R(0.02f));
        // cheek fullness (younger/heavier faces)
        float cf = Saturate(0.3f + 0.5f * fem + 1.2f * wc + 0.3f * (1.f - D.age));
        S.ellipsoid(P(sx * 0.036f, 0.055f, 0.006f), vec3(0.022f, 0.019f, 0.024f) * (hs * (0.5f + 0.4f * cf)), HM, R(0.03f));
    }
    S.ellipsoid(Pv(L.chin + vec3(0, -0.009f, 0.001f)), vec3(0.0195f * (1.f + 0.25f * (jw - 1.f)), 0.013f, 0.0165f) * hs, HM, R(0.015f));
    // under the chin into the neck
    S.ellipsoid(P(0, 0.028f, -0.057f * D.chinH), vec3(0.036f, 0.045f, 0.022f) * hs, HM | MK_NECK, R(0.022f));
    // muzzle / mouth region
    S.ellipsoid(P(0, 0.064f, -0.012f), vec3(0.034f * D.lipW, 0.0305f, 0.029f) * hs, HM, R(0.02f));
    // lips: upper and lower, each a curved chain of round cones
    float lf = D.lipFull;
    float ulR = 0.0052f * lf, llR = 0.0066f * lf;
    vec3 ulC(0, L.stomion.y + 0.0003f, L.stomion.z + 0.0058f), llC(0, L.stomion.y - 0.0028f, L.stomion.z - 0.0066f);
    for (int sd = 0; sd < 2; sd++) {
        float sx = sd ? 1.f : -1.f;
        vec3 mc = L.mouthCorner[sd];
        vec3 ulM(sx * 0.0115f * D.lipW, ulC.y - 0.0012f, ulC.z + 0.0004f), llM(sx * 0.011f * D.lipW, llC.y - 0.001f, llC.z - 0.0002f);
        S.cone(Pv(ulC), Pv(ulM), R(ulR), R(ulR * 0.95f), HM, R(0.003f));
        S.cone(Pv(ulM), Pv(mc + vec3(-sx * 0.002f, 0.0f, 0.0015f)), R(ulR * 0.95f), R(0.0022f), HM, R(0.003f));
        S.cone(Pv(llC), Pv(llM), R(llR), R(llR * 0.92f), HM, R(0.003f));
        S.cone(Pv(llM), Pv(mc + vec3(-sx * 0.002f, 0.0f, -0.0015f)), R(llR * 0.92f), R(0.0022f), HM, R(0.003f));
    }
    // nose: bridge, tip, wings, columella
    float nw = D.noseW;
    S.cone(Pv(L.nasion + vec3(0, -0.005f, 0.001f)), Pv(L.noseTip + vec3(0, -0.006f, 0.004f)), R(0.0058f * (0.8f + 0.2f * nw)),
           R(0.0088f * nw), HM, R(0.012f), 1.f, 1.f, vec3(1, 0, 0));
    S.ellipsoid(Pv(L.noseTip + vec3(0, -0.0072f, -0.0005f)), vec3(0.0098f * nw, 0.0096f, 0.0092f) * hs, HM, R(0.008f));
    for (int sd = 0; sd < 2; sd++) {
        S.ellipsoid(Pv(L.ala[sd] + vec3(0, -0.0015f, 0.0005f)), vec3(0.0072f, 0.0078f, 0.0068f) * hs, HM, R(0.006f));
    }
    S.cone(Pv(L.noseTip + vec3(0, -0.006f, -0.0065f)), Pv(L.subnasale + vec3(0, 0.001f, 0.002f)), R(0.0036f), R(0.0038f), HM, R(0.004f));
    // eye sockets (carved), then lids wrapped around the eyeballs
    for (int sd = 0; sd < 2; sd++) {
        vec3 e = L.eye[sd];
        Prim& q = S.prims[S.ellipsoid(Pv(e + vec3(0, 0.0055f, 0.0018f)), vec3(0.0195f, 0.0145f, 0.0135f) * hs, HM, R(0.01f))];
        q.op = OP_SUB;
    }
    for (int sd = 0; sd < 2; sd++) {
        vec3 e = L.eye[sd];
        float er = L.eyeR;
        S.ellipsoid(Pv(e), vec3(er + 0.0009f) * hs, HM, R(0.003f));
        // upper lid fullness (hooded/monolid for high lidFold)
        float up = 0.0012f + 0.0016f * D.lidFold;
        S.ellipsoid(Pv(e + vec3(0, -0.0006f, 0.0032f)), vec3(er + 0.0016f, er + up, er * 0.78f) * hs, HM, R(0.0035f));
        S.ellipsoid(Pv(e + vec3(0, -0.001f, -0.0035f)), vec3(er + 0.0011f, er + 0.0009f, er * 0.62f) * hs, HM, R(0.003f));
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

void buildHeadGrid(BuildCtx& c) {
    const BodyDims& D = *c.D;
    MeshB& m = c.m;
    HeadInfo& H = c.head;
    FaceLm Lm;
    faceLandmarks(c, Lm);
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
        vec3 inner = e + vec3(-0.0132f * D.eyeSize, 0.0078f, -0.0012f) * hs;
        vec3 outer = e + vec3(0.0138f * D.eyeSize, 0.0018f, 0.0010f + 0.03f * D.eyeTilt) * hs;
        float th, ph;
        angOf(inner - C, L.thI, L.phI);
        angOf(outer - C, L.thO, L.phO);
        vec3 top = e + vec3(0.001f, er * 0.93f, er * 0.3f);
        vec3 bot = e + vec3(0.0015f, er * 0.9f, -er * 0.43f);
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
    }
    const float deg = kDegToRad;
    // ---- column layouts (right half, 23 columns strictly between 0 and pi)
    const int KH = 23;
    const int NC = 2 * KH + 2;
    H.cols = NC;
    std::vector<float> colUniform(KH), colEye(KH), colMouth(KH), colNose(KH), colFace(KH);
    for (int k = 0; k < KH; k++) colUniform[k] = kPi * (k + 1) / (KH + 1);
    for (int k = 0; k < KH; k++) colFace[k] = kPi * powf((k + 1.f) / (KH + 1), 1.3f);
    {
        int n = 0;
        for (int k = 1; k <= 3; k++) colEye[n++] = L.thI * k / 3.f;
        for (int k = 1; k <= 7; k++) colEye[n++] = L.thI + (L.thO - L.thI) * k / 7.f;
        int rest = KH - n;
        for (int k = 1; k <= rest; k++) colEye[n++] = L.thO + (kPi - L.thO) * powf((float)k / (rest + 1), 1.3f);
    }
    {
        int n = 0;
        for (int k = 1; k <= 6; k++) colMouth[n++] = L.thMC * k / 6.f;
        int rest = KH - n;
        for (int k = 1; k <= rest; k++) colMouth[n++] = L.thMC + (kPi - L.thMC) * powf((float)k / (rest + 1), 1.38f);
    }
    {
        int n = 0;
        for (int k = 1; k <= 4; k++) colNose[n++] = L.thAla * k / 4.f;
        int rest = KH - n;
        for (int k = 1; k <= rest; k++) colNose[n++] = L.thAla + (kPi - L.thAla) * powf((float)k / (rest + 1), 1.4f);
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
    // ---- row definitions (front phi in degrees; special kinds)
    enum { RK_PLAIN, RK_LIPLO2, RK_LIPLO1, RK_MOUTHLO, RK_MOUTHHI, RK_LIPUP1, RK_LIPUP2, RK_LIDLO, RK_EYELO, RK_EYEHI, RK_LIDUP };
    struct RowDef {
        float phi;
        int kind;
        int colA, colB;   // 0 uniform 1 face 2 mouth 3 nose 4 eye
        float colT;
    };
    const RowDef rows[] = {
        {-68.f, RK_PLAIN, 0, 1, 0.35f}, {-63.5f, RK_PLAIN, 0, 1, 0.7f}, {-58.5f, RK_PLAIN, 1, 2, 0.2f}, {-53.5f, RK_PLAIN, 1, 2, 0.5f},
        {-48.5f, RK_PLAIN, 1, 2, 0.8f}, {-44.8f, RK_PLAIN, 2, 2, 0.f}, {0.f, RK_LIPLO2, 2, 2, 0.f}, {0.f, RK_LIPLO1, 2, 2, 0.f},
        {0.f, RK_MOUTHLO, 2, 2, 0.f}, {0.f, RK_MOUTHHI, 2, 2, 0.f}, {0.f, RK_LIPUP1, 2, 2, 0.f}, {0.f, RK_LIPUP2, 2, 2, 0.f},
        {-27.8f, RK_PLAIN, 2, 3, 0.4f}, {-25.3f, RK_PLAIN, 2, 3, 0.8f}, {-21.6f, RK_PLAIN, 3, 3, 0.f}, {-18.f, RK_PLAIN, 3, 3, 0.f},
        {-14.4f, RK_PLAIN, 3, 4, 0.25f}, {-10.f, RK_PLAIN, 3, 4, 0.5f}, {-5.5f, RK_PLAIN, 3, 4, 0.75f}, {-1.6f, RK_PLAIN, 4, 4, 0.f},
        {0.f, RK_LIDLO, 4, 4, 0.f}, {0.f, RK_EYELO, 4, 4, 0.f}, {0.f, RK_EYEHI, 4, 4, 0.f}, {0.f, RK_LIDUP, 4, 4, 0.f},
        {15.8f, RK_PLAIN, 4, 1, 0.25f}, {19.2f, RK_PLAIN, 4, 1, 0.5f}, {24.f, RK_PLAIN, 4, 1, 0.75f}, {30.f, RK_PLAIN, 1, 1, 0.f},
        {37.f, RK_PLAIN, 1, 0, 0.2f}, {45.f, RK_PLAIN, 1, 0, 0.4f}, {54.f, RK_PLAIN, 1, 0, 0.6f}, {63.f, RK_PLAIN, 1, 0, 0.8f},
        {72.f, RK_PLAIN, 0, 0, 0.f}, {81.f, RK_PLAIN, 0, 0, 0.f},
    };
    const int NRD = (int)(sizeof(rows) / sizeof(rows[0]));
    const int NR = NRD + 1;   // + row 0 (neck ring)
    H.rows = NR;
    H.rowChin = 3;
    H.rowMouthLo = 9;
    H.rowMouthHi = 10;
    H.rowNoseBase = 14;
    H.rowEyeLo = 22;
    H.rowEyeHi = 23;
    H.rowBrow = 26;
    H.rowHairline = 30;
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
                bool lip = rd.kind <= RK_LIPUP2;
                int first = lip ? 5 : 19, last = lip ? 12 : 24;   // bracketing plain rows (index into rows[])
                int ord = r - first;                                 // 1..(last-first-1)
                float plain = Lerp(rows[first].phi, rows[last].phi, (float)ord / (last - first)) * deg;
                float feat = phF;
                switch (rd.kind) {
                    case RK_EYELO: feat = phC - hLo; break;
                    case RK_EYEHI: feat = phC + hHi; break;
                    case RK_LIDLO: feat = phC - hLo - (2.3f + 0.6f * inSpan) * deg; break;
                    case RK_LIDUP: feat = phC + hHi + (2.1f + 0.9f * inSpan) * deg; break;
                    case RK_MOUTHLO: feat = phM - gap; break;
                    case RK_MOUTHHI: feat = phM + gap; break;
                    case RK_LIPLO1: feat = phM - gap - 3.1f * deg * pinch; break;
                    case RK_LIPLO2: feat = phM - gap - 6.0f * deg * pinch; break;
                    case RK_LIPUP1: feat = phM + gap + 2.4f * deg * pinch; break;
                    case RK_LIPUP2: feat = phM + gap + 5.0f * deg * pinch; break;
                    default: break;
                }
                phF = Lerp(plain, feat, lip ? wLip : wEye);
            }
            // under-chin rows start from the neck ring
            float ph0 = row0Phi[(size_t)(k * (NC - 1) / Max(NC - 1, 1)) % NC];
            ph0 = row0Phi[k];
            if (j <= 2) {
                float target = rows[2].phi * deg;
                phF = Lerp(ph0, target, (float)j / 3.f);
            }
            // back layout: spread rows evenly from the neck ring up to the crown
            float phB = Lerp(ph0, backTop, powf(rowFrac, 0.92f));
            float w = sstep(118.f * deg, 58.f * deg, ath);
            float ph = Lerp(phB, phF, w);
            // keep rows above the neck ring
            ph = Max(ph, ph0 + 1.0f * deg * (float)j);
            vec3 dir(cosf(ph) * sinf(th), cosf(ph) * cosf(th), sinf(ph));
            float t = c.sdf.castOut(C, dir, HM, 0.22f * hs);
            vec3 p = C + dir * t;
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
            if (j < H.rowMouthLo) jawW = front * sstep(-0.004f, -0.012f, hp.z - Lm.stomion.z + 0.01f * (1.f - front));
            if (j <= H.rowMouthLo) jawW = Max(jawW, front * (j <= H.rowMouthLo ? 1.f : 0.f) * sstep(80.f * deg, 30.f * deg, ath));
            jawW *= sstep(-0.02f, 0.03f, hp.y);   // towards the ear the jaw influence fades
            if (j <= 2) jawW *= 0.6f;
            WAcc acc;
            acc.add(B_JAW, jawW);
            acc.add(B_HEAD, 1.f - jawW);
            v.sw = acc.finish();
            // ---- colors: lips, nostrils, cheeks, lids
            vec3 col = c.skin;
            float lipMask = 0.f;
            if ((rd.kind >= RK_LIPLO2 && rd.kind <= RK_LIPUP2)) {
                float u = ath / L.thMC;
                lipMask = 1.f - sstep(0.85f, 1.12f, u);
                if (rd.kind == RK_LIPLO2 || rd.kind == RK_LIPUP2) lipMask *= 0.55f;
            }
            if (lipMask > 0.f) {
                col = lerp(col, c.lipCol, lipMask);
                v.flags |= BuildCtx::F_LIP;
            }
            float lum = dot(c.skin, vec3(0.3f, 0.59f, 0.11f));
            {
                // nostrils: underside of the nose between columella and alae
                vec3 hq = hp;
                float nx = fabsf(hq.x);
                if (hq.z < Lm.ala[1].z + 0.002f && hq.z > Lm.subnasale.z - 0.001f && hq.y > Lm.subnasale.y + 0.0015f &&
                    hq.y < Lm.noseTip.y - 0.005f && nx > 0.003f && nx < fabsf(Lm.ala[1].x) - 0.002f)
                    col = col * 0.45f;
            }
            float cheek = bump(ath, 40.f * deg, 16.f * deg) * bump(ph, -8.f * deg, 12.f * deg) * sstep(0.04f, 0.25f, lum);
            col = lerp(col, mulColor(col, vec3(1.12f, 0.9f, 0.88f)), cheek * 0.45f);
            if (rd.kind >= RK_LIDLO && rd.kind <= RK_LIDUP) col = col * Lerp(1.f, 0.9f, sstep(0.5f, 1.f, 1.f - Saturate(fabsf(ath - H.thetaEye) / (22.f * deg))));
            if (j >= 12 && j <= 30) v.flags |= BuildCtx::F_FACE;
            if (ath < 110.f * deg && j <= 21 && j >= 1) v.flags |= BuildCtx::F_BEARD;
            if (j >= 24) v.flags |= BuildCtx::F_SCALP;
            v.col = col;
            H.grid[(size_t)j * NC + k] = m.add(v);
        }
    }
    // pole
    {
        vec3 dir(0, 0, 1);
        dir = normalize(vec3(0, -0.08f, 1.f));
        float t = c.sdf.castOut(C, dir, HM, 0.25f * hs);
        BVert v;
        v.p = C + dir * t;
        v.col = c.skin;
        v.mat = MAT_SKIN;
        v.part = PART_HEAD;
        v.sw = skin1(B_HEAD);
        v.pa = 0.f;
        v.pb = kHalfPi;
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
            m.quad(a0, b0, b1, a1);
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

static void addEyeball(BuildCtx& c, int sd, vec3 irisCol) {
    MeshB& m = c.m;
    const HeadInfo& H = c.head;
    vec3 ctr = H.eyeC[sd];
    float r = H.eyeR;
    const int NS = 16;
    const float polar[] = {0.f, 5.f, 9.5f, 13.f, 18.f, 23.f, 27.5f, 31.f, 38.f, 52.f, 70.f, 92.f, 115.f};
    const int NP = (int)(sizeof(polar) / sizeof(polar[0]));
    std::vector<u32> prev;
    u32 first = (u32)m.v.size();
    (void)first;
    // eyes look slightly outward-forward in bind
    vec3 fw = normalize(vec3((sd ? 1.f : -1.f) * 0.04f, 1.f, 0.f));
    vec3 ex, ez;
    ez = vec3(0, 0, 1);
    ex = normalize(cross(fw, ez));
    ez = cross(ex, fw);
    for (int pi = 0; pi < NP; pi++) {
        float a = polar[pi] * kDegToRad;
        int n = pi == 0 ? 1 : NS;
        std::vector<u32> ring(n);
        for (int k = 0; k < n; k++) {
            float ph = kTwoPi * k / NS;
            vec3 dir = fw * cosf(a) + (ex * cosf(ph) + ez * sinf(ph)) * sinf(a);
            float bulge = polar[pi] < 30.f ? 0.0011f * c.D->headS * cosf(polar[pi] / 30.f * kHalfPi) : 0.f;
            vec3 p = ctr + dir * (r + bulge);
            vec3 col;
            if (polar[pi] < 12.f) col = vec3(0.012f, 0.01f, 0.01f);
            else if (polar[pi] < 29.f) {
                float t = (polar[pi] - 12.f) / 17.f;
                col = lerp(irisCol * 0.75f, irisCol * 1.15f, t);
                if (polar[pi] > 26.f) col = irisCol * 0.45f;
            } else {
                col = vec3(0.78f, 0.74f, 0.7f);
                if (polar[pi] > 60.f) col = vec3(0.6f, 0.45f, 0.42f);
            }
            BVert v;
            v.p = p;
            v.n = dir;
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
    // ear frame: out (lateral, slightly forward), up (tilted back), back
    float tiltBack = 0.26f;
    vec3 out = normalize(vec3(sx, 0.28f, 0.f));
    vec3 up = normalize(vec3(0, -sinf(tiltBack), cosf(tiltBack)));
    vec3 back = normalize(cross(up, out) * -sx);
    if (dot(back, vec3(0, -1, 0)) < 0.f) back = -back;
    up = normalize(cross(out, back) * -sx);
    if (up.z < 0.f) up = -up;
    const int NE = 18;
    const float h = 0.062f * hs, w = 0.034f * hs;
    // outline in (b = back, u = up) relative to the root, per angle (0 = front)
    auto outline = [&](float a, float scale) {
        // center of the ear shell behind/above the root
        vec2 cen(0.46f * w, 0.1f * h);
        float cu = cosf(a), su = sinf(a);
        float rx = 0.5f * w * (1.f + 0.12f * su), ry = 0.5f * h;
        if (su < 0.f) rx *= 1.f + 0.18f * su;   // narrower lobe
        vec2 p(cen.x - cu * rx, cen.y + su * ry);
        return vec2(Lerp(cen.x, p.x, scale), Lerp(cen.y, p.y, scale));
    };
    struct ERing {
        float scale, outBase, outRim, backBias;
        float colMul;
    };
    const float eo = D.earOut;
    const ERing rings[] = {
        {0.62f, -0.002f, -0.002f, 0.f, 1.f},              // root (inside the head)
        {0.97f, 0.003f, 0.004f * eo, 0.f, 1.f},           // back side of the rim
        {1.0f, 0.007f, 0.0115f * eo, 0.f, 1.f},           // rim top (helix)
        {0.88f, 0.0075f, 0.0125f * eo, 0.f, 0.97f},       // helix inner edge
        {0.76f, 0.005f, 0.0075f * eo, 0.f, 0.9f},         // scapha groove
        {0.6f, 0.006f, 0.0095f * eo, 0.f, 0.97f},         // antihelix ridge
        {0.4f, 0.0025f, 0.003f * eo, 0.f, 0.8f},          // concha
        {0.12f, 0.0005f, 0.0005f, 0.f, 0.68f},            // canal
    };
    const int NRg = (int)(sizeof(rings) / sizeof(rings[0]));
    std::vector<u32> prev;
    vec3 earCol = lerp(c.skin, mulColor(c.skin, vec3(1.1f, 0.85f, 0.82f)), 0.35f);
    size_t i0 = m.idx.size();
    for (int r = 0; r < NRg; r++) {
        std::vector<u32> ring(NE);
        for (int k = 0; k < NE; k++) {
            float a = kTwoPi * k / NE;
            vec2 q = outline(a, rings[r].scale);
            // protrusion: the front edge (attached to the face) protrudes less; top/back most
            float frontness = sstep(0.2f, 1.f, cosf(a));
            float lobe = sstep(0.2f, 1.f, -sinf(a));
            float o = Lerp(rings[r].outBase, rings[r].outRim, 1.f - frontness * 0.85f) * (1.f - 0.35f * lobe);
            if (r == 0) o = -0.003f;
            vec3 p = root + back * (q.x) + up * (q.y) + out * (o * D.headS);
            // make the lobe thicker
            BVert v;
            v.p = p;
            v.col = earCol * rings[r].colMul;
            v.mat = MAT_SKIN;
            v.part = PART_EAR;
            v.side = (u8)sd;
            v.sw = skin1(B_HEAD);
            v.uv = vec2(q.x * 3.f, q.y * 3.f);
            v.t = up;
            ring[k] = m.add(v);
        }
        if (r > 0)
            for (int k = 0; k < NE; k++) {
                u32 a0 = prev[k], a1 = prev[(k + 1) % NE], b0 = ring[k], b1 = ring[(k + 1) % NE];
                // outward: for rings 0->2 the surface faces backwards/inwards (behind the ear), later rings face out
                vec3 cen = (m.v[a0].p + m.v[a1].p + m.v[b0].p + m.v[b1].p) * 0.25f;
                vec3 f = r <= 2 ? normalize(cen - root) * 0.5f + (-out) * 0.2f : out;
                if (r == 2) f = normalize(cen - (root + out * 0.004f * hs));
                triFacing(m, a0, b0, b1, f);
                triFacing(m, a0, b1, a1, f);
            }
        prev = ring;
    }
    // cap the canal
    u32 ctr;
    {
        BVert v = m.v[prev[0]];
        vec2 q = outline(0.f, 0.f);
        v.p = root + back * q.x + up * q.y + out * (-0.002f * D.headS);
        v.col = earCol * 0.5f;
        ctr = m.add(v);
    }
    for (int k = 0; k < NE; k++) triFacing(m, prev[k], ctr, prev[(k + 1) % NE], out);
    m.computeNormals(i0, m.idx.size());
}

static void addBrow(BuildCtx& c, int sd, vec3 col) {
    const BodyDims& D = *c.D;
    MeshB& m = c.m;
    const HeadInfo& H = c.head;
    const float sx = sd ? 1.f : -1.f;
    const int NU = 11, NV = 3;
    float thick = Lerp(1.f, 0.72f, D.fem) * (0.85f + 0.3f * (float)(c.d->seed % 7u) / 6.f);
    float thIn = H.thetaEye - 14.5f * kDegToRad, thOut = H.thetaEye + 19.5f * kDegToRad;
    float phBase = 13.8f * kDegToRad + 1.2f * kDegToRad * (D.browH - 1.f) * 5.f;
    std::vector<u32> grid(NU * NV);
    size_t i0 = m.idx.size();
    for (int i = 0; i < NU; i++) {
        float u = (float)i / (NU - 1);
        float th = Lerp(thIn, thOut, u);
        float arch = (Lerp(1.2f, 2.6f, D.fem) * sinf(kPi * powf(u, 0.8f)) - 0.8f * u) * kDegToRad;
        float hgt = Lerp(3.1f, 1.1f, powf(u, 1.1f)) * kDegToRad * thick;
        for (int j = 0; j < NV; j++) {
            float v = (float)j / (NV - 1);
            float ph = phBase + arch + (v - 0.45f) * hgt;
            vec3 dir(cosf(ph) * sinf(th) * sx, cosf(ph) * cosf(th), sinf(ph));
            float t = c.sdf.castOut(H.C, dir, MK_HEAD, 0.2f * D.headS);
            float lift = (0.0004f + 0.0007f * sinf(kPi * v) * (1.f - 0.5f * u)) * D.headS;
            vec3 p = H.C + dir * (t + lift);
            BVert bv;
            bv.p = p;
            bv.n = dir;
            bv.t = normalize(vec3(sx * cosf(th), -sinf(th), 0.f));
            bv.uv = vec2(u * 0.06f, v * 0.01f);
            bv.col = col;
            bv.mat = MAT_HAIR;
            bv.part = PART_FACEDETAIL;
            bv.side = (u8)sd;
            bv.sw = skin1(B_HEAD);
            grid[i * NV + j] = m.add(bv);
        }
    }
    for (int i = 0; i + 1 < NU; i++)
        for (int j = 0; j + 1 < NV; j++) {
            u32 a = grid[i * NV + j], b = grid[(i + 1) * NV + j], cc = grid[(i + 1) * NV + j + 1], d = grid[i * NV + j + 1];
            vec3 out = normalize(m.v[a].p - H.C);
            triFacing(m, a, b, cc, out);
            triFacing(m, a, cc, d, out);
        }
    m.computeNormals(i0, m.idx.size());
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
    // lashes
    float len = Lerp(0.0028f, 0.0045f, D.fem) * D.headS;
    std::vector<u32> base, tip;
    for (size_t i = 0; i < up.size(); i++) {
        BVert v = m.v[up[i]];
        float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
        float u = Saturate((at - (H.thetaEye - 11.f * kDegToRad)) / (22.f * kDegToRad));
        float l = len * (0.35f + 0.65f * sinf(kPi * Saturate(u * 0.9f + 0.05f)));
        vec3 d = normalize(v.p - ec);
        vec3 dirL = normalize(d * 0.7f + vec3(0, 0.55f, 0.35f));
        BVert b = v;
        b.mat = MAT_HAIR;
        b.col = lashCol;
        b.part = PART_FACEDETAIL;
        b.p = v.p + d * 0.0002f;
        b.uv = vec2(0, 0);
        BVert t = b;
        t.p = v.p + dirL * l;
        t.uv = vec2(0.004f, 0);
        base.push_back(m.add(b));
        tip.push_back(m.add(t));
    }
    size_t i1 = m.idx.size();
    for (size_t i = 0; i + 1 < base.size(); i++) {
        vec3 f = vec3(0, 1, 0.2f);
        triFacing(m, base[i], base[i + 1], tip[i + 1], f);
        triFacing(m, base[i], tip[i + 1], tip[i], f);
    }
    m.computeNormals(i1, m.idx.size());
    for (size_t i = i1; i < m.idx.size(); i++) m.v[m.idx[i]].n = normalize(m.v[m.idx[i]].n + vec3(0, 0.3f, 0.8f));
}

// Teeth rows and a dark mouth cavity behind the lips.
static void addMouth(BuildCtx& c) {
    const BodyDims& D = *c.D;
    MeshB& m = c.m;
    FaceLm Lm;
    faceLandmarks(c, Lm);
    const float hs = D.headS;
    size_t i0 = m.idx.size();
    // cavity: half ellipsoid facing inwards
    {
        vec3 cen = headToModel(c, vec3(0, 0.072f, -0.019f));
        vec3 rr = vec3(0.021f, 0.024f, 0.013f) * hs;
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
    // teeth: upper and lower arch strips
    for (int row = 0; row < 2; row++) {
        const int NU = 9;
        std::vector<u32> top, bot;
        float z0 = row == 0 ? -0.0105f : -0.0285f, z1 = row == 0 ? -0.0198f : -0.0205f;
        for (int i = 0; i <= NU; i++) {
            float u = (float)i / NU * 2.f - 1.f;
            float x = u * 0.02f;
            float y = 0.0905f - 0.018f * u * u;
            for (int e = 0; e < 2; e++) {
                BVert v;
                v.p = headToModel(c, vec3(x, y, e == 0 ? z0 : z1));
                v.n = normalize(vec3(u * 0.8f, 1.f, 0.f));
                v.col = vec3(0.72f, 0.69f, 0.6f) * (1.f - 0.25f * fabsf(u));
                v.mat = MAT_EYE;
                v.part = PART_MOUTH;
                v.sw = skin1(row == 0 ? B_HEAD : B_JAW);
                (e == 0 ? top : bot).push_back(m.add(v));
            }
        }
        for (int i = 0; i < NU; i++) {
            vec3 f = m.v[top[i]].n;
            triFacing(m, top[i], top[i + 1], bot[i + 1], f);
            triFacing(m, top[i], bot[i + 1], bot[i], f);
        }
    }
    (void)i0;
    (void)Lm;
}

void buildFaceDetails(BuildCtx& c) {
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
    addEar(c, 0);
    addEar(c, 1);
    vec3 browCol = d.hairColor * 0.8f;
    if (d.age > 0.75f) browCol = lerp(browCol, vec3(0.45f, 0.43f, 0.4f), 0.5f);
    addBrow(c, 0, browCol);
    addBrow(c, 1, browCol);
    vec3 lash = d.hairColor * 0.35f + vec3(0.004f);
    addLidDetails(c, 0, lash);
    addLidDetails(c, 1, lash);
    addMouth(c);
    (void)D;
}

}  // namespace detail
}  // namespace Anim
