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
    g.driver = ss.driver;
    g.belt = belt;
    return g;
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
    float shell = 0, tShell = -1;     // deepest any vertex is inside the car's hard surfaces (moving in / out)
    int bShell = -1, shellFrames = 0; // ... the body part, and the frames with more than 1 cm
    float seatedShell = 0;            // ... while sitting in the seat (the seated pose itself)
    vec3 pShell;
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
    void add(const MeshData& m, quat q, vec3 pivot, bool all, u8 src = 0) {
        for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            const VtxStatic& va = m.verts[m.indices[i]];
            if (!all && !hard(va.mat, va.color)) continue;
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
        for (u32 t : cells[((size_t)z * ny + y) * nx + x]) {
            const Tri& T = tris[t];
            vec3 q = closest(p, T.a, T.b, T.c);
            float d = length(p - q);
            if (d < best) {
                best = d;
                sgn = dot(p - q, T.n);
                bt = (int)t;
            }
        }
        if (best < band && sgn < 0.f) {
            if (tri) *tri = bt;
            return best;
        }
        return 0.f;
    }
};

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
            int bone = vx.bones[0];
            for (int k = 1; k < 4; k++)
                if (vx.weights[k] > vx.weights[0]) bone = vx.bones[k];
            float ax = p.x * side;   // out from the vehicle's centre on the door's side
            {
                bool heldNow = heldPart(bone) && (enter ? (tt > 0.28f && tt < 0.9f) || (tt > 2.06f && tt < 2.52f) : true);
                int tb = -1, td = -1;
                float db = body.depth(p, &tb), dd = heldNow ? 0.f : door.depth(p, &td);
                float dpt = Max(db, dd);
                const HardGrid::Tri* hitTri = dd > db ? (td >= 0 ? &door.tris[td] : nullptr) : (tb >= 0 ? &body.tris[tb] : nullptr);
                // the seated phases (the seated pose itself) apart from the moving ones
                bool sitting = enter ? tt > 1.92f : tt < 1.1f;
                if (sitting) st.seatedShell = Max(st.seatedShell, dpt);
                else {
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
            if (tt > 2.18f && tt < 2.44f) st.handIn = Max(st.handIn, length(hand - onDoor(D.handleIn + vec3(0, 0, 0.004f))));
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
    if (getenv("CARVIEW_SEATED")) {
        // the seated pose alone (the drive / passenger stance), every seat, three heights: the door frame's surfaces
        // and the cabin's linings it reaches into
        const float heights[3] = {1.58f, 1.76f, 1.94f};
        for (size_t si = 0; si < vm.seats.size(); si++)
            for (int hi = 0; hi < 3; hi++) {
                CarChar ch;
                ch.d = randomCharacter(1000u + (u32)hi * 31u, 0);
                ch.d.height = heights[hi];
                ch.d.hat = -1;
                buildSkeleton(ch.d, ch.sk);
                buildCharacterMesh(ch.d, ch.sk, ch.mesh);
                Animator an;
                an.init(&ch.sk, 77u);
                an.setCharacter(ch.d);
                for (int f = 0; f < 60; f++) {
                    AnimInput in;
                    in.stance = vm.seats[si].driver ? 1 : 2;
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
                printf("%-12s seat %zu h %.2f: head joint (%.2f %.2f %.2f)", vm.name.c_str(), si, heights[hi], head.x, head.y, head.z);
                for (int b = 0; b < B_COUNT; b++)
                    if (worst[b] > 0.01f) printf("  b%d %.3f@(%.2f %.2f %.2f)", b, worst[b], wp[b].x, wp[b].y, wp[b].z);
                printf("\n");
            }
        return 0;
    }
    if (check) {
        // every seat with a door, a short, a medium and a tall body
        const float heights[3] = {1.58f, 1.76f, 1.94f};
        const char* fSeat = getenv("CARVIEW_SEAT");
        const char* fH = getenv("CARVIEW_H");
        const char* fDir = getenv("CARVIEW_DIR");
        for (size_t si = 0; si < vm.seats.size(); si++) {
            if (vm.seats[si].door < 0) continue;
            if (fSeat && atoi(fSeat) != (int)si) continue;
            for (int hi = 0; hi < 3; hi++) {
                if (fH && atoi(fH) != hi) continue;
                CarChar ch;
                ch.d = randomCharacter((u32)(charSeed >= 0 ? charSeed : 1000) + (u32)hi * 31u, 0);
                ch.d.height = heights[hi];
                ch.d.hat = -1;
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
                    if (e) printf(" hips %.3f belt on %.2f", st.hipErr, st.beltOn);
                    else printf(" belt off %.2f open %.2f", st.beltOff, st.maxOpen);
                    printf("  (%.0f ms)\n", (c1 - c0) * 1e3);
                }
            }
        }
        return 0;
    }
    Geo g;
    addMesh(g, vm.body, quat(), vec3(0.f), vec3(0.f));
    // a character getting in / out through the seat's door (the door swung by its animator)
    int runDoor = -1;
    float runOpen = 0.f;
    if (charSeed >= 0 && (enterT >= 0.f || exitT >= 0.f) && seat < (int)vm.seats.size()) {
        static CarChar ch;
        ch.d = randomCharacter((u32)charSeed, 0);
        if (const char* hv = getenv("CARVIEW_HEIGHT")) ch.d.height = (float)atof(hv);
        if (const char* gv = getenv("CARVIEW_GENDER")) ch.d.gender = atoi(gv) ? FEMALE : MALE;
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
