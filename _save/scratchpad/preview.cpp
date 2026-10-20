// Native software-rasterized preview of the procedural vehicle models (development tool, not shipped).
// Usage: preview <index|-1 for all> <outdir> [--views N] [--wire]
#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "vm/vehicle_models.cpp"
#include <chrono>

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
        default: base = vec3(0.5f) * vc.xyz(); break;
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

void renderView(const Vehicles::VehicleModel& vm, const Cam& cam, Img& img, bool wire) {
    background(img, cam);
    mat4 I;
    raster(img, cam, vm.body, I, wire);
    for (auto& w : vm.wheels) {
        mat4 M = mat4TRS(w.pos, quatAxisAngle(vec3(0, 0, 1), w.left ? kPi : 0.f), vec3(1));
        raster(img, cam, vm.wheel, M, wire);
    }
    if (!vm.rotor.empty()) raster(img, cam, vm.rotor, mat4Translation(vm.rotorPos), wire);
    if (!vm.tailRotor.empty()) raster(img, cam, vm.tailRotor, mat4Translation(vm.tailRotorPos), wire);
}

int main(int argc, char** argv) {
    int which = argc > 1 ? atoi(argv[1]) : 0;
    std::string outdir = argc > 2 ? argv[2] : "/tmp";
    bool wire = false;
    int mode = 0;  // 0 sheet, 1 closeups
    std::string only;
    bool customCam = false;
    vec3 ce, ct;
    float cfov = 30.f;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--wire")) wire = true;
        if (!strcmp(argv[i], "--close")) mode = 1;
        if (!strcmp(argv[i], "--lights")) gLights = true;
        if (!strcmp(argv[i], "--pick") && i + 2 < argc) { gPickX = atoi(argv[i + 1]) * 2; gPickY = atoi(argv[i + 2]) * 2; i += 2; }
        if (!strcmp(argv[i], "--cam") && i + 7 < argc) {
            customCam = true;
            ce = vec3((float)atof(argv[i + 1]), (float)atof(argv[i + 2]), (float)atof(argv[i + 3]));
            ct = vec3((float)atof(argv[i + 4]), (float)atof(argv[i + 5]), (float)atof(argv[i + 6]));
            cfov = (float)atof(argv[i + 7]);
            i += 7;
        }
        if (!strcmp(argv[i], "--color") && i + 3 < argc) { gPrimary = vec3((float)atof(argv[i + 1]), (float)atof(argv[i + 2]), (float)atof(argv[i + 3])); i += 3; }
    }
    int n = Vehicles::modelCount();
    int a = which < 0 ? 0 : which, b = which < 0 ? n - 1 : which;
    for (int idx = a; idx <= b; idx++) {
        Vehicles::VehicleModel vm;
        auto t0 = std::chrono::steady_clock::now();
        Vehicles::buildModel(idx, vm);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!vm.paletteColors.empty() && !vm.fixedLivery) gPrimary = vm.paletteColors[idx % vm.paletteColors.size()];
        if (vm.fixedLivery) { gPrimary = vm.liveryPrimary; gSecondary = vm.liverySecondary; }
        else gSecondary = vec3(0.1f);
        AABB bb = vm.body.bounds;
        printf("[%d] %s %s cls=%d body=%zu tris wheel=%zu rotor=%zu tail=%zu  (%.1f ms)\n", idx, vm.maker.c_str(), vm.name.c_str(), (int)vm.cls,
               vm.body.indices.size() / 3, vm.wheel.indices.size() / 3, vm.rotor.indices.size() / 3, vm.tailRotor.indices.size() / 3, ms);
        printf("    bounds (%.2f %.2f %.2f)-(%.2f %.2f %.2f) size %.2f x %.2f x %.2f\n", bb.mn.x, bb.mn.y, bb.mn.z, bb.mx.x, bb.mx.y, bb.mx.z,
               bb.mx.x - bb.mn.x, bb.mx.y - bb.mn.y, bb.mx.z - bb.mn.z);
        if (!vm.wheel.empty()) {
            float rmax = 0, xmin = 1e9f, xmax = -1e9f;
            for (auto& v : vm.wheel.verts) {
                rmax = Max(rmax, sqrtf(v.pos.y * v.pos.y + v.pos.z * v.pos.z));
                xmin = Min(xmin, v.pos.x); xmax = Max(xmax, v.pos.x);
            }
            printf("    wheel mesh radius %.3f (spec %.3f) width %.3f (spec %.3f)\n", rmax, vm.wheels.empty() ? 0.f : vm.wheels[0].radius, xmax - xmin,
                   vm.wheels.empty() ? 0.f : vm.wheels[0].width);
        }
        for (auto& w : vm.wheels) printf("    wheel (%.2f %.2f %.2f) r=%.3f steer=%d drive=%d left=%d\n", w.pos.x, w.pos.y, w.pos.z, w.radius, w.steer, w.drive, w.left);
        printf("    mass %.0f power %.0f kW torque %.0f top %.1f m/s gears %d driveF %.2f box c(%.2f %.2f %.2f) h(%.2f %.2f %.2f) com z %.2f seats %zu lights %zu\n",
               vm.mass, vm.power, vm.torque, vm.topSpeed, vm.gears, vm.driveFront, vm.boxCenter.x, vm.boxCenter.y, vm.boxCenter.z, vm.boxHalf.x,
               vm.boxHalf.y, vm.boxHalf.z, vm.centerOfMass.z, vm.seats.size(), vm.lights.size());
        // views
        const int W = 560, H = 360, SS = 2;
        vec3 c = bb.center();
        float L = Max(bb.mx.y - bb.mn.y, Max(bb.mx.x - bb.mn.x, bb.mx.z - bb.mn.z));
        std::vector<Cam> cams;
        float dist = L * 1.35f + 2.f;
        if (mode == 0) {
            Cam k;
            k.look(c + vec3(-0.55f, 0.78f, 0.28f) * dist, c, W * SS, H * SS, 34.f); cams.push_back(k);
            k.look(c + vec3(0.6f, -0.75f, 0.3f) * dist, c, W * SS, H * SS, 34.f); cams.push_back(k);
            k.look(c + vec3(-1.f, 0.0f, 0.05f) * dist, c, W * SS, H * SS, 34.f); cams.push_back(k);
            k.look(c + vec3(0.0f, 1.f, 0.12f) * dist, c, W * SS, H * SS, 30.f); cams.push_back(k);
            k.look(c + vec3(0.0f, -1.f, 0.15f) * dist, c, W * SS, H * SS, 30.f); cams.push_back(k);
            k.look(c + vec3(-0.35f, 0.25f, 1.f) * dist, c, W * SS, H * SS, 34.f); cams.push_back(k);
        } else {
            Cam k;
            vec3 fc = vec3(0, bb.mx.y - 0.4f, (bb.mn.z + bb.mx.z) * 0.45f);
            k.look(fc + vec3(-1.2f, 1.6f, 0.4f), fc, W * SS, H * SS, 40.f); cams.push_back(k);
            vec3 rc = vec3(0, bb.mn.y + 0.4f, (bb.mn.z + bb.mx.z) * 0.45f);
            k.look(rc + vec3(1.2f, -1.6f, 0.45f), rc, W * SS, H * SS, 40.f); cams.push_back(k);
            vec3 wc = vm.wheels.empty() ? c : vm.wheels[0].pos;
            k.look(wc + vec3(-1.6f, 0.9f, 0.4f), wc + vec3(-0.5f, 0, 0), W * SS, H * SS, 40.f); cams.push_back(k);
            vec3 sc = vec3(bb.mn.x, 0.2f, bb.mx.z * 0.75f);
            k.look(sc + vec3(-1.8f, 1.2f, 0.6f), sc, W * SS, H * SS, 45.f); cams.push_back(k);
            k.look(c + vec3(-0.2f, 0.9f, 1.3f) * (dist * 0.55f), c, W * SS, H * SS, 34.f); cams.push_back(k);
            vec3 ic = vec3(-0.3f, 0.4f, bb.mx.z * 0.7f);
            k.look(ic + vec3(-0.1f, -0.9f, 0.9f), ic + vec3(0, 0.5f, -0.2f), W * SS, H * SS, 60.f); cams.push_back(k);
        }
        int cols = 3, rowsN = 2;
        if (customCam) {
            Cam k;
            k.look(ce, ct, W * 3 * SS, H * 2 * SS, cfov);
            Img img;
            img.init(W * 3 * SS, H * 2 * SS);
            renderView(vm, k, img, wire);
            std::vector<vec3> big(W * 3 * H * 2);
            for (int y = 0; y < H * 2; y++)
                for (int x = 0; x < W * 3; x++) {
                    vec3 s2(0);
                    for (int yy = 0; yy < SS; yy++)
                        for (int xx = 0; xx < SS; xx++) s2 += img.c[(y * SS + yy) * img.w + x * SS + xx];
                    big[y * W * 3 + x] = s2 / (float)(SS * SS);
                }
            char path[512];
            if (gPickInfo[0]) printf("PICK: %s\n", gPickInfo);
            snprintf(path, sizeof(path), "%s/m%02dx.ppm", outdir.c_str(), idx);
            writePPM(path, big, W * 3, H * 2);
            continue;
        }
        std::vector<vec3> sheet(W * cols * H * rowsN);
        for (size_t v = 0; v < cams.size(); v++) {
            Img img;
            img.init(W * SS, H * SS);
            renderView(vm, cams[v], img, wire);
            int ox = (int)(v % cols) * W, oy = (int)(v / cols) * H;
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    vec3 s(0);
                    for (int yy = 0; yy < SS; yy++)
                        for (int xx = 0; xx < SS; xx++) s += img.c[(y * SS + yy) * img.w + x * SS + xx];
                    sheet[(oy + y) * W * cols + ox + x] = s / (float)(SS * SS);
                }
        }
        char path[512];
        snprintf(path, sizeof(path), "%s/m%02d%s.ppm", outdir.c_str(), idx, mode == 1 ? "c" : "");
        writePPM(path, sheet, W * cols, H * rowsN);
    }
    return 0;
}
