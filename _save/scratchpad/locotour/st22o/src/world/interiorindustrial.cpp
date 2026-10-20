// Industrial interiors: Rook's garage / chop shop (roll-up doors, two-post lifts with cars, workbenches, tool chests,
// engine hoist, tire racks, stripped parts, glazed office) and the Port Isle warehouse (pallet racking aisles,
// forklifts, containers and crate stacks for cover, a mezzanine with stairs, catwalks and an office).
#include "interiors.h"
#include "sites.h"

namespace World {
namespace ikit {

// ------------------------------------------------------------------------------------------------ shared
// Roll-up doors for every OP_ROLLUP opening: shutter (dynamic), coil housing, guide rails, apron ramp, bollards
void rollupDoors(IB& b, u32 color) {
    const InteriorDef& d = *b.d;
    for (const InteriorOpening& op : d.openings) {
        if (op.kind != OP_ROLLUP) continue;
        vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
        float x0 = Min(a.x, c.x), x1 = Max(a.x, c.x), h = op.z1 - op.z0, xc = (x0 + x1) * 0.5f;
        door(b, vec3(xc, 0.f, 0.f), vec2(1, 0), vec2(0, 1), x1 - x0, h, DK_ROLLUP, 2, color, true, 0.16f);
        if (!b.geo()) continue;
        InPart ip(b, IP_SHELL);
        u32 steel = M(MAT_METAL_PAINTED);
        box(b, vec3(xc, kT + 0.25f, h + 0.28f), vec3((x1 - x0) * 0.5f + 0.15f, 0.25f, 0.28f), Gy(0.55f), steel, SK_NONE);   // coil housing
        for (float x : {x0 - 0.06f, x1 + 0.06f}) box(b, vec3(x, kT + 0.06f, h * 0.5f), vec3(0.06f, 0.06f, h * 0.5f), Gy(0.45f), steel, SK_NZ);
        box(b, vec3(xc, kT * 0.5f, -0.01f), vec3((x1 - x0) * 0.5f, kT * 0.5f, 0.01f), Gy(0.5f), M(MAT_METAL_BRUSHED), SK_NZ);
        // apron ramp from the street up to the floor, stepped collision so tires climb it
        vec3 outside = d.toWorld(vec3(xc, -2.6f, 0.f));
        float gz = gMap ? gMap->heightAt(outside.x, outside.y) : d.origin.z;
        float rz;
        if (gRoads && gRoads->surfaceHeight(outside.xy(), &rz, d.origin.z + 1.f) && rz > gz - 0.5f) gz = rz;
        float drop = Clamp(d.origin.z - gz, 0.f, 0.9f);
        if (drop > 0.03f) {
            float y0 = -Max(1.5f, drop * 6.f), w = (x1 - x0) * 0.5f + 0.4f;
            quadF(b, vec3(xc - w, y0, -drop), vec3(xc + w, y0, -drop), vec3(xc + w, 0.f, 0.f), vec3(xc - w, 0.f, 0.f), vec3(0, -drop, -y0), Gy(0.66f), M(MAT_CONCRETE));
            for (int e = -1; e <= 1; e += 2) {
                vec3 p0(xc + e * w, y0, -drop), p1(xc + e * w, 0.f, 0.f), p2(xc + e * w, 0.f, -drop - 0.2f), p3(xc + e * w, y0, -drop - 0.2f);
                quadF(b, p3, p2, p1, p0, vec3((float)e, 0, 0), Gy(0.6f), M(MAT_CONCRETE));
            }
            int ns = Max(2, (int)ceilf(drop / 0.06f));
            for (int k = 0; k < ns; k++) {
                float t0 = (float)k / ns, t1 = (float)(k + 1) / ns;
                float top = -drop + drop * t1;
                collideMM(b, vec3(xc - w, y0 * (1.f - t0), -drop - 0.4f), vec3(xc + w, y0 * (1.f - t1) + 0.001f, top));
            }
        }
        // bollards
        for (int e = -1; e <= 1; e += 2) {
            vec3 bp(e < 0 ? x0 - 0.45f : x1 + 0.45f, -0.35f, -drop);
            cyl(b, bp, 0.1f, 0.1f, 1.05f + drop, 10, C(0.95f, 0.75f, 0.05f), M(MAT_METAL_PAINTED), true);
            collide(b, bp + vec3(0, 0, (1.05f + drop) * 0.5f), vec3(0.1f, 0.1f, (1.05f + drop) * 0.5f));
        }
    }
}

// Industrial dome pendant hanging on a chain (+ light)
void shopLamp(IB& b, vec3 top, float drop, int roomIdx, float cd, float radius) {
    vec3 p = top - vec3(0, 0, drop);
    InPart ip(b, IP_FURNITURE);
    tube(b, top, p + vec3(0, 0, 0.25f), 0.006f, 4, Gy(0.3f), M(MAT_METAL_PAINTED));
    lathe(b, p, {vec2(0.32f, 0.f), vec2(0.28f, 0.1f), vec2(0.12f, 0.22f), vec2(0.06f, 0.26f)}, 16, Gy(0.3f), M(MAT_METAL_PAINTED), true);
    lathe(b, p + vec3(0, 0, -0.001f), {vec2(0.06f, 0.26f), vec2(0.12f, 0.22f), vec2(0.28f, 0.1f), vec2(0.32f, 0.f)}, 16, Gy(0.85f), M(MAT_METAL_BRUSHED), false);
    b.pushAxes(p + vec3(0, 0, 0.04f), vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 0, -1));
    disc(b, vec3(0.f), 0.2f, 14, C(1.f, 0.93f, 0.8f, 0.9f), EM());
    b.pop();
    light(b, p - vec3(0, 0, 0.1f), vec3(1.f, 0.93f, 0.8f) * cd, radius, roomIdx, vec3(0, 0, -1), 70.f, 40.f);
}

// Oil drum (standing)
void oilDrum(IB& b, vec3 p, u32 col) {
    cyl(b, p, 0.29f, 0.29f, 0.88f, 14, col, M(MAT_METAL_PAINTED), true);
    for (float z : {0.29f, 0.59f}) cyl(b, p + vec3(0, 0, z - 0.01f), 0.3f, 0.3f, 0.02f, 14, C(rgbOf(col) * 0.8f), M(MAT_METAL_PAINTED), false);
    disc(b, p + vec3(0.12f, 0.f, 0.881f), 0.03f, 8, Gy(0.3f), M(MAT_METAL_PAINTED));
    collide(b, p + vec3(0, 0, 0.44f), vec3(0.29f, 0.29f, 0.44f));
}

// Wooden crate (cube-ish) with plank lines
void crate(IB& b, vec3 p, float yaw, vec3 he, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 wood = C(vec3(0.62f, 0.5f, 0.34f) * r.range(0.8f, 1.1f));
    box(b, vec3(0, 0, he.z), he, wood, M(MAT_WOOD), SK_NZ);
    u32 dark = C(rgbOf(wood) * 0.75f);
    for (int s = -1; s <= 1; s += 2) {
        box(b, vec3(0, s * (he.y + 0.004f), he.z), vec3(he.x, 0.004f, 0.05f), dark, M(MAT_WOOD), SK_NONE);
        box(b, vec3(s * (he.x + 0.004f), 0, he.z), vec3(0.004f, he.y, 0.05f), dark, M(MAT_WOOD), SK_NONE);
        box(b, vec3(0, s * (he.y + 0.005f), he.z * 2.f - 0.05f), vec3(he.x, 0.005f, 0.05f), dark, M(MAT_WOOD), SK_NONE);
        box(b, vec3(0, s * (he.y + 0.005f), 0.05f), vec3(he.x, 0.005f, 0.05f), dark, M(MAT_WOOD), SK_NONE);
    }
    collide(b, vec3(0, 0, he.z), he);
}

// Pallet with a load: 0 cartons, 1 wrapped load, 2 drums, 3 sacks (kept light: racks hold hundreds of them)
void palletLoad(IB& b, vec3 p, float yaw, int kind, float maxH, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 pw = C(0.66f, 0.55f, 0.4f);
    box(b, vec3(0, 0, 0.07f), vec3(0.6f, 0.5f, 0.07f), pw, M(MAT_WOOD), SK_NZ);
    for (int s = -1; s <= 1; s += 2)
        for (int k = -1; k <= 1; k += 2) {
            float x = k * 0.3f, y = s * 0.501f;
            quadF(b, vec3(x - 0.18f, y, 0.02f), vec3(x + 0.18f, y, 0.02f), vec3(x + 0.18f, y, 0.1f), vec3(x - 0.18f, y, 0.1f), vec3(0, (float)s, 0), Gy(0.08f), M(MAT_WOOD));
        }
    float h = Min(maxH - 0.14f, r.range(0.6f, 1.3f));
    if (h < 0.2f) return;
    switch (kind) {
        case 0: {
            vec3 c = r.chance(0.6f) ? vec3(0.6f, 0.45f, 0.3f) : hsv(r.f(), 0.3f, 0.7f);
            float top = 0.14f + h;
            box(b, vec3(0, 0, 0.14f + h * 0.5f), vec3(0.58f, 0.48f, h * 0.5f), C(c * r.range(0.9f, 1.05f)), M(MAT_METAL_PAINTED), SK_NZ);
            // carton seams on the long faces + a printed label
            for (int s = -1; s <= 1; s += 2) {
                float y = s * 0.482f;
                quadF(b, vec3(-0.005f, y, 0.14f), vec3(0.005f, y, 0.14f), vec3(0.005f, y, top), vec3(-0.005f, y, top), vec3(0, (float)s, 0), C(c * 0.6f), M(MAT_METAL_PAINTED));
                quadF(b, vec3(-0.58f, y, 0.14f + h * 0.5f - 0.004f), vec3(0.58f, y, 0.14f + h * 0.5f - 0.004f), vec3(0.58f, y, 0.14f + h * 0.5f + 0.004f),
                      vec3(-0.58f, y, 0.14f + h * 0.5f + 0.004f), vec3(0, (float)s, 0), C(c * 0.6f), M(MAT_METAL_PAINTED));
                quadF(b, vec3(0.15f, y, top - 0.22f), vec3(0.4f, y, top - 0.22f), vec3(0.4f, y, top - 0.08f), vec3(0.15f, y, top - 0.08f), vec3(0, (float)s, 0), Gy(0.92f), M(MAT_PAINT_WHITE));
            }
            break;
        }
        case 1:
            box(b, vec3(0, 0, 0.14f + h * 0.5f), vec3(0.6f, 0.5f, h * 0.5f), C(0.84f, 0.87f, 0.9f), M(MAT_PLASTIC), SK_NZ);
            break;
        case 2: {
            vec3 c = r.chance(0.5f) ? vec3(0.1f, 0.25f, 0.55f) : vec3(0.6f, 0.12f, 0.08f);
            for (int i = 0; i < 2; i++)
                for (int j = 0; j < 2; j++) cyl(b, vec3(-0.3f + i * 0.6f, -0.25f + j * 0.5f, 0.14f), 0.28f, 0.28f, Min(0.88f, h), 7, C(c), M(MAT_METAL_PAINTED), true);
            break;
        }
        default:
            for (int l = 0; l < 3; l++)
                box(b, vec3(r.range(-0.03f, 0.03f), r.range(-0.03f, 0.03f), 0.22f + l * 0.18f), vec3(0.56f, 0.46f, 0.09f), C(vec3(0.8f, 0.75f, 0.6f) * r.range(0.9f, 1.05f)),
                    M(MAT_FABRIC), SK_NZ);
            break;
    }
}

// Forklift (forks toward +y), mast with the forks at liftZ
void forklift(IB& b, vec3 p, float yaw, float liftZ, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 body = r.chance(0.5f) ? C(0.95f, 0.65f, 0.05f) : C(0.85f, 0.2f, 0.1f), steel = M(MAT_METAL_PAINTED);
    rbox(b, vec3(0, -0.4f, 0.55f), vec3(0.55f, 0.85f, 0.35f), 0.05f, body, steel, true);
    rbox(b, vec3(0, -1.05f, 0.62f), vec3(0.56f, 0.3f, 0.42f), 0.06f, Gy(0.2f), steel, true);   // counterweight
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = 0; sy < 2; sy++) {
            b.pushAxes(vec3(sx * 0.5f, sy == 0 ? 0.25f : -0.95f, 0.28f), vec3(0, 1, 0), vec3(0, 0, (float)sx), vec3((float)sx, 0, 0));
            cyl(b, vec3(0, 0, -0.1f), 0.28f, 0.28f, 0.2f, 14, Gy(0.06f), M(MAT_RUBBER), true, true);
            b.pop();
        }
    // overhead guard
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = 0; sy < 2; sy++) tube(b, vec3(sx * 0.45f, sy == 0 ? 0.3f : -0.8f, 0.85f), vec3(sx * 0.45f, sy == 0 ? 0.25f : -0.85f, 2.1f), 0.03f, 6, Gy(0.15f), steel);
    box(b, vec3(0, -0.28f, 2.12f), vec3(0.48f, 0.6f, 0.03f), Gy(0.15f), steel, SK_NONE);
    // seat + wheel
    rbox(b, vec3(0, -0.55f, 1.0f), vec3(0.25f, 0.22f, 0.07f), 0.04f, Gy(0.1f), M(MAT_LEATHER), true);
    tube(b, vec3(0, 0.05f, 0.95f), vec3(0, -0.05f, 1.3f), 0.02f, 6, Gy(0.1f), steel);
    b.pushAxes(vec3(0, -0.07f, 1.33f), vec3(1, 0, 0), normalize(vec3(0, 0.4f, 1.f)), normalize(vec3(0, -1.f, 0.4f)));
    cyl(b, vec3(0, 0, -0.015f), 0.17f, 0.17f, 0.03f, 14, Gy(0.08f), M(MAT_RUBBER), true, true);
    b.pop();
    // mast, carriage, forks
    for (int sx = -1; sx <= 1; sx += 2) box(b, vec3(sx * 0.35f, 0.55f, 1.2f), vec3(0.05f, 0.06f, 1.2f), Gy(0.15f), steel, SK_NONE);
    box(b, vec3(0, 0.62f, liftZ + 0.3f), vec3(0.45f, 0.03f, 0.3f), Gy(0.15f), steel, SK_NONE);
    for (int sx = -1; sx <= 1; sx += 2) box(b, vec3(sx * 0.28f, 1.2f, liftZ + 0.03f), vec3(0.06f, 0.58f, 0.025f), Gy(0.2f), steel, SK_NONE);
    collide(b, vec3(0, -0.4f, 1.05f), vec3(0.6f, 0.95f, 1.05f));
}

// Two-post car lift (arms toward +/-y), posts at x = +-1.6; carriage and arms at liftZ
void carLift(IB& b, vec3 p, float yaw, float liftZ, u32 col, float topZ = 3.95f) {
    At at(b, p, yaw);
    u32 steel = M(MAT_METAL_PAINTED);
    const float ph = topZ * 0.5f;   // post half height (the crossbar sits on top; 3.95 m unless the ceiling is lower)
    for (int sx = -1; sx <= 1; sx += 2) {
        float x = sx * 1.75f;
        box(b, vec3(x, 0.f, ph), vec3(0.16f, 0.18f, ph), col, steel, SK_NZ);
        box(b, vec3(x, 0.f, 0.02f), vec3(0.3f, 0.35f, 0.02f), Gy(0.3f), steel, SK_NZ);
        box(b, vec3(x - sx * 0.2f, 0.f, liftZ + 0.15f), vec3(0.08f, 0.2f, 0.25f), Gy(0.2f), steel, SK_NONE);   // carriage
        for (int sy = -1; sy <= 1; sy += 2) {
            // swing arm from the carriage under the car, pad at the tip
            vec3 a(x - sx * 0.25f, sy * 0.1f, liftZ - 0.05f), c(sx * 0.85f, sy * 1.25f, liftZ - 0.05f);
            vec3 dd = c - a;
            float L = length(dd);
            b.pushAxes(a, dd / L, normalize(cross(vec3(0, 0, 1), dd / L)), vec3(0, 0, 1));
            box(b, vec3(L * 0.5f, 0.f, 0.f), vec3(L * 0.5f, 0.06f, 0.04f), Gy(0.25f), steel, SK_NONE);
            b.pop();
            cyl(b, c + vec3(0, 0, 0.04f), 0.08f, 0.08f, 0.06f, 10, Gy(0.1f), M(MAT_RUBBER), true);
        }
        collide(b, vec3(x, 0.f, ph), vec3(0.18f, 0.2f, ph));
    }
    box(b, vec3(0.f, 0.f, topZ), vec3(1.95f, 0.12f, 0.08f), col, steel, SK_NONE);   // overhead crossbar
    tube(b, vec3(-1.75f, 0.12f, topZ - 0.05f), vec3(1.75f, 0.12f, topZ - 0.05f), 0.012f, 5, Gy(0.1f), M(MAT_RUBBER));
    box(b, vec3(1.95f, 0.f, 1.3f), vec3(0.06f, 0.1f, 0.14f), Gy(0.85f), M(MAT_PLASTIC), SK_NONE);   // controls
    box(b, vec3(2.012f, 0.f, 1.32f), vec3(0.001f, 0.03f, 0.03f), C(0.1f, 0.9f, 0.2f, 0.8f), EM(), SK_NONE);
}

// Workbench against a wall (front +y): pegboard with tools, a vise, clutter
void workbench(IB& b, vec3 p, float yaw, float len, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 steel = M(MAT_METAL_PAINTED);
    box(b, vec3(0, 0.35f, 0.9f), vec3(len * 0.5f, 0.35f, 0.03f), C(0.5f, 0.38f, 0.24f), M(MAT_WOOD), SK_NONE);
    for (int sx = -1; sx <= 1; sx += 2) box(b, vec3(sx * (len * 0.5f - 0.05f), 0.35f, 0.43f), vec3(0.04f, 0.32f, 0.43f), Gy(0.3f), steel, SK_NZ);
    box(b, vec3(0, 0.35f, 0.2f), vec3(len * 0.5f - 0.08f, 0.3f, 0.015f), Gy(0.3f), steel, SK_NONE);
    box(b, vec3(0, 0.01f, 1.6f), vec3(len * 0.5f, 0.01f, 0.65f), C(0.62f, 0.5f, 0.34f), M(MAT_WOOD), SK_NY);   // pegboard
    collide(b, vec3(0, 0.35f, 0.47f), vec3(len * 0.5f, 0.35f, 0.47f));
    InPart ip(b, IP_DETAIL);
    // tool silhouettes on the pegboard
    for (float x = -len * 0.5f + 0.15f; x < len * 0.5f - 0.1f; x += r.range(0.08f, 0.16f)) {
        float z = r.range(1.2f, 2.0f);
        int kind = r.irange(0, 3);
        u32 tc = kind == 3 ? C(0.8f, 0.1f, 0.08f) : Gy(r.range(0.55f, 0.8f));
        if (kind == 0) box(b, vec3(x, 0.03f, z), vec3(0.012f, 0.008f, r.range(0.1f, 0.16f)), tc, M(MAT_CHROME), SK_NONE);                      // wrench
        else if (kind == 1) {
            box(b, vec3(x, 0.03f, z - 0.05f), vec3(0.012f, 0.012f, 0.1f), C(0.45f, 0.3f, 0.15f), M(MAT_WOOD), SK_NONE);                      // hammer
            box(b, vec3(x, 0.035f, z + 0.06f), vec3(0.05f, 0.015f, 0.018f), Gy(0.3f), M(MAT_METAL_PAINTED), SK_NONE);
        } else if (kind == 2) box(b, vec3(x, 0.03f, z), vec3(0.008f, 0.008f, 0.09f), C(0.9f, 0.7f, 0.1f), M(MAT_PLASTIC), SK_NONE);           // screwdriver
        else box(b, vec3(x, 0.035f, z), vec3(0.03f, 0.015f, 0.06f), tc, M(MAT_PLASTIC), SK_NONE);                                            // pliers / tape
    }
    // vise at one end, a parts tray and a rag
    box(b, vec3(len * 0.5f - 0.3f, 0.55f, 0.99f), vec3(0.1f, 0.08f, 0.06f), C(0.2f, 0.3f, 0.5f), M(MAT_METAL_PAINTED), SK_NZ);
    box(b, vec3(len * 0.5f - 0.3f, 0.65f, 1.02f), vec3(0.08f, 0.03f, 0.05f), C(0.2f, 0.3f, 0.5f), M(MAT_METAL_PAINTED), SK_NONE);
    box(b, vec3(-0.2f, 0.4f, 0.945f), vec3(0.2f, 0.12f, 0.015f), Gy(0.4f), M(MAT_METAL_BRUSHED), SK_NZ);
    for (int k = 0; k < 6; k++) cyl(b, vec3(-0.3f + r.range(0.f, 0.2f), 0.35f + r.range(0.f, 0.1f), 0.96f), 0.012f, 0.012f, 0.02f, 6, Gy(0.6f), M(MAT_CHROME), true);
    rbox(b, vec3(0.3f, 0.45f, 0.94f), vec3(0.12f, 0.1f, 0.01f), 0.008f, C(0.7f, 0.1f, 0.08f), M(MAT_CLOTH));
    light(b, vec3(0.f, 0.4f, 2.4f), vec3(0.95f, 0.98f, 1.f) * 120.f, 3.5f, -1, vec3(0, 0, -1), 70.f, 40.f);
    box(b, vec3(0.f, 0.4f, 2.43f), vec3(len * 0.4f, 0.06f, 0.03f), C(0.95f, 0.98f, 1.f, 0.9f), EM(), SK_PZ);
}

// Rolling tool chest (front +y)
void toolChest(IB& b, vec3 p, float yaw, u32 col) {
    At at(b, p, yaw);
    rbox(b, vec3(0, 0, 0.55f), vec3(0.5f, 0.28f, 0.45f), 0.02f, col, M(MAT_METAL_PAINTED), true);
    rbox(b, vec3(0, 0, 1.2f), vec3(0.48f, 0.26f, 0.2f), 0.02f, col, M(MAT_METAL_PAINTED), true);
    for (int k = 0; k < 6; k++) box(b, vec3(0, 0.285f, 0.2f + k * 0.14f), vec3(0.4f, 0.004f, 0.008f), Gy(0.8f), M(MAT_CHROME), SK_NZ);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) sphere(b, vec3(sx * 0.42f, sy * 0.2f, 0.05f), 0.05f, 6, Gy(0.1f), M(MAT_RUBBER));
    collide(b, vec3(0, 0, 0.7f), vec3(0.5f, 0.28f, 0.7f));
}

// Tire rack with stacked tires (front +y, against a wall)
void tireRack(IB& b, vec3 p, float yaw, float len, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    for (int lv = 0; lv < 3; lv++) {
        float z = 0.1f + lv * 0.8f;
        for (int e = -1; e <= 1; e += 2) tube(b, vec3(-len * 0.5f, 0.15f + e * 0.18f, z), vec3(len * 0.5f, 0.15f + e * 0.18f, z), 0.02f, 5, C(0.2f, 0.3f, 0.6f), M(MAT_METAL_PAINTED));
        InPart ip(b, IP_DETAIL);
        for (float x = -len * 0.5f + 0.15f; x < len * 0.5f - 0.1f; x += 0.24f) {
            if (r.chance(0.12f)) continue;
            b.pushAxes(vec3(x, 0.15f, z + 0.34f), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1, 0, 0));
            lathe(b, vec3(0, 0, -0.1f), {vec2(0.21f, 0.f), vec2(0.32f, 0.f), vec2(0.335f, 0.1f), vec2(0.32f, 0.2f), vec2(0.21f, 0.2f)}, 16, Gy(0.07f), M(MAT_RUBBER), false);
            lathe(b, vec3(0, 0, -0.1f), {vec2(0.21f, 0.2f), vec2(0.2f, 0.1f), vec2(0.21f, 0.f)}, 16, Gy(0.05f), M(MAT_RUBBER), false);
            b.pop();
        }
    }
    for (int e = -1; e <= 1; e += 2) box(b, vec3(e * len * 0.5f, 0.15f, 1.2f), vec3(0.025f, 0.2f, 1.2f), C(0.2f, 0.3f, 0.6f), M(MAT_METAL_PAINTED), SK_NZ);
    collide(b, vec3(0, 0.15f, 1.2f), vec3(len * 0.5f, 0.35f, 1.2f));
}

// Engine hoist ("cherry picker") with a chain
void engineHoist(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    u32 red = C(0.7f, 0.08f, 0.06f), steel = M(MAT_METAL_PAINTED);
    for (int sx = -1; sx <= 1; sx += 2) {
        box(b, vec3(sx * 0.4f, 0.4f, 0.07f), vec3(0.05f, 0.8f, 0.05f), red, steel, SK_NONE);
        sphere(b, vec3(sx * 0.4f, 1.15f, 0.05f), 0.05f, 6, Gy(0.1f), M(MAT_RUBBER));
    }
    box(b, vec3(0.f, -0.35f, 0.07f), vec3(0.45f, 0.06f, 0.05f), red, steel, SK_NONE);
    box(b, vec3(0.f, -0.35f, 0.85f), vec3(0.06f, 0.06f, 0.8f), red, steel, SK_NONE);
    vec3 a(0.f, -0.35f, 1.55f), c(0.f, 1.0f, 1.9f);
    vec3 dd = c - a;
    float L = length(dd);
    b.pushAxes(a, vec3(1, 0, 0), dd / L, cross(vec3(1, 0, 0), dd / L));
    box(b, vec3(0.f, L * 0.5f, 0.f), vec3(0.05f, L * 0.5f, 0.05f), red, steel, SK_NONE);
    b.pop();
    tube(b, vec3(0.f, -0.3f, 0.5f), vec3(0.f, 0.3f, 1.65f), 0.035f, 8, Gy(0.3f), M(MAT_CHROME));
    for (int k = 0; k < 8; k++) box(b, c + vec3(0.f, 0.f, -0.06f - k * 0.06f), vec3(0.012f, 0.02f, 0.03f), Gy(0.25f), steel, SK_NONE);
    box(b, c + vec3(0.f, 0.f, -0.58f), vec3(0.03f, 0.03f, 0.04f), Gy(0.2f), steel, SK_NONE);
    collide(b, vec3(0, 0.3f, 0.9f), vec3(0.45f, 0.9f, 0.9f));
}

// Engine block on a stand
void engineStand(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    u32 steel = M(MAT_METAL_PAINTED);
    for (int k = 0; k < 3; k++) {
        float a = kTwoPi * k / 3.f;
        tube(b, vec3(0, 0, 0.05f), vec3(cosf(a) * 0.45f, sinf(a) * 0.45f, 0.03f), 0.03f, 6, C(0.2f, 0.3f, 0.6f), steel);
    }
    tube(b, vec3(0, 0, 0.05f), vec3(0, 0, 0.75f), 0.04f, 6, C(0.2f, 0.3f, 0.6f), steel);
    rbox(b, vec3(0.35f, 0.f, 0.85f), vec3(0.3f, 0.25f, 0.28f), 0.03f, Gy(0.35f), M(MAT_METAL_BRUSHED), true);
    rbox(b, vec3(0.35f, 0.f, 1.16f), vec3(0.26f, 0.2f, 0.06f), 0.02f, C(0.5f, 0.1f, 0.08f), steel, true);
    for (int k = 0; k < 4; k++) cyl(b, vec3(0.15f + k * 0.13f, 0.27f, 0.9f), 0.03f, 0.03f, 0.05f, 6, Gy(0.2f), steel, true);
    collide(b, vec3(0.2f, 0.f, 0.6f), vec3(0.5f, 0.45f, 0.6f));
}

// Loose car parts leaning on a wall (front +y): doors, a bumper, a hood, rims
void carParts(IB& b, vec3 p, float yaw, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    InPart ip(b, IP_FURNITURE);
    for (int k = 0; k < 3; k++) {
        vec3 c = hsv(r.f(), r.range(0.3f, 0.8f), r.range(0.3f, 0.8f));
        b.pushAxes(vec3(-1.2f + k * 1.1f, 0.25f, 0.f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.25f)), normalize(vec3(0, -0.25f, 1)));
        rbox(b, vec3(0, 0.f, 0.55f), vec3(0.5f, 0.04f, 0.5f), 0.03f, C(c), M(MAT_CARPAINT), true);
        box(b, vec3(0.05f, 0.041f, 0.82f), vec3(0.35f, 0.001f, 0.14f), Gy(0.1f), M(MAT_GLASS), SK_NZ);
        box(b, vec3(-0.3f, 0.05f, 0.55f), vec3(0.06f, 0.012f, 0.015f), Gy(0.7f), M(MAT_CHROME), SK_NZ);
        b.pop();
    }
    b.pushAxes(vec3(1.9f, 0.3f, 0.f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.1f)), normalize(vec3(0, -0.1f, 1)));
    rbox(b, vec3(0, 0.f, 0.3f), vec3(0.9f, 0.08f, 0.2f), 0.06f, C(hsv(r.f(), 0.5f, 0.5f)), M(MAT_CARPAINT), true);
    b.pop();
    for (int k = 0; k < 3; k++) {
        vec3 wp(0.9f + k * 0.2f, 0.9f, 0.f);
        b.pushAxes(wp + vec3(0, 0, 0.32f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0));
        lathe(b, vec3(0, 0, -0.1f), {vec2(0.2f, 0.f), vec2(0.32f, 0.f), vec2(0.335f, 0.1f), vec2(0.32f, 0.2f), vec2(0.2f, 0.2f)}, 14, Gy(0.07f), M(MAT_RUBBER), false);
        disc(b, vec3(0, 0, 0.09f), 0.2f, 14, Gy(0.7f), M(MAT_CHROME));
        b.pop();
    }
    collide(b, vec3(0.2f, 0.4f, 0.5f), vec3(2.2f, 0.45f, 0.5f));
}

// Air compressor (vertical tank)
void compressor(IB& b, vec3 p) {
    lathe(b, p, {vec2(0.2f, 0.f), vec2(0.28f, 0.05f), vec2(0.28f, 1.3f), vec2(0.2f, 1.42f), vec2(0.f, 1.45f)}, 16, C(0.75f, 0.1f, 0.08f), M(MAT_METAL_PAINTED), false);
    rbox(b, p + vec3(0.f, 0.f, 1.55f), vec3(0.22f, 0.18f, 0.12f), 0.03f, Gy(0.2f), M(MAT_METAL_PAINTED), true);
    b.pushAxes(p + vec3(0.2f, 0.f, 1.55f), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1, 0, 0));
    cyl(b, vec3(0.f), 0.12f, 0.12f, 0.03f, 12, Gy(0.3f), M(MAT_METAL_PAINTED), true, true);
    b.pop();
    tube(b, p + vec3(0.f, 0.28f, 1.0f), p + vec3(0.5f, 0.6f, 0.02f), 0.012f, 5, C(0.1f, 0.3f, 0.8f), M(MAT_RUBBER));
    collide(b, p + vec3(0, 0, 0.8f), vec3(0.3f, 0.3f, 0.8f));
}

// Pallet rack bay run along local x (front +y and back -y open), uprights every `bay` meters, loads on each level
void palletRack(IB& b, vec3 p, float yaw, float len, int levels, float levelH, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 blue = C(0.1f, 0.25f, 0.6f), orange = C(0.95f, 0.45f, 0.05f), steel = M(MAT_METAL_PAINTED);
    const float dp = 1.1f, bay = 2.75f;
    int nb = Max(1, (int)roundf(len / bay));
    float bw = len / nb, H = levels * levelH + 0.3f;
    for (int i = 0; i <= nb; i++) {
        float x = -len * 0.5f + i * bw;
        for (int e = -1; e <= 1; e += 2) box(b, vec3(x, e * (dp * 0.5f - 0.05f), H * 0.5f), vec3(0.05f, 0.05f, H * 0.5f), blue, steel, SK_NZ);
        for (int k = 0; k < (int)(H / 0.9f); k++) {
            float z0 = 0.2f + k * 0.9f;
            b.pushAxes(vec3(x, -(dp * 0.5f - 0.05f), z0), vec3(0, 1, 0), vec3(-1, 0, 0), vec3(0, 0, 1));
            tube(b, vec3(0.f), vec3(dp - 0.1f, 0.f, 0.45f), 0.015f, 4, blue, steel);
            b.pop();
        }
        collide(b, vec3(x, 0.f, H * 0.5f), vec3(0.06f, dp * 0.5f, H * 0.5f));
    }
    for (int lv = 1; lv <= levels; lv++) {
        float z = lv * levelH;
        for (int e = -1; e <= 1; e += 2) box(b, vec3(0.f, e * (dp * 0.5f - 0.05f), z), vec3(len * 0.5f, 0.05f, 0.06f), orange, steel, SK_NONE);
        collide(b, vec3(0.f, 0.f, z), vec3(len * 0.5f, dp * 0.5f, 0.07f));
    }
    InPart ip(b, IP_DETAIL);
    for (int lv = 0; lv <= levels; lv++) {
        float z = lv == 0 ? 0.f : lv * levelH + 0.06f;
        float maxH = levelH - 0.25f;
        for (int i = 0; i < nb; i++) {
            float x = -len * 0.5f + (i + 0.5f) * bw;
            for (int s = -1; s <= 1; s += 2) {
                if (r.chance(0.18f)) continue;
                palletLoad(b, vec3(x + s * bw * 0.24f, 0.f, z), kHalfPi, r.irange(0, 3), maxH, r.next());
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------ Rook's garage
void layoutChopShop(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0x6A6Au);
    const float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT, H = d.ceil;
    float dx0 = 0.f, dx1 = 0.f, dh = 2.2f;
    mainDoorOpening(d, dx0, dx1, dh);
    const float doorX = (dx0 + dx1) * 0.5f;
    const float pt = 0.2f;
    const float yO = Y1 - 4.6f;                 // back rooms partition center
    std::vector<float> rolls;
    for (const InteriorOpening& op : d.openings)
        if (op.kind == OP_ROLLUP) {
            vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
            rolls.push_back((a.x + c.x) * 0.5f);
        }
    const float mainX = rolls.empty() ? (X0 + X1) * 0.5f : rolls[0];
    const float os = mainX > (X0 + X1) * 0.5f ? -1.f : 1.f;          // office on the side away from the main bay
    const float ox0 = os > 0.f ? X1 - 6.0f : X0, ox1 = os > 0.f ? X1 : X0 + 6.0f;
    const float oH = Min(3.0f, H - 0.5f);
    int garage = room(b, vec3(X0, Y0, 0.f), vec3(X1, yO - pt * 0.5f, H), vec3(24.f, 24.f, 23.f), 0.12f, LS_BUSINESS);
    int office = room(b, vec3(os > 0.f ? ox0 + pt * 0.5f : ox0, yO + pt * 0.5f, 0.f), vec3(os > 0.f ? ox1 : ox1 - pt * 0.5f, Y1, oH), vec3(16.f, 14.f, 11.f), 0.f,
                      LS_BUSINESS);
    int parts = room(b, vec3(os > 0.f ? X0 : ox1 + pt * 0.5f, yO + pt * 0.5f, 0.f), vec3(os > 0.f ? ox0 - pt * 0.5f : X1, Y1, oH), vec3(12.f, 12.f, 11.f), 0.f,
                     LS_ALWAYS);
    storefrontEntrance(b, DK_HINGED, 2, C(0.35f, 0.38f, 0.4f));
    rollupDoors(b, C(0.62f, 0.64f, 0.66f));
    shopSign(b, "ROOK AUTO REPAIR", C(0.1f, 0.1f, 0.12f), C(0.95f, 0.6f, 0.1f), dh + 2.6f);
    // office door + glazed front, parts room door
    const float odX = os > 0.f ? ox0 + 1.0f : ox1 - 1.0f, pdX = os > 0.f ? ox0 - 1.2f : ox1 + 1.2f;
    door(b, vec3(odX, yO, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.1f, DK_HINGED, 3, C(0.45f, 0.32f, 0.2f), false);
    door(b, vec3(pdX, yO, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.1f, DK_HINGED, 2, Gy(0.5f), false);
    partitionX(b, X0, X1, yO, pt, H, {vec2(odX - 0.45f, odX + 0.45f), vec2(pdX - 0.45f, pdX + 0.45f)});
    partitionY(b, yO + pt * 0.5f, Y1, os > 0.f ? ox0 : ox1, pt, oH);
    const float gw0 = os > 0.f ? odX + 0.7f : ox0 + 0.6f, gw1 = os > 0.f ? ox1 - 0.6f : odX - 0.7f, gz0 = 1.0f, gz1 = Min(2.3f, oH - 0.3f);
    {
        const InteriorRoom& rg = d.rooms[garage];
        const InteriorRoom& ro = d.rooms[office];
        tExtraHoles.push_back({garage, 2, {gw0 - rg.mn.x, gw1 - rg.mn.x, gz0, gz1}});
        tExtraHoles.push_back({office, 0, {ro.mx.x - gw1, ro.mx.x - gw0, gz0, gz1}});
    }
    ShellStyle gs;
    gs.wallCol = C(0.78f, 0.78f, 0.74f);
    gs.wallMat = M(MAT_CONCRETE_PANEL);
    gs.floorMat = M(MAT_CONCRETE);
    gs.floorCol = C(0.56f, 0.55f, 0.52f);
    gs.floorUV = 0.5f;
    gs.ceilMat = M(MAT_ROOF_METAL);
    gs.ceilCol = Gy(0.55f);
    gs.wainscotH = 1.2f;
    gs.wainCol = C(0.3f, 0.32f, 0.36f);
    gs.wainMat = M(MAT_METAL_PAINTED);
    gs.baseH = 0.f;
    gs.revealCol = Gy(0.5f);
    shell(b, garage, gs);
    ShellStyle os2;
    os2.wallCol = C(0.72f, 0.66f, 0.54f);
    os2.wallMat = M(MAT_WOOD);
    os2.floorMat = M(MAT_TILE);
    os2.floorCol = C(0.5f, 0.45f, 0.38f);
    os2.ceilMat = M(MAT_CEILING_TILE);
    os2.ceilCol = Gy(0.85f);
    shell(b, office, os2);
    ShellStyle ps = gs;
    ps.wainscotH = 0.f;
    ps.ceilMat = M(MAT_CONCRETE);
    shell(b, parts, ps);
    doorFrameInner(b, vec3(odX, yO, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.1f, pt, C(0.35f, 0.25f, 0.15f));
    doorFrameInner(b, vec3(pdX, yO, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.1f, pt, Gy(0.4f));
    {
        InPart ip(b, IP_SHELL);
        reveal(b, vec3(gw0, yO - pt * 0.5f, 0.f), vec3(1, 0, 0), vec3(0, 1, 0), 0.f, gw1 - gw0, gz0, gz1, 0.f, pt, C(0.35f, 0.25f, 0.15f), M(MAT_WOOD), true);
        quadF(b, vec3(gw0, yO, gz0), vec3(gw1, yO, gz0), vec3(gw1, yO, gz1), vec3(gw0, yO, gz1), vec3(0, -1, 0), glassCol(0.75f, vec3(0.8f, 0.85f, 0.8f)), kGlassMat);
        quadF(b, vec3(gw0, yO, gz0), vec3(gw1, yO, gz0), vec3(gw1, yO, gz1), vec3(gw0, yO, gz1), vec3(0, 1, 0), glassCol(0.75f, vec3(0.8f, 0.85f, 0.8f)), kGlassMat);
        // floor: oil stains and painted bay lines
        // oil stains: clusters of overlapping irregular blots, a shade darker than the slab
        Rng sr(d.seed + 1u);
        for (int k = 0; k < 9; k++) {
            vec3 sp(sr.range(X0 + 1.f, X1 - 1.f), sr.range(Y0 + 2.f, yO - 1.f), 0.f);
            int nb = sr.irange(2, 4);
            for (int j = 0; j < nb; j++) {
                vec3 q = sp + vec3(sr.range(-0.3f, 0.3f), sr.range(-0.3f, 0.3f), 0.002f + (k * 4 + j) * 0.00005f);
                float rr = sr.range(0.08f, 0.32f), a = sr.f() * kPi;
                b.pushAxes(q, vec3(cosf(a), sinf(a), 0.f) * sr.range(0.7f, 1.3f), vec3(-sinf(a), cosf(a), 0.f), vec3(0, 0, 1));
                disc(b, vec3(0.f), rr, 9, C(vec3(0.34f, 0.32f, 0.29f) * sr.range(0.85f, 1.1f)), M(MAT_CONCRETE));
                b.pop();
            }
        }
    }
    // ---- lifts: a car raised on the main bay's lift, a stripped shell on stands in the next bay
    const float liftY = Y0 + 7.2f;
    {
        carLift(b, vec3(mainX, liftY, 0.f), 0.f, 1.75f, C(0.75f, 0.1f, 0.08f));
        marker(b, IM_CAR, vec3(mainX, liftY, 1.75f - 0.2f), 0.f);
        float x2 = mainX + os * 5.2f;
        bool lift2 = x2 > X0 + 2.4f && x2 < X1 - 2.4f && !(x2 > Min(ox0, ox1) - 2.5f && liftY + 2.5f > yO);
        if (lift2) {
            carLift(b, vec3(x2, liftY, 0.f), 0.f, 0.2f, C(0.1f, 0.25f, 0.6f));
            marker(b, IM_CAR_STRIPPED, vec3(x2, liftY, 0.12f), 0.f);
            collide(b, vec3(x2, liftY, 0.8f), vec3(0.95f, 2.3f, 0.65f));
            InPart ip(b, IP_FURNITURE);
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sy = -1; sy <= 1; sy += 2) {
                    vec3 jp(x2 + sx * 0.75f, liftY + sy * 1.35f, 0.f);
                    lathe(b, jp, {vec2(0.14f, 0.f), vec2(0.05f, 0.25f), vec2(0.03f, 0.3f)}, 4, C(0.8f, 0.1f, 0.08f), M(MAT_METAL_PAINTED), true);
                }
            scenario(b, vec3(x2 - os * 1.3f, liftY - 1.0f, 0.f), os > 0.f ? -1.2f : 1.2f, 14, SR_MECHANIC, SF_STAFF | SF_OPTIONAL);
        }
        {
            InPart ip(b, IP_SHELL);
            for (float x : {mainX, x2}) {
                if (x < X0 + 2.4f || x > X1 - 2.4f || (x == x2 && !lift2)) continue;
                for (int e = -1; e <= 1; e += 2) box(b, vec3(x + e * 2.4f, liftY, 0.003f), vec3(0.05f, 3.2f, 0.003f), C(0.95f, 0.8f, 0.1f), M(MAT_PAINT_WHITE), SK_NZ);
            }
        }
        scenario(b, vec3(mainX + 0.6f, liftY - 0.6f, 0.f), 0.4f, 14, SR_MECHANIC, SF_STAFF);
    }
    // ---- walls: workbenches + pegboards, tool chests, tire rack, parts, compressor, drums, hoist, engine stand
    {
        float bw = os > 0.f ? X0 : X1;                              // bench wall (away from the office)
        float yawB = os > 0.f ? -kHalfPi : kHalfPi;
        float by0 = Y0 + 1.5f, by1 = yO - 1.2f;
        float L = Min(6.f, by1 - by0);
        {
            At at(b, vec3(bw, by0 + L * 0.5f, 0.f), yawB);
            workbench(b, vec3(-L * 0.25f, 0.f, 0.f), 0.f, L * 0.5f - 0.1f, r.next());
            workbench(b, vec3(L * 0.25f, 0.f, 0.f), 0.f, L * 0.5f - 0.1f, r.next());
            toolChest(b, vec3(-L * 0.5f - 0.7f, 0.3f, 0.f), 0.f, C(0.75f, 0.08f, 0.06f));
            scenario(b, vec3(L * 0.2f, 1.1f, 0.f), kPi, 0, SR_MECHANIC, SF_STAFF | SF_OPTIONAL);
        }
        if (by1 - by0 > L + 2.5f) {
            At at(b, vec3(bw, by0 + L + 1.8f, 0.f), yawB);
            tireRack(b, vec3(0.f), 0.f, 2.4f, r.next());
        }
        float ow = os > 0.f ? X1 : X0;
        float yawO = -yawB;
        {
            At at(b, vec3(ow, Y0 + 2.8f, 0.f), yawO);
            carParts(b, vec3(0.f), 0.f, r.next());
        }
        compressor(b, vec3(ow - os * 0.45f, Y0 + 0.6f, 0.f));
        oilDrum(b, vec3(ow - os * 0.45f, Y0 + 5.6f, 0.f), C(0.1f, 0.3f, 0.6f));
        oilDrum(b, vec3(ow - os * 0.45f, Y0 + 6.25f, 0.f), C(0.7f, 0.1f, 0.08f));
        oilDrum(b, vec3(ow - os * 1.1f, Y0 + 5.9f, 0.f), C(0.2f, 0.4f, 0.2f));
        engineHoist(b, vec3(mainX + os * 2.6f, Y0 + 2.6f, 0.f), os > 0.f ? 0.4f : -0.4f);
        engineStand(b, vec3(mainX - os * 2.9f, liftY + 3.3f, 0.f), 0.8f);
        scenario(b, vec3(doorX + (doorX < mainX ? -1.2f : 1.2f), Y0 + 1.4f, 0.f), 0.f, 10, SR_MECHANIC, SF_OPTIONAL);
    }
    // ---- overhead: trusses, shop lamps
    {
        InPart ip(b, IP_FURNITURE);
        for (float y = Y0 + 3.f; y < yO; y += 5.f) box(b, vec3((X0 + X1) * 0.5f, y, H - 0.3f), vec3((X1 - X0) * 0.5f, 0.1f, 0.3f), Gy(0.35f), M(MAT_METAL_PAINTED), SK_NONE);
        int nx = Max(2, (int)((X1 - X0) / 5.f)), ny = Max(2, (int)((yO - Y0) / 5.f));
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                vec3 top(X0 + (X1 - X0) * (i + 0.5f) / nx, Y0 + (yO - Y0) * (j + 0.5f) / ny, H - 0.6f);
                shopLamp(b, top, Max(0.5f, H - 5.2f), garage, 1100.f, 11.f);
            }
    }
    // ---- office: desk, chair, old sofa, filing cabinet, safe, calendar, coffee, TV
    {
        float oxa = Min(ox0, ox1) + (os > 0.f ? pt * 0.5f : 0.f), oxb = Max(ox0, ox1) - (os > 0.f ? 0.f : pt * 0.5f);
        float oy0 = yO + pt * 0.5f, oy1 = Y1;
        float ocx = (oxa + oxb) * 0.5f;
        officeDesk(b, vec3(ocx + os * 1.0f, oy1 - 1.2f, 0.f), 0.f, r.next(), true);
        officeChair(b, vec3(ocx + os * 1.0f, oy1 - 0.45f, 0.f), kPi, C(0.25f, 0.15f, 0.1f));
        scenario(b, vec3(ocx + os * 1.0f, oy1 - 0.45f, 0.f), kPi, 6, SR_MECHANIC, SF_STAFF);
        sofa(b, vec3(ocx - os * 1.4f, oy1 - 0.5f, 0.f), kPi, 1.9f, C(0.3f, 0.18f, 0.1f), r.next());
        {
            InPart ip(b, IP_FURNITURE);
            float fx = os > 0.f ? oxb - 0.3f : oxa + 0.3f;
            box(b, vec3(fx, oy0 + 0.6f, 0.66f), vec3(0.24f, 0.3f, 0.66f), Gy(0.45f), M(MAT_METAL_PAINTED), SK_NZ);
            collide(b, vec3(fx, oy0 + 0.6f, 0.66f), vec3(0.24f, 0.3f, 0.66f));
            At at(b, vec3(os > 0.f ? oxb : oxa, oy0 + 2.0f, 0.f), os > 0.f ? kHalfPi : -kHalfPi);
            picture(b, vec3(0.f, 0.f, 1.6f), 0.45f, 0.6f, r.next(), Gy(0.95f));
            gunSafe(b, vec3(0.8f, 0.f, 0.f), 0.f, Gy(0.15f));
        }
        coffeeStation(b, vec3(ocx - os * 1.4f, oy0 + 0.02f, 0.f), 0.f, 1.2f, r.next());
        domeLight(b, vec3(ocx, (oy0 + oy1) * 0.5f, oH), office, 120.f);
    }
    // ---- parts room: shelving with parts and boxes, an engine block, transmission
    {
        float pxa = Min(os > 0.f ? X0 : ox1 + pt * 0.5f, os > 0.f ? ox0 - pt * 0.5f : X1), pxb = Max(os > 0.f ? X0 : ox1 + pt * 0.5f, os > 0.f ? ox0 - pt * 0.5f : X1);
        storageShelf(b, vec3((pxa + pxb) * 0.5f, Y1 - 0.28f, 0.f), kPi, Min(5.f, pxb - pxa - 1.f), 2.2f, r.next());
        engineStand(b, vec3((pxa + pxb) * 0.5f - 1.5f, yO + 1.6f, 0.f), 2.2f);
        crate(b, vec3((pxa + pxb) * 0.5f + 1.6f, yO + 1.4f, 0.f), 0.3f, vec3(0.5f, 0.4f, 0.4f), r.next());
        troffer(b, vec3((pxa + pxb) * 0.5f, (yO + Y1) * 0.5f, oH), 1.2f, 0.3f, parts, 380.f, vec3(1.f, 0.97f, 0.9f), 6.f);
    }
    roomDressing(b, garage, true, d.seed ^ 0xD1u);
    roomDressing(b, office, false, d.seed ^ 0xD2u);
}

// ------------------------------------------------------------------------------------------------ Port Isle warehouse
void layoutWarehouse(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0x3A2Eu);
    const float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT, H = d.ceil;
    float dx0 = 0.f, dx1 = 0.f, dh = 2.2f;
    mainDoorOpening(d, dx0, dx1, dh);
    const float doorX = (dx0 + dx1) * 0.5f;
    const float mzY = Y1 - 6.f, mzZ = 3.6f;                 // mezzanine front edge and deck height
    const float mzX0 = X0, mzX1 = Min(X1, X0 + 16.f);
    int hall = room(b, vec3(X0, Y0, 0.f), vec3(X1, Y1, H), vec3(16.f, 16.f, 15.f), 0.1f, LS_BUSINESS);
    int moff = room(b, vec3(mzX0, mzY + 0.8f, mzZ), vec3(mzX0 + 6.f, Y1, mzZ + 2.8f), vec3(18.f, 17.f, 15.f), 0.f, LS_BUSINESS);
    storefrontEntrance(b, DK_HINGED, 2, C(0.3f, 0.33f, 0.35f));
    rollupDoors(b, C(0.55f, 0.58f, 0.6f));
    shopSign(b, "PORT ISLE LOGISTICS", C(0.08f, 0.2f, 0.35f), Gy(0.95f), dh + 3.0f);
    ShellStyle ws;
    ws.wallCol = C(0.72f, 0.72f, 0.68f);
    ws.wallMat = M(MAT_CONCRETE_PANEL);
    ws.floorMat = M(MAT_CONCRETE);
    ws.floorCol = C(0.58f, 0.57f, 0.54f);
    ws.floorUV = 0.4f;
    ws.ceilMat = M(MAT_ROOF_METAL);
    ws.ceilCol = Gy(0.6f);
    ws.baseH = 0.f;
    ws.wainscotH = 1.4f;
    ws.wainCol = C(0.25f, 0.3f, 0.38f);
    ws.wainMat = M(MAT_METAL_PAINTED);
    ws.revealCol = Gy(0.5f);
    shell(b, hall, ws);
    // ---- racking aisles in the middle band (3.4 m aisles), staging area in front, mezzanine at the back
    const float ya = Y0 + 8.f, yb = mzY - 3.4f;
    {
        float x = X0 + 0.6f;
        int row = 0;
        while (x + 1.2f < X1 - 0.3f && yb - ya > 4.f) {
            bool wallRow = row == 0;
            float cx = wallRow ? X0 + 0.6f : x + 1.1f;
            float w = wallRow ? 1.1f : 2.2f;
            if (cx + w * 0.5f > X1 - 0.2f) break;
            for (int s = 0; s < (wallRow ? 1 : 2); s++) {
                float rx = wallRow ? cx : cx + (s == 0 ? -0.55f : 0.55f);
                palletRack(b, vec3(rx, (ya + yb) * 0.5f, 0.f), kHalfPi, yb - ya, 3, 1.8f, r.next());
            }
            x = cx + w * 0.5f + 3.4f;
            row++;
        }
        // floor lane markings along the aisles' ends
        InPart ip(b, IP_SHELL);
        box(b, vec3((X0 + X1) * 0.5f, ya - 0.5f, 0.003f), vec3((X1 - X0) * 0.5f - 0.5f, 0.06f, 0.003f), C(0.95f, 0.8f, 0.1f), M(MAT_PAINT_WHITE), SK_NZ);
        box(b, vec3((X0 + X1) * 0.5f, yb + 0.5f, 0.003f), vec3((X1 - X0) * 0.5f - 0.5f, 0.06f, 0.003f), C(0.95f, 0.8f, 0.1f), M(MAT_PAINT_WHITE), SK_NZ);
    }
    // ---- staging area: pallets, forklifts, a container, crate stacks (cover)
    {
        Rng sr(d.seed + 3u);
        for (int k = 0; k < 7; k++) {
            float x = sr.range(X0 + 2.f, X1 - 2.f), y = sr.range(Y0 + 3.2f, ya - 1.8f);
            if (fabsf(x - doorX) < 2.f && y < Y0 + 4.f) continue;
            palletLoad(b, vec3(x, y, 0.f), sr.chance(0.5f) ? 0.f : kHalfPi, sr.irange(0, 3), 2.f, sr.next());
            collide(b, vec3(x, y, 0.7f), vec3(0.6f, 0.6f, 0.7f));
        }
        forklift(b, vec3(X0 + (X1 - X0) * 0.35f, ya - 2.4f, 0.f), 0.3f, 0.1f, r.next());
        forklift(b, vec3(X0 + (X1 - X0) * 0.7f, ya - 3.4f, 0.f), -2.6f, 1.4f, r.next());
        // container (cover), doors toward +y
        {
            vec3 cp(X1 - 3.8f, Y0 + 4.2f, 0.f);
            At at(b, cp, kHalfPi);
            u32 cc = C(hsv(sr.f(), 0.6f, 0.45f));
            box(b, vec3(0.f, 0.f, 1.3f), vec3(3.03f, 1.22f, 1.3f), cc, M(MAT_CORRUGATED), SK_NZ);
            for (int e = -1; e <= 1; e += 2) box(b, vec3(3.04f, e * 0.6f, 1.3f), vec3(0.01f, 0.58f, 1.25f), C(rgbOf(cc) * 0.85f), M(MAT_METAL_PAINTED), SK_NONE);
            for (int e = -1; e <= 1; e += 2) tube(b, vec3(3.06f, e * 0.3f, 0.2f), vec3(3.06f, e * 0.3f, 2.4f), 0.02f, 5, Gy(0.5f), M(MAT_METAL_PAINTED));
            collide(b, vec3(0.f, 0.f, 1.3f), vec3(3.05f, 1.22f, 1.3f));
        }
        for (int k = 0; k < 5; k++) {
            vec3 cp(X0 + 3.f + k * 1.3f, ya - 5.8f + (k & 1) * 0.3f, 0.f);
            crate(b, cp, sr.range(-0.2f, 0.2f), vec3(0.6f, 0.6f, 0.6f), sr.next());
            if (k % 2 == 0) crate(b, cp + vec3(0.f, 0.f, 1.2f), sr.range(-0.3f, 0.3f), vec3(0.5f, 0.5f, 0.45f), sr.next());
        }
        for (int k = 0; k < 4; k++) oilDrum(b, vec3(X1 - 1.2f - (k % 2) * 0.62f, ya - 1.5f - (k / 2) * 0.62f, 0.f), C(0.1f, 0.3f, 0.6f));
    }
    // ---- mezzanine: columns, deck, railing, stairs, office on the deck
    {
        InPart ip(b, IP_SHELL);
        u32 steel = M(MAT_METAL_PAINTED), yel = C(0.95f, 0.75f, 0.05f);
        float my0 = mzY, my1 = Y1;
        box(b, vec3((mzX0 + mzX1) * 0.5f, (my0 + my1) * 0.5f, mzZ - 0.1f), vec3((mzX1 - mzX0) * 0.5f, (my1 - my0) * 0.5f, 0.1f), Gy(0.4f), M(MAT_METAL_BRUSHED), SK_NONE);
        box(b, vec3((mzX0 + mzX1) * 0.5f, my0 + 0.06f, mzZ - 0.35f), vec3((mzX1 - mzX0) * 0.5f, 0.06f, 0.25f), Gy(0.3f), steel, SK_NONE);
        collideMM(b, vec3(mzX0, my0, mzZ - 0.2f), vec3(mzX1, my1, mzZ));
        for (float x = mzX0 + 0.3f; x < mzX1; x += 5.f) {
            box(b, vec3(x, my0 + 0.15f, (mzZ - 0.2f) * 0.5f), vec3(0.12f, 0.12f, (mzZ - 0.2f) * 0.5f), yel, steel, SK_NZ);
            collide(b, vec3(x, my0 + 0.15f, (mzZ - 0.2f) * 0.5f), vec3(0.12f, 0.12f, (mzZ - 0.2f) * 0.5f));
        }
        // railing on the open edges (gap at the stairs)
        const float stX = mzX1 - 1.2f;   // stair landing x
        auto railRun = [&](vec3 a, vec3 c) {
            vec3 dd = c - a;
            float L = length(dd);
            if (L < 0.2f) return;
            int n = Max(1, (int)(L / 1.5f));
            for (int k = 0; k <= n; k++) {
                vec3 pp = a + dd * ((float)k / n);
                box(b, pp + vec3(0, 0, 0.55f), vec3(0.025f, 0.025f, 0.55f), yel, steel, SK_NZ);
            }
            tube(b, a + vec3(0, 0, 1.1f), c + vec3(0, 0, 1.1f), 0.025f, 6, yel, steel, true);
            tube(b, a + vec3(0, 0, 0.55f), c + vec3(0, 0, 0.55f), 0.02f, 6, yel, steel, true);
            vec3 mn(Min(a.x, c.x) - 0.04f, Min(a.y, c.y) - 0.04f, a.z), mx(Max(a.x, c.x) + 0.04f, Max(a.y, c.y) + 0.04f, a.z + 1.1f);
            collideMM(b, mn, mx);
        };
        railRun(vec3(mzX0 + 0.05f, my0 + 0.05f, mzZ), vec3(stX - 0.7f, my0 + 0.05f, mzZ));
        if (mzX1 < X1 - 0.5f) railRun(vec3(mzX1 - 0.05f, my0 + 0.05f, mzZ), vec3(mzX1 - 0.05f, my1, mzZ));
        // stairs down along -y from the landing
        const int ns = (int)ceilf(mzZ / 0.18f);
        const float rise = mzZ / ns, run = 0.27f;
        for (int k = 0; k < ns - 1; k++) {
            float top = mzZ - rise * (k + 1);
            float y1s = my0 - run * k, y0s = y1s - run;
            box(b, vec3(stX, (y0s + y1s) * 0.5f, top - 0.03f), vec3(0.55f, run * 0.5f, 0.03f), Gy(0.45f), M(MAT_METAL_BRUSHED), SK_NONE);
            collideMM(b, vec3(stX - 0.55f, y0s, top - 0.2f), vec3(stX + 0.55f, y1s, top));
        }
        float yEnd = my0 - run * (ns - 1);
        for (int e = -1; e <= 1; e += 2) {
            vec3 a(stX + e * 0.58f, my0, mzZ), c(stX + e * 0.58f, yEnd, 0.f);
            vec3 dd = c - a;
            float L = length(dd);
            b.pushAxes(a, vec3(1, 0, 0), dd / L, cross(vec3(1, 0, 0), dd / L));
            box(b, vec3(0.f, L * 0.5f, 0.f), vec3(0.02f, L * 0.5f, 0.12f), yel, steel, SK_NONE);
            b.pop();
            tube(b, a + vec3(0, 0, 1.0f), c + vec3(0, 0, 1.0f), 0.022f, 6, yel, steel, true);
            for (int k = 0; k <= 4; k++) tube(b, lerp(a, c, k / 4.f), lerp(a, c, k / 4.f) + vec3(0, 0, 1.0f), 0.018f, 5, yel, steel);
        }
        // office box on the mezzanine: walls come from its room shell, glass front
        float ox1 = mzX0 + 6.f, oy0 = mzY + 0.8f;
        {
            const InteriorRoom& ro = d.rooms[moff];
            tExtraHoles.push_back({moff, 0, {ro.mx.x - (ox1 - 0.6f), ro.mx.x - (mzX0 + 0.6f), 0.9f, 2.2f}});
            tExtraHoles.push_back({moff, 1, {0.2f, 1.1f, 0.f, 2.1f}});   // doorway on the stair side
        }
        partitionX(b, mzX0, ox1, oy0, 0.12f, mzZ + 2.8f, {});
        partitionY(b, oy0, Y1, ox1, 0.12f, mzZ + 2.8f, {vec2(Y1 - 1.1f, Y1 - 0.2f)});
        box(b, vec3((mzX0 + ox1) * 0.5f, oy0 - 0.06f, mzZ + 2.9f), vec3((ox1 - mzX0) * 0.5f + 0.06f, 0.06f, 0.1f), Gy(0.7f), steel, SK_NONE);
        quadF(b, vec3(mzX0 + 0.6f, oy0 - 0.06f, mzZ + 0.9f), vec3(ox1 - 0.6f, oy0 - 0.06f, mzZ + 0.9f), vec3(ox1 - 0.6f, oy0 - 0.06f, mzZ + 2.2f),
              vec3(mzX0 + 0.6f, oy0 - 0.06f, mzZ + 2.2f), vec3(0, -1, 0), glassCol(0.8f), kGlassMat);
        quadF(b, vec3(mzX0 + 0.6f, oy0 - 0.06f, mzZ + 0.9f), vec3(ox1 - 0.6f, oy0 - 0.06f, mzZ + 0.9f), vec3(ox1 - 0.6f, oy0 - 0.06f, mzZ + 2.2f),
              vec3(mzX0 + 0.6f, oy0 - 0.06f, mzZ + 2.2f), vec3(0, 1, 0), glassCol(0.8f), kGlassMat);
        // outside faces of the office box (seen from the warehouse floor)
        quadF(b, vec3(mzX0, oy0 - 0.12f, mzZ), vec3(ox1 + 0.12f, oy0 - 0.12f, mzZ), vec3(ox1 + 0.12f, oy0 - 0.12f, mzZ + 2.9f), vec3(mzX0, oy0 - 0.12f, mzZ + 2.9f),
              vec3(0, -1, 0), C(0.8f, 0.8f, 0.76f), M(MAT_METAL_PAINTED));
        quadF(b, vec3(ox1 + 0.12f, oy0 - 0.12f, mzZ + 2.1f), vec3(ox1 + 0.12f, Y1, mzZ + 2.1f), vec3(ox1 + 0.12f, Y1, mzZ + 2.9f), vec3(ox1 + 0.12f, oy0 - 0.12f, mzZ + 2.9f),
              vec3(1, 0, 0), C(0.8f, 0.8f, 0.76f), M(MAT_METAL_PAINTED));
        quadF(b, vec3(ox1 + 0.12f, oy0 - 0.12f, mzZ), vec3(ox1 + 0.12f, Y1 - 1.1f, mzZ), vec3(ox1 + 0.12f, Y1 - 1.1f, mzZ + 2.1f), vec3(ox1 + 0.12f, oy0 - 0.12f, mzZ + 2.1f),
              vec3(1, 0, 0), C(0.8f, 0.8f, 0.76f), M(MAT_METAL_PAINTED));
        quadF(b, vec3(mzX0, oy0 - 0.12f, mzZ + 2.9f), vec3(ox1 + 0.12f, oy0 - 0.12f, mzZ + 2.9f), vec3(ox1 + 0.12f, Y1, mzZ + 2.9f), vec3(mzX0, Y1, mzZ + 2.9f), vec3(0, 0, 1),
              Gy(0.6f), M(MAT_METAL_PAINTED));
    }
    {
        ShellStyle ms;
        ms.wallCol = C(0.84f, 0.84f, 0.8f);
        ms.floorMat = M(MAT_CARPET);
        ms.floorCol = C(0.3f, 0.32f, 0.35f);
        ms.ceilMat = M(MAT_CEILING_TILE);
        ms.ceilCol = Gy(0.88f);
        ms.baseH = 0.08f;
        shell(b, moff, ms);
        officeDesk(b, vec3(mzX0 + 2.2f, Y1 - 1.3f, mzZ), 0.f, r.next(), true);
        officeChair(b, vec3(mzX0 + 2.2f, Y1 - 0.55f, mzZ), kPi, C(0.1f, 0.1f, 0.12f));
        scenario(b, vec3(mzX0 + 2.2f, Y1 - 0.55f, mzZ), kPi, 6, SR_WORKER, SF_OPTIONAL);
        storageShelf(b, vec3(mzX0 + 0.3f, Y1 - 3.0f, mzZ), -kHalfPi, 2.0f, 1.9f, r.next());
        domeLight(b, vec3(mzX0 + 3.f, (mzY + 0.8f + Y1) * 0.5f, mzZ + 2.8f), moff, 140.f);
        // storage on the open deck
        for (int k = 0; k < 3; k++) palletLoad(b, vec3(mzX0 + 7.5f + k * 1.6f, Y1 - 1.2f, mzZ), 0.f, k % 4, 1.8f, r.next());
        collideMM(b, vec3(mzX0 + 6.8f, Y1 - 1.8f, mzZ), vec3(mzX0 + 12.f, Y1 - 0.6f, mzZ + 1.4f));
    }
    // ---- high-bay lamps
    {
        int nx = Clamp((int)((X1 - X0) / 11.f), 2, 4), ny = Clamp((int)((Y1 - Y0) / 11.f), 2, 3);
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                vec3 top(X0 + (X1 - X0) * (i + 0.5f) / nx, Y0 + (Y1 - Y0) * (j + 0.5f) / ny, H);
                shopLamp(b, top, 1.2f, hall, 2600.f, 21.f);
            }
    }
    scenario(b, vec3(doorX + 1.5f, Y0 + 2.2f, 0.f), 0.f, 19, SR_GUARD, SF_STAFF);
    scenario(b, vec3(X0 + (X1 - X0) * 0.35f + 1.2f, ya - 2.4f, 0.f), -1.2f, 14, SR_WORKER, SF_OPTIONAL);
    scenario(b, vec3(X0 + (X1 - X0) * 0.55f, ya - 0.9f, 0.f), 0.7f, 0, SR_WORKER, SF_OPTIONAL);
    roomDressing(b, hall, true, d.seed ^ 0xE1u);
}

}  // namespace ikit
}  // namespace World
