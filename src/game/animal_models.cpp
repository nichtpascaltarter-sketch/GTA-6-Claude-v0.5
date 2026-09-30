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
            F.g[b] = pose.g[b];
        } else {
            F.r[b] = normalize(F.r[par] * pose.q[b]);
            F.p[b] = F.p[par] + rotate(F.r[par], (sk.bind[b] - sk.bind[par]) * F.g[par]);
            F.g[b] = F.g[par] * pose.g[b];
        }
        if (skin) {
            mat3 R = mat3FromQuat(F.r[b]);
            vec3 s = pose.s[b] * F.g[b];
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
    {"sanderling",       PLAN_BIRD,     1,   0.20f, 0.13f, 0.40f, 3.f,   0.06f},
    {"grackle",          PLAN_BIRD,     2,   0.40f, 0.24f, 0.55f, 4.f,   0.2f},
    {"frigatebird",      PLAN_BIRD,     2,   1.00f, 0.40f, 2.20f, 15.f,  1.4f},
    {"cormorant",        PLAN_BIRD,     1,   0.80f, 0.60f, 1.25f, 12.f,  1.8f},
    {"cattle egret",     PLAN_BIRD,     1,   0.50f, 0.46f, 0.90f, 6.f,   0.35f},
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
    float tailLen, tailW, tailShape, tailTilt;  // tailShape: 0 square, 1 rounded, 2 pointed, 3 deeply forked
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
    static const BirdSpec kSanderling = {0.11f, 0.034f, 0.032f, 0.038f, 0.025f, 0.014f, 0.9f, 0.f, 0.026f, 0.014f,
                                         0.025f, 0.0035f, 0.004f, 0.f, 0.f, 0.f, -0.05f, 0.002f,
                                         0.05f, 0.022f, 1.f, 0.f, 0.40f, 0.06f, 0.06f, 0.02f, 0.3f, 0, 0.f, 0.22f, 0.28f,
                                         0.03f, 0.025f, 0.0028f, 0.018f, 0.012f, 0.f, false, false, 0.0035f, 0.15f};
    static const BirdSpec kGrackle = {0.15f, 0.04f, 0.04f, 0.045f, 0.04f, 0.017f, 1.f, 0.f, 0.035f, 0.017f,
                                      0.032f, 0.005f, 0.008f, 0.1f, 0.f, 0.f, -0.05f, 0.003f,
                                      0.19f, 0.035f, 1.f, 0.12f, 0.52f, 0.085f, 0.09f, 0.03f, 0.2f, 0, 0.f, 0.22f, 0.28f,
                                      0.04f, 0.038f, 0.004f, 0.03f, 0.017f, 0.f, false, false, 0.0045f, 0.35f};
    static const BirdSpec kFrigate = {0.38f, 0.075f, 0.07f, 0.08f, 0.08f, 0.03f, 0.7f, 0.f, 0.07f, 0.03f,
                                      0.11f, 0.009f, 0.013f, 0.f, 0.9f, 0.f, -0.05f, 0.005f,
                                      0.40f, 0.07f, 3.f, 0.f, 2.2f, 0.26f, 0.22f, 0.05f, 0.45f, 0, 0.f, 0.24f, 0.3f,
                                      0.04f, 0.025f, 0.006f, 0.04f, 0.035f, 0.f, true, false, 0.006f, 0.3f};
    static const BirdSpec kCormorant = {0.40f, 0.075f, 0.07f, 0.08f, 0.20f, 0.024f, 1.1f, 0.35f, 0.07f, 0.026f,
                                        0.065f, 0.008f, 0.011f, 0.f, 0.6f, 0.f, -0.05f, 0.005f,
                                        0.15f, 0.06f, 1.f, -0.1f, 1.25f, 0.2f, 0.22f, 0.08f, 0.1f, 3, 0.06f, 0.28f, 0.32f,
                                        0.07f, 0.055f, 0.008f, 0.07f, 0.045f, -0.05f, true, false, 0.006f, 0.95f};
    static const BirdSpec kCattleEgret = {0.22f, 0.052f, 0.05f, 0.058f, 0.16f, 0.017f, 1.1f, 0.7f, 0.05f, 0.019f,
                                          0.06f, 0.006f, 0.009f, 0.f, 0.f, 0.f, -0.08f, 0.004f,
                                          0.08f, 0.04f, 1.f, 0.f, 0.9f, 0.15f, 0.16f, 0.06f, 0.05f, 0, 0.f, 0.3f, 0.34f,
                                          0.1f, 0.08f, 0.0045f, 0.05f, 0.025f, -0.01f, false, true, 0.0045f, 0.5f};
    switch (sp) {
        case SP_SANDPIPER: return kSanderling;
        case SP_GRACKLE: return kGrackle;
        case SP_FRIGATE: return kFrigate;
        case SP_CORMORANT: return kCormorant;
        case SP_CEGRET: return kCattleEgret;
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
            case SP_GRACKLE: iris = var == 0 ? C(0.95f, 0.88f, 0.35f) : C(0.3f, 0.2f, 0.12f); break;
            case SP_CORMORANT: iris = C(0.2f, 0.7f, 0.42f); break;
            case SP_CEGRET: iris = C(0.95f, 0.85f, 0.3f); break;
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
        case SP_SANDPIPER: {   // sanderling (winter): pale grey above, white below, black shoulder, bill and legs
            vec3 grey = C(0.66f, 0.66f, 0.64f), dark = C(0.12f, 0.12f, 0.13f);
            float fleck = streak > 0.78f ? 0.35f : 0.f;
            switch (in.part) {
                case BP_BODY: return feather(sinf(in.th) > 0.15f && in.p.y > -0.05f ? mixc(grey, dark, fleck) : white);
                case BP_NECK: return feather(sinf(in.th) > 0.3f ? grey : white);
                case BP_HEAD: return feather(sinf(in.th) > 0.45f && in.s < 0.7f ? grey : white);
                case BP_BILL: case BP_JAW: mat = MAT_SKIN; return dark;
                case BP_WING:
                    if (!in.upper) return feather(white);
                    if (in.s > 0.62f) return in.chord > 0.5f && in.chord < 0.66f && in.s < 0.85f ? white : dark;   // white wing bar
                    if (in.chord > 0.52f && in.chord < 0.68f) return feather(white);
                    return feather(in.s < 0.18f ? dark : grey);
                case BP_FOLD: return in.s < 0.14f ? dark : (in.s > 0.8f ? dark : feather(mixc(grey, dark, fleck)));
                case BP_TAIL: return feather(fabsf(cosf(in.th)) < 0.4f ? C(0.35f, 0.35f, 0.36f) : white);
                case BP_LEG: case BP_TOE: mat = MAT_SKIN; return dark;
                default: return grey;
            }
        }
        case SP_GRACKLE: {   // boat-tailed grackle: glossy blue-black male, brown female
            bool male = var == 0;
            float irid = 0.5f + 0.5f * sinf(in.p.y * 40.f + in.th * 2.f + mottle * 4.f);
            vec3 gloss = mixc(C(0.035f, 0.03f, 0.075f), C(0.02f, 0.06f, 0.08f), irid);
            vec3 fb = C(0.33f, 0.23f, 0.15f), fbelly = C(0.62f, 0.47f, 0.32f), fdark = C(0.19f, 0.14f, 0.1f);
            switch (in.part) {
                case BP_BILL: case BP_JAW: mat = MAT_SKIN; return C(0.03f, 0.03f, 0.03f);
                case BP_LEG: case BP_TOE: mat = MAT_SKIN; return C(0.05f, 0.05f, 0.05f);
                case BP_WING: case BP_FOLD: case BP_FINGER: case BP_TAIL: return male ? feather(gloss * 0.9f) : feather(fdark);
                case BP_BODY: case BP_NECK: return male ? feather(gloss) : feather(sinf(in.th) < -0.2f ? fbelly : fb);
                case BP_HEAD: return male ? feather(gloss) : feather(sinf(in.th) < 0.f ? fbelly : fb);
                default: return male ? gloss : fb;
            }
        }
        case SP_FRIGATE: {   // magnificent frigatebird: black; the male has a red throat pouch, the female a white breast
            bool male = var == 0;
            vec3 blk = C(0.03f, 0.03f, 0.035f), brown = C(0.33f, 0.26f, 0.19f);
            switch (in.part) {
                case BP_BODY: return feather(!male && sinf(in.th) < -0.15f && in.p.y > -0.06f ? white : blk);
                case BP_NECK: {
                    if (male && sinf(in.th) < -0.35f && in.s > 0.25f) { mat = MAT_SKIN; return C(0.72f, 0.07f, 0.05f); }
                    return feather(!male && sinf(in.th) < -0.3f && in.s < 0.45f ? white : blk);
                }
                case BP_HEAD: return feather(blk);
                case BP_BILL: case BP_JAW: mat = MAT_SKIN; return male ? C(0.42f, 0.42f, 0.45f) : C(0.62f, 0.64f, 0.7f);
                case BP_WING: return feather(!male && in.upper && in.s < 0.42f && in.chord > 0.35f && in.chord < 0.7f ? brown : blk);
                case BP_FOLD: return feather(!male && in.s > 0.25f && in.s < 0.5f ? brown : blk);
                case BP_LEG: case BP_TOE: mat = MAT_SKIN; return male ? C(0.1f, 0.1f, 0.1f) : C(0.75f, 0.35f, 0.35f);
                default: return feather(blk);
            }
        }
        case SP_CORMORANT: {   // double-crested cormorant: black with bronze scaled back, orange throat skin, hooked bill
            vec3 blk = C(0.035f, 0.035f, 0.03f), bronze = C(0.2f, 0.17f, 0.11f);
            float sv = in.p.y * 28.f + fabsf(in.p.x) * 18.f;
            float scale = sv - floorf(sv);   // scaly edging of the back feathers
            switch (in.part) {
                case BP_BODY: return feather(sinf(in.th) > 0.35f && in.p.y > -0.1f ? (scale < 0.8f ? bronze : blk) : blk);
                case BP_FOLD: return feather(in.s < 0.55f ? (scale < 0.78f ? bronze : blk) : blk);
                case BP_WING: return feather(in.upper && in.s < 0.45f && in.chord < 0.6f ? (scale < 0.75f ? bronze : blk) : blk);
                case BP_HEAD: if (in.s > 0.72f && sinf(in.th) < 0.1f) { mat = MAT_SKIN; return C(0.9f, 0.5f, 0.12f); } return feather(blk);
                case BP_JAW: mat = MAT_SKIN; return in.s < 0.5f ? C(0.88f, 0.48f, 0.12f) : C(0.3f, 0.28f, 0.25f);
                case BP_BILL: mat = MAT_SKIN; return C(0.22f, 0.21f, 0.2f);
                case BP_LEG: case BP_TOE: mat = MAT_SKIN; return C(0.04f, 0.04f, 0.04f);
                default: return feather(blk);
            }
        }
        case SP_CEGRET: {   // cattle egret in breeding plumage: white with buff crown, back and breast plumes
            vec3 buff = C(0.93f, 0.66f, 0.38f), wh = C(0.96f, 0.95f, 0.92f);
            switch (in.part) {
                case BP_HEAD: return feather(sinf(in.th) > 0.25f && in.s < 0.75f ? buff : wh);
                case BP_NECK: return feather(sinf(in.th) < -0.35f && in.s < 0.55f ? mixc(wh, buff, 0.8f) : (sinf(in.th) > 0.3f && in.s > 0.6f ? mixc(wh, buff, 0.5f) : wh));
                case BP_BODY: return feather(sinf(in.th) > 0.55f && in.p.y > -0.05f && in.p.y < 0.08f ? mixc(wh, buff, 0.75f) : wh);
                case BP_BILL: case BP_JAW: mat = MAT_SKIN; return C(0.97f, 0.7f, 0.18f);
                case BP_LEG: if (in.s < 0.2f) return feather(wh); mat = MAT_SKIN; return C(0.72f, 0.55f, 0.28f);
                case BP_TOE: mat = MAT_SKIN; return C(0.6f, 0.45f, 0.24f);
                default: return feather(wh);
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
    else L = {5, 4, 6, 3, 4, 4, 2, 4, 3, 2, false, false, false, longLegs, false, true};
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
    // ---- tail (forked tails: two long outer streamers)
    if (B.tailShape > 2.5f) {
        vec3 t0 = sk.bind[TAIL];
        for (int sd = -1; sd <= 1; sd += 2) {
            vec3 dirT = normalize(vec3((float)sd * 0.16f, -1.f, B.tailTilt));
            std::vector<vec3> tp = {t0 + vec3(0, B.tailLen * 0.04f, 0), t0 + dirT * (B.tailLen * 0.5f), t0 + dirT * B.tailLen};
            std::vector<Sect> S = sectionsAlong(tp, L.tailRings + 1, vec3(0, 0, 1), [&](float t, Sect& s) {
                s.w = Lerp(B.tailW * 0.55f, B.tailW * 0.1f, powf(t, 0.7f));
                s.hT = Lerp(B.bodyHT * 0.2f, 0.003f, smooth01(t * 2.f)) + 0.002f;
                s.hB = Lerp(B.bodyHB * 0.15f, 0.002f, smooth01(t * 2.f)) + 0.0015f;
                s.ex = 2.2f;
            });
            float tl = S.back().u;
            loft(mb, S, L.tailSides, [&](int, float u, float th, vec3 p, VAttr& a) {
                paint(BP_TAIL, u / tl, 0.f, sinf(th) > 0.f, th, p, a);
                a.sw = u < tl * 0.1f ? skin2(BODY, TAIL, 0.5f + 5.f * u / tl) : skin1(TAIL);
            }, true, true);
        }
    } else {
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
    float neckDrop = 0.f;           // lowers the neck root (fraction of the barrel depth): horses carry the neck from low on the chest
};

QuadSpec quadSpec(int sp, int var) {
    QuadSpec q = {};
    switch (sp) {
        case SP_DOG: {
            q = {0.57f, 0.55f, 0.46f, 0.115f, 0.26f, 0.25f, 0.28f, 0.07f, 0.07f,
                 0.20f, 0.075f, 0.058f, 0.85f, 1.f,
                 0.13f, 0.068f, 0.105f, 0.095f, 0.042f, 0.064f, -0.3f,
                 0.10f, 0.055f, 1.f, 0.3f,
                 0.40f, 0.032f, 1.f, -0.35f, 0.f,
                 0.075f,
                 0.44f, -0.06f, 0.12f, 0.0f, 0.05f,
                 0.42f, 0.14f, 0.22f, -0.16f, -0.10f,
                 0.075f, 0.05f, 0.028f, 0.03f, false, false, 0.f, 0.f, 0.f, 0.f, 0.f};
            if (var == 2) {   // german shepherd: longer, erect ears, sloping back, bushy low tail
                q.withers = 0.62f; q.hipH = 0.55f; q.bodyLen = 0.52f; q.earFlop = 0.f; q.earLen = 0.12f; q.earOut = 0.25f;
                q.muzzleLen = 0.12f; q.muzzleH = 0.056f; q.headH = 0.1f; q.tailBush = 1.6f; q.tailAngle = -0.9f; q.tailLen = 0.44f;
            } else if (var == 3) {   // small terrier
                q.withers = 0.30f; q.hipH = 0.30f; q.bodyLen = 0.26f; q.bodyW = 0.07f; q.depth = 0.15f; q.rump = 0.04f; q.brisket = 0.04f;
                q.neckLen = 0.1f; q.neckR = 0.045f; q.neckR2 = 0.035f; q.headLen = 0.085f; q.headW = 0.044f; q.headH = 0.066f;
                q.muzzleLen = 0.05f; q.muzzleW = 0.024f; q.muzzleH = 0.036f; q.earLen = 0.05f; q.earW = 0.035f; q.earFlop = 0.3f;
                q.tailLen = 0.14f; q.tailR = 0.014f; q.tailAngle = 0.9f; q.legX = 0.045f;
                q.rThigh = 0.045f; q.rArm = 0.03f; q.rShin = 0.016f; q.rFoot = 0.018f;
            } else if (var == 4) {   // stocky brindle stray
                q.withers = 0.50f; q.hipH = 0.49f; q.bodyLen = 0.42f; q.bodyW = 0.13f; q.depth = 0.25f; q.headW = 0.078f; q.headH = 0.11f;
                q.muzzleLen = 0.075f; q.muzzleW = 0.047f; q.muzzleH = 0.068f; q.earFlop = 0.45f; q.earLen = 0.07f; q.tailLen = 0.3f; q.tailR = 0.022f;
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
                 0.30f, 0.135f, 0.25f, 0.17f, 0.1f, 0.155f, -0.9f,
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
                 0.80f, 0.25f, 0.15f, 0.75f, 0.7f,
                 0.34f, 0.1f, 0.27f, 0.22f, 0.08f, 0.15f, -0.95f,
                 0.15f, 0.055f, 0.f, 0.15f,
                 1.00f, 0.06f, 1.f, -1.15f, 0.f,
                 0.14f,
                 0.46f, -0.04f, 0.28f, 0.0f, 0.05f,
                 0.52f, 0.12f, 0.32f, -0.12f, -0.03f,
                 0.22f, 0.12f, 0.045f, 0.055f, true, false, 0.f, 0.f, 1.f, 0.f, 0.f};
            q.neckDrop = 0.2f;
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
    vec3 n1 = vec3(0, yC + Q.brisket * 0.35f, W - Q.depth * (0.15f + Q.neckDrop));
    sk.add(CHEST, n1);                                                // NECK1
    sk.add(NECK1, n1 + nd * (Q.neckLen * 0.5f));                     // NECK2
    vec3 hd = n1 + nd * Q.neckLen;
    sk.add(NECK2, hd);                                                // HEAD (poll)
    vec3 hf = vec3(0, cosf(Q.headPitch), sinf(Q.headPitch));
    vec3 hu = normalize(cross(vec3(1, 0, 0), hf));
    sk.add(HEAD, hd + hf * (Q.headLen * 0.3f) - hu * (Q.headH * 0.45f));        // JAW hinge
    float earX = Q.headW * (0.55f + 0.3f * Q.earFlop);
    sk.add(HEAD, hd + vec3(-earX, 0, 0) + hf * 0.01f + hu * (Q.headH * 0.3f));   // EAR_L
    sk.add(HEAD, hd + vec3(earX, 0, 0) + hf * 0.01f + hu * (Q.headH * 0.3f));    // EAR_R
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
                float tip = sqrtf(Max(1.f - powf(k, Q.hoof ? 7.f : 3.5f), 0.f));
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
            s.hB = Lerp(Q.headH * (Q.hoof ? 0.42f : 0.3f), Q.muzzleH * (Q.hoof ? 0.3f : 0.24f), smooth01(t)) * tip;
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
        std::vector<vec3> lp = smoothPath({top, j1, j2, j3, toe + vec3(0, Q.hoof ? 0.f : Q.rFoot * 0.2f, Q.rFoot * (Q.hoof ? 0.9f : 1.1f))}, lod == 0 ? 3 : 1);
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
            vec3 pc = toe + vec3(0, Q.rFoot * (Q.plantigrade ? 0.2f : 0.3f), Q.rFoot * 0.78f);
            ellipsoid(mb, pc, vec3(0, 1, 0), vec3(0, 0, 1), Q.rFoot * (Q.plantigrade ? 2.2f : 1.25f), Q.rFoot * 1.2f, Q.rFoot * 0.82f, lod == 0 ? 6 : 4,
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

// ==================================================================================================================
// Reptiles: alligator and green iguana (one lofted body from the tail tip to the snout, lower jaw, sprawling legs)
namespace fauna_detail {

struct ReptSpec {
    float snout, skullBack, neck, shoulder, pelvis, tailBase, tailTip;   // y positions along the body
    float bodyW, bodyHT, bodyHB, bodyZ;                                  // barrel half extents and centre height
    float headW, headH, snoutW, snoutH;
    float tailW, tailH;
    float legX, shoulderZ, upperLen, lowerLen, footLen, legR;
    float scute, crest, dewlap;                                          // back osteoderms, dorsal spines, throat flap
};

ReptSpec reptSpec(int sp) {
    if (sp == SP_IGUANA)
        return {0.30f, 0.21f, 0.17f, 0.13f, -0.10f, -0.15f, -1.05f,
                0.055f, 0.055f, 0.05f, 0.1f,
                0.034f, 0.036f, 0.018f, 0.018f,
                0.035f, 0.04f,
                0.06f, 0.1f, 0.075f, 0.075f, 0.05f, 0.012f,
                0.f, 1.f, 1.f};
    return {1.30f, 0.80f, 0.62f, 0.48f, -0.40f, -0.52f, -2.10f,
            0.30f, 0.14f, 0.15f, 0.30f,
            0.17f, 0.10f, 0.085f, 0.045f,
            0.16f, 0.15f,
            0.19f, 0.26f, 0.2f, 0.19f, 0.14f, 0.05f,
            1.f, 0.f, 0.f};
}

enum ReptPart : int { RP_BODY = 0, RP_JAW, RP_LEG, RP_TOE, RP_EYE, RP_TOOTH, RP_CREST, RP_DEWLAP };

vec3 reptPaint(int sp, int var, int part, float s, float th, vec3 p, u8& mat) {
    mat = MAT_SKIN;
    float up = sinf(th);
    float mott = fbm3(p * 6.f, 555u + (u32)var, 3) * 0.5f + 0.5f;
    float fine = n3(p * 70.f, 21u) * 0.5f + 0.5f;
    if (part == RP_EYE) {
        mat = MAT_EYE;
        if (s < 0.2f) return C(0.02f, 0.02f, 0.02f);
        return sp == SP_GATOR ? C(0.62f, 0.58f, 0.2f) : C(0.75f, 0.55f, 0.25f);
    }
    if (part == RP_TOOTH) return C(0.93f, 0.9f, 0.8f);
    if (sp == SP_GATOR) {
        vec3 back = C(0.11f, 0.12f, 0.09f), side = C(0.2f, 0.2f, 0.15f), belly = C(0.78f, 0.74f, 0.58f);
        vec3 c = mixc(side, back, sstep(-0.1f, 0.6f, up));
        c = mixc(c, belly, sstep(-0.35f, -0.75f, up));
        if (part == RP_JAW && up > 0.4f) {   // inside of the mouth: cream gums, yellowish tongue, darker throat
            vec3 m = mixc(C(0.8f, 0.72f, 0.56f), C(0.74f, 0.6f, 0.42f), sstep(0.8f, 0.97f, up));
            return mixc(C(0.36f, 0.27f, 0.2f), m, sstep(0.0f, 0.25f, s)) * (0.9f + 0.1f * fine);
        }
        if (part == RP_BODY && s > 0.83f && up < -0.2f && p.y > 0.75f) c = C(0.8f, 0.72f, 0.58f);   // palate / jaw lining
        if (p.y < -0.5f && up > -0.3f) c = mixc(c, C(0.28f, 0.27f, 0.18f), sinf(p.y * 9.f) > 0.55f ? 0.5f : 0.f);   // faint tail bands
        return c * (0.8f + 0.25f * mott) * (0.92f + 0.12f * fine);
    }
    // iguana: green with darker bands (variant 1: grey-orange adult male)
    vec3 base = var == 0 ? C(0.38f, 0.58f, 0.26f) : C(0.62f, 0.50f, 0.34f);
    vec3 band = var == 0 ? C(0.16f, 0.26f, 0.12f) : C(0.3f, 0.26f, 0.2f);
    if (part == RP_CREST) return var == 0 ? C(0.5f, 0.62f, 0.32f) : C(0.75f, 0.52f, 0.3f);
    if (part == RP_DEWLAP) return base * 1.1f;
    vec3 c = base;
    if (p.y < 0.05f && sinf(p.y * 32.f) > 0.6f && up > -0.5f) c = band;
    c = mixc(c, C(0.75f, 0.78f, 0.6f), sstep(-0.4f, -0.85f, up));
    return c * (0.85f + 0.2f * mott) * (0.92f + 0.12f * fine);
}

void buildReptSkeleton(const ReptSpec& R, Skel& sk, vec3* J) {
    using namespace ReptBone;
    sk = Skel();
    float z = R.bodyZ;
    sk.add(-1, vec3(0, 0, z));                                  // BODY
    sk.add(BODY, vec3(0, R.pelvis, z));                         // PELVIS
    sk.add(BODY, vec3(0, R.shoulder, z));                       // CHEST
    sk.add(CHEST, vec3(0, R.neck, z + R.bodyHT * 0.1f));        // NECK
    sk.add(NECK, vec3(0, R.skullBack, z + R.bodyHT * 0.15f));   // HEAD
    sk.add(HEAD, vec3(0, R.skullBack + 0.02f * (R.snout - R.skullBack), z - R.headH * 0.2f));   // JAW hinge
    int prev = PELVIS;
    for (int k = 0; k < 6; k++) {
        float t = (float)k / 6.f;
        float y = Lerp(R.tailBase, R.tailTip, powf(t, 0.85f));
        prev = sk.add(prev, vec3(0, y, z - 0.02f * t));         // TAIL1..TAIL6
    }
    for (int leg = 0; leg < 4; leg++) {
        bool front = leg < 2;
        float sd = (leg & 1) ? 1.f : -1.f;
        float y = front ? R.shoulder - 0.03f : R.pelvis + 0.02f;
        vec3 s = vec3(sd * R.legX, y, z + R.bodyHT * 0.05f);
        vec3 e = s + vec3(sd * R.upperLen * 0.95f, front ? -R.upperLen * 0.2f : R.upperLen * 0.25f, -R.upperLen * 0.25f);
        vec3 w = vec3(e.x + sd * R.lowerLen * 0.1f, e.y + (front ? R.lowerLen * 0.25f : -R.lowerLen * 0.2f), R.legR * 0.8f);
        int b1 = sk.add(front ? CHEST : PELVIS, s);
        int b2 = sk.add(b1, e);
        sk.add(b2, w);
        J[b1 + 20] = w + vec3(sd * R.footLen * 0.2f, R.footLen * (front ? 0.85f : 0.95f), -R.legR * 0.8f);
    }
    for (int b = 0; b < sk.n; b++) J[b] = sk.bind[b];
}

void buildReptMesh(int sp, int var, const ReptSpec& R, const Skel& sk, const vec3* J, int lod, MBuild& mb) {
    using namespace ReptBone;
    int rings = lod == 0 ? 56 : 18, sides = lod == 0 ? 20 : 8;
    float z = R.bodyZ;
    float L = R.snout - R.tailTip;
    // body path: tail tip -> snout, spine slightly arched
    std::vector<Sect> S((size_t)rings);
    for (int i = 0; i < rings; i++) {
        float t = (float)i / (float)(rings - 1);
        float y = R.tailTip + t * L;
        float w, hT, hB, cz = z, ex = 2.2f;
        if (y < R.tailBase) {   // tail: taller than wide, tapering
            float k = (y - R.tailTip) / (R.tailBase - R.tailTip);   // 0 tip .. 1 base
            float eW = powf(k, 0.85f), eH = powf(k, sp == SP_GATOR ? 0.55f : 0.8f);   // gator tails: tall, flattened sideways
            w = Lerp(0.004f, R.tailW, eW);
            hT = Lerp(0.006f, R.tailH, eH);
            hB = Lerp(0.004f, R.tailH * 0.8f, eH);
            cz = z + Lerp(-0.02f, 0.f, k) * (L / 3.4f);
            ex = 2.4f;
        } else if (y < R.shoulder) {   // trunk
            float k = (y - R.tailBase) / (R.shoulder - R.tailBase);
            float bul = sinf(kPi * Lerp(0.1f, 0.9f, k));
            w = Lerp(R.tailW, R.bodyW, smooth01(k * 3.f)) * (0.9f + 0.1f * bul);
            if (k > 0.8f) w = Lerp(w, R.headW * 1.3f, (k - 0.8f) / 0.2f);
            hT = Lerp(R.tailH, R.bodyHT, smooth01(k * 2.f));
            hB = Lerp(R.tailH * 0.8f, R.bodyHB, smooth01(k * 2.f));
            ex = 2.6f;
        } else if (y < R.skullBack) {   // neck
            float k = (y - R.shoulder) / (R.skullBack - R.shoulder);
            w = Lerp(R.headW * 1.3f, R.headW * 1.05f, k);
            hT = Lerp(R.bodyHT, R.headH * 1.05f, k);
            hB = Lerp(R.bodyHB, R.headH * 0.55f, k);
            cz = z + Lerp(0.f, R.bodyHT * 0.15f, k);
        } else {   // head: skull -> snout (upper jaw)
            float k = (y - R.skullBack) / (R.snout - R.skullBack);
            float tip = sqrtf(Max(1.f - powf(k, 6.f), 0.f));
            w = Lerp(R.headW, R.snoutW, powf(k, sp == SP_GATOR ? 0.7f : 1.2f)) * tip;
            hT = Lerp(R.headH, R.snoutH, powf(k, 0.8f)) * tip;
            hB = Lerp(R.headH * 0.45f, R.snoutH * 0.4f, k) * tip;
            cz = z + R.bodyHT * 0.15f - k * R.headH * 0.15f;
            ex = sp == SP_GATOR ? 2.4f : 2.1f;
        }
        S[i].c = vec3(0, y, cz);
        S[i].w = w;
        S[i].hT = hT;
        S[i].hB = hB;
        S[i].ex = ex;
    }
    frameSections(S, vec3(0, 0, 1));
    float tailLenU = R.tailBase - R.tailTip;
    loft(mb, S, sides, [&](int, float u, float th, vec3 p, VAttr& a) {
        u8 mat;
        float y = R.tailTip + u;
        a.col = reptPaint(sp, var, RP_BODY, u / L, th, p, mat);
        a.mat = mat;
        // osteoderm rows on the back, double crest on the tail
        float up = sinf(th);
        if (R.scute > 0.f && lod == 0) {
            float along = powf(0.5f + 0.5f * cosf(y * kTwoPi / 0.075f), 3.f);
            if (y > R.tailBase && y < R.skullBack) {
                float rows = 0.f;
                for (int r = -3; r <= 3; r++) rows = Max(rows, gauss1(th, kHalfPi + 0.16f * (float)r, 0.045f));
                a.disp = 0.012f * rows * along * sstep(0.3f, 0.7f, up);
            } else if (y <= R.tailBase) {
                float k = (y - R.tailTip) / tailLenU;
                float crest = Max(gauss1(th, kHalfPi - 0.22f, 0.07f), gauss1(th, kHalfPi + 0.22f, 0.07f));
                if (k < 0.45f) crest = gauss1(th, kHalfPi, 0.1f);
                a.disp = (0.03f * k + 0.008f) * crest * powf(0.5f + 0.5f * cosf(y * kTwoPi / 0.09f), 2.f);
            } else {   // head: bumpy skin, raised eye sockets handled by the eyes
                a.disp = 0.003f * (fbm3(p * 25.f, 9u, 2)) * sstep(0.f, 0.6f, up);
            }
        }
        // each tail bone owns the stretch from its joint towards the tip; the trunk is split pelvis / body / chest
        Chain ch;
        ch.blend = L * 0.02f;
        ch.add(TAIL6, 0.f);
        for (int k = 4; k >= 0; k--) ch.add(TAIL1 + k, sk.bind[TAIL1 + k + 1].y - R.tailTip);
        ch.add(PELVIS, sk.bind[TAIL1].y - R.tailTip);
        ch.add(BODY, R.pelvis * 0.5f - R.tailTip);
        ch.add(CHEST, R.shoulder * 0.7f - R.tailTip);
        ch.add(NECK, R.neck - 0.02f * L - R.tailTip);
        ch.add(HEAD, R.skullBack - 0.01f * L - R.tailTip);
        a.sw = ch.eval(u);
    }, true, false);
    // lower jaw
    {
        vec3 hinge = sk.bind[JAW];
        float jl = R.snout - hinge.y - 0.01f;
        int jr = lod == 0 ? 16 : 6;
        std::vector<Sect> JS((size_t)jr);
        for (int i = 0; i < jr; i++) {
            float t = (float)i / (float)(jr - 1);
            float tip = sqrtf(Max(1.f - powf(t, 6.f), 0.f));
            JS[i].c = hinge + vec3(0, t * jl, -R.headH * 0.05f - t * R.snoutH * 0.1f);
            JS[i].w = Lerp(R.headW * 0.95f, R.snoutW * 0.92f, powf(t, 0.7f)) * tip;
            JS[i].hT = R.snoutH * 0.15f * tip + 0.003f;
            JS[i].hB = Lerp(R.headH * 0.45f, R.snoutH * 0.45f, t) * tip;
            JS[i].ex = 2.4f;
        }
        frameSections(JS, vec3(0, 0, 1));
        loft(mb, JS, lod == 0 ? 14 : 6, [&](int, float u, float th, vec3 p, VAttr& a) {
            u8 mat;
            a.col = reptPaint(sp, var, RP_JAW, u / jl, th, p, mat);
            a.mat = mat;
            a.sw = skin1(JAW);
            if (lod == 0 && sp == SP_GATOR) a.disp = 0.009f * gauss1(sinf(th), 0.62f, 0.14f) * sstep(0.02f, 0.2f, u / jl);   // gum ridges
        }, true, false);
        // teeth along both jaws (gators show them with the mouth closed)
        if (sp == SP_GATOR && lod == 0) {
            for (int jaw = 0; jaw < 2; jaw++)
                for (int sd = -1; sd <= 1; sd += 2)
                    for (int k = 0; k < 11; k++) {
                        float t = 0.18f + 0.075f * (float)k;
                        float y = hinge.y + t * jl;
                        float halfW = Lerp(R.headW * 0.95f, R.snoutW * 0.92f, powf(t, 0.7f)) * 0.93f;
                        vec3 root = vec3(sd * halfW, y, jaw == 0 ? S[0].c.z : hinge.z);
                        float zBase = jaw == 0 ? R.bodyZ + R.bodyHT * 0.15f - t * R.headH * 0.15f - Lerp(R.headH * 0.45f, R.snoutH * 0.4f, t) * 0.6f : hinge.z + 0.002f;
                        root.z = zBase;
                        vec3 dir = jaw == 0 ? vec3(0, 0, -1) : vec3(0, 0, 1);
                        float tl = 0.012f + 0.01f * (k % 3 == 1 ? 1.f : 0.f);
                        std::vector<Sect> TS(3);
                        for (int q = 0; q < 3; q++) TS[q].c = root + dir * (tl * (float)q / 2.f) + vec3(sd * 0.002f, 0, 0);
                        frameSections(TS, vec3(0, 1, 0));
                        for (int q = 0; q < 3; q++) TS[q].w = TS[q].hT = TS[q].hB = Lerp(0.005f, 0.0005f, (float)q / 2.f);
                        loft(mb, TS, 4, [&](int, float, float th, vec3 p, VAttr& a) {
                            u8 mat;
                            a.col = reptPaint(sp, var, RP_TOOTH, 0.f, th, p, mat);
                            a.mat = mat;
                            a.sw = skin1(jaw == 0 ? HEAD : JAW);
                        }, false, false);
                    }
        }
    }
    // eyes on raised sockets
    if (lod == 0 || sp == SP_GATOR) {
        for (int sd = -1; sd <= 1; sd += 2) {
            float y = R.skullBack + (R.snout - R.skullBack) * (sp == SP_GATOR ? 0.12f : 0.3f);
            float k = (y - R.skullBack) / (R.snout - R.skullBack);
            float w = Lerp(R.headW, R.snoutW, powf(k, 0.7f));
            float top = R.bodyZ + R.bodyHT * 0.15f - k * R.headH * 0.15f + Lerp(R.headH, R.snoutH, k) * 0.75f;
            float er = sp == SP_GATOR ? 0.028f : 0.01f;
            vec3 ec = vec3(sd * w * (sp == SP_GATOR ? 0.52f : 0.8f), y, sp == SP_GATOR ? top + er * 0.35f : top - R.headH * 0.3f);
            vec3 axis = sp == SP_GATOR ? normalize(vec3(sd * 0.6f, 0.3f, 0.75f)) : vec3((float)sd, 0.2f, 0.2f);
            ellipsoid(mb, ec, axis, vec3(0, 1, 0), er * 0.9f, er, er * 0.85f, lod == 0 ? 6 : 4, lod == 0 ? 8 : 5, [&](int ring, float, float th, vec3 p, VAttr& a) {
                u8 mat;
                a.col = reptPaint(sp, var, RP_EYE, 1.f - (float)ring / 5.f, th, p, mat);
                a.mat = mat;
                a.sw = skin1(HEAD);
            });
        }
    }
    // iguana dorsal spines and dewlap
    if (R.crest > 0.f && lod == 0) {
        for (int k = 0; k < 26; k++) {
            float y = Lerp(R.skullBack - 0.01f, R.tailBase - 0.25f, (float)k / 25.f);
            float h = 0.028f * (1.f - (float)k / 30.f) * (y > R.pelvis ? 1.f : 0.6f);
            vec3 base = vec3(0, y, R.bodyZ + (y > R.tailBase ? R.bodyHT : R.tailH) * 0.95f);
            std::vector<Sect> CS(3);
            for (int q = 0; q < 3; q++) CS[q].c = base + vec3(0, -h * 0.25f * (float)q / 2.f, h * (float)q / 2.f);
            frameSections(CS, vec3(0, 1, 0));
            for (int q = 0; q < 3; q++) {
                CS[q].w = 0.0015f;
                CS[q].hT = CS[q].hB = Lerp(0.004f, 0.0006f, (float)q / 2.f);
            }
            int bone = y > R.shoulder ? NECK : (y > 0.f ? CHEST : (y > R.pelvis ? BODY : PELVIS));
            loft(mb, CS, 3, [&](int, float, float th, vec3 p, VAttr& a) {
                u8 mat;
                a.col = reptPaint(sp, var, RP_CREST, 0.f, th, p, mat);
                a.mat = mat;
                a.sw = skin1(bone);
            }, false, false);
        }
    }
    if (R.dewlap > 0.f && lod == 0) {
        std::vector<Sect> DS(6);
        for (int q = 0; q < 6; q++) {
            float t = (float)q / 5.f;
            DS[q].c = vec3(0, Lerp(R.skullBack + 0.04f, R.neck - 0.02f, t), R.bodyZ - R.headH * 0.4f);
        }
        frameSections(DS, vec3(1, 0, 0));
        for (int q = 0; q < 6; q++) {
            float t = (float)q / 5.f;
            DS[q].w = 0.002f;
            DS[q].hT = 0.005f;
            DS[q].hB = 0.035f * sinf(kPi * Lerp(0.1f, 0.9f, t));
        }
        loft(mb, DS, 5, [&](int, float, float th, vec3 p, VAttr& a) {
            u8 mat;
            a.col = reptPaint(sp, var, RP_DEWLAP, 0.f, th, p, mat);
            a.mat = mat;
            a.sw = skin1(JAW);
        }, true, true);
    }
    // legs + toes
    for (int leg = 0; leg < 4; leg++) {
        bool front = leg < 2;
        int sd = (leg & 1) ? 1 : -1;
        int b1 = front ? (sd < 0 ? FL1 : FR1) : (sd < 0 ? HL1 : HR1);
        vec3 s = sk.bind[b1], e = sk.bind[b1 + 1], w = sk.bind[b1 + 2], toe = J[b1 + 20];
        vec3 root = s - vec3(sd * R.legX * 0.5f, 0, 0);
        std::vector<vec3> lp = smoothPath({root, s, e, w}, lod == 0 ? 3 : 1);
        float total = 0.f;
        for (size_t i = 1; i < lp.size(); i++) total += length(lp[i] - lp[i - 1]);
        float aS = length(s - root), aE = aS + length(e - s);
        float rTop = front ? R.legR * 1.4f : R.legR * 1.9f;
        std::vector<Sect> LS = sectionsAlong(lp, lod == 0 ? 10 : 4, vec3(0, 0, 1), [&](float t, Sect& sc) {
            float r = Lerp(rTop, R.legR * 0.8f, smooth01(t));
            sc.w = sc.hT = sc.hB = r;
        });
        int parentB = front ? CHEST : PELVIS;
        loft(mb, LS, lod == 0 ? 8 : 4, [&](int, float u, float th, vec3 p, VAttr& a) {
            u8 mat;
            a.col = reptPaint(sp, var, RP_LEG, u / total, th, p, mat);
            a.mat = mat;
            Chain ch;
            ch.blend = total * 0.08f;
            ch.add(parentB, 0.f);
            ch.add(b1, aS * 0.7f);
            ch.add(b1 + 1, aE);
            a.sw = ch.eval(u);
        }, true, true);
        // foot pad + toes fanning forward
        int nt = front ? 5 : 4;
        vec3 fdir = normalize(vec3(toe.x - w.x, toe.y - w.y, 0.f));
        vec3 fside = normalize(cross(fdir, vec3(0, 0, 1)));
        for (int k = 0; k < nt; k++) {
            float f = (float)k / (float)(nt - 1) - 0.5f;
            vec3 d = normalize(fdir + fside * (f * 1.3f));
            float tlen = R.footLen * (0.55f + 0.45f * (1.f - fabsf(f) * 1.4f));
            vec3 base = w + vec3(0, 0, -R.legR * 0.4f);
            vec3 tip = base + d * tlen + vec3(0, 0, -R.legR * 0.35f);
            std::vector<Sect> TS(3);
            TS[0].c = base;
            TS[1].c = lerp(base, tip, 0.6f) + vec3(0, 0, R.legR * 0.15f);
            TS[2].c = tip;
            frameSections(TS, vec3(0, 0, 1));
            for (int q = 0; q < 3; q++) {
                float rr = R.legR * (q == 2 ? 0.12f : (q == 0 ? 0.42f : 0.32f));
                TS[q].w = rr;
                TS[q].hT = TS[q].hB = rr * 0.75f;
            }
            if (lod > 0 && (k & 1)) continue;
            loft(mb, TS, lod == 0 ? 5 : 3, [&](int, float, float th, vec3 p, VAttr& a) {
                u8 mat;
                a.col = reptPaint(sp, var, RP_TOE, 1.f, th, p, mat);
                a.mat = mat;
                a.sw = skin1(b1 + 2);
            }, true, false);
        }
    }
}

void buildReptile(int sp, int var, ModelData& out) {
    using namespace ReptBone;
    ReptSpec R = reptSpec(sp);
    vec3 J[64];
    buildReptSkeleton(R, out.skel, J);
    MBuild m0, m1;
    buildReptMesh(sp, var, R, out.skel, J, 0, m0);
    buildReptMesh(sp, var, R, out.skel, J, 1, m1);
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
    out.legLen = R.bodyZ;
    out.headTip = vec3(0, R.snout, R.bodyZ);
    out.mouth = vec3(0, R.snout - 0.1f * (R.snout - R.skullBack), R.bodyZ);
}

// ==================================================================================================================
// Swimmers: dolphin, manatee (fusiform body + flippers + fluke / paddle), sea turtle, fish (batched shoals)
struct SwimSpec {
    float front, back;           // y of the nose tip / tail end
    float bodyW, bodyHT, bodyHB;
    float headY;                 // where the head (FRONT bone) starts
};

vec3 swimPaint(int sp, int var, int part, float s, float th, vec3 p, u8& mat) {
    mat = MAT_SKIN;
    float up = sinf(th);
    float mott = fbm3(p * 4.f, 777u + (u32)var, 3) * 0.5f + 0.5f;
    if (part == 9) {   // eye
        mat = MAT_EYE;
        if (sp == SP_FISH) return s < 0.25f ? C(0.02f, 0.02f, 0.02f) : (var == 3 ? C(0.8f, 0.8f, 0.82f) : C(0.85f, 0.7f, 0.3f));
        return C(0.03f, 0.03f, 0.03f);
    }
    switch (sp) {
        case SP_DOLPHIN: {
            vec3 dorsal = C(0.33f, 0.36f, 0.41f), flank = C(0.55f, 0.58f, 0.62f), belly = C(0.88f, 0.85f, 0.84f);
            float wave = 0.12f * sinf(p.y * 3.f);
            vec3 c = mixc(flank, dorsal, sstep(0.05f + wave, 0.55f, up));
            c = mixc(c, belly, sstep(-0.25f + wave, -0.65f, up));
            if (part == 1 || part == 2) c = mixc(dorsal, flank, up < 0.f ? 0.4f : 0.f);   // fins darker
            if (part == 0 && p.y > 1.15f && up > -0.2f) c = dorsal;                       // rostrum
            return c * (0.95f + 0.08f * mott);
        }
        case SP_MANATEE: {
            vec3 base = C(0.45f, 0.43f, 0.39f);
            vec3 c = mixc(base, C(0.36f, 0.42f, 0.30f), sstep(0.55f, 0.8f, mott) * sstep(0.f, 0.5f, up));   // algae on the back
            if (n3(p * 9.f, 42u) > 0.62f && up > 0.f) c = C(0.72f, 0.72f, 0.68f);                           // propeller scars
            return c * (0.9f + 0.15f * mott);
        }
        case SP_TURTLE: {
            if (part == 5) {   // carapace
                float cell = worley2(p.x * 7.f, p.y * 6.f, 90u);
                vec3 shell = var == 0 ? C(0.48f, 0.28f, 0.14f) : C(0.36f, 0.31f, 0.18f);
                vec3 edge = var == 0 ? C(0.25f, 0.14f, 0.07f) : C(0.18f, 0.15f, 0.08f);
                vec3 c = mixc(edge, shell, sstep(0.18f, 0.42f, cell));
                if (up < -0.3f) c = C(0.85f, 0.78f, 0.55f);   // plastron
                return c * (0.9f + 0.2f * mott);
            }
            vec3 skin = var == 0 ? C(0.62f, 0.50f, 0.30f) : C(0.52f, 0.52f, 0.42f);
            float sc = worley2(p.x * 40.f, p.y * 40.f, 91u);
            return mixc(C(0.25f, 0.18f, 0.1f), skin, sstep(0.08f, 0.3f, sc)) * (up < -0.3f ? 1.25f : 1.f);
        }
        case SP_FISH: {
            vec3 back, side, belly;
            switch (var) {
                case 0: back = C(0.34f, 0.42f, 0.62f); side = C(0.72f, 0.76f, 0.82f); belly = C(0.92f, 0.92f, 0.9f); break;   // yellowtail snapper
                case 1: back = C(0.62f, 0.65f, 0.40f); side = C(0.85f, 0.85f, 0.62f); belly = C(0.92f, 0.92f, 0.86f); break;  // sergeant major
                case 2: back = C(0.30f, 0.34f, 0.36f); side = C(0.70f, 0.72f, 0.72f); belly = C(0.9f, 0.9f, 0.9f); break;     // mullet
                default: back = C(0.22f, 0.28f, 0.36f); side = C(0.80f, 0.82f, 0.85f); belly = C(0.95f, 0.95f, 0.95f); break; // tarpon
            }
            vec3 c = mixc(side, back, sstep(0.1f, 0.65f, up));
            c = mixc(c, belly, sstep(-0.3f, -0.7f, up));
            if (var == 0 && fabsf(up) < 0.12f && part == 0) c = C(0.95f, 0.8f, 0.2f);   // yellow midline stripe
            if (var == 0 && part == 3) c = C(0.96f, 0.82f, 0.18f);                       // yellow tail
            if (var == 1 && part == 0 && sinf(p.y * 55.f) > 0.55f && up > -0.5f) c = C(0.06f, 0.06f, 0.06f);   // black bars
            if (var == 2 && part == 0 && sinf(up * 14.f) > 0.8f && up > -0.2f) c = c * 0.7f;                     // mullet lines
            if (part == 3 || part == 4) c = var == 0 ? c : mixc(c, back, 0.4f);
            if (var == 3 || var == 2) mat = MAT_CHROME;   // silvery scales
            return c;
        }
        default: return C(0.5f, 0.5f, 0.5f);
    }
}

// Fins and flippers: a flat tapered loft from `root` along `dir`, width axis `wAxis` (for the frame), thickness t.
void fin(MBuild& mb, int sp, int var, int part, vec3 root, vec3 dir, vec3 planeN, float len, float w0, float w1, float t, float sweepBack,
         int bone, int rings, int sides) {
    std::vector<Sect> S((size_t)rings);
    vec3 d = normalize(dir);
    for (int i = 0; i < rings; i++) {
        float k = (float)i / (float)(rings - 1);
        S[i].c = root + d * (len * k) + vec3(0, -sweepBack * k * k, 0);
    }
    frameSections(S, planeN);
    for (int i = 0; i < rings; i++) {
        float k = (float)i / (float)(rings - 1);
        S[i].w = Lerp(w0, w1, k) * (k > 0.8f ? Lerp(1.f, 0.45f, (k - 0.8f) / 0.2f) : 1.f);
        S[i].hT = S[i].hB = t * (1.f - 0.7f * k) + 0.0015f;
        S[i].ex = 1.5f;
    }
    loft(mb, S, sides, [&](int, float, float th, vec3 p, VAttr& a) {
        u8 mat;
        a.col = swimPaint(sp, var, part, 0.f, th, p, mat);
        a.mat = mat;
        a.sw = skin1(bone);
    }, true, true);
}

void buildSwimSkeleton(const SwimSpec& W, Skel& sk) {
    using namespace SwimBone;
    sk = Skel();
    float len = W.front - W.back;
    sk.add(-1, vec3(0, 0, 0));                                     // BODY
    sk.add(BODY, vec3(0, W.headY - len * 0.1f, 0));                // FRONT
    sk.add(FRONT, vec3(0, W.headY, 0));                            // HEAD
    sk.add(BODY, vec3(0, W.back + len * 0.3f, 0));                 // BACK1
    sk.add(BACK1, vec3(0, W.back + len * 0.16f, 0));               // BACK2
    sk.add(BACK2, vec3(0, W.back + len * 0.05f, 0));               // TAILFIN
    sk.add(FRONT, vec3(-W.bodyW * 0.8f, W.headY - len * 0.1f, -W.bodyHB * 0.4f));   // FIN_L
    sk.add(FRONT, vec3(W.bodyW * 0.8f, W.headY - len * 0.1f, -W.bodyHB * 0.4f));    // FIN_R
    sk.add(BACK1, vec3(-W.bodyW * 0.7f, W.back + len * 0.3f, -W.bodyHB * 0.2f));    // FIN_L2
    sk.add(BACK1, vec3(W.bodyW * 0.7f, W.back + len * 0.3f, -W.bodyHB * 0.2f));     // FIN_R2
}

void swimBodyLoft(MBuild& mb, int sp, int var, const SwimSpec& W, const Skel& sk, int rings, int sides,
                  const std::function<void(float t, Sect& s)>& prof, int part) {
    using namespace SwimBone;
    std::vector<Sect> S((size_t)rings);
    for (int i = 0; i < rings; i++) S[i].c = vec3(0, Lerp(W.back, W.front, (float)i / (float)(rings - 1)), 0);
    frameSections(S, vec3(0, 0, 1));
    for (int i = 0; i < rings; i++) prof((float)i / (float)(rings - 1), S[i]);
    float len = W.front - W.back;
    loft(mb, S, sides, [&](int, float u, float th, vec3 p, VAttr& a) {
        u8 mat;
        a.col = swimPaint(sp, var, part, u / len, th, p, mat);
        a.mat = mat;
        Chain ch;
        ch.blend = len * 0.06f;
        ch.add(TAILFIN, 0.f);
        ch.add(BACK2, sk.bind[TAILFIN].y - W.back + len * 0.02f);
        ch.add(BACK1, sk.bind[BACK2].y - W.back);
        ch.add(BODY, sk.bind[BACK1].y - W.back + len * 0.05f);
        ch.add(FRONT, sk.bind[FRONT].y - W.back);
        ch.add(HEAD, sk.bind[HEAD].y - W.back);
        a.sw = ch.eval(u);
    }, true, true);
}

void buildSwimmer(int sp, int var, ModelData& out) {
    using namespace SwimBone;
    SwimSpec W;
    MBuild m[2];
    if (sp == SP_DOLPHIN) {
        W = {1.35f, -1.35f, 0.23f, 0.26f, 0.25f, 0.95f};
        buildSwimSkeleton(W, out.skel);
        for (int lod = 0; lod < 2; lod++) {
            MBuild& mb = m[lod];
            swimBodyLoft(mb, sp, var, W, out.skel, lod == 0 ? 34 : 12, lod == 0 ? 18 : 8, [&](float t, Sect& s) {
                // peduncle (compressed) -> body -> melon -> short rostrum
                float body = powf(Max(sinf(kPi * Lerp(0.02f, 0.93f, t)), 0.f), 0.75f);
                float ped = smooth01(t / 0.35f);
                s.w = W.bodyW * body * Lerp(0.18f, 1.f, ped);
                s.hT = W.bodyHT * body * Lerp(0.55f, 1.f, ped);
                s.hB = W.bodyHB * body * Lerp(0.5f, 1.f, ped);
                if (t > 0.9f) {   // rostrum: the melon drops into a short beak
                    float k = (t - 0.9f) / 0.1f;
                    s.w = Lerp(s.w, 0.035f, smooth01(k * 1.5f)) * sqrtf(Max(1.f - powf(k, 4.f), 0.f));
                    s.hT = Lerp(s.hT, 0.03f, smooth01(k * 1.8f)) * sqrtf(Max(1.f - powf(k, 4.f), 0.f));
                    s.hB = Lerp(s.hB, 0.03f, smooth01(k * 1.2f)) * sqrtf(Max(1.f - powf(k, 4.f), 0.f));
                    s.c.z -= 0.06f * k;
                }
                s.ex = 2.1f;
            }, 0);
            // fluke: horizontal, swept back
            for (int sd = -1; sd <= 1; sd += 2)
                fin(mb, sp, var, 1, out.skel.bind[TAILFIN] + vec3(0, -0.12f, 0), vec3((float)sd, -0.35f, 0), vec3(0, 0, 1), 0.36f, 0.13f, 0.06f, 0.022f, 0.08f,
                    TAILFIN, lod == 0 ? 7 : 3, lod == 0 ? 6 : 4);
            // dorsal fin (vertical, falcate)
            fin(mb, sp, var, 2, vec3(0, -0.05f, W.bodyHT * 0.85f), vec3(0, -0.45f, 1.f), vec3(1, 0, 0), 0.34f, 0.17f, 0.04f, 0.022f, 0.12f, BODY, lod == 0 ? 7 : 3,
                lod == 0 ? 6 : 4);
            // pectoral flippers
            for (int sd = -1; sd <= 1; sd += 2)
                fin(mb, sp, var, 1, vec3(sd * W.bodyW * 0.75f, 0.62f, -W.bodyHB * 0.45f), vec3((float)sd, -0.6f, -0.35f), vec3(0, 0, 1), 0.3f, 0.07f, 0.035f, 0.018f,
                    0.05f, sd < 0 ? FIN_L : FIN_R, lod == 0 ? 6 : 3, lod == 0 ? 6 : 4);
            if (lod == 0)
                for (int sd = -1; sd <= 1; sd += 2)
                    ellipsoid(mb, vec3(sd * 0.105f, 1.02f, 0.02f), vec3((float)sd, 0.2f, 0), vec3(0, 0, 1), 0.009f, 0.011f, 0.008f, 5, 6, [&](int ring, float, float th, vec3 p, VAttr& a) {
                        u8 mat;
                        a.col = swimPaint(sp, var, 9, 1.f - (float)ring / 4.f, th, p, mat);
                        a.mat = mat;
                        a.sw = skin1(HEAD);
                    });
        }
    } else if (sp == SP_MANATEE) {
        W = {1.25f, -1.45f, 0.42f, 0.36f, 0.40f, 0.85f};
        buildSwimSkeleton(W, out.skel);
        for (int lod = 0; lod < 2; lod++) {
            MBuild& mb = m[lod];
            swimBodyLoft(mb, sp, var, W, out.skel, lod == 0 ? 30 : 12, lod == 0 ? 18 : 8, [&](float t, Sect& s) {
                float body = powf(Max(sinf(kPi * Lerp(0.03f, 0.97f, t)), 0.f), 0.55f);
                s.w = W.bodyW * body * Lerp(0.45f, 1.f, smooth01(t / 0.3f));
                s.hT = W.bodyHT * body * Lerp(0.35f, 1.f, smooth01(t / 0.3f));
                s.hB = W.bodyHB * body * Lerp(0.35f, 1.f, smooth01(t / 0.3f));
                if (t > 0.72f) {   // neck narrows, big blunt muzzle
                    float k = (t - 0.72f) / 0.28f;
                    float neck = 1.f - 0.35f * gauss1(k, 0.3f, 0.2f);
                    s.w *= neck;
                    s.hT *= neck * Lerp(1.f, 0.8f, k);
                    s.hB *= neck;
                    s.c.z -= 0.08f * k;
                }
                s.ex = 2.2f;
            }, 0);
            // round paddle tail
            {
                std::vector<Sect> PS(lod == 0 ? 8 : 4);
                int n = (int)PS.size();
                for (int i = 0; i < n; i++) PS[i].c = vec3(0, W.back + 0.05f - 0.55f * (float)i / (float)(n - 1), -0.02f);
                frameSections(PS, vec3(0, 0, 1));
                for (int i = 0; i < n; i++) {
                    float k = (float)i / (float)(n - 1);
                    PS[i].w = 0.12f + 0.32f * sqrtf(Max(sinf(kPi * Lerp(0.05f, 1.f, k)), 0.f)) * (k > 0.85f ? Lerp(1.f, 0.3f, (k - 0.85f) / 0.15f) : 1.f);
                    PS[i].hT = PS[i].hB = 0.05f * (1.f - k) + 0.012f;
                    PS[i].ex = 1.8f;
                }
                loft(mb, PS, lod == 0 ? 10 : 5, [&](int, float, float th, vec3 p, VAttr& a) {
                    u8 mat;
                    a.col = swimPaint(sp, var, 3, 0.f, th, p, mat);
                    a.mat = mat;
                    a.sw = skin1(TAILFIN);
                }, true, true);
            }
            for (int sd = -1; sd <= 1; sd += 2)
                fin(mb, sp, var, 1, vec3(sd * W.bodyW * 0.7f, 0.55f, -W.bodyHB * 0.5f), vec3((float)sd, 0.25f, -0.6f), vec3(0, 0, 1), 0.34f, 0.08f, 0.07f, 0.03f,
                    0.f, sd < 0 ? FIN_L : FIN_R, lod == 0 ? 6 : 3, lod == 0 ? 6 : 4);
            if (lod == 0)
                for (int sd = -1; sd <= 1; sd += 2)
                    ellipsoid(mb, vec3(sd * 0.16f, 1.02f, 0.05f), vec3((float)sd, 0.3f, 0), vec3(0, 0, 1), 0.007f, 0.008f, 0.006f, 5, 6, [&](int ring, float, float th, vec3 p, VAttr& a) {
                        u8 mat;
                        a.col = swimPaint(sp, var, 9, 1.f - (float)ring / 4.f, th, p, mat);
                        a.mat = mat;
                        a.sw = skin1(HEAD);
                    });
        }
    } else if (sp == SP_TURTLE) {
        W = {0.62f, -0.5f, 0.42f, 0.2f, 0.07f, 0.45f};
        buildSwimSkeleton(W, out.skel);
        for (int lod = 0; lod < 2; lod++) {
            MBuild& mb = m[lod];
            // carapace dome with a flat plastron
            {
                int n = lod == 0 ? 18 : 8;
                std::vector<Sect> S((size_t)n);
                for (int i = 0; i < n; i++) S[i].c = vec3(0, Lerp(-0.48f, 0.45f, (float)i / (float)(n - 1)), 0);
                frameSections(S, vec3(0, 0, 1));
                for (int i = 0; i < n; i++) {
                    float t = (float)i / (float)(n - 1);
                    float e = powf(Max(sinf(kPi * Lerp(0.02f, 0.98f, t)), 0.f), 0.5f) * (t < 0.35f ? Lerp(0.75f, 1.f, t / 0.35f) : 1.f);
                    S[i].w = W.bodyW * e;
                    S[i].hT = W.bodyHT * e;
                    S[i].hB = W.bodyHB * e;
                    S[i].ex = 2.3f;
                }
                loft(mb, S, lod == 0 ? 20 : 8, [&](int, float, float th, vec3 p, VAttr& a) {
                    u8 mat;
                    a.col = swimPaint(sp, var, 5, 0.f, th, p, mat);
                    a.mat = mat;
                    a.sw = skin1(BODY);
                }, true, true);
            }
            // head and neck
            {
                std::vector<vec3> hp = {vec3(0, 0.3f, 0.0f), vec3(0, 0.47f, 0.02f), vec3(0, 0.6f, 0.02f), vec3(0, 0.66f, 0.0f)};
                std::vector<Sect> S = sectionsAlong(smoothPath(hp, lod == 0 ? 3 : 1), lod == 0 ? 9 : 4, vec3(0, 0, 1), [&](float t, Sect& s) {
                    float r = Lerp(0.075f, 0.085f, gauss1(t, 0.72f, 0.2f)) * sqrtf(Max(1.f - powf(t, 6.f), 0.f));
                    s.w = r;
                    s.hT = r * 0.85f;
                    s.hB = r * 0.7f;
                });
                loft(mb, S, lod == 0 ? 10 : 5, [&](int, float u, float th, vec3 p, VAttr& a) {
                    u8 mat;
                    a.col = swimPaint(sp, var, 0, 0.f, th, p, mat);
                    a.mat = mat;
                    a.sw = u < 0.12f ? skin2(FRONT, HEAD, u / 0.12f) : skin1(HEAD);
                }, true, false);
                if (lod == 0)
                    for (int sd = -1; sd <= 1; sd += 2)
                        ellipsoid(mb, vec3(sd * 0.055f, 0.6f, 0.035f), vec3((float)sd, 0.3f, 0.1f), vec3(0, 0, 1), 0.01f, 0.012f, 0.01f, 5, 6,
                                  [&](int ring, float, float th, vec3 p, VAttr& a) {
                                      u8 mat;
                                      a.col = swimPaint(sp, var, 9, 1.f - (float)ring / 4.f, th, p, mat);
                                      a.mat = mat;
                                      a.sw = skin1(HEAD);
                                  });
            }
            // front flippers (long, wing-like) and rear flippers
            for (int sd = -1; sd <= 1; sd += 2) {
                fin(mb, sp, var, 0, vec3(sd * 0.3f, 0.25f, -0.02f), vec3((float)sd, -0.2f, 0), vec3(0, 0, 1), 0.46f, 0.1f, 0.05f, 0.022f, 0.18f,
                    sd < 0 ? FIN_L : FIN_R, lod == 0 ? 7 : 3, lod == 0 ? 6 : 4);
                fin(mb, sp, var, 0, vec3(sd * 0.25f, -0.38f, -0.02f), vec3((float)sd * 0.6f, -1.f, 0), vec3(0, 0, 1), 0.2f, 0.07f, 0.05f, 0.02f, 0.02f,
                    sd < 0 ? FIN_L2 : FIN_R2, lod == 0 ? 5 : 3, lod == 0 ? 6 : 4);
            }
        }
    } else {   // fish
        float len = var == 3 ? 1.4f : (var == 2 ? 0.4f : (var == 1 ? 0.2f : 0.36f));
        float deep = var == 1 ? 0.62f : (var == 2 ? 0.26f : (var == 3 ? 0.27f : 0.3f));   // depth / length
        W = {len * 0.5f, -len * 0.38f, len * 0.07f * (var == 2 ? 1.25f : 1.f), len * deep * 0.52f, len * deep * 0.48f, len * 0.3f};
        buildSwimSkeleton(W, out.skel);
        for (int lod = 0; lod < 2; lod++) {
            MBuild& mb = m[lod];
            swimBodyLoft(mb, sp, var, W, out.skel, lod == 0 ? 16 : 6, lod == 0 ? 12 : 5, [&](float t, Sect& s) {
                float body = powf(Max(sinf(kPi * Lerp(0.02f, 0.97f, t)), 0.f), 0.7f);
                float ped = smooth01(t / 0.3f);
                s.w = W.bodyW * body * Lerp(0.3f, 1.f, ped);
                s.hT = W.bodyHT * body * Lerp(0.3f, 1.f, ped);
                s.hB = W.bodyHB * body * Lerp(0.3f, 1.f, ped);
                s.ex = 2.2f;
            }, 0);
            // forked caudal fin: two lobes
            vec3 tb = vec3(0, W.back + len * 0.02f, 0);
            for (int lobe = -1; lobe <= 1; lobe += 2)
                fin(mb, sp, var, 3, tb, vec3(0, -1.f, 0.85f * (float)lobe), vec3(1, 0, 0), len * 0.24f, len * 0.05f, len * 0.025f, len * 0.006f, 0.f, TAILFIN,
                    lod == 0 ? 4 : 2, lod == 0 ? 4 : 3);
            if (lod == 0) {
                fin(mb, sp, var, 4, vec3(0, len * 0.05f, W.bodyHT * 0.85f), vec3(0, -0.8f, 0.6f), vec3(1, 0, 0), len * 0.2f, len * 0.07f, len * 0.02f, len * 0.004f,
                    0.f, BODY, 3, 4);   // dorsal
                fin(mb, sp, var, 4, vec3(0, -len * 0.12f, -W.bodyHB * 0.8f), vec3(0, -0.7f, -0.6f), vec3(1, 0, 0), len * 0.1f, len * 0.04f, len * 0.015f,
                    len * 0.004f, 0.f, BACK1, 3, 4);   // anal
                for (int sd = -1; sd <= 1; sd += 2) {
                    fin(mb, sp, var, 4, vec3(sd * W.bodyW * 0.8f, len * 0.26f, -W.bodyHB * 0.2f), vec3((float)sd, -1.2f, -0.3f), vec3(0, 0, 1), len * 0.11f,
                        len * 0.03f, len * 0.015f, len * 0.003f, 0.f, sd < 0 ? FIN_L : FIN_R, 3, 4);
                    ellipsoid(mb, vec3(sd * W.bodyW * 0.62f, len * 0.4f, W.bodyHT * 0.25f), vec3((float)sd, 0.1f, 0), vec3(0, 0, 1), len * 0.018f, len * 0.028f,
                              len * 0.028f, 5, 6, [&](int ring, float, float th, vec3 p, VAttr& a) {
                                  u8 mat;
                                  a.col = swimPaint(sp, var, 9, 1.f - (float)ring / 4.f, th, p, mat);
                                  a.mat = mat;
                                  a.sw = skin1(HEAD);
                              });
                }
            }
        }
        // fish are drawn in batches: 4 bones per fish (head, body, rear body, tail fin)
        static const int kFishMap[SwimBone::COUNT] = {1, 0, 0, 1, 2, 3, 1, 1, 1, 1};
        int map[kMaxBones];
        for (int i = 0; i < kMaxBones; i++) map[i] = i < SwimBone::COUNT ? kFishMap[i] : 1;
        m[0].remapBones(map);
        m[1].remapBones(map);
        out.batchN = 4;
        const int fb[4] = {FRONT, BODY, BACK2, TAILFIN};
        for (int i = 0; i < 4; i++) out.batchBones[i] = fb[i];
        out.batchCap = 64;
        emitBatch(m[0], 4, out.batchCap, out.batch[0]);
        emitBatch(m[1], 4, out.batchCap, out.batch[1]);
        out.headTip = vec3(0, W.front, 0);
        return;
    }
    m[0].emit(out.lod[0]);
    m[1].emit(out.lod[1]);
    out.headTip = vec3(0, W.front, 0);
    out.mouth = out.headTip;
}

}  // namespace fauna_detail

// ==================================================================================================================
// Procedural animation
namespace fauna_detail {

// Rotation (local to the parent frame) that aims a bone's bind direction at `want`, minimal twist.
inline quat aimBone(quat parentW, vec3 bindDir, vec3 want, quat& worldOut) {
    vec3 cur = rotate(parentW, bindDir);
    if (length2(cur) < 1e-12f || length2(want) < 1e-12f) {
        worldOut = parentW;
        return quat();
    }
    quat w = normalize(quatFromTo(normalize(cur), normalize(want)) * parentW);
    worldOut = w;
    return normalize(conj(parentW) * w);
}

// Knee position of a two-bone chain from A towards T (clamped to reach), bending towards `pole`.
inline vec3 ikKnee(vec3 A, vec3& T, float l1, float l2, vec3 pole) {
    vec3 d = T - A;
    float dist = length(d);
    vec3 dir = dist > 1e-6f ? d / dist : vec3(0, 0, -1);
    float maxR = (l1 + l2) * 0.998f, minR = fabsf(l1 - l2) * 1.02f + 1e-4f;
    dist = Clamp(dist, minR, maxR);
    T = A + dir * dist;
    float cosA = Clamp((l1 * l1 + dist * dist - l2 * l2) / (2.f * l1 * dist), -1.f, 1.f);
    float sinA = sqrtf(Max(0.f, 1.f - cosA * cosA));
    vec3 pn = pole - dir * dot(pole, dir);
    if (length2(pn) < 1e-10f) pn = anyPerp(dir);
    pn = normalize(pn);
    return A + (dir * cosA + pn * sinA) * l1;
}

// Three-segment leg: the last segment points along d3, the first two solve a two-bone IK to its root.
void legIK3(const Skel& sk, Pose& P, const Frames& F, int b1, int b2, int b3, vec3 legEnd, vec3 target, vec3 d3, vec3 pole) {
    int par = sk.parent[b1];
    quat pr = F.r[par];
    float g = F.g[par];
    vec3 A = F.p[par] + rotate(pr, (sk.bind[b1] - sk.bind[par]) * g);
    float l1 = length(sk.bind[b2] - sk.bind[b1]) * g, l2 = length(sk.bind[b3] - sk.bind[b2]) * g, l3 = length(legEnd) * g;
    vec3 Cj = target - normalize(d3) * l3;
    vec3 B = ikKnee(A, Cj, l1, l2, pole);
    quat w1, w2, w3;
    P.q[b1] = aimBone(pr, sk.bind[b2] - sk.bind[b1], B - A, w1);
    P.q[b2] = aimBone(w1, sk.bind[b3] - sk.bind[b2], Cj - B, w2);
    P.q[b3] = aimBone(w2, legEnd, d3, w3);
}

inline float fracf(float x) { return x - floorf(x); }

// Rotation about a pivot expressed as root offset + root rotation (bone 0 rotates about its own joint).
inline void rotateRootAbout(const Skel& sk, Pose& P, vec3 pivot, quat q) {
    vec3 b0 = sk.bind[0] + P.rootPos;
    vec3 nb = pivot + rotate(q, b0 - pivot);
    P.rootPos += nb - b0;
    P.rootRot = normalize(q * P.rootRot);
}

}  // namespace fauna_detail

// ---- birds --------------------------------------------------------------------------------------------------------
void animateBird(const ModelData& m, const BirdAnim& a, Pose& P) {
    using namespace BirdBone;
    const Skel& sk = m.skel;
    const BirdSpec& B = birdSpec(m.species);
    P.reset(sk.n);
    float fold = Saturate(a.fold), spread = 1.f - fold;
    float dead = Saturate(a.dead);
    bool wader = B.tibia > 0.1f;
    bool bigBird = B.span > 1.2f;
    // ---- spread wings: stroke with lagging segments, fold of the hand on the upstroke
    float amp = a.flapAmp * (1.f - dead);
    float ph = a.flap;
    float A1 = (bigBird ? 0.5f : 0.62f) * amp, A2 = 0.22f * amp, A3 = (bigBird ? 0.38f : 0.5f) * amp;
    float soar = Saturate(a.soar);
    float dihedral = Lerp(0.07f, 0.3f, soar) - 0.35f * dead;
    float handDroop = Lerp(-0.14f, 0.06f, soar) - 0.3f * dead;
    float upstroke = Max(0.f, cosf(ph));
    for (int sd = -1; sd <= 1; sd += 2) {
        int w1 = sd < 0 ? WL1 : WR1, w2 = sd < 0 ? WL2 : WR2, w3 = sd < 0 ? WL3 : WR3;
        float s = -(float)sd;   // +1 left: elevation = qy(+e) for the left wing, qy(-e) for the right
        float e1 = dihedral + A1 * sinf(ph);
        float e2 = A2 * sinf(ph - 0.5f);
        float e3 = handDroop + A3 * sinf(ph - 1.0f);
        float sweep1 = 0.18f * upstroke * amp + 0.95f * Saturate(a.dive) - 0.35f * Saturate(a.flare);
        float sweep2 = -0.25f * upstroke * amp - 0.9f * Saturate(a.dive);
        float sweep3 = 0.55f * Max(0.f, cosf(ph - 0.3f)) * amp + 0.85f * Saturate(a.dive) + 0.2f * dead;
        float twist = -0.22f * cosf(ph) * amp - 0.25f * Saturate(a.flare);
        // qz sweeps: left wing backwards = +, right wing backwards = -
        P.q[w1] = qz(s * sweep1) * qy(s * (e1 + 0.3f * Saturate(a.dive) + 0.3f * Saturate(a.flare))) * qx(twist);
        P.q[w2] = qz(s * sweep2) * qy(s * e2);
        P.q[w3] = qz(s * sweep3) * qy(s * e3) * qx(twist * 0.6f);
        // collapse into the shoulder when folded (the folded wing takes over)
        P.g[w1] = Max(sstep(0.f, 0.75f, spread), 0.0f);
    }
    P.g[FOLDL] = P.g[FOLDR] = sstep(0.25f, 1.f, fold);
    // ---- tail: fans when braking / landing, pitches with the stroke
    P.s[TAIL] = vec3(1.f + 0.9f * Saturate(a.tail), 1.f, 1.f);
    P.q[TAIL] = qx(-0.25f * Saturate(a.tail) + 0.05f * amp * sinf(ph + 1.f) + 0.25f * dead);
    // ---- neck and head
    float ext = Clamp(a.neck, -1.f, 1.f);
    float retract = Max(0.f, -ext), stretch = Max(0.f, ext);
    float peck = Saturate(a.peck);
    float nS = B.neckS;
    // straighten the S (strike) or fold it back onto the shoulders (flight retraction)
    float n1 = -stretch * (nS * 0.9f + 0.35f) + retract * (0.9f + nS * 0.5f) - peck * 0.95f;
    float n2 = stretch * nS * 0.8f - retract * (1.6f + nS) - peck * 0.5f;
    float n3 = -stretch * nS * 0.4f + retract * (1.0f + nS * 0.5f) - peck * 0.2f;
    float hp = -retract * 0.35f + a.headPitch + peck * 0.4f;
    // pigeon head bob: neck thrusts forward then holds while walking
    float bob = a.walkAmt * (m.species == SP_PIGEON ? 1.f : 0.3f) * (fracf(a.walk / kTwoPi * 2.f) < 0.35f ? -0.25f : 0.12f);
    float hy = Clamp(a.headYaw, -1.9f, 1.9f);
    P.q[NECK1] = qz(hy * 0.15f) * qx(n1 + bob * 0.5f + 0.35f * dead);
    P.q[NECK2] = qz(hy * 0.25f) * qx(n2 - bob);
    P.q[NECK3] = qz(hy * 0.25f) * qx(n3 + bob * 0.5f - 0.5f * dead);
    P.q[HEAD] = qz(hy * 0.35f) * qx(hp - 0.3f * dead);
    P.q[JAW] = qx(-Saturate(a.mouth) * (m.species == SP_PELICAN ? 0.55f : 0.45f));
    if (m.species == SP_PELICAN) P.s[JAW] = vec3(1.f, 1.f, 1.f + 0.8f * Saturate(a.mouth));
    // ---- legs
    float legs = Saturate(a.legs) * (1.f - dead);
    float sit = Saturate(a.sit);
    float stance = B.stance * legs * (1.f - sit);
    float bodyPitch = stance + 0.9f * Saturate(a.flare) - 0.25f * peck * legs;
    P.rootRot = qx(bodyPitch);
    for (int sd = -1; sd <= 1; sd += 2) {
        int l1 = sd < 0 ? LL1 : LR1, l2 = sd < 0 ? LL2 : LR2, l3 = sd < 0 ? LL3 : LR3;
        float side = sd < 0 ? 0.f : kPi;
        float w = a.walkAmt * legs;
        float swing = sinf(a.walk + side);
        float lift = Max(0.f, cosf(a.walk + side));
        // tucked in flight: waders trail their legs straight behind, the others pull the feet up under the tail
        float tuckA = wader ? -1.5f : -1.2f, tuckB = wader ? -0.32f : 1.9f;
        float stepA = -bodyPitch + w * (wader ? 0.4f : 0.5f) * swing;
        float stepB = w * (wader ? 1.1f : 0.7f) * lift;
        float sitA = 0.6f, sitB = -2.3f;
        float aA = Lerp(Lerp(tuckA, stepA, legs), sitA, sit * legs) + 0.4f * Saturate(a.flare);
        float aB = Lerp(Lerp(tuckB, stepB, legs), sitB, sit * legs);
        P.q[l1] = qx(aA);
        P.q[l2] = qx(aB);
        P.q[l3] = qx(-aA - aB + (1.f - legs) * (wader ? 0.f : 1.2f));   // keep the foot flat on the ground
        if (dead > 0.f) P.q[l2] = qx(Lerp(aB, 0.6f, dead));
    }
    // standing: shift the body so the feet stay on the ground reference
    if (legs > 0.01f) {
        Frames F;
        poseSkeleton(sk, P, nullptr, &F);
        float footZ = Min(F.p[LL3].z, F.p[LR3].z);
        float bindZ = sk.bind[LL3].z;
        P.rootPos.z += (bindZ - footZ) * legs;
        P.rootPos.z -= sit * legs * (m.legLen - B.bodyHB * 0.8f);
        P.rootPos.z += 0.012f * a.walkAmt * legs * fabsf(sinf(a.walk)) * (B.bodyLen / 0.3f);
    }
}

// ---- quadrupeds ---------------------------------------------------------------------------------------------------
namespace fauna_detail {
struct GaitMix {
    float off[4];
    float duty, lift, bob, flex;
    float a, b;   // stride = h * (a + b * froude speed)
};
GaitMix gaitMix(float g) {
    static const float kOff[4][4] = {{0.25f, 0.75f, 0.f, 0.5f}, {0.f, 0.5f, 0.5f, 0.f}, {0.6f, 0.3f, 0.3f, 0.f}, {0.55f, 0.45f, 0.f, 0.1f}};
    static const float kDuty[4] = {0.66f, 0.5f, 0.4f, 0.34f};
    static const float kLift[4] = {0.10f, 0.16f, 0.2f, 0.24f};
    static const float kBob[4] = {0.012f, 0.025f, 0.04f, 0.05f};
    static const float kFlex[4] = {0.f, 0.02f, 0.08f, 0.14f};
    static const float kA[4] = {0.9f, 1.f, 1.2f, 1.5f};
    static const float kB[4] = {0.6f, 0.6f, 0.9f, 1.0f};
    g = Clamp(g, 0.f, 3.f);
    int g0 = Min((int)floorf(g), 3), g1 = Min(g0 + 1, 3);
    float f = g - (float)g0;
    GaitMix m;
    for (int i = 0; i < 4; i++) {
        // blend phase offsets on the circle
        float d = kOff[g1][i] - kOff[g0][i];
        if (d > 0.5f) d -= 1.f;
        if (d < -0.5f) d += 1.f;
        m.off[i] = fracf(kOff[g0][i] + d * f);
    }
    m.duty = Lerp(kDuty[g0], kDuty[g1], f);
    m.lift = Lerp(kLift[g0], kLift[g1], f);
    m.bob = Lerp(kBob[g0], kBob[g1], f);
    m.flex = Lerp(kFlex[g0], kFlex[g1], f);
    m.a = Lerp(kA[g0], kA[g1], f);
    m.b = Lerp(kB[g0], kB[g1], f);
    return m;
}
}  // namespace fauna_detail

float quadCycleRate(const ModelData& m, float speed, float gait) {
    QuadSpec Q = quadSpec(m.species, m.variant);
    float h = (Q.withers + Q.hipH) * 0.5f;
    GaitMix gm = gaitMix(gait);
    float fr = speed / sqrtf(9.81f * h);
    float stride = h * (gm.a + gm.b * fr);
    return speed > 0.01f ? speed / Max(stride, 0.05f) : 0.f;
}

void animateQuad(const ModelData& m, const QuadAnim& a, Pose& P, const float* footGround) {
    using namespace QuadBone;
    const Skel& sk = m.skel;
    P.reset(sk.n);
    QuadSpec Q = quadSpec(m.species, m.variant);
    float W = Q.withers, H = Q.hipH, h = (W + H) * 0.5f;
    float dead = Saturate(a.dead), sit = Saturate(a.sit) * (1.f - dead), lie = Saturate(a.lie) * (1.f - dead);
    float crouch = Saturate(a.crouch) * (1.f - dead), rear = Saturate(a.rear) * (1.f - dead);
    GaitMix gm = gaitMix(a.gait);
    float moving = sstep(0.05f, 0.4f, a.speed) * (1.f - sit) * (1.f - lie) * (1.f - dead);
    float fr = a.speed / sqrtf(9.81f * h);
    float stride = h * (gm.a + gm.b * fr);
    float stroke = stride * gm.duty;
    float ph = a.phase;
    bool small = m.species == SP_DOG || m.species == SP_CAT || m.species == SP_RACCOON;
    float flexK = (m.species == SP_HORSE || m.species == SP_COW) ? 0.3f : (m.species == SP_DEER ? 0.7f : 1.f);
    // ---- body motion
    float bob = gm.bob * W * moving * (a.gait > 2.2f ? sinf(kTwoPi * ph) : cosf(2.f * kTwoPi * ph));
    float flex = gm.flex * flexK * moving * sinf(kTwoPi * (ph - 0.1f));
    float pitch = (a.gait > 2.2f ? 0.05f * sinf(kTwoPi * (ph + 0.15f)) : 0.f) * moving;
    float turn = Clamp(a.turn, -2.5f, 2.5f);
    P.q[CHEST] = qz(turn * 0.08f) * qx(flex);
    P.q[PELVIS] = qz(-turn * 0.06f) * qx(-flex);
    P.q[BODY] = qy(-turn * 0.03f * Saturate(a.speed / 3.f));   // lean into turns
    P.rootPos = vec3(0, 0, bob - crouch * 0.22f * h);
    P.rootRot = qx(pitch - crouch * 0.05f);
    // postures
    if (sit > 0.f) {   // dogs/cats: hips down, chest up
        vec3 pivot = vec3(0, sk.bind[FL1].y, 0.f);
        rotateRootAbout(sk, P, pivot, qx(0.62f * sit));
        P.rootPos.z -= 0.06f * h * sit;
    }
    if (lie > 0.f) {
        float drop = (sk.bind[BODY].z - Q.depth * 0.95f) * lie;
        P.rootPos.z -= drop;
    }
    if (rear > 0.f) rotateRootAbout(sk, P, vec3(0, sk.bind[HL1].y, 0.f), qx(0.75f * rear));
    if (dead > 0.f) {
        float rollDir = 1.f;
        P.rootRot = normalize(slerp(P.rootRot, qy(1.5f * rollDir), dead));
        P.rootPos.z = Lerp(P.rootPos.z, -(sk.bind[BODY].z - Q.bodyW * 1.05f), dead);
    }
    // ---- neck, head, look, grazing
    float hd = Saturate(a.headDown) * (1.f - dead);
    float longNeck = m.species == SP_HORSE || m.species == SP_COW || m.species == SP_DEER ? 1.f : 0.f;
    float nod = (longNeck > 0.f ? 0.05f : 0.02f) * moving * sinf(2.f * kTwoPi * (ph + 0.1f)) * (a.gait < 1.5f ? 1.f : 0.3f);
    float alertUp = 0.22f * Saturate(a.alert);
    float ly = Clamp(a.lookYaw, -1.6f, 1.6f), lp = Clamp(a.lookPitch, -0.8f, 0.8f);
    float n1 = -hd * (longNeck > 0.f ? 1.05f : 0.75f) + alertUp + nod - 0.25f * crouch - (a.gait > 2.2f ? 0.18f : 0.f) * moving + sit * 0.25f;
    P.q[NECK1] = qz(ly * 0.25f) * qx(n1 - 0.4f * dead);
    P.q[NECK2] = qz(ly * 0.35f) * qx(-hd * 0.35f + alertUp * 0.3f - 0.3f * dead);
    P.q[HEAD] = qz(ly * 0.4f) * qx(lp - hd * 0.25f + (longNeck > 0.f ? hd * 0.2f : 0.f) - 0.2f * dead);
    float maxJaw = m.species == SP_CAT ? 0.6f : (m.species == SP_DOG ? 0.5f : (m.species == SP_RACCOON ? 0.45f : 0.3f));
    P.q[JAW] = qx(-Saturate(a.mouth) * maxJaw);
    // ears: erect ears prick forward when alert; floppy ones swing with the gait
    for (int sd = -1; sd <= 1; sd += 2) {
        int eb = sd < 0 ? EAR_L : EAR_R;
        float sw = Q.earFlop > 0.5f ? 0.15f * moving * sinf(kTwoPi * 2.f * ph + (float)sd) : 0.f;
        float perk = Q.earFlop > 0.5f ? 0.f : 0.2f * Saturate(a.alert) - 0.5f * crouch;
        P.q[eb] = qx(perk + sw);
    }
    // ---- tail: wag / swish / flag
    {
        float wag = Saturate(a.tailWag);
        float t = a.t;
        float sw = 0.f, up = 0.f;
        if (m.species == SP_DOG) {
            sw = wag * 0.55f * sinf(t * 17.f);
            up = 0.35f * Saturate(a.alert) - 0.7f * crouch + 0.2f * moving * (a.gait > 1.5f ? 1.f : 0.f);
        } else if (m.species == SP_CAT) {
            sw = 0.25f * sinf(t * 1.3f) + wag * 0.4f * sinf(t * 6.f);
            up = 0.4f * moving - 0.5f * crouch;
        } else if (m.species == SP_HORSE || m.species == SP_COW) {
            sw = 0.22f * sinf(t * 0.9f + 1.f) + 0.12f * sinf(t * 2.3f) + wag * 0.5f * sinf(t * 5.f);
            up = 0.25f * moving * (a.gait > 1.5f ? 1.f : 0.f);
        } else if (m.species == SP_DEER) {
            sw = 0.1f * sinf(t * 3.f);
            up = 1.1f * Saturate(a.alert * 0.4f + moving * (a.gait > 1.5f ? 1.f : 0.f));   // white flag up when fleeing
        } else {
            sw = 0.15f * sinf(t * 1.5f);
            up = -0.3f * crouch;
        }
        if (dead > 0.f) {
            sw *= 1.f - dead;
            up = Lerp(up, -0.3f, dead);
        }
        P.q[TAIL1] = qz(sw * 0.5f) * qx(up);
        P.q[TAIL2] = qz(sw * 0.7f + 0.2f * sinf(a.t * 17.f - 1.f) * wag * (m.species == SP_DOG ? 1.f : 0.f)) * qx(up * 0.3f);
        P.q[TAIL3] = qz(sw * 0.8f) * qx(up * 0.2f);
    }
    // ---- legs
    Frames F;
    poseSkeleton(sk, P, nullptr, &F);
    for (int leg = 0; leg < 4; leg++) {
        bool front = leg < 2;
        int sd = (leg & 1) ? 1 : -1;
        int b1 = m.legBone[leg][0], b2 = m.legBone[leg][1], b3 = m.legBone[leg][2];
        vec3 rest = sk.bind[b3] + m.legEnd[leg];   // toe contact in the bind pose
        vec3 target = rest;
        vec3 d3 = normalize(m.legEnd[leg]);
        vec3 pole = front ? vec3(0, -1, 0) : vec3(0, 1, 0);
        if (dead > 0.f) {
            // limp: legs relaxed, slightly bent
            P.q[b1] = qx((front ? 0.25f : -0.3f) * dead + 0.1f * (float)sd);
            P.q[b2] = qx((front ? -0.4f : 0.5f) * dead);
            P.q[b3] = qx((front ? 0.6f : -0.4f) * dead);
            continue;
        }
        // gait cycle
        float p = fracf(ph - gm.off[leg] + 1.f);
        float y = 0.f, z = 0.f;
        if (p < gm.duty) {
            float s = p / gm.duty;
            y = stroke * (0.5f - s);
        } else {
            float s = (p - gm.duty) / (1.f - gm.duty);
            y = stroke * (-0.5f + smooth01(s));
            z = gm.lift * h * sinf(kPi * s) * (front ? 1.1f : 0.9f);
            // the lower segment flips back during the swing (carpus / fetlock flexion)
            float flexS = sinf(kPi * s) * moving;
            if (front) d3 = normalize(d3 + vec3(0, -1.6f, 0.9f) * flexS);
            else d3 = normalize(d3 + vec3(0, -0.6f, 0.2f) * flexS);
        }
        target = rest + vec3(0, y * moving, z * moving);
        if (footGround) target.z += footGround[leg];
        // postures
        if (sit > 0.f && !front) {
            target = lerp(target, rest + vec3(0, Q.bodyLen * 0.28f, 0.f), sit);
            d3 = normalize(lerp(d3, vec3(0, 1.f, -0.12f), sit));
        }
        if (sit > 0.f && front) target = lerp(target, rest + vec3(0, -0.02f * h, 0.f), sit);
        if (lie > 0.f) {
            if (front && small) {   // sphinx: forelegs forward
                target = lerp(target, rest + vec3(0, 0.3f * h, 0.f), lie);
                d3 = normalize(lerp(d3, vec3(0, 1.f, -0.1f), lie));
            } else {   // folded underneath
                target = lerp(target, rest + vec3((float)sd * 0.05f * h, front ? -0.1f * h : 0.15f * h, 0.f), lie);
                d3 = normalize(lerp(d3, front ? vec3(0, -1.f, -0.15f) : vec3(0, 1.f, -0.1f), lie));
            }
        }
        if (rear > 0.f && front) {
            target = lerp(target, F.p[b1] + vec3(0, 0.2f * h, -0.35f * h), rear);
            d3 = normalize(lerp(d3, vec3(0, -1.f, 0.4f), rear));
        }
        legIK3(sk, P, F, b1, b2, b3, m.legEnd[leg], target, d3, pole);
    }
}

// ---- reptiles -----------------------------------------------------------------------------------------------------
float reptileCycleRate(const ModelData& m, float speed) {
    float L = speciesInfo(m.species).length;
    float stride = L * 0.28f + speed * 0.25f;
    return speed > 0.01f ? speed / Max(stride, 0.05f) : 0.f;
}

void animateReptile(const ModelData& m, const ReptileAnim& a, Pose& P) {
    using namespace ReptBone;
    const Skel& sk = m.skel;
    P.reset(sk.n);
    ReptSpec R = reptSpec(m.species);
    float dead = Saturate(a.dead), swim = Saturate(a.swim) * (1.f - dead);
    float walkK = (1.f - swim) * (1.f - dead);
    float moving = sstep(0.03f, 0.25f, a.speed) * walkK;
    float ph = a.phase;
    float L = speciesInfo(m.species).length;
    // ---- spine: standing wave when walking, travelling wave down the tail when swimming
    float und = 0.16f * moving;
    float turn = Clamp(a.turn, -2.f, 2.f);
    P.q[CHEST] = qz(und * sinf(kTwoPi * ph) + turn * 0.12f);
    P.q[PELVIS] = qz(-und * sinf(kTwoPi * ph) - turn * 0.1f);
    float sph = a.swimPhase;
    for (int k = 0; k < 6; k++) {
        float kk = (float)k;
        float walkWave = -0.12f * moving * sinf(kTwoPi * ph - 0.6f * kk);
        float swimWave = swim * (0.1f + 0.05f * kk) * sinf(sph - 0.75f * kk);
        float idle = 0.03f * sinf(a.t * 0.4f + kk * 0.5f) * (1.f - moving) * (1.f - dead);
        P.q[TAIL1 + k] = qz(walkWave + swimWave + idle - turn * 0.08f);
    }
    P.q[BODY] = qz(swim * 0.05f * sinf(sph + 0.8f));
    // ---- head, jaw, hiss
    float hiss = Saturate(a.hiss) * (1.f - dead);
    float jaw = Max(Saturate(a.jaw), hiss * 0.75f);
    P.q[NECK] = qz(a.headYaw * 0.5f) * qx(hiss * 0.18f + a.headPitch * 0.5f);
    // gaping lifts the head (upper jaw) while the lower jaw rests near the ground; a hiss opens it downwards too
    float lift0 = jaw * (m.species == SP_GATOR ? 0.42f : 0.2f) * (1.f - hiss * 0.5f);
    P.q[HEAD] = qz(a.headYaw * 0.5f) * qx(a.headPitch * 0.5f + lift0 - 0.05f * dead);
    P.q[JAW] = qx(-jaw * (m.species == SP_GATOR ? 0.72f : 0.5f));
    float inflate = 1.f + 0.07f * hiss;
    P.s[BODY] = P.s[CHEST] = vec3(inflate, 1.f, inflate);
    // ---- body height: belly on the ground .. high walk; swimming floats level
    float lift = Saturate(a.lift);
    float belly = R.bodyZ - R.bodyHB;
    P.rootPos.z = -(1.f - lift) * (belly - 0.01f * L / 3.4f) * walkK;
    P.rootRot = qy(a.roll);
    if (dead > 0.f) P.rootRot = normalize(P.rootRot * slerp(quat(), qy(kPi * 0.5f), dead));
    // ---- legs
    Frames F;
    poseSkeleton(sk, P, nullptr, &F);
    float stride = L * 0.28f + a.speed * 0.25f;
    float duty = 0.68f;
    static const float kOff[4] = {0.f, 0.5f, 0.5f, 0.f};   // diagonal pairs
    for (int leg = 0; leg < 4; leg++) {
        bool front = leg < 2;
        int sd = (leg & 1) ? 1 : -1;
        int b1 = m.legBone[leg][0], b2 = m.legBone[leg][1], b3 = m.legBone[leg][2];
        if (swim > 0.5f || dead > 0.5f) {
            // tucked back along the flanks
            float s = -(float)sd;
            P.q[b1] = qz(s * (front ? 1.15f : 1.4f)) * qy(s * 0.25f);
            P.q[b2] = qz(s * (front ? -0.6f : -0.3f)) * qy(s * -0.4f);
            P.q[b3] = qz(s * 0.4f);
            if (dead > 0.5f) P.q[b1] = qy(s * 0.6f) * qz(s * 0.3f);
            continue;
        }
        vec3 rest = sk.bind[b3] + m.legEnd[leg];
        rest.z = -P.rootPos.z * 0.f;   // feet stay on the ground (model z = 0)
        rest.z = 0.f;
        float p = fracf(ph - kOff[leg] + 1.f);
        float y = 0.f, z = 0.f;
        float strk = stride * duty * moving;
        if (p < duty) y = strk * (0.5f - p / duty);
        else {
            float s = (p - duty) / (1.f - duty);
            y = strk * (-0.5f + smooth01(s));
            z = 0.12f * R.bodyZ * sinf(kPi * s) * moving;
        }
        vec3 target = rest + vec3((float)sd * 0.03f * (1.f - lift) * L / 3.4f, y, z);
        vec3 d3 = normalize(vec3((float)sd * 0.25f, front ? 0.25f : 0.35f, -1.f));
        vec3 pole = normalize(vec3((float)sd * 1.f, front ? -0.6f : 0.4f, 0.8f));
        legIK3(sk, P, F, b1, b2, b3, m.legEnd[leg], target, d3, pole);
    }
}

// ---- swimmers -----------------------------------------------------------------------------------------------------
void animateSwimmer(const ModelData& m, const SwimAnim& a, Pose& P) {
    using namespace SwimBone;
    P.reset(m.skel.n);
    float amp = Clamp(a.amp, 0.f, 1.6f) * (1.f - Saturate(a.dead));
    float ph = a.phase;
    float turn = Clamp(a.turn, -2.f, 2.f);
    float bend = Clamp(a.pitch, -1.2f, 1.2f);
    switch (m.species) {
        case SP_DOLPHIN: case SP_MANATEE: {
            float k = m.species == SP_MANATEE ? 0.8f : 1.f;
            P.q[FRONT] = qz(turn * 0.12f) * qx(-0.04f * amp * sinf(ph) + bend * 0.25f);
            P.q[HEAD] = qz(turn * 0.06f + a.headYaw) * qx(-0.03f * amp * sinf(ph - 0.3f) + bend * 0.1f);
            P.q[BACK1] = qz(-turn * 0.14f) * qx(0.12f * k * amp * sinf(ph - 0.4f) - bend * 0.2f);
            P.q[BACK2] = qz(-turn * 0.18f) * qx(0.24f * k * amp * sinf(ph - 1.0f) - bend * 0.15f);
            P.q[TAILFIN] = qz(-turn * 0.1f) * qx(0.38f * k * amp * sinf(ph - 1.7f));
            float fl = 0.12f * sinf(ph * 0.5f) + turn * 0.25f;
            P.q[FIN_L] = qy(0.2f + fl) * qx(-0.1f);
            P.q[FIN_R] = qy(-0.2f + fl) * qx(-0.1f);
            break;
        }
        case SP_TURTLE: {
            float st = sinf(ph), ct = cosf(ph);
            P.q[FIN_L] = qz(0.35f * amp * ct) * qy(0.55f * amp * st + 0.1f) * qx(0.35f * amp * ct);
            P.q[FIN_R] = qz(-0.35f * amp * ct) * qy(-0.55f * amp * st - 0.1f) * qx(0.35f * amp * ct);
            P.q[FIN_L2] = qz(0.25f * sinf(ph * 0.5f)) * qy(0.2f);
            P.q[FIN_R2] = qz(-0.25f * sinf(ph * 0.5f)) * qy(-0.2f);
            P.q[HEAD] = qz(a.headYaw + turn * 0.2f) * qx(0.05f * sinf(a.t * 0.7f));
            P.q[FRONT] = qx(bend * 0.1f);
            break;
        }
        default: {   // fish: lateral undulation
            P.q[FRONT] = qz(0.06f * amp * sinf(ph) + turn * 0.12f);
            P.q[BACK1] = qz(-0.12f * amp * sinf(ph - 0.8f) - turn * 0.12f) * qx(bend * 0.1f);
            P.q[BACK2] = qz(-0.28f * amp * sinf(ph - 1.5f) - turn * 0.18f);
            P.q[TAILFIN] = qz(-0.42f * amp * sinf(ph - 2.2f) - turn * 0.1f);
            P.q[FIN_L] = qz(0.35f + 0.3f * sinf(a.t * 7.f));
            P.q[FIN_R] = qz(-0.35f - 0.3f * sinf(a.t * 7.f + 1.f));
            break;
        }
    }
}

// ---- leash --------------------------------------------------------------------------------------------------------
void leashMatrices(const ModelData& m, const Frames& fr, vec3 handModel, float ropeLen, bool hidden, mat4* skin) {
    using namespace QuadBone;
    const Skel& sk = m.skel;
    if (sk.n <= LEASH0 + kLeashSegments - 1) return;
    vec3 collar = fr.p[NECK2] + rotate(fr.r[NECK2], (m.collar - sk.bind[NECK2]) * fr.g[NECK2]);
    if (hidden) {
        for (int k = 0; k < kLeashSegments; k++) skin[LEASH0 + k] = mat4(vec4(0.f), vec4(0.f), vec4(0.f), vec4(collar, 1.f));
        return;
    }
    vec3 d = handModel - collar;
    float dist = length(d);
    float slack = Max(0.f, ropeLen - dist);
    float sag = Min(sqrtf(3.f * Max(dist, 0.05f) * slack / 8.f) + slack * 0.25f, 1.2f);
    vec3 P[kLeashSegments + 1];
    for (int k = 0; k <= kLeashSegments; k++) {
        float t = (float)k / (float)kLeashSegments;
        P[k] = lerp(collar, handModel, t) - vec3(0, 0, sag * 4.f * t * (1.f - t));
        P[k].z = Max(P[k].z, 0.012f);   // lies on the ground rather than going through it
    }
    vec3 bd = normalize(vec3(0, 0.35f, 1.f));
    const float seg0 = 0.3f;
    for (int k = 0; k < kLeashSegments; k++) {
        vec3 q0 = sk.bind[LEASH0 + k];
        vec3 dir = P[k + 1] - P[k];
        float len = length(dir);
        vec3 u = len > 1e-5f ? dir / len : bd;
        mat3 R = mat3FromQuat(quatFromTo(bd, u));
        float s = len / seg0;
        // L(v) = R (v + (s - 1) bd (bd . v))
        vec3 cx = R * (vec3(1, 0, 0) + bd * ((s - 1.f) * bd.x));
        vec3 cy = R * (vec3(0, 1, 0) + bd * ((s - 1.f) * bd.y));
        vec3 cz = R * (vec3(0, 0, 1) + bd * ((s - 1.f) * bd.z));
        vec3 t = P[k] - (cx * q0.x + cy * q0.y + cz * q0.z);
        skin[LEASH0 + k] = mat4(vec4(cx, 0.f), vec4(cy, 0.f), vec4(cz, 0.f), vec4(t, 1.f));
    }
}

// ---- boids --------------------------------------------------------------------------------------------------------
vec3 boidSteer(const vec3* pos, const vec3* vel, int n, int self, const BoidParams& bp) {
    vec3 sep(0.f), ali(0.f), coh(0.f);
    int na = 0;
    float view2 = bp.viewDist * bp.viewDist;
    vec3 me = pos[self];
    for (int i = 0; i < n; i++) {
        if (i == self) continue;
        vec3 d = me - pos[i];
        float d2 = length2(d);
        if (d2 > view2) continue;
        float dd = sqrtf(d2);
        if (dd < bp.sepDist) {
            if (dd > 1e-4f) sep += d * ((1.f - dd / bp.sepDist) / dd);
            else sep += vec3(((i * 7 + self) & 1) ? 0.3f : -0.3f, 0.2f, 0.f);
        }
        ali += vel[i];
        coh += pos[i];
        na++;
    }
    vec3 f = sep * bp.wSep;
    if (na > 0) {
        float inv = 1.f / (float)na;
        f += (ali * inv - vel[self]) * bp.wAli;
        f += (coh * inv - me) * bp.wCoh;
    }
    return f;
}

// ---- dispatcher ---------------------------------------------------------------------------------------------------
void buildModel(int species, int variant, ModelData& out) {
    out = ModelData();
    out.species = species;
    out.variant = variant;
    switch (speciesInfo(species).plan) {
        case PLAN_BIRD: buildBird(species, variant, out); break;
        case PLAN_QUAD: buildQuad(species, variant, out); break;
        case PLAN_REPTILE: buildReptile(species, variant, out); break;
        default: buildSwimmer(species, variant, out); break;
    }
}

}  // namespace Fauna
