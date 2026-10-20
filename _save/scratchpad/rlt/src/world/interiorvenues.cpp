// Venues: Mama Lucha's diner (window booths, counter with stools, kitchen behind a pass-through, hallway to the
// restrooms) and Club Riptide (open-air beach club: LED dance floor under a light truss, DJ stage with a video wall,
// palapa bar, VIP deck with cabanas), laid out where the story mission stages it (story_act2.cpp buildClub).
#include "interiors.h"
#include "sites.h"

namespace World {
namespace ikit {

// ------------------------------------------------------------------------------------------------ diner pieces
// Condiment set: napkin dispenser, ketchup, mustard, salt and pepper (base on the table top)
void condiments(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    InPart ip(b, IP_DETAIL);
    rbox(b, vec3(0, 0, 0.07f), vec3(0.06f, 0.045f, 0.07f), 0.01f, Gy(0.85f), M(MAT_CHROME), true);
    box(b, vec3(0, 0.046f, 0.075f), vec3(0.045f, 0.001f, 0.05f), Gy(0.97f), M(MAT_PAINT_WHITE), SK_NZ);
    bottle(b, vec3(0.1f, 0.01f, 0.f), 0.026f, 0.19f, C(0.75f, 0.06f, 0.04f), M(MAT_PLASTIC), C(0.8f, 0.1f, 0.05f));
    bottle(b, vec3(0.16f, 0.01f, 0.f), 0.026f, 0.19f, C(0.95f, 0.75f, 0.05f), M(MAT_PLASTIC), C(0.95f, 0.8f, 0.1f));
    for (int k = 0; k < 2; k++) {
        cyl(b, vec3(-0.1f - k * 0.05f, 0.01f, 0.f), 0.018f, 0.02f, 0.07f, 8, C(0.9f, 0.92f, 0.95f, 1.f), M(MAT_GLASS), false);
        lathe(b, vec3(-0.1f - k * 0.05f, 0.01f, 0.07f), {vec2(0.02f, 0.f), vec2(0.02f, 0.012f), vec2(0.01f, 0.022f)}, 8, Gy(0.85f), M(MAT_CHROME), true);
        disc(b, vec3(-0.1f - k * 0.05f, 0.01f, 0.004f), 0.017f, 8, k == 0 ? Gy(0.97f) : Gy(0.12f), M(MAT_PAINT_WHITE));
    }
}

// Window booth: the table end against the wall at local y = 0, benches along local y at x = +-, local +y into the room.
// Unit width 1.92 m (neighbouring units share bench backs).
void boothUnit(IB& b, vec3 p, float yaw, u32 vinyl, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    const float tl = 1.2f, tw = 0.76f, bx = tw * 0.5f + 0.02f, bd = 0.56f, L = tl + 0.12f;
    u32 chrome = M(MAT_CHROME), leather = M(MAT_LEATHER);
    rbox(b, vec3(0, tl * 0.5f + 0.02f, 0.745f), vec3(tw * 0.5f, tl * 0.5f, 0.016f), 0.006f, C(0.92f, 0.9f, 0.86f), M(MAT_MARBLE));
    box(b, vec3(0, tl * 0.5f + 0.02f, 0.742f), vec3(tw * 0.5f + 0.005f, tl * 0.5f + 0.005f, 0.017f), Gy(0.85f), chrome, SK_PZ);
    cyl(b, vec3(0, tl * 0.6f, 0.f), 0.2f, 0.19f, 0.02f, 16, Gy(0.25f), chrome, true);
    cyl(b, vec3(0, tl * 0.6f, 0.02f), 0.04f, 0.04f, 0.71f, 10, Gy(0.8f), chrome, false);
    box(b, vec3(0, 0.05f, 0.68f), vec3(0.05f, 0.05f, 0.05f), Gy(0.3f), M(MAT_METAL_PAINTED), SK_NONE);
    collide(b, vec3(0, tl * 0.5f + 0.02f, 0.38f), vec3(tw * 0.5f, tl * 0.5f, 0.38f));
    for (int sd = -1; sd <= 1; sd += 2) {
        float x = sd * (bx + bd * 0.5f);
        box(b, vec3(x, L * 0.5f, 0.2f), vec3(bd * 0.5f - 0.03f, L * 0.5f, 0.2f), C(rgbOf(vinyl) * 0.45f), M(MAT_METAL_PAINTED), SK_NZ);
        rbox(b, vec3(x - sd * 0.03f, L * 0.5f, 0.44f), vec3(bd * 0.5f - 0.05f, L * 0.5f - 0.01f, 0.065f), 0.045f, vinyl, leather);
        // tufted back: three rolls
        for (int k = 0; k < 3; k++)
            rbox(b, vec3(x + sd * (bd * 0.5f - 0.09f), L * 0.5f, 0.64f + k * 0.16f), vec3(0.08f, L * 0.5f - 0.01f, 0.085f), 0.06f, vinyl, leather, k == 0);
        tube(b, vec3(x + sd * (bd * 0.5f - 0.09f), 0.f, 1.1f), vec3(x + sd * (bd * 0.5f - 0.09f), L, 1.1f), 0.022f, 8, Gy(0.88f), chrome, true);
        collide(b, vec3(x, L * 0.5f, 0.3f), vec3(bd * 0.5f, L * 0.5f, 0.3f));
        collide(b, vec3(x + sd * (bd * 0.5f - 0.09f), L * 0.5f, 0.85f), vec3(0.09f, L * 0.5f, 0.3f));
        // diners (optional): two seats per bench, facing the table
        float yawIn = sd < 0 ? -kHalfPi : kHalfPi;
        scenario(b, vec3(x - sd * 0.05f, 0.42f, 0.f), yawIn, 6, SR_PATRON, SF_OPTIONAL);
        if (r.chance(0.5f)) scenario(b, vec3(x - sd * 0.05f, 0.95f, 0.f), yawIn, 6, SR_PATRON, SF_OPTIONAL);
    }
    condiments(b, vec3(0.f, 0.2f, 0.761f), 0.f);
    // tabletop jukebox selector at the wall end
    {
        InPart ip(b, IP_DETAIL);
        rbox(b, vec3(0.f, 0.07f, 0.84f), vec3(0.14f, 0.05f, 0.08f), 0.02f, Gy(0.85f), chrome, true);
        box(b, vec3(0.f, 0.121f, 0.85f), vec3(0.1f, 0.001f, 0.05f), C(1.f, 0.85f, 0.55f, 0.7f), EM(), SK_NZ);
        for (int k = 0; k < 2; k++) box(b, vec3(-0.06f + k * 0.12f, 0.122f, 0.79f), vec3(0.025f, 0.004f, 0.008f), C(0.9f, 0.2f, 0.15f), M(MAT_PLASTIC), SK_NZ);
        // place settings: mugs / plates on some tables
        if (r.chance(0.6f)) {
            disc(b, vec3(-0.18f, 0.75f, 0.763f), 0.12f, 14, Gy(0.97f), M(MAT_PAINT_WHITE));
            cyl(b, vec3(0.2f, 0.62f, 0.762f), 0.042f, 0.042f, 0.09f, 10, Gy(0.95f), M(MAT_PAINT_WHITE), false);
            disc(b, vec3(0.2f, 0.62f, 0.8515f), 0.036f, 10, C(0.25f, 0.13f, 0.06f), M(MAT_PAINT_WHITE));
        }
        box(b, vec3(0.2f, 0.95f, 0.764f), vec3(0.12f, 0.17f, 0.003f), C(0.85f, 0.12f, 0.1f), M(MAT_PAINT_WHITE), SK_NZ);   // menu
    }
}

// Stainless cook line (front +y, back against a wall at y = 0): griddle, range with pots, fryers, under a hood
void cookLine(IB& b, vec3 p, float yaw, float len, int roomIdx, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 steel = M(MAT_METAL_BRUSHED);
    float hl = len * 0.5f, dp = 0.8f, H = 0.92f;
    box(b, vec3(0, dp * 0.5f, H * 0.5f), vec3(hl, dp * 0.5f, H * 0.5f), Gy(0.72f), steel, SK_NY);
    int n = Max(2, (int)(len / 0.9f));
    float uw = len / n;
    for (int k = 0; k < n; k++) {
        float x = -hl + uw * (k + 0.5f);
        int kind = k % 3;
        // door / knobs on the front
        box(b, vec3(x, dp + 0.004f, 0.42f), vec3(uw * 0.5f - 0.02f, 0.004f, 0.3f), Gy(0.78f), steel, SK_NZ);
        tube(b, vec3(x - uw * 0.3f, dp + 0.04f, 0.68f), vec3(x + uw * 0.3f, dp + 0.04f, 0.68f), 0.012f, 6, Gy(0.85f), M(MAT_CHROME), true);
        for (int kk = 0; kk < 3; kk++) {
            b.pushAxes(vec3(x - uw * 0.25f + kk * uw * 0.25f, dp, 0.82f), vec3(1, 0, 0), vec3(0, 0, -1), vec3(0, 1, 0));
            cyl(b, vec3(0.f), 0.022f, 0.02f, 0.03f, 8, Gy(0.1f), M(MAT_PLASTIC), true);
            b.pop();
        }
        if (kind == 0) {
            // flat-top griddle with a splash guard and a spatula
            box(b, vec3(x, dp * 0.5f, H + 0.012f), vec3(uw * 0.5f - 0.01f, dp * 0.5f - 0.04f, 0.012f), Gy(0.12f), steel, SK_NZ);
            box(b, vec3(x, 0.06f, H + 0.1f), vec3(uw * 0.5f - 0.01f, 0.01f, 0.1f), Gy(0.75f), steel, SK_NZ);
            InPart ip(b, IP_DETAIL);
            for (int e = 0; e < 4; e++) disc(b, vec3(x - 0.25f + e * 0.16f, dp * 0.5f + r.range(-0.1f, 0.1f), H + 0.0255f), 0.055f, 10, C(0.35f, 0.18f, 0.08f), M(MAT_PAINT_WHITE));
            box(b, vec3(x + 0.3f, dp * 0.7f, H + 0.03f), vec3(0.05f, 0.08f, 0.003f), Gy(0.8f), steel, SK_NZ);
        } else if (kind == 1) {
            // range: grates and two pots
            box(b, vec3(x, dp * 0.5f, H + 0.004f), vec3(uw * 0.5f - 0.01f, dp * 0.5f - 0.02f, 0.004f), Gy(0.08f), steel, SK_NZ);
            for (int e = 0; e < 4; e++) {
                float gx = x + ((e & 1) ? 0.2f : -0.2f), gy = dp * 0.5f + ((e & 2) ? 0.18f : -0.18f);
                box(b, vec3(gx, gy, H + 0.02f), vec3(0.15f, 0.012f, 0.012f), Gy(0.1f), M(MAT_METAL_PAINTED), SK_NZ);
                box(b, vec3(gx, gy, H + 0.02f), vec3(0.012f, 0.15f, 0.012f), Gy(0.1f), M(MAT_METAL_PAINTED), SK_NZ);
            }
            cyl(b, vec3(x - 0.2f, dp * 0.5f - 0.18f, H + 0.03f), 0.14f, 0.14f, 0.22f, 14, Gy(0.75f), steel, true);
            cyl(b, vec3(x + 0.2f, dp * 0.5f + 0.18f, H + 0.03f), 0.12f, 0.12f, 0.08f, 14, Gy(0.2f), M(MAT_METAL_PAINTED), true);
        } else {
            // twin fryers with baskets
            for (int e = -1; e <= 1; e += 2) {
                box(b, vec3(x + e * uw * 0.24f, dp * 0.5f, H + 0.004f), vec3(uw * 0.22f, dp * 0.35f, 0.004f), C(0.55f, 0.4f, 0.1f), M(MAT_GLASS), SK_NZ);
                box(b, vec3(x + e * uw * 0.24f, dp * 0.5f, H + 0.09f), vec3(uw * 0.2f, dp * 0.2f, 0.05f), Gy(0.6f), steel, SK_NZ | SK_PZ);
                tube(b, vec3(x + e * uw * 0.24f, dp * 0.7f, H + 0.12f), vec3(x + e * uw * 0.24f, dp + 0.1f, H + 0.2f), 0.01f, 6, Gy(0.1f), M(MAT_PLASTIC), true);
            }
        }
    }
    // hood with filters and a lamp strip
    float hz0 = 1.95f, hz1 = 2.55f;
    box(b, vec3(0, 0.55f, hz1 - 0.02f), vec3(hl + 0.1f, 0.55f, 0.02f), Gy(0.7f), steel, SK_NONE);
    box(b, vec3(0, 1.08f, (hz0 + hz1) * 0.5f), vec3(hl + 0.1f, 0.02f, (hz1 - hz0) * 0.5f), Gy(0.75f), steel, SK_NONE);
    for (int e = -1; e <= 1; e += 2) box(b, vec3(e * (hl + 0.08f), 0.55f, (hz0 + hz1) * 0.5f), vec3(0.02f, 0.55f, (hz1 - hz0) * 0.5f), Gy(0.72f), steel, SK_NONE);
    box(b, vec3(0, 0.3f, hz1 - 0.2f), vec3(hl, 0.02f, 0.18f), Gy(0.4f), steel, SK_NONE);
    box(b, vec3(0, 0.9f, hz0 + 0.02f), vec3(hl * 0.9f, 0.02f, 0.01f), C(1.f, 0.92f, 0.8f, 0.8f), EM(), SK_PZ);
    box(b, vec3(0, 0.55f, hz1 + 0.15f), vec3(0.2f, 0.2f, 0.15f), Gy(0.7f), steel, SK_NONE);
    light(b, vec3(0, 0.7f, hz0 - 0.1f), vec3(1.f, 0.9f, 0.75f) * 160.f, 3.5f, roomIdx, vec3(0, 0, -1), 75.f, 45.f);
    collide(b, vec3(0, dp * 0.5f, H * 0.5f), vec3(hl, dp * 0.5f, H * 0.5f));
}

// Stainless work table with an undershelf (centered)
void prepTable(IB& b, vec3 p, float yaw, float len, float wd, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 steel = M(MAT_METAL_BRUSHED);
    box(b, vec3(0, 0, 0.89f), vec3(len * 0.5f, wd * 0.5f, 0.02f), Gy(0.78f), steel, SK_NONE);
    box(b, vec3(0, 0, 0.2f), vec3(len * 0.5f - 0.04f, wd * 0.5f - 0.04f, 0.01f), Gy(0.7f), steel, SK_NONE);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) tube(b, vec3(sx * (len * 0.5f - 0.05f), sy * (wd * 0.5f - 0.05f), 0.f), vec3(sx * (len * 0.5f - 0.05f), sy * (wd * 0.5f - 0.05f), 0.87f), 0.02f, 6, Gy(0.8f), steel);
    InPart ip(b, IP_DETAIL);
    for (int k = 0; k < 3; k++) disc(b, vec3(-len * 0.3f + k * 0.3f, r.range(-0.1f, 0.1f), 0.911f), 0.13f, 14, Gy(0.97f), M(MAT_PAINT_WHITE));
    box(b, vec3(len * 0.3f, 0.f, 0.925f), vec3(0.2f, 0.14f, 0.015f), C(0.95f, 0.95f, 0.9f), M(MAT_PLASTIC), SK_NZ);
    for (int k = 0; k < 4; k++) productBox(b, vec3(-len * 0.3f + k * 0.2f, 0.f, 0.21f), vec3(0.08f, 0.12f, 0.1f), C(hsv(r.f(), 0.5f, 0.6f)), Gy(0.9f));
    InPart ip2(b, IP_FURNITURE);
    collide(b, vec3(0, 0, 0.45f), vec3(len * 0.5f, wd * 0.5f, 0.45f));
}

// Wire shelving unit with kitchen stock (back against a wall at y = 0)
void wireShelf(IB& b, vec3 p, float yaw, float len, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 chrome = M(MAT_CHROME);
    float dp = 0.45f;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = 0; sy <= 1; sy++) tube(b, vec3(sx * (len * 0.5f - 0.02f), 0.03f + sy * (dp - 0.06f), 0.f), vec3(sx * (len * 0.5f - 0.02f), 0.03f + sy * (dp - 0.06f), 1.8f), 0.013f, 6, Gy(0.75f), chrome);
    for (int lv = 0; lv < 4; lv++) {
        float z = 0.15f + lv * 0.55f;
        box(b, vec3(0, dp * 0.5f, z), vec3(len * 0.5f, dp * 0.5f, 0.012f), Gy(0.7f), chrome, SK_NONE);
        InPart ip(b, IP_DETAIL);
        float x = -len * 0.5f + 0.06f;
        while (x < len * 0.5f - 0.2f) {
            int kind = r.irange(0, 2);
            if (kind == 0) {
                float rr = r.range(0.08f, 0.13f);
                can(b, vec3(x + rr, dp * 0.5f, z + 0.012f), rr, r.range(0.15f, 0.25f), C(hsv(r.f(), 0.5f, 0.7f)), Gy(0.7f));
                x += rr * 2.f + 0.03f;
            } else if (kind == 1) {
                float w = r.range(0.2f, 0.35f);
                productBox(b, vec3(x + w * 0.5f, dp * 0.5f, z + 0.012f), vec3(w * 0.5f, 0.16f, r.range(0.1f, 0.2f)), C(hsv(r.f(), 0.3f, 0.75f)), Gy(0.95f));
                x += w + 0.03f;
            } else {
                cyl(b, vec3(x + 0.15f, dp * 0.5f, z + 0.012f), 0.15f, 0.15f, 0.14f, 14, Gy(0.75f), M(MAT_METAL_BRUSHED), false);
                x += 0.33f;
            }
        }
    }
    collide(b, vec3(0, dp * 0.5f, 0.9f), vec3(len * 0.5f, dp * 0.5f, 0.9f));
}

// Reach-in refrigerator (stainless, two doors), back against a wall at y = 0
void reachIn(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    float w = 1.2f, dp = 0.8f, H = 2.0f;
    box(b, vec3(0, dp * 0.5f, H * 0.5f), vec3(w * 0.5f, dp * 0.5f, H * 0.5f), Gy(0.78f), M(MAT_METAL_BRUSHED), SK_NZ);
    box(b, vec3(0, dp + 0.002f, H * 0.5f + 0.05f), vec3(0.004f, 0.002f, H * 0.5f - 0.1f), Gy(0.3f), M(MAT_METAL_PAINTED), SK_NZ);
    for (int e = -1; e <= 1; e += 2) tube(b, vec3(e * 0.06f, dp + 0.04f, 1.1f), vec3(e * 0.06f, dp + 0.04f, 1.5f), 0.014f, 6, Gy(0.85f), M(MAT_CHROME), true);
    box(b, vec3(0, dp + 0.002f, H - 0.08f), vec3(0.08f, 0.002f, 0.03f), C(0.2f, 0.9f, 0.4f, 0.8f), EM(), SK_NZ);
    collide(b, vec3(0, dp * 0.5f, H * 0.5f), vec3(w * 0.5f, dp * 0.5f, H * 0.5f));
}

// Dish sink with a sprayer and a dishwasher hood (back against a wall at y = 0)
void dishStation(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    u32 steel = M(MAT_METAL_BRUSHED);
    box(b, vec3(0, 0.35f, 0.45f), vec3(0.9f, 0.35f, 0.45f), Gy(0.72f), steel, SK_NY);
    box(b, vec3(0, 0.35f, 0.905f), vec3(0.9f, 0.35f, 0.005f), Gy(0.8f), steel, SK_NZ);
    for (int e = -1; e <= 1; e += 2) box(b, vec3(e * 0.4f, 0.37f, 0.91f), vec3(0.28f, 0.22f, 0.002f), Gy(0.3f), steel, SK_NZ);
    box(b, vec3(0, 0.02f, 1.15f), vec3(0.9f, 0.02f, 0.25f), Gy(0.8f), steel, SK_NY);
    tube(b, vec3(0.f, 0.05f, 0.95f), vec3(0.f, 0.05f, 1.75f), 0.014f, 6, Gy(0.85f), M(MAT_CHROME));
    tube(b, vec3(0.f, 0.05f, 1.75f), vec3(0.f, 0.3f, 1.6f), 0.01f, 6, Gy(0.2f), M(MAT_RUBBER));
    rbox(b, vec3(1.35f, 0.38f, 1.0f), vec3(0.34f, 0.36f, 0.55f), 0.02f, Gy(0.8f), steel, true);
    collide(b, vec3(0.2f, 0.37f, 0.6f), vec3(1.5f, 0.37f, 0.6f));
}

// Menu board (dark panel, lines of text with prices), facing +y at local center c
void menuBoard(IB& b, vec3 c, float w, float h, const char* title, const char* const* lines, int n, u32 seed) {
    Rng r(seed);
    box(b, c + vec3(0, 0.02f, 0.f), vec3(w * 0.5f + 0.04f, 0.02f, h * 0.5f + 0.04f), C(0.35f, 0.22f, 0.12f), M(MAT_WOOD), SK_NY);
    box(b, c + vec3(0, 0.041f, 0.f), vec3(w * 0.5f, 0.001f, h * 0.5f), C(0.05f, 0.06f, 0.06f), M(MAT_PAINT_WHITE), SK_NZ | SK_NY);
    float th = Min(0.1f, h * 0.14f);
    textC(b, title, c + vec3(0, 0.043f, h * 0.5f - th * 0.9f), vec3(-1, 0, 0), vec3(0, 0, 1), th, th * 0.16f, C(1.f, 0.85f, 0.35f), M(MAT_PAINT_WHITE));
    float lh = Min(0.055f, (h - th * 1.8f) / (n + 0.5f) * 0.72f);
    for (int i = 0; i < n; i++) {
        float z = h * 0.5f - th * 1.8f - (i + 0.6f) * (h - th * 1.8f) / (n + 0.5f);
        text(b, lines[i], c + vec3(w * 0.5f - 0.05f, 0.043f, z), vec3(-1, 0, 0), vec3(0, 0, 1), lh, lh * 0.15f, Gy(0.95f), M(MAT_PAINT_WHITE));
    }
}

// Neon script sign on a backing rail (faces +y), animated glow (pattern 4 = pulse)
void neonSign(IB& b, vec3 c, const char* s, float h, vec3 col, u32 anim, u32 phase, int roomIdx, float cd) {
    InPart ip(b, IP_SHELL);
    textC(b, s, c + vec3(0, 0.05f, 0.f), vec3(-1, 0, 0), vec3(0, 0, 1), h, h * 0.09f, C(col, 0.95f), EM(anim, phase), 0.012f);
    box(b, c + vec3(0, 0.02f, 0.f), vec3(Min(3.f, (float)strlen(s) * h * 0.4f), 0.01f, 0.012f), Gy(0.3f), M(MAT_METAL_PAINTED), SK_NONE);
    if (cd > 0.f) light(b, c + vec3(0, 0.5f, 0.f), col * cd, 3.5f + h * 4.f, roomIdx);
}

// ------------------------------------------------------------------------------------------------ Mama Lucha's
void layoutDiner(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0xD1E7u);
    const float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT, H = d.ceil;
    float dx0 = 0.f, dx1 = 0.f, dh = 2.2f;
    mainDoorOpening(d, dx0, dx1, dh);
    const float doorX = (dx0 + dx1) * 0.5f;
    const float pt = 0.12f;
    // bands front to back: booths, aisle, stools + counter, work aisle, back counter / pass-through wall, kitchen, back
    const float yC0 = Y0 + 4.3f, yC1 = yC0 + 0.72f, yB = yC1 + 1.0f, yP = yB + 0.62f;
    const float yK = Min(yP + 4.8f, Y1 - 2.7f);
    const float xh = X1 - 1.36f;                      // kitchen | hallway partition center
    int dine = room(b, vec3(X0, Y0, 0.f), vec3(X1, yP - pt * 0.5f, H), vec3(40.f, 33.f, 25.f), 0.1f, LS_BUSINESS);
    int kitchen = room(b, vec3(X0, yP + pt * 0.5f, 0.f), vec3(xh - pt * 0.5f, yK - pt * 0.5f, H), vec3(42.f, 42.f, 40.f), 0.f, LS_BUSINESS);
    int hall = room(b, vec3(xh + pt * 0.5f, yP + pt * 0.5f, 0.f), vec3(X1, Y1, H), vec3(22.f, 19.f, 15.f), 0.f, LS_BUSINESS);
    int store = room(b, vec3(X0, yK + pt * 0.5f, 0.f), vec3(xh - pt * 0.5f, Y1, H), vec3(14.f, 14.f, 13.f), 0.f, LS_ALWAYS);
    // ---- doors, openings between rooms
    storefrontEntrance(b, DK_HINGED_PAIR, 1, Gy(0.75f));
    const float kdX = X0 + 0.95f, sdY = yK + 1.0f, ksX = X0 + 1.3f;
    door(b, vec3(kdX, yP, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.1f, DK_HINGED, 2, Gy(0.78f), false);
    door(b, vec3(xh, sdY, 0.f), vec2(0, 1), vec2(-1, 0), 0.85f, 2.05f, DK_HINGED, 0, C(0.5f, 0.32f, 0.2f), false);
    door(b, vec3(ksX, yK, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.05f, DK_HINGED, 2, Gy(0.7f), false);
    const float arch0 = xh + pt * 0.5f + 0.12f, arch1 = X1 - 0.12f, archH = 2.3f;
    const float pc = (X0 + 1.6f + xh) * 0.5f, pw = Min(2.4f, (xh - X0) - 3.4f);   // pass-through center / width
    const float pz0 = 1.12f, pz1 = 1.8f;
    {
        const InteriorRoom& rd = d.rooms[dine];
        const InteriorRoom& rk = d.rooms[kitchen];
        const InteriorRoom& rh = d.rooms[hall];
        tExtraHoles.push_back({dine, 2, {arch0 - rd.mn.x, arch1 - rd.mn.x, 0.f, archH}});
        tExtraHoles.push_back({hall, 0, {rh.mx.x - arch1, rh.mx.x - arch0, 0.f, archH}});
        tExtraHoles.push_back({dine, 2, {pc - pw * 0.5f - rd.mn.x, pc + pw * 0.5f - rd.mn.x, pz0, pz1}});
        tExtraHoles.push_back({kitchen, 0, {rk.mx.x - (pc + pw * 0.5f), rk.mx.x - (pc - pw * 0.5f), pz0, pz1}});
    }
    partitionX(b, X0, X1, yP, pt, H, {vec2(kdX - 0.45f, kdX + 0.45f), vec2(arch0, arch1)});
    partitionY(b, yP + pt * 0.5f, Y1, xh, pt, H, {vec2(sdY - 0.43f, sdY + 0.43f)});
    partitionX(b, X0, xh, yK, pt, H, {vec2(ksX - 0.45f, ksX + 0.45f)});
    // ---- shells: checkerboard floor, two-tone walls with a chrome rail, tiled kitchen
    ShellStyle ds;
    ds.checker = true;
    ds.checkA = Gy(0.92f);
    ds.checkB = Gy(0.07f);
    ds.checkSize = 0.3f;
    ds.floorMat = M(MAT_TILE);
    ds.wallCol = C(0.96f, 0.9f, 0.76f);
    ds.wainscotH = 1.05f;
    ds.wainCol = C(0.1f, 0.55f, 0.55f);
    ds.wainMat = M(MAT_METAL_PAINTED);
    ds.chairRail = 1.07f;
    ds.baseCol = Gy(0.85f);
    ds.baseMat = M(MAT_CHROME);
    ds.baseH = 0.1f;
    ds.ceilMat = M(MAT_CEILING_TILE);
    ds.ceilCol = Gy(0.95f);
    ds.revealCol = C(0.1f, 0.5f, 0.5f);
    shell(b, dine, ds);
    ShellStyle hs = ds;
    shell(b, hall, hs);
    ShellStyle ks;
    ks.wallCol = Gy(0.95f);
    ks.wallMat = M(MAT_TILE);
    ks.floorMat = M(MAT_TILE);
    ks.floorCol = C(0.62f, 0.3f, 0.22f);
    ks.floorUV = 1.f;
    ks.ceilMat = M(MAT_CEILING_TILE);
    ks.ceilCol = Gy(0.93f);
    ks.baseH = 0.12f;
    ks.baseCol = C(0.5f, 0.25f, 0.2f);
    ks.baseMat = M(MAT_TILE);
    shell(b, kitchen, ks);
    ShellStyle ss = ks;
    ss.wallMat = M(MAT_PLASTER);
    ss.wallCol = Gy(0.82f);
    ss.floorMat = M(MAT_CONCRETE);
    ss.floorCol = Gy(0.7f);
    shell(b, store, ss);
    doorFrameInner(b, vec3(kdX, yP, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.1f, pt, Gy(0.8f));
    doorFrameInner(b, vec3(xh, sdY, 0.f), vec2(0, 1), vec2(-1, 0), 0.85f, 2.05f, pt, Gy(0.9f));
    doorFrameInner(b, vec3(ksX, yK, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.05f, pt, Gy(0.8f));
    {
        // archway lining to the restroom hallway, pass-through lining with a stainless shelf
        InPart ip(b, IP_SHELL);
        reveal(b, vec3(arch0, yP - pt * 0.5f, 0.f), vec3(1, 0, 0), vec3(0, 1, 0), 0.f, arch1 - arch0, 0.f, archH, 0.f, pt, C(0.1f, 0.5f, 0.5f), M(MAT_PLASTER), false);
        reveal(b, vec3(pc - pw * 0.5f, yP - pt * 0.5f, 0.f), vec3(1, 0, 0), vec3(0, 1, 0), 0.f, pw, pz0, pz1, 0.f, pt, Gy(0.75f), M(MAT_METAL_BRUSHED), false);
        box(b, vec3(pc, yP, pz0 + 0.015f), vec3(pw * 0.5f + 0.05f, pt * 0.5f + 0.18f, 0.015f), Gy(0.8f), M(MAT_METAL_BRUSHED), SK_NONE);
        // heat lamps and the order rail over the pass
        for (int k = 0; k < 3; k++) {
            float x = pc - pw * 0.33f + k * pw * 0.33f;
            box(b, vec3(x, yP + 0.2f, pz1 - 0.06f), vec3(0.12f, 0.08f, 0.04f), Gy(0.6f), M(MAT_METAL_BRUSHED), SK_NONE);
            box(b, vec3(x, yP + 0.2f, pz1 - 0.101f), vec3(0.1f, 0.06f, 0.001f), C(1.f, 0.35f, 0.1f, 0.8f), EM(), SK_PZ);
            light(b, vec3(x, yP + 0.1f, pz0 + 0.3f), vec3(1.f, 0.4f, 0.15f) * 25.f, 1.5f, kitchen);
        }
        box(b, vec3(pc, yP - pt * 0.5f - 0.05f, pz1 + 0.06f), vec3(pw * 0.5f, 0.02f, 0.02f), Gy(0.85f), M(MAT_CHROME), SK_NONE);
        InPart ip2(b, IP_DETAIL);
        for (int k = 0; k < 5; k++) {
            float x = pc - pw * 0.42f + k * pw * 0.2f + r.range(-0.05f, 0.05f);
            box(b, vec3(x, yP - pt * 0.5f - 0.072f, pz1 - 0.02f), vec3(0.04f, 0.001f, 0.07f), C(1.f, 0.98f, 0.9f), M(MAT_PAINT_WHITE), SK_NZ);
        }
        for (int k = 0; k < 2; k++) {
            float x = pc - pw * 0.2f + k * pw * 0.4f;
            disc(b, vec3(x, yP, pz0 + 0.031f), 0.13f, 14, Gy(0.97f), M(MAT_PAINT_WHITE));
            lathe(b, vec3(x, yP, pz0 + 0.031f), {vec2(0.08f, 0.f), vec2(0.07f, 0.03f), vec2(0.03f, 0.05f)}, 10, C(0.8f, 0.55f, 0.3f), M(MAT_PAINT_WHITE), true);
        }
    }
    // ---- booths along the front windows
    {
        u32 vinyl = C(0.72f, 0.06f, 0.07f);
        const float uw = 1.92f;
        float gap0 = Min(dx0, dx1) - 0.7f, gap1 = Max(dx0, dx1) + 0.7f;
        for (int side = 0; side < 2; side++) {
            float a = side == 0 ? X0 : gap1, c = side == 0 ? gap0 : X1;
            int n = (int)floorf((c - a) / uw);
            float start = side == 0 ? c - n * uw : a;   // hug the door side for the left run, the wall for the right
            if (side == 0) start = a + ((c - a) - n * uw);
            for (int k = 0; k < n; k++) boothUnit(b, vec3(start + uw * (k + 0.5f), Y0, 0.f), 0.f, vinyl, r.next());
        }
        // globe pendants over the booths
        for (float x = X0 + 1.f; x < X1 - 0.8f; x += 1.92f)
            if (x < gap0 || x > gap1) pendant(b, vec3(x, Y0 + 0.75f, H), H - 1.75f, dine, Gy(0.85f), 110.f, vec3(1.f, 0.85f, 0.62f), 4.5f, 1);
    }
    // ---- counter with stools, register at the door-side end
    const float cxa = X0 + 1.5f, cxb = X1 - 2.3f;
    {
        u32 front = C(0.72f, 0.06f, 0.07f), chrome = M(MAT_CHROME);
        float cl = cxb - cxa, cm = (cxa + cxb) * 0.5f;
        box(b, vec3(cm, (yC0 + yC1) * 0.5f + 0.05f, 0.5f), vec3(cl * 0.5f, (yC1 - yC0) * 0.5f - 0.05f, 0.5f), front, M(MAT_METAL_PAINTED), SK_NZ);
        box(b, vec3(cm, yC0 + 0.1f, 0.12f), vec3(cl * 0.5f, 0.012f, 0.07f), Gy(0.25f), M(MAT_METAL_PAINTED), SK_NZ);   // kick
        for (int k = 0; k < 2; k++) box(b, vec3(cm, yC0 + 0.095f, 0.45f + k * 0.35f), vec3(cl * 0.5f + 0.005f, 0.006f, 0.025f), Gy(0.9f), chrome, SK_NONE);
        rbox(b, vec3(cm, (yC0 + yC1) * 0.5f, 1.02f), vec3(cl * 0.5f + 0.06f, (yC1 - yC0) * 0.5f + 0.06f, 0.025f), 0.012f, C(0.9f, 0.88f, 0.84f), M(MAT_MARBLE));
        box(b, vec3(cm, (yC0 + yC1) * 0.5f, 1.0f), vec3(cl * 0.5f + 0.066f, (yC1 - yC0) * 0.5f + 0.066f, 0.02f), Gy(0.88f), chrome, SK_PZ);
        tube(b, vec3(cxa, yC0 - 0.12f, 0.2f), vec3(cxb, yC0 - 0.12f, 0.2f), 0.025f, 8, Gy(0.9f), chrome, true);   // foot rail
        for (float x = cxa + 0.3f; x < cxb; x += 1.2f) tube(b, vec3(x, yC0 - 0.12f, 0.2f), vec3(x, yC0 + 0.06f, 0.2f), 0.015f, 6, Gy(0.9f), chrome);
        collide(b, vec3(cm, (yC0 + yC1) * 0.5f, 0.52f), vec3(cl * 0.5f + 0.06f, (yC1 - yC0) * 0.5f + 0.06f, 0.52f));
        int ns = 0;
        for (float x = cxa + 0.35f; x < cxb - 0.2f; x += 0.66f, ns++) {
            barStool(b, vec3(x, yC0 - 0.42f, 0.f), C(0.72f, 0.06f, 0.07f));
            collide(b, vec3(x, yC0 - 0.42f, 0.38f), vec3(0.17f, 0.17f, 0.38f));
            if (ns % 2 == 1) scenario(b, vec3(x, yC0 - 0.42f, 0.f), 0.f, 6, SR_PATRON, SF_OPTIONAL, 0.26f);
            if (ns % 3 == 0) condiments(b, vec3(x + 0.33f, (yC0 + yC1) * 0.5f, 1.045f), kPi);
        }
        InPart ip(b, IP_DETAIL);
        // cake stand with a pie under a glass dome, stacked coffee cups, the register at the right end
        vec3 cs(cm + cl * 0.2f, (yC0 + yC1) * 0.5f + 0.1f, 1.045f);
        lathe(b, cs, {vec2(0.08f, 0.f), vec2(0.03f, 0.02f), vec2(0.025f, 0.12f), vec2(0.17f, 0.13f), vec2(0.17f, 0.14f)}, 16, Gy(0.95f), M(MAT_GLASS), true);
        cyl(b, cs + vec3(0, 0, 0.14f), 0.14f, 0.13f, 0.05f, 16, C(0.9f, 0.72f, 0.4f), M(MAT_PAINT_WHITE), true);
        lathe(b, cs + vec3(0, 0, 0.14f), {vec2(0.165f, 0.f), vec2(0.165f, 0.1f), vec2(0.12f, 0.2f), vec2(0.f, 0.23f)}, 16, C(0.9f, 0.95f, 1.f, 1.f), kGlassMat, false);
        for (int k = 0; k < 4; k++) cyl(b, vec3(cm - cl * 0.25f + (k % 2) * 0.1f, (yC0 + yC1) * 0.5f + 0.15f, 1.045f + (k / 2) * 0.1f), 0.045f, 0.035f, 0.09f, 10, Gy(0.96f), M(MAT_PAINT_WHITE), true);
        vec3 rp(cxb - 0.35f, (yC0 + yC1) * 0.5f + 0.05f, 1.045f);
        rbox(b, rp + vec3(0, 0, 0.06f), vec3(0.2f, 0.18f, 0.06f), 0.02f, Gy(0.15f), M(MAT_PLASTIC), true);
        b.pushAxes(rp + vec3(0, 0.02f, 0.2f), vec3(-1, 0, 0), normalize(vec3(0, -1.f, 0.4f)), normalize(vec3(0, 0.4f, 1.f)));
        rbox(b, vec3(0.f), vec3(0.16f, 0.012f, 0.1f), 0.008f, Gy(0.1f), M(MAT_PLASTIC), true);
        box(b, vec3(0, 0.0125f, 0.f), vec3(0.14f, 0.001f, 0.08f), C(0.3f, 0.6f, 0.9f, 0.6f), EM(), SK_NZ);
        b.pop();
        marker(b, IM_COUNTER, vec3(cxb - 0.35f, yC0 - 0.75f, 0.f), 0.f);
    }
    // ---- behind the counter: back counter with coffee, shakes and pies, menu boards and the neon name
    {
        float bcl = (xh - pt) - (X0 + 1.5f);
        coffeeStation(b, vec3(pc - pw * 0.5f - 1.3f, yP - pt * 0.5f, 0.f), kPi, Min(1.6f, pc - pw * 0.5f - X0 - 1.6f), r.next());
        {
            // back counter under the pass (base cabinets)
            float xa = pc - pw * 0.5f - 0.2f, xc = Min(xh - pt * 0.5f - 0.05f, cxb + 0.3f);
            box(b, vec3((xa + xc) * 0.5f, yP - pt * 0.5f - 0.3f, 0.45f), vec3((xc - xa) * 0.5f, 0.3f, 0.45f), C(0.1f, 0.5f, 0.5f), M(MAT_METAL_PAINTED), SK_NZ);
            box(b, vec3((xa + xc) * 0.5f, yP - pt * 0.5f - 0.3f, 0.915f), vec3((xc - xa) * 0.5f + 0.02f, 0.32f, 0.015f), Gy(0.8f), M(MAT_METAL_BRUSHED), SK_NONE);
            collide(b, vec3((xa + xc) * 0.5f, yP - pt * 0.5f - 0.3f, 0.46f), vec3((xc - xa) * 0.5f, 0.3f, 0.46f));
            InPart ip(b, IP_DETAIL);
            // milkshake mixer with steel cups
            vec3 mp(xc - 0.4f, yP - pt * 0.5f - 0.3f, 0.93f);
            rbox(b, mp + vec3(0, 0, 0.2f), vec3(0.09f, 0.11f, 0.2f), 0.03f, C(0.1f, 0.6f, 0.6f), M(MAT_METAL_PAINTED), true);
            for (int k = -1; k <= 1; k++) lathe(b, mp + vec3(k * 0.07f, 0.13f, 0.08f), {vec2(0.03f, 0.f), vec2(0.045f, 0.17f)}, 10, Gy(0.85f), M(MAT_METAL_BRUSHED), false);
            for (int k = 0; k < 3; k++) {
                vec3 pp(xa + 0.4f + k * 0.35f, yP - pt * 0.5f - 0.35f, 0.93f);
                lathe(b, pp, {vec2(0.07f, 0.f), vec2(0.025f, 0.02f), vec2(0.02f, 0.1f), vec2(0.15f, 0.11f), vec2(0.15f, 0.12f)}, 14, Gy(0.92f), M(MAT_GLASS), true);
                cyl(b, pp + vec3(0, 0, 0.12f), 0.13f, 0.12f, 0.045f, 14, k == 1 ? C(0.35f, 0.2f, 0.1f) : C(0.85f, 0.75f, 0.2f), M(MAT_PAINT_WHITE), true);
            }
        }
        (void)bcl;
        static const char* kBreakfast[] = {"PANCAKE STACK        6.50", "HUEVOS RANCHEROS     8.95", "GUAVA FRENCH TOAST   7.25", "EGGS ANY STYLE       5.75",
                                           "CAFE CON LECHE       2.75"};
        static const char* kLunch[] = {"CUBAN SANDWICH       9.50", "LUCHA BURGER + FRIES 10.95", "FRITA CUBANA         8.50", "BLACK BEANS + RICE   4.25",
                                       "KEY LIME PIE         4.25"};
        At at(b, vec3(pc, yP - pt * 0.5f, 0.f), kPi);
        InPart ip(b, IP_SHELL);
        float mw = Min(1.5f, (pw + 1.6f) * 0.5f);
        menuBoard(b, vec3(-mw * 0.55f, 0.f, 2.45f), mw, 0.78f, "BREAKFAST ALL DAY", kBreakfast, 5, r.next());
        menuBoard(b, vec3(mw * 0.55f, 0.f, 2.45f), mw, 0.78f, "LUNCH + DINNER", kLunch, 5, r.next());
        if (H > 3.3f) neonSign(b, vec3(0.f, 0.f, Min(H - 0.3f, 3.2f)), "Mama Lucha's", 0.34f, vec3(1.f, 0.25f, 0.6f), 4, 17, dine, 60.f);
        for (int k = -1; k <= 1; k += 2) light(b, vec3(k * mw * 0.55f, 0.8f, 3.05f), vec3(1.f, 0.9f, 0.75f) * 60.f, 2.5f, dine, vec3(0, -0.6f, -1.f), 50.f, 30.f);
    }
    // globe pendants over the counter, ceiling fans, neon ribbon along the crown
    for (float x = cxa + 0.6f; x < cxb - 0.2f; x += 1.7f) pendant(b, vec3(x, (yC0 + yC1) * 0.5f, H), H - 2.05f, dine, Gy(0.85f), 170.f, vec3(1.f, 0.86f, 0.64f), 5.f, 1);
    ceilingFan(b, vec3(X0 + (X1 - X0) * 0.3f, Y0 + 3.2f, H), dine, C(0.3f, 0.2f, 0.12f), 0.f);
    ceilingFan(b, vec3(X0 + (X1 - X0) * 0.72f, Y0 + 3.2f, H), dine, C(0.3f, 0.2f, 0.12f), 0.f);
    {
        InPart ip(b, IP_SHELL);
        u32 neon = C(0.2f, 1.f, 0.9f, 0.9f);
        box(b, vec3((X0 + X1) * 0.5f, yP - pt * 0.5f - 0.03f, H - 0.12f), vec3((X1 - X0) * 0.5f, 0.012f, 0.012f), neon, EM(4, 3), SK_NONE);
        box(b, vec3(X0 + 0.03f, (Y0 + yP) * 0.5f, H - 0.12f), vec3(0.012f, (yP - Y0) * 0.5f - 0.1f, 0.012f), neon, EM(4, 3), SK_NONE);
        box(b, vec3(X1 - 0.03f, (Y0 + yP) * 0.5f, H - 0.12f), vec3(0.012f, (yP - Y0) * 0.5f - 0.1f, 0.012f), neon, EM(4, 3), SK_NONE);
    }
    // ---- entrance zone: OPEN sign in the window, jukebox, gumball machine, pictures, a clock
    {
        float sx = doorX + (doorX > (X0 + X1) * 0.5f ? -2.3f : 2.3f);
        {
            At at(b, vec3(sx, 0.2f, 0.f), kPi);   // faces the street
            InPart ip(b, IP_SHELL);
            textC(b, "OPEN", vec3(0.f, 0.012f, 2.25f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.28f, 0.03f, C(1.f, 0.15f, 0.1f, 0.95f), EM(7, 5));
            box(b, vec3(0.f, -0.01f, 2.25f), vec3(0.62f, 0.004f, 0.24f), C(0.1f, 0.4f, 1.f, 0.9f), EM(4, 9), SK_NONE);
            box(b, vec3(0.f, -0.005f, 2.25f), vec3(0.6f, 0.006f, 0.22f), Gy(0.04f), M(MAT_PLASTIC), SK_NONE);
        }
        // jukebox in the corner by the counter end
        vec3 jp(X1 - 0.45f, yC0 - 0.6f, 0.f);
        At at(b, jp, kHalfPi);
        rbox(b, vec3(0, 0.f, 0.6f), vec3(0.45f, 0.3f, 0.6f), 0.05f, C(0.5f, 0.3f, 0.15f), M(MAT_WOOD), true);
        b.pushAxes(vec3(0, 0.f, 1.2f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0));
        cyl(b, vec3(0, 0, -0.3f), 0.45f, 0.45f, 0.6f, 16, C(1.f, 0.6f, 0.2f, 0.8f), EM(3, 40), true, true);
        b.pop();
        box(b, vec3(0, 0.301f, 0.95f), vec3(0.3f, 0.001f, 0.15f), C(1.f, 0.9f, 0.6f, 0.6f), EM(), SK_NZ);
        for (int k = 0; k < 6; k++) box(b, vec3(-0.3f + k * 0.12f, 0.302f, 0.55f), vec3(0.012f, 0.001f, 0.25f), C(hsv(k / 6.f, 0.8f, 1.f), 0.8f), EM(2, (u32)k * 20u), SK_NZ);
        light(b, vec3(0, 0.6f, 1.0f), vec3(1.f, 0.6f, 0.3f) * 40.f, 2.5f, dine, vec3(0.f), 0.f, 0.f, 5, 7);
        collide(b, vec3(0, 0, 0.7f), vec3(0.45f, 0.3f, 0.7f));
    }
    {
        vec3 gp(doorX + (doorX > (X0 + X1) * 0.5f ? 1.3f : -1.3f), Y0 + 0.6f, 0.f);
        if (gp.x > X0 + 0.4f && gp.x < X1 - 0.4f) {
            lathe(b, gp, {vec2(0.14f, 0.f), vec2(0.14f, 0.02f), vec2(0.04f, 0.05f), vec2(0.04f, 0.8f), vec2(0.12f, 0.82f), vec2(0.12f, 0.95f)}, 12, C(0.75f, 0.08f, 0.08f), M(MAT_METAL_PAINTED), true);
            sphere(b, gp + vec3(0, 0, 0.95f), 0.17f, 14, C(0.95f, 0.97f, 1.f, 1.f), kGlassMat);
            Rng gr(d.seed);
            InPart ip(b, IP_DETAIL);
            for (int k = 0; k < 16; k++) {
                vec3 o = gr.onSphere() * 0.1f;
                o.z = -fabsf(o.z) * 0.6f;
                sphere(b, gp + vec3(0, 0, 1.05f) + o, 0.03f, 6, C(hsv(gr.f(), 0.8f, 0.95f)), M(MAT_PLASTIC));
            }
            lathe(b, gp + vec3(0, 0, 1.12f), {vec2(0.08f, 0.f), vec2(0.06f, 0.05f), vec2(0.02f, 0.07f)}, 12, C(0.75f, 0.08f, 0.08f), M(MAT_METAL_PAINTED), true);
            collide(b, gp + vec3(0, 0, 0.6f), vec3(0.15f, 0.15f, 0.6f));
        }
        for (int k = 0; k < 3; k++) {
            float y = Y0 + 1.2f + k * 1.1f;
            if (y > yC0 - 0.8f) break;
            At at(b, vec3(X0, y, 0.f), -kHalfPi);
            picture(b, vec3(0.f, 0.f, 1.8f), 0.55f, 0.42f, r.next(), Gy(0.85f));
        }
        At at(b, vec3(X1, Y0 + 2.2f, 0.f), kHalfPi);
        wallClock(b, vec3(0.f, 0.f, 2.3f), r.next());
    }
    // ---- kitchen: cook line on the back partition, prep table behind the pass, reach-in, dish station, shelving
    {
        float kx0 = X0, kx1 = xh - pt * 0.5f, ky0 = yP + pt * 0.5f, ky1 = yK - pt * 0.5f;
        float lineL = Min(3.6f, kx1 - kx0 - 2.2f);
        float lineX = kx0 + 1.4f + lineL * 0.5f;
        cookLine(b, vec3(lineX, ky1, 0.f), kPi, lineL, kitchen, r.next());
        prepTable(b, vec3(pc, ky0 + 1.05f, 0.f), 0.f, Min(2.0f, pw + 0.2f), 0.7f, r.next());
        reachIn(b, vec3(kx1, ky0 + 1.9f, 0.f), kHalfPi);
        dishStation(b, vec3(kx1, ky1 - 1.6f, 0.f), kHalfPi);
        wireShelf(b, vec3(kx0, (ky0 + ky1) * 0.5f + 0.4f, 0.f), -kHalfPi, 1.5f, r.next());
        for (int k = 0; k < 2; k++) troffer(b, vec3(kx0 + (kx1 - kx0) * (0.3f + 0.4f * k), (ky0 + ky1) * 0.5f, H), 1.2f, 0.6f, kitchen, 520.f, vec3(1.f, 0.98f, 0.94f), 6.5f);
        scenario(b, vec3(lineX - 0.3f, ky1 - 1.25f, 0.f), kPi, 0, SR_COOK, SF_STAFF);
        scenario(b, vec3(pc + 0.4f, ky0 + 1.75f, 0.f), kPi, 0, SR_COOK, SF_STAFF | SF_OPTIONAL);
    }
    // ---- staff out front: a waitress behind the counter, another by the booths
    scenario(b, vec3((cxa + cxb) * 0.5f - 0.8f, yC1 + 0.45f, 0.f), kPi, 7, SR_WAITER, SF_STAFF);
    scenario(b, vec3(X0 + (X1 - X0) * 0.35f, Y0 + 2.9f, 0.f), -0.3f, 7, SR_WAITER, SF_STAFF | SF_OPTIONAL);
    // ---- hallway: restroom door (decorative) at the end, sign
    {
        float hx = (xh + X1) * 0.5f + 0.03f;
        At at(b, vec3(hx, Y1, 0.f), kPi);
        InPart ip(b, IP_FURNITURE);
        b.pushAxes(vec3(-0.43f, 0.03f, 0.f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1));
        doorLeafGeo(b, 0.86f, 2.05f, 0.045f, 0, C(0.1f, 0.5f, 0.5f), true);
        b.pop();
        box(b, vec3(0.f, 0.05f, 2.15f), vec3(0.2f, 0.004f, 0.05f), Gy(0.95f), M(MAT_PAINT_WHITE), SK_NZ);
        textC(b, "RESTROOMS", vec3(0.f, 0.055f, 2.15f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.05f, 0.008f, Gy(0.1f), M(MAT_PAINT_WHITE));
        downlight(b, vec3(0.f, 1.5f, H), hall, 200.f);
        downlight(b, vec3(0.f, 3.5f, H), hall, 200.f);
    }
    // ---- storage: shelving, sacks, mop sink
    {
        float sx0 = X0, sx1 = xh - pt * 0.5f, sy0 = yK + pt * 0.5f, sy1 = Y1;
        wireShelf(b, vec3((sx0 + sx1) * 0.5f, sy1, 0.f), kPi, Min(3.0f, sx1 - sx0 - 2.4f), r.next());
        InPart ip(b, IP_FURNITURE);
        for (int k = 0; k < 4; k++) rbox(b, vec3(sx1 - 0.5f, sy0 + 0.6f + k * 0.5f, 0.15f), vec3(0.3f, 0.2f, 0.15f), 0.06f, C(0.85f, 0.8f, 0.68f), M(MAT_FABRIC), true);
        collide(b, vec3(sx1 - 0.5f, sy0 + 1.35f, 0.3f), vec3(0.32f, 0.95f, 0.3f));
        domeLight(b, vec3((sx0 + sx1) * 0.5f, (sy0 + sy1) * 0.5f, H), store, 110.f, vec3(1.f, 0.95f, 0.85f));
    }
    roomDressing(b, dine, true, d.seed ^ 0x81u);
    roomDressing(b, kitchen, true, d.seed ^ 0x82u);
    roomDressing(b, hall, true, d.seed ^ 0x83u);
}

// ------------------------------------------------------------------------------------------------ club pieces
// Square truss beam from a to c (four chords with zig-zag lacing)
void truss(IB& b, vec3 a, vec3 c, float s, u32 col) {
    vec3 dd = c - a;
    float L = length(dd);
    if (L < 0.1f) return;
    vec3 z = dd / L, x = normalize(anyPerp(z)), y = cross(z, x);
    b.pushAxes(a, x, y, z);
    u32 m = M(MAT_METAL_BRUSHED);
    vec2 cs[4] = {vec2(-s, -s), vec2(s, -s), vec2(s, s), vec2(-s, s)};
    for (int k = 0; k < 4; k++) tube(b, vec3(cs[k], 0.f), vec3(cs[k], L), 0.024f, 6, col, m);
    int n = Max(1, (int)(L / (s * 2.2f)));
    for (int i = 0; i < n; i++) {
        float z0 = L * i / n, z1 = L * (i + 1) / n;
        for (int k = 0; k < 4; k++) {
            vec2 p0 = cs[k], p1 = cs[(k + 1) & 3];
            tube(b, vec3(p0, (i & 1) ? z1 : z0), vec3(p1, (i & 1) ? z0 : z1), 0.012f, 4, col, m);
        }
    }
    b.pop();
}

// Moving-head light fixture hanging under a truss (base at p, pointing along dir), light sweeping
void movingHead(IB& b, vec3 p, vec3 dir, vec3 col, float cd, int roomIdx, u8 anim, u8 phase) {
    box(b, p - vec3(0, 0, 0.08f), vec3(0.15f, 0.12f, 0.08f), Gy(0.08f), M(MAT_METAL_PAINTED), SK_NONE);
    for (int e = -1; e <= 1; e += 2) box(b, p + vec3(e * 0.14f, 0, -0.25f), vec3(0.02f, 0.05f, 0.12f), Gy(0.1f), M(MAT_METAL_PAINTED), SK_NONE);
    vec3 z = normalize(dir), x = normalize(anyPerp(z)), y = cross(z, x);
    b.pushAxes(p - vec3(0, 0, 0.3f), x, y, z);
    cyl(b, vec3(0, 0, -0.14f), 0.11f, 0.12f, 0.24f, 12, Gy(0.08f), M(MAT_METAL_PAINTED), false, true);
    disc(b, vec3(0, 0, 0.1f), 0.09f, 12, C(col, 0.95f), EM(anim == 3 ? 5u : 4u, phase));
    b.pop();
    light(b, p - vec3(0, 0, 0.35f), col * cd, 26.f, roomIdx, dir, 16.f, 6.f, anim, phase);
}

// Speaker cabinet (front +y) with drivers
void speakerBox(IB& b, vec3 p, float yaw, float w, float h) {
    At at(b, p, yaw);
    rbox(b, vec3(0, 0, h * 0.5f), vec3(w * 0.5f, 0.3f, h * 0.5f), 0.02f, Gy(0.05f), M(MAT_PLASTIC), true);
    int n = Max(1, (int)(h / 0.5f));
    for (int k = 0; k < n; k++) {
        float z = h * (k + 0.5f) / n;
        float rr = Min(w * 0.4f, h / n * 0.4f);
        b.pushAxes(vec3(0, 0.3f, z), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0));
        lathe(b, vec3(0, 0, -0.001f), {vec2(rr, 0.f), vec2(rr * 0.9f, 0.04f), vec2(rr * 0.25f, 0.08f), vec2(0.f, 0.07f)}, 16, Gy(0.12f), M(MAT_RUBBER), false);
        b.pop();
    }
    collide(b, vec3(0, 0, h * 0.5f), vec3(w * 0.5f, 0.3f, h * 0.5f));
}

// Velvet rope line: chrome stanchions with a sagging red rope between them
void velvetRope(IB& b, vec3 a, vec3 c, int posts) {
    posts = Max(2, posts);
    vec3 prev(0.f);
    for (int i = 0; i < posts; i++) {
        vec3 p = lerp(a, c, (float)i / (posts - 1));
        lathe(b, p, {vec2(0.16f, 0.f), vec2(0.15f, 0.03f), vec2(0.03f, 0.06f), vec2(0.025f, 0.9f), vec2(0.045f, 0.93f), vec2(0.f, 0.98f)}, 12, Gy(0.85f), M(MAT_CHROME), false);
        if (i > 0) {
            vec3 m = (prev + p) * 0.5f + vec3(0, 0, 0.72f);
            tube(b, prev + vec3(0, 0, 0.88f), m, 0.018f, 6, C(0.55f, 0.02f, 0.05f), M(MAT_FABRIC));
            tube(b, m, p + vec3(0, 0, 0.88f), 0.018f, 6, C(0.55f, 0.02f, 0.05f), M(MAT_FABRIC));
        }
        prev = p;
    }
}

// Tiki torch with a live flame light (night)
void tikiTorch(IB& b, vec3 p, int roomIdx, u8 phase) {
    tube(b, p, p + vec3(0, 0, 1.7f), 0.025f, 6, C(0.45f, 0.32f, 0.18f), M(MAT_WOOD));
    lathe(b, p + vec3(0, 0, 1.7f), {vec2(0.05f, 0.f), vec2(0.09f, 0.12f), vec2(0.07f, 0.2f)}, 8, C(0.55f, 0.4f, 0.22f), M(MAT_FABRIC), true);
    sphere(b, p + vec3(0, 0, 1.97f), 0.07f, 8, C(1.f, 0.55f, 0.15f, 0.95f), EM(6), 1.6f);
    light(b, p + vec3(0, 0, 2.05f), vec3(1.f, 0.55f, 0.2f) * 70.f, 7.f, roomIdx, vec3(0.f), 0.f, 0.f, 6, phase);
}

// Festoon string with glowing bulbs between two points (sagging)
void festoon(IB& b, vec3 a, vec3 c, float sag, int bulbs) {
    vec3 prev = a;
    for (int i = 1; i <= 8; i++) {
        float t = i / 8.f;
        vec3 p = lerp(a, c, t) - vec3(0, 0, sag * 4.f * t * (1.f - t));
        tube(b, prev, p, 0.004f, 3, Gy(0.08f), M(MAT_RUBBER));
        prev = p;
    }
    for (int i = 0; i < bulbs; i++) {
        float t = (i + 0.5f) / bulbs;
        vec3 p = lerp(a, c, t) - vec3(0, 0, sag * 4.f * t * (1.f - t) + 0.05f);
        sphere(b, p, 0.028f, 6, C(1.f, 0.82f, 0.5f, 0.9f), EM(6), 1.2f);
    }
}

// Day bed / lounge sofa for the VIP deck (front +y)
void loungeSofa(IB& b, vec3 p, float yaw, float w, u32 fabric, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    box(b, vec3(0, 0, 0.14f), vec3(w * 0.5f, 0.45f, 0.14f), C(0.85f, 0.82f, 0.75f), M(MAT_WOOD), SK_NZ);
    rbox(b, vec3(0, 0.05f, 0.35f), vec3(w * 0.5f - 0.02f, 0.4f, 0.08f), 0.06f, fabric, M(MAT_CLOTH));
    rbox(b, vec3(0, -0.36f, 0.6f), vec3(w * 0.5f - 0.02f, 0.1f, 0.2f), 0.08f, fabric, M(MAT_CLOTH), true);
    for (int k = 0; k < (int)(w / 0.7f); k++) {
        vec3 pc = hsv(r.f(), 0.5f, 0.8f);
        b.pushAxes(vec3(-w * 0.5f + 0.4f + k * 0.7f, -0.22f, 0.58f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.5f)), normalize(vec3(0, -0.5f, 1)));
        rbox(b, vec3(0.f), vec3(0.2f, 0.07f, 0.2f), 0.06f, C(pc), M(MAT_CLOTH), true);
        b.pop();
    }
    collide(b, vec3(0, 0, 0.3f), vec3(w * 0.5f, 0.45f, 0.3f));
}

// Champagne service: low table with an ice bucket, bottle and flutes
void champagneTable(IB& b, vec3 p) {
    lathe(b, p, {vec2(0.25f, 0.f), vec2(0.2f, 0.03f), vec2(0.05f, 0.08f), vec2(0.05f, 0.4f), vec2(0.35f, 0.42f), vec2(0.35f, 0.45f)}, 16, Gy(0.92f), M(MAT_PAINT_WHITE), true);
    collide(b, p + vec3(0, 0, 0.22f), vec3(0.33f, 0.33f, 0.22f));
    InPart ip(b, IP_DETAIL);
    lathe(b, p + vec3(0.08f, 0, 0.45f), {vec2(0.07f, 0.f), vec2(0.1f, 0.18f), vec2(0.105f, 0.19f)}, 12, Gy(0.85f), M(MAT_CHROME), false);
    bottle(b, p + vec3(0.08f, 0.02f, 0.5f), 0.04f, 0.3f, C(0.05f, 0.2f, 0.08f), M(MAT_GLASS), C(0.9f, 0.75f, 0.3f));
    for (int k = 0; k < 3; k++) {
        vec3 g = p + vec3(-0.15f + k * 0.04f, -0.12f + k * 0.08f, 0.45f);
        lathe(b, g, {vec2(0.03f, 0.f), vec2(0.004f, 0.01f), vec2(0.004f, 0.09f), vec2(0.025f, 0.12f), vec2(0.03f, 0.2f)}, 8, C(0.95f, 0.95f, 0.85f, 1.f), M(MAT_GLASS), false);
    }
}

// ------------------------------------------------------------------------------------------------ Club Riptide
void layoutClub(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0xC1BBu);
    // terrain under the structure (model z), for platform heights and skirts
    auto groundL = [&](float x, float y) {
        vec3 w = d.toWorld(vec3(x, y, 0.f));
        return gMap ? gMap->heightAt(w.x, w.y) - d.origin.z : 0.f;
    };
    auto topOver = [&](float x0, float y0, float x1, float y1, bool lowest) {
        float v = lowest ? 1e9f : -1e9f;
        for (int j = 0; j <= 5; j++)
            for (int i = 0; i <= 5; i++) {
                float g = groundL(Lerp(x0, x1, i / 5.f), Lerp(y0, y1, j / 5.f));
                v = lowest ? Min(v, g) : Max(v, g);
            }
        return v;
    };
    // platforms: entrance plaza, main deck (bar + dance floor), DJ stage, boardwalk, VIP deck
    struct Deck {
        float x0, y0, x1, y1, z, lo;
    };
    auto mkDeck = [&](float x0, float y0, float x1, float y1, float lift) {
        Deck k{x0, y0, x1, y1, 0.f, 0.f};
        k.z = topOver(x0, y0, x1, y1, false) + lift;
        k.lo = topOver(x0, y0, x1, y1, true) - 0.4f;
        return k;
    };
    Deck ent = mkDeck(-5.f, -1.5f, 5.f, 4.f, 0.12f);
    Deck main = mkDeck(-12.f, 4.f, 16.f, 30.f, 0.25f);
    Deck walk = mkDeck(-18.f, 13.f, -12.f, 21.f, 0.25f);
    Deck vip = mkDeck(-53.f, 7.f, -18.f, 31.f, 0.45f);
    main.z = Max(main.z, ent.z);
    walk.z = Clamp(walk.z, main.z, vip.z);
    const float djZ = main.z + 0.9f;
    int rEnt = room(b, vec3(ent.x0, ent.y0, ent.z), vec3(ent.x1, ent.y1, ent.z + 4.f), vec3(0.f), 0.f, LS_NIGHTLIFE, true);
    int rMain = room(b, vec3(main.x0, main.y0, main.z), vec3(main.x1, main.y1, main.z + 5.f), vec3(0.f), 0.f, LS_NIGHTLIFE, true);
    int rVip = room(b, vec3(vip.x0, vip.y0, vip.z), vec3(vip.x1, vip.y1, vip.z + 4.f), vec3(0.f), 0.f, LS_NIGHTLIFE, true);
    int rWalk = room(b, vec3(walk.x0, walk.y0, walk.z), vec3(walk.x1, walk.y1, walk.z + 4.f), vec3(0.f), 0.f, LS_NIGHTLIFE, true);
    (void)rWalk;
    marker(b, IM_DOOR_OUT, vec3(0.f, -3.2f, ent.z - 0.1f), kPi);
    marker(b, IM_ENTRY, vec3(0.f, 3.f, ent.z), 0.f);
    // ---- decks: planks, skirts, collision
    auto buildDeck = [&](const Deck& k, u32 plank, float stripW) {
        InPart ip(b, IP_SHELL);
        int n = Max(1, (int)((k.x1 - k.x0) / stripW));
        for (int i = 0; i < n; i++) {
            float xa = k.x0 + (k.x1 - k.x0) * i / n, xc = k.x0 + (k.x1 - k.x0) * (i + 1) / n - 0.01f;
            quad(b, vec3(xa, k.y0, k.z), vec3(xc, k.y0, k.z), vec3(xc, k.y1, k.z), vec3(xa, k.y1, k.z), vec2(xa, k.y0), vec2(xc, k.y0), vec2(xc, k.y1), vec2(xa, k.y1),
                 C(rgbOf(plank) * (0.88f + 0.2f * hash01((float)i, k.x0, k.y0))), M(MAT_WOOD));
        }
        u32 skirt = C(rgbOf(plank) * 0.7f);
        quadF(b, vec3(k.x0, k.y0, k.lo), vec3(k.x1, k.y0, k.lo), vec3(k.x1, k.y0, k.z), vec3(k.x0, k.y0, k.z), vec3(0, -1, 0), skirt, M(MAT_WOOD));
        quadF(b, vec3(k.x1, k.y1, k.lo), vec3(k.x0, k.y1, k.lo), vec3(k.x0, k.y1, k.z), vec3(k.x1, k.y1, k.z), vec3(0, 1, 0), skirt, M(MAT_WOOD));
        quadF(b, vec3(k.x0, k.y1, k.lo), vec3(k.x0, k.y0, k.lo), vec3(k.x0, k.y0, k.z), vec3(k.x0, k.y1, k.z), vec3(-1, 0, 0), skirt, M(MAT_WOOD));
        quadF(b, vec3(k.x1, k.y0, k.lo), vec3(k.x1, k.y1, k.lo), vec3(k.x1, k.y1, k.z), vec3(k.x1, k.y0, k.z), vec3(1, 0, 0), skirt, M(MAT_WOOD));
        collideMM(b, vec3(k.x0, k.y0, k.lo), vec3(k.x1, k.y1, k.z));
    };
    buildDeck(ent, C(0.62f, 0.52f, 0.42f), 0.3f);
    buildDeck(main, C(0.58f, 0.46f, 0.34f), 0.3f);
    buildDeck(walk, C(0.62f, 0.5f, 0.38f), 0.3f);
    buildDeck(vip, C(0.82f, 0.78f, 0.7f), 0.3f);
    // steps: sand -> entrance plaza (front edge), boardwalk -> VIP deck
    {
        InPart ip(b, IP_SHELL);
        float gz = groundL(0.f, -2.2f);
        int ns = Max(1, (int)ceilf((ent.z - gz) / 0.18f) - 1);
        for (int k = 0; k < ns; k++) {
            float top = ent.z - (ent.z - gz) * (k + 1) / (ns + 1);
            float y1 = ent.y0 - 0.32f * k, y0 = y1 - 0.32f;
            box(b, vec3(0.f, (y0 + y1) * 0.5f, (top + gz - 0.2f) * 0.5f), vec3(3.5f, 0.16f, (top - gz + 0.2f) * 0.5f), C(0.55f, 0.45f, 0.35f), M(MAT_WOOD), SK_NZ);
            collideMM(b, vec3(-3.5f, y0, gz - 0.3f), vec3(3.5f, y1, top));
        }
        if (vip.z - walk.z > 0.1f) {
            int nv = Max(1, (int)ceilf((vip.z - walk.z) / 0.18f) - 1);
            for (int k = 0; k < nv; k++) {
                float top = walk.z + (vip.z - walk.z) * (k + 1) / (nv + 1);
                float x1 = walk.x0 + 0.35f * (nv - k), x0 = x1 - 0.35f;
                box(b, vec3((x0 + x1) * 0.5f, (walk.y0 + walk.y1) * 0.5f, (top + walk.z - 0.1f) * 0.5f), vec3(0.175f, (walk.y1 - walk.y0) * 0.5f, (top - walk.z + 0.1f) * 0.5f),
                    C(0.82f, 0.78f, 0.7f), M(MAT_WOOD), SK_NZ);
                collideMM(b, vec3(x0, walk.y0, walk.z - 0.2f), vec3(x1, walk.y1, top));
            }
        }
    }
    // ---- entrance: gateway arch with the neon name, velvet rope queue, host podium, tiki torches
    {
        float z = ent.z;
        InPart ip(b, IP_SHELL);
        u32 post = C(0.2f, 0.16f, 0.12f);
        for (int e = -1; e <= 1; e += 2) {
            box(b, vec3(e * 2.6f, 0.5f, z + 2.1f), vec3(0.2f, 0.2f, 2.1f), post, M(MAT_WOOD), SK_NZ);
            collide(b, vec3(e * 2.6f, 0.5f, z + 2.1f), vec3(0.2f, 0.2f, 2.1f));
        }
        box(b, vec3(0.f, 0.5f, z + 4.45f), vec3(3.3f, 0.3f, 0.35f), post, M(MAT_WOOD), SK_NONE);
        // thatch cap
        quadF(b, vec3(-3.6f, 0.05f, z + 4.8f), vec3(3.6f, 0.05f, z + 4.8f), vec3(3.4f, 0.5f, z + 5.3f), vec3(-3.4f, 0.5f, z + 5.3f), vec3(0, -1, 1), C(0.62f, 0.5f, 0.3f), M(MAT_FABRIC));
        quadF(b, vec3(3.6f, 0.95f, z + 4.8f), vec3(-3.6f, 0.95f, z + 4.8f), vec3(-3.4f, 0.5f, z + 5.3f), vec3(3.4f, 0.5f, z + 5.3f), vec3(0, 1, 1), C(0.62f, 0.5f, 0.3f), M(MAT_FABRIC));
        At at(b, vec3(0.f, 0.19f, z + 4.45f), kPi);   // sign faces the promenade (-y)
        textC(b, "CLUB RIPTIDE", vec3(0.f, 0.02f, 0.f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.42f, 0.05f, C(0.2f, 1.f, 0.95f, 0.95f), EM(4, 11), 0.02f);
        box(b, vec3(0.f, -0.005f, -0.3f), vec3(2.6f, 0.01f, 0.012f), C(1.f, 0.2f, 0.7f, 0.95f), EM(2, 50), SK_NONE);
        box(b, vec3(0.f, -0.005f, 0.3f), vec3(2.6f, 0.01f, 0.012f), C(1.f, 0.2f, 0.7f, 0.95f), EM(2, 90), SK_NONE);
    }
    light(b, vec3(0.f, -1.5f, ent.z + 3.8f), vec3(0.4f, 1.f, 0.95f) * 180.f, 9.f, rEnt, vec3(0.f), 0.f, 0.f, 5, 11);
    {
        InPart ip(b, IP_FURNITURE);
        velvetRope(b, vec3(-2.3f, -0.9f, ent.z), vec3(-2.3f, 2.8f, ent.z), 4);
        velvetRope(b, vec3(1.2f, -0.9f, ent.z), vec3(1.2f, 1.2f, ent.z), 3);
        // host podium
        rbox(b, vec3(-1.3f, 1.6f, ent.z + 0.55f), vec3(0.3f, 0.22f, 0.55f), 0.03f, Gy(0.08f), M(MAT_PLASTIC), true);
        box(b, vec3(-1.3f, 1.39f, ent.z + 0.8f), vec3(0.2f, 0.004f, 0.06f), C(0.2f, 1.f, 0.95f, 0.8f), EM(), SK_NZ);
        collide(b, vec3(-1.3f, 1.6f, ent.z + 0.55f), vec3(0.3f, 0.22f, 0.55f));
        for (int e = -1; e <= 1; e += 2) tikiTorch(b, vec3(e * 3.4f, -0.6f, ent.z), rEnt, (u8)(40 + e * 20));
    }
    scenario(b, vec3(1.8f, 2.f, ent.z), kPi, 19, SR_BOUNCER, SF_STAFF);
    scenario(b, vec3(-1.3f, 2.1f, ent.z), kPi, 0, SR_RECEPTION, SF_STAFF | SF_NIGHT);
    // ---- dance floor: LED tiles (animated), frame, truss with moving heads
    const float fx0 = -8.f, fx1 = 8.f, fy0 = 12.f, fy1 = 28.f;
    {
        InPart ip(b, IP_SHELL);
        // tiles sit 1.2 cm above the frame plate (no depth fighting at distance); colour waves ripple out from the
        // centre (colour-cycle phase by ring), a sparse set of tiles strobes on top
        float z = main.z + 0.018f;
        for (int j = 0; j < 16; j++)
            for (int i = 0; i < 16; i++) {
                float xa = fx0 + i + 0.04f, xc = fx0 + i + 0.96f, ya = fy0 + j + 0.04f, yc = fy0 + j + 0.96f;
                float ring = std::max(fabsf(i - 7.5f), fabsf(j - 7.5f));
                u32 phase = (u32)((int)(ring * 18.f) + ((i ^ j) & 1) * 128) & 255u;
                bool strobe = ((i * 5 + j * 3) % 11) == 0;
                quad(b, vec3(xa, ya, z), vec3(xc, ya, z), vec3(xc, yc, z), vec3(xa, yc, z), vec2(0.f), vec2(1, 0), vec2(1, 1), vec2(0, 1),
                     strobe ? C(0.95f, 0.95f, 1.f, 0.07f) : C(0.9f, 0.9f, 0.95f, 0.045f), EM(strobe ? 2u : 3u, phase));
            }
        box(b, vec3((fx0 + fx1) * 0.5f, (fy0 + fy1) * 0.5f, main.z + 0.003f), vec3((fx1 - fx0) * 0.5f + 0.1f, (fy1 - fy0) * 0.5f + 0.1f, 0.003f), Gy(0.03f), M(MAT_METAL_PAINTED), SK_NZ);
    }
    {
        InPart ip(b, IP_FURNITURE);
        u32 tc = Gy(0.75f);
        float tz = main.z + 5.2f, s = 0.15f;
        vec2 cs[4] = {vec2(fx0 - 0.6f, fy0 - 0.6f), vec2(fx1 + 0.6f, fy0 - 0.6f), vec2(fx1 + 0.6f, fy1 + 0.6f), vec2(fx0 - 0.6f, fy1 + 0.6f)};
        for (int k = 0; k < 4; k++) {
            truss(b, vec3(cs[k], main.z), vec3(cs[k], tz), s, tc);
            box(b, vec3(cs[k], main.z + 0.01f), vec3(0.35f, 0.35f, 0.01f), Gy(0.2f), M(MAT_METAL_PAINTED), SK_NZ);
            collide(b, vec3(cs[k], main.z + tz * 0.5f), vec3(0.2f, 0.2f, tz * 0.5f));
            truss(b, vec3(cs[k], tz), vec3(cs[(k + 1) & 3], tz), s, tc);
        }
        static const vec3 kCols[6] = {vec3(1.f, 0.1f, 0.6f), vec3(0.1f, 0.9f, 1.f), vec3(0.6f, 0.2f, 1.f), vec3(1.f, 0.6f, 0.1f), vec3(0.2f, 1.f, 0.4f), vec3(1.f, 1.f, 1.f)};
        int li = 0;
        for (int k = 0; k < 4; k++) {
            vec2 a = cs[k], c = cs[(k + 1) & 3];
            for (int m = 1; m <= 2; m++) {
                vec2 p = lerp(a, c, m / 3.f);
                vec3 tgt((fx0 + fx1) * 0.5f + r.range(-4.f, 4.f), (fy0 + fy1) * 0.5f + r.range(-4.f, 4.f), main.z);
                vec3 pp(p, tz - s - 0.02f);
                movingHead(b, pp, normalize(tgt - pp), kCols[li % 6], 1400.f, rMain, (li % 4 == 3) ? 3 : 2, (u8)(li * 37));
                li++;
            }
        }
    }
    for (int k = 0; k < 10; k++) {
        float a = kTwoPi * k / 10.f, rr = 2.5f + (k % 3) * 2.2f;
        scenario(b, vec3(cosf(a) * rr, 20.f + sinf(a) * rr, main.z), a + kPi * 0.5f + r.range(-0.5f, 0.5f), 9, SR_DANCER, SF_NIGHT | (k > 5 ? SF_OPTIONAL : 0));
    }
    // ---- DJ stage: riser with steps, booth, speakers, video wall
    {
        const float sx0 = 10.5f, sx1 = 15.5f, sy0 = 15.f, sy1 = 25.f;
        InPart ip(b, IP_SHELL);
        box(b, vec3((sx0 + sx1) * 0.5f, (sy0 + sy1) * 0.5f, (djZ + main.z) * 0.5f), vec3((sx1 - sx0) * 0.5f, (sy1 - sy0) * 0.5f, (djZ - main.z) * 0.5f), Gy(0.06f), M(MAT_METAL_PAINTED), SK_NZ);
        box(b, vec3(sx0 - 0.002f, (sy0 + sy1) * 0.5f, main.z + 0.1f), vec3(0.002f, (sy1 - sy0) * 0.5f, 0.02f), C(0.2f, 0.9f, 1.f, 0.9f), EM(2, 60), SK_NONE);
        collideMM(b, vec3(sx0, sy0, main.z), vec3(sx1, sy1, djZ));
        for (int k = 0; k < 4; k++) {
            float top = main.z + (djZ - main.z) * (k + 1) / 5.f;
            float y1 = sy0 - 0.3f * (3 - k), y0 = y1 - 0.3f;
            box(b, vec3(sx1 - 0.7f, (y0 + y1) * 0.5f, (top + main.z) * 0.5f), vec3(0.6f, 0.15f, (top - main.z) * 0.5f), Gy(0.1f), M(MAT_METAL_PAINTED), SK_NZ);
            collideMM(b, vec3(sx1 - 1.3f, y0, main.z), vec3(sx1 - 0.1f, y1, top));
        }
        // DJ booth facing the floor (-x)
        At at(b, vec3(11.2f, 20.f, djZ), kHalfPi);
        rbox(b, vec3(0, 0, 0.5f), vec3(1.2f, 0.35f, 0.5f), 0.02f, Gy(0.05f), M(MAT_PLASTIC), true);
        box(b, vec3(0, 0.351f, 0.5f), vec3(1.1f, 0.001f, 0.35f), C(0.8f, 0.1f, 1.f, 0.12f), EM(4, 70), SK_NZ);
        textC(b, "RIPTIDE", vec3(0, 0.353f, 0.5f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.2f, 0.03f, C(1.f, 1.f, 1.f, 0.95f), EM(4, 30));
        collide(b, vec3(0, 0, 0.5f), vec3(1.2f, 0.35f, 0.5f));
        InPart ip2(b, IP_DETAIL);
        for (int e = -1; e <= 1; e += 2) {
            cyl(b, vec3(e * 0.55f, -0.05f, 1.0f), 0.17f, 0.17f, 0.03f, 16, Gy(0.1f), M(MAT_PLASTIC), true, false);
            disc(b, vec3(e * 0.55f, -0.05f, 1.031f), 0.14f, 16, Gy(0.03f), M(MAT_PLASTIC));
        }
        rbox(b, vec3(0, -0.05f, 1.03f), vec3(0.18f, 0.2f, 0.03f), 0.01f, Gy(0.12f), M(MAT_PLASTIC));
        for (int k = 0; k < 12; k++) box(b, vec3(-0.12f + (k % 4) * 0.08f, -0.15f + (k / 4) * 0.1f, 1.061f), vec3(0.012f, 0.012f, 0.001f), C(hsv(k / 12.f, 0.9f, 1.f), 0.9f), EM(1, (u32)k * 21u), SK_NZ);
        InPart ip3(b, IP_FURNITURE);
        for (int e = -1; e <= 1; e += 2) speakerBox(b, vec3(e * 3.8f, -0.3f, 0.f), 0.f, 0.9f, 2.4f);
        light(b, vec3(0, -0.5f, 2.2f), vec3(0.9f, 0.3f, 1.f) * 120.f, 6.f, rMain, vec3(0.f), 0.f, 0.f, 5, 20);
    }
    {
        // video wall behind the DJ
        InPart ip(b, IP_SHELL);
        float wx = 15.6f, wy0 = 15.2f, wy1 = 24.8f, wz0 = djZ + 0.3f, wz1 = djZ + 4.2f;
        box(b, vec3(wx + 0.15f, (wy0 + wy1) * 0.5f, (main.z + wz1 + 0.2f) * 0.5f), vec3(0.15f, (wy1 - wy0) * 0.5f + 0.2f, (wz1 + 0.2f - main.z) * 0.5f), Gy(0.05f), M(MAT_METAL_PAINTED), SK_NZ);
        for (int j = 0; j < 4; j++)
            for (int i = 0; i < 8; i++) {
                float ya = wy1 - (wy1 - wy0) * i / 8.f - 0.02f, yc = wy1 - (wy1 - wy0) * (i + 1) / 8.f + 0.02f;
                float za = wz0 + (wz1 - wz0) * j / 4.f + 0.02f, zc = wz0 + (wz1 - wz0) * (j + 1) / 4.f - 0.02f;
                quadF(b, vec3(wx - 0.001f, ya, za), vec3(wx - 0.001f, yc, za), vec3(wx - 0.001f, yc, zc), vec3(wx - 0.001f, ya, zc), vec3(-1, 0, 0), C(0.8f, 0.6f, 1.f, 0.06f),
                      EM(3u, (u32)(i * 9 + j * 23 + ((i + j) & 1) * 96)));
            }
        light(b, vec3(wx - 2.f, 20.f, (wz0 + wz1) * 0.5f), vec3(0.7f, 0.5f, 1.f) * 260.f, 14.f, rMain, vec3(0.f), 0.f, 0.f, 5, 90);
    }
    scenario(b, vec3(12.f, 20.f, djZ), kHalfPi, 9, SR_DJ, SF_NIGHT | SF_STAFF);
    {
        InPart ip(b, IP_FURNITURE);
        speakerBox(b, vec3(fx1 + 1.2f, fy0 - 1.2f, main.z), 0.8f + kPi, 1.0f, 1.8f);
        speakerBox(b, vec3(fx1 + 1.2f, fy1 + 1.2f, main.z), -0.8f, 1.0f, 1.8f);
        speakerBox(b, vec3(fx0 - 1.2f, fy1 + 1.2f, main.z), 0.8f - kPi * 0.5f, 1.0f, 1.8f);
    }
    // ---- palapa bar on the south side of the entrance path
    {
        const float bx0 = 4.5f, bx1 = 14.5f, by = 7.f, z = main.z;
        InPart ip(b, IP_SHELL);
        // bar counter (customers on the +y side), back bar with bottles
        box(b, vec3((bx0 + bx1) * 0.5f, by, z + 0.53f), vec3((bx1 - bx0) * 0.5f, 0.3f, 0.53f), C(0.3f, 0.2f, 0.12f), M(MAT_WOOD), SK_NZ);
        for (int k = 0; k < 20; k++) {
            float x = bx0 + 0.25f + k * 0.5f;
            box(b, vec3(x, by + 0.301f, z + 0.53f), vec3(0.18f, 0.004f, 0.45f), C(0.42f, 0.3f, 0.18f), M(MAT_WOOD), SK_NZ);
        }
        box(b, vec3((bx0 + bx1) * 0.5f, by + 0.06f, z + 1.08f), vec3((bx1 - bx0) * 0.5f + 0.08f, 0.42f, 0.03f), C(0.5f, 0.36f, 0.22f), M(MAT_WOOD), SK_NONE);
        box(b, vec3((bx0 + bx1) * 0.5f, by + 0.31f, z + 0.5f), vec3((bx1 - bx0) * 0.5f, 0.004f, 0.02f), C(0.2f, 0.9f, 1.f, 0.9f), EM(2, 30), SK_NONE);
        collide(b, vec3((bx0 + bx1) * 0.5f, by, z + 0.55f), vec3((bx1 - bx0) * 0.5f + 0.08f, 0.35f, 0.55f));
        box(b, vec3((bx0 + bx1) * 0.5f, 5.1f, z + 0.45f), vec3((bx1 - bx0) * 0.5f - 0.5f, 0.3f, 0.45f), C(0.25f, 0.18f, 0.12f), M(MAT_WOOD), SK_NZ);
        collide(b, vec3((bx0 + bx1) * 0.5f, 5.1f, z + 0.45f), vec3((bx1 - bx0) * 0.5f - 0.5f, 0.3f, 0.45f));
        for (int lv = 0; lv < 2; lv++) {
            float sz = z + 1.3f + lv * 0.45f;
            box(b, vec3((bx0 + bx1) * 0.5f, 5.0f, sz), vec3((bx1 - bx0) * 0.5f - 0.6f, 0.15f, 0.015f), C(0.35f, 0.25f, 0.15f), M(MAT_WOOD), SK_NONE);
            box(b, vec3((bx0 + bx1) * 0.5f, 4.86f, sz + 0.2f), vec3((bx1 - bx0) * 0.5f - 0.6f, 0.004f, 0.2f), C(1.f, 0.6f, 0.3f, 0.16f), EM(4, (u32)lv * 60u), SK_NONE);
            InPart ipd(b, IP_DETAIL);
            Rng br(d.seed + (u32)lv);
            for (float x = bx0 + 0.8f; x < bx1 - 0.8f; x += 0.14f) {
                vec3 bc = hsv(br.f(), br.range(0.3f, 0.9f), br.range(0.2f, 0.8f));
                bottle(b, vec3(x, 5.0f, sz + 0.015f), 0.035f, br.range(0.24f, 0.33f), C(bc, 1.f), M(MAT_GLASS), C(0.8f, 0.7f, 0.3f));
            }
        }
        // palapa: posts and a thatched hip roof
        float rx0 = bx0 - 0.8f, rx1 = bx1 + 0.8f, ry0 = 4.2f, ry1 = 8.8f, ez = z + 3.0f, pz = z + 4.4f;
        for (float x : {rx0 + 0.3f, rx1 - 0.3f})
            for (float y : {ry0 + 0.3f, ry1 - 0.3f}) {
                cyl(b, vec3(x, y, z), 0.12f, 0.11f, ez - z, 8, C(0.4f, 0.3f, 0.2f), M(MAT_BARK), false);
                collide(b, vec3(x, y, (z + ez) * 0.5f), vec3(0.12f, 0.12f, (ez - z) * 0.5f));
            }
        vec3 rc((rx0 + rx1) * 0.5f, (ry0 + ry1) * 0.5f, pz);
        vec3 c0(rx0 - 0.6f, ry0 - 0.6f, ez - 0.25f), c1(rx1 + 0.6f, ry0 - 0.6f, ez - 0.25f), c2(rx1 + 0.6f, ry1 + 0.6f, ez - 0.25f), c3(rx0 - 0.6f, ry1 + 0.6f, ez - 0.25f);
        vec3 r0 = rc + vec3(-(rx1 - rx0) * 0.3f, 0, 0), r1 = rc + vec3((rx1 - rx0) * 0.3f, 0, 0);
        u32 thatch = C(0.66f, 0.54f, 0.34f), tm = M(MAT_FABRIC);
        quadF(b, c0, c1, r1, r0, vec3(0, -1, 1), thatch, tm);
        quadF(b, c2, c3, r0, r1, vec3(0, 1, 1), thatch, tm);
        tri(b, c3, c0, r0, thatch, tm);
        tri(b, c1, c2, r1, thatch, tm);
        quadF(b, c0, c1, r1, r0, vec3(0, 1, -1), C(0.5f, 0.4f, 0.25f), tm);
        quadF(b, c2, c3, r0, r1, vec3(0, -1, -1), C(0.5f, 0.4f, 0.25f), tm);
        tri(b, c0, c3, r0, C(0.5f, 0.4f, 0.25f), tm);
        tri(b, c2, c1, r1, C(0.5f, 0.4f, 0.25f), tm);
        // fringe
        for (int k = 0; k < 4; k++) {
            vec3 a = k == 0 ? c0 : (k == 1 ? c1 : (k == 2 ? c2 : c3)), c = k == 0 ? c1 : (k == 1 ? c2 : (k == 2 ? c3 : c0));
            vec3 nn = normalize(cross(c - a, vec3(0, 0, 1)));
            quadF(b, a, c, c - vec3(0, 0, 0.25f), a - vec3(0, 0, 0.25f), nn, C(0.6f, 0.48f, 0.3f), tm);
            quadF(b, a, c, c - vec3(0, 0, 0.25f), a - vec3(0, 0, 0.25f), -nn, C(0.5f, 0.4f, 0.25f), tm);
        }
        for (int k = 0; k < 3; k++) {
            vec3 lp((bx0 + bx1) * 0.5f - 3.f + k * 3.f, 7.f, ez - 0.35f);
            sphere(b, lp, 0.14f, 10, C(1.f, 0.75f, 0.45f, 0.85f), EM(6), 1.1f);
            light(b, lp - vec3(0, 0, 0.2f), vec3(1.f, 0.72f, 0.45f) * 110.f, 6.f, rMain);
        }
        InPart ip2(b, IP_FURNITURE);
        for (float x = bx0 + 0.5f; x < bx1 - 0.3f; x += 0.8f) {
            barStool(b, vec3(x, by + 0.75f, z), C(0.15f, 0.12f, 0.1f));
            collide(b, vec3(x, by + 0.75f, z + 0.38f), vec3(0.17f, 0.17f, 0.38f));
        }
    }
    scenario(b, vec3(7.f, 5.95f, main.z), 0.f, 0, SR_BARTENDER, SF_STAFF);
    scenario(b, vec3(11.5f, 5.95f, main.z), 0.f, 7, SR_BARTENDER, SF_STAFF | SF_NIGHT);
    scenario(b, vec3(6.1f, 7.75f, main.z), kPi, 6, SR_PATRON, SF_OPTIONAL, 0.26f);
    scenario(b, vec3(9.3f, 8.4f, main.z), kPi, 7, SR_PATRON, SF_OPTIONAL | SF_NIGHT);
    scenario(b, vec3(10.f, 8.6f, main.z), kPi + 0.8f, 7, SR_PATRON, SF_OPTIONAL | SF_NIGHT);
    marker(b, IM_COUNTER, vec3(9.5f, 8.3f, main.z), kPi);
    // ---- festoon lights and torches around the main deck
    {
        InPart ip(b, IP_FURNITURE);
        vec3 poles[6] = {vec3(-11.5f, 5.f, 0.f), vec3(-11.5f, 17.f, 0.f), vec3(-11.5f, 29.5f, 0.f), vec3(15.5f, 29.5f, 0.f), vec3(3.f, 29.5f, 0.f), vec3(3.f, 4.5f, 0.f)};
        for (auto& p : poles) {
            p.z = main.z;
            cyl(b, p, 0.06f, 0.05f, 4.2f, 8, C(0.4f, 0.3f, 0.2f), M(MAT_WOOD), true);
            collide(b, p + vec3(0, 0, 2.1f), vec3(0.07f, 0.07f, 2.1f));
        }
        festoon(b, poles[0] + vec3(0, 0, 4.1f), poles[1] + vec3(0, 0, 4.1f), 0.5f, 14);
        festoon(b, poles[1] + vec3(0, 0, 4.1f), poles[2] + vec3(0, 0, 4.1f), 0.5f, 14);
        festoon(b, poles[2] + vec3(0, 0, 4.1f), poles[4] + vec3(0, 0, 4.1f), 0.6f, 16);
        festoon(b, poles[4] + vec3(0, 0, 4.1f), poles[3] + vec3(0, 0, 4.1f), 0.6f, 14);
        festoon(b, poles[0] + vec3(0, 0, 4.1f), poles[5] + vec3(0, 0, 4.1f), 0.5f, 16);
        festoon(b, poles[5] + vec3(0, 0, 4.1f), poles[4] + vec3(0, 0, 4.1f), 0.9f, 28);
        for (int k = 0; k < 3; k++) light(b, poles[k] + vec3(1.5f, 0.f, 3.4f), vec3(1.f, 0.8f, 0.5f) * 90.f, 9.f, rMain);
        tikiTorch(b, vec3(-11.3f, 10.f, main.z), rMain, 7);
        tikiTorch(b, vec3(-11.3f, 25.f, main.z), rMain, 99);
        tikiTorch(b, vec3(15.3f, 11.f, main.z), rMain, 150);
    }
    // ---- boardwalk rope rails and the VIP gate
    {
        InPart ip(b, IP_FURNITURE);
        velvetRope(b, vec3(walk.x1 - 0.2f, walk.y0 + 0.2f, walk.z), vec3(walk.x0 + 0.3f, walk.y0 + 0.2f, walk.z), 4);
        velvetRope(b, vec3(walk.x1 - 0.2f, walk.y1 - 0.2f, walk.z), vec3(walk.x0 + 0.3f, walk.y1 - 0.2f, walk.z), 4);
        for (int e = 0; e < 2; e++) collideMM(b, vec3(walk.x0, e == 0 ? walk.y0 : walk.y1 - 0.3f, walk.z), vec3(walk.x1, e == 0 ? walk.y0 + 0.3f : walk.y1, walk.z + 1.f));
        InPart ip2(b, IP_SHELL);
        for (int e = -1; e <= 1; e += 2) {
            box(b, vec3(vip.x1 - 0.15f, 17.f + e * 2.3f, vip.z + 1.3f), vec3(0.15f, 0.15f, 1.3f), Gy(0.92f), M(MAT_PAINT_WHITE), SK_NZ);
            collide(b, vec3(vip.x1 - 0.15f, 17.f + e * 2.3f, vip.z + 1.3f), vec3(0.15f, 0.15f, 1.3f));
        }
        box(b, vec3(vip.x1 - 0.15f, 17.f, vip.z + 2.7f), vec3(0.18f, 2.5f, 0.12f), Gy(0.92f), M(MAT_PAINT_WHITE), SK_NONE);
        At at(b, vec3(vip.x1 + 0.04f, 17.f, vip.z + 2.7f), -kHalfPi);   // faces +x (toward the dance floor)
        textC(b, "VIP", vec3(0.f, 0.f, 0.f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.18f, 0.03f, C(1.f, 0.85f, 0.4f, 0.95f), EM(4, 70));
    }
    scenario(b, vec3(-26.f, 18.f, vip.z), 1.2f, 19, SR_GUARD, SF_STAFF | SF_NIGHT);
    // ---- VIP deck: rope edge, cabanas along the seaward side, lounges, palms, lanterns
    {
        InPart ip(b, IP_FURNITURE);
        velvetRope(b, vec3(vip.x1 - 0.3f, vip.y0 + 0.3f, vip.z), vec3(vip.x0 + 0.3f, vip.y0 + 0.3f, vip.z), 12);
        velvetRope(b, vec3(vip.x0 + 0.3f, vip.y0 + 0.3f, vip.z), vec3(vip.x0 + 0.3f, vip.y1 - 0.3f, vip.z), 7);
        collideMM(b, vec3(vip.x0, vip.y0, vip.z), vec3(vip.x1, vip.y0 + 0.35f, vip.z + 1.f));
        collideMM(b, vec3(vip.x0, vip.y0, vip.z), vec3(vip.x0 + 0.35f, vip.y1, vip.z + 1.f));
        const float cabX[3] = {-49.f, -41.5f, -34.f};
        for (int k = 0; k < 3; k++) {
            float cx = cabX[k], cy = 26.5f;
            u32 fab = Gy(0.97f);
            // posts, canopy, curtains on the sides and back
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sy = -1; sy <= 1; sy += 2) {
                    box(b, vec3(cx + sx * 2.3f, cy + sy * 1.9f, vip.z + 1.35f), vec3(0.06f, 0.06f, 1.35f), Gy(0.95f), M(MAT_WOOD), SK_NZ);
                    collide(b, vec3(cx + sx * 2.3f, cy + sy * 1.9f, vip.z + 1.35f), vec3(0.07f, 0.07f, 1.35f));
                }
            box(b, vec3(cx, cy, vip.z + 2.75f), vec3(2.45f, 2.05f, 0.05f), fab, M(MAT_FABRIC), SK_NONE);
            for (int sx = -1; sx <= 1; sx += 2) {
                At at(b, vec3(cx + sx * 2.3f, cy, vip.z), sx < 0 ? -kHalfPi : kHalfPi);
                curtains(b, -1.85f, 1.85f, 0.1f, 2.5f, -0.02f, fab, (u32)(k * 7 + sx + 3), 0.25f);
            }
            {
                At at(b, vec3(cx, cy - 1.9f, vip.z), kPi);
                curtains(b, -2.25f, 2.25f, 0.1f, 2.5f, -0.02f, fab, (u32)(k * 11 + 5), 0.3f);
            }
            loungeSofa(b, vec3(cx, cy + 0.8f, vip.z), kPi, 3.4f, C(0.95f, 0.93f, 0.88f), (u32)(d.seed + k));
            champagneTable(b, vec3(cx, cy - 0.5f, vip.z));
            sphere(b, vec3(cx, cy, vip.z + 2.5f), 0.16f, 10, C(1.f, 0.8f, 0.5f, 0.8f), EM(6), 1.2f);
            light(b, vec3(cx, cy, vip.z + 2.3f), vec3(1.f, 0.78f, 0.5f) * 90.f, 6.f, rVip);
            scenario(b, vec3(cx - 0.8f, cy + 0.85f, vip.z), kPi, 6, SR_VIP, SF_NIGHT | SF_OPTIONAL);
            scenario(b, vec3(cx + 0.9f, cy + 0.85f, vip.z), kPi, 6, SR_VIP, SF_NIGHT | SF_OPTIONAL);
        }
        // lounge groups in the middle of the deck
        for (int k = 0; k < 3; k++) {
            float gx = -46.f + k * 9.5f, gy = 14.5f;
            loungeSofa(b, vec3(gx, gy - 1.3f, vip.z), 0.f, 2.6f, C(hsv(0.55f + k * 0.12f, 0.45f, 0.75f)), (u32)(d.seed * 3u + (u32)k));
            loungeSofa(b, vec3(gx, gy + 1.3f, vip.z), kPi, 2.6f, C(hsv(0.55f + k * 0.12f, 0.45f, 0.75f)), (u32)(d.seed * 5u + (u32)k));
            champagneTable(b, vec3(gx, gy, vip.z));
            scenario(b, vec3(gx - 0.5f, gy - 1.25f, vip.z), 0.f, 6, SR_VIP, SF_NIGHT | SF_OPTIONAL);
            scenario(b, vec3(gx + 0.6f, gy + 1.25f, vip.z), kPi, 6, SR_VIP, SF_OPTIONAL);
            plant(b, vec3(gx + 3.6f, gy, vip.z), 2.2f, (u32)(d.seed + 77u + (u32)k), C(0.95f, 0.95f, 0.92f), 1);
            tikiTorch(b, vec3(gx - 3.4f, gy + 3.f, vip.z), rVip, (u8)(k * 50));
        }
    }
    scenario(b, vec3(-44.f, 26.f, vip.z), -1.6f, 19, SR_GUARD, SF_STAFF | SF_NIGHT | SF_OPTIONAL);
}

}  // namespace ikit
}  // namespace World
