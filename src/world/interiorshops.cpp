// Shops: gun stores (Palmetto Arms, Northside Arms: barred windows, glass display counters with handguns, long-gun
// racks, ammo wall, accessories, a range door) and the Threads clothing boutique (window mannequins, wall rails,
// round racks, folded tables, shoe wall, fitting rooms, cash desk). Counters carry the shop-menu marker.
#include "interiors.h"
#include "sites.h"

namespace World {
namespace ikit {

// ------------------------------------------------------------------------------------------------ gun shop pieces
// Handgun lying on its side on a surface at p (local x = muzzle direction)
void pistolFlat(IB& b, vec3 p, float yaw, u32 col, u32 grip) {
    At at(b, p, yaw);
    u32 m = M(MAT_METAL_PAINTED);
    box(b, vec3(0.02f, 0.035f, 0.013f), vec3(0.095f, 0.016f, 0.013f), col, m, SK_NZ);            // slide
    box(b, vec3(0.02f, 0.012f, 0.011f), vec3(0.08f, 0.009f, 0.011f), C(rgbOf(col) * 0.8f), m, SK_NZ);   // frame
    b.pushAxes(vec3(-0.05f, 0.f, 0.f), normalize(vec3(0.97f, -0.25f, 0.f)), normalize(vec3(0.25f, 0.97f, 0.f)), vec3(0, 0, 1));
    box(b, vec3(0.f, -0.045f, 0.012f), vec3(0.02f, 0.05f, 0.012f), grip, M(MAT_RUBBER), SK_NZ);   // grip
    b.pop();
    tube(b, vec3(-0.005f, 0.004f, 0.012f), vec3(0.03f, -0.02f, 0.012f), 0.004f, 4, C(rgbOf(col) * 0.7f), m);   // trigger guard
    tube(b, vec3(0.03f, -0.02f, 0.012f), vec3(0.045f, 0.003f, 0.012f), 0.004f, 4, C(rgbOf(col) * 0.7f), m);
}

// Long gun standing on its butt (local z up, muzzle up). kind 0 black rifle, 1 pump shotgun, 2 scoped hunting rifle
void longGun(IB& b, vec3 p, float yaw, int kind, float tilt) {
    At at(b, p, yaw);
    b.pushAxes(vec3(0.f), vec3(1, 0, 0), vec3(0, cosf(tilt), -sinf(tilt)), vec3(0, sinf(tilt), cosf(tilt)));
    u32 blk = Gy(0.06f), wood = C(0.42f, 0.24f, 0.12f), m = M(MAT_METAL_PAINTED);
    u32 furn = kind == 0 ? blk : wood;
    u32 fm = kind == 0 ? M(MAT_PLASTIC) : M(MAT_WOOD);
    // stock (tapering toward the grip)
    box(b, vec3(0.f, -0.02f, 0.14f), vec3(0.02f, 0.06f, 0.14f), furn, fm, SK_NONE);
    box(b, vec3(0.f, 0.f, 0.32f), vec3(0.018f, 0.035f, 0.06f), furn, fm, SK_NZ);
    // receiver
    box(b, vec3(0.f, 0.005f, 0.47f), vec3(0.018f, 0.032f, 0.1f), blk, m, SK_NZ);
    if (kind == 0) {
        box(b, vec3(0.f, 0.06f, 0.46f), vec3(0.014f, 0.035f, 0.02f), blk, M(MAT_PLASTIC), SK_NONE);                        // magazine
        box(b, vec3(0.f, 0.005f, 0.66f), vec3(0.02f, 0.028f, 0.1f), blk, M(MAT_PLASTIC), SK_NZ);                             // handguard
        box(b, vec3(0.f, -0.035f, 0.5f), vec3(0.008f, 0.008f, 0.14f), blk, m, SK_NONE);                                   // rail
        tube(b, vec3(0.f, 0.005f, 0.76f), vec3(0.f, 0.005f, 0.98f), 0.008f, 6, blk, m, true);
    } else if (kind == 1) {
        tube(b, vec3(0.f, 0.f, 0.56f), vec3(0.f, 0.f, 1.02f), 0.011f, 6, blk, m, true);                                    // barrel
        tube(b, vec3(0.f, 0.022f, 0.56f), vec3(0.f, 0.022f, 0.95f), 0.009f, 6, blk, m, true);                              // magazine tube
        box(b, vec3(0.f, 0.02f, 0.66f), vec3(0.022f, 0.022f, 0.07f), wood, M(MAT_WOOD), SK_NONE);                         // pump
    } else {
        box(b, vec3(0.f, 0.01f, 0.64f), vec3(0.02f, 0.03f, 0.12f), wood, M(MAT_WOOD), SK_NZ);
        tube(b, vec3(0.f, 0.f, 0.56f), vec3(0.f, 0.f, 1.06f), 0.008f, 6, blk, m, true);
        tube(b, vec3(0.f, -0.055f, 0.4f), vec3(0.f, -0.055f, 0.66f), 0.014f, 8, blk, m, true);                             // scope
        cyl(b, vec3(0.f, -0.055f, 0.36f), 0.02f, 0.016f, 0.05f, 8, blk, m, true, true);
        cyl(b, vec3(0.f, -0.055f, 0.64f), 0.016f, 0.022f, 0.06f, 8, blk, m, true, false);
    }
    // trigger guard + pistol grip
    tube(b, vec3(0.f, 0.035f, 0.4f), vec3(0.f, 0.06f, 0.44f), 0.004f, 4, blk, m);
    b.pop();
}

// Glass display counter (customer side +y): cabinet, felt deck with handguns, glass front and top
void gunCase(IB& b, vec3 p, float yaw, float len, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    const float hl = len * 0.5f, dp = 0.6f;
    u32 wood = C(0.2f, 0.13f, 0.08f);
    box(b, vec3(0, 0, 0.3f), vec3(hl, dp * 0.5f, 0.3f), wood, M(MAT_WOOD), SK_NZ);
    box(b, vec3(0, dp * 0.5f + 0.002f, 0.08f), vec3(hl, 0.003f, 0.07f), Gy(0.05f), M(MAT_RUBBER), SK_NZ);
    box(b, vec3(0, 0, 0.61f), vec3(hl - 0.02f, dp * 0.5f - 0.02f, 0.01f), C(0.3f, 0.05f, 0.06f), M(MAT_FABRIC), SK_NZ);   // felt
    // aluminium frame edges
    u32 al = Gy(0.8f), am = M(MAT_METAL_BRUSHED);
    for (int e = -1; e <= 1; e += 2) {
        box(b, vec3(e * (hl - 0.01f), 0, 0.8f), vec3(0.01f, dp * 0.5f, 0.2f), al, am, SK_NONE);
        box(b, vec3(0, e * (dp * 0.5f - 0.01f), 0.995f), vec3(hl, 0.01f, 0.01f), al, am, SK_NONE);
        box(b, vec3(0, dp * 0.5f - 0.01f, 0.61f + (e > 0 ? 0.f : 0.f)), vec3(hl, 0.01f, 0.012f), al, am, SK_NONE);
    }
    // glass: front, top and back (sliding doors) panes
    u32 g = glassCol(0.93f, vec3(0.9f, 0.95f, 0.95f));
    quadF(b, vec3(-hl + 0.02f, dp * 0.5f - 0.01f, 0.62f), vec3(hl - 0.02f, dp * 0.5f - 0.01f, 0.62f), vec3(hl - 0.02f, dp * 0.5f - 0.01f, 0.985f),
          vec3(-hl + 0.02f, dp * 0.5f - 0.01f, 0.985f), vec3(0, 1, 0), g, kGlassMat);
    quadF(b, vec3(-hl + 0.02f, -dp * 0.5f + 0.02f, 1.f), vec3(hl - 0.02f, -dp * 0.5f + 0.02f, 1.f), vec3(hl - 0.02f, dp * 0.5f - 0.02f, 1.f),
          vec3(-hl + 0.02f, dp * 0.5f - 0.02f, 1.f), vec3(0, 0, 1), g, kGlassMat);
    quadF(b, vec3(-hl + 0.02f, -dp * 0.5f + 0.01f, 0.62f), vec3(hl - 0.02f, -dp * 0.5f + 0.01f, 0.62f), vec3(hl - 0.02f, -dp * 0.5f + 0.01f, 0.985f),
          vec3(-hl + 0.02f, -dp * 0.5f + 0.01f, 0.985f), vec3(0, -1, 0), g, kGlassMat);
    collide(b, vec3(0, 0, 0.5f), vec3(hl, dp * 0.5f, 0.5f));
    InPart ip(b, IP_DETAIL);
    int n = (int)(len / 0.28f);
    for (int k = 0; k < n; k++) {
        float x = -hl + 0.16f + k * (len - 0.32f) / Max(1, n - 1);
        vec3 col = r.chance(0.7f) ? vec3(0.06f) : (r.chance(0.5f) ? vec3(0.55f) : vec3(0.35f, 0.3f, 0.22f));
        pistolFlat(b, vec3(x, r.range(-0.08f, 0.05f), 0.62f), r.range(-0.4f, 0.4f) + (k & 1 ? kPi : 0.f), C(col), Gy(0.05f));
        box(b, vec3(x, 0.2f, 0.621f), vec3(0.03f, 0.018f, 0.001f), Gy(0.95f), M(MAT_PAINT_WHITE), SK_NZ);   // price tag
    }
    // display light strip under the top
    box(b, vec3(0, 0.f, 0.98f), vec3(hl - 0.05f, 0.012f, 0.004f), C(1.f, 0.96f, 0.9f, 0.8f), EM(), SK_PZ);
}

// Wall rack of long guns (against a wall, front +y): slatwall panel, standing guns in a slotted rail, upper row on pegs
void longGunWall(IB& b, vec3 p, float yaw, float len, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    box(b, vec3(0, 0.015f, 1.35f), vec3(len * 0.5f, 0.015f, 1.25f), C(0.55f, 0.52f, 0.46f), M(MAT_WOOD), SK_NY);
    for (int k = 0; k < 16; k++) box(b, vec3(0, 0.031f, 0.2f + k * 0.15f), vec3(len * 0.5f, 0.002f, 0.004f), Gy(0.3f), M(MAT_METAL_PAINTED), SK_NZ);
    // lower rail (butt stops) and the upper bar the muzzles lean on
    box(b, vec3(0, 0.2f, 0.06f), vec3(len * 0.5f, 0.16f, 0.06f), C(0.25f, 0.16f, 0.1f), M(MAT_WOOD), SK_NZ);
    box(b, vec3(0, 0.08f, 1.12f), vec3(len * 0.5f, 0.05f, 0.03f), C(0.25f, 0.16f, 0.1f), M(MAT_WOOD), SK_NONE);
    int n = (int)(len / 0.16f);
    InPart ip(b, IP_DETAIL);
    for (int k = 0; k < n; k++) {
        float x = -len * 0.5f + 0.1f + k * (len - 0.2f) / Max(1, n - 1);
        longGun(b, vec3(x, 0.24f, 0.1f), kHalfPi, r.irange(0, 2), 0.13f);
    }
    // upper row: rifles lying horizontally on pegs
    for (int row = 0; row < 2; row++) {
        float z = 1.55f + row * 0.38f;
        for (int k = 0; k < (int)(len / 1.25f); k++) {
            float x = -len * 0.5f + 0.65f + k * 1.25f;
            for (int e = -1; e <= 1; e += 2) tube(b, vec3(x + e * 0.35f, 0.03f, z - 0.02f), vec3(x + e * 0.35f, 0.12f, z - 0.02f), 0.008f, 5, Gy(0.6f), M(MAT_CHROME), true);
            b.pushAxes(vec3(x - 0.55f, 0.08f, z + 0.01f), vec3(0, 0, 1), vec3(0, 1, 0), vec3(1, 0, 0));
            longGun(b, vec3(0.f), 0.f, r.irange(0, 2), 0.f);
            b.pop();
        }
    }
    InPart ip2(b, IP_FURNITURE);
    collide(b, vec3(0, 0.2f, 1.2f), vec3(len * 0.5f, 0.2f, 1.2f));
}

// Ammunition shelving (against a wall, front +y): small boxes in colored rows with labels
void ammoWall(IB& b, vec3 p, float yaw, float len, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    const float dp = 0.35f;
    box(b, vec3(0, 0.01f, 1.1f), vec3(len * 0.5f, 0.01f, 1.1f), Gy(0.3f), M(MAT_METAL_PAINTED), SK_NY);
    for (int e = -1; e <= 1; e += 2) box(b, vec3(e * (len * 0.5f - 0.015f), dp * 0.5f, 1.1f), vec3(0.015f, dp * 0.5f, 1.1f), Gy(0.3f), M(MAT_METAL_PAINTED), SK_NZ);
    for (int lv = 0; lv < 6; lv++) {
        float z = 0.1f + lv * 0.36f;
        box(b, vec3(0, dp * 0.5f, z), vec3(len * 0.5f - 0.03f, dp * 0.5f, 0.012f), Gy(0.35f), M(MAT_METAL_PAINTED), SK_NONE);
        box(b, vec3(0, dp + 0.004f, z - 0.01f), vec3(len * 0.5f - 0.03f, 0.004f, 0.025f), C(0.95f, 0.85f, 0.2f), M(MAT_PAINT_WHITE), SK_NY);
        if (lv == 5) break;
        InPart ip(b, IP_DETAIL);
        float x = -len * 0.5f + 0.05f;
        while (x < len * 0.5f - 0.15f) {
            vec3 c = r.chance(0.35f) ? vec3(0.1f, 0.3f, 0.12f) : (r.chance(0.5f) ? vec3(0.6f, 0.1f, 0.08f) : hsv(r.f(), 0.6f, 0.6f));
            float w = r.range(0.09f, 0.14f), h = r.range(0.06f, 0.1f);
            int stack = r.irange(1, 3);
            for (int s = 0; s < stack; s++) productBox(b, vec3(x + w * 0.5f, dp - 0.08f, z + 0.012f + s * h), vec3(w * 0.5f - 0.004f, 0.06f, h * 0.5f - 0.002f), C(c), Gy(0.92f));
            x += w + 0.008f;
        }
    }
    collide(b, vec3(0, dp * 0.5f, 1.1f), vec3(len * 0.5f, dp * 0.5f, 1.1f));
}

// Paper shooting target (silhouette) on a wall, facing +y
void paperTarget(IB& b, vec3 c, u32 seed) {
    Rng r(seed);
    box(b, c, vec3(0.3f, 0.002f, 0.45f), Gy(0.95f), M(MAT_PAINT_WHITE), SK_NY);
    u32 ink = r.chance(0.5f) ? Gy(0.1f) : C(0.1f, 0.2f, 0.5f);
    box(b, c + vec3(0, 0.003f, -0.12f), vec3(0.2f, 0.001f, 0.28f), ink, M(MAT_PAINT_WHITE), SK_NY);
    b.pushAxes(c + vec3(0, 0.004f, 0.25f), vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0));
    disc(b, vec3(0.f), 0.1f, 14, ink, M(MAT_PAINT_WHITE));
    b.pop();
    for (int k = 0; k < 3; k++) {
        b.pushAxes(c + vec3(0, 0.005f, 0.f), vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0));
        disc(b, vec3(0, -0.05f, 0.f), 0.16f - k * 0.05f, 16, k & 1 ? ink : Gy(0.95f), M(MAT_PAINT_WHITE));
        b.pop();
    }
}

// Gun safe for sale (front +y)
void gunSafe(IB& b, vec3 p, float yaw, u32 col) {
    At at(b, p, yaw);
    rbox(b, vec3(0, 0.32f, 0.8f), vec3(0.38f, 0.32f, 0.8f), 0.03f, col, M(MAT_METAL_PAINTED), true);
    box(b, vec3(0, 0.645f, 0.82f), vec3(0.33f, 0.004f, 0.72f), C(rgbOf(col) * 1.2f), M(MAT_METAL_PAINTED), SK_NZ);
    b.pushAxes(vec3(0.12f, 0.65f, 1.0f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0));
    cyl(b, vec3(0, 0, -0.04f), 0.05f, 0.05f, 0.04f, 14, Gy(0.8f), M(MAT_CHROME), false, true);
    b.pop();
    for (int k = 0; k < 3; k++) {
        float a = kTwoPi * k / 3.f;
        tube(b, vec3(-0.05f, 0.66f, 0.8f), vec3(-0.05f + cosf(a) * 0.1f, 0.7f, 0.8f + sinf(a) * 0.1f), 0.01f, 5, Gy(0.8f), M(MAT_CHROME), true);
    }
    collide(b, vec3(0, 0.32f, 0.8f), vec3(0.38f, 0.32f, 0.8f));
}

// Security bars on the inside of a window (vertical bars + two rails), x0..x1 at plane y, z0..z1
void windowBars(IB& b, float x0, float x1, float z0, float z1, float y) {
    InPart ip(b, IP_SHELL);
    u32 c = Gy(0.12f), m = M(MAT_METAL_PAINTED);
    int n = Max(2, (int)((x1 - x0) / 0.13f));
    for (int k = 0; k <= n; k++) {
        float x = x0 + (x1 - x0) * k / n;
        box(b, vec3(x, y, (z0 + z1) * 0.5f), vec3(0.009f, 0.009f, (z1 - z0) * 0.5f), c, m, SK_NONE);
    }
    for (float z : {z0 + 0.1f, (z0 + z1) * 0.5f, z1 - 0.1f}) box(b, vec3((x0 + x1) * 0.5f, y + 0.012f, z), vec3((x1 - x0) * 0.5f, 0.008f, 0.02f), c, m, SK_NONE);
}

// ------------------------------------------------------------------------------------------------ gun shop
void layoutGunShop(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0x6A45u);
    const float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT, H = d.ceil;
    float dx0 = 0.f, dx1 = 0.f, dh = 2.2f;
    mainDoorOpening(d, dx0, dx1, dh);
    const float doorX = (dx0 + dx1) * 0.5f;
    const float pt = 0.15f;
    const bool backRoom = Y1 - Y0 > 10.5f;
    const float yR = backRoom ? Y1 - 3.0f : Y1 + pt * 0.5f;   // partition center
    int shop = room(b, vec3(X0, Y0, 0.f), vec3(X1, yR - pt * 0.5f, H), vec3(34.f, 34.f, 32.f), 0.06f, LS_BUSINESS);
    int back = backRoom ? room(b, vec3(X0, yR + pt * 0.5f, 0.f), vec3(X1, Y1, H), vec3(12.f, 12.f, 11.f), 0.f, LS_ALWAYS) : -1;
    storefrontEntrance(b, DK_HINGED_PAIR, 1, Gy(0.2f));
    const float cs = doorX > (X0 + X1) * 0.5f ? -1.f : 1.f;   // the staff gate at the end away from the door
    const float gateX = cs > 0.f ? X1 - 0.55f : X0 + 0.55f;
    const float staffX = cs > 0.f ? X1 - 0.8f : X0 + 0.8f;
    if (backRoom) {
        door(b, vec3(staffX, yR, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.1f, DK_HINGED, 2, C(0.3f, 0.32f, 0.34f), false);
        partitionX(b, X0, X1, yR, pt, H, {vec2(staffX - 0.45f, staffX + 0.45f)});
    }
    // ---- shell: dark slatwall-green walls, stained concrete, drop ceiling
    ShellStyle st;
    st.wallCol = C(0.36f, 0.4f, 0.33f);
    st.floorMat = M(MAT_CONCRETE);
    st.floorCol = C(0.5f, 0.47f, 0.43f);
    st.ceilMat = M(MAT_CEILING_TILE);
    st.ceilCol = Gy(0.9f);
    st.wainscotH = 1.0f;
    st.wainCol = C(0.3f, 0.22f, 0.15f);
    st.wainMat = M(MAT_WOOD);
    st.baseH = 0.12f;
    st.baseCol = C(0.2f, 0.14f, 0.1f);
    st.baseMat = M(MAT_WOOD);
    st.revealCol = Gy(0.75f);
    shell(b, shop, st);
    if (backRoom) {
        ShellStyle bs;
        bs.wallCol = Gy(0.7f);
        bs.floorMat = M(MAT_CONCRETE);
        bs.floorCol = Gy(0.6f);
        bs.ceilMat = M(MAT_CONCRETE);
        bs.ceilCol = Gy(0.65f);
        bs.baseH = 0.f;
        shell(b, back, bs);
        doorFrameInner(b, vec3(staffX, yR, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.1f, pt, Gy(0.4f));
    }
    // bars behind every storefront window
    for (const InteriorOpening& op : d.openings) {
        if (op.kind != OP_GLASS) continue;
        vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
        if (fabsf(a.y) > 0.1f) continue;
        float xa = Min(a.x, c.x), xc = Max(a.x, c.x);
        if (xa < Max(dx0, dx1) + 0.05f && xc > Min(dx0, dx1) - 0.05f) {
            // sidelights beside / transom over the door: bar only the parts outside the doorway
            if (xa < Min(dx0, dx1) - 0.1f) windowBars(b, xa, Min(dx0, dx1) - 0.02f, op.z0 - d.origin.z, op.z1 - d.origin.z, Y0 + 0.08f);
            if (xc > Max(dx0, dx1) + 0.1f) windowBars(b, Max(dx0, dx1) + 0.02f, xc, op.z0 - d.origin.z, op.z1 - d.origin.z, Y0 + 0.08f);
            continue;
        }
        windowBars(b, xa, xc, op.z0 - d.origin.z, op.z1 - d.origin.z, Y0 + 0.08f);
    }
    // ---- counter: glass cases across the store, a flip gate at the staff end
    const float yc = Y0 + Clamp((yR - Y0) * 0.52f, 4.2f, 6.2f);
    {
        float xa = cs > 0.f ? X0 + 0.62f : gateX + 0.45f, xc = cs > 0.f ? gateX - 0.45f : X1 - 0.62f;
        float len = xc - xa;
        int n = Max(1, (int)roundf(len / 1.8f));
        for (int k = 0; k < n; k++) gunCase(b, vec3(xa + len * (k + 0.5f) / n, yc, 0.f), 0.f, len / n - 0.01f, r.next());
        // return case along the side wall on the door side (L-shaped counter)
        float ex = cs > 0.f ? X0 + 0.31f : X1 - 0.31f;
        gunCase(b, vec3(ex, yc - 1.05f, 0.f), cs > 0.f ? -kHalfPi : kHalfPi, 1.5f, r.next());
        // hinged counter flap at the gate (a solid section)
        box(b, vec3(gateX, yc, 0.95f), vec3(0.45f, 0.3f, 0.03f), C(0.25f, 0.16f, 0.1f), M(MAT_WOOD), SK_NONE);
        box(b, vec3(gateX, yc + 0.28f, 0.5f), vec3(0.43f, 0.02f, 0.42f), C(0.25f, 0.16f, 0.1f), M(MAT_WOOD), SK_NONE);
        collide(b, vec3(gateX, yc, 0.5f), vec3(0.45f, 0.3f, 0.5f));
        // register and card terminal
        InPart ip(b, IP_DETAIL);
        float rx = (xa + xc) * 0.5f;
        rbox(b, vec3(rx, yc - 0.1f, 1.06f), vec3(0.2f, 0.15f, 0.05f), 0.02f, Gy(0.12f), M(MAT_PLASTIC), true);
        b.pushAxes(vec3(rx, yc - 0.1f, 1.2f), vec3(-1, 0, 0), normalize(vec3(0, -1.f, 0.35f)), normalize(vec3(0, 0.35f, 1.f)));
        rbox(b, vec3(0.f), vec3(0.15f, 0.01f, 0.1f), 0.006f, Gy(0.1f), M(MAT_PLASTIC), true);
        box(b, vec3(0, 0.011f, 0.f), vec3(0.13f, 0.001f, 0.08f), C(0.3f, 0.6f, 0.4f, 0.6f), EM(), SK_NZ);
        b.pop();
        rbox(b, vec3(rx + 0.4f, yc + 0.15f, 1.03f), vec3(0.04f, 0.07f, 0.02f), 0.01f, Gy(0.1f), M(MAT_PLASTIC), true);
        marker(b, IM_COUNTER, vec3(rx, yc + 0.95f, 0.f), kPi);
    }
    scenario(b, vec3((X0 + X1) * 0.5f + cs * 0.4f, yc - 0.8f, 0.f), 0.f, 0, SR_CLERK, SF_STAFF);
    scenario(b, vec3((X0 + X1) * 0.5f - cs * 2.2f, yc - 0.9f, 0.f), 0.4f * cs, 11, SR_CLERK, SF_STAFF | SF_OPTIONAL);
    // ---- behind the counter: long-gun wall, ammo wall on the staff side, the store name
    {
        float wy = yR - pt * 0.5f;
        float wl = Min(X1 - X0 - 3.2f, 7.5f);
        float wx = (X0 + X1) * 0.5f - cs * 0.6f;
        longGunWall(b, vec3(wx, wy, 0.f), kPi, wl, r.next());
        float ax = cs > 0.f ? X1 : X0;
        float ayc = yc - 0.2f + (wy - yc) * 0.5f;
        ammoWall(b, vec3(ax, Min(ayc, wy - 1.3f), 0.f), cs > 0.f ? kHalfPi : -kHalfPi, Min(2.2f, wy - yc - 0.2f), r.next());
        At at(b, vec3(wx, wy, 0.f), kPi);
        InPart ip(b, IP_SHELL);
        float nz = Min(3.0f, H - 0.4f);
        box(b, vec3(0.f, 0.02f, nz), vec3(Min(2.4f, wl * 0.4f), 0.02f, 0.26f), C(0.12f, 0.1f, 0.08f), M(MAT_WOOD), SK_NY);
        textC(b, d.name.c_str(), vec3(0.f, 0.045f, nz), vec3(-1, 0, 0), vec3(0, 0, 1), 0.22f, 0.035f, C(0.95f, 0.8f, 0.4f), M(MAT_PAINT_WHITE), 0.01f);
        light(b, vec3(0.f, 1.2f, 3.3f), vec3(1.f, 0.9f, 0.75f) * 80.f, 3.f, shop, vec3(0, -0.4f, -1.f), 55.f, 35.f);
    }
    // ---- customer side: accessories on the side wall, safes, targets, clothing rack, signs
    {
        float sx = cs > 0.f ? X0 : X1;
        float yawS = cs > 0.f ? -kHalfPi : kHalfPi;   // faces into the room from the side wall
        float y0 = Y0 + 1.2f, y1 = yc - 2.0f;
        if (y1 - y0 > 1.5f) {
            At at(b, vec3(sx, (y0 + y1) * 0.5f, 0.f), yawS);
            float L = y1 - y0;
            box(b, vec3(0, 0.015f, 1.4f), vec3(L * 0.5f, 0.015f, 0.8f), C(0.5f, 0.47f, 0.42f), M(MAT_WOOD), SK_NY);
            InPart ip(b, IP_DETAIL);
            Rng ar(d.seed + 11u);
            for (int row = 0; row < 3; row++)
                for (float x = -L * 0.5f + 0.2f; x < L * 0.5f - 0.15f; x += 0.28f) {
                    float z = 1.0f + row * 0.4f;
                    tube(b, vec3(x, 0.03f, z + 0.2f), vec3(x, 0.14f, z + 0.22f), 0.004f, 4, Gy(0.7f), M(MAT_CHROME), true);
                    int kind = ar.irange(0, 3);
                    if (kind == 0) rbox(b, vec3(x, 0.07f, z + 0.05f), vec3(0.06f, 0.03f, 0.13f), 0.02f, Gy(0.08f), M(MAT_LEATHER), true);            // holster
                    else if (kind == 1) productBox(b, vec3(x, 0.07f, z - 0.07f), vec3(0.08f, 0.025f, 0.12f), C(hsv(ar.f(), 0.6f, 0.6f)), Gy(0.9f));   // blister
                    else if (kind == 2) cyl(b, vec3(x, 0.08f, z - 0.1f), 0.022f, 0.022f, 0.22f, 8, Gy(0.1f), M(MAT_METAL_PAINTED), true);            // flashlight
                    else rbox(b, vec3(x, 0.06f, z), vec3(0.09f, 0.02f, 0.09f), 0.02f, C(0.35f, 0.3f, 0.2f), M(MAT_FABRIC), true);                     // pouch
                }
            InPart ip2(b, IP_FURNITURE);
            collide(b, vec3(0, 0.1f, 1.4f), vec3(L * 0.5f, 0.1f, 0.8f));
        }
        // gun safes along the front part of the far wall
        float ox = cs > 0.f ? X1 : X0;
        float yawO = -yawS;
        for (int k = 0; k < 2; k++) {
            float y = Y0 + 1.4f + k * 0.95f;
            if (y > yc - 1.2f) break;
            At at(b, vec3(ox, y, 0.f), yawO);
            gunSafe(b, vec3(0.f), 0.f, k == 0 ? C(0.15f, 0.16f, 0.15f) : C(0.35f, 0.08f, 0.06f));
        }
        // paper targets above the safes, a clothing rack of camo
        for (int k = 0; k < 3; k++) {
            At at(b, vec3(ox, Y0 + 1.2f + k * 0.75f, 0.f), yawO);
            paperTarget(b, vec3(0.f, 0.f, 2.3f), r.next());
        }
        {
            vec3 rp((X0 + X1) * 0.5f + cs * -1.5f, Y0 + 2.4f, 0.f);
            if (fabsf(rp.x - doorX) > 1.8f) {
                At at(b, rp, 0.f);
                for (int e = -1; e <= 1; e += 2) tube(b, vec3(e * 0.6f, 0.f, 0.f), vec3(e * 0.6f, 0.f, 1.45f), 0.016f, 6, Gy(0.75f), M(MAT_CHROME));
                tube(b, vec3(-0.62f, 0.f, 1.45f), vec3(0.62f, 0.f, 1.45f), 0.014f, 6, Gy(0.75f), M(MAT_CHROME), true);
                for (int e = -1; e <= 1; e += 2) box(b, vec3(e * 0.6f, 0.f, 0.03f), vec3(0.04f, 0.3f, 0.03f), Gy(0.3f), M(MAT_METAL_PAINTED), SK_NZ);
                Rng cr(d.seed + 5u);
                InPart ip(b, IP_DETAIL);
                for (float x = -0.52f; x < 0.54f; x += 0.08f) {
                    vec3 c = cr.chance(0.5f) ? vec3(0.28f, 0.3f, 0.18f) : (cr.chance(0.5f) ? vec3(0.4f, 0.33f, 0.22f) : vec3(0.9f, 0.4f, 0.1f));
                    rbox(b, vec3(x, 0.f, 1.08f), vec3(0.02f, 0.22f, 0.32f), 0.015f, C(c), M(MAT_CLOTH), true);
                }
                InPart ip2(b, IP_FURNITURE);
                collide(b, vec3(0, 0, 0.75f), vec3(0.62f, 0.25f, 0.75f));
            }
        }
        // hanging sign
        hangingSign(b, vec3((X0 + X1) * 0.5f, Y0 + 2.5f, H - 0.5f), 0.f, "NO LOADED FIREARMS", C(0.7f, 0.08f, 0.06f), Gy(1.f), H);
        scenario(b, vec3(sx + (cs > 0.f ? 1.0f : -1.0f), (y0 + y1) * 0.5f, 0.f), yawS + kPi, 14, SR_SHOPPER, SF_OPTIONAL);
    }
    // ---- lights: troffers over the floor, track spots on the gun wall
    {
        int nx = Max(1, (int)((X1 - X0) / 3.f)), ny = Max(1, (int)((yR - Y0) / 3.2f));
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++)
                troffer(b, vec3(X0 + (X1 - X0) * (i + 0.5f) / nx, Y0 + (yR - Y0) * (j + 0.5f) / ny, H), 1.2f, 0.6f, shop, 620.f, vec3(1.f, 0.97f, 0.9f), 7.f);
        securityDome(b, vec3((X0 + X1) * 0.5f, yc + 0.8f, H), 0.07f, true);
        securityDome(b, vec3(cs > 0.f ? X0 + 0.4f : X1 - 0.4f, Y0 + 0.5f, H), 0.07f, true);
    }
    // ---- back room: the range door (sign), safes, crates of stock
    if (backRoom) {
        float by = (yR + Y1) * 0.5f;
        InPart ip(b, IP_FURNITURE);
        float rx = cs > 0.f ? X0 + 1.6f : X1 - 1.6f;
        {
            At at(b, vec3(rx, Y1, 0.f), kPi);
            b.pushAxes(vec3(-0.5f, 0.03f, 0.f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1));
            doorLeafGeo(b, 1.0f, 2.1f, 0.05f, 2, C(0.25f, 0.27f, 0.28f), true);
            b.pop();
            box(b, vec3(0.f, 0.03f, 2.35f), vec3(0.45f, 0.012f, 0.12f), C(0.8f, 0.1f, 0.08f), M(MAT_PAINT_WHITE), SK_NZ);
            textC(b, "RANGE - EARS ON", vec3(0.f, 0.043f, 2.35f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.08f, 0.012f, Gy(1.f), M(MAT_PAINT_WHITE));
            box(b, vec3(0.75f, 0.04f, 2.3f), vec3(0.06f, 0.03f, 0.04f), C(1.f, 0.1f, 0.05f, 0.9f), EM(7, 3), SK_NONE);
        }
        storageShelf(b, vec3((X0 + X1) * 0.5f + cs * 0.8f, Y1 - 0.28f, 0.f), kPi, Min(3.2f, X1 - X0 - 4.5f), 2.0f, r.next());
        for (int k = 0; k < 4; k++) {
            float x = (cs > 0.f ? X0 + 3.2f : X1 - 3.2f) + (k % 2) * 0.62f * cs, z = (k / 2) * 0.42f;
            box(b, vec3(x, by - 0.3f, z + 0.21f), vec3(0.3f, 0.45f, 0.21f), C(0.3f, 0.35f, 0.2f), M(MAT_METAL_PAINTED), SK_NZ);
        }
        collide(b, vec3((cs > 0.f ? X0 + 3.2f : X1 - 3.2f) + 0.31f * cs, by - 0.3f, 0.42f), vec3(0.62f, 0.45f, 0.42f));
        troffer(b, vec3((X0 + X1) * 0.5f, by, H), 1.2f, 0.3f, back, 380.f, vec3(1.f, 0.96f, 0.88f), 5.5f);
    }
}

// ------------------------------------------------------------------------------------------------ clothing pieces
// Garment on a hanger hanging from a rail at `hook` (local frame: x along the rail = thickness, y = garment width)
void garment(IB& b, vec3 hook, float yaw, int kind, u32 col) {
    At at(b, hook, yaw);
    u32 m = M(MAT_CLOTH);
    // hanger
    tube(b, vec3(0, 0, 0.f), vec3(0, 0, -0.05f), 0.003f, 3, Gy(0.7f), M(MAT_CHROME));
    tube(b, vec3(0, -0.2f, -0.12f), vec3(0, 0, -0.05f), 0.006f, 4, C(0.45f, 0.3f, 0.18f), M(MAT_WOOD));
    tube(b, vec3(0, 0.2f, -0.12f), vec3(0, 0, -0.05f), 0.006f, 4, C(0.45f, 0.3f, 0.18f), M(MAT_WOOD));
    switch (kind) {
        case 0:   // shirt / tee
            rbox(b, vec3(0, 0, -0.42f), vec3(0.012f, 0.22f, 0.3f), 0.01f, col, m, true);
            rbox(b, vec3(0, 0, -0.16f), vec3(0.014f, 0.24f, 0.05f), 0.012f, col, m, true);
            break;
        case 1:   // jacket (thicker, lapels)
            rbox(b, vec3(0, 0, -0.47f), vec3(0.025f, 0.23f, 0.36f), 0.02f, col, m, true);
            rbox(b, vec3(0.026f, 0.f, -0.3f), vec3(0.004f, 0.06f, 0.16f), 0.003f, C(rgbOf(col) * 0.75f), m, true);
            break;
        case 2: {  // dress: flared skirt
            rbox(b, vec3(0, 0, -0.33f), vec3(0.012f, 0.16f, 0.2f), 0.01f, col, m, true);
            vec3 a0(-0.02f, -0.16f, -0.5f), a1(-0.02f, 0.16f, -0.5f), a2(-0.03f, 0.27f, -1.05f), a3(-0.03f, -0.27f, -1.05f);
            quadF(b, a0, a1, a2, a3, vec3(-1, 0, 0), col, m);
            quadF(b, a0 + vec3(0.04f, 0, 0), a1 + vec3(0.04f, 0, 0), a2 + vec3(0.06f, 0, 0), a3 + vec3(0.06f, 0, 0), vec3(1, 0, 0), col, m);
            quadF(b, a1, a1 + vec3(0.04f, 0, 0), a2 + vec3(0.06f, 0, 0), a2, vec3(0, 1, 0.2f), col, m);
            quadF(b, a3, a3 + vec3(0.06f, 0, 0), a0 + vec3(0.04f, 0, 0), a0, vec3(0, -1, 0.2f), col, m);
            break;
        }
        default:  // trousers folded over the bar
            rbox(b, vec3(0, 0, -0.22f), vec3(0.018f, 0.17f, 0.12f), 0.012f, col, m, true);
            break;
    }
}

// Mannequin (stylized, featureless) wearing a top and bottoms, on a base (front +y)
void mannequin(IB& b, vec3 p, float yaw, u32 top, u32 bottom, bool dress) {
    At at(b, p, yaw);
    u32 skin = Gy(0.9f), sm = M(MAT_PLASTIC), cm = M(MAT_CLOTH);
    cyl(b, vec3(0.f), 0.2f, 0.2f, 0.02f, 16, Gy(0.2f), M(MAT_CHROME), true, false);
    tube(b, vec3(0, 0.f, 0.02f), vec3(0, 0.f, 0.5f), 0.012f, 6, Gy(0.7f), M(MAT_CHROME));
    if (dress) {
        lathe(b, vec3(0, 0, 0.35f), {vec2(0.26f, 0.f), vec2(0.2f, 0.4f), vec2(0.14f, 0.62f), vec2(0.15f, 0.8f), vec2(0.17f, 0.95f), vec2(0.14f, 1.05f), vec2(0.06f, 1.12f)}, 14, top, cm, false);
    } else {
        for (int e = -1; e <= 1; e += 2) cyl(b, vec3(e * 0.08f, 0.f, 0.12f), 0.055f, 0.075f, 0.85f, 10, bottom, cm, false);
        lathe(b, vec3(0, 0, 0.92f), {vec2(0.15f, 0.f), vec2(0.16f, 0.12f), vec2(0.14f, 0.3f), vec2(0.17f, 0.45f), vec2(0.15f, 0.55f), vec2(0.06f, 0.6f)}, 14, top, cm, false);
        lathe(b, vec3(0, 0, 0.84f), {vec2(0.16f, 0.f), vec2(0.155f, 0.1f)}, 14, bottom, cm, false);
    }
    cyl(b, vec3(0, 0, 1.52f), 0.045f, 0.04f, 0.1f, 10, skin, sm, false);
    sphere(b, vec3(0, 0.01f, 1.72f), 0.1f, 12, skin, sm, 1.15f);
    for (int e = -1; e <= 1; e += 2) {
        tube(b, vec3(e * 0.18f, 0.f, 1.42f), vec3(e * 0.22f, 0.03f, 1.12f), 0.04f, 8, dress ? skin : top, dress ? sm : cm);
        tube(b, vec3(e * 0.22f, 0.03f, 1.12f), vec3(e * 0.2f, 0.08f, 0.88f), 0.032f, 8, skin, sm, true);
    }
    collide(b, vec3(0, 0, 0.9f), vec3(0.22f, 0.2f, 0.9f));
}

// Round garment rack (radius r) with garments hung radially
void roundRack(IB& b, vec3 p, float rad, u32 seed) {
    At at(b, p, 0.f);
    Rng r(seed);
    cyl(b, vec3(0.f), 0.28f, 0.28f, 0.03f, 16, Gy(0.2f), M(MAT_CHROME), true, false);
    tube(b, vec3(0, 0, 0.03f), vec3(0, 0, 1.35f), 0.022f, 8, Gy(0.8f), M(MAT_CHROME));
    for (int k = 0; k < 4; k++) {
        float a = kTwoPi * k / 4.f + 0.4f;
        tube(b, vec3(0, 0, 1.33f), vec3(cosf(a) * rad, sinf(a) * rad, 1.33f), 0.012f, 6, Gy(0.8f), M(MAT_CHROME));
    }
    for (int k = 0; k < 16; k++) {
        float a0 = kTwoPi * k / 16.f, a1 = kTwoPi * (k + 1) / 16.f;
        tube(b, vec3(cosf(a0) * rad, sinf(a0) * rad, 1.35f), vec3(cosf(a1) * rad, sinf(a1) * rad, 1.35f), 0.012f, 5, Gy(0.8f), M(MAT_CHROME));
    }
    InPart ip(b, IP_DETAIL);
    int kind = r.irange(0, 2);
    vec3 base = hsv(r.f(), r.range(0.2f, 0.7f), r.range(0.3f, 0.85f));
    int n = (int)(kTwoPi * rad / 0.075f);
    for (int k = 0; k < n; k++) {
        float a = kTwoPi * (k + r.range(-0.2f, 0.2f)) / n;
        vec3 c = base * r.range(0.8f, 1.15f);
        if (r.chance(0.25f)) c = hsv(r.f(), 0.5f, 0.7f);
        // garment width along the radius: local y = radial
        garment(b, vec3(cosf(a) * rad, sinf(a) * rad, 1.35f), a - kHalfPi, kind, C(c));
    }
    InPart ip2(b, IP_FURNITURE);
    collide(b, vec3(0, 0, 0.7f), vec3(rad + 0.15f, rad + 0.15f, 0.7f));
}

// Wall rail with garments (rail along local x, garments sticking out +y) and a shelf with folded stacks above
void wallRail(IB& b, vec3 p, float yaw, float len, float railZ, u32 seed, bool shelf) {
    At at(b, p, yaw);
    Rng r(seed);
    for (int e = -1; e <= 1; e += 2) tube(b, vec3(e * (len * 0.5f - 0.05f), 0.f, railZ), vec3(e * (len * 0.5f - 0.05f), 0.3f, railZ), 0.012f, 6, Gy(0.2f), M(MAT_METAL_PAINTED), true);
    tube(b, vec3(-len * 0.5f, 0.28f, railZ), vec3(len * 0.5f, 0.28f, railZ), 0.013f, 6, Gy(0.2f), M(MAT_METAL_PAINTED), true);
    InPart ip(b, IP_DETAIL);
    int kind = r.irange(0, 3);
    vec3 base = hsv(r.f(), r.range(0.2f, 0.7f), r.range(0.25f, 0.85f));
    for (float x = -len * 0.5f + 0.08f; x < len * 0.5f - 0.06f; x += r.range(0.05f, 0.08f)) {
        if (r.chance(0.08f)) {
            x += 0.1f;
            continue;
        }
        vec3 c = base * r.range(0.8f, 1.15f);
        if (r.chance(0.3f)) c = hsv(r.f(), 0.45f, 0.75f);
        garment(b, vec3(x, 0.28f, railZ), 0.f, kind, C(c));
    }
    if (shelf) {
        InPart ip2(b, IP_FURNITURE);
        box(b, vec3(0, 0.18f, railZ + 0.42f), vec3(len * 0.5f, 0.18f, 0.015f), Gy(0.95f), M(MAT_WOOD), SK_NONE);
        InPart ip3(b, IP_DETAIL);
        for (float x = -len * 0.5f + 0.2f; x < len * 0.5f - 0.2f; x += 0.36f) {
            vec3 c = hsv(r.f(), r.range(0.2f, 0.6f), r.range(0.3f, 0.9f));
            int n = r.irange(3, 6);
            for (int k = 0; k < n; k++) box(b, vec3(x, 0.17f, railZ + 0.435f + k * 0.035f + 0.0175f), vec3(0.15f, 0.12f, 0.017f), C(c * (1.f - 0.04f * k)), M(MAT_CLOTH), SK_NZ);
        }
    }
    InPart ip4(b, IP_FURNITURE);
    collide(b, vec3(0, 0.25f, 0.9f), vec3(len * 0.5f, 0.25f, 0.9f));
}

// Display table with folded stacks and a bust form
void foldTable(IB& b, vec3 p, float yaw, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 wood = C(0.72f, 0.58f, 0.42f);
    box(b, vec3(0, 0, 0.78f), vec3(0.8f, 0.45f, 0.03f), wood, M(MAT_WOOD), SK_NONE);
    for (int e = -1; e <= 1; e += 2) box(b, vec3(e * 0.72f, 0, 0.375f), vec3(0.04f, 0.4f, 0.375f), wood, M(MAT_WOOD), SK_NZ);
    box(b, vec3(0, 0, 0.3f), vec3(0.68f, 0.38f, 0.015f), wood, M(MAT_WOOD), SK_NONE);
    collide(b, vec3(0, 0, 0.4f), vec3(0.8f, 0.45f, 0.4f));
    InPart ip(b, IP_DETAIL);
    for (int sx = 0; sx < 3; sx++)
        for (int sy = 0; sy < 2; sy++) {
            vec3 c = hsv(r.f(), r.range(0.2f, 0.7f), r.range(0.3f, 0.9f));
            int n = r.irange(3, 7);
            float x = -0.5f + sx * 0.5f, y = -0.2f + sy * 0.4f;
            if (sx == 1 && sy == 1) continue;
            for (int k = 0; k < n; k++) box(b, vec3(x + r.range(-0.01f, 0.01f), y, 0.81f + k * 0.03f + 0.015f), vec3(0.16f, 0.13f, 0.015f), C(c * (1.f - 0.03f * k)), M(MAT_CLOTH), SK_NZ);
        }
    // bust form in the middle with a top
    cyl(b, vec3(0.f, 0.2f, 0.81f), 0.08f, 0.08f, 0.02f, 12, Gy(0.2f), M(MAT_METAL_PAINTED), true);
    tube(b, vec3(0.f, 0.2f, 0.83f), vec3(0.f, 0.2f, 1.1f), 0.01f, 6, Gy(0.6f), M(MAT_CHROME));
    lathe(b, vec3(0.f, 0.2f, 1.1f), {vec2(0.13f, 0.f), vec2(0.15f, 0.15f), vec2(0.13f, 0.3f), vec2(0.04f, 0.36f)}, 12, C(hsv(r.f(), 0.5f, 0.7f)), M(MAT_CLOTH), true);
}

// Shoe wall: floating shelves with pairs of shoes (against a wall, front +y)
void shoeWall(IB& b, vec3 p, float yaw, float len, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    for (int lv = 0; lv < 5; lv++) {
        float z = 0.5f + lv * 0.36f;
        box(b, vec3(0, 0.14f, z), vec3(len * 0.5f, 0.14f, 0.015f), Gy(0.95f), M(MAT_WOOD), SK_NONE);
        box(b, vec3(0, 0.26f, z - 0.02f), vec3(len * 0.5f - 0.05f, 0.004f, 0.004f), C(1.f, 0.95f, 0.85f, 0.7f), EM(), SK_NONE);
        InPart ip(b, IP_DETAIL);
        for (float x = -len * 0.5f + 0.2f; x < len * 0.5f - 0.15f; x += 0.34f) {
            vec3 c = r.chance(0.4f) ? vec3(r.range(0.05f, 0.95f)) : hsv(r.f(), r.range(0.3f, 0.8f), r.range(0.3f, 0.9f));
            bool heel = r.chance(0.3f);
            for (int e = 0; e < 2; e++) {
                float sx = x + e * 0.1f - 0.05f;
                rbox(b, vec3(sx, 0.14f, z + 0.045f), vec3(0.04f, 0.12f, 0.03f), 0.025f, C(c), M(MAT_LEATHER), true);
                if (heel) box(b, vec3(sx, 0.05f, z + 0.06f), vec3(0.01f, 0.012f, 0.045f), C(c * 0.6f), M(MAT_LEATHER), SK_NONE);
                else box(b, vec3(sx, 0.14f, z + 0.018f), vec3(0.042f, 0.125f, 0.008f), Gy(0.95f), M(MAT_RUBBER), SK_NONE);
            }
        }
    }
    collide(b, vec3(0, 0.14f, 1.2f), vec3(len * 0.5f, 0.14f, 1.2f));
}

// ------------------------------------------------------------------------------------------------ Threads
void layoutClothes(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0x7E4Du);
    const float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT, H = d.ceil;
    float dx0 = 0.f, dx1 = 0.f, dh = 2.2f;
    mainDoorOpening(d, dx0, dx1, dh);
    const float doorX = (dx0 + dx1) * 0.5f;
    const float pt = 0.12f;
    const float yS = Y1 - 2.6f;                                  // stock room partition center
    const float yF = yS - pt * 0.5f - 1.6f;                       // fitting room front line
    int shop = room(b, vec3(X0, Y0, 0.f), vec3(X1, yS - pt * 0.5f, H), vec3(44.f, 40.f, 34.f), 0.08f, LS_BUSINESS);
    int stock = room(b, vec3(X0, yS + pt * 0.5f, 0.f), vec3(X1, Y1, H), vec3(14.f, 14.f, 13.f), 0.f, LS_ALWAYS);
    storefrontEntrance(b, DK_HINGED_PAIR, 1, Gy(0.85f));
    const float cs = doorX > (X0 + X1) * 0.5f ? -1.f : 1.f;       // fitting rooms / stock door on this side
    const float stockX = cs > 0.f ? X1 - 0.7f : X0 + 0.7f;
    door(b, vec3(stockX, yS, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.1f, DK_HINGED, 0, Gy(0.92f), false);
    partitionX(b, X0, X1, yS, pt, H, {vec2(stockX - 0.45f, stockX + 0.45f)});
    ShellStyle st;
    st.wallCol = Gy(0.95f);
    st.floorMat = M(MAT_WOOD_FLOOR);
    st.floorCol = C(0.8f, 0.68f, 0.52f);
    st.ceilCol = Gy(0.96f);
    st.baseH = 0.08f;
    st.baseCol = Gy(0.2f);
    st.crownH = 0.06f;
    st.revealCol = Gy(0.9f);
    shell(b, shop, st);
    ShellStyle ss;
    ss.wallCol = Gy(0.8f);
    ss.floorMat = M(MAT_CONCRETE);
    ss.floorCol = Gy(0.65f);
    ss.ceilCol = Gy(0.8f);
    ss.baseH = 0.f;
    shell(b, stock, ss);
    doorFrameInner(b, vec3(stockX, yS, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.1f, pt, Gy(0.2f));
    // accent back wall panel with the neon script name
    {
        InPart ip(b, IP_SHELL);
        float ax0 = cs > 0.f ? X0 : stockX + 0.6f, ax1 = cs > 0.f ? stockX - 0.6f : X1;
        box(b, vec3((ax0 + ax1) * 0.5f, yS - pt * 0.5f - 0.01f, H * 0.5f + 0.5f), vec3((ax1 - ax0) * 0.5f, 0.01f, H * 0.5f - 0.5f), C(0.72f, 0.42f, 0.32f), M(MAT_PLASTER), SK_PY);
    }
    // ---- fitting rooms along the back wall on the stock-door side
    {
        const int n = 3;
        const float cw = 1.15f, fx0 = cs > 0.f ? stockX - 0.6f - n * cw : stockX + 0.6f, fy1 = yS - pt * 0.5f;
        InPart ip(b, IP_FURNITURE);
        for (int k = 0; k <= n; k++) {
            float x = fx0 + k * cw;
            box(b, vec3(x, (yF + fy1) * 0.5f, 1.1f), vec3(0.03f, (fy1 - yF) * 0.5f, 1.1f), Gy(0.95f), M(MAT_WOOD), SK_NONE);
            collide(b, vec3(x, (yF + fy1) * 0.5f, 1.1f), vec3(0.03f, (fy1 - yF) * 0.5f, 1.1f));
        }
        box(b, vec3(fx0 + n * cw * 0.5f, yF, 2.2f), vec3(n * cw * 0.5f + 0.03f, 0.05f, 0.03f), Gy(0.2f), M(MAT_METAL_PAINTED), SK_NONE);
        for (int k = 0; k < n; k++) {
            float cx = fx0 + (k + 0.5f) * cw;
            // curtain (half drawn), mirror on the back wall, a bench and hooks
            b.push(vec3(cx, yF + 0.04f, 0.f), 0.f);
            curtains(b, -cw * 0.5f + 0.05f, cw * 0.5f - 0.05f, 0.1f, 2.05f, 0.f, C(0.45f, 0.12f, 0.16f), (u32)(k + 3), k == 1 ? 0.15f : 0.6f);
            b.pop();
            box(b, vec3(cx, fy1 - 0.02f, 1.2f), vec3(0.3f, 0.01f, 0.75f), Gy(0.95f), M(MAT_CHROME), SK_NY);
            box(b, vec3(cx, fy1 - 0.2f, 0.45f), vec3(0.35f, 0.17f, 0.025f), C(0.72f, 0.58f, 0.42f), M(MAT_WOOD), SK_NONE);
            tube(b, vec3(cx - 0.4f, fy1 - 0.02f, 1.8f), vec3(cx - 0.4f, fy1 - 0.1f, 1.82f), 0.008f, 5, Gy(0.7f), M(MAT_CHROME), true);
            downlight(b, vec3(cx, (yF + fy1) * 0.5f, 2.2f), shop, 90.f);
        }
        marker(b, IM_WARDROBE, vec3(fx0 + 1.5f * cw, yF - 0.7f, 0.f), 0.f);
        // standing mirror and a waiting bench outside the fitting rooms
        float mx = cs > 0.f ? fx0 - 0.7f : fx0 + n * cw + 0.7f;
        At at(b, vec3(mx, yF - 0.3f, 0.f), 0.f);
        box(b, vec3(0.f, 0.f, 1.0f), vec3(0.35f, 0.03f, 0.95f), C(0.2f, 0.16f, 0.12f), M(MAT_WOOD), SK_NONE);
        box(b, vec3(0.f, -0.031f, 1.0f), vec3(0.31f, 0.001f, 0.9f), Gy(0.95f), M(MAT_CHROME), SK_NONE);
        collide(b, vec3(0.f, 0.f, 1.0f), vec3(0.35f, 0.05f, 1.0f));
        marker(b, IM_MIRROR, vec3(mx, yF - 1.2f, 0.f), 0.f);
    }
    // ---- cash desk near the door on the fitting-room side
    {
        float cx = doorX + cs * 3.0f, cy = Y0 + 3.2f;
        cx = Clamp(cx, X0 + 1.2f, X1 - 1.2f);
        At at(b, vec3(cx, cy, 0.f), cs > 0.f ? kHalfPi : -kHalfPi);   // customer side (+y local) faces the door side
        rbox(b, vec3(0, 0, 0.5f), vec3(0.9f, 0.32f, 0.5f), 0.03f, Gy(0.96f), M(MAT_PAINT_WHITE), true);
        box(b, vec3(0, 0, 1.015f), vec3(0.95f, 0.36f, 0.015f), C(0.3f, 0.22f, 0.15f), M(MAT_WOOD), SK_NONE);
        collide(b, vec3(0, 0, 0.52f), vec3(0.95f, 0.36f, 0.52f));
        InPart ip(b, IP_DETAIL);
        b.pushAxes(vec3(0.2f, -0.05f, 1.1f), vec3(-1, 0, 0), normalize(vec3(0, -1.f, 0.6f)), normalize(vec3(0, 0.6f, 1.f)));
        rbox(b, vec3(0.f), vec3(0.13f, 0.008f, 0.09f), 0.006f, Gy(0.1f), M(MAT_PLASTIC), true);
        box(b, vec3(0, 0.009f, 0.f), vec3(0.12f, 0.001f, 0.08f), C(0.8f, 0.8f, 0.85f, 0.6f), EM(), SK_NZ);
        b.pop();
        for (int k = 0; k < 4; k++) box(b, vec3(-0.5f, 0.1f, 1.035f + k * 0.012f), vec3(0.2f, 0.12f, 0.006f), C(0.72f, 0.42f, 0.32f), M(MAT_PAINT_WHITE), SK_NZ);
        plant(b, vec3(-0.75f, -0.15f, 1.03f), 0.35f, r.next());
        marker(b, IM_COUNTER, vec3(0.f, 1.0f, 0.f), kPi);
        scenario(b, vec3(0.f, -0.8f, 0.f), 0.f, 0, SR_CLERK, SF_STAFF);
    }
    // ---- window mannequins facing the street
    {
        Rng mr(d.seed + 3u);
        for (const InteriorOpening& op : d.openings) {
            if (op.kind != OP_GLASS) continue;
            vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
            if (fabsf(a.y) > 0.1f || op.z0 - d.origin.z > 1.f) continue;
            float xa = Min(a.x, c.x), xc = Max(a.x, c.x);
            if (xc > Min(dx0, dx1) - 0.4f && xa < Max(dx0, dx1) + 0.4f) continue;
            InPart ip(b, IP_FURNITURE);
            box(b, vec3((xa + xc) * 0.5f, Y0 + 0.55f, 0.1f), vec3((xc - xa) * 0.5f - 0.1f, 0.45f, 0.1f), Gy(0.95f), M(MAT_PAINT_WHITE), SK_NZ);
            collide(b, vec3((xa + xc) * 0.5f, Y0 + 0.55f, 0.1f), vec3((xc - xa) * 0.5f - 0.1f, 0.45f, 0.1f));
            int n = (xc - xa) > 2.4f ? 2 : 1;
            for (int k = 0; k < n; k++) {
                float x = n == 1 ? (xa + xc) * 0.5f : Lerp(xa + 0.6f, xc - 0.6f, (float)k);
                bool dress = mr.chance(0.45f);
                mannequin(b, vec3(x, Y0 + 0.6f, 0.2f), kPi + mr.range(-0.3f, 0.3f), C(hsv(mr.f(), mr.range(0.3f, 0.8f), mr.range(0.4f, 0.9f))),
                          C(hsv(mr.f(), 0.3f, mr.range(0.15f, 0.5f))), dress);
            }
            light(b, vec3((xa + xc) * 0.5f, Y0 + 1.2f, H - 0.2f), vec3(1.f, 0.92f, 0.8f) * 120.f, 3.5f, shop, vec3(0, -0.5f, -1.f), 45.f, 25.f);
        }
    }
    // ---- side wall rails with shelves, shoe wall
    {
        float ya = Y0 + 1.6f, yb2 = yF - 0.4f;
        float L = yb2 - ya;
        for (int sd = -1; sd <= 1; sd += 2) {
            float wx = sd < 0 ? X0 : X1;
            float yawW = sd < 0 ? -kHalfPi : kHalfPi;
            bool shoes = (sd > 0) == (cs < 0.f);
            if (shoes && L > 4.f) {
                {
                    At at(b, vec3(wx, yb2 - 1.1f, 0.f), yawW);
                    shoeWall(b, vec3(0.f), 0.f, 2.0f, r.next());
                }
                At at(b, vec3(wx, ya + (L - 2.4f) * 0.5f, 0.f), yawW);
                wallRail(b, vec3(0.f), 0.f, L - 2.4f, 1.65f, r.next(), true);
            } else if (L > 1.f) {
                At at(b, vec3(wx, (ya + yb2) * 0.5f, 0.f), yawW);
                wallRail(b, vec3(0.f), 0.f, L, 1.65f, r.next(), true);
            }
        }
    }
    // ---- floor fixtures: round racks, fold tables
    {
        float cxm = (X0 + X1) * 0.5f;
        float ya = Y0 + 4.6f, yb2 = yF - 1.4f;
        int rows = Max(1, (int)((yb2 - ya) / 2.6f));
        for (int j = 0; j < rows; j++) {
            float y = ya + (yb2 - ya) * (j + 0.5f) / rows;
            float span = (X1 - X0) - 4.2f;
            for (int i = 0; i < 2; i++) {
                float x = cxm + (i == 0 ? -1.f : 1.f) * span * 0.25f;
                if ((i + j) & 1) roundRack(b, vec3(x, y, 0.f), 0.55f, r.next());
                else foldTable(b, vec3(x, y, 0.f), 0.f, r.next());
            }
        }
        scenario(b, vec3(cxm, ya + 1.2f, 0.f), r.range(-1.f, 1.f), 14, SR_SHOPPER, SF_OPTIONAL);
        scenario(b, vec3(cxm + 1.2f, yb2 - 0.3f, 0.f), kPi * 0.8f, 14, SR_SHOPPER, SF_OPTIONAL);
        scenario(b, vec3(cxm - 1.6f, (ya + yb2) * 0.5f, 0.f), 0.5f, 7, SR_CLERK, SF_STAFF | SF_OPTIONAL);
    }
    // ---- lights: black cone pendants down the middle, downlights along the walls, the neon name
    {
        for (float y = Y0 + 2.5f; y < yF - 0.8f; y += 2.6f) pendant(b, vec3((X0 + X1) * 0.5f, y, H), Min(1.2f, H - 2.4f), shop, Gy(0.08f), 260.f, vec3(1.f, 0.88f, 0.7f), 6.f, 0);
        for (float y = Y0 + 2.2f; y < yF - 0.5f; y += 2.2f) {
            downlight(b, vec3(X0 + 0.9f, y, H), shop, 220.f);
            downlight(b, vec3(X1 - 0.9f, y, H), shop, 220.f);
        }
        float nx = cs > 0.f ? (X0 + (stockX - 3.6f * 1.15f - 0.6f)) * 0.5f : (X1 + (stockX + 3.6f * 1.15f + 0.6f)) * 0.5f;
        At at(b, vec3(nx, yS - pt * 0.5f - 0.02f, 0.f), kPi);
        neonSign(b, vec3(0.f, 0.f, Min(2.6f, H - 0.5f)), "Threads", 0.42f, vec3(1.f, 0.35f, 0.7f), 4, 3, shop, 45.f);
    }
    // ---- stock room: shelving with boxes, a rolling rack
    {
        InPart ip(b, IP_FURNITURE);
        storageShelf(b, vec3((X0 + X1) * 0.5f - cs * 1.0f, Y1 - 0.28f, 0.f), kPi, Min(4.0f, X1 - X0 - 3.f), 2.0f, r.next());
        troffer(b, vec3((X0 + X1) * 0.5f, (yS + Y1) * 0.5f, H), 1.2f, 0.3f, stock, 360.f, vec3(1.f, 0.96f, 0.9f), 5.f);
    }
}

}  // namespace ikit
}  // namespace World
