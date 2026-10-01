// Interior furniture library (homes, kitchens, bathrooms, restaurants): procedural pieces built with the kit
// (interiorkit.cpp) in a local frame whose +y is the item's front. Every piece is detailed for close-ups (bevelled
// cushions, turned legs, handles, drawer lines, fabric folds) and registers its own collision where people can't pass.
#include "interiors.h"
#include "sites.h"

namespace World {
namespace ikit {

// ------------------------------------------------------------------------------------------------ seating
// Upholstered sofa: plinth, legs, seat and back cushions, rolled arms, throw pillows (front +y)
void sofa(IB& b, vec3 p, float yaw, float w, u32 fabric, u32 seed, bool collideIt = true, int seats = 0) {
    At at(b, p, yaw);
    Rng r(seed);
    if (seats <= 0) seats = w > 1.9f ? 3 : (w > 1.3f ? 2 : 1);
    const float D = 0.92f, armW = 0.2f;
    vec3 fc = rgbOf(fabric);
    u32 fab = M(MAT_CLOTH);
    u32 legC = C(0.25f, 0.16f, 0.09f);
    // legs
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) cyl(b, vec3(sx * (w * 0.5f - 0.08f), sy * (D * 0.5f - 0.08f), 0.f), 0.022f, 0.03f, 0.1f, 8, legC, M(MAT_WOOD), false);
    // base
    rbox(b, vec3(0, 0, 0.23f), vec3(w * 0.5f, D * 0.5f, 0.13f), 0.03f, fabric, fab, true);
    // back frame
    rbox(b, vec3(0, -D * 0.5f + 0.1f, 0.58f), vec3(w * 0.5f - 0.02f, 0.1f, 0.26f), 0.05f, fabric, fab);
    // arms
    for (int sx = -1; sx <= 1; sx += 2) rbox(b, vec3(sx * (w * 0.5f - armW * 0.5f), 0.02f, 0.45f), vec3(armW * 0.5f, D * 0.5f - 0.01f, 0.2f), 0.07f, fabric, fab);
    // seat cushions
    float inner = w - armW * 2.f;
    float cw = inner / seats;
    for (int k = 0; k < seats; k++) {
        float x = -inner * 0.5f + cw * (k + 0.5f);
        rbox(b, vec3(x, 0.07f, 0.44f), vec3(cw * 0.5f - 0.006f, D * 0.5f - 0.15f, 0.085f), 0.05f, C(fc * r.range(0.95f, 1.03f)), fab);
        // back cushion leaning back ~12 degrees
        b.pushAxes(vec3(x, -D * 0.5f + 0.27f, 0.68f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.2f)), normalize(vec3(0, -0.2f, 1)));
        rbox(b, vec3(0.f), vec3(cw * 0.5f - 0.008f, 0.09f, 0.2f), 0.07f, C(fc * r.range(0.95f, 1.04f)), fab, true);
        b.pop();
    }
    // throw pillows in the corners
    int np = r.irange(1, 3);
    for (int k = 0; k < np; k++) {
        float sx = (k % 2 == 0) ? -1.f : 1.f;
        vec3 pc = hsv(r.f(), r.range(0.3f, 0.7f), r.range(0.5f, 0.85f));
        b.pushAxes(vec3(sx * (inner * 0.5f - 0.2f) + (k > 1 ? 0.25f : 0.f), -D * 0.5f + 0.4f, 0.66f), normalize(vec3(1, 0, sx * 0.15f)),
                   normalize(vec3(0, 1, 0.45f)), normalize(cross(normalize(vec3(1, 0, sx * 0.15f)), normalize(vec3(0, 1, 0.45f)))));
        rbox(b, vec3(0.f), vec3(0.2f, 0.07f, 0.2f), 0.06f, C(pc), fab, true);
        b.pop();
    }
    if (collideIt) collide(b, vec3(0, 0, 0.4f), vec3(w * 0.5f, D * 0.5f, 0.4f));
}

void armchair(IB& b, vec3 p, float yaw, u32 fabric, u32 seed) { sofa(b, p, yaw, 0.95f, fabric, seed, true, 1); }

// Wooden dining / kitchen chair (front +y = where the sitter faces)
void chair(IB& b, vec3 p, float yaw, u32 col, int style = 0, bool collideIt = false) {
    At at(b, p, yaw);
    u32 wood = M(MAT_WOOD);
    float s = 0.21f;
    float seatZ = 0.45f;
    if (style == 1) {
        // metal cafe chair with a vinyl seat
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) tube(b, vec3(sx * s, sy * s, 0.f), vec3(sx * (s - 0.02f), sy * (s - 0.02f), seatZ - 0.03f), 0.012f, 6, Gy(0.75f), M(MAT_CHROME));
        rbox(b, vec3(0, 0, seatZ), vec3(s + 0.02f, s + 0.02f, 0.035f), 0.02f, col, M(MAT_LEATHER));
        for (int sx = -1; sx <= 1; sx += 2) tube(b, vec3(sx * (s - 0.02f), -s + 0.02f, seatZ), vec3(sx * (s - 0.02f), -s - 0.02f, 0.88f), 0.012f, 6, Gy(0.75f), M(MAT_CHROME));
        rbox(b, vec3(0, -s - 0.01f, 0.78f), vec3(s, 0.025f, 0.1f), 0.02f, col, M(MAT_LEATHER));
    } else {
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) {
                float top = sy < 0 ? 0.92f : seatZ - 0.02f;
                box(b, vec3(sx * (s - 0.02f), sy * (s - 0.02f), top * 0.5f), vec3(0.02f, 0.02f, top * 0.5f), col, wood, SK_NZ);
            }
        rbox(b, vec3(0, 0, seatZ), vec3(s + 0.01f, s + 0.02f, 0.02f), 0.008f, col, wood);
        // stretchers
        for (int sx = -1; sx <= 1; sx += 2) box(b, vec3(sx * (s - 0.02f), 0, 0.15f), vec3(0.012f, s - 0.03f, 0.012f), col, wood, SK_NONE);
        // back rail + slats
        box(b, vec3(0, -s + 0.02f, 0.86f), vec3(s - 0.01f, 0.018f, 0.05f), col, wood, SK_NONE);
        for (int k = -1; k <= 1; k++) box(b, vec3(k * 0.08f, -s + 0.02f, 0.66f), vec3(0.018f, 0.012f, 0.16f), col, wood, SK_PZ | SK_NZ);
    }
    if (collideIt) collide(b, vec3(0, 0, 0.45f), vec3(s, s, 0.45f));
}

// Bar / diner stool with a chrome pedestal and footrest ring (seat top at seatZ)
void barStool(IB& b, vec3 p, u32 seatCol, float seatZ = 0.76f) {
    At at(b, p, 0.f);
    u32 chrome = M(MAT_CHROME);
    lathe(b, vec3(0.f), {vec2(0.2f, 0.f), vec2(0.2f, 0.015f), vec2(0.07f, 0.04f), vec2(0.035f, 0.06f)}, 14, Gy(0.8f), chrome, false);
    cyl(b, vec3(0, 0, 0.05f), 0.032f, 0.028f, seatZ - 0.11f, 10, Gy(0.85f), chrome, false);
    // footrest ring
    for (int k = 0; k < 10; k++) {
        float a0 = kTwoPi * k / 10.f, a1 = kTwoPi * (k + 1) / 10.f;
        tube(b, vec3(cosf(a0) * 0.19f, sinf(a0) * 0.19f, seatZ * 0.38f), vec3(cosf(a1) * 0.19f, sinf(a1) * 0.19f, seatZ * 0.38f), 0.01f, 5, Gy(0.85f), chrome);
    }
    for (int k = 0; k < 3; k++) {
        float a = kTwoPi * k / 3.f;
        tube(b, vec3(0, 0, seatZ * 0.38f), vec3(cosf(a) * 0.19f, sinf(a) * 0.19f, seatZ * 0.38f), 0.008f, 5, Gy(0.85f), chrome);
    }
    cyl(b, vec3(0, 0, seatZ - 0.07f), 0.19f, 0.19f, 0.02f, 16, Gy(0.85f), chrome, false, true);
    lathe(b, vec3(0, 0, seatZ - 0.05f), {vec2(0.2f, 0.f), vec2(0.215f, 0.02f), vec2(0.21f, 0.045f), vec2(0.17f, 0.06f), vec2(0.0f, 0.065f)}, 16, seatCol, M(MAT_LEATHER), false);
}

// ------------------------------------------------------------------------------------------------ tables
void coffeeTable(IB& b, vec3 p, float yaw, float w, float dp, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 wood = C(0.36f, 0.24f, 0.14f) , wm = M(MAT_WOOD);
    rbox(b, vec3(0, 0, 0.42f), vec3(w * 0.5f, dp * 0.5f, 0.025f), 0.01f, wood, wm);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) box(b, vec3(sx * (w * 0.5f - 0.05f), sy * (dp * 0.5f - 0.05f), 0.2f), vec3(0.025f, 0.025f, 0.2f), wood, wm, SK_NZ);
    box(b, vec3(0, 0, 0.12f), vec3(w * 0.5f - 0.06f, dp * 0.5f - 0.06f, 0.012f), wood, wm, SK_NONE);   // lower shelf
    // magazines on the shelf, a mug and a remote on top, a bowl
    for (int k = 0; k < 3; k++) box(b, vec3(-w * 0.2f + r.range(-0.03f, 0.03f), r.range(-0.05f, 0.05f), 0.134f + k * 0.006f), vec3(0.11f, 0.15f, 0.003f), C(hsv(r.f(), 0.5f, 0.8f)), M(MAT_PAINT_WHITE), SK_NZ);
    cyl(b, vec3(w * 0.25f, dp * 0.15f, 0.445f), 0.042f, 0.042f, 0.095f, 12, C(hsv(r.f(), 0.4f, 0.85f)), M(MAT_PAINT_WHITE), false);
    rbox(b, vec3(-w * 0.15f, dp * 0.2f, 0.452f), vec3(0.025f, 0.08f, 0.008f), 0.006f, Gy(0.08f), M(MAT_PLASTIC));
    lathe(b, vec3(0.f, -dp * 0.15f, 0.445f), {vec2(0.05f, 0.f), vec2(0.12f, 0.05f), vec2(0.13f, 0.07f)}, 16, C(hsv(r.f(), 0.6f, 0.6f)), M(MAT_PAINT_WHITE), false);
    for (int k = 0; k < 3; k++) sphere(b, vec3(-0.03f + k * 0.035f, -dp * 0.15f + (k & 1) * 0.03f, 0.49f), 0.03f, 8, k == 1 ? C(0.9f, 0.7f, 0.1f) : C(0.8f, 0.15f, 0.1f), M(MAT_PAINT_WHITE));
    collide(b, vec3(0, 0, 0.22f), vec3(w * 0.5f, dp * 0.5f, 0.22f));
}

// Dining table (rectangular, front +y); optional place settings
void diningTable(IB& b, vec3 p, float yaw, float w, float dp, u32 col, bool settings, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 wm = M(MAT_WOOD);
    rbox(b, vec3(0, 0, 0.745f), vec3(w * 0.5f, dp * 0.5f, 0.022f), 0.01f, col, wm);
    box(b, vec3(0, 0, 0.68f), vec3(w * 0.5f - 0.05f, dp * 0.5f - 0.05f, 0.045f), col, wm, SK_PZ);   // apron
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            vec3 lp(sx * (w * 0.5f - 0.07f), sy * (dp * 0.5f - 0.07f), 0.f);
            lathe(b, lp, {vec2(0.02f, 0.f), vec2(0.024f, 0.1f), vec2(0.03f, 0.3f), vec2(0.022f, 0.55f), vec2(0.03f, 0.64f), vec2(0.03f, 0.66f)}, 8, col, wm, false);
        }
    if (settings) {
        for (int sy = -1; sy <= 1; sy += 2)
            for (float x = -w * 0.25f; x <= w * 0.26f; x += Max(w * 0.5f, 0.6f)) {
                disc(b, vec3(x, sy * (dp * 0.5f - 0.2f), 0.769f), 0.12f, 16, Gy(0.95f), M(MAT_PAINT_WHITE));
                box(b, vec3(x + 0.16f, sy * (dp * 0.5f - 0.2f), 0.769f), vec3(0.01f, 0.09f, 0.002f), Gy(0.8f), M(MAT_CHROME), SK_NZ);
                cyl(b, vec3(x - 0.16f, sy * (dp * 0.5f - 0.28f), 0.768f), 0.032f, 0.036f, 0.11f, 10, C(0.85f, 0.9f, 0.95f, 1.f), M(MAT_GLASS), false);
            }
    }
    // centerpiece
    if (r.chance(0.6f)) {
        lathe(b, vec3(0, 0, 0.768f), {vec2(0.05f, 0.f), vec2(0.07f, 0.08f), vec2(0.04f, 0.2f), vec2(0.045f, 0.24f)}, 12, C(hsv(r.f(), 0.5f, 0.7f)), M(MAT_PAINT_WHITE), false);
        for (int k = 0; k < 5; k++) sphere(b, vec3(r.range(-0.05f, 0.05f), r.range(-0.05f, 0.05f), 1.04f + r.range(-0.03f, 0.05f)), 0.035f, 6, C(hsv(r.f(), 0.8f, 0.9f)), M(MAT_LEAVES));
    }
    collide(b, vec3(0, 0, 0.38f), vec3(w * 0.5f - 0.08f, dp * 0.5f - 0.08f, 0.38f));
}

// ------------------------------------------------------------------------------------------------ living room
// TV on a low media console with a sound bar, console and decor (front +y). Returns the screen center (local).
void tvUnit(IB& b, vec3 p, float yaw, float w, u32 seed, int room, bool lit) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 cab = r.chance(0.5f) ? C(0.1f, 0.09f, 0.08f) : C(0.52f, 0.36f, 0.22f);
    rbox(b, vec3(0, 0, 0.26f), vec3(w * 0.5f, 0.21f, 0.23f), 0.012f, cab, M(MAT_WOOD), true);
    for (int k = 0; k < 3; k++) {
        float x = -w * 0.5f + w * (k + 0.5f) / 3.f;
        box(b, vec3(x, 0.212f, 0.26f), vec3(w / 6.f - 0.015f, 0.003f, 0.18f), C(rgbOf(cab) * 1.15f), M(MAT_WOOD), SK_NZ | SK_NY);
        box(b, vec3(x + w / 6.f - 0.06f, 0.222f, 0.3f), vec3(0.008f, 0.01f, 0.05f), Gy(0.7f), M(MAT_CHROME), SK_NZ);
    }
    // TV: thin panel on a stand
    float tw = Min(w * 0.85f, 1.25f), th = tw * 0.5625f;
    float zc = 0.52f + 0.06f + th * 0.5f;
    box(b, vec3(0, -0.02f, 0.495f), vec3(0.18f, 0.08f, 0.006f), Gy(0.06f), M(MAT_PLASTIC), SK_NZ);
    box(b, vec3(0, -0.04f, 0.53f), vec3(0.03f, 0.02f, 0.04f), Gy(0.06f), M(MAT_PLASTIC), SK_NZ);
    rbox(b, vec3(0, -0.04f, zc), vec3(tw * 0.5f, 0.025f, th * 0.5f), 0.006f, Gy(0.03f), M(MAT_PLASTIC), true);
    box(b, vec3(0, -0.014f, zc), vec3(tw * 0.5f - 0.015f, 0.001f, th * 0.5f - 0.015f), lit ? C(0.45f, 0.55f, 0.75f, 0.65f) : Gy(0.02f), lit ? EM(9, (u32)(seed & 255u)) : M(MAT_GLASS), SK_NZ);
    // sound bar and a game console with a status light
    rbox(b, vec3(0, 0.12f, 0.515f), vec3(tw * 0.35f, 0.04f, 0.03f), 0.012f, Gy(0.05f), M(MAT_PLASTIC));
    rbox(b, vec3(w * 0.3f, 0.05f, 0.1f), vec3(0.15f, 0.12f, 0.035f), 0.01f, Gy(0.9f), M(MAT_PAINT_WHITE));
    box(b, vec3(w * 0.3f - 0.1f, 0.171f, 0.1f), vec3(0.02f, 0.001f, 0.003f), C(0.2f, 0.6f, 1.f, 0.6f), EM(), SK_NZ);
    plant(b, vec3(-w * 0.5f + 0.12f, 0.05f, 0.49f), 0.5f, r.next());
    if (lit) light(b, vec3(0, 0.35f, zc), vec3(0.55f, 0.65f, 1.f) * 25.f, 3.5f, room, vec3(0.f), 0.f, 0.f, 4, (u8)(seed & 255u));
    collide(b, vec3(0, 0, 0.26f), vec3(w * 0.5f, 0.21f, 0.26f));
}

// Floor lamp with a fabric shade (+ light)
void floorLamp(IB& b, vec3 p, int room, u32 shade, float cd = 90.f) {
    At at(b, p, 0.f);
    lathe(b, vec3(0.f), {vec2(0.15f, 0.f), vec2(0.15f, 0.02f), vec2(0.03f, 0.04f)}, 14, Gy(0.2f), M(MAT_METAL_BRUSHED), false);
    cyl(b, vec3(0, 0, 0.03f), 0.012f, 0.012f, 1.4f, 8, Gy(0.2f), M(MAT_METAL_BRUSHED), false);
    lathe(b, vec3(0, 0, 1.28f), {vec2(0.22f, 0.f), vec2(0.15f, 0.3f)}, 18, shade, M(MAT_FABRIC), false);
    lathe(b, vec3(0, 0, 1.2799f), {vec2(0.15f, 0.3f), vec2(0.22f, 0.f)}, 18, C(rgbOf(shade) * 0.8f), M(MAT_FABRIC), false);   // inside
    sphere(b, vec3(0, 0, 1.36f), 0.04f, 8, C(1.f, 0.85f, 0.6f, 1.f), EM());
    light(b, vec3(0, 0, 1.35f), vec3(1.f, 0.78f, 0.52f) * cd, 5.5f, room);
}

// Table lamp (base + shade) with a warm light
void tableLamp(IB& b, vec3 p, int room, u32 baseCol, u32 shade, float cd = 45.f) {
    At at(b, p, 0.f);
    lathe(b, vec3(0.f), {vec2(0.06f, 0.f), vec2(0.08f, 0.06f), vec2(0.09f, 0.16f), vec2(0.05f, 0.26f), vec2(0.015f, 0.3f)}, 12, baseCol, M(MAT_PAINT_WHITE), false);
    cyl(b, vec3(0, 0, 0.3f), 0.006f, 0.006f, 0.08f, 6, Gy(0.7f), M(MAT_CHROME), false);
    lathe(b, vec3(0, 0, 0.3f), {vec2(0.15f, 0.f), vec2(0.1f, 0.2f)}, 16, shade, M(MAT_FABRIC), false);
    lathe(b, vec3(0, 0, 0.2999f), {vec2(0.1f, 0.2f), vec2(0.15f, 0.f)}, 16, C(rgbOf(shade) * 0.8f), M(MAT_FABRIC), false);
    sphere(b, vec3(0, 0, 0.36f), 0.03f, 8, C(1.f, 0.85f, 0.6f, 1.f), EM());
    light(b, vec3(0, 0, 0.36f), vec3(1.f, 0.75f, 0.5f) * cd, 4.f, room);
}

// Bookshelf against a wall (front +y) with books and ornaments
void bookshelf(IB& b, vec3 p, float yaw, float w, float h, u32 col, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 wm = M(MAT_WOOD);
    float dp = 0.32f;
    box(b, vec3(-w * 0.5f + 0.015f, dp * 0.5f, h * 0.5f), vec3(0.015f, dp * 0.5f, h * 0.5f), col, wm, SK_NY);
    box(b, vec3(w * 0.5f - 0.015f, dp * 0.5f, h * 0.5f), vec3(0.015f, dp * 0.5f, h * 0.5f), col, wm, SK_NY);
    box(b, vec3(0, 0.008f, h * 0.5f), vec3(w * 0.5f - 0.03f, 0.008f, h * 0.5f), C(rgbOf(col) * 0.8f), wm, SK_NY);
    int shelves = Max(3, (int)(h / 0.36f));
    for (int k = 0; k <= shelves; k++) {
        float z = 0.05f + (h - 0.07f) * k / shelves;
        box(b, vec3(0, dp * 0.5f, z), vec3(w * 0.5f - 0.03f, dp * 0.5f, 0.012f), col, wm, SK_NONE);
        if (k == shelves) break;
        InPart ip(b, IP_DETAIL);
        float x0 = -w * 0.5f + 0.04f, x1 = w * 0.5f - 0.04f;
        if (r.chance(0.3f)) {
            // ornament: vase or framed photo, then books
            float ox = r.chance(0.5f) ? x0 + 0.1f : x1 - 0.1f;
            if (r.chance(0.5f)) lathe(b, vec3(ox, dp * 0.5f, z + 0.012f), {vec2(0.03f, 0.f), vec2(0.06f, 0.08f), vec2(0.035f, 0.18f), vec2(0.04f, 0.2f)}, 10, C(hsv(r.f(), 0.5f, 0.7f)), M(MAT_PAINT_WHITE), false);
            else {
                b.pushAxes(vec3(ox, dp * 0.6f, z + 0.012f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.2f)), normalize(vec3(0, -0.2f, 1)));
                picture(b, vec3(0, 0, 0.1f), 0.14f, 0.19f, r.next(), Gy(0.1f));
                b.pop();
            }
            if (ox < 0.f) x0 += 0.22f;
            else x1 -= 0.22f;
        }
        b.push(vec3(0, dp * 0.5f, 0.f), 0.f);
        books(b, x0, x1, z + 0.012f, dp * 0.8f, r.next());
        b.pop();
    }
    collide(b, vec3(0, dp * 0.5f, h * 0.5f), vec3(w * 0.5f, dp * 0.5f, h * 0.5f));
}

// Curtains on a window (both sides gathered, pleated) + rod; window rect in the face frame given by the caller:
// local frame: x along the wall, +y into the room (curtain hangs at y), z up; x0..x1, z0..z1 = window rect
void curtains(IB& b, float x0, float x1, float z0, float z1, float y, u32 col, u32 seed, float closed = 0.2f) {
    Rng r(seed);
    InPart ip(b, IP_FURNITURE);
    float top = z1 + 0.12f, bot = Max(0.02f, z0 - 0.15f);
    tube(b, vec3(x0 - 0.2f, y + 0.05f, top + 0.03f), vec3(x1 + 0.2f, y + 0.05f, top + 0.03f), 0.012f, 8, Gy(0.3f), M(MAT_METAL_BRUSHED), true);
    for (int side = 0; side < 2; side++) {
        float w = (x1 - x0) * 0.5f * Clamp(closed + r.range(0.f, 0.1f), 0.12f, 1.f) + 0.18f;
        float a = side == 0 ? x0 - 0.2f : x1 + 0.2f - w;
        int folds = Max(3, (int)(w / 0.09f));
        for (int k = 0; k < folds; k++) {
            float s0 = a + w * k / folds, s1 = a + w * (k + 1) / folds;
            float d0 = (k & 1) ? 0.05f : 0.f, d1 = (k & 1) ? 0.f : 0.05f;
            vec3 A(s0, y + d0, bot), B(s1, y + d1, bot), Cc(s1, y + d1, top), D(s0, y + d0, top);
            quadF(b, A, B, Cc, D, vec3(0, 1, 0), col, M(MAT_CLOTH));
            quadF(b, A, B, Cc, D, vec3(0, -1, 0), C(rgbOf(col) * 0.7f), M(MAT_CLOTH));
        }
    }
}

// ------------------------------------------------------------------------------------------------ bedroom
// Bed with frame, headboard (against -y), mattress, sheet, duvet, pillows (foot of the bed toward +y)
void bed(IB& b, vec3 p, float yaw, float w, float len, u32 frameCol, u32 duvet, u32 seed, bool messy) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 wm = M(MAT_WOOD);
    float hl = len * 0.5f;
    // frame + legs
    rbox(b, vec3(0, hl * 0.02f, 0.2f), vec3(w * 0.5f + 0.03f, hl, 0.1f), 0.015f, frameCol, wm);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) box(b, vec3(sx * (w * 0.5f - 0.02f), sy * (hl - 0.04f), 0.05f), vec3(0.03f, 0.03f, 0.05f), frameCol, wm, SK_NZ);
    // headboard with an upholstered inset
    rbox(b, vec3(0, -hl - 0.03f, 0.58f), vec3(w * 0.5f + 0.06f, 0.04f, 0.58f), 0.02f, frameCol, wm, true);
    rbox(b, vec3(0, -hl + 0.015f, 0.72f), vec3(w * 0.5f - 0.06f, 0.025f, 0.3f), 0.03f, C(rgbOf(duvet) * 0.7f + vec3(0.1f)), M(MAT_CLOTH));
    // mattress + fitted sheet
    rbox(b, vec3(0, 0, 0.42f), vec3(w * 0.5f, hl - 0.02f, 0.12f), 0.05f, Gy(0.95f), M(MAT_CLOTH));
    // duvet: covers the lower 70 %, drapes over the sides, folded top edge
    float dStart = -hl + len * 0.3f;
    float dz = messy ? r.range(0.02f, 0.06f) : 0.f;
    rbox(b, vec3(0, (dStart + hl) * 0.5f + 0.02f, 0.47f + dz), vec3(w * 0.5f + 0.05f, (hl - dStart) * 0.5f + 0.02f, 0.1f), 0.06f, duvet, M(MAT_CLOTH));
    rbox(b, vec3(0, dStart + 0.12f, 0.575f + dz), vec3(w * 0.5f + 0.02f, 0.13f, 0.035f), 0.03f, C(rgbOf(duvet) * 0.85f + vec3(0.12f)), M(MAT_CLOTH));
    if (messy) {
        // crumpled bump in the duvet
        rbox(b, vec3(r.range(-w * 0.2f, w * 0.2f), r.range(0.f, hl * 0.4f), 0.58f), vec3(0.25f, 0.3f, 0.06f), 0.06f, duvet, M(MAT_CLOTH));
    }
    // pillows
    int np = w > 1.2f ? 2 : 1;
    for (int k = 0; k < np; k++) {
        float x = np == 1 ? 0.f : (k == 0 ? -w * 0.25f : w * 0.25f);
        b.pushAxes(vec3(x, -hl + 0.22f, 0.63f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.35f)), normalize(vec3(0, -0.35f, 1)));
        rbox(b, vec3(0.f), vec3(Min(0.33f, w * 0.23f), 0.07f, 0.18f), 0.06f, Gy(0.97f), M(MAT_CLOTH), true);
        b.pop();
    }
    collide(b, vec3(0, 0, 0.3f), vec3(w * 0.5f + 0.03f, hl, 0.3f));
}

// Nightstand with a drawer, a lamp, a clock and a book (front +y)
void nightstand(IB& b, vec3 p, float yaw, u32 col, int room, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    rbox(b, vec3(0, 0, 0.29f), vec3(0.24f, 0.2f, 0.29f), 0.01f, col, M(MAT_WOOD), true);
    box(b, vec3(0, 0.2f, 0.44f), vec3(0.21f, 0.004f, 0.08f), C(rgbOf(col) * 1.1f), M(MAT_WOOD), SK_NZ);
    box(b, vec3(0, 0.215f, 0.44f), vec3(0.04f, 0.012f, 0.01f), Gy(0.75f), M(MAT_CHROME), SK_NZ);
    tableLamp(b, vec3(-0.08f, -0.03f, 0.58f), room, C(hsv(r.f(), 0.3f, 0.8f)), C(0.95f, 0.9f, 0.8f));
    // clock with glowing digits
    rbox(b, vec3(0.12f, 0.08f, 0.62f), vec3(0.06f, 0.03f, 0.035f), 0.01f, Gy(0.08f), M(MAT_PLASTIC), true);
    text(b, "7:45", vec3(0.075f, 0.111f, 0.605f), vec3(1, 0, 0), vec3(0, 0, 1), 0.03f, 0.005f, C(1.f, 0.15f, 0.1f, 0.8f), EM());
    box(b, vec3(0.1f, -0.1f, 0.6f), vec3(0.08f, 0.11f, 0.02f), C(hsv(r.f(), 0.6f, 0.5f)), M(MAT_FABRIC), SK_NZ);
    collide(b, vec3(0, 0, 0.29f), vec3(0.24f, 0.2f, 0.29f));
}

// Wardrobe (front +y): two doors, crown, handles; clothes visible when doorAjar
void wardrobe(IB& b, vec3 p, float yaw, float w, u32 col, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    float H = 2.05f, dp = 0.58f;
    rbox(b, vec3(0, dp * 0.5f, H * 0.5f), vec3(w * 0.5f, dp * 0.5f, H * 0.5f), 0.01f, col, M(MAT_WOOD), true);
    box(b, vec3(0, dp * 0.5f + 0.02f, H + 0.03f), vec3(w * 0.5f + 0.03f, dp * 0.5f + 0.03f, 0.03f), col, M(MAT_WOOD), SK_NONE);
    int doors = w > 1.1f ? 3 : 2;
    for (int k = 0; k < doors; k++) {
        float x0 = -w * 0.5f + w * k / doors, x1 = x0 + w / doors;
        box(b, vec3((x0 + x1) * 0.5f, dp + 0.004f, H * 0.5f + 0.02f), vec3((x1 - x0) * 0.5f - 0.008f, 0.004f, H * 0.5f - 0.05f), C(rgbOf(col) * 1.07f), M(MAT_WOOD), SK_NZ | SK_NY);
        float hx = (k % 2 == 0) ? x1 - 0.06f : x0 + 0.06f;
        tube(b, vec3(hx, dp + 0.035f, 0.95f), vec3(hx, dp + 0.035f, 1.25f), 0.008f, 6, Gy(0.75f), M(MAT_CHROME), true);
    }
    // a garment bag hanging on the side, shoes on the floor in front
    for (int k = 0; k < 2; k++) {
        vec3 sc = hsv(r.f(), 0.4f, 0.4f);
        rbox(b, vec3(-w * 0.3f + k * 0.14f, dp + 0.25f, 0.05f), vec3(0.045f, 0.12f, 0.05f), 0.03f, C(sc), M(MAT_LEATHER));
    }
    collide(b, vec3(0, dp * 0.5f, H * 0.5f), vec3(w * 0.5f, dp * 0.5f, H * 0.5f));
}

// Dresser with drawers and a mirror above (front +y)
void dresser(IB& b, vec3 p, float yaw, float w, u32 col, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    float H = 0.86f, dp = 0.48f;
    rbox(b, vec3(0, dp * 0.5f, H * 0.5f), vec3(w * 0.5f, dp * 0.5f, H * 0.5f), 0.01f, col, M(MAT_WOOD), true);
    for (int row = 0; row < 3; row++)
        for (int cl = 0; cl < 2; cl++) {
            float x = -w * 0.25f + cl * w * 0.5f, z = 0.16f + row * 0.25f;
            box(b, vec3(x, dp + 0.004f, z + 0.03f), vec3(w * 0.25f - 0.02f, 0.004f, 0.105f), C(rgbOf(col) * 1.08f), M(MAT_WOOD), SK_NZ | SK_NY);
            sphere(b, vec3(x, dp + 0.02f, z + 0.03f), 0.018f, 8, Gy(0.8f), M(MAT_CHROME));
        }
    // mirror
    box(b, vec3(0, 0.02f, H + 0.55f), vec3(w * 0.32f, 0.02f, 0.42f), col, M(MAT_WOOD), SK_NY);
    box(b, vec3(0, 0.041f, H + 0.55f), vec3(w * 0.29f, 0.001f, 0.39f), Gy(0.95f), M(MAT_CHROME), SK_NZ | SK_NY);
    // perfume bottles, jewelry box, photo
    for (int k = 0; k < 3; k++) bottle(b, vec3(-w * 0.35f + k * 0.07f, dp * 0.6f, H + 0.005f), 0.022f, 0.1f + k * 0.02f, C(hsv(r.f(), 0.4f, 0.8f), 1.f), M(MAT_GLASS), Gy(0.8f), 8);
    rbox(b, vec3(w * 0.25f, dp * 0.55f, H + 0.05f), vec3(0.1f, 0.07f, 0.045f), 0.01f, C(0.5f, 0.1f, 0.15f), M(MAT_LEATHER));
    collide(b, vec3(0, dp * 0.5f, H * 0.5f), vec3(w * 0.5f, dp * 0.5f, H * 0.5f));
}

// ------------------------------------------------------------------------------------------------ kitchen
// Run of base cabinets with a countertop (front +y), optional sink / stove, upper cabinets and backsplash
void kitchenRun(IB& b, vec3 p, float yaw, float len, u32 cabCol, u32 topCol, bool sink, bool stove, bool uppers, u32 seed, int room) {
    At at(b, p, yaw);
    Rng r(seed);
    float hl = len * 0.5f, dp = 0.62f, H = 0.9f;
    u32 wm = M(MAT_WOOD);
    // plinth (recessed) + carcass + doors
    box(b, vec3(0, dp * 0.5f - 0.03f, 0.05f), vec3(hl, dp * 0.5f - 0.06f, 0.05f), Gy(0.15f), M(MAT_METAL_PAINTED), SK_NZ | SK_NY);
    box(b, vec3(0, dp * 0.5f - 0.02f, 0.48f), vec3(hl, dp * 0.5f - 0.02f, 0.38f), cabCol, wm, SK_NY);
    int units = Max(1, (int)roundf(len / 0.6f));
    float uw = len / units;
    float sinkX = sink ? (stove ? -hl + uw * 1.5f : 0.f) : 1e9f;
    float stoveX = stove ? (sink ? hl - uw * 1.5f : 0.f) : 1e9f;
    for (int k = 0; k < units; k++) {
        float x = -hl + uw * (k + 0.5f);
        bool isStove = fabsf(x - stoveX) < uw * 0.6f;
        if (isStove) continue;
        // drawer on top + door below
        box(b, vec3(x, dp - 0.036f, 0.76f), vec3(uw * 0.5f - 0.006f, 0.008f, 0.07f), C(rgbOf(cabCol) * 1.06f), wm, SK_NZ | SK_NY);
        box(b, vec3(x, dp - 0.036f, 0.38f), vec3(uw * 0.5f - 0.006f, 0.008f, 0.27f), C(rgbOf(cabCol) * 1.06f), wm, SK_NZ | SK_NY);
        box(b, vec3(x, dp - 0.02f, 0.78f), vec3(0.07f, 0.008f, 0.006f), Gy(0.75f), M(MAT_CHROME), SK_NZ);
        box(b, vec3(x + uw * 0.5f - 0.06f, dp - 0.02f, 0.5f), vec3(0.006f, 0.008f, 0.07f), Gy(0.75f), M(MAT_CHROME), SK_NZ);
    }
    // countertop with a sink cut-out
    if (sink) {
        float sw = Min(uw * 0.8f, 0.55f);
        float a0 = -hl, a1 = sinkX - sw * 0.5f, a2 = sinkX + sw * 0.5f;
        box(b, vec3((a0 + a1) * 0.5f, dp * 0.5f + 0.01f, H - 0.02f), vec3((a1 - a0) * 0.5f, dp * 0.5f + 0.01f, 0.02f), topCol, M(MAT_MARBLE), SK_NONE);
        box(b, vec3((a2 + hl) * 0.5f, dp * 0.5f + 0.01f, H - 0.02f), vec3((hl - a2) * 0.5f, dp * 0.5f + 0.01f, 0.02f), topCol, M(MAT_MARBLE), SK_NONE);
        box(b, vec3(sinkX, 0.08f, H - 0.02f), vec3(sw * 0.5f, 0.08f, 0.02f), topCol, M(MAT_MARBLE), SK_NONE);
        box(b, vec3(sinkX, dp - 0.04f, H - 0.02f), vec3(sw * 0.5f, 0.04f, 0.02f), topCol, M(MAT_MARBLE), SK_NONE);
        // basin (inside faces)
        float by0 = 0.16f, by1 = dp - 0.08f, bz = H - 0.2f;
        quadF(b, vec3(sinkX - sw * 0.5f, by0, bz), vec3(sinkX + sw * 0.5f, by0, bz), vec3(sinkX + sw * 0.5f, by1, bz), vec3(sinkX - sw * 0.5f, by1, bz), vec3(0, 0, 1), Gy(0.75f), M(MAT_METAL_BRUSHED));
        quadF(b, vec3(sinkX - sw * 0.5f, by0, bz), vec3(sinkX + sw * 0.5f, by0, bz), vec3(sinkX + sw * 0.5f, by0, H), vec3(sinkX - sw * 0.5f, by0, H), vec3(0, 1, 0), Gy(0.72f), M(MAT_METAL_BRUSHED));
        quadF(b, vec3(sinkX - sw * 0.5f, by1, bz), vec3(sinkX + sw * 0.5f, by1, bz), vec3(sinkX + sw * 0.5f, by1, H), vec3(sinkX - sw * 0.5f, by1, H), vec3(0, -1, 0), Gy(0.72f), M(MAT_METAL_BRUSHED));
        quadF(b, vec3(sinkX - sw * 0.5f, by0, bz), vec3(sinkX - sw * 0.5f, by1, bz), vec3(sinkX - sw * 0.5f, by1, H), vec3(sinkX - sw * 0.5f, by0, H), vec3(1, 0, 0), Gy(0.72f), M(MAT_METAL_BRUSHED));
        quadF(b, vec3(sinkX + sw * 0.5f, by0, bz), vec3(sinkX + sw * 0.5f, by1, bz), vec3(sinkX + sw * 0.5f, by1, H), vec3(sinkX + sw * 0.5f, by0, H), vec3(-1, 0, 0), Gy(0.72f), M(MAT_METAL_BRUSHED));
        disc(b, vec3(sinkX, (by0 + by1) * 0.5f, bz + 0.002f), 0.025f, 8, Gy(0.3f), M(MAT_METAL_BRUSHED));
        // faucet (goose neck)
        cyl(b, vec3(sinkX, 0.06f, H), 0.025f, 0.02f, 0.05f, 10, Gy(0.85f), M(MAT_CHROME), true);
        tube(b, vec3(sinkX, 0.06f, H), vec3(sinkX, 0.06f, H + 0.3f), 0.012f, 8, Gy(0.85f), M(MAT_CHROME));
        tube(b, vec3(sinkX, 0.06f, H + 0.3f), vec3(sinkX, 0.2f, H + 0.3f), 0.012f, 8, Gy(0.85f), M(MAT_CHROME));
        tube(b, vec3(sinkX, 0.2f, H + 0.3f), vec3(sinkX, 0.24f, H + 0.22f), 0.012f, 8, Gy(0.85f), M(MAT_CHROME), true);
        tube(b, vec3(sinkX + 0.06f, 0.06f, H + 0.05f), vec3(sinkX + 0.06f, 0.1f, H + 0.14f), 0.008f, 6, Gy(0.85f), M(MAT_CHROME), true);
        // dish rack with plates next to the sink
        float rx = sinkX + sw * 0.5f + 0.25f;
        if (rx < hl - 0.2f) {
            box(b, vec3(rx, dp * 0.5f, H + 0.01f), vec3(0.2f, 0.16f, 0.01f), Gy(0.9f), M(MAT_PAINT_WHITE), SK_NZ);
            for (int k = 0; k < 4; k++) {
                b.pushAxes(vec3(rx - 0.12f + k * 0.07f, dp * 0.5f, H + 0.13f), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1, 0, 0));
                disc(b, vec3(0.f), 0.11f, 14, Gy(0.95f), M(MAT_PAINT_WHITE));
                b.pop();
            }
        }
    } else {
        box(b, vec3(0, dp * 0.5f + 0.01f, H - 0.02f), vec3(hl, dp * 0.5f + 0.01f, 0.02f), topCol, M(MAT_MARBLE), SK_NONE);
    }
    // stove: front, oven door with window, knobs, cooktop with burners
    if (stove) {
        float sx = stoveX, sw = uw * 0.5f - 0.004f;
        box(b, vec3(sx, dp * 0.5f - 0.01f, 0.45f), vec3(sw, dp * 0.5f - 0.01f, 0.44f), Gy(0.85f), M(MAT_METAL_BRUSHED), SK_NY | SK_PZ);
        box(b, vec3(sx, dp - 0.01f, 0.4f), vec3(sw - 0.04f, 0.012f, 0.25f), Gy(0.12f), M(MAT_PLASTIC), SK_NZ | SK_NY);
        box(b, vec3(sx, dp + 0.004f, 0.42f), vec3(sw - 0.1f, 0.002f, 0.13f), C(0.12f, 0.1f, 0.08f), M(MAT_GLASS), SK_NZ);
        tube(b, vec3(sx - sw + 0.08f, dp + 0.04f, 0.67f), vec3(sx + sw - 0.08f, dp + 0.04f, 0.67f), 0.012f, 8, Gy(0.85f), M(MAT_CHROME), true);
        for (int k = 0; k < 4; k++) cyl(b, vec3(sx - sw + 0.1f + k * (sw * 2.f - 0.2f) / 3.f, dp + 0.002f, 0.8f), 0.02f, 0.018f, 0.0f, 10, Gy(0.2f), M(MAT_PLASTIC), true);
        for (int k = 0; k < 4; k++) {
            b.pushAxes(vec3(sx - sw + 0.1f + k * (sw * 2.f - 0.2f) / 3.f, dp - 0.01f, 0.8f), vec3(1, 0, 0), vec3(0, 0, -1), vec3(0, 1, 0));
            cyl(b, vec3(0.f), 0.022f, 0.02f, 0.03f, 10, Gy(0.15f), M(MAT_PLASTIC), true);
            b.pop();
        }
        box(b, vec3(sx, dp * 0.5f, H - 0.005f), vec3(sw, dp * 0.5f, 0.005f), Gy(0.05f), M(MAT_GLASS), SK_NZ);
        for (int k = 0; k < 4; k++) {
            float bx = sx + ((k & 1) ? 0.14f : -0.14f), by = dp * 0.5f + ((k & 2) ? 0.13f : -0.13f);
            cyl(b, vec3(bx, by, H), 0.085f, 0.085f, 0.004f, 14, C(0.2f, 0.2f, 0.2f), M(MAT_METAL_BRUSHED), true);
        }
        // a pot on the stove
        cyl(b, vec3(sx + 0.14f, dp * 0.5f - 0.13f, H + 0.004f), 0.1f, 0.1f, 0.14f, 14, Gy(0.75f), M(MAT_METAL_BRUSHED), true);
        tube(b, vec3(sx + 0.24f, dp * 0.5f - 0.13f, H + 0.12f), vec3(sx + 0.36f, dp * 0.5f - 0.13f, H + 0.13f), 0.01f, 6, Gy(0.1f), M(MAT_PLASTIC), true);
        // hood
        if (uppers) {
            lathe(b, vec3(sx, dp * 0.45f, 1.55f), {vec2(0.4f, 0.f), vec2(0.36f, 0.1f), vec2(0.12f, 0.3f), vec2(0.12f, 0.75f)}, 4, Gy(0.8f), M(MAT_METAL_BRUSHED), false);
            light(b, vec3(sx, dp * 0.5f, 1.5f), vec3(1.f, 0.85f, 0.6f) * 35.f, 2.2f, room, vec3(0, 0, -1), 70.f, 40.f);
        }
    }
    // backsplash tiles + upper cabinets
    if (uppers) {
        box(b, vec3(0, 0.005f, (H + 1.45f) * 0.5f), vec3(hl, 0.005f, (1.45f - H) * 0.5f), C(0.92f, 0.94f, 0.95f), M(MAT_TILE), SK_NY);
        float uz0 = 1.45f, uz1 = 2.15f, udp = 0.34f;
        for (int k = 0; k < units; k++) {
            float x = -hl + uw * (k + 0.5f);
            if (fabsf(x - stoveX) < uw * 0.6f) continue;
            box(b, vec3(x, udp * 0.5f, (uz0 + uz1) * 0.5f), vec3(uw * 0.5f, udp * 0.5f, (uz1 - uz0) * 0.5f), cabCol, wm, SK_NY);
            box(b, vec3(x, udp + 0.004f, (uz0 + uz1) * 0.5f), vec3(uw * 0.5f - 0.006f, 0.004f, (uz1 - uz0) * 0.5f - 0.006f), C(rgbOf(cabCol) * 1.06f), wm, SK_NZ | SK_NY);
            box(b, vec3(x + uw * 0.5f - 0.06f, udp + 0.016f, uz0 + 0.08f), vec3(0.006f, 0.008f, 0.06f), Gy(0.75f), M(MAT_CHROME), SK_NZ);
            // under-cabinet light strip
            box(b, vec3(x, udp * 0.6f, uz0 - 0.003f), vec3(uw * 0.4f, 0.015f, 0.003f), C(1.f, 0.92f, 0.8f, 0.6f), EM(), SK_PZ);
        }
    }
    // clutter on the counter: kettle, toaster, knife block, cutting board, fruit bowl
    {
        InPart ip(b, IP_DETAIL);
        float x = -hl + 0.25f;
        if (x < sinkX - 0.5f || !sink) {
            lathe(b, vec3(x, dp * 0.45f, H), {vec2(0.08f, 0.f), vec2(0.09f, 0.1f), vec2(0.07f, 0.2f), vec2(0.04f, 0.22f)}, 12, C(hsv(r.f(), 0.5f, 0.7f)), M(MAT_METAL_PAINTED), true);
            tube(b, vec3(x + 0.08f, dp * 0.45f, H + 0.12f), vec3(x + 0.13f, dp * 0.45f, H + 0.18f), 0.012f, 6, C(hsv(r.f(), 0.5f, 0.7f)), M(MAT_METAL_PAINTED), true);
        }
        float x2 = hl - 0.3f;
        if (fabsf(x2 - stoveX) > uw && fabsf(x2 - sinkX) > 0.5f) {
            rbox(b, vec3(x2, dp * 0.4f, H + 0.1f), vec3(0.14f, 0.09f, 0.1f), 0.03f, Gy(0.85f), M(MAT_METAL_BRUSHED));
            box(b, vec3(x2 - 0.03f, dp * 0.4f, H + 0.2f), vec3(0.08f, 0.01f, 0.002f), Gy(0.1f), M(MAT_PLASTIC), SK_NZ);
        }
        box(b, vec3(0.1f, dp * 0.5f, H + 0.01f), vec3(0.18f, 0.12f, 0.01f), C(0.6f, 0.42f, 0.25f), M(MAT_WOOD), SK_NZ);
        lathe(b, vec3(-0.25f + (sink ? 0.f : 0.3f), dp * 0.55f, H), {vec2(0.05f, 0.f), vec2(0.13f, 0.06f), vec2(0.14f, 0.08f)}, 14, C(0.85f, 0.8f, 0.7f), M(MAT_PAINT_WHITE), false);
        for (int k = 0; k < 4; k++) sphere(b, vec3(-0.25f + (sink ? 0.f : 0.3f) + (k - 1.5f) * 0.05f, dp * 0.55f + (k & 1) * 0.04f, H + 0.09f), 0.035f, 8,
                                           k < 2 ? C(0.9f, 0.75f, 0.1f) : C(0.2f, 0.6f, 0.15f), M(MAT_PAINT_WHITE));
    }
    collide(b, vec3(0, dp * 0.5f, H * 0.5f), vec3(hl, dp * 0.5f, H * 0.5f));
}

// Refrigerator (front +y): top freezer, handles, magnets and a note
void fridge(IB& b, vec3 p, float yaw, u32 col, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    float H = 1.82f, w = 0.7f, dp = 0.68f;
    rbox(b, vec3(0, dp * 0.5f, H * 0.5f), vec3(w * 0.5f, dp * 0.5f, H * 0.5f), 0.025f, col, M(MAT_METAL_BRUSHED), true);
    box(b, vec3(0, dp + 0.003f, 1.28f), vec3(w * 0.5f - 0.01f, 0.003f, 0.004f), Gy(0.3f), M(MAT_PLASTIC), SK_NZ);   // door split
    tube(b, vec3(-w * 0.5f + 0.05f, dp + 0.04f, 1.36f), vec3(-w * 0.5f + 0.05f, dp + 0.04f, 1.6f), 0.012f, 6, Gy(0.7f), M(MAT_CHROME), true);
    tube(b, vec3(-w * 0.5f + 0.05f, dp + 0.04f, 0.8f), vec3(-w * 0.5f + 0.05f, dp + 0.04f, 1.2f), 0.012f, 6, Gy(0.7f), M(MAT_CHROME), true);
    InPart ip(b, IP_DETAIL);
    for (int k = 0; k < 6; k++) {
        vec3 mc = hsv(r.f(), 0.7f, 0.9f);
        box(b, vec3(r.range(-0.2f, 0.2f), dp + 0.006f, r.range(0.9f, 1.7f)), vec3(r.range(0.02f, 0.05f), 0.004f, r.range(0.02f, 0.05f)), C(mc), M(MAT_PAINT_WHITE), SK_NZ);
    }
    box(b, vec3(0.05f, dp + 0.005f, 1.05f), vec3(0.07f, 0.001f, 0.1f), Gy(0.95f), M(MAT_PAINT_WHITE), SK_NZ);   // note / photo
    InPart ip2(b, IP_FURNITURE);
    collide(b, vec3(0, dp * 0.5f, H * 0.5f), vec3(w * 0.5f, dp * 0.5f, H * 0.5f));
}

// ------------------------------------------------------------------------------------------------ bathroom
void toilet(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    u32 ch = Gy(0.95f), mat = M(MAT_PAINT_WHITE);
    lathe(b, vec3(0, 0.05f, 0.f), {vec2(0.12f, 0.f), vec2(0.11f, 0.2f), vec2(0.17f, 0.36f), vec2(0.19f, 0.4f)}, 14, ch, mat, false);
    b.pushAxes(vec3(0, 0.05f, 0.41f), vec3(1, 0, 0), vec3(0, 1.25f, 0), vec3(0, 0, 1));   // stretched oval seat (scaled y axis)
    lathe(b, vec3(0.f), {vec2(0.19f, 0.f), vec2(0.2f, 0.015f), vec2(0.12f, 0.03f)}, 18, Gy(0.97f), mat, false);
    b.pop();
    rbox(b, vec3(0, -0.2f, 0.62f), vec3(0.2f, 0.09f, 0.2f), 0.03f, ch, mat, true);   // tank
    rbox(b, vec3(0, -0.2f, 0.83f), vec3(0.21f, 0.1f, 0.015f), 0.01f, ch, mat, true);
    box(b, vec3(0.13f, -0.2f, 0.86f), vec3(0.02f, 0.02f, 0.01f), Gy(0.8f), M(MAT_CHROME), SK_NZ);
    collide(b, vec3(0, -0.05f, 0.4f), vec3(0.2f, 0.28f, 0.4f));
}

void bathSink(IB& b, vec3 p, float yaw, u32 cab, int room) {
    At at(b, p, yaw);
    rbox(b, vec3(0, 0.25f, 0.42f), vec3(0.4f, 0.24f, 0.4f), 0.01f, cab, M(MAT_WOOD), true);
    rbox(b, vec3(0, 0.26f, 0.83f), vec3(0.42f, 0.26f, 0.02f), 0.01f, Gy(0.92f), M(MAT_MARBLE));
    // vessel basin
    lathe(b, vec3(0, 0.26f, 0.85f), {vec2(0.1f, 0.f), vec2(0.17f, 0.06f), vec2(0.19f, 0.13f), vec2(0.18f, 0.13f), vec2(0.16f, 0.07f), vec2(0.02f, 0.02f)}, 18, Gy(0.97f), M(MAT_PAINT_WHITE), false);
    tube(b, vec3(0, 0.03f, 0.85f), vec3(0, 0.03f, 1.12f), 0.012f, 8, Gy(0.85f), M(MAT_CHROME));
    tube(b, vec3(0, 0.03f, 1.12f), vec3(0, 0.16f, 1.1f), 0.012f, 8, Gy(0.85f), M(MAT_CHROME), true);
    // mirror cabinet with a light bar
    rbox(b, vec3(0, 0.07f, 1.6f), vec3(0.4f, 0.07f, 0.35f), 0.01f, Gy(0.9f), M(MAT_PAINT_WHITE), true);
    box(b, vec3(0, 0.141f, 1.6f), vec3(0.38f, 0.001f, 0.33f), Gy(0.95f), M(MAT_CHROME), SK_NZ);
    box(b, vec3(0, 0.1f, 2.0f), vec3(0.35f, 0.04f, 0.03f), C(1.f, 0.95f, 0.85f, 0.8f), EM(), SK_NONE);
    light(b, vec3(0, 0.35f, 1.98f), vec3(1.f, 0.93f, 0.82f) * 60.f, 3.f, room);
    // soap, toothbrush cup
    rbox(b, vec3(0.28f, 0.2f, 0.87f), vec3(0.035f, 0.025f, 0.02f), 0.01f, C(0.9f, 0.6f, 0.7f), M(MAT_PAINT_WHITE));
    cyl(b, vec3(-0.28f, 0.2f, 0.85f), 0.03f, 0.03f, 0.1f, 10, C(0.8f, 0.9f, 0.95f, 1.f), M(MAT_GLASS), false);
    tube(b, vec3(-0.28f, 0.2f, 0.9f), vec3(-0.27f, 0.21f, 1.02f), 0.005f, 4, C(0.2f, 0.5f, 0.9f), M(MAT_PLASTIC));
    collide(b, vec3(0, 0.26f, 0.43f), vec3(0.42f, 0.26f, 0.43f));
}

// Bathtub with shower (against a wall along local x, front +y), tiled apron, rod and curtain
void bathtub(IB& b, vec3 p, float yaw, float len, u32 curtainCol) {
    At at(b, p, yaw);
    float hl = len * 0.5f, dp = 0.75f, H = 0.56f;
    u32 white = Gy(0.96f), mat = M(MAT_PAINT_WHITE);
    // rim + apron
    box(b, vec3(0, dp * 0.5f, H - 0.03f), vec3(hl, dp * 0.5f, 0.03f), white, mat, SK_NONE);
    box(b, vec3(0, dp - 0.02f, (H - 0.06f) * 0.5f), vec3(hl, 0.02f, (H - 0.06f) * 0.5f), C(0.85f, 0.9f, 0.92f), M(MAT_TILE), SK_NZ);
    // basin (inner faces)
    float ix0 = -hl + 0.08f, ix1 = hl - 0.08f, iy0 = 0.08f, iy1 = dp - 0.08f, bz = 0.12f, tz = H - 0.06f;
    quadF(b, vec3(ix0, iy0, bz), vec3(ix1, iy0, bz), vec3(ix1, iy1, bz), vec3(ix0, iy1, bz), vec3(0, 0, 1), white, mat);
    quadF(b, vec3(ix0, iy0, bz), vec3(ix1, iy0, bz), vec3(ix1, iy0, tz), vec3(ix0, iy0, tz), vec3(0, 1, 0), white, mat);
    quadF(b, vec3(ix0, iy1, bz), vec3(ix1, iy1, bz), vec3(ix1, iy1, tz), vec3(ix0, iy1, tz), vec3(0, -1, 0), white, mat);
    quadF(b, vec3(ix0, iy0, bz), vec3(ix0, iy1, bz), vec3(ix0 - 0.05f, iy1, tz), vec3(ix0 - 0.05f, iy0, tz), vec3(1, 0, 0.3f), white, mat);
    quadF(b, vec3(ix1, iy0, bz), vec3(ix1, iy1, bz), vec3(ix1, iy1, tz), vec3(ix1, iy0, tz), vec3(-1, 0, 0), white, mat);
    // rim top surfaces around the basin
    box(b, vec3(0, iy0 * 0.5f, H), vec3(hl, iy0 * 0.5f, 0.004f), white, mat, SK_NZ);
    box(b, vec3(0, (iy1 + dp) * 0.5f, H), vec3(hl, (dp - iy1) * 0.5f, 0.004f), white, mat, SK_NZ);
    box(b, vec3((-hl + ix0) * 0.5f, dp * 0.5f, H), vec3((ix0 + hl) * 0.5f, (iy1 - iy0) * 0.5f, 0.004f), white, mat, SK_NZ);
    box(b, vec3((hl + ix1) * 0.5f, dp * 0.5f, H), vec3((hl - ix1) * 0.5f, (iy1 - iy0) * 0.5f, 0.004f), white, mat, SK_NZ);
    // shower head, mixer
    tube(b, vec3(hl - 0.05f, 0.03f, H + 0.3f), vec3(hl - 0.05f, 0.03f, 1.95f), 0.012f, 8, Gy(0.85f), M(MAT_CHROME));
    tube(b, vec3(hl - 0.05f, 0.03f, 1.95f), vec3(hl - 0.2f, 0.25f, 1.9f), 0.012f, 8, Gy(0.85f), M(MAT_CHROME));
    lathe(b, vec3(hl - 0.2f, 0.25f, 1.86f), {vec2(0.02f, 0.04f), vec2(0.08f, 0.f)}, 12, Gy(0.85f), M(MAT_CHROME), false);
    cyl(b, vec3(hl - 0.05f, 0.04f, H + 0.25f), 0.04f, 0.04f, 0.02f, 10, Gy(0.85f), M(MAT_CHROME), true);
    // curtain rod + curtain (wavy, partially drawn)
    tube(b, vec3(-hl, dp - 0.04f, 2.0f), vec3(hl, dp - 0.04f, 2.0f), 0.01f, 8, Gy(0.85f), M(MAT_CHROME), true);
    int folds = (int)(len * 0.6f / 0.08f);
    for (int k = 0; k < folds; k++) {
        float x0 = -hl + 0.03f + k * 0.08f, x1 = x0 + 0.08f;
        float y0 = dp - 0.04f + ((k & 1) ? 0.03f : -0.03f), y1 = dp - 0.04f + ((k & 1) ? -0.03f : 0.03f);
        quadF(b, vec3(x0, y0, 0.25f), vec3(x1, y1, 0.25f), vec3(x1, y1, 1.97f), vec3(x0, y0, 1.97f), vec3(0, 1, 0), curtainCol, M(MAT_CLOTH));
        quadF(b, vec3(x0, y0, 0.25f), vec3(x1, y1, 0.25f), vec3(x1, y1, 1.97f), vec3(x0, y0, 1.97f), vec3(0, -1, 0), C(rgbOf(curtainCol) * 0.8f), M(MAT_CLOTH));
    }
    collide(b, vec3(0, dp * 0.5f, H * 0.5f), vec3(hl, dp * 0.5f, H * 0.5f));
}

void towelRail(IB& b, vec3 p, float yaw, u32 towelCol) {
    At at(b, p, yaw);
    tube(b, vec3(-0.3f, 0.07f, 1.2f), vec3(0.3f, 0.07f, 1.2f), 0.012f, 8, Gy(0.85f), M(MAT_CHROME), true);
    for (int s = -1; s <= 1; s += 2) tube(b, vec3(s * 0.3f, 0.f, 1.2f), vec3(s * 0.3f, 0.07f, 1.2f), 0.01f, 6, Gy(0.85f), M(MAT_CHROME));
    rbox(b, vec3(0.f, 0.07f, 0.95f), vec3(0.25f, 0.025f, 0.26f), 0.02f, towelCol, M(MAT_CLOTH), true);
}

// ------------------------------------------------------------------------------------------------ ceiling & walls
// Ceiling fan with a light kit (static blades at an angle)
void ceilingFan(IB& b, vec3 top, int room, u32 bladeCol, float cd = 120.f) {
    At at(b, top, 0.3f);
    cyl(b, vec3(0, 0, -0.03f), 0.06f, 0.06f, 0.03f, 12, Gy(0.85f), M(MAT_METAL_BRUSHED), false, true);
    cyl(b, vec3(0, 0, -0.35f), 0.012f, 0.012f, 0.32f, 6, Gy(0.85f), M(MAT_METAL_BRUSHED), false);
    lathe(b, vec3(0, 0, -0.5f), {vec2(0.02f, 0.f), vec2(0.11f, 0.03f), vec2(0.12f, 0.1f), vec2(0.05f, 0.16f)}, 14, Gy(0.85f), M(MAT_METAL_BRUSHED), false);
    for (int k = 0; k < 5; k++) {
        float a = kTwoPi * k / 5.f;
        b.push(vec3(0, 0, -0.44f), a);
        b.pushAxes(vec3(0.42f, 0, 0), vec3(1, 0, 0), normalize(vec3(0, 1, 0.15f)), normalize(vec3(0, -0.15f, 1)));
        rbox(b, vec3(0.f), vec3(0.3f, 0.07f, 0.006f), 0.004f, bladeCol, M(MAT_WOOD), true);
        b.pop();
        b.pop();
    }
    sphere(b, vec3(0, 0, -0.58f), 0.09f, 10, C(1.f, 0.93f, 0.8f, 0.8f), EM(), 0.8f);
    light(b, vec3(0, 0, -0.62f), vec3(1.f, 0.82f, 0.6f) * cd, 5.5f, room);
}

// Wall clock (face +y)
void wallClock(IB& b, vec3 c, u32 seed) {
    Rng r(seed);
    b.pushAxes(c, vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0));
    cyl(b, vec3(0.f), 0.17f, 0.17f, 0.04f, 20, C(hsv(r.f(), 0.3f, 0.3f)), M(MAT_PLASTIC), false, false);
    disc(b, vec3(0, 0, 0.035f), 0.15f, 20, Gy(0.95f), M(MAT_PAINT_WHITE));
    for (int k = 0; k < 12; k++) {
        float a = kTwoPi * k / 12.f;
        box(b, vec3(cosf(a) * 0.13f, sinf(a) * 0.13f, 0.037f), vec3(0.005f, 0.005f, 0.001f), Gy(0.1f), M(MAT_PAINT_WHITE), SK_NZ);
    }
    b.pushAxes(vec3(0, 0, 0.038f), normalize(vec3(cosf(1.1f), sinf(1.1f), 0)), normalize(vec3(-sinf(1.1f), cosf(1.1f), 0)), vec3(0, 0, 1));
    box(b, vec3(0.05f, 0, 0), vec3(0.05f, 0.004f, 0.001f), Gy(0.05f), M(MAT_PAINT_WHITE), SK_NZ);
    b.pop();
    b.pushAxes(vec3(0, 0, 0.039f), normalize(vec3(cosf(-0.6f), sinf(-0.6f), 0)), normalize(vec3(-sinf(-0.6f), cosf(-0.6f), 0)), vec3(0, 0, 1));
    box(b, vec3(0.07f, 0, 0), vec3(0.07f, 0.003f, 0.001f), Gy(0.05f), M(MAT_PAINT_WHITE), SK_NZ);
    b.pop();
    b.pop();
}

// Laundry pile / clothes thrown over something (a lumpy heap)
void clothesPile(IB& b, vec3 p, u32 seed) {
    Rng r(seed);
    for (int k = 0; k < 5; k++) {
        vec3 c = hsv(r.f(), r.range(0.2f, 0.7f), r.range(0.2f, 0.8f));
        b.push(p + vec3(r.range(-0.15f, 0.15f), r.range(-0.12f, 0.12f), k * 0.035f), r.f() * kTwoPi);
        rbox(b, vec3(0, 0, 0.03f), vec3(r.range(0.12f, 0.22f), r.range(0.1f, 0.18f), 0.03f), 0.025f, C(c), M(MAT_CLOTH));
        b.pop();
    }
}

// Beer / soda cans and a pizza box (bachelor clutter)
void clutterCans(IB& b, vec3 p, int n, u32 seed) {
    Rng r(seed);
    for (int k = 0; k < n; k++) {
        vec3 q = p + vec3(r.range(-0.25f, 0.25f), r.range(-0.2f, 0.2f), 0.f);
        if (r.chance(0.3f)) {
            // crushed can lying on its side
            b.pushAxes(q + vec3(0, 0, 0.033f), vec3(0, 0, 1), vec3(0, 1, 0), vec3(-1, 0, 0));
            cyl(b, vec3(0, 0, -0.06f), 0.033f, 0.03f, 0.1f, 8, C(hsv(r.f(), 0.7f, 0.7f)), M(MAT_METAL_PAINTED), true, true);
            b.pop();
        } else {
            can(b, q, 0.033f, 0.123f, C(hsv(r.f(), 0.7f, 0.7f)), Gy(0.75f));
        }
    }
}
void pizzaBox(IB& b, vec3 p, float yaw, bool open) {
    At at(b, p, yaw);
    box(b, vec3(0, 0, 0.022f), vec3(0.2f, 0.2f, 0.022f), C(0.75f, 0.62f, 0.45f), M(MAT_METAL_PAINTED), SK_NZ);
    if (open) {
        b.pushAxes(vec3(0, -0.2f, 0.044f), vec3(1, 0, 0), normalize(vec3(0, -0.4f, 1)), normalize(vec3(0, -1, -0.4f)));
        box(b, vec3(0, 0.2f, 0.003f), vec3(0.2f, 0.2f, 0.003f), C(0.78f, 0.65f, 0.48f), M(MAT_METAL_PAINTED), SK_NONE);
        b.pop();
        disc(b, vec3(0, 0, 0.045f), 0.17f, 16, C(0.85f, 0.55f, 0.25f), M(MAT_PAINT_WHITE));
        for (int k = 0; k < 7; k++) disc(b, vec3(cosf(k * 0.9f) * 0.1f, sinf(k * 0.9f) * 0.1f, 0.047f), 0.02f, 8, C(0.6f, 0.1f, 0.05f), M(MAT_PAINT_WHITE));
    } else {
        textC(b, "PIZZA", vec3(0, 0, 0.0445f), vec3(1, 0, 0), vec3(0, 1, 0), 0.06f, 0.01f, C(0.8f, 0.1f, 0.1f), M(MAT_PAINT_WHITE));
    }
}

}  // namespace ikit
}  // namespace World
