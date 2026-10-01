// Native (Linux) vehicle door / entry preview: builds a car with the real vehicle generator (opening doors cut out of
// the body) and renders it with the preview rasterizer (tests/anim/preview.cpp), optionally with a character getting
// in or out through a door.
// Build: g++ -O2 -std=c++17 -I src tests/anim/carview.cpp -o /tmp/carview
// Usage: carview out.ppm [--model N] [--open F] [--door D] [--view side|front34|rear34|top|inside|back]
//                        [--w W] [--h H] [--ss N] [--yaw deg] [--pitch deg] [--dist D] [--target x,y,z] [--fov deg]
//                        [--info] (print the doors' metadata) [--list] (models) [--wire]
#define PREVIEW_NO_MAIN
#include "preview.cpp"
#include "../../src/sim/vehicle_models.cpp"

using namespace Vehicles;

static vec3 paintCol(0.62f, 0.07f, 0.06f), paint2(0.9f, 0.9f, 0.9f);

static vec3 carAlbedo(u32 mat, u32 color) {
    vec3 c(((color >> 0) & 255) / 255.f, ((color >> 8) & 255) / 255.f, ((color >> 16) & 255) / 255.f);
    float a = ((color >> 24) & 255) / 255.f;
    switch (mat & 0xff) {
        case MAT_CARPAINT: return mulColor(c, a > 0.5f ? paintCol : paint2) * 0.85f;
        case MAT_PLASTIC: return c * 0.08f;
        case MAT_RUBBER: case MAT_TIRE: return vec3(0.03f);
        case MAT_CAR_GLASS: return vec3(0.025f);
        case MAT_CHROME: return c * 0.75f;
        case MAT_INTERIOR: return c * 0.09f;
        case MAT_LEATHER: return c * 0.12f;
        case MAT_FABRIC: return c * 0.25f;
        case MAT_METAL_BRUSHED: return c * 0.5f;
        case MAT_METAL_PAINTED: return c * 0.35f;
        case MAT_RIM: return c * 0.6f;
        case MAT_LIGHT_HEAD: return vec3(0.8f);
        case MAT_LIGHT_TAIL: return vec3(0.6f, 0.05f, 0.04f);
        case MAT_LIGHT_INDICATOR: return vec3(0.8f, 0.45f, 0.05f);
        case MAT_EMISSIVE: return c * 0.5f;
        default: return c * 0.6f;
    }
}

struct Geo {
    std::vector<vec3> P, N, A;
    std::vector<u32> M, I;
    std::vector<vec3> GP;   // glass triangles (positions, 3 per triangle)
};
// Appends a vehicle mesh transformed by (rotation q about pivot, then translation t)
static void addMesh(Geo& g, const MeshData& m, quat q, vec3 pivot, vec3 t) {
    u32 base = (u32)g.P.size();
    for (const VtxStatic& v : m.verts) {
        g.P.push_back(pivot + rotate(q, v.pos - pivot) + t);
        g.N.push_back(rotate(q, unpackNormalOct(v.normal)));
        g.A.push_back(carAlbedo(v.mat, v.color));
        g.M.push_back(v.mat);
    }
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        u32 a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
        if ((m.verts[a].mat & 0xff) == MAT_CAR_WINDOW) {
            g.GP.push_back(g.P[base + a]);
            g.GP.push_back(g.P[base + b]);
            g.GP.push_back(g.P[base + c]);
            continue;
        }
        g.I.push_back(base + a);
        g.I.push_back(base + b);
        g.I.push_back(base + c);
    }
}

// See-through glass: depth-tested, blended over what is behind (no depth write)
static void drawGlass(Img& img, const Cam& cam, const std::vector<vec3>& P) {
    for (size_t t = 0; t + 2 < P.size(); t += 3) {
        vec3 s[3];
        bool okAll = true;
        for (int k = 0; k < 3; k++) {
            vec4 v = cam.view * vec4(P[t + k], 1.f);
            if (-v.z < gNear) okAll = false;
            vec4 c = cam.proj * v;
            float iw = 1.f / c.w;
            s[k] = vec3((c.x * iw * 0.5f + 0.5f) * img.w, (0.5f - c.y * iw * 0.5f) * img.h, -v.z);
        }
        if (!okAll) continue;
        float area = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[1].y - s[0].y) * (s[2].x - s[0].x);
        if (fabsf(area) < 1e-9f) continue;
        float ia = 1.f / area;
        int x0 = Max(0, (int)floorf(Min(s[0].x, Min(s[1].x, s[2].x)))), x1 = Min(img.w - 1, (int)ceilf(Max(s[0].x, Max(s[1].x, s[2].x))));
        int y0 = Max(0, (int)floorf(Min(s[0].y, Min(s[1].y, s[2].y)))), y1 = Min(img.h - 1, (int)ceilf(Max(s[0].y, Max(s[1].y, s[2].y))));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                float px = x + 0.5f, py = y + 0.5f;
                float w0 = ((s[1].x - px) * (s[2].y - py) - (s[1].y - py) * (s[2].x - px)) * ia;
                float w1 = ((s[2].x - px) * (s[0].y - py) - (s[2].y - py) * (s[0].x - px)) * ia;
                float w2 = 1.f - w0 - w1;
                if (w0 < 0.f || w1 < 0.f || w2 < 0.f) continue;
                float z = w0 * s[0].z + w1 * s[1].z + w2 * s[2].z;
                size_t o = (size_t)y * img.w + x;
                if (z >= img.z[o]) continue;
                img.c[o] = img.c[o] * 0.72f + vec3(0.05f, 0.065f, 0.075f);
            }
    }
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "/tmp/carview.ppm";
    int model = 3, W = 1000, H = 640, ss = 2, door = -1;
    float open = 0.f, yaw = -1e9f, pitch = 12.f, dist = -1.f, fov = 32.f;
    const char* view = "front34";
    bool info = false, list = false, haveTarget = false;
    vec3 target(0.f);
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        auto nx = [&]() { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--model") model = atoi(nx());
        else if (a == "--open") open = (float)atof(nx());
        else if (a == "--door") door = atoi(nx());
        else if (a == "--view") view = nx();
        else if (a == "--w") W = atoi(nx());
        else if (a == "--h") H = atoi(nx());
        else if (a == "--ss") ss = atoi(nx());
        else if (a == "--yaw") yaw = (float)atof(nx());
        else if (a == "--pitch") pitch = (float)atof(nx());
        else if (a == "--dist") dist = (float)atof(nx());
        else if (a == "--fov") fov = (float)atof(nx());
        else if (a == "--target") {
            sscanf(nx(), "%f,%f,%f", &target.x, &target.y, &target.z);
            haveTarget = true;
        } else if (a == "--info") info = true;
        else if (a == "--list") list = true;
        else if (a == "--wire") wire = true;
    }
    if (list) {
        for (int i = 0; i < modelCount(); i++) {
            VehicleModel vm;
            buildModel(i, vm);
            printf("%2d %-16s cls %2d doors %zu seats %zu\n", i, vm.name.c_str(), (int)vm.cls, vm.doors.size(), vm.seats.size());
        }
        return 0;
    }
    VehicleModel vm;
    double t0 = TimeSeconds();
    buildModel(model, vm);
    double t1 = TimeSeconds();
    if (info) {
        printf("%s (%s): body %zu tris, built in %.0f ms\n", vm.name.c_str(), vm.maker.c_str(), vm.body.indices.size() / 3, (t1 - t0) * 1e3);
        for (size_t d = 0; d < vm.doors.size(); d++) {
            const DoorSpec& D = vm.doors[d];
            printf(" door %zu %s %s: %zu tris  hinge (%.3f %.3f %.3f) axis (%.3f %.3f %.3f) max %.2f\n", d, D.left ? "L" : "R", D.front ? "front" : "rear",
                   D.mesh.indices.size() / 3, D.hinge.x, D.hinge.y, D.hinge.z, D.axis.x, D.axis.y, D.axis.z, D.maxAngle);
            printf("   handle (%.3f %.3f %.3f) in (%.3f %.3f %.3f) grip (%.3f %.3f %.3f)  y %.3f..%.3f sill z %.3f x %.3f roof %.3f\n", D.handle.x,
                   D.handle.y, D.handle.z, D.handleIn.x, D.handleIn.y, D.handleIn.z, D.grip.x, D.grip.y, D.grip.z, D.yRear, D.yFront, D.sillZ,
                   D.sillX, D.roofZ);
        }
        for (size_t s = 0; s < vm.seats.size(); s++)
            printf(" seat %zu (%.3f %.3f %.3f) door %d\n", s, vm.seats[s].pos.x, vm.seats[s].pos.y, vm.seats[s].pos.z, vm.seats[s].door);
    }
    Geo g;
    addMesh(g, vm.body, quat(), vec3(0.f), vec3(0.f));
    for (size_t d = 0; d < vm.doors.size(); d++) {
        const DoorSpec& D = vm.doors[d];
        float a = (door < 0 || door == (int)d) ? open * D.maxAngle : 0.f;
        addMesh(g, D.mesh, quatAxisAngle(D.axis, a), D.hinge, vec3(0.f));
    }
    if (vm.steerWheel.indices.size()) {
        vec3 za = vm.steerWheelAxis, xa(1, 0, 0);
        mat3 R(xa, normalize(cross(za, xa)), za);
        MeshData sw = vm.steerWheel;
        for (VtxStatic& v : sw.verts) {
            v.pos = vm.steerWheelPos + R * v.pos;
            v.normal = packNormalOct(R * unpackNormalOct(v.normal));
        }
        addMesh(g, sw, quat(), vec3(0.f), vec3(0.f));
    }
    for (const WheelSpec& w : vm.wheels) {
        MeshData wm = vm.wheel;
        quat q = w.left ? quatAxisAngle(vec3(0, 0, 1), kPi) : quat();
        for (VtxStatic& v : wm.verts) {
            v.pos = w.pos + rotate(q, v.pos);
            v.normal = packNormalOct(rotate(q, unpackNormalOct(v.normal)));
        }
        addMesh(g, wm, quat(), vec3(0.f), vec3(0.f));
    }
    // camera
    AABB bb = vm.body.bounds;
    vec3 ctr = haveTarget ? target : vec3(0.f, (bb.mn.y + bb.mx.y) * 0.5f, 0.75f);
    std::string v = view;
    float yw = -60.f, R = dist > 0.f ? dist : 7.5f;
    if (v == "side") yw = -90.f;
    else if (v == "front34") yw = -50.f;
    else if (v == "rear34") yw = -130.f;
    else if (v == "back") yw = 180.f;
    else if (v == "top") { yw = -90.f; pitch = 75.f; }
    if (yaw > -1e8f) yw = yaw;
    // yaw measured from +Y (car forward) towards -X (the car's left side, the driver's door)
    float ya = yw * kDegToRad, pa = pitch * kDegToRad;
    vec3 dir(sinf(ya) * cosf(pa), cosf(ya) * cosf(pa), sinf(pa));
    Cam cam;
    cam.eye = ctr + dir * R;
    cam.target = ctr;
    cam.fov = fov;
    gNear = 0.05f;
    Img img(W * ss, H * ss);
    cam.setup(img.w, img.h);
    // ground
    {
        std::vector<vec3> P = {vec3(-30, -30, 0), vec3(30, -30, 0), vec3(30, 30, 0), vec3(-30, 30, 0)};
        std::vector<vec3> N(4, vec3(0, 0, 1)), A(4, vec3(0.3f, 0.3f, 0.29f));
        std::vector<u32> M(4, MAT_CONCRETE), I = {0, 1, 2, 0, 2, 3};
        drawMesh(img, cam, P, N, A, M, I);
    }
    drawMesh(img, cam, g.P, g.N, g.A, g.M, g.I);
    drawGlass(img, cam, g.GP);
    if (ss > 1) img.down(ss).save(out);
    else img.save(out);
    return 0;
}
