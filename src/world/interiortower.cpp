// Solaris One interiors. The lobby fills the double-height ground floor behind the podium's south curtain wall: a
// grand sliding entrance under the canopy with a glass door on each side, white marble with the Solaris sun inlay,
// a vertical garden and a media wall on the side walls, gold rings hanging over the inlay, the curved security desk,
// two lounge groups, speed gates into the elevator lobby and the gold express elevator. The express elevator is the
// only way up to Sandoval's penthouse office in the top tower segment, 456 m above the plaza: glass on all four
// faces, a lounge with a grand piano under a crystal chandelier, the Solaris Pier model, the executive desk facing
// the city, a bar and a dining table in the wings, art, and the round door of his private vault. IM_ELEVATOR and
// IM_ELEVATOR_TOP mark the cab floors; interiors_game.cpp rides between them with a fade (InteriorDef::link).
#include "interiors.h"
#include "sites.h"

namespace World {
namespace ikit {

// ------------------------------------------------------------------------------------------------ shared pieces
// Door cut-out of a curtain wall: s range along the wall, from the floor up to z1
struct CwCut {
    float s0, s1, z1;
};

// Curtain wall glazing an opening: glass from a (model space, bottom of the opening) along u (counter-clockwise,
// outside on the right) over w x h, mullions on the facade's bay grid (bays), transoms at the given heights; door
// cut-outs stay open for the door leaves and get jamb mullions and a head rail
void curtainGlass(IB& b, vec3 a, vec2 u, float w, float h, int bays, std::initializer_list<float> transoms, const std::vector<CwCut>& cuts, u32 frameCol,
                  float clarity) {
    if (!b.geo()) return;
    InPart ip(b, IP_SHELL);
    At at(b, a, atan2f(u.y, u.x));   // local x along the wall, local +y inward
    const float gy = 0.035f, my = gy + 0.055f, md = 0.07f;
    u32 fm = M(MAT_METAL_BRUSHED);
    std::vector<vec4> pieces = {vec4(0.f, w, 0.f, h)};
    for (const CwCut& c : cuts) {
        std::vector<vec4> next;
        for (const vec4& p : pieces) {
            if (c.s1 <= p.x || c.s0 >= p.y || c.z1 <= p.z) {
                next.push_back(p);
                continue;
            }
            if (c.s0 > p.x) next.push_back(vec4(p.x, c.s0, p.z, p.w));
            if (c.s1 < p.y) next.push_back(vec4(c.s1, p.y, p.z, p.w));
            if (c.z1 < p.w) next.push_back(vec4(Max(p.x, c.s0), Min(p.y, c.s1), c.z1, p.w));
        }
        pieces = next;
    }
    for (const vec4& p : pieces)
        if (p.y - p.x > 0.02f && p.w - p.z > 0.02f)
            quad(b, vec3(p.x, gy, p.z), vec3(p.y, gy, p.z), vec3(p.y, gy, p.w), vec3(p.x, gy, p.w), vec2(p.x, p.z), vec2(p.y, p.z), vec2(p.y, p.w), vec2(p.x, p.w),
                 glassCol(clarity), kGlassMat);
    // mullions (interrupted by door cut-outs)
    for (int k = 0; k <= bays; k++) {
        float s = Clamp(w * k / bays, 0.03f, w - 0.03f), zlo = 0.f;
        for (const CwCut& c : cuts)
            if (s > c.s0 + 0.05f && s < c.s1 - 0.05f) zlo = Max(zlo, c.z1 + 0.08f);
        if (h - zlo > 0.05f) box(b, vec3(s, my, (zlo + h) * 0.5f), vec3(0.03f, md, (h - zlo) * 0.5f), frameCol, fm, SK_PZ | SK_NZ);
    }
    // transoms, base and head rails (split around the doors)
    auto rail = [&](float z, float hh, bool doorsCut) {
        std::vector<vec2> segs = {vec2(0.f, w)};
        if (doorsCut)
            for (const CwCut& c : cuts) {
                if (c.z1 < z) continue;
                std::vector<vec2> next;
                for (vec2 sg : segs) {
                    if (c.s1 <= sg.x || c.s0 >= sg.y) {
                        next.push_back(sg);
                        continue;
                    }
                    if (c.s0 > sg.x) next.push_back(vec2(sg.x, c.s0));
                    if (c.s1 < sg.y) next.push_back(vec2(c.s1, sg.y));
                }
                segs = next;
            }
        for (vec2 sg : segs)
            if (sg.y - sg.x > 0.02f) box(b, vec3((sg.x + sg.y) * 0.5f, my, z), vec3((sg.y - sg.x) * 0.5f, md, hh), frameCol, fm, SK_NONE);
    };
    rail(0.04f, 0.04f, true);
    rail(h - 0.04f, 0.04f, false);
    for (float t : transoms) rail(t, 0.035f, true);
    for (const CwCut& c : cuts) {
        box(b, vec3((c.s0 + c.s1) * 0.5f, my, c.z1 + 0.045f), vec3((c.s1 - c.s0) * 0.5f + 0.04f, md + 0.01f, 0.045f), frameCol, fm, SK_NONE);
        for (float s : {c.s0, c.s1}) box(b, vec3(s, my, c.z1 * 0.5f), vec3(0.04f, md + 0.01f, c.z1 * 0.5f), frameCol, fm, SK_PZ | SK_NZ);
    }
}

// Flat annulus facing up (floor inlays)
void ringFlat(IB& b, vec3 c, float r0, float r1, int seg, u32 col, u32 mat) {
    for (int k = 0; k < seg; k++) {
        float a0 = kTwoPi * k / seg, a1 = kTwoPi * (k + 1) / seg;
        vec3 p0 = c + vec3(cosf(a0) * r0, sinf(a0) * r0, 0.f), p1 = c + vec3(cosf(a1) * r0, sinf(a1) * r0, 0.f);
        vec3 q0 = c + vec3(cosf(a0) * r1, sinf(a0) * r1, 0.f), q1 = c + vec3(cosf(a1) * r1, sinf(a1) * r1, 0.f);
        quad(b, p0, q0, q1, p1, p0.xy(), q0.xy(), q1.xy(), p1.xy(), col, mat);
    }
}

// Ring of tube segments (torus) around c in the plane spanned by e1 / e2
void torusRing(IB& b, vec3 c, vec3 e1, vec3 e2, float R, float r, int seg, u32 col, u32 mat) {
    vec3 prev = c + e1 * R;
    for (int i = 1; i <= seg; i++) {
        float a = kTwoPi * i / seg;
        vec3 p = c + (e1 * cosf(a) + e2 * sinf(a)) * R;
        tube(b, prev, p, r, 6, col, mat);
        prev = p;
    }
}

// Filled polygon at height z (fan from `hub`, which must see every edge), facing up or down
void polyFan(IB& b, const std::vector<vec2>& pts, vec2 hub, float z, bool up, u32 col, u32 mat) {
    size_t n = pts.size();
    for (size_t i = 0; i < n; i++) {
        vec3 a(hub, z), p(pts[i], z), q(pts[(i + 1) % n], z);
        bool ccw = cross(p.xy() - hub, q.xy() - hub) > 0.f;
        if (ccw == up) tri(b, a, p, q, col, mat);
        else tri(b, a, q, p, col, mat);
    }
}
// Side walls of an extruded polygon (counter-clockwise points, outside faces)
void polyWalls(IB& b, const std::vector<vec2>& pts, float z0, float z1, u32 col, u32 mat) {
    size_t n = pts.size();
    for (size_t i = 0; i < n; i++) {
        vec2 p = pts[i], q = pts[(i + 1) % n];
        quadM(b, vec3(p, z0), vec3(q, z0), vec3(q, z1), vec3(p, z1), col, mat);
    }
}

// Solaris sun inlay in the floor: gold ring, dark field, sixteen gold rays and a gold core
void sunInlay(IB& b, vec3 c, float R) {
    InPart ip(b, IP_SHELL);
    u32 gold = C(0.8f, 0.62f, 0.3f), dark = C(0.08f, 0.08f, 0.09f), gm = M(MAT_MARBLE);
    ringFlat(b, c + vec3(0, 0, 0.004f), R - 0.14f, R, 48, gold, gm);
    ringFlat(b, c + vec3(0, 0, 0.004f), R * 0.3f, R - 0.14f, 48, dark, gm);
    for (int k = 0; k < 16; k++) {
        float a = kTwoPi * k / 16 + kPi / 16, L = (k & 1) ? R * 0.66f : R * 0.9f, wa = kPi / 16 * 0.6f;
        vec3 tip = c + vec3(cosf(a) * L, sinf(a) * L, 0.009f);
        vec3 l = c + vec3(cosf(a - wa) * R * 0.3f, sinf(a - wa) * R * 0.3f, 0.009f), rr = c + vec3(cosf(a + wa) * R * 0.3f, sinf(a + wa) * R * 0.3f, 0.009f);
        tri(b, l, tip, rr, gold, gm);
    }
    ringFlat(b, c + vec3(0, 0, 0.004f), R * 0.22f, R * 0.3f, 32, gold, gm);
    disc(b, c + vec3(0, 0, 0.004f), R * 0.22f, 32, C(0.12f, 0.11f, 0.1f), gm);
    disc(b, c + vec3(0, 0, 0.009f), R * 0.14f, 24, gold, M(MAT_CHROME));
}

// Three gold rings hanging over the inlay on fine cables, washed by warm light
void goldRings(IB& b, vec3 c, float R, float ceilZ, int roomIdx) {
    InPart ip(b, IP_FURNITURE);
    u32 gold = C(0.86f, 0.67f, 0.34f), gm = M(MAT_CHROME);
    const float tilts[3] = {0.22f, -0.3f, 0.12f}, rads[3] = {R, R * 0.76f, R * 0.54f}, spins[3] = {0.3f, 1.4f, 2.5f};
    for (int k = 0; k < 3; k++) {
        vec3 e1(cosf(spins[k]), sinf(spins[k]), 0.f);
        vec3 e2 = vec3(-e1.y, e1.x, 0.f) * cosf(tilts[k]) + vec3(0, 0, 1) * sinf(tilts[k]);
        vec3 rc = c + vec3(0, 0, k * 0.45f);
        torusRing(b, rc, e1, e2, rads[k], 0.055f - k * 0.008f, 36, gold, gm);
        for (int j = 0; j < 3; j++) {
            float a = kTwoPi * j / 3.f + 0.5f + k;
            vec3 p = rc + (e1 * cosf(a) + e2 * sinf(a)) * rads[k];
            tube(b, p, vec3(p.x, p.y, ceilZ), 0.004f, 3, Gy(0.35f), M(MAT_METAL_BRUSHED));
        }
    }
    light(b, c + vec3(0, 0, 2.3f), vec3(1.f, 0.82f, 0.55f) * 320.f, 7.f, roomIdx);
    for (int e = -1; e <= 1; e += 2) {
        vec3 sp = c + vec3(e * 5.f, -3.f, ceilZ - c.z - 0.2f);
        light(b, sp, vec3(1.f, 0.9f, 0.75f) * 900.f, 12.f, roomIdx, normalize(c - sp), 22.f, 10.f);
    }
}

// Elevator cab dressing: mirror opposite the door, handrails on the side walls, a lit ceiling panel and the button
// panel beside the door (door on room side `doorSide`, see faceFrame)
void elevatorCab(IB& b, int roomIdx, int doorSide, const char* dest) {
    if (!b.geo()) return;
    const InteriorDef& d = *b.d;
    const InteriorRoom& rm = d.rooms[roomIdx];
    const float H = rm.mx.z - rm.mn.z;
    u32 brass = C(0.78f, 0.6f, 0.32f);
    InPart ip(b, IP_FURNITURE);
    for (int side = 0; side < 4; side++) {
        vec3 o, u, n;
        float w;
        faceFrame(rm, side, o, u, n, w);
        if (side == doorSide) {
            // button panel on the door wall
            vec3 pc = o + u * (w - 0.28f) + n * 0.012f + vec3(0, 0, 1.25f);
            float yaw = atan2f(-n.x, n.y);
            At at(b, pc, yaw);
            box(b, vec3(0.f), vec3(0.09f, 0.01f, 0.24f), brass, M(MAT_CHROME), SK_NY);
            const char* labels[3] = {dest, "L", "B"};
            for (int k = 0; k < 3; k++) {
                vec3 bp(0.f, 0.012f, 0.12f - k * 0.12f);
                b.pushAxes(bp, vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0));
                cyl(b, vec3(0.f), 0.028f, 0.028f, 0.008f, 12, Gy(0.85f), M(MAT_CHROME), true, false);
                disc(b, vec3(0, 0, 0.0085f), 0.018f, 12, k == 0 ? C(1.f, 0.75f, 0.35f, 0.6f) : Gy(0.4f), k == 0 ? EM() : M(MAT_PLASTIC));
                b.pop();
                textC(b, labels[k], bp + vec3(-0.055f, 0.002f, -0.012f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.025f, 0.004f, Gy(0.15f), M(MAT_PAINT_WHITE));
            }
            continue;
        }
        if (side == (doorSide + 2) % 4) {
            // full mirror between brass trims
            quadF(b, o + u * 0.12f + n * 0.008f + vec3(0, 0, 0.95f), o + u * (w - 0.12f) + n * 0.008f + vec3(0, 0, 0.95f),
                  o + u * (w - 0.12f) + n * 0.008f + vec3(0, 0, H - 0.25f), o + u * 0.12f + n * 0.008f + vec3(0, 0, H - 0.25f), n, Gy(0.9f), M(MAT_CHROME));
        }
        // handrail on standoffs
        vec3 r0 = o + u * 0.18f + n * 0.07f + vec3(0, 0, 0.92f), r1 = o + u * (w - 0.18f) + n * 0.07f + vec3(0, 0, 0.92f);
        tube(b, r0, r1, 0.02f, 8, brass, M(MAT_CHROME), true);
        tube(b, r0 - n * 0.06f, r0, 0.012f, 6, brass, M(MAT_CHROME));
        tube(b, r1 - n * 0.06f, r1, 0.012f, 6, brass, M(MAT_CHROME));
        // lower kick plate
        quadF(b, o + n * 0.004f + vec3(0, 0, 0.02f), o + u * w + n * 0.004f + vec3(0, 0, 0.02f), o + u * w + n * 0.004f + vec3(0, 0, 0.3f),
              o + n * 0.004f + vec3(0, 0, 0.3f), n, Gy(0.3f), M(MAT_METAL_BRUSHED));
    }
    // lit ceiling panel
    vec3 cc((rm.mn.x + rm.mx.x) * 0.5f, (rm.mn.y + rm.mx.y) * 0.5f, rm.mx.z);
    {
        InPart ip2(b, IP_SHELL);
        float hx = (rm.mx.x - rm.mn.x) * 0.5f - 0.25f, hy = (rm.mx.y - rm.mn.y) * 0.5f - 0.25f;
        b.pushAxes(cc - vec3(0, 0, 0.01f), vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 0, -1));
        quad(b, vec3(-hx, -hy, 0.f), vec3(hx, -hy, 0.f), vec3(hx, hy, 0.f), vec3(-hx, hy, 0.f), vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), C(1.f, 0.93f, 0.8f, 0.5f),
             EM());
        b.pop();
    }
    light(b, cc - vec3(0, 0, 0.3f), vec3(1.f, 0.9f, 0.75f) * 140.f, 3.5f, roomIdx);
}

// Wall of speed gates across a passage (along local x, people pass along y): stainless cabinets with black glass
// tops, card readers, glass wings (retracted halfway) and lit arrows
void speedGates(IB& b, vec3 p, float halfW, int cabinets) {
    At at(b, p, 0.f);
    u32 steel = M(MAT_METAL_BRUSHED);
    for (int k = 0; k < cabinets; k++) {
        float x = -halfW + 0.1f + (2.f * halfW - 0.2f) * k / (cabinets - 1);
        rbox(b, vec3(x, 0.f, 0.5f), vec3(0.1f, 0.7f, 0.5f), 0.02f, Gy(0.78f), steel);
        box(b, vec3(x, 0.f, 1.004f), vec3(0.1f, 0.7f, 0.004f), Gy(0.04f), M(MAT_PLASTIC), SK_NZ);
        collide(b, vec3(x, 0.f, 0.5f), vec3(0.1f, 0.7f, 0.5f));
        InPart ip(b, IP_DETAIL);
        for (int e = -1; e <= 1; e += 2) {
            box(b, vec3(x, e * 0.5f, 1.01f), vec3(0.05f, 0.07f, 0.004f), C(0.15f, 0.9f, 0.45f, 0.7f), EM(), SK_NZ);
            box(b, vec3(x, e * 0.701f, 0.85f), vec3(0.05f, 0.001f, 0.05f), C(0.15f, 0.9f, 0.45f, 0.6f), EM(), SK_NONE);
        }
        InPart ip2(b, IP_FURNITURE);
        for (int e = -1; e <= 1; e += 2) {
            if ((k == 0 && e < 0) || (k == cabinets - 1 && e > 0)) continue;
            float x0 = x + e * 0.1f, x1 = x + e * 0.34f;
            quadF(b, vec3(x0, -0.05f, 0.35f), vec3(x1, -0.05f, 0.4f), vec3(x1, -0.05f, 0.95f), vec3(x0, -0.05f, 0.98f), vec3(0, -1, 0), glassCol(0.8f), kGlassMat);
            quadF(b, vec3(x0, -0.05f, 0.35f), vec3(x1, -0.05f, 0.4f), vec3(x1, -0.05f, 0.95f), vec3(x0, -0.05f, 0.98f), vec3(0, 1, 0), glassCol(0.8f), kGlassMat);
        }
    }
}

// Vertical garden panel on a wall (local frame: wall face at y = 0, leaves toward +y, x along the wall centered)
void greenWall(IB& b, float w, float z0, float z1, int roomIdx, u32 seed) {
    Rng r(seed);
    InPart ip(b, IP_SHELL);
    box(b, vec3(0.f, 0.03f, (z0 + z1) * 0.5f), vec3(w * 0.5f, 0.03f, (z1 - z0) * 0.5f), C(0.1f, 0.08f, 0.06f), M(MAT_DIRT), SK_NY);
    u32 trim = Gy(0.7f);
    box(b, vec3(0.f, 0.08f, z1 + 0.04f), vec3(w * 0.5f + 0.06f, 0.08f, 0.04f), trim, M(MAT_METAL_BRUSHED), SK_NY);
    box(b, vec3(0.f, 0.08f, z0 - 0.04f), vec3(w * 0.5f + 0.06f, 0.08f, 0.04f), trim, M(MAT_METAL_BRUSHED), SK_NY);
    for (int e = -1; e <= 1; e += 2) box(b, vec3(e * (w * 0.5f + 0.03f), 0.08f, (z0 + z1) * 0.5f), vec3(0.03f, 0.08f, (z1 - z0) * 0.5f), trim, M(MAT_METAL_BRUSHED), SK_NY | SK_PZ | SK_NZ);
    InPart ip2(b, IP_FURNITURE);
    u32 lm = M(MAT_LEAVES);
    const float step = 0.4f;
    int nx = (int)(w / step), nz = (int)((z1 - z0) / step);
    for (int j = 0; j < nz; j++)
        for (int i = 0; i < nx; i++) {
            vec3 c(-w * 0.5f + step * (i + 0.5f) + r.range(-0.1f, 0.1f), 0.07f + r.range(0.f, 0.08f), z0 + step * (j + 0.5f) + r.range(-0.1f, 0.1f));
            float kind = r.f();
            vec3 base = kind < 0.12f ? vec3(0.55f, 0.6f, 0.2f) : (kind < 0.2f ? vec3(0.35f, 0.25f, 0.4f) : vec3(0.18f, r.range(0.35f, 0.5f), 0.14f));
            int nl = 5;
            for (int k = 0; k < nl; k++) {
                float a = r.f() * kTwoPi;
                vec3 dir = normalize(vec3(cosf(a), r.range(0.3f, 0.9f), sinf(a) * 0.9f - 0.25f));
                float L = r.range(0.16f, 0.28f), W = L * 0.42f;
                vec3 side = normalize(cross(dir, vec3(0, 1, 0))) * W;
                vec3 tip = c + dir * L, mid = c + dir * (L * 0.5f) + vec3(0, 0.03f, 0.f);
                u32 col = C(base * r.range(0.8f, 1.2f));
                vec3 n1 = cross(mid + side - c, tip - c);
                if (n1.y > 0.f) tri(b, c, mid + side, tip, col, lm);
                else tri(b, c, tip, mid + side, col, lm);
                vec3 n2 = cross(tip - c, mid - side - c);
                if (n2.y > 0.f) tri(b, c, tip, mid - side, col, lm);
                else tri(b, c, mid - side, tip, col, lm);
            }
            if (kind > 0.93f)
                for (int k = 0; k < 3; k++) sphere(b, c + vec3(r.range(-0.08f, 0.08f), 0.12f, r.range(-0.08f, 0.08f)), 0.025f, 5, C(hsv(r.range(0.9f, 1.05f), 0.6f, 0.95f)), lm);
        }
    // grow light washing the garden from the top trim
    InPart ip3(b, IP_SHELL);
    box(b, vec3(0.f, 0.17f, z1 + 0.005f), vec3(w * 0.5f, 0.012f, 0.012f), C(1.f, 0.85f, 0.6f, 0.6f), EM(), SK_NONE);
    light(b, vec3(0.f, 1.2f, z1 + 0.2f), vec3(1.f, 0.88f, 0.68f) * 650.f, 9.f, roomIdx, normalize(vec3(0, -0.35f, -1)), 70.f, 45.f);
}

// LED media wall (local frame: wall face at y = 0, screen toward +y): a sunrise over the bay made of LED panels,
// the Solaris Pier campaign across it
void mediaWall(IB& b, float w, float z0, float z1, int roomIdx) {
    InPart ip(b, IP_SHELL);
    box(b, vec3(0.f, 0.06f, (z0 + z1) * 0.5f), vec3(w * 0.5f + 0.08f, 0.06f, (z1 - z0) * 0.5f + 0.08f), Gy(0.03f), M(MAT_PLASTIC), SK_NY);
    const int nx = 12, nz = 5;
    for (int j = 0; j < nz; j++)
        for (int i = 0; i < nx; i++) {
            float x0 = -w * 0.5f + w * i / nx + 0.006f, x1 = -w * 0.5f + w * (i + 1) / nx - 0.006f;
            float za = z0 + (z1 - z0) * j / nz + 0.006f, zb = z0 + (z1 - z0) * (j + 1) / nz - 0.006f;
            float t = (j + 0.5f) / nz, sx = fabsf((i + 0.5f) / nx - 0.62f);
            // sea at the bottom, gold horizon glow, deepening sky
            vec3 col = t < 0.35f ? lerp(vec3(0.02f, 0.25f, 0.35f), vec3(0.1f, 0.45f, 0.55f), t / 0.35f)
                                 : lerp(vec3(1.f, 0.62f, 0.25f), vec3(0.12f, 0.2f, 0.45f), Saturate((t - 0.35f) / 0.65f));
            col = lerp(col, vec3(1.f, 0.85f, 0.5f), Saturate(0.5f - sx * 1.8f) * (t > 0.3f && t < 0.62f ? 0.9f : 0.f));
            quadF(b, vec3(x0, 0.121f, za), vec3(x1, 0.121f, za), vec3(x1, 0.121f, zb), vec3(x0, 0.121f, zb), vec3(0, 1, 0), C(col, 0.045f), EM(4, (u32)(i * 9 + j * 31)));
        }
    float th = (z1 - z0) * 0.14f;
    textC(b, "SOLARIS PIER", vec3(0.f, 0.126f, z0 + (z1 - z0) * 0.62f), vec3(-1, 0, 0), vec3(0, 0, 1), th, th * 0.09f, C(1.f, 1.f, 1.f, 0.18f), EM());
    textC(b, "THE NEW PORTO SOL WATERFRONT", vec3(0.f, 0.126f, z0 + (z1 - z0) * 0.4f), vec3(-1, 0, 0), vec3(0, 0, 1), th * 0.32f, th * 0.035f, C(1.f, 0.9f, 0.7f, 0.2f),
          EM());
    light(b, vec3(0.f, 1.8f, (z0 + z1) * 0.5f), vec3(0.45f, 0.65f, 1.f) * 70.f, 8.f, roomIdx);
}

// Curved security desk around O (radius R, arc a0..a1 facing outward): marble front with gold lines, dark top,
// work surface, monitors and a CCTV bank behind
void securityDesk(IB& b, vec3 O, float R, float a0, float a1, int roomIdx, u32 seed) {
    Rng r(seed);
    const int n = 12;
    u32 marble = C(0.92f, 0.91f, 0.88f), topC = C(0.1f, 0.09f, 0.09f), gold = C(0.82f, 0.64f, 0.32f);
    float da = (a1 - a0) / n, segL = 2.f * R * sinf(da * 0.5f) + 0.03f;
    for (int k = 0; k < n; k++) {
        float a = a0 + da * (k + 0.5f);
        At at(b, O + vec3(cosf(a), sinf(a), 0.f) * R, a + kHalfPi);   // local +y toward the center, x along the arc
        InPart ip(b, IP_FURNITURE);
        box(b, vec3(0.f, 0.2f, 0.55f), vec3(segL * 0.5f, 0.2f, 0.55f), marble, M(MAT_MARBLE), SK_NZ | SK_PY);
        box(b, vec3(0.f, -0.004f, 0.16f), vec3(segL * 0.5f, 0.004f, 0.012f), gold, M(MAT_CHROME), SK_NONE);
        box(b, vec3(0.f, -0.004f, 0.96f), vec3(segL * 0.5f, 0.004f, 0.012f), gold, M(MAT_CHROME), SK_NONE);
        box(b, vec3(0.f, 0.14f, 1.125f), vec3(segL * 0.5f + 0.004f, 0.2f, 0.025f), topC, M(MAT_MARBLE), SK_NONE);
        box(b, vec3(0.f, 0.72f, 0.74f), vec3(segL * 0.5f * 0.8f, 0.34f, 0.02f), C(0.32f, 0.22f, 0.14f), M(MAT_WOOD), SK_NONE);
        box(b, vec3(0.f, 0.41f, 0.37f), vec3(segL * 0.5f, 0.01f, 0.37f), C(0.3f, 0.21f, 0.13f), M(MAT_WOOD), SK_NY);
        collide(b, vec3(0.f, 0.2f, 0.57f), vec3(segL * 0.5f, 0.22f, 0.57f));
        if (k % 3 == 1) {
            InPart ip2(b, IP_DETAIL);
            At at2(b, vec3(0.f, 0.62f, 0.76f), kPi);   // monitor facing the guard (toward -local y = outward)
            rbox(b, vec3(0, 0.f, 0.26f), vec3(0.26f, 0.015f, 0.16f), 0.01f, Gy(0.07f), M(MAT_PLASTIC), true);
            box(b, vec3(0, -0.016f, 0.26f), vec3(0.24f, 0.001f, 0.14f), C(0.25f, 0.45f, 0.75f, 0.35f), EM(), SK_NZ);
            box(b, vec3(0, 0.03f, 0.06f), vec3(0.02f, 0.02f, 0.06f), Gy(0.1f), M(MAT_PLASTIC), SK_NZ);
            rbox(b, vec3(0.f, -0.25f, 0.012f), vec3(0.2f, 0.07f, 0.012f), 0.005f, Gy(0.12f), M(MAT_PLASTIC), true);
        }
    }
    // gold sun emblem on the middle of the front
    float am = (a0 + a1) * 0.5f;
    vec3 fp = O + vec3(cosf(am), sinf(am), 0.f) * (R + 0.012f) + vec3(0, 0, 0.58f);
    vec3 outN(cosf(am), sinf(am), 0.f);
    b.pushAxes(fp, vec3(-outN.y, outN.x, 0.f), vec3(0, 0, 1), outN);
    cyl(b, vec3(0.f), 0.2f, 0.2f, 0.015f, 24, gold, M(MAT_CHROME), true, false);
    for (int k = 0; k < 12; k++) {
        float a = kTwoPi * k / 12;
        tri(b, vec3(cosf(a - 0.12f) * 0.2f, sinf(a - 0.12f) * 0.2f, 0.008f), vec3(cosf(a) * 0.34f, sinf(a) * 0.34f, 0.008f),
            vec3(cosf(a + 0.12f) * 0.2f, sinf(a + 0.12f) * 0.2f, 0.008f), gold, M(MAT_CHROME));
    }
    b.pop();
    // CCTV bank on a low cabinet behind the desk
    {
        vec3 cp = O - vec3(cosf(am), sinf(am), 0.f) * 0.9f;
        At at(b, cp, am + kHalfPi);   // local +y toward... (the desk center side): screens face the desk
        InPart ip(b, IP_FURNITURE);
        box(b, vec3(0.f, 0.f, 0.4f), vec3(1.1f, 0.25f, 0.4f), C(0.12f, 0.12f, 0.13f), M(MAT_METAL_PAINTED), SK_NZ);
        collide(b, vec3(0.f, 0.f, 0.4f), vec3(1.1f, 0.25f, 0.4f));
        InPart ip2(b, IP_DETAIL);
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 3; i++) {
                vec3 sc(-0.7f + i * 0.7f, 0.1f, 1.05f + j * 0.42f);
                rbox(b, sc, vec3(0.33f, 0.03f, 0.2f), 0.01f, Gy(0.06f), M(MAT_PLASTIC), true);
                box(b, sc + vec3(0.f, -0.031f, 0.f), vec3(0.31f, 0.001f, 0.18f), C(0.55f, 0.65f, 0.6f, 0.25f), EM(r.chance(0.3f) ? 7u : 0u, r.next() & 255u), SK_NZ);
            }
        box(b, vec3(0.f, 0.14f, 1.25f), vec3(1.05f, 0.02f, 0.46f), Gy(0.08f), M(MAT_METAL_PAINTED), SK_NY);
    }
    light(b, O + vec3(0, 0, 3.6f), vec3(1.f, 0.9f, 0.75f) * 380.f, 7.f, roomIdx, vec3(0, 0, -1), 60.f, 35.f);
}

// Round vault door on a wall (face +y at c, center height c.z): frame ring, slab with bolts, hinge blocks, the
// spoked wheel and a combination dial
void vaultDoor(IB& b, vec3 c, float R) {
    u32 steel = M(MAT_METAL_BRUSHED), chrome = M(MAT_CHROME);
    b.pushAxes(c, vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0));   // local z = out of the wall
    cyl(b, vec3(0.f), R + 0.22f, R + 0.22f, 0.05f, 40, Gy(0.3f), steel, true, false);
    cyl(b, vec3(0, 0, 0.05f), R, R * 0.97f, 0.2f, 40, Gy(0.62f), steel, true, false);
    for (int k = 0; k < 16; k++) {
        float a = kTwoPi * k / 16;
        cyl(b, vec3(cosf(a) * R * 0.86f, sinf(a) * R * 0.86f, 0.25f), 0.045f, 0.04f, 0.03f, 8, Gy(0.8f), chrome, true, false);
    }
    cyl(b, vec3(0, 0, 0.25f), 0.2f, 0.18f, 0.12f, 16, Gy(0.75f), chrome, true, false);
    for (int k = 0; k < 6; k++) {
        float a = kTwoPi * k / 6 + 0.26f;
        tube(b, vec3(0, 0, 0.33f), vec3(cosf(a) * 0.62f, sinf(a) * 0.62f, 0.33f), 0.03f, 6, Gy(0.8f), chrome);
    }
    torusRing(b, vec3(0, 0, 0.33f), vec3(1, 0, 0), vec3(0, 1, 0), 0.62f, 0.035f, 24, Gy(0.82f), chrome);
    cyl(b, vec3(R * 0.55f, R * 0.5f, 0.25f), 0.1f, 0.1f, 0.05f, 16, Gy(0.2f), M(MAT_PLASTIC), true, false);
    for (int e = -1; e <= 1; e += 2) box(b, vec3(-R - 0.1f, e * R * 0.45f, 0.14f), vec3(0.14f, 0.22f, 0.14f), Gy(0.45f), steel, SK_NZ);
    b.pop();
}

// Grand piano (keyboard toward +y, the player sits at +y), lid raised on its stick, bench
void grandPiano(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    u32 lac = C(0.015f, 0.015f, 0.018f), lm = M(MAT_PLASTIC);
    // case outline (counter-clockwise from above): straight bass side at x = -0.76, curved treble side
    std::vector<vec2> cs = {vec2(-0.76f, 0.f),   vec2(-0.76f, -1.85f), vec2(-0.6f, -2.05f), vec2(-0.3f, -2.12f), vec2(-0.02f, -2.02f), vec2(0.12f, -1.78f),
                            vec2(0.2f, -1.45f),  vec2(0.32f, -1.15f),  vec2(0.55f, -0.88f), vec2(0.74f, -0.62f), vec2(0.76f, -0.3f),  vec2(0.76f, 0.f)};
    vec2 hub(-0.3f, -0.8f);
    polyWalls(b, cs, 0.62f, 0.96f, lac, lm);
    polyFan(b, cs, hub, 0.62f, false, lac, lm);
    polyFan(b, cs, hub, 0.95f, true, C(0.35f, 0.25f, 0.15f), M(MAT_WOOD));   // soundboard seen with the lid up
    // lid hinged along the bass side, raised ~35 degrees on its stick
    {
        const float ang = 0.62f;
        b.pushAxes(vec3(-0.76f, 0.f, 0.965f), vec3(cosf(ang), 0.f, sinf(ang)), vec3(0, 1, 0), vec3(-sinf(ang), 0.f, cosf(ang)));
        std::vector<vec2> lid;
        for (vec2 q : cs) lid.push_back(vec2(q.x + 0.76f, q.y));
        polyFan(b, lid, hub + vec2(0.76f, 0.f), 0.f, true, lac, lm);
        polyFan(b, lid, hub + vec2(0.76f, 0.f), -0.004f, false, lac, lm);
        b.pop();
        tube(b, vec3(0.45f, -0.9f, 0.96f), vec3(-0.76f + 1.25f * cosf(ang), -0.9f, 0.965f + 1.25f * sinf(ang)), 0.01f, 4, lac, lm);   // lid stick
    }
    // key bed, keys, fallboard and music desk
    box(b, vec3(0.f, 0.14f, 0.72f), vec3(0.76f, 0.16f, 0.06f), lac, lm, SK_NONE);
    box(b, vec3(0.f, 0.18f, 0.785f), vec3(0.7f, 0.12f, 0.006f), Gy(0.95f), lm, SK_NONE);
    for (int k = 0; k < 36; k++) {
        int o = k % 5;
        float x = -0.68f + k * (1.36f / 36.f);
        if (o == 2) continue;
        box(b, vec3(x, 0.12f, 0.8f), vec3(0.008f, 0.06f, 0.01f), Gy(0.03f), lm, SK_NZ);
    }
    box(b, vec3(0.f, 0.03f, 0.86f), vec3(0.72f, 0.03f, 0.06f), lac, lm, SK_NONE);
    b.pushAxes(vec3(0.f, -0.08f, 1.05f), vec3(1, 0, 0), normalize(vec3(0, 0.3f, 1.f)), normalize(vec3(0, -1.f, 0.3f)));
    box(b, vec3(0.f), vec3(0.4f, 0.13f, 0.008f), lac, lm, SK_NONE);
    b.pop();
    // legs with brass casters, pedal lyre
    vec2 legs[3] = {vec2(-0.62f, -0.12f), vec2(0.62f, -0.12f), vec2(-0.45f, -1.85f)};
    for (vec2 l : legs) {
        lathe(b, vec3(l, 0.f), {vec2(0.05f, 0.f), vec2(0.06f, 0.06f), vec2(0.045f, 0.3f), vec2(0.07f, 0.55f), vec2(0.07f, 0.62f)}, 10, lac, lm, false);
        sphere(b, vec3(l, 0.03f), 0.035f, 6, C(0.8f, 0.62f, 0.3f), M(MAT_CHROME));
    }
    box(b, vec3(0.f, -0.25f, 0.33f), vec3(0.08f, 0.03f, 0.3f), lac, lm, SK_NZ);
    for (int k = -1; k <= 1; k++) box(b, vec3(k * 0.05f, -0.2f, 0.06f), vec3(0.012f, 0.06f, 0.006f), C(0.8f, 0.62f, 0.3f), M(MAT_CHROME), SK_NONE);
    collide(b, vec3(0.f, -1.0f, 0.48f), vec3(0.76f, 1.08f, 0.48f));
    // bench
    rbox(b, vec3(0.f, 0.72f, 0.46f), vec3(0.42f, 0.18f, 0.04f), 0.02f, C(0.05f, 0.04f, 0.04f), M(MAT_LEATHER), true);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) box(b, vec3(sx * 0.37f, 0.72f + sy * 0.13f, 0.21f), vec3(0.02f, 0.02f, 0.21f), lac, lm, SK_NZ);
}

// Executive desk (the executive sits at +y): walnut top with a gold edge, modesty panel with inlay, pedestals,
// monitor, lamp, nameplate facing visitors, papers and a humidor
void executiveDesk(IB& b, vec3 p, float yaw, int roomIdx, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 walnut = C(0.22f, 0.13f, 0.08f), wm = M(MAT_WOOD), gold = C(0.82f, 0.64f, 0.32f);
    const float L = 3.0f, Dd = 1.1f;
    rbox(b, vec3(0.f, 0.f, 0.765f), vec3(L * 0.5f, Dd * 0.5f, 0.03f), 0.012f, walnut, wm, true);
    box(b, vec3(0.f, -Dd * 0.5f - 0.002f, 0.765f), vec3(L * 0.5f, 0.003f, 0.012f), gold, M(MAT_CHROME), SK_NONE);
    box(b, vec3(0.f, -Dd * 0.5f + 0.06f, 0.4f), vec3(L * 0.5f - 0.05f, 0.03f, 0.34f), walnut, wm, SK_NONE);
    for (int k = 0; k < 3; k++) box(b, vec3(0.f, -Dd * 0.5f + 0.028f, 0.22f + k * 0.16f), vec3(L * 0.5f - 0.12f, 0.002f, 0.005f), gold, M(MAT_CHROME), SK_NONE);
    for (int e = -1; e <= 1; e += 2) {
        box(b, vec3(e * (L * 0.5f - 0.3f), 0.05f, 0.37f), vec3(0.28f, Dd * 0.5f - 0.1f, 0.37f), walnut, wm, SK_NZ);
        for (int k = 0; k < 3; k++) box(b, vec3(e * (L * 0.5f - 0.3f), Dd * 0.5f - 0.04f, 0.15f + k * 0.22f), vec3(0.08f, 0.012f, 0.012f), gold, M(MAT_CHROME), SK_NONE);
    }
    collide(b, vec3(0.f, 0.f, 0.4f), vec3(L * 0.5f, Dd * 0.5f, 0.4f));
    InPart ip(b, IP_DETAIL);
    {
        At at2(b, vec3(0.2f, -0.25f, 0.795f), kPi);   // monitor facing the executive (+y side)
        rbox(b, vec3(0, 0.f, 0.3f), vec3(0.33f, 0.015f, 0.2f), 0.01f, Gy(0.06f), M(MAT_PLASTIC), true);
        box(b, vec3(0, -0.016f, 0.3f), vec3(0.31f, 0.001f, 0.18f), C(0.3f, 0.5f, 0.8f, 0.3f), EM(), SK_NZ);
        box(b, vec3(0, 0.03f, 0.08f), vec3(0.02f, 0.02f, 0.08f), Gy(0.6f), M(MAT_CHROME), SK_NZ);
    }
    rbox(b, vec3(0.2f, 0.18f, 0.8f), vec3(0.22f, 0.07f, 0.008f), 0.004f, Gy(0.12f), M(MAT_PLASTIC), true);
    tableLamp(b, vec3(-1.15f, -0.2f, 0.795f), roomIdx, gold, C(0.2f, 0.35f, 0.25f), 60.f);
    {
        // nameplate facing the visitors (-y)
        box(b, vec3(-0.35f, -0.4f, 0.82f), vec3(0.2f, 0.03f, 0.025f), gold, M(MAT_CHROME), SK_NZ);
        textC(b, "R. SANDOVAL", vec3(-0.35f, -0.431f, 0.815f), vec3(1, 0, 0), vec3(0, 0, 1), 0.022f, 0.004f, Gy(0.05f), M(MAT_PAINT_WHITE));
    }
    for (int k = 0; k < 4; k++) {
        float a = r.range(-0.2f, 0.2f);
        At at3(b, vec3(0.75f + r.range(-0.05f, 0.05f), 0.05f + r.range(-0.05f, 0.05f), 0.797f + k * 0.002f), a);
        box(b, vec3(0.f), vec3(0.105f, 0.148f, 0.001f), Gy(0.95f), M(MAT_PAINT_WHITE), SK_NZ);
    }
    rbox(b, vec3(-0.75f, 0.1f, 0.84f), vec3(0.16f, 0.11f, 0.05f), 0.01f, C(0.3f, 0.16f, 0.08f), wm, true);   // humidor
    box(b, vec3(-0.75f, 0.1f, 0.892f), vec3(0.05f, 0.02f, 0.004f), gold, M(MAT_CHROME), SK_NZ);
    cyl(b, vec3(1.25f, 0.2f, 0.795f), 0.04f, 0.04f, 0.1f, 10, C(0.9f, 0.9f, 0.88f), M(MAT_PAINT_WHITE), false);   // cup
    rbox(b, vec3(-0.15f, -0.1f, 0.8f), vec3(0.08f, 0.05f, 0.012f), 0.01f, Gy(0.08f), M(MAT_PLASTIC), true);   // phone
}

// Tall-back leather executive chair (sitter faces +y)
void execChair(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    u32 leather = C(0.06f, 0.04f, 0.03f), lm = M(MAT_LEATHER), chrome = M(MAT_CHROME);
    for (int k = 0; k < 5; k++) {
        float a = kTwoPi * k / 5 + 0.3f;
        tube(b, vec3(0, 0, 0.1f), vec3(cosf(a) * 0.34f, sinf(a) * 0.34f, 0.06f), 0.022f, 6, Gy(0.7f), chrome);
        sphere(b, vec3(cosf(a) * 0.34f, sinf(a) * 0.34f, 0.03f), 0.03f, 6, Gy(0.1f), M(MAT_PLASTIC));
    }
    cyl(b, vec3(0, 0, 0.08f), 0.03f, 0.03f, 0.34f, 8, Gy(0.6f), chrome, false);
    rbox(b, vec3(0, 0.02f, 0.5f), vec3(0.3f, 0.29f, 0.07f), 0.05f, leather, lm, true);
    b.pushAxes(vec3(0, -0.27f, 0.95f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.15f)), normalize(vec3(0, -0.15f, 1)));
    rbox(b, vec3(0.f), vec3(0.29f, 0.07f, 0.42f), 0.06f, leather, lm, true);
    rbox(b, vec3(0.f, 0.05f, 0.36f), vec3(0.2f, 0.04f, 0.08f), 0.035f, leather, lm, true);
    b.pop();
    for (int e = -1; e <= 1; e += 2) {
        rbox(b, vec3(e * 0.33f, 0.02f, 0.72f), vec3(0.04f, 0.22f, 0.03f), 0.015f, leather, lm, true);
        tube(b, vec3(e * 0.31f, 0.05f, 0.55f), vec3(e * 0.33f, 0.05f, 0.7f), 0.015f, 6, Gy(0.7f), chrome);
    }
    collide(b, vec3(0, 0, 0.55f), vec3(0.33f, 0.33f, 0.55f));
}

// Architectural model of Solaris Pier on a plinth under a glass case (front with the plaque toward -y)
void pierModel(IB& b, vec3 p, float yaw, int roomIdx, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    const float hx = 1.5f, hy = 0.95f, zt = 0.9f;
    u32 white = Gy(0.95f), gold = C(0.82f, 0.64f, 0.32f);
    box(b, vec3(0.f, 0.f, zt * 0.5f), vec3(hx + 0.05f, hy + 0.05f, zt * 0.5f), C(0.1f, 0.1f, 0.11f), M(MAT_PLASTIC), SK_NZ);
    box(b, vec3(0.f, -hy - 0.052f, zt - 0.12f), vec3(hx + 0.05f, 0.002f, 0.01f), gold, M(MAT_CHROME), SK_NONE);
    box(b, vec3(0.f, -hy - 0.055f, zt * 0.45f), vec3(0.45f, 0.004f, 0.1f), gold, M(MAT_CHROME), SK_NONE);
    textC(b, "SOLARIS PIER", vec3(0.f, -hy - 0.06f, zt * 0.47f), vec3(1, 0, 0), vec3(0, 0, 1), 0.06f, 0.008f, Gy(0.05f), M(MAT_PAINT_WHITE));
    textC(b, "PORTO SOL WATERFRONT", vec3(0.f, -hy - 0.06f, zt * 0.39f), vec3(1, 0, 0), vec3(0, 0, 1), 0.028f, 0.004f, Gy(0.05f), M(MAT_PAINT_WHITE));
    collide(b, vec3(0.f, 0.f, zt * 0.5f), vec3(hx + 0.05f, hy + 0.05f, zt * 0.5f));
    InPart ip(b, IP_DETAIL);
    // water (front) and land (back), the pier with its pavilion, towers (one twisting like Solaris One), trees
    box(b, vec3(0.f, -hy * 0.5f, zt + 0.01f), vec3(hx, hy * 0.5f, 0.01f), C(0.15f, 0.42f, 0.52f), M(MAT_PLASTIC), SK_NZ);
    box(b, vec3(0.f, hy * 0.5f, zt + 0.02f), vec3(hx, hy * 0.5f, 0.02f), C(0.85f, 0.82f, 0.74f), M(MAT_PAINT_WHITE), SK_NZ);
    box(b, vec3(0.35f, -hy * 0.45f, zt + 0.035f), vec3(0.05f, hy * 0.45f, 0.012f), white, M(MAT_PAINT_WHITE), SK_NZ);
    box(b, vec3(0.35f, -hy * 0.88f, zt + 0.06f), vec3(0.14f, 0.08f, 0.035f), white, M(MAT_PAINT_WHITE), SK_NZ);
    for (int k = 0; k < 7; k++) {
        float x = -hx + 0.25f + k * 0.4f + r.range(-0.05f, 0.05f), y = hy * 0.45f + r.range(-0.2f, 0.25f);
        float h = r.range(0.15f, 0.55f);
        box(b, vec3(x, y, zt + 0.04f + h * 0.5f), vec3(r.range(0.07f, 0.12f), r.range(0.07f, 0.12f), h * 0.5f), white, M(MAT_PAINT_WHITE), SK_NZ);
        for (float z = zt + 0.1f; z < zt + 0.04f + h; z += 0.05f) box(b, vec3(x, y, z), vec3(0.13f, 0.13f, 0.002f), C(0.55f, 0.7f, 0.8f), M(MAT_PLASTIC), SK_NONE);
    }
    for (int k = 0; k < 10; k++) {
        float a = k * 0.09f, s = 0.1f - k * 0.004f;
        At at2(b, vec3(-0.5f, hy * 0.2f, zt + 0.04f + k * 0.075f), a);
        box(b, vec3(0.f, 0.f, 0.0375f), vec3(s, s, 0.0375f), white, M(MAT_PAINT_WHITE), SK_NZ);
    }
    for (int k = 0; k < 14; k++) sphere(b, vec3(r.range(-hx + 0.1f, hx - 0.1f), r.range(0.05f, 0.2f), zt + 0.07f), 0.03f, 5, C(0.25f, 0.5f, 0.2f), M(MAT_LEAVES));
    // glass case
    InPart ip2(b, IP_FURNITURE);
    const float gz = zt + 0.8f;
    vec3 cn[4] = {vec3(-hx, -hy, 0.f), vec3(hx, -hy, 0.f), vec3(hx, hy, 0.f), vec3(-hx, hy, 0.f)};
    for (int k = 0; k < 4; k++) {
        vec3 a = cn[k], c2 = cn[(k + 1) & 3];
        vec3 out = normalize(vec3(c2.y - a.y, a.x - c2.x, 0.f));
        quadF(b, a + vec3(0, 0, zt), c2 + vec3(0, 0, zt), c2 + vec3(0, 0, gz), a + vec3(0, 0, gz), out, glassCol(0.93f), kGlassMat);
    }
    quadF(b, vec3(-hx, -hy, gz), vec3(hx, -hy, gz), vec3(hx, hy, gz), vec3(-hx, hy, gz), vec3(0, 0, 1), glassCol(0.93f), kGlassMat);
    light(b, vec3(0.f, -1.2f, 3.2f), vec3(1.f, 0.95f, 0.85f) * 240.f, 5.f, roomIdx, normalize(vec3(0, 1.2f, -2.3f)), 35.f, 20.f);
}

// Crystal chandelier: three tiers of glowing drops on gold rings
void chandelier(IB& b, vec3 top, float drop, float R, int roomIdx) {
    InPart ip(b, IP_FURNITURE);
    u32 gold = C(0.85f, 0.66f, 0.34f), gm = M(MAT_CHROME);
    vec3 c = top - vec3(0, 0, drop);
    tube(b, top, c + vec3(0, 0, 0.75f), 0.018f, 6, gold, gm);
    cyl(b, top - vec3(0, 0, 0.05f), 0.12f, 0.12f, 0.05f, 16, gold, gm, false, true);
    for (int t = 0; t < 3; t++) {
        float rr = R * (1.f - t * 0.3f), z = c.z + t * 0.28f;
        torusRing(b, vec3(c.x, c.y, z), vec3(1, 0, 0), vec3(0, 1, 0), rr, 0.018f, 24, gold, gm);
        tube(b, vec3(c.x, c.y, c.z + 0.75f), vec3(c.x + rr, c.y, z), 0.008f, 4, gold, gm);
        tube(b, vec3(c.x, c.y, c.z + 0.75f), vec3(c.x - rr, c.y, z), 0.008f, 4, gold, gm);
        int n = 22 - t * 6;
        for (int k = 0; k < n; k++) {
            float a = kTwoPi * (k + 0.5f * t) / n;
            vec3 dp(c.x + cosf(a) * rr, c.y + sinf(a) * rr, z - 0.12f - (k & 1) * 0.08f);
            tube(b, dp + vec3(0, 0, 0.12f + (k & 1) * 0.08f), dp + vec3(0, 0, 0.03f), 0.003f, 3, gold, gm);
            sphere(b, dp, 0.032f, 6, C(1.f, 0.93f, 0.8f, 0.22f), EM(), 1.5f);
        }
    }
    sphere(b, c + vec3(0, 0, -0.2f), 0.09f, 10, C(1.f, 0.93f, 0.8f, 0.25f), EM(), 1.3f);
    light(b, c + vec3(0, 0, 0.2f), vec3(1.f, 0.86f, 0.62f) * 1100.f, 13.f, roomIdx);
}

// Back bar against a wall (front +y): cabinet, stone top, mirror, lit glass shelves full of bottles
void backBar(IB& b, vec3 p, float yaw, float len, int roomIdx, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 wood = C(0.2f, 0.12f, 0.07f), wm = M(MAT_WOOD);
    box(b, vec3(0.f, 0.3f, 0.45f), vec3(len * 0.5f, 0.3f, 0.45f), wood, wm, SK_NZ | SK_NY);
    box(b, vec3(0.f, 0.32f, 0.915f), vec3(len * 0.5f + 0.02f, 0.32f, 0.015f), C(0.1f, 0.1f, 0.1f), M(MAT_MARBLE), SK_NONE);
    collide(b, vec3(0.f, 0.3f, 0.45f), vec3(len * 0.5f, 0.3f, 0.45f));
    quadF(b, vec3(-len * 0.5f, 0.01f, 0.95f), vec3(len * 0.5f, 0.01f, 0.95f), vec3(len * 0.5f, 0.01f, 2.5f), vec3(-len * 0.5f, 0.01f, 2.5f), vec3(0, 1, 0), Gy(0.75f),
          M(MAT_CHROME));
    InPart ip(b, IP_DETAIL);
    for (int s = 0; s < 3; s++) {
        float z = 1.3f + s * 0.42f;
        box(b, vec3(0.f, 0.14f, z), vec3(len * 0.5f - 0.05f, 0.13f, 0.008f), C(0.8f, 0.9f, 0.9f, 1.f), M(MAT_GLASS), SK_NONE);
        box(b, vec3(0.f, 0.02f, z - 0.02f), vec3(len * 0.5f - 0.05f, 0.01f, 0.006f), C(1.f, 0.7f, 0.35f, 0.4f), EM(), SK_NONE);
        for (float x = -len * 0.5f + 0.12f; x < len * 0.5f - 0.1f; x += r.range(0.1f, 0.16f)) {
            vec3 bc = hsv(r.range(0.02f, 0.14f), r.range(0.3f, 0.9f), r.range(0.2f, 0.7f));
            if (r.chance(0.2f)) bc = vec3(0.05f, 0.2f, 0.08f);
            bottle(b, vec3(x, 0.14f + r.range(-0.04f, 0.04f), z + 0.008f), r.range(0.03f, 0.045f), r.range(0.24f, 0.34f), C(bc, 1.f), M(MAT_GLASS),
                   r.chance(0.5f) ? C(0.8f, 0.65f, 0.3f) : Gy(0.1f));
        }
    }
    for (int k = 0; k < 6; k++) {
        vec3 g(-len * 0.4f + k * 0.12f, 0.45f, 0.93f);
        lathe(b, g, {vec2(0.03f, 0.f), vec2(0.035f, 0.02f), vec2(0.04f, 0.1f)}, 8, C(0.9f, 0.95f, 0.95f, 1.f), M(MAT_GLASS), false);
    }
    light(b, vec3(0.f, 0.6f, 2.2f), vec3(1.f, 0.72f, 0.42f) * 160.f, 4.5f, roomIdx, vec3(0, 0, -1), 70.f, 40.f);
}

// Bar counter (customers at -y): backlit onyx front, dark stone top, brass foot rail
void barCounter(IB& b, vec3 p, float yaw, float len, int roomIdx) {
    At at(b, p, yaw);
    box(b, vec3(0.f, 0.f, 0.52f), vec3(len * 0.5f, 0.28f, 0.52f), C(0.12f, 0.1f, 0.08f), M(MAT_WOOD), SK_NZ);
    quadF(b, vec3(-len * 0.5f + 0.05f, -0.282f, 0.12f), vec3(len * 0.5f - 0.05f, -0.282f, 0.12f), vec3(len * 0.5f - 0.05f, -0.282f, 0.98f),
          vec3(-len * 0.5f + 0.05f, -0.282f, 0.98f), vec3(0, -1, 0), C(1.f, 0.72f, 0.4f, 0.05f), EM());
    rbox(b, vec3(0.f, -0.05f, 1.065f), vec3(len * 0.5f + 0.05f, 0.4f, 0.025f), 0.01f, C(0.06f, 0.06f, 0.06f), M(MAT_MARBLE), true);
    tube(b, vec3(-len * 0.5f, -0.45f, 0.2f), vec3(len * 0.5f, -0.45f, 0.2f), 0.025f, 8, C(0.8f, 0.62f, 0.3f), M(MAT_CHROME), true);
    for (float x = -len * 0.5f + 0.3f; x < len * 0.5f; x += 1.2f) tube(b, vec3(x, -0.45f, 0.2f), vec3(x, -0.29f, 0.25f), 0.015f, 6, C(0.8f, 0.62f, 0.3f), M(MAT_CHROME));
    collide(b, vec3(0.f, 0.f, 0.54f), vec3(len * 0.5f, 0.3f, 0.54f));
    light(b, vec3(0.f, -1.f, 0.6f), vec3(1.f, 0.7f, 0.4f) * 45.f, 3.5f, roomIdx);
}

// Abstract bronze sculpture on a stone plinth
void sculpture(IB& b, vec3 p, u32 seed) {
    Rng r(seed);
    box(b, p + vec3(0, 0, 0.5f), vec3(0.3f, 0.3f, 0.5f), Gy(0.9f), M(MAT_MARBLE), SK_NZ);
    collide(b, p + vec3(0, 0, 0.5f), vec3(0.3f, 0.3f, 0.5f));
    u32 bronze = C(0.45f, 0.3f, 0.16f);
    if (r.chance(0.5f)) {
        vec3 c = p + vec3(0, 0, 1.45f);
        torusRing(b, c, vec3(1, 0, 0), vec3(0, 0.6f, 0.8f), 0.4f, 0.07f, 20, bronze, M(MAT_CHROME));
        torusRing(b, c + vec3(0, 0, -0.05f), vec3(0, 1, 0), vec3(0.8f, 0, 0.6f), 0.3f, 0.06f, 18, bronze, M(MAT_CHROME));
    } else {
        lathe(b, p + vec3(0, 0, 1.f), {vec2(0.12f, 0.f), vec2(0.2f, 0.25f), vec2(0.08f, 0.55f), vec2(0.18f, 0.8f), vec2(0.02f, 1.05f)}, 16, bronze, M(MAT_CHROME), false);
    }
}

// Brass telescope on a tripod pointing out of the window (toward +y)
void telescope(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    u32 brass = C(0.78f, 0.6f, 0.3f), wood = C(0.3f, 0.18f, 0.1f);
    for (int k = 0; k < 3; k++) {
        float a = kTwoPi * k / 3 + 0.5f;
        tube(b, vec3(cosf(a) * 0.35f, sinf(a) * 0.35f, 0.f), vec3(0, 0, 1.15f), 0.018f, 5, wood, M(MAT_WOOD));
    }
    tube(b, vec3(0, -0.45f, 1.05f), vec3(0, 0.6f, 1.38f), 0.055f, 10, brass, M(MAT_CHROME), true);
    tube(b, vec3(0, 0.6f, 1.38f), vec3(0, 0.75f, 1.43f), 0.07f, 10, brass, M(MAT_CHROME), true);
}

// Antique globe on a turned stand
void globeStand(IB& b, vec3 p) {
    lathe(b, p, {vec2(0.25f, 0.f), vec2(0.22f, 0.05f), vec2(0.05f, 0.1f), vec2(0.04f, 0.55f), vec2(0.08f, 0.62f)}, 12, C(0.3f, 0.18f, 0.1f), M(MAT_WOOD), true);
    sphere(b, p + vec3(0, 0, 0.95f), 0.3f, 14, C(0.62f, 0.52f, 0.34f), M(MAT_PAINT_WHITE));
    torusRing(b, p + vec3(0, 0, 0.95f), vec3(0, 1, 0), normalize(vec3(0.4f, 0, 1.f)), 0.33f, 0.012f, 20, C(0.8f, 0.62f, 0.3f), M(MAT_CHROME));
    collide(b, p + vec3(0, 0, 0.6f), vec3(0.3f, 0.3f, 0.6f));
}

// ------------------------------------------------------------------------------------------------ Solaris One lobby
void layoutTowerLobby(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0x50A1u);
    const float X1 = d.x1 - 0.3f, X0 = -X1, Y0 = 0.35f, H = d.ceil;   // side walls: the podium's solid mass beyond
    const float yB = 20.f;                                             // hall back wall (center line)
    const float eX = 5.2f, eY1 = 28.f, eH = 4.2f;                       // elevator lobby
    const float cabX = 1.05f, cabY0 = eY1 + 0.15f, cabY1 = cabY0 + 2.1f, cabH = 2.7f;
    // curtain wall and doors in the podium's south face (bay grid 1.6 m from x = x0)
    openingLocal(b, vec2(d.x0, 0.f), vec2(d.x1, 0.f), 0.f, H, OP_GLASS);
    openingLocal(b, vec2(-2.4f, 0.f), vec2(2.4f, 0.f), 0.f, 3.2f, OP_DOOR);
    openingLocal(b, vec2(-10.4f, 0.f), vec2(-8.8f, 0.f), 0.f, 2.7f, OP_DOOR);
    openingLocal(b, vec2(8.8f, 0.f), vec2(10.4f, 0.f), 0.f, 2.7f, OP_DOOR);
    int hall = room(b, vec3(X0, Y0, 0.f), vec3(X1, yB - 0.15f, H), vec3(58.f, 55.f, 50.f), 0.3f, LS_ALWAYS);
    int elev = room(b, vec3(-eX, yB + 0.15f, 0.f), vec3(eX, eY1, eH), vec3(40.f, 36.f, 30.f), 0.03f, LS_ALWAYS);
    int cab = room(b, vec3(-cabX, cabY0, 0.f), vec3(cabX, cabY1, cabH), vec3(46.f, 42.f, 36.f), 0.f, LS_ALWAYS);
    door(b, vec3(0.f, 0.f, 0.f), vec2(1, 0), vec2(0, 1), 4.8f, 3.2f, DK_SLIDING_PAIR, 1, Gy(0.3f), true, 0.25f);
    door(b, vec3(-9.6f, 0.f, 0.f), vec2(1, 0), vec2(0, 1), 1.6f, 2.7f, DK_HINGED, 1, Gy(0.3f), true, 0.22f);
    door(b, vec3(9.6f, 0.f, 0.f), vec2(-1, 0), vec2(0, 1), 1.6f, 2.7f, DK_HINGED, 1, Gy(0.3f), true, 0.22f);
    const u32 gold = C(0.82f, 0.64f, 0.32f);
    door(b, vec3(0.f, eY1 + 0.075f, 0.f), vec2(1, 0), vec2(0, 1), 1.2f, 2.3f, DK_ELEVATOR, 5, gold, false);
    marker(b, IM_DOOR_OUT, vec3(0.f, -2.2f, 0.f), kPi);
    marker(b, IM_ENTRY, vec3(0.f, 2.f, 0.f), 0.f);
    marker(b, IM_ELEVATOR, vec3(0.f, (cabY0 + cabY1) * 0.5f, 0.f), kPi);
    tExtraHoles.push_back({hall, 2, {-eX - X0, eX - X0, 0.f, eH}});
    // ---- shells: travertine and white marble hall, walnut elevator lobby, bronze cab
    ShellStyle hs;
    hs.wallCol = C(0.84f, 0.8f, 0.72f);
    hs.wallMat = M(MAT_STONE);
    hs.floorMat = M(MAT_MARBLE);
    hs.floorCol = C(0.93f, 0.92f, 0.9f);
    hs.floorUV = 0.2f;
    hs.ceilMat = M(MAT_PLASTER);
    hs.ceilCol = Gy(0.95f);
    hs.baseH = 0.12f;
    hs.baseCol = C(0.16f, 0.15f, 0.14f);
    hs.baseMat = M(MAT_STONE);
    hs.revealCol = C(0.2f, 0.19f, 0.18f);
    hs.revealMat = M(MAT_METAL_BRUSHED);
    shell(b, hall, hs);
    ShellStyle es;
    es.wallCol = C(0.36f, 0.24f, 0.15f);
    es.wallMat = M(MAT_WOOD);
    es.floorMat = M(MAT_STONE);
    es.floorCol = C(0.16f, 0.15f, 0.15f);
    es.floorUV = 0.5f;
    es.ceilMat = M(MAT_PLASTER);
    es.ceilCol = Gy(0.92f);
    es.baseH = 0.12f;
    es.baseCol = C(0.1f, 0.1f, 0.1f);
    es.baseMat = M(MAT_STONE);
    es.walls = 2 | 4 | 8;
    shell(b, elev, es);
    ShellStyle cs;
    cs.wallCol = C(0.55f, 0.42f, 0.27f);
    cs.wallMat = M(MAT_METAL_BRUSHED);
    cs.floorMat = M(MAT_STONE);
    cs.floorCol = C(0.1f, 0.1f, 0.1f);
    cs.ceilMat = M(MAT_METAL_BRUSHED);
    cs.ceilCol = Gy(0.3f);
    cs.baseH = 0.f;
    shell(b, cab, cs);
    elevatorCab(b, cab, 0, "PH");
    // walnut lining of the opening into the elevator lobby
    {
        InPart ip(b, IP_SHELL);
        reveal(b, vec3(-eX, yB - 0.15f, 0.f), vec3(1, 0, 0), vec3(0, 1, 0), 0.f, 2.f * eX, 0.f, eH, 0.f, 0.3f, C(0.3f, 0.2f, 0.12f), M(MAT_WOOD), false);
    }
    // ---- entrance: curtain wall, granite landing out to the plaza, threshold mats
    {
        std::vector<CwCut> cuts = {{-10.4f - d.x0, -8.8f - d.x0, 2.7f}, {-2.4f - d.x0, 2.4f - d.x0, 3.2f}, {8.8f - d.x0, 10.4f - d.x0, 2.7f}};
        curtainGlass(b, vec3(d.x0, 0.f, 0.f), vec2(1, 0), d.x1 - d.x0, H, 23, {3.2f, 5.95f}, cuts, C(0.16f, 0.15f, 0.14f), 0.92f);
        InPart ip(b, IP_SHELL);
        box(b, vec3(0.f, -0.7f, -0.12f), vec3(d.x1, 0.7f, 0.12f), C(0.22f, 0.21f, 0.2f), M(MAT_STONE), SK_NZ);
        collide(b, vec3(0.f, -0.7f, -0.12f), vec3(d.x1, 0.7f, 0.12f));
        box(b, vec3(0.f, 0.15f, -0.01f), vec3(d.x1, 0.2f, 0.012f), C(0.22f, 0.21f, 0.2f), M(MAT_STONE), SK_NZ);
        InPart ip2(b, IP_FURNITURE);
        for (float x : {-9.6f, 0.f, 9.6f}) box(b, vec3(x, Y0 + 1.1f, 0.006f), vec3(x == 0.f ? 2.6f : 1.f, 0.9f, 0.006f), C(0.12f, 0.12f, 0.13f), M(MAT_CARPET), SK_NZ);
    }
    // ---- floor: the Solaris sun under hanging gold rings
    const vec3 sun(0.f, 7.2f, 0.f);
    sunInlay(b, sun, 3.2f);
    goldRings(b, sun + vec3(0, 0, 4.6f), 2.6f, H, hall);
    // ---- security desk (Carl's post) and the lettering over the elevator lobby
    const vec3 deskO(0.f, 14.6f, 0.f);
    securityDesk(b, deskO, 2.8f, 200.f * kDegToRad, 340.f * kDegToRad, hall, r.next());
    {
        InPart ip(b, IP_FURNITURE);
        barStool(b, vec3(-0.8f, 13.3f, 0.f), C(0.08f, 0.07f, 0.07f));
        barStool(b, vec3(0.9f, 13.3f, 0.f), C(0.08f, 0.07f, 0.07f));
    }
    scenario(b, vec3(-0.8f, 13.3f, 0.f), kPi, 6, SR_GUARD, SF_STAFF, 0.26f);
    scenario(b, vec3(0.9f, 13.5f, 0.f), kPi, 0, SR_GUARD, SF_STAFF | SF_OPTIONAL);
    scenario(b, vec3(3.2f, 12.9f, 0.f), kPi - 0.5f, 7, SR_RECEPTION, SF_STAFF | SF_DAY);
    {
        InPart ip(b, IP_SHELL);
        At at(b, vec3(0.f, yB - 0.15f, 0.f), kPi);   // back wall; local +y points into the hall
        textC(b, "SOLARIS ONE", vec3(0.f, 0.02f, 5.7f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.62f, 0.07f, gold, M(MAT_CHROME), 0.05f, 0.45f);
        // dark backing and walnut slats on both sides of the opening
        u32 wal = C(0.3f, 0.19f, 0.11f);
        for (int e = -1; e <= 1; e += 2) {
            float xa = eX + 0.2f, xb = X1 - 0.15f;
            box(b, vec3(e * (xa + xb) * 0.5f, 0.006f, H * 0.5f), vec3((xb - xa) * 0.5f, 0.005f, H * 0.5f - 0.13f), C(0.06f, 0.05f, 0.05f), M(MAT_PAINT_WHITE),
                SK_NY | SK_PZ | SK_NZ);
            for (float x = eX + 0.35f; x < X1 - 0.3f; x += 0.12f) box(b, vec3(e * x, 0.036f, H * 0.5f), vec3(0.035f, 0.025f, H * 0.5f - 0.14f), wal, M(MAT_WOOD), SK_NY | SK_PZ | SK_NZ);
        }
        for (int k = -1; k <= 1; k += 2) light(b, vec3(k * 2.6f, 2.2f, H - 0.25f), vec3(1.f, 0.88f, 0.7f) * 700.f, 9.f, hall, normalize(vec3(0.f, -1.f, -0.35f)), 30.f, 15.f);
    }
    // ---- side walls: vertical garden (west) with a planter bench, media wall (east)
    {
        At at(b, vec3(X0, 10.f, 0.f), -kHalfPi);   // local +y = +x (into the hall), local x along -y
        greenWall(b, 13.f, 0.9f, 7.4f, hall, r.next());
        InPart ip(b, IP_FURNITURE);
        box(b, vec3(0.f, 0.3f, 0.22f), vec3(6.5f, 0.3f, 0.22f), C(0.3f, 0.29f, 0.27f), M(MAT_STONE), SK_NZ);
        box(b, vec3(0.f, 0.35f, 0.455f), vec3(6.5f, 0.26f, 0.015f), C(0.5f, 0.34f, 0.2f), M(MAT_WOOD), SK_NONE);
        collide(b, vec3(0.f, 0.3f, 0.23f), vec3(6.5f, 0.3f, 0.23f));
        scenario(b, vec3(-3.f, 0.75f, 0.f), 0.f, 6, SR_PATRON, SF_OPTIONAL | SF_DAY);
        scenario(b, vec3(2.5f, 0.75f, 0.f), 0.f, 6, SR_PATRON, SF_OPTIONAL);
    }
    {
        At at(b, vec3(X1, 10.f, 0.f), kHalfPi);
        mediaWall(b, 11.f, 1.4f, 6.4f, hall);
    }
    // ---- lounge groups, planters with palms, a directory totem
    for (int e = -1; e <= 1; e += 2) {
        float cx = e * 12.6f, cy = 8.4f;
        InPart ip(b, IP_FURNITURE);
        rug(b, vec3(cx, cy, 0.f), 3.2f, 4.4f, r.next());
        sofa(b, vec3(cx + e * 2.3f, cy, 0.f), e < 0 ? -kHalfPi : kHalfPi, 2.4f, C(0.62f, 0.58f, 0.52f), r.next());
        armchair(b, vec3(cx - e * 1.4f, cy - 1.1f, 0.f), e < 0 ? kHalfPi : -kHalfPi, C(0.25f, 0.3f, 0.34f), r.next());
        armchair(b, vec3(cx - e * 1.4f, cy + 1.1f, 0.f), e < 0 ? kHalfPi : -kHalfPi, C(0.25f, 0.3f, 0.34f), r.next());
        coffeeTable(b, vec3(cx + e * 0.4f, cy, 0.f), kHalfPi, 1.3f, 0.7f, r.next());
        floorLamp(b, vec3(cx + e * 2.4f, cy + 1.7f, 0.f), hall, C(0.9f, 0.85f, 0.75f), 70.f);
        scenario(b, vec3(cx + e * 2.2f, cy - 0.5f, 0.f), e < 0 ? -kHalfPi : kHalfPi, 6, SR_PATRON, SF_OPTIONAL);
        scenario(b, vec3(cx - e * 1.4f, cy + 1.1f, 0.f), e < 0 ? kHalfPi : -kHalfPi, 6, SR_PATRON, SF_OPTIONAL | SF_DAY);
        planter(b, vec3(e * 16.9f, 2.0f, 0.f), 2.1f, r.next());
        planter(b, vec3(e * 16.9f, 18.6f, 0.f), 2.1f, r.next());
        planter(b, vec3(e * 6.3f, 18.9f, 0.f), 1.6f, r.next());
    }
    {
        At at(b, vec3(7.4f, 18.4f, 0.f), 0.f);   // directory totem facing the entrance
        InPart ip(b, IP_FURNITURE);
        box(b, vec3(0.f, 0.f, 1.1f), vec3(0.32f, 0.08f, 1.1f), C(0.14f, 0.13f, 0.13f), M(MAT_STONE), SK_NZ);
        collide(b, vec3(0.f, 0.f, 1.1f), vec3(0.32f, 0.08f, 1.1f));
        textC(b, "SOLARIS ONE", vec3(0.f, -0.082f, 1.95f), vec3(1, 0, 0), vec3(0, 0, 1), 0.06f, 0.009f, gold, M(MAT_CHROME));
        const char* rows[5] = {"PH  PENTHOUSE", "60-89 RESIDENCES", "12-59 OFFICES", "2-11 SOLARIS CLUB", "L   LOBBY"};
        for (int k = 0; k < 5; k++) text(b, rows[k], vec3(-0.26f, -0.082f, 1.7f - k * 0.13f), vec3(1, 0, 0), vec3(0, 0, 1), 0.04f, 0.006f, Gy(0.9f), M(MAT_PAINT_WHITE));
    }
    // ---- speed gates and the elevator lobby: six cars on the side walls, the gold express elevator at the end
    speedGates(b, vec3(0.f, yB + 1.2f, 0.f), eX, 7);
    scenario(b, vec3(3.9f, 18.3f, 0.f), kPi + 0.3f, 19, SR_GUARD, SF_STAFF);
    for (int e = -1; e <= 1; e += 2)
        for (int k = 0; k < 3; k++) {
            float y = 23.0f + k * 1.95f;
            elevatorLanding(b, vec3(e * eX, y, 0.f), e < 0 ? -kHalfPi : kHalfPi, 1);
            collide(b, vec3(e * (eX - 0.04f), y, 1.1f), vec3(0.06f, 0.75f, 1.1f));
        }
    scenario(b, vec3(-3.6f, 24.2f, 0.f), -kHalfPi + 0.4f, 14, SR_PATRON, SF_OPTIONAL | SF_DAY);
    scenario(b, vec3(3.4f, 25.8f, 0.f), kHalfPi, 0, SR_PATRON, SF_OPTIONAL);
    {
        InPart ip(b, IP_SHELL);
        At at(b, vec3(0.f, eY1, 0.f), kPi);   // express elevator surround on the end wall; local +y into the lobby
        for (int e = -1; e <= 1; e += 2) box(b, vec3(e * 0.72f, 0.04f, 1.25f), vec3(0.12f, 0.04f, 1.25f), gold, M(MAT_CHROME), SK_NY);
        box(b, vec3(0.f, 0.04f, 2.58f), vec3(0.84f, 0.04f, 0.08f), gold, M(MAT_CHROME), SK_NY);
        box(b, vec3(0.f, 0.02f, 3.05f), vec3(1.1f, 0.02f, 0.26f), Gy(0.05f), M(MAT_PLASTIC), SK_NY);
        textC(b, "PENTHOUSE", vec3(0.f, 0.043f, 3.05f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.2f, 0.022f, C(1.f, 0.78f, 0.45f, 0.35f), EM());
        box(b, vec3(-1.15f, 0.03f, 1.2f), vec3(0.07f, 0.03f, 0.12f), Gy(0.1f), M(MAT_PLASTIC), SK_NY);
        box(b, vec3(-1.15f, 0.061f, 1.25f), vec3(0.04f, 0.001f, 0.03f), C(1.f, 0.2f, 0.1f, 0.7f), EM(7, 40), SK_NONE);
        light(b, vec3(0.f, 1.2f, eH - 0.2f), vec3(1.f, 0.85f, 0.6f) * 280.f, 5.f, elev, normalize(vec3(0.f, -0.3f, -1.f)), 55.f, 30.f);
    }
    partitionX(b, X0, X1, yB, 0.3f, H, {vec2(-eX, eX)});
    partitionY(b, yB, eY1 + 0.15f, -eX - 0.15f, 0.3f, eH);
    partitionY(b, yB, eY1 + 0.15f, eX + 0.15f, 0.3f, eH);
    partitionX(b, -eX, eX, eY1 + 0.075f, 0.15f, eH, {vec2(-0.6f, 0.6f)});
    partitionY(b, cabY0, cabY1 + 0.15f, -cabX - 0.075f, 0.15f, cabH);
    partitionY(b, cabY0, cabY1 + 0.15f, cabX + 0.075f, 0.15f, cabH);
    partitionX(b, -cabX, cabX, cabY1 + 0.075f, 0.15f, cabH);
    // ---- people crossing the lobby
    scenario(b, vec3(4.6f, 6.4f, 0.f), -2.2f, 7, SR_PATRON, SF_DAY);
    scenario(b, vec3(5.3f, 7.3f, 0.f), 1.0f, 7, SR_PATRON, SF_DAY | SF_OPTIONAL);
    scenario(b, vec3(-4.4f, 11.2f, 0.f), -0.9f, 7, SR_PATRON, SF_OPTIONAL);
    scenario(b, vec3(-3.5f, 11.9f, 0.f), 2.4f, 7, SR_PATRON, SF_OPTIONAL);
    scenario(b, vec3(-7.8f, 3.4f, 0.f), 0.4f, 14, SR_PATRON, SF_OPTIONAL | SF_DAY);
    // ---- lights: high downlight grid, elevator lobby cans
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 6; i++) {
            vec3 lp(X0 + (X1 - X0) * (i + 0.5f) / 6.f, Y0 + (yB - Y0) * (j + 0.5f) / 3.f, H);
            if (length(lp.xy() - sun.xy()) < 2.f) continue;
            downlight(b, lp, hall, 1300.f, vec3(1.f, 0.9f, 0.76f), 12.f);
        }
    for (int k = 0; k < 3; k++) downlight(b, vec3(0.f, yB + 1.6f + k * 2.3f, eH), elev, 420.f, vec3(1.f, 0.86f, 0.66f), 6.5f);
    roomDressing(b, hall, true, d.seed ^ 0x1u);
}

// ------------------------------------------------------------------------------------------------ penthouse
void layoutPenthouse(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0x9E47u);
    const float Hh = d.x1, Cc = Hh * 0.3f, E = Hh - Cc, D = d.depth, H = d.ceil, g = 0.15f;
    // glass on the four flat faces of the chamfered floor plate (counter-clockwise, outside on the right)
    openingLocal(b, vec2(-E, 0.f), vec2(E, 0.f), 0.f, H, OP_GLASS);
    openingLocal(b, vec2(Hh, Cc), vec2(Hh, D - Cc), 0.f, H, OP_GLASS);
    openingLocal(b, vec2(E, D), vec2(-E, D), 0.f, H, OP_GLASS);
    openingLocal(b, vec2(-Hh, D - Cc), vec2(-Hh, Cc), 0.f, H, OP_GLASS);
    // rooms: the hall between the south and north glass, wings behind the east and west glass, the cab in the
    // south-east corner pier (the chamfered corners are closed piers)
    const vec3 warm(30.f, 26.f, 21.f);
    int hall = room(b, vec3(-E, g, 0.f), vec3(E, D - g, H), warm, 0.35f, LS_EVENING);
    int wingE = room(b, vec3(E, Cc, 0.f), vec3(Hh - g, D - Cc, H), warm, 0.35f, LS_EVENING);
    int wingW = room(b, vec3(-(Hh - g), Cc, 0.f), vec3(-E, D - Cc, H), warm, 0.35f, LS_EVENING);
    const float cx0 = E + 0.15f, cx1 = cx0 + 2.0f, cy1 = Cc - 0.15f, cy0 = cy1 - 2.0f, cyc = (cy0 + cy1) * 0.5f;
    int cab = room(b, vec3(cx0, cy0, 0.f), vec3(cx1, cy1, 2.7f), vec3(46.f, 42.f, 36.f), 0.f, LS_ALWAYS);
    const u32 gold = C(0.82f, 0.64f, 0.32f);
    door(b, vec3(E + 0.075f, cyc, 0.f), vec2(0, 1), vec2(-1, 0), 1.1f, 2.3f, DK_ELEVATOR, 5, gold, false);
    marker(b, IM_ELEVATOR_TOP, vec3((cx0 + cx1) * 0.5f, cyc, 0.f), kHalfPi);
    marker(b, IM_ENTRY, vec3(E - 1.4f, cyc, 0.f), kHalfPi);
    tExtraHoles.push_back({hall, 1, {Cc - g, D - Cc - g, 0.f, H}});
    tExtraHoles.push_back({hall, 3, {Cc - g, D - Cc - g, 0.f, H}});
    // ---- shells: dark walnut floor, nero marble piers, white ceiling
    ShellStyle ps;
    ps.wallCol = C(0.14f, 0.13f, 0.13f);
    ps.wallMat = M(MAT_MARBLE);
    ps.floorMat = M(MAT_WOOD_FLOOR);
    ps.floorCol = C(0.45f, 0.32f, 0.22f);
    ps.floorUV = 0.8f;
    ps.ceilMat = M(MAT_PLASTER);
    ps.ceilCol = Gy(0.93f);
    ps.baseH = 0.1f;
    ps.baseCol = gold;
    ps.baseMat = M(MAT_CHROME);
    ps.revealCol = C(0.12f, 0.11f, 0.1f);
    ps.revealMat = M(MAT_METAL_BRUSHED);
    shell(b, hall, ps);
    ShellStyle ws = ps;
    ws.walls = 1 | 2 | 4;
    shell(b, wingE, ws);
    ws.walls = 1 | 4 | 8;
    shell(b, wingW, ws);
    ShellStyle cs;
    cs.wallCol = C(0.55f, 0.42f, 0.27f);
    cs.wallMat = M(MAT_METAL_BRUSHED);
    cs.floorMat = M(MAT_STONE);
    cs.floorCol = C(0.1f, 0.1f, 0.1f);
    cs.ceilMat = M(MAT_METAL_BRUSHED);
    cs.ceilCol = Gy(0.3f);
    cs.baseH = 0.f;
    shell(b, cab, cs);
    elevatorCab(b, cab, 3, "L");
    // ---- curtain walls (16 bays per face like the tower facade, a transom on its 4.1 m floor line)
    {
        const u32 bronze = C(0.14f, 0.12f, 0.1f);
        int bays = Max(1, (int)roundf(2.f * E / 1.6f));
        curtainGlass(b, vec3(-E, 0.f, 0.f), vec2(1, 0), 2.f * E, H, bays, {4.1f}, {}, bronze, 0.93f);
        curtainGlass(b, vec3(Hh, Cc, 0.f), vec2(0, 1), D - 2.f * Cc, H, bays, {4.1f}, {}, bronze, 0.93f);
        curtainGlass(b, vec3(E, D, 0.f), vec2(-1, 0), 2.f * E, H, bays, {4.1f}, {}, bronze, 0.93f);
        curtainGlass(b, vec3(-Hh, D - Cc, 0.f), vec2(0, -1), D - 2.f * Cc, H, bays, {4.1f}, {}, bronze, 0.93f);
    }
    // ---- piers: collision around the corner piers, the cab and the wing ends
    partitionY(b, g, Cc, E + 0.075f, 0.15f, H, {vec2(cyc - 0.55f, cyc + 0.55f)});
    partitionY(b, D - Cc, D - g, E + 0.075f, 0.15f, H);
    partitionY(b, g, Cc, -E - 0.075f, 0.15f, H);
    partitionY(b, D - Cc, D - g, -E - 0.075f, 0.15f, H);
    partitionX(b, E, Hh, Cc - 0.075f, 0.15f, H);
    partitionX(b, E, Hh, D - Cc + 0.075f, 0.15f, H);
    partitionX(b, -Hh, -E, Cc - 0.075f, 0.15f, H);
    partitionX(b, -Hh, -E, D - Cc + 0.075f, 0.15f, H);
    partitionX(b, cx0, cx1 + 0.15f, cy0 - 0.075f, 0.15f, 2.8f);
    partitionY(b, cy0 - 0.15f, cy1, cx1 + 0.075f, 0.15f, 2.8f);
    // arrival: guards by the elevator, the vault door on the opposite pier with its own guard
    scenario(b, vec3(E - 1.3f, cyc - 1.4f, 0.f), kHalfPi + 0.3f, 19, SR_GUARD, SF_STAFF);
    scenario(b, vec3(E - 1.3f, cyc + 1.6f, 0.f), kHalfPi - 0.3f, 19, SR_GUARD, SF_STAFF | SF_OPTIONAL);
    {
        InPart ip(b, IP_FURNITURE);
        At at(b, vec3(-E, 2.9f, 0.f), -kHalfPi);   // west pier, facing +x
        vaultDoor(b, vec3(0.f, 0.f, 1.3f), 1.05f);
        collide(b, vec3(0.f, 0.2f, 1.3f), vec3(1.3f, 0.2f, 1.3f));
        light(b, vec3(0.f, 1.2f, 3.4f), vec3(1.f, 0.9f, 0.75f) * 260.f, 5.f, hall, normalize(vec3(0, -0.4f, -1.f)), 40.f, 20.f);
    }
    scenario(b, vec3(-E + 1.4f, 5.2f, 0.f), -kHalfPi + 0.4f, 19, SR_GUARD, SF_STAFF);
    // ---- lounge under the chandelier, facing the south glass; the grand piano by the window
    {
        const vec3 lc(0.f, 8.6f, 0.f);
        InPart ip(b, IP_FURNITURE);
        rug(b, lc + vec3(0.f, 0.3f, 0.f), 6.4f, 5.2f, r.next());
        sofa(b, lc + vec3(0.f, 2.1f, 0.f), kPi, 3.4f, C(0.86f, 0.83f, 0.78f), r.next());
        sofa(b, lc + vec3(-2.7f, 0.f, 0.f), -kHalfPi, 2.4f, C(0.86f, 0.83f, 0.78f), r.next());
        sofa(b, lc + vec3(2.7f, 0.f, 0.f), kHalfPi, 2.4f, C(0.86f, 0.83f, 0.78f), r.next());
        coffeeTable(b, lc + vec3(0.f, 0.1f, 0.f), 0.f, 1.8f, 1.0f, r.next());
        armchair(b, lc + vec3(-1.3f, -2.1f, 0.f), 0.3f, C(0.35f, 0.22f, 0.12f), r.next());
        armchair(b, lc + vec3(1.3f, -2.1f, 0.f), -0.3f, C(0.35f, 0.22f, 0.12f), r.next());
        champagneTable(b, lc + vec3(0.f, -2.3f, 0.f));
        floorLamp(b, lc + vec3(-3.1f, 2.2f, 0.f), hall, C(0.95f, 0.9f, 0.8f), 80.f);
        floorLamp(b, lc + vec3(3.1f, 2.2f, 0.f), hall, C(0.95f, 0.9f, 0.8f), 80.f);
        grandPiano(b, vec3(-8.2f, 4.2f, 0.f), 0.35f);
        scenario(b, lc + vec3(-2.6f, 0.4f, 0.f), -kHalfPi, 6, SR_VIP, SF_NIGHT | SF_OPTIONAL);
        scenario(b, lc + vec3(0.6f, 2.0f, 0.f), kPi, 6, SR_VIP, SF_NIGHT | SF_OPTIONAL);
        scenario(b, lc + vec3(2.6f, -0.4f, 0.f), kHalfPi, 6, SR_VIP, SF_OPTIONAL);
        scenario(b, vec3(-8.2f + sinf(0.35f) * -0.72f, 4.2f + cosf(0.35f) * 0.72f, 0.f), kPi + 0.35f, 6, SR_PATRON, SF_NIGHT | SF_OPTIONAL);
        chandelier(b, vec3(lc.x, lc.y, H), 2.8f, 1.1f, hall);
    }
    // ---- the Solaris Pier model mid-floor; sculptures and palms
    pierModel(b, vec3(0.f, 18.2f, 0.f), 0.f, hall, r.next());
    scenario(b, vec3(-1.2f, 16.6f, 0.f), 0.4f, 7, SR_VIP, SF_OPTIONAL);
    scenario(b, vec3(-0.4f, 16.4f, 0.f), -0.3f, 7, SR_VIP, SF_OPTIONAL | SF_NIGHT);
    {
        InPart ip(b, IP_FURNITURE);
        sculpture(b, vec3(-6.5f, 14.5f, 0.f), r.next());
        sculpture(b, vec3(6.5f, 22.f, 0.f), r.next());
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = 0; sy < 2; sy++) planter(b, vec3(sx * (E - 0.9f), sy ? D - 1.1f : 1.1f, 0.f), 2.2f, r.next());
    }
    // ---- Sandoval's desk facing the room with the city behind; bookshelves on the north piers, a globe
    {
        const float dy = D - 6.2f;
        InPart ip(b, IP_FURNITURE);
        rug(b, vec3(0.f, dy + 0.2f, 0.f), 5.6f, 4.6f, r.next());
        executiveDesk(b, vec3(0.f, dy, 0.f), 0.f, hall, r.next());
        execChair(b, vec3(0.f, dy + 1.0f, 0.f), kPi);
        for (int e = -1; e <= 1; e += 2) armchair(b, vec3(e * 0.95f, dy - 1.45f, 0.f), e * 0.25f, C(0.1f, 0.08f, 0.07f), r.next());
        globeStand(b, vec3(3.4f, dy + 1.6f, 0.f));
        // low credenza against the north glass with a bronze and a lamp
        box(b, vec3(0.f, D - 0.75f, 0.36f), vec3(1.6f, 0.25f, 0.36f), C(0.22f, 0.13f, 0.08f), M(MAT_WOOD), SK_NZ);
        collide(b, vec3(0.f, D - 0.75f, 0.36f), vec3(1.6f, 0.25f, 0.36f));
        tableLamp(b, vec3(-1.1f, D - 0.75f, 0.72f), hall, gold, C(0.92f, 0.88f, 0.8f), 50.f);
        lathe(b, vec3(0.6f, D - 0.75f, 0.72f), {vec2(0.1f, 0.f), vec2(0.14f, 0.2f), vec2(0.06f, 0.42f), vec2(0.1f, 0.55f)}, 14, C(0.45f, 0.3f, 0.16f), M(MAT_CHROME), true);
        bookshelf(b, vec3(E, D - Cc * 0.5f - g * 0.5f, 0.f), kHalfPi, Cc - 0.6f, 2.6f, C(0.2f, 0.12f, 0.07f), r.next());
        bookshelf(b, vec3(-E, D - Cc * 0.5f - g * 0.5f, 0.f), -kHalfPi, Cc - 0.6f, 2.6f, C(0.2f, 0.12f, 0.07f), r.next());
        scenario(b, vec3(0.f, dy + 1.0f, 0.f), kPi, 6, SR_VIP, 0);
        scenario(b, vec3(2.4f, dy - 0.6f, 0.f), -2.3f, 7, SR_RECEPTION, SF_STAFF | SF_DAY);
        light(b, vec3(0.f, dy - 0.3f, H - 0.3f), vec3(1.f, 0.9f, 0.72f) * 900.f, 8.f, hall, vec3(0, 0, -1), 40.f, 25.f);
    }
    // ---- east wing: bar at the north end, window seats and a telescope; TV on the south end
    {
        const float wx = (E + Hh - g) * 0.5f;
        backBar(b, vec3(wx, D - Cc, 0.f), kPi, Hh - g - E - 0.4f, wingE, r.next());
        barCounter(b, vec3(wx, D - Cc - 2.1f, 0.f), 0.f, 3.6f, wingE);
        InPart ip(b, IP_FURNITURE);
        for (int k = 0; k < 4; k++) barStool(b, vec3(wx - 1.3f + k * 0.87f, D - Cc - 2.75f, 0.f), C(0.08f, 0.07f, 0.07f));
        scenario(b, vec3(wx, D - Cc - 1.1f, 0.f), kPi, 0, SR_BARTENDER, SF_STAFF | SF_NIGHT);
        scenario(b, vec3(wx - 0.43f, D - Cc - 2.75f, 0.f), 0.f, 6, SR_VIP, SF_NIGHT | SF_OPTIONAL, 0.26f);
        for (int k = 0; k < 2; k++) {
            float y = 10.f + k * 3.2f;
            armchair(b, vec3(Hh - 1.4f, y, 0.f), -kHalfPi, C(0.1f, 0.08f, 0.07f), r.next());
            sideTable(b, vec3(Hh - 1.4f, y + 1.1f, 0.f), C(0.22f, 0.13f, 0.08f));
        }
        telescope(b, vec3(Hh - 1.1f, 17.6f, 0.f), -kHalfPi);
        At at(b, vec3(wx, Cc, 0.f), 0.f);   // TV on the south end wall of the wing, facing north
        wallTV(b, vec3(0.f, 0.f, 2.2f), 2.6f, wingE, r.next());
    }
    downlight(b, vec3((E + Hh - g) * 0.5f, 12.f, H), wingE, 650.f, vec3(1.f, 0.86f, 0.64f), 9.f);
    // ---- west wing: dining for ten under three pendants, sideboard and a large painting
    {
        const float wx = -(E + Hh - g) * 0.5f, yc = (Cc + D - Cc) * 0.5f;
        InPart ip(b, IP_FURNITURE);
        diningTable(b, vec3(wx, yc, 0.f), kHalfPi, 4.4f, 1.2f, C(0.16f, 0.1f, 0.06f), true, r.next());
        for (int k = 0; k < 4; k++) {
            float y = yc - 1.65f + k * 1.1f;
            chair(b, vec3(wx - 0.95f, y, 0.f), -kHalfPi, C(0.1f, 0.07f, 0.05f), 0, true);
            chair(b, vec3(wx + 0.95f, y, 0.f), kHalfPi, C(0.1f, 0.07f, 0.05f), 0, true);
        }
        chair(b, vec3(wx, yc - 2.55f, 0.f), 0.f, C(0.1f, 0.07f, 0.05f), 0, true);
        chair(b, vec3(wx, yc + 2.55f, 0.f), kPi, C(0.1f, 0.07f, 0.05f), 0, true);
        for (int k = -1; k <= 1; k++) pendant(b, vec3(wx, yc + k * 1.4f, H), H - 2.4f, wingW, gold, 160.f, vec3(1.f, 0.82f, 0.58f), 5.5f, 1);
        scenario(b, vec3(wx + 0.95f, yc - 0.55f, 0.f), kHalfPi, 6, SR_VIP, SF_NIGHT | SF_OPTIONAL);
        scenario(b, vec3(wx - 0.95f, yc + 0.55f, 0.f), -kHalfPi, 6, SR_VIP, SF_NIGHT | SF_OPTIONAL);
        At at(b, vec3(wx, D - Cc, 0.f), kPi);   // north end wall, facing south
        box(b, vec3(0.f, 0.25f, 0.42f), vec3(1.7f, 0.25f, 0.42f), C(0.2f, 0.12f, 0.07f), M(MAT_WOOD), SK_NZ | SK_NY);
        collide(b, vec3(0.f, 0.25f, 0.42f), vec3(1.7f, 0.25f, 0.42f));
        picture(b, vec3(0.f, 0.f, 2.5f), 3.2f, 2.0f, r.next(), gold);
        lathe(b, vec3(-1.2f, 0.25f, 0.84f), {vec2(0.08f, 0.f), vec2(0.13f, 0.12f), vec2(0.05f, 0.35f), vec2(0.08f, 0.45f)}, 12, Gy(0.95f), M(MAT_PAINT_WHITE), true);
        light(b, vec3(0.f, 1.3f, 4.2f), vec3(1.f, 0.9f, 0.75f) * 200.f, 5.f, wingW, normalize(vec3(0, -0.5f, -1.f)), 45.f, 25.f);
    }
    {
        // art on the remaining piers
        InPart ip(b, IP_FURNITURE);
        At at(b, vec3(E, 1.3f + g, 0.f), kHalfPi);   // south-east pier beside the cab, facing west
        picture(b, vec3(0.f, 0.f, 2.1f), 1.1f, 1.5f, r.next(), gold);
    }
    // ---- lights: warm downlight grid over the hall and the wings
    for (int j = 0; j < 5; j++)
        for (int i = 0; i < 3; i++) {
            vec3 lp(-E + 2.f * E * (i + 0.5f) / 3.f, g + (D - 2.f * g) * (j + 0.5f) / 5.f, H);
            if (length(lp.xy() - vec2(0.f, 8.6f)) < 3.f) continue;
            downlight(b, lp, hall, 950.f, vec3(1.f, 0.84f, 0.62f), 11.f);
        }
    downlight(b, vec3(-(E + Hh - g) * 0.5f, Cc + 3.f, H), wingW, 600.f, vec3(1.f, 0.84f, 0.62f), 9.f);
    downlight(b, vec3((E + Hh - g) * 0.5f, D - Cc - 3.f, H), wingE, 600.f, vec3(1.f, 0.84f, 0.62f), 9.f);
}

}  // namespace ikit
}  // namespace World
