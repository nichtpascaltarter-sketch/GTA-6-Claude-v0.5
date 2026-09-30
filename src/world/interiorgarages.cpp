// Car businesses. Palm Motors: a glass-fronted showroom with a hero car on a lit podium and more on the floor under
// track spots, the brand wall, sales desks, a customer lounge, rims on the wall, sale banners, a roll-up delivery
// door and glazed back offices. Tide Customs (Calle Luna and Sol Beach): a body and paint shop with a drive-in bay,
// a lit paint booth with a car inside, a lift, the tire machine and balancer, a rim wall under the neon logo, the
// paint mixing rack, benches and a customer counter. Sunwash Car Wash: an automatic wash bay with a brush gantry,
// spray arches and a drying blower, the pay kiosk and a vacuum station.
#include "interiors.h"
#include "sites.h"

namespace World {
namespace ikit {

// ------------------------------------------------------------------------------------------------ shared pieces
// Display car slot: the vehicle itself is drawn by gameplay (IM_CAR), the layout adds its collision
void displayCar(IB& b, vec3 p, float yaw) {
    marker(b, IM_CAR, p, yaw);
    At at(b, p, yaw);
    collide(b, vec3(0.f, 0.f, 0.7f), vec3(0.95f, 2.3f, 0.7f));
}

// Round display podium with an LED ring (top at h)
void carPodium(IB& b, vec3 c, float R, float h, int roomIdx) {
    InPart ip(b, IP_SHELL);
    cyl(b, c, R, R, h, 48, Gy(0.92f), M(MAT_MARBLE), true, false);
    cyl(b, c + vec3(0, 0, h * 0.35f), R + 0.004f, R + 0.004f, 0.025f, 48, C(0.6f, 0.9f, 1.f, 0.6f), EM(), false, false);
    collide(b, c + vec3(0, 0, h * 0.5f), vec3(R * 0.7f, R * 0.7f, h * 0.5f));
    light(b, c + vec3(0, 0, 0.3f), vec3(0.6f, 0.85f, 1.f) * 40.f, R + 1.5f, roomIdx);
}

// Alloy wheel (rim + tire) standing on edge, axis along local x (face toward +x)
void rimWheel(IB& b, vec3 c, float yaw, float R, u32 rimCol, int spokes) {
    At at(b, c, yaw);
    b.pushAxes(vec3(0.f), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1, 0, 0));
    cyl(b, vec3(0, 0, -0.11f), R, R, 0.22f, 20, Gy(0.05f), M(MAT_RUBBER), true, true);
    disc(b, vec3(0, 0, 0.111f), R * 0.72f, 20, Gy(0.15f), M(MAT_METAL_PAINTED));
    cyl(b, vec3(0, 0, 0.112f), R * 0.7f, R * 0.7f, 0.02f, 20, rimCol, M(MAT_CHROME), false, false);
    for (int k = 0; k < spokes; k++) {
        float a = kTwoPi * k / spokes;
        tube(b, vec3(cosf(a) * 0.05f, sinf(a) * 0.05f, 0.125f), vec3(cosf(a) * R * 0.68f, sinf(a) * R * 0.68f, 0.12f), 0.018f, 4, rimCol, M(MAT_CHROME));
    }
    cyl(b, vec3(0, 0, 0.11f), 0.07f, 0.07f, 0.03f, 12, rimCol, M(MAT_CHROME), true, false);
    b.pop();
}

// Hanging vertical banner from the ceiling (two faces +-y), text on both sides
void hangingBanner(IB& b, vec3 top, float w, float h, u32 bg, u32 fg, const char* l1, const char* l2) {
    InPart ip(b, IP_FURNITURE);
    tube(b, top + vec3(-w * 0.5f, 0, 0), top + vec3(-w * 0.5f, 0, -0.5f), 0.004f, 3, Gy(0.3f), M(MAT_METAL_PAINTED));
    tube(b, top + vec3(w * 0.5f, 0, 0), top + vec3(w * 0.5f, 0, -0.5f), 0.004f, 3, Gy(0.3f), M(MAT_METAL_PAINTED));
    vec3 c = top - vec3(0, 0, 0.5f + h * 0.5f);
    box(b, c, vec3(w * 0.5f, 0.006f, h * 0.5f), bg, M(MAT_FABRIC), SK_NONE);
    tube(b, c + vec3(-w * 0.5f - 0.03f, 0, h * 0.5f), c + vec3(w * 0.5f + 0.03f, 0, h * 0.5f), 0.015f, 5, Gy(0.8f), M(MAT_CHROME), true);
    for (int s = -1; s <= 1; s += 2) {
        vec3 right(s < 0 ? 1.f : -1.f, 0.f, 0.f);
        textC(b, l1, c + vec3(0, s * 0.008f, h * 0.18f), right, vec3(0, 0, 1), w * 0.2f, w * 0.03f, fg, M(MAT_PAINT_WHITE));
        textC(b, l2, c + vec3(0, s * 0.008f, -h * 0.18f), right, vec3(0, 0, 1), w * 0.11f, w * 0.018f, fg, M(MAT_PAINT_WHITE));
    }
}

// Stylized palm logo (trunk arc and fronds) on a wall plane facing +y
void palmLogo(IB& b, vec3 c, float s, u32 col, u32 mat) {
    const int n = 6;
    vec3 prev = c + vec3(0.f, 0.f, -s);
    for (int k = 1; k <= n; k++) {
        float t = (float)k / n;
        vec3 p = c + vec3(sinf(t * 0.9f) * s * 0.35f - s * 0.1f, 0.f, -s + t * s);
        vec3 side(0.05f * s * (1.2f - t * 0.6f), 0.f, 0.f);
        quadF(b, prev - side, prev + side, p + side * 0.9f, p - side * 0.9f, vec3(0, 1, 0), col, mat);
        prev = p;
    }
    for (int k = 0; k < 6; k++) {
        float a = kPi * (0.08f + k * 0.17f);
        vec3 d(cosf(a), 0.f, sinf(a) * 0.55f - 0.15f);
        vec3 tip = prev + d * s * 0.62f, mid = prev + d * s * 0.3f + vec3(0, 0, 0.1f * s);
        vec3 w(-d.z * 0.09f * s, 0.f, d.x * 0.09f * s);
        tri(b, prev, mid - w, tip, col, mat);
        tri(b, prev, tip, mid + w, col, mat);
        tri(b, prev, mid + w, tip, col, mat);
        tri(b, prev, tip, mid - w, col, mat);
    }
}

// Glazed partition along x at y = yc (x0..x1, height h) with a hinged glass door at doorX, drawn on both sides;
// registers the wall holes (tExtraHoles) for the two rooms and its collision
void glassPartition(IB& b, int front, int back, float x0, float x1, float yc, float h, float doorX, float doorW, u32 frame) {
    const InteriorDef& d = *b.d;
    const InteriorRoom& rf = d.rooms[front];
    const InteriorRoom& rb = d.rooms[back];
    tExtraHoles.push_back({front, 2, {x0 - rf.mn.x, x1 - rf.mn.x, 0.f, h}});
    tExtraHoles.push_back({back, 0, {rb.mx.x - x1, rb.mx.x - x0, 0.f, h}});
    door(b, vec3(doorX, yc, 0.f), vec2(1, 0), vec2(0, 1), doorW, 2.2f, DK_HINGED, 1, frame, false);
    partitionX(b, x0, x1, yc, 0.1f, h, {vec2(doorX - doorW * 0.5f, doorX + doorW * 0.5f)});
    if (!b.geo()) return;
    InPart ip(b, IP_SHELL);
    u32 fm = M(MAT_METAL_BRUSHED);
    int bays = Max(1, (int)roundf((x1 - x0) / 1.5f));
    for (int k = 0; k <= bays; k++) {
        float x = x0 + (x1 - x0) * k / bays;
        if (fabsf(x - doorX) < doorW * 0.5f + 0.02f) continue;
        box(b, vec3(x, yc, h * 0.5f), vec3(0.03f, 0.05f, h * 0.5f), frame, fm, SK_PZ | SK_NZ);
    }
    for (float x : {doorX - doorW * 0.5f - 0.03f, doorX + doorW * 0.5f + 0.03f}) box(b, vec3(x, yc, h * 0.5f), vec3(0.03f, 0.05f, h * 0.5f), frame, fm, SK_PZ | SK_NZ);
    box(b, vec3((x0 + x1) * 0.5f, yc, h - 0.03f), vec3((x1 - x0) * 0.5f, 0.05f, 0.03f), frame, fm, SK_NONE);
    box(b, vec3(doorX, yc, 2.25f), vec3(doorW * 0.5f + 0.06f, 0.05f, 0.05f), frame, fm, SK_NONE);
    auto pane = [&](float xa, float xb, float za, float zb) {
        if (xb - xa < 0.03f) return;
        quadF(b, vec3(xa, yc, za), vec3(xb, yc, za), vec3(xb, yc, zb), vec3(xa, yc, zb), vec3(0, -1, 0), glassCol(0.9f), kGlassMat);
        quadF(b, vec3(xa, yc, za), vec3(xb, yc, za), vec3(xb, yc, zb), vec3(xa, yc, zb), vec3(0, 1, 0), glassCol(0.9f), kGlassMat);
    };
    float da = doorX - doorW * 0.5f - 0.06f, db = doorX + doorW * 0.5f + 0.06f;
    pane(x0, Max(x0, da), 0.f, h);
    pane(Min(x1, db), x1, 0.f, h);
    pane(da, db, 2.3f, h);
    box(b, vec3((x0 + x1) * 0.5f, yc, 0.05f), vec3((x1 - x0) * 0.5f, 0.05f, 0.05f), frame, fm, SK_NZ);
}

// Tire changer machine (front +y): turntable with jaws, the mount head on its column, pedals
void tireChanger(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    u32 red = C(0.75f, 0.08f, 0.06f), pm = M(MAT_METAL_PAINTED);
    rbox(b, vec3(0.f, 0.f, 0.4f), vec3(0.45f, 0.4f, 0.4f), 0.03f, red, pm, true);
    cyl(b, vec3(0.f, 0.05f, 0.8f), 0.4f, 0.4f, 0.05f, 20, Gy(0.2f), pm, true, false);
    for (int k = 0; k < 4; k++) {
        float a = kHalfPi * k;
        box(b, vec3(cosf(a) * 0.3f, 0.05f + sinf(a) * 0.3f, 0.87f), vec3(0.04f, 0.04f, 0.03f), Gy(0.7f), M(MAT_CHROME), SK_NZ);
    }
    box(b, vec3(0.f, -0.45f, 1.0f), vec3(0.1f, 0.1f, 0.6f), red, pm, SK_NZ);
    tube(b, vec3(0.f, -0.45f, 1.55f), vec3(0.f, 0.05f, 1.55f), 0.05f, 8, Gy(0.3f), pm, true);
    tube(b, vec3(0.f, 0.05f, 1.55f), vec3(0.f, 0.05f, 1.05f), 0.03f, 8, Gy(0.7f), M(MAT_CHROME), true);
    for (int k = 0; k < 3; k++) box(b, vec3(-0.2f + k * 0.2f, 0.42f, 0.04f), vec3(0.06f, 0.08f, 0.03f), Gy(0.15f), pm, SK_NZ);
    collide(b, vec3(0.f, 0.f, 0.45f), vec3(0.45f, 0.45f, 0.45f));
}

// Wheel balancer (front +y): cabinet, shaft with a wheel, hood, display
void wheelBalancer(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    u32 blue = C(0.1f, 0.25f, 0.55f), pm = M(MAT_METAL_PAINTED);
    rbox(b, vec3(0.f, 0.f, 0.5f), vec3(0.35f, 0.3f, 0.5f), 0.03f, blue, pm, true);
    tube(b, vec3(0.35f, 0.f, 0.65f), vec3(0.6f, 0.f, 0.65f), 0.03f, 8, Gy(0.7f), M(MAT_CHROME), true);
    rimWheel(b, vec3(0.62f, 0.f, 0.65f), 0.f, 0.33f, Gy(0.8f), 5);
    b.pushAxes(vec3(0.05f, -0.18f, 1.2f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.5f)), normalize(vec3(0, -0.5f, 1.f)));
    rbox(b, vec3(0.f), vec3(0.2f, 0.02f, 0.13f), 0.01f, Gy(0.1f), M(MAT_PLASTIC), true);
    box(b, vec3(0.f, 0.021f, 0.f), vec3(0.17f, 0.001f, 0.1f), C(0.3f, 0.9f, 0.4f, 0.5f), EM(), SK_NZ);
    b.pop();
    collide(b, vec3(0.1f, 0.f, 0.5f), vec3(0.45f, 0.3f, 0.5f));
}

// Paint booth: glass walls on steel posts, lit filter ceiling, floor grating, extraction housing (open at -y)
void paintBooth(IB& b, float x0, float x1, float y0, float y1, float h, int roomIdx) {
    InPart ip(b, IP_SHELL);
    u32 steel = Gy(0.85f), sm = M(MAT_METAL_PAINTED);
    // floor grating
    box(b, vec3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0.02f), vec3((x1 - x0) * 0.5f, (y1 - y0) * 0.5f, 0.02f), Gy(0.25f), M(MAT_METAL_BRUSHED), SK_NZ);
    for (float x = x0 + 0.5f; x < x1 - 0.2f; x += 0.5f) box(b, vec3(x, (y0 + y1) * 0.5f, 0.041f), vec3(0.01f, (y1 - y0) * 0.5f - 0.1f, 0.002f), Gy(0.1f), sm, SK_NZ);
    // posts, roof frame and the filter ceiling
    for (float x : {x0, x1})
        for (float y = y0; y <= y1 + 0.01f; y += (y1 - y0) / 3.f) {
            box(b, vec3(x, y, h * 0.5f), vec3(0.06f, 0.06f, h * 0.5f), steel, sm, SK_NZ);
            collide(b, vec3(x, y, h * 0.5f), vec3(0.06f, 0.06f, h * 0.5f));
        }
    box(b, vec3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, h + 0.25f), vec3((x1 - x0) * 0.5f + 0.08f, (y1 - y0) * 0.5f + 0.08f, 0.25f), steel, sm, SK_NONE);
    b.pushAxes(vec3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, h - 0.005f), vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 0, -1));
    float hx = (x1 - x0) * 0.5f - 0.1f, hy = (y1 - y0) * 0.5f - 0.1f;
    quad(b, vec3(-hx, -hy, 0.f), vec3(hx, -hy, 0.f), vec3(hx, hy, 0.f), vec3(-hx, hy, 0.f), vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), C(1.f, 0.98f, 0.95f, 0.22f), EM());
    b.pop();
    // glass side and back walls
    auto glass = [&](vec3 a, vec3 c, vec3 facing) {
        quadF(b, a, c, c + vec3(0, 0, h - 0.2f), a + vec3(0, 0, h - 0.2f), facing, glassCol(0.85f), kGlassMat);
        quadF(b, a, c, c + vec3(0, 0, h - 0.2f), a + vec3(0, 0, h - 0.2f), -facing, glassCol(0.85f), kGlassMat);
    };
    glass(vec3(x0, y0, 0.1f), vec3(x0, y1, 0.1f), vec3(-1, 0, 0));
    glass(vec3(x1, y0, 0.1f), vec3(x1, y1, 0.1f), vec3(1, 0, 0));
    glass(vec3(x0, y1, 0.1f), vec3(x1, y1, 0.1f), vec3(0, 1, 0));
    collideMM(b, vec3(x0 - 0.05f, y0, 0.f), vec3(x0 + 0.05f, y1, h));
    collideMM(b, vec3(x1 - 0.05f, y0, 0.f), vec3(x1 + 0.05f, y1, h));
    collideMM(b, vec3(x0, y1 - 0.05f, 0.f), vec3(x1, y1 + 0.05f, h));
    // exhaust filter bank on the back wall
    box(b, vec3((x0 + x1) * 0.5f, y1 - 0.2f, 0.6f), vec3((x1 - x0) * 0.5f - 0.3f, 0.15f, 0.5f), Gy(0.55f), sm, SK_NZ);
    for (float x = x0 + 0.6f; x < x1 - 0.4f; x += 0.6f) box(b, vec3(x, y1 - 0.36f, 0.6f), vec3(0.26f, 0.005f, 0.4f), C(0.9f, 0.85f, 0.7f), M(MAT_FABRIC), SK_NONE);
    for (int k = 0; k < 2; k++) light(b, vec3(x0 + (x1 - x0) * (0.3f + k * 0.4f), (y0 + y1) * 0.5f, h - 0.3f), vec3(1.f, 0.99f, 0.96f) * 700.f, 7.f, roomIdx, vec3(0, 0, -1), 80.f, 55.f);
}

// Paint mixing rack: shelving with rows of colored cans, the mixing scale and a tint dispenser (front +y)
void paintRack(IB& b, vec3 p, float yaw, float len, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 sm = M(MAT_METAL_PAINTED);
    for (int e = -1; e <= 1; e += 2) box(b, vec3(e * len * 0.5f, 0.25f, 1.1f), vec3(0.025f, 0.25f, 1.1f), Gy(0.35f), sm, SK_NZ);
    for (int s = 0; s < 4; s++) {
        float z = 0.3f + s * 0.5f;
        box(b, vec3(0.f, 0.25f, z), vec3(len * 0.5f, 0.24f, 0.012f), Gy(0.5f), sm, SK_NONE);
        InPart ip(b, IP_DETAIL);
        for (float x = -len * 0.5f + 0.12f; x < len * 0.5f - 0.1f; x += 0.17f) can(b, vec3(x, 0.25f, z + 0.012f), 0.07f, 0.18f, C(hsv(r.f(), r.range(0.5f, 0.95f), r.range(0.4f, 0.95f))), 0);
    }
    collide(b, vec3(0.f, 0.25f, 1.1f), vec3(len * 0.5f, 0.25f, 1.1f));
}

// ------------------------------------------------------------------------------------------------ Palm Motors
void layoutDealership(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0xCA25u);
    const float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT, H = d.ceil;
    float dx0 = 0.f, dx1 = 0.f, dh = 2.2f;
    mainDoorOpening(d, dx0, dx1, dh);
    const float doorX = (dx0 + dx1) * 0.5f;
    float rollX = 1e9f;
    for (const InteriorOpening& op : d.openings)
        if (op.kind == OP_ROLLUP) rollX = (d.toLocal(vec3(op.a, op.z0)).x + d.toLocal(vec3(op.b, op.z0)).x) * 0.5f;
    const float yO = Y1 - Clamp((Y1 - Y0) * 0.26f, 3.6f, 5.f);
    const float oH = Min(H, 3.1f);
    int show = room(b, vec3(X0, Y0, 0.f), vec3(X1, yO - 0.05f, H), vec3(60.f, 60.f, 62.f), 0.3f, LS_BUSINESS);
    int office = room(b, vec3(X0, yO + 0.05f, 0.f), vec3(X1, Y1, oH), vec3(40.f, 40.f, 40.f), 0.05f, LS_BUSINESS);
    storefrontEntrance(b, DK_SLIDING_PAIR, 1, Gy(0.75f));
    rollupDoors(b, Gy(0.82f));
    shopSign(b, "PALM MOTORS", C(0.03f, 0.22f, 0.18f), Gy(0.98f), dh + 0.8f);
    const float pdX = Clamp(doorX > (X0 + X1) * 0.5f ? X0 + 2.2f : X1 - 2.2f, X0 + 1.2f, X1 - 1.2f);
    glassPartition(b, show, office, X0, X1, yO, oH, pdX, 1.0f, Gy(0.7f));
    // ---- shells: polished white floor, white walls with a green brand accent, office carpet
    ShellStyle ss;
    ss.wallCol = Gy(0.93f);
    ss.floorMat = M(MAT_TILE);
    ss.floorCol = Gy(0.9f);
    ss.floorUV = 0.33f;
    ss.ceilMat = M(MAT_PLASTER);
    ss.ceilCol = Gy(0.95f);
    ss.baseH = 0.1f;
    ss.baseCol = Gy(0.3f);
    ss.revealCol = Gy(0.7f);
    shell(b, show, ss);
    ShellStyle os;
    os.wallCol = C(0.86f, 0.87f, 0.84f);
    os.floorMat = M(MAT_CARPET);
    os.floorCol = C(0.22f, 0.26f, 0.28f);
    os.ceilMat = M(MAT_CEILING_TILE);
    os.ceilCol = Gy(0.9f);
    shell(b, office, os);
    // ---- cars: the hero on its podium, more on the floor clear of the doors
    const float sy0 = Y0 + 1.2f, sy1 = yO - 1.2f;
    std::vector<vec3> cars;   // x, y, yaw
    auto clearOf = [&](float x, float y, float rad) {
        if (fabsf(x - doorX) < 2.2f + rad && y < Y0 + 4.5f) return false;
        if (rollX < 1e8f && fabsf(x - rollX) < 2.4f + rad) return false;
        if (x - rad < X0 + 0.4f || x + rad > X1 - 0.4f || y - rad < sy0 - 0.8f || y + rad > sy1 + 0.6f) return false;
        for (const vec3& c : cars)
            if (length(vec2(c.x, c.y) - vec2(x, y)) < 5.2f) return false;
        return true;
    };
    bool hero = false;
    for (int k = 0; k < 7 && !hero; k++) {
        float x = Lerp(X0 + 3.4f, X1 - 3.4f, hash01((float)k, 1.3f, (float)d.seed));
        float y = Lerp(sy0 + 2.5f, sy1 - 2.5f, 0.45f);
        if (!clearOf(x, y, 2.9f)) continue;
        carPodium(b, vec3(x, y, 0.f), 2.9f, 0.22f, show);
        displayCar(b, vec3(x, y, 0.22f), 0.55f);
        cars.push_back(vec3(x, y, 0.55f));
        hero = true;
        light(b, vec3(x, y - 2.2f, H - 0.2f), vec3(1.f, 0.96f, 0.9f) * 1400.f, 9.f, show, normalize(vec3(0.f, 2.2f, -(H - 1.2f))), 30.f, 15.f);
    }
    for (int k = 0; k < 24 && cars.size() < 4; k++) {
        float x = r.range(X0 + 2.6f, X1 - 2.6f), y = r.range(sy0 + 1.4f, sy1 - 1.4f);
        if (!clearOf(x, y, 2.4f)) continue;
        float yaw = (r.chance(0.5f) ? 1.f : -1.f) * r.range(0.35f, 0.8f) + (r.chance(0.3f) ? kPi : 0.f);
        displayCar(b, vec3(x, y, 0.f), yaw);
        cars.push_back(vec3(x, y, yaw));
        light(b, vec3(x, y, H - 0.2f), vec3(1.f, 0.96f, 0.9f) * 900.f, 8.f, show, vec3(0, 0, -1), 40.f, 22.f);
    }
    for (size_t k = 0; k < cars.size(); k++) {
        vec3 c = cars[k];
        float a = hash01(c.x, c.y) * kTwoPi;
        scenario(b, vec3(c.x + cosf(a) * 2.2f, c.y + sinf(a) * 2.6f, 0.f), a + kHalfPi + kPi * 0.5f, k == 0 ? 14 : 7, SR_SHOPPER, SF_DAY | SF_OPTIONAL);
    }
    // ---- brand wall: a solid panel in the middle of the office glazing with the palm and the name
    {
        float bw = Min(6.f, (X1 - X0) * 0.4f), bx = (X0 + X1) * 0.5f;
        if (fabsf(bx - pdX) < bw * 0.5f + 0.9f) bx = pdX < bx ? pdX + bw * 0.5f + 0.9f : pdX - bw * 0.5f - 0.9f;
        InPart ip(b, IP_SHELL);
        box(b, vec3(bx, yO - 0.12f, oH * 0.5f + 0.05f), vec3(bw * 0.5f, 0.07f, oH * 0.5f + 0.05f), C(0.03f, 0.22f, 0.18f), M(MAT_PAINT_WHITE), SK_NZ);
        collide(b, vec3(bx, yO - 0.12f, oH * 0.5f), vec3(bw * 0.5f, 0.07f, oH * 0.5f));
        At at(b, vec3(bx, yO - 0.19f, 0.f), kPi);   // local +y toward the showroom
        palmLogo(b, vec3(-bw * 0.28f, 0.012f, oH * 0.62f), 0.7f, C(0.95f, 0.8f, 0.4f), M(MAT_CHROME));
        textC(b, "PALM", vec3(bw * 0.12f, 0.02f, oH * 0.68f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.42f, 0.05f, Gy(0.98f), M(MAT_PAINT_WHITE), 0.03f);
        textC(b, "MOTORS", vec3(bw * 0.12f, 0.02f, oH * 0.5f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.2f, 0.028f, C(0.95f, 0.8f, 0.4f), M(MAT_PAINT_WHITE), 0.02f);
        light(b, vec3(0.f, 1.6f, H - 0.3f), vec3(1.f, 0.95f, 0.85f) * 500.f, 6.f, show, normalize(vec3(0.f, -1.f, -1.2f)), 45.f, 25.f);
    }
    // ---- sales desks along the side wall away from the entrance; reception counter by the door; lounge
    const float sx = doorX > (X0 + X1) * 0.5f ? X0 + 1.3f : X1 - 1.3f, sgn = doorX > (X0 + X1) * 0.5f ? 1.f : -1.f;
    for (int k = 0; k < 2; k++) {
        float y = sy0 + 2.f + k * 3.2f;
        if (y > sy1 - 1.f) break;
        bool blocked = false;
        for (const vec3& c : cars)
            if (fabsf(c.x - sx) < 3.6f && fabsf(c.y - y) < 3.4f) blocked = true;
        if (blocked) continue;
        officeDesk(b, vec3(sx, y, 0.f), sgn > 0.f ? -kHalfPi : kHalfPi, r.next(), false);
        officeChair(b, vec3(sx - sgn * 0.75f, y, 0.f), sgn > 0.f ? -kHalfPi : kHalfPi, C(0.1f, 0.1f, 0.12f));
        chair(b, vec3(sx + sgn * 0.85f, y - 0.35f, 0.f), sgn > 0.f ? kHalfPi : -kHalfPi, C(0.1f, 0.3f, 0.26f), 1, true);
        chair(b, vec3(sx + sgn * 0.85f, y + 0.35f, 0.f), sgn > 0.f ? kHalfPi : -kHalfPi, C(0.1f, 0.3f, 0.26f), 1, true);
        scenario(b, vec3(sx - sgn * 0.75f, y, 0.f), sgn > 0.f ? -kHalfPi : kHalfPi, 6, SR_CLERK, SF_STAFF | SF_DAY);
        scenario(b, vec3(sx + sgn * 0.85f, y + 0.35f, 0.f), sgn > 0.f ? kHalfPi : -kHalfPi, 6, SR_SHOPPER, SF_OPTIONAL | SF_DAY);
    }
    {
        // reception counter beside the entrance, facing the door
        float cx = Clamp(doorX + sgn * 3.2f, X0 + 1.6f, X1 - 1.6f), cy = Y0 + 3.2f;
        bool ok = true;
        for (const vec3& c : cars)
            if (fabsf(c.x - cx) < 3.3f && fabsf(c.y - cy) < 3.6f) ok = false;
        if (!ok) cy = Min(sy1 - 0.8f, Y0 + 2.0f);
        At at(b, vec3(cx, cy, 0.f), 0.f);
        InPart ip(b, IP_FURNITURE);
        rbox(b, vec3(0.f, 0.f, 0.55f), vec3(1.2f, 0.35f, 0.55f), 0.03f, Gy(0.95f), M(MAT_PAINT_WHITE), true);
        box(b, vec3(0.f, -0.352f, 0.55f), vec3(1.15f, 0.002f, 0.08f), C(0.03f, 0.3f, 0.24f), M(MAT_PAINT_WHITE), SK_NONE);
        rbox(b, vec3(0.f, 0.05f, 1.12f), vec3(1.25f, 0.42f, 0.02f), 0.01f, C(0.15f, 0.15f, 0.16f), M(MAT_MARBLE), true);
        collide(b, vec3(0.f, 0.f, 0.57f), vec3(1.25f, 0.42f, 0.57f));
        officeChair(b, vec3(0.f, 0.85f, 0.f), kPi, C(0.1f, 0.1f, 0.12f));
        {
            At at2(b, vec3(0.3f, 0.2f, 1.14f), 0.f);
            rbox(b, vec3(0.f, 0.f, 0.2f), vec3(0.24f, 0.02f, 0.15f), 0.01f, Gy(0.07f), M(MAT_PLASTIC), true);
            box(b, vec3(0.f, 0.021f, 0.2f), vec3(0.22f, 0.001f, 0.13f), C(0.3f, 0.55f, 0.9f, 0.4f), EM(), SK_NZ);
        }
        for (int k = 0; k < 3; k++) box(b, vec3(-0.7f + k * 0.12f, -0.2f, 1.145f + k * 0.001f), vec3(0.1f, 0.14f, 0.002f), C(hsv(0.4f + k * 0.1f, 0.4f, 0.9f)), M(MAT_PAINT_WHITE), SK_NZ);
        marker(b, IM_COUNTER, vec3(0.f, -0.95f, 0.f), 0.f);
        scenario(b, vec3(0.f, 0.85f, 0.f), kPi, 6, SR_CLERK, SF_STAFF);
    }
    {
        // customer lounge in the far front corner: sofa, table, water, a palm
        float lx = sgn > 0.f ? X1 - 1.4f : X0 + 1.4f, ly = Y0 + 1.6f;
        bool ok = fabsf(lx - rollX) > 3.5f;
        for (const vec3& c : cars)
            if (fabsf(c.x - lx) < 3.4f && fabsf(c.y - ly) < 3.4f) ok = false;
        if (ok) {
            InPart ip(b, IP_FURNITURE);
            sofa(b, vec3(lx, ly + 0.9f, 0.f), sgn > 0.f ? kHalfPi : -kHalfPi, 2.0f, C(0.8f, 0.78f, 0.72f), r.next());
            coffeeTable(b, vec3(lx - sgn * 1.25f, ly + 0.9f, 0.f), kHalfPi, 1.0f, 0.55f, r.next());
            waterCooler(b, vec3(lx, ly + 2.5f, 0.f));
            scenario(b, vec3(lx, ly + 0.6f, 0.f), sgn > 0.f ? kHalfPi : -kHalfPi, 6, SR_SHOPPER, SF_OPTIONAL);
        }
    }
    planter(b, vec3(X0 + 0.6f, yO - 0.7f, 0.f), 1.9f, r.next());
    planter(b, vec3(X1 - 0.6f, yO - 0.7f, 0.f), 1.9f, r.next());
    {
        // rims on the side wall by the door
        float wx = sgn > 0.f ? X0 : X1;
        At at(b, vec3(wx, (sy0 + sy1) * 0.5f, 0.f), sgn > 0.f ? -kHalfPi : kHalfPi);
        InPart ip(b, IP_FURNITURE);
        box(b, vec3(0.f, 0.03f, 1.6f), vec3(1.6f, 0.03f, 0.75f), C(0.12f, 0.12f, 0.13f), M(MAT_METAL_PAINTED), SK_NY);
        for (int k = 0; k < 6; k++) {
            float x = -1.2f + (k % 3) * 1.2f, z = 1.25f + (k / 3) * 0.72f;
            rimWheel(b, vec3(x, 0.2f, z), -kHalfPi, 0.3f, k & 1 ? Gy(0.85f) : C(0.2f, 0.2f, 0.22f), 5 + (k % 3));
        }
    }
    // sale banners and track lights
    hangingBanner(b, vec3((X0 + X1) * 0.5f - (X1 - X0) * 0.25f, Y0 + 2.4f, H), 0.9f, 2.0f, C(0.03f, 0.3f, 0.24f), Gy(0.98f), "0%", "APR FINANCING");
    hangingBanner(b, vec3((X0 + X1) * 0.5f + (X1 - X0) * 0.25f, Y0 + 2.4f, H), 0.9f, 2.0f, C(0.85f, 0.65f, 0.15f), C(0.03f, 0.2f, 0.15f), "2026", "MODELS IN STOCK");
    {
        int nx = Max(2, (int)((X1 - X0) / 3.6f)), ny = Max(1, (int)((yO - Y0) / 3.6f));
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) troffer(b, vec3(X0 + (X1 - X0) * (i + 0.5f) / nx, Y0 + (yO - Y0) * (j + 0.5f) / ny, H), 1.2f, 0.6f, show, 520.f, vec3(1.f, 0.98f, 0.95f), 9.f);
    }
    // ---- back offices: finance desks, filing, a key cabinet
    {
        float oy = (yO + Y1) * 0.5f;
        int n = Max(1, (int)((X1 - X0 - 2.f) / 3.4f));
        for (int k = 0; k < n; k++) {
            float x = X0 + 1.7f + (X1 - X0 - 3.4f) * (n > 1 ? (float)k / (n - 1) : 0.5f);
            if (fabsf(x - pdX) < 1.4f) continue;
            officeDesk(b, vec3(x, oy + 0.4f, 0.f), 0.f, r.next(), false);
            officeChair(b, vec3(x, oy + 1.2f, 0.f), kPi, C(0.12f, 0.12f, 0.14f));
            chair(b, vec3(x - 0.35f, oy - 0.5f, 0.f), 0.f, C(0.1f, 0.3f, 0.26f), 1, true);
            if (k == 0) scenario(b, vec3(x, oy + 1.2f, 0.f), kPi, 6, SR_CLERK, SF_STAFF | SF_DAY);
        }
        InPart ip(b, IP_FURNITURE);
        At at(b, vec3(X1, Y1 - 0.9f, 0.f), kHalfPi);
        box(b, vec3(0.f, 0.05f, 1.5f), vec3(0.3f, 0.05f, 0.4f), C(0.4f, 0.3f, 0.2f), M(MAT_WOOD), SK_NY);
        for (int k = 0; k < 12; k++) {
            vec3 kp(-0.22f + (k % 4) * 0.15f, 0.11f, 1.75f - (k / 4) * 0.22f);
            box(b, kp, vec3(0.004f, 0.01f, 0.004f), Gy(0.7f), M(MAT_CHROME), SK_NONE);
            box(b, kp - vec3(0.f, 0.f, 0.05f), vec3(0.012f, 0.004f, 0.04f), k % 3 ? Gy(0.1f) : C(0.7f, 0.1f, 0.1f), M(MAT_PLASTIC), SK_NONE);
        }
        int nt = Max(1, (int)((X1 - X0) / 4.f));
        for (int i = 0; i < nt; i++) troffer(b, vec3(X0 + (X1 - X0) * (i + 0.5f) / nt, (yO + Y1) * 0.5f, oH), 1.2f, 0.6f, office, 380.f, vec3(1.f, 0.98f, 0.95f), 6.5f);
    }
    roomDressing(b, show, true, d.seed ^ 0x11u);
    roomDressing(b, office, true, d.seed ^ 0x12u);
}

// ------------------------------------------------------------------------------------------------ Tide Customs
void layoutModShop(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0x71DEu);
    const float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT, H = d.ceil;
    float dx0 = 0.f, dx1 = 0.f, dh = 2.2f;
    mainDoorOpening(d, dx0, dx1, dh);
    const float doorX = (dx0 + dx1) * 0.5f;
    float bayX = (X0 + X1) * 0.5f;
    for (const InteriorOpening& op : d.openings)
        if (op.kind == OP_ROLLUP) bayX = (d.toLocal(vec3(op.a, op.z0)).x + d.toLocal(vec3(op.b, op.z0)).x) * 0.5f;
    int shop = room(b, vec3(X0, Y0, 0.f), vec3(X1, Y1, H), vec3(34.f, 34.f, 33.f), 0.12f, LS_BUSINESS);
    storefrontEntrance(b, DK_HINGED, 1, C(0.1f, 0.45f, 0.5f));
    rollupDoors(b, C(0.12f, 0.5f, 0.55f));
    shopSign(b, "TIDE CUSTOMS", C(0.04f, 0.06f, 0.08f), C(0.2f, 0.9f, 0.95f), dh + 2.6f);
    ShellStyle gs;
    gs.wallCol = C(0.84f, 0.85f, 0.84f);
    gs.wallMat = M(MAT_CONCRETE_PANEL);
    gs.floorMat = M(MAT_TILE);
    gs.floorCol = C(0.42f, 0.44f, 0.46f);
    gs.floorUV = 0.5f;
    gs.ceilMat = M(MAT_ROOF_METAL);
    gs.ceilCol = Gy(0.6f);
    gs.wainscotH = 1.1f;
    gs.wainCol = C(0.08f, 0.35f, 0.4f);
    gs.wainMat = M(MAT_METAL_PAINTED);
    gs.baseH = 0.f;
    gs.revealCol = Gy(0.5f);
    shell(b, shop, gs);
    const float side = bayX > (X0 + X1) * 0.5f ? -1.f : 1.f;   // work areas on the side away from the drive-in bay
    // ---- the drive-in bay: yellow lane markings from the roll-up door, a car waiting for its new paint
    {
        InPart ip(b, IP_SHELL);
        for (int e = -1; e <= 1; e += 2) box(b, vec3(bayX + e * 1.6f, (Y0 + Y1) * 0.35f, 0.003f), vec3(0.05f, (Y1 - Y0) * 0.33f, 0.003f), C(0.95f, 0.75f, 0.1f), M(MAT_PAINT_YELLOW), SK_NZ);
    }
    displayCar(b, vec3(bayX, Y0 + Clamp((Y1 - Y0) * 0.32f, 4.f, 6.f), 0.f), r.chance(0.5f) ? 0.f : kPi);
    // ---- paint booth (or a second lift when the shop is narrow)
    const float boothW = 5.2f, boothL = Min(7.6f, Y1 - Y0 - 2.4f);
    float bx0 = side > 0.f ? bayX + 2.6f : bayX - 2.6f - boothW, bx1 = bx0 + boothW;
    bool booth = bx0 > X0 + 0.3f && bx1 < X1 - 0.3f && boothL > 5.5f;
    if (booth) {
        float by1 = Y1 - 0.4f, by0 = by1 - boothL;
        paintBooth(b, bx0, bx1, by0, by1, Min(3.4f, H - 0.8f), shop);
        displayCar(b, vec3((bx0 + bx1) * 0.5f, (by0 + by1) * 0.5f + 0.3f, 0.04f), kPi);
        scenario(b, vec3(bx0 + 0.8f, by0 + 1.5f, 0.f), -kHalfPi * side, 14, SR_MECHANIC, SF_STAFF);
        {
            InPart ip(b, IP_FURNITURE);
            // spray gun on a hose reel by the booth entrance
            At at(b, vec3(side > 0.f ? bx1 + 0.35f : bx0 - 0.35f, by0 + 0.4f, 0.f), 0.f);
            cyl(b, vec3(0.f, 0.f, 1.2f), 0.22f, 0.22f, 0.08f, 16, C(0.9f, 0.2f, 0.1f), M(MAT_METAL_PAINTED), true, true);
            box(b, vec3(0.f, 0.f, 0.6f), vec3(0.03f, 0.03f, 0.6f), Gy(0.3f), M(MAT_METAL_PAINTED), SK_NZ);
        }
    } else {
        float lx = side > 0.f ? Min(bayX + 5.2f, X1 - 2.3f) : Max(bayX - 5.2f, X0 + 2.3f);
        if (fabsf(lx - bayX) > 4.4f) {
            carLift(b, vec3(lx, Y1 - 3.5f, 0.f), 0.f, 1.6f, C(0.1f, 0.45f, 0.5f));
            marker(b, IM_CAR, vec3(lx, Y1 - 3.5f, 1.6f - 0.2f), kPi);
            scenario(b, vec3(lx + 1.2f, Y1 - 5.8f, 0.f), 0.4f, 14, SR_MECHANIC, SF_STAFF);
        }
    }
    // ---- rim wall under the neon logo on the wall across from the booth side
    {
        float wx = side > 0.f ? X0 : X1;
        float wy = (Y0 + Y1) * 0.55f;
        At at(b, vec3(wx, wy, 0.f), side > 0.f ? -kHalfPi : kHalfPi);
        InPart ip(b, IP_FURNITURE);
        box(b, vec3(0.f, 0.03f, 1.75f), vec3(2.1f, 0.03f, 0.95f), C(0.06f, 0.06f, 0.07f), M(MAT_METAL_PAINTED), SK_NY);
        for (int k = 0; k < 8; k++) {
            float x = -1.55f + (k % 4) * 1.03f, z = 1.25f + (k / 4) * 0.85f;
            rimWheel(b, vec3(x, 0.19f, z), -kHalfPi, 0.3f, k % 3 == 0 ? C(0.85f, 0.65f, 0.2f) : (k % 3 == 1 ? Gy(0.85f) : Gy(0.12f)), 5 + (k & 1));
        }
        textC(b, "TIDE CUSTOMS", vec3(0.f, 0.07f, 3.05f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.34f, 0.03f, C(0.2f, 0.95f, 1.f, 0.9f), EM(4, 20), 0.f);
        light(b, vec3(0.f, 0.8f, 3.0f), vec3(0.2f, 0.9f, 1.f) * 70.f, 5.f, shop);
        tireChanger(b, vec3(-1.2f, 1.6f, 0.f), 0.f);
        wheelBalancer(b, vec3(1.3f, 1.4f, 0.f), 0.f);
        scenario(b, vec3(-1.2f, 2.4f, 0.f), kPi, 0, SR_MECHANIC, SF_STAFF | SF_OPTIONAL);
    }
    // ---- back wall: paint rack, benches, tool chests; compressor in the corner
    {
        float wx0 = side > 0.f ? X0 + 0.4f : (booth ? Max(bayX + 2.4f, X0 + 0.4f) : X0 + 0.4f);
        float wx1 = side > 0.f ? (booth ? Min(bayX - 2.4f, X1 - 0.4f) : X1 - 0.4f) : X1 - 0.4f;
        if (booth) {
            if (side > 0.f) wx1 = Min(wx1, bx0 - 0.5f);
            else wx0 = Max(wx0, bx1 + 0.5f);
        }
        float len = wx1 - wx0;
        if (len > 2.4f) {
            At at(b, vec3(wx0 + Min(2.4f, len * 0.5f) * 0.5f + 0.1f, Y1, 0.f), kPi);
            paintRack(b, vec3(0.f), 0.f, Min(2.4f, len * 0.5f), r.next());
        }
        if (len > 4.2f) workbench(b, vec3(wx1 - 1.1f, Y1, 0.f), kPi, 2.0f, r.next());
        toolChest(b, vec3(Clamp(bayX - side * 2.8f, X0 + 0.8f, X1 - 0.8f), Y1 - 1.2f, 0.f), kPi, C(0.75f, 0.08f, 0.06f));
        compressor(b, vec3(side > 0.f ? X0 + 0.6f : X1 - 0.6f, Y1 - 0.6f, 0.f));
        oilDrum(b, vec3(side > 0.f ? X0 + 0.5f : X1 - 0.5f, Y0 + 5.2f, 0.f), C(0.1f, 0.4f, 0.45f));
    }
    // ---- customer counter by the pedestrian door, a bench and a vending machine
    {
        float cx = Clamp(doorX + (doorX > bayX ? 1.9f : -1.9f), X0 + 1.2f, X1 - 1.2f);
        if (fabsf(cx - bayX) < 3.f) cx = doorX;
        float cy = Y0 + 2.4f;
        At at(b, vec3(cx, cy, 0.f), 0.f);
        InPart ip(b, IP_FURNITURE);
        box(b, vec3(0.f, 0.f, 0.52f), vec3(0.9f, 0.3f, 0.52f), C(0.06f, 0.3f, 0.34f), M(MAT_METAL_PAINTED), SK_NZ);
        rbox(b, vec3(0.f, 0.02f, 1.06f), vec3(0.95f, 0.36f, 0.02f), 0.01f, Gy(0.15f), M(MAT_MARBLE), true);
        collide(b, vec3(0.f, 0.f, 0.54f), vec3(0.95f, 0.36f, 0.54f));
        {
            At at2(b, vec3(0.3f, 0.1f, 1.08f), 0.f);
            rbox(b, vec3(0.f, 0.f, 0.18f), vec3(0.22f, 0.02f, 0.14f), 0.01f, Gy(0.07f), M(MAT_PLASTIC), true);
            box(b, vec3(0.f, 0.021f, 0.18f), vec3(0.2f, 0.001f, 0.12f), C(0.2f, 0.8f, 0.9f, 0.35f), EM(), SK_NZ);
        }
        box(b, vec3(-0.5f, -0.1f, 1.1f), vec3(0.15f, 0.1f, 0.02f), Gy(0.9f), M(MAT_PAINT_WHITE), SK_NZ);
        marker(b, IM_COUNTER, vec3(0.f, -0.9f, 0.f), 0.f);
        scenario(b, vec3(0.f, 0.75f, 0.f), kPi, 0, SR_CLERK, SF_STAFF);
    }
    // ---- lights: shop lamps over the floor, strip over the bay
    {
        int nx = Max(2, (int)((X1 - X0) / 4.f)), ny = Max(2, (int)((Y1 - Y0) / 4.5f));
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                vec3 lp(X0 + (X1 - X0) * (i + 0.5f) / nx, Y0 + (Y1 - Y0) * (j + 0.5f) / ny, H);
                if (booth && lp.x > bx0 - 0.3f && lp.x < bx1 + 0.3f && lp.y > Y1 - boothL - 0.8f) continue;
                shopLamp(b, lp, Min(1.2f, H - 3.2f), shop, 620.f, 9.f);
            }
    }
    roomDressing(b, shop, true, d.seed ^ 0x21u);
}

// ------------------------------------------------------------------------------------------------ Sunwash Car Wash
// Pay booth in a back corner (walls on its two open sides, pay window toward the front, door on the inner side)
void payBooth(IB& b, float x0, float x1, float y0, float y1, float h, float innerX, int roomIdx, u32 col) {
    const float t = 0.08f;
    u32 pm = M(MAT_METAL_PAINTED);
    const float wz0 = 1.0f, wz1 = 2.1f, wx0 = x0 + 0.5f, wx1 = x1 - 0.5f;
    const bool innerLeft = fabsf(innerX - x0) < fabsf(innerX - x1);
    const float dy = y0 + 1.1f;
    door(b, vec3(innerX, dy, 0.f), vec2(0, 1), vec2(innerLeft ? 1.f : -1.f, 0.f), 0.85f, 2.05f, DK_HINGED, 3, col, false);
    {
        InPart ip(b, IP_SHELL);
        // front wall with the pay window
        box(b, vec3((x0 + x1) * 0.5f, y0, wz0 * 0.5f), vec3((x1 - x0) * 0.5f, t, wz0 * 0.5f), col, pm, SK_NZ);
        box(b, vec3((x0 + x1) * 0.5f, y0, (wz1 + h) * 0.5f), vec3((x1 - x0) * 0.5f, t, (h - wz1) * 0.5f), col, pm, SK_NZ);
        box(b, vec3((x0 + wx0) * 0.5f, y0, (wz0 + wz1) * 0.5f), vec3((wx0 - x0) * 0.5f, t, (wz1 - wz0) * 0.5f), col, pm, SK_NONE);
        box(b, vec3((wx1 + x1) * 0.5f, y0, (wz0 + wz1) * 0.5f), vec3((x1 - wx1) * 0.5f, t, (wz1 - wz0) * 0.5f), col, pm, SK_NONE);
        quadF(b, vec3(wx0, y0, wz0), vec3(wx1, y0, wz0), vec3(wx1, y0, wz1), vec3(wx0, y0, wz1), vec3(0, -1, 0), glassCol(0.85f), kGlassMat);
        quadF(b, vec3(wx0, y0, wz0), vec3(wx1, y0, wz0), vec3(wx1, y0, wz1), vec3(wx0, y0, wz1), vec3(0, 1, 0), glassCol(0.85f), kGlassMat);
        box(b, vec3((wx0 + wx1) * 0.5f, y0 - 0.2f, wz0), vec3((wx1 - wx0) * 0.5f, 0.2f, 0.025f), Gy(0.85f), M(MAT_METAL_BRUSHED), SK_NONE);   // pay ledge
        // inner side wall with the door
        auto sideSeg = [&](float ya, float yb) {
            if (yb - ya > 0.02f) box(b, vec3(innerX, (ya + yb) * 0.5f, h * 0.5f), vec3(t, (yb - ya) * 0.5f, h * 0.5f), col, pm, SK_NZ);
        };
        sideSeg(y0, dy - 0.45f);
        sideSeg(dy + 0.45f, y1);
        box(b, vec3(innerX, dy, (2.05f + h) * 0.5f), vec3(t, 0.45f, (h - 2.05f) * 0.5f), col, pm, SK_NONE);
        box(b, vec3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, h + 0.03f), vec3((x1 - x0) * 0.5f + t, (y1 - y0) * 0.5f + t, 0.03f), Gy(0.9f), pm, SK_NONE);
        textC(b, "PAY HERE", vec3((x0 + x1) * 0.5f, y0 - t - 0.005f, wz1 + 0.3f), vec3(1, 0, 0), vec3(0, 0, 1), 0.16f, 0.022f, C(1.f, 0.85f, 0.2f, 0.8f), EM(), 0.f);
    }
    collideMM(b, vec3(x0, y0 - t, 0.f), vec3(x1, y0 + t, h));
    partitionY(b, y0, y1, innerX, 2.f * t, h, {vec2(dy - 0.45f, dy + 0.45f)});
    light(b, vec3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, h - 0.2f), vec3(1.f, 0.96f, 0.88f) * 160.f, 3.5f, roomIdx);
}

void layoutCarWash(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0x5A5Au);
    const float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT, H = d.ceil;
    float dx0 = 0.f, dx1 = 0.f, dh = 2.2f;
    mainDoorOpening(d, dx0, dx1, dh);
    const float doorX = (dx0 + dx1) * 0.5f;
    float bayX = (X0 + X1) * 0.5f;
    for (const InteriorOpening& op : d.openings)
        if (op.kind == OP_ROLLUP) bayX = (d.toLocal(vec3(op.a, op.z0)).x + d.toLocal(vec3(op.b, op.z0)).x) * 0.5f;
    const float ks = doorX > bayX ? 1.f : -1.f;   // booth and customer side = the pedestrian door's side
    int bay = room(b, vec3(X0, Y0, 0.f), vec3(X1, Y1, H), vec3(30.f, 32.f, 34.f), 0.15f, LS_BUSINESS);
    storefrontEntrance(b, DK_HINGED, 1, C(0.1f, 0.35f, 0.7f));
    rollupDoors(b, C(0.15f, 0.4f, 0.75f));
    shopSign(b, "SUNWASH", C(0.1f, 0.35f, 0.75f), C(1.f, 0.85f, 0.2f), dh + 2.6f);
    ShellStyle bs;
    bs.wallCol = C(0.8f, 0.86f, 0.9f);
    bs.wallMat = M(MAT_TILE);
    bs.floorMat = M(MAT_CONCRETE);
    bs.floorCol = C(0.45f, 0.47f, 0.5f);
    bs.floorUV = 0.5f;
    bs.ceilMat = M(MAT_ROOF_METAL);
    bs.ceilCol = Gy(0.7f);
    bs.baseH = 0.f;
    bs.wainscotH = 1.4f;
    bs.wainCol = C(0.1f, 0.35f, 0.75f);
    bs.wainMat = M(MAT_TILE);
    shell(b, bay, bs);
    // ---- wash bay: trench drains, guide rails, the gantry with brushes over a waiting car, spray arch and dryer
    const float wy0 = Y0 + 1.5f, wy1 = Y1 - 1.f, wyc = (wy0 + wy1) * 0.5f;
    {
        InPart ip(b, IP_SHELL);
        for (float y = wy0 + 1.f; y < wy1; y += 3.f) box(b, vec3(bayX, y, 0.002f), vec3(1.6f, 0.1f, 0.003f), Gy(0.15f), M(MAT_METAL_BRUSHED), SK_NZ);
        for (int e = -1; e <= 1; e += 2) box(b, vec3(bayX + e * 1.25f, wyc, 0.06f), vec3(0.08f, (wy1 - wy0) * 0.5f, 0.06f), C(0.95f, 0.8f, 0.1f), M(MAT_PAINT_YELLOW), SK_NZ);
    }
    displayCar(b, vec3(bayX, wyc - 0.4f, 0.f), 0.f);
    {
        InPart ip(b, IP_FURNITURE);
        u32 frame = C(0.1f, 0.35f, 0.75f), fm = M(MAT_METAL_PAINTED);
        float gy = wyc + 1.4f, gh = Min(3.1f, H - 0.3f);
        for (int e = -1; e <= 1; e += 2) {
            box(b, vec3(bayX + e * 1.9f, gy, gh * 0.5f), vec3(0.14f, 0.5f, gh * 0.5f), frame, fm, SK_NZ);
            collide(b, vec3(bayX + e * 1.9f, gy, gh * 0.5f), vec3(0.14f, 0.5f, gh * 0.5f));
            box(b, vec3(bayX + e * 1.9f, wyc, 0.03f), vec3(0.06f, (wy1 - wy0) * 0.5f, 0.03f), Gy(0.6f), M(MAT_METAL_BRUSHED), SK_NZ);
            // vertical brush: a core wrapped in colored cloth strips
            vec3 bc(bayX + e * 1.45f, gy - 0.1f, 0.15f);
            cyl(b, bc, 0.06f, 0.06f, gh - 0.5f, 8, Gy(0.6f), M(MAT_METAL_BRUSHED), true);
            for (int k = 0; k < 14; k++) {
                float a = kTwoPi * k / 14;
                vec3 dd(cosf(a), sinf(a), 0.f);
                u32 col = k % 3 == 0 ? C(1.f, 0.85f, 0.15f) : (k % 3 == 1 ? C(0.15f, 0.45f, 0.95f) : C(0.95f, 0.3f, 0.5f));
                quadF(b, bc + dd * 0.07f, bc + dd * 0.38f, bc + dd * 0.38f + vec3(0, 0, gh - 0.6f), bc + dd * 0.07f + vec3(0, 0, gh - 0.6f), vec3(-dd.y, dd.x, 0.f), col,
                      M(MAT_FABRIC));
                quadF(b, bc + dd * 0.07f, bc + dd * 0.38f, bc + dd * 0.38f + vec3(0, 0, gh - 0.6f), bc + dd * 0.07f + vec3(0, 0, gh - 0.6f), vec3(dd.y, -dd.x, 0.f), col,
                      M(MAT_FABRIC));
            }
        }
        box(b, vec3(bayX, gy, gh - 0.1f), vec3(2.05f, 0.5f, 0.12f), frame, fm, SK_NONE);
        textC(b, "SUNWASH", vec3(bayX, gy - 0.505f, gh - 0.1f), vec3(1, 0, 0), vec3(0, 0, 1), 0.14f, 0.02f, C(1.f, 0.85f, 0.2f), M(MAT_PAINT_WHITE));
        tube(b, vec3(bayX - 1.35f, gy + 0.2f, gh - 0.45f), vec3(bayX + 1.35f, gy + 0.2f, gh - 0.45f), 0.06f, 8, Gy(0.6f), M(MAT_METAL_BRUSHED), true);
        for (int k = 0; k < 16; k++) {
            float x = bayX - 1.3f + k * 0.173f;
            u32 col = k % 2 ? C(0.15f, 0.45f, 0.95f) : C(1.f, 0.85f, 0.15f);
            quadF(b, vec3(x, gy + 0.2f, gh - 0.45f), vec3(x + 0.12f, gy + 0.2f, gh - 0.45f), vec3(x + 0.12f, gy + 0.2f, gh - 0.95f), vec3(x, gy + 0.2f, gh - 0.95f), vec3(0, -1, 0),
                  col, M(MAT_FABRIC));
            quadF(b, vec3(x, gy + 0.2f, gh - 0.45f), vec3(x + 0.12f, gy + 0.2f, gh - 0.45f), vec3(x + 0.12f, gy + 0.2f, gh - 0.95f), vec3(x, gy + 0.2f, gh - 0.95f), vec3(0, 1, 0),
                  col, M(MAT_FABRIC));
        }
        float ay = wy0 + 0.8f;
        for (int e = -1; e <= 1; e += 2) {
            tube(b, vec3(bayX + e * 1.8f, ay, 0.f), vec3(bayX + e * 1.8f, ay, gh - 0.4f), 0.05f, 8, Gy(0.75f), M(MAT_METAL_BRUSHED));
            collide(b, vec3(bayX + e * 1.8f, ay, (gh - 0.4f) * 0.5f), vec3(0.06f, 0.06f, (gh - 0.4f) * 0.5f));
            for (float z = 0.4f; z < gh - 0.5f; z += 0.35f) sphere(b, vec3(bayX + e * 1.74f, ay, z), 0.035f, 6, C(0.2f, 0.5f, 1.f), M(MAT_PLASTIC));
        }
        tube(b, vec3(bayX - 1.8f, ay, gh - 0.4f), vec3(bayX + 1.8f, ay, gh - 0.4f), 0.05f, 8, Gy(0.75f), M(MAT_METAL_BRUSHED));
        rbox(b, vec3(bayX, ay + 0.1f, gh - 0.15f), vec3(1.3f, 0.35f, 0.15f), 0.05f, C(0.95f, 0.8f, 0.15f), fm, true);
        At at(b, vec3(bayX + 1.9f, Y1 - 0.3f, 1.8f), 0.f);   // stop / go signal at the far end
        rbox(b, vec3(0.f), vec3(0.14f, 0.1f, 0.3f), 0.02f, Gy(0.1f), fm, true);
        sphere(b, vec3(0.f, -0.1f, 0.12f), 0.07f, 10, C(1.f, 0.1f, 0.05f, 0.7f), EM(7, 0));
        sphere(b, vec3(0.f, -0.1f, -0.12f), 0.07f, 10, C(0.1f, 1.f, 0.2f, 0.7f), EM(7, 128));
    }
    {
        InPart ip(b, IP_DETAIL);   // soap suds on the floor
        Rng sr(d.seed + 3u);
        for (int k = 0; k < 40; k++) {
            vec3 p(bayX + sr.range(-1.8f, 1.8f), wyc + sr.range(-3.f, 3.f), 0.004f);
            if (fabsf(p.x - bayX) < 0.9f && fabsf(p.y - (wyc - 0.4f)) < 2.2f) continue;
            sphere(b, p, sr.range(0.03f, 0.09f), 6, Gy(0.97f), M(MAT_PAINT_WHITE), 0.4f);
        }
    }
    // ---- pay booth in the back corner on the customer side, a vacuum station and detailing supplies
    {
        float w = Min(3.2f, ks > 0.f ? X1 - (bayX + 2.6f) : (bayX - 2.6f) - X0);
        if (w > 2.2f) {
            float x0 = ks > 0.f ? X1 - w : X0, x1 = ks > 0.f ? X1 : X0 + w, y1 = Y1, y0 = Y1 - 3.6f;
            float innerX = ks > 0.f ? x0 : x1;
            float bh = Min(2.8f, H - 0.4f);
            payBooth(b, x0, x1, y0, y1, bh, innerX, bay, C(0.1f, 0.35f, 0.7f));
            InPart ip(b, IP_FURNITURE);
            At at(b, vec3((x0 + x1) * 0.5f, y0 + 0.45f, 0.f), 0.f);   // counter under the window, clerk behind it
            box(b, vec3(0.f, 0.f, 0.48f), vec3(w * 0.5f - 0.2f, 0.25f, 0.48f), Gy(0.85f), M(MAT_METAL_PAINTED), SK_NZ);
            box(b, vec3(-0.4f, 0.05f, 1.0f), vec3(0.16f, 0.12f, 0.06f), Gy(0.12f), M(MAT_PLASTIC), SK_NZ);
            officeChair(b, vec3(0.f, 0.9f, 0.f), kPi, C(0.1f, 0.1f, 0.12f));
            scenario(b, vec3(0.f, 0.9f, 0.f), kPi, 6, SR_CLERK, SF_STAFF);
            marker(b, IM_COUNTER, vec3(0.f, -1.0f, 0.f), 0.f);
        }
        float vx = ks > 0.f ? X1 - 0.6f : X0 + 0.6f;
        At at(b, vec3(vx, Y0 + 3.2f, 0.f), ks > 0.f ? kHalfPi : -kHalfPi);
        InPart ip(b, IP_FURNITURE);
        rbox(b, vec3(0.f, 0.f, 0.7f), vec3(0.35f, 0.3f, 0.7f), 0.05f, C(1.f, 0.85f, 0.15f), M(MAT_METAL_PAINTED), true);
        textC(b, "VACUUM", vec3(0.f, 0.305f, 1.2f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.08f, 0.012f, C(0.1f, 0.3f, 0.7f), M(MAT_PAINT_WHITE));
        tube(b, vec3(0.2f, 0.25f, 1.1f), vec3(0.5f, 0.9f, 0.3f), 0.035f, 6, Gy(0.2f), M(MAT_RUBBER));
        collide(b, vec3(0.f, 0.f, 0.7f), vec3(0.35f, 0.3f, 0.7f));
        storageShelf(b, vec3(1.9f, -0.3f, 0.f), 0.f, 1.4f, 1.8f, r.next());
    }
    scenario(b, vec3(bayX + 2.4f * ks, wyc + 2.5f, 0.f), -0.5f * ks, 14, SR_WORKER, SF_STAFF | SF_DAY);
    {
        int ny = Max(2, (int)((Y1 - Y0) / 4.f));
        for (int j = 0; j < ny; j++) troffer(b, vec3(bayX, Y0 + (Y1 - Y0) * (j + 0.5f) / ny, H), 1.8f, 0.4f, bay, 700.f, vec3(0.92f, 0.97f, 1.f), 9.f);
        troffer(b, vec3(bayX + ks * 3.4f, (Y0 + Y1) * 0.5f, H), 1.2f, 0.6f, bay, 420.f, vec3(1.f, 0.97f, 0.9f), 7.f);
    }
    roomDressing(b, bay, true, d.seed ^ 0x31u);
}

}  // namespace ikit
}  // namespace World
