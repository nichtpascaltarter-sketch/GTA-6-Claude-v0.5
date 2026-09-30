// Wildlife models: small procedural skeletons, skinned meshes lofted from profile curves (bodies, necks, wings, fins,
// legs, tails, ears, antlers ...) with vertex-colour plumage / coat patterns, LODs and the batched LOD meshes that
// pack many birds or fish into one draw, plus the procedural animation of every body plan: wing beats with lagging
// segments, glides and soaring, perching, walking gaits (walk / trot / canter / gallop with three-segment leg IK),
// sitting and lying, tail wags and swishes, reptile sprawl and swimming undulation, dolphin and fish tail beats.
// Engine independent (math + mesh data only): the game uploads the meshes (wildlife.cpp).
#include "wildlife.h"
#include "../core/noise.h"

namespace Fauna {
namespace fauna_detail {

// ------------------------------------------------------------------------------------------------------------------
// Small math helpers
FORCEINLINE float spow(float x, float e) { return x < 0.f ? -powf(-x, e) : powf(x, e); }
FORCEINLINE float smooth01(float x) {
    x = Saturate(x);
    return x * x * (3.f - 2.f * x);
}
FORCEINLINE float sstep(float a, float b, float x) { return smooth01((x - a) / (b - a)); }
FORCEINLINE float gauss1(float x, float c, float w) {
    float d = (x - c) / w;
    return expf(-d * d);
}
FORCEINLINE quat qx(float a) { return quatAxisAngle(vec3(1, 0, 0), a); }
FORCEINLINE quat qy(float a) { return quatAxisAngle(vec3(0, 1, 0), a); }
FORCEINLINE quat qz(float a) { return quatAxisAngle(vec3(0, 0, 1), a); }
// sRGB authoring colour -> linear vertex colour
FORCEINLINE vec3 C(float r, float g, float b) { return srgbToLinear(vec3(r, g, b)); }
FORCEINLINE vec3 mixc(vec3 a, vec3 b, float t) { return a + (b - a) * Saturate(t); }
FORCEINLINE float n3(vec3 p, u32 seed) { return perlin3(p.x, p.y, p.z, seed); }   // ~[-1,1]
float fbm3(vec3 p, u32 seed, int oct) {
    float s = 0.f, a = 0.5f, norm = 0.f;
    for (int i = 0; i < oct; i++) {
        s += a * perlin3(p.x, p.y, p.z, seed + (u32)i * 131u);
        norm += a;
        p = p * 2.03f + vec3(17.1f, 3.3f, 9.7f);
        a *= 0.5f;
    }
    return s / norm;
}

// ------------------------------------------------------------------------------------------------------------------
// Skin weights
struct SkinW {
    u8 b[4] = {0, 0, 0, 0};
    float w[4] = {1.f, 0.f, 0.f, 0.f};
};
SkinW skin1(int b) {
    SkinW s;
    s.b[0] = s.b[1] = s.b[2] = s.b[3] = (u8)b;
    return s;
}
SkinW skin2(int b0, int b1, float t) {   // t: 0 -> b0, 1 -> b1
    t = Saturate(t);
    SkinW s;
    s.b[0] = (u8)b0;
    s.b[1] = (u8)b1;
    s.b[2] = s.b[3] = (u8)b0;
    s.w[0] = 1.f - t;
    s.w[1] = t;
    s.w[2] = s.w[3] = 0.f;
    return s;
}

// Rigid skinning along a bone chain with smooth blends around the joints. at[k] = where bone k's segment starts
// (loft distance units); positions before at[0] belong to bone 0 as well.
struct Chain {
    int bone[10];
    float at[10];
    int n = 0;
    float blend = 0.02f;
    void add(int b, float a) {
        bone[n] = b;
        at[n] = a;
        n++;
    }
    SkinW eval(float s) const {
        if (n <= 0) return skin1(0);
        int k = 0;
        while (k + 1 < n && s >= at[k + 1]) k++;
        // nearest joint (k or k+1) within the blend radius
        if (k + 1 < n && at[k + 1] - s < blend) {
            float t = 0.5f + 0.5f * (s - at[k + 1]) / blend;   // 0 at at-blend .. 0.5 at the joint
            return skin2(bone[k], bone[k + 1], smooth01(t));
        }
        if (k > 0 && s - at[k] < blend) {
            float t = 0.5f + 0.5f * (s - at[k]) / blend;       // 0.5 at the joint .. 1 at at+blend
            return skin2(bone[k - 1], bone[k], smooth01(t));
        }
        return skin1(bone[k]);
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Build mesh
struct MVert {
    vec3 p, n, t;
    vec2 uv;
    vec3 col;
    u8 mat = MAT_HAIR;
    SkinW sw;
};

struct MBuild {
    std::vector<MVert> v;
    std::vector<u32> idx;
    u32 add(const MVert& x) {
        v.push_back(x);
        return (u32)v.size() - 1;
    }
    void tri(u32 a, u32 b, u32 c) {
        idx.push_back(a);
        idx.push_back(b);
        idx.push_back(c);
    }
    void quad(u32 a, u32 b, u32 c, u32 d) {   // CCW seen from outside
        tri(a, b, c);
        tri(a, c, d);
    }
    // Remap bone indices (LOD reduction): map[fullBone] = new bone
    void remapBones(const int* map) {
        for (MVert& x : v) {
            float acc[kMaxBones] = {};
            for (int i = 0; i < 4; i++) acc[map[x.sw.b[i]]] += x.sw.w[i];
            SkinW s;
            int k = 0;
            for (int b = 0; b < kMaxBones && k < 4; b++)
                if (acc[b] > 1e-4f) {
                    s.b[k] = (u8)b;
                    s.w[k] = acc[b];
                    k++;
                }
            for (int i = k; i < 4; i++) {
                s.b[i] = s.b[0];
                s.w[i] = 0.f;
            }
            float sum = s.w[0] + s.w[1] + s.w[2] + s.w[3];
            if (sum > 0.f)
                for (int i = 0; i < 4; i++) s.w[i] /= sum;
            x.sw = s;
        }
    }
    void emit(SkinnedMeshData& out, int boneOffset = 0) const {
        out.verts.reserve(out.verts.size() + v.size());
        u32 base = (u32)out.verts.size();
        for (const MVert& x : v) {
            vec3 n = length2(x.n) > 1e-12f ? normalize(x.n) : vec3(0, 0, 1);
            vec3 t = x.t - n * dot(x.t, n);
            if (length2(t) < 1e-10f) t = anyPerp(n);
            t = normalize(t);
            u8 bones[4], wts[4];
            int sum = 0, best = 0;
            for (int i = 0; i < 4; i++) {
                bones[i] = (u8)(x.sw.b[i] + boneOffset);
                int q = (int)lrintf(Saturate(x.sw.w[i]) * 255.f);
                wts[i] = (u8)q;
                sum += q;
                if (x.sw.w[i] > x.sw.w[best]) best = i;
            }
            wts[best] = (u8)Clamp((int)wts[best] + (255 - sum), 0, 255);
            vec3 c = x.col;
            out.addVertex(x.p, n, t, x.uv, packRGBA8(c.x, c.y, c.z, 1.f), makeMat(x.mat), bones, wts);
        }
        for (u32 i : idx) out.indices.push_back(i + base);
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Lofting: a tube through cross-sections. A section is an ellipse / superellipse in the plane (x, y) around its centre;
// the tube runs along cross(y, x) (forward). th = 0 is +x ("right"), pi/2 is +y ("up").
struct Sect {
    vec3 c, x, y;
    float w = 0.1f, hT = 0.1f, hB = 0.1f;   // half width, half height above / below the centre
    float ex = 2.f;                         // 2 ellipse, > 2 boxier, < 2 pinched (lens / diamond)
    float u = 0.f;                          // distance along the tube (filled by the frame builder)
};

// Per-vertex attributes requested from the part builder.
struct VAttr {
    vec3 col = vec3(0.5f);
    u8 mat = MAT_HAIR;
    SkinW sw;
    float disp = 0.f;   // extra radial displacement (m), e.g. scutes, ridges, muscles
};
typedef std::function<void(int ring, float u, float th, vec3 p, VAttr& a)> AttrFn;

inline vec3 sectPoint(const Sect& s, float th) {
    float c = cosf(th), sn = sinf(th);
    float e = 2.f / s.ex;
    float px = s.w * spow(c, e);
    float py = (sn >= 0.f ? s.hT : s.hB) * spow(sn, e);
    return s.c + s.x * px + s.y * py;
}

// Builds parallel-transport frames along a polyline of section centres (starting from an up hint) and fills u.
void frameSections(std::vector<Sect>& S, vec3 upHint) {
    int n = (int)S.size();
    if (n < 2) return;
    vec3 fPrev = normalize(S[1].c - S[0].c);
    vec3 x = cross(fPrev, upHint);
    if (length2(x) < 1e-8f) x = anyPerp(fPrev);
    x = normalize(x);
    vec3 y = cross(x, fPrev);
    float u = 0.f;
    for (int i = 0; i < n; i++) {
        vec3 f;
        if (i == 0) f = S[1].c - S[0].c;
        else if (i == n - 1) f = S[n - 1].c - S[n - 2].c;
        else f = S[i + 1].c - S[i - 1].c;
        if (length2(f) < 1e-12f) f = fPrev;
        f = normalize(f);
        quat r = quatFromTo(fPrev, f);
        x = normalize(rotate(r, x));
        y = normalize(cross(x, f));
        x = normalize(cross(f, y));
        fPrev = f;
        if (i > 0) u += length(S[i].c - S[i - 1].c);
        S[i].x = x;
        S[i].y = y;
        S[i].u = u;
    }
}

// Emits the tube. sides = vertices around; caps close open ends with a fan (skipped for pointed ends).
// Returns the index of the first vertex.
u32 loft(MBuild& b, const std::vector<Sect>& S, int sides, const AttrFn& fn, bool capStart = true, bool capEnd = true) {
    int nr = (int)S.size();
    u32 base = (u32)b.v.size();
    if (nr < 2 || sides < 3) return base;
    int stride = sides + 1;
    std::vector<vec3> P((size_t)nr * stride);
    std::vector<VAttr> A((size_t)nr * stride);
    float perim = 0.f;
    for (int i = 0; i < nr; i++) {
        for (int j = 0; j <= sides; j++) {
            float th = -kHalfPi + kTwoPi * (float)j / (float)sides;   // seam at the belly
            vec3 p = sectPoint(S[i], th);
            VAttr a;
            fn(i, S[i].u, th, p, a);
            if (a.disp != 0.f) {
                vec3 rd = p - S[i].c;
                float l = length(rd);
                if (l > 1e-6f) p += rd * (a.disp / l);
            }
            P[(size_t)i * stride + j] = p;
            A[(size_t)i * stride + j] = a;
        }
        perim = Max(perim, kPi * (S[i].w + 0.5f * (S[i].hT + S[i].hB)));
    }
    auto at = [&](int i, int j) -> const vec3& { return P[(size_t)Clamp(i, 0, nr - 1) * stride + ((j % sides) + sides) % sides]; };
    for (int i = 0; i < nr; i++) {
        vec3 f = (i + 1 < nr ? S[i + 1].c : S[i].c) - (i > 0 ? S[i - 1].c : S[i].c);
        f = normalize(f);
        for (int j = 0; j <= sides; j++) {
            vec3 dth = at(i, j + 1) - at(i, j - 1);
            vec3 du = at(i + 1, j) - at(i - 1, j);
            vec3 nrm = cross(du, dth);
            if (length2(dth) < 1e-12f) {
                // degenerate ring (pointed tip): use the neighbouring ring's normal direction blended with the tip axis
                int k = i == 0 ? 1 : i - 1;
                vec3 d2 = at(k, j + 1) - at(k, j - 1), du2 = at(k + 1, j) - at(k - 1, j);
                nrm = normalize(cross(du2, d2)) + (i == 0 ? -f : f) * 0.8f;
            }
            if (length2(nrm) < 1e-14f) nrm = i == 0 ? -f : f;
            MVert v;
            v.p = P[(size_t)i * stride + j];
            v.n = normalize(nrm);
            v.t = f;
            v.uv = vec2(S[i].u, perim * (float)j / (float)sides);
            const VAttr& a = A[(size_t)i * stride + j];
            v.col = a.col;
            v.mat = a.mat;
            v.sw = a.sw;
            b.add(v);
        }
    }
    for (int i = 0; i + 1 < nr; i++)
        for (int j = 0; j < sides; j++) {
            u32 a = base + (u32)(i * stride + j), d = base + (u32)((i + 1) * stride + j);
            b.quad(a, d, d + 1, a + 1);
        }
    auto cap = [&](int i, bool start) {
        const Sect& s = S[i];
        if (s.w < 1e-4f || (s.hT + s.hB) < 2e-4f) return;
        vec3 f = normalize(S[Min(i + 1, nr - 1)].c - S[Max(i - 1, 0)].c);
        vec3 c = s.c + s.y * (s.hT - s.hB) * 0.5f;
        MVert v = b.v[base + (u32)(i * stride)];
        v.p = c;
        v.n = start ? -f : f;
        v.uv = vec2(s.u, 0.f);
        VAttr a;
        fn(i, s.u, 0.f, c, a);
        v.col = a.col;
        v.mat = a.mat;
        v.sw = a.sw;
        u32 ci = b.add(v);
        for (int j = 0; j < sides; j++) {
            u32 p0 = base + (u32)(i * stride + j), p1 = p0 + 1;
            if (start) b.tri(ci, p0, p1);
            else b.tri(ci, p1, p0);
        }
    };
    if (capStart) cap(0, true);
    if (capEnd) cap(nr - 1, false);
    return base;
}

// Sections along a polyline with a per-sample profile. prof(t in 0..1, arc length, out w/hT/hB/ex)
typedef std::function<void(float t, Sect& s)> ProfFn;
std::vector<Sect> sectionsAlong(const std::vector<vec3>& pts, int count, vec3 upHint, const ProfFn& prof) {
    // arc-length parametrize the polyline, sample `count` sections
    std::vector<float> acc(pts.size(), 0.f);
    for (size_t i = 1; i < pts.size(); i++) acc[i] = acc[i - 1] + length(pts[i] - pts[i - 1]);
    float total = Max(acc.back(), 1e-5f);
    std::vector<Sect> S((size_t)count);
    for (int k = 0; k < count; k++) {
        float t = (float)k / (float)(count - 1);
        float d = t * total;
        size_t i = 0;
        while (i + 2 < pts.size() && acc[i + 1] < d) i++;
        float seg = Max(acc[i + 1] - acc[i], 1e-6f);
        S[k].c = lerp(pts[i], pts[i + 1], Saturate((d - acc[i]) / seg));
    }
    frameSections(S, upHint);
    for (int k = 0; k < count; k++) {
        float t = (float)k / (float)(count - 1);
        prof(t, S[k]);
    }
    return S;
}

// Smooth Catmull-Rom resampling of control points (for curved necks, tails, bills)
std::vector<vec3> smoothPath(const std::vector<vec3>& cp, int perSeg) {
    std::vector<vec3> out;
    int n = (int)cp.size();
    if (n < 2) return cp;
    for (int i = 0; i + 1 < n; i++) {
        vec3 p0 = cp[Max(i - 1, 0)], p1 = cp[i], p2 = cp[i + 1], p3 = cp[Min(i + 2, n - 1)];
        for (int k = 0; k < perSeg; k++) {
            float t = (float)k / (float)perSeg, t2 = t * t, t3 = t2 * t;
            vec3 p = (p1 * 2.f + (p2 - p0) * t + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * t3) * 0.5f;
            out.push_back(p);
        }
    }
    out.push_back(cp.back());
    return out;
}

// Ellipsoid / sphere as a loft along `axis` (eyes, noses, hooves, humps)
void ellipsoid(MBuild& b, vec3 c, vec3 axis, vec3 upHint, float len, float w, float h, int rings, int sides, const AttrFn& fn) {
    std::vector<Sect> S((size_t)rings);
    vec3 a = normalize(axis);
    for (int i = 0; i < rings; i++) {
        float t = (float)i / (float)(rings - 1);
        float ang = t * kPi;
        S[i].c = c + a * (-cosf(ang) * len);
        S[i].w = Max(sinf(ang), 0.f) * w;
        S[i].hT = S[i].hB = Max(sinf(ang), 0.f) * h;
    }
    frameSections(S, upHint);
    loft(b, S, sides, fn, false, false);
}

// ------------------------------------------------------------------------------------------------------------------
// Batched meshes: `count` copies of a reduced mesh, copy k skinned to bones [k*bonesPer, (k+1)*bonesPer).
void emitBatch(const MBuild& m, int bonesPer, int count, SkinnedMeshData& out) {
    out.verts.clear();
    out.indices.clear();
    out.bounds = AABB();
    for (int k = 0; k < count; k++) m.emit(out, k * bonesPer);
}

}  // namespace fauna_detail

using namespace fauna_detail;

// ------------------------------------------------------------------------------------------------------------------
void poseSkeleton(const Skel& sk, const Pose& pose, mat4* skin, Frames* frames) {
    Frames local;
    Frames& F = frames ? *frames : local;
    for (int b = 0; b < sk.n; b++) {
        int par = sk.parent[b];
        if (par < 0) {
            F.r[b] = normalize(pose.rootRot * pose.q[b]);
            F.p[b] = sk.bind[b] + pose.rootPos;
        } else {
            F.r[b] = normalize(F.r[par] * pose.q[b]);
            F.p[b] = F.p[par] + rotate(F.r[par], sk.bind[b] - sk.bind[par]);
        }
        if (skin) {
            mat3 R = mat3FromQuat(F.r[b]);
            vec3 s = pose.s[b];
            vec3 c0 = R.c[0] * s.x, c1 = R.c[1] * s.y, c2 = R.c[2] * s.z;
            vec3 bp = sk.bind[b];
            vec3 t = F.p[b] - (c0 * bp.x + c1 * bp.y + c2 * bp.z);
            skin[b] = mat4(vec4(c0, 0.f), vec4(c1, 0.f), vec4(c2, 0.f), vec4(t, 1.f));
        }
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Species table
static const SpeciesInfo kSpecies[SP_COUNT] = {
    // name              plan           var  length height span   hp     mass
    {"seagull",          PLAN_BIRD,     2,   0.56f, 0.30f, 1.36f, 8.f,   1.0f},
    {"brown pelican",    PLAN_BIRD,     1,   1.25f, 0.70f, 2.10f, 20.f,  3.5f},
    {"pigeon",           PLAN_BIRD,     3,   0.32f, 0.22f, 0.66f, 5.f,   0.35f},
    {"great blue heron", PLAN_BIRD,     1,   1.15f, 1.10f, 1.85f, 15.f,  2.4f},
    {"great egret",      PLAN_BIRD,     1,   0.98f, 0.95f, 1.45f, 12.f,  1.0f},
    {"roseate spoonbill",PLAN_BIRD,     1,   0.80f, 0.75f, 1.30f, 12.f,  1.5f},
    {"flamingo",         PLAN_BIRD,     1,   1.25f, 1.35f, 1.55f, 15.f,  2.5f},
    {"white ibis",       PLAN_BIRD,     1,   0.62f, 0.55f, 0.95f, 8.f,   0.9f},
    {"vulture",          PLAN_BIRD,     2,   0.70f, 0.60f, 1.75f, 18.f,  1.6f},
    {"parakeet",         PLAN_BIRD,     2,   0.29f, 0.20f, 0.48f, 4.f,   0.1f},
    {"alligator",        PLAN_REPTILE,  1,   3.40f, 0.45f, 0.f,   180.f, 250.f},
    {"iguana",           PLAN_REPTILE,  2,   1.35f, 0.20f, 0.f,   15.f,  4.f},
    {"bottlenose dolphin",PLAN_CETACEAN,1,   2.70f, 0.55f, 0.f,   120.f, 200.f},
    {"manatee",          PLAN_CETACEAN, 1,   3.00f, 0.80f, 0.f,   200.f, 450.f},
    {"sea turtle",       PLAN_TURTLE,   2,   1.00f, 0.35f, 0.f,   60.f,  110.f},
    {"fish",             PLAN_FISH,     4,   0.35f, 0.12f, 0.f,   2.f,   0.6f},
    {"dog",              PLAN_QUAD,     5,   0.95f, 0.58f, 0.f,   45.f,  28.f},
    {"cat",              PLAN_QUAD,     3,   0.48f, 0.26f, 0.f,   20.f,  4.5f},
    {"raccoon",          PLAN_QUAD,     1,   0.55f, 0.30f, 0.f,   25.f,  7.f},
    {"white-tailed deer",PLAN_QUAD,     2,   1.80f, 0.95f, 0.f,   70.f,  70.f},
    {"cow",              PLAN_QUAD,     3,   2.40f, 1.35f, 0.f,   160.f, 600.f},
    {"horse",            PLAN_QUAD,     3,   2.40f, 1.60f, 0.f,   150.f, 500.f},
};
const SpeciesInfo& speciesInfo(int sp) { return kSpecies[Clamp(sp, 0, (int)SP_COUNT - 1)]; }

// ==================================================================================================================
// Birds
namespace fauna_detail {

struct BirdSpec {
    float bodyLen, bodyW, bodyHT, bodyHB;
    float neckLen, neckR, neckRise, neckS;      // neck length / radius, angle above horizontal, S-curve (radians)
    float headLen, headR;
    float billLen, billW, billH, billDroop, billHook, billSpoon, billPitch;
    float jawDepth;                             // lower mandible depth (pelican pouch)
    float tailLen, tailW, tailShape, tailTilt;  // tailShape: 0 square, 1 rounded, 2 pointed
    float span, chordRoot, chordMid, chordTip, sweep;
    int fingers;                                // separated primaries (soaring birds)
    float fingerLen;
    float hum, fore;                            // humerus / forearm share of the half span (hand = rest)
    float tibia, tarsus, legR, toeLen, legX, legY;
    bool webbed, featherThigh;
    float eyeR;
    float stance;                               // body pitch when standing (radians, + = breast up)
};

const BirdSpec& birdSpec(int sp) {
    static const BirdSpec kGull = {0.30f, 0.068f, 0.064f, 0.074f, 0.075f, 0.03f, 0.9f, 0.f, 0.075f, 0.033f,
                                   0.052f, 0.0095f, 0.0145f, 0.f, 0.6f, 0.f, -0.08f, 0.006f,
                                   0.13f, 0.055f, 0.f, 0.f, 1.36f, 0.155f, 0.175f, 0.03f, 0.28f, 0, 0.f, 0.24f, 0.28f,
                                   0.075f, 0.06f, 0.0055f, 0.045f, 0.03f, -0.01f, true, false, 0.0055f, 0.05f};
    static const BirdSpec kPelican = {0.55f, 0.13f, 0.12f, 0.13f, 0.30f, 0.042f, 1.05f, 0.5f, 0.10f, 0.045f,
                                      0.30f, 0.028f, 0.018f, 0.f, 0.8f, 0.f, -0.2f, 0.05f,
                                      0.15f, 0.075f, 1.f, 0.f, 2.1f, 0.30f, 0.34f, 0.16f, 0.05f, 6, 0.17f, 0.30f, 0.34f,
                                      0.12f, 0.075f, 0.012f, 0.09f, 0.06f, 0.f, true, false, 0.008f, 0.15f};
    static const BirdSpec kPigeon = {0.18f, 0.052f, 0.052f, 0.062f, 0.045f, 0.022f, 0.95f, -0.1f, 0.038f, 0.02f,
                                     0.016f, 0.004f, 0.005f, 0.f, 0.1f, 0.f, 0.f, 0.003f,
                                     0.11f, 0.045f, 1.f, 0.05f, 0.66f, 0.10f, 0.105f, 0.03f, 0.2f, 0, 0.f, 0.22f, 0.28f,
                                     0.035f, 0.026f, 0.0045f, 0.03f, 0.02f, 0.f, false, false, 0.005f, 0.25f};
    static const BirdSpec kHeron = {0.40f, 0.085f, 0.09f, 0.10f, 0.48f, 0.022f, 1.3f, 0.7f, 0.08f, 0.026f,
                                    0.14f, 0.011f, 0.016f, 0.f, 0.f, 0.f, -0.14f, 0.006f,
                                    0.13f, 0.065f, 1.f, 0.f, 1.85f, 0.27f, 0.30f, 0.14f, 0.05f, 5, 0.12f, 0.30f, 0.34f,
                                    0.26f, 0.21f, 0.009f, 0.10f, 0.05f, -0.02f, false, true, 0.007f, 0.45f};
    static const BirdSpec kEgret = {0.33f, 0.065f, 0.07f, 0.08f, 0.46f, 0.016f, 1.3f, 0.85f, 0.065f, 0.02f,
                                    0.12f, 0.009f, 0.013f, 0.f, 0.f, 0.f, -0.09f, 0.005f,
                                    0.12f, 0.055f, 1.f, 0.f, 1.45f, 0.22f, 0.25f, 0.12f, 0.05f, 4, 0.09f, 0.30f, 0.34f,
                                    0.22f, 0.17f, 0.007f, 0.09f, 0.04f, -0.02f, false, true, 0.006f, 0.5f};
    static const BirdSpec kSpoonbill = {0.34f, 0.085f, 0.085f, 0.095f, 0.22f, 0.022f, 0.95f, 0.3f, 0.06f, 0.025f,
                                        0.17f, 0.012f, 0.008f, 0.f, 0.f, 1.f, -0.18f, 0.004f,
                                        0.10f, 0.055f, 1.f, 0.f, 1.30f, 0.22f, 0.25f, 0.10f, 0.05f, 3, 0.07f, 0.30f, 0.33f,
                                        0.15f, 0.12f, 0.008f, 0.08f, 0.045f, -0.01f, false, true, 0.006f, 0.3f};
    static const BirdSpec kFlamingo = {0.42f, 0.10f, 0.10f, 0.11f, 0.56f, 0.02f, 1.4f, 0.9f, 0.06f, 0.028f,
                                       0.11f, 0.014f, 0.022f, 1.f, 0.f, 0.f, -0.52f, 0.008f,
                                       0.10f, 0.06f, 1.f, 0.f, 1.55f, 0.22f, 0.25f, 0.08f, 0.1f, 0, 0.f, 0.30f, 0.33f,
                                       0.36f, 0.32f, 0.0085f, 0.07f, 0.05f, -0.02f, true, true, 0.006f, 0.25f};
    static const BirdSpec kIbis = {0.26f, 0.065f, 0.065f, 0.075f, 0.17f, 0.02f, 1.05f, 0.3f, 0.05f, 0.021f,
                                   0.15f, 0.007f, 0.009f, 0.7f, 0.f, 0.f, -0.26f, 0.004f,
                                   0.10f, 0.045f, 1.f, 0.f, 0.95f, 0.16f, 0.18f, 0.06f, 0.12f, 0, 0.f, 0.29f, 0.32f,
                                   0.11f, 0.09f, 0.006f, 0.06f, 0.035f, -0.01f, false, true, 0.005f, 0.25f};
    static const BirdSpec kVulture = {0.33f, 0.10f, 0.09f, 0.10f, 0.09f, 0.03f, 0.6f, 0.1f, 0.06f, 0.026f,
                                      0.035f, 0.010f, 0.016f, 0.f, 0.9f, 0.f, -0.26f, 0.006f,
                                      0.24f, 0.09f, 1.f, 0.f, 1.75f, 0.30f, 0.32f, 0.20f, 0.0f, 6, 0.16f, 0.28f, 0.33f,
                                      0.09f, 0.075f, 0.009f, 0.065f, 0.05f, 0.f, false, false, 0.0055f, 0.3f};
    static const BirdSpec kParakeet = {0.11f, 0.033f, 0.035f, 0.040f, 0.025f, 0.018f, 0.95f, 0.f, 0.035f, 0.019f,
                                       0.018f, 0.008f, 0.016f, 0.f, 1.2f, 0.f, -0.5f, 0.006f,
                                       0.14f, 0.018f, 2.f, 0.1f, 0.48f, 0.07f, 0.075f, 0.02f, 0.2f, 0, 0.f, 0.22f, 0.28f,
                                       0.025f, 0.014f, 0.004f, 0.02f, 0.015f, 0.f, false, false, 0.005f, 0.6f};
    switch (sp) {
        case SP_PELICAN: return kPelican;
        case SP_PIGEON: return kPigeon;
        case SP_HERON: return kHeron;
        case SP_EGRET: return kEgret;
        case SP_SPOONBILL: return kSpoonbill;
        case SP_FLAMINGO: return kFlamingo;
        case SP_IBIS: return kIbis;
        case SP_VULTURE: return kVulture;
        case SP_PARROT: return kParakeet;
        default: return kGull;
    }
}

enum BirdPart : int { BP_BODY = 0, BP_NECK, BP_HEAD, BP_BILL, BP_JAW, BP_WING, BP_FOLD, BP_TAIL, BP_LEG, BP_TOE, BP_EYE, BP_FINGER };

struct BirdPaintIn {
    int part;
    float s;        // 0..1 along the part (wing: span, fold: root->tip, bill: base->tip, leg: hip->foot)
    float chord;    // wings: 0 leading edge .. 1 trailing edge
    bool upper;     // wings / fold / tail: upper surface; body: back half
    float th;       // angle around the part (pi/2 = top)
    vec3 p;         // bind position
};

// Plumage and bare-part colours of every bird species / variant. Returns linear colour, sets the material.
vec3 birdPaint(int sp, int var, const BirdPaintIn& in, u8& mat) {
    mat = MAT_HAIR;
    const vec3 white = C(0.95f, 0.95f, 0.94f), black = C(0.07f, 0.07f, 0.08f);
    float streak = n3(in.p * 90.f, 77u) * 0.5f + 0.5f;           // fine feather-edge variation
    float mottle = fbm3(in.p * 18.f, 91u, 3) * 0.5f + 0.5f;
    auto feather = [&](vec3 c) { return c * (0.92f + 0.14f * streak); };
    if (in.part == BP_EYE) {
        mat = MAT_EYE;
        // pupil faces sideways (the eye sphere's axis points out of the head): s = 0 at the outer pole
        vec3 iris;
        switch (sp) {
            case SP_GULL: iris = var == 0 ? C(0.92f, 0.86f, 0.55f) : C(0.12f, 0.08f, 0.06f); break;
            case SP_PIGEON: iris = C(0.95f, 0.45f, 0.1f); break;
            case SP_HERON: case SP_EGRET: case SP_FLAMINGO: iris = C(0.95f, 0.82f, 0.2f); break;
            case SP_SPOONBILL: iris = C(0.8f, 0.1f, 0.1f); break;
            case SP_IBIS: iris = C(0.55f, 0.75f, 0.9f); break;
            default: iris = C(0.25f, 0.15f, 0.08f); break;
        }
        if (in.s < 0.16f) return C(0.02f, 0.02f, 0.025f);
        if (in.s < 0.32f) return iris;
        return C(0.05f, 0.04f, 0.04f);
    }
    switch (sp) {
        case SP_GULL: {
            bool laughing = var == 1;
            vec3 mantle = laughing ? C(0.40f, 0.43f, 0.47f) : C(0.64f, 0.68f, 0.72f);
            vec3 bill = laughing ? C(0.42f, 0.07f, 0.07f) : C(0.96f, 0.80f, 0.24f);
            vec3 legs = laughing ? C(0.30f, 0.08f, 0.07f) : C(0.86f, 0.62f, 0.56f);
            switch (in.part) {
                case BP_BODY: return feather(sinf(in.th) > 0.55f && in.p.y > -0.12f ? mantle : white);
                case BP_NECK: return feather(white);
                case BP_HEAD: return feather(laughing ? C(0.08f, 0.08f, 0.09f) : white);
                case BP_BILL: case BP_JAW:
                    mat = MAT_SKIN;
                    if (!laughing && in.part == BP_JAW && in.s > 0.7f && in.s < 0.88f) return C(0.85f, 0.18f, 0.08f);
                    return bill;
                case BP_WING: {
                    if (in.upper) {
                        if (in.s > 0.72f) {
                            bool mirror = !laughing && in.s > 0.9f && in.chord > 0.25f && in.chord < 0.6f;
                            return mirror ? white : black;
                        }
                        if (in.chord > 0.86f && in.s < 0.7f) return feather(white);   // white trailing edge
                        return feather(mantle);
                    }
                    return in.s > 0.84f ? C(0.22f, 0.22f, 0.24f) : feather(C(0.86f, 0.87f, 0.88f));
                }
                case BP_FOLD: return in.s > 0.78f ? (in.s > 0.94f && !laughing ? white : black) : feather(mantle);
                case BP_FINGER: return black;
                case BP_TAIL: return feather(white);
                case BP_LEG: case BP_TOE: mat = MAT_SKIN; return legs;
                default: return white;
            }
        }
        case SP_PELICAN: {
            vec3 body = C(0.40f, 0.38f, 0.34f), silver = C(0.66f, 0.64f, 0.60f), dark = C(0.13f, 0.11f, 0.10f);
            switch (in.part) {
                case BP_BODY: {
                    if (sinf(in.th) > 0.3f) return mixc(body, silver, 0.4f + 0.6f * streak);
                    return feather(C(0.24f, 0.20f, 0.17f));
                }
                case BP_NECK: return feather(sinf(in.th) > -0.2f ? C(0.42f, 0.24f, 0.13f) : C(0.93f, 0.91f, 0.84f));
                case BP_HEAD: return feather(C(0.96f, 0.93f, 0.80f));
                case BP_BILL: mat = MAT_SKIN; return in.s > 0.9f ? C(0.75f, 0.35f, 0.2f) : mixc(C(0.55f, 0.52f, 0.47f), C(0.70f, 0.62f, 0.50f), mottle);
                case BP_JAW: mat = MAT_SKIN; return in.th < -0.5f ? C(0.24f, 0.25f, 0.21f) : C(0.52f, 0.50f, 0.45f);
                case BP_WING:
                    if (in.upper && in.s < 0.62f && in.chord < 0.55f) return mixc(body, silver, 0.3f + 0.7f * streak);
                    return feather(dark);
                case BP_FOLD: return in.s < 0.55f ? mixc(body, silver, 0.3f + 0.7f * streak) : feather(dark);
                case BP_FINGER: return dark;
                case BP_TAIL: return feather(C(0.30f, 0.28f, 0.25f));
                case BP_LEG: case BP_TOE: mat = MAT_SKIN; return C(0.16f, 0.16f, 0.15f);
                default: return body;
            }
        }
        case SP_PIGEON: {
            vec3 body = var == 1 ? C(0.24f, 0.24f, 0.27f) : C(0.52f, 0.55f, 0.63f);
            vec3 head = var == 1 ? C(0.17f, 0.17f, 0.2f) : C(0.40f, 0.42f, 0.50f);
            vec3 wing = var == 1 ? C(0.30f, 0.30f, 0.33f) : C(0.64f, 0.67f, 0.74f);
            vec3 pied = C(0.93f, 0.92f, 0.9f), brown = C(0.45f, 0.30f, 0.20f);
            bool isPied = var == 2;
            float patch = fbm3(in.p * 9.f, 313u, 3);
            auto piedCol = [&](vec3 c) { return isPied ? (patch > 0.05f ? pied : brown) : c; };
            switch (in.part) {
                case BP_BODY: return feather(piedCol(sinf(in.th) < -0.2f ? body * 0.95f : body));
                case BP_NECK: {
                    if (isPied) return feather(pied);
                    float irid = 0.5f + 0.5f * sinf(in.th * 2.f + in.s * 6.f);
                    return feather(mixc(C(0.33f, 0.55f, 0.42f), C(0.52f, 0.38f, 0.60f), irid) * (var == 1 ? 0.6f : 1.f));
                }
                case BP_HEAD: return feather(isPied ? pied : head);
                case BP_BILL: case BP_JAW: mat = MAT_SKIN; return in.s < 0.25f && in.part == BP_BILL ? C(0.85f, 0.84f, 0.8f) : C(0.14f, 0.13f, 0.13f);
                case BP_WING: {
                    if (isPied) return feather(piedCol(pied));
                    bool bar = in.upper && ((in.s > 0.2f && in.s < 0.28f) || (in.s > 0.34f && in.s < 0.42f)) && in.chord > 0.35f && var == 0;
                    if (bar) return black;
                    if (var == 1 && in.upper && in.s < 0.5f) return feather(mixc(wing, black, mottle > 0.55f ? 0.8f : 0.1f));
                    if (in.s > 0.62f) return feather(C(0.26f, 0.27f, 0.31f));
                    return feather(in.upper ? wing : C(0.78f, 0.80f, 0.84f));
                }
                case BP_FOLD: {
                    if (isPied) return feather(piedCol(pied));
                    bool bar = var == 0 && ((in.s > 0.32f && in.s < 0.42f) || (in.s > 0.5f && in.s < 0.6f));
                    return bar ? black : feather(in.s > 0.75f ? C(0.26f, 0.27f, 0.31f) : wing);
                }
                case BP_TAIL: return in.s > 0.8f ? black : feather(piedCol(C(0.44f, 0.46f, 0.54f)));
                case BP_LEG: case BP_TOE: mat = MAT_SKIN; return C(0.85f, 0.32f, 0.32f);
                default: return body;
            }
        }
        case SP_HERON: {
            vec3 body = C(0.46f, 0.50f, 0.58f), neck = C(0.64f, 0.62f, 0.66f), flight = C(0.17f, 0.19f, 0.24f);
            switch (in.part) {
                case BP_BODY: return feather(sinf(in.th) < -0.3f && in.p.y > 0.05f ? C(0.22f, 0.22f, 0.26f) : body);
                case BP_NECK: return feather(sinf(in.th) < -0.55f ? mixc(C(0.93f, 0.92f, 0.9f), C(0.2f, 0.2f, 0.22f), streak > 0.7f ? 0.8f : 0.f) : neck);
                case BP_HEAD: {
                    bool stripe = sinf(in.th) > 0.1f && fabsf(cosf(in.th)) > 0.25f && in.s > 0.1f && in.s < 0.8f;
                    return stripe ? C(0.08f, 0.08f, 0.1f) : feather(white);
                }
                case BP_BILL: case BP_JAW: mat = MAT_SKIN; return mixc(C(0.88f, 0.72f, 0.28f), C(0.45f, 0.40f, 0.30f), in.part == BP_BILL && sinf(in.th) > 0.3f ? 0.6f : 0.f);
                case BP_WING: return feather(in.s > 0.5f || in.chord > 0.62f ? flight : (in.upper ? body : C(0.40f, 0.44f, 0.52f)));
                case BP_FOLD: return feather(in.s < 0.12f ? C(0.1f, 0.1f, 0.12f) : (in.s > 0.6f ? flight : body));
                case BP_FINGER: return flight;
                case BP_TAIL: return feather(C(0.40f, 0.44f, 0.52f));
                case BP_LEG:
                    if (in.s < 0.25f) return feather(C(0.55f, 0.32f, 0.2f));
                    mat = MAT_SKIN;
                    return C(0.42f, 0.40f, 0.30f);
                case BP_TOE: mat = MAT_SKIN; return C(0.35f, 0.33f, 0.25f);
                default: return body;
            }
        }
        case SP_EGRET: {
            switch (in.part) {
                case BP_BILL: case BP_JAW: mat = MAT_SKIN; return C(0.95f, 0.78f, 0.16f);
                case BP_LEG: if (in.s < 0.2f) return feather(white); mat = MAT_SKIN; return C(0.06f, 0.06f, 0.06f);
                case BP_TOE: mat = MAT_SKIN; return C(0.06f, 0.06f, 0.06f);
                default: return feather(C(0.97f, 0.97f, 0.96f));
            }
        }
        case SP_SPOONBILL: {
            vec3 pink = C(0.94f, 0.56f, 0.66f), carmine = C(0.82f, 0.18f, 0.35f);
            switch (in.part) {
                case BP_BODY: return feather(sinf(in.th) > 0.2f && in.p.y > 0.02f ? mixc(pink, C(0.97f, 0.95f, 0.95f), 0.6f) : pink);
                case BP_NECK: return feather(C(0.97f, 0.95f, 0.95f));
                case BP_HEAD: mat = MAT_SKIN; return C(0.55f, 0.60f, 0.48f);
                case BP_BILL: case BP_JAW: mat = MAT_SKIN; return mixc(C(0.62f, 0.64f, 0.58f), C(0.40f, 0.40f, 0.36f), mottle);
                case BP_WING: return feather(in.upper && in.s < 0.38f && in.chord < 0.45f ? carmine : C(0.96f, 0.62f, 0.72f));
                case BP_FOLD: return feather(in.s < 0.3f ? carmine : C(0.96f, 0.62f, 0.72f));
                case BP_FINGER: return C(0.9f, 0.55f, 0.65f);
                case BP_TAIL: return feather(C(0.96f, 0.60f, 0.40f));
                case BP_LEG: case BP_TOE: mat = MAT_SKIN; return C(0.75f, 0.25f, 0.30f);
                default: return pink;
            }
        }
        case SP_FLAMINGO: {
            vec3 coral = C(0.97f, 0.50f, 0.48f), cover = C(0.95f, 0.34f, 0.40f);
            switch (in.part) {
                case BP_BODY: return feather(mixc(coral, C(0.98f, 0.64f, 0.60f), mottle * 0.6f));
                case BP_NECK: case BP_HEAD: return feather(C(0.97f, 0.54f, 0.52f));
                case BP_BILL: case BP_JAW: mat = MAT_SKIN; return in.s > 0.55f ? C(0.05f, 0.05f, 0.05f) : C(0.95f, 0.76f, 0.74f);
                case BP_WING: return feather(in.s > 0.55f || in.chord > 0.72f ? black : (in.upper ? cover : C(0.97f, 0.4f, 0.45f)));
                case BP_FOLD: return feather(in.s > 0.72f ? black : cover);
                case BP_TAIL: return feather(coral);
                case BP_LEG: case BP_TOE: mat = MAT_SKIN; return C(0.95f, 0.55f, 0.60f);
                default: return coral;
            }
        }
        case SP_IBIS: {
            switch (in.part) {
                case BP_HEAD: if (in.s > 0.6f) { mat = MAT_SKIN; return C(0.92f, 0.45f, 0.4f); } return feather(white);
                case BP_BILL: case BP_JAW: mat = MAT_SKIN; return in.s > 0.8f ? C(0.5f, 0.2f, 0.18f) : C(0.92f, 0.36f, 0.30f);
                case BP_WING: return in.s > 0.85f ? black : feather(white);
                case BP_FOLD: return in.s > 0.88f ? black : feather(white);
                case BP_LEG: case BP_TOE: mat = MAT_SKIN; return C(0.92f, 0.36f, 0.32f);
                default: return feather(white);
            }
        }
        case SP_VULTURE: {
            bool blackV = var == 1;
            vec3 body = blackV ? C(0.06f, 0.06f, 0.065f) : C(0.13f, 0.10f, 0.09f);
            vec3 silver = C(0.55f, 0.55f, 0.57f);
            switch (in.part) {
                case BP_NECK: return in.s > 0.6f ? (mat = MAT_SKIN, blackV ? C(0.28f, 0.28f, 0.28f) : C(0.6f, 0.2f, 0.18f)) : feather(body);
                case BP_HEAD: mat = MAT_SKIN; return blackV ? C(0.30f, 0.30f, 0.30f) : C(0.74f, 0.24f, 0.21f);
                case BP_BILL: case BP_JAW: mat = MAT_SKIN; return blackV ? C(0.25f, 0.24f, 0.22f) : C(0.92f, 0.88f, 0.78f);
                case BP_WING:
                    if (blackV) return in.s > 0.72f ? feather(C(0.72f, 0.72f, 0.72f)) : feather(body);
                    if (!in.upper && in.chord > 0.42f) return feather(silver);
                    return feather(C(0.16f, 0.13f, 0.11f));
                case BP_FINGER: return blackV ? C(0.72f, 0.72f, 0.72f) : (in.upper ? C(0.16f, 0.13f, 0.11f) : silver);
                case BP_FOLD: return feather(body * 1.2f);
                case BP_LEG: case BP_TOE: mat = MAT_SKIN; return blackV ? C(0.55f, 0.55f, 0.55f) : C(0.66f, 0.55f, 0.52f);
                default: return feather(body);
            }
        }
        case SP_PARROT: {
            bool nanday = var == 1;
            vec3 green = C(0.36f, 0.72f, 0.30f), grey = C(0.72f, 0.74f, 0.74f), blue = C(0.20f, 0.36f, 0.78f);
            switch (in.part) {
                case BP_BODY: return feather(!nanday && sinf(in.th) < -0.3f && in.p.y > -0.01f ? grey : green);
                case BP_NECK: return feather(!nanday && sinf(in.th) < 0.f ? grey : green);
                case BP_HEAD: return feather(nanday ? C(0.07f, 0.07f, 0.08f) : (sinf(in.th) < 0.4f || in.s > 0.6f ? grey : green));
                case BP_BILL: case BP_JAW: mat = MAT_SKIN; return nanday ? C(0.08f, 0.08f, 0.08f) : C(0.96f, 0.76f, 0.56f);
                case BP_WING: return feather(in.s > 0.5f || in.chord > 0.6f ? blue : green);
                case BP_FOLD: return feather(in.s > 0.6f ? blue : green);
                case BP_TAIL: return feather(mixc(green, blue, in.s * 0.6f));
                case BP_LEG:
                    if (nanday && in.s < 0.3f) return feather(C(0.8f, 0.2f, 0.15f));
                    mat = MAT_SKIN;
                    return C(0.5f, 0.5f, 0.52f);
                case BP_TOE: mat = MAT_SKIN; return C(0.5f, 0.5f, 0.52f);
                default: return green;
            }
        }
        default: return white;
    }
}

struct BirdLod {
    int bodyRings, bodySides, neckRings, neckSides, wingStations, wingSides, tailRings, tailSides, legSides, legRings;
    bool eyes, toes, fingers, legs, jaw, fold;
};

void buildBirdMesh(int sp, int var, const Skel& sk, const BirdSpec& B, int lod, MBuild& mb, const vec3* J) {
    using namespace BirdBone;
    bool longLegs = B.tibia > 0.1f;
    BirdLod L;
    if (lod == 0) L = {16, 16, 26, 12, 14, 8, 6, 6, 6, 6, true, true, true, true, true, true};
    else if (lod == 1) L = {7, 7, 10, 5, 6, 4, 3, 4, 3, 3, false, false, false, longLegs, true, true};
    else L = {5, 4, 6, 3, 4, 3, 2, 3, 3, 2, false, false, false, longLegs, false, true};
    auto paint = [&](int part, float s, float chord, bool upper, float th, vec3 p, VAttr& a) {
        BirdPaintIn in;
        in.part = part;
        in.s = s;
        in.chord = chord;
        in.upper = upper;
        in.th = th;
        in.p = p;
        u8 mat;
        a.col = birdPaint(sp, var, in, mat);
        a.mat = mat;
    };
    float yT = -B.bodyLen * 0.5f, yF = B.bodyLen * 0.5f;
    // ---- body
    {
        std::vector<vec3> path = {vec3(0, yT, B.bodyHT * 0.3f), vec3(0, 0, 0), vec3(0, yF, -B.bodyHB * 0.08f)};
        path = smoothPath(path, 6);
        std::vector<Sect> S = sectionsAlong(path, L.bodyRings, vec3(0, 0, 1), [&](float t, Sect& s) {
            float sh = powf(Max(sinf(kPi * (0.035f + 0.93f * t)), 0.f), 0.55f);
            sh *= Lerp(0.5f, 1.f, smooth01(t / 0.5f));
            s.w = B.bodyW * sh * (0.88f + 0.12f * sinf(kPi * t));
            s.hT = B.bodyHT * sh;
            s.hB = B.bodyHB * sh * (1.f + 0.12f * smooth01((t - 0.45f) / 0.5f));
            s.ex = 2.1f;
        });
        float len = S.back().u;
        loft(mb, S, L.bodySides, [&](int, float u, float th, vec3 p, VAttr& a) {
            float t = u / len;
            paint(BP_BODY, t, 0.f, sinf(th) > 0.f, th, p, a);
            a.sw = t < 0.18f ? skin2(BODY, TAIL, (0.18f - t) / 0.18f * 0.45f) : skin1(BODY);
        }, true, true);
    }
    // ---- neck, head and upper mandible as one tube; lower mandible separately
    vec3 hf = J[HEAD + 20];   // head forward (stashed by the skeleton builder)
    {
        vec3 n0 = vec3(0, yF - B.bodyLen * 0.2f, B.bodyHT * 0.25f);
        vec3 skullC = J[HEAD] + hf * (B.headLen * 0.3f) + vec3(0, 0, B.headR * 0.35f);
        vec3 billBase = J[HEAD] + hf * (B.headLen * 0.82f) + vec3(0, 0, B.headR * 0.12f);
        vec3 droop = vec3(0, 0, -B.billLen * 0.35f * B.billDroop);
        std::vector<vec3> cp = {n0, J[NECK1], J[NECK2], J[NECK3], J[HEAD], skullC, billBase};
        // bill control points (droop bends the far half down, hooks curl the tip)
        cp.push_back(billBase + hf * (B.billLen * 0.5f) + droop * 0.3f);
        cp.push_back(billBase + hf * (B.billLen * 0.92f) + droop + vec3(0, 0, -B.billH * 0.3f * B.billHook));
        cp.push_back(billBase + hf * B.billLen + droop * 1.1f + vec3(0, 0, -B.billH * 0.9f * B.billHook));
        std::vector<vec3> path = smoothPath(cp, lod == 0 ? 5 : 2);
        // arc positions of the landmarks
        auto arcTo = [&](vec3 q) {
            float best = 1e9f, acc = 0.f, at = 0.f;
            for (size_t i = 0; i < path.size(); i++) {
                if (i > 0) acc += length(path[i] - path[i - 1]);
                float d = length(path[i] - q);
                if (d < best) {
                    best = d;
                    at = acc;
                }
            }
            return at;
        };
        float aN1 = arcTo(J[NECK1]), aN2 = arcTo(J[NECK2]), aN3 = arcTo(J[NECK3]), aH = arcTo(J[HEAD]), aBB = arcTo(billBase);
        float total = 0.f;
        for (size_t i = 1; i < path.size(); i++) total += length(path[i] - path[i - 1]);
        int rings = Max(L.neckRings, 5);
        std::vector<Sect> S = sectionsAlong(path, rings, vec3(0, 0, 1), [&](float t, Sect& s) {
            float a = t * total;
            if (a < aH) {   // neck: thick where it enters the body
                float k = Saturate((a - aN1) / Max(aH - aN1, 1e-4f));
                float r = Lerp(B.neckR * 1.45f, B.neckR, smooth01(k * 1.6f));
                if (a < aN1) r = Lerp(B.bodyW * 0.75f, B.neckR * 1.45f, smooth01(a / Max(aN1, 1e-4f)));
                s.w = r;
                s.hT = r;
                s.hB = r * 1.08f;
                s.ex = 2.f;
            } else if (a < aBB) {   // head
                float k = (a - aH) / Max(aBB - aH, 1e-4f);
                float bulge = sinf(kPi * Min(k * 1.15f, 1.f));
                float r = Lerp(B.neckR, B.headR, smooth01(k * 2.5f)) * (0.82f + 0.18f * bulge);
                r = Lerp(r, Max(B.billW, B.billH * 0.7f) * 1.25f, smooth01((k - 0.7f) / 0.3f));
                s.w = r;
                s.hT = r * 1.05f;
                s.hB = Lerp(r * 0.95f, B.billH * 0.35f, smooth01((k - 0.6f) / 0.4f));
                s.ex = 2.f;
            } else {   // upper mandible
                float k = Saturate((a - aBB) / Max(total - aBB, 1e-4f));
                float taper = powf(1.f - k, 0.75f);
                s.w = B.billW * taper + B.billSpoon * 0.016f * gauss1(k, 0.86f, 0.12f) * (B.billW / 0.012f);
                s.hT = B.billH * 0.62f * powf(1.f - k, 0.9f) + 0.0008f;
                s.hB = B.billH * 0.14f * (1.f - k) + 0.0004f;
                s.ex = 2.2f;
                if (k > 0.985f) s.w = s.hT = s.hB = 0.f;
            }
        });
        loft(mb, S, L.neckSides, [&](int, float u, float th, vec3 p, VAttr& a) {
            int part = u < aH ? BP_NECK : (u < aBB ? BP_HEAD : BP_BILL);
            float s = part == BP_NECK ? u / Max(aH, 1e-4f) : (part == BP_HEAD ? (u - aH) / Max(aBB - aH, 1e-4f) : (u - aBB) / Max(total - aBB, 1e-4f));
            paint(part, s, 0.f, sinf(th) > 0.f, th, p, a);
            Chain ch;
            ch.blend = Max(0.25f * (aN2 - aN1), 0.01f);
            ch.add(BODY, 0.f);
            ch.add(NECK1, aN1);
            ch.add(NECK2, aN2);
            ch.add(NECK3, aN3);
            ch.add(HEAD, aH);
            a.sw = ch.eval(u);
        }, true, false);
        // lower mandible (JAW)
        if (L.jaw) {
            vec3 hinge = sk.bind[JAW];
            vec3 tip = billBase + hf * (B.billLen * 0.96f) + droop - vec3(0, 0, B.billH * 0.15f);
            std::vector<vec3> jp = smoothPath({hinge, billBase + hf * (B.billLen * 0.45f) + droop * 0.3f - vec3(0, 0, B.billH * 0.3f), tip}, lod == 0 ? 6 : 2);
            std::vector<Sect> JS = sectionsAlong(jp, lod == 0 ? 10 : 4, vec3(0, 0, 1), [&](float t, Sect& s) {
                float taper = powf(1.f - t, 0.8f);
                s.w = B.billW * 0.88f * taper + B.billSpoon * 0.014f * gauss1(t, 0.86f, 0.12f) * (B.billW / 0.012f) + 0.0004f;
                s.hT = B.billH * 0.12f * (1.f - t) + 0.0004f;
                s.hB = B.billH * 0.38f * powf(1.f - t, 0.9f) + B.jawDepth * powf(Max(sinf(kPi * Min(t * 1.1f, 1.f)), 0.f), 0.6f) + 0.0004f;
                s.ex = 2.f;
                if (t > 0.985f) s.w = s.hT = s.hB = 0.f;
            });
            float jl = JS.back().u;
            loft(mb, JS, Max(L.neckSides - 2, 3), [&](int, float u, float th, vec3 p, VAttr& a) {
                paint(BP_JAW, u / jl, 0.f, sinf(th) > 0.f, th, p, a);
                a.sw = u < jl * 0.12f ? skin2(HEAD, JAW, u / (jl * 0.12f)) : skin1(JAW);
            }, true, false);
        }
        // eyes
        if (L.eyes) {
            for (int sd = -1; sd <= 1; sd += 2) {
                vec3 ec = skullC + hf * (B.headR * 0.25f) + vec3(sd * B.headR * 0.74f, 0, B.headR * 0.12f);
                ellipsoid(mb, ec - vec3(sd * B.eyeR * 0.4f, 0, 0), vec3((float)sd, 0, 0), vec3(0, 0, 1), B.eyeR, B.eyeR, B.eyeR, 6, 8,
                          [&](int ring, float, float th, vec3 p, VAttr& a) {
                              paint(BP_EYE, 1.f - (float)ring / 5.f, 0.f, false, th, p, a);
                              a.sw = skin1(HEAD);
                          });
            }
        }
    }
    // ---- spread wings (bind pose = fully extended)
    for (int sd = -1; sd <= 1; sd += 2) {
        int b1 = sd < 0 ? WL1 : WR1, b2 = sd < 0 ? WL2 : WR2, b3 = sd < 0 ? WL3 : WR3;
        vec3 sh = sk.bind[b1], el = sk.bind[b2], wr = sk.bind[b3];
        vec3 tip = J[b3 + 20];
        bool fingers = B.fingers > 0;
        float mainEnd = fingers && L.fingers ? 0.86f : 1.f;
        int st = L.wingStations;
        // leading edge polyline sampled by span fraction
        auto lePoint = [&](float s) {
            float half = length(tip - sh);
            float d = s * half;
            float dse = length(el - sh), dew = length(wr - el);
            vec3 p;
            if (d < dse) p = lerp(sh, el, d / dse);
            else if (d < dse + dew) p = lerp(el, wr, (d - dse) / dew);
            else p = lerp(wr, tip, Saturate((d - dse - dew) / Max(half - dse - dew, 1e-4f)));
            return p;
        };
        float half = length(tip - sh);
        float sE = length(el - sh) / half, sW = (length(el - sh) + length(wr - el)) / half;
        std::vector<vec3> pts;
        std::vector<float> chords;
        for (int k = 0; k < st; k++) {
            float s = mainEnd * (float)k / (float)(st - 1);
            float c;
            if (s < sE) c = Lerp(B.chordRoot, B.chordMid, smooth01(s / sE));
            else if (s < sW) c = B.chordMid;
            else {
                float k2 = (s - sW) / Max(1.f - sW, 1e-4f);
                float tipC = fingers ? B.chordTip : B.chordTip * 0.2f;
                c = Lerp(B.chordMid, tipC, powf(k2, fingers ? 1.2f : 0.8f));
            }
            vec3 le = lePoint(s) + vec3(0, B.chordMid * 0.08f, B.bodyHT * 0.02f);
            // quarter-chord line: sweep the chord backwards (-Y)
            pts.push_back(le - vec3(0, c * 0.5f, 0));
            chords.push_back(c);
        }
        std::vector<Sect> S((size_t)st);
        for (int k = 0; k < st; k++) S[k].c = pts[k];
        frameSections(S, vec3(0, 0, 1));
        for (int k = 0; k < st; k++) {
            float s = mainEnd * (float)k / (float)(st - 1);
            S[k].w = chords[k] * 0.5f;
            float thick = chords[k] * Lerp(0.12f, 0.035f, smooth01(s / 0.7f));
            S[k].hT = thick * 0.6f + 0.0015f;
            S[k].hB = thick * 0.4f + 0.001f;
            S[k].ex = 1.5f;
        }
        loft(mb, S, L.wingSides, [&](int ring, float u, float th, vec3 p, VAttr& a) {
            float s = mainEnd * (float)ring / (float)(st - 1);
            // section x axis points forward for the left wing, backward for the right one
            float along = cosf(th) * (sd < 0 ? 1.f : -1.f);   // +1 leading edge .. -1 trailing edge
            float chord = 0.5f - 0.5f * along;
            paint(BP_WING, s, chord, sinf(th) > 0.f, th, p, a);
            float d = s * half;
            Chain ch;
            ch.blend = half * 0.05f;
            ch.add(BODY, -1.f);
            ch.add(b1, 0.f);
            ch.add(b2, sE * half);
            ch.add(b3, sW * half);
            a.sw = ch.eval(d);
            (void)u;
        }, true, true);
        // separated primaries of soaring birds
        if (fingers && L.fingers) {
            int nf = B.fingers;
            vec3 base = lePoint(mainEnd);
            float cEnd = chords.back();
            for (int f = 0; f < nf; f++) {
                float ff = (float)f / (float)Max(nf - 1, 1);
                vec3 root = base - vec3(0, cEnd * (0.1f + 0.75f * ff), 0);
                float ang = Lerp(0.18f, -0.55f, ff);   // fan: leading finger forward-out, trailing ones swept back
                vec3 dir = normalize(vec3((float)sd * cosf(ang), sinf(ang), 0.f));
                float len = B.fingerLen * (0.75f + 0.35f * sinf(kPi * Lerp(0.15f, 0.85f, ff)));
                std::vector<Sect> FS(4);
                for (int k = 0; k < 4; k++) FS[k].c = root + dir * (len * (float)k / 3.f) + vec3(0, 0, len * 0.06f * (float)(k * k) / 9.f);
                frameSections(FS, vec3(0, 0, 1));
                for (int k = 0; k < 4; k++) {
                    float t = (float)k / 3.f;
                    FS[k].w = B.fingerLen * 0.075f * (1.f - 0.55f * t) + 0.002f;
                    FS[k].hT = FS[k].hB = 0.0022f * (1.f - 0.6f * t);
                    FS[k].ex = 1.6f;
                    if (k == 3) FS[k].w *= 0.35f;
                }
                loft(mb, FS, 4, [&](int, float, float th, vec3 p, VAttr& a) {
                    paint(BP_FINGER, 1.f, 0.5f, sinf(th) > 0.f, th, p, a);
                    a.sw = skin1(b3);
                }, true, true);
            }
        }
    }
    // ---- folded wings along the flanks (visible when perched)
    if (L.fold) {
        for (int sd = -1; sd <= 1; sd += 2) {
            int fb = sd < 0 ? FOLDL : FOLDR;
            vec3 a0 = vec3(sd * B.bodyW * 0.78f, B.bodyLen * 0.22f, B.bodyHT * 0.28f);
            vec3 a1 = vec3(sd * B.bodyW * 0.95f, -B.bodyLen * 0.05f, B.bodyHT * 0.22f);
            float tipBack = B.tailShape > 1.5f ? 0.35f : 0.75f;
            vec3 a2 = vec3(sd * B.bodyW * 0.55f, yT - B.tailLen * tipBack, B.bodyHT * 0.34f);
            std::vector<vec3> fp = smoothPath({a0, a1, a2}, lod == 0 ? 5 : 2);
            int rings = lod == 0 ? 10 : (lod == 1 ? 4 : 3);
            std::vector<Sect> S = sectionsAlong(fp, rings, vec3((float)sd, 0, 0), [&](float t, Sect& s) {
                float h = B.bodyHT * (0.95f * powf(Max(sinf(kPi * Lerp(0.12f, 1.f, t)), 0.f), 0.7f) + 0.02f);
                if (t > 0.8f) h *= Lerp(1.f, 0.35f, (t - 0.8f) / 0.2f);
                s.w = h;
                s.hT = 0.01f * (B.bodyLen / 0.3f) + 0.002f;
                s.hB = 0.004f;
                s.ex = 1.8f;
            });
            float fl = S.back().u;
            loft(mb, S, lod == 0 ? 8 : 4, [&](int, float u, float th, vec3 p, VAttr& a) {
                paint(BP_FOLD, u / fl, 0.5f, true, th, p, a);
                a.sw = skin1(fb);
            }, true, true);
        }
    }
    // ---- tail
    {
        vec3 t0 = sk.bind[TAIL];
        vec3 dirT = normalize(vec3(0, -1, B.tailTilt));
        std::vector<vec3> tp = {t0 + dirT * (-B.tailLen * 0.05f), t0 + dirT * B.tailLen};
        std::vector<Sect> S = sectionsAlong(tp, L.tailRings + 1, vec3(0, 0, 1), [&](float t, Sect& s) {
            float w = Lerp(B.tailW * 0.45f, B.tailW, smooth01(t * 1.2f));
            if (B.tailShape > 1.5f) w = Lerp(B.tailW * 1.3f, B.tailW * 0.25f, t);
            else if (B.tailShape > 0.5f && t > 0.75f) w *= Lerp(1.f, 0.75f, (t - 0.75f) / 0.25f);
            s.w = w;
            s.hT = Lerp(B.bodyHT * 0.28f, 0.004f, smooth01(t * 2.f)) + 0.002f;
            s.hB = Lerp(B.bodyHB * 0.22f, 0.003f, smooth01(t * 2.f)) + 0.0015f;
            s.ex = 2.2f;
        });
        float tl = S.back().u;
        loft(mb, S, L.tailSides, [&](int, float u, float th, vec3 p, VAttr& a) {
            paint(BP_TAIL, u / tl, 0.f, sinf(th) > 0.f, th, p, a);
            a.sw = u < tl * 0.1f ? skin2(BODY, TAIL, 0.5f + 5.f * u / tl) : skin1(TAIL);
        }, true, true);
    }
    // ---- legs and feet
    if (L.legs) {
        for (int sd = -1; sd <= 1; sd += 2) {
            int l1 = sd < 0 ? LL1 : LR1, l2 = sd < 0 ? LL2 : LR2, l3 = sd < 0 ? LL3 : LR3;
            vec3 k = sk.bind[l1], an = sk.bind[l2], ft = sk.bind[l3];
            vec3 top = k + vec3(0, 0, B.bodyHB * 0.35f);
            std::vector<vec3> lp = {top, k, an, ft};
            int rings = L.legRings + (lod == 0 ? 6 : 0);
            std::vector<Sect> S = sectionsAlong(lp, rings, vec3(0, 1, 0), [&](float t, Sect& s) {
                float r = B.legR;
                float thighR = B.featherThigh ? B.legR * 2.6f : B.legR * 1.6f;
                s.w = s.hT = s.hB = Lerp(thighR, r, smooth01(t / 0.45f)) * (t > 0.9f ? 1.15f : 1.f);
                s.ex = 2.f;
            });
            float ll = S.back().u;
            float aK = length(k - top), aA = aK + length(an - k);
            loft(mb, S, L.legSides, [&](int, float u, float th, vec3 p, VAttr& a) {
                paint(BP_LEG, u / ll, 0.f, false, th, p, a);
                Chain ch;
                ch.blend = 0.01f;
                ch.add(BODY, 0.f);
                ch.add(l1, aK * 0.6f);
                ch.add(l2, aA);
                ch.add(l3, ll - 0.002f);
                a.sw = ch.eval(u);
            }, true, false);
            if (L.toes) {
                vec3 toeDirs[4] = {vec3(0.42f, 0.9f, 0), vec3(0, 1, 0), vec3(-0.42f, 0.9f, 0), vec3(0, -1, 0)};
                vec3 tips[4];
                for (int tI = 0; tI < 4; tI++) {
                    vec3 dir = normalize(toeDirs[tI]);
                    float len = tI == 3 ? B.toeLen * (B.featherThigh ? 0.55f : 0.35f) : B.toeLen * (tI == 1 ? 1.f : 0.8f);
                    vec3 base = ft + vec3(0, 0, -B.legR * 0.3f);
                    vec3 tipP = base + dir * len + vec3(0, 0, -B.legR * 0.8f);
                    tips[tI] = tipP;
                    std::vector<Sect> TS(3);
                    TS[0].c = base;
                    TS[1].c = lerp(base, tipP, 0.55f) + vec3(0, 0, B.legR * 0.3f);
                    TS[2].c = tipP;
                    frameSections(TS, vec3(0, 0, 1));
                    for (int q = 0; q < 3; q++) {
                        float rr = B.legR * (q == 2 ? 0.25f : 0.62f);
                        TS[q].w = rr;
                        TS[q].hT = TS[q].hB = rr * 0.8f;
                    }
                    loft(mb, TS, 4, [&](int, float, float th, vec3 p, VAttr& a) {
                        paint(BP_TOE, 1.f, 0.f, false, th, p, a);
                        a.sw = skin1(l3);
                    }, true, true);
                }
                if (B.webbed) {   // web between the front toes: a thin fan
                    vec3 base = ft + vec3(0, 0, -B.legR * 0.5f);
                    std::vector<Sect> WS(3);
                    for (int q = 0; q < 3; q++) {
                        float t = (float)q / 2.f;
                        WS[q].c = lerp(base, tips[1] - vec3(0, B.toeLen * 0.12f, 0), t * 0.9f);
                    }
                    frameSections(WS, vec3(0, 0, 1));
                    for (int q = 0; q < 3; q++) {
                        float t = (float)q / 2.f;
                        WS[q].w = length(tips[0].xy() - tips[2].xy()) * 0.5f * t * 0.85f + 0.001f;
                        WS[q].hT = WS[q].hB = 0.0012f;
                        WS[q].ex = 1.4f;
                    }
                    loft(mb, WS, 4, [&](int, float, float th, vec3 p, VAttr& a) {
                        paint(BP_TOE, 1.f, 0.f, false, th, p, a);
                        a.sw = skin1(l3);
                    }, true, true);
                }
            }
        }
    }
}

// Skeleton in the bind pose (wings spread, standing legs). J receives joint positions plus a few extra landmarks:
// J[HEAD + 20] = head forward direction, J[WL3 + 20] / J[WR3 + 20] = wing tips.
void buildBirdSkeleton(const BirdSpec& B, Skel& sk, vec3* J) {
    using namespace BirdBone;
    sk = Skel();
    float yF = B.bodyLen * 0.5f, yT = -B.bodyLen * 0.5f;
    sk.add(-1, vec3(0, 0, 0));   // BODY
    vec3 n1 = vec3(0, yF - B.bodyLen * 0.1f, B.bodyHT * 0.55f);
    auto dirA = [](float a) { return vec3(0, cosf(a), sinf(a)); };
    vec3 n2 = n1 + dirA(B.neckRise + B.neckS) * (B.neckLen / 3.f);
    vec3 n3 = n2 + dirA(B.neckRise - B.neckS * 0.8f) * (B.neckLen / 3.f);
    vec3 hd = n3 + dirA(B.neckRise + B.neckS * 0.6f) * (B.neckLen / 3.f);
    sk.add(BODY, n1);    // NECK1
    sk.add(NECK1, n2);   // NECK2
    sk.add(NECK2, n3);   // NECK3
    sk.add(NECK3, hd);   // HEAD
    vec3 hf = normalize(vec3(0, cosf(B.billPitch), sinf(B.billPitch)));
    vec3 billBase = hd + hf * (B.headLen * 0.82f) + vec3(0, 0, B.headR * 0.12f);
    sk.add(HEAD, billBase - hf * (B.headLen * 0.35f) + vec3(0, 0, -B.headR * 0.45f));   // JAW hinge
    sk.add(BODY, vec3(0, yT + B.bodyLen * 0.04f, B.bodyHT * 0.18f));                   // TAIL
    float halfSpan = B.span * 0.5f;
    for (int sd = -1; sd <= 1; sd += 2) {
        vec3 sh = vec3(sd * B.bodyW * 0.72f, yF - B.bodyLen * 0.3f, B.bodyHT * 0.5f);
        float hs = halfSpan - fabsf(sh.x);
        vec3 el = sh + vec3(sd * hs * B.hum, -hs * B.hum * 0.08f, 0.f);
        vec3 wr = el + vec3(sd * hs * B.fore, hs * B.fore * 0.06f, 0.f);
        float hand = 1.f - B.hum - B.fore;
        vec3 tip = wr + vec3(sd * hs * hand, -hs * hand * B.sweep, 0.f);
        int w1 = sk.add(BODY, sh);
        int w2 = sk.add(w1, el);
        int w3 = sk.add(w2, wr);
        J[w3 + 20] = tip;
    }
    for (int sd = -1; sd <= 1; sd += 2) {
        vec3 knee = vec3(sd * B.legX, B.legY, -B.bodyHB * 0.35f);
        vec3 ankle = knee + vec3(0, -B.tibia * 0.22f, -B.tibia * 0.97f);
        vec3 foot = ankle + vec3(0, B.tarsus * 0.1f, -B.tarsus);
        int l1 = sk.add(BODY, knee);
        int l2 = sk.add(l1, ankle);
        sk.add(l2, foot);
    }
    sk.add(BODY, vec3(-B.bodyW * 0.8f, B.bodyLen * 0.2f, B.bodyHT * 0.3f));   // FOLDL
    sk.add(BODY, vec3(B.bodyW * 0.8f, B.bodyLen * 0.2f, B.bodyHT * 0.3f));    // FOLDR
    for (int b = 0; b < sk.n; b++) J[b] = sk.bind[b];
    J[HEAD + 20] = hf;
}

void buildBird(int sp, int var, ModelData& out) {
    using namespace BirdBone;
    const BirdSpec& B = birdSpec(sp);
    vec3 J[64];
    buildBirdSkeleton(B, out.skel, J);
    MBuild m0, m1, m2;
    buildBirdMesh(sp, var, out.skel, B, 0, m0, J);
    buildBirdMesh(sp, var, out.skel, B, 1, m1, J);
    buildBirdMesh(sp, var, out.skel, B, 2, m2, J);
    m0.emit(out.lod[0]);
    // batched LODs: 8 bones per bird
    static const int kBatchMap[BirdBone::COUNT] = {0, 0, 1, 1, 1, 1, 0, 2, 2, 3, 4, 4, 5, 7, 7, 7, 7, 7, 7, 6, 6};
    const int bb[8] = {BODY, HEAD, WL1, WL3, WR1, WR3, FOLDL, LL1};
    out.batchN = 8;
    for (int i = 0; i < 8; i++) out.batchBones[i] = bb[i];
    out.batchCap = 256 / 8;
    int map[kMaxBones];
    for (int i = 0; i < kMaxBones; i++) map[i] = i < BirdBone::COUNT ? kBatchMap[i] : 0;
    m1.remapBones(map);
    m2.remapBones(map);
    emitBatch(m1, 8, out.batchCap, out.batch[0]);
    emitBatch(m2, 8, out.batchCap, out.batch[1]);
    out.legs = 2;
    out.legBone[0][0] = LL1; out.legBone[0][1] = LL2; out.legBone[0][2] = LL3;
    out.legBone[1][0] = LR1; out.legBone[1][1] = LR2; out.legBone[1][2] = LR3;
    out.legEnd[0] = out.legEnd[1] = vec3(0, 0, -B.legR);
    out.legLen = -out.skel.bind[LL3].z + B.legR;
    out.headTip = J[HEAD] + J[HEAD + 20] * (B.headLen * 0.82f + B.billLen);
    out.mouth = out.skel.bind[JAW];
}

}  // namespace fauna_detail

// ==================================================================================================================
// Quadrupeds (dogs, cats, raccoons, deer, cattle, horses)
namespace fauna_detail {

struct QuadSpec {
    float withers, hipH;            // shoulder / hip height (m)
    float bodyLen;                  // hip joint -> shoulder joint
    float bodyW, depth;             // barrel half width, barrel depth
    float chestDeep, tuck;          // extra chest depth (fraction), belly tuck-up at the loin (fraction)
    float rump, brisket;            // extension behind the hip / in front of the shoulder
    float neckLen, neckR, neckR2, neckAngle, neckFlat;   // neckFlat: w/h ratio (horses < 1)
    float headLen, headW, headH, muzzleLen, muzzleW, muzzleH, headPitch;
    float earLen, earW, earFlop, earOut;
    float tailLen, tailR, tailBush, tailAngle, tailTuft;
    float legX;
    // joint heights (fraction of withers / hip height) and forward offsets (fraction of the same height)
    float fElbowZ, fElbowY, fWristZ, fWristY, fToeY;
    float hStifleZ, hStifleY, hHockZ, hHockY, hToeY;
    float rThigh, rArm, rShin, rFoot;   // radii (m)
    bool hoof, plantigrade;
    float hump, dewlap, mane, horns, antlers;
};

QuadSpec quadSpec(int sp, int var) {
    QuadSpec q = {};
    switch (sp) {
        case SP_DOG: {
            q = {0.57f, 0.55f, 0.46f, 0.115f, 0.26f, 0.25f, 0.28f, 0.07f, 0.07f,
                 0.20f, 0.075f, 0.058f, 0.85f, 1.f,
                 0.14f, 0.062f, 0.075f, 0.10f, 0.038f, 0.045f, -0.25f,
                 0.10f, 0.055f, 1.f, 0.3f,
                 0.40f, 0.028f, 1.f, -0.35f, 0.f,
                 0.075f,
                 0.44f, -0.06f, 0.12f, 0.0f, 0.05f,
                 0.42f, 0.14f, 0.22f, -0.16f, -0.10f,
                 0.075f, 0.05f, 0.028f, 0.03f, false, false, 0.f, 0.f, 0.f, 0.f, 0.f};
            if (var == 2) {   // german shepherd: longer, erect ears, sloping back, bushy low tail
                q.withers = 0.62f; q.hipH = 0.55f; q.bodyLen = 0.52f; q.earFlop = 0.f; q.earLen = 0.12f; q.earOut = 0.25f;
                q.muzzleLen = 0.12f; q.tailBush = 1.6f; q.tailAngle = -0.9f; q.tailLen = 0.44f;
            } else if (var == 3) {   // small terrier
                q.withers = 0.30f; q.hipH = 0.30f; q.bodyLen = 0.26f; q.bodyW = 0.07f; q.depth = 0.15f; q.rump = 0.04f; q.brisket = 0.04f;
                q.neckLen = 0.1f; q.neckR = 0.045f; q.neckR2 = 0.035f; q.headLen = 0.09f; q.headW = 0.042f; q.headH = 0.05f;
                q.muzzleLen = 0.05f; q.muzzleW = 0.022f; q.muzzleH = 0.026f; q.earLen = 0.05f; q.earW = 0.035f; q.earFlop = 0.3f;
                q.tailLen = 0.14f; q.tailR = 0.014f; q.tailAngle = 0.9f; q.legX = 0.045f;
                q.rThigh = 0.045f; q.rArm = 0.03f; q.rShin = 0.016f; q.rFoot = 0.018f;
            } else if (var == 4) {   // stocky brindle stray
                q.withers = 0.50f; q.hipH = 0.49f; q.bodyLen = 0.42f; q.bodyW = 0.13f; q.depth = 0.25f; q.headW = 0.075f; q.headH = 0.08f;
                q.muzzleLen = 0.075f; q.muzzleW = 0.045f; q.earFlop = 0.45f; q.earLen = 0.07f; q.tailLen = 0.3f; q.tailR = 0.022f;
                q.tailBush = 0.6f; q.tailAngle = -0.2f; q.rThigh = 0.08f; q.rArm = 0.055f;
            }
            break;
        }
        case SP_CAT:
            q = {0.25f, 0.26f, 0.25f, 0.055f, 0.115f, 0.1f, 0.2f, 0.04f, 0.035f,
                 0.08f, 0.036f, 0.03f, 0.6f, 1.f,
                 0.07f, 0.046f, 0.046f, 0.025f, 0.022f, 0.02f, -0.1f,
                 0.045f, 0.03f, 0.f, 0.35f,
                 0.30f, 0.012f, 1.2f, 0.4f, 0.f,
                 0.035f,
                 0.42f, -0.05f, 0.1f, 0.0f, 0.04f,
                 0.45f, 0.15f, 0.22f, -0.2f, -0.12f,
                 0.036f, 0.022f, 0.012f, 0.015f, false, false, 0.f, 0.f, 0.f, 0.f, 0.f};
            break;
        case SP_RACCOON:
            q = {0.26f, 0.32f, 0.30f, 0.09f, 0.16f, 0.1f, 0.05f, 0.06f, 0.04f,
                 0.06f, 0.06f, 0.05f, 0.55f, 1.f,
                 0.09f, 0.06f, 0.055f, 0.05f, 0.018f, 0.02f, -0.3f,
                 0.04f, 0.035f, 0.f, 0.35f,
                 0.30f, 0.045f, 1.f, -0.5f, 0.f,
                 0.055f,
                 0.36f, -0.05f, 0.06f, 0.02f, 0.14f,
                 0.36f, 0.12f, 0.07f, -0.12f, 0.22f,
                 0.06f, 0.035f, 0.022f, 0.02f, false, true, 0.f, 0.f, 0.f, 0.f, 0.f};
            break;
        case SP_DEER:
            q = {0.95f, 0.98f, 0.78f, 0.15f, 0.38f, 0.2f, 0.18f, 0.12f, 0.1f,
                 0.45f, 0.11f, 0.065f, 0.95f, 0.9f,
                 0.20f, 0.08f, 0.09f, 0.12f, 0.042f, 0.05f, -0.55f,
                 0.17f, 0.07f, 0.f, 0.6f,
                 0.20f, 0.05f, 0.5f, 0.1f, 0.f,
                 0.085f,
                 0.52f, -0.05f, 0.28f, 0.0f, 0.03f,
                 0.56f, 0.1f, 0.34f, -0.1f, -0.04f,
                 0.12f, 0.06f, 0.02f, 0.018f, true, false, 0.f, 0.f, 0.f, 0.f, var == 1 ? 1.f : 0.f};
            break;
        case SP_COW:
            q = {1.35f, 1.38f, 1.30f, 0.33f, 0.74f, 0.12f, 0.02f, 0.16f, 0.2f,
                 0.50f, 0.28f, 0.2f, 0.35f, 0.9f,
                 0.40f, 0.14f, 0.19f, 0.2f, 0.11f, 0.12f, -0.75f,
                 0.20f, 0.08f, 0.f, 1.3f,
                 0.90f, 0.035f, 0.5f, -1.3f, 1.f,
                 0.17f,
                 0.45f, -0.03f, 0.22f, 0.0f, 0.04f,
                 0.52f, 0.1f, 0.27f, -0.1f, -0.03f,
                 0.24f, 0.13f, 0.055f, 0.06f, true, false, 0.f, 0.f, 0.f, 0.f, 0.f};
            if (var == 1) { q.hump = 1.f; q.dewlap = 1.f; q.earFlop = 1.f; q.earLen = 0.3f; q.horns = 0.4f; }
            if (var == 2) q.horns = 0.7f;
            break;
        case SP_HORSE:
            q = {1.60f, 1.58f, 1.25f, 0.28f, 0.66f, 0.1f, 0.05f, 0.16f, 0.18f,
                 0.80f, 0.23f, 0.14f, 0.95f, 0.72f,
                 0.34f, 0.105f, 0.20f, 0.24f, 0.075f, 0.1f, -1.05f,
                 0.15f, 0.055f, 0.f, 0.15f,
                 1.00f, 0.06f, 1.f, -1.15f, 0.f,
                 0.14f,
                 0.46f, -0.04f, 0.28f, 0.0f, 0.05f,
                 0.52f, 0.12f, 0.32f, -0.12f, -0.03f,
                 0.22f, 0.12f, 0.045f, 0.055f, true, false, 0.f, 0.f, 1.f, 0.f, 0.f};
            break;
        default: break;
    }
    return q;
}

enum QuadPart : int { QP_BODY = 0, QP_NECK, QP_HEAD, QP_JAW, QP_EAR, QP_EYE, QP_NOSE, QP_TAIL, QP_LEG, QP_FOOT, QP_MANE, QP_HORN, QP_ANTLER, QP_HUMP, QP_DEWLAP, QP_LEASH };

struct QuadPaintIn {
    int part;
    float s;          // along the part 0..1 (head: 0 poll .. 1 nose; leg: 0 top .. 1 foot)
    float th;         // around (pi/2 = top)
    int side;         // -1 left, +1 right
    bool front;       // legs: front pair
    vec3 p;           // bind position
};

vec3 quadPaint(int sp, int var, const QuadSpec& Q, const QuadPaintIn& in, u8& mat) {
    mat = MAT_HAIR;
    float up = sinf(in.th);                       // +1 top .. -1 bottom
    float grain = n3(in.p * 60.f, 11u) * 0.5f + 0.5f;
    float blot = fbm3(in.p * (3.f / Max(Q.withers, 0.2f)), 1234u + (u32)var * 77u, 4);
    auto fur = [&](vec3 c) { return c * (0.9f + 0.18f * grain); };
    if (in.part == QP_EYE) {
        mat = MAT_EYE;
        if (in.s < 0.2f) return C(0.02f, 0.015f, 0.01f);
        vec3 iris = sp == SP_CAT ? C(0.75f, 0.72f, 0.2f) : (sp == SP_DOG ? C(0.35f, 0.2f, 0.08f) : C(0.12f, 0.07f, 0.04f));
        return in.s < 0.45f ? iris : C(0.08f, 0.06f, 0.05f);
    }
    if (in.part == QP_NOSE) {
        mat = MAT_SKIN;
        if (sp == SP_COW && var == 2) return C(0.75f, 0.55f, 0.5f);
        if (sp == SP_HORSE) return var == 2 ? C(0.2f, 0.2f, 0.21f) : C(0.12f, 0.1f, 0.09f);
        if (sp == SP_CAT && var == 0) return C(0.85f, 0.5f, 0.48f);
        return C(0.05f, 0.045f, 0.045f);
    }
    if (in.part == QP_LEASH) { mat = MAT_CLOTH; return var == 1 ? C(0.8f, 0.12f, 0.1f) : C(0.12f, 0.25f, 0.7f); }
    if (in.part == QP_HORN) { mat = MAT_SKIN; return mixc(C(0.85f, 0.8f, 0.68f), C(0.25f, 0.22f, 0.2f), in.s); }
    if (in.part == QP_ANTLER) { mat = MAT_SKIN; return mixc(C(0.45f, 0.36f, 0.26f), C(0.85f, 0.8f, 0.7f), in.s * in.s); }
    if (in.part == QP_FOOT) {
        mat = MAT_SKIN;
        if (Q.hoof) return sp == SP_HORSE && var == 2 ? C(0.35f, 0.33f, 0.3f) : C(0.1f, 0.09f, 0.08f);
        if (sp == SP_RACCOON) return C(0.2f, 0.18f, 0.17f);
        mat = MAT_HAIR;   // paws are furry, colour below
    }
    bool isFootFur = in.part == QP_FOOT;
    switch (sp) {
        case SP_DOG: {
            if (var == 0 || var == 1) {   // labradors
                vec3 base = var == 0 ? C(0.86f, 0.70f, 0.44f) : C(0.045f, 0.04f, 0.036f);
                if (var == 0 && (up < -0.4f || in.part == QP_LEG)) base = C(0.9f, 0.78f, 0.56f);
                if (in.part == QP_EAR && var == 0) base = C(0.78f, 0.58f, 0.32f);
                if (in.part == QP_JAW && in.s > 0.1f) { mat = MAT_SKIN; return up > 0.3f ? C(0.62f, 0.3f, 0.32f) : fur(base); }
                return fur(base);
            }
            if (var == 2) {   // shepherd: tan with a black saddle and mask
                vec3 tan = C(0.74f, 0.50f, 0.26f), blk = C(0.05f, 0.045f, 0.04f);
                if (in.part == QP_BODY) return fur(up > 0.15f + 0.15f * blot && in.p.y < Q.bodyLen * 0.5f ? blk : tan);
                if (in.part == QP_TAIL) return fur(up > -0.2f || in.s > 0.6f ? blk : tan);
                if (in.part == QP_HEAD) return fur(in.s > 0.62f || (up > 0.6f && in.s < 0.4f) ? blk : tan);
                if (in.part == QP_JAW) return fur(blk);
                if (in.part == QP_EAR) return fur(blk);
                if (in.part == QP_NECK) return fur(up > 0.4f ? blk : tan);
                return fur(tan);
            }
            if (var == 3) {   // white terrier with a tan eye patch
                vec3 wht = C(0.94f, 0.93f, 0.9f), tanP = C(0.72f, 0.5f, 0.28f);
                if (in.part == QP_EAR && in.side < 0) return fur(tanP);
                if (in.part == QP_HEAD && in.side < 0 && in.s < 0.6f && up > -0.2f) return fur(tanP);
                if (in.part == QP_BODY && blot > 0.28f) return fur(tanP);
                return fur(wht);
            }
            // brindle stray
            vec3 base = C(0.42f, 0.30f, 0.19f), dark = C(0.12f, 0.09f, 0.07f);
            float stripe = sinf(in.p.y * 55.f + in.p.z * 20.f + blot * 6.f);
            vec3 c = mixc(base, dark, stripe > 0.35f ? 0.75f : 0.f);
            if ((in.part == QP_BODY || in.part == QP_NECK) && up < -0.5f && in.p.y > 0.f) c = C(0.9f, 0.88f, 0.84f);
            if (in.part == QP_LEG && in.s > 0.75f) c = C(0.9f, 0.88f, 0.84f);
            if (isFootFur) c = C(0.9f, 0.88f, 0.84f);
            return fur(c);
        }
        case SP_CAT: {
            if (var == 0) {   // orange tabby
                vec3 base = C(0.90f, 0.58f, 0.27f), dark = C(0.62f, 0.30f, 0.10f), wht = C(0.95f, 0.9f, 0.82f);
                if ((in.part == QP_BODY || in.part == QP_NECK || in.part == QP_JAW) && up < -0.55f) return fur(wht);
                float stripes = in.part == QP_TAIL ? sinf(in.s * 38.f) : sinf(in.p.y * 70.f + up * 3.f + blot * 3.f);
                return fur(mixc(base, dark, stripes > 0.4f ? 0.8f : 0.f));
            }
            vec3 blk = C(0.035f, 0.033f, 0.033f), wht = C(0.93f, 0.93f, 0.92f);
            if (var == 1) return fur(blk);
            bool white = ((in.part == QP_BODY || in.part == QP_NECK) && up < -0.35f && in.p.y > -0.02f) || in.part == QP_JAW ||
                         (in.part == QP_HEAD && in.s > 0.7f && up < 0.3f) || (in.part == QP_LEG && in.s > 0.72f) || isFootFur;
            return fur(white ? wht : blk);
        }
        case SP_RACCOON: {
            vec3 grey = C(0.48f, 0.45f, 0.41f), dark = C(0.08f, 0.075f, 0.07f), wht = C(0.9f, 0.88f, 0.84f);
            if (in.part == QP_TAIL) return fur(sinf(in.s * 30.f) > 0.1f || in.s > 0.92f ? dark : C(0.62f, 0.56f, 0.46f));
            if (in.part == QP_HEAD) {
                bool mask = in.s > 0.35f && in.s < 0.62f && up > -0.35f && up < 0.55f;
                if (mask) return fur(dark);
                if (in.s > 0.62f || (in.s > 0.25f && up > 0.55f)) return fur(wht);
                return fur(grey);
            }
            if (in.part == QP_EAR) return fur(in.s > 0.6f ? wht : dark);
            if (in.part == QP_LEG) return fur(mixc(grey, dark, in.s));
            return fur(mixc(grey, C(0.3f, 0.28f, 0.26f), blot * 0.8f + 0.2f));
        }
        case SP_DEER: {
            vec3 brown = C(0.66f, 0.41f, 0.22f), back = C(0.52f, 0.32f, 0.17f), wht = C(0.93f, 0.9f, 0.84f);
            if (in.part == QP_TAIL) return fur(up < 0.2f ? wht : back);
            if ((in.part == QP_BODY || in.part == QP_NECK) && up < -0.55f) return fur(wht);
            if (in.part == QP_JAW) return fur(wht);
            if (in.part == QP_HEAD) {
                if (in.s > 0.86f && up > -0.3f) return fur(C(0.2f, 0.15f, 0.1f));
                if (in.s > 0.72f) return fur(wht);
                return fur(mixc(brown, back, up * 0.5f + 0.5f));
            }
            if (in.part == QP_EAR) return fur(in.s > 0.8f ? C(0.2f, 0.15f, 0.1f) : brown);
            if (in.part == QP_LEG) return fur(mixc(brown, C(0.55f, 0.36f, 0.22f), in.s));
            return fur(mixc(brown, back, Saturate(up)));
        }
        case SP_COW: {
            if (var == 0) {   // Angus
                vec3 blk = C(0.035f, 0.032f, 0.03f);
                if (in.part == QP_TAIL && Q.tailTuft > 0.f && in.s > 0.85f) return fur(blk);
                return fur(blk);
            }
            if (var == 1) {   // Brahman
                vec3 light = C(0.76f, 0.75f, 0.72f), dark = C(0.42f, 0.41f, 0.40f);
                if (in.part == QP_HUMP) return fur(dark);
                if (in.part == QP_NECK) return fur(mixc(light, dark, 0.5f + 0.5f * up));
                if (in.part == QP_EAR) return fur(C(0.6f, 0.58f, 0.55f));
                if (in.part == QP_TAIL && in.s > 0.85f) return fur(C(0.08f, 0.08f, 0.08f));
                if (in.part == QP_LEG && in.s > 0.8f) return fur(dark);
                return fur(light);
            }
            // Hereford: red with a white face, belly, legs and switch
            vec3 red = C(0.55f, 0.21f, 0.09f), wht = C(0.93f, 0.9f, 0.84f);
            if (in.part == QP_HEAD || in.part == QP_JAW) return fur(wht);
            if (in.part == QP_EAR) return fur(red);
            if (in.part == QP_NECK) return fur(up > 0.55f || up < -0.55f ? wht : red);
            if (in.part == QP_BODY && up < -0.6f) return fur(wht);
            if (in.part == QP_LEG && in.s > 0.55f) return fur(wht);
            if (in.part == QP_TAIL && in.s > 0.85f) return fur(wht);
            return fur(red);
        }
        case SP_HORSE: {
            vec3 coat, maneC, legC;
            if (var == 0) { coat = C(0.44f, 0.23f, 0.11f); maneC = C(0.04f, 0.035f, 0.03f); legC = maneC; }
            else if (var == 1) { coat = C(0.62f, 0.31f, 0.13f); maneC = C(0.72f, 0.45f, 0.22f); legC = coat; }
            else { coat = mixc(C(0.78f, 0.78f, 0.76f), C(0.55f, 0.55f, 0.54f), (blot > 0.1f ? 0.7f : 0.f)); maneC = C(0.62f, 0.62f, 0.62f); legC = C(0.35f, 0.35f, 0.35f); }
            if (in.part == QP_MANE || (in.part == QP_TAIL)) return fur(maneC);
            if (in.part == QP_LEG) return fur(mixc(coat, legC, sstep(0.4f, 0.65f, in.s)));
            if (isFootFur) return fur(legC);
            if (in.part == QP_HEAD && var == 1 && in.s > 0.3f && fabsf(cosf(in.th)) < 0.25f && up > 0.f) return fur(C(0.94f, 0.92f, 0.88f));  // blaze
            if (in.part == QP_HEAD && in.s > 0.85f) return fur(coat * 0.6f);
            return fur(coat);
        }
        default: return C(0.5f, 0.5f, 0.5f);
    }
}

void buildQuadSkeleton(const QuadSpec& Q, Skel& sk, vec3* J) {
    using namespace QuadBone;
    sk = Skel();
    float W = Q.withers, H = Q.hipH;
    float yP = -Q.bodyLen * 0.5f, yC = Q.bodyLen * 0.5f;
    sk.add(-1, vec3(0, 0, (W + H) * 0.5f - Q.depth * 0.3f));          // BODY
    sk.add(BODY, vec3(0, yP, H - Q.depth * 0.28f));                   // PELVIS
    sk.add(BODY, vec3(0, yC, W - Q.depth * 0.32f));                   // CHEST
    vec3 nd = vec3(0, cosf(Q.neckAngle), sinf(Q.neckAngle));
    vec3 n1 = vec3(0, yC + Q.brisket * 0.35f, W - Q.depth * 0.15f);
    sk.add(CHEST, n1);                                                // NECK1
    sk.add(NECK1, n1 + nd * (Q.neckLen * 0.5f));                     // NECK2
    vec3 hd = n1 + nd * Q.neckLen;
    sk.add(NECK2, hd);                                                // HEAD (poll)
    vec3 hf = vec3(0, cosf(Q.headPitch), sinf(Q.headPitch));
    vec3 hu = normalize(cross(vec3(1, 0, 0), hf));
    sk.add(HEAD, hd + hf * (Q.headLen * 0.3f) - hu * (Q.headH * 0.45f));        // JAW hinge
    sk.add(HEAD, hd + vec3(-Q.headW * 0.55f, 0, 0) + hf * 0.01f + hu * (Q.headH * 0.3f));   // EAR_L
    sk.add(HEAD, hd + vec3(Q.headW * 0.55f, 0, 0) + hf * 0.01f + hu * (Q.headH * 0.3f));    // EAR_R
    vec3 td = vec3(0, -cosf(Q.tailAngle), sinf(Q.tailAngle));
    vec3 t1 = vec3(0, yP - Q.rump * 0.85f, H - Q.depth * 0.08f);
    sk.add(PELVIS, t1);                                               // TAIL1
    sk.add(TAIL1, t1 + td * (Q.tailLen * 0.33f));                    // TAIL2
    sk.add(TAIL2, t1 + td * (Q.tailLen * 0.66f));                    // TAIL3
    for (int sd = -1; sd <= 1; sd += 2) {   // front legs (FL then FR)
        vec3 s = vec3(sd * Q.legX, yC + 0.02f * W, 0.62f * W);
        vec3 e = vec3(sd * Q.legX * 1.05f, s.y + Q.fElbowY * W, Q.fElbowZ * W);
        vec3 w = vec3(sd * Q.legX * 0.95f, s.y + Q.fWristY * W, Q.fWristZ * W);
        int b1 = sk.add(CHEST, s);
        int b2 = sk.add(b1, e);
        sk.add(b2, w);
        J[b1 + 20] = vec3(sd * Q.legX * 0.95f, s.y + Q.fToeY * W, 0.f);   // toe contact
    }
    for (int sd = -1; sd <= 1; sd += 2) {   // hind legs (HL then HR)
        vec3 h = vec3(sd * Q.legX, yP, 0.62f * H);
        if (Q.plantigrade) h.z = 0.6f * H;
        vec3 k = vec3(sd * Q.legX * 1.1f, yP + Q.hStifleY * H, Q.hStifleZ * H);
        vec3 a = vec3(sd * Q.legX, yP + Q.hHockY * H, Q.hHockZ * H);
        int b1 = sk.add(PELVIS, h);
        int b2 = sk.add(b1, k);
        sk.add(b2, a);
        J[b1 + 20] = vec3(sd * Q.legX, yP + Q.hToeY * H, 0.f);
    }
    // leash (dogs): hangs up and forward from the collar in the bind pose
    vec3 collar = n1 + nd * (Q.neckLen * 0.62f) + vec3(0, 0, Q.neckR2 * 0.9f);
    vec3 ld = normalize(vec3(0, 0.35f, 1.f));
    int prev = NECK2;
    for (int k = 0; k < kLeashSegments; k++) prev = sk.add(k == 0 ? NECK2 : prev, collar + ld * (0.3f * (float)k));
    for (int b = 0; b < sk.n; b++) J[b] = sk.bind[b];
    J[HEAD + 40] = hf;
    J[HEAD + 41] = hu;
    J[HEAD + 42] = collar;
}

void buildQuadMesh(int sp, int var, const QuadSpec& Q, const Skel& sk, const vec3* J, int lod, bool leash, MBuild& mb) {
    using namespace QuadBone;
    int bodyRings = lod == 0 ? 20 : 8, bodySides = lod == 0 ? 18 : 8;
    int limbSides = lod == 0 ? 10 : 5, limbRings = lod == 0 ? 12 : 5;
    auto paint = [&](int part, float s, float th, int side, bool frontLeg, vec3 p, VAttr& a) {
        QuadPaintIn in;
        in.part = part;
        in.s = s;
        in.th = th;
        in.side = side;
        in.front = frontLeg;
        in.p = p;
        u8 mat;
        a.col = quadPaint(sp, var, Q, in, mat);
        a.mat = mat;
    };
    float W = Q.withers, H = Q.hipH;
    float yP = -Q.bodyLen * 0.5f, yC = Q.bodyLen * 0.5f;
    vec3 hf = J[HEAD + 40], hu = J[HEAD + 41];
    // ---- barrel: rump -> chest
    {
        float y0 = yP - Q.rump, y1 = yC + Q.brisket;
        int n = bodyRings;
        std::vector<Sect> S((size_t)n);
        for (int i = 0; i < n; i++) {
            float t = (float)i / (float)(n - 1);
            float y = Lerp(y0, y1, t);
            float topZ = Lerp(H, W, smooth01((y - yP) / Max(yC - yP, 1e-3f)));
            topZ -= Q.depth * 0.05f * sinf(kPi * Saturate((y - yP) / Max(yC - yP, 1e-3f)));   // slight back dip
            float env = powf(Max(sinf(kPi * (0.04f + 0.92f * t)), 0.f), 0.32f);
            float chestK = smooth01((t - 0.45f) / 0.4f);
            float loin = gauss1(t, 0.38f, 0.16f);
            float hT = Q.depth * 0.45f * env;
            float hB = Q.depth * (0.55f + Q.chestDeep * chestK - Q.tuck * loin) * env;
            if (t > 0.9f) hB *= Lerp(1.f, 0.75f, (t - 0.9f) / 0.1f);
            float w = Q.bodyW * env * (0.92f + 0.12f * chestK - 0.1f * loin + 0.1f * gauss1(t, 0.12f, 0.1f));
            S[i].c = vec3(0, y, topZ - hT);
            S[i].w = w;
            S[i].hT = hT;
            S[i].hB = hB;
            S[i].ex = 2.2f;
        }
        frameSections(S, vec3(0, 0, 1));
        float len = S.back().u;
        float aP = (yP - y0), aC = (yC - y0);
        loft(mb, S, bodySides, [&](int, float u, float th, vec3 p, VAttr& a) {
            paint(QP_BODY, u / len, th, cosf(th) < 0.f ? -1 : 1, false, p, a);
            Chain ch;
            ch.blend = Q.bodyLen * 0.18f;
            ch.add(PELVIS, 0.f);
            ch.add(BODY, (aP + aC) * 0.35f);
            ch.add(CHEST, (aP + aC) * 0.68f);
            a.sw = ch.eval(u);
        }, true, true);
    }
    // ---- neck (from inside the chest to the poll); horses: mane on top
    vec3 n1 = sk.bind[NECK1], hd = sk.bind[HEAD];
    vec3 nd = normalize(hd - n1);
    {
        vec3 a0 = n1 - nd * (Q.neckLen * 0.35f) - vec3(0, 0, Q.depth * 0.12f);
        std::vector<vec3> np = smoothPath({a0, n1, sk.bind[NECK2], hd, hd + hf * (Q.headLen * 0.15f)}, lod == 0 ? 4 : 2);
        int rings = lod == 0 ? 12 : 5;
        std::vector<Sect> S = sectionsAlong(np, rings, vec3(0, 0, 1), [&](float t, Sect& s) {
            float r = Lerp(Q.neckR * 1.25f, Q.neckR2, smooth01(t * 1.2f));
            s.w = r * Q.neckFlat;
            s.hT = r;
            s.hB = r * (1.f + Q.dewlap * 0.6f * gauss1(t, 0.35f, 0.25f));
            s.ex = 2.1f;
        });
        float len = S.back().u;
        float aN1 = length(n1 - a0), aN2 = aN1 + length(sk.bind[NECK2] - n1), aH = aN2 + length(hd - sk.bind[NECK2]);
        loft(mb, S, lod == 0 ? 14 : 6, [&](int, float u, float th, vec3 p, VAttr& a) {
            paint(QP_NECK, u / len, th, cosf(th) < 0.f ? -1 : 1, false, p, a);
            Chain ch;
            ch.blend = Q.neckLen * 0.15f;
            ch.add(CHEST, 0.f);
            ch.add(NECK1, aN1);
            ch.add(NECK2, aN2);
            ch.add(HEAD, aH);
            a.sw = ch.eval(u);
        }, true, true);
        if (Q.mane > 0.f) {   // mane crest along the top of the neck + forelock
            std::vector<vec3> mp;
            for (int k = 0; k <= 8; k++) {
                float t = (float)k / 8.f;
                vec3 c = lerp(n1 - nd * (Q.neckLen * 0.05f), hd, t);
                float r = Lerp(Q.neckR * 1.1f, Q.neckR2, t);
                mp.push_back(c + vec3(0, 0, r * 0.95f) + vec3(0, -r * 0.2f, 0));
            }
            std::vector<Sect> MS = sectionsAlong(mp, lod == 0 ? 10 : 4, vec3(1, 0, 0), [&](float t, Sect& s) {
                s.w = 0.018f;
                s.hT = 0.09f * (0.6f + 0.4f * sinf(kPi * t)) + 0.02f;
                s.hB = 0.03f;
                s.ex = 1.6f;
            });
            float ml = MS.back().u;
            loft(mb, MS, lod == 0 ? 6 : 4, [&](int, float u, float th, vec3 p, VAttr& a) {
                paint(QP_MANE, u / ml, th, 0, false, p, a);
                Chain ch;
                ch.blend = 0.08f;
                ch.add(NECK1, 0.f);
                ch.add(NECK2, ml * 0.5f);
                ch.add(HEAD, ml * 0.97f);
                a.sw = ch.eval(u);
            }, true, true);
        }
        if (Q.dewlap > 0.f && lod == 0) {   // Brahman dewlap: a skin fold hanging under the neck
            std::vector<vec3> dp;
            for (int k = 0; k <= 6; k++) {
                float t = (float)k / 6.f;
                dp.push_back(lerp(n1 - nd * (Q.neckLen * 0.2f), hd - nd * (Q.neckLen * 0.1f), t) - vec3(0, 0, Q.neckR * 0.9f));
            }
            std::vector<Sect> DS = sectionsAlong(dp, 7, vec3(1, 0, 0), [&](float t, Sect& s) {
                s.w = 0.02f;
                s.hT = 0.02f;
                s.hB = 0.16f * sinf(kPi * t) + 0.01f;
                s.ex = 1.8f;
            });
            float dl = DS.back().u;
            loft(mb, DS, 6, [&](int, float u, float th, vec3 p, VAttr& a) {
                paint(QP_DEWLAP, u / dl, th, 0, false, p, a);
                a.sw = u < dl * 0.5f ? skin2(CHEST, NECK1, u / (dl * 0.5f)) : skin2(NECK1, NECK2, (u - dl * 0.5f) / (dl * 0.5f));
            }, true, true);
        }
        if (Q.hump > 0.f) {
            vec3 hc = sk.bind[CHEST] + vec3(0, -Q.bodyLen * 0.02f, Q.depth * 0.42f);
            ellipsoid(mb, hc, vec3(0, 1, 0.2f), vec3(0, 0, 1), 0.22f, 0.13f, 0.17f, lod == 0 ? 8 : 4, lod == 0 ? 10 : 5,
                      [&](int, float, float th, vec3 p, VAttr& a) {
                          paint(QP_HUMP, 0.5f, th, 0, false, p, a);
                          a.sw = skin1(CHEST);
                      });
        }
    }
    // ---- head: poll -> nose tip (skull + muzzle), lower jaw, ears, eyes, nose
    {
        vec3 back = hd - hf * (Q.headLen * 0.25f) + hu * (Q.headH * 0.05f);
        vec3 skull = hd + hf * (Q.headLen * 0.35f) + hu * (Q.headH * 0.1f);
        vec3 stop = hd + hf * Q.headLen + hu * (Q.headH * 0.0f);
        vec3 nose = stop + hf * Q.muzzleLen - hu * (Q.muzzleH * 0.15f);
        std::vector<vec3> hp = smoothPath({back, hd + hf * (Q.headLen * 0.1f), skull, stop, nose}, lod == 0 ? 4 : 2);
        float total = 0.f;
        for (size_t i = 1; i < hp.size(); i++) total += length(hp[i] - hp[i - 1]);
        float aStop = Q.headLen * 1.25f;
        int rings = lod == 0 ? 16 : 6;
        std::vector<Sect> S = sectionsAlong(hp, rings, hu, [&](float t, Sect& s) {
            float a = t * total;
            float skullK = Saturate(a / Max(aStop, 1e-3f));
            float w, hT, hB;
            if (a < aStop) {
                float bul = sinf(kPi * Lerp(0.12f, 0.95f, skullK));
                w = Q.headW * (0.7f + 0.3f * bul);
                hT = Q.headH * 0.5f * (0.75f + 0.25f * bul);
                hB = Q.headH * 0.4f * (0.8f + 0.2f * bul);
                float tw = smooth01((skullK - 0.7f) / 0.3f);
                w = Lerp(w, Q.muzzleW * 1.15f, tw);
                hT = Lerp(hT, Q.muzzleH * 0.55f, tw);
                hB = Lerp(hB, Q.muzzleH * 0.35f, tw);
            } else {
                float k = Saturate((a - aStop) / Max(total - aStop, 1e-3f));
                float tip = sqrtf(Max(1.f - powf(k, 3.f), 0.f));
                w = Q.muzzleW * (1.1f - 0.15f * k) * tip;
                hT = Q.muzzleH * 0.55f * tip;
                hB = Q.muzzleH * 0.35f * tip;
            }
            s.w = w;
            s.hT = hT;
            s.hB = hB;
            s.ex = 2.2f;
        });
        loft(mb, S, lod == 0 ? 14 : 6, [&](int, float u, float th, vec3 p, VAttr& a) {
            paint(QP_HEAD, u / total, th, cosf(th) < 0.f ? -1 : 1, false, p, a);
            a.sw = u < Q.headLen * 0.12f ? skin2(NECK2, HEAD, 0.5f + 0.5f * u / (Q.headLen * 0.12f)) : skin1(HEAD);
        }, true, false);
        // lower jaw
        vec3 hinge = sk.bind[JAW];
        vec3 jtip = nose - hf * (Q.muzzleLen * 0.12f) - hu * (Q.muzzleH * 0.4f);
        std::vector<vec3> jp = smoothPath({hinge, lerp(hinge, jtip, 0.5f) - hu * (Q.muzzleH * 0.1f), jtip}, lod == 0 ? 4 : 2);
        std::vector<Sect> JS = sectionsAlong(jp, lod == 0 ? 8 : 4, hu, [&](float t, Sect& s) {
            float tip = sqrtf(Max(1.f - powf(t, 4.f), 0.f));
            s.w = Lerp(Q.headW * 0.6f, Q.muzzleW * 0.8f, t) * tip;
            s.hT = Q.muzzleH * 0.12f * tip + 0.002f;
            s.hB = Lerp(Q.headH * 0.28f, Q.muzzleH * 0.22f, t) * tip;
            s.ex = 2.2f;
        });
        float jl = JS.back().u;
        loft(mb, JS, lod == 0 ? 10 : 5, [&](int, float u, float th, vec3 p, VAttr& a) {
            paint(QP_JAW, u / jl, th, 0, false, p, a);
            a.sw = skin1(JAW);
        }, true, false);
        // nose leather
        if (lod == 0) {
            ellipsoid(mb, nose - hf * (Q.muzzleW * 0.25f) + hu * (Q.muzzleH * 0.05f), hf, hu, Q.muzzleW * 0.35f, Q.muzzleW * 0.6f, Q.muzzleH * 0.38f, 5, 8,
                      [&](int, float, float th, vec3 p, VAttr& a) {
                          paint(QP_NOSE, 0.f, th, 0, false, p, a);
                          a.sw = skin1(HEAD);
                      });
            // eyes
            for (int sd = -1; sd <= 1; sd += 2) {
                float er = Clamp(Q.headH * 0.12f, 0.006f, 0.022f);
                if (sp == SP_CAT) er = 0.009f;
                vec3 ec = hd + hf * (Q.headLen * 0.62f) + hu * (Q.headH * 0.22f) + vec3(sd * Q.headW * 0.72f, 0, 0);
                vec3 axis = normalize(vec3((float)sd, 0, 0) + hf * (sp == SP_CAT || sp == SP_DOG ? 0.9f : 0.3f));
                ellipsoid(mb, ec - axis * (er * 0.3f), axis, hu, er, er, er, 6, 8, [&](int ring, float, float th, vec3 p, VAttr& a) {
                    paint(QP_EYE, 1.f - (float)ring / 5.f, th, sd, false, p, a);
                    a.sw = skin1(HEAD);
                });
            }
        }
        // ears
        for (int sd = -1; sd <= 1; sd += 2) {
            int eb = sd < 0 ? EAR_L : EAR_R;
            vec3 root = sk.bind[eb];
            vec3 erect = normalize(hu * 1.f + vec3(sd * Q.earOut, 0, 0) - hf * 0.25f);
            vec3 flop = normalize(-hu * 0.9f + vec3(sd * 0.55f, 0, 0) + hf * 0.1f);
            vec3 dir = normalize(lerp(erect, flop, Q.earFlop));
            vec3 mid = root + dir * (Q.earLen * 0.5f) + (Q.earFlop > 0.5f ? vec3(sd * Q.earW * 0.3f, 0, 0) : vec3(0));
            std::vector<vec3> ep = {root - dir * (Q.earLen * 0.1f), mid, root + dir * Q.earLen};
            vec3 face = normalize(Q.earFlop > 0.5f ? vec3((float)sd, 0, 0) : hf);   // ear opening faces forward (erect) / sideways (floppy)
            std::vector<Sect> ES = sectionsAlong(smoothPath(ep, 3), lod == 0 ? 6 : 3, face, [&](float t, Sect& s) {
                float wdt = Q.earW * (Q.earFlop > 0.5f ? (0.8f + 0.25f * sinf(kPi * t)) * (t > 0.85f ? 0.8f : 1.f) : (1.f - t * 0.95f));
                s.w = wdt;
                s.hT = Q.earW * 0.12f + 0.002f;
                s.hB = Q.earW * 0.05f + 0.001f;
                s.ex = 1.6f;
            });
            float el = ES.back().u;
            loft(mb, ES, lod == 0 ? 6 : 4, [&](int, float u, float th, vec3 p, VAttr& a) {
                paint(QP_EAR, u / el, th, sd, false, p, a);
                a.sw = u < el * 0.15f ? skin2(HEAD, eb, u / (el * 0.15f)) : skin1(eb);
            }, true, true);
        }
        // horns (cattle) and antlers (bucks)
        if (Q.horns > 0.f) {
            for (int sd = -1; sd <= 1; sd += 2) {
                vec3 root = hd + vec3(sd * Q.headW * 0.7f, 0, 0) + hu * (Q.headH * 0.35f) + hf * 0.02f;
                vec3 d1 = normalize(vec3((float)sd, 0, 0.35f));
                float hl = 0.12f + 0.2f * Q.horns;
                std::vector<vec3> hp2 = smoothPath({root, root + d1 * (hl * 0.5f), root + d1 * (hl * 0.8f) + vec3(0, 0.03f, hl * 0.35f), root + d1 * hl + vec3(0, 0.05f, hl * 0.6f)}, 3);
                std::vector<Sect> HS = sectionsAlong(hp2, lod == 0 ? 7 : 3, hf, [&](float t, Sect& s) {
                    s.w = s.hT = s.hB = Lerp(0.032f, 0.004f, t);
                });
                float hl2 = HS.back().u;
                loft(mb, HS, lod == 0 ? 6 : 4, [&](int, float u, float th, vec3 p, VAttr& a) {
                    paint(QP_HORN, u / hl2, th, sd, false, p, a);
                    a.sw = skin1(HEAD);
                }, true, false);
            }
        }
        if (Q.antlers > 0.f) {
            for (int sd = -1; sd <= 1; sd += 2) {
                vec3 root = hd + vec3(sd * Q.headW * 0.45f, 0, 0) + hu * (Q.headH * 0.4f) + hf * 0.03f;
                // main beam: up, back and out, then curving forward; tines rise from it
                std::vector<vec3> beam = {root, root + vec3(sd * 0.08f, -0.06f, 0.14f), root + vec3(sd * 0.2f, -0.02f, 0.26f), root + vec3(sd * 0.25f, 0.12f, 0.33f),
                                          root + vec3(sd * 0.18f, 0.26f, 0.36f)};
                std::vector<vec3> bp = smoothPath(beam, 3);
                std::vector<Sect> BS = sectionsAlong(bp, lod == 0 ? 12 : 4, hf, [&](float t, Sect& s) { s.w = s.hT = s.hB = Lerp(0.02f, 0.005f, t); });
                float bl = BS.back().u;
                loft(mb, BS, lod == 0 ? 6 : 4, [&](int, float u, float th, vec3 p, VAttr& a) {
                    paint(QP_ANTLER, u / bl, th, sd, false, p, a);
                    a.sw = skin1(HEAD);
                }, true, false);
                if (lod == 0) {
                    for (int k = 0; k < 3; k++) {
                        float t = 0.35f + 0.22f * (float)k;
                        vec3 base = bp[(size_t)Clamp((int)(t * (float)(bp.size() - 1)), 0, (int)bp.size() - 1)];
                        vec3 tip = base + vec3(sd * 0.02f, 0.02f, 0.13f - 0.02f * (float)k);
                        std::vector<Sect> TS(4);
                        for (int q = 0; q < 4; q++) TS[q].c = lerp(base, tip, (float)q / 3.f);
                        frameSections(TS, hf);
                        for (int q = 0; q < 4; q++) TS[q].w = TS[q].hT = TS[q].hB = Lerp(0.011f, 0.003f, (float)q / 3.f);
                        loft(mb, TS, 5, [&](int ring, float, float th, vec3 p, VAttr& a) {
                            paint(QP_ANTLER, 0.5f + (float)ring / 6.f, th, sd, false, p, a);
                            a.sw = skin1(HEAD);
                        }, false, false);
                    }
                }
            }
        }
    }
    // ---- tail
    {
        vec3 t1 = sk.bind[TAIL1], t2 = sk.bind[TAIL2], t3 = sk.bind[TAIL3];
        vec3 td = normalize(t3 - t1);
        vec3 tip = t1 + td * Q.tailLen;
        bool horse = sp == SP_HORSE;
        std::vector<vec3> tp;
        if (horse) {   // dock then a long fall of hair
            tp = smoothPath({t1 - td * 0.03f, t1 + td * 0.12f, t2 + vec3(0, -0.05f, -0.1f), t3 + vec3(0, -0.05f, -0.28f), tip + vec3(0, 0.05f, -0.45f)}, 3);
        } else {
            tp = smoothPath({t1 - td * (Q.tailR * 1.5f), t2, t3, tip}, lod == 0 ? 4 : 2);
        }
        std::vector<Sect> S = sectionsAlong(tp, lod == 0 ? 12 : 5, vec3(0, 0, 1), [&](float t, Sect& s) {
            float r = Q.tailR * Lerp(1.f, 0.35f, t);
            if (Q.tailBush > 1.f) r *= 1.f + (Q.tailBush - 1.f) * 1.4f * sinf(kPi * Min(t * 1.2f, 1.f));
            if (Q.tailTuft > 0.f && t > 0.82f) r = Q.tailR * (1.2f + 1.6f * sinf(kPi * (t - 0.82f) / 0.18f));
            if (horse) r = Lerp(Q.tailR, 0.1f, smooth01(t * 1.5f)) * (t > 0.85f ? Lerp(1.f, 0.4f, (t - 0.85f) / 0.15f) : 1.f);
            s.w = r * (horse ? 0.7f : 1.f);
            s.hT = s.hB = r;
            s.ex = 2.f;
        });
        float tl = S.back().u;
        float a2 = length(t2 - t1), a3 = a2 + length(t3 - t2);
        loft(mb, S, lod == 0 ? 8 : 4, [&](int, float u, float th, vec3 p, VAttr& a) {
            paint(QP_TAIL, u / tl, th, 0, false, p, a);
            Chain ch;
            ch.blend = Q.tailLen * 0.08f;
            ch.add(PELVIS, 0.f);
            ch.add(TAIL1, Q.tailR * 1.5f);
            ch.add(TAIL2, a2);
            ch.add(TAIL3, a3);
            a.sw = ch.eval(u);
        }, true, true);
    }
    // ---- legs: shoulder/hip -> elbow/stifle -> wrist/hock -> toe
    for (int leg = 0; leg < 4; leg++) {
        bool front = leg < 2;
        int sd = (leg & 1) ? 1 : -1;
        int b1 = front ? (sd < 0 ? FL1 : FR1) : (sd < 0 ? HL1 : HR1);
        int b2 = b1 + 1, b3 = b1 + 2;
        vec3 j1 = sk.bind[b1], j2 = sk.bind[b2], j3 = sk.bind[b3], toe = J[b1 + 20];
        int parentB = front ? CHEST : PELVIS;
        vec3 top = j1 + vec3(-sd * Q.legX * 0.35f, front ? -0.02f * W : 0.03f * H, (front ? W : H) * 0.1f);
        std::vector<vec3> lp = smoothPath({top, j1, j2, j3, toe + vec3(0, 0, Q.rFoot * 0.9f)}, lod == 0 ? 3 : 1);
        float aJ1 = length(j1 - top), aJ2 = aJ1 + length(j2 - j1), aJ3 = aJ2 + length(j3 - j2);
        float total = 0.f;
        for (size_t i = 1; i < lp.size(); i++) total += length(lp[i] - lp[i - 1]);
        std::vector<Sect> S = sectionsAlong(lp, limbRings + 2, vec3(0, 1, 0), [&](float t, Sect& s) {
            float a = t * total;
            float r;
            if (a < aJ2) r = Lerp(front ? Q.rArm * 1.25f : Q.rThigh * 1.3f, front ? Q.rArm * 0.7f : Q.rThigh * 0.6f, smooth01(a / Max(aJ2, 1e-3f)));
            else if (a < aJ3) r = Lerp(front ? Q.rArm * 0.6f : Q.rThigh * 0.45f, Q.rShin, smooth01((a - aJ2) / Max(aJ3 - aJ2, 1e-3f)));
            else r = Lerp(Q.rShin * 1.1f, Q.rFoot, smooth01((a - aJ3) / Max(total - aJ3, 1e-3f)));
            float flat = a < aJ2 ? (front ? 0.8f : 0.62f) : 1.f;   // thighs are flat and deep
            s.w = r * flat;
            s.hT = s.hB = r;
            s.ex = 2.f;
        });
        loft(mb, S, limbSides, [&](int, float u, float th, vec3 p, VAttr& a) {
            paint(QP_LEG, u / total, th, sd, front, p, a);
            Chain ch;
            ch.blend = Max(total * 0.05f, 0.01f);
            ch.add(parentB, 0.f);
            ch.add(b1, aJ1 * 0.75f);
            ch.add(b2, aJ2);
            ch.add(b3, aJ3);
            a.sw = ch.eval(u);
        }, true, false);
        // hoof / paw
        vec3 fdir = normalize(toe - j3);
        if (Q.hoof) {
            std::vector<Sect> HS(4);
            vec3 hb = toe + vec3(0, 0, Q.rFoot * 1.6f);
            for (int q = 0; q < 4; q++) HS[q].c = hb + vec3(0, 0, -Q.rFoot * 1.6f * (float)q / 3.f) + vec3(0, Q.rFoot * 0.3f * (float)q / 3.f, 0);
            frameSections(HS, vec3(0, 1, 0));
            for (int q = 0; q < 4; q++) {
                float t = (float)q / 3.f;
                HS[q].w = Q.rFoot * (0.95f + 0.3f * t);
                HS[q].hT = Q.rFoot * (1.f + 0.45f * t);
                HS[q].hB = Q.rFoot * (0.8f + 0.2f * t);
            }
            loft(mb, HS, limbSides, [&](int, float, float th, vec3 p, VAttr& a) {
                paint(QP_FOOT, 1.f, th, sd, front, p, a);
                a.sw = skin1(b3);
            }, false, true);
        } else if (lod == 0 || !front) {
            vec3 pc = toe + vec3(0, Q.rFoot * (Q.plantigrade ? 0.2f : 0.6f), Q.rFoot * 0.75f);
            ellipsoid(mb, pc, vec3(0, 1, 0), vec3(0, 0, 1), Q.rFoot * (Q.plantigrade ? 2.2f : 1.5f), Q.rFoot * 1.15f, Q.rFoot * 0.8f, lod == 0 ? 6 : 4,
                      lod == 0 ? 8 : 5, [&](int, float, float th, vec3 p, VAttr& a) {
                          paint(QP_FOOT, 1.f, th, sd, front, p, a);
                          a.sw = skin1(b3);
                      });
        }
        (void)fdir;
    }
    // ---- leash (dogs): thin rope tube along the leash bones
    if (leash) {
        vec3 c0 = J[HEAD + 42];
        std::vector<Sect> LS((size_t)kLeashSegments + 1);
        for (int k = 0; k <= kLeashSegments; k++) LS[k].c = k < kLeashSegments ? sk.bind[LEASH0 + k] : sk.bind[LEASH0 + kLeashSegments - 1] + normalize(vec3(0, 0.35f, 1.f)) * 0.3f;
        frameSections(LS, vec3(0, 1, 0));
        for (int k = 0; k <= kLeashSegments; k++) LS[k].w = LS[k].hT = LS[k].hB = 0.0065f;
        loft(mb, LS, 4, [&](int ring, float, float th, vec3 p, VAttr& a) {
            paint(QP_LEASH, 0.f, th, 0, false, p, a);
            int k = Min(ring, kLeashSegments - 1);
            a.sw = (ring > 0 && ring < kLeashSegments) ? skin2(LEASH0 + ring - 1, LEASH0 + ring, 0.5f) : skin1(LEASH0 + k);
        }, true, true);
        // collar ring around the neck
        vec3 nc = sk.bind[NECK1] + nd * (Q.neckLen * 0.62f);
        std::vector<Sect> CS(3);
        for (int q = 0; q < 3; q++) CS[q].c = nc + nd * (0.012f * (float)(q - 1));
        frameSections(CS, vec3(0, 0, 1));
        float r = Lerp(Q.neckR * 1.25f, Q.neckR2, 0.62f) + 0.004f;
        for (int q = 0; q < 3; q++) {
            CS[q].w = r * Q.neckFlat;
            CS[q].hT = r;
            CS[q].hB = r;
        }
        loft(mb, CS, lod == 0 ? 12 : 6, [&](int, float, float th, vec3 p, VAttr& a) {
            paint(QP_LEASH, 0.f, th, 0, false, p, a);
            a.sw = skin1(NECK2);
        }, false, false);
        (void)c0;
    }
}

void buildQuad(int sp, int var, ModelData& out) {
    using namespace QuadBone;
    QuadSpec Q = quadSpec(sp, var);
    vec3 J[80];
    buildQuadSkeleton(Q, out.skel, J);
    bool leash = sp == SP_DOG;
    MBuild m0, m1;
    buildQuadMesh(sp, var, Q, out.skel, J, 0, leash, m0);
    buildQuadMesh(sp, var, Q, out.skel, J, 1, leash, m1);
    m0.emit(out.lod[0]);
    m1.emit(out.lod[1]);
    out.legs = 4;
    const int lb[4] = {FL1, FR1, HL1, HR1};
    for (int l = 0; l < 4; l++) {
        out.legBone[l][0] = lb[l];
        out.legBone[l][1] = lb[l] + 1;
        out.legBone[l][2] = lb[l] + 2;
        out.legEnd[l] = J[lb[l] + 20] - out.skel.bind[lb[l] + 2];
    }
    out.legLen = Q.withers;
    out.collar = J[HEAD + 42];
    out.headTip = out.skel.bind[HEAD] + J[HEAD + 40] * (Q.headLen + Q.muzzleLen);
    out.mouth = out.headTip - J[HEAD + 40] * (Q.muzzleLen * 0.3f) - J[HEAD + 41] * (Q.muzzleH * 0.3f);
    // leashed dogs reach up to ~2 m from the collar: keep the culling bounds generous
    if (leash)
        for (int l = 0; l < 2; l++) {
            out.lod[l].bounds.add(out.collar + vec3(2.2f, 2.2f, 1.5f));
            out.lod[l].bounds.add(out.collar - vec3(2.2f, 2.2f, 1.f));
        }
}

}  // namespace fauna_detail
