// Decor vegetation placed on the GPU around the camera (props_render.cpp, shaders/propcull.hlsl csDecorPlace): the
// small plants of Miami gardens and woods that the world generator does not place one by one. Crotons and flowering
// shrubs (hibiscus, ixora) along walls, fences and hedges and in planting beds, ferns and saw palmettos on the
// forest floor, flower clumps in lawns and meadows. Meshes are built once at start-up from the prop builders'
// helpers (crossed foliage cards, arched fronds); they are drawn by the GPU-driven prop path like any prop.
// Included from renderer.cpp.
namespace Render {

namespace vegdecor {

using namespace World::propmesh_detail;

// Types (the placement shader picks them by index: keep the order in sync with propcull.hlsl)
enum DecorType : int { DECOR_CROTON = 0, DECOR_FLOWERING, DECOR_FERN, DECOR_PALMETTO, DECOR_FLOWERS, DECOR_TYPES };
const int kVariants[DECOR_TYPES] = {3, 2, 2, 2, 3};

struct DecorPrototype {
    MeshData mesh;
    float radius = 1.f, lodDistance = 60.f;
    bool shadow = true;
};

// a rounded mass of crossed leaf cards (a shrub's foliage), centred at c with half extents e
inline void leafMass(MeshData& m, vec3 c, vec3 e, int clusters, float cardSize, u32 col, Rng& r) {
    for (int k = 0; k < clusters; k++) {
        vec3 d = r.onSphere();
        d.z = fabsf(d.z) * 0.8f + 0.1f;
        vec3 p = c + vec3(d.x * e.x, d.y * e.y, d.z * e.z) * r.range(0.45f, 0.9f);
        leafCluster(m, p, cardSize * r.range(0.8f, 1.15f), col, makeMat(MAT_LEAVES), r);
    }
}

inline void stems(MeshData& m, vec3 top, int n, float spread, Rng& r) {
    u32 col = packRGBA8(0.32f, 0.25f, 0.16f, 1.f);
    for (int k = 0; k < n; k++) {
        float a = r.f() * kTwoPi;
        vec3 t = top + vec3(cosf(a), sinf(a), 0.f) * spread * r.range(0.3f, 1.f);
        tube(m, {vec3(0, 0, 0), t}, {0.018f, 0.01f}, 4, col, makeMat(MAT_BARK));
    }
}

inline void build(int type, int variant, DecorPrototype& p) {
    MeshData& m = p.mesh;
    Rng r(0xdec0u + (u64)type * 977u + (u64)variant * 131u);
    switch (type) {
        case DECOR_CROTON: {
            // variegated leaves: red-orange, yellow-green, deep green with yellow veins (the layer tint carries it)
            const vec3 tints[3] = {vec3(1.1f, 0.55f, 0.32f), vec3(1.05f, 1.f, 0.42f), vec3(0.62f, 0.95f, 0.45f)};
            float h = r.range(0.75f, 1.1f);
            stems(m, vec3(0, 0, h * 0.55f), 4, 0.25f, r);
            leafMass(m, vec3(0, 0, h * 0.58f), vec3(0.42f, 0.42f, h * 0.42f), 9, 0.42f, World::foliageColor(tints[variant % 3], World::FOL_BROADLEAF), r);
            p.radius = 0.75f;
            p.lodDistance = 70.f;
            break;
        }
        case DECOR_FLOWERING: {
            // hibiscus (red) / ixora (orange): green mass with flower clusters over the top and the sides
            const vec3 bloom[2] = {vec3(1.f, 0.25f, 0.3f), vec3(1.f, 0.55f, 0.2f)};
            float h = r.range(0.9f, 1.35f);
            stems(m, vec3(0, 0, h * 0.5f), 5, 0.3f, r);
            leafMass(m, vec3(0, 0, h * 0.55f), vec3(0.5f, 0.5f, h * 0.45f), 9, 0.45f, World::foliageColor(vec3(0.7f, 0.95f, 0.5f), World::FOL_BROADLEAF), r);
            leafMass(m, vec3(0, 0, h * 0.72f), vec3(0.48f, 0.48f, h * 0.3f), 5, 0.3f, World::foliageColor(bloom[variant % 2], World::FOL_FLOWERS), r);
            p.radius = 0.9f;
            p.lodDistance = 70.f;
            break;
        }
        case DECOR_FERN: {
            // a rosette of short arched fronds (Boston / sword fern)
            float s = variant == 0 ? 0.55f : 0.8f;
            int n = variant == 0 ? 9 : 12;
            for (int k = 0; k < n; k++) {
                float a = kTwoPi * k / n + r.range(-0.2f, 0.2f);
                float elev = r.range(0.45f, 1.0f);
                vec3 d = normalize(vec3(cosf(a) * cosf(elev), sinf(a) * cosf(elev), sinf(elev)));
                frond(m, vec3(0, 0, 0.03f), d, s * r.range(0.8f, 1.15f), r.range(0.25f, 0.45f), 0.11f * s,
                      World::foliageColor(vec3(0.62f, 1.05f, 0.45f) * r.range(0.9f, 1.05f), World::FOL_PALM_FROND));
            }
            p.radius = s;
            p.lodDistance = 45.f;
            p.shadow = false;
            break;
        }
        case DECOR_PALMETTO: {
            // saw palmetto: a few crawling stems, each with a fan of stiff leaflets
            float s = variant == 0 ? 1.0f : 1.35f;
            int stemsN = variant == 0 ? 3 : 4;
            for (int k = 0; k < stemsN; k++) {
                float a = kTwoPi * k / stemsN + r.range(-0.4f, 0.4f);
                vec3 base(cosf(a) * 0.25f * s, sinf(a) * 0.25f * s, 0.f);
                vec3 head = base + vec3(cosf(a) * 0.2f, sinf(a) * 0.2f, s * r.range(0.45f, 0.65f));
                tube(m, {base, head}, {0.03f, 0.02f}, 4, packRGBA8(0.4f, 0.36f, 0.24f, 1.f), makeMat(MAT_BARK));
                // the fan: leaflets radiating from the head over about 160 degrees, facing out
                vec3 out = normalize(vec3(cosf(a), sinf(a), 0.6f));
                vec3 side = normalize(cross(out, vec3(0, 0, 1)));
                int nl = 9;
                for (int i = 0; i < nl; i++) {
                    float u = ((float)i / (nl - 1) - 0.5f) * 2.8f;
                    vec3 d = normalize(out * cosf(u) + side * sinf(u) + vec3(0, 0, 0.35f));
                    frond(m, head, d, s * r.range(0.45f, 0.6f), 0.06f, 0.05f * s,
                          World::foliageColor(vec3(0.72f, 0.95f, 0.62f) * r.range(0.9f, 1.05f), World::FOL_PALM_FROND));
                }
            }
            p.radius = s * 1.1f;
            p.lodDistance = 60.f;
            break;
        }
        default: {
            // flower clump: a few low crossed cards of the flower layer over a little green
            const vec3 tints[3] = {vec3(1.f, 0.98f, 0.92f), vec3(1.f, 0.85f, 0.3f), vec3(0.75f, 0.5f, 1.f)};
            for (int k = 0; k < 2; k++)
                leafCluster(m, vec3(r.range(-0.1f, 0.1f), r.range(-0.1f, 0.1f), 0.12f), 0.24f,
                            World::foliageColor(vec3(0.7f, 0.95f, 0.5f), World::FOL_BROADLEAF), makeMat(MAT_LEAVES), r);
            for (int k = 0; k < 3; k++)
                leafCluster(m, vec3(r.range(-0.14f, 0.14f), r.range(-0.14f, 0.14f), r.range(0.2f, 0.3f)), 0.2f,
                            World::foliageColor(tints[variant % 3], World::FOL_FLOWERS), makeMat(MAT_LEAVES), r);
            p.radius = 0.35f;
            p.lodDistance = 40.f;
            p.shadow = false;
            break;
        }
    }
}

}  // namespace vegdecor

}  // namespace Render
