// Places planning / preview tool (development aid): generates the world once, then runs commands.
//   map:x0,y0,x1,y1,mpp,out.ppm         2D plan (regions, water, roads, buildings, pads, site elements, 100 m grid)
//   bld:x,y,r                           buildings near a point
//   roads:x,y,r                         road edges near a point
//   style:S,R                           buildings of style S in region R (numbers)
//   cells:x0,y0,x1,y1                   near/far vertex counts and generation times per cell
//   view:out.ppm,W,H,x,y,z,yaw,pitch[,fov]   software-rasterized 3D view (NIGHT=1 for emissives)
//   sites:x,y,r                         site elements near a point
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
#include "src/world/campus.cpp"
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

namespace planx {

// Mesh one site element into a sink (the per-kind dispatch of sitecell.cpp, for the places)
void genPlaceElem(const SiteElem& e, sitegeo::G& g) {
    switch (e.kind) {
        case SK_DECO_HOTEL: deco_strip::genHotel(e, g); break;
        case SK_DECO_PARK: deco_strip::genDecoPark(e, g); break;
        default: break;
    }
}

const char* styleName(int s) {
    static const char* n[] = {"tower", "midrise", "condo", "deco", "shops", "strip", "house", "villa", "warehouse", "factory", "farmhouse", "barn", "motel",
                              "gas", "garage", "church", "shack"};
    return s >= 0 && s < BS_COUNT ? n[s] : "?";
}

// ------------------------------------------------------------------------------------------------ 2D plan
struct Canvas {
    int W, H;
    float x0, y0, mpp;
    std::vector<vec3> px;
    void set(int x, int y, vec3 c) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        px[(size_t)y * W + x] = c;
    }
    void blend(int x, int y, vec3 c, float a) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        vec3& p = px[(size_t)y * W + x];
        p = lerp(p, c, a);
    }
    vec2 toPx(vec2 w) const { return vec2((w.x - x0) / mpp, H - 1 - (w.y - y0) / mpp); }
    vec2 toW(float x, float y) const { return vec2(x0 + (x + 0.5f) * mpp, y0 + (H - 1 - y + 0.5f) * mpp); }
    // filled convex polygon in world coords
    void poly(const std::vector<vec2>& pw, vec3 c, float a = 1.f) {
        if (pw.size() < 3) return;
        std::vector<vec2> p;
        for (auto& q : pw) p.push_back(toPx(q));
        float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f;
        for (auto& q : p) { mnx = Min(mnx, q.x); mny = Min(mny, q.y); mxx = Max(mxx, q.x); mxy = Max(mxy, q.y); }
        int ix0 = Max(0, (int)floorf(mnx)), ix1 = Min(W - 1, (int)ceilf(mxx)), iy0 = Max(0, (int)floorf(mny)), iy1 = Min(H - 1, (int)ceilf(mxy));
        for (int y = iy0; y <= iy1; y++)
            for (int x = ix0; x <= ix1; x++) {
                vec2 q(x + 0.5f, y + 0.5f);
                bool in = false;
                for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
                    if (((p[i].y > q.y) != (p[j].y > q.y)) && (q.x < (p[j].x - p[i].x) * (q.y - p[i].y) / (p[j].y - p[i].y) + p[i].x)) in = !in;
                }
                if (in) blend(x, y, c, a);
            }
    }
    void line(vec2 aw, vec2 bw, float widthM, vec3 c, float a = 1.f) {
        vec2 d = bw - aw;
        float len = length(d);
        if (len < 1e-4f) return;
        vec2 t = d / len, n = perp(t) * Max(widthM * 0.5f, mpp * 0.5f);
        poly({aw - n, bw - n, bw + n, aw + n}, c, a);
    }
    void rectOutline(vec2 c, vec2 ax, float hx, float hy, vec3 col) {
        vec2 ay = perp(ax);
        vec2 p[4] = {c - ax * hx - ay * hy, c + ax * hx - ay * hy, c + ax * hx + ay * hy, c - ax * hx + ay * hy};
        for (int k = 0; k < 4; k++) line(p[k], p[(k + 1) % 4], mpp * 1.2f, col);
    }
    void text(const char* s, vec2 w, float hPx, vec3 col) {
        vec2 o = toPx(w);
        float unit = hPx / 9.f;
        float x = 0.f;
        for (const char* c = s; *c; c++) {
            if (*c == ' ') { x += 4.5f * unit; continue; }
            const auto& strokes = sitegeo::glyphStrokes(*c);
            for (auto& st : strokes)
                for (size_t i = 0; i + 1 < st.pts.size(); i++) {
                    vec2 a = o + vec2(x + st.pts[i].x * unit, -st.pts[i].y * unit), b = o + vec2(x + st.pts[i + 1].x * unit, -st.pts[i + 1].y * unit);
                    int n = (int)(length(b - a) * 2.f) + 1;
                    for (int k = 0; k <= n; k++) {
                        vec2 q = lerp(a, b, (float)k / n);
                        set((int)q.x, (int)q.y, col);
                    }
                }
            x += 8.f * unit;
        }
    }
    void save(const char* path) {
        FILE* f = fopen(path, "wb");
        fprintf(f, "P6 %d %d 255\n", W, H);
        for (auto& c : px) {
            vec3 s = saturate(c);
            unsigned char b[3] = {(unsigned char)(s.x * 255), (unsigned char)(s.y * 255), (unsigned char)(s.z * 255)};
            fwrite(b, 1, 3, f);
        }
        fclose(f);
    }
};

vec3 regionTint(int r) {
    float hue = fmodf(r * 0.618034f, 1.f);
    return hsvToRgb(hue, 0.35f, 0.85f);
}

void plan2D(float x0, float y0, float x1, float y1, float mpp, const char* out) {
    const WorldMap& map = *gMap;
    Canvas cv;
    cv.W = (int)((x1 - x0) / mpp);
    cv.H = (int)((y1 - y0) / mpp);
    cv.x0 = x0;
    cv.y0 = y0;
    cv.mpp = mpp;
    cv.px.assign((size_t)cv.W * cv.H, vec3(0.f));
    for (int y = 0; y < cv.H; y++)
        for (int x = 0; x < cv.W; x++) {
            vec2 w = cv.toW((float)x, (float)y);
            vec3 c;
            if (map.isWater(w.x, w.y)) c = vec3(0.25f, 0.45f, 0.7f);
            else {
                c = regionTint(map.regionAt(w.x, w.y)) * (0.8f + 0.2f * Saturate(map.heightAt(w.x, w.y) / 10.f));
                if (map.beachSand(w.x, w.y) > 0.5f) c = lerp(c, vec3(0.95f, 0.88f, 0.6f), 0.6f);
            }
            cv.set(x, y, c);
        }
    // grid
    float gs = 100.f / mpp >= 8.f ? 100.f : 1000.f, gm = gs * 5.f;
    for (float gx = ceilf(x0 / gs) * gs; gx < x1; gx += gs) cv.line(vec2(gx, y0), vec2(gx, y1), mpp * (fmodf(fabsf(gx), gm) < 1.f ? 1.5f : 0.6f), vec3(0.f), 0.35f);
    for (float gy = ceilf(y0 / gs) * gs; gy < y1; gy += gs) cv.line(vec2(x0, gy), vec2(x1, gy), mpp * (fmodf(fabsf(gy), gm) < 1.f ? 1.5f : 0.6f), vec3(0.f), 0.35f);
    // pads
    for (const Pad& p : gSites->pads) {
        if (p.c.x + p.hx + p.hy < x0 || p.c.x - p.hx - p.hy > x1 || p.c.y + p.hx + p.hy < y0 || p.c.y - p.hx - p.hy > y1) continue;
        vec3 c = p.kind == PAD_TURF ? vec3(0.3f, 0.7f, 0.3f) : (p.kind == PAD_PLAZA ? vec3(0.8f, 0.7f, 0.6f) : (p.kind == PAD_PARKING ? vec3(0.35f) : vec3(0.5f)));
        cv.poly(sitegeo::rectPoly(p.c, p.ax, p.hx, p.hy), c, 0.7f);
    }
    // roads
    for (const RoadEdge& e : gRoads->edges) {
        bool in = false;
        for (auto& q : e.pts)
            if (q.x > x0 - 50 && q.x < x1 + 50 && q.y > y0 - 50 && q.y < y1 + 50) { in = true; break; }
        if (!in) continue;
        for (size_t i = 0; i + 1 < e.pts.size(); i++) {
            cv.line(e.pts[i].xy(), e.pts[i + 1].xy(), 2.f * (e.halfWidth + e.sidewalk), vec3(0.7f), 1.f);
            cv.line(e.pts[i].xy(), e.pts[i + 1].xy(), 2.f * e.halfWidth, e.cls == RC_HIGHWAY || e.cls == RC_RAMP ? vec3(0.45f, 0.35f, 0.2f) : vec3(0.2f), 1.f);
        }
    }
    // buildings
    const float W = x1 - x0;
    for (const Building& b : gBuildings->buildings) {
        if (b.c.x < x0 - 60 || b.c.x > x1 + 60 || b.c.y < y0 - 60 || b.c.y > y1 + 60) continue;
        vec3 c = vec3(0.9f, 0.4f, 0.3f);
        switch (b.style) {
            case BS_DECO: c = vec3(1.f, 0.5f, 0.8f); break;
            case BS_CHURCH: c = vec3(1.f, 1.f, 0.2f); break;
            case BS_HOUSE: case BS_VILLA: c = vec3(0.85f, 0.6f, 0.45f); break;
            case BS_TOWER: case BS_CONDO: c = vec3(0.4f, 0.5f, 0.9f); break;
            case BS_WAREHOUSE: case BS_FACTORY: c = vec3(0.6f, 0.6f, 0.65f); break;
            default: break;
        }
        cv.poly(sitegeo::rectPoly(b.c, b.ax, b.hx, b.hy), c, 0.9f);
        cv.line(b.c, b.c + b.front * Min(b.hy, 6.f), mpp, vec3(0.f));
        (void)W;
    }
    // site elements
    for (const SiteElem& e : gSites->elems) {
        if (e.c.x < x0 - 300 || e.c.x > x1 + 300 || e.c.y < y0 - 300 || e.c.y > y1 + 300) continue;
        if (e.isLine()) cv.line(e.a, e.b, mpp * 2.f, vec3(0.1f, 0.9f, 0.9f));
        else cv.rectOutline(e.c, e.ax, e.hx, e.hy, vec3(0.1f, 0.9f, 0.9f));
        for (size_t i = 0; i + 1 < e.pts.size() && e.kind != SK_BILLBOARD; i++) cv.line(e.pts[i], e.pts[i + 1], mpp, vec3(0.2f, 1.f, 0.6f));
    }
    // grid labels
    float ls = 500.f / mpp >= 90.f ? 500.f : (1000.f / mpp >= 90.f ? 1000.f : 2000.f);
    for (float gx = ceilf(x0 / ls) * ls; gx < x1; gx += ls)
        for (float gy = ceilf(y0 / ls) * ls; gy < y1; gy += ls) cv.text(StrFormat("%d,%d", (int)gx, (int)gy).c_str(), vec2(gx + 4 * mpp, gy + 4 * mpp), 12.f, vec3(1.f));
    cv.save(out);
    printf("map %s: %dx%d\n", out, cv.W, cv.H);
}

// ------------------------------------------------------------------------------------------------ 3D view (software raster)
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
        case PROP_BUS_STOP: case PROP_NEWS_BOX: case PROP_POWER_POLE: nv = 4; break;
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

void rasterTriG(Img& img, const Cam& cam, vec3 a, vec3 b, vec3 c, vec3 ca, vec3 cb, vec3 cc, float bias) {
    vec3 P[3] = {a, b, c};
    vec3 cs[3];
    vec3 CC[3] = {ca, cb, cc};
    for (int i = 0; i < 3; i++) {
        vec3 d = P[i] - cam.pos;
        cs[i] = vec3(dot(d, cam.r), dot(d, cam.u), dot(d, cam.f));
    }
    const float nz = 0.3f;
    vec3 poly[4], pcol[4];
    int np = 0;
    for (int i = 0; i < 3; i++) {
        vec3 p = cs[i], q = cs[(i + 1) % 3];
        bool ip = p.z >= nz, iq = q.z >= nz;
        if (ip) { poly[np] = p; pcol[np++] = CC[i]; }
        if (ip != iq) {
            float t = (nz - p.z) / (q.z - p.z);
            poly[np] = lerp(p, q, t);
            pcol[np++] = lerp(CC[i], CC[(i + 1) % 3], t);
        }
    }
    if (np < 3) return;
    for (int k = 1; k + 1 < np; k++) {
        vec3 v[3] = {poly[0], poly[k], poly[k + 1]};
        vec3 vc[3] = {pcol[0], pcol[k], pcol[k + 1]};
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
                    float q0 = w0 * iz[0], q1 = w1 * iz[1], q2 = w2 * iz[2];
                    float qs = q0 + q1 + q2;
                    img.col[idx] = (vc[0] * q0 + vc[1] * q1 + vc[2] * q2) / qs;
                }
            }
    }
}
void rasterTri(Img& img, const Cam& cam, vec3 a, vec3 b, vec3 c, vec3 color, float bias) { rasterTriG(img, cam, a, b, c, color, color, color, bias); }

vec3 shade(vec3 albedo, vec3 n, bool emissive, vec3 sun) {
    if (emissive) return albedo * 1.2f;
    float l = Max(0.f, dot(n, sun));
    float sky = 0.35f + 0.15f * n.z;
    float k = gNight ? 0.15f : 1.f;
    return albedo * (l * 0.85f + sky) * k;
}

vec3 emissiveColor(const VtxStatic& V) {
    vec4 vc = unpackRGBA8(V.color);
    float night = gNight ? 1.f : 0.f;
    return vec3(vc.x, vc.y, vc.z) * Min(3.f, 0.25f + vc.w * 2.5f * (0.2f + night));
}

void drawMesh(Img& img, const Cam& cam, const MeshData& m, vec3 org, float bias, vec3 sun, int& tris, float maxDist) {
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const VtxStatic& A = m.verts[m.indices[i]];
        const VtxStatic& B = m.verts[m.indices[i + 1]];
        const VtxStatic& C = m.verts[m.indices[i + 2]];
        vec3 a = A.pos + org, b = B.pos + org, c = C.pos + org;
        if (length2(a - cam.pos) > maxDist * maxDist) continue;
        vec3 n = cross(b - a, c - a);
        if (length2(n) < 1e-12f) continue;
        n = normalize(n);
        vec3 toCam = cam.pos - a;
        if (dot(n, toCam) < 0) continue;
        vec3 cols[3];
        const VtxStatic* V[3] = {&A, &B, &C};
        for (int k = 0; k < 3; k++) {
            bool em;
            vec3 albedo = matColor(V[k]->mat, V[k]->color, em);
            if (em) { cols[k] = emissiveColor(*V[k]); continue; }
            vec3 vn = unpackNormalOct(V[k]->normal);
            if (dot(vn, n) < -0.2f) vn = n;
            cols[k] = shade(albedo, vn, em, sun);
        }
        rasterTriG(img, cam, a, b, c, cols[0], cols[1], cols[2], bias);
        tris++;
    }
}

void view3D(const char* out, int W, int H, float x, float y, float z, float yaw, float pitch, float fov, float range) {
    const WorldMap& map = *gMap;
    Cam cam;
    if (z < 0.f) {
        float gz = map.heightAt(x, y), rz;
        if (gRoads->surfaceHeight(vec2(x, y), &rz, gz + 3.f)) gz = Max(gz, rz);
        float pz;
        if (gSites->padHeight(vec2(x, y), &pz, gz + 3.f)) gz = Max(gz, pz);
        z = gz - z;
    }
    cam.pos = vec3(x, y, z);
    float yr = yaw * kDegToRad, pr = pitch * kDegToRad;
    cam.f = vec3(-sinf(yr) * cosf(pr), cosf(yr) * cosf(pr), sinf(pr));
    cam.r = normalize(cross(cam.f, vec3(0, 0, 1)));
    if (length2(cross(cam.f, vec3(0, 0, 1))) < 1e-6f) cam.r = vec3(cosf(yr), sinf(yr), 0);
    cam.u = cross(cam.r, cam.f);
    cam.W = W;
    cam.H = H;
    cam.focal = (H * 0.5f) / tanf(fov * 0.5f * kDegToRad);
    Img img;
    img.W = W;
    img.H = H;
    img.col.assign((size_t)W * H, vec3(0.55f, 0.7f, 0.9f));
    img.z.assign((size_t)W * H, 1e30f);
    for (int py = 0; py < H; py++)
        for (int px = 0; px < W; px++)
            img.col[(size_t)py * W + px] = gNight ? vec3(0.02f, 0.03f, 0.06f) : lerp(vec3(0.75f, 0.85f, 0.95f), vec3(0.35f, 0.55f, 0.85f), (float)py / H * 0.3f);
    vec3 sun = normalize(vec3(-0.45f, -0.35f, 0.82f));
    int tris = 0;
    {
        float R = Min(range, 2600.f);
        for (float ty = cam.pos.y - R; ty < cam.pos.y + R; ty += 8.f)
            for (float tx = cam.pos.x - R; tx < cam.pos.x + R; tx += 8.f) {
                float dx = tx - cam.pos.x, dy = ty - cam.pos.y;
                float dist = sqrtf(dx * dx + dy * dy);
                float step = dist < 600.f ? 8.f : (dist < 1500.f ? 16.f : 32.f);
                if (fmodf(fabsf(tx - (cam.pos.x - R)), step) > 0.1f || fmodf(fabsf(ty - (cam.pos.y - R)), step) > 0.1f) continue;
                vec3 p00(tx, ty, map.heightAt(tx, ty)), p10(tx + step, ty, map.heightAt(tx + step, ty));
                vec3 p11(tx + step, ty + step, map.heightAt(tx + step, ty + step)), p01(tx, ty + step, map.heightAt(tx, ty + step));
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
                    vec3 wc = gNight ? vec3(0.01f, 0.03f, 0.05f) : vec3(0.12f, 0.35f, 0.45f);
                    rasterTri(img, cam, vec3(tx, ty, wl), vec3(tx + step, ty, wl), vec3(tx + step, ty + step, wl), wc, 1.f);
                    rasterTri(img, cam, vec3(tx, ty, wl), vec3(tx + step, ty + step, wl), vec3(tx, ty + step, wl), wc, 1.f);
                }
            }
    }
    int cps = kCellsPerSide;
    int ccx = (int)floorf((cam.pos.x + kWorldHalf) / kCellSize), ccy = (int)floorf((cam.pos.y + kWorldHalf) / kCellSize);
    int rr = (int)ceilf(range / kCellSize) + 1;
    for (int dy = -rr; dy <= rr; dy++)
        for (int dx = -rr; dx <= rr; dx++) {
            int cx = ccx + dx, cy = ccy + dy;
            if (cx < 0 || cy < 0 || cx >= cps || cy >= cps) continue;
            vec2 o = cellOrigin(cx, cy);
            float qx = Max(Max(o.x - cam.pos.x, 0.f), cam.pos.x - (o.x + kCellSize));
            float qy = Max(Max(o.y - cam.pos.y, 0.f), cam.pos.y - (o.y + kCellSize));
            float d = sqrtf(qx * qx + qy * qy);
            if (d > range) continue;
            bool detail = d < 420.f;
            CellGeometry geo;
            generateCell(cx, cy, detail, geo);
            vec3 org(o, 0);
            drawMesh(img, cam, geo.opaque, org, 1.f, sun, tris, range + 300.f);
            drawMesh(img, cam, geo.decals, org, 0.9995f, sun, tris, range + 300.f);
            for (auto& p : geo.props) {
                if (length(p.pos - cam.pos) > 300.f) continue;
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
                    if (mid == MAT_LEAVES || mid == MAT_PALM_FROND) albedo = matColor(A.mat, A.color | 0xff000000u, em);
                    if (dot(n, cam.pos - a) < 0) {
                        if (mid != MAT_LEAVES && mid != MAT_PALM_FROND) continue;
                        n = -n;
                    }
                    vec3 cc = em ? emissiveColor(A) : shade(albedo, n, em, sun);
                    rasterTri(img, cam, a, b, c, cc, 1.f);
                    tris++;
                }
            }
            if (gNight)
                for (auto& L : geo.lights) {
                    // light pools: brighten nearby pixels later (cheap: splat a dot)
                    (void)L;
                }
        }
    if (!gNight)
        for (size_t i = 0; i < img.col.size(); i++) {
            if (img.z[i] > 1e29f) continue;
            float f = 1.f - expf(-img.z[i] / 5000.f);
            img.col[i] = lerp(img.col[i], vec3(0.7f, 0.78f, 0.88f), f);
        }
    FILE* fo = fopen(out, "wb");
    fprintf(fo, "P6 %d %d 255\n", W, H);
    for (auto& c : img.col) {
        vec3 s = saturate(c);
        unsigned char px[3] = {(unsigned char)(powf(s.x, 1.f / 1.6f) * 255), (unsigned char)(powf(s.y, 1.f / 1.6f) * 255), (unsigned char)(powf(s.z, 1.f / 1.6f) * 255)};
        fwrite(px, 1, 3, fo);
    }
    fclose(fo);
    printf("view %s: %d tris\n", out, tris);
}

void cellStats(float x0, float y0, float x1, float y1) {
    int cps = kCellsPerSide;
    size_t mxN = 0, mxF = 0;
    for (int cy = 0; cy < cps; cy++)
        for (int cx = 0; cx < cps; cx++) {
            vec2 o = cellOrigin(cx, cy);
            if (o.x + kCellSize < x0 || o.x > x1 || o.y + kCellSize < y0 || o.y > y1) continue;
            for (int d = 1; d >= 0; d--) {
                CellGeometry geo;
                double t0 = TimeSeconds();
                generateCell(cx, cy, d == 1, geo);
                double dt = TimeSeconds() - t0;
                size_t nv = geo.opaque.verts.size() + geo.decals.verts.size();
                printf("cell %d,%d (%.0f,%.0f) %s: %zu verts %zu tris, %zu props, %zu lights, %zu cols, %.1f ms\n", cx, cy, o.x, o.y, d ? "near" : "far ", nv,
                       (geo.opaque.indices.size() + geo.decals.indices.size()) / 3, geo.props.size(), geo.lights.size(), geo.collision.size(), dt * 1000.0);
                if (d) mxN = Max(mxN, nv);
                else mxF = Max(mxF, nv);
            }
        }
    printf("max near %zu verts, max far %zu verts\n", mxN, mxF);
}

}  // namespace planx

int main(int argc, char** argv) {
    using namespace planx;
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    double t0 = TimeSeconds();
    WorldMap map;
    map.generate();
    gMap = &map;
    RoadNetwork roads;
    roads.generate(map);
    gRoads = &roads;
    BuildingSet bs;
    bs.generate(map, roads);
    gBuildings = &bs;
    printf("world generated in %.1f s\n", TimeSeconds() - t0);
    gNight = getenv("NIGHT") != nullptr;
    for (int ai = 1; ai < argc; ai++) {
        std::string a = argv[ai];
        size_t colon = a.find(':');
        std::string cmd = a.substr(0, colon), rest = colon == std::string::npos ? "" : a.substr(colon + 1);
        std::vector<std::string> f;
        {
            size_t s = 0;
            while (s <= rest.size()) {
                size_t e = rest.find(',', s);
                if (e == std::string::npos) e = rest.size();
                f.push_back(rest.substr(s, e - s));
                s = e + 1;
            }
        }
        auto F = [&](size_t i) { return i < f.size() ? (float)atof(f[i].c_str()) : 0.f; };
        if (cmd == "map") plan2D(F(0), F(1), F(2), F(3), F(4), f[5].c_str());
        else if (cmd == "bld") {
            vec2 p(F(0), F(1));
            for (size_t i = 0; i < bs.buildings.size(); i++) {
                const Building& b = bs.buildings[i];
                if (length(b.c - p) > F(2)) continue;
                printf("bld %zu %s c (%.1f, %.1f) ax (%.2f, %.2f) hx %.1f hy %.1f floors %d h %.1f base %.2f front (%.2f, %.2f) region %s interior %d\n", i,
                       styleName(b.style), b.c.x, b.c.y, b.ax.x, b.ax.y, b.hx, b.hy, b.floors, b.height, b.baseZ, b.front.x, b.front.y,
                       regionInfo((Region)b.region).name, b.interior);
            }
        } else if (cmd == "roads") {
            vec2 p(F(0), F(1));
            std::vector<int> cand;
            roads.edgesInRect(p - vec2(F(2)), p + vec2(F(2)), cand);
            for (int ei : cand) {
                const RoadEdge& e = roads.edges[ei];
                printf("edge %d '%s' cls %d hw %.1f sw %.1f len %.0f from (%.0f, %.0f) to (%.0f, %.0f) nodes %d-%d flags %d\n", ei, e.name.c_str(), e.cls, e.halfWidth,
                       e.sidewalk, e.length, e.pts.front().x, e.pts.front().y, e.pts.back().x, e.pts.back().y, e.n0, e.n1, e.flags);
            }
        } else if (cmd == "style") {
            int st = (int)F(0), rg = (int)F(1);
            for (size_t i = 0; i < bs.buildings.size(); i++) {
                const Building& b = bs.buildings[i];
                if (b.style != st || (rg >= 0 && b.region != rg)) continue;
                printf("bld %zu %s c (%.1f, %.1f) hx %.1f hy %.1f front (%.2f, %.2f) lot (%.1f, %.1f) lhx %.1f lhy %.1f region %s\n", i, styleName(b.style), b.c.x, b.c.y,
                       b.hx, b.hy, b.front.x, b.front.y, b.lotC.x, b.lotC.y, b.lotHx, b.lotHy, regionInfo((Region)b.region).name);
            }
        } else if (cmd == "cells") cellStats(F(0), F(1), F(2), F(3));
        else if (cmd == "view") view3D(f[0].c_str(), (int)F(1), (int)F(2), F(3), F(4), F(5), F(6), F(7), f.size() > 8 ? F(8) : 60.f, f.size() > 9 ? F(9) : 1200.f);
        else if (cmd == "sites") {
            vec2 p(F(0), F(1));
            for (size_t i = 0; i < gSites->elems.size(); i++) {
                const SiteElem& e = gSites->elems[i];
                if (length(e.c - p) > F(2)) continue;
                printf("site %zu kind %d var %d c (%.1f, %.1f) ax (%.2f, %.2f) hx %.1f hy %.1f z %.2f h %.1f '%s'\n", i, e.kind, e.variant, e.c.x, e.c.y, e.ax.x, e.ax.y,
                       e.hx, e.hy, e.z, e.h, e.text.c_str());
            }
        } else if (cmd == "regions") {
            struct Acc { double sx = 0, sy = 0; int n = 0; float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f; };
            std::vector<Acc> acc(REG_COUNT);
            for (float y = -10200.f; y < 10200.f; y += 50.f)
                for (float x = -10200.f; x < 10200.f; x += 50.f) {
                    if (map.isWater(x, y)) continue;
                    Acc& A = acc[map.regionAt(x, y)];
                    A.sx += x; A.sy += y; A.n++;
                    A.x0 = Min(A.x0, x); A.y0 = Min(A.y0, y); A.x1 = Max(A.x1, x); A.y1 = Max(A.y1, y);
                }
            for (int r = 0; r < REG_COUNT; r++)
                if (acc[r].n) printf("region %2d %-26s centroid (%6.0f, %6.0f) bbox x %6.0f..%6.0f y %6.0f..%6.0f area %.1f km2\n", r, regionInfo((Region)r).name,
                                     acc[r].sx / acc[r].n, acc[r].sy / acc[r].n, acc[r].x0, acc[r].x1, acc[r].y0, acc[r].y1, acc[r].n * 2500.0 / 1e6);
        } else if (cmd == "kindstats") {
            int kind = (int)F(0);
            size_t tot = 0, mx = 0;
            int n = 0;
            for (size_t i = 0; i < gSites->elems.size(); i++) {
                const SiteElem& e = gSites->elems[i];
                if (e.kind != kind) continue;
                for (int d = 1; d >= 0; d--) {
                    int cx = (int)floorf((e.c.x + kWorldHalf) / kCellSize), cy = (int)floorf((e.c.y + kWorldHalf) / kCellSize);
                    CellGeometry out;
                    sitegeo::G g;
                    g.m = &out.opaque;
                    g.d = &out.decals;
                    g.org = vec3(cellOrigin(cx, cy), 0.f);
                    g.detail = d == 1;
                    g.cx = cx;
                    g.cy = cy;
                    g.col = d ? &out.collision : nullptr;
                    g.props = d ? &out.props : nullptr;
                    g.lights = d ? &out.lights : nullptr;
                    double t0 = TimeSeconds();
                    genPlaceElem(e, g);
                    size_t nv = out.opaque.verts.size() + out.decals.verts.size();
                    printf("elem %zu kind %d var %d '%s' %s: %zu verts, %zu props, %zu lights, %zu cols, %.2f ms\n", i, e.kind, e.variant, e.text.c_str(), d ? "near" : "far ", nv,
                           out.props.size(), out.lights.size(), out.collision.size(), (TimeSeconds() - t0) * 1000.0);
                    if (d) { tot += nv; mx = Max(mx, nv); n++; }
                }
            }
            printf("kind %d: %d elements, near total %zu verts, max %zu\n", kind, n, tot, mx);
        } else if (cmd == "region") {
            vec2 p(F(0), F(1));
            printf("region at (%.0f, %.0f): %s, height %.2f, water %d, coast %.0f\n", p.x, p.y, regionInfo(map.regionAt(p.x, p.y)).name, map.heightAt(p.x, p.y),
                   (int)map.isWater(p.x, p.y), map.coastDistance(p.x, p.y));
        }
    }
    Jobs::shutdown();
    return 0;
}
