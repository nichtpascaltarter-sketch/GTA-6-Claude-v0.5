// Procedural prototype meshes for props and vegetation (built once at startup).
#include "worldtypes.h"
#include "../render/mesh.h"

namespace World {

// Foliage atlas layers (alpha-tested cards), generated on the GPU (see foliage.hlsl)
enum FoliageLayer { FOL_PALM_FROND = 0, FOL_BROADLEAF, FOL_PINE, FOL_GRASS, FOL_MANGROVE, FOL_FLOWERS, FOL_COUNT };

// In prop meshes, MAT_LEAVES/MAT_PALM_FROND geometry uses the vertex color alpha to pick the foliage layer:
// color.a = layer / 8.
inline u32 foliageColor(vec3 tint, int layer) { return packRGBA8(tint.x, tint.y, tint.z, (layer + 0.5f) / 8.f); }

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
            // trunk: swollen bole at the base, leaf-scar rings on coconut palms, slight S-curve on leaning palms
            int rings = tall ? 14 : 22;
            vec3 wob(cosf(ang + 1.3f), sinf(ang + 1.3f), 0);
            for (int i = 0; i <= rings; i++) {
                float t = (float)i / rings;
                trunk.push_back(leanDir * (lean * t * t) + wob * (tall ? 0.f : 0.25f * sinf(t * kPi)) + vec3(0, 0, h * t));
                float base = tall ? Lerp(0.34f, 0.23f, t) + (t > 0.85f ? 0.05f : 0.f) + 0.04f * sinf(t * kPi) : Lerp(0.25f, 0.16f, t);
                float bole = t < 0.1f ? (0.1f - t) * (tall ? 2.6f : 2.0f) : 0.f;
                float scar = tall ? 0.f : ((i & 1) ? 0.016f : -0.01f);
                rad.push_back(base + bole + scar);
            }
            u32 barkC = tall ? packRGBA8(0.72f, 0.7f, 0.66f, 1) : packRGBA8(r.range(0.5f, 0.6f), r.range(0.43f, 0.5f), r.range(0.34f, 0.4f), 1);
            tube(m, trunk, rad, 8, barkC, makeMat(MAT_BARK), 3.f);
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
                frond(m, top, d, len, droop, inner ? 0.62f : 0.75f, foliageColor(col, FOL_PALM_FROND));
            }
            // spear: a couple of young upright fronds
            for (int k = 0; k < 2; k++) {
                float a = r.f() * kTwoPi;
                vec3 d = normalize(vec3(cosf(a) * 0.25f, sinf(a) * 0.25f, 1.f));
                frond(m, top, d, r.range(1.6f, 2.2f), 0.05f, 0.4f, foliageColor(fc * vec3(1.05f, 1.1f, 0.9f), FOL_PALM_FROND));
            }
            // skirt of dead fronds hanging against the trunk (some specimens)
            if (!tall && (variant & 1)) {
                int nd = r.irange(3, 6);
                for (int k = 0; k < nd; k++) {
                    float a = r.f() * kTwoPi;
                    vec3 d = normalize(vec3(cosf(a) * 0.45f, sinf(a) * 0.45f, -1.f));
                    frond(m, top - vec3(0, 0, 0.3f), d, r.range(2.0f, 2.8f), 0.05f, 0.42f, foliageColor(vec3(0.75f, 0.55f, 0.3f), FOL_PALM_FROND));
                }
            }
            // coconut cluster
            if (!tall)
                for (int k = 0; k < 6; k++) {
                    float a = r.f() * kTwoPi;
                    m.box(top + vec3(cosf(a) * 0.28f, sinf(a) * 0.28f, -0.3f - r.range(0.f, 0.25f)), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.12f),
                          k & 1 ? packRGBA8(0.32f, 0.38f, 0.12f, 1) : packRGBA8(0.45f, 0.35f, 0.15f, 1), makeMat(MAT_BARK), true);
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
            // Spanish moss hanging from oak limbs (some specimens)
            if (oak && (variant & 2)) {
                for (vec3 tp : tips) {
                    int nm = r.irange(1, 2);
                    for (int j = 0; j < nm; j++) {
                        vec3 c = tp + vec3(r.range(-0.8f, 0.8f), r.range(-0.8f, 0.8f), -r.range(0.2f, 0.6f));
                        float a = r.f() * kPi;
                        float len = r.range(0.9f, 1.8f);
                        card(m, c - vec3(0, 0, len), vec3(cosf(a), sinf(a), 0), vec3(0, 0, 1), 0.7f, len, foliageColor(vec3(0.75f, 0.78f, 0.62f), FOL_GRASS),
                             makeMat(MAT_LEAVES));
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
            u32 frame = packRGBA8(0.25f, 0.25f, 0.27f, 1);
            for (int s = -1; s <= 1; s += 2) m.box(vec3(s * 1.6f, 0.5f, 1.2f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.05f, 0.05f, 1.2f), frame, metal, true);
            m.box(vec3(0, 0.2f, 2.45f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1.8f, 0.8f, 0.05f), frame, metal, true);
            m.box(vec3(0, 0.95f, 1.3f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1.6f, 0.02f, 1.0f), packRGBA8(0.6f, 0.75f, 0.8f, 1), makeMat(MAT_GLASS), true);
            // ad panel (emissive at night)
            m.box(vec3(1.62f, 0.3f, 1.3f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.04f, 0.6f, 0.9f), packRGBA8(0.9f, 0.85f, 0.7f, 0.25f), makeMat(MAT_EMISSIVE), true);
            m.box(vec3(0, 0.6f, 0.45f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1.2f, 0.2f, 0.03f), packRGBA8(0.5f, 0.5f, 0.5f, 1), metal, true);
            p.radius = 2.5f;
            p.lodDistance = 120.f;
            break;
        }
        case PROP_POWER_POLE: {
            tube(m, {vec3(0, 0, -0.5f), vec3(0, 0, 10.f)}, {0.16f, 0.12f}, 6, packRGBA8(0.35f, 0.28f, 0.2f, 1), makeMat(MAT_WOOD));
            m.box(vec3(0, 0, 9.3f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1.2f, 0.06f, 0.06f), packRGBA8(0.35f, 0.28f, 0.2f, 1), makeMat(MAT_WOOD), true);
            m.cylinder(vec3(0.4f, 0, 8.2f), 0.25f, 0.25f, 0.8f, 8, packRGBA8(0.5f, 0.52f, 0.55f, 1), metal, true);
            p.radius = 10.f;
            p.lodDistance = 250.f;
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
            vec3 c = variant ? vec3(0.1f, 0.3f, 0.5f) : vec3(0.15f, 0.35f, 0.2f);
            m.box(vec3(0, 0, 0.65f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.9f, 0.6f, 0.6f), packRGBA8(c.x, c.y, c.z, 1), metal, true);
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

}  // namespace World
