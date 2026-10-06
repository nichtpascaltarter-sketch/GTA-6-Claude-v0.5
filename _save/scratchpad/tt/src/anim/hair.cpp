// Hair and facial hair.
//
// Scalp hair is a shell over the head grid (garment engine with MAT_HAIR) whose coverage is bounded by a hairline
// curve in (theta, phi) head space (with temple recession for older men and an optional fringe) and whose thickness
// profile defines the style (buzz, short, afro, slicked, quiff...). The thickness tapers to zero at the hairline
// and the scalp under it is tinted with the hair color so the edge blends. Long styles add draped geometry: curtains
// (long straight, bob) and ponytails/braids are swept down the back and pushed out of the body's signed distance
// field so they rest on the neck, shoulders and upper back; their lower parts are skinned to the neck/chest so they
// stay on the back when the head turns. Facial hair uses the same shell over the lower face (mustache, goatee,
// short and full beards) or a vertex color tint (stubble).
#include "anim_internal.h"

namespace Anim {
namespace detail {

// Hairline elevation (radians) as a function of |theta| (0 = front) for this character.
struct HairParams {
    int style = 0;
    float recession = 0.f;    // temple recession (0..1)
    float crownBald = 0.f;    // male pattern crown/top thinning (0..1)
    bool fringe = false;
    bool coversEars = false;
    float volume = 1.f;
    vec3 col;
};

static HairParams hairParams(const BuildCtx& c) {
    const CharacterDesc& d = *c.d;
    HairParams h;
    h.style = Clamp(d.hairStyle, 0, HAIR_STYLE_COUNT - 1);
    Rng r(hash32(d.seed * 97u + 29u));
    bool male = d.gender == MALE;
    float rec = male ? Saturate(sstep(0.15f, 0.9f, d.age) * r.range(0.3f, 1.1f)) : 0.f;
    h.recession = rec;
    h.crownBald = male && d.age > 0.45f && r.chance(0.35f) ? sstep(0.45f, 0.9f, d.age) : 0.f;
    if (h.style == HAIR_BUZZ || h.style == HAIR_BRAIDS || h.style == HAIR_CURLY) h.crownBald *= 0.3f;
    h.fringe = (h.style == HAIR_BOB || h.style == HAIR_LONG) && r.chance(0.35f);
    h.coversEars = h.style == HAIR_LONG || h.style == HAIR_BOB || (h.style == HAIR_CURLY && !male);
    h.volume = r.range(0.9f, 1.12f);
    h.col = d.hairColor;
    return h;
}

static float hairlinePhi(const HairParams& h, float at) {
    // (|theta| deg, phi deg) control points: forehead, temples, sideburn, over the ear, nape
    static const float T[][2] = {{0, 44}, {20, 43}, {32, 40}, {45, 33}, {58, 22}, {66, 9}, {72, 2}, {78, 9}, {86, 19}, {96, 23},
                                 {106, 18}, {116, 4}, {128, -8}, {145, -16}, {180, -20}};
    const int n = (int)(sizeof(T) / sizeof(T[0]));
    float a = at * kRadToDeg;
    float ph = T[n - 1][1];
    for (int i = 0; i + 1 < n; i++)
        if (a <= T[i + 1][0]) {
            float t = (a - T[i][0]) / (T[i + 1][0] - T[i][0]);
            ph = Lerp(T[i][1], T[i + 1][1], t);
            break;
        }
    // temple recession (M shape) and a slightly higher front for older men
    ph += h.recession * (14.f * bump(a, 34.f, 14.f) + 5.f * bump(a, 0.f, 25.f));
    if (h.coversEars && a > 72.f && a < 120.f) ph = Min(ph, -6.f + 10.f * bump(a, 72.f, 6.f));
    if (h.fringe && a < 42.f) ph = Min(ph, 27.f + 9.f * Sq(a / 42.f));
    return ph * kDegToRad;
}

// Hair thickness over a head grid vertex (before hairline tapering).
static float styleThickness(const BuildCtx& c, const HairParams& h, const BVert& v) {
    const float hs = c.D->headS;
    float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
    float ph = v.pb;
    const float deg = kDegToRad;
    float top = sstep(15.f * deg, 70.f * deg, ph);
    float back = sstep(95.f * deg, 160.f * deg, at);
    float front = sstep(70.f * deg, 15.f * deg, at);
    float nape = sstep(12.f * deg, -18.f * deg, ph) * back;
    float T = 0.f;
    switch (h.style) {
        case HAIR_BALD: return 0.f;
        case HAIR_BUZZ: T = 0.0025f; break;
        case HAIR_SHORT: T = Lerp(0.0065f, 0.016f, top) * (1.f - 0.5f * nape) + 0.003f * front * top; break;
        case HAIR_QUIFF: T = Lerp(0.004f, 0.013f, top) + 0.02f * front * bump(ph, 50.f * deg, 16.f * deg); T *= 1.f - 0.4f * nape; break;
        case HAIR_SLICKED: T = Lerp(0.005f, 0.011f, top) * (1.f - 0.3f * nape); break;
        case HAIR_PONYTAIL: case HAIR_BUN: T = Lerp(0.0045f, 0.008f, top); break;
        case HAIR_BRAIDS: {
            // cornrows: ridges running front to back
            float ridge = fabsf(sinf(v.pa * 12.f));
            T = 0.0035f + 0.0045f * ridge;
            break;
        }
        case HAIR_LONG: T = Lerp(0.009f, 0.015f, top) + 0.004f * back; break;
        case HAIR_BOB: T = Lerp(0.011f, 0.018f, top) + 0.005f * back; break;
        case HAIR_CURLY: {
            // afro: outer surface approximates an ellipsoid around the skull
            vec3 A = c.head.origin + vec3(0, -0.014f, 0.085f) * hs;
            vec3 rad = vec3(0.112f, 0.125f, 0.112f) * (hs * h.volume * (c.d->gender == FEMALE ? 1.05f : 0.93f));
            vec3 d = normalize(v.bp - c.head.C);
            vec3 o = c.head.C - A;
            vec3 dd(d.x / rad.x, d.y / rad.y, d.z / rad.z), oo(o.x / rad.x, o.y / rad.y, o.z / rad.z);
            float a = dot(dd, dd), b = dot(oo, dd), cc = dot(oo, oo) - 1.f;
            float disc = b * b - a * cc;
            float tOut = disc > 0.f ? (-b + sqrtf(disc)) / a : 0.f;
            float dist = length(v.bp - c.head.C);
            T = Max(0.005f, tOut - dist);
            T *= 1.f - 0.55f * front * sstep(60.f * deg, 30.f * deg, ph);
            T = Min(T, 0.075f * hs);
            // curls
            vec3 q = v.bp * 90.f;
            T *= 0.9f + 0.2f * (0.5f + 0.5f * sinf(q.x) * sinf(q.y + 1.3f) * sinf(q.z + 2.1f));
            break;
        }
        default: T = 0.008f; break;
    }
    if (h.crownBald > 0.f) T *= 1.f - h.crownBald * sstep(50.f * deg, 72.f * deg, ph);
    return T * hs;
}

float hairVolumeAt(const BuildCtx& c, const BVert& v) {
    HairParams h = hairParams(c);
    if (h.style == HAIR_BALD) return 0.f;
    float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
    if (v.pb < hairlinePhi(h, at)) return 0.f;
    return styleThickness(c, h, v);
}

// Coverage of the hair region (meters, positive inside) with a slightly irregular hairline.
static float hairCoverage(const BuildCtx& c, const HairParams& h, const BVert& v) {
    if (v.part != PART_HEAD || v.pc < 1.2f) return -1.f;
    float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
    float cv = (v.pb - hairlinePhi(h, at)) * 0.1f * c.D->headS;
    u32 hsh = hash32((u32)(v.bp.x * 5000.f) * 73856093u ^ (u32)(v.bp.y * 5000.f) * 19349663u ^ (u32)(v.bp.z * 5000.f) * 83492791u);
    cv += (hashToFloat(hsh) - 0.5f) * 0.004f;
    if (h.crownBald > 0.7f) {
        // horseshoe pattern: top of the head bald
        cv = Min(cv, (62.f * kDegToRad - v.pb) * 0.1f * c.D->headS + 0.01f * (1.f - h.crownBald));
    }
    return cv;
}

// ------------------------------------------------------------------------------------------------
// Draped geometry (curtains, ponytails, braids)

static vec3 pushOutside(const BuildCtx& c, vec3 p, float minDist, u32 mask) {
    for (int it = 0; it < 4; it++) {
        float d = c.sdf.eval(p, mask);
        if (d >= minDist) break;
        vec3 g = c.sdf.grad(p, mask);
        float gl = length(g);
        if (gl < 1e-5f) break;
        p += g / gl * (minDist - d);
    }
    return p;
}

static SkinW drapeWeights(float t, float headKeep) {
    WAcc acc;
    float wh = 1.f - sstep(headKeep, headKeep + 0.45f, t);
    float wc = sstep(headKeep + 0.2f, 1.f, t) * 0.75f;
    acc.add(B_HEAD, wh);
    acc.add(B_CHEST, wc);
    acc.add(B_NECK, Max(0.f, 1.f - wh - wc));
    return acc.finish();
}

// A sheet of hair hanging from the head around the back/sides (theta range [thA, thB] through the back).
static void addCurtain(OutfitCtx& o, const HairParams& h, float thA, float length, float lift, float flare, float jag) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float hs = D.headS;
    const HeadInfo& H = c.head;
    const int K = 23, NR = 9;
    const u32 bodyMask = MK_HEAD | MK_NECK | MK_TORSO | MK_ARM_L | MK_ARM_R;
    Rng r(hash32(c.d->seed * 131u + 7u));
    std::vector<vec3> outer((size_t)K * NR);
    std::vector<vec3> nrm((size_t)K * NR);
    std::vector<float> tt((size_t)K * NR);
    for (int k = 0; k < K; k++) {
        float u = (float)k / (K - 1);
        float th = thA + (kTwoPi - 2.f * thA) * u;   // from +thA through the back (pi) to -thA
        float at = th > kPi ? kTwoPi - th : th;
        float ph0 = hairlinePhi(h, at) + 12.f * kDegToRad;
        ph0 = Max(ph0, 10.f * kDegToRad);
        vec3 dir(cosf(ph0) * sinf(th), cosf(ph0) * cosf(th), sinf(ph0));
        float t0 = c.sdf.castOut(H.C, dir, MK_HEAD, 0.25f * hs);
        BVert tmp;
        tmp.pa = th;
        tmp.pb = ph0;
        tmp.part = PART_HEAD;
        tmp.pc = 1.5f;
        tmp.bp = H.C + dir * t0;
        float T = styleThickness(c, h, tmp);
        vec3 p = H.C + dir * (t0 + T * 0.8f + lift);
        float len = length * (1.f + jag * (r.f() - 0.5f)) * (1.f + 0.15f * Sq(cosf((th - kPi) * 0.5f)));
        float seg = len / (NR - 1);
        vec3 outDir = normalize(vec3(sinf(th), cosf(th), 0.f));
        for (int i = 0; i < NR; i++) {
            if (i > 0) {
                vec3 prev = outer[(size_t)k * NR + i - 1];
                p = prev + vec3(0, 0, -seg) + outDir * (flare * seg);
                p = pushOutside(c, p, 0.006f + T * 0.5f, bodyMask);
            }
            outer[(size_t)k * NR + i] = p;
            tt[(size_t)k * NR + i] = (float)i / (NR - 1);
        }
    }
    // normals from the grid
    for (int k = 0; k < K; k++)
        for (int i = 0; i < NR; i++) {
            vec3 du = outer[(size_t)Min(k + 1, K - 1) * NR + i] - outer[(size_t)Max(k - 1, 0) * NR + i];
            vec3 dv = outer[(size_t)k * NR + Min(i + 1, NR - 1)] - outer[(size_t)k * NR + Max(i - 1, 0)];
            vec3 n = normalize(cross(dv, du));
            vec3 radial = outer[(size_t)k * NR + i] - vec3(H.C.x, H.C.y, outer[(size_t)k * NR + i].z);
            if (dot(n, radial) < 0.f) n = -n;
            nrm[(size_t)k * NR + i] = n;
        }
    MeshB m;
    float thick = 0.007f * hs * h.volume;
    std::vector<u32> gOut((size_t)K * NR), gIn((size_t)K * NR);
    for (int k = 0; k < K; k++)
        for (int i = 0; i < NR; i++) {
            size_t id = (size_t)k * NR + i;
            float taper = 1.f - 0.7f * Sq(tt[id]);
            BVert v;
            v.p = outer[id];
            v.bp = v.p;
            v.n = nrm[id];
            v.t = vec3(0, 0, -1);
            v.uv = vec2(tt[id] * length, (float)k * 0.02f);
            v.col = h.col * (0.9f + 0.2f * hashToFloat(hash32((u32)(k * 31 + i))));
            v.mat = MAT_HAIR;
            v.part = PART_HAIR;
            v.sw = drapeWeights(tt[id], 0.12f);
            gOut[id] = m.add(v);
            v.p = outer[id] - nrm[id] * thick * taper;
            v.n = -nrm[id];
            v.col = h.col * 0.6f;
            gIn[id] = m.add(v);
        }
    auto quadF = [&](u32 a, u32 b, u32 cc, u32 d, vec3 f) {
        vec3 nn = cross(m.v[b].p - m.v[a].p, m.v[d].p - m.v[a].p);
        if (dot(nn, f) >= 0.f) m.quad(a, b, cc, d);
        else m.quad(a, d, cc, b);
    };
    for (int k = 0; k + 1 < K; k++)
        for (int i = 0; i + 1 < NR; i++) {
            size_t a = (size_t)k * NR + i, b = (size_t)(k + 1) * NR + i, cc = (size_t)(k + 1) * NR + i + 1, d = (size_t)k * NR + i + 1;
            quadF(gOut[a], gOut[b], gOut[cc], gOut[d], nrm[a]);
            quadF(gIn[a], gIn[b], gIn[cc], gIn[d], -nrm[a]);
        }
    // bottom edge and side edges
    for (int k = 0; k + 1 < K; k++) {
        size_t a = (size_t)k * NR + NR - 1, b = (size_t)(k + 1) * NR + NR - 1;
        quadF(gOut[a], gOut[b], gIn[b], gIn[a], vec3(0, 0, -1));
    }
    for (int side = 0; side < 2; side++) {
        int k = side ? K - 1 : 0;
        vec3 f = normalize(outer[(size_t)k * NR] - outer[(size_t)(side ? K - 2 : 1) * NR]);
        for (int i = 0; i + 1 < NR; i++) {
            size_t a = (size_t)k * NR + i, d = (size_t)k * NR + i + 1;
            quadF(gOut[a], gOut[d], gIn[d], gIn[a], f);
        }
    }
    m.computeNormals(0, m.idx.size());
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

// Tube of hair swept from a head point down the back (ponytail / braid).
static void addHairTube(OutfitCtx& o, const HairParams& h, vec3 start, vec3 dir0, float length, float r0, float rMid, float rEnd, int seg,
                        bool tie) {
    BuildCtx& c = o.c;
    const u32 bodyMask = MK_HEAD | MK_NECK | MK_TORSO;
    const int NP = 10;
    std::vector<vec3> pts(NP);
    std::vector<float> rad(NP);
    std::vector<SkinW> sws(NP);
    vec3 p = start;
    vec3 dir = normalize(dir0);
    float step = length / (NP - 1);
    for (int i = 0; i < NP; i++) {
        float t = (float)i / (NP - 1);
        if (i > 0) {
            dir = normalize(lerp(dir, vec3(0, -0.15f, -1.f), 0.45f));
            p = p + dir * step;
            p = pushOutside(c, p, rad[i - 1] + 0.004f, bodyMask);
        }
        pts[i] = p;
        rad[i] = t < 0.35f ? Lerp(r0, rMid, t / 0.35f) : Lerp(rMid, rEnd, (t - 0.35f) / 0.65f);
        sws[i] = drapeWeights(t, 0.05f);
    }
    MeshB m;
    // addTube lives in clothing.cpp (same translation unit); rebuild a simple one here for hair material
    std::vector<u32> prev;
    for (int i = 0; i < NP; i++) {
        vec3 tng = i + 1 < NP ? normalize(pts[i + 1] - pts[i]) : normalize(pts[i] - pts[i - 1]);
        vec3 a = normalize(anyPerp(tng));
        vec3 b = cross(tng, a);
        std::vector<u32> ring(seg);
        for (int k = 0; k < seg; k++) {
            float th = kTwoPi * k / seg;
            vec3 d = a * cosf(th) + b * sinf(th);
            BVert v;
            v.p = pts[i] + d * rad[i];
            v.bp = v.p;
            v.n = d;
            v.t = tng;
            v.uv = vec2((float)i / (NP - 1) * length, th * rad[i]);
            v.col = h.col * (0.9f + 0.2f * hashToFloat(hash32((u32)(i * 17 + k))));
            v.mat = MAT_HAIR;
            v.part = PART_HAIR;
            v.sw = sws[i];
            ring[k] = m.add(v);
        }
        if (i > 0)
            for (int k = 0; k < seg; k++) {
                u32 a0 = prev[k], a1 = prev[(k + 1) % seg], b0 = ring[k], b1 = ring[(k + 1) % seg];
                vec3 nn = cross(m.v[b0].p - m.v[a0].p, m.v[a1].p - m.v[a0].p);
                if (dot(nn, m.v[a0].n) >= 0.f) m.quad(a0, b0, b1, a1);
                else m.quad(a0, a1, b1, b0);
            }
        prev = ring;
    }
    // tip cap
    BVert tip = m.v[prev[0]];
    tip.p = pts[NP - 1] + normalize(pts[NP - 1] - pts[NP - 2]) * rEnd;
    tip.n = normalize(pts[NP - 1] - pts[NP - 2]);
    u32 ti = m.add(tip);
    for (int k = 0; k < seg; k++) {
        u32 a0 = prev[k], a1 = prev[(k + 1) % seg];
        vec3 nn = cross(m.v[a1].p - m.v[a0].p, m.v[ti].p - m.v[a0].p);
        if (dot(nn, tip.n) >= 0.f) m.tri(a0, a1, ti);
        else m.tri(a0, ti, a1);
    }
    if (tie) {
        // hair tie: short dark band at the base
        vec3 tng = normalize(pts[1] - pts[0]);
        vec3 a = normalize(anyPerp(tng)), b = cross(tng, a);
        vec3 cc = lerp(pts[0], pts[1], 0.35f);
        float rr = Lerp(rad[0], rad[1], 0.35f) + 0.0025f;
        std::vector<u32> r0v(seg), r1v(seg);
        for (int k = 0; k < seg; k++) {
            float th = kTwoPi * k / seg;
            vec3 d = a * cosf(th) + b * sinf(th);
            BVert v;
            v.p = cc + d * rr - tng * 0.006f;
            v.bp = v.p;
            v.n = d;
            v.t = tng;
            v.col = vec3(0.03f);
            v.mat = MAT_CLOTH;
            v.part = PART_ACC;
            v.sw = sws[0];
            r0v[k] = m.add(v);
            v.p = cc + d * rr + tng * 0.006f;
            r1v[k] = m.add(v);
        }
        for (int k = 0; k < seg; k++) {
            u32 a0 = r0v[k], a1 = r0v[(k + 1) % seg], b0 = r1v[k], b1 = r1v[(k + 1) % seg];
            vec3 nn = cross(m.v[b0].p - m.v[a0].p, m.v[a1].p - m.v[a0].p);
            if (dot(nn, m.v[a0].n) >= 0.f) m.quad(a0, b0, b1, a1);
            else m.quad(a0, a1, b1, b0);
        }
    }
    m.computeNormals(0, 0);
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

static void addBun(OutfitCtx& o, const HairParams& h) {
    BuildCtx& c = o.c;
    const float hs = c.D->headS;
    const HeadInfo& H = c.head;
    float th = kPi, ph = 38.f * kDegToRad;
    vec3 dir(cosf(ph) * sinf(th), cosf(ph) * cosf(th), sinf(ph));
    float t0 = c.sdf.castOut(H.C, dir, MK_HEAD, 0.25f * hs);
    float R = 0.034f * hs * h.volume;
    vec3 cen = H.C + dir * (t0 + R * 0.75f);
    MeshB m;
    const int NU = 12, NV = 8;
    std::vector<u32> g((NU + 1) * (NV + 1));
    for (int i = 0; i <= NU; i++)
        for (int j = 0; j <= NV; j++) {
            float a = kTwoPi * i / NU, b = -kHalfPi + kPi * j / NV;
            vec3 d(cosf(a) * cosf(b), sinf(a) * cosf(b), sinf(b));
            vec3 q(d.x * R, d.y * R * 0.85f, d.z * R * 0.8f);
            q += d * (0.002f * sinf(a * 5.f + b * 3.f));
            BVert v;
            v.p = cen + q;
            v.bp = v.p;
            v.n = d;
            v.t = vec3(-sinf(a), cosf(a), 0);
            v.uv = vec2(a * R, b * R);
            v.col = h.col * (0.9f + 0.15f * sinf(a * 7.f));
            v.mat = MAT_HAIR;
            v.part = PART_HAIR;
            v.sw = skin1(B_HEAD);
            g[i * (NV + 1) + j] = m.add(v);
        }
    for (int i = 0; i < NU; i++)
        for (int j = 0; j < NV; j++) {
            u32 a = g[i * (NV + 1) + j], b = g[(i + 1) * (NV + 1) + j], cc = g[(i + 1) * (NV + 1) + j + 1], d = g[i * (NV + 1) + j + 1];
            vec3 nn = cross(m.v[b].p - m.v[a].p, m.v[d].p - m.v[a].p);
            if (dot(nn, m.v[a].n) >= 0.f) m.quad(a, b, cc, d);
            else m.quad(a, d, cc, b);
        }
    m.computeNormals(0, m.idx.size());
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

// ------------------------------------------------------------------------------------------------
// Facial hair

static void buildFacialHair(OutfitCtx& o) {
    BuildCtx& c = o.c;
    const CharacterDesc& d = *c.d;
    if (d.gender != MALE || d.facialHair < 0) return;
    const HeadInfo& H = c.head;
    const float hs = c.D->headS;
    const int kind = d.facialHair;
    const float deg = kDegToRad;
    const float thMC = H.thetaMouth;
    vec3 fcol = d.hairColor * 0.85f;
    // beard region in head grid terms
    auto rowOf = [](const BVert& v) { return (int)lrintf((v.pc - 1.2f) * 34.f); };
    auto region = [=](const BVert& v, bool mustache, bool chin, bool cheeks) -> float {
        if (v.part != PART_HEAD || v.pc < 1.2f) return -1.f;
        float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
        int j = rowOf(v);
        float best = -1.f;
        // mustache: above the upper lip border (rows 12..14) within the mouth span (+ margin)
        if (mustache) {
            float inRow = (j >= 12 && j <= 14) ? 1.f : -1.f;
            float m = Min((thMC + 5.f * deg - at) * 0.1f, 0.01f * inRow);
            if (j == 13 || j == 12) m = Min((thMC + 5.f * deg - at) * 0.1f, 0.01f);
            best = Max(best, m);
        }
        // chin: under the lower lip, rows 1..6, narrow
        if (chin) {
            float c1 = Min((22.f * deg - at) * 0.1f, j <= 6 && j >= 1 ? 0.01f : -1.f);
            // connect mustache to chin around the mouth corners
            float c2 = Min((thMC + 6.f * deg - at) * 0.1f, Min(((thMC + 6.f * deg) - fabsf(at - thMC - 2.f * deg) * 4.f) * 0.1f,
                                                                (j >= 6 && j <= 12) ? 0.01f : -1.f));
            best = Max(best, Max(c1, at > thMC * 0.85f ? c2 : -1.f));
        }
        // cheeks + jaw + under the chin: up to a line from the sideburn to the mouth corner
        if (cheeks) {
            float phTop = Lerp(-22.f, 6.f, sstep(thMC * kRadToDeg, 72.f, at * kRadToDeg)) * deg;
            float cov = Min((phTop - v.pb) * 0.1f, (80.f * deg - at) * 0.1f);
            // keep lips clear
            bool lip = j >= 7 && j <= 12 && at < thMC * 1.15f;
            if (lip) cov = -1.f;
            if (j < 1) cov = -1.f;
            best = Max(best, cov);
        }
        return best;
    };
    if (kind == FH_STUBBLE) {
        // tint only (no geometry)
        for (size_t i = 0; i < c.m.v.size(); i++) {
            BVert& v = c.m.v[i];
            float cv = region(v, true, true, true);
            if (cv > -0.004f) v.col = lerp(v.col, mulColor(v.col, vec3(0.55f)) + fcol * 0.25f, 0.55f * sstep(-0.004f, 0.006f, cv));
        }
        return;
    }
    GarmentDef g;
    g.parts = 1u << PART_HEAD;
    g.mat = MAT_HAIR;
    g.col = fcol;
    g.hem = false;
    g.hideMargin = 0.006f;
    g.thick = 0.001f;
    g.smooth = 1;
    bool must = true, chin = kind == FH_GOATEE || kind == FH_BEARD || kind == FH_SHORTBEARD, cheeks = kind == FH_BEARD || kind == FH_SHORTBEARD;
    g.cov = [=](const BVert& v) { return region(v, must, chin, cheeks); };
    float base = kind == FH_BEARD ? 0.009f : (kind == FH_SHORTBEARD ? 0.0035f : 0.0035f);
    g.extraFn = [=](const BVert& v) -> float {
        float cv = region(v, must, chin, cheeks);
        float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
        float chinBoost = kind == FH_BEARD ? 0.008f * sstep(35.f * deg, 5.f * deg, at) * sstep(-35.f * deg, -55.f * deg, v.pb) : 0.f;
        return (base + chinBoost) * hs * sstep(0.f, 0.008f, cv);
    };
    g.colFn = [=](const BVert& v, vec3 cc) { return cc * (0.85f + 0.3f * hashToFloat(hash32((u32)(v.bp.x * 9000.f) ^ (u32)(v.bp.z * 7000.f) * 2654435761u))); };
    // tint the skin under the beard edge
    for (size_t i = 0; i < c.surfaceIdxEnd; i++) {
        if (i >= c.m.v.size()) break;
        BVert& v = c.m.v[i];
        float cv = region(v, must, chin, cheeks);
        if (cv > -0.005f) v.col = lerp(v.col, fcol * 0.7f, 0.5f * sstep(-0.005f, 0.005f, cv));
    }
    emitGarment(o, g);
}

// ------------------------------------------------------------------------------------------------

void buildHairLayer(OutfitCtx& o) {
    BuildCtx& c = o.c;
    const CharacterDesc& d = *c.d;
    HairParams h = hairParams(c);
    const float hs = c.D->headS;
    buildFacialHair(o);
    if (h.style == HAIR_BALD) {
        // slight shadow of shaved hair on the sides for older men
        return;
    }
    // tint the scalp under the hair so thin edges read as hair
    for (size_t i = 0; i < c.m.v.size(); i++) {
        BVert& v = c.m.v[i];
        if (v.part != PART_HEAD || v.pc < 1.2f) continue;
        float cv = hairCoverage(c, h, v);
        if (cv > -0.01f) v.col = lerp(v.col, h.col * 0.75f, (h.style == HAIR_BUZZ ? 0.8f : 0.6f) * sstep(-0.01f, 0.006f, cv));
    }
    GarmentDef g;
    g.parts = 1u << PART_HEAD;
    g.mat = MAT_HAIR;
    g.col = h.col;
    g.thick = 0.0008f;
    g.hem = false;
    g.swapUV = true;
    g.hideMargin = 0.014f;
    g.smooth = h.style == HAIR_CURLY ? 4 : 2;
    const BuildCtx* cp = &c;
    HairParams hp = h;
    g.cov = [=](const BVert& v) { return hairCoverage(*cp, hp, v); };
    g.extraFn = [=](const BVert& v) -> float {
        float cv = hairCoverage(*cp, hp, v);
        float T = styleThickness(*cp, hp, v);
        return T * sstep(0.f, 0.012f, cv);
    };
    g.colFn = [=](const BVert& v, vec3 cc) {
        float n = hashToFloat(hash32((u32)(v.bp.x * 7000.f) * 2654435761u ^ (u32)(v.bp.y * 6000.f) ^ (u32)(v.bp.z * 5000.f) * 40503u));
        return cc * (0.82f + 0.3f * n);
    };
    emitGarment(o, g);
    // styles with draped parts
    const HeadInfo& H = c.head;
    Rng r(hash32(d.seed * 157u + 3u));
    if (h.style == HAIR_LONG) addCurtain(o, h, 62.f * kDegToRad, r.range(0.2f, 0.34f) * hs / 1.0f, 0.001f, 0.05f, 0.25f);
    if (h.style == HAIR_BOB) addCurtain(o, h, 48.f * kDegToRad, 0.075f * hs, 0.002f, 0.12f, 0.1f);
    if (h.style == HAIR_PONYTAIL) {
        float ph = 22.f * kDegToRad;
        vec3 dir(0, -cosf(ph), sinf(ph));
        float t0 = c.sdf.castOut(H.C, dir, MK_HEAD, 0.25f * hs);
        vec3 start = H.C + dir * (t0 + 0.004f * hs);
        addHairTube(o, h, start, vec3(0, -1, -0.3f), r.range(0.2f, 0.3f), 0.018f * hs, 0.024f * hs, 0.006f * hs, 10, true);
    }
    if (h.style == HAIR_BUN) addBun(o, h);
    if (h.style == HAIR_BRAIDS && r.chance(0.6f)) {
        // box braids hanging from the lower back of the head
        for (int i = 0; i < 9; i++) {
            float th = kPi + (i - 4) * 0.2f;
            float ph = (4.f + 3.f * r.f()) * kDegToRad;
            vec3 dir(cosf(ph) * sinf(th), cosf(ph) * cosf(th), sinf(ph));
            float t0 = c.sdf.castOut(H.C, dir, MK_HEAD, 0.25f * hs);
            vec3 start = H.C + dir * (t0 + 0.004f * hs);
            addHairTube(o, h, start, vec3(sinf(th) * 0.3f, cosf(th) * 0.3f, -1.f), r.range(0.22f, 0.32f), 0.005f * hs, 0.0055f * hs, 0.004f * hs, 5,
                        false);
        }
    }
    // long hair covers the ears
    if (h.coversEars) {
        for (size_t t = c.surfaceIdxEnd; t + 2 < c.m.idx.size(); t += 3)
            if (c.m.v[c.m.idx[t]].part == PART_EAR) o.hideBody[t / 3] = 1;
    }
}

}  // namespace detail
}  // namespace Anim
