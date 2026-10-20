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

// The door for `seat` in a ped's model space (ped root at `root`, facing `yaw`, in the vehicle frame)
static CarDoorInfo doorInfo(const VehicleModel& vm, int seat, vec3 root, float yaw, bool belt) {
    CarDoorInfo g;
    const SeatSpec& ss = vm.seats[seat];
    if (ss.door < 0) return g;
    const DoorSpec& D = vm.doors[ss.door];
    quat q = quatAxisAngle(vec3(0, 0, 1), -yaw);
    auto P = [&](vec3 p) { return rotate(q, p - root); };
    auto V = [&](vec3 v) { return rotate(q, v); };
    g.valid = true;
    g.seat = P(ss.pos);
    g.fwd = V(vec3(0, 1, 0));
    g.out = V(D.outward);
    g.hinge = P(D.hinge);
    g.axis = V(D.axis);
    g.maxOpen = D.maxAngle;
    g.handle = P(D.handle);
    g.handleIn = P(D.handleIn);
    g.grip = P(D.grip);
    float sx = D.left ? -D.sillX : D.sillX;
    g.front = P(vec3(sx, D.yFront, D.sillZ));
    g.rear = P(vec3(sx, D.yRear, D.sillZ));
    for (int k = 0; k < 6; k++) g.top[k] = P(D.top[k]);
    g.sillZ = D.sillZ;
    g.roofZ = D.roofZ;
    g.headZ = getenv("CARVIEW_NOFIT") ? 9.f : ss.headZ;   // (CARVIEW_NOFIT: no headroom fit, the door clips' either)
    g.driver = ss.driver;
    g.belt = belt;
    if (ss.driver && vm.steerWheelRadius > 0.f) {
        g.wheelC = P(vm.steerWheelPos);
        g.wheelN = V(normalize(vm.steerWheelAxis));
        g.wheelR = vm.steerWheelRadius;
    }
    return g;
}

// The seated inputs the game gives (peds.cpp animatePed): the driver's hands on this vehicle's own rim
static void seatInputs(const VehicleModel& vm, int seat, AnimInput& in) {
    in.wheelR = 0.f;
    in.headroom = 0.f;
    if ((in.stance == 1 || in.stance == 2) && seat < (int)vm.seats.size() && vm.seats[seat].headZ < 5.f && !getenv("CARVIEW_NOFIT"))
        in.headroom = vm.seats[seat].headZ - (vm.seats[seat].pos.z - 0.5f);
    if ((in.stance == 1 || in.stance == 2) && seat < (int)vm.seats.size() && vm.seats[seat].floorZ > 0.f && !getenv("CARVIEW_NOFIT"))
        in.seatFloor = vm.seats[seat].floorZ - (vm.seats[seat].pos.z - 0.5f);
    if (in.stance == 1 && vm.steerWheelRadius > 0.f && seat < (int)vm.seats.size()) {
        in.wheelC = vm.steerWheelPos - (vm.seats[seat].pos - vec3(0.f, 0.f, 0.5f));
        in.wheelN = normalize(vm.steerWheelAxis);
        in.wheelR = vm.steerWheelRadius;
    }
}

struct CarChar {
    CharacterDesc d;
    Skeleton sk;
    SkinnedMeshData mesh;
};
// Skinned character mesh posed by `pose`, placed at root / yaw (vehicle frame), appended to the scene
static void addChar(Geo& g, const CarChar& ch, const Pose& pose, vec3 root, float yaw) {
    mat4 ms[B_COUNT], skin[B_COUNT];
    computeMatrices(ch.sk, pose, ms, skin);
    quat q = quatAxisAngle(vec3(0, 0, 1), yaw);
    u32 base = (u32)g.P.size();
    for (const VtxSkinned& vx : ch.mesh.verts) {
        mat4 m;
        for (int k = 0; k < 4; k++) m.c[k] = vec4(0);
        for (int k = 0; k < 4; k++) {
            float w = vx.weights[k] / 255.f;
            if (w <= 0) continue;
            const mat4& s = skin[vx.bones[k]];
            for (int c = 0; c < 4; c++) m.c[c] = m.c[c] + s.c[c] * w;
        }
        g.P.push_back(root + rotate(q, transformPoint(m, vx.pos)));
        g.N.push_back(rotate(q, normalize(transformDir(m, unpackNormalOct(vx.normal)))));
        vec4 cc = unpackRGBA8(vx.color);
        g.A.push_back(matAlbedo(vx.mat, cc.xyz()));
        g.M.push_back((vx.mat & 0xffu) == MAT_HAIR ? (u32)MAT_CLOTH : vx.mat);
    }
    for (u32 i : ch.mesh.indices) g.I.push_back(base + i);
}

// Steps a character through getting in (enter) or out at 60 Hz up to time t: its pose, root (vehicle frame) and the
// door's opening then. Getting in ends with the warp into the seat at the clip's end (as the game does).
struct CarRun {
    Pose pose;
    vec3 root;
    float yaw = 0.f, door = 0.f;
    bool belt = false;
    Animator an;
};
static void runCarClip(const VehicleModel& vm, const CarChar& ch, int seat, bool enter, float t, CarRun& out) {
    const SeatSpec& ss = vm.seats[seat];
    bool leftDoor = ss.door >= 0 ? vm.doors[ss.door].left : ss.exitLeft;
    Animator& an = out.an;
    an.init(&ch.sk, 77u);
    an.setCharacter(ch.d);
    const float dt = 1.f / 60.f;
    CarDoorInfo probe = doorInfo(vm, seat, vec3(0.f), 0.f, true);   // vehicle frame (for the spots)
    vec3 spot;
    float spotYaw;
    if (enter) carEntrySpot(probe, spot, spotYaw);
    else carExitSpot(probe, spot, spotYaw);
    vec3 seatRoot = ss.pos - vec3(0.f, 0.f, 0.5f);
    int clip = enter ? (leftDoor ? CLIP_ENTER_CAR_L : CLIP_ENTER_CAR_R) : (leftDoor ? CLIP_EXIT_CAR_L : CLIP_EXIT_CAR_R);
    CarDoorInfo g = doorInfo(vm, seat, spot, spotYaw, true);
    float len = carClipLength(clip, g);
    float settle = 1.0f;   // getting out: seated (belted) for a moment first
    bool seated = !enter;
    float tt = enter ? 0.f : -settle;
    out.door = 0.f;
    for (int i = 0; tt <= t + 1e-4f; i++, tt += dt) {
        AnimInput in;
        if (enter) {
            if (tt >= len - dt * 0.5f) seated = true;   // the game's warp into the seat
            in.stance = seated ? (ss.driver ? 1 : 2) : 0;
            if (!seated) {
                in.action = clip;
                in.car = g;
            }
        } else {
            in.stance = tt < 0.f ? (ss.driver ? 1 : 2) : 0;
            if (tt >= 0.f) {
                in.action = clip;
                in.car = g;
                in.car.belt = true;
            }
        }
        if (seated && enter) {
            in.action = -1;
        }
        seatInputs(vm, seat, in);
        an.update(in, dt);
        bool inSeat = enter ? seated : tt < 0.f;
        out.root = inSeat ? seatRoot : spot;
        out.yaw = inSeat ? 0.f : spotYaw;
        float dv = an.carDoor();
        if (dv >= 0.f) out.door = dv;
    }
    out.pose = an.pose;
    out.belt = an.seatBelt();
}

// ------------------------------------------------------------------------------------------------
// Clipping check of a run through a door: every skinned vertex of the character, each frame, against the opening
// (roof rail, pillars, sill / floor), the door itself (as a slab at its current angle) and the ground; and how far
// the hand is from the handle it holds. Penetrations in metres (0 = clear).
struct ClipStats {
    float roof = 0, pillar = 0, sill = 0, door = 0, ground = 0;
    float tRoof = -1, tPillar = -1, tSill = -1, tDoor = -1, tGround = -1;
    int bRoof = -1, bPillar = -1, bSill = -1, bDoor = -1, bGround = -1;   // the body part (bone) worst off
    float handOut = 0, handIn = 0, hipErr = 0, beltOn = -1, beltOff = -1;
    float pop = 0, tPop = -1;   // the hips' largest move in one frame (vehicle frame): a jump between the clip and the seat
    float shell = 0, tShell = -1;     // deepest any vertex is inside the car's hard surfaces (moving in / out)
    int bShell = -1, shellFrames = 0; // ... the body part, and the frames with more than 1 cm
    float seatedShell = 0;            // ... while sitting in the seat (the seated pose itself)
    vec3 pShell;
    float cross = 0, tCross = -1;     // deepest any vertex is past a surface it went through (CrossScan)
    int bCross = -1, crossFrames = 0, crossVerts = 0;   // ... its body part; frames with any vertex through; most at once
    vec3 pCross;
    const char* sCross = "";
    u32 mCross = 0;
    float maxOpen = 0;
    int frames = 0;
};
static float polyDist(const std::vector<vec2>& P, vec2 q) {
    float best = 1e9f;
    for (size_t i = 0; i < P.size(); i++) {
        vec2 a = P[i], b = P[(i + 1) % P.size()];
        vec2 d = b - a;
        float u = Clamp(dot(q - a, d) / Max(dot(d, d), 1e-9f), 0.f, 1.f);
        best = Min(best, length(q - (a + d * u)));
    }
    return best;
}
static bool inPoly(const std::vector<vec2>& P, vec2 q) {
    bool in = false;
    for (size_t i = 0, j = P.size() - 1; i < P.size(); j = i++)
        if ((P[i].y > q.y) != (P[j].y > q.y) && q.x < (P[j].x - P[i].x) * (q.y - P[i].y) / (P[j].y - P[i].y) + P[i].x) in = !in;
    return in;
}
// y extent of a polygon at height z, and its top at y
static bool polySpanY(const std::vector<vec2>& P, float z, float& y0, float& y1) {
    y0 = 1e9f;
    y1 = -1e9f;
    for (size_t i = 0; i < P.size(); i++) {
        vec2 a = P[i], b = P[(i + 1) % P.size()];
        if ((a.y - z) * (b.y - z) > 0.f || fabsf(b.y - a.y) < 1e-7f) continue;
        float y = a.x + (b.x - a.x) * (z - a.y) / (b.y - a.y);
        y0 = Min(y0, y);
        y1 = Max(y1, y);
    }
    return y0 <= y1;
}
static float polyTopZ(const std::vector<vec2>& P, float y) {
    float top = -1e9f;
    for (size_t i = 0; i < P.size(); i++) {
        vec2 a = P[i], b = P[(i + 1) % P.size()];
        if ((a.x - y) * (b.x - y) > 0.f || fabsf(b.x - a.x) < 1e-7f) continue;
        top = Max(top, a.y + (b.y - a.y) * (y - a.x) / (b.x - a.x));
    }
    return top;
}
// Hard surfaces of the car (painted shell, jambs, sill, glass, trims; the seats, dash and cabin linings are soft and
// left out) in a uniform grid: how far a point is behind (inside) one of them, within a 5 cm shell thickness.
struct HardGrid {
    struct Tri { vec3 a, b, c, n; u32 mat, color; u8 src; };   // src: 0 body, 1 another door, 2 the door in use
    std::vector<Tri> tris;
    std::vector<std::vector<u32>> cells;
    vec3 mn;
    float cs = 0.05f;
    int nx = 0, ny = 0, nz = 0;
    float band = 0.06f;
    // the door frame: the shell, glass, trims, jambs, sill and pillars, with the cabin's linings (light fabric: the
    // headliner and the pillar trims inside them); the cabin's furniture (seats, dash, floor) is left out
    static bool hard(u32 mat, u32 color) {
        u32 m = mat & 0xffu;
        if (m == MAT_FABRIC) return (color & 0xffu) > 100u;
        return m == MAT_CARPAINT || m == MAT_CAR_GLASS || m == MAT_CAR_WINDOW || m == MAT_CHROME || m == MAT_METAL_BRUSHED || m == MAT_PLASTIC ||
               m == MAT_RUBBER;
    }
    static bool glass(u32 mat) { return (mat & 0xffu) == MAT_CAR_WINDOW || (mat & 0xffu) == MAT_CAR_GLASS; }
    // glassMode: 0 every surface, 1 the opaque ones, 2 the glass alone (thin sheets: CrossScan's)
    void add(const MeshData& m, quat q, vec3 pivot, bool all, u8 src = 0, int glassMode = 1) {
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const VtxStatic& va = m.verts[m.indices[i]];
            if (!all && !hard(va.mat, va.color)) continue;
            if ((glassMode == 1 && glass(va.mat)) || (glassMode == 2 && !glass(va.mat))) continue;
            vec3 a = pivot + rotate(q, va.pos - pivot), b = pivot + rotate(q, m.verts[m.indices[i + 1]].pos - pivot),
                 c = pivot + rotate(q, m.verts[m.indices[i + 2]].pos - pivot);
            vec3 n = cross(b - a, c - a);
            float l = length(n);
            if (l < 1e-10f) continue;
            tris.push_back(Tri{a, b, c, n / l, va.mat & 0xffu, va.color, src});
        }
    }
    void build(vec3 lo, vec3 hi) {
        mn = lo - vec3(band);
        nx = (int)((hi.x - lo.x + 2 * band) / cs) + 1;
        ny = (int)((hi.y - lo.y + 2 * band) / cs) + 1;
        nz = (int)((hi.z - lo.z + 2 * band) / cs) + 1;
        cells.assign((size_t)nx * ny * nz, {});
        for (u32 t = 0; t < (u32)tris.size(); t++) {
            const Tri& T = tris[t];
            vec3 a = vmin(T.a, vmin(T.b, T.c)) - vec3(band), b = vmax(T.a, vmax(T.b, T.c)) + vec3(band);
            int x0 = Max(0, (int)((a.x - mn.x) / cs)), x1 = Min(nx - 1, (int)((b.x - mn.x) / cs));
            int y0 = Max(0, (int)((a.y - mn.y) / cs)), y1 = Min(ny - 1, (int)((b.y - mn.y) / cs));
            int z0 = Max(0, (int)((a.z - mn.z) / cs)), z1 = Min(nz - 1, (int)((b.z - mn.z) / cs));
            for (int z = z0; z <= z1; z++)
                for (int y = y0; y <= y1; y++)
                    for (int x = x0; x <= x1; x++) cells[((size_t)z * ny + y) * nx + x].push_back(t);
        }
    }
    static vec3 closest(vec3 p, vec3 a, vec3 b, vec3 c) {
        vec3 ab = b - a, ac = c - a, ap = p - a;
        float d1 = dot(ab, ap), d2 = dot(ac, ap);
        if (d1 <= 0 && d2 <= 0) return a;
        vec3 bp = p - b;
        float d3 = dot(ab, bp), d4 = dot(ac, bp);
        if (d3 >= 0 && d4 <= d3) return b;
        float vc = d1 * d4 - d3 * d2;
        if (vc <= 0 && d1 >= 0 && d3 <= 0) return a + ab * (d1 / (d1 - d3));
        vec3 cp = p - c;
        float d5 = dot(ab, cp), d6 = dot(ac, cp);
        if (d6 >= 0 && d5 <= d6) return c;
        float vb = d5 * d2 - d1 * d6;
        if (vb <= 0 && d2 >= 0 && d6 <= 0) return a + ac * (d2 / (d2 - d6));
        float va = d3 * d6 - d5 * d4;
        if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
        float den = 1.f / (va + vb + vc);
        return a + ab * (vb * den) + ac * (vc * den);
    }
    // depth behind the nearest hard surface the point is behind (0 = in front of / away from all of them)
    float depth(vec3 p, int* tri = nullptr) const {
        if (tri) *tri = -1;
        if (cells.empty()) return 0.f;
        int x = (int)((p.x - mn.x) / cs), y = (int)((p.y - mn.y) / cs), z = (int)((p.z - mn.z) / cs);
        if (x < 0 || y < 0 || z < 0 || x >= nx || y >= ny || z >= nz) return 0.f;
        float best = 1e9f, sgn = 1.f;
        int bt = -1;
        const std::vector<u32>& cl = cells[((size_t)z * ny + y) * nx + x];
        for (u32 t : cl) {
            const Tri& T = tris[t];
            vec3 q = closest(p, T.a, T.b, T.c);
            float d = length(p - q);
            if (d < best) {
                best = d;
                sgn = dot(p - q, T.n);
                bt = (int)t;
            }
        }
        // behind a face only when straight behind it: nearest to an edge or a corner (an open panel's edge, a seam
        // several faces share), the point is beside it, not in it
        if (best < band && sgn < 0.f) {
            const Tri& T = tris[bt];
            vec3 q = p - T.n * dot(p - T.a, T.n);
            vec3 e0 = T.b - T.a, e1 = T.c - T.a, e2 = q - T.a;
            float d00 = dot(e0, e0), d01 = dot(e0, e1), d11 = dot(e1, e1), d20 = dot(e2, e0), d21 = dot(e2, e1);
            float den = d00 * d11 - d01 * d01;
            if (fabsf(den) > 1e-14f) {
                float v = (d11 * d20 - d01 * d21) / den, w = (d00 * d21 - d01 * d20) / den;
                if (v < -1e-3f || w < -1e-3f || v + w > 1.001f) sgn = 1.f;
            }
        }
        if (best < band && sgn < 0.f) {
            if (tri) *tri = bt;
            return best;
        }
        return 0.f;
    }
};

// Segment p0 -> p1 through triangle T: where along it (0..1), or -1
static float segTri(vec3 p0, vec3 p1, const HardGrid::Tri& T) {
    vec3 d = p1 - p0, e1 = T.b - T.a, e2 = T.c - T.a;
    vec3 h = cross(d, e2);
    float det = dot(e1, h);
    if (fabsf(det) < 1e-16f) return -1.f;
    float inv = 1.f / det;
    vec3 sv = p0 - T.a;
    float u = dot(sv, h) * inv;
    if (u < 0.f || u > 1.f) return -1.f;
    vec3 q = cross(sv, e1);
    float v = dot(d, q) * inv;
    if (v < 0.f || u + v > 1.f) return -1.f;
    float t = dot(e2, q) * inv;
    return t >= 0.f && t <= 1.f ? t : -1.f;
}
// The triangles of g the segment goes through (index, where along it)
static void segCross(const HardGrid& g, vec3 p0, vec3 p1, std::vector<std::pair<u32, float>>& out) {
    out.clear();
    if (g.cells.empty()) return;
    float L = length(p1 - p0);
    int n = (int)(L / (g.band * 0.9f)) + 1;
    int lastCell = -1;
    for (int k = 0; k < n; k++) {
        vec3 p = p0 + (p1 - p0) * ((float)k / n);
        int x = (int)((p.x - g.mn.x) / g.cs), y = (int)((p.y - g.mn.y) / g.cs), z = (int)((p.z - g.mn.z) / g.cs);
        if (x < 0 || y < 0 || z < 0 || x >= g.nx || y >= g.ny || z >= g.nz) continue;
        int c = (z * g.ny + y) * g.nx + x;
        if (c == lastCell) continue;
        lastCell = c;
        for (u32 t : g.cells[(size_t)c]) {
            float u = segTri(p0, p1, g.tris[t]);
            if (u < 0.f) continue;
            bool dup = false;
            for (auto& o : out) dup |= o.first == t;
            if (!dup) out.push_back({t, u});
        }
    }
}
// One frame of a run: the time, the door's opening and every vertex of the character (vehicle frame)
struct RunFrame {
    float t = 0.f, open = 0.f;
    bool sitting = false;
    std::vector<vec3> P;
    // per body part (partOf): the deepest static overlap with an opaque surface / crossing of a glass one, where and
    // against what (src 0 body, 1 another door, 2 the door; material)
    float shell[6] = {}, glass[6] = {};
    vec3 shellP[6], glassP[6];
    int shellSrc[6] = {}, glassSrc[6] = {};
    u32 shellMat[6] = {}, glassMat[6] = {};
};
// Body parts for the event lists: 0 head, 1 trunk, 2 left arm, 3 right arm, 4 left leg, 5 right leg
static int partOf(int b) {
    if (b == B_NECK || b == B_HEAD || (b >= B_JAW && b <= B_BROW_R)) return 0;
    if (b >= B_PELVIS && b <= B_CHEST) return 1;
    if ((b >= B_CLAVICLE_L && b <= B_HAND_L) || b == B_FINGERS_L || b == B_THUMB_L || b == B_FOREARM_ROLL_L || (b >= B_INDEX1_L && b < B_INDEX1_R)) return 2;
    if ((b >= B_CLAVICLE_R && b <= B_HAND_R) || b == B_FINGERS_R || b == B_THUMB_R || b == B_FOREARM_ROLL_R || b >= B_INDEX1_R) return 3;
    if (b >= B_THIGH_L && b <= B_TOE_L) return 4;
    return 5;
}
static const char* kPartName[6] = {"head", "trunk", "L arm", "R arm", "L leg", "R leg"};
// Thin-surface crossings over a run: each vertex's motion between frames against the hard surfaces of the body (and
// the other doors, shut) and the door in use (in its own frame, as it swings), from both sides. A vertex that went
// through a surface stays through it until it comes back through it (or through the same surface next to it). Run
// from the end that is clear of the car (getting in: forwards from standing outside; getting out: backwards from
// standing outside), so the seated pose's own overlaps do not count as crossings.
struct CrossScan {
    struct Thru { u8 grid; u32 tri; };
    std::vector<std::vector<Thru>> thru;   // per vertex
    std::vector<int> frameCount;           // vertices through something, per frame
    std::vector<float> frameDepth;         // the deepest (distance past the surface crossed), per frame
    std::vector<std::vector<u8>> mark;     // per frame (only when asked for): 1 through
    int worstFrame = -1, worstVert = -1, worstGrid = -1;
    u32 worstTri = 0;
    float worst = 0.f;
};
static void crossScan(std::vector<RunFrame>& F, const DoorSpec& D, const HardGrid& body, const HardGrid& door,
                      const std::vector<u8>& held, const std::vector<int>& vbone, bool forward, bool marks, CrossScan& cs) {
    size_t nv = F.empty() ? 0 : F[0].P.size(), nf = F.size();
    cs.thru.assign(nv, {});
    cs.frameCount.assign(nf, 0);
    cs.frameDepth.assign(nf, 0.f);
    if (marks) cs.mark.assign(nf, std::vector<u8>(nv, 0));
    auto local = [&](vec3 p, float open) { return D.hinge + rotate(quatAxisAngle(D.axis, -open * D.maxAngle), p - D.hinge); };
    std::vector<std::pair<u32, float>> hits;
    auto plane = [](const HardGrid::Tri& T, vec3 p) { return dot(p - T.a, T.n); };
    for (size_t step = 0; step < nf; step++) {
        size_t f = forward ? step : nf - 1 - step;
        RunFrame& cur = F[f];
        if (step > 0) {
            const RunFrame& prev = F[forward ? f - 1 : f + 1];
            for (size_t v = 0; v < nv; v++) {
                vec3 a = prev.P[v], b = cur.P[v];
                for (int gi = 0; gi < 2; gi++) {
                    if (gi == 1 && held[v]) continue;   // the hand on the door holds it
                    const HardGrid& g = gi == 0 ? body : door;
                    vec3 a2 = gi == 0 ? a : local(a, prev.open), b2 = gi == 0 ? b : local(b, cur.open);
                    if (gi == 0 && a2.x == b2.x && a2.y == b2.y && a2.z == b2.z) continue;
                    segCross(g, a2, b2, hits);
                    if (hits.empty()) continue;
                    std::sort(hits.begin(), hits.end(), [](const std::pair<u32, float>& x, const std::pair<u32, float>& y) { return x.second < y.second; });
                    vec3 lastX(1e9f);
                    for (auto& h : hits) {
                        const HardGrid::Tri& T = g.tris[h.first];
                        vec3 x = a2 + (b2 - a2) * h.second;
                        if (length(x - lastX) < 0.003f) continue;   // through a shared edge: one crossing
                        lastX = x;
                        std::vector<CrossScan::Thru>& L = cs.thru[v];
                        int found = -1;
                        for (int k = 0; k < (int)L.size() && found < 0; k++) {
                            if (L[k].grid != gi) continue;
                            const HardGrid::Tri& E = g.tris[L[k].tri];
                            if (L[k].tri == h.first || (fabsf(plane(E, x)) < 0.012f && dot(E.n, T.n) > 0.7f && length(x - E.a) < 0.3f)) found = k;
                        }
                        if (found >= 0) L.erase(L.begin() + found);
                        else L.push_back(CrossScan::Thru{(u8)gi, h.first});
                        static const bool dbg = getenv("CARVIEW_XDEBUG") != nullptr;
                        if (dbg && found < 0)
                            printf("    through t %.2f v %zu %s (%.2f %.2f %.2f)->(%.2f %.2f %.2f) tri (%.2f %.2f %.2f) n (%.2f %.2f %.2f) mat %u src %d open %.2f->%.2f\n",
                                   cur.t, v, gi ? "door" : "body", a2.x, a2.y, a2.z, b2.x, b2.y, b2.z, T.a.x, T.a.y, T.a.z, T.n.x, T.n.y, T.n.z, T.mat,
                                   (int)T.src, prev.open, cur.open);
                    }
                }
            }
        }
        for (size_t v = 0; v < nv; v++) {
            std::vector<CrossScan::Thru>& L = cs.thru[v];
            if (L.empty()) continue;
            float dmax = 0.f;
            int wg = -1;
            u32 wt = 0;
            for (size_t k = 0; k < L.size();) {
                CrossScan::Thru& e = L[k];
                const HardGrid& g = e.grid == 0 ? body : door;
                vec3 p = e.grid == 0 ? cur.P[v] : local(cur.P[v], cur.open);
                // how far past the sheet it went through: the nearest of its triangles (followed from frame to
                // frame); none within the grid's band: away from it (round its edge, or out the far side)
                const HardGrid::Tri& E = g.tris[e.tri];
                int x = (int)((p.x - g.mn.x) / g.cs), y = (int)((p.y - g.mn.y) / g.cs), z = (int)((p.z - g.mn.z) / g.cs);
                float best = 1e9f;
                u32 bt = e.tri;
                if (x >= 0 && y >= 0 && z >= 0 && x < g.nx && y < g.ny && z < g.nz)
                    for (u32 t : g.cells[((size_t)z * g.ny + y) * g.nx + x]) {
                        const HardGrid::Tri& T = g.tris[t];
                        if (dot(T.n, E.n) < 0.8f || fabsf(plane(E, T.a)) > 0.03f) continue;
                        float d = length(p - HardGrid::closest(p, T.a, T.b, T.c));
                        if (d < best) {
                            best = d;
                            bt = t;
                        }
                    }
                if (best > g.band) {
                    L.erase(L.begin() + k);
                    continue;
                }
                e.tri = bt;
                if (best >= dmax) {
                    dmax = best;
                    wg = e.grid;
                    wt = e.tri;
                }
                k++;
            }
            if (dmax < 0.002f) continue;
            cs.frameCount[f]++;
            cs.frameDepth[f] = Max(cs.frameDepth[f], dmax);
            if (marks) cs.mark[f][v] = 1;
            int pt = partOf(vbone[v]);
            if (dmax > cur.glass[pt]) {
                const HardGrid::Tri& T = (wg == 0 ? body : door).tris[wt];
                cur.glass[pt] = dmax;
                cur.glassP[pt] = cur.P[v];
                cur.glassSrc[pt] = T.src;
                cur.glassMat[pt] = T.mat;
            }
            if (dmax > cs.worst && !cur.sitting) {
                cs.worst = dmax;
                cs.worstFrame = (int)f;
                cs.worstVert = (int)v;
                cs.worstGrid = wg;
                cs.worstTri = wt;
            }
        }
    }
}

static void checkRun(const VehicleModel& vm, const CarChar& ch, int seat, bool enter, ClipStats& st) {
    const SeatSpec& ss = vm.seats[seat];
    const DoorSpec& D = vm.doors[ss.door];
    const float side = D.left ? -1.f : 1.f;
    const std::vector<vec2>& O = D.outline;
    float zTop = -1e9f, zLow = 1e9f;
    for (const vec2& q : O) {
        zTop = Max(zTop, q.y);
        zLow = Min(zLow, q.y);
    }
    const float beltZ = D.handle.z + 0.085f;
    // the door card's plane (the release handle stands 1.2 cm off it, 9.7 cm in from the belt line) and the rocker's
    // underside (7.5 cm under the door's bottom edge): under it is open air
    const float xRail = fabsf(D.grip.x), xSill = D.sillX, xCard = fabsf(D.handleIn.x) + 0.035f, zRocker = zLow - 0.075f;
    // the hand that holds the door: its palm and finger bones touch the door by design
    const int holdHand = D.left ? B_HAND_L : B_HAND_R, holdPh0 = D.left ? B_INDEX1_L : B_INDEX1_R;
    auto heldPart = [&](int b) { return b == holdHand || (b >= holdPh0 && b < holdPh0 + kHandPhalanges); };
    // the cabin's ceiling: the opening's top at the side, rising to the headliner under the roof's crown inboard
    const float headliner = Min(vm.body.bounds.mx.z - 0.07f, zTop + 0.2f);
    auto xOut = [&](float z) { return Lerp(xSill, xRail, Saturate((z - beltZ) / Max(zTop - beltZ, 0.05f))); };
    CarDoorInfo probe = doorInfo(vm, seat, vec3(0.f), 0.f, true);
    vec3 spot;
    float spotYaw;
    if (enter) carEntrySpot(probe, spot, spotYaw);
    else carExitSpot(probe, spot, spotYaw);
    int clip = enter ? (D.left ? CLIP_ENTER_CAR_L : CLIP_ENTER_CAR_R) : (D.left ? CLIP_EXIT_CAR_L : CLIP_EXIT_CAR_R);
    CarDoorInfo g = doorInfo(vm, seat, spot, spotYaw, true);
    float len = carClipLength(clip, g);
    const float wIn = enter ? Max(len - 2.74f, 0.f) : 0.f;   // (getting in: the steps to the doorway stretch everything after the pull)
    Animator an;
    an.init(&ch.sk, 77u);
    an.setCharacter(ch.d);
    const float dt = 1.f / 60.f;
    const vec3 seatRoot = ss.pos - vec3(0.f, 0.f, 0.5f);
    HardGrid body;
    body.add(vm.body, quat(), vec3(0.f), false);
    for (size_t d = 0; d < vm.doors.size(); d++)
        if ((int)d != ss.door) body.add(vm.doors[d].mesh, quat(), vec3(0.f), true, 1);
    body.build(vm.body.bounds.mn, vm.body.bounds.mx);
    float tt = enter ? 0.f : -1.f;
    bool seated = !enter;
    bool wasBelt = !enter;
    vec3 prevHips(0.f);
    std::vector<RunFrame> frames;
    std::vector<int> vbone(ch.mesh.verts.size());
    std::vector<u8> held(ch.mesh.verts.size());
    for (size_t i = 0; i < ch.mesh.verts.size(); i++) {
        const VtxSkinned& vx = ch.mesh.verts[i];
        int bone = vx.bones[0];
        for (int k = 1; k < 4; k++)
            if (vx.weights[k] > vx.weights[0]) bone = vx.bones[k];
        vbone[i] = bone;
        held[i] = heldPart(bone) ? 1 : 0;
    }
    for (; tt <= len + (enter ? 1.4f : 0.3f); tt += dt) {
        AnimInput in;
        if (enter) {
            if (tt >= len - dt * 0.5f) seated = true;
            in.stance = seated ? (ss.driver ? 1 : 2) : 0;
            if (!seated) {
                in.action = clip;
                in.car = g;
            }
        } else {
            in.stance = tt < 0.f ? (ss.driver ? 1 : 2) : 0;
            if (tt >= 0.f) {
                in.action = clip;
                in.car = g;
            }
        }
        seatInputs(vm, seat, in);
        an.update(in, dt);
        bool inSeat = enter ? seated : tt < 0.f;
        vec3 root = inSeat ? seatRoot : spot;
        float yaw = inSeat ? 0.f : spotYaw;
        quat qr = quatAxisAngle(vec3(0, 0, 1), yaw);
        float open = Max(an.carDoor(), 0.f);
        st.maxOpen = Max(st.maxOpen, open);
        bool belt = an.seatBelt();
        if (belt && !wasBelt && st.beltOn < 0.f) st.beltOn = tt;
        if (!belt && wasBelt && st.beltOff < 0.f) st.beltOff = tt;
        wasBelt = belt;
        if (tt < 0.f) continue;
        st.frames++;
        mat4 ms[B_COUNT], skin[B_COUNT];
        computeMatrices(ch.sk, an.pose, ms, skin);
        {
            vec3 hp = root + rotate(qr, (ms[B_THIGH_L].c[3].xyz() + ms[B_THIGH_R].c[3].xyz()) * 0.5f);
            if (st.frames > 1 && length(hp - prevHips) > st.pop) {
                st.pop = length(hp - prevHips);
                st.tPop = tt;
            }
            if (getenv("CARVIEW_POPTRACE")) {
                vec3 pl = root + rotate(qr, ms[B_PELVIS].c[3].xyz());
                vec3 hd = root + rotate(qr, ms[B_HEAD].c[3].xyz());
                printf("  t %.3f hips (%.3f %.3f %.3f) pelvis (%.3f %.3f %.3f) head (%.3f %.3f %.3f) step %.3f\n", tt, hp.x, hp.y, hp.z, pl.x, pl.y, pl.z, hd.x, hd.y,
                       hd.z, length(hp - prevHips));
            }
            prevHips = hp;
        }
        quat qd = quatAxisAngle(D.axis, -open * D.maxAngle);   // back to the shut door's frame
        HardGrid door;
        door.add(D.mesh, quatAxisAngle(D.axis, open * D.maxAngle), D.hinge, true, 2);
        AABB dbb;
        for (const HardGrid::Tri& T : door.tris) {
            dbb.add(T.a);
            dbb.add(T.b);
            dbb.add(T.c);
        }
        door.build(dbb.mn, dbb.mx);
        frames.emplace_back();
        RunFrame& rf = frames.back();
        rf.t = tt;
        rf.open = open;
        rf.sitting = enter ? tt > 1.9f + wIn : tt < 1.1f;
        rf.P.reserve(ch.mesh.verts.size());
        float frameShell = 0.f;
        const HardGrid::Tri* frameTri = nullptr;
        int frameBone = -1;
        vec3 frameP(0.f);
        for (const VtxSkinned& vx : ch.mesh.verts) {
            mat4 m;
            for (int k = 0; k < 4; k++) m.c[k] = vec4(0);
            for (int k = 0; k < 4; k++) {
                float w = vx.weights[k] / 255.f;
                if (w <= 0) continue;
                const mat4& sk = skin[vx.bones[k]];
                for (int c = 0; c < 4; c++) m.c[c] = m.c[c] + sk.c[c] * w;
            }
            vec3 p = root + rotate(qr, transformPoint(m, vx.pos));
            rf.P.push_back(p);
            int bone = vx.bones[0];
            for (int k = 1; k < 4; k++)
                if (vx.weights[k] > vx.weights[0]) bone = vx.bones[k];
            float ax = p.x * side;   // out from the vehicle's centre on the door's side
            {
                bool heldNow = heldPart(bone) && (enter ? (tt > 0.2f && tt < 1.2f + wIn) || (tt > 2.06f + wIn && tt < 2.52f + wIn) : true);   // (exit: every hold, and the let-go)
                int tb = -1, td = -1;
                float db = heldNow ? 0.f : body.depth(p, &tb), dd = heldNow ? 0.f : door.depth(p, &td);
                float dpt = Max(db, dd);
                const HardGrid::Tri* hitTri = dd > db ? (td >= 0 ? &door.tris[td] : nullptr) : (tb >= 0 ? &body.tris[tb] : nullptr);
                // the seated phases (the seated pose itself) apart from the moving ones
                bool sitting = enter ? tt > 1.9f + wIn : tt < 1.1f;
                if (sitting) st.seatedShell = Max(st.seatedShell, dpt);
                int pt = partOf(bone);
                if (dpt > rf.shell[pt] && hitTri) {
                    rf.shell[pt] = dpt;
                    rf.shellP[pt] = p;
                    rf.shellSrc[pt] = hitTri->src;
                    rf.shellMat[pt] = hitTri->mat;
                }
                if (!sitting) {
                    if (dpt > frameShell) {
                        frameShell = dpt;
                        frameTri = hitTri;
                        frameBone = bone;
                        frameP = p;
                    }
                    if (dpt > st.shell) {
                        st.shell = dpt;
                        st.tShell = tt;
                        st.bShell = bone;
                        st.pShell = p;
                    }
                }
            }
            // ground outside the car
            if (ax > xSill + 0.02f && -p.z > st.ground) {
                st.ground = -p.z;
                st.tGround = tt;
                st.bGround = bone;
            }
            // floor / sill inside the side skin (within the opening's length), above the rocker's underside
            float y0, y1;
            if (ax < xSill - 0.02f && ax > -xSill && p.z > zRocker && p.y > D.yRear - 0.3f && p.y < D.yFront + 0.1f && D.sillZ - p.z > st.sill) {
                st.sill = D.sillZ - p.z;
                st.tSill = tt;
                st.bSill = bone;
            }
            // roof over the opening and the cabin (inside the rail line; above the roof's top is outside)
            float zr = polyTopZ(O, Clamp(p.y, D.yRear + 0.02f, D.yFront - 0.02f)) - 0.03f;
            zr += (Max(headliner, zr) - zr) * Saturate((xRail - ax) / 0.45f);
            if (ax < xRail + 0.01f && ax > -xRail && p.y > D.yRear - 0.1f && p.y < D.yFront && p.z < zr + 0.09f && p.z - zr > st.roof) {
                st.roof = p.z - zr;
                st.tRoof = tt;
                st.bRoof = bone;
            }
            // pillars: in the side wall's band, beside the opening
            float xb = xOut(p.z);
            if (ax > xb - 0.12f && ax < xb + 0.005f && p.z > zLow && p.z < zTop && polySpanY(O, p.z, y0, y1)) {
                float pen = p.y > y1 ? p.y - y1 : (p.y < y0 ? y0 - p.y : 0.f);
                if (pen > 0.f && pen < 0.4f && pen > st.pillar) {
                    st.pillar = pen;
                    st.tPillar = tt;
                    st.bPillar = bone;
                }
            }
            // the door as a slab between its card and its skin, where it is now (the hand holding it excepted)
            vec3 pc = D.hinge + rotate(qd, p - D.hinge);
            float acx = pc.x * side;
            vec2 q(pc.y, pc.z);
            float xi = pc.z < beltZ ? xCard : xOut(pc.z) - 0.035f, xo = xOut(pc.z);
            bool held = heldPart(bone);
            if (!held && acx > xi && acx < xo && inPoly(O, q)) {
                float pen = Min(Min(acx - xi, xo - acx), polyDist(O, q));
                if (pen > st.door) {
                    st.door = pen;
                    st.tDoor = tt;
                    st.bDoor = bone;
                    if (getenv("CARVIEW_DEBUG"))
                        printf("  door hit t %.2f bone %d p (%.3f %.3f %.3f) shut (%.3f %.3f %.3f) open %.2f xi %.3f xo %.3f\n", tt, bone, p.x, p.y, p.z, pc.x,
                               pc.y, pc.z, open, xi, xo);
                }
            }
        }
        if (getenv("CARVIEW_HEADDBG")) {
            // the head's real top against the key builder's head model and ceiling (as carDoorPose authors them)
            using namespace Anim::detail;
            float lo = 0.f, hi = 9.f;
            sscanf(getenv("CARVIEW_HEADDBG"), "%f,%f", &lo, &hi);
            if (tt >= lo && tt <= hi) {
                vec3 topP(0.f, 0.f, -1.f);
                for (size_t i = 0; i < rf.P.size(); i++)
                    if (partOf(vbone[i]) == 0 && rf.P[i].z > topP.z) topP = rf.P[i];
                const ClipLib& L = clipLib();
                const AuthorCtx& A = L.ctx[skeletonStyle(ch.sk) > 0.5f ? 1 : 0];
                float ls = Max(skelLegLen(ch.sk) / Max(skelLegLen(A.sk), 1e-3f), 0.3f);
                CarDoorInfo gg = D.left ? g : mirrorDoor(g);
                CarGeo cg = carGeo(scaleDoor(gg, 1.f / ls), &gg);
                std::vector<Key> KK;
                if (enter) carEntryKeys(A, cg, KK);
                else carExitKeys(A, cg, KK);
                Rig rr;
                sampleKeys(A, KK, tt, false, KK.back().t, rr);
                Pose ps;
                rigToPose(A, rr, ps);
                quat q;
                vec3 hp;
                boneModel(A.sk, ps, B_HEAD, q, hp);
                const float rh = 0.105f * A.D.s, cz = A.D.H - A.headP.z - rh;
                vec3 crown = hp + rotate(q, vec3(0.f, 0.02f * A.D.s, cz)) + vec3(0, 0, rh);
                float ceilA = cg.ceilAt(crown);
                vec3 crownV = root + rotate(qr, crown * ls);
                printf("   t %.2f real top (%.3f %.3f %.3f)  model crown (%.3f %.3f %.3f)  ceiling %.3f (%.3f rail line d)\n", tt, topP.x, topP.y, topP.z,
                       crownV.x, crownV.y, crownV.z, ceilA * ls, (dot(crown, cg.N) - cg.dRail) * ls);
            }
        }
        if (frameShell > 0.02f) st.shellFrames++;
        if (getenv("CARVIEW_SURF") && frameShell > 0.01f && frameTri)
            printf("   t %.2f %.3f b%d (%.2f %.2f %.2f) %s mat %u col %06x n (%.2f %.2f %.2f)\n", tt, frameShell, frameBone, frameP.x, frameP.y, frameP.z,
                   frameTri->src == 0 ? "body" : frameTri->src == 1 ? "other door" : "door", frameTri->mat, frameTri->color & 0xffffffu, frameTri->n.x,
                   frameTri->n.y, frameTri->n.z);
        // the hand on the handle (outer while pulling it open, inner while pulling it shut / pushing it open)
        vec3 gp, ga, gpalm;
        handGrip(ch.sk, ms, !D.left, gp, ga, gpalm);
        vec3 hand = root + rotate(qr, gp);
        quat qo = quatAxisAngle(D.axis, open * D.maxAngle);
        auto onDoor = [&](vec3 p) { return D.hinge + rotate(qo, p - D.hinge); };
        if (enter) {
            if (tt > 0.32f && tt < 0.78f) st.handOut = Max(st.handOut, length(hand - onDoor(D.handle - vec3(0, 0, 0.006f))));
            if (getenv("CARVIEW_REACHOUT") && tt > 0.2f && tt < 0.9f) {
                int up = D.left ? B_UPPERARM_L : B_UPPERARM_R;
                vec3 sh = root + rotate(qr, ms[up].c[3].xyz());
                vec3 tg = onDoor(D.handle - vec3(0, 0, 0.006f));
                float arm = ch.sk.boneLength[up] + ch.sk.boneLength[up + 1];
                printf("   t %.2f open %.2f hand-handle %.3f shoulder-handle %.3f arm %.3f shoulder (%.2f %.2f %.2f)\n", tt, open, length(hand - tg),
                       length(sh - tg), arm, sh.x, sh.y, sh.z);
            }
            if (tt > 2.18f + wIn && tt < 2.44f + wIn) st.handIn = Max(st.handIn, length(hand - onDoor(D.handleIn + vec3(0, 0, 0.004f))));
            if (getenv("CARVIEW_REACH") && tt > 2.0f && tt < 2.5f) {
                int up = D.left ? B_UPPERARM_L : B_UPPERARM_R;
                vec3 sh = root + rotate(qr, ms[up].c[3].xyz());
                vec3 tg = onDoor(D.handleIn + vec3(0, 0, 0.004f));
                float arm = ch.sk.boneLength[up] + ch.sk.boneLength[up + 1];
                printf("   t %.2f open %.2f hand-target %.3f shoulder-target %.3f arm %.3f\n", tt, open, length(hand - tg), length(sh - tg), arm);
            }
        } else {
            float t0 = 0.48f;
            if (tt > t0 + 0.24f && tt < t0 + 0.44f) st.handIn = Max(st.handIn, length(hand - onDoor(D.handleIn + vec3(0, 0, 0.004f))));
        }
    }
    // through-surface crossings (getting in: forwards from outside; getting out: backwards from outside)
    {
        HardGrid dg, bg;
        dg.add(D.mesh, quat(), vec3(0.f), true, 2, 0);
        dg.build(D.mesh.bounds.mn, D.mesh.bounds.mx);
        bg.add(vm.body, quat(), vec3(0.f), false, 0, 0);
        for (size_t d = 0; d < vm.doors.size(); d++)
            if ((int)d != ss.door) bg.add(vm.doors[d].mesh, quat(), vec3(0.f), true, 1, 0);
        bg.build(vm.body.bounds.mn, vm.body.bounds.mx);
        CrossScan cs;
        crossScan(frames, D, bg, dg, held, vbone, enter, false, cs);
        for (size_t f = 0; f < frames.size(); f++) {
            if (frames[f].sitting) continue;   // (the seated pose's own: apart)
            if (cs.frameCount[f] > 0 && cs.frameDepth[f] > 0.005f) st.crossFrames++;
            st.crossVerts = Max(st.crossVerts, cs.frameCount[f]);
            if (getenv("CARVIEW_SURF") && cs.frameCount[f] > 0)
                printf("   cross t %.2f: %d vertices, %.3f deep\n", frames[f].t, cs.frameCount[f], cs.frameDepth[f]);
        }
        if (getenv("CARVIEW_EVENTS")) {
            // runs of frames with a body part into an opaque surface (over 2 cm) or through glass (over 5 mm)
            for (int kind = 0; kind < 2; kind++)
                for (int pt = 0; pt < 6; pt++) {
                    size_t f = 0;
                    while (f < frames.size()) {
                        auto val = [&](size_t i) { return kind == 0 ? frames[i].shell[pt] : frames[i].glass[pt]; };
                        float thr = kind == 0 ? 0.02f : 0.005f;
                        if (val(f) <= thr) {
                            f++;
                            continue;
                        }
                        size_t a = f, w = f;
                        while (f < frames.size() && (val(f) > thr || (f + 1 < frames.size() && val(f + 1) > thr))) {
                            if (val(f) > val(w)) w = f;
                            f++;
                        }
                        const RunFrame& W = frames[w];
                        vec3 P = kind == 0 ? W.shellP[pt] : W.glassP[pt];
                        int src = kind == 0 ? W.shellSrc[pt] : W.glassSrc[pt];
                        u32 mat = kind == 0 ? W.shellMat[pt] : W.glassMat[pt];
                        printf("    %s %-5s %.2f-%.2f%s max %.3f@%.2f (%.2f %.2f %.2f) %s mat %u\n", kind ? "CROSS" : "shell", kPartName[pt],
                               frames[a].t, frames[f - 1].t, frames[a].sitting && frames[f - 1].sitting ? " (seated)" : "", val(w), W.t, P.x, P.y, P.z,
                               src == 0 ? "body" : src == 1 ? "other door" : "door", mat);
                    }
                }
        }
        if (cs.worstFrame >= 0) {
            st.cross = cs.worst;
            st.tCross = frames[cs.worstFrame].t;
            st.bCross = vbone[cs.worstVert];
            st.pCross = frames[cs.worstFrame].P[cs.worstVert];
            const HardGrid::Tri& T = (cs.worstGrid == 0 ? bg : dg).tris[cs.worstTri];
            st.sCross = T.src == 0 ? "body" : T.src == 1 ? "other door" : "door";
            st.mCross = T.mat;
        }
    }
    // seated at the end of getting in: the hips on the seat
    if (enter) {
        mat4 ms[B_COUNT];
        computeMatrices(ch.sk, an.pose, ms, nullptr);
        vec3 hips = (ms[B_THIGH_L].c[3].xyz() + ms[B_THIGH_R].c[3].xyz()) * 0.5f + seatRoot;
        st.hipErr = length(hips - ss.pos);
    }
}

int main(int argc, char** argv) {
    const char* out = argc > 1 ? argv[1] : "/tmp/carview.ppm";
    int model = 3, W = 1000, H = 640, ss = 2, door = -1;
    float open = 0.f, yaw = -1e9f, pitch = 12.f, dist = -1.f, fov = 32.f;
    const char* view = "front34";
    bool info = false, list = false, haveTarget = false;
    int seat = 0, charSeed = -1;
    bool check = false;
    float enterT = -1.f, exitT = -1.f;
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
        } else if (a == "--seat") seat = atoi(nx());
        else if (a == "--char") charSeed = atoi(nx());
        else if (a == "--enter") enterT = (float)atof(nx());
        else if (a == "--exit") exitT = (float)atof(nx());
        else if (a == "--info") info = true;
        else if (a == "--check") check = true;
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
        for (size_t d = 0; d < vm.doors.size(); d += 2) {
            printf(" door %zu outline:", d);
            for (const vec2& q : vm.doors[d].outline) printf(" (%.2f %.2f)", q.x, q.y);
            printf("\n");
        }
        for (size_t s = 0; s < vm.seats.size(); s++)
            printf(" seat %zu (%.3f %.3f %.3f) door %d\n", s, vm.seats[s].pos.x, vm.seats[s].pos.y, vm.seats[s].pos.z, vm.seats[s].door);
    }
    if (getenv("CARVIEW_HEADTOP")) {
        // the check's characters standing: how far their head's top (hair included) is over the height they are given
        const float heights[5] = {1.58f, 1.68f, 1.76f, 1.84f, 1.94f};
        for (int hi = 0; hi < 5; hi++) {
            for (u32 seed : {1000u + (u32)hi * 31u, 2000u + (u32)hi * 7u, 3000u + (u32)hi * 13u}) {
                CarChar ch;
                ch.d = randomCharacter(seed, 0);
                ch.d.height = heights[hi];
                ch.d.hat = -1;
                buildSkeleton(ch.d, ch.sk);
                buildCharacterMesh(ch.d, ch.sk, ch.mesh);
                float top = 0.f, topSkin = 0.f;
                for (const VtxSkinned& vx : ch.mesh.verts) {
                    top = Max(top, vx.pos.z);
                    if ((vx.mat & 0xffu) != MAT_HAIR) topSkin = Max(topSkin, vx.pos.z);
                }
                auto trunk = [](const Skeleton& k) {
                    return length(k.bindLocalPos[B_SPINE1]) + length(k.bindLocalPos[B_SPINE2]) + length(k.bindLocalPos[B_CHEST]) +
                           length(k.bindLocalPos[B_NECK]) + length(k.bindLocalPos[B_HEAD]);
                };
                auto leg = [](const Skeleton& k) { return length(k.bindLocalPos[B_CALF_L]) + length(k.bindLocalPos[B_FOOT_L]); };
                CharacterDesc rd = randomCharacter(1u, 0);
                rd.gender = ch.d.gender;
                printf("h %.2f seed %u: top %.3f (+%.3f), without hair %.3f (+%.3f) hair style %d  trunk %.3f leg %.3f trunk/leg %.3f\n", heights[hi],
                       seed, top, top - heights[hi], topSkin, topSkin - heights[hi], ch.d.hairStyle, trunk(ch.sk), leg(ch.sk), trunk(ch.sk) / leg(ch.sk));
            }
        }
        return 0;
    }
    if (const char* pr = getenv("CARVIEW_DEPTHPROBE")) {
        // the static metric at a point (body + shut doors): its depth and the triangles nearest to it
        vec3 q(0.f);
        sscanf(pr, "%f,%f,%f", &q.x, &q.y, &q.z);
        HardGrid hg;
        hg.add(vm.body, quat(), vec3(0.f), false);
        for (const DoorSpec& Dd : vm.doors) hg.add(Dd.mesh, quat(), vec3(0.f), true, 1);
        hg.build(vm.body.bounds.mn, vm.body.bounds.mx);
        printf("depth %.4f\n", hg.depth(q));
        std::vector<std::pair<float, u32>> near;
        for (u32 t = 0; t < (u32)hg.tris.size(); t++) {
            const HardGrid::Tri& T = hg.tris[t];
            float d = length(q - HardGrid::closest(q, T.a, T.b, T.c));
            if (d < 0.08f) near.push_back({d, t});
        }
        std::sort(near.begin(), near.end());
        for (size_t i = 0; i < near.size() && i < 12; i++) {
            const HardGrid::Tri& T = hg.tris[near[i].second];
            vec3 c = HardGrid::closest(q, T.a, T.b, T.c);
            printf("  d %.4f sign %+.4f src %d mat %u col %06x n (%.2f %.2f %.2f) closest (%.3f %.3f %.3f) a (%.3f %.3f %.3f)\n", near[i].first,
                   dot(q - c, T.n), (int)T.src, T.mat, T.color & 0xffffffu, T.n.x, T.n.y, T.n.z, c.x, c.y, c.z, T.a.x, T.a.y, T.a.z);
        }
        return 0;
    }
    if (const char* pr = getenv("CARVIEW_PROBE")) {
        // the surfaces a vertical line at (x, y) passes through (body and shut doors): height, material, normal
        float px = 0.f, py = 0.f;
        sscanf(pr, "%f,%f", &px, &py);
        std::vector<std::pair<float, std::string>> hits;
        auto scan = [&](const MeshData& m, const char* what) {
            for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
                const VtxStatic &va = m.verts[m.indices[i]], &vb = m.verts[m.indices[i + 1]], &vc = m.verts[m.indices[i + 2]];
                HardGrid::Tri T{va.pos, vb.pos, vc.pos, normalize(cross(vb.pos - va.pos, vc.pos - va.pos)), va.mat & 0xffu, va.color, 0};
                float u = segTri(vec3(px, py, -1.f), vec3(px, py, 3.f), T);
                if (u < 0.f) continue;
                char buf[160];
                snprintf(buf, sizeof buf, "%s mat %u col %06x n (%.2f %.2f %.2f)", what, T.mat, T.color & 0xffffffu, T.n.x, T.n.y, T.n.z);
                hits.push_back({-1.f + 4.f * u, buf});
            }
        };
        scan(vm.body, "body");
        for (const DoorSpec& Dd : vm.doors) scan(Dd.mesh, "door");
        std::sort(hits.begin(), hits.end());
        for (auto& h : hits) printf("  z %.3f %s\n", h.first, h.second.c_str());
        return 0;
    }
    if (getenv("CARVIEW_SEATED")) {
        // the seated pose alone (the drive / passenger stance), every seat, three heights: the door frame's surfaces
        // and the cabin's linings it reaches into
        const float heights[5] = {1.58f, 1.68f, 1.76f, 1.84f, 1.94f};
        for (size_t si = 0; si < vm.seats.size(); si++)
            for (int hi = 0; hi < 5; hi++) {
                CarChar ch;
                ch.d = randomCharacter(1000u + (u32)hi * 31u, 0);
                ch.d.height = heights[hi];
                ch.d.hat = -1;
                if (ch.d.bottom == Anim::detail::BOT_SKIRT && !getenv("CARVIEW_SKIRT")) ch.d.bottom = Anim::detail::BOT_JEANS;   // (cloth: skirts flare through)
                buildSkeleton(ch.d, ch.sk);
                buildCharacterMesh(ch.d, ch.sk, ch.mesh);
                Animator an;
                an.init(&ch.sk, 77u);
                an.setCharacter(ch.d);
                for (int f = 0; f < 60; f++) {
                    AnimInput in;
                    in.stance = vm.seats[si].driver ? 1 : 2;
                    seatInputs(vm, (int)si, in);
                    an.update(in, 1.f / 60.f);
                }
                HardGrid body;
                body.add(vm.body, quat(), vec3(0.f), false);
                for (const DoorSpec& Dd : vm.doors) body.add(Dd.mesh, quat(), vec3(0.f), true);
                body.build(vm.body.bounds.mn, vm.body.bounds.mx);
                mat4 ms[B_COUNT], skin[B_COUNT];
                computeMatrices(ch.sk, an.pose, ms, skin);
                vec3 root = vm.seats[si].pos - vec3(0, 0, 0.5f);
                float worst[B_COUNT] = {};
                vec3 wp[B_COUNT];
                for (const VtxSkinned& vx : ch.mesh.verts) {
                    mat4 m;
                    for (int k = 0; k < 4; k++) m.c[k] = vec4(0);
                    for (int k = 0; k < 4; k++) {
                        float w = vx.weights[k] / 255.f;
                        if (w <= 0) continue;
                        for (int c = 0; c < 4; c++) m.c[c] = m.c[c] + skin[vx.bones[k]].c[c] * w;
                    }
                    vec3 p = root + transformPoint(m, vx.pos);
                    int bone = vx.bones[0];
                    for (int k = 1; k < 4; k++)
                        if (vx.weights[k] > vx.weights[0]) bone = vx.bones[k];
                    float d = body.depth(p);
                    if (d > worst[bone]) {
                        worst[bone] = d;
                        wp[bone] = p;
                    }
                }
                vec3 head = root + ms[B_HEAD].c[3].xyz();
                float topZ = 0.f;
                for (const VtxSkinned& vx : ch.mesh.verts) {
                    int b = vx.bones[0];
                    for (int k = 1; k < 4; k++)
                        if (vx.weights[k] > vx.weights[0]) b = vx.bones[k];
                    if (partOf(b) != 0) continue;
                    mat4 m;
                    for (int k = 0; k < 4; k++) m.c[k] = vec4(0);
                    for (int k = 0; k < 4; k++) {
                        float w = vx.weights[k] / 255.f;
                        if (w <= 0) continue;
                        for (int c = 0; c < 4; c++) m.c[c] = m.c[c] + skin[vx.bones[k]].c[c] * w;
                    }
                    topZ = Max(topZ, (root + transformPoint(m, vx.pos)).z);
                }
                float hz = vm.seats[si].headZ;
                if (getenv("CARVIEW_SEATED_MARGIN")) {
                    if (hi == 0) printf("%-16s seat %zu:", vm.name.c_str(), si);
                    printf(" %+.3f", hz - topZ);
                    if (hi == 4) printf("\n");
                    continue;
                }
                printf("%-12s seat %zu h %.2f: head top %.3f headliner %.3f (%+.3f)  head joint (%.2f %.2f %.2f)", vm.name.c_str(), si, heights[hi], topZ, hz,
                       hz - topZ, head.x, head.y, head.z);
                if (getenv("CARVIEW_SEATED_SHORT")) {
                    printf("\n");
                    continue;
                }
                if (getenv("CARVIEW_SEATED_MARGIN")) continue;
                for (int b = 0; b < B_COUNT; b++)
                    if (worst[b] > 0.01f) printf("  b%d %.3f@(%.2f %.2f %.2f)", b, worst[b], wp[b].x, wp[b].y, wp[b].z);
                printf("\n");
            }
        return 0;
    }
    if (check) {
        // every seat with a door, a short, a medium and a tall body
        std::vector<float> heights = {1.58f, 1.68f, 1.76f, 1.84f, 1.94f};
        if (const char* hs = getenv("CARVIEW_HEIGHTS")) {
            heights.clear();
            for (const char* q = hs; *q;) {
                heights.push_back((float)atof(q));
                while (*q && *q != ',') q++;
                if (*q) q++;
            }
        }
        const char* fSeat = getenv("CARVIEW_SEAT");
        const char* fH = getenv("CARVIEW_H");
        const char* fDir = getenv("CARVIEW_DIR");
        for (size_t si = 0; si < vm.seats.size(); si++) {
            if (vm.seats[si].door < 0) continue;
            if (fSeat && atoi(fSeat) != (int)si) continue;
            for (int hi = 0; hi < (int)heights.size(); hi++) {
                if (fH && atoi(fH) != hi) continue;
                CarChar ch;
                ch.d = randomCharacter((u32)(charSeed >= 0 ? charSeed : 1000) + (u32)hi * 31u, 0);
                ch.d.height = heights[hi];
                ch.d.hat = -1;
                if (ch.d.bottom == Anim::detail::BOT_SKIRT && !getenv("CARVIEW_SKIRT")) ch.d.bottom = Anim::detail::BOT_JEANS;   // (cloth: skirts flare through)
                buildSkeleton(ch.d, ch.sk);
                buildCharacterMesh(ch.d, ch.sk, ch.mesh);
                for (int e = 1; e >= 0; e--) {
                    if (fDir && (strcmp(fDir, "in") == 0) != (e == 1)) continue;
                    ClipStats st;
                    double c0 = TimeSeconds();
                    checkRun(vm, ch, (int)si, e == 1, st);
                    double c1 = TimeSeconds();
                    printf("%-12s seat %zu h %.2f %s: roof %.3f@%.2f(b%d) pillar %.3f@%.2f(b%d) sill %.3f@%.2f(b%d) door %.3f@%.2f(b%d) ground %.3f(b%d)  hand out %.3f in %.3f",
                           vm.name.c_str(), si, heights[hi], e ? "in " : "out", st.roof, st.tRoof, st.bRoof, st.pillar, st.tPillar, st.bPillar, st.sill,
                           st.tSill, st.bSill, st.door, st.tDoor, st.bDoor, st.ground, st.bGround, st.handOut, st.handIn);
                    printf("  SHELL %.3f@%.2f(b%d at %.2f %.2f %.2f) %d/%d frames, seated %.3f", st.shell, st.tShell, st.bShell, st.pShell.x, st.pShell.y,
                           st.pShell.z, st.shellFrames, st.frames, st.seatedShell);
                    printf("  CROSS %.3f@%.2f(b%d at %.2f %.2f %.2f %s mat %u) %d frames, %d verts", st.cross, st.tCross, st.bCross, st.pCross.x,
                           st.pCross.y, st.pCross.z, st.sCross, st.mCross, st.crossFrames, st.crossVerts);
                    printf(" pop %.3f@%.2f", st.pop, st.tPop);
                    if (e) printf(" hips %.3f belt on %.2f", st.hipErr, st.beltOn);
                    else printf(" belt off %.2f open %.2f", st.beltOff, st.maxOpen);
                    printf("  (%.0f ms)\n", (c1 - c0) * 1e3);
                }
            }
        }
        return 0;
    }
    Geo g;
    if (!getenv("CARVIEW_NOBODY")) addMesh(g, vm.body, quat(), vec3(0.f), vec3(0.f));   // (CARVIEW_NOBODY / _NODOORS: one or the other alone)
    // a character getting in / out through the seat's door (the door swung by its animator)
    int runDoor = -1;
    float runOpen = 0.f;
    if (charSeed >= 0 && (enterT >= 0.f || exitT >= 0.f) && seat < (int)vm.seats.size()) {
        static CarChar ch;
        ch.d = randomCharacter((u32)charSeed, 0);
        if (const char* hv = getenv("CARVIEW_HEIGHT")) ch.d.height = (float)atof(hv);
        if (const char* gv = getenv("CARVIEW_GENDER")) ch.d.gender = atoi(gv) ? FEMALE : MALE;
        ch.d.hat = -1;
        if (ch.d.bottom == Anim::detail::BOT_SKIRT && !getenv("CARVIEW_SKIRT")) ch.d.bottom = Anim::detail::BOT_JEANS;
        buildSkeleton(ch.d, ch.sk);
        buildCharacterMesh(ch.d, ch.sk, ch.mesh);
        static CarRun run;
        runCarClip(vm, ch, seat, enterT >= 0.f, enterT >= 0.f ? enterT : exitT, run);
        size_t vBase = g.P.size();
        addChar(g, ch, run.pose, run.root, run.yaw);
        runDoor = vm.seats[seat].door;
        runOpen = run.door;
        if (getenv("CARVIEW_HITS")) {
            // the character's vertices inside the car's hard surfaces (as --check counts them): magenta over 2 cm,
            // yellow over 5 mm
            HardGrid hg;
            hg.add(vm.body, quat(), vec3(0.f), false);
            for (size_t d = 0; d < vm.doors.size(); d++) {
                const DoorSpec& Dd = vm.doors[d];
                float a = (int)d == runDoor ? runOpen * Dd.maxAngle : 0.f;
                hg.add(Dd.mesh, quatAxisAngle(Dd.axis, a), Dd.hinge, true, (int)d == runDoor ? 2 : 1);
            }
            hg.build(vm.body.bounds.mn - vec3(1.5f, 1.5f, 0.f), vm.body.bounds.mx + vec3(1.5f, 1.5f, 0.f));
            int n2 = 0;
            for (size_t i = vBase; i < g.P.size(); i++) {
                float dp = hg.depth(g.P[i]);
                if (dp > 0.02f) {
                    g.A[i] = vec3(0.9f, 0.f, 0.9f);
                    n2++;
                } else if (dp > 0.005f) g.A[i] = vec3(0.9f, 0.8f, 0.f);
            }
            printf("  %d vertices over 2 cm\n", n2);
        }
        printf("t %.2f: door %.2f belt %d root (%.2f %.2f) yaw %.0f\n", enterT >= 0.f ? enterT : exitT, run.door, run.belt ? 1 : 0, run.root.x,
               run.root.y, run.yaw * 57.29578f);
    }
    for (size_t d = 0; d < vm.doors.size(); d++) {
        const DoorSpec& D = vm.doors[d];
        float a = (door < 0 || door == (int)d) ? open * D.maxAngle : 0.f;
        if (runDoor >= 0) a = (int)d == runDoor ? runOpen * D.maxAngle : 0.f;
        if (getenv("CARVIEW_HIDEDOOR") && (int)d == runDoor) continue;   // (a cut-away: the seat's door left out)
        if (getenv("CARVIEW_NODOORS")) continue;
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
