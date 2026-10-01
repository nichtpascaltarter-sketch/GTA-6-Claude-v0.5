// Civic lobbies: police headquarters (security screening, raised front desk behind glass, badge wall, waiting benches,
// wanted board, elevators, a bullpen of desks behind a glazed partition) and the hospital emergency lobby (sliding
// doors, registration desk, waiting rows facing a TV, vending, triage cubicles with beds and monitors).
#include "interiors.h"
#include "sites.h"

namespace World {
namespace ikit {

// ------------------------------------------------------------------------------------------------ shared pieces
// Beam seating: n molded seats on a steel beam (front +y)
void beamSeats(IB& b, vec3 p, float yaw, int n, u32 seatCol) {
    At at(b, p, yaw);
    const float pitch = 0.58f, L = n * pitch;
    u32 steel = M(MAT_METAL_BRUSHED);
    box(b, vec3(0, -0.05f, 0.36f), vec3(L * 0.5f, 0.04f, 0.03f), Gy(0.6f), steel, SK_NONE);
    for (int e = -1; e <= 1; e += 2) {
        float x = e * (L * 0.5f - 0.3f);
        box(b, vec3(x, -0.05f, 0.18f), vec3(0.03f, 0.03f, 0.18f), Gy(0.6f), steel, SK_NZ);
        box(b, vec3(x, -0.05f, 0.01f), vec3(0.04f, 0.28f, 0.01f), Gy(0.6f), steel, SK_NZ);
    }
    for (int k = 0; k < n; k++) {
        float x = -L * 0.5f + pitch * (k + 0.5f);
        rbox(b, vec3(x, 0.02f, 0.44f), vec3(0.25f, 0.22f, 0.03f), 0.02f, seatCol, M(MAT_PLASTIC), true);
        b.pushAxes(vec3(x, -0.21f, 0.72f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.18f)), normalize(vec3(0, -0.18f, 1)));
        rbox(b, vec3(0.f), vec3(0.24f, 0.02f, 0.22f), 0.02f, seatCol, M(MAT_PLASTIC), true);
        b.pop();
        if (k > 0) box(b, vec3(x - pitch * 0.5f, 0.f, 0.62f), vec3(0.02f, 0.2f, 0.015f), Gy(0.3f), M(MAT_PLASTIC), SK_NONE);   // armrest
    }
    collide(b, vec3(0, 0, 0.4f), vec3(L * 0.5f, 0.28f, 0.4f));
}

// Vending machine (front +y, back against a wall at y = 0), lit panel with products
void vendingMachine(IB& b, vec3 p, float yaw, u32 body, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    rbox(b, vec3(0, 0.4f, 0.95f), vec3(0.45f, 0.4f, 0.95f), 0.02f, body, M(MAT_METAL_PAINTED), true);
    box(b, vec3(-0.1f, 0.801f, 1.1f), vec3(0.3f, 0.001f, 0.7f), C(0.9f, 0.95f, 1.f, 0.55f), EM(), SK_NZ);
    InPart ip(b, IP_DETAIL);
    for (int row = 0; row < 5; row++)
        for (int c = 0; c < 5; c++) {
            vec3 pc = hsv(r.f(), 0.7f, 0.85f);
            box(b, vec3(-0.34f + c * 0.12f, 0.803f, 0.55f + row * 0.25f), vec3(0.04f, 0.001f, 0.07f), C(pc), M(MAT_PAINT_WHITE), SK_NZ);
        }
    box(b, vec3(0.3f, 0.802f, 1.2f), vec3(0.08f, 0.001f, 0.2f), Gy(0.15f), M(MAT_PLASTIC), SK_NZ);
    box(b, vec3(-0.1f, 0.81f, 0.22f), vec3(0.3f, 0.012f, 0.09f), Gy(0.05f), M(MAT_PLASTIC), SK_NZ);
    InPart ip2(b, IP_FURNITURE);
    collide(b, vec3(0, 0.4f, 0.95f), vec3(0.45f, 0.4f, 0.95f));
}

// Water cooler with a bottle (centered)
void waterCooler(IB& b, vec3 p) {
    rbox(b, p + vec3(0, 0, 0.5f), vec3(0.16f, 0.16f, 0.5f), 0.02f, Gy(0.93f), M(MAT_PLASTIC), true);
    lathe(b, p + vec3(0, 0, 1.0f), {vec2(0.13f, 0.f), vec2(0.14f, 0.05f), vec2(0.14f, 0.3f), vec2(0.06f, 0.38f), vec2(0.03f, 0.4f)}, 14, C(0.55f, 0.75f, 0.95f, 1.f),
          kGlassMat, true);
    box(b, p + vec3(0, 0.17f, 0.8f), vec3(0.05f, 0.012f, 0.02f), C(0.1f, 0.3f, 0.9f), M(MAT_PLASTIC), SK_NONE);
    collide(b, p + vec3(0, 0, 0.6f), vec3(0.17f, 0.17f, 0.6f));
}

// Large planter with a tall plant
void planter(IB& b, vec3 p, float size, u32 seed) {
    box(b, p + vec3(0, 0, 0.3f), vec3(0.35f, 0.35f, 0.3f), Gy(0.25f), M(MAT_CONCRETE_PANEL), SK_NZ);
    box(b, p + vec3(0, 0, 0.58f), vec3(0.32f, 0.32f, 0.01f), C(0.2f, 0.14f, 0.1f), M(MAT_DIRT), SK_NZ);
    plant(b, p + vec3(0, 0, 0.5f), size, seed, Gy(0.25f), 1);
    collide(b, p + vec3(0, 0, 0.3f), vec3(0.35f, 0.35f, 0.3f));
}

// Wall-mounted TV (face +y at local origin on the wall), animated picture
void wallTV(IB& b, vec3 c, float w, int roomIdx, u32 seed) {
    float h = w * 0.5625f;
    box(b, c + vec3(0, 0.04f, 0.f), vec3(w * 0.5f, 0.03f, h * 0.5f), Gy(0.03f), M(MAT_PLASTIC), SK_NY);
    box(b, c + vec3(0, 0.071f, 0.f), vec3(w * 0.5f - 0.02f, 0.001f, h * 0.5f - 0.02f), C(0.45f, 0.55f, 0.75f, 0.6f), EM(9, seed & 255u), SK_NZ);
    light(b, c + vec3(0, 0.6f, 0.f), vec3(0.6f, 0.7f, 1.f) * 30.f, 4.f, roomIdx, vec3(0.f), 0.f, 0.f, 4, (u8)(seed & 255u));
}

// Elevator landing: two steel doors in a frame with a call panel and a floor indicator (against a wall, front +y)
void elevatorLanding(IB& b, vec3 p, float yaw, int cars) {
    At at(b, p, yaw);
    InPart ip(b, IP_SHELL);
    for (int k = 0; k < cars; k++) {
        float x = (k - (cars - 1) * 0.5f) * 1.9f;
        box(b, vec3(x, 0.03f, 1.12f), vec3(0.7f, 0.03f, 1.12f), Gy(0.6f), M(MAT_METAL_BRUSHED), SK_NY);
        for (int e = -1; e <= 1; e += 2) box(b, vec3(x + e * 0.27f, 0.065f, 1.05f), vec3(0.265f, 0.006f, 1.05f), Gy(0.82f), M(MAT_METAL_BRUSHED), SK_NY);
        box(b, vec3(x, 0.071f, 1.05f), vec3(0.002f, 0.001f, 1.05f), Gy(0.3f), M(MAT_METAL_PAINTED), SK_NONE);
        box(b, vec3(x, 0.07f, 2.4f), vec3(0.22f, 0.01f, 0.07f), Gy(0.05f), M(MAT_PLASTIC), SK_NY);
        textC(b, k == 0 ? "1" : "G", vec3(x, 0.081f, 2.37f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.08f, 0.012f, C(1.f, 0.4f, 0.1f, 0.9f), EM());
    }
    box(b, vec3((cars - 1) * 0.95f + 0.95f, 0.05f, 1.1f), vec3(0.06f, 0.02f, 0.12f), Gy(0.8f), M(MAT_METAL_BRUSHED), SK_NY);
    for (int e = 0; e < 2; e++) {
        b.pushAxes(vec3((cars - 1) * 0.95f + 0.95f, 0.07f, 1.05f + e * 0.1f), vec3(1, 0, 0), vec3(0, 0, -1), vec3(0, 1, 0));
        cyl(b, vec3(0.f), 0.02f, 0.02f, 0.012f, 10, C(1.f, 0.9f, 0.6f, 0.8f), EM(), true);
        b.pop();
    }
}

// Badge emblem: a seven-point star inside a ring (face +y at c)
void badgeEmblem(IB& b, vec3 c, float rad, u32 gold, u32 field) {
    b.pushAxes(c, vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0));
    cyl(b, vec3(0.f), rad, rad, 0.03f, 28, gold, M(MAT_CHROME), true, false);
    disc(b, vec3(0, 0, 0.031f), rad * 0.86f, 28, field, M(MAT_PAINT_WHITE));
    const int pts = 7;
    for (int k = 0; k < pts; k++) {
        float a0 = kTwoPi * k / pts + kHalfPi, a1 = a0 + kPi / pts, a2 = a0 - kPi / pts;
        vec3 tip(cosf(a0) * rad * 0.78f, sinf(a0) * rad * 0.78f, 0.04f);
        vec3 i1(cosf(a1) * rad * 0.34f, sinf(a1) * rad * 0.34f, 0.04f), i2(cosf(a2) * rad * 0.34f, sinf(a2) * rad * 0.34f, 0.04f);
        tri(b, vec3(0, 0, 0.045f), i2, tip, gold, M(MAT_CHROME));
        tri(b, vec3(0, 0, 0.045f), tip, i1, gold, M(MAT_CHROME));
    }
    b.pop();
}

// Wanted / notice board with pinned sheets (face +y)
void noticeBoard(IB& b, vec3 c, float w, float h, u32 seed, bool wanted) {
    Rng r(seed);
    box(b, c + vec3(0, 0.015f, 0.f), vec3(w * 0.5f + 0.03f, 0.015f, h * 0.5f + 0.03f), C(0.35f, 0.22f, 0.12f), M(MAT_WOOD), SK_NY);
    box(b, c + vec3(0, 0.031f, 0.f), vec3(w * 0.5f, 0.001f, h * 0.5f), C(0.66f, 0.5f, 0.33f), M(MAT_FABRIC), SK_NZ | SK_NY);
    InPart ip(b, IP_DETAIL);
    int cols = Max(2, (int)(w / 0.3f)), rows = Max(1, (int)(h / 0.38f));
    for (int j = 0; j < rows; j++)
        for (int i = 0; i < cols; i++) {
            if (r.chance(0.15f)) continue;
            vec3 pc = c + vec3(-w * 0.5f + w * (i + 0.5f) / cols + r.range(-0.02f, 0.02f), 0.034f, -h * 0.5f + h * (j + 0.5f) / rows + r.range(-0.02f, 0.02f));
            box(b, pc, vec3(0.1f, 0.001f, 0.14f), Gy(r.range(0.85f, 0.97f)), M(MAT_PAINT_WHITE), SK_NZ);
            if (wanted) {
                box(b, pc + vec3(0, 0.002f, 0.03f), vec3(0.06f, 0.001f, 0.07f), Gy(r.range(0.2f, 0.5f)), M(MAT_PAINT_WHITE), SK_NZ);
                textC(b, "WANTED", pc + vec3(0, 0.003f, 0.115f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.018f, 0.004f, Gy(0.05f), M(MAT_PAINT_WHITE));
            } else {
                for (int l = 0; l < 4; l++) box(b, pc + vec3(0, 0.002f, 0.08f - l * 0.045f), vec3(0.07f, 0.001f, 0.006f), Gy(0.3f), M(MAT_PAINT_WHITE), SK_NZ);
            }
            sphere(b, pc + vec3(0, 0.006f, 0.12f), 0.008f, 4, C(hsv(r.f(), 0.8f, 0.9f)), M(MAT_PLASTIC));
        }
}

// Walk-through metal detector arch (centered, passage along local y)
void metalDetector(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    for (int e = -1; e <= 1; e += 2) {
        box(b, vec3(e * 0.5f, 0.f, 1.05f), vec3(0.06f, 0.3f, 1.05f), Gy(0.85f), M(MAT_PLASTIC), SK_NZ);
        collide(b, vec3(e * 0.5f, 0.f, 1.05f), vec3(0.06f, 0.3f, 1.05f));
    }
    box(b, vec3(0.f, 0.f, 2.18f), vec3(0.56f, 0.3f, 0.08f), Gy(0.85f), M(MAT_PLASTIC), SK_NONE);
    box(b, vec3(0.f, -0.301f, 2.18f), vec3(0.2f, 0.001f, 0.03f), C(0.2f, 1.f, 0.3f, 0.9f), EM(7, 20), SK_NONE);
}

// X-ray baggage scanner with rollers (conveyor along local x)
void xrayScanner(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    u32 body = Gy(0.8f);
    box(b, vec3(0.f, 0.f, 0.75f), vec3(0.7f, 0.5f, 0.5f), body, M(MAT_METAL_PAINTED), SK_NZ);
    box(b, vec3(0.f, 0.f, 0.55f), vec3(0.72f, 0.36f, 0.25f), Gy(0.05f), M(MAT_PLASTIC), SK_NONE);
    for (int e = -1; e <= 1; e += 2) {
        box(b, vec3(e * 1.25f, 0.f, 0.72f), vec3(0.55f, 0.36f, 0.03f), Gy(0.25f), M(MAT_RUBBER), SK_NONE);
        for (int k = 0; k < 2; k++) tube(b, vec3(e * (0.8f + k * 0.85f), -0.3f, 0.f), vec3(e * (0.8f + k * 0.85f), -0.3f, 0.7f), 0.02f, 6, Gy(0.7f), M(MAT_METAL_BRUSHED));
        for (int k = 0; k < 2; k++) tube(b, vec3(e * (0.8f + k * 0.85f), 0.3f, 0.f), vec3(e * (0.8f + k * 0.85f), 0.3f, 0.7f), 0.02f, 6, Gy(0.7f), M(MAT_METAL_BRUSHED));
    }
    // operator screen
    b.pushAxes(vec3(0.f, -0.55f, 1.35f), vec3(1, 0, 0), normalize(vec3(0, -1.f, 0.2f)), normalize(vec3(0, 0.2f, 1.f)));
    rbox(b, vec3(0.f), vec3(0.22f, 0.02f, 0.15f), 0.01f, Gy(0.1f), M(MAT_PLASTIC), true);
    b.pop();
    collide(b, vec3(0.f, 0.f, 0.6f), vec3(1.8f, 0.5f, 0.6f));
}

// ------------------------------------------------------------------------------------------------ police HQ
void layoutPolice(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0x9011u);
    const float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT, H = d.ceil;
    float dx0 = 0.f, dx1 = 0.f, dh = 2.2f;
    mainDoorOpening(d, dx0, dx1, dh);
    const float doorX = (dx0 + dx1) * 0.5f;
    const float pt = 0.15f;
    const float yB = Y0 + Clamp((Y1 - Y0) * 0.62f, 8.f, 11.f);    // lobby | bullpen partition center
    const float HB = Min(H, 3.3f);
    const float xc = Clamp((X0 + X1) * 0.5f, X0 + 4.5f, X1 - 4.5f);
    int lobby = room(b, vec3(X0, Y0, 0.f), vec3(X1, yB - pt * 0.5f, H), vec3(42.f, 44.f, 46.f), 0.1f, LS_ALWAYS);
    int pen = room(b, vec3(X0, yB + pt * 0.5f, 0.f), vec3(X1, Y1, HB), vec3(34.f, 35.f, 36.f), 0.f, LS_ALWAYS);
    storefrontEntrance(b, DK_HINGED_PAIR, 1, Gy(0.7f));
    shopSign(b, "POLICE", C(0.05f, 0.1f, 0.25f), Gy(0.95f), d.storefront ? 0.f : dh + 0.4f);
    // secure door to the bullpen, glazed partition bays on both sides of the desk
    const float secX = Min(xc + 4.4f, X1 - 1.2f);
    door(b, vec3(secX, yB, 0.f), vec2(1, 0), vec2(0, 1), 1.0f, 2.2f, DK_HINGED, 2, C(0.2f, 0.25f, 0.35f), false);
    partitionX(b, X0, X1, yB, pt, H, {vec2(secX - 0.5f, secX + 0.5f)});
    std::vector<vec2> wins;
    if (xc - 3.8f - (X0 + 1.2f) > 1.5f) wins.push_back(vec2(X0 + 1.2f, xc - 3.8f));
    if (secX - 0.9f - (xc + 3.8f) > 1.2f) wins.push_back(vec2(xc + 3.8f, secX - 0.9f));
    const float wz0 = 1.0f, wz1 = Min(2.4f, HB - 0.3f);
    {
        const InteriorRoom& rl = d.rooms[lobby];
        const InteriorRoom& rp = d.rooms[pen];
        for (vec2 w : wins) {
            tExtraHoles.push_back({lobby, 2, {w.x - rl.mn.x, w.y - rl.mn.x, wz0, wz1}});
            tExtraHoles.push_back({pen, 0, {rp.mx.x - w.y, rp.mx.x - w.x, wz0, wz1}});
        }
    }
    // ---- shells: terrazzo lobby with a tall ceiling, carpeted bullpen with a drop ceiling
    ShellStyle ls;
    ls.wallCol = C(0.86f, 0.87f, 0.85f);
    ls.floorMat = M(MAT_MARBLE);
    ls.floorCol = C(0.72f, 0.72f, 0.7f);
    ls.floorUV = 0.25f;
    ls.ceilMat = M(MAT_CEILING_TILE);
    ls.ceilCol = Gy(0.92f);
    ls.wainscotH = 1.2f;
    ls.wainCol = C(0.2f, 0.26f, 0.36f);
    ls.wainMat = M(MAT_STONE);
    ls.baseH = 0.12f;
    ls.baseCol = Gy(0.25f);
    ls.baseMat = M(MAT_STONE);
    ls.revealCol = Gy(0.7f);
    shell(b, lobby, ls);
    ShellStyle ps;
    ps.wallCol = C(0.82f, 0.84f, 0.84f);
    ps.floorMat = M(MAT_CARPET);
    ps.floorCol = C(0.28f, 0.3f, 0.34f);
    ps.ceilMat = M(MAT_CEILING_TILE);
    ps.ceilCol = Gy(0.9f);
    ps.baseH = 0.1f;
    ps.baseCol = Gy(0.3f);
    ps.baseMat = M(MAT_RUBBER);
    shell(b, pen, ps);
    doorFrameInner(b, vec3(secX, yB, 0.f), vec2(1, 0), vec2(0, 1), 1.0f, 2.2f, pt, Gy(0.35f));
    for (vec2 w : wins) {
        InPart ip(b, IP_SHELL);
        reveal(b, vec3(w.x, yB - pt * 0.5f, 0.f), vec3(1, 0, 0), vec3(0, 1, 0), 0.f, w.y - w.x, wz0, wz1, 0.f, pt, Gy(0.35f), M(MAT_METAL_PAINTED), true);
        quadF(b, vec3(w.x, yB, wz0), vec3(w.y, yB, wz0), vec3(w.y, yB, wz1), vec3(w.x, yB, wz1), vec3(0, -1, 0), glassCol(0.9f), kGlassMat);
        quadF(b, vec3(w.x, yB, wz0), vec3(w.y, yB, wz0), vec3(w.y, yB, wz1), vec3(w.x, yB, wz1), vec3(0, 1, 0), glassCol(0.9f), kGlassMat);
        // half-closed blinds on the bullpen side
        for (float z = wz1 - 0.06f; z > wz0 + 0.5f; z -= 0.07f) box(b, vec3((w.x + w.y) * 0.5f, yB + pt * 0.5f + 0.04f, z), vec3((w.y - w.x) * 0.5f - 0.02f, 0.02f, 0.003f), Gy(0.85f), M(MAT_METAL_PAINTED), SK_NONE);
    }
    {
        InPart ip(b, IP_SHELL);
        At at(b, vec3(secX, yB - pt * 0.5f, 0.f), kPi);
        box(b, vec3(0.f, 0.02f, 2.45f), vec3(0.6f, 0.012f, 0.1f), C(0.8f, 0.1f, 0.08f), M(MAT_PAINT_WHITE), SK_NY);
        textC(b, "AUTHORIZED PERSONNEL ONLY", vec3(0.f, 0.034f, 2.45f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.05f, 0.008f, Gy(1.f), M(MAT_PAINT_WHITE));
        box(b, vec3(-0.75f, 0.03f, 1.3f), vec3(0.06f, 0.02f, 0.1f), Gy(0.2f), M(MAT_PLASTIC), SK_NY);
        box(b, vec3(-0.75f, 0.051f, 1.35f), vec3(0.03f, 0.001f, 0.02f), C(1.f, 0.1f, 0.05f, 0.9f), EM(), SK_NZ);
    }
    // ---- front desk: public counter behind ballistic glass, badge wall behind it
    const float yD = yB - 3.0f;   // public face of the desk
    {
        const float L = 6.8f, x0 = xc - L * 0.5f, x1 = xc + L * 0.5f;
        InPart ip(b, IP_FURNITURE);
        box(b, vec3(xc, yD + 0.3f, 0.55f), vec3(L * 0.5f, 0.3f, 0.55f), C(0.18f, 0.2f, 0.26f), M(MAT_WOOD), SK_NZ);
        box(b, vec3(xc, yD - 0.004f, 0.9f), vec3(L * 0.5f, 0.004f, 0.05f), Gy(0.75f), M(MAT_METAL_BRUSHED), SK_NZ);
        rbox(b, vec3(xc, yD + 0.22f, 1.12f), vec3(L * 0.5f + 0.04f, 0.3f, 0.025f), 0.01f, C(0.55f, 0.55f, 0.53f), M(MAT_MARBLE));
        box(b, vec3(xc, yD + 0.95f, 0.75f), vec3(L * 0.5f, 0.35f, 0.02f), C(0.4f, 0.3f, 0.2f), M(MAT_WOOD), SK_NONE);   // work surface
        for (int e = -1; e <= 1; e += 2) box(b, vec3(xc + e * (L * 0.5f - 0.02f), yD + 0.95f, 0.37f), vec3(0.02f, 0.35f, 0.37f), C(0.18f, 0.2f, 0.26f), M(MAT_WOOD), SK_NZ);
        collide(b, vec3(xc, yD + 0.3f, 0.57f), vec3(L * 0.5f + 0.04f, 0.32f, 0.57f));
        // glass screen with posts and a pass slot
        int np = 4;
        for (int k = 0; k <= np; k++) box(b, vec3(x0 + L * k / np, yD + 0.2f, 1.62f), vec3(0.025f, 0.025f, 0.48f), Gy(0.7f), M(MAT_METAL_BRUSHED), SK_NONE);
        box(b, vec3(xc, yD + 0.2f, 2.12f), vec3(L * 0.5f + 0.02f, 0.03f, 0.03f), Gy(0.7f), M(MAT_METAL_BRUSHED), SK_NONE);
        quadF(b, vec3(x0, yD + 0.2f, 1.22f), vec3(x1, yD + 0.2f, 1.22f), vec3(x1, yD + 0.2f, 2.1f), vec3(x0, yD + 0.2f, 2.1f), vec3(0, -1, 0), glassCol(0.88f), kGlassMat);
        quadF(b, vec3(x0, yD + 0.2f, 1.22f), vec3(x1, yD + 0.2f, 1.22f), vec3(x1, yD + 0.2f, 2.1f), vec3(x0, yD + 0.2f, 2.1f), vec3(0, 1, 0), glassCol(0.88f), kGlassMat);
        for (int k = 0; k < np; k++) {
            float x = x0 + L * (k + 0.5f) / np;
            b.pushAxes(vec3(x, yD + 0.19f, 1.55f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0));
            cyl(b, vec3(0.f), 0.07f, 0.07f, 0.01f, 12, Gy(0.6f), M(MAT_METAL_BRUSHED), true, false);   // speaking grille
            b.pop();
        }
        // monitors, phones, a desk lamp, stacked files behind the glass
        InPart ip2(b, IP_DETAIL);
        for (int k = 0; k < 3; k++) {
            float x = x0 + 1.1f + k * 2.3f;
            At at(b, vec3(x, yD + 1.0f, 0.77f), kPi);
            rbox(b, vec3(0, 0.1f, 0.25f), vec3(0.25f, 0.02f, 0.16f), 0.01f, Gy(0.08f), M(MAT_PLASTIC), true);
            box(b, vec3(0, 0.079f, 0.25f), vec3(0.23f, 0.001f, 0.14f), C(0.2f, 0.4f, 0.8f, 0.5f), EM(), SK_NZ);
            box(b, vec3(0, 0.12f, 0.05f), vec3(0.02f, 0.02f, 0.05f), Gy(0.1f), M(MAT_PLASTIC), SK_NZ);
            rbox(b, vec3(0.4f, -0.05f, 0.03f), vec3(0.09f, 0.1f, 0.03f), 0.02f, Gy(0.12f), M(MAT_PLASTIC), true);
            for (int f = 0; f < 3; f++) box(b, vec3(-0.45f, -0.1f, 0.01f + f * 0.03f), vec3(0.15f, 0.12f, 0.015f), C(0.9f, 0.75f, 0.45f), M(MAT_PAINT_WHITE), SK_NZ);
        }
        for (int k = 0; k < 2; k++) {
            InPart ip3(b, IP_FURNITURE);
            barStool(b, vec3(xc - 1.2f + k * 2.4f, yD + 1.45f, 0.f), C(0.1f, 0.1f, 0.12f));
        }
        marker(b, IM_COUNTER, vec3(xc, yD - 0.6f, 0.f), 0.f);
    }
    scenario(b, vec3(xc - 1.2f, yD + 1.45f, 0.f), kPi, 6, SR_COP, SF_STAFF, 0.26f);
    scenario(b, vec3(xc + 1.2f, yD + 1.35f, 0.f), kPi, 0, SR_COP, SF_STAFF | SF_OPTIONAL);
    {
        // badge emblem and lettering on the partition above the desk, a floor seal in front
        InPart ip(b, IP_SHELL);
        At at(b, vec3(xc, yB - pt * 0.5f, 0.f), kPi);
        badgeEmblem(b, vec3(0.f, 0.f, Min(H - 0.9f, 3.2f)), 0.55f, C(0.85f, 0.68f, 0.3f), C(0.08f, 0.14f, 0.3f));
        textC(b, "POLICE DEPARTMENT", vec3(0.f, 0.02f, Min(H - 0.9f, 3.2f) - 0.85f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.2f, 0.03f, C(0.85f, 0.68f, 0.3f), M(MAT_CHROME), 0.015f);
        light(b, vec3(0.f, 1.5f, H - 0.2f), vec3(1.f, 0.92f, 0.8f) * 140.f, 4.5f, lobby, vec3(0, -0.6f, -1.f), 45.f, 25.f);
    }
    {
        InPart ip(b, IP_SHELL);
        vec3 sc(xc, yD - 2.4f, 0.004f);
        disc(b, sc, 1.35f, 36, C(0.8f, 0.65f, 0.3f), M(MAT_MARBLE));
        disc(b, sc + vec3(0, 0, 0.001f), 1.2f, 36, C(0.1f, 0.16f, 0.32f), M(MAT_MARBLE));
        const int pts = 7;
        for (int k = 0; k < pts; k++) {
            float a0 = kTwoPi * k / pts + kHalfPi, a1 = a0 + kPi / pts, a2 = a0 - kPi / pts;
            vec3 tip = sc + vec3(cosf(a0) * 0.95f, sinf(a0) * 0.95f, 0.002f);
            vec3 i1 = sc + vec3(cosf(a1) * 0.4f, sinf(a1) * 0.4f, 0.002f), i2 = sc + vec3(cosf(a2) * 0.4f, sinf(a2) * 0.4f, 0.002f);
            tri(b, sc + vec3(0, 0, 0.002f), i2, tip, C(0.8f, 0.65f, 0.3f), M(MAT_MARBLE));
            tri(b, sc + vec3(0, 0, 0.002f), tip, i1, C(0.8f, 0.65f, 0.3f), M(MAT_MARBLE));
        }
    }
    // ---- security screening behind the entrance
    {
        float sy = Y0 + 2.8f;
        metalDetector(b, vec3(doorX, sy, 0.f), 0.f);
        float xs = doorX + (doorX < xc ? 2.4f : -2.4f);
        xrayScanner(b, vec3(xs, sy, 0.f), 0.f);
        // belt barriers funnel to the arch
        InPart ip(b, IP_FURNITURE);
        for (int e = -1; e <= 1; e += 2) {
            vec3 a(doorX + e * 0.62f, sy + 0.3f, 0.f), c(doorX + e * 1.3f, sy + 1.8f, 0.f);
            if (e * (xs - doorX) > 0.f) continue;
            lathe(b, a, {vec2(0.15f, 0.f), vec2(0.03f, 0.03f), vec2(0.03f, 0.95f), vec2(0.035f, 1.f)}, 10, Gy(0.8f), M(MAT_CHROME), true);
            lathe(b, c, {vec2(0.15f, 0.f), vec2(0.03f, 0.03f), vec2(0.03f, 0.95f), vec2(0.035f, 1.f)}, 10, Gy(0.8f), M(MAT_CHROME), true);
            tube(b, a + vec3(0, 0, 0.92f), c + vec3(0, 0, 0.92f), 0.02f, 4, C(0.1f, 0.15f, 0.4f), M(MAT_FABRIC));
        }
        scenario(b, vec3(doorX + (doorX < xc ? 1.3f : -1.3f), sy + 0.9f, 0.f), kPi, 19, SR_COP, SF_STAFF);
    }
    // ---- waiting benches on both side walls, notice / wanted boards, vending, water, planters, TV, elevators
    {
        int n = Clamp((int)((yD - 1.2f - (Y0 + 4.5f)) / 0.58f), 2, 6);
        float by = Y0 + 4.5f + n * 0.29f;
        beamSeats(b, vec3(X0 + 0.45f, by, 0.f), -kHalfPi, n, C(0.15f, 0.25f, 0.45f));
        beamSeats(b, vec3(X1 - 0.45f, by, 0.f), kHalfPi, n, C(0.15f, 0.25f, 0.45f));
        scenario(b, vec3(X0 + 0.5f, by - 0.29f, 0.f), -kHalfPi, 6, SR_PATRON, SF_OPTIONAL);
        scenario(b, vec3(X1 - 0.5f, by + 0.29f, 0.f), kHalfPi, 6, SR_PATRON, SF_OPTIONAL);
        scenario(b, vec3(X0 + 0.5f, by + 0.87f, 0.f), -kHalfPi, 6, SR_PATRON, SF_OPTIONAL);
        {
            At at(b, vec3(X0, by, 0.f), -kHalfPi);
            noticeBoard(b, vec3(0.f, 0.f, 1.75f), Min(2.4f, n * 0.58f), 0.9f, r.next(), true);
        }
        {
            At at(b, vec3(X1, by, 0.f), kHalfPi);
            noticeBoard(b, vec3(0.f, 0.f, 1.75f), Min(2.4f, n * 0.58f), 0.9f, r.next(), false);
        }
        vendingMachine(b, vec3(X0, Y0 + 3.4f, 0.f), -kHalfPi, C(0.1f, 0.3f, 0.6f), r.next());
        waterCooler(b, vec3(X1 - 0.3f, Y0 + 3.4f, 0.f));
        planter(b, vec3(X0 + 0.55f, Y0 + 0.65f, 0.f), 1.6f, r.next());
        planter(b, vec3(X1 - 0.55f, Y0 + 0.65f, 0.f), 1.6f, r.next());
        {
            At at(b, vec3(X1, yD - 0.2f, 0.f), kHalfPi);
            wallTV(b, vec3(0.f, 0.f, 2.4f), 1.3f, lobby, r.next());
        }
        float ex = xc - 4.6f;
        if (ex - 2.1f > X0 + 0.3f) {
            elevatorLanding(b, vec3(ex - 1.0f, yB - pt * 0.5f, 0.f), kPi, 1);
            collide(b, vec3(ex - 1.0f, yB - pt * 0.5f - 0.05f, 1.1f), vec3(0.75f, 0.06f, 1.1f));
            scenario(b, vec3(ex - 1.0f, yB - 1.6f, 0.f), 0.3f, 7, SR_COP, SF_OPTIONAL);
            scenario(b, vec3(ex - 0.4f, yB - 2.1f, 0.f), kPi - 0.6f, 7, SR_COP, SF_OPTIONAL);
        }
        // memorial plaque by the entrance
        At at(b, vec3(doorX + (doorX < xc ? -2.2f : 2.2f), Y0, 0.f), 0.f);
        InPart ip(b, IP_SHELL);
        box(b, vec3(0.f, 0.01f, 1.6f), vec3(0.45f, 0.01f, 0.32f), C(0.55f, 0.4f, 0.2f), M(MAT_CHROME), SK_NY);
        textC(b, "IN MEMORY OF THOSE", vec3(0.f, 0.022f, 1.78f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.045f, 0.008f, C(0.2f, 0.12f, 0.05f), M(MAT_PAINT_WHITE));
        textC(b, "WHO SERVED", vec3(0.f, 0.022f, 1.7f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.045f, 0.008f, C(0.2f, 0.12f, 0.05f), M(MAT_PAINT_WHITE));
        for (int l = 0; l < 5; l++) box(b, vec3(0.f, 0.021f, 1.6f - l * 0.06f), vec3(0.3f, 0.001f, 0.008f), C(0.2f, 0.12f, 0.05f), M(MAT_PAINT_WHITE), SK_NZ);
    }
    // ---- lights: downlight grid + ring pendants over the desk
    {
        int nx = Max(2, (int)((X1 - X0) / 3.2f)), ny = Max(2, (int)((yB - Y0) / 3.2f));
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) downlight(b, vec3(X0 + (X1 - X0) * (i + 0.5f) / nx, Y0 + (yB - Y0) * (j + 0.5f) / ny, H), lobby, 380.f, vec3(1.f, 0.93f, 0.82f), 8.f);
        for (int k = -1; k <= 1; k += 2) {
            vec3 top(xc + k * 1.7f, yD + 0.4f, H);
            InPart ip(b, IP_FURNITURE);
            tube(b, top, top - vec3(0, 0, H - 2.9f), 0.004f, 3, Gy(0.2f), M(MAT_METAL_PAINTED));
            cyl(b, top - vec3(0, 0, H - 2.85f), 0.6f, 0.6f, 0.05f, 24, Gy(0.2f), M(MAT_METAL_PAINTED), true, false);
            b.pushAxes(top - vec3(0, 0, H - 2.85f), vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 0, -1));
            disc(b, vec3(0, 0, 0.001f), 0.57f, 24, C(1.f, 0.95f, 0.88f, 0.9f), EM());
            b.pop();
            light(b, top - vec3(0, 0, H - 2.75f), vec3(1.f, 0.95f, 0.88f) * 300.f, 6.f, lobby, vec3(0, 0, -1), 75.f, 45.f);
        }
    }
    // ---- bullpen: facing desk pairs, filing cabinets, whiteboard, coffee
    {
        float py0 = yB + pt * 0.5f, py1 = Y1;
        int ncol = Max(1, (int)((X1 - X0 - 3.f) / 3.2f));
        for (int i = 0; i < ncol; i++) {
            float x = X0 + 1.6f + (X1 - X0 - 3.2f) * (i + 0.5f) / ncol;
            if (fabsf(x - secX) < 1.4f) continue;
            float y = (py0 + py1) * 0.5f;
            officeDesk(b, vec3(x, y - 0.36f, 0.f), kPi, r.next(), false);
            officeDesk(b, vec3(x, y + 0.36f, 0.f), 0.f, r.next(), false);
            officeChair(b, vec3(x - 0.1f, y - 1.2f, 0.f), 0.f, C(0.1f, 0.1f, 0.12f));
            officeChair(b, vec3(x + 0.1f, y + 1.2f, 0.f), kPi, C(0.1f, 0.1f, 0.12f));
            if (i % 2 == 0) scenario(b, vec3(x - 0.1f, y - 1.2f, 0.f), 0.f, 6, SR_COP, SF_OPTIONAL);
            else scenario(b, vec3(x + 0.1f, y + 1.2f, 0.f), kPi, 6, SR_COP, SF_OPTIONAL);
        }
        InPart ip(b, IP_FURNITURE);
        for (int k = 0; k < 3; k++) {
            float x = X1 - 0.3f, y = py0 + 0.6f + k * 0.5f;
            box(b, vec3(x, y, 0.66f), vec3(0.3f, 0.24f, 0.66f), Gy(0.5f), M(MAT_METAL_PAINTED), SK_NZ);
            for (int dr = 0; dr < 4; dr++) box(b, vec3(x - 0.301f, y, 0.2f + dr * 0.32f), vec3(0.001f, 0.08f, 0.012f), Gy(0.75f), M(MAT_CHROME), SK_NONE);
        }
        collide(b, vec3(X1 - 0.3f, py0 + 1.1f, 0.66f), vec3(0.3f, 0.75f, 0.66f));
        {
            At at(b, vec3(X0, (py0 + py1) * 0.5f, 0.f), -kHalfPi);
            box(b, vec3(0.f, 0.02f, 1.5f), vec3(1.2f, 0.02f, 0.6f), Gy(0.97f), M(MAT_PAINT_WHITE), SK_NY);
            InPart ipd(b, IP_DETAIL);
            Rng wr(d.seed + 9u);
            for (int k = 0; k < 9; k++) box(b, vec3(wr.range(-1.0f, 1.0f), 0.041f, wr.range(1.05f, 1.95f)), vec3(wr.range(0.05f, 0.25f), 0.001f, 0.006f), C(hsv(wr.chance(0.5f) ? 0.6f : 0.f, 0.8f, 0.6f)), M(MAT_PAINT_WHITE), SK_NZ);
            for (int k = 0; k < 4; k++) box(b, vec3(-0.8f + k * 0.5f, 0.042f, 1.85f), vec3(0.07f, 0.001f, 0.09f), Gy(0.9f), M(MAT_PAINT_WHITE), SK_NZ);
            scenario(b, vec3(0.f, 1.1f, 0.f), 0.f, 7, SR_COP, SF_OPTIONAL);
        }
        coffeeStation(b, vec3(X0 + 1.8f, py1, 0.f), kPi, 1.4f, r.next());
        int nx = Max(1, (int)((X1 - X0) / 3.2f));
        for (int i = 0; i < nx; i++) troffer(b, vec3(X0 + (X1 - X0) * (i + 0.5f) / nx, (py0 + py1) * 0.5f, HB), 1.2f, 0.6f, pen, 460.f, vec3(0.97f, 0.99f, 1.f), 6.5f);
    }
    roomDressing(b, lobby, true, d.seed ^ 0xB1u);
    roomDressing(b, pen, true, d.seed ^ 0xB2u);
}

// ------------------------------------------------------------------------------------------------ hospital pieces
// Hospital bed (head toward -y), rails, mattress, pillow, blanket
void hospitalBed(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    u32 frame = Gy(0.85f), steel = M(MAT_METAL_BRUSHED);
    box(b, vec3(0, 0, 0.5f), vec3(0.47f, 1.05f, 0.06f), frame, M(MAT_METAL_PAINTED), SK_NONE);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            tube(b, vec3(sx * 0.4f, sy * 0.95f, 0.1f), vec3(sx * 0.4f, sy * 0.95f, 0.45f), 0.025f, 6, Gy(0.7f), steel);
            sphere(b, vec3(sx * 0.4f, sy * 0.95f, 0.06f), 0.05f, 8, Gy(0.1f), M(MAT_RUBBER));
        }
    rbox(b, vec3(0, 0.05f, 0.63f), vec3(0.44f, 0.98f, 0.08f), 0.04f, C(0.85f, 0.9f, 0.92f), M(MAT_CLOTH));
    rbox(b, vec3(0, 0.35f, 0.72f), vec3(0.46f, 0.6f, 0.03f), 0.02f, C(0.55f, 0.72f, 0.82f), M(MAT_CLOTH));
    b.pushAxes(vec3(0, -0.8f, 0.78f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.5f)), normalize(vec3(0, -0.5f, 1)));
    rbox(b, vec3(0.f), vec3(0.3f, 0.06f, 0.16f), 0.05f, Gy(0.97f), M(MAT_CLOTH), true);
    b.pop();
    box(b, vec3(0, -1.08f, 0.85f), vec3(0.46f, 0.03f, 0.35f), C(0.8f, 0.82f, 0.84f), M(MAT_PLASTIC), SK_NONE);   // headboard
    box(b, vec3(0, 1.08f, 0.72f), vec3(0.46f, 0.03f, 0.2f), C(0.8f, 0.82f, 0.84f), M(MAT_PLASTIC), SK_NONE);
    for (int sx = -1; sx <= 1; sx += 2) {
        tube(b, vec3(sx * 0.49f, -0.7f, 0.85f), vec3(sx * 0.49f, 0.2f, 0.85f), 0.018f, 6, Gy(0.8f), steel, true);
        tube(b, vec3(sx * 0.49f, -0.7f, 0.62f), vec3(sx * 0.49f, -0.7f, 0.85f), 0.015f, 6, Gy(0.8f), steel);
        tube(b, vec3(sx * 0.49f, 0.2f, 0.62f), vec3(sx * 0.49f, 0.2f, 0.85f), 0.015f, 6, Gy(0.8f), steel);
    }
    collide(b, vec3(0, 0, 0.45f), vec3(0.5f, 1.1f, 0.45f));
}

// IV stand with a bag, and a patient monitor on a pole (animated trace)
void ivMonitor(IB& b, vec3 p, int roomIdx, u32 seed) {
    lathe(b, p, {vec2(0.25f, 0.f), vec2(0.24f, 0.03f), vec2(0.02f, 0.06f)}, 5, Gy(0.75f), M(MAT_METAL_BRUSHED), false);
    tube(b, p, p + vec3(0, 0, 1.9f), 0.012f, 6, Gy(0.85f), M(MAT_CHROME));
    tube(b, p + vec3(-0.15f, 0, 1.9f), p + vec3(0.15f, 0, 1.9f), 0.008f, 5, Gy(0.85f), M(MAT_CHROME), true);
    rbox(b, p + vec3(0.12f, 0, 1.72f), vec3(0.06f, 0.015f, 0.1f), 0.012f, C(0.9f, 0.95f, 1.f, 1.f), kGlassMat, true);
    tube(b, p + vec3(0.12f, 0, 1.62f), p + vec3(0.3f, 0.4f, 0.8f), 0.003f, 3, C(0.9f, 0.95f, 1.f), M(MAT_PLASTIC));
    At at(b, p + vec3(0, 0, 1.35f), 0.f);
    rbox(b, vec3(0, 0.08f, 0.f), vec3(0.16f, 0.06f, 0.12f), 0.015f, Gy(0.9f), M(MAT_PLASTIC), true);
    box(b, vec3(0, 0.141f, 0.f), vec3(0.13f, 0.001f, 0.09f), C(0.02f, 0.04f, 0.04f), M(MAT_PLASTIC), SK_NZ);
    // heartbeat trace (blinking segments)
    float xs[7] = {-0.12f, -0.06f, -0.03f, -0.01f, 0.01f, 0.04f, 0.12f};
    float zs[7] = {0.f, 0.f, 0.06f, -0.04f, 0.f, 0.f, 0.f};
    for (int k = 0; k < 6; k++) {
        vec3 a(xs[k], 0.143f, zs[k] * 0.8f), c(xs[k + 1], 0.143f, zs[k + 1] * 0.8f);
        vec3 dd = c - a;
        float L = length(dd);
        b.pushAxes(a, dd / L, vec3(0, 1, 0), cross(dd / L, vec3(0, 1, 0)));
        box(b, vec3(L * 0.5f, 0.f, 0.f), vec3(L * 0.5f, 0.0005f, 0.003f), C(0.2f, 1.f, 0.4f, 0.9f), EM(2, (u32)(k * 30 + (seed & 63u))), SK_NONE);
        b.pop();
    }
    light(b, vec3(0, 0.3f, 0.f), vec3(0.3f, 1.f, 0.5f) * 3.f, 1.2f, roomIdx);
}

// Wheelchair (front +y)
void wheelchair(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    u32 steel = M(MAT_CHROME);
    for (int e = -1; e <= 1; e += 2) {
        b.pushAxes(vec3(e * 0.3f, -0.1f, 0.3f), vec3(0, 1, 0), vec3(0, 0, (float)e), vec3((float)e, 0, 0));
        lathe(b, vec3(0, 0, -0.02f), {vec2(0.27f, 0.f), vec2(0.3f, 0.02f), vec2(0.27f, 0.04f)}, 18, Gy(0.08f), M(MAT_RUBBER), false);
        cyl(b, vec3(0, 0, -0.005f), 0.02f, 0.02f, 0.03f, 6, Gy(0.7f), steel, true, true);
        for (int k = 0; k < 6; k++) {
            float a = kPi * k / 6.f;
            tube(b, vec3(cosf(a) * 0.26f, sinf(a) * 0.26f, 0.01f), vec3(-cosf(a) * 0.26f, -sinf(a) * 0.26f, 0.01f), 0.003f, 3, Gy(0.75f), steel);
        }
        b.pop();
        sphere(b, vec3(e * 0.22f, 0.32f, 0.07f), 0.07f, 8, Gy(0.1f), M(MAT_RUBBER));
        tube(b, vec3(e * 0.24f, -0.25f, 0.5f), vec3(e * 0.24f, -0.25f, 0.95f), 0.012f, 6, Gy(0.8f), steel);
        tube(b, vec3(e * 0.24f, -0.25f, 0.95f), vec3(e * 0.24f, -0.35f, 0.97f), 0.012f, 6, Gy(0.1f), M(MAT_RUBBER), true);
        tube(b, vec3(e * 0.22f, 0.3f, 0.07f), vec3(e * 0.23f, 0.15f, 0.48f), 0.012f, 6, Gy(0.8f), steel);
    }
    box(b, vec3(0, 0.05f, 0.5f), vec3(0.22f, 0.22f, 0.02f), C(0.1f, 0.2f, 0.35f), M(MAT_FABRIC), SK_NONE);
    box(b, vec3(0, -0.24f, 0.75f), vec3(0.22f, 0.01f, 0.2f), C(0.1f, 0.2f, 0.35f), M(MAT_FABRIC), SK_NONE);
    box(b, vec3(0, 0.38f, 0.12f), vec3(0.18f, 0.08f, 0.01f), Gy(0.3f), M(MAT_METAL_PAINTED), SK_NONE);
    collide(b, vec3(0, 0.05f, 0.45f), vec3(0.32f, 0.4f, 0.45f));
}

// Curtain cubicle: ceiling track (U shape) with curtains on three sides, open toward -y (local)
void curtainBay(IB& b, vec3 p, float yaw, float w, float dp, float trackZ, u32 col, u32 seed) {
    At at(b, p, yaw);
    InPart ip(b, IP_FURNITURE);
    u32 steel = M(MAT_METAL_BRUSHED);
    box(b, vec3(-w * 0.5f, 0.f, trackZ), vec3(0.015f, dp * 0.5f, 0.012f), Gy(0.85f), steel, SK_NONE);
    box(b, vec3(w * 0.5f, 0.f, trackZ), vec3(0.015f, dp * 0.5f, 0.012f), Gy(0.85f), steel, SK_NONE);
    box(b, vec3(0.f, -dp * 0.5f, trackZ), vec3(w * 0.5f, 0.015f, 0.012f), Gy(0.85f), steel, SK_NONE);
    for (int e = -1; e <= 1; e += 2) {
        At s(b, vec3(e * w * 0.5f, 0.f, 0.f), e < 0 ? -kHalfPi : kHalfPi);
        curtains(b, -dp * 0.5f, dp * 0.5f, 0.35f, trackZ - 0.14f, 0.f, col, seed + (u32)(e + 1), 0.9f);
    }
    {
        At s(b, vec3(0.f, -dp * 0.5f, 0.f), kPi);
        curtains(b, -w * 0.5f + 0.2f, w * 0.5f - 0.2f, 0.35f, trackZ - 0.14f, 0.f, col, seed + 7u, 0.25f);
    }
}

// ------------------------------------------------------------------------------------------------ hospital lobby
void layoutHospital(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0x4059u);
    const float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT, H = d.ceil;
    float dx0 = 0.f, dx1 = 0.f, dh = 2.2f;
    mainDoorOpening(d, dx0, dx1, dh);
    const float doorX = (dx0 + dx1) * 0.5f;
    const float pt = 0.15f;
    const float yT = Y0 + Clamp((Y1 - Y0) * 0.6f, 7.f, 10.f);   // lobby | triage partition
    const float HT = Min(H, 3.2f);
    int lobby = room(b, vec3(X0, Y0, 0.f), vec3(X1, yT - pt * 0.5f, H), vec3(58.f, 60.f, 60.f), 0.08f, LS_ALWAYS);
    int triage = room(b, vec3(X0, yT + pt * 0.5f, 0.f), vec3(X1, Y1, HT), vec3(60.f, 62.f, 62.f), 0.f, LS_ALWAYS);
    storefrontEntrance(b, DK_SLIDING_PAIR, 1, Gy(0.8f));
    // exterior: EMERGENCY letters and an H sign over the entrance
    {
        float top = dh;
        for (const InteriorOpening& op : d.openings) {
            vec3 a = d.toLocal(vec3(op.a, op.z0));
            if (fabsf(a.y) < 0.1f) top = Max(top, op.z1 - d.origin.z);
        }
        float z = d.signZ1 > 0.f ? (d.signZ0 + d.signZ1) * 0.5f : top + 0.45f;
        InPart ip(b, IP_SHELL);
        if (d.signZ1 > 0.f) {
            float x0 = d.x0 + 0.08f, x1 = d.x1 - 0.08f;
            box(b, vec3((x0 + x1) * 0.5f, -0.045f, z), vec3((x1 - x0) * 0.5f, 0.04f, (d.signZ1 - d.signZ0) * 0.5f + 0.03f), Gy(0.95f), M(MAT_METAL_PAINTED), SK_PY);
        }
        At at(b, vec3(doorX, -0.09f, z), kPi);
        textC(b, "EMERGENCY", vec3(0.f, 0.f, -0.02f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.38f, 0.06f, C(0.9f, 0.08f, 0.06f), EM(), 0.03f);
        float hx = 2.6f;
        box(b, vec3(hx, -0.02f, 0.f), vec3(0.3f, 0.03f, 0.3f), C(0.1f, 0.3f, 0.8f), EM(), SK_NONE);
        textC(b, "H", vec3(hx, 0.015f, 0.f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.36f, 0.07f, Gy(1.f), M(MAT_PAINT_WHITE), 0.01f);
        light(b, vec3(0.f, -1.f, 0.f), vec3(1.f, 0.2f, 0.15f) * 90.f, 6.f, -1);
    }
    // double doors to triage, partition
    const float tdX = Clamp(doorX + (doorX < (X0 + X1) * 0.5f ? 4.f : -4.f), X0 + 1.5f, X1 - 1.5f);
    door(b, vec3(tdX, yT, 0.f), vec2(1, 0), vec2(0, 1), 1.6f, 2.2f, DK_HINGED_PAIR, 2, C(0.75f, 0.82f, 0.85f), false);
    partitionX(b, X0, X1, yT, pt, H, {vec2(tdX - 0.8f, tdX + 0.8f)});
    ShellStyle ls;
    ls.wallCol = C(0.92f, 0.94f, 0.94f);
    ls.floorMat = M(MAT_TILE);
    ls.floorCol = C(0.78f, 0.84f, 0.86f);
    ls.floorUV = 1.f;
    ls.ceilMat = M(MAT_CEILING_TILE);
    ls.ceilCol = Gy(0.95f);
    ls.chairRail = 0.95f;
    ls.baseCol = C(0.3f, 0.6f, 0.62f);
    ls.baseMat = M(MAT_PLASTIC);
    ls.baseH = 0.12f;
    ls.revealCol = Gy(0.85f);
    shell(b, lobby, ls);
    ShellStyle ts = ls;
    ts.floorCol = C(0.84f, 0.86f, 0.84f);
    shell(b, triage, ts);
    doorFrameInner(b, vec3(tdX, yT, 0.f), vec2(1, 0), vec2(0, 1), 1.6f, 2.2f, pt, C(0.3f, 0.6f, 0.62f));
    {
        InPart ip(b, IP_SHELL);
        At at(b, vec3(tdX, yT - pt * 0.5f, 0.f), kPi);
        box(b, vec3(0.f, 0.02f, 2.55f), vec3(0.7f, 0.012f, 0.12f), C(0.8f, 0.1f, 0.08f), M(MAT_PAINT_WHITE), SK_NY);
        textC(b, "TRIAGE - STAFF ONLY", vec3(0.f, 0.034f, 2.55f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.07f, 0.011f, Gy(1.f), M(MAT_PAINT_WHITE));
    }
    // ---- registration desk (front +y faces the lobby) on the side away from the triage doors
    const float cs = tdX > (X0 + X1) * 0.5f ? -1.f : 1.f;
    const float dxc = Clamp((X0 + X1) * 0.5f + cs * 3.2f, X0 + 2.8f, X1 - 2.8f);
    const float dyc = yT - 2.2f;
    {
        InPart ip(b, IP_FURNITURE);
        const float L = 4.2f;
        box(b, vec3(dxc, dyc, 0.55f), vec3(L * 0.5f, 0.3f, 0.55f), C(0.3f, 0.6f, 0.62f), M(MAT_METAL_PAINTED), SK_NZ);
        rbox(b, vec3(dxc, dyc - 0.08f, 1.12f), vec3(L * 0.5f + 0.05f, 0.25f, 0.025f), 0.01f, Gy(0.95f), M(MAT_MARBLE));
        box(b, vec3(dxc, dyc + 0.6f, 0.75f), vec3(L * 0.5f, 0.35f, 0.02f), Gy(0.9f), M(MAT_PLASTIC), SK_NONE);
        for (int e = -1; e <= 1; e += 2) box(b, vec3(dxc + e * (L * 0.5f - 0.02f), dyc + 0.6f, 0.37f), vec3(0.02f, 0.35f, 0.37f), C(0.3f, 0.6f, 0.62f), M(MAT_METAL_PAINTED), SK_NZ);
        collide(b, vec3(dxc, dyc, 0.57f), vec3(L * 0.5f + 0.05f, 0.33f, 0.57f));
        InPart ip2(b, IP_DETAIL);
        for (int k = 0; k < 2; k++) {
            At at(b, vec3(dxc - 1.0f + k * 2.0f, dyc + 0.75f, 0.77f), kPi);
            rbox(b, vec3(0, 0.1f, 0.25f), vec3(0.25f, 0.02f, 0.16f), 0.01f, Gy(0.08f), M(MAT_PLASTIC), true);
            box(b, vec3(0, 0.079f, 0.25f), vec3(0.23f, 0.001f, 0.14f), C(0.4f, 0.7f, 0.8f, 0.5f), EM(), SK_NZ);
            box(b, vec3(0, 0.12f, 0.05f), vec3(0.02f, 0.02f, 0.05f), Gy(0.1f), M(MAT_PLASTIC), SK_NZ);
        }
        // hanging signs over the desk
        hangingSign(b, vec3(dxc - 1.1f, dyc, H - 0.6f), 0.f, "REGISTRATION", C(0.1f, 0.3f, 0.6f), Gy(1.f), H);
        hangingSign(b, vec3(dxc + 1.1f, dyc, H - 0.6f), 0.f, "INFORMATION", C(0.1f, 0.3f, 0.6f), Gy(1.f), H);
        InPart ip3(b, IP_FURNITURE);
        for (int k = 0; k < 2; k++) officeChair(b, vec3(dxc - 1.0f + k * 2.0f, dyc + 1.3f, 0.f), kPi, C(0.2f, 0.4f, 0.5f));
        marker(b, IM_COUNTER, vec3(dxc, dyc - 0.9f, 0.f), 0.f);
    }
    scenario(b, vec3(dxc - 1.0f, dyc + 1.3f, 0.f), kPi, 6, SR_NURSE, SF_STAFF);
    scenario(b, vec3(dxc + 1.0f, dyc + 1.3f, 0.f), kPi, 6, SR_RECEPTION, SF_STAFF | SF_OPTIONAL);
    // ---- waiting area: back-to-back rows facing a wall TV, kids corner, vending, sanitizer, plants
    {
        float wx0 = cs > 0.f ? X0 + 0.8f : dxc + 2.8f, wx1 = cs > 0.f ? dxc - 2.8f : X1 - 0.8f;
        if (wx1 - wx0 < 2.5f) {
            wx0 = X0 + 0.8f;
            wx1 = X1 - 0.8f;
        }
        int n = Clamp((int)((wx1 - wx0) / 0.58f), 2, 7);
        float wxc = (wx0 + wx1) * 0.5f;
        for (int row = 0; row < 2; row++) {
            float y = Y0 + 3.2f + row * 1.9f;
            if (y > dyc - 1.5f) break;
            beamSeats(b, vec3(wxc, y - 0.3f, 0.f), 0.f, n, C(0.2f, 0.55f, 0.6f));
            beamSeats(b, vec3(wxc, y + 0.3f, 0.f), kPi, n, C(0.2f, 0.55f, 0.6f));
            scenario(b, vec3(wxc - 0.29f * (n - 2), y - 0.3f, 0.f), 0.f, 6, SR_PATIENT, SF_OPTIONAL);
            scenario(b, vec3(wxc + 0.29f * (n - 3), y + 0.3f, 0.f), kPi, 6, SR_PATIENT, SF_OPTIONAL);
        }
        float tvX = cs > 0.f ? X0 : X1;
        {
            At at(b, vec3(tvX, Y0 + 4.2f, 0.f), cs > 0.f ? -kHalfPi : kHalfPi);
            wallTV(b, vec3(0.f, 0.f, 2.3f), 1.4f, lobby, r.next());
        }
        vendingMachine(b, vec3(cs > 0.f ? X1 : X0, Y0 + 2.2f, 0.f), cs > 0.f ? kHalfPi : -kHalfPi, C(0.7f, 0.1f, 0.1f), r.next());
        vendingMachine(b, vec3(cs > 0.f ? X1 : X0, Y0 + 3.2f, 0.f), cs > 0.f ? kHalfPi : -kHalfPi, C(0.1f, 0.2f, 0.45f), r.next());
        waterCooler(b, vec3(cs > 0.f ? X1 - 0.3f : X0 + 0.3f, Y0 + 4.2f, 0.f));
        planter(b, vec3(X0 + 0.55f, Y0 + 0.65f, 0.f), 1.4f, r.next());
        planter(b, vec3(X1 - 0.55f, Y0 + 0.65f, 0.f), 1.4f, r.next());
        // sanitizer post by the door
        vec3 sp(doorX + 1.6f, Y0 + 1.4f, 0.f);
        lathe(b, sp, {vec2(0.18f, 0.f), vec2(0.03f, 0.03f), vec2(0.025f, 1.1f)}, 10, Gy(0.85f), M(MAT_METAL_BRUSHED), true);
        rbox(b, sp + vec3(0, 0, 1.2f), vec3(0.07f, 0.06f, 0.12f), 0.02f, Gy(0.95f), M(MAT_PLASTIC), true);
        collide(b, sp + vec3(0, 0, 0.6f), vec3(0.1f, 0.1f, 0.6f));
        // wheelchair parked near the entrance
        wheelchair(b, vec3(doorX - 1.9f, Y0 + 1.2f, 0.f), 0.4f);
        // wayfinding signs
        hangingSign(b, vec3(tdX, yT - 1.2f, H - 0.55f), 0.f, "EMERGENCY  >", C(0.75f, 0.08f, 0.06f), Gy(1.f), H);
        {
            At at(b, vec3(cs > 0.f ? X1 : X0, dyc, 0.f), cs > 0.f ? kHalfPi : -kHalfPi);
            InPart ip(b, IP_SHELL);
            box(b, vec3(0.f, 0.01f, 2.2f), vec3(0.7f, 0.01f, 0.3f), C(0.1f, 0.3f, 0.6f), M(MAT_PAINT_WHITE), SK_NY);
            textC(b, "RADIOLOGY", vec3(0.f, 0.022f, 2.3f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.07f, 0.011f, Gy(1.f), M(MAT_PAINT_WHITE));
            textC(b, "PHARMACY", vec3(0.f, 0.022f, 2.15f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.07f, 0.011f, Gy(1.f), M(MAT_PAINT_WHITE));
            textC(b, "ELEVATORS", vec3(0.f, 0.022f, 2.0f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.07f, 0.011f, Gy(1.f), M(MAT_PAINT_WHITE));
        }
        scenario(b, vec3(tdX - 1.2f, yT - 1.0f, 0.f), 0.5f, 7, SR_DOCTOR, SF_OPTIONAL);
        scenario(b, vec3(tdX - 0.6f, yT - 1.5f, 0.f), kPi - 0.4f, 7, SR_NURSE, SF_OPTIONAL);
        scenario(b, vec3(doorX + (cs > 0.f ? -2.5f : 2.5f), Y0 + 1.6f, 0.f), 0.f, 19, SR_GUARD, SF_STAFF);
    }
    // ---- lights
    {
        int nx = Max(2, (int)((X1 - X0) / 2.8f)), ny = Max(2, (int)((yT - Y0) / 2.8f));
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) troffer(b, vec3(X0 + (X1 - X0) * (i + 0.5f) / nx, Y0 + (yT - Y0) * (j + 0.5f) / ny, H), 1.2f, 0.6f, lobby, 760.f, vec3(0.96f, 0.99f, 1.f), 7.5f);
    }
    // ---- triage: curtained bays with beds and monitors, a gurney in the corridor, a nurses' station
    {
        float ty0 = yT + pt * 0.5f, ty1 = Y1;
        float bayD = Min(3.2f, ty1 - ty0 - 1.6f);
        int nb = Clamp((int)((X1 - X0 - 1.f) / 2.6f), 1, 5);
        for (int k = 0; k < nb; k++) {
            float bx = X0 + 0.5f + (X1 - X0 - 1.f) * (k + 0.5f) / nb;
            if (fabsf(bx - tdX) < 1.2f) continue;
            float by = ty1 - bayD * 0.5f;
            curtainBay(b, vec3(bx, by, 0.f), 0.f, 2.4f, bayD, HT - 0.12f, C(0.55f, 0.75f, 0.8f), (u32)(k * 13 + 1));
            hospitalBed(b, vec3(bx, by + 0.1f, 0.f), kPi);
            ivMonitor(b, vec3(bx + 0.8f, ty1 - 0.4f, 0.f), triage, (u32)k);
            if (k % 2 == 0) scenario(b, vec3(bx - 0.9f, by - 0.3f, 0.f), -kHalfPi, 0, SR_NURSE, SF_OPTIONAL);
        }
        // gurney + crash cart along the corridor
        InPart ip(b, IP_FURNITURE);
        vec3 gp(tdX + (tdX < (X0 + X1) * 0.5f ? 2.2f : -2.2f), ty0 + 0.8f, 0.f);
        At at(b, gp, kHalfPi);
        box(b, vec3(0, 0, 0.8f), vec3(0.3f, 0.95f, 0.05f), Gy(0.85f), M(MAT_METAL_BRUSHED), SK_NONE);
        rbox(b, vec3(0, 0, 0.88f), vec3(0.28f, 0.9f, 0.04f), 0.03f, Gy(0.95f), M(MAT_CLOTH));
        for (int sx = -1; sx <= 1; sx += 2)
            for (int sy = -1; sy <= 1; sy += 2) tube(b, vec3(sx * 0.25f, sy * 0.85f, 0.08f), vec3(sx * 0.25f, sy * 0.85f, 0.78f), 0.018f, 6, Gy(0.8f), M(MAT_CHROME));
        collide(b, vec3(0, 0, 0.45f), vec3(0.3f, 0.95f, 0.45f));
        int nx = Max(1, (int)((X1 - X0) / 3.f));
        for (int i = 0; i < nx; i++) troffer(b, vec3(X0 + (X1 - X0) * (i + 0.5f) / nx, ty0 + 0.9f, HT), 1.2f, 0.6f, triage, 650.f, vec3(0.96f, 0.99f, 1.f), 6.5f);
    }
    roomDressing(b, lobby, true, d.seed ^ 0xC1u);
    roomDressing(b, triage, true, d.seed ^ 0xC2u);
}

}  // namespace ikit
}  // namespace World
