#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/noise.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/jobs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/worldmap.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sites.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roads.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roadmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/buildings.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/buildmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/propmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/cellgen.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sitegeo.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/airport.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/port.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/landmarks.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/leisure.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/rural.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/transit.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/transitmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sitecell.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/facadedetail.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorkit.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorfurniture.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorlayouts.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorhomes.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorvenues.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorshops.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorcivic.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorindustrial.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiortower.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorgarages.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorresidences.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiors.cpp"

#include <thread>
using namespace World;
struct Img {
    int w, h;
    std::vector<vec3> c;
    std::vector<float> z;
    void init(int W, int H) { w = W; h = H; c.assign(W * H, vec3(0)); z.assign(W * H, 1e30f); }
};

struct Cam {
    vec3 eye, fwd, right, up;
    float f;  // focal (pixels)
    bool ortho = false;
    float orthoScale = 1.f;
    int w, h;
    void look(vec3 e, vec3 t, int W, int H, float fovDeg) {
        eye = e; w = W; h = H;
        fwd = normalize(t - e);
        right = normalize(cross(fwd, vec3(0, 0, 1)));
        up = cross(right, fwd);
        f = (H * 0.5f) / tanf(fovDeg * 0.5f * kDegToRad);
    }
    bool proj(vec3 p, vec3& s) const {
        vec3 d = p - eye;
        float zc = dot(d, fwd);
        if (zc < 0.05f) return false;
        float x = dot(d, right), y = dot(d, up);
        if (ortho) { s = vec3(w * 0.5f + x * orthoScale, h * 0.5f - y * orthoScale, zc); return true; }
        s = vec3(w * 0.5f + x * f / zc, h * 0.5f - y * f / zc, zc);
        return true;
    }
};

static vec3 gPrimary(0.60f, 0.06f, 0.05f), gSecondary(0.1f, 0.1f, 0.1f);
static bool gLights = false;
static int gPickX = -1, gPickY = -1;
static char gPickInfo[512] = "";

vec3 skyCol(vec3 r) {
    float t = Saturate(r.z * 0.5f + 0.5f);
    vec3 horizon(0.75f, 0.8f, 0.85f), zen(0.25f, 0.45f, 0.8f), ground(0.25f, 0.22f, 0.2f);
    return r.z >= 0 ? lerp(horizon, zen, Saturate(r.z * 1.4f)) : lerp(horizon, ground, Saturate(-r.z * 3.f));
}

// world material shading on top of the vehicle palette
vec3 worldBase(u32 mat, vec4 vc) {
    switch (mat) {
        case MAT_ASPHALT: case MAT_ASPHALT_OLD: return vec3(0.16f) * vc.xyz() * 2.f;
        case MAT_SIDEWALK: case MAT_CURB: case MAT_CONCRETE: case MAT_CONCRETE_PANEL: case MAT_PAVERS: return vec3(0.55f) * vc.xyz();
        case MAT_PAINT_WHITE: return vec3(0.85f) * vc.xyz();
        case MAT_PAINT_YELLOW: return vec3(0.8f, 0.65f, 0.1f);
        case MAT_GLASS: return vec3(0.12f, 0.16f, 0.2f);
        case MAT_GRASS: return vec3(0.18f, 0.3f, 0.1f);
        case MAT_SAND: return vec3(0.75f, 0.68f, 0.5f);
        default: return vec3(0.5f) * vc.xyz();
    }
}

vec3 shade(u32 matw, u32 colw, vec3 n, vec3 v, vec3 pos, bool back) {
    u32 mat = matw & 0xff;
    if (back) {
        // magenta, hue-shifted per material to identify the culprit
        float h = (float)(mat % 7) / 7.f;
        return vec3(1.f, h * 0.6f, 1.f - h * 0.8f);
    }
    vec4 vc = unpackRGBA8(colw);
    vec3 L = normalize(vec3(-0.45f, 0.5f, 0.75f));
    vec3 base(0.5f);
    float spec = 0.f, refl = 0.f, metal = 0.f;
    vec3 emis(0);
    switch (mat) {
        case MAT_CARPAINT: base = vc.w > 0.5f ? gPrimary : gSecondary; spec = 0.5f; refl = 0.12f; break;
        case MAT_CAR_GLASS: base = vec3(0.015f, 0.02f, 0.025f); spec = 0.8f; refl = 0.35f; break;
        case MAT_CAR_WINDOW: base = vec3(0.02f, 0.025f, 0.03f) * vc.xyz(); spec = 0.9f; refl = 0.45f; break;
        case MAT_PLASTIC: base = vec3(0.045f) * vc.xyz(); spec = 0.08f; break;
        case MAT_CHROME: base = vec3(0.9f); metal = 1.f; refl = 0.9f; spec = 1.f; break;
        case MAT_RIM: base = vec3(0.7f) * vc.xyz(); metal = 1.f; refl = 0.6f; spec = 0.6f; break;
        case MAT_TIRE: base = vec3(0.035f); spec = 0.03f; break;
        case MAT_RUBBER: base = vec3(0.03f); break;
        case MAT_LIGHT_HEAD: base = vec3(0.85f, 0.85f, 0.82f); spec = 0.8f; refl = 0.3f; if (gLights) emis = vec3(3, 3, 2.6f); break;
        case MAT_LIGHT_TAIL: base = vc.y > 0.5f ? vec3(0.8f, 0.75f, 0.75f) : vec3(0.45f, 0.02f, 0.02f); spec = 0.6f; refl = 0.2f;
            if (gLights && vc.y <= 0.5f) emis = vec3(2.5f, 0.1f, 0.05f); break;
        case MAT_LIGHT_INDICATOR: base = vc.w < 0.5f ? vc.xyz() * 0.6f : vec3(0.7f, 0.4f, 0.03f); spec = 0.6f; if (gLights) emis = vc.xyz() * 2.f; break;
        case MAT_INTERIOR: base = vec3(0.07f) * vc.xyz(); break;
        case MAT_LEATHER: base = vec3(0.12f, 0.07f, 0.04f) * vc.xyz(); spec = 0.1f; break;
        case MAT_FABRIC: base = vec3(0.85f) * vc.xyz(); break;
        case MAT_METAL_PAINTED: base = vec3(0.6f) * vc.xyz(); spec = 0.2f; break;
        case MAT_METAL_BRUSHED: base = vec3(0.75f) * vc.xyz(); metal = 1.f; refl = 0.4f; spec = 0.4f; break;
        case MAT_EMISSIVE: base = vc.xyz(); emis = vc.xyz() * vc.w * 2.f; break;
        case MAT_WOOD: base = vec3(0.35f, 0.22f, 0.12f) * vc.xyz(); break;
        default: base = worldBase(mat, vc); break;
    }
    float ndl = Max(dot(n, L), 0.f);
    vec3 amb = lerp(vec3(0.18f, 0.17f, 0.16f), vec3(0.35f, 0.4f, 0.5f), Saturate(n.z * 0.5f + 0.5f));
    vec3 diff = base * (1.f - metal);
    vec3 col = diff * (amb + vec3(1.0f, 0.95f, 0.85f) * ndl * 1.1f);
    vec3 r = reflect(-v, n);
    float fres = 0.04f + 0.96f * powf(1.f - Saturate(dot(n, v)), 5.f);
    vec3 env = skyCol(r);
    if (metal > 0.f) col += env * base * refl;
    else col += env * (refl * 0.3f + fres * refl);
    float sp = powf(Max(dot(r, L), 0.f), 60.f) * spec;
    col += vec3(sp);
    col += emis;
    return col;
}

struct DrawMesh {
    const MeshData* m;
    mat4 M;
};

void raster(Img& img, const Cam& cam, const MeshData& m, const mat4& M, bool wire) {
    size_t nt = m.indices.size() / 3;
    mat3 R(M.c[0].xyz(), M.c[1].xyz(), M.c[2].xyz());
    for (size_t t = 0; t < nt; t++) {
        const VtxStatic* v[3] = {&m.verts[m.indices[t * 3]], &m.verts[m.indices[t * 3 + 1]], &m.verts[m.indices[t * 3 + 2]]};
        vec3 wp[3], sp[3], wn[3];
        bool ok = true;
        for (int k = 0; k < 3; k++) {
            wp[k] = transformPoint(M, v[k]->pos);
            wn[k] = normalize(R * unpackNormalOct(v[k]->normal));
            if (!cam.proj(wp[k], sp[k])) ok = false;
        }
        if (!ok) continue;
        float area = (sp[1].x - sp[0].x) * (sp[2].y - sp[0].y) - (sp[2].x - sp[0].x) * (sp[1].y - sp[0].y);
        bool back = area > 0.f;  // screen y down: CCW front faces have negative area
        if (fabsf(area) < 1e-6f) continue;
        int x0 = Max(0, (int)floorf(Min(sp[0].x, Min(sp[1].x, sp[2].x))));
        int x1 = Min(img.w - 1, (int)ceilf(Max(sp[0].x, Max(sp[1].x, sp[2].x))));
        int y0 = Max(0, (int)floorf(Min(sp[0].y, Min(sp[1].y, sp[2].y))));
        int y1 = Min(img.h - 1, (int)ceilf(Max(sp[0].y, Max(sp[1].y, sp[2].y))));
        if (x0 > x1 || y0 > y1) continue;
        float ia = 1.f / area;
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((sp[1].x - px) * (sp[2].y - py) - (sp[2].x - px) * (sp[1].y - py)) * ia;
                float w1 = ((sp[2].x - px) * (sp[0].y - py) - (sp[0].x - px) * (sp[2].y - py)) * ia;
                float w2 = 1.f - w0 - w1;
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                if ((v[0]->mat & 0xff) == MAT_CAR_WINDOW) {
                    // see-through window: back faces culled, screen-door transparency by clarity (vertex alpha)
                    if (back) continue;
                    float clarity = unpackRGBA8(v[0]->color).w;
                    float th = (((x & 1) * 2 + (y & 1)) + 0.5f) / 4.f;
                    if (th < clarity * 0.7f) continue;
                }
                // perspective-correct
                float iz = w0 / sp[0].z + w1 / sp[1].z + w2 / sp[2].z;
                float z = 1.f / iz;
                if (back) z *= 1.002f;  // front faces win against coincident back faces
                int idx = y * img.w + x;
                if (z >= img.z[idx]) continue;
                img.z[idx] = z;
                float b0 = w0 / sp[0].z * z, b1 = w1 / sp[1].z * z, b2 = w2 / sp[2].z * z;
                vec3 n = normalize(wn[0] * b0 + wn[1] * b1 + wn[2] * b2);
                vec3 p = wp[0] * b0 + wp[1] * b1 + wp[2] * b2;
                vec3 vv = normalize(cam.eye - p);
                if (dot(n, vv) < 0.f && !back) n = n;  // keep
                vec3 c = shade(v[0]->mat, v[0]->color, n, vv, p, back);
                if (wire) {
                    float mw = Min(w0, Min(w1, w2));
                    if (mw < 0.02f) c = c * 0.4f + vec3(0.2f, 0.9f, 0.3f) * 0.6f;
                }
                img.c[idx] = c;
                if (x == gPickX && y == gPickY) {
                    vec3 fn = normalize(cross(wp[1] - wp[0], wp[2] - wp[0]));
                    snprintf(gPickInfo, sizeof(gPickInfo), "tri %zu mat %u back %d p=(%.3f %.3f %.3f) fn=(%.2f %.2f %.2f) v0=(%.3f %.3f %.3f) v1=(%.3f %.3f %.3f) v2=(%.3f %.3f %.3f)",
                             t, v[0]->mat & 0xff, (int)back, p.x, p.y, p.z, fn.x, fn.y, fn.z, wp[0].x, wp[0].y, wp[0].z, wp[1].x, wp[1].y, wp[1].z, wp[2].x, wp[2].y, wp[2].z);
                }
            }
    }
}

void background(Img& img, const Cam& cam) {
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < img.w; x++) {
            vec3 d = normalize(cam.fwd * cam.f + cam.right * (x + 0.5f - img.w * 0.5f) - cam.up * (y + 0.5f - img.h * 0.5f));
            if (cam.ortho) d = cam.fwd;
            vec3 c = skyCol(d) * 0.9f;
            // ground plane z = 0
            vec3 o = cam.eye;
            if (cam.ortho) o = cam.eye + cam.right * ((x + 0.5f - img.w * 0.5f) / cam.orthoScale) - cam.up * ((y + 0.5f - img.h * 0.5f) / cam.orthoScale);
            if (d.z < -1e-4f) {
                float t = -o.z / d.z;
                vec3 p = o + d * t;
                int cx = (int)floorf(p.x), cy = (int)floorf(p.y);
                float chk = ((cx + cy) & 1) ? 0.30f : 0.36f;
                c = vec3(chk, chk * 0.98f, chk * 0.95f);
                img.z[y * img.w + x] = t;
            }
            img.c[y * img.w + x] = c;
        }
}

void writePPM(const char* path, const std::vector<vec3>& c, int w, int h) {
    FILE* f = fopen(path, "wb");
    fprintf(f, "P6 %d %d 255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        vec3 v = c[i];
        v = vec3(v.x / (1.f + v.x * 0.15f), v.y / (1.f + v.y * 0.15f), v.z / (1.f + v.z * 0.15f));
        unsigned char px[3] = {(unsigned char)(powf(Saturate(v.x), 1.f / 2.2f) * 255.f), (unsigned char)(powf(Saturate(v.y), 1.f / 2.2f) * 255.f),
                               (unsigned char)(powf(Saturate(v.z), 1.f / 2.2f) * 255.f)};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}



int main(int argc, char** argv) {
    if (argc < 9) { printf("usage: cellview out.ppm ex ey ez tx ty tz fov [w h radius]\n"); return 1; }
    vec3 eye((float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])), tgt((float)atof(argv[5]), (float)atof(argv[6]), (float)atof(argv[7]));
    float fov = (float)atof(argv[8]);
    int W = argc > 9 ? atoi(argv[9]) : 960, H = argc > 10 ? atoi(argv[10]) : 540;
    float radius = argc > 11 ? (float)atof(argv[11]) : 180.f;
    if (argc > 13) { gPickX = atoi(argv[12]); gPickY = atoi(argv[13]); }
    Jobs::init(2);
    WorldMap map;
    map.generate();
    gMap = &map;
    RoadNetwork roads;
    roads.generate(map);
    gRoads = &roads;
    BuildingSet bs;
    bs.generate(map, roads);
    gBuildings = &bs;
    // cells around the view: between the eye and the target, plus radius
    vec2 mn = vmin(eye.xy(), tgt.xy()) - vec2(radius), mx = vmax(eye.xy(), tgt.xy()) + vec2(radius);
    int cps = kCellsPerSide;
    int x0 = Clamp((int)floorf((mn.x + kWorldHalf) / kCellSize), 0, cps - 1), x1 = Clamp((int)floorf((mx.x + kWorldHalf) / kCellSize), 0, cps - 1);
    int y0 = Clamp((int)floorf((mn.y + kWorldHalf) / kCellSize), 0, cps - 1), y1 = Clamp((int)floorf((mx.y + kWorldHalf) / kCellSize), 0, cps - 1);
    std::vector<CellGeometry*> geos;
    std::vector<vec3> orgs;
    for (int cy = y0; cy <= y1; cy++)
        for (int cx = x0; cx <= x1; cx++) {
            CellGeometry* geo = new CellGeometry;
            generateCell(cx, cy, true, *geo);
            geos.push_back(geo);
            orgs.push_back(vec3(cellOrigin(cx, cy), 0.f));
        }
    Img img;
    img.init(W, H);
    Cam cam;
    cam.look(eye, tgt, W, H, fov);
    background(img, cam);
    size_t tris = 0;
    for (size_t i = 0; i < geos.size(); i++) {
        mat4 M = mat4Translation(orgs[i]);
        raster(img, cam, geos[i]->opaque, M, false);
        tris += geos[i]->opaque.indices.size() / 3;
    }
    // decals: pulled a little toward the camera
    for (size_t i = 0; i < geos.size(); i++) {
        vec3 toCam = normalize(eye - (orgs[i] + vec3(kCellSize * 0.5f, kCellSize * 0.5f, 0.f)));
        mat4 M = mat4Translation(orgs[i] + toCam * 0.04f);
        raster(img, cam, geos[i]->decals, M, false);
    }
    // props as small markers
    for (size_t i = 0; i < geos.size(); i++)
        for (const PropInstance& p : geos[i]->props) {
            vec3 s;
            if (!cam.proj(p.pos + vec3(0, 0, 1.f), s)) continue;
            int px = (int)s.x, py = (int)s.y;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int x = px + dx, y = py + dy;
                    if (x < 0 || y < 0 || x >= W || y >= H) continue;
                    if (s.z > img.z[y * W + x] + 0.5f) continue;
                    img.c[y * W + x] = vec3(0.9f, 0.2f, 0.9f);
                }
        }
    writePPM(argv[1], img.c, W, H);
    printf("wrote %s: %zu cells, %zu triangles\n", argv[1], geos.size(), tris);
    if (gPickInfo[0]) printf("pick: %s\n", gPickInfo);
    fflush(stdout);
    Jobs::shutdown();
    return 0;
}
