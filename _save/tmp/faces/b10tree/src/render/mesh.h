// Static mesh vertex format and CPU-side mesh building helpers.
#pragma once
#include "../core/math.h"

// World materials (index into the material table / texture arrays). Keep in sync with
// MaterialLibrary::init and shaders/world.hlsl.
enum MaterialId : u8 {
    MAT_ASPHALT = 0, MAT_ASPHALT_OLD, MAT_CONCRETE, MAT_SIDEWALK, MAT_CURB, MAT_PAINT_WHITE, MAT_PAINT_YELLOW,
    MAT_BRICK, MAT_STUCCO, MAT_PLASTER, MAT_WOOD_SIDING, MAT_ROOF_TILE, MAT_ROOF_SHINGLE, MAT_ROOF_METAL, MAT_ROOF_GRAVEL,
    MAT_GLASS, MAT_METAL_PAINTED, MAT_METAL_BRUSHED, MAT_PAVERS, MAT_MARBLE, MAT_STONE, MAT_CORRUGATED, MAT_FABRIC,
    MAT_WOOD, MAT_TILE_POOL, MAT_GRASS, MAT_DIRT, MAT_RUBBER, MAT_EMISSIVE, MAT_CHROME, MAT_CONCRETE_PANEL, MAT_FACADE,
    MAT_LEAVES, MAT_BARK, MAT_PALM_FROND, MAT_SAND,
    // Vehicles and characters (dynamic objects)
    MAT_CARPAINT, MAT_PLASTIC, MAT_LEATHER, MAT_LIGHT_HEAD, MAT_LIGHT_TAIL, MAT_SKIN, MAT_HAIR, MAT_CLOTH, MAT_DENIM,
    MAT_EYE, MAT_TIRE, MAT_RIM, MAT_CAR_GLASS, MAT_INTERIOR, MAT_LIGHT_INDICATOR, MAT_DECAL_TEXT,
    // see-through vehicle windows: forward-shaded after lighting (vertex colour rgb = tint, a = clarity 1 clear..0 dark)
    MAT_CAR_WINDOW,
    // Interiors (world/interiorkit.cpp): glossy ceramic floor/wall tiles (30 cm at uv = meters, tint by vertex colour),
    // matte acoustic ceiling tiles (60 cm), fine loop-pile carpet, light oak floor planks
    MAT_TILE, MAT_CEILING_TILE, MAT_CARPET, MAT_WOOD_FLOOR,
    MAT_COUNT
};

// Vertex: 36 bytes.
struct VtxStatic {
    vec3 pos;      // cell-relative position
    u32 normal;    // octahedral snorm16x2
    u32 tangent;   // octahedral snorm16x2 (bitangent sign in mat bit 31)
    vec2 uv;       // meters for tiling materials, or facade coordinates
    u32 color;     // RGBA8 tint (a = material-specific parameter, e.g. wetness mask / emissive strength)
    u32 mat;       // bits 0-7 material, 8-30 param (facade id etc.), 31 bitangent sign
};

inline u32 makeMat(u32 material, u32 param = 0, bool flipB = false) {
    return (material & 0xffu) | ((param & 0x7fffffu) << 8) | (flipB ? 0x80000000u : 0u);
}

struct MeshData {
    std::vector<VtxStatic> verts;
    std::vector<u32> indices;
    AABB bounds;
    void clear() { verts.clear(); indices.clear(); bounds = AABB(); }
    bool empty() const { return indices.empty(); }

    u32 addVertex(vec3 p, vec3 n, vec3 t, vec2 uv, u32 color, u32 mat) {
        VtxStatic v;
        v.pos = p;
        v.normal = packNormalOct(n);
        v.tangent = packNormalOct(t);
        v.uv = uv;
        v.color = color;
        v.mat = mat;
        verts.push_back(v);
        bounds.add(p);
        return (u32)verts.size() - 1;
    }
    void tri(u32 a, u32 b, u32 c) { indices.push_back(a); indices.push_back(b); indices.push_back(c); }
    // Quad with CCW winding a,b,c,d (as seen from the front).
    void quadIdx(u32 a, u32 b, u32 c, u32 d) { tri(a, b, c); tri(a, c, d); }

    // Planar quad from 4 corners (CCW from front). UVs given per corner.
    void quad(vec3 a, vec3 b, vec3 c, vec3 d, vec2 ua, vec2 ub, vec2 uc, vec2 ud, u32 color, u32 mat) {
        vec3 n = normalize(cross(b - a, d - a));
        vec3 t = normalize(b - a);
        if (length2(b - a) < 1e-10f) t = normalize(c - d);
        u32 i0 = addVertex(a, n, t, ua, color, mat), i1 = addVertex(b, n, t, ub, color, mat);
        u32 i2 = addVertex(c, n, t, uc, color, mat), i3 = addVertex(d, n, t, ud, color, mat);
        quadIdx(i0, i1, i2, i3);
    }
    // Quad whose winding is fixed so that its normal faces `facing` (robust for generated geometry).
    void quadFacing(vec3 a, vec3 b, vec3 c, vec3 d, vec2 ua, vec2 ub, vec2 uc, vec2 ud, u32 color, u32 mat, vec3 facing) {
        vec3 n = cross(b - a, d - a);
        if (dot(n, facing) < 0.f) quad(a, d, c, b, ua, ud, uc, ub, color, mat);
        else quad(a, b, c, d, ua, ub, uc, ud, color, mat);
    }
    // Wall quad between two base points with height: uv in meters (u along, v up) offset by u0/v0
    void wall(vec3 p0, vec3 p1, float h, u32 color, u32 mat, float u0 = 0, float v0 = 0) {
        float len = length(p1 - p0);
        vec3 up(0, 0, h);
        quad(p0, p1, p1 + up, p0 + up, vec2(u0, v0), vec2(u0 + len, v0), vec2(u0 + len, v0 + h), vec2(u0, v0 + h), color, mat);
    }
    // Axis-aligned or oriented box: center, half extents along axes x,y,z (unit vectors)
    void box(vec3 c, vec3 ax, vec3 ay, vec3 az, vec3 he, u32 color, u32 mat, bool bottom = false, float uvScale = 1.f) {
        vec3 X = ax * he.x, Y = ay * he.y, Z = az * he.z;
        vec3 p[8] = {c - X - Y - Z, c + X - Y - Z, c + X + Y - Z, c - X + Y - Z, c - X - Y + Z, c + X - Y + Z, c + X + Y + Z, c - X + Y + Z};
        float lx = he.x * 2 * uvScale, ly = he.y * 2 * uvScale, lz = he.z * 2 * uvScale;
        quad(p[0], p[1], p[5], p[4], vec2(0, 0), vec2(lx, 0), vec2(lx, lz), vec2(0, lz), color, mat);  // -Y
        quad(p[1], p[2], p[6], p[5], vec2(0, 0), vec2(ly, 0), vec2(ly, lz), vec2(0, lz), color, mat);  // +X
        quad(p[2], p[3], p[7], p[6], vec2(0, 0), vec2(lx, 0), vec2(lx, lz), vec2(0, lz), color, mat);  // +Y
        quad(p[3], p[0], p[4], p[7], vec2(0, 0), vec2(ly, 0), vec2(ly, lz), vec2(0, lz), color, mat);  // -X
        quad(p[4], p[5], p[6], p[7], vec2(0, 0), vec2(lx, 0), vec2(lx, ly), vec2(0, ly), color, mat);  // top
        if (bottom) quad(p[3], p[2], p[1], p[0], vec2(0, 0), vec2(lx, 0), vec2(lx, ly), vec2(0, ly), color, mat);
    }
    void boxAA(vec3 mn, vec3 mx, u32 color, u32 mat, bool bottom = false) {
        box((mn + mx) * 0.5f, vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), (mx - mn) * 0.5f, color, mat, bottom);
    }
    // Cylinder along z from base center, radius, height, segments
    void cylinder(vec3 base, float r0, float r1, float h, int seg, u32 color, u32 mat, bool cap = true) {
        u32 start = (u32)verts.size();
        for (int i = 0; i <= seg; i++) {
            float a = kTwoPi * i / seg;
            vec3 n(cosf(a), sinf(a), (r0 - r1) / Max(h, 1e-3f));
            n = normalize(n);
            vec3 t(-sinf(a), cosf(a), 0);
            float u = (float)i / seg * kTwoPi * Max(r0, r1);
            addVertex(base + vec3(cosf(a) * r0, sinf(a) * r0, 0), n, t, vec2(u, 0), color, mat);
            addVertex(base + vec3(cosf(a) * r1, sinf(a) * r1, h), n, t, vec2(u, h), color, mat);
        }
        for (int i = 0; i < seg; i++) {
            u32 a = start + i * 2, b = a + 2;
            quadIdx(a, b, b + 1, a + 1);
        }
        if (cap && r1 > 0.001f) {
            u32 c = addVertex(base + vec3(0, 0, h), vec3(0, 0, 1), vec3(1, 0, 0), vec2(0, 0), color, mat);
            u32 first = (u32)verts.size();
            for (int i = 0; i <= seg; i++) {
                float a = kTwoPi * i / seg;
                addVertex(base + vec3(cosf(a) * r1, sinf(a) * r1, h), vec3(0, 0, 1), vec3(1, 0, 0), vec2(cosf(a) * r1, sinf(a) * r1), color, mat);
            }
            for (int i = 0; i < seg; i++) tri(c, first + i, first + i + 1);
        }
    }
    // Flat polygon (convex or simple, CCW from above) at given z via ear clipping
    void polygon(const std::vector<vec3>& poly, vec3 normal, u32 color, u32 mat, float uvScale = 1.f);
    // Extruded prism walls (footprint CCW from above), from z0 to z1
    void extrudeWalls(const std::vector<vec2>& fp, float z0, float z1, u32 color, u32 mat);
    void append(const MeshData& o);
};

// Skinned vertex for characters: 44 bytes.
struct VtxSkinned {
    vec3 pos;      // bind-pose model space
    u32 normal;
    u32 tangent;
    vec2 uv;
    u32 color;     // RGBA8 tint (skin tone / cloth color)
    u32 mat;       // material id (bits 0-7) | param << 8
    u8 bones[4];
    u8 weights[4]; // unorm, sum to 255
};

struct SkinnedMeshData {
    std::vector<VtxSkinned> verts;
    std::vector<u32> indices;
    AABB bounds;
    u32 addVertex(vec3 p, vec3 n, vec3 t, vec2 uv, u32 color, u32 mat, const u8 bones[4], const u8 weights[4]) {
        VtxSkinned v;
        v.pos = p;
        v.normal = packNormalOct(n);
        v.tangent = packNormalOct(t);
        v.uv = uv;
        v.color = color;
        v.mat = mat;
        for (int i = 0; i < 4; i++) { v.bones[i] = bones[i]; v.weights[i] = weights[i]; }
        verts.push_back(v);
        bounds.add(p);
        return (u32)verts.size() - 1;
    }
    void tri(u32 a, u32 b, u32 c) { indices.push_back(a); indices.push_back(b); indices.push_back(c); }
};

// Ear-clipping triangulation of a simple polygon (CCW). Returns triangle indices into poly.
void triangulatePolygon(const std::vector<vec2>& poly, std::vector<u32>& outTris);
