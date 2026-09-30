// Residential towers: a ground-floor lobby (concierge desk, mailboxes, seating, the elevator with its cab) linked by
// the elevator to a unit upstairs (the Sol Beach Condo, the Downtown Penthouse on the top floor). The unit gets real
// windows in the facade's window grid, a sliding door onto the condo balcony (with collision, privacy screens and
// patio furniture), an open living room with kitchen and dining, a bedroom, a bathroom, a walk-in closet and the
// private elevator vestibule. Safehouse markers: IM_BED, IM_WARDROBE, IM_MIRROR.
#include "interiors.h"
#include "sites.h"

namespace World {
namespace ikit {

// Mailbox wall: a grid of small brass-numbered doors (front +y)
void mailboxes(IB& b, vec3 p, float yaw, int cols, int rows) {
    At at(b, p, yaw);
    float w = cols * 0.32f, h = rows * 0.2f;
    box(b, vec3(0.f, 0.06f, 1.1f + h * 0.5f), vec3(w * 0.5f + 0.03f, 0.06f, h * 0.5f + 0.03f), C(0.55f, 0.4f, 0.22f), M(MAT_METAL_BRUSHED), SK_NY);
    InPart ip(b, IP_DETAIL);
    for (int j = 0; j < rows; j++)
        for (int i = 0; i < cols; i++) {
            vec3 c(-w * 0.5f + 0.16f + i * 0.32f, 0.121f, 1.1f + 0.1f + j * 0.2f);
            box(b, c, vec3(0.145f, 0.002f, 0.085f), C(0.62f, 0.47f, 0.27f), M(MAT_CHROME), SK_NY);
            box(b, c + vec3(0.1f, 0.004f, 0.f), vec3(0.012f, 0.004f, 0.012f), Gy(0.2f), M(MAT_METAL_PAINTED), SK_NONE);
            box(b, c + vec3(-0.06f, 0.004f, 0.045f), vec3(0.04f, 0.001f, 0.012f), Gy(0.95f), M(MAT_PAINT_WHITE), SK_NONE);
        }
}

// Privacy screen of frosted glass in a steel frame (balcony dividers), plane x = const, y0..y1
void privacyScreen(IB& b, float x, float y0, float y1, float h) {
    u32 fr = Gy(0.25f), fm = M(MAT_METAL_PAINTED);
    box(b, vec3(x, (y0 + y1) * 0.5f, h), vec3(0.03f, (y1 - y0) * 0.5f, 0.03f), fr, fm, SK_NONE);
    box(b, vec3(x, y0 + 0.03f, h * 0.5f), vec3(0.03f, 0.03f, h * 0.5f), fr, fm, SK_PZ);
    box(b, vec3(x, y1 - 0.03f, h * 0.5f), vec3(0.03f, 0.03f, h * 0.5f), fr, fm, SK_PZ);
    quadF(b, vec3(x, y0, 0.05f), vec3(x, y1, 0.05f), vec3(x, y1, h), vec3(x, y0, h), vec3(1, 0, 0), glassCol(0.35f, vec3(0.9f, 0.95f, 0.95f)), kGlassMat);
    quadF(b, vec3(x, y0, 0.05f), vec3(x, y1, 0.05f), vec3(x, y1, h), vec3(x, y0, h), vec3(-1, 0, 0), glassCol(0.35f, vec3(0.9f, 0.95f, 0.95f)), kGlassMat);
    collideMM(b, vec3(x - 0.04f, y0, 0.f), vec3(x + 0.04f, y1, h));
}

// L-shaped sectional sofa (front +y): the main run along x (back at -y, an arm at the free end) and the return at the
// +x end (ret > 0) or the -x end (ret < 0), |ret| deep toward +y with its back on the outer side and an arm at its end
void sectional(IB& b, vec3 p, float yaw, float w, float ret, u32 fabric, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    InPart ip(b, IP_FURNITURE);
    const float D = 0.95f, armW = 0.2f, s = ret >= 0.f ? 1.f : -1.f, yEnd = Max(fabsf(ret), D + 0.6f) - D * 0.5f;
    const float xo = s * (w * 0.5f - D * 0.5f);   // centre line of the return
    const vec3 fc = rgbOf(fabric);
    const u32 fab = M(MAT_CLOTH), legC = C(0.1f, 0.09f, 0.08f);
    const vec2 legs[5] = {vec2(-s * (w * 0.5f - 0.08f), -D * 0.5f + 0.08f), vec2(-s * (w * 0.5f - 0.08f), D * 0.5f - 0.08f),
                          vec2(s * (w * 0.5f - 0.08f), -D * 0.5f + 0.08f), vec2(s * (w * 0.5f - 0.08f), yEnd - 0.08f), vec2(s * (w * 0.5f - D + 0.08f), yEnd - 0.08f)};
    for (const vec2& l : legs) cyl(b, vec3(l.x, l.y, 0.f), 0.02f, 0.026f, 0.1f, 8, legC, M(MAT_METAL_BRUSHED), false);
    // bases, backs, arms
    rbox(b, vec3(0.f, 0.f, 0.23f), vec3(w * 0.5f, D * 0.5f, 0.13f), 0.03f, fabric, fab, true);
    rbox(b, vec3(xo, (yEnd + D * 0.5f) * 0.5f, 0.23f), vec3(D * 0.5f, (yEnd - D * 0.5f) * 0.5f, 0.13f), 0.03f, fabric, fab, true);
    rbox(b, vec3(0.f, -D * 0.5f + 0.1f, 0.58f), vec3(w * 0.5f - 0.02f, 0.1f, 0.26f), 0.05f, fabric, fab);
    rbox(b, vec3(s * (w * 0.5f - 0.1f), (yEnd - D * 0.5f) * 0.5f, 0.58f), vec3(0.1f, (yEnd + D * 0.5f) * 0.5f - 0.02f, 0.26f), 0.05f, fabric, fab);
    rbox(b, vec3(-s * (w * 0.5f - armW * 0.5f), 0.02f, 0.45f), vec3(armW * 0.5f, D * 0.5f - 0.01f, 0.2f), 0.07f, fabric, fab);
    rbox(b, vec3(xo, yEnd - armW * 0.5f, 0.45f), vec3(D * 0.5f - 0.01f, armW * 0.5f, 0.2f), 0.07f, fabric, fab);
    // seat and back cushions along the main run, then along the return (which takes the corner seat)
    auto backCushion = [&](vec3 c, vec2 f, float hw) {
        b.pushAxes(c, vec3(f.y, -f.x, 0.f), normalize(vec3(f.x, f.y, 0.2f)), normalize(vec3(-f.x * 0.2f, -f.y * 0.2f, 1.f)));
        rbox(b, vec3(0.f), vec3(hw, 0.09f, 0.2f), 0.07f, C(fc * r.range(0.95f, 1.04f)), fab, true);
        b.pop();
    };
    const float lo = s > 0.f ? -(w * 0.5f - armW) : -(w * 0.5f - D), hi = s > 0.f ? w * 0.5f - D : w * 0.5f - armW;
    const int n = Max(1, (int)roundf((hi - lo) / 0.8f));
    const float cw = (hi - lo) / n;
    for (int k = 0; k < n; k++) {
        float x = lo + cw * (k + 0.5f);
        rbox(b, vec3(x, 0.07f, 0.44f), vec3(cw * 0.5f - 0.006f, D * 0.5f - 0.15f, 0.085f), 0.05f, C(fc * r.range(0.95f, 1.03f)), fab);
        backCushion(vec3(x, -D * 0.5f + 0.27f, 0.68f), vec2(0.f, 1.f), cw * 0.5f - 0.008f);
    }
    const float ya = -D * 0.5f + 0.2f, yb = yEnd - armW;
    const int m = Max(1, (int)roundf((yb - ya) / 0.8f));
    const float ch = (yb - ya) / m;
    for (int k = 0; k < m; k++) {
        float y = ya + ch * (k + 0.5f);
        rbox(b, vec3(xo - s * 0.07f, y, 0.44f), vec3(D * 0.5f - 0.15f, ch * 0.5f - 0.006f, 0.085f), 0.05f, C(fc * r.range(0.95f, 1.03f)), fab);
        backCushion(vec3(s * (w * 0.5f - 0.27f), y, 0.68f), vec2(-s, 0.f), ch * 0.5f - 0.008f);
    }
    // throw pillows: two in the corner, one at the free end
    for (int k = 0; k < 3; k++) {
        vec3 pc = hsv(r.f(), r.range(0.2f, 0.5f), r.range(0.45f, 0.8f));
        vec2 f = k == 1 ? vec2(-s, 0.f) : vec2(0.f, 1.f);
        vec3 c = k == 0 ? vec3(s * (w * 0.5f - D - 0.15f), -D * 0.5f + 0.4f, 0.66f)
                        : (k == 1 ? vec3(s * (w * 0.5f - 0.4f), -D * 0.5f + 0.55f, 0.66f) : vec3(-s * (w * 0.5f - armW - 0.25f), -D * 0.5f + 0.4f, 0.66f));
        vec3 ax = vec3(f.y, -f.x, 0.f), ay = normalize(vec3(f.x, f.y, 0.45f));
        b.pushAxes(c, ax, ay, normalize(cross(ax, ay)));
        rbox(b, vec3(0.f), vec3(0.2f, 0.07f, 0.2f), 0.06f, C(pc), fab, true);
        b.pop();
    }
    collide(b, vec3(0.f, 0.f, 0.4f), vec3(w * 0.5f, D * 0.5f, 0.4f));
    collide(b, vec3(xo, (yEnd + D * 0.5f) * 0.5f, 0.4f), vec3(D * 0.5f, (yEnd - D * 0.5f) * 0.5f, 0.4f));
}

// Low sideboard against a wall (front +y): lacquered doors with brass pulls, a stone top, a table lamp, vases, a book
// stack with a bowl
void sideboard(IB& b, vec3 p, float yaw, float w, u32 col, u32 top, int roomIdx, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    InPart ip(b, IP_FURNITURE);
    const float D = 0.45f, Ht = 0.74f;
    const u32 brass = C(0.8f, 0.62f, 0.32f);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) cyl(b, vec3(sx * (w * 0.5f - 0.07f), sy * (D * 0.5f - 0.07f), 0.f), 0.014f, 0.018f, 0.1f, 8, brass, M(MAT_CHROME), false);
    rbox(b, vec3(0.f, 0.f, (0.1f + Ht - 0.02f) * 0.5f), vec3(w * 0.5f, D * 0.5f, (Ht - 0.12f) * 0.5f), 0.01f, col, M(MAT_PLASTIC), true);
    const int nd = w > 1.5f ? 4 : 3;
    for (int k = 0; k < nd; k++) {
        float x = -w * 0.5f + w * (k + 0.5f) / nd;
        box(b, vec3(x, D * 0.5f + 0.004f, (0.1f + Ht - 0.02f) * 0.5f), vec3(w * 0.5f / nd - 0.008f, 0.004f, (Ht - 0.12f) * 0.5f - 0.02f), C(rgbOf(col) * 1.1f), M(MAT_PLASTIC),
            SK_NZ | SK_NY);
        box(b, vec3(x, D * 0.5f + 0.014f, Ht - 0.1f), vec3(0.07f, 0.006f, 0.006f), brass, M(MAT_CHROME), SK_NY);
    }
    rbox(b, vec3(0.f, 0.01f, Ht), vec3(w * 0.5f + 0.02f, D * 0.5f + 0.02f, 0.02f), 0.005f, top, M(MAT_MARBLE), true);
    collide(b, vec3(0.f, 0.f, Ht * 0.5f), vec3(w * 0.5f, D * 0.5f, Ht * 0.5f));
    const float z = Ht + 0.02f;
    tableLamp(b, vec3(-w * 0.5f + 0.28f, 0.f, z), roomIdx, C(0.85f, 0.8f, 0.7f), C(0.95f, 0.9f, 0.8f), 50.f);
    vec3 vc = hsv(r.range(0.02f, 0.12f), r.range(0.1f, 0.4f), r.range(0.3f, 0.9f));
    lathe(b, vec3(0.05f, 0.03f, z), {vec2(0.05f, 0.f), vec2(0.1f, 0.12f), vec2(0.08f, 0.28f), vec2(0.035f, 0.38f), vec2(0.045f, 0.42f)}, 14, C(vc), M(MAT_PAINT_WHITE), false);
    lathe(b, vec3(0.24f, -0.04f, z), {vec2(0.04f, 0.f), vec2(0.07f, 0.08f), vec2(0.03f, 0.2f), vec2(0.035f, 0.23f)}, 12, C(vc * 0.6f), M(MAT_PAINT_WHITE), false);
    for (int k = 0; k < 3; k++)
        box(b, vec3(w * 0.5f - 0.3f + r.range(-0.02f, 0.02f), 0.f, z + 0.02f + k * 0.04f), vec3(0.14f - k * 0.015f, 0.1f - k * 0.01f, 0.02f),
            C(hsv(r.f(), r.range(0.2f, 0.5f), r.range(0.3f, 0.8f))), M(MAT_FABRIC), k ? SK_NZ : SK_NONE);
    lathe(b, vec3(w * 0.5f - 0.3f, 0.f, z + 0.12f), {vec2(0.03f, 0.f), vec2(0.1f, 0.05f), vec2(0.11f, 0.06f)}, 14, C(0.12f, 0.1f, 0.09f), M(MAT_PAINT_WHITE), false);
}

// Brass bar cart (front +y): two glass shelves on a tube frame with casters and a handle, bottles, a decanter,
// glasses and an ice bucket
void barCart(IB& b, vec3 p, float yaw, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    InPart ip(b, IP_FURNITURE);
    const float w = 0.8f, dp = 0.44f, hx = w * 0.5f - 0.02f, hy = dp * 0.5f - 0.02f;
    const u32 brass = C(0.8f, 0.62f, 0.32f), bm = M(MAT_CHROME);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            tube(b, vec3(sx * hx, sy * hy, 0.07f), vec3(sx * hx, sy * hy, 0.84f), 0.011f, 6, brass, bm);
            sphere(b, vec3(sx * hx, sy * hy, 0.035f), 0.035f, 8, Gy(0.08f), M(MAT_RUBBER));
        }
    for (int sy = -1; sy <= 1; sy += 2) tube(b, vec3(-hx, sy * hy, 0.8f), vec3(-hx - 0.1f, sy * hy, 0.84f), 0.01f, 6, brass, bm);
    tube(b, vec3(-hx - 0.1f, -hy, 0.84f), vec3(-hx - 0.1f, hy, 0.84f), 0.012f, 6, brass, bm, true);
    for (float z : {0.3f, 0.76f}) {
        box(b, vec3(0.f, 0.f, z), vec3(hx, hy, 0.005f), C(0.8f, 0.9f, 0.9f, 1.f), M(MAT_GLASS), SK_NONE);
        for (int sy = -1; sy <= 1; sy += 2) tube(b, vec3(-hx, sy * hy, z + 0.02f), vec3(hx, sy * hy, z + 0.02f), 0.008f, 6, brass, bm);
        for (int sx = -1; sx <= 1; sx += 2) tube(b, vec3(sx * hx, -hy, z + 0.02f), vec3(sx * hx, hy, z + 0.02f), 0.008f, 6, brass, bm);
    }
    for (int k = 0; k < 4; k++) {
        vec3 bc = hsv(r.range(0.02f, 0.14f), r.range(0.3f, 0.9f), r.range(0.2f, 0.7f));
        if (k == 3) bc = vec3(0.05f, 0.2f, 0.08f);
        bottle(b, vec3(-0.26f + k * 0.12f, -0.08f + r.range(-0.03f, 0.03f), 0.765f), r.range(0.03f, 0.04f), r.range(0.24f, 0.32f), C(bc, 1.f), M(MAT_GLASS),
               r.chance(0.5f) ? brass : Gy(0.1f), 8);
    }
    lathe(b, vec3(0.25f, 0.08f, 0.765f), {vec2(0.05f, 0.f), vec2(0.08f, 0.07f), vec2(0.03f, 0.16f), vec2(0.018f, 0.2f)}, 12, C(0.75f, 0.45f, 0.15f, 1.f), M(MAT_GLASS), false);
    sphere(b, vec3(0.25f, 0.08f, 0.985f), 0.028f, 8, C(0.9f, 0.95f, 0.95f, 1.f), M(MAT_GLASS));
    for (int k = 0; k < 4; k++)
        lathe(b, vec3(-0.25f + k * 0.1f, 0.1f, 0.765f), {vec2(0.03f, 0.f), vec2(0.035f, 0.02f), vec2(0.04f, 0.09f)}, 8, C(0.9f, 0.95f, 0.95f, 1.f), M(MAT_GLASS), false);
    lathe(b, vec3(0.f, 0.f, 0.305f), {vec2(0.08f, 0.f), vec2(0.1f, 0.18f), vec2(0.105f, 0.19f)}, 14, Gy(0.75f), M(MAT_METAL_BRUSHED), false);
    for (int k = 0; k < 2; k++) bottle(b, vec3(0.22f, -0.06f + k * 0.12f, 0.305f), 0.038f, 0.3f, C(0.25f, 0.4f, 0.2f, 1.f), M(MAT_GLASS), brass, 8);
    collide(b, vec3(0.f, 0.f, 0.45f), vec3(w * 0.5f, dp * 0.5f, 0.45f));
}

// Large low-pile area rug with a border band (centre c on the floor, w along x, dp along y)
void areaRug(IB& b, vec3 c, float w, float dp, u32 base, u32 border) {
    InPart ip(b, IP_FURNITURE);
    box(b, c + vec3(0.f, 0.f, 0.005f), vec3(w * 0.5f, dp * 0.5f, 0.005f), border, M(MAT_CARPET), SK_NZ);
    box(b, c + vec3(0.f, 0.f, 0.0105f), vec3(w * 0.5f - 0.16f, dp * 0.5f - 0.16f, 0.0005f), base, M(MAT_CARPET), SK_NZ);
}

// Feature wall (local frame: wall face at y = 0, cladding toward +y, x along the wall centred, height h): style 0
// dark book-matched stone slabs with brass reveals, style 1 lacquered oak slats on a black backing; grazer spots
// along the top wash it
void featureWall(IB& b, float w, float h, int style, int roomIdx) {
    InPart ip(b, IP_SHELL);
    if (style == 0) {
        const int n = Max(2, (int)roundf(w / 1.2f));
        const float sw = w / n;
        for (int k = 0; k < n; k++) {
            float x = -w * 0.5f + sw * (k + 0.5f);
            box(b, vec3(x, 0.02f, h * 0.5f), vec3(sw * 0.5f - 0.005f, 0.02f, h * 0.5f), C(k & 1 ? vec3(0.17f, 0.16f, 0.16f) : vec3(0.19f, 0.18f, 0.17f)), M(MAT_MARBLE), SK_NZ);
            if (k) box(b, vec3(x - sw * 0.5f, 0.012f, h * 0.5f), vec3(0.006f, 0.012f, h * 0.5f), C(0.8f, 0.62f, 0.32f), M(MAT_CHROME), SK_NZ);
        }
    } else {
        box(b, vec3(0.f, 0.006f, h * 0.5f), vec3(w * 0.5f, 0.006f, h * 0.5f), Gy(0.03f), M(MAT_PAINT_WHITE), SK_NZ);
        for (float x = -w * 0.5f + 0.05f; x < w * 0.5f - 0.03f; x += 0.1f)
            box(b, vec3(x, 0.03f, h * 0.5f), vec3(0.022f, 0.018f, h * 0.5f), C(0.55f, 0.38f, 0.22f), M(MAT_PLASTIC), SK_NZ);
    }
    box(b, vec3(0.f, 0.05f, h + 0.01f), vec3(w * 0.5f, 0.05f, 0.01f), Gy(0.9f), M(MAT_PAINT_WHITE), SK_NONE);
    const int nl = Max(2, (int)(w / 2.2f));
    for (int k = 0; k < nl; k++)
        light(b, vec3(-w * 0.5f + w * (k + 0.5f) / nl, 0.35f, h - 0.05f), vec3(1.f, 0.82f, 0.6f) * 160.f, 4.5f, roomIdx, normalize(vec3(0.f, -0.3f, -1.f)), 55.f, 30.f);
}

// ------------------------------------------------------------------------------------------------ tower lobby
void layoutResLobby(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0x10B8u);
    const float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT, H = d.ceil;
    float dx0 = 0.f, dx1 = 0.f, dh = 2.2f;
    mainDoorOpening(d, dx0, dx1, dh);
    const float doorX = (dx0 + dx1) * 0.5f;
    const float yE = Y1 - 2.45f;                                      // back wall of the hall (elevator wall)
    const float cx = Clamp((X0 + X1) * 0.5f + (doorX > (X0 + X1) * 0.5f ? -1.5f : 1.5f), X0 + 1.4f, X1 - 1.4f);
    const float cabX0 = cx - 1.0f, cabX1 = cx + 1.0f, cabY0 = yE + 0.075f, cabY1 = cabY0 + 2.05f, cabH = 2.6f;
    int hall = room(b, vec3(X0, Y0, 0.f), vec3(X1, yE - 0.075f, H), vec3(44.f, 42.f, 38.f), 0.18f, LS_ALWAYS);
    int cab = room(b, vec3(cabX0, cabY0, 0.f), vec3(cabX1, cabY1, cabH), vec3(46.f, 42.f, 36.f), 0.f, LS_ALWAYS);
    storefrontEntrance(b, DK_HINGED_PAIR, 3, C(0.2f, 0.14f, 0.08f));
    door(b, vec3(cx, yE, 0.f), vec2(1, 0), vec2(0, 1), 1.1f, 2.25f, DK_ELEVATOR, 5, Gy(0.7f), false);
    marker(b, IM_ELEVATOR, vec3(cx, (cabY0 + cabY1) * 0.5f, 0.f), kPi);
    ShellStyle hs;
    hs.wallCol = C(0.9f, 0.88f, 0.83f);
    hs.floorMat = M(MAT_MARBLE);
    hs.floorCol = C(0.9f, 0.88f, 0.84f);
    hs.floorUV = 0.3f;
    hs.ceilMat = M(MAT_PLASTER);
    hs.ceilCol = Gy(0.94f);
    hs.wainscotH = 1.1f;
    hs.wainCol = C(0.36f, 0.24f, 0.14f);
    hs.wainMat = M(MAT_WOOD);
    hs.baseH = 0.12f;
    hs.baseCol = C(0.2f, 0.14f, 0.09f);
    hs.baseMat = M(MAT_WOOD);
    hs.crownH = 0.12f;
    hs.revealCol = Gy(0.85f);
    shell(b, hall, hs);
    ShellStyle cs;
    cs.wallCol = C(0.62f, 0.62f, 0.64f);
    cs.wallMat = M(MAT_METAL_BRUSHED);
    cs.floorMat = M(MAT_STONE);
    cs.floorCol = C(0.18f, 0.17f, 0.17f);
    cs.ceilMat = M(MAT_METAL_BRUSHED);
    cs.ceilCol = Gy(0.35f);
    cs.baseH = 0.f;
    shell(b, cab, cs);
    elevatorCab(b, cab, 0, "PH");
    partitionX(b, X0, X1, yE, 0.15f, H, {vec2(cx - 0.55f, cx + 0.55f)});
    partitionY(b, cabY0, cabY1 + 0.1f, cabX0 - 0.05f, 0.1f, cabH);
    partitionY(b, cabY0, cabY1 + 0.1f, cabX1 + 0.05f, 0.1f, cabH);
    partitionX(b, cabX0, cabX1, cabY1 + 0.05f, 0.1f, cabH);
    {
        // brushed steel surround, floor indicator and call button around the elevator
        InPart ip(b, IP_SHELL);
        At at(b, vec3(cx, yE - 0.075f, 0.f), kPi);   // local +y into the hall
        for (int e = -1; e <= 1; e += 2) box(b, vec3(e * 0.66f, 0.03f, 1.15f), vec3(0.1f, 0.03f, 1.15f), Gy(0.7f), M(MAT_METAL_BRUSHED), SK_NY);
        box(b, vec3(0.f, 0.03f, 2.4f), vec3(0.76f, 0.03f, 0.08f), Gy(0.7f), M(MAT_METAL_BRUSHED), SK_NY);
        box(b, vec3(0.f, 0.02f, 2.7f), vec3(0.22f, 0.02f, 0.08f), Gy(0.05f), M(MAT_PLASTIC), SK_NY);
        textC(b, "PH", vec3(0.f, 0.041f, 2.66f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.08f, 0.012f, C(1.f, 0.5f, 0.15f, 0.8f), EM());
        box(b, vec3(0.9f, 0.025f, 1.15f), vec3(0.05f, 0.025f, 0.1f), Gy(0.7f), M(MAT_METAL_BRUSHED), SK_NY);
        b.pushAxes(vec3(0.9f, 0.05f, 1.17f), vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0));
        cyl(b, vec3(0.f), 0.025f, 0.025f, 0.01f, 12, C(1.f, 0.9f, 0.6f, 0.7f), EM(), true, false);
        b.pop();
    }
    // concierge desk facing the door, mailboxes on the side wall, seating, plants, art
    {
        float kx = Clamp(doorX + (doorX > (X0 + X1) * 0.5f ? -2.6f : 2.6f), X0 + 1.3f, X1 - 1.3f), ky = Y0 + 3.4f;
        if (fabsf(kx - cx) < 1.6f && ky > yE - 2.5f) ky = yE - 2.5f;
        At at(b, vec3(kx, ky, 0.f), 0.f);
        InPart ip(b, IP_FURNITURE);
        rbox(b, vec3(0.f, 0.f, 0.55f), vec3(1.0f, 0.32f, 0.55f), 0.03f, C(0.3f, 0.2f, 0.12f), M(MAT_WOOD), true);
        rbox(b, vec3(0.f, 0.02f, 1.12f), vec3(1.05f, 0.38f, 0.02f), 0.01f, C(0.12f, 0.12f, 0.12f), M(MAT_MARBLE), true);
        collide(b, vec3(0.f, 0.f, 0.57f), vec3(1.05f, 0.38f, 0.57f));
        tableLamp(b, vec3(-0.65f, 0.1f, 1.14f), hall, C(0.8f, 0.62f, 0.3f), C(0.95f, 0.9f, 0.8f), 40.f);
        officeChair(b, vec3(0.f, 0.8f, 0.f), kPi, C(0.1f, 0.1f, 0.12f));
        scenario(b, vec3(0.f, 0.8f, 0.f), kPi, 6, SR_RECEPTION, SF_STAFF);
    }
    {
        float mx = doorX > (X0 + X1) * 0.5f ? X1 : X0;
        At at(b, vec3(mx, (Y0 + yE) * 0.5f + 0.6f, 0.f), doorX > (X0 + X1) * 0.5f ? kHalfPi : -kHalfPi);
        mailboxes(b, vec3(0.f), 0.f, 6, 5);
    }
    {
        float sx = doorX > (X0 + X1) * 0.5f ? X0 + 0.55f : X1 - 0.55f;
        InPart ip(b, IP_FURNITURE);
        sofa(b, vec3(sx, (Y0 + yE) * 0.5f + 0.3f, 0.f), doorX > (X0 + X1) * 0.5f ? -kHalfPi : kHalfPi, 2.0f, C(0.3f, 0.36f, 0.34f), r.next());
        scenario(b, vec3(sx, (Y0 + yE) * 0.5f, 0.f), doorX > (X0 + X1) * 0.5f ? -kHalfPi : kHalfPi, 6, SR_PATRON, SF_OPTIONAL);
        At at(b, vec3(doorX > (X0 + X1) * 0.5f ? X0 : X1, (Y0 + yE) * 0.5f + 0.3f, 0.f), doorX > (X0 + X1) * 0.5f ? -kHalfPi : kHalfPi);
        picture(b, vec3(0.f, 0.f, 1.85f), 1.3f, 0.9f, r.next());
    }
    planter(b, vec3(X0 + 0.55f, yE - 0.55f, 0.f), 1.6f, r.next());
    planter(b, vec3(X1 - 0.55f, yE - 0.55f, 0.f), 1.6f, r.next());
    for (int k = 0; k < 2; k++) pendant(b, vec3((X0 + X1) * 0.5f + (k ? 1.8f : -1.8f), (Y0 + yE) * 0.5f, H), 0.7f, hall, C(0.8f, 0.62f, 0.3f), 260.f, vec3(1.f, 0.85f, 0.62f), 6.f, 1);
    downlight(b, vec3(cx, yE - 1.2f, H), hall, 320.f);
    downlight(b, vec3(doorX, Y0 + 1.4f, H), hall, 320.f);
    roomDressing(b, hall, true, d.seed ^ 0x51u);
}

// ------------------------------------------------------------------------------------------------ condo unit
void layoutCondoUnit(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0xC0D0u);
    const float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT, H = d.ceil;
    const Building* bld = layoutBuilding(d);
    const bool balcony = bld && bld->style == BS_CONDO;
    const bool top = bld && d.origin.z + d.shellTop + 0.5f > bld->baseZ + bld->height;   // penthouse: the top floor
    // ---- plan: facade windows of this floor (the shader's window grid), the balcony door in the living room
    const float pt = 0.12f;
    float xbRaw = X0 + Clamp((X1 - X0) * 0.34f, 3.8f, 5.4f);
    float xb = d.bayX0 + roundf((xbRaw - d.bayX0) / d.bw) * d.bw;       // bedroom | living on a bay boundary
    if (xb < X0 + 3.4f || xb > X1 - 5.f) xb = xbRaw;
    const float yS = Y1 - 2.55f;                                         // back strip: bath, cab, closet
    {
        const FacadeGPU* f = bld ? &layoutBuildings()->facades[bld->facade] : nullptr;
        float winW = d.bw * (f ? f->winW : 0.6f), sill = (f ? f->sillH : 0.9f) - 0.12f, winH = (f ? f->floorH * f->winH : 1.6f);
        sill = Max(sill, 0.05f);
        float zTop = Min(sill + winH, H - 0.08f);
        int first = (int)roundf((d.x0 - d.bayX0) / d.bw), last = (int)roundf((d.x1 - d.bayX0) / d.bw) - 1;
        int doorBay = (int)floorf(((xb + X1) * 0.5f - d.bayX0) / d.bw);
        for (int k = first; k <= last; k++) {
            float wx0 = d.bayX0 + k * d.bw + (d.bw - winW) * 0.5f, wx1 = wx0 + winW;
            if (balcony && k == doorBay) openingLocal(b, vec2(wx0, 0.f), vec2(wx1, 0.f), 0.f, Min(2.35f, H - 0.1f), OP_DOOR);
            else if (zTop - sill > 0.4f) openingLocal(b, vec2(wx0, 0.f), vec2(wx1, 0.f), sill, zTop, OP_GLASS);
        }
    }
    const float cabX0 = xb + pt * 0.5f + 0.35f, cabX1 = cabX0 + 2.0f, cabY0 = yS + pt * 0.5f, cabY1 = cabY0 + 2.05f;
    const bool closet = X1 - (cabX1 + pt) > 1.9f;
    const float closX1 = closet ? Min(X1, cabX1 + pt + 2.8f) : cabX1;
    const bool alcove = closet && X1 - (closX1 + pt) > 3.2f;           // wide units: kitchen alcove behind the living room
    const float alX0 = closX1 + pt;
    const vec3 warm(24.f, 21.f, 17.f);
    int living = room(b, vec3(xb + pt * 0.5f, Y0, 0.f), vec3(X1, yS - pt * 0.5f, H), warm, 0.3f, LS_EVENING);
    int bedroom = room(b, vec3(X0, Y0, 0.f), vec3(xb - pt * 0.5f, yS - pt * 0.5f, H), warm, 0.25f, LS_EVENING);
    int bath = room(b, vec3(X0, yS + pt * 0.5f, 0.f), vec3(xb - pt * 0.5f, Y1, H), vec3(28.f, 27.f, 25.f), 0.f, LS_EVENING);
    int cab = room(b, vec3(cabX0, cabY0, 0.f), vec3(cabX1, cabY1, 2.6f), vec3(46.f, 42.f, 36.f), 0.f, LS_ALWAYS);
    int clos = closet ? room(b, vec3(cabX1 + pt, yS + pt * 0.5f, 0.f), vec3(closX1, Y1, H), vec3(26.f, 24.f, 21.f), 0.f, LS_EVENING) : -1;
    int kit = alcove ? room(b, vec3(alX0, yS + pt * 0.5f, 0.f), vec3(X1, Y1, H), warm, 0.f, LS_EVENING) : -1;
    // doors
    const float bdY = Y0 + (yS - Y0) * 0.72f, baX = (X0 + xb) * 0.5f, clX = closet ? (cabX1 + pt + closX1) * 0.5f : 0.f;
    if (alcove) {
        // wide opening between the living room and the kitchen alcove
        const InteriorRoom& rl = d.rooms[living];
        const InteriorRoom& rk = d.rooms[kit];
        tExtraHoles.push_back({living, 2, {alX0 + 0.3f - rl.mn.x, X1 - 0.3f - rl.mn.x, 0.f, Min(2.45f, H - 0.15f)}});
        tExtraHoles.push_back({kit, 0, {rk.mx.x - (X1 - 0.3f), rk.mx.x - (alX0 + 0.3f), 0.f, Min(2.45f, H - 0.15f)}});
    }
    door(b, vec3(xb, bdY, 0.f), vec2(0, 1), vec2(-1, 0), 0.9f, 2.1f, DK_HINGED, 0, Gy(0.95f), false);
    door(b, vec3(baX, yS, 0.f), vec2(1, 0), vec2(0, 1), 0.8f, 2.1f, DK_HINGED, 0, Gy(0.95f), false);
    door(b, vec3((cabX0 + cabX1) * 0.5f, yS, 0.f), vec2(1, 0), vec2(0, 1), 1.1f, 2.25f, DK_ELEVATOR, 5, Gy(0.7f), false);
    if (closet) door(b, vec3(clX, yS, 0.f), vec2(1, 0), vec2(0, 1), 0.85f, 2.1f, DK_HINGED, 0, Gy(0.95f), false);
    int balDoor = -1;
    for (const InteriorOpening& op : d.openings)
        if (op.kind == OP_DOOR) {
            vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
            balDoor = door(b, vec3((a.x + c.x) * 0.5f, 0.f, 0.f), vec2(1, 0), vec2(0, 1), fabsf(c.x - a.x), op.z1 - op.z0, DK_SLIDING_PAIR, 1, Gy(0.25f), true, 0.14f);
        }
    (void)balDoor;
    marker(b, IM_ELEVATOR_TOP, vec3((cabX0 + cabX1) * 0.5f, (cabY0 + cabY1) * 0.5f, 0.f), kPi);
    marker(b, IM_ENTRY, vec3((cabX0 + cabX1) * 0.5f, yS - 1.2f, 0.f), kPi);
    // ---- shells
    const u32 wood = top ? C(0.2f, 0.13f, 0.08f) : C(0.52f, 0.38f, 0.24f);
    ShellStyle ls;
    ls.wallCol = top ? C(0.9f, 0.89f, 0.86f) : C(0.94f, 0.92f, 0.87f);
    ls.floorMat = M(MAT_WOOD_FLOOR);
    ls.floorCol = top ? C(0.42f, 0.3f, 0.2f) : C(0.8f, 0.68f, 0.52f);
    ls.ceilMat = M(MAT_PLASTER);
    ls.ceilCol = Gy(0.96f);
    ls.baseH = 0.1f;
    ls.baseCol = Gy(0.95f);
    ls.crownH = top ? 0.1f : 0.f;
    ls.revealCol = Gy(0.92f);
    shell(b, living, ls);
    shell(b, bedroom, ls);
    ShellStyle bs2;
    bs2.wallCol = Gy(0.93f);
    bs2.wainscotH = 1.25f;
    bs2.wainCol = top ? C(0.2f, 0.2f, 0.22f) : C(0.75f, 0.85f, 0.88f);
    bs2.wainMat = M(MAT_TILE);
    bs2.floorMat = M(MAT_TILE);
    bs2.floorCol = top ? C(0.25f, 0.25f, 0.27f) : C(0.85f, 0.85f, 0.83f);
    bs2.floorUV = 1.3f;
    bs2.ceilMat = M(MAT_PLASTER);
    bs2.ceilCol = Gy(0.96f);
    bs2.baseH = 0.f;
    shell(b, bath, bs2);
    ShellStyle cs;
    cs.wallCol = C(0.55f, 0.42f, 0.27f);
    cs.wallMat = M(MAT_METAL_BRUSHED);
    cs.floorMat = M(MAT_STONE);
    cs.floorCol = C(0.12f, 0.12f, 0.12f);
    cs.ceilMat = M(MAT_METAL_BRUSHED);
    cs.ceilCol = Gy(0.3f);
    cs.baseH = 0.f;
    shell(b, cab, cs);
    elevatorCab(b, cab, 0, "L");
    if (closet) shell(b, clos, ls);
    if (alcove) {
        ShellStyle ks = ls;
        ks.floorMat = M(MAT_TILE);
        ks.floorCol = top ? C(0.2f, 0.2f, 0.21f) : C(0.86f, 0.85f, 0.82f);
        ks.floorUV = 0.8f;
        shell(b, kit, ks);
        InPart ip(b, IP_SHELL);
        reveal(b, vec3(alX0 + 0.3f, yS - pt * 0.5f, 0.f), vec3(1, 0, 0), vec3(0, 1, 0), 0.f, X1 - alX0 - 0.6f, 0.f, Min(2.45f, H - 0.15f), 0.f, pt, Gy(0.95f), M(MAT_PAINT_WHITE), false);
    }
    doorFrameInner(b, vec3(xb, bdY, 0.f), vec2(0, 1), vec2(-1, 0), 0.9f, 2.1f, pt, Gy(0.95f));
    doorFrameInner(b, vec3(baX, yS, 0.f), vec2(1, 0), vec2(0, 1), 0.8f, 2.1f, pt, Gy(0.95f));
    if (closet) doorFrameInner(b, vec3(clX, yS, 0.f), vec2(1, 0), vec2(0, 1), 0.85f, 2.1f, pt, Gy(0.95f));
    glazeAll(b, 0.93f, top ? Gy(0.15f) : Gy(0.85f), false);
    partitionY(b, Y0, Y1, xb, pt, H, {vec2(bdY - 0.45f, bdY + 0.45f)});
    if (closet && alcove)
        partitionX(b, X0, X1, yS, pt, H, {vec2(baX - 0.4f, baX + 0.4f), vec2((cabX0 + cabX1) * 0.5f - 0.55f, (cabX0 + cabX1) * 0.5f + 0.55f), vec2(clX - 0.43f, clX + 0.43f),
                                          vec2(alX0 + 0.3f, X1 - 0.3f)});
    else if (closet) partitionX(b, X0, X1, yS, pt, H, {vec2(baX - 0.4f, baX + 0.4f), vec2((cabX0 + cabX1) * 0.5f - 0.55f, (cabX0 + cabX1) * 0.5f + 0.55f), vec2(clX - 0.43f, clX + 0.43f)});
    else partitionX(b, X0, X1, yS, pt, H, {vec2(baX - 0.4f, baX + 0.4f), vec2((cabX0 + cabX1) * 0.5f - 0.55f, (cabX0 + cabX1) * 0.5f + 0.55f)});
    partitionY(b, yS, cabY1 + 0.1f, cabX0 - 0.05f, 0.1f, 2.7f);
    partitionY(b, yS, Y1, cabX1 + pt * 0.5f, pt, H);
    if (alcove) partitionY(b, yS, Y1, closX1 + pt * 0.5f, pt, H);
    partitionX(b, cabX0, cabX1, cabY1 + 0.05f, 0.1f, 2.7f);
    // ---- balcony: collision for the slab and railing (buildmesh draws them), privacy screens, patio furniture
    if (balcony) {
        float hxB = bld->hx * 0.92f;
        float bx0 = Max(d.x0, -hxB), bx1 = Min(d.x1, hxB);
        collideMM(b, vec3(bx0, -1.6f, -0.3f), vec3(bx1, 0.f, 0.f));
        collideMM(b, vec3(bx0, -1.62f, 0.f), vec3(bx1, -1.5f, 1.05f));
        InPart ip(b, IP_FURNITURE);
        privacyScreen(b, bx0 + 0.04f, -1.52f, -0.02f, 2.1f);
        privacyScreen(b, bx1 - 0.04f, -1.52f, -0.02f, 2.1f);
        float px = Clamp((xb + X1) * 0.5f + 1.6f, bx0 + 0.6f, bx1 - 0.6f);
        patioChair(b, vec3(px, -0.8f, 0.f), kPi, Gy(0.95f));
        patioChair(b, vec3(px - 1.3f, -0.8f, 0.f), kPi, Gy(0.95f));
        sideTable(b, vec3(px - 0.65f, -0.75f, 0.f), Gy(0.9f));
        plant(b, vec3(bx0 + 0.45f, -0.6f, 0.f), 1.1f, r.next(), Gy(0.9f), 1);
    }
    // ---- living: sofa facing the TV wall (the bedroom partition), kitchen along the far side wall, island, dining
    const float LX0 = xb + pt * 0.5f, LX1 = X1, LY1 = yS - pt * 0.5f;
    const float lw = LX1 - LX0;
    if (alcove) {
        // kitchen along the back wall of the alcove, fridge at its end, the island out in the living room
        u32 cabC = top ? C(0.12f, 0.12f, 0.13f) : C(0.92f, 0.92f, 0.9f);
        float kl = Min(X1 - alX0 - 1.1f, 4.2f), kxc = alX0 + 0.2f + kl * 0.5f;
        kitchenRun(b, vec3(kxc, Y1, 0.f), kPi, kl, cabC, top ? C(0.9f, 0.9f, 0.88f) : C(0.2f, 0.2f, 0.22f), true, true, true, r.next(), kit);
        fridge(b, vec3(Min(kxc + kl * 0.5f + 0.4f, X1 - 0.4f), Y1, 0.f), kPi, top ? Gy(0.7f) : Gy(0.88f), r.next());
        downlight(b, vec3(kxc, (yS + Y1) * 0.5f, H), kit, 260.f, vec3(1.f, 0.86f, 0.66f), 5.5f);
        float ix = (alX0 + X1) * 0.5f, iy = LY1 - 1.3f;
        InPart ip(b, IP_FURNITURE);
        rbox(b, vec3(ix, iy, 0.45f), vec3(1.1f, 0.42f, 0.45f), 0.02f, cabC, M(MAT_WOOD), true);
        rbox(b, vec3(ix, iy - 0.08f, 0.92f), vec3(1.15f, 0.58f, 0.025f), 0.01f, top ? Gy(0.92f) : C(0.2f, 0.2f, 0.22f), M(MAT_MARBLE), true);
        collide(b, vec3(ix, iy, 0.47f), vec3(1.15f, 0.58f, 0.47f));
        for (int k = -1; k <= 1; k += 2) barStool(b, vec3(ix + k * 0.5f, iy - 0.95f, 0.f), wood, 0.72f);
        for (int k = -1; k <= 1; k += 2) pendant(b, vec3(ix + k * 0.5f, iy, H), H - 1.9f, living, top ? Gy(0.1f) : C(0.8f, 0.62f, 0.3f), 150.f, vec3(1.f, 0.84f, 0.6f), 4.5f, 0);
    } else {
        float kitchenX = LX1;   // kitchen run against the side wall, facing -x
        float kl = Clamp(LY1 - Y0 - 2.6f, 2.4f, 4.2f), kyc = LY1 - 0.4f - kl * 0.5f;
        u32 cabC = top ? C(0.12f, 0.12f, 0.13f) : C(0.92f, 0.92f, 0.9f);
        kitchenRun(b, vec3(kitchenX, kyc, 0.f), kHalfPi, kl, cabC, top ? C(0.9f, 0.9f, 0.88f) : C(0.2f, 0.2f, 0.22f), true, true, true, r.next(), living);
        fridge(b, vec3(kitchenX, kyc - kl * 0.5f - 0.4f, 0.f), kHalfPi, top ? Gy(0.7f) : Gy(0.88f), r.next());
        // island with stools
        float ix = kitchenX - 2.2f, iy = kyc;
        if (ix - 0.5f > LX0 + 3.0f) {
            InPart ip(b, IP_FURNITURE);
            rbox(b, vec3(ix, iy, 0.45f), vec3(0.45f, Min(1.1f, kl * 0.4f), 0.45f), 0.02f, cabC, M(MAT_WOOD), true);
            rbox(b, vec3(ix - 0.08f, iy, 0.92f), vec3(0.6f, Min(1.15f, kl * 0.42f), 0.025f), 0.01f, top ? Gy(0.92f) : C(0.2f, 0.2f, 0.22f), M(MAT_MARBLE), true);
            collide(b, vec3(ix, iy, 0.47f), vec3(0.6f, Min(1.15f, kl * 0.42f), 0.47f));
            for (int k = -1; k <= 1; k += 2) barStool(b, vec3(ix - 0.95f, iy + k * 0.5f, 0.f), wood, 0.72f);
            for (int k = -1; k <= 1; k += 2) pendant(b, vec3(ix, iy + k * 0.5f, H), H - 1.9f, living, top ? Gy(0.1f) : C(0.8f, 0.62f, 0.3f), 150.f, vec3(1.f, 0.84f, 0.6f), 4.5f, 0);
        }
    }
    {
        // lounge: TV on the partition wall (facing +x), sofa facing it, coffee table, armchair, rug, lamp
        float ly = Y0 + Clamp((LY1 - Y0) * 0.42f, 2.2f, 4.0f);
        float tvX = LX0 + 0.25f;
        tvUnit(b, vec3(tvX, ly, 0.f), -kHalfPi, 1.8f, r.next(), living, false);
        float sx = Min(tvX + 3.2f, LX1 - 3.2f);
        u32 fab = top ? C(0.86f, 0.84f, 0.8f) : C(hsv(r.f(), 0.25f, 0.55f));
        rug(b, vec3((tvX + sx) * 0.5f + 0.2f, ly, 0.f), 2.8f, 2.6f, r.next());
        sofa(b, vec3(sx, ly, 0.f), kHalfPi, 2.3f, fab, r.next());
        coffeeTable(b, vec3((tvX + sx) * 0.5f + 0.3f, ly, 0.f), kHalfPi, 1.1f, 0.6f, r.next());
        armchair(b, vec3((tvX + sx) * 0.5f + 0.2f, ly - 1.7f, 0.f), 0.2f, top ? C(0.1f, 0.08f, 0.07f) : C(hsv(r.f(), 0.3f, 0.45f)), r.next());
        floorLamp(b, vec3(sx + 0.3f, ly + 1.45f, 0.f), living, C(0.95f, 0.9f, 0.8f));
        plant(b, vec3(LX0 + 0.45f, Y0 + 0.5f, 0.f), 1.2f, r.next(), top ? Gy(0.15f) : 0, 1);
        {
            At at(b, vec3(LX0, ly, 0.f), -kHalfPi);
            picture(b, vec3(1.6f, 0.f, 1.7f), 0.8f, 1.0f, r.next());
        }
        // dining by the window beyond the lounge
        float dx = LX1 - 2.3f, dy = Y0 + 1.2f;
        if (dx > sx + 1.8f) {
            u32 tc = top ? C(0.15f, 0.1f, 0.06f) : C(0.55f, 0.4f, 0.26f);
            diningTable(b, vec3(dx, dy + 0.3f, 0.f), 0.f, 1.4f, 0.85f, tc, true, r.next());
            for (int k = 0; k < 4; k++) chair(b, vec3(dx + ((k & 1) ? 0.35f : -0.35f), dy + 0.3f + ((k & 2) ? 0.65f : -0.65f), 0.f), (k & 2) ? kPi : 0.f, tc, 1, true);
        }
        // penthouse: a second lounge mid-room facing the view, and a grand piano by the glass when there is space
        const float mx0 = sx + 1.6f, mx1 = dx - 1.4f;
        if (top && dx > sx + 1.8f && mx1 - mx0 > 4.6f && LY1 - Y0 > 7.4f) {
            const bool piano = mx1 - mx0 > 6.6f;
            const float lcx = piano ? mx0 + 2.2f : (mx0 + mx1) * 0.5f, ry = Y0 + 2.5f;
            rug(b, vec3(lcx, ry, 0.f), 3.6f, 3.0f, r.next());
            sofa(b, vec3(lcx, Y0 + 3.65f, 0.f), kPi, 2.6f, C(0.78f, 0.76f, 0.72f), r.next());
            coffeeTable(b, vec3(lcx, ry - 0.1f, 0.f), 0.f, 1.2f, 0.65f, r.next());
            for (int e = -1; e <= 1; e += 2) armchair(b, vec3(lcx + e * 1.7f, ry - 0.2f, 0.f), e * kHalfPi, C(0.1f, 0.08f, 0.07f), r.next());
            floorLamp(b, vec3(lcx + 1.55f, Y0 + 3.75f, 0.f), living, C(0.95f, 0.9f, 0.8f));
            if (piano) grandPiano(b, vec3(mx1 - 0.85f, Y0 + 2.5f, 0.f), 0.f);
        }
        int nd = Max(2, (int)(lw / 3.f));
        for (int k = 0; k < nd; k++) downlight(b, vec3(LX0 + lw * (k + 0.5f) / nd, ly, H), living, 260.f, vec3(1.f, 0.85f, 0.64f), 6.f);
        downlight(b, vec3(LX0 + lw * 0.5f, Y0 + 1.0f, H), living, 220.f, vec3(1.f, 0.85f, 0.64f), 6.f);
    }
    // ---- bedroom: bed against the back partition facing the windows, nightstands, dresser + mirror
    {
        float bcx = (X0 + xb) * 0.5f, BY1 = yS - pt * 0.5f;
        u32 duvet = top ? C(0.92f, 0.9f, 0.86f) : C(hsv(r.f(), 0.25f, 0.8f));
        bed(b, vec3(bcx, BY1 - 1.12f, 0.f), kPi, top ? 1.9f : 1.6f, 2.1f, wood, duvet, r.next(), false);
        float ns = (top ? 1.9f : 1.6f) * 0.5f + 0.32f;
        if (bcx - ns - 0.25f > X0) nightstand(b, vec3(bcx - ns, BY1 - 0.22f, 0.f), kPi, wood, bedroom, r.next());
        if (bcx + ns + 0.25f < xb - pt * 0.5f) nightstand(b, vec3(bcx + ns, BY1 - 0.22f, 0.f), kPi, wood, bedroom, r.next());
        rug(b, vec3(bcx, BY1 - 2.6f, 0.f), 2.2f, 1.2f, r.next());
        dresser(b, vec3(X0, Y0 + 1.6f, 0.f), -kHalfPi, 1.3f, wood, r.next());
        {
            At at(b, vec3(bcx, BY1, 0.f), kPi);
            picture(b, vec3(0.f, 0.f, 1.6f), 1.4f, 0.55f, r.next());
        }
        pendant(b, vec3(bcx, BY1 - 2.2f, H), 0.55f, bedroom, top ? Gy(0.15f) : C(0.9f, 0.85f, 0.75f), 170.f, vec3(1.f, 0.82f, 0.58f), 5.f, 2);
        marker(b, IM_BED, vec3(bcx, BY1 - 2.25f - 0.45f, 0.f), 0.f);
        marker(b, IM_MIRROR, vec3(X0 + 1.0f, Y0 + 1.6f, 0.f), -kHalfPi);
    }
    // ---- bathroom: tub along the back wall, toilet, vanity; walk-in closet with wardrobes
    {
        float bcx = (X0 + xb) * 0.5f, BY0 = yS + pt * 0.5f;
        bathtub(b, vec3(bcx, Y1, 0.f), kPi, Min(1.7f, xb - X0 - 0.2f), top ? Gy(0.9f) : C(hsv(r.f(), 0.3f, 0.85f)));
        toilet(b, vec3(X0 + 0.31f, BY0 + 0.55f, 0.f), -kHalfPi);
        bathSink(b, vec3(xb - pt * 0.5f, BY0 + 0.7f, 0.f), kHalfPi, top ? Gy(0.15f) : Gy(0.95f), bath);
        domeLight(b, vec3(bcx, (BY0 + Y1) * 0.5f, H), bath, 150.f, vec3(1.f, 0.95f, 0.88f));
    }
    if (closet) {
        float cx0 = cabX1 + pt, cw = closX1 - cx0;
        wardrobe(b, vec3(cx0 + cw * 0.5f, Y1, 0.f), kPi, Min(2.2f, cw - 0.2f), wood, r.next());
        marker(b, IM_WARDROBE, vec3(cx0 + cw * 0.5f, Y1 - 1.2f, 0.f), kPi);
        domeLight(b, vec3(cx0 + cw * 0.5f, (yS + Y1) * 0.5f, H), clos, 110.f, vec3(1.f, 0.9f, 0.75f));
    } else {
        wardrobe(b, vec3(X0, (Y0 + yS) * 0.5f + 0.4f, 0.f), -kHalfPi, 1.4f, wood, r.next());
        marker(b, IM_WARDROBE, vec3(X0 + 1.1f, (Y0 + yS) * 0.5f + 0.4f, 0.f), -kHalfPi);
    }
    roomDressing(b, living, false, d.seed ^ 0x71u);
    roomDressing(b, bedroom, false, d.seed ^ 0x72u);
}

}  // namespace ikit
}  // namespace World
