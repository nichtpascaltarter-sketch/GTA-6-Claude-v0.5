// Procedural prototype meshes for props and vegetation (built once at startup).
#include "worldtypes.h"
#include "../render/mesh.h"

namespace World {

// Foliage atlas layers (alpha-tested cards), generated on the GPU (see foliage.hlsl)
enum FoliageLayer { FOL_PALM_FROND = 0, FOL_BROADLEAF, FOL_PINE, FOL_GRASS, FOL_MANGROVE, FOL_FLOWERS, FOL_COUNT };

// In prop meshes, MAT_LEAVES/MAT_PALM_FROND geometry uses the vertex color alpha to pick the foliage layer:
// color.a = layer / 8.
inline u32 foliageColor(vec3 tint, int layer) { return packRGBA8(tint.x, tint.y, tint.z, (layer + 0.5f) / 8.f); }

// Defined in landmarks.cpp (needs the stroke font and the billboard ad art); prototypes are built at renderer start-up.
void propAdPanel(MeshData& m, vec3 center, vec3 right, vec3 up, float w, float h, int ad, float glow);
void propText(MeshData& m, const char* txt, vec3 origin, vec3 right, vec3 up, float h, u32 col, u32 mat);

struct PropPrototype {
    MeshData mesh;
    float radius = 1.f;     // bounding radius
    float lodDistance = 150.f;
    bool castsShadow = true;
    bool foliage = false;
    int lightSlot = -1;      // emissive lamp position index
};

namespace propmesh_detail {

const u32 W = 0xffffffffu;

void tube(MeshData& m, const std::vector<vec3>& path, const std::vector<float>& radius, int seg, u32 col, u32 mat, float vScale = 1.f) {
    // Generalized cylinder along a polyline
    u32 base = (u32)m.verts.size();
    float v = 0;
    vec3 prevN = anyPerp(path.size() > 1 ? normalize(path[1] - path[0]) : vec3(0, 0, 1));
    for (size_t i = 0; i < path.size(); i++) {
        vec3 t = i + 1 < path.size() ? normalize(path[i + 1] - path[i]) : normalize(path[i] - path[i - 1]);
        if (i > 0 && i + 1 < path.size()) t = normalize(path[i + 1] - path[i - 1]);
        vec3 n = normalize(prevN - t * dot(prevN, t));
        prevN = n;
        vec3 b = cross(t, n);
        if (i > 0) v += length(path[i] - path[i - 1]) * vScale;
        for (int k = 0; k <= seg; k++) {
            float a = kTwoPi * k / seg;
            vec3 dir = n * cosf(a) + b * sinf(a);
            m.addVertex(path[i] + dir * radius[i], dir, t, vec2((float)k / seg * kTwoPi * radius[0], v), col, mat);
        }
    }
    for (size_t i = 0; i + 1 < path.size(); i++)
        for (int k = 0; k < seg; k++) {
            u32 a = base + (u32)(i * (seg + 1) + k), b = a + 1, c = a + seg + 1, d = c + 1;
            m.quadIdx(a, b, d, c);
        }
}

// Double-sided card (two quads) facing along n
void card(MeshData& m, vec3 c, vec3 right, vec3 up, float w, float h, u32 col, u32 mat, vec2 uv0 = vec2(0, 0), vec2 uv1 = vec2(1, 1)) {
    vec3 a = c - right * (w * 0.5f), b = c + right * (w * 0.5f);
    vec3 n = normalize(cross(right, up));
    u32 i0 = m.addVertex(a, n, right, vec2(uv0.x, uv1.y), col, mat), i1 = m.addVertex(b, n, right, vec2(uv1.x, uv1.y), col, mat);
    u32 i2 = m.addVertex(b + up * h, n, right, vec2(uv1.x, uv0.y), col, mat), i3 = m.addVertex(a + up * h, n, right, vec2(uv0.x, uv0.y), col, mat);
    m.quadIdx(i0, i1, i2, i3);
    u32 j0 = m.addVertex(a, -n, right, vec2(uv0.x, uv1.y), col, mat), j1 = m.addVertex(b, -n, right, vec2(uv1.x, uv1.y), col, mat);
    u32 j2 = m.addVertex(b + up * h, -n, right, vec2(uv1.x, uv0.y), col, mat), j3 = m.addVertex(a + up * h, -n, right, vec2(uv0.x, uv0.y), col, mat);
    m.quadIdx(j0, j3, j2, j1);
}

// Palm frond: a curved strip along an arc with leaflet texture, double sided
void frond(MeshData& m, vec3 base, vec3 dir, float len, float droop, float width, u32 col) {
    vec3 side = normalize(cross(dir, vec3(0, 0, 1)));
    if (length2(side) < 1e-6f) side = vec3(1, 0, 0);
    const int N = 8;
    u32 start = (u32)m.verts.size();
    for (int i = 0; i <= N; i++) {
        float t = (float)i / N;
        // parabolic droop
        vec3 p = base + dir * (len * t) + vec3(0, 0, -droop * t * t * len);
        vec3 tangent = normalize(dir * len + vec3(0, 0, -2.f * droop * t * len));
        float w = width * sinf(Min(1.f, t * 1.1f + 0.08f) * kPi * 0.95f + 0.1f);
        // fold the leaflets slightly (V shape)
        vec3 up = normalize(cross(side, tangent));
        vec3 l = p - side * w + up * (w * 0.25f), r = p + side * w + up * (w * 0.25f);
        vec3 n = up;
        m.addVertex(l, n, side, vec2(0, 1.f - t), col, makeMat(MAT_PALM_FROND));
        m.addVertex(p, n, side, vec2(0.5f, 1.f - t), col, makeMat(MAT_PALM_FROND));
        m.addVertex(r, n, side, vec2(1, 1.f - t), col, makeMat(MAT_PALM_FROND));
    }
    for (int i = 0; i < N; i++) {
        u32 a = start + i * 3;
        m.quadIdx(a, a + 1, a + 4, a + 3);
        m.quadIdx(a + 1, a + 2, a + 5, a + 4);
        // back faces
        m.quadIdx(a + 3, a + 4, a + 1, a);
        m.quadIdx(a + 4, a + 5, a + 2, a + 1);
    }
}

// Palm crown frond (the palm builders): a curved strip with leaflet texture, double sided. The rachis droops ever more steeply
// towards the tip; the blade folds into a V along it, deeper in the middle, the leaflets hanging lower towards the tip,
// and turns a little about the rachis on the way out, so the crown reads as feathery, not as flat paddles.
void palmFrondStrip(MeshData& m, vec3 base, vec3 dir, float len, float droop, float width, u32 col) {
    vec3 side0 = normalize(cross(dir, vec3(0, 0, 1)));
    if (length2(side0) < 1e-6f) side0 = vec3(1, 0, 0);
    const int N = 9;
    float twist = (float)((hash32((u32)(int)(len * 1000.f) ^ (u32)(int)(dir.x * 7919.f)) & 255u) / 255.f - 0.5f) * 0.6f;
    u32 start = (u32)m.verts.size();
    for (int i = 0; i <= N; i++) {
        float t = (float)i / N;
        vec3 p = base + dir * (len * t) + vec3(0, 0, -droop * t * t * len);
        vec3 tangent = normalize(dir * len + vec3(0, 0, -2.f * droop * t * len));
        float w = width * sinf(Min(1.f, t * 1.1f + 0.08f) * kPi * 0.95f + 0.1f);
        float a = twist * t;
        vec3 up0 = normalize(cross(side0, tangent));
        vec3 side = side0 * cosf(a) + up0 * sinf(a);
        vec3 up = normalize(cross(side, tangent));
        float fold = 0.18f + 0.22f * sinf(t * kPi);           // V depth (fraction of the half width)
        vec3 hang = vec3(0, 0, -w * 0.3f * t * t);             // leaflets hang towards the tip
        vec3 l = p - side * w + up * (w * fold) + hang, r = p + side * w + up * (w * fold) + hang;
        vec3 n = up;
        m.addVertex(l, n, side, vec2(0, 1.f - t), col, makeMat(MAT_PALM_FROND));
        m.addVertex(p, n, side, vec2(0.5f, 1.f - t), col, makeMat(MAT_PALM_FROND));
        m.addVertex(r, n, side, vec2(1, 1.f - t), col, makeMat(MAT_PALM_FROND));
    }
    for (int i = 0; i < N; i++) {
        u32 a = start + i * 3;
        m.quadIdx(a, a + 1, a + 4, a + 3);
        m.quadIdx(a + 1, a + 2, a + 5, a + 4);
        // back faces
        m.quadIdx(a + 3, a + 4, a + 1, a);
        m.quadIdx(a + 4, a + 5, a + 2, a + 1);
    }
}

// Leaf cluster: several crossed cards around a center
void leafCluster(MeshData& m, vec3 c, float size, u32 col, u32 mat, Rng& r) {
    for (int k = 0; k < 3; k++) {
        float a = r.f() * kPi;
        vec3 right(cosf(a), sinf(a), 0);
        vec3 up = normalize(vec3(r.range(-0.3f, 0.3f), r.range(-0.3f, 0.3f), 1.f));
        up = normalize(up - right * dot(up, right));
        card(m, c - up * (size * 0.5f), right, up, size, size, col, mat);
    }
    // horizontal card
    card(m, c, vec3(1, 0, 0), normalize(vec3(0, 1, 0.15f)), size, size, col, mat);
}

}  // namespace propmesh_detail

using namespace propmesh_detail;

void buildPropPrototype(PropType type, int variant, PropPrototype& p) {
    MeshData& m = p.mesh;
    Rng r(hash32((u32)type * 131u + (u32)variant * 7u + 1u));
    u32 metal = makeMat(MAT_METAL_PAINTED);
    switch (type) {
        case PROP_STREETLIGHT: {
            u32 poleC = variant == 1 ? packRGBA8(0.2f, 0.25f, 0.22f, 1) : packRGBA8(0.55f, 0.56f, 0.58f, 1);
            m.cylinder(vec3(0, 0, 0), 0.16f, 0.09f, 8.5f, 8, poleC, metal, false);
            m.cylinder(vec3(0, 0, 0), 0.28f, 0.25f, 0.6f, 8, poleC, metal, true);
            // arm curving over the road (+x)
            std::vector<vec3> arm = {vec3(0, 0, 8.2f), vec3(0.6f, 0, 8.6f), vec3(1.5f, 0, 8.75f), vec3(2.2f, 0, 8.7f)};
            tube(m, arm, {0.07f, 0.06f, 0.055f, 0.05f}, 6, poleC, metal);
            m.box(vec3(2.25f, 0, 8.6f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.4f, 0.2f, 0.08f), poleC, metal, false);
            // emissive lens
            m.quad(vec3(1.9f, -0.16f, 8.51f), vec3(1.9f, 0.16f, 8.51f), vec3(2.6f, 0.16f, 8.51f), vec3(2.6f, -0.16f, 8.51f), vec2(0, 0), vec2(1, 0), vec2(1, 1),
                   vec2(0, 1), packRGBA8(1.f, 0.85f, 0.6f, 0.6f), makeMat(MAT_EMISSIVE));
            p.radius = 9.f;
            p.lodDistance = 260.f;
            break;
        }
        case PROP_STREETLIGHT_DOUBLE: {
            u32 poleC = packRGBA8(0.55f, 0.56f, 0.58f, 1);
            m.cylinder(vec3(0, 0, 0), 0.2f, 0.12f, 11.5f, 8, poleC, metal, false);
            for (int s = -1; s <= 1; s += 2) {
                std::vector<vec3> arm = {vec3(0, 0, 11.f), vec3(s * 0.8f, 0, 11.5f), vec3(s * 2.8f, 0, 11.6f)};
                tube(m, arm, {0.08f, 0.07f, 0.06f}, 6, poleC, metal);
                m.box(vec3(s * 2.9f, 0, 11.5f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.45f, 0.22f, 0.09f), poleC, metal, false);
                m.quadFacing(vec3(s * 2.5f, -0.18f, 11.4f), vec3(s * 2.5f, 0.18f, 11.4f), vec3(s * 3.3f, 0.18f, 11.4f), vec3(s * 3.3f, -0.18f, 11.4f), vec2(0, 0),
                             vec2(1, 0), vec2(1, 1), vec2(0, 1), packRGBA8(1.f, 0.85f, 0.6f, 0.6f), makeMat(MAT_EMISSIVE), vec3(0, 0, -1));
            }
            p.radius = 12.f;
            p.lodDistance = 320.f;
            break;
        }
        case PROP_TRAFFIC_LIGHT: {
            // pole at origin, mast arm reaching over the road along +x (yaw set so +x points across lanes)
            u32 poleC = variant ? packRGBA8(0.75f, 0.72f, 0.3f, 1) : packRGBA8(0.2f, 0.2f, 0.21f, 1);
            float mast = variant ? 8.5f : 5.5f;
            m.cylinder(vec3(0, 0, 0), 0.18f, 0.14f, 6.3f, 10, poleC, metal, true);
            std::vector<vec3> arm = {vec3(0, 0, 5.8f), vec3(mast * 0.5f, 0, 6.0f), vec3(mast, 0, 6.05f)};
            tube(m, arm, {0.11f, 0.09f, 0.07f}, 6, poleC, metal);
            // signal heads facing oncoming traffic (-y side faces traffic approaching from -y)
            int heads = variant ? 2 : 1;
            for (int hI = 0; hI < heads; hI++) {
                float x = mast * (heads == 1 ? 0.8f : (0.45f + 0.45f * hI));
                vec3 hc(x, 0, 5.2f);
                m.box(hc, vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.2f, 0.16f, 0.62f), packRGBA8(0.12f, 0.12f, 0.1f, 1), metal, true);
                // three lamps: emissive discs encoded with color alpha = 1 (state driven in shader via variant bits)
                for (int l = 0; l < 3; l++) {
                    vec3 lc = hc + vec3(0, -0.17f, 0.38f - l * 0.38f);
                    vec3 col = l == 0 ? vec3(1, 0.1f, 0.05f) : (l == 1 ? vec3(1, 0.65f, 0.05f) : vec3(0.1f, 1, 0.45f));
                    // lamp id in alpha: 0.25 red, 0.5 yellow, 0.75 green  -> shader turns them on/off
                    float aId = (l + 1) * 0.25f;
                    std::vector<vec3> disc;
                    for (int k = 0; k < 10; k++) {
                        float a = kTwoPi * k / 10.f;
                        disc.push_back(lc + vec3(cosf(a) * 0.13f, 0, sinf(a) * 0.13f));
                    }
                    u32 base = (u32)m.verts.size();
                    for (auto& d : disc) m.addVertex(d, vec3(0, -1, 0), vec3(1, 0, 0), vec2(0, 0), packRGBA8(col.x, col.y, col.z, aId), makeMat(MAT_EMISSIVE, 1));
                    for (int k = 1; k + 1 < 10; k++) m.tri(base, base + k + 1, base + k);
                    // visor
                    m.box(lc + vec3(0, -0.28f, 0.1f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.15f, 0.12f, 0.015f), packRGBA8(0.1f, 0.1f, 0.1f, 1), metal, true);
                }
            }
            // street name sign on the mast
            m.box(vec3(mast * 0.35f, -0.02f, 6.35f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.8f, 0.02f, 0.14f), packRGBA8(0.05f, 0.35f, 0.15f, 1), metal, true);
            p.radius = mast + 1.f;
            p.lodDistance = 240.f;
            break;
        }
        case PROP_STOP_SIGN: {
            m.cylinder(vec3(0, 0, 0), 0.04f, 0.04f, 2.4f, 6, packRGBA8(0.6f, 0.6f, 0.6f, 1), metal, true);
            std::vector<vec3> oct;
            for (int k = 0; k < 8; k++) {
                float a = kTwoPi * (k + 0.5f) / 8.f;
                oct.push_back(vec3(cosf(a) * 0.38f, -0.05f, 2.3f + sinf(a) * 0.38f));
            }
            u32 base = (u32)m.verts.size();
            for (auto& v : oct) m.addVertex(v, vec3(0, -1, 0), vec3(1, 0, 0), vec2(0, 0), packRGBA8(0.75f, 0.05f, 0.04f, 1), metal);
            for (int k = 1; k + 1 < 8; k++) m.tri(base, base + k + 1, base + k);
            p.radius = 3.f;
            p.lodDistance = 120.f;
            break;
        }
        case PROP_PALM:
        case PROP_PALM_TALL: {
            bool tall = type == PROP_PALM_TALL;  // royal palm: smooth grey column, green crownshaft; else coconut palm
            float h = tall ? r.range(11.f, 15.f) : r.range(6.5f, 10.f);
            float lean = tall ? r.range(0.f, 0.4f) : r.range(0.7f, 2.4f);
            float ang = r.f() * kTwoPi;
            vec3 leanDir(cosf(ang), sinf(ang), 0);
            std::vector<vec3> trunk;
            std::vector<float> rad;
            // trunk: swollen bole at the base, slight S-curve on leaning palms; the leaf-scar rings are shaded on the
            // bark (props.hlsl: material param 2 coconut palm, 3 royal palm)
            int rings = tall ? 14 : 22;
            vec3 wob(cosf(ang + 1.3f), sinf(ang + 1.3f), 0);
            for (int i = 0; i <= rings; i++) {
                float t = (float)i / rings;
                trunk.push_back(leanDir * (lean * t * t) + wob * (tall ? 0.f : 0.25f * sinf(t * kPi)) + vec3(0, 0, h * t));
                float base = tall ? Lerp(0.34f, 0.23f, t) + (t > 0.85f ? 0.05f : 0.f) + 0.04f * sinf(t * kPi) : Lerp(0.25f, 0.16f, t);
                float bole = t < 0.1f ? (0.1f - t) * (tall ? 2.6f : 2.0f) : 0.f;
                rad.push_back(base + bole);
            }
            u32 barkC = tall ? packRGBA8(0.72f, 0.7f, 0.66f, 1) : packRGBA8(r.range(0.5f, 0.6f), r.range(0.43f, 0.5f), r.range(0.34f, 0.4f), 1);
            tube(m, trunk, rad, 10, barkC, makeMat(MAT_BARK, tall ? 3u : 2u), 3.f);
            vec3 top = trunk.back();
            if (tall) {
                // green crownshaft
                std::vector<vec3> cs = {top - vec3(0, 0, 1.6f), top - vec3(0, 0, 0.5f), top + vec3(0, 0, 0.7f)};
                tube(m, cs, {0.27f, 0.25f, 0.19f}, 8, packRGBA8(0.35f, 0.52f, 0.22f, 1), makeMat(MAT_BARK));
                top += vec3(0, 0, 0.6f);
            } else {
                // fibrous boot where the fronds attach
                std::vector<vec3> bt = {top - vec3(0, 0, 0.55f), top + vec3(0, 0, 0.15f)};
                tube(m, bt, {0.2f, 0.3f}, 8, packRGBA8(0.42f, 0.34f, 0.24f, 1), makeMat(MAT_BARK));
            }
            vec3 fc(r.range(0.8f, 1.1f), r.range(0.9f, 1.1f), r.range(0.75f, 1.f));
            // main crown: two interleaved rings of arching fronds
            int nf = tall ? r.irange(14, 18) : r.irange(15, 20);
            for (int k = 0; k < nf; k++) {
                bool inner = (k & 1) == 1;
                float a = kTwoPi * k / nf + r.range(-0.18f, 0.18f);
                float elev = inner ? r.range(0.35f, 0.8f) : r.range(-0.15f, 0.35f);
                vec3 d = normalize(vec3(cosf(a) * cosf(elev), sinf(a) * cosf(elev), sinf(elev)));
                float len = (tall ? r.range(3.4f, 4.4f) : r.range(3.3f, 4.6f)) * (inner ? 0.8f : 1.f);
                float droop = inner ? r.range(0.2f, 0.4f) : r.range(0.45f, 0.85f);
                vec3 col = fc * (k % 6 == 0 ? vec3(0.95f, 0.85f, 0.6f) : vec3(1.f));
                palmFrondStrip(m, top, d, len, droop, inner ? 0.62f : 0.75f, foliageColor(col, FOL_PALM_FROND));
            }
            // spear: a couple of young upright fronds
            for (int k = 0; k < 2; k++) {
                float a = r.f() * kTwoPi;
                vec3 d = normalize(vec3(cosf(a) * 0.25f, sinf(a) * 0.25f, 1.f));
                palmFrondStrip(m, top, d, r.range(1.6f, 2.2f), 0.05f, 0.4f, foliageColor(fc * vec3(1.05f, 1.1f, 0.9f), FOL_PALM_FROND));
            }
            // skirt of dead fronds hanging against the trunk (some specimens)
            if (!tall && (variant & 1)) {
                int nd = r.irange(3, 6);
                for (int k = 0; k < nd; k++) {
                    float a = r.f() * kTwoPi;
                    vec3 d = normalize(vec3(cosf(a) * 0.45f, sinf(a) * 0.45f, -1.f));
                    palmFrondStrip(m, top - vec3(0, 0, 0.3f), d, r.range(2.0f, 2.8f), 0.05f, 0.42f, foliageColor(vec3(0.75f, 0.55f, 0.3f), FOL_PALM_FROND));
                }
            }
            // coconut cluster: round nuts hanging under the crown, green and ripening brown
            if (!tall)
                for (int k = 0; k < 6; k++) {
                    float a = kTwoPi * k / 6.f + r.range(-0.3f, 0.3f);
                    vec3 c = top + vec3(cosf(a) * 0.24f, sinf(a) * 0.24f, -0.32f - r.range(0.f, 0.22f));
                    u32 nc = k & 1 ? packRGBA8(0.3f, 0.37f, 0.11f, 1) : packRGBA8(0.42f, 0.32f, 0.14f, 1);
                    float rr = r.range(0.11f, 0.14f);
                    const int SEG = 6, RING = 4;
                    u32 b0 = (u32)m.verts.size();
                    for (int j = 0; j <= RING; j++) {
                        float th = kPi * j / RING;
                        for (int q = 0; q <= SEG; q++) {
                            float ph = kTwoPi * q / SEG;
                            vec3 nrm(sinf(th) * cosf(ph), sinf(th) * sinf(ph), cosf(th));
                            m.addVertex(c + vec3(nrm.x, nrm.y, nrm.z * 1.12f) * rr, nrm, vec3(-sinf(ph), cosf(ph), 0), vec2((float)q / SEG, (float)j / RING), nc, makeMat(MAT_BARK));
                        }
                    }
                    for (int j = 0; j < RING; j++)
                        for (int q = 0; q < SEG; q++) {
                            u32 i0 = b0 + (u32)(j * (SEG + 1) + q);
                            m.quadIdx(i0, i0 + SEG + 1, i0 + SEG + 2, i0 + 1);
                        }
                }
            p.radius = h + 5.f;
            p.lodDistance = tall ? 400.f : 300.f;
            p.foliage = true;
            break;
        }
        case PROP_TREE_OAK:
        case PROP_MANGROVE:
        case PROP_BUSH:
        case PROP_CYPRESS: {
            bool bush = type == PROP_BUSH;
            bool mangrove = type == PROP_MANGROVE;
            bool cypress = type == PROP_CYPRESS;
            bool oak = type == PROP_TREE_OAK;
            float h = bush ? r.range(1.f, 2.2f) : (mangrove ? r.range(3.f, 6.f) : (cypress ? r.range(9.f, 16.f) : r.range(6.f, 11.f)));
            float trunkH = bush ? 0.2f : h * (cypress ? 0.65f : (oak ? 0.32f : 0.4f));
            u32 bark = mangrove ? packRGBA8(0.45f, 0.38f, 0.3f, 1) : packRGBA8(0.4f, 0.33f, 0.26f, 1);
            // live oaks: wide, low, spreading crowns (canopy radius > height/2)
            float cr = bush ? h * 0.7f : (cypress ? h * 0.22f : (oak ? h * r.range(0.5f, 0.62f) : h * 0.42f));
            float cz = bush ? h * 0.55f : (cypress ? h * 0.72f : trunkH + cr * (oak ? 0.55f : 0.8f));
            std::vector<vec3> tips;  // branch tips (canopy anchors)
            if (!bush) {
                vec3 tt(r.range(-0.3f, 0.3f), r.range(-0.3f, 0.3f), trunkH);
                tube(m, {vec3(0, 0, -0.3f), tt * 0.5f + vec3(0, 0, trunkH * 0.1f), tt}, {h * (oak ? 0.06f : 0.045f), h * 0.04f, h * 0.03f}, 8, bark, makeMat(MAT_BARK), 2.f);
                // root flare
                if (oak)
                    for (int k = 0; k < 4; k++) {
                        float a = kTwoPi * k / 4 + r.f();
                        tube(m, {vec3(0, 0, 0.5f), vec3(cosf(a) * 0.7f, sinf(a) * 0.7f, -0.1f)}, {h * 0.03f, h * 0.012f}, 5, bark, makeMat(MAT_BARK), 2.f);
                    }
                // main scaffold branches, then a fork of two secondary branches at each end
                int nb = cypress ? 3 : r.irange(oak ? 4 : 3, oak ? 6 : 5);
                for (int k = 0; k < nb; k++) {
                    float a = kTwoPi * k / nb + r.f() * 0.6f;
                    vec3 b0 = tt * 0.95f;
                    float reach = oak ? cr * r.range(0.55f, 0.75f) : h * 0.25f;
                    vec3 b1 = b0 + vec3(cosf(a) * reach, sinf(a) * reach, h * (oak ? r.range(0.08f, 0.2f) : r.range(0.15f, 0.3f)));
                    tube(m, {b0, (b0 + b1) * 0.5f + vec3(0, 0, h * 0.05f), b1}, {h * 0.024f, h * 0.018f, h * 0.012f}, 5, bark, makeMat(MAT_BARK), 2.f);
                    if (oak || mangrove) {
                        for (int j = -1; j <= 1; j += 2) {
                            float a2 = a + j * r.range(0.35f, 0.7f);
                            vec3 b2 = b1 + vec3(cosf(a2) * reach * 0.5f, sinf(a2) * reach * 0.5f, h * r.range(0.05f, 0.15f));
                            tube(m, {b1, b2}, {h * 0.011f, h * 0.006f}, 4, bark, makeMat(MAT_BARK), 2.f);
                            tips.push_back(b2);
                        }
                    } else {
                        tips.push_back(b1);
                    }
                }
                if (mangrove) {
                    // prop roots arching into the water
                    for (int k = 0; k < 7; k++) {
                        float a = kTwoPi * k / 7 + r.f() * 0.5f;
                        vec3 d(cosf(a), sinf(a), 0);
                        std::vector<vec3> root = {vec3(0, 0, 1.4f), d * 0.7f + vec3(0, 0, 1.2f), d * 1.4f + vec3(0, 0, 0.5f), d * 1.7f + vec3(0, 0, -0.4f)};
                        tube(m, root, {0.07f, 0.06f, 0.05f, 0.05f}, 5, bark, makeMat(MAT_BARK));
                    }
                }
            }
            // Canopy: leaf clusters on branch tips plus a filled ellipsoid shell
            int clusters = bush ? r.irange(10, 14) : (cypress ? 12 : (oak ? 20 : 18));
            vec3 lc = mangrove ? vec3(0.8f, 1.f, 0.7f) : (cypress ? vec3(0.85f, 1.0f, 0.75f) : vec3(r.range(0.85f, 1.1f), r.range(0.9f, 1.1f), 0.85f));
            int layer = mangrove ? FOL_MANGROVE : (cypress ? FOL_PINE : FOL_BROADLEAF);
            if (bush && r.chance(0.4f)) layer = FOL_FLOWERS;
            for (vec3 tp : tips) {
                int nc = oak ? 2 : 1;
                for (int j = 0; j < nc; j++) {
                    vec3 c = tp + vec3(r.range(-0.6f, 0.6f), r.range(-0.6f, 0.6f), r.range(0.1f, 0.8f));
                    vec3 tint = lc * r.range(0.88f, 1.08f);
                    leafCluster(m, c, cr * 0.55f + r.range(0.2f, 0.7f), foliageColor(tint, layer), makeMat(MAT_LEAVES), r);
                }
            }
            for (int k = 0; k < clusters; k++) {
                vec3 d = r.onSphere();
                d.z = d.z * (cypress ? 1.8f : (oak ? 0.45f : 0.7f));
                if (bush && d.z < -0.2f) d.z = -0.2f;  // bushes sit on the ground: no clusters below the base
                vec3 c = vec3(0, 0, cz) + vec3(d.x * cr, d.y * cr, d.z * cr);
                vec3 tint = lc * r.range(0.9f, 1.08f);
                leafCluster(m, c, cr * (bush ? 0.8f : (oak ? 0.6f : 0.75f)) + r.range(0.f, 0.5f), foliageColor(tint, layer), makeMat(MAT_LEAVES), r);
            }
            // Spanish moss hanging from oak limbs (some specimens): clumps of thin grey-green strands, kinked half way and tapering
            // to a point, each turned its own way (a flat card read as a pale pane edge-on and as a dark bar from the side)
            if (oak && (variant & 2)) {
                for (vec3 tp : tips) {
                    int nm = r.irange(1, 2);
                    for (int j = 0; j < nm; j++) {
                        vec3 c = tp + vec3(r.range(-0.8f, 0.8f), r.range(-0.8f, 0.8f), -r.range(0.2f, 0.6f));
                        float len = r.range(0.9f, 1.8f);
                        // muted grey-green: the strands sit in the canopy's shade and must not read as pale spikes
                        vec3 col = vec3(0.4f, 0.43f, 0.36f) * r.range(0.85f, 1.08f);
                        u32 mc = packRGBA8(col.x, col.y, col.z, 1.f), mm = makeMat(MAT_PLASTER);
                        for (int s = 0; s < 9; s++) {
                            float a = r.f() * kTwoPi;
                            vec3 side(cosf(a), sinf(a), 0.f);
                            vec3 top = c + vec3(r.range(-0.35f, 0.35f), r.range(-0.35f, 0.35f), r.range(-0.12f, 0.12f));
                            float l = len * r.range(0.4f, 1.f), w = r.range(0.012f, 0.026f);
                            // two segments with a kink half way, so each strand hangs like a string rather than a straight spike
                            vec3 mid = top + vec3(r.range(-0.12f, 0.12f), r.range(-0.12f, 0.12f), -l * r.range(0.4f, 0.6f));
                            vec3 tip = vec3(top.x + r.range(-0.15f, 0.15f), top.y + r.range(-0.15f, 0.15f), top.z - l);
                            vec3 p[5] = {top - side * w, top + side * w, mid + side * (w * 0.55f), mid - side * (w * 0.55f), tip};
                            for (int face = 0; face < 2; face++) {
                                vec3 n = normalize(cross(p[1] - p[0], p[4] - p[0])) * (face ? -1.f : 1.f);
                                u32 i[5];
                                for (int k = 0; k < 5; k++) i[k] = m.addVertex(p[k], n, side, vec2(k == 1 || k == 2 ? 1.f : 0.f, k < 2 ? 0.f : (k < 4 ? 0.5f : 1.f)), mc, mm);
                                if (face == 0) {
                                    m.tri(i[0], i[1], i[2]);
                                    m.tri(i[0], i[2], i[3]);
                                    m.tri(i[3], i[2], i[4]);
                                } else {
                                    m.tri(i[0], i[2], i[1]);
                                    m.tri(i[0], i[3], i[2]);
                                    m.tri(i[3], i[4], i[2]);
                                }
                            }
                        }
                    }
                }
            }
            p.radius = h + cr;
            p.lodDistance = bush ? 120.f : 350.f;
            p.foliage = true;
            break;
        }
        case PROP_TREE_PINE: {
            float h = r.range(12.f, 22.f);
            tube(m, {vec3(0, 0, -0.3f), vec3(0, 0, h)}, {0.28f, 0.08f}, 7, packRGBA8(0.45f, 0.32f, 0.22f, 1), makeMat(MAT_BARK), 2.f);
            // slash pine: tufts clustered at the top third
            for (int k = 0; k < 14; k++) {
                float z = h * r.range(0.6f, 1.0f);
                float a = r.f() * kTwoPi;
                float rr = (h - z) * 0.35f + 0.8f;
                vec3 c(cosf(a) * rr, sinf(a) * rr, z);
                tube(m, {vec3(0, 0, z - 0.4f), c}, {0.07f, 0.04f}, 4, packRGBA8(0.45f, 0.32f, 0.22f, 1), makeMat(MAT_BARK));
                leafCluster(m, c, 2.2f, foliageColor(vec3(0.9f, 1.f, 0.8f), FOL_PINE), makeMat(MAT_LEAVES), r);
            }
            p.radius = h + 3.f;
            p.lodDistance = 400.f;
            p.foliage = true;
            break;
        }
        case PROP_SAWGRASS: {
            for (int k = 0; k < 6; k++) {
                float a = r.f() * kPi;
                card(m, vec3(r.range(-0.8f, 0.8f), r.range(-0.8f, 0.8f), -0.1f), vec3(cosf(a), sinf(a), 0), vec3(0, 0, 1), r.range(1.2f, 2.0f), r.range(0.9f, 1.6f),
                     foliageColor(vec3(1.f, 0.95f, 0.8f), FOL_GRASS), makeMat(MAT_LEAVES));
            }
            p.radius = 2.f;
            p.lodDistance = 90.f;
            p.castsShadow = false;
            p.foliage = true;
            break;
        }
        case PROP_BENCH: {
            u32 wood = packRGBA8(0.6f, 0.45f, 0.3f, 1);
            m.box(vec3(0, 0, 0.45f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.9f, 0.22f, 0.03f), wood, makeMat(MAT_WOOD), true);
            m.box(vec3(0, 0.2f, 0.75f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.9f, 0.02f, 0.2f), wood, makeMat(MAT_WOOD), true);
            for (int s = -1; s <= 1; s += 2) m.box(vec3(s * 0.8f, 0, 0.22f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.03f, 0.2f, 0.22f), packRGBA8(0.2f, 0.2f, 0.2f, 1), metal, true);
            p.radius = 1.2f;
            p.lodDistance = 80.f;
            break;
        }
        case PROP_BIN: {
            m.cylinder(vec3(0, 0, 0), 0.28f, 0.3f, 0.95f, 10, packRGBA8(0.15f, 0.3f, 0.2f, 1), metal, true);
            p.radius = 0.6f;
            p.lodDistance = 70.f;
            break;
        }
        case PROP_HYDRANT: {
            u32 c = packRGBA8(0.8f, 0.7f, 0.1f, 1);
            m.cylinder(vec3(0, 0, 0), 0.13f, 0.12f, 0.6f, 8, c, metal, true);
            m.cylinder(vec3(0, 0, 0.6f), 0.12f, 0.05f, 0.15f, 8, c, metal, true);
            m.box(vec3(0, 0, 0.45f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.2f, 0.05f, 0.05f), c, metal, false);
            p.radius = 0.5f;
            p.lodDistance = 60.f;
            break;
        }
        case PROP_BUS_STOP: {
            // Shelter (also used by the transit agent's stops): frame, roof, glass back wall, bench, backlit ad box on the right end.
            // The ad glows like a real lightbox (a few percent of a lamp); the prop shader adds the night boost.
            u32 frame = packRGBA8(0.25f, 0.25f, 0.27f, 1);
            for (int s = -1; s <= 1; s += 2) m.box(vec3(s * 1.6f, 0.5f, 1.2f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.05f, 0.05f, 1.2f), frame, metal, true);
            m.box(vec3(0, 0.2f, 2.45f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1.8f, 0.8f, 0.05f), frame, metal, true);
            m.box(vec3(0, 0.95f, 1.3f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1.6f, 0.02f, 1.0f), packRGBA8(0.6f, 0.75f, 0.8f, 1), makeMat(MAT_GLASS), true);
            // ad box: dark metal case, lit poster on both faces
            m.box(vec3(1.62f, 0.3f, 1.3f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.06f, 0.62f, 0.95f), frame, metal, true);
            propAdPanel(m, vec3(1.685f, 0.3f, 1.3f), vec3(0, 1, 0), vec3(0, 0, 1), 1.12f, 1.72f, variant * 5 + 3, 0.02f);
            propAdPanel(m, vec3(1.555f, 0.3f, 1.3f), vec3(0, -1, 0), vec3(0, 0, 1), 1.12f, 1.72f, variant * 5 + 11, 0.02f);
            m.box(vec3(0, 0.6f, 0.45f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1.2f, 0.2f, 0.03f), packRGBA8(0.5f, 0.5f, 0.5f, 1), metal, true);
            // roof edge light strip (soft)
            m.quadFacing(vec3(-1.7f, -0.55f, 2.39f), vec3(1.7f, -0.55f, 2.39f), vec3(1.7f, 0.9f, 2.39f), vec3(-1.7f, 0.9f, 2.39f), vec2(0, 0), vec2(1, 0), vec2(1, 1),
                         vec2(0, 1), packRGBA8(1.f, 0.95f, 0.85f, 0.012f), makeMat(MAT_EMISSIVE), vec3(0, 0, -1));
            p.radius = 2.5f;
            p.lodDistance = 150.f;
            break;
        }
        case PROP_POWER_POLE: {
            // Wooden distribution pole: crossarm (local x across the street) with three pin insulators at x = -1.1 / 0 / +1.1,
            // z 9.5 (roadmesh.cpp strings the conductors there), telecom cable at 7.2 m; transformer can on variant 1
            u32 wood = packRGBA8(0.36f, 0.29f, 0.21f, 1), steel = packRGBA8(0.5f, 0.52f, 0.55f, 1), ins = packRGBA8(0.55f, 0.62f, 0.55f, 1);
            tube(m, {vec3(0, 0, -0.5f), vec3(0, 0, 10.f)}, {0.16f, 0.12f}, 7, wood, makeMat(MAT_WOOD));
            m.box(vec3(0, 0, 9.3f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1.3f, 0.06f, 0.06f), wood, makeMat(MAT_WOOD), true);
            for (int k = -1; k <= 1; k++) m.cylinder(vec3(k * 1.1f, 0, 9.36f), 0.05f, 0.035f, 0.16f, 6, ins, makeMat(MAT_GLASS), true);
            // braces
            tube(m, {vec3(0, 0, 8.7f), vec3(0.8f, 0, 9.25f)}, {0.025f, 0.025f}, 4, steel, metal);
            tube(m, {vec3(0, 0, 8.7f), vec3(-0.8f, 0, 9.25f)}, {0.025f, 0.025f}, 4, steel, metal);
            // telecom clamp (field side) and ground wire
            m.box(vec3(-0.2f, 0, 7.2f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.08f, 0.1f, 0.06f), steel, metal, true);
            m.box(vec3(0, 0.13f, 4.f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.015f, 0.015f, 4.2f), packRGBA8(0.3f, 0.3f, 0.3f, 1), metal, false);
            if (variant & 1) {
                m.cylinder(vec3(0.42f, 0, 7.7f), 0.28f, 0.28f, 0.9f, 10, packRGBA8(0.55f, 0.57f, 0.58f, 1), metal, true);
                m.box(vec3(0.25f, 0, 8.1f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.1f, 0.08f, 0.2f), steel, metal, true);
            }
            if (variant & 2) {
                // cobra-head street lamp on a bracket arm over the street (+x)
                u32 alu = packRGBA8(0.62f, 0.63f, 0.64f, 1);
                tube(m, {vec3(0.1f, 0, 7.35f), vec3(0.9f, 0, 7.62f), vec3(1.75f, 0, 7.72f)}, {0.045f, 0.04f, 0.035f}, 6, alu, metal);
                m.box(vec3(1.95f, 0, 7.66f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.34f, 0.16f, 0.08f), alu, metal, true);
                m.quadFacing(vec3(1.7f, -0.12f, 7.575f), vec3(2.22f, -0.12f, 7.575f), vec3(2.22f, 0.12f, 7.575f), vec3(1.7f, 0.12f, 7.575f), vec2(0, 0), vec2(1, 0),
                             vec2(1, 1), vec2(0, 1), packRGBA8(1.f, 0.8f, 0.5f, 0.6f), makeMat(MAT_EMISSIVE), vec3(0, 0, -1));
            }
            p.radius = 10.f;
            p.lodDistance = 300.f;
            break;
        }
        case PROP_UMBRELLA: {
            vec3 c = hsvToRgb(r.f(), 0.6f, 0.9f);
            m.cylinder(vec3(0, 0, 0), 0.03f, 0.03f, 2.2f, 5, W, metal, false);
            m.cylinder(vec3(0, 0, 1.9f), 1.3f, 0.02f, 0.45f, 12, packRGBA8(c.x, c.y, c.z, 1), makeMat(MAT_FABRIC), false);
            p.radius = 2.f;
            p.lodDistance = 100.f;
            break;
        }
        case PROP_LIFEGUARD_TOWER: {
            vec3 c = hsvToRgb(r.f(), 0.55f, 0.95f);
            for (int k = 0; k < 4; k++)
                m.box(vec3((k & 1) ? 1.f : -1.f, (k & 2) ? 1.f : -1.f, 1.1f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.08f, 0.08f, 1.1f), W, makeMat(MAT_WOOD), true);
            m.box(vec3(0, 0, 2.3f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1.3f, 1.3f, 0.1f), W, makeMat(MAT_WOOD), true);
            m.box(vec3(0, 0, 3.2f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1.1f, 1.1f, 0.8f), packRGBA8(c.x, c.y, c.z, 1), makeMat(MAT_WOOD_SIDING), true);
            MeshData tmp;
            m.box(vec3(0, 0, 4.15f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1.4f, 1.4f, 0.1f), packRGBA8(0.9f, 0.9f, 0.9f, 1), makeMat(MAT_ROOF_METAL), true);
            p.radius = 4.f;
            p.lodDistance = 200.f;
            break;
        }
        case PROP_DUMPSTER: {
            // front-load container: steel body on casters, sloped front, two black plastic lids, fork pockets
            vec3 c = variant ? vec3(0.1f, 0.3f, 0.5f) : vec3(0.15f, 0.35f, 0.2f);
            u32 body = packRGBA8(c.x, c.y, c.z, 1), dark = packRGBA8(0.06f, 0.06f, 0.06f, 1);
            m.box(vec3(0, 0.08f, 0.68f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.9f, 0.52f, 0.55f), body, metal, true);
            vec3 sl = normalize(vec3(0, -0.5f, 1.f));
            m.box(vec3(0, -0.5f, 0.55f), vec3(1, 0, 0), normalize(cross(sl, vec3(1, 0, 0))), sl, vec3(0.9f, 0.04f, 0.42f), body, metal, true);
            for (int s = -1; s <= 1; s += 2) {
                m.box(vec3(s * 0.45f, 0.05f, 1.26f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.12f)), normalize(vec3(0, -0.12f, 1)), vec3(0.44f, 0.62f, 0.03f), dark,
                      makeMat(MAT_PLASTIC), true);
                m.box(vec3(s * 0.93f, 0.1f, 0.95f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.04f, 0.32f, 0.08f), body, metal, true);
                for (int t = -1; t <= 1; t += 2) m.cylinder(vec3(s * 0.75f, t * 0.4f, 0.f), 0.07f, 0.07f, 0.13f, 6, dark, makeMat(MAT_RUBBER), true);
            }
            p.radius = 1.2f;
            p.lodDistance = 90.f;
            break;
        }
        case PROP_NEWS_BOX: {
            const vec3 cols[4] = {vec3(0.75f, 0.1f, 0.08f), vec3(0.1f, 0.3f, 0.65f), vec3(0.95f, 0.75f, 0.1f), vec3(0.92f, 0.92f, 0.9f)};
            vec3 c = cols[variant & 3];
            u32 body = packRGBA8(c.x, c.y, c.z, 1);
            m.box(vec3(0, 0, 0.25f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.06f, 0.06f, 0.25f), packRGBA8(0.2f, 0.2f, 0.2f, 1), metal, true);
            m.box(vec3(0, 0, 0.9f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.26f, 0.22f, 0.42f), body, metal, true);
            m.quad(vec3(-0.2f, -0.225f, 0.85f), vec3(0.2f, -0.225f, 0.85f), vec3(0.2f, -0.225f, 1.2f), vec3(-0.2f, -0.225f, 1.2f), vec2(0, 0), vec2(1, 0), vec2(1, 1),
                   vec2(0, 1), packRGBA8(0.2f, 0.25f, 0.28f, 1), makeMat(MAT_GLASS));
            m.box(vec3(0, -0.23f, 0.62f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.2f, 0.01f, 0.03f), packRGBA8(0.15f, 0.15f, 0.15f, 1), metal, true);
            p.radius = 0.8f;
            p.lodDistance = 70.f;
            break;
        }
        case PROP_PARKING_METER: {
            u32 gray = packRGBA8(0.45f, 0.47f, 0.5f, 1);
            m.cylinder(vec3(0, 0, 0), 0.04f, 0.04f, 1.15f, 6, gray, metal, true);
            for (int s = -1; s <= 1; s += 2) {
                m.box(vec3(s * 0.12f, 0, 1.28f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.1f, 0.08f, 0.14f), packRGBA8(0.25f, 0.27f, 0.3f, 1), metal, true);
                m.quad(vec3(s * 0.12f - 0.06f, -0.081f, 1.3f), vec3(s * 0.12f + 0.06f, -0.081f, 1.3f), vec3(s * 0.12f + 0.06f, -0.081f, 1.38f),
                       vec3(s * 0.12f - 0.06f, -0.081f, 1.38f), vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), packRGBA8(0.3f, 0.35f, 0.3f, 1), makeMat(MAT_GLASS));
            }
            p.radius = 0.5f;
            p.lodDistance = 60.f;
            break;
        }
        case PROP_PLANTER: {
            if ((variant & 1) == 0) {
                m.box(vec3(0, 0, 0.3f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.9f, 0.4f, 0.3f), packRGBA8(0.75f, 0.73f, 0.68f, 1), makeMat(MAT_CONCRETE), true);
                for (int k = 0; k < 4; k++)
                    leafCluster(m, vec3(-0.6f + k * 0.4f, r.range(-0.1f, 0.1f), 0.75f), 0.7f,
                                foliageColor(k == 2 ? vec3(1.f, 0.4f, 0.55f) : vec3(0.8f, 1.f, 0.7f), k == 2 ? FOL_FLOWERS : FOL_BROADLEAF), makeMat(MAT_LEAVES), r);
            } else {
                m.cylinder(vec3(0, 0, 0), 0.45f, 0.55f, 0.75f, 12, packRGBA8(0.72f, 0.45f, 0.3f, 1), makeMat(MAT_CONCRETE), true);
                tube(m, {vec3(0, 0, 0.7f), vec3(0.1f, 0, 2.2f)}, {0.07f, 0.05f}, 6, packRGBA8(0.5f, 0.42f, 0.32f, 1), makeMat(MAT_BARK));
                for (int k = 0; k < 7; k++) {
                    float a = kTwoPi * k / 7;
                    frond(m, vec3(0.1f, 0, 2.2f), normalize(vec3(cosf(a), sinf(a), 0.35f)), 1.2f, 0.35f, 0.3f, foliageColor(vec3(0.9f, 1.f, 0.8f), FOL_PALM_FROND));
                }
            }
            p.radius = 1.4f;
            p.lodDistance = 90.f;
            p.foliage = true;
            break;
        }
        case PROP_BOLLARD: {
            m.cylinder(vec3(0, 0, 0), 0.1f, 0.1f, 0.9f, 8, packRGBA8(0.3f, 0.3f, 0.32f, 1), metal, true);
            m.cylinder(vec3(0, 0, 0.78f), 0.105f, 0.105f, 0.08f, 8, packRGBA8(0.95f, 0.75f, 0.1f, 1), metal, false);
            p.radius = 0.3f;
            p.lodDistance = 60.f;
            break;
        }
        case PROP_TRASH_BAGS: {
            for (int k = 0; k < 5; k++) {
                float a = r.f() * kTwoPi;
                vec3 c(cosf(a) * r.range(0.f, 0.45f), sinf(a) * r.range(0.f, 0.35f), 0.22f + (k == 4 ? 0.3f : 0.f));
                vec3 ax = normalize(vec3(cosf(a + 0.5f), sinf(a + 0.5f), r.range(-0.2f, 0.2f)));
                vec3 ay = normalize(cross(vec3(0, 0, 1), ax));
                vec3 col = (k % 3 == 0) ? vec3(0.1f, 0.2f, 0.12f) : vec3(0.05f);
                m.box(c, ax, ay, normalize(cross(ax, ay)), vec3(0.3f, 0.24f, 0.22f), packRGBA8(col.x, col.y, col.z, 1), makeMat(MAT_RUBBER), true);
                m.box(c + vec3(0, 0, 0.24f), ax, ay, normalize(cross(ax, ay)), vec3(0.08f, 0.06f, 0.05f), packRGBA8(col.x, col.y, col.z, 1), makeMat(MAT_RUBBER), false);
            }
            p.radius = 0.9f;
            p.lodDistance = 60.f;
            p.castsShadow = false;
            break;
        }
        case PROP_AC_UNIT: {
            m.box(vec3(0, 0, 0.45f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.45f, 0.45f, 0.4f), packRGBA8(0.82f, 0.82f, 0.78f, 1), metal, false);
            m.quad(vec3(-0.34f, -0.34f, 0.855f), vec3(0.34f, -0.34f, 0.855f), vec3(0.34f, 0.34f, 0.855f), vec3(-0.34f, 0.34f, 0.855f), vec2(0, 0), vec2(1, 0), vec2(1, 1),
                   vec2(0, 1), packRGBA8(0.12f, 0.12f, 0.12f, 1), makeMat(MAT_METAL_BRUSHED));
            m.box(vec3(0, 0, 0.03f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.55f, 0.55f, 0.04f), packRGBA8(0.7f, 0.7f, 0.68f, 1), makeMat(MAT_CONCRETE), false);
            p.radius = 0.8f;
            p.lodDistance = 70.f;
            break;
        }
        case PROP_BARRIER: {
            // water-filled plastic barrier segment (orange/white)
            u32 c0 = (variant & 1) ? packRGBA8(0.95f, 0.95f, 0.93f, 1) : packRGBA8(1.f, 0.42f, 0.08f, 1);
            m.box(vec3(0, 0, 0.25f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.95f, 0.26f, 0.25f), c0, makeMat(MAT_PLASTIC), false);
            m.box(vec3(0, 0, 0.72f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.95f, 0.16f, 0.22f), c0, makeMat(MAT_PLASTIC), false);
            m.box(vec3(0, 0, 0.72f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.96f, 0.17f, 0.05f), packRGBA8(0.95f, 0.95f, 0.95f, 1), makeMat(MAT_PLASTIC), false);
            p.radius = 1.1f;
            p.lodDistance = 90.f;
            break;
        }
        case PROP_BIKE_RACK: {
            u32 steel = packRGBA8(0.55f, 0.57f, 0.6f, 1);
            for (int k = 0; k < 4; k++) {
                float x = -0.9f + k * 0.6f;
                tube(m, {vec3(x, -0.3f, 0.f), vec3(x, -0.3f, 0.65f), vec3(x, -0.15f, 0.82f), vec3(x, 0.15f, 0.82f), vec3(x, 0.3f, 0.65f), vec3(x, 0.3f, 0.f)},
                     {0.025f, 0.025f, 0.025f, 0.025f, 0.025f, 0.025f}, 5, steel, makeMat(MAT_METAL_BRUSHED));
            }
            m.box(vec3(0, 0, 0.02f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1.05f, 0.03f, 0.02f), steel, metal, false);
            p.radius = 1.2f;
            p.lodDistance = 60.f;
            break;
        }
        case PROP_CONE: {
            u32 orange = packRGBA8(1.f, 0.35f, 0.05f, 1);
            m.box(vec3(0, 0, 0.02f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.19f, 0.19f, 0.02f), packRGBA8(0.1f, 0.1f, 0.1f, 1), makeMat(MAT_RUBBER), false);
            m.cylinder(vec3(0, 0, 0.04f), 0.15f, 0.1f, 0.24f, 10, orange, makeMat(MAT_PLASTIC), false);
            m.cylinder(vec3(0, 0, 0.28f), 0.1f, 0.075f, 0.12f, 10, packRGBA8(0.95f, 0.95f, 0.95f, 1), makeMat(MAT_PLASTIC), false);
            m.cylinder(vec3(0, 0, 0.4f), 0.075f, 0.02f, 0.3f, 10, orange, makeMat(MAT_PLASTIC), false);
            p.radius = 0.4f;
            p.lodDistance = 70.f;
            p.castsShadow = false;
            break;
        }
        case PROP_MAILBOX: {
            // curbside collection box (Palmera Post): rounded top, legs, slot
            u32 body = packRGBA8(0.08f, 0.22f, 0.4f, 1);
            m.box(vec3(0, 0, 0.72f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.3f, 0.26f, 0.38f), body, metal, true);
            std::vector<vec3> arc;
            for (int k = 0; k <= 8; k++) {
                float a = kPi * k / 8;
                arc.push_back(vec3(0, -cosf(a) * 0.26f, 1.1f + sinf(a) * 0.14f));
            }
            for (int k = 0; k < 8; k++) {
                vec3 a0 = arc[k], a1 = arc[k + 1];
                vec3 n = normalize(vec3(0, (a0.y + a1.y) * 0.5f, ((a0.z + a1.z) * 0.5f - 1.1f)));
                m.quadFacing(a0 + vec3(-0.3f, 0, 0), a1 + vec3(-0.3f, 0, 0), a1 + vec3(0.3f, 0, 0), a0 + vec3(0.3f, 0, 0), vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1),
                             body, metal, n);
            }
            for (int s = -1; s <= 1; s += 2)
                for (int t = -1; t <= 1; t += 2)
                    m.box(vec3(s * 0.24f, t * 0.2f, 0.17f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.03f, 0.03f, 0.17f), packRGBA8(0.15f, 0.15f, 0.17f, 1),
                          metal, false);
            m.box(vec3(0, -0.262f, 1.0f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.16f, 0.005f, 0.025f), packRGBA8(0.05f, 0.05f, 0.05f, 1), metal, false);
            propText(m, "POST", vec3(-0.17f, -0.265f, 0.62f), vec3(1, 0, 0), vec3(0, 0, 1), 0.1f, packRGBA8(0.95f, 0.95f, 0.95f, 1), makeMat(MAT_PAINT_WHITE));
            p.radius = 0.6f;
            p.lodDistance = 70.f;
            break;
        }
        case PROP_STREET_TREE: {
            // shade tree in a square cast-iron grate; variant 2 adds a steel tree guard
            float h = r.range(5.5f, 7.5f);
            u32 bark = packRGBA8(0.42f, 0.35f, 0.27f, 1);
            tube(m, {vec3(0, 0, -0.2f), vec3(r.range(-0.15f, 0.15f), r.range(-0.15f, 0.15f), h * 0.45f)}, {0.14f, 0.1f}, 7, bark, makeMat(MAT_BARK), 2.f);
            for (int k = 0; k < 4; k++) {
                float a = kTwoPi * k / 4 + r.f();
                vec3 b0(0, 0, h * 0.42f), b1 = b0 + vec3(cosf(a) * 1.2f, sinf(a) * 1.2f, h * 0.2f);
                tube(m, {b0, b1}, {0.07f, 0.035f}, 5, bark, makeMat(MAT_BARK), 2.f);
            }
            vec3 lc(r.range(0.85f, 1.05f), r.range(0.95f, 1.1f), 0.8f);
            for (int k = 0; k < 11; k++) {
                vec3 d = r.onSphere();
                d.z *= 0.6f;
                leafCluster(m, vec3(0, 0, h * 0.7f) + d * 1.7f, 1.5f + r.range(0.f, 0.4f), foliageColor(lc * r.range(0.9f, 1.05f), FOL_BROADLEAF), makeMat(MAT_LEAVES), r);
            }
            u32 iron = packRGBA8(0.12f, 0.12f, 0.13f, 1);
            m.quadFacing(vec3(-0.65f, -0.65f, 0.01f), vec3(0.65f, -0.65f, 0.01f), vec3(0.65f, 0.65f, 0.01f), vec3(-0.65f, 0.65f, 0.01f), vec2(0, 0), vec2(1.3f, 0),
                         vec2(1.3f, 1.3f), vec2(0, 1.3f), iron, makeMat(MAT_METAL_BRUSHED), vec3(0, 0, 1));
            for (int k = 0; k < 4; k++) {
                float a = kHalfPi * k;
                vec3 ax(cosf(a), sinf(a), 0), ay(-sinf(a), cosf(a), 0);
                m.box(ax * 0.62f + vec3(0, 0, 0.012f), ay, ax, vec3(0, 0, 1), vec3(0.65f, 0.03f, 0.012f), iron, metal, false);
            }
            if (variant == 2)
                for (int k = 0; k < 6; k++) {
                    float a = kTwoPi * k / 6;
                    m.box(vec3(cosf(a) * 0.45f, sinf(a) * 0.45f, 0.7f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.015f, 0.015f, 0.7f), iron, metal, false);
                }
            p.radius = h + 2.f;
            p.lodDistance = 260.f;
            p.foliage = true;
            break;
        }
        case PROP_SIGNAL_SPAN: {
            // span-wire signal head hung from a messenger wire: hanger at the top (z 0 = wire attachment), lamps facing -y;
            // variant 1 is a doghouse-free twin (two heads side by side for wide approaches)
            u32 housing = packRGBA8(0.72f, 0.62f, 0.1f, 1), dark = packRGBA8(0.08f, 0.08f, 0.08f, 1);
            int heads = variant == 1 ? 2 : 1;
            m.box(vec3(0, 0, -0.15f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.04f, 0.04f, 0.15f), dark, metal, true);
            if (heads == 2) m.box(vec3(0, 0, -0.32f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.45f, 0.04f, 0.03f), dark, metal, true);
            for (int hI = 0; hI < heads; hI++) {
                float x = heads == 1 ? 0.f : (hI ? 0.4f : -0.4f);
                vec3 hc(x, 0, -0.95f);
                m.box(hc, vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.2f, 0.16f, 0.62f), housing, metal, true);
                m.box(hc + vec3(0, 0.12f, 0), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.3f, 0.02f, 0.72f), dark, metal, true);
                for (int l = 0; l < 3; l++) {
                    vec3 lc = hc + vec3(0, -0.17f, 0.38f - l * 0.38f);
                    vec3 col = l == 0 ? vec3(1, 0.1f, 0.05f) : (l == 1 ? vec3(1, 0.65f, 0.05f) : vec3(0.1f, 1, 0.45f));
                    float aId = (l + 1) * 0.25f;
                    u32 base = (u32)m.verts.size();
                    for (int k = 0; k < 10; k++) {
                        float a = kTwoPi * k / 10.f;
                        m.addVertex(lc + vec3(cosf(a) * 0.13f, 0, sinf(a) * 0.13f), vec3(0, -1, 0), vec3(1, 0, 0), vec2(0, 0), packRGBA8(col.x, col.y, col.z, aId),
                                    makeMat(MAT_EMISSIVE, 1));
                    }
                    for (int k = 1; k + 1 < 10; k++) m.tri(base, base + k + 1, base + k);
                    m.box(lc + vec3(0, -0.28f, 0.1f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.15f, 0.12f, 0.015f), dark, metal, true);
                }
            }
            p.radius = 2.f;
            p.lodDistance = 260.f;
            break;
        }
        case PROP_WORK_SIGN: {
            // A-frame "ROAD WORK" sign (orange diamond panels)
            u32 legs = packRGBA8(0.2f, 0.2f, 0.2f, 1);
            for (int s = -1; s <= 1; s += 2) {
                vec3 up = normalize(vec3(0, s * 0.3f, 1.f));
                vec3 base(0, s * 0.35f, 0.f);
                for (int t = -1; t <= 1; t += 2)
                    tube(m, {base + vec3(t * 0.4f, 0, 0), base + vec3(t * 0.4f, 0, 0) + up * 1.3f}, {0.02f, 0.02f}, 4, legs, metal);
                vec3 c = base + up * 0.95f;
                vec3 n(0, (float)s, 0.3f);
                vec3 rt(s > 0 ? 1.f : -1.f, 0, 0);
                vec3 dn = normalize(cross(n, rt));
                (void)dn;
                vec3 d0 = c + up * 0.42f, d1 = c + rt * 0.42f, d2 = c - up * 0.42f, d3 = c - rt * 0.42f;
                m.quadFacing(d0, d1, d2, d3, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), packRGBA8(1.f, 0.5f, 0.05f, 1), makeMat(MAT_PAINT_WHITE), normalize(n));
                vec3 tn = normalize(n);
                propText(m, "ROAD", c + tn * 0.01f - rt * 0.2f + up * 0.02f, rt, up, 0.12f, packRGBA8(0.05f, 0.05f, 0.05f, 1), makeMat(MAT_PAINT_WHITE));
                propText(m, "WORK", c + tn * 0.01f - rt * 0.2f - up * 0.16f, rt, up, 0.12f, packRGBA8(0.05f, 0.05f, 0.05f, 1), makeMat(MAT_PAINT_WHITE));
            }
            p.radius = 1.2f;
            p.lodDistance = 90.f;
            break;
        }
        default: {
            m.box(vec3(0, 0, 0.5f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.3f, 0.3f, 0.5f), W, metal, true);
            p.radius = 1.f;
            p.lodDistance = 60.f;
            break;
        }
    }
}

// ------------------------------------------------------------------------------------------------ hedges and topiary
// Garden and park greenery built into the cell mesh (MAT_LEAVES, tinted by the vertex colour), shared by facadedetail.cpp
// (house and villa gardens) and landmarks.cpp (formal parterres). Styles: HEDGE_CLIPPED (rounded top, small leaf lumps),
// HEDGE_FORMAL (crisp, park parterres), HEDGE_WILD (untrimmed: big lumps, ragged uneven top, width wandering).
enum HedgeStyle : int { HEDGE_CLIPPED = 0, HEDGE_FORMAL, HEDGE_WILD };

namespace hedge_detail {
struct Style {
    float step, big, small, gb, round, taper;
};
inline Style styleOf(int style, float W) {
    switch (style) {
        case HEDGE_FORMAL: return {0.6f, 0.018f, 0.012f, 0.15f, Min(0.08f, W * 0.2f), 0.97f};
        case HEDGE_WILD: return {0.45f, 0.15f, 0.06f, 0.1f, W * 0.7f, 0.9f};
        default: return {0.6f, 0.05f, 0.028f, 0.2f, Min(0.22f, W * 0.45f), 0.93f};
    }
}
// Cheap smooth value noise in [-1, 1] (hash lattice, smoothstep blend): hedges are built on streaming threads per cell
inline float vnoise2(float x, float y, u32 seed) {
    float fx0 = floorf(x), fy0 = floorf(y);
    int xi = (int)fx0, yi = (int)fy0;
    float fx = x - fx0, fy = y - fy0;
    fx = fx * fx * (3.f - 2.f * fx);
    fy = fy * fy * (3.f - 2.f * fy);
    float a = hashToFloat(hash3i(xi, yi, (int)seed)), b = hashToFloat(hash3i(xi + 1, yi, (int)seed));
    float c = hashToFloat(hash3i(xi, yi + 1, (int)seed)), d = hashToFloat(hash3i(xi + 1, yi + 1, (int)seed));
    return Lerp(Lerp(a, b, fx), Lerp(c, d, fx), fy) * 2.f - 1.f;
}
// leaf-mass displacement over surface coordinates (u along, v around, metres): lumps plus leaf-cluster scale
inline float lumps(float u, float v, u32 seed, float big, float small) {
    return vnoise2(u * 1.7f, v * 1.7f, seed) * big + vnoise2(u * 4.6f + 31.f, v * 4.6f, seed + 17u) * small;
}
inline u32 leafC(vec3 c) { return packRGBA8(Saturate(c.x), Saturate(c.y), Saturate(c.z), 1.f); }
}  // namespace hedge_detail

// Leafy hedge from ground point a to b, height h and width wd: a swept profile displaced into lumps (rounded or ragged
// top, the leaf mass lifted off the ground over a dark core with stems showing), shaded in the vertex colours: hollows
// and the base darker, new growth lighter on clipped tops, patchy tones along the run. Follows the terrain. Returns the
// number of vertices written. zFixed (when above -1e8) replaces the terrain height (hedges on plazas and decks).
size_t hedgeMesh(MeshData& m, vec3 org, const WorldMap& map, vec2 a, vec2 b, float h, float wd, int style, vec3 tint, u32 seed, float zFixed = -1e9f) {
    using namespace hedge_detail;
    size_t v0 = m.verts.size();
    float L = length(b - a);
    if (L < 0.3f || h < 0.25f) return 0;
    vec2 t2 = (b - a) / L;
    vec3 T(t2, 0.f), N(perp(t2), 0.f), Z(0, 0, 1);
    float W = wd * 0.5f;
    Style st = styleOf(style, W);
    bool wild = style == HEDGE_WILD, clipped = style == HEDGE_CLIPPED;
    const int NP = 13;
    int ns = Max(2, (int)ceilf(L / st.step)) + 1;
    std::vector<vec3> P((size_t)ns * NP), Nr((size_t)ns * NP);
    std::vector<float> D((size_t)ns * NP), gz(ns), wk(ns);
    std::vector<vec2> prof((size_t)ns * NP);
    for (int k = 0; k < ns; k++) {
        float s = L * k / (ns - 1);
        vec2 p2 = a + t2 * s;
        gz[k] = zFixed > -1e8f ? zFixed : map.heightAt(p2.x, p2.y);
        float e = Min(s, L - s), f = SmoothStep(0.f, 0.4f, e);
        float su = dot(p2, t2);   // world-anchored distance along the run (pieces of one hedge line up)
        float nw = vnoise2(su * 0.45f, 3.1f, seed), nh = vnoise2(su * 0.33f, 7.7f, seed + 5u);
        float w = W * (wild ? 1.f + 0.2f * nw : 1.f + 0.02f * nw) * (0.8f + 0.2f * f);
        float hh = wild ? h * (1.f + 0.14f * nh) + 0.1f * vnoise2(su * 1.1f, 1.3f, seed + 9u) : h * (1.f + 0.015f * nh);
        hh *= 0.94f + 0.06f * f;
        wk[k] = w;
        float r = Min(st.round, Min(hh - st.gb, w) * 0.85f), wt = w * st.taper;
        float dome = wild ? r * 0.25f : r * 0.08f;
        vec2 pr[NP] = {vec2(-w * 0.8f, st.gb),
                       vec2(-w, st.gb + 0.14f),
                       vec2(-(w + wt) * 0.5f, (st.gb + hh - r) * 0.5f),
                       vec2(-wt, hh - r),
                       vec2(-wt + r * 0.293f, hh - r * 0.293f),
                       vec2(-wt + r, hh),
                       vec2(0.f, hh + dome),
                       vec2(wt - r, hh),
                       vec2(wt - r * 0.293f, hh - r * 0.293f),
                       vec2(wt, hh - r),
                       vec2((w + wt) * 0.5f, (st.gb + hh - r) * 0.5f),
                       vec2(w, st.gb + 0.14f),
                       vec2(w * 0.8f, st.gb)};
        float arc = 0.f;
        for (int i = 0; i < NP; i++) {
            vec2 dP = pr[Min(i + 1, NP - 1)] - pr[Max(i - 1, 0)];
            vec2 on = normalize(perp(dP));   // outward in the (lateral, up) plane
            vec3 base = vec3(p2, gz[k]) + N * pr[i].x + Z * pr[i].y;
            vec3 out = N * on.x + Z * on.y;
            if (i > 0) arc += length(pr[i] - pr[i - 1]);
            float d = lumps(su, arc, seed, st.big, st.small) * ((i == 0 || i == NP - 1) ? 0.5f : 1.f);
            size_t q = (size_t)k * NP + i;
            P[q] = base + out * d;
            Nr[q] = out;
            D[q] = d;
            prof[q] = vec2(pr[i].y - st.gb, hh - st.gb);   // height above the leaf base, leaf-mass height
        }
    }
    // normals from the displaced surface, oriented outward
    for (int k = 0; k < ns; k++)
        for (int i = 0; i < NP; i++) {
            size_t q = (size_t)k * NP + i;
            vec3 ta = P[(size_t)Min(k + 1, ns - 1) * NP + i] - P[(size_t)Max(k - 1, 0) * NP + i];
            vec3 tp = P[(size_t)k * NP + Min(i + 1, NP - 1)] - P[(size_t)k * NP + Max(i - 1, 0)];
            vec3 nn = cross(ta, tp);
            if (dot(nn, Nr[q]) < 0.f) nn = -nn;
            Nr[q] = normalize(nn + Nr[q] * 0.35f);
        }
    u32 leaf = makeMat(MAT_LEAVES), bark = makeMat(MAT_BARK);
    u32 base = (u32)m.verts.size();
    float amp = st.big + st.small;
    for (int k = 0; k < ns; k++)
        for (int i = 0; i < NP; i++) {
            size_t q = (size_t)k * NP + i;
            vec3 pw = P[q];
            float zr = Saturate(prof[q].x / Max(prof[q].y, 0.1f));
            float shade = 0.6f + 0.4f * SmoothStep(0.f, 0.6f, zr);
            float ao = Clamp(1.f + D[q] / amp * 0.3f, 0.7f, 1.12f);
            float su = dot(pw.xy(), t2);
            float patch = 0.88f + 0.22f * (vnoise2(su * 0.35f, pw.z * 0.5f, seed + 3u) * 0.5f + 0.5f);
            vec3 c = tint * (shade * ao * patch);
            if (i >= 4 && i <= 8) c = c * (clipped ? vec3(1.1f, 1.13f, 0.86f) : vec3(1.04f, 1.05f, 0.96f));   // new growth on top
            if (wild && vnoise2(su * 0.9f, pw.z + (float)i, seed + 11u) > 0.45f) c = c * vec3(1.12f, 1.02f, 0.7f);   // sun-yellowed tips
            vec3 tg = normalize(T - Nr[q] * dot(T, Nr[q]));
            m.addVertex(pw - org, Nr[q], tg, vec2(su, pw.z + (float)i * 0.3f), leafC(c), leaf);
        }
    for (int k = 0; k + 1 < ns; k++)
        for (int i = 0; i + 1 < NP; i++) {
            u32 A = base + (u32)(k * NP + i), B = A + NP, C = B + 1, E = A + 1;
            m.quadIdx(A, B, C, E);
        }
    // end caps: fans from each end ring to a rounded tip
    for (int end = 0; end < 2; end++) {
        int k = end ? ns - 1 : 0;
        vec3 dir = end ? T : -T;
        vec3 ctr(0.f);
        for (int i = 0; i < NP; i++) ctr += P[(size_t)k * NP + i];
        ctr = ctr / (float)NP + dir * (wk[k] * 0.35f);
        u32 cb = m.addVertex(ctr - org, dir, N, vec2(0.f), leafC(tint * 0.85f), leaf);
        u32 r0 = (u32)m.verts.size();
        for (int i = 0; i < NP; i++) {
            size_t q = (size_t)k * NP + i;
            m.addVertex(P[q] - org, normalize(Nr[q] + dir * 1.2f), N, vec2(P[q].z, (float)i * 0.3f), leafC(tint * (0.7f + 0.2f * Saturate(prof[q].x))), leaf);
        }
        for (int i = 0; i + 1 < NP; i++) {
            vec3 fn = cross(m.verts[r0 + i].pos - m.verts[cb].pos, m.verts[r0 + i + 1].pos - m.verts[cb].pos);
            if (dot(fn, dir) >= 0.f) m.tri(cb, r0 + i, r0 + i + 1);
            else m.tri(cb, r0 + i + 1, r0 + i);
        }
        // underside closing the ring
        vec3 fn = cross(m.verts[r0 + NP - 1].pos - m.verts[cb].pos, m.verts[r0].pos - m.verts[cb].pos);
        if (dot(fn, dir) >= 0.f) m.tri(cb, r0 + NP - 1, r0);
        else m.tri(cb, r0, r0 + NP - 1);
    }
    // dark core under the leaf mass (the shaded inside of the hedge) and the stems showing below it
    u32 coreC = packRGBA8(0.3f, 0.26f, 0.19f, 1.f), stemC = packRGBA8(0.62f, 0.5f, 0.38f, 1.f);
    for (int side = -1; side <= 1; side += 2) {
        u32 cb0 = (u32)m.verts.size();
        for (int k = 0; k < ns; k++) {
            vec2 p2 = a + t2 * (L * k / (ns - 1));
            vec3 pb = vec3(p2, gz[k] - 0.06f) + N * (side * wk[k] * 0.66f);
            m.addVertex(pb - org, N * (float)side, T, vec2(dot(p2, t2), 0.f), coreC, bark);
            m.addVertex(pb + Z * (st.gb + 0.2f) - org, N * (float)side, T, vec2(dot(p2, t2), st.gb + 0.2f), coreC, bark);
        }
        for (int k = 0; k + 1 < ns; k++) {
            u32 A = cb0 + k * 2, B = A + 2;
            if (side > 0) m.quadIdx(A, A + 1, B + 1, B);
            else m.quadIdx(A, B, B + 1, A + 1);
        }
    }
    Rng r(seed ^ 0x57E4u);
    float stepStem = wild ? 0.5f : 0.7f;
    for (float s = 0.25f; s < L - 0.2f; s += stepStem * r.range(0.8f, 1.2f)) {
        vec2 p2 = a + t2 * s;
        int side = r.chance(0.5f) ? 1 : -1;
        int k = Clamp((int)roundf(s / L * (ns - 1)), 0, ns - 1);
        float g0 = zFixed > -1e8f ? zFixed : map.heightAt(p2.x, p2.y);
        vec3 pb = vec3(p2, g0 - 0.03f) + N * (side * wk[k] * r.range(0.68f, 0.78f));
        vec3 lean = N * (side * r.range(0.0f, 0.06f)) + T * r.range(-0.05f, 0.05f);
        float sw = wild ? r.range(0.03f, 0.05f) : r.range(0.025f, 0.04f);
        vec3 top = pb + Z * (st.gb + 0.08f) + lean;
        vec3 n = N * (float)side;
        m.quadFacing(pb - T * sw - org, pb + T * sw - org, top + T * sw * 0.7f - org, top - T * sw * 0.7f - org, vec2(0, 0), vec2(sw, 0), vec2(sw, 0.3f), vec2(0, 0.3f),
                     stemC, bark, n);
    }
    return m.verts.size() - v0;
}

// Displaced leaf ellipsoid (topiary balls and tiers, shrubs, climbers): centre c, radii R along ax, ay and up; su x sv
// facets. blossom (x >= 0): flowers over the sunlit upper half.
void leafBlobEx(MeshData& m, vec3 org, vec3 c, vec3 ax, vec3 ay, vec3 R, vec3 tint, u32 seed, float bump, int SU = 12, int SV = 7,
                vec3 blossom = vec3(-1.f)) {
    using namespace hedge_detail;
    vec3 az(0, 0, 1);
    u32 leaf = makeMat(MAT_LEAVES);
    u32 base = (u32)m.verts.size();
    float rr = (R.x + R.y) * 0.5f;
    for (int j = 0; j <= SV; j++) {
        float v = (float)j / SV, th = v * kPi;   // 0 top .. pi bottom
        for (int i = 0; i <= SU; i++) {
            float ph = kTwoPi * i / SU;
            vec3 n0(sinf(th) * cosf(ph), sinf(th) * sinf(ph), cosf(th));
            vec3 nr = normalize(ax * (n0.x / R.x) + ay * (n0.y / R.y) + az * (n0.z / R.z));
            vec3 p = c + ax * (n0.x * R.x) + ay * (n0.y * R.y) + az * (n0.z * R.z);
            float d = (j == 0 || j == SV) ? 0.f : lumps(kTwoPi * (float)(i % SU) / SU * rr, th * R.z, seed, bump, bump * 0.5f);   // seam-safe
            p = p + nr * d;
            float shade = 0.62f + 0.38f * Saturate(n0.z * 0.5f + 0.6f);
            vec3 col = tint * (shade * Clamp(1.f + d / Max(bump * 1.5f, 1e-3f) * 0.3f, 0.75f, 1.1f));
            if (n0.z > 0.5f) col = col * vec3(1.08f, 1.1f, 0.88f);
            if (blossom.x >= 0.f && n0.z > -0.1f && vnoise2(ph * rr * 3.f, th * R.z * 3.f, seed + 23u) > -0.15f) col = blossom * (0.8f + 0.25f * shade);
            vec3 tg = normalize(cross(az, nr) + ax * 1e-3f);
            m.addVertex(p - org, nr, tg, vec2(ph * rr, th * R.z), leafC(col), leaf);
        }
    }
    for (int j = 0; j < SV; j++)
        for (int i = 0; i < SU; i++) {
            u32 A = base + j * (SU + 1) + i, B = A + 1, C = A + SU + 1, E = C + 1;
            m.quadIdx(A, C, E, B);
        }
}
void leafBlob(MeshData& m, vec3 org, vec3 c, float rxy, float rz, vec3 tint, u32 seed, float bump) {
    leafBlobEx(m, org, c, vec3(1, 0, 0), vec3(0, 1, 0), vec3(rxy, rxy, rz), tint, seed, bump);
}

// Clipped topiary standing on the ground at p: 0 ball, 1 cone, 2 standard (ball on a clear stem), 3 tiered stem,
// 4 ball in a terracotta pot. size ~ overall height.
void topiaryMesh(MeshData& m, vec3 org, vec3 p, int kind, float size, vec3 tint, u32 seed) {
    using namespace hedge_detail;
    u32 bark = makeMat(MAT_BARK), leaf = makeMat(MAT_LEAVES);
    auto stem = [&](float h0, float h1, float r) {
        m.cylinder(p + vec3(0, 0, h0) - org, r, r * 0.8f, h1 - h0, 6, packRGBA8(0.55f, 0.44f, 0.33f, 1.f), bark, false);
    };
    switch (kind) {
        case 0: leafBlob(m, org, p + vec3(0, 0, size * 0.46f), size * 0.52f, size * 0.5f, tint, seed, size * 0.03f); break;
        case 1: {
            // cone: rings of a lathe, lumped slightly, dark at the foot
            const int SU = 12, SV = 6;
            float R = size * 0.34f;
            u32 base = (u32)m.verts.size();
            for (int j = 0; j <= SV; j++) {
                float v = (float)j / SV;
                float rr = R * (1.f - v) + 0.02f, z = size * v;
                for (int i = 0; i <= SU; i++) {
                    float ph = kTwoPi * i / SU;
                    vec3 n0 = normalize(vec3(cosf(ph) * size, sinf(ph) * size, R));
                    vec3 q = p + vec3(cosf(ph) * rr, sinf(ph) * rr, z);
                    q = q + n0 * (j == SV ? 0.f : lumps(kTwoPi * (float)(i % SU) / SU * R, z, seed, size * 0.02f, size * 0.012f));
                    vec3 col = tint * (0.62f + 0.4f * v) * (j == SV ? 1.05f : 1.f);
                    m.addVertex(q - org, n0, vec3(-sinf(ph), cosf(ph), 0.f), vec2(ph * rr, z), leafC(col), leaf);
                }
            }
            for (int j = 0; j < SV; j++)
                for (int i = 0; i < SU; i++) {
                    u32 A = base + j * (SU + 1) + i, B = A + 1, C = A + SU + 1, E = C + 1;
                    m.quadIdx(A, B, E, C);
                }
            break;
        }
        case 2:
            stem(-0.05f, size * 0.62f, 0.035f);
            leafBlob(m, org, p + vec3(0, 0, size * 0.72f), size * 0.27f, size * 0.25f, tint, seed, size * 0.02f);
            break;
        case 3:
            stem(-0.05f, size * 0.9f, 0.03f);
            leafBlob(m, org, p + vec3(0, 0, size * 0.3f), size * 0.24f, size * 0.2f, tint, seed, size * 0.018f);
            leafBlob(m, org, p + vec3(0, 0, size * 0.66f), size * 0.18f, size * 0.15f, tint, seed + 1u, size * 0.015f);
            leafBlob(m, org, p + vec3(0, 0, size * 0.92f), size * 0.11f, size * 0.1f, tint, seed + 2u, size * 0.01f);
            break;
        default: {
            // terracotta pot (tapered, rim) with a clipped ball
            float pr = size * 0.22f, ph = size * 0.32f;
            u32 potC = packRGBA8(0.78f, 0.45f, 0.3f, 1.f), potM = makeMat(MAT_CONCRETE);
            m.cylinder(p - org, pr * 0.75f, pr, ph, 12, potC, potM, true);
            m.cylinder(p + vec3(0, 0, ph - 0.05f) - org, pr * 1.08f, pr * 1.08f, 0.07f, 12, potC, potM, true);
            leafBlob(m, org, p + vec3(0, 0, ph + size * 0.3f), size * 0.34f, size * 0.32f, tint, seed, size * 0.025f);
            break;
        }
    }
}

}  // namespace World
