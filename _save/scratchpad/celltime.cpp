// Native preview of streamed world cells with a tiny software rasterizer (development aid, not part of the game).
// Usage: siteview out.ppm W H  x,y,z,yaw,pitch[,fov] [more views...]   (views share one world generation)
#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/core/noise.cpp"
#include "src/core/jobs.cpp"
#include "src/render/mesh.cpp"
#include "src/world/worldmap.cpp"
#include "src/world/sites.cpp"
#include "src/world/roads.cpp"
#include "src/world/roadmesh.cpp"
#include "src/world/buildings.cpp"
#include "src/world/buildmesh.cpp"
#include "src/world/propmesh.cpp"
#include "src/world/cellgen.cpp"
#include "src/world/sitegeo.cpp"
#include "src/world/airport.cpp"
#include "src/world/port.cpp"
#include "src/world/landmarks.cpp"
#include "src/world/leisure.cpp"
#include "src/world/rural.cpp"
#include "src/world/sitecell.cpp"
#include <thread>
#include <map>

using namespace World;

vec3 matColor(u32 mat, u32 vcol, bool& emissive) {
    u32 id = mat & 0xff, param = (mat >> 8) & 0x7fffff;
    vec4 vc = unpackRGBA8(vcol);
    vec3 c(0.6f);
    emissive = false;
    switch (id) {
        case MAT_ASPHALT: c = vec3(0.13f); break;
        case MAT_ASPHALT_OLD: c = vec3(0.19f); break;
        case MAT_CONCRETE: c = vec3(0.55f, 0.54f, 0.5f); break;
        case MAT_SIDEWALK: c = vec3(0.6f, 0.58f, 0.55f); break;
        case MAT_CURB: c = vec3(0.65f); break;
        case MAT_PAINT_WHITE: c = vec3(0.9f); break;
        case MAT_PAINT_YELLOW: c = vec3(0.85f, 0.65f, 0.1f); break;
        case MAT_BRICK: c = vec3(0.55f, 0.25f, 0.15f); break;
        case MAT_STUCCO: case MAT_PLASTER: c = vec3(0.85f, 0.84f, 0.8f); break;
        case MAT_WOOD_SIDING: c = vec3(0.8f); break;
        case MAT_ROOF_TILE: c = vec3(0.6f, 0.28f, 0.14f); break;
        case MAT_ROOF_SHINGLE: c = vec3(0.25f); break;
        case MAT_ROOF_METAL: c = vec3(0.6f); break;
        case MAT_ROOF_GRAVEL: c = vec3(0.5f); break;
        case MAT_GLASS: c = vec3(0.3f, 0.38f, 0.45f); break;
        case MAT_METAL_PAINTED: c = vec3(0.75f); break;
        case MAT_METAL_BRUSHED: c = vec3(0.75f); break;
        case MAT_PAVERS: c = vec3(0.6f, 0.47f, 0.35f); break;
        case MAT_MARBLE: c = vec3(0.85f); break;
        case MAT_STONE: c = vec3(0.6f, 0.55f, 0.47f); break;
        case MAT_CORRUGATED: c = vec3(0.6f); break;
        case MAT_FABRIC: c = vec3(0.9f); break;
        case MAT_WOOD: c = vec3(0.55f, 0.4f, 0.26f); break;
        case MAT_TILE_POOL: c = vec3(0.3f, 0.6f, 0.75f); break;
        case MAT_GRASS: c = vec3(0.22f, 0.38f, 0.12f); break;
        case MAT_DIRT: c = vec3(0.4f, 0.3f, 0.2f); break;
        case MAT_RUBBER: c = vec3(0.1f); break;
        case MAT_SAND: c = vec3(0.8f, 0.72f, 0.55f); break;
        case MAT_CONCRETE_PANEL: c = vec3(0.6f); break;
        case MAT_EMISSIVE: c = vec3(1.f); emissive = true; break;
        case MAT_FACADE: {
            if (gBuildings && param < gBuildings->facades.size()) {
                const FacadeGPU& f = gBuildings->facades[param];
                vec4 w = unpackRGBA8(f.wallColor), gl = unpackRGBA8(f.glassColor);
                float glassAmt = f.style == 1.f ? 0.8f : 0.35f;
                c = lerp(vec3(w.x, w.y, w.z) * 0.85f, vec3(gl.x, gl.y, gl.z) * 0.45f, glassAmt);
            }
            break;
        }
        default: break;
    }
    return c * vec3(vc.x, vc.y, vc.z);
}

struct Img {
    int W, H;
    std::vector<vec3> col;
    std::vector<float> z;
};

struct Cam {
    vec3 pos, f, r, u;
    float focal;
    int W, H;
};

void rasterTri(Img& img, const Cam& cam, vec3 a, vec3 b, vec3 c, vec3 color, float bias) {
    vec3 P[3] = {a, b, c};
    // camera space
    vec3 cs[3];
    for (int i = 0; i < 3; i++) {
        vec3 d = P[i] - cam.pos;
        cs[i] = vec3(dot(d, cam.r), dot(d, cam.u), dot(d, cam.f));
    }
    // near clip
    const float nz = 0.3f;
    std::vector<vec3> poly;
    for (int i = 0; i < 3; i++) {
        vec3 p = cs[i], q = cs[(i + 1) % 3];
        bool ip = p.z >= nz, iq = q.z >= nz;
        if (ip) poly.push_back(p);
        if (ip != iq) {
            float t = (nz - p.z) / (q.z - p.z);
            poly.push_back(lerp(p, q, t));
        }
    }
    if (poly.size() < 3) return;
    for (size_t k = 1; k + 1 < poly.size(); k++) {
        vec3 v[3] = {poly[0], poly[k], poly[k + 1]};
        float sx[3], sy[3], iz[3];
        for (int i = 0; i < 3; i++) {
            iz[i] = 1.f / v[i].z;
            sx[i] = cam.W * 0.5f + v[i].x * iz[i] * cam.focal;
            sy[i] = cam.H * 0.5f - v[i].y * iz[i] * cam.focal;
        }
        float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (fabsf(area) < 1e-6f) continue;
        int x0 = Max(0, (int)floorf(Min(sx[0], Min(sx[1], sx[2])))), x1 = Min(cam.W - 1, (int)ceilf(Max(sx[0], Max(sx[1], sx[2]))));
        int y0 = Max(0, (int)floorf(Min(sy[0], Min(sy[1], sy[2])))), y1 = Min(cam.H - 1, (int)ceilf(Max(sy[0], Max(sy[1], sy[2]))));
        if (x1 < x0 || y1 < y0) continue;
        float inv = 1.f / area;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((sx[1] - px) * (sy[2] - py) - (sx[2] - px) * (sy[1] - py)) * inv;
                float w1 = ((sx[2] - px) * (sy[0] - py) - (sx[0] - px) * (sy[2] - py)) * inv;
                float w2 = 1.f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                float izp = w0 * iz[0] + w1 * iz[1] + w2 * iz[2];
                float z = 1.f / izp * bias;
                size_t idx = (size_t)y * cam.W + x;
                if (z < img.z[idx]) {
                    img.z[idx] = z;
                    img.col[idx] = color;
                }
            }
    }
}

vec3 shade(vec3 albedo, vec3 n, bool emissive, vec3 sun) {
    if (emissive) return albedo * 1.2f;
    float l = Max(0.f, dot(n, sun));
    float sky = 0.35f + 0.15f * n.z;
    return albedo * (l * 0.85f + sky);
}

void drawMesh(Img& img, const Cam& cam, const MeshData& m, vec3 org, float bias, vec3 sun, int& tris) {
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const VtxStatic& A = m.verts[m.indices[i]];
        const VtxStatic& B = m.verts[m.indices[i + 1]];
        const VtxStatic& C = m.verts[m.indices[i + 2]];
        vec3 a = A.pos + org, b = B.pos + org, c = C.pos + org;
        vec3 n = cross(b - a, c - a);
        if (length2(n) < 1e-12f) continue;
        n = normalize(n);
        // back-face culling (CCW front) except for decals
        vec3 toCam = cam.pos - a;
        bool em;
        vec3 albedo = matColor(A.mat, A.color, em);
        if (dot(n, toCam) < 0) continue;
        rasterTri(img, cam, a, b, c, shade(albedo, n, em, sun), bias);
        tris++;
    }
}


int main(int argc, char** argv) {
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    WorldMap map; map.generate(); gMap = &map;
    RoadNetwork roads; roads.generate(map); gRoads = &roads;
    BuildingSet bs; bs.generate(map, roads); gBuildings = &bs;
    int cps = kCellsPerSide;
    struct R { double t; int cx, cy; bool d; size_t v; };
    std::vector<R> res;
    double tot[2] = {0, 0}; int cnt[2] = {0, 0};
    for (int cy = 0; cy < cps; cy++)
        for (int cx = 0; cx < cps; cx++) {
            if (gSites->cellElems[(size_t)cy * cps + cx].empty()) continue;
            for (int d = 0; d < 2; d++) {
                CellGeometry geo;
                double t0 = TimeSeconds();
                generateCell(cx, cy, d == 0, geo);
                double t1 = TimeSeconds();
                CellGeometry base;  // same cell without sites for comparison is not separable; report total
                res.push_back({t1 - t0, cx, cy, d == 0, geo.opaque.verts.size() + geo.decals.verts.size()});
                tot[d] += t1 - t0; cnt[d]++;
            }
        }
    std::sort(res.begin(), res.end(), [](const R& a, const R& b) { return a.t > b.t; });
    printf("site cells: LOD0 avg %.2f ms (%d), LOD1 avg %.2f ms (%d)\n", tot[0] / cnt[0] * 1000, cnt[0], tot[1] / cnt[1] * 1000, cnt[1]);
    for (int i = 0; i < 25 && i < (int)res.size(); i++) {
        vec2 o = cellOrigin(res[i].cx, res[i].cy) + vec2(128.f);
        printf("%6.2f ms %s cell (%d,%d) center (%.0f,%.0f) verts %zu elems:", res[i].t * 1000, res[i].d ? "LOD0" : "LOD1", res[i].cx, res[i].cy, o.x, o.y, res[i].v);
        std::map<int,int> kinds;
        for (int e : gSites->cellElems[(size_t)res[i].cy * cps + res[i].cx]) kinds[gSites->elems[e].kind]++;
        for (auto& k : kinds) printf(" %d:%d", k.first, k.second);
        printf("\n");
    }
    Jobs::shutdown();
}
