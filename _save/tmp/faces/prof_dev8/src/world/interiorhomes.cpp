// Homes: Mari's apartment (a ground-floor unit behind a residential facade: living room + kitchen, bedroom, bath)
// and Dex's trailer (a single-wide on blocks that replaces a Flats house: deck, awning, yard clutter, three rooms).
// Safehouse markers: bed (save), wardrobe (outfits), entry / door-out.
#include "interiors.h"
#include "sites.h"

namespace World {
namespace ikit {

// ------------------------------------------------------------------------------------------------ home props
// Coat hooks board with jackets and a bag (against a wall, front +y)
void coatHooks(IB& b, vec3 p, float yaw, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    InPart ip(b, IP_FURNITURE);
    box(b, vec3(0, 0.012f, 1.72f), vec3(0.45f, 0.012f, 0.06f), C(0.4f, 0.27f, 0.16f), M(MAT_WOOD), SK_NY);
    for (int k = 0; k < 4; k++) {
        float x = -0.33f + k * 0.22f;
        tube(b, vec3(x, 0.02f, 1.7f), vec3(x, 0.08f, 1.74f), 0.008f, 6, Gy(0.2f), M(MAT_METAL_PAINTED), true);
        if (k == 1 || (k == 3 && r.chance(0.6f))) {
            vec3 c = hsv(r.f(), r.range(0.3f, 0.7f), r.range(0.2f, 0.6f));
            // jacket: shoulders + body hanging from the hook
            rbox(b, vec3(x, 0.09f, 1.52f), vec3(0.2f, 0.06f, 0.18f), 0.05f, C(c), M(MAT_CLOTH), true);
            rbox(b, vec3(x, 0.08f, 1.2f), vec3(0.18f, 0.05f, 0.18f), 0.05f, C(c * 0.9f), M(MAT_CLOTH), true);
        } else if (k == 2) {
            // tote bag
            rbox(b, vec3(x, 0.07f, 1.42f), vec3(0.14f, 0.04f, 0.17f), 0.03f, C(hsv(r.f(), 0.3f, 0.75f)), M(MAT_FABRIC), true);
            tube(b, vec3(x - 0.08f, 0.07f, 1.58f), vec3(x, 0.06f, 1.72f), 0.006f, 4, C(0.3f, 0.2f, 0.1f), M(MAT_LEATHER));
            tube(b, vec3(x + 0.08f, 0.07f, 1.58f), vec3(x, 0.06f, 1.72f), 0.006f, 4, C(0.3f, 0.2f, 0.1f), M(MAT_LEATHER));
        }
    }
    // shoe rack below with a few pairs
    box(b, vec3(0, 0.16f, 0.16f), vec3(0.4f, 0.14f, 0.012f), C(0.4f, 0.27f, 0.16f), M(MAT_WOOD), SK_NONE);
    for (int s = -1; s <= 1; s += 2) box(b, vec3(s * 0.39f, 0.16f, 0.09f), vec3(0.012f, 0.14f, 0.09f), C(0.4f, 0.27f, 0.16f), M(MAT_WOOD), SK_NZ);
    for (int k = 0; k < 3; k++) {
        vec3 c = hsv(r.f(), r.range(0.1f, 0.6f), r.range(0.15f, 0.8f));
        for (int e = 0; e < 2; e++) rbox(b, vec3(-0.26f + k * 0.26f + e * 0.1f - 0.05f, 0.17f, 0.21f), vec3(0.04f, 0.12f, 0.04f), 0.03f, C(c), M(MAT_LEATHER));
    }
}

// Small round side table (lathe top + stem)
void sideTable(IB& b, vec3 p, u32 col) {
    lathe(b, p, {vec2(0.16f, 0.f), vec2(0.16f, 0.02f), vec2(0.03f, 0.05f), vec2(0.025f, 0.5f), vec2(0.24f, 0.52f), vec2(0.24f, 0.55f)}, 14, col, M(MAT_WOOD), true);
    collide(b, p + vec3(0, 0, 0.28f), vec3(0.2f, 0.2f, 0.28f));
}

// Computer desk with two monitors, a tower with an RGB strip, keyboard, energy drinks (front +y = user side, back
// against a wall at y = 0)
void computerDesk(IB& b, vec3 p, float yaw, u32 seed, int roomIdx) {
    At at(b, p, yaw);
    Rng r(seed);
    const float w = 1.5f, dp = 0.7f, H = 0.75f;
    box(b, vec3(0, dp * 0.5f, H - 0.015f), vec3(w * 0.5f, dp * 0.5f, 0.015f), C(0.1f, 0.1f, 0.11f), M(MAT_WOOD), SK_NONE);
    for (int sx = -1; sx <= 1; sx += 2)
        box(b, vec3(sx * (w * 0.5f - 0.03f), dp * 0.5f, (H - 0.03f) * 0.5f), vec3(0.025f, dp * 0.5f - 0.03f, (H - 0.03f) * 0.5f), Gy(0.15f), M(MAT_METAL_PAINTED), SK_NZ);
    for (int k = 0; k < 2; k++) {
        float x = k == 0 ? -0.33f : 0.33f;
        At m(b, vec3(x, 0.2f, H), k == 0 ? -0.22f : 0.22f);
        box(b, vec3(0, 0, 0.006f), vec3(0.11f, 0.08f, 0.006f), Gy(0.1f), M(MAT_PLASTIC), SK_NZ);
        box(b, vec3(0, -0.03f, 0.13f), vec3(0.022f, 0.015f, 0.12f), Gy(0.1f), M(MAT_PLASTIC), SK_NZ);
        rbox(b, vec3(0, -0.005f, 0.37f), vec3(0.3f, 0.02f, 0.18f), 0.005f, Gy(0.05f), M(MAT_PLASTIC), true);
        vec3 sc = k == 0 ? vec3(0.04f, 0.16f, 0.08f) : vec3(0.05f, 0.09f, 0.2f);
        box(b, vec3(0, 0.0165f, 0.37f), vec3(0.28f, 0.001f, 0.16f), C(sc, 0.7f), EM(), SK_NZ);
        InPart ip(b, IP_DETAIL);
        for (int l = 0; l < 9; l++) {
            float lw = r.range(0.04f, 0.2f), ind = (float)r.irange(0, 3) * 0.025f;
            vec3 tc = k == 0 ? vec3(0.3f, 1.f, 0.45f) : (r.chance(0.3f) ? vec3(1.f, 0.7f, 0.3f) : vec3(0.55f, 0.75f, 1.f));
            box(b, vec3(-0.25f + ind + lw, 0.0185f, 0.5f - l * 0.03f), vec3(lw, 0.001f, 0.005f), C(tc * 0.6f, 0.9f), EM(), SK_NZ);
        }
    }
    {
        InPart ip(b, IP_DETAIL);
        // keyboard with a backlight, mouse on a pad
        rbox(b, vec3(-0.05f, 0.45f, H + 0.012f), vec3(0.22f, 0.07f, 0.012f), 0.005f, Gy(0.06f), M(MAT_PLASTIC));
        box(b, vec3(-0.05f, 0.45f, H + 0.0245f), vec3(0.2f, 0.06f, 0.0005f), C(0.3f, 0.1f, 0.5f, 0.5f), EM(3, (u32)(seed & 255u)), SK_NZ);
        box(b, vec3(0.32f, 0.45f, H + 0.002f), vec3(0.14f, 0.12f, 0.002f), Gy(0.08f), M(MAT_RUBBER), SK_NZ);
        rbox(b, vec3(0.32f, 0.45f, H + 0.017f), vec3(0.032f, 0.055f, 0.016f), 0.014f, Gy(0.08f), M(MAT_PLASTIC));
        for (int k = 0; k < 3; k++) can(b, vec3(0.55f - k * 0.08f, 0.2f + (k & 1) * 0.07f, H), 0.033f, 0.16f, C(hsv(0.3f + k * 0.2f, 0.9f, 0.7f)), Gy(0.7f));
        // headset
        b.pushAxes(vec3(-0.6f, 0.35f, H + 0.09f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0));
        for (int k = 0; k < 8; k++) {
            float a0 = kPi * k / 8.f, a1 = kPi * (k + 1) / 8.f;
            tube(b, vec3(cosf(a0) * 0.09f, sinf(a0) * 0.09f - 0.09f, 0.f), vec3(cosf(a1) * 0.09f, sinf(a1) * 0.09f - 0.09f, 0.f), 0.012f, 5, Gy(0.1f), M(MAT_PLASTIC));
        }
        b.pop();
    }
    // tower under the desk with an RGB strip
    rbox(b, vec3(0.5f, 0.35f, 0.23f), vec3(0.1f, 0.24f, 0.23f), 0.01f, Gy(0.06f), M(MAT_PLASTIC), true);
    box(b, vec3(0.5f, 0.591f, 0.23f), vec3(0.006f, 0.001f, 0.19f), C(0.5f, 0.2f, 1.f, 0.6f), EM(3, (u32)((seed >> 8) & 255u)), SK_NZ);
    light(b, vec3(0.f, 0.55f, H + 0.45f), vec3(0.45f, 0.6f, 1.f) * 10.f, 2.2f, roomIdx);
    collide(b, vec3(0, dp * 0.5f, H * 0.5f), vec3(w * 0.5f, dp * 0.5f, H * 0.5f));
}

// Flush dome ceiling light (trailers, bathrooms)
void domeLight(IB& b, vec3 p, int roomIdx, float cd = 110.f, vec3 tint = vec3(1.f, 0.84f, 0.62f)) {
    InPart ip(b, IP_SHELL);
    cyl(b, p - vec3(0, 0, 0.02f), 0.16f, 0.16f, 0.02f, 16, Gy(0.85f), M(MAT_METAL_PAINTED), false, true);
    b.pushAxes(p - vec3(0, 0, 0.02f), vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 0, -1));
    lathe(b, vec3(0.f), {vec2(0.15f, 0.f), vec2(0.13f, 0.05f), vec2(0.07f, 0.085f), vec2(0.f, 0.095f)}, 16, C(tint, 0.9f), EM(), false);
    b.pop();
    light(b, p - vec3(0, 0, 0.14f), tint * cd, 4.5f, roomIdx);
}

// Kettle grill on three legs
void kettleGrill(IB& b, vec3 p) {
    At at(b, p, 0.f);
    for (int k = 0; k < 3; k++) {
        float a = kTwoPi * k / 3.f;
        tube(b, vec3(cosf(a) * 0.24f, sinf(a) * 0.24f, 0.f), vec3(cosf(a) * 0.15f, sinf(a) * 0.15f, 0.62f), 0.012f, 5, Gy(0.2f), M(MAT_METAL_PAINTED));
    }
    lathe(b, vec3(0, 0, 0.55f), {vec2(0.05f, 0.f), vec2(0.2f, 0.06f), vec2(0.28f, 0.2f), vec2(0.29f, 0.3f)}, 16, Gy(0.08f), M(MAT_METAL_PAINTED), false);
    lathe(b, vec3(0, 0, 0.85f), {vec2(0.29f, 0.f), vec2(0.27f, 0.1f), vec2(0.18f, 0.2f), vec2(0.f, 0.24f)}, 16, Gy(0.08f), M(MAT_METAL_PAINTED), false);
    tube(b, vec3(-0.07f, 0.f, 1.12f), vec3(0.07f, 0.f, 1.12f), 0.012f, 6, C(0.35f, 0.22f, 0.12f), M(MAT_WOOD), true);
    collide(b, vec3(0, 0, 0.55f), vec3(0.28f, 0.28f, 0.55f));
}

// Molded plastic patio chair (front +y)
void patioChair(IB& b, vec3 p, float yaw, u32 col) {
    At at(b, p, yaw);
    u32 m = M(MAT_PLASTIC);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) tube(b, vec3(sx * 0.24f, sy * 0.22f, 0.f), vec3(sx * 0.21f, sy * 0.2f, 0.42f), 0.018f, 6, col, m);
    rbox(b, vec3(0, 0.02f, 0.43f), vec3(0.24f, 0.23f, 0.02f), 0.015f, col, m, true);
    b.pushAxes(vec3(0, -0.21f, 0.66f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.25f)), normalize(vec3(0, -0.25f, 1)));
    rbox(b, vec3(0.f), vec3(0.24f, 0.015f, 0.22f), 0.012f, col, m, true);
    b.pop();
    for (int sx = -1; sx <= 1; sx += 2) box(b, vec3(sx * 0.25f, 0.f, 0.62f), vec3(0.02f, 0.22f, 0.015f), col, m, SK_NONE);
}

// Propane cylinder (tall bottle with a guard collar)
void propaneTank(IB& b, vec3 p) {
    lathe(b, p, {vec2(0.16f, 0.f), vec2(0.19f, 0.04f), vec2(0.19f, 0.95f), vec2(0.14f, 1.05f), vec2(0.06f, 1.1f)}, 14, Gy(0.92f), M(MAT_METAL_PAINTED), true);
    cyl(b, p + vec3(0, 0, 1.1f), 0.08f, 0.08f, 0.14f, 10, Gy(0.6f), M(MAT_METAL_PAINTED), false, false);
    collide(b, p + vec3(0, 0, 0.6f), vec3(0.19f, 0.19f, 0.6f));
}

// ------------------------------------------------------------------------------------------------ Mari's apartment
void layoutApartment(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0xA9A7u);
    const float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT, H = d.ceil;
    float dx0 = 0.f, dx1 = 0.f, dh = 2.2f;
    mainDoorOpening(d, dx0, dx1, dh);
    const float doorX = (dx0 + dx1) * 0.5f;
    const float W = X1 - X0, D = Y1 - Y0;
    const float pt = 0.12f;                                   // partition thickness
    const float ym = Y0 + Clamp(D * 0.54f, 4.4f, 6.4f);       // living zone | private zone
    const float s = doorX > (X0 + X1) * 0.5f ? -1.f : 1.f;    // kitchen + bath side, away from the entrance (-1 left)
    const float KW = s < 0.f ? X0 : X1, LW = s < 0.f ? X1 : X0;
    const float dirL = -s;                                    // from the kitchen wall toward the living wall
    auto kx = [&](float off) { return KW + dirL * off; };
    auto lx = [&](float off) { return LW - dirL * off; };
    const float yawKW = dirL > 0.f ? -kHalfPi : kHalfPi;      // faces +dirL (against the kitchen wall)
    const float yawLW = -yawKW;                               // faces -dirL (against the living wall)
    const float bathW = Clamp(W * 0.3f, 2.4f, 3.0f);
    const float xb = kx(bathW + pt * 0.5f);                   // bath | bedroom partition center
    const float bathA = KW, bathB = xb - dirL * pt * 0.5f, bedA = xb + dirL * pt * 0.5f, bedB = LW;
    // ---- rooms
    int living = room(b, vec3(X0, Y0, 0.f), vec3(X1, ym - pt * 0.5f, H), vec3(15.f, 12.5f, 9.f), 0.12f, LS_EVENING);
    int bath = room(b, vec3(Min(bathA, bathB), ym + pt * 0.5f, 0.f), vec3(Max(bathA, bathB), Y1, H), vec3(16.f, 16.f, 15.f), 0.f, LS_ALWAYS);
    int bedroom = room(b, vec3(Min(bedA, bedB), ym + pt * 0.5f, 0.f), vec3(Max(bedA, bedB), Y1, H), vec3(10.f, 8.5f, 6.5f), 0.02f, LS_ALWAYS);
    // ---- doors: wood entrance door with a glass lite, bath and bedroom doors in the partition
    storefrontEntrance(b, DK_HINGED, 3, C(0.34f, 0.2f, 0.12f));
    const float bathDoorX = kx(bathW * 0.5f);
    const float bedDoorX = xb + dirL * (pt * 0.5f + 0.25f + 0.45f);
    door(b, vec3(bathDoorX, ym, 0.f), vec2(dirL, 0.f), vec2(0, 1), 0.8f, 2.05f, DK_HINGED, 0, C(0.93f, 0.92f, 0.88f), false);
    door(b, vec3(bedDoorX, ym, 0.f), vec2(-dirL, 0.f), vec2(0, 1), 0.9f, 2.05f, DK_HINGED, 0, C(0.93f, 0.92f, 0.88f), false);
    partitionX(b, X0, X1, ym, pt, H, {vec2(bathDoorX - 0.4f, bathDoorX + 0.4f), vec2(bedDoorX - 0.45f, bedDoorX + 0.45f)});
    partitionY(b, ym + pt * 0.5f, Y1, xb, pt, H);
    // ---- shells
    vec3 wallTone = hsv(r.range(0.05f, 0.13f), r.range(0.1f, 0.22f), 0.88f);
    ShellStyle ls;
    ls.wallCol = C(wallTone);
    ls.floorMat = M(MAT_WOOD_FLOOR);
    ls.floorCol = C(0.66f, 0.48f, 0.32f);
    ls.ceilCol = Gy(0.93f);
    ls.baseH = 0.11f;
    ls.baseCol = Gy(0.95f);
    ls.crownH = 0.08f;
    ls.revealCol = Gy(0.92f);
    shell(b, living, ls);
    ShellStyle bs = ls;
    bs.wallCol = C(hsv(r.range(0.5f, 0.62f), 0.16f, 0.78f));
    shell(b, bedroom, bs);
    ShellStyle ws;
    ws.wallCol = Gy(0.93f);
    ws.floorMat = M(MAT_TILE);
    ws.floorCol = C(0.8f, 0.82f, 0.83f);
    ws.floorUV = 1.5f;
    ws.wainscotH = 1.25f;
    ws.wainCol = C(hsv(r.range(0.45f, 0.58f), 0.25f, 0.85f));
    ws.wainMat = M(MAT_TILE);
    ws.baseH = 0.f;
    ws.ceilCol = Gy(0.95f);
    shell(b, bath, ws);
    doorFrameInner(b, vec3(bathDoorX, ym, 0.f), vec2(1, 0), vec2(0, 1), 0.8f, 2.05f, pt, Gy(0.95f));
    doorFrameInner(b, vec3(bedDoorX, ym, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.05f, pt, Gy(0.95f));
    // window curtains (front facade windows)
    {
        vec3 cc = hsv(r.f(), r.range(0.2f, 0.5f), r.range(0.55f, 0.85f));
        for (const InteriorOpening& op : d.openings) {
            if (op.kind != OP_GLASS) continue;
            vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
            if (fabsf(a.y) > 0.1f || op.z0 - d.origin.z > 2.f) continue;
            if (Min(a.x, c.x) < Min(dx0, dx1) + 0.05f && Max(a.x, c.x) > Max(dx0, dx1) - 0.05f) continue;   // transom over the door
            curtains(b, Min(a.x, c.x), Max(a.x, c.x), op.z0 - d.origin.z, Min(op.z1 - d.origin.z, H - 0.2f), Y0 + 0.07f, C(cc), r.next(), 0.18f);
        }
    }
    const u32 wood = r.chance(0.5f) ? C(0.42f, 0.28f, 0.16f) : C(0.2f, 0.14f, 0.1f);
    // ---- kitchen along the kitchen wall, fridge at the partition end
    {
        float kyEnd = ym - pt * 0.5f - 0.74f;
        float kl = Clamp(kyEnd - (Y0 + 1.1f), 1.8f, 3.6f);
        float kyc = kyEnd - kl * 0.5f;
        u32 cab = r.chance(0.5f) ? C(0.9f, 0.9f, 0.86f) : C(0.26f, 0.4f, 0.38f);
        kitchenRun(b, vec3(KW, kyc, 0.f), yawKW, kl, cab, C(0.25f, 0.25f, 0.27f), true, true, true, r.next(), living);
        fridge(b, vec3(KW, kyEnd + 0.37f, 0.f), yawKW, Gy(0.86f), r.next());
        {
            InPart ip(b, IP_SHELL);
            float ya = kyc - kl * 0.5f - 0.3f, yb2 = ym - pt * 0.5f, xa = KW, xc = kx(1.75f);
            box(b, vec3((xa + xc) * 0.5f, (ya + yb2) * 0.5f, 0.003f), vec3(fabsf(xc - xa) * 0.5f, (yb2 - ya) * 0.5f, 0.003f), C(0.78f, 0.74f, 0.66f), M(MAT_TILE), SK_NZ);
        }
        downlight(b, vec3(kx(1.0f), kyc - kl * 0.25f, H), living, 240.f);
        downlight(b, vec3(kx(1.0f), kyc + kl * 0.25f, H), living, 240.f);
        {
            At at(b, vec3(KW, kyc, 0.f), yawKW);
            wallClock(b, vec3(0.f, 0.f, 2.35f), r.next());
        }
    }
    // ---- dining table by the front window on the kitchen side
    {
        vec2 dt(kx(2.45f), Y0 + 1.75f);
        if (fabsf(dt.x - doorX) < 2.1f) dt.y = Y0 + 2.9f;
        u32 tc = C(0.5f, 0.34f, 0.2f);
        diningTable(b, vec3(dt, 0.f), 0.f, 1.2f, 0.8f, tc, false, r.next());
        for (int k = 0; k < 4; k++) {
            float cx = dt.x + ((k & 1) ? 0.3f : -0.3f), cy = dt.y + ((k & 2) ? 0.62f : -0.62f);
            chair(b, vec3(cx, cy, 0.f), (k & 2) ? kPi : 0.f, tc, 0, true);
        }
        InPart ip(b, IP_DETAIL);
        // laptop and a mug
        At at(b, vec3(dt.x - 0.15f, dt.y + 0.05f, 0.767f), 0.3f);
        box(b, vec3(0, 0, 0.008f), vec3(0.17f, 0.12f, 0.008f), Gy(0.7f), M(MAT_METAL_BRUSHED), SK_NZ);
        b.pushAxes(vec3(0, -0.12f, 0.016f), vec3(1, 0, 0), normalize(vec3(0, -0.25f, 1.f)), normalize(vec3(0, 1.f, 0.25f)));
        box(b, vec3(0, 0.12f, -0.004f), vec3(0.17f, 0.12f, 0.004f), Gy(0.7f), M(MAT_METAL_BRUSHED), SK_NONE);
        box(b, vec3(0, 0.12f, 0.0005f), vec3(0.155f, 0.1f, 0.0005f), C(0.2f, 0.3f, 0.45f, 0.6f), EM(), SK_NZ);
        b.pop();
        cyl(b, vec3(0.3f, 0.1f, 0.f), 0.04f, 0.04f, 0.1f, 10, C(0.85f, 0.3f, 0.2f), M(MAT_PAINT_WHITE), false);
        pendant(b, vec3(dt, H), H - 1.6f, living, C(hsv(r.f(), 0.5f, 0.5f)), 200.f);
    }
    // ---- living area: TV on the living wall, sofa facing it, coffee table, rug, armchair, lamps, bookshelf
    {
        float ly = (Y0 + ym - pt * 0.5f) * 0.5f + 0.15f;
        u32 fab = C(hsv(r.f(), r.range(0.15f, 0.45f), r.range(0.35f, 0.6f)));
        rug(b, vec3(lx(2.35f), ly, 0.f), 2.6f, 2.5f, r.next());
        tvUnit(b, vec3(LW - dirL * 0.23f, ly, 0.f), yawLW, 1.7f, r.next(), living, false);
        sofa(b, vec3(lx(3.35f), ly, 0.f), yawKW, 2.1f, fab, r.next());
        coffeeTable(b, vec3(lx(2.15f), ly, 0.f), yawKW, 1.1f, 0.6f, r.next());
        vec3 ac(lx(2.15f), ly - 1.8f, 0.f);
        armchair(b, ac, atan2f(-(lx(2.15f) - ac.x), ly - ac.y) * 0.5f, C(hsv(r.f(), 0.35f, 0.45f)), r.next());
        floorLamp(b, vec3(lx(3.35f), ly + 1.4f, 0.f), living, C(0.95f, 0.9f, 0.8f));
        sideTable(b, vec3(lx(3.35f), ly - 1.35f, 0.f), wood);
        tableLamp(b, vec3(lx(3.35f), ly - 1.35f, 0.55f), living, C(hsv(r.f(), 0.4f, 0.7f)), C(0.95f, 0.92f, 0.85f));
        bookshelf(b, vec3(lx(1.0f), ym - pt * 0.5f, 0.f), kPi, 1.2f, 2.0f, wood, r.next());
        plant(b, vec3(lx(0.45f), Y0 + 0.45f, 0.f), 1.0f, r.next(), 0, 1);
        plant(b, vec3(kx(0.4f), Y0 + 0.45f, 0.f), 0.7f, r.next());
        ceilingFan(b, vec3(lx(2.6f), ly, H), living, C(0.45f, 0.3f, 0.18f));
        {
            At at(b, vec3(LW, ly, 0.f), yawLW);
            picture(b, vec3(0.f, 0.f, 1.75f), 1.1f, 0.7f, r.next());
        }
        {
            At at(b, vec3(lx(1.0f), ym - pt * 0.5f, 0.f), kPi);
            picture(b, vec3(-1.3f * dirL, 0.f, 1.55f), 0.5f, 0.65f, r.next());
        }
        coatHooks(b, vec3(doorX + (doorX > (X0 + X1) * 0.5f ? -1.25f : 1.25f), Y0, 0.f), 0.f, r.next());
    }
    // ---- bedroom
    {
        float bedCx = (bedA + bedB) * 0.5f + dirL * 0.25f;
        u32 duvet = C(hsv(r.f(), r.range(0.2f, 0.5f), r.range(0.55f, 0.85f)));
        bed(b, vec3(bedCx, Y1 - 1.1f, 0.f), kPi, 1.6f, 2.05f, wood, duvet, r.next(), true);
        nightstand(b, vec3(bedCx - 1.12f, Y1 - 0.22f, 0.f), kPi, wood, bedroom, r.next());
        nightstand(b, vec3(bedCx + 1.12f, Y1 - 0.22f, 0.f), kPi, wood, bedroom, r.next());
        rug(b, vec3(bedCx, Y1 - 2.55f, 0.f), 2.2f, 1.3f, r.next());
        float wy = ym + pt * 0.5f + 0.25f + 0.8f;
        wardrobe(b, vec3(LW, wy, 0.f), yawLW, 1.6f, wood, r.next());
        dresser(b, vec3(lx(2.3f), ym + pt * 0.5f, 0.f), 0.f, 1.3f, wood, r.next());
        clothesPile(b, vec3(bedCx + dirL * 1.55f, Y1 - 2.7f, 0.f), r.next());
        pendant(b, vec3(bedCx, Y1 - 2.4f, H), 0.5f, bedroom, C(hsv(r.f(), 0.3f, 0.8f)), 170.f, vec3(1.f, 0.8f, 0.55f), 5.f, 2);
        {
            At at(b, vec3(bedCx, Y1, 0.f), kPi);
            picture(b, vec3(0.f, 0.f, 1.55f), 1.3f, 0.5f, r.next());
        }
        marker(b, IM_BED, vec3(bedCx, Y1 - 2.2f - 0.45f, 0.f), 0.f);
        marker(b, IM_WARDROBE, vec3(LW - dirL * 1.15f, wy, 0.f), yawKW);
        marker(b, IM_MIRROR, vec3(lx(2.3f), ym + pt * 0.5f + 1.0f, 0.f), kPi);
    }
    // ---- bathroom
    {
        float bcx = (bathA + bathB) * 0.5f;
        bathtub(b, vec3(bcx, Y1, 0.f), kPi, Min(1.7f, bathW - 0.06f), C(hsv(r.f(), 0.3f, 0.85f)));
        toilet(b, vec3(KW + dirL * 0.31f, ym + 2.2f, 0.f), yawKW);
        bathSink(b, vec3(xb - dirL * pt * 0.5f, ym + 1.4f, 0.f), yawLW, Gy(0.95f), bath);
        {
            At at(b, vec3(xb - dirL * pt * 0.5f, ym + 2.6f, 0.f), yawLW);
            towelRail(b, vec3(0.f), 0.f, C(hsv(r.f(), 0.4f, 0.8f)));
        }
        {
            InPart ip(b, IP_FURNITURE);
            box(b, vec3(bcx, Y1 - 1.05f, 0.005f), vec3(0.4f, 0.25f, 0.005f), C(hsv(r.f(), 0.3f, 0.8f)), M(MAT_CARPET), SK_NZ);
        }
        domeLight(b, vec3(bcx, (ym + Y1) * 0.5f, H), bath, 140.f, vec3(1.f, 0.95f, 0.88f));
    }
    roomDressing(b, living, false, d.seed ^ 0x61u);
    roomDressing(b, bedroom, false, d.seed ^ 0x62u);
}

// ------------------------------------------------------------------------------------------------ Dex's trailer
void layoutTrailer(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0x7A11u);
    const float hx = (d.x1 - d.x0) * 0.5f, depth = d.depth;
    const float Lt = Clamp(2.f * hx - 4.f, 13.f, 16.5f), Wt = 4.3f;
    const float tx0 = -Lt * 0.5f, tx1 = Lt * 0.5f;
    float ty0 = Clamp(depth * 0.3f, 2.4f, 3.5f);
    if (ty0 + Wt > depth - 0.6f) ty0 = Max(2.4f, depth - 0.6f - Wt);
    const float ty1 = ty0 + Wt;
    const float zf = 0.62f, wt = 0.1f, ih = 2.15f, pt = 0.08f, zc = zf + ih;
    const float ix0 = tx0 + wt, ix1 = tx1 - wt, iy0 = ty0 + wt, iy1 = ty1 - wt;
    const float xp0 = ix0 + 3.5f + pt * 0.5f;     // bedroom | living partition center
    const float xp1 = ix1 - 2.3f - pt * 0.5f;     // living | bath partition center
    const float doorX = xp0 + 3.3f;
    // ---- rooms (floor raised on blocks)
    int bedroom = room(b, vec3(ix0, iy0, zf), vec3(xp0 - pt * 0.5f, iy1, zc), vec3(8.f, 6.5f, 5.f), 0.06f, LS_EVENING);
    int living = room(b, vec3(xp0 + pt * 0.5f, iy0, zf), vec3(xp1 - pt * 0.5f, iy1, zc), vec3(11.f, 9.f, 7.f), 0.1f, LS_EVENING);
    int bath = room(b, vec3(xp1 + pt * 0.5f, iy0, zf), vec3(ix1, iy1, zc), vec3(12.f, 12.f, 11.f), 0.02f, LS_ALWAYS);
    // ---- openings: walls run counter-clockwise (front +x at ty0, right end +y, back -x at ty1, left end -y)
    const float wz0 = zf + 0.95f, wz1 = zf + 1.8f;
    openingLocal(b, vec2(doorX - 0.45f, ty0), vec2(doorX + 0.45f, ty0), zf, zf + 1.98f, OP_DOOR);
    openingLocal(b, vec2(xp0 + 0.6f, ty0), vec2(xp0 + 1.9f, ty0), wz0, wz1, OP_GLASS);          // over the sofa
    if (doorX + 2.5f < xp1 - 0.8f) openingLocal(b, vec2(doorX + 1.3f, ty0), vec2(doorX + 2.5f, ty0), wz0, wz1, OP_GLASS);
    openingLocal(b, vec2(xp1 + 0.35f, ty0), vec2(xp1 + 0.9f, ty0), zf + 1.45f, zf + 1.9f, OP_GLASS);   // bath
    const float runC = xp1 - 2.1f;                                                                            // kitchen run center
    openingLocal(b, vec2(xp0 + 3.6f, ty1), vec2(xp0 + 2.4f, ty1), wz0, wz1, OP_GLASS);                      // living back
    openingLocal(b, vec2(ix0 + 2.9f, ty1), vec2(ix0 + 1.9f, ty1), wz0, wz1, OP_GLASS);                    // bedroom back
    openingLocal(b, vec2(tx0, iy1 - 0.4f), vec2(tx0, iy1 - 1.3f), zf + 1.2f, zf + 1.8f, OP_GLASS);         // over the headboard
    // ---- doors
    door(b, vec3(doorX, ty0 + wt * 0.5f, zf), vec2(1, 0), vec2(0, 1), 0.86f, 1.96f, DK_HINGED, 3, C(0.86f, 0.86f, 0.82f), true, 0.f);
    const float bedDoorY = iy0 + 0.55f, bathDoorY = iy0 + 1.4f;
    door(b, vec3(xp0, bedDoorY, zf), vec2(0, 1), vec2(-1, 0), 0.8f, 1.95f, DK_HINGED, 0, C(0.55f, 0.38f, 0.24f), false);
    door(b, vec3(xp1, bathDoorY, zf), vec2(0, -1), vec2(1, 0), 0.75f, 1.95f, DK_HINGED, 0, C(0.55f, 0.38f, 0.24f), false);
    marker(b, IM_ENTRY, vec3(doorX, iy0 + 0.9f, zf), 0.f);
    // ---- collision: floor on blocks, walls, partitions
    collideMM(b, vec3(tx0, ty0, -0.6f), vec3(tx1, ty1, zf));
    {
        At at(b, vec3(0.f, 0.f, zf), 0.f);
        partitionX(b, tx0, tx1, ty0 + wt * 0.5f, wt, ih, {vec2(doorX - 0.45f, doorX + 0.45f)});
        partitionX(b, tx0, tx1, ty1 - wt * 0.5f, wt, ih);
        partitionY(b, ty0, ty1, tx0 + wt * 0.5f, wt, ih);
        partitionY(b, ty0, ty1, tx1 - wt * 0.5f, wt, ih);
        partitionY(b, iy0, iy1, xp0, pt, ih, {vec2(bedDoorY - 0.4f, bedDoorY + 0.4f)});
        partitionY(b, iy0, iy1, xp1, pt, ih, {vec2(bathDoorY - 0.38f, bathDoorY + 0.38f)});
    }
    // ---- interior shells: wood paneling, carpet, linoleum in the bath
    ShellStyle ps;
    ps.wallCol = C(0.5f, 0.36f, 0.24f);
    ps.wallMat = M(MAT_WOOD);
    ps.floorMat = M(MAT_CARPET);
    ps.floorCol = C(0.42f, 0.36f, 0.3f);
    ps.ceilCol = C(0.86f, 0.84f, 0.8f);
    ps.ceilMat = M(MAT_CEILING_TILE);
    ps.ceilUV = 0.5f;
    ps.baseH = 0.07f;
    ps.baseCol = C(0.36f, 0.25f, 0.16f);
    ps.baseMat = M(MAT_WOOD);
    ps.revealCol = C(0.8f, 0.78f, 0.74f);
    shell(b, bedroom, ps);
    shell(b, living, ps);
    ShellStyle bs = ps;
    bs.wallCol = C(0.86f, 0.84f, 0.76f);
    bs.wallMat = M(MAT_PLASTER);
    bs.floorMat = M(MAT_TILE);
    bs.floorCol = C(0.74f, 0.7f, 0.6f);
    bs.wainscotH = 1.1f;
    bs.wainCol = C(0.62f, 0.72f, 0.66f);
    bs.wainMat = M(MAT_TILE);
    shell(b, bath, bs);
    doorFrameInner(b, vec3(xp0, bedDoorY, zf), vec2(0, 1), vec2(-1, 0), 0.8f, 1.95f, pt, C(0.4f, 0.28f, 0.18f));
    doorFrameInner(b, vec3(xp1, bathDoorY, zf), vec2(0, -1), vec2(1, 0), 0.75f, 1.95f, pt, C(0.4f, 0.28f, 0.18f));
    {
        // linoleum in the kitchen part of the living room
        InPart ip(b, IP_SHELL);
        float ka = runC - 1.4f, kb = xp1 - pt * 0.5f;
        box(b, vec3((ka + kb) * 0.5f, (iy0 + iy1) * 0.5f + 0.9f, zf + 0.003f), vec3((kb - ka) * 0.5f, (iy1 - iy0) * 0.5f - 0.9f, 0.003f), C(0.72f, 0.66f, 0.52f), M(MAT_TILE), SK_NZ);
    }
    // ---- exterior skin: two-tone lap siding, trim band, skirting, gable roof
    {
        const vec2 A(tx0, ty0), B(tx1, ty0), Cc(tx1, ty1), Dd(tx0, ty1);
        const vec2 wallsA[4] = {A, B, Cc, Dd}, wallsB[4] = {B, Cc, Dd, A};
        vec3 lower = hsv(r.range(0.45f, 0.58f), 0.35f, 0.6f), upper = vec3(0.9f, 0.88f, 0.8f);
        const float band = zf + 0.95f;
        for (int k = 0; k < 4; k++) {
            exteriorWall(b, wallsA[k], wallsB[k], zf - 0.15f, band, C(lower), M(MAT_WOOD_SIDING));
            exteriorWall(b, wallsA[k], wallsB[k], band, zc + 0.12f, C(upper), M(MAT_WOOD_SIDING));
            exteriorWall(b, wallsA[k], wallsB[k], -0.45f, zf - 0.15f, C(0.62f, 0.6f, 0.56f), M(MAT_METAL_PAINTED));   // skirting
        }
        InPart ip(b, IP_SHELL);
        u32 trim = Gy(0.92f);
        // trim band + corner trims
        box(b, vec3(0.f, ty0 - 0.012f, band), vec3(Lt * 0.5f + 0.012f, 0.012f, 0.035f), trim, M(MAT_METAL_PAINTED), SK_NONE);
        box(b, vec3(0.f, ty1 + 0.012f, band), vec3(Lt * 0.5f + 0.012f, 0.012f, 0.035f), trim, M(MAT_METAL_PAINTED), SK_NONE);
        box(b, vec3(tx0 - 0.012f, (ty0 + ty1) * 0.5f, band), vec3(0.012f, Wt * 0.5f, 0.035f), trim, M(MAT_METAL_PAINTED), SK_NONE);
        box(b, vec3(tx1 + 0.012f, (ty0 + ty1) * 0.5f, band), vec3(0.012f, Wt * 0.5f, 0.035f), trim, M(MAT_METAL_PAINTED), SK_NONE);
        for (int k = 0; k < 4; k++) {
            vec2 c = wallsA[k];
            box(b, vec3(c, (zf - 0.15f + zc + 0.12f) * 0.5f), vec3(0.04f, 0.04f, (zc + 0.27f - zf) * 0.5f), trim, M(MAT_METAL_PAINTED), SK_NONE);
        }
        // exterior window frames and two retro awnings
        for (const InteriorOpening& op : d.openings) {
            if (op.kind != OP_GLASS) continue;
            vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
            vec2 t = normalize(c.xy() - a.xy()), n(t.y, -t.x);
            float w = length(c.xy() - a.xy()), z0 = op.z0 - d.origin.z, z1 = op.z1 - d.origin.z;
            vec2 mid = (a.xy() + c.xy()) * 0.5f + n * 0.015f;
            At at(b, vec3(mid, 0.f), atan2f(t.y, t.x));   // local x along the wall, local -y = outward
            box(b, vec3(0.f, -0.0f, z1 + 0.03f), vec3(w * 0.5f + 0.06f, 0.02f, 0.03f), trim, M(MAT_METAL_PAINTED), SK_NONE);
            box(b, vec3(0.f, -0.0f, z0 - 0.03f), vec3(w * 0.5f + 0.06f, 0.03f, 0.03f), trim, M(MAT_METAL_PAINTED), SK_NONE);
            box(b, vec3(-w * 0.5f - 0.03f, 0.f, (z0 + z1) * 0.5f), vec3(0.03f, 0.02f, (z1 - z0) * 0.5f), trim, M(MAT_METAL_PAINTED), SK_NONE);
            box(b, vec3(w * 0.5f + 0.03f, 0.f, (z0 + z1) * 0.5f), vec3(0.03f, 0.02f, (z1 - z0) * 0.5f), trim, M(MAT_METAL_PAINTED), SK_NONE);
            if (w > 1.f && z1 - z0 > 0.7f) {
                // striped aluminium awning, sloping out and down
                b.pushAxes(vec3(0.f, 0.f, z1 + 0.12f), vec3(1, 0, 0), normalize(vec3(0, -1.f, -0.55f)), normalize(vec3(0, -0.55f, 1.f)));
                for (int k = 0; k < 6; k++) {
                    float x0 = -w * 0.5f - 0.1f + (w + 0.2f) * k / 6.f, x1 = x0 + (w + 0.2f) / 6.f;
                    u32 sc = (k & 1) ? Gy(0.92f) : C(lower);
                    box(b, vec3((x0 + x1) * 0.5f, 0.28f, 0.f), vec3((x1 - x0) * 0.5f, 0.28f, 0.006f), sc, M(MAT_METAL_PAINTED), SK_NONE);
                }
                b.pop();
            }
        }
        // gable roof along x: eaves over the long walls, ridge in the middle
        u32 roofC = Gy(0.72f), roofM = M(MAT_ROOF_METAL);
        float ze = zc + 0.12f, zr = zc + 0.5f, ym2 = (ty0 + ty1) * 0.5f, ov = 0.22f;
        float xa = tx0 - 0.15f, xb2 = tx1 + 0.15f;
        vec3 fe0(xa, ty0 - ov, ze - 0.06f), fe1(xb2, ty0 - ov, ze - 0.06f), r0(xa, ym2, zr), r1(xb2, ym2, zr);
        vec3 be0(xa, ty1 + ov, ze - 0.06f), be1(xb2, ty1 + ov, ze - 0.06f);
        quadF(b, fe0, fe1, r1, r0, vec3(0, -1, 3), roofC, roofM);
        quadF(b, be1, be0, r0, r1, vec3(0, 1, 3), roofC, roofM);
        quadF(b, fe0, fe1, r1, r0, vec3(0, 1, -3), Gy(0.5f), roofM);    // underside
        quadF(b, be1, be0, r0, r1, vec3(0, -1, -3), Gy(0.5f), roofM);
        // gable ends
        for (int e = 0; e < 2; e++) {
            float x = e == 0 ? tx0 : tx1;
            vec3 n(e == 0 ? -1.f : 1.f, 0, 0);
            vec3 p0(x, ty0, ze), p1(x, ty1, ze), p2(x, ym2, zr - 0.03f);
            if (e == 0) tri(b, p1, p0, p2, C(upper), M(MAT_WOOD_SIDING));
            else tri(b, p0, p1, p2, C(upper), M(MAT_WOOD_SIDING));
            (void)n;
        }
        // fascia boards along the eaves
        box(b, vec3(0.f, ty0 - ov, ze - 0.1f), vec3((xb2 - xa) * 0.5f, 0.015f, 0.06f), trim, M(MAT_METAL_PAINTED), SK_NONE);
        box(b, vec3(0.f, ty1 + ov, ze - 0.1f), vec3((xb2 - xa) * 0.5f, 0.015f, 0.06f), trim, M(MAT_METAL_PAINTED), SK_NONE);
        // satellite dish on the roof near the bedroom end
        {
            At at(b, vec3(tx0 + 1.4f, ym2 - 0.7f, zr - 0.2f), 2.4f);
            tube(b, vec3(0.f), vec3(0, 0, 0.45f), 0.02f, 6, Gy(0.8f), M(MAT_METAL_PAINTED));
            b.pushAxes(vec3(0, 0, 0.55f), vec3(1, 0, 0), normalize(vec3(0, 0.5f, -1.f)), normalize(vec3(0, 1.f, 0.5f)));
            lathe(b, vec3(0.f), {vec2(0.02f, 0.f), vec2(0.2f, 0.04f), vec2(0.3f, 0.09f)}, 16, Gy(0.9f), M(MAT_METAL_PAINTED), false);
            lathe(b, vec3(0.f), {vec2(0.3f, 0.09f), vec2(0.2f, 0.04f), vec2(0.02f, 0.f)}, 16, Gy(0.85f), M(MAT_METAL_PAINTED), false);
            tube(b, vec3(0, 0, 0.05f), vec3(0, 0, 0.35f), 0.01f, 5, Gy(0.3f), M(MAT_METAL_PAINTED), true);
            b.pop();
        }
    }
    glazeAll(b, 0.82f, Gy(0.85f), false);
    // ---- deck with steps, railing and an awning in front of the door
    {
        float dk0 = doorX - 1.9f, dk1 = doorX + 2.6f, dy0 = ty0 - 2.3f, dz = zf - 0.03f;
        vec3 foot = d.toWorld(vec3(doorX, dy0 - 0.9f, 0.f));
        float gz = gMap ? gMap->heightAt(foot.x, foot.y) - d.origin.z : 0.f;
        gz = Clamp(gz, -0.6f, dz - 0.2f);
        InPart ip(b, IP_SHELL);
        u32 plank = C(0.55f, 0.47f, 0.38f), pm = M(MAT_WOOD);
        for (int k = 0; k < 14; k++) {
            float x0 = dk0 + (dk1 - dk0) * k / 14.f, x1 = dk0 + (dk1 - dk0) * (k + 1) / 14.f - 0.008f;
            box(b, vec3((x0 + x1) * 0.5f, (dy0 + ty0) * 0.5f, dz - 0.02f), vec3((x1 - x0) * 0.5f, (ty0 - dy0) * 0.5f, 0.02f), C(rgbOf(plank) * r.range(0.85f, 1.08f)), pm, SK_NZ);
        }
        box(b, vec3((dk0 + dk1) * 0.5f, dy0 + 0.02f, (dz - 0.04f + gz) * 0.5f), vec3((dk1 - dk0) * 0.5f, 0.02f, (dz - 0.04f - gz) * 0.5f), C(0.45f, 0.38f, 0.3f), pm, SK_NZ);
        box(b, vec3(dk0 + 0.02f, (dy0 + ty0) * 0.5f, (dz - 0.04f + gz) * 0.5f), vec3(0.02f, (ty0 - dy0) * 0.5f, (dz - 0.04f - gz) * 0.5f), C(0.45f, 0.38f, 0.3f), pm, SK_NZ);
        box(b, vec3(dk1 - 0.02f, (dy0 + ty0) * 0.5f, (dz - 0.04f + gz) * 0.5f), vec3(0.02f, (ty0 - dy0) * 0.5f, (dz - 0.04f - gz) * 0.5f), C(0.45f, 0.38f, 0.3f), pm, SK_NZ);
        collideMM(b, vec3(dk0, dy0, -0.6f), vec3(dk1, ty0, dz));
        // steps down to the yard in front of the door
        int ns = Max(1, (int)ceilf((dz - gz) / 0.2f));
        float rise = (dz - gz) / (ns + 1);
        for (int k = 0; k < ns; k++) {
            float top = dz - rise * (k + 1), y1 = dy0 - 0.3f * k, y0s = y1 - 0.3f;
            box(b, vec3(doorX, (y0s + y1) * 0.5f, (top + gz - 0.1f) * 0.5f), vec3(0.6f, (y1 - y0s) * 0.5f, (top - gz + 0.1f) * 0.5f), plank, pm, SK_NZ);
            collideMM(b, vec3(doorX - 0.6f, y0s, gz - 0.3f), vec3(doorX + 0.6f, y1, top));
        }
        marker(b, IM_DOOR_OUT, vec3(doorX, dy0 - 0.3f * ns - 1.0f, gz), kPi);
        // railing on three sides (gap for the steps)
        u32 rail = C(0.6f, 0.52f, 0.42f);
        auto post = [&](float x, float y) { box(b, vec3(x, y, dz + 0.45f), vec3(0.04f, 0.04f, 0.45f), rail, pm, SK_NZ); };
        for (float x : {dk0 + 0.04f, doorX - 0.66f, doorX + 0.66f, dk1 - 0.04f}) post(x, dy0 + 0.04f);
        post(dk0 + 0.04f, ty0 - 0.1f);
        post(dk1 - 0.04f, ty0 - 0.1f);
        for (int e = 0; e < 2; e++) {
            float xa = e == 0 ? dk0 + 0.04f : doorX + 0.66f, xc = e == 0 ? doorX - 0.66f : dk1 - 0.04f;
            box(b, vec3((xa + xc) * 0.5f, dy0 + 0.04f, dz + 0.9f), vec3((xc - xa) * 0.5f, 0.05f, 0.025f), rail, pm, SK_NONE);
            box(b, vec3((xa + xc) * 0.5f, dy0 + 0.04f, dz + 0.45f), vec3((xc - xa) * 0.5f, 0.03f, 0.02f), rail, pm, SK_NONE);
            collideMM(b, vec3(xa, dy0, dz), vec3(xc, dy0 + 0.08f, dz + 0.95f));
        }
        for (int e = 0; e < 2; e++) {
            float x = e == 0 ? dk0 + 0.04f : dk1 - 0.04f;
            box(b, vec3(x, (dy0 + ty0) * 0.5f, dz + 0.9f), vec3(0.05f, (ty0 - dy0) * 0.5f - 0.05f, 0.025f), rail, pm, SK_NONE);
            box(b, vec3(x, (dy0 + ty0) * 0.5f, dz + 0.45f), vec3(0.03f, (ty0 - dy0) * 0.5f - 0.05f, 0.02f), rail, pm, SK_NONE);
            collideMM(b, vec3(x - 0.04f, dy0, dz), vec3(x + 0.04f, ty0, dz + 0.95f));
        }
        // awning: corrugated sheet on two posts, sloping away from the trailer
        float az0 = zc + 0.02f, az1 = zc - 0.28f;
        for (float x : {dk0 + 0.1f, dk1 - 0.1f}) box(b, vec3(x, dy0 + 0.1f, (dz + az1) * 0.5f), vec3(0.04f, 0.04f, (az1 - dz) * 0.5f), Gy(0.85f), M(MAT_METAL_PAINTED), SK_NZ);
        vec3 a0(dk0 - 0.1f, ty0, az0), a1(dk1 + 0.1f, ty0, az0), a2(dk1 + 0.1f, dy0 - 0.1f, az1), a3(dk0 - 0.1f, dy0 - 0.1f, az1);
        quadF(b, a0, a1, a2, a3, vec3(0, -0.3f, 1.f), Gy(0.8f), M(MAT_CORRUGATED));
        quadF(b, a0, a1, a2, a3, vec3(0, 0.3f, -1.f), Gy(0.6f), M(MAT_CORRUGATED));
        light(b, vec3(doorX + 0.7f, ty0 - 0.3f, zc - 0.2f), vec3(1.f, 0.8f, 0.55f) * 60.f, 5.f, -1);
        sphere(b, vec3(doorX + 0.7f, ty0 - 0.08f, zc - 0.15f), 0.07f, 8, C(1.f, 0.85f, 0.6f, 0.8f), EM(6), 1.2f);
        // deck furniture: two plastic chairs, a cooler, the grill
        InPart ip2(b, IP_FURNITURE);
        patioChair(b, vec3(dk1 - 0.6f, ty0 - 1.2f, dz), -2.4f, Gy(0.93f));
        patioChair(b, vec3(dk1 - 1.4f, ty0 - 1.7f, dz), 2.8f, C(0.3f, 0.55f, 0.35f));
        rbox(b, vec3(dk1 - 1.0f, ty0 - 0.5f, dz + 0.2f), vec3(0.33f, 0.22f, 0.2f), 0.03f, C(0.15f, 0.35f, 0.7f), M(MAT_PLASTIC), true);
        box(b, vec3(dk1 - 1.0f, ty0 - 0.5f, dz + 0.41f), vec3(0.34f, 0.23f, 0.012f), Gy(0.95f), M(MAT_PLASTIC), SK_NONE);
        clutterCans(b, vec3(dk1 - 1.0f, ty0 - 0.5f, dz + 0.422f), 3, r.next());
        kettleGrill(b, vec3(dk0 + 0.5f, dy0 + 0.55f, dz));
    }
    // ---- yard: propane tank at the bath end, trash can, clothesline, tires
    {
        InPart ip(b, IP_FURNITURE);
        propaneTank(b, vec3(tx1 + 0.35f, ty0 + 0.8f, -0.1f));
        propaneTank(b, vec3(tx1 + 0.35f, ty0 + 1.25f, -0.1f));
        vec3 tc(tx1 + 1.1f, ty0 - 0.6f, -0.1f);
        cyl(b, tc, 0.27f, 0.3f, 0.95f, 12, C(0.2f, 0.3f, 0.22f), M(MAT_PLASTIC), false);
        cyl(b, tc + vec3(0, 0, 0.95f), 0.32f, 0.3f, 0.06f, 12, C(0.2f, 0.3f, 0.22f), M(MAT_PLASTIC), true);
        collide(b, tc + vec3(0, 0, 0.5f), vec3(0.3f, 0.3f, 0.5f));
        if (ty1 + 3.f < depth + 1.f) {
            float cy = ty1 + 2.2f;
            for (float x : {-3.5f, 3.5f}) {
                tube(b, vec3(x, cy, -0.2f), vec3(x, cy, 1.9f), 0.03f, 6, Gy(0.6f), M(MAT_METAL_PAINTED));
                tube(b, vec3(x, cy - 0.5f, 1.85f), vec3(x, cy + 0.5f, 1.85f), 0.025f, 6, Gy(0.6f), M(MAT_METAL_PAINTED), true);
                collide(b, vec3(x, cy, 0.9f), vec3(0.05f, 0.05f, 1.1f));
            }
            for (int l = 0; l < 3; l++) tube(b, vec3(-3.5f, cy - 0.4f + l * 0.4f, 1.83f), vec3(3.5f, cy - 0.4f + l * 0.4f, 1.8f), 0.004f, 3, Gy(0.85f), M(MAT_PLASTIC));
            for (int k = 0; k < 5; k++) {
                vec3 c = hsv(r.f(), r.range(0.2f, 0.7f), r.range(0.4f, 0.9f));
                float x = -2.8f + k * 1.2f + r.range(-0.2f, 0.2f), y = cy - 0.4f + r.irange(0, 2) * 0.4f;
                quadF(b, vec3(x - 0.3f, y, 1.8f), vec3(x + 0.3f, y, 1.8f), vec3(x + 0.3f, y, 1.2f + r.range(0.f, 0.2f)), vec3(x - 0.3f, y, 1.25f), vec3(0, -1, 0), C(c), M(MAT_CLOTH));
                quadF(b, vec3(x - 0.3f, y, 1.8f), vec3(x + 0.3f, y, 1.8f), vec3(x + 0.3f, y, 1.2f), vec3(x - 0.3f, y, 1.25f), vec3(0, 1, 0), C(c * 0.85f), M(MAT_CLOTH));
            }
        }
        // stacked old tires by the bedroom end
        for (int k = 0; k < 3; k++) {
            vec3 tp(tx0 - 1.0f, ty0 + 0.6f, -0.1f + k * 0.2f);
            lathe(b, tp, {vec2(0.2f, 0.f), vec2(0.32f, 0.f), vec2(0.34f, 0.1f), vec2(0.32f, 0.2f), vec2(0.2f, 0.2f)}, 14, Gy(0.07f), M(MAT_RUBBER), false);
            lathe(b, tp, {vec2(0.2f, 0.2f), vec2(0.18f, 0.1f), vec2(0.2f, 0.f)}, 14, Gy(0.05f), M(MAT_RUBBER), false);
        }
        collide(b, vec3(tx0 - 1.0f, ty0 + 0.6f, 0.2f), vec3(0.34f, 0.34f, 0.3f));
    }
    // ---- living room: lounge by the bedroom partition, TV on the back wall, dinette, kitchen along the back wall
    {
        u32 fab = C(0.36f, 0.3f, 0.22f);
        At lift(b, vec3(0.f, 0.f, zf), 0.f);
        sofa(b, vec3(xp0 + 1.35f, iy0 + 0.48f, 0.f), 0.f, 2.0f, fab, r.next());
        tvUnit(b, vec3(xp0 + 1.35f, iy1 - 0.23f, 0.f), kPi, 1.4f, r.next(), living, true);
        coffeeTable(b, vec3(xp0 + 1.35f, iy0 + 1.75f, 0.f), 0.f, 1.0f, 0.55f, r.next());
        vec3 rc(xp0 + 3.0f, iy0 + 2.45f, 0.f);
        armchair(b, rc, atan2f(-(xp0 + 1.35f - rc.x), iy1 - rc.y), C(0.3f, 0.22f, 0.16f), r.next());
        {
            InPart ip(b, IP_DETAIL);
            pizzaBox(b, vec3(xp0 + 1.1f, iy0 + 1.75f, 0.465f), 0.3f, true);
            pizzaBox(b, vec3(xp0 + 3.0f, iy0 + 3.4f, 0.f), -0.5f, false);
            pizzaBox(b, vec3(xp0 + 3.02f, iy0 + 3.38f, 0.045f), -0.4f, false);
            clutterCans(b, vec3(xp0 + 1.6f, iy0 + 1.8f, 0.465f), 5, r.next());
            clutterCans(b, vec3(xp0 + 2.3f, iy0 + 1.2f, 0.f), 4, r.next());
            clothesPile(b, vec3(xp0 + 0.9f, iy0 + 0.5f, 0.52f), r.next());
            // game controller on the sofa
            rbox(b, vec3(xp0 + 1.7f, iy0 + 0.55f, 0.55f), vec3(0.075f, 0.045f, 0.02f), 0.015f, Gy(0.08f), M(MAT_PLASTIC));
        }
        {
            At at(b, vec3(xp0 + pt * 0.5f, iy0 + 2.4f, 0.f), -kHalfPi);
            picture(b, vec3(0.f, 0.f, 1.35f), 0.7f, 0.95f, r.next(), Gy(0.1f));
        }
        // dinette by the front window
        float tx = doorX + 1.85f;
        diningTable(b, vec3(tx, iy0 + 0.72f, 0.f), 0.f, 0.9f, 0.7f, C(0.75f, 0.72f, 0.62f), false, r.next());
        chair(b, vec3(tx - 0.65f, iy0 + 0.72f, 0.f), -kHalfPi, C(0.6f, 0.15f, 0.12f), 1, true);
        chair(b, vec3(tx + 0.65f, iy0 + 0.72f, 0.f), kHalfPi, C(0.6f, 0.15f, 0.12f), 1, true);
        // kitchen along the back wall, fridge at the bath end
        kitchenRun(b, vec3(runC, iy1, 0.f), kPi, 2.4f, C(0.62f, 0.5f, 0.36f), C(0.78f, 0.74f, 0.64f), true, true, true, r.next(), living);
        fridge(b, vec3(xp1 - 0.5f, iy1, 0.f), kPi, C(0.9f, 0.88f, 0.8f), r.next());
    }
    domeLight(b, vec3(xp0 + 1.6f, (iy0 + iy1) * 0.5f, zc), living, 100.f);
    domeLight(b, vec3(doorX + 1.9f, (iy0 + iy1) * 0.5f, zc), living, 100.f);
    // ---- bedroom: bed with the headboard on the end wall, wardrobe on the partition, Dex's computer desk
    {
        At lift(b, vec3(0.f, 0.f, zf), 0.f);
        float bedY = iy1 - 0.77f;
        bed(b, vec3(ix0 + 1.1f, bedY, 0.f), -kHalfPi, 1.4f, 2.0f, C(0.3f, 0.2f, 0.12f), C(0.25f, 0.3f, 0.4f), r.next(), true);
        nightstand(b, vec3(ix0 + 0.25f, bedY - 0.99f, 0.f), -kHalfPi, C(0.3f, 0.2f, 0.12f), bedroom, r.next());
        wardrobe(b, vec3(xp0 - pt * 0.5f, iy1 - 0.62f, 0.f), kHalfPi, 1.0f, C(0.45f, 0.32f, 0.2f), r.next());
        computerDesk(b, vec3(ix0 + 1.0f, iy0, 0.f), 0.f, r.next(), bedroom);
        officeChair(b, vec3(ix0 + 1.0f, iy0 + 1.15f, 0.f), kPi, C(0.1f, 0.1f, 0.12f));
        clothesPile(b, vec3(ix0 + 2.5f, iy0 + 2.0f, 0.f), r.next());
        {
            At at(b, vec3(xp0 - pt * 0.5f, iy0 + 1.5f, 0.f), kHalfPi);
            picture(b, vec3(0.f, 0.f, 1.45f), 0.6f, 0.85f, r.next(), Gy(0.08f));
        }
        marker(b, IM_BED, vec3(ix0 + 1.25f, bedY - 1.3f, 0.f), 0.f);
        marker(b, IM_WARDROBE, vec3(xp0 - pt * 0.5f - 1.1f, iy1 - 0.62f, 0.f), -kHalfPi);
    }
    domeLight(b, vec3(ix0 + 1.8f, (iy0 + iy1) * 0.5f, zc), bedroom, 80.f);
    // ---- bathroom
    {
        At lift(b, vec3(0.f, 0.f, zf), 0.f);
        bathtub(b, vec3(ix1, iy1 - 0.9f, 0.f), kHalfPi, 1.65f, C(0.8f, 0.85f, 0.7f));
        toilet(b, vec3(xp1 + 1.5f, iy0 + 0.3f, 0.f), 0.f);
        bathSink(b, vec3(xp1 + 0.75f, iy1, 0.f), kPi, C(0.62f, 0.5f, 0.36f), bath);
        towelRail(b, vec3(xp1 + pt * 0.5f, iy1 - 1.2f, 0.f), -kHalfPi, C(0.7f, 0.3f, 0.25f));
    }
    domeLight(b, vec3((xp1 + ix1) * 0.5f, (iy0 + iy1) * 0.5f, zc), bath, 90.f, vec3(1.f, 0.95f, 0.88f));
    roomDressing(b, living, false, d.seed ^ 0x71u);
    roomDressing(b, bedroom, false, d.seed ^ 0x72u);
}

}  // namespace ikit
}  // namespace World
