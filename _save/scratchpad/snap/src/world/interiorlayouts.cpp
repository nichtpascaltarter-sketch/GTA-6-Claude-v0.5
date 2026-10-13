// Interior layouts: rooms, fixtures, furniture, lights, NPC scenario points and gameplay markers for every interior
// kind, built with the construction kit (interiorkit.cpp) in the interior frame. Each layout runs twice with the same
// random stream: once at world generation (plan: metadata only) and on demand when the interior streams in (build).
#include "interiors.h"
#include "sites.h"

namespace World {
namespace ikit {

const float kT = 0.3f;   // exterior wall thickness (matches interior_plan::kWallT)

// ------------------------------------------------------------------------------------------------ common helpers
// Main exterior door opening of the interior (OP_DOOR on the front facade)
const InteriorOpening* mainDoorOpening(const InteriorDef& d, float& x0, float& x1, float& h) {
    for (const InteriorOpening& op : d.openings) {
        if (op.kind != OP_DOOR) continue;
        vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
        x0 = Min(a.x, c.x);
        x1 = Max(a.x, c.x);
        h = op.z1 - op.z0;
        return &op;
    }
    return nullptr;
}

// Glass for every facade glass opening, split around door rectangles (sidelights + transom), with mullions for
// storefronts
void glazeAll(IB& b, float clarity, u32 frameCol, bool storefront) {
    if (!b.geo()) return;
    const InteriorDef& d = *b.d;
    std::vector<vec4> doors;   // x0, x1, z0, z1 (front facade local)
    for (const InteriorOpening& op : d.openings) {
        if (op.kind != OP_DOOR && op.kind != OP_ROLLUP) continue;
        vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
        doors.push_back(vec4(Min(a.x, c.x), Max(a.x, c.x), op.z0 - d.origin.z, op.z1 - d.origin.z));
    }
    for (const InteriorOpening& op : d.openings) {
        if (op.kind != OP_GLASS) continue;
        vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
        bool front = fabsf(a.y) < 0.1f && fabsf(c.y) < 0.1f;
        float gx0 = Min(a.x, c.x), gx1 = Max(a.x, c.x), gz0 = op.z0 - d.origin.z, gz1 = op.z1 - d.origin.z;
        std::vector<vec4> pieces = {vec4(gx0, gx1, gz0, gz1)};
        if (front)
            for (const vec4& dr : doors) {
                std::vector<vec4> next;
                for (const vec4& p : pieces) {
                    if (dr.y <= p.x || dr.x >= p.y || dr.w <= p.z || dr.z >= p.w) { next.push_back(p); continue; }
                    if (dr.x > p.x) next.push_back(vec4(p.x, dr.x, p.z, p.w));
                    if (dr.y < p.y) next.push_back(vec4(dr.y, p.y, p.z, p.w));
                    if (dr.w < p.w) next.push_back(vec4(Max(p.x, dr.x), Min(p.y, dr.y), dr.w, p.w));
                }
                pieces = next;
            }
        for (const vec4& p : pieces) {
            if (p.y - p.x < 0.05f || p.w - p.z < 0.05f) continue;
            InteriorOpening g = op;
            if (front) {
                g.a = d.toWorld(vec3(p.x, 0.f, 0.f)).xy();
                g.b = d.toWorld(vec3(p.y, 0.f, 0.f)).xy();
            }
            g.z0 = d.origin.z + p.z;
            g.z1 = d.origin.z + p.w;
            glazeOpening(b, g, clarity, frameCol, M(MAT_METAL_BRUSHED), storefront && p.y - p.x > 1.6f);
        }
    }
}

// Threshold under the entrance, landing and step down to the sidewalk (world ground is queried read-only)
void entranceStep(IB& b, float x0, float x1) {
    const InteriorDef& d = *b.d;
    vec3 outside = d.toWorld(vec3((x0 + x1) * 0.5f, -1.2f, 0.f));
    float gz = gMap ? gMap->heightAt(outside.x, outside.y) : d.origin.z;
    float rz;
    if (gRoads && gRoads->surfaceHeight(outside.xy(), &rz, d.origin.z + 1.f) && rz > gz - 0.5f) gz = rz;
    float drop = d.origin.z - gz;
    InPart ip(b, IP_SHELL);
    // threshold through the wall
    box(b, vec3((x0 + x1) * 0.5f, kT * 0.5f, -0.02f), vec3((x1 - x0) * 0.5f, kT * 0.5f, 0.02f), Gy(0.55f), M(MAT_METAL_BRUSHED), SK_NZ);
    if (drop > 0.06f) {
        float w = (x1 - x0) * 0.5f + 0.45f;
        float z0 = -Min(drop, 1.2f) - 0.15f;
        box(b, vec3((x0 + x1) * 0.5f, -0.55f, (z0 + 0.f) * 0.5f), vec3(w, 0.55f, (0.f - z0) * 0.5f), Gy(0.72f), M(MAT_CONCRETE), SK_NZ | SK_PY);
        collide(b, vec3((x0 + x1) * 0.5f, -0.55f, z0 * 0.5f), vec3(w, 0.55f, -z0 * 0.5f));
        if (drop > 0.3f) {
            float zs = -drop * 0.5f;
            box(b, vec3((x0 + x1) * 0.5f, -1.4f, (z0 + zs) * 0.5f), vec3(w, 0.3f, (zs - z0) * 0.5f), Gy(0.7f), M(MAT_CONCRETE), SK_NZ | SK_PY);
            collide(b, vec3((x0 + x1) * 0.5f, -1.4f, (z0 + zs) * 0.5f), vec3(w, 0.3f, (zs - z0) * 0.5f));
        }
    }
}

// Standard storefront entrance: automatic sliding glass doors in the facade door, glazing, threshold and markers.
// Returns the door index (-1 if the interior has no door opening).
int storefrontEntrance(IB& b, u8 kind = DK_SLIDING_PAIR, u8 style = 1, u32 color = 0xffb0b0b0u) {
    const InteriorDef& d = *b.d;
    float x0 = 0, x1 = 0, h = 2.2f;
    if (!mainDoorOpening(d, x0, x1, h)) return -1;
    float xc = (x0 + x1) * 0.5f;
    int di = door(b, vec3(xc, 0.f, 0.f), vec2(1, 0), vec2(0, 1), x1 - x0, h, kind, style, color, true, 0.095f);
    glazeAll(b, 0.92f, Gy(0.75f), d.storefront);
    entranceStep(b, x0, x1);
    marker(b, IM_DOOR_OUT, vec3(xc, -1.3f, 0.f), kPi);
    marker(b, IM_ENTRY, vec3(xc, kT + 1.0f, 0.f), 0.f);
    // entrance mat
    {
        InPart ip(b, IP_FURNITURE);
        box(b, vec3(xc, kT + 0.75f, 0.006f), vec3(Min(0.9f, (x1 - x0) * 0.5f + 0.1f), 0.6f, 0.006f), C(0.12f, 0.12f, 0.13f), M(MAT_CARPET), SK_NZ);
    }
    return di;
}

// ------------------------------------------------------------------------------------------------ door leaves
void doorLeaves(const InteriorDef& d, InteriorMesh& out) {
    for (size_t i = 0; i < d.doors.size(); i++) {
        const InteriorDoor& dr = d.doors[i];
        int leaves = (dr.kind == DK_HINGED || dr.kind == DK_ROLLUP) ? 1 : 2;
        for (int s = 0; s < leaves; s++) {
            InteriorDoorLeaf lf;
            lf.door = (int)i;
            lf.side = s;
            IB b;
            InteriorMesh sink;   // leaves are geometry only: collision / lights written by helpers go nowhere
            b.d = const_cast<InteriorDef*>(&d);
            b.out = &sink;
            for (int p = 0; p < IP_COUNT; p++) b.parts[p] = &lf.mesh;
            float lw = leaves == 2 ? dr.w * 0.5f + (dr.kind == DK_SLIDING_PAIR || dr.kind == DK_ELEVATOR ? 0.03f : -0.005f) : dr.w - 0.01f;
            float th = dr.style == 1 ? 0.05f : (dr.style == 4 ? 0.035f : 0.045f);
            if (dr.kind == DK_ROLLUP) {
                // corrugated roller shutter hanging from the head (z from -h to 0), scaled down as it rolls up
                box(b, vec3(dr.w * 0.5f, 0.f, -dr.h * 0.5f), vec3(dr.w * 0.5f, 0.03f, dr.h * 0.5f), dr.color, M(MAT_CORRUGATED), SK_NONE);
                for (float z = -dr.h + 0.15f; z < 0.f; z += 0.3f) box(b, vec3(dr.w * 0.5f, 0.f, z), vec3(dr.w * 0.5f - 0.02f, 0.036f, 0.012f), dr.color, M(MAT_METAL_PAINTED), SK_NONE);
                box(b, vec3(dr.w * 0.5f, 0.f, -dr.h + 0.04f), vec3(dr.w * 0.5f, 0.045f, 0.04f), Gy(0.25f), M(MAT_METAL_PAINTED), SK_NONE);
            } else {
                doorLeafGeo(b, lw, dr.h - (dr.kind == DK_SLIDING_PAIR ? 0.01f : 0.02f), th, dr.style, dr.color, s == 0);
            }
            out.leaves.push_back(std::move(lf));
        }
    }
}

// ------------------------------------------------------------------------------------------------ store fixtures
// Products on one shelf: x0..x1 along the shelf, front edge at y = yf (products extend toward -y), shelf top z
void shelfRow(IB& b, float x0, float x1, float z, float yf, float depth, float maxH, u32 seed) {
    Rng r(seed);
    int type = r.irange(0, 5);
    if (maxH < 0.16f && type == 1) type = 0;
    float x = x0 + 0.015f;
    int blockLeft = 0;
    vec3 col(1.f), band(1.f);
    float hue = r.f();
    while (x < x1 - 0.03f) {
        if (blockLeft <= 0) {
            blockLeft = r.irange(2, 6);
            hue = r.f();
            col = hsv(hue, r.range(0.5f, 0.95f), r.range(0.45f, 0.95f));
            band = r.chance(0.5f) ? vec3(0.95f) : hsv(hue + 0.5f, 0.8f, 0.9f);
            if (r.chance(0.07f)) {
                x += r.range(0.06f, 0.18f);   // gap (sold out)
                continue;
            }
        }
        float w = 0.08f, h = 0.12f;
        switch (type) {
            case 0: {  // cans in pairs of stacks
                float rr = 0.033f;
                w = rr * 2.f + 0.004f;
                h = 0.123f;
                can(b, vec3(x + rr, yf - rr - 0.01f, z), rr, h, C(col), C(band * 0.9f));
                if (maxH > 0.27f) can(b, vec3(x + rr, yf - rr - 0.01f, z + h), rr, h, C(col), C(band * 0.9f));
                break;
            }
            case 1: {  // bottles
                float rr = r.chance(0.5f) ? 0.036f : 0.045f;
                h = Min(maxH - 0.03f, rr > 0.04f ? 0.3f : 0.22f);
                w = rr * 2.f + 0.006f;
                bottle(b, vec3(x + rr, yf - rr - 0.01f, z), rr, h, C(col * 0.8f, 1.f), M(MAT_PLASTIC), C(band));
                box(b, vec3(x + rr, yf - 0.01f - rr * 1.02f + rr, z + h * 0.45f), vec3(rr * 0.8f, 0.001f, h * 0.12f), C(band), M(MAT_PAINT_WHITE), SK_NZ | SK_NY);
                break;
            }
            case 2: {  // cereal / snack boxes
                w = r.chance(0.5f) ? 0.19f : 0.15f;
                h = Min(maxH - 0.02f, w > 0.17f ? 0.3f : 0.24f);
                productBox(b, vec3(x + w * 0.5f, yf - 0.045f, z), vec3(w * 0.5f - 0.004f, 0.035f, h * 0.5f), C(col), C(band));
                break;
            }
            case 3: {  // chip bags (pillows)
                w = 0.19f;
                h = Min(maxH - 0.02f, 0.28f);
                rbox(b, vec3(x + w * 0.5f, yf - 0.05f, z + h * 0.5f), vec3(w * 0.5f - 0.006f, 0.04f, h * 0.5f), 0.03f, C(col), M(MAT_METAL_PAINTED));
                box(b, vec3(x + w * 0.5f, yf - 0.009f, z + h * 0.55f), vec3(w * 0.3f, 0.001f, h * 0.18f), C(band), M(MAT_PAINT_WHITE), SK_NZ | SK_NY);
                break;
            }
            case 4: {  // jars / tubs
                float rr = 0.04f;
                w = rr * 2.f + 0.006f;
                h = 0.11f;
                cyl(b, vec3(x + rr, yf - rr - 0.01f, z), rr, rr, h * 0.85f, 8, C(col * 0.5f + vec3(0.4f)), M(MAT_GLASS), false);
                cyl(b, vec3(x + rr, yf - rr - 0.01f, z + h * 0.85f), rr * 1.02f, rr * 1.02f, h * 0.15f, 8, C(band), M(MAT_METAL_PAINTED), true);
                break;
            }
            default: {  // small candy boxes stacked
                w = 0.1f;
                h = 0.07f;
                int stack = Clamp((int)(maxH / 0.075f) - 1, 1, 3);
                for (int k = 0; k < stack; k++) productBox(b, vec3(x + w * 0.5f, yf - 0.05f, z + k * 0.075f), vec3(w * 0.5f - 0.004f, 0.04f, h * 0.5f), C(col * (1.f - 0.08f * k)), C(band));
                break;
            }
        }
        // stock behind the front facing (one block per facing)
        if (depth > 0.2f) box(b, vec3(x + w * 0.5f, yf - 0.1f - (depth - 0.12f) * 0.5f, z + h * 0.45f), vec3(w * 0.5f - 0.005f, (depth - 0.14f) * 0.5f, h * 0.45f), C(col * 0.6f), M(MAT_METAL_PAINTED), SK_NZ | SK_NY);
        x += w + 0.006f;
        blockLeft--;
    }
}

// Double-sided gondola shelving along local x (customers on both sides, fronts at +y and -y)
void gondola(IB& b, vec3 p, float yaw, float len, float h, int levels, u32 seed, bool detail = true) {
    At at(b, p, yaw);
    u32 metal = M(MAT_METAL_PAINTED);
    u32 shelfCol = Gy(0.86f), baseCol = Gy(0.2f);
    float hl = len * 0.5f, dep = 0.45f;
    {
        InPart ip(b, IP_SHELL);
        box(b, vec3(0, 0, 0.06f), vec3(hl, dep, 0.06f), baseCol, metal, SK_NZ);                    // kick plate base
        box(b, vec3(0, 0, h * 0.5f + 0.06f), vec3(hl, 0.025f, h * 0.5f - 0.06f), shelfCol, metal, SK_NZ);   // center backboard
        int posts = Max(1, (int)roundf(len / 1.2f));
        for (int k = 0; k <= posts; k++) {
            float x = -hl + len * k / posts;
            box(b, vec3(x, 0, h * 0.5f), vec3(0.02f, 0.05f, h * 0.5f), Gy(0.7f), metal, SK_NZ);
        }
        // end panels with a price-card rail
        for (int e = -1; e <= 1; e += 2) box(b, vec3(e * (hl + 0.012f), 0, h * 0.5f), vec3(0.012f, dep, h * 0.5f), Gy(0.92f), metal, SK_NZ);
        collide(b, vec3(0, 0, h * 0.5f), vec3(hl + 0.02f, dep, h * 0.5f));
    }
    for (int side = -1; side <= 1; side += 2) {
        At s2(b, vec3(0.f), side > 0 ? 0.f : kPi);
        for (int lv = 0; lv < levels; lv++) {
            float z = 0.12f + lv * ((h - 0.2f) / levels);
            float sd = lv == 0 ? dep : dep - 0.06f * Min(lv, 3);
            {
                InPart ip(b, IP_SHELL);
                box(b, vec3(0, sd * 0.5f + 0.012f, z), vec3(hl - 0.02f, sd * 0.5f, 0.012f), shelfCol, metal, SK_NONE);
                // price rail
                box(b, vec3(0, sd + 0.018f, z - 0.005f), vec3(hl - 0.02f, 0.006f, 0.022f), C(0.95f, 0.85f, 0.2f), M(MAT_PAINT_WHITE), SK_NY);
            }
            if (!detail) continue;
            InPart ip(b, IP_DETAIL);
            float maxH = (h - 0.2f) / levels - 0.03f;
            if (lv == levels - 1) maxH = 0.3f;
            shelfRow(b, -hl + 0.03f, hl - 0.03f, z + 0.012f, sd + 0.01f, sd - 0.02f, maxH, hash32(seed * 31u + (u32)lv * 7u + (u32)(side + 1)));
        }
    }
}

// Glass-door drink coolers along a wall (front +y), header sign
void coolerRun(IB& b, vec3 p, float yaw, int doors, u32 seed, int roomIdx, const char* header) {
    At at(b, p, yaw);
    const float dw = 0.76f, H = 2.05f, dep = 0.78f;
    float len = doors * dw;
    float hl = len * 0.5f;
    u32 frame = Gy(0.08f), steel = M(MAT_METAL_BRUSHED), metal = M(MAT_METAL_PAINTED);
    {
        InPart ip(b, IP_SHELL);
        box(b, vec3(0, dep * 0.5f, H + 0.2f), vec3(hl + 0.04f, dep * 0.5f, 0.2f), C(0.1f, 0.25f, 0.5f), metal, SK_NY);   // header box
        box(b, vec3(0, dep * 0.5f, 0.08f), vec3(hl + 0.04f, dep * 0.5f, 0.08f), frame, metal, SK_NZ | SK_NY);         // base
        for (int e = -1; e <= 1; e += 2) box(b, vec3(e * (hl + 0.02f), dep * 0.5f, H * 0.5f), vec3(0.02f, dep * 0.5f, H * 0.5f), frame, metal, SK_NZ | SK_NY);
        box(b, vec3(0, 0.02f, H * 0.5f), vec3(hl, 0.02f, H * 0.5f), Gy(0.9f), M(MAT_PAINT_WHITE), SK_NZ);   // back liner
        box(b, vec3(0, dep * 0.5f, H + 0.005f), vec3(hl, dep * 0.5f, 0.005f), Gy(0.85f), M(MAT_PAINT_WHITE), SK_PZ);   // cabinet ceiling
        collide(b, vec3(0, dep * 0.5f, (H + 0.4f) * 0.5f), vec3(hl + 0.04f, dep * 0.5f, (H + 0.4f) * 0.5f));
        // header text
        if (header) textC(b, header, vec3(0, dep + 0.002f, H + 0.2f), vec3(1, 0, 0), vec3(0, 0, 1), 0.2f, 0.028f, C(0.9f, 0.95f, 1.f, 0.8f), EM(), 0.f, 0.3f);
    }
    for (int k = 0; k < doors; k++) {
        float xc = -hl + dw * (k + 0.5f);
        {
            InPart ip(b, IP_SHELL);
            // door frame (mullions) with LED strip lights
            box(b, vec3(xc - dw * 0.5f + 0.02f, dep - 0.02f, H * 0.5f + 0.08f), vec3(0.02f, 0.025f, H * 0.5f - 0.08f), frame, steel, SK_NZ);
            box(b, vec3(xc - dw * 0.5f + 0.02f, dep - 0.05f, H * 0.5f + 0.08f), vec3(0.008f, 0.004f, H * 0.5f - 0.12f), C(0.9f, 0.95f, 1.f, 0.9f), EM(), SK_NZ);
            box(b, vec3(xc, dep - 0.015f, H - 0.02f), vec3(dw * 0.5f - 0.02f, 0.02f, 0.03f), frame, steel, SK_NONE);
            box(b, vec3(xc, dep - 0.015f, 0.18f), vec3(dw * 0.5f - 0.02f, 0.02f, 0.03f), frame, steel, SK_NONE);
            // glass door (see-through) + handle
            quadF(b, vec3(xc - dw * 0.5f + 0.04f, dep - 0.005f, 0.21f), vec3(xc + dw * 0.5f - 0.04f, dep - 0.005f, 0.21f), vec3(xc + dw * 0.5f - 0.04f, dep - 0.005f, H - 0.05f),
                  vec3(xc - dw * 0.5f + 0.04f, dep - 0.005f, H - 0.05f), vec3(0, 1, 0), glassCol(0.9f, vec3(0.8f, 0.9f, 0.95f)), kGlassMat);
            tube(b, vec3(xc + dw * 0.5f - 0.1f, dep + 0.04f, 0.8f), vec3(xc + dw * 0.5f - 0.1f, dep + 0.04f, 1.55f), 0.012f, 8, Gy(0.85f), M(MAT_CHROME), true);
            tube(b, vec3(xc + dw * 0.5f - 0.1f, dep - 0.004f, 0.85f), vec3(xc + dw * 0.5f - 0.1f, dep + 0.04f, 0.85f), 0.008f, 6, Gy(0.85f), M(MAT_CHROME));
            tube(b, vec3(xc + dw * 0.5f - 0.1f, dep - 0.004f, 1.5f), vec3(xc + dw * 0.5f - 0.1f, dep + 0.04f, 1.5f), 0.008f, 6, Gy(0.85f), M(MAT_CHROME));
        }
        // shelves of drinks
        for (int lv = 0; lv < 5; lv++) {
            float z = 0.28f + lv * 0.36f;
            {
                InPart ip(b, IP_SHELL);
                box(b, vec3(xc, dep * 0.5f - 0.03f, z - 0.01f), vec3(dw * 0.5f - 0.03f, dep * 0.5f - 0.08f, 0.008f), C(0.85f, 0.88f, 0.9f), steel, SK_NONE);
            }
            InPart ip(b, IP_DETAIL);
            Rng r(hash32(seed + (u32)k * 17u + (u32)lv * 131u));
            bool cans = lv >= 3 ? r.chance(0.3f) : r.chance(0.6f);
            float x = xc - dw * 0.5f + 0.05f;
            vec3 col = hsv(r.f(), r.range(0.5f, 0.9f), r.range(0.4f, 0.95f));
            int block = 0;
            while (x < xc + dw * 0.5f - 0.07f) {
                if (block++ % 4 == 0) col = hsv(r.f(), r.range(0.5f, 0.9f), r.range(0.4f, 0.95f));
                if (cans) {
                    float rr = 0.033f;
                    can(b, vec3(x + rr, dep - 0.13f, z), rr, 0.123f, C(col), Gy(0.8f));
                    can(b, vec3(x + rr, dep - 0.13f, z + 0.124f), rr, 0.123f, C(col), Gy(0.8f));
                    x += rr * 2.f + 0.005f;
                } else {
                    float rr = r.chance(0.5f) ? 0.036f : 0.043f;
                    bool glassy = r.chance(0.3f);
                    bottle(b, vec3(x + rr, dep - 0.13f, z), rr, rr > 0.04f ? 0.3f : 0.23f, glassy ? C(col * 0.4f + vec3(0.05f)) : C(col, 1.f), glassy ? M(MAT_GLASS) : M(MAT_PLASTIC), C(col * 0.5f + vec3(0.4f)), 6);
                    x += rr * 2.f + 0.008f;
                }
                // back stock
                box(b, vec3(x - 0.035f, dep * 0.5f - 0.15f, z + 0.1f), vec3(0.035f, dep * 0.5f - 0.2f, 0.1f), C(col * 0.45f), M(MAT_METAL_PAINTED), SK_NZ | SK_NY);
            }
        }
    }
    // cold white light inside the coolers
    light(b, vec3(0, dep * 0.6f, H - 0.15f), vec3(0.85f, 0.95f, 1.f) * 180.f * (float)Max(1, doors / 3), 2.5f + len * 0.45f, roomIdx);
}

// Checkout counter along local x: customer side +y, clerk side -y; register, card terminal, lottery display,
// candy racks on the customer side, impulse items
void checkoutCounter(IB& b, vec3 p, float yaw, float len, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    float hl = len * 0.5f, H = 0.98f, dep = 0.32f;
    u32 front = C(hsv(r.range(0.55f, 0.65f), 0.55f, 0.35f)), top = C(0.82f, 0.8f, 0.76f);
    {
        InPart ip(b, IP_SHELL);
        box(b, vec3(0, 0, H * 0.5f), vec3(hl, dep, H * 0.5f - 0.02f), front, M(MAT_METAL_PAINTED), SK_NZ);
        rbox(b, vec3(0, 0.02f, H - 0.02f), vec3(hl + 0.03f, dep + 0.05f, 0.025f), 0.012f, top, M(MAT_MARBLE));
        box(b, vec3(0, dep + 0.005f, 0.06f), vec3(hl - 0.01f, 0.01f, 0.06f), Gy(0.1f), M(MAT_RUBBER), SK_NZ);
        // accent stripe
        box(b, vec3(0, dep + 0.003f, H * 0.62f), vec3(hl, 0.003f, 0.05f), C(0.95f, 0.75f, 0.15f), M(MAT_PAINT_WHITE), SK_NZ);
        collide(b, vec3(0, 0.02f, H * 0.5f), vec3(hl + 0.03f, dep + 0.05f, H * 0.5f));
    }
    InPart ip(b, IP_FURNITURE);
    float zt = H + 0.005f;
    // register: base, cash drawer, screen on a stand, customer display
    float rx = hl * 0.35f;
    rbox(b, vec3(rx, -0.1f, zt + 0.05f), vec3(0.2f, 0.18f, 0.05f), 0.01f, Gy(0.12f), M(MAT_PLASTIC));
    box(b, vec3(rx, -0.1f, zt + 0.105f), vec3(0.13f, 0.1f, 0.006f), Gy(0.2f), M(MAT_PLASTIC), SK_NZ);
    tube(b, vec3(rx, -0.18f, zt + 0.1f), vec3(rx, -0.2f, zt + 0.32f), 0.015f, 6, Gy(0.2f), M(MAT_PLASTIC));
    {
        // screen tilted back, facing the clerk (-y)
        b.pushAxes(vec3(rx, -0.2f, zt + 0.42f), vec3(-1, 0, 0), normalize(vec3(0, -1, 0.35f)), normalize(vec3(0, 0.35f, 1)));
        rbox(b, vec3(0.f), vec3(0.17f, 0.02f, 0.12f), 0.008f, Gy(0.08f), M(MAT_PLASTIC), true);
        box(b, vec3(0, 0.021f, 0), vec3(0.15f, 0.001f, 0.1f), C(0.3f, 0.6f, 0.9f, 0.5f), EM(), SK_NZ);
        b.pop();
    }
    // card terminal on the customer side
    rbox(b, vec3(-hl * 0.1f, dep - 0.12f, zt + 0.03f), vec3(0.05f, 0.08f, 0.03f), 0.01f, Gy(0.1f), M(MAT_PLASTIC));
    box(b, vec3(-hl * 0.1f, dep - 0.1f, zt + 0.061f), vec3(0.035f, 0.035f, 0.001f), C(0.2f, 0.5f, 0.3f, 0.4f), EM(), SK_NZ);
    // lottery ticket dispenser (colorful fan of ticket rolls under clear acrylic)
    float lx = -hl + 0.35f;
    box(b, vec3(lx, 0.f, zt + 0.12f), vec3(0.28f, 0.1f, 0.12f), Gy(0.15f), M(MAT_PLASTIC), SK_NZ);
    for (int k = 0; k < 8; k++) {
        vec3 c = hsv(k * 0.13f + 0.05f, 0.8f, 0.95f);
        box(b, vec3(lx - 0.24f + k * 0.068f, 0.101f, zt + 0.14f), vec3(0.03f, 0.002f, 0.07f), C(c), M(MAT_PAINT_WHITE), SK_NZ);
    }
    textC(b, "LOTTO", vec3(lx, 0.102f, zt + 0.26f), vec3(1, 0, 0), vec3(0, 0, 1), 0.05f, 0.008f, C(1.f, 0.9f, 0.2f, 0.7f), EM(), 0.f, 0.3f);
    // impulse: gum / mints tray on the counter, a tip jar
    for (int k = 0; k < 5; k++) productBox(b, vec3(hl - 0.5f + k * 0.07f, dep - 0.08f, zt), vec3(0.03f, 0.02f, 0.035f), C(hsv(r.f(), 0.7f, 0.9f)), Gy(0.95f));
    cyl(b, vec3(hl - 0.15f, dep - 0.12f, zt), 0.045f, 0.05f, 0.14f, 10, C(0.8f, 0.9f, 0.9f, 1.f), M(MAT_GLASS), false);
    // candy racks on the customer side (sloped shelves)
    {
        InPart ip2(b, IP_DETAIL);
        for (int lv = 0; lv < 3; lv++) {
            float z = 0.2f + lv * 0.24f;
            box(b, vec3(0, dep + 0.08f, z), vec3(hl - 0.1f, 0.08f, 0.008f), Gy(0.3f), M(MAT_METAL_PAINTED), SK_NONE);
            float x = -hl + 0.12f;
            Rng rr(hash32(seed + (u32)lv * 97u));
            while (x < hl - 0.14f) {
                vec3 c = hsv(rr.f(), 0.8f, rr.range(0.5f, 0.95f));
                float w = rr.range(0.05f, 0.08f);
                box(b, vec3(x + w * 0.5f, dep + 0.08f, z + 0.035f), vec3(w * 0.5f - 0.004f, 0.06f, 0.03f), C(c), M(MAT_METAL_PAINTED), SK_NZ);
                x += w;
            }
        }
    }
}

// Tobacco / cigarette wall behind the counter (against a wall, front +y): grid of colorful packs with a header
void tobaccoWall(IB& b, vec3 p, float yaw, float len, u32 seed) {
    At at(b, p, yaw);
    float hl = len * 0.5f;
    {
        InPart ip(b, IP_SHELL);
        box(b, vec3(0, 0.18f, 1.25f), vec3(hl, 0.18f, 0.75f), Gy(0.15f), M(MAT_METAL_PAINTED), SK_NZ);
        box(b, vec3(0, 0.3f, 2.12f), vec3(hl, 0.3f, 0.12f), C(0.7f, 0.1f, 0.1f), M(MAT_METAL_PAINTED), SK_NZ);
        textC(b, "TOBACCO  VAPE", vec3(0, 0.602f, 2.12f), vec3(1, 0, 0), vec3(0, 0, 1), 0.1f, 0.016f, C(1.f, 0.95f, 0.85f, 0.7f), EM(), 0.f, 0.35f);
        // back counter below
        box(b, vec3(0, 0.3f, 0.45f), vec3(hl, 0.3f, 0.45f), Gy(0.25f), M(MAT_WOOD), SK_NZ);
        box(b, vec3(0, 0.31f, 0.91f), vec3(hl + 0.01f, 0.31f, 0.015f), Gy(0.6f), M(MAT_MARBLE), SK_NONE);
        collide(b, vec3(0, 0.3f, 1.1f), vec3(hl, 0.3f, 1.1f));
    }
    InPart ip(b, IP_DETAIL);
    Rng r(seed);
    for (float z = 0.6f; z < 1.9f; z += 0.13f) {
        box(b, vec3(0, 0.36f, z - 0.005f), vec3(hl - 0.03f, 0.005f, 0.004f), Gy(0.7f), M(MAT_METAL_BRUSHED), SK_NONE);
        float x = -hl + 0.04f;
        vec3 c = hsv(r.f(), 0.7f, 0.9f);
        int blk = 0;
        while (x < hl - 0.06f) {
            if (blk++ % 3 == 0) c = r.chance(0.5f) ? hsv(r.f(), r.range(0.5f, 0.9f), r.range(0.5f, 0.95f)) : vec3(0.95f);
            box(b, vec3(x + 0.028f, 0.34f, z + 0.045f), vec3(0.026f, 0.012f, 0.045f), C(c), M(MAT_PAINT_WHITE), SK_NZ);
            box(b, vec3(x + 0.028f, 0.3525f, z + 0.07f), vec3(0.022f, 0.001f, 0.012f), C(vec3(1.f) - c), M(MAT_PAINT_WHITE), SK_NZ | SK_NY);
            x += 0.058f;
        }
    }
}

// Coffee / slushie / hot-dog station (against a wall, front +y)
void coffeeStation(IB& b, vec3 p, float yaw, float len, u32 seed) {
    At at(b, p, yaw);
    float hl = len * 0.5f;
    Rng r(seed);
    {
        InPart ip(b, IP_SHELL);
        box(b, vec3(0, 0.3f, 0.45f), vec3(hl, 0.3f, 0.45f), C(0.35f, 0.22f, 0.14f), M(MAT_WOOD), SK_NZ);
        rbox(b, vec3(0, 0.31f, 0.915f), vec3(hl + 0.02f, 0.32f, 0.02f), 0.01f, C(0.2f, 0.2f, 0.21f), M(MAT_MARBLE));
        box(b, vec3(0, 0.02f, 1.6f), vec3(hl, 0.02f, 0.65f), C(0.55f, 0.35f, 0.2f), M(MAT_WOOD), SK_NZ);   // back panel
        box(b, vec3(0, 0.06f, 2.12f), vec3(hl, 0.06f, 0.12f), C(0.1f, 0.1f, 0.12f), M(MAT_METAL_PAINTED), SK_NZ);
        textC(b, "FRESH COFFEE", vec3(-hl * 0.35f, 0.122f, 2.12f), vec3(1, 0, 0), vec3(0, 0, 1), 0.1f, 0.016f, C(1.f, 0.75f, 0.35f, 0.75f), EM(), 0.f, 0.35f);
        collide(b, vec3(0, 0.31f, 0.47f), vec3(hl + 0.02f, 0.32f, 0.47f));
    }
    InPart ip(b, IP_FURNITURE);
    float zt = 0.935f;
    // coffee brewers
    float x = -hl + 0.3f;
    for (int k = 0; k < 3 && x < hl * 0.2f; k++) {
        rbox(b, vec3(x, 0.25f, zt + 0.3f), vec3(0.14f, 0.18f, 0.3f), 0.02f, Gy(0.08f), M(MAT_METAL_BRUSHED));
        box(b, vec3(x, 0.44f, zt + 0.52f), vec3(0.1f, 0.005f, 0.05f), C(0.9f, 0.3f, 0.1f, 0.5f), EM(), SK_NZ);
        lathe(b, vec3(x, 0.35f, zt), {vec2(0.07f, 0.f), vec2(0.075f, 0.1f), vec2(0.06f, 0.16f), vec2(0.03f, 0.19f)}, 10, C(0.2f, 0.3f, 0.3f, 1.f), M(MAT_GLASS), true);
        cyl(b, vec3(x, 0.35f, zt + 0.03f), 0.068f, 0.07f, 0.06f, 10, C(0.12f, 0.06f, 0.03f), M(MAT_GLASS), true);
        x += 0.36f;
    }
    // cup stacks and lids
    for (int k = 0; k < 3; k++) {
        cyl(b, vec3(x + k * 0.1f, 0.4f, zt), 0.035f, 0.045f, 0.35f, 10, Gy(0.95f), M(MAT_PAINT_WHITE), true);
    }
    x += 0.4f;
    // slushie machine: two bowls with colored ice
    if (x < hl - 0.4f) {
        rbox(b, vec3(x + 0.2f, 0.28f, zt + 0.15f), vec3(0.22f, 0.22f, 0.15f), 0.02f, Gy(0.85f), M(MAT_METAL_BRUSHED));
        for (int k = 0; k < 2; k++) {
            vec3 c = k == 0 ? vec3(0.1f, 0.35f, 1.f) : vec3(1.f, 0.1f, 0.2f);
            cyl(b, vec3(x + 0.1f + k * 0.2f, 0.3f, zt + 0.3f), 0.085f, 0.09f, 0.34f, 12, C(c, 1.f), M(MAT_GLASS), true);
            cyl(b, vec3(x + 0.1f + k * 0.2f, 0.3f, zt + 0.3f), 0.08f, 0.08f, 0.22f, 12, C(c * 0.8f, 0.6f), EM(), false);
        }
        x += 0.5f;
    }
    // hot dog roller
    if (x < hl - 0.3f) {
        box(b, vec3(x + 0.22f, 0.3f, zt + 0.06f), vec3(0.24f, 0.22f, 0.06f), Gy(0.75f), M(MAT_METAL_BRUSHED), SK_NZ);
        for (int k = 0; k < 7; k++) {
            float yy = 0.14f + k * 0.045f;
            b.pushAxes(vec3(x + 0.02f, yy, zt + 0.14f), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1, 0, 0));
            cyl(b, vec3(0.f), 0.012f, 0.012f, 0.4f, 6, Gy(0.8f), M(MAT_CHROME), false);
            b.pop();
            if (k % 2 == 0) {
                b.pushAxes(vec3(x + 0.1f, yy, zt + 0.158f), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1, 0, 0));
                cyl(b, vec3(0.f), 0.014f, 0.014f, 0.24f, 6, C(0.55f, 0.22f, 0.12f), M(MAT_PLASTIC), true, true);
                b.pop();
            }
        }
        // sneeze guard
        quadF(b, vec3(x - 0.02f, 0.55f, zt + 0.15f), vec3(x + 0.46f, 0.55f, zt + 0.15f), vec3(x + 0.46f, 0.52f, zt + 0.4f), vec3(x - 0.02f, 0.52f, zt + 0.4f), vec3(0, 1, 0.2f),
              glassCol(0.95f), kGlassMat);
    }
    // napkins and stirrers
    box(b, vec3(hl - 0.15f, 0.3f, zt + 0.08f), vec3(0.06f, 0.08f, 0.08f), Gy(0.2f), M(MAT_METAL_PAINTED), SK_NZ);
}

// ATM (front +y)
void atm(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    InPart ip(b, IP_FURNITURE);
    rbox(b, vec3(0, 0.f, 0.75f), vec3(0.3f, 0.3f, 0.75f), 0.02f, C(0.15f, 0.2f, 0.3f), M(MAT_METAL_PAINTED));
    box(b, vec3(0, 0.15f, 1.3f), vec3(0.24f, 0.16f, 0.14f), Gy(0.1f), M(MAT_METAL_PAINTED), SK_NZ);
    b.pushAxes(vec3(0, 0.3f, 1.26f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.5f)), normalize(vec3(0, -0.5f, 1)));
    box(b, vec3(0, 0.005f, 0), vec3(0.16f, 0.005f, 0.11f), C(0.2f, 0.5f, 1.f, 0.6f), EM(), SK_NZ);
    b.pop();
    box(b, vec3(0, 0.33f, 1.02f), vec3(0.12f, 0.06f, 0.012f), Gy(0.6f), M(MAT_METAL_BRUSHED), SK_NONE);   // keypad shelf
    for (int k = 0; k < 12; k++) box(b, vec3(-0.06f + (k % 3) * 0.06f, 0.32f + (k / 3) * 0.022f, 1.034f), vec3(0.022f, 0.008f, 0.004f), Gy(0.3f), M(MAT_PLASTIC), SK_NZ);
    box(b, vec3(0, 0.301f, 1.52f), vec3(0.26f, 0.001f, 0.05f), C(0.2f, 0.8f, 0.4f, 0.6f), EM(), SK_NZ);
    textC(b, "ATM", vec3(0, 0.303f, 1.52f), vec3(1, 0, 0), vec3(0, 0, 1), 0.06f, 0.012f, Gy(1.f, 0.8f), EM(), 0.f, 0.3f);
    collide(b, vec3(0, 0, 0.75f), vec3(0.3f, 0.3f, 0.75f));
}

// Chest freezer with ice bags (front +y)
void iceChest(IB& b, vec3 p, float yaw) {
    At at(b, p, yaw);
    InPart ip(b, IP_FURNITURE);
    rbox(b, vec3(0, 0, 0.45f), vec3(0.6f, 0.4f, 0.45f), 0.03f, Gy(0.95f), M(MAT_PAINT_WHITE));
    box(b, vec3(0, 0.401f, 0.55f), vec3(0.5f, 0.001f, 0.2f), C(0.1f, 0.4f, 0.85f), M(MAT_PAINT_WHITE), SK_NZ);
    textC(b, "ICE", vec3(0, 0.403f, 0.55f), vec3(1, 0, 0), vec3(0, 0, 1), 0.22f, 0.04f, Gy(1.f), M(MAT_PAINT_WHITE), 0.f, 0.3f);
    box(b, vec3(0, 0.f, 0.905f), vec3(0.58f, 0.38f, 0.008f), Gy(0.85f), M(MAT_METAL_BRUSHED), SK_NZ);
    collide(b, vec3(0, 0, 0.45f), vec3(0.6f, 0.4f, 0.45f));
}

// Sign board hanging from the ceiling (two-sided text)
void hangingSign(IB& b, vec3 p, float yaw, const char* txt, u32 bg, u32 fg, float ceilZ) {
    At at(b, p, yaw);
    InPart ip(b, IP_FURNITURE);
    float w = sitegeo::textAdvance(txt, 0.16f, 0.3f) * 0.5f + 0.2f;
    box(b, vec3(0, 0, 0.f), vec3(w, 0.015f, 0.14f), bg, M(MAT_METAL_PAINTED), SK_NONE);
    for (int s = -1; s <= 1; s += 2) {
        At fl(b, vec3(0.f), s > 0 ? 0.f : kPi);
        textC(b, txt, vec3(0, 0.017f, 0.f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.16f, 0.026f, fg, M(MAT_PAINT_WHITE), 0.f, 0.3f);
    }
    for (int s = -1; s <= 1; s += 2) tube(b, vec3(s * (w - 0.1f), 0, 0.14f), vec3(s * (w - 0.1f), 0, ceilZ - p.z), 0.004f, 4, Gy(0.6f), M(MAT_METAL_BRUSHED));
}

// Paper poster on the inside of a shop window, readable from the street (text faces -y)
void windowPoster(IB& b, vec3 c, float w, float h, const char* txt, u32 bg, u32 fg) {
    InPart ip(b, IP_SHELL);
    // printed side toward the glass (-y), plain back toward the room
    quadF(b, vec3(c.x - w * 0.5f, c.y, c.z - h * 0.5f), vec3(c.x + w * 0.5f, c.y, c.z - h * 0.5f), vec3(c.x + w * 0.5f, c.y, c.z + h * 0.5f),
          vec3(c.x - w * 0.5f, c.y, c.z + h * 0.5f), vec3(0, -1, 0), bg, M(MAT_PAINT_WHITE));
    quadF(b, vec3(c.x - w * 0.5f, c.y + 0.001f, c.z - h * 0.5f), vec3(c.x + w * 0.5f, c.y + 0.001f, c.z - h * 0.5f), vec3(c.x + w * 0.5f, c.y + 0.001f, c.z + h * 0.5f),
          vec3(c.x - w * 0.5f, c.y + 0.001f, c.z + h * 0.5f), vec3(0, 1, 0), Gy(0.9f), M(MAT_PAINT_WHITE));
    float th = Min(h * 0.28f, w / Max(1.f, (float)strlen(txt)) * 1.2f);
    textC(b, txt, vec3(c.x, c.y - 0.002f, c.z), vec3(1, 0, 0), vec3(0, 0, 1), th, th * 0.16f, fg, M(MAT_PAINT_WHITE), 0.f, 0.3f);
}

// Wire storage shelving with cardboard boxes (front +y)
void storageShelf(IB& b, vec3 p, float yaw, float len, float h, u32 seed) {
    At at(b, p, yaw);
    Rng r(seed);
    float hl = len * 0.5f, dp = 0.25f;
    u32 wire = Gy(0.7f), wm = M(MAT_CHROME);
    for (int e = -1; e <= 1; e += 2)
        for (int f = -1; f <= 1; f += 2) tube(b, vec3(e * (hl - 0.02f), f * (dp - 0.02f), 0.f), vec3(e * (hl - 0.02f), f * (dp - 0.02f), h), 0.013f, 6, wire, wm);
    for (int lv = 0; lv < 4; lv++) {
        float z = 0.15f + lv * (h - 0.25f) / 3.f;
        box(b, vec3(0, 0, z), vec3(hl, dp, 0.01f), Gy(0.55f), M(MAT_METAL_BRUSHED), SK_NONE);
        float x = -hl + 0.04f;
        while (x < hl - 0.2f) {
            float w = r.range(0.25f, 0.5f), bh = r.range(0.15f, 0.35f), bd = r.range(0.15f, dp - 0.02f);
            if (x + w > hl - 0.03f) break;
            if (r.chance(0.8f)) {
                vec3 kraft = vec3(0.62f, 0.45f, 0.28f) * r.range(0.85f, 1.1f);
                box(b, vec3(x + w * 0.5f, dp - bd - 0.01f + bd * 0.5f, z + 0.01f + bh * 0.5f), vec3(w * 0.5f - 0.01f, bd * 0.5f, bh * 0.5f), C(kraft), M(MAT_METAL_PAINTED), SK_NZ);
                box(b, vec3(x + w * 0.5f, dp - 0.009f, z + 0.01f + bh * 0.5f), vec3(0.04f, 0.001f, bh * 0.5f), C(kraft * 1.2f), M(MAT_PAINT_WHITE), SK_NZ | SK_NY);   // tape
            }
            x += w + 0.02f;
        }
    }
    collide(b, vec3(0, 0, h * 0.5f), vec3(hl, dp, h * 0.5f));
}

// Office desk with a CRT/LCD CCTV monitor, chair, papers (front +y = where the user sits)
void officeDesk(IB& b, vec3 p, float yaw, u32 seed, bool cctv) {
    At at(b, p, yaw);
    Rng r(seed);
    u32 wood = C(0.45f, 0.32f, 0.2f);
    rbox(b, vec3(0, 0, 0.74f), vec3(0.6f, 0.35f, 0.018f), 0.006f, wood, M(MAT_WOOD));
    box(b, vec3(0.4f, 0.f, 0.36f), vec3(0.19f, 0.33f, 0.36f), wood, M(MAT_WOOD), SK_NZ);   // drawer pedestal
    for (int k = 0; k < 3; k++) box(b, vec3(0.4f, 0.331f, 0.14f + k * 0.22f), vec3(0.03f, 0.008f, 0.008f), Gy(0.7f), M(MAT_CHROME), SK_NZ);
    tube(b, vec3(-0.56f, -0.3f, 0.f), vec3(-0.56f, -0.3f, 0.72f), 0.02f, 6, Gy(0.3f), M(MAT_METAL_PAINTED));
    tube(b, vec3(-0.56f, 0.3f, 0.f), vec3(-0.56f, 0.3f, 0.72f), 0.02f, 6, Gy(0.3f), M(MAT_METAL_PAINTED));
    // monitor
    rbox(b, vec3(0, -0.2f, 0.98f), vec3(0.26f, 0.025f, 0.17f), 0.01f, Gy(0.08f), M(MAT_PLASTIC), true);
    tube(b, vec3(0, -0.22f, 0.76f), vec3(0, -0.22f, 0.83f), 0.02f, 6, Gy(0.1f), M(MAT_PLASTIC));
    box(b, vec3(0, -0.22f, 0.765f), vec3(0.1f, 0.07f, 0.008f), Gy(0.1f), M(MAT_PLASTIC), SK_NZ);
    if (cctv) {
        for (int k = 0; k < 4; k++)
            box(b, vec3(-0.12f + (k & 1) * 0.24f, -0.174f, 0.9f + (k / 2) * 0.16f), vec3(0.115f, 0.001f, 0.075f), C(0.4f + 0.1f * k, 0.5f, 0.45f, 0.35f), EM(), SK_NZ);
    } else {
        box(b, vec3(0, -0.174f, 0.98f), vec3(0.24f, 0.001f, 0.15f), C(0.3f, 0.55f, 0.9f, 0.45f), EM(), SK_NZ);
    }
    // keyboard, mouse, papers, mug
    rbox(b, vec3(0, 0.08f, 0.765f), vec3(0.22f, 0.07f, 0.01f), 0.004f, Gy(0.12f), M(MAT_PLASTIC));
    rbox(b, vec3(0.3f, 0.1f, 0.765f), vec3(0.03f, 0.05f, 0.012f), 0.01f, Gy(0.12f), M(MAT_PLASTIC));
    for (int k = 0; k < 3; k++) box(b, vec3(-0.35f + r.range(-0.03f, 0.03f), 0.05f + r.range(-0.03f, 0.03f), 0.76f + k * 0.002f), vec3(0.105f, 0.148f, 0.001f), Gy(0.95f), M(MAT_PAINT_WHITE), SK_NZ);
    cyl(b, vec3(0.45f, -0.1f, 0.758f), 0.04f, 0.04f, 0.1f, 10, C(hsv(r.f(), 0.5f, 0.8f)), M(MAT_PAINT_WHITE), false);
    collide(b, vec3(0, 0, 0.38f), vec3(0.6f, 0.35f, 0.38f));
}

// Swivel office chair (front +y)
void officeChair(IB& b, vec3 p, float yaw, u32 col) {
    At at(b, p, yaw);
    for (int k = 0; k < 5; k++) {
        float a = kTwoPi * k / 5.f;
        vec2 d(cosf(a), sinf(a));
        tube(b, vec3(0, 0, 0.08f), vec3(d * 0.3f, 0.06f), 0.018f, 5, Gy(0.1f), M(MAT_PLASTIC));
        sphere(b, vec3(d * 0.3f, 0.03f), 0.028f, 6, Gy(0.05f), M(MAT_RUBBER));
    }
    cyl(b, vec3(0, 0, 0.08f), 0.03f, 0.025f, 0.35f, 8, Gy(0.6f), M(MAT_CHROME), false);
    rbox(b, vec3(0, 0.02f, 0.47f), vec3(0.24f, 0.23f, 0.045f), 0.035f, col, M(MAT_CLOTH));
    b.pushAxes(vec3(0, -0.22f, 0.78f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.12f)), normalize(vec3(0, -0.12f, 1)));
    rbox(b, vec3(0.f), vec3(0.22f, 0.04f, 0.26f), 0.035f, col, M(MAT_CLOTH), true);
    b.pop();
    tube(b, vec3(0, -0.22f, 0.5f), vec3(0, -0.24f, 0.6f), 0.02f, 6, Gy(0.15f), M(MAT_PLASTIC));
}

// Security mirror dome (corner of the ceiling) and CCTV dome
void securityDome(IB& b, vec3 p, float r, bool camera) {
    InPart ip(b, IP_FURNITURE);
    b.pushAxes(p, vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 0, -1));
    sphere(b, vec3(0, 0, 0.f), r, 12, camera ? Gy(0.1f) : Gy(0.9f), camera ? M(MAT_GLASS) : M(MAT_CHROME), 0.5f);
    cyl(b, vec3(0.f, 0.f, -0.02f), r * 1.05f, r * 1.05f, 0.02f, 12, Gy(0.9f), M(MAT_PAINT_WHITE), false, true);
    b.pop();
}

// ------------------------------------------------------------------------------------------------ convenience store
void layoutConvenience(IB& b) {
    const InteriorDef& d = *b.d;
    Rng r(d.seed ^ 0xC0DEu);
    float X0 = d.x0 + kT, X1 = d.x1 - kT, Y0 = kT, Y1 = d.depth - kT;
    float H = d.ceil;
    float dx0 = 0, dx1 = 0, dh = 2.2f;
    mainDoorOpening(d, dx0, dx1, dh);
    float doorX = (dx0 + dx1) * 0.5f;
    bool backRoom = Y1 - Y0 > 10.f;
    float backY = backRoom ? Y1 - 2.9f : Y1;
    // palette: chain store look (white, accent color band)
    float hue = r.f();
    vec3 accent = hsv(hue, 0.75f, 0.7f);
    int shop = room(b, vec3(X0, Y0, 0.f), vec3(X1, backY, H), vec3(62.f, 64.f, 66.f), 0.05f, LS_ALWAYS);
    int back = backRoom ? room(b, vec3(X0, backY + 0.12f, 0.f), vec3(X1, Y1, H), vec3(18.f, 18.f, 17.f), 0.0f, LS_ALWAYS) : -1;
    // doors: automatic sliding entrance, staff door to the back room at the right end of the partition
    storefrontEntrance(b);
    float cs = (doorX - X0) > (X1 - doorX) ? -1.f : 1.f;   // counter on the roomier side of the door
    float staffX = cs > 0.f ? X0 + 0.75f : X1 - 0.75f;
    if (backRoom) door(b, vec3(staffX, backY + 0.06f, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.1f, DK_HINGED, 2, C(0.6f, 0.62f, 0.65f), false);
    // shell
    ShellStyle st;
    st.wallCol = C(0.93f, 0.93f, 0.9f);
    st.floorMat = M(MAT_TILE);
    st.floorCol = C(0.86f, 0.86f, 0.84f);
    st.floorUV = 1.f / 1.5f;   // 45 cm vinyl tiles
    st.ceilMat = M(MAT_CEILING_TILE);
    st.ceilCol = Gy(0.95f);
    st.baseH = 0.1f;
    st.baseCol = C(accent * 0.6f);
    st.baseMat = M(MAT_RUBBER);
    st.wainscotH = 1.05f;
    st.wainMat = M(MAT_METAL_PAINTED);
    st.wainCol = C(accent * 0.9f + vec3(0.1f));
    st.revealCol = Gy(0.8f);
    shell(b, shop, st);
    if (backRoom) {
        ShellStyle bs;
        bs.wallCol = C(0.75f, 0.75f, 0.72f);
        bs.floorMat = M(MAT_CONCRETE);
        bs.floorCol = Gy(0.8f);
        bs.ceilMat = M(MAT_CONCRETE);
        bs.ceilCol = Gy(0.7f);
        bs.baseH = 0.f;
        shell(b, back, bs);
        doorFrameInner(b, vec3(staffX, backY + 0.06f, 0.f), vec2(1, 0), vec2(0, 1), 0.9f, 2.1f, 0.12f, Gy(0.85f));
        // partition top between the two ceilings is hidden; partition faces come from both room shells
    }
    // ---- ceiling lights: troffer grid over the sales floor
    {
        int nx = Max(1, (int)((X1 - X0) / 2.6f)), ny = Max(1, (int)((backY - Y0) / 2.8f));
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                float x = X0 + (X1 - X0) * (i + 0.5f) / nx, y = Y0 + (backY - Y0) * (j + 0.5f) / ny;
                troffer(b, vec3(x, y, H), 1.2f, 0.6f, shop, 950.f, vec3(0.97f, 0.99f, 1.f), 7.5f);
            }
        if (backRoom) troffer(b, vec3((X0 + X1) * 0.5f, (backY + Y1) * 0.5f, H), 1.2f, 0.3f, back, 400.f, vec3(1.f, 0.97f, 0.9f), 5.5f);
    }
    // ---- checkout counter + tobacco wall on the counter side
    // back counter (0.6) + clerk aisle (0.9) + checkout counter half depth (0.37)
    float counterX = cs > 0.f ? X1 - 1.87f : X0 + 1.87f;
    float cy0 = Y0 + 0.9f, clen = Min(3.0f, (backY - Y0) * 0.3f);
    checkoutCounter(b, vec3(counterX, cy0 + clen * 0.5f, 0.f), cs > 0.f ? kHalfPi : -kHalfPi, clen, r.next());
    tobaccoWall(b, vec3(cs > 0.f ? X1 : X0, cy0 + clen * 0.5f, 0.f), cs > 0.f ? kHalfPi : -kHalfPi, clen + 0.4f, r.next());
    scenario(b, vec3(cs > 0.f ? X1 - 1.05f : X0 + 1.05f, cy0 + clen * 0.5f, 0.f), cs > 0.f ? kHalfPi : -kHalfPi, 0, SR_CLERK, SF_STAFF);
    marker(b, IM_COUNTER, vec3(counterX - cs * 0.95f, cy0 + clen * 0.5f, 0.f), cs > 0.f ? -kHalfPi : kHalfPi);
    marker(b, IM_SNACKS, vec3(counterX - cs * 0.95f, cy0 + clen * 0.5f, 0.f), cs > 0.f ? -kHalfPi : kHalfPi);
    // ---- coffee station along the opposite wall
    float wallOpp = cs > 0.f ? X0 : X1;
    float coffLen = Min(2.6f, (backY - Y0) * 0.35f);
    coffeeStation(b, vec3(wallOpp, Y0 + 2.2f + coffLen * 0.5f, 0.f), cs > 0.f ? -kHalfPi : kHalfPi, coffLen, r.next());
    // ---- coolers along the back (partition or back wall)
    float coolX0 = X0 + 0.2f, coolX1 = X1 - 0.2f;
    if (backRoom) {
        if (cs > 0.f) coolX0 = staffX + 0.65f;
        else coolX1 = staffX - 0.65f;
    }
    int ndoors = Clamp((int)((coolX1 - coolX0) / 0.76f), 2, 12);
    coolerRun(b, vec3((coolX0 + coolX1) * 0.5f, backY, 0.f), kPi, ndoors, r.next(), shop, "COLD DRINKS");
    // ---- gondola aisles in the band between the coffee side and the counter side (1.2 m aisles)
    float bx0 = cs > 0.f ? X0 + 0.6f + 1.15f : counterX + 0.37f + 1.3f;
    float bx1 = cs > 0.f ? counterX - 0.37f - 1.3f : X1 - 0.6f - 1.15f;
    float gy0 = Y0 + 2.5f, gy1 = backY - 0.78f - 1.4f;
    if (gy1 - gy0 > 1.5f && bx1 - bx0 > 0.9f) {
        int n = Max(1, (int)((bx1 - bx0 + 1.2f) / (0.9f + 1.2f)));
        for (int k = 0; k < n; k++) {
            float x = n == 1 ? (bx0 + bx1) * 0.5f : Lerp(bx0 + 0.45f, bx1 - 0.45f, (float)k / (n - 1));
            gondola(b, vec3(x, (gy0 + gy1) * 0.5f, 0.f), kHalfPi, gy1 - gy0, 1.55f, 4, r.next());
            static const char* kAisles[] = {"SNACKS", "GROCERY", "HOUSEHOLD", "CANDY", "HEALTH", "PET", "AUTO"};
            hangingSign(b, vec3(x, (gy0 + gy1) * 0.5f, H - 0.55f), kHalfPi, kAisles[(d.seed + k) % 7], C(accent * 0.8f), Gy(1.f), H);
            if (k == 0 || k == n - 1) {
                float sideX = x + (k == 0 ? -1.05f : 1.05f);
                scenario(b, vec3(sideX, gy0 + (gy1 - gy0) * r.range(0.25f, 0.75f), 0.f), k == 0 ? -kHalfPi : kHalfPi, 14, SR_SHOPPER, SF_OPTIONAL);
            }
        }
    }
    scenario(b, vec3((coolX0 + coolX1) * 0.5f + r.range(-1.f, 1.f), backY - 1.1f, 0.f), kPi, 14, SR_SHOPPER, SF_OPTIONAL);
    // ---- entrance zone: ice chest under the window, ATM, newspaper rack
    {
        float iceX = cs > 0.f ? Max(X0 + 0.75f, doorX - 2.4f) : Min(X1 - 0.75f, doorX + 2.4f);
        if (fabsf(iceX - doorX) > 1.5f) iceChest(b, vec3(iceX, Y0 + 0.48f, 0.f), 0.f);
        float atmX = cs > 0.f ? X0 + 0.4f : X1 - 0.4f;
        atm(b, vec3(atmX, Y0 + 1.35f, 0.f), cs > 0.f ? -kHalfPi : kHalfPi);
    }
    // ---- posters on the storefront glass
    {
        static const char* kPosters[] = {"SALE", "2 FOR 3", "LOTTO", "ICE COLD", "HOT COFFEE", "OPEN 24H", "SNACKS", "ATM INSIDE"};
        int pi = (int)(d.seed % 8u);
        for (const InteriorOpening& op : d.openings) {
            if (op.kind != OP_GLASS) continue;
            vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
            if (fabsf(a.y) > 0.1f) continue;
            float cx = (a.x + c.x) * 0.5f;
            if (fabsf(cx - doorX) < 1.2f) continue;
            float z0 = op.z0 - d.origin.z;
            if (r.chance(0.7f)) {
                vec3 bg = r.chance(0.5f) ? vec3(0.95f, 0.85f, 0.1f) : (r.chance(0.5f) ? vec3(0.9f, 0.1f, 0.1f) : vec3(0.95f));
                vec3 fg = bg.x > 0.9f && bg.z > 0.9f ? vec3(0.8f, 0.05f, 0.05f) : (bg.y > 0.5f ? vec3(0.8f, 0.05f, 0.05f) : vec3(1.f));
                windowPoster(b, vec3(cx + r.range(-0.3f, 0.3f), 0.04f, z0 + 0.95f), 0.65f, 0.9f, kPosters[(pi++) % 8], C(bg), C(fg));
            }
        }
    }
    // ---- security: convex mirror in the far corner, camera domes
    securityDome(b, vec3(cs > 0.f ? X0 + 0.35f : X1 - 0.35f, backY - 0.35f, H), 0.22f, false);
    securityDome(b, vec3((X0 + X1) * 0.5f, (Y0 + backY) * 0.5f, H), 0.07f, true);
    securityDome(b, vec3(counterX, cy0, H), 0.07f, true);
    // ---- back room: storage shelving, desk with CCTV, mop sink, cases of drinks
    if (backRoom) {
        InPart ip(b, IP_FURNITURE);
        float by = (backY + 0.12f + Y1) * 0.5f;
        float kx0 = cs > 0.f ? staffX + 0.8f : X0 + 0.3f, kx1 = cs > 0.f ? X1 - 0.3f : staffX - 0.8f;
        if (kx1 - kx0 > 2.5f) storageShelf(b, vec3((kx0 + kx1) * 0.5f + 0.6f, Y1 - 0.28f, 0.f), kPi, Min(3.6f, kx1 - kx0 - 1.4f), 1.9f, r.next());
        officeDesk(b, vec3(cs > 0.f ? kx0 + 0.7f : kx1 - 0.7f, by, 0.f), cs > 0.f ? -kHalfPi : kHalfPi, r.next(), true);
        officeChair(b, vec3(cs > 0.f ? kx0 + 1.4f : kx1 - 1.4f, by, 0.f), cs > 0.f ? -kHalfPi : kHalfPi, C(0.1f, 0.1f, 0.12f));
        // stacked cases of soda
        for (int k = 0; k < 6; k++) {
            float x = (kx0 + kx1) * 0.5f + (k % 3) * 0.42f - 0.4f, z = (k / 3) * 0.26f;
            box(b, vec3(x, by + 0.1f, z + 0.13f), vec3(0.2f, 0.28f, 0.13f), C(hsv(0.6f + k * 0.07f, 0.6f, 0.6f)), M(MAT_METAL_PAINTED), SK_NZ);
        }
        collide(b, vec3((kx0 + kx1) * 0.5f + 0.02f, by + 0.1f, 0.26f), vec3(0.62f, 0.28f, 0.26f));
        // mop bucket
        cyl(b, vec3(cs > 0.f ? X1 - 0.5f : X0 + 0.5f, by - 0.4f, 0.f), 0.18f, 0.2f, 0.32f, 12, C(0.95f, 0.8f, 0.1f), M(MAT_METAL_PAINTED), false);
        tube(b, vec3(cs > 0.f ? X1 - 0.5f : X0 + 0.5f, by - 0.4f, 0.1f), vec3(cs > 0.f ? X1 - 0.55f : X0 + 0.55f, by - 0.3f, 1.3f), 0.012f, 6, C(0.6f, 0.45f, 0.3f), M(MAT_WOOD));
        scenario(b, vec3(cs > 0.f ? kx0 + 1.4f : kx1 - 1.4f, by, 0.f), cs > 0.f ? kHalfPi : -kHalfPi, 6, SR_WORKER, SF_OPTIONAL | SF_STAFF);
    }
}

// ------------------------------------------------------------------------------------------------ dispatch
void runLayout(IB& b) {
    b.roomCounter = 0;
    b.doorCounter = 0;
    b.f = Frame();
    b.part = IP_FURNITURE;
    tExtraHoles.clear();
    switch (b.d->kind) {
        case IK_CONVENIENCE: layoutConvenience(b); break;
        default: layoutConvenience(b); break;
    }
}

}  // namespace ikit
}  // namespace World
