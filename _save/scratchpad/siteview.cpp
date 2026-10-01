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
#include "src/world/transit.cpp"
#include "src/world/transitmesh.cpp"
#include "src/world/sitecell.cpp"
#include "src/world/facadedetail.cpp"
#include "src/world/interiorkit.cpp"
#include "src/world/interiorfurniture.cpp"
#include "src/world/interiorlayouts.cpp"
#include "src/world/interiorhomes.cpp"
#include "src/world/interiorvenues.cpp"
#include "src/world/interiorshops.cpp"
#include "src/world/interiorcivic.cpp"
#include "src/world/interiorindustrial.cpp"
#include "src/world/interiortower.cpp"
#include "src/world/interiorgarages.cpp"
#include "src/world/interiorresidences.cpp"
#include "src/world/interiors.cpp"
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
        case MAT_LEAVES: c = vec3(0.2f, 0.36f, 0.12f); break;
        case MAT_PALM_FROND: c = vec3(0.25f, 0.42f, 0.14f); break;
        case MAT_BARK: c = vec3(0.4f, 0.32f, 0.24f); break;
        case MAT_PLASTIC: c = vec3(0.85f); break;
        case MAT_CHROME: c = vec3(0.8f); break;
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

bool gNight = false;
const PropPrototype& protoFor(int type, int variant) {
    static std::map<int, PropPrototype> cache;
    int nv = 1;
    switch (type) {
        case PROP_PALM: case PROP_TREE_OAK: case PROP_BUSH: nv = 4; break;
        case PROP_PALM_TALL: case PROP_TREE_PINE: case PROP_MANGROVE: case PROP_CYPRESS: case PROP_SAWGRASS: nv = 3; break;
        case PROP_STREETLIGHT: case PROP_TRAFFIC_LIGHT: case PROP_DUMPSTER: nv = 2; break;
        case PROP_BUS_STOP: case PROP_NEWS_BOX: case PROP_POWER_POLE: case PROP_MAILBOX: nv = 4; break;
        case PROP_PLANTER: case PROP_BARRIER: case PROP_SIGNAL_SPAN: nv = 2; break;
        case PROP_STREET_TREE: nv = 3; break;
        default: break;
    }
    int key = type * 16 + (variant % nv);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    PropPrototype& P = cache[key];
    buildPropPrototype((PropType)type, variant % nv, P);
    return P;
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
    if (argc < 5) {
        printf("usage: siteview out.ppm W H x,y,z,yaw,pitch[,fov] ...\n");
        return 1;
    }
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    WorldMap map;
    map.generate();
    gMap = &map;
    RoadNetwork roads;
    roads.generate(map);
    gRoads = &roads;
    BuildingSet bs;
    bs.generate(map, roads);
    gBuildings = &bs;
    int W = atoi(argv[2]), H = atoi(argv[3]);
    {
        int nb = 0;
        for (auto& e : gSites->elems)
            if (e.kind == SK_BILLBOARD && nb++ < 6) printf("billboard at %.0f,%.0f,%.1f face %.2f,%.2f pole %.1f\n", e.c.x, e.c.y, e.z, e.ax.x, e.ax.y, e.p[0]);
    }
    if (getenv("FINDSPAN")) {
        Region want[] = {REG_FLATS, REG_SUBURBS, REG_NORTH_CITY, REG_CALLE_LUNA, REG_DOWNTOWN, REG_GROVE};
        for (Region w : want) {
            int k = 0;
            for (size_t ni = 0; ni < roads.nodes.size() && k < 3; ni++) {
                const RoadNode& nd = roads.nodes[ni];
                if (nd.control != 2 || map.regionAt(nd.p.x, nd.p.y) != w) continue;
                printf("signal node %zu in %s at %.0f,%.0f z %.1f deg %zu\n", ni, regionInfo(w).name, nd.p.x, nd.p.y, nd.z, nd.edges.size());
                k++;
            }
        }
    }
    if (getenv("FINDWORK")) {
        int k = 0;
        for (size_t ei = 0; ei < roads.edges.size() && k < 8; ei++) {
            const RoadEdge& e = roads.edges[ei];
            street_dressing::Profile pf = street_dressing::profileFor(street_dressing::edgeRegion(e, map));
            for (int side = -1; side <= 1; side += 2) {
                float sc;
                if (!street_dressing::edgeWorkZone(roads, (int)ei, pf, side, &sc)) continue;
                vec3 c = e.posAt(sc), t = e.tangentAt(sc);
                printf("work zone edge %zu side %d at %.0f,%.0f z %.1f dir %.2f,%.2f region %s\n", ei, side, c.x, c.y, c.z, t.x, t.y, regionInfo(map.regionAt(c.x, c.y)).name);
                k++;
            }
        }
    }
    vec3 sun = normalize(vec3(-0.45f, -0.35f, 0.82f));
    for (int vi = 4; vi < argc; vi++) {
        float x, y, z, yaw, pitch, fov = 60.f;
        int n = sscanf(argv[vi], "%f,%f,%f,%f,%f,%f", &x, &y, &z, &yaw, &pitch, &fov);
        if (n < 5) continue;
        Cam cam;
        cam.pos = vec3(x, y, z);
        gNight = getenv("NIGHT") != nullptr;
        printf("view %d region %s\n", vi - 3, regionInfo(map.regionAt(x, y)).name);
        if (yaw > 900.f) {
            // snap to the nearest street and look along it (yaw 999 = forward, 998 = backward); z = eye height above the road
            float ds, dd, dside;
            int ne = roads.nearestEdge(vec2(x, y), 200.f, &ds, &dd, &dside);
            if (ne >= 0) {
                const RoadEdge& e = roads.edges[ne];
                vec3 P = e.posAt(ds), T = e.tangentAt(ds);
                vec2 t2 = normalize(T.xy());
                if (yaw < 998.5f) t2 = -t2;
                vec2 side = vec2(t2.y, -t2.x);
                vec2 p2 = P.xy() + side * (e.halfWidth * 0.5f);
                cam.pos = vec3(p2, P.z + z);
                yaw = atan2f(-t2.x, t2.y) / kDegToRad;
                printf("view %d snapped to (%.0f,%.0f,%.1f) yaw %.0f\n", vi - 3, p2.x, p2.y, P.z + z, yaw);
            }
        }
        float yr = yaw * kDegToRad, pr = pitch * kDegToRad;
        cam.f = vec3(-sinf(yr) * cosf(pr), cosf(yr) * cosf(pr), sinf(pr));
        cam.r = normalize(cross(cam.f, vec3(0, 0, 1)));
        cam.u = cross(cam.r, cam.f);
        cam.W = W;
        cam.H = H;
        cam.focal = (H * 0.5f) / tanf(fov * 0.5f * kDegToRad);
        Img img;
        img.W = W;
        img.H = H;
        img.col.assign((size_t)W * H, vec3(0.55f, 0.7f, 0.9f));
        img.z.assign((size_t)W * H, 1e30f);
        // sky gradient
        for (int py = 0; py < H; py++)
            for (int px = 0; px < W; px++) img.col[(size_t)py * W + px] = lerp(vec3(0.75f, 0.85f, 0.95f), vec3(0.35f, 0.55f, 0.85f), (float)py / H * 0.3f);
        // terrain + water
        int tris = 0;
        {
            float R = 2600.f;
            for (float ty = cam.pos.y - R; ty < cam.pos.y + R; ty += 8.f) {
                for (float tx = cam.pos.x - R; tx < cam.pos.x + R; tx += 8.f) {
                    float dx = tx - cam.pos.x, dy = ty - cam.pos.y;
                    float dist = sqrtf(dx * dx + dy * dy);
                    float step = dist < 600.f ? 8.f : (dist < 1500.f ? 16.f : 32.f);
                    if (fmodf(fabsf(tx - (cam.pos.x - R)), step) > 0.1f || fmodf(fabsf(ty - (cam.pos.y - R)), step) > 0.1f) continue;
                    vec3 p00(tx, ty, map.heightAt(tx, ty)), p10(tx + step, ty, map.heightAt(tx + step, ty));
                    vec3 p11(tx + step, ty + step, map.heightAt(tx + step, ty + step)), p01(tx, ty + step, map.heightAt(tx, ty + step));
                    // color from splat
                    float sx = worldToTexel(tx + step * 0.5f), sy = worldToTexel(ty + step * 0.5f);
                    int ix = Clamp((int)sx, 0, kHeightRes - 1), iy = Clamp((int)sy, 0, kHeightRes - 1);
                    size_t idx = (size_t)iy * kHeightRes + ix;
                    vec4 s0 = unpackRGBA8(map.splat0[idx]), s1 = unpackRGBA8(map.splat1[idx]);
                    vec3 cols[8] = {vec3(0.8f, 0.74f, 0.55f), vec3(0.3f, 0.45f, 0.18f), vec3(0.45f, 0.36f, 0.25f), vec3(0.5f), vec3(0.3f, 0.27f, 0.2f),
                                    vec3(0.5f, 0.5f, 0.28f), vec3(0.18f, 0.32f, 0.15f), vec3(0.5f, 0.5f, 0.52f)};
                    float w[8] = {s0.x, s0.y, s0.z, s0.w, s1.x, s1.y, s1.z, s1.w};
                    vec3 c(0);
                    for (int k = 0; k < 8; k++) c += cols[k] * w[k];
                    vec3 n = normalize(cross(p10 - p00, p01 - p00));
                    vec3 sc = shade(c, n, false, sun);
                    rasterTri(img, cam, p00, p10, p11, sc, 1.f);
                    rasterTri(img, cam, p00, p11, p01, sc, 1.f);
                    float wl = map.waterAt(tx + step * 0.5f, ty + step * 0.5f);
                    if (wl > kNoWater + 1.f) {
                        vec3 wc(0.12f, 0.35f, 0.45f);
                        rasterTri(img, cam, vec3(tx, ty, wl), vec3(tx + step, ty, wl), vec3(tx + step, ty + step, wl), wc, 1.f);
                        rasterTri(img, cam, vec3(tx, ty, wl), vec3(tx + step, ty + step, wl), vec3(tx, ty + step, wl), wc, 1.f);
                    }
                }
            }
        }
        // cells
        double tNear = 0, tFar = 0, tMaxNear = 0, tMaxFar = 0;
        int nNear = 0, nFar = 0;
        size_t vNear = 0, vFar = 0, vMaxNear = 0;
        int cps = kCellsPerSide;
        int ccx = (int)floorf((cam.pos.x + kWorldHalf) / kCellSize), ccy = (int)floorf((cam.pos.y + kWorldHalf) / kCellSize);
        int rr = (int)ceilf(2300.f / kCellSize) + 1;
        for (int dy = -rr; dy <= rr; dy++)
            for (int dx = -rr; dx <= rr; dx++) {
                int cx = ccx + dx, cy = ccy + dy;
                if (cx < 0 || cy < 0 || cx >= cps || cy >= cps) continue;
                vec2 o = cellOrigin(cx, cy);
                float qx = Max(Max(o.x - cam.pos.x, 0.f), cam.pos.x - (o.x + kCellSize));
                float qy = Max(Max(o.y - cam.pos.y, 0.f), cam.pos.y - (o.y + kCellSize));
                float d = sqrtf(qx * qx + qy * qy);
                if (d > 2300.f) continue;
                bool detail = d < 420.f;
                CellGeometry geo;
                double t0 = TimeSeconds();
                generateCell(cx, cy, detail, geo);
                double dt = TimeSeconds() - t0;
                size_t nv = geo.opaque.verts.size() + geo.decals.verts.size();
                if (detail) { tNear += dt; nNear++; vNear += nv; tMaxNear = Max(tMaxNear, dt); vMaxNear = Max(vMaxNear, nv); }
                else { tFar += dt; nFar++; vFar += nv; tMaxFar = Max(tMaxFar, dt); }
                vec3 org(o, 0);
                drawMesh(img, cam, geo.opaque, org, 1.f, sun, tris);
                drawMesh(img, cam, geo.decals, org, 0.9995f, sun, tris);
                // props with their real prototypes (foliage cards drawn solid)
                for (auto& p : geo.props) {
                    if (length(p.pos - cam.pos) > 260.f) continue;
                    const PropPrototype& P = protoFor(p.type, p.variant);
                    float cy2 = cosf(p.yaw), sy2 = sinf(p.yaw);
                    auto X = [&](vec3 v) { v = v * p.scale; return p.pos + vec3(v.x * cy2 - v.y * sy2, v.x * sy2 + v.y * cy2, v.z); };
                    const MeshData& m = P.mesh;
                    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
                        const VtxStatic& A = m.verts[m.indices[i]];
                        vec3 a = X(A.pos), b = X(m.verts[m.indices[i + 1]].pos), c = X(m.verts[m.indices[i + 2]].pos);
                        vec3 n = cross(b - a, c - a);
                        if (length2(n) < 1e-12f) continue;
                        n = normalize(n);
                        bool em;
                        vec3 albedo = matColor(A.mat, A.color, em);
                        u32 mid = A.mat & 0xff;
                        if (mid == MAT_LEAVES || mid == MAT_PALM_FROND) albedo = matColor(A.mat, A.color | 0xff000000u, em) ;
                        if (dot(n, cam.pos - a) < 0) { if (mid != MAT_LEAVES && mid != MAT_PALM_FROND) continue; n = -n; }
                        if (em) {
                            vec4 vc = unpackRGBA8(A.color);
                            float night = gNight ? 1.f : 0.f;
                            albedo = vec3(vc.x, vc.y, vc.z) * Min(3.f, vc.w * 400.f * (0.03f + night) * 6.f / 60.f);
                        }
                        rasterTri(img, cam, a, b, c, shade(albedo, n, em, sun), 1.f);
                        tris++;
                    }
                }
            }
        printf("view %d: near %d cells avg %.2f ms max %.2f ms (avg %zu verts, max %zu), far %d cells avg %.2f ms max %.2f ms (avg %zu verts), %d tris\n",
               vi - 3, nNear, nNear ? tNear / nNear * 1000.0 : 0.0, tMaxNear * 1000.0, nNear ? vNear / nNear : 0, vMaxNear, nFar, nFar ? tFar / nFar * 1000.0 : 0.0,
               tMaxFar * 1000.0, nFar ? vFar / nFar : 0, tris);
        // simple distance haze
        for (size_t i = 0; i < img.col.size(); i++) {
            if (img.z[i] > 1e29f) continue;
            float f = 1.f - expf(-img.z[i] / 5000.f);
            img.col[i] = lerp(img.col[i], vec3(0.7f, 0.78f, 0.88f), f);
        }
        std::string path = argv[1];
        if (argc > 5) path = StrFormat("%s_%d.ppm", argv[1], vi - 3);
        FILE* fo = fopen(path.c_str(), "wb");
        fprintf(fo, "P6 %d %d 255\n", W, H);
        for (auto& c : img.col) {
            vec3 s = saturate(c);
            unsigned char px[3] = {(unsigned char)(powf(s.x, 1.f / 1.6f) * 255), (unsigned char)(powf(s.y, 1.f / 1.6f) * 255), (unsigned char)(powf(s.z, 1.f / 1.6f) * 255)};
            fwrite(px, 1, 3, fo);
        }
        fclose(fo);
    }
    Jobs::shutdown();
    return 0;
}
