// SkyLine metro cars (cab car and intermediate car) and the Bay Ferry, built procedurally like the vehicle catalog:
// model space +X right, +Y forward, +Z up, origin at top-of-rail (waterline for the ferry) under the body center.
// Materials follow vehicle_models.h: MAT_CARPAINT (vertex alpha 1 = primary livery white, alpha 0 = secondary teal),
// MAT_CAR_WINDOW see-through glass (rgb tint, alpha clarity), MAT_LIGHT_TAIL lamps (vertex green > 0.5 = white lamp lit
// by the reverse bit, used for the leading cab's headlights; red lamps lit by the lights / brake bits), MAT_EMISSIVE
// interior lighting and destination displays. Three levels of detail: 0 full interior, 1 simplified cabin, 2 shell.
#include "gameworld.h"

namespace Game {
namespace TransitModels {

namespace tm_detail {

constexpr float kHalfLen = 9.0f, kHalfW = 1.45f, kFloorZ = 1.12f, kRoofZ = 3.74f, kShoulderZ = 3.30f, kSkirtZ = 0.92f;
constexpr float kWinZ0 = 1.95f, kWinZ1 = 2.95f, kDoorZ1 = 3.08f, kDoorHalf = 0.7f;
constexpr float kCeilZ = 3.02f;
const float kDoorY[3] = {-5.6f, 0.f, 5.6f};

inline u32 col(float r, float g, float b, float a = 1.f) { return packRGBA8(r, g, b, a); }
inline u32 col(float v) { return packRGBA8(v, v, v, 1.f); }
const u32 kPrimary = 0xffffffffu;   // paint slot 0 (alpha 1)
const u32 kSecondary = 0x00ffffffu; // paint slot 1 (alpha 0)

// Side wall half width at height z (slight tumblehome above and below the waist)
float sideX(float z) {
    if (z > 1.6f) {
        float t = (z - 1.6f) / (kShoulderZ - 1.6f);
        return kHalfW - 0.055f * t * t;
    }
    float t = (1.6f - z) / (1.6f - kSkirtZ);
    return kHalfW - 0.06f * t * t;
}

struct MB {
    MeshData& m;
    int lod;
    void quad(vec3 a, vec3 b, vec3 c, vec3 d, u32 cl, u32 mat, vec3 facing) {
        float lu = length(b - a), lv = length(d - a);
        m.quadFacing(a, b, c, d, vec2(0, 0), vec2(lu, 0), vec2(lu, lv), vec2(0, lv), cl, mat, facing);
    }
    void box(vec3 c, vec3 he, u32 cl, u32 mat, bool bottom = true) { m.box(c, vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), he, cl, mat, bottom); }
    void boxAx(vec3 c, vec3 ax, vec3 ay, vec3 he, u32 cl, u32 mat) { m.box(c, normalize(ax), normalize(ay), normalize(cross(ax, ay)), he, cl, mat, true); }
    void cylX(vec3 c, float r, float halfW, int seg, u32 cl, u32 mat) {
        // cylinder along X (wheels)
        u32 start = (u32)m.verts.size();
        for (int k = 0; k <= seg; k++) {
            float a = kTwoPi * k / seg;
            vec3 n(0, cosf(a), sinf(a));
            m.addVertex(c + vec3(-halfW, 0, 0) + n * r, n, vec3(1, 0, 0), vec2(a * r, 0), cl, makeMat(mat));
            m.addVertex(c + vec3(halfW, 0, 0) + n * r, n, vec3(1, 0, 0), vec2(a * r, halfW * 2), cl, makeMat(mat));
        }
        for (int k = 0; k < seg; k++) {
            u32 i0 = start + k * 2;
            m.quadIdx(i0, i0 + 2, i0 + 3, i0 + 1);
        }
        for (int sd = -1; sd <= 1; sd += 2) {
            u32 cc = m.addVertex(c + vec3(sd * halfW, 0, 0), vec3((float)sd, 0, 0), vec3(0, 1, 0), vec2(0), cl, makeMat(mat));
            u32 first = (u32)m.verts.size();
            for (int k = 0; k <= seg; k++) {
                float a = kTwoPi * k / seg;
                m.addVertex(c + vec3(sd * halfW, cosf(a) * r, sinf(a) * r), vec3((float)sd, 0, 0), vec3(0, 1, 0), vec2(cosf(a), sinf(a)) * r, cl, makeMat(mat));
            }
            for (int k = 0; k < seg; k++) {
                if (sd > 0) m.tri(cc, first + k, first + k + 1);
                else m.tri(cc, first + k + 1, first + k);
            }
        }
    }
    void cylZ(vec3 base, float r, float h, int seg, u32 cl, u32 mat) { m.cylinder(base, r, r, h, seg, cl, makeMat(mat), true); }
    void rod(vec3 a, vec3 b, float r, int seg, u32 cl, u32 mat) {
        vec3 t = b - a;
        float len = length(t);
        if (len < 1e-4f) return;
        t = t / len;
        vec3 n = normalize(anyPerp(t));
        vec3 bn = cross(t, n);
        u32 base = (u32)m.verts.size();
        for (int k = 0; k <= seg; k++) {
            float an = kTwoPi * k / seg;
            vec3 dir = n * cosf(an) + bn * sinf(an);
            m.addVertex(a + dir * r, dir, t, vec2((float)k / seg, 0), cl, makeMat(mat));
            m.addVertex(b + dir * r, dir, t, vec2((float)k / seg, len), cl, makeMat(mat));
        }
        for (int k = 0; k < seg; k++) {
            u32 i0 = base + k * 2;
            m.quadIdx(i0, i0 + 2, i0 + 3, i0 + 1);
        }
    }
};

// Stroke text on a plane (glyphs from the world stroke font: World::sitegeo, available in the unity build)
void strokeTextM(MeshData& m, const char* txt, vec3 origin, vec3 right, vec3 up, float h, float w, u32 cl, u32 mat) {
    World::sitegeo::G g;
    g.m = &m;
    g.d = &m;
    g.org = vec3(0.f);
    World::sitegeo::strokeText(g, m, txt, origin, right, up, h, w, cl, mat, 0.f);
}

// y intervals of the side wall: 0 solid, 1 window, 2 door opening, 3 cab side window
struct Span {
    float y0, y1;
    int type;
};

std::vector<Span> sideSpans(bool cab) {
    std::vector<Span> s;
    auto add = [&](float a, float b, int t) { s.push_back({a, b, t}); };
    // rear end
    add(-kHalfLen, -8.25f, 0);
    add(-8.25f, -6.5f, 1);
    add(-6.5f, kDoorY[0] - kDoorHalf, 0);
    for (int d = 0; d < 3; d++) {
        add(kDoorY[d] - kDoorHalf, kDoorY[d] + kDoorHalf, 2);
        if (d < 2) {
            float a = kDoorY[d] + kDoorHalf, b = kDoorY[d + 1] - kDoorHalf;
            float pil = 0.2f, w = (b - a - 3.f * pil) * 0.5f;
            add(a, a + pil, 0);
            add(a + pil, a + pil + w, 1);
            add(a + pil + w, a + 2.f * pil + w, 0);
            add(a + 2.f * pil + w, b - pil, 1);
            add(b - pil, b, 0);
        }
    }
    add(kDoorY[2] + kDoorHalf, 6.5f, 0);
    if (cab) {
        add(6.5f, 7.35f, 1);
        add(7.35f, 7.6f, 0);
        add(7.6f, 8.45f, 3);   // driver's side window
        add(8.45f, kHalfLen, 0);
    } else {
        add(6.5f, 8.25f, 1);
        add(8.25f, kHalfLen, 0);
    }
    return s;
}

// Wall panel between heights z0..z1 over a y span on one side (sd = +1 right, -1 left), following the tumblehome
void wallPanel(MB& b, int sd, float y0, float y1, float z0, float z1, u32 cl, u32 mat) {
    int n = z1 - z0 > 0.6f ? 3 : 1;
    for (int i = 0; i < n; i++) {
        float za = Lerp(z0, z1, (float)i / n), zb = Lerp(z0, z1, (float)(i + 1) / n);
        float xa = sideX(za) * sd, xb = sideX(zb) * sd;
        b.quad(vec3(xa, y0, za), vec3(xa, y1, za), vec3(xb, y1, zb), vec3(xb, y0, zb), cl, mat, vec3((float)sd, 0, 0));
    }
}

void livery(MB& b, int sd, float y0, float y1, float z0, float z1) {
    // horizontal bands: white lower body, teal belt stripe, white, (window band handled by the caller), white upper
    struct Band { float a, b; u32 c; };
    const Band bands[] = {{kSkirtZ, 1.52f, kPrimary}, {1.52f, 1.74f, kSecondary}, {1.74f, kWinZ0, kPrimary}, {kWinZ0, kWinZ1, col(0.06f, 0.07f, 0.08f)},
                          {kWinZ1, kShoulderZ, kPrimary}};
    for (const Band& bd : bands) {
        float a = Max(bd.a, z0), c = Min(bd.b, z1);
        if (c - a < 1e-3f) continue;
        bool band = bd.a == kWinZ0;
        wallPanel(b, sd, y0, y1, a, c, bd.c, band ? makeMat(MAT_PLASTIC) : makeMat(MAT_CARPAINT));
    }
}

void windowGlass(MB& b, int sd, float y0, float y1, float z0, float z1, float clarity, bool frame) {
    float x0 = sideX(z0) * sd, x1 = sideX(z1) * sd;
    float inset = 0.02f * sd;
    u32 gc = col(0.86f, 0.93f, 0.95f, clarity);
    b.quad(vec3(x0 - inset, y0, z0), vec3(x0 - inset, y1, z0), vec3(x1 - inset, y1, z1), vec3(x1 - inset, y0, z1), gc, makeMat(MAT_CAR_WINDOW), vec3((float)sd, 0, 0));
    if (frame && b.lod == 0) {
        // rubber gasket around the pane
        u32 rc = col(0.05f, 0.05f, 0.05f);
        float t = 0.035f;
        b.quad(vec3(x0 - inset * 0.5f, y0, z0), vec3(x0 - inset * 0.5f, y1, z0), vec3(x0 - inset * 0.5f, y1, z0 + t), vec3(x0 - inset * 0.5f, y0, z0 + t), rc,
               makeMat(MAT_RUBBER), vec3((float)sd, 0, 0));
        b.quad(vec3(x1 - inset * 0.5f, y0, z1 - t), vec3(x1 - inset * 0.5f, y1, z1 - t), vec3(x1 - inset * 0.5f, y1, z1), vec3(x1 - inset * 0.5f, y0, z1), rc,
               makeMat(MAT_RUBBER), vec3((float)sd, 0, 0));
    }
}

// Seats: forward-facing pairs (the passenger pose faces the car's +Y)
void seatPair(MB& b, float xc, float y, bool simple) {
    u32 cushion = col(0.1f, 0.32f, 0.42f), shell = col(0.72f, 0.74f, 0.76f);
    if (simple) {
        b.box(vec3(xc, y, kFloorZ + 0.5f), vec3(0.44f, 0.24f, 0.07f), cushion, makeMat(MAT_FABRIC));
        b.box(vec3(xc, y - 0.22f, kFloorZ + 0.85f), vec3(0.44f, 0.05f, 0.33f), cushion, makeMat(MAT_FABRIC));
        return;
    }
    b.box(vec3(xc, y, kFloorZ + 0.44f), vec3(0.45f, 0.24f, 0.035f), shell, makeMat(MAT_PLASTIC));
    b.box(vec3(xc, y + 0.01f, kFloorZ + 0.5f), vec3(0.43f, 0.22f, 0.04f), cushion, makeMat(MAT_FABRIC));
    b.boxAx(vec3(xc, y - 0.25f, kFloorZ + 0.86f), vec3(1, 0, 0), vec3(0, 0.98f, 0.2f), vec3(0.45f, 0.035f, 0.38f), shell, makeMat(MAT_PLASTIC));
    b.boxAx(vec3(xc, y - 0.215f, kFloorZ + 0.87f), vec3(1, 0, 0), vec3(0, 0.98f, 0.2f), vec3(0.42f, 0.02f, 0.34f), cushion, makeMat(MAT_FABRIC));
    // pedestal and grab handle on the aisle corner
    b.box(vec3(xc, y, kFloorZ + 0.21f), vec3(0.05f, 0.18f, 0.21f), col(0.3f), makeMat(MAT_METAL_PAINTED));
    float aisle = xc > 0 ? xc - 0.45f : xc + 0.45f;
    b.rod(vec3(aisle, y - 0.27f, kFloorZ + 1.15f), vec3(aisle, y - 0.27f, kFloorZ + 1.3f), 0.018f, 6, col(0.85f), MAT_CHROME);
}

}  // namespace tm_detail

using namespace tm_detail;

// Passenger seat slots used by gameplay (hip positions); seat 0 is the driver's (cab) or a crew jump seat
void carSeats(bool cab, std::vector<Vehicles::SeatSpec>& out) {
    out.clear();
    out.push_back(Vehicles::SeatSpec{cab ? vec3(0.45f, 7.95f, kFloorZ + 0.62f) : vec3(0.62f, -7.35f, kFloorZ + 0.55f), true, false});
    // aisle seats of the forward-facing pairs (see buildCar's seat rows), alternating sides
    const vec2 slots[7] = {vec2(0.62f, -3.95f), vec2(-0.62f, -2.35f), vec2(0.62f, 1.65f), vec2(-0.62f, 3.25f), vec2(-0.62f, -7.35f), vec2(0.62f, 7.05f),
                           vec2(-0.62f, 1.65f)};
    for (int i = 0; i < 7; i++) {
        vec2 q = slots[i];
        if (cab && q.y > 6.5f) q.y = 6.9f;
        out.push_back(Vehicles::SeatSpec{vec3(q.x, q.y, kFloorZ + 0.55f), false, q.x < 0.f});
    }
}

void buildCar(bool cab, int lod, MeshData& m) {
    MB b{m, lod};
    float clarity = lod >= 2 ? 0.32f : 0.9f;
    float yFront = cab ? 9.28f : kHalfLen;   // cab nose extends a little
    // ------------------------------------------------------------------ side walls with windows and door openings
    std::vector<Span> spans = sideSpans(cab);
    for (int sd = -1; sd <= 1; sd += 2) {
        for (const Span& sp : spans) {
            switch (sp.type) {
                case 0:
                    livery(b, sd, sp.y0, sp.y1, kSkirtZ, kShoulderZ);
                    break;
                case 1:
                case 3: {
                    float w0 = sp.type == 3 ? 2.05f : kWinZ0, w1 = sp.type == 3 ? 2.9f : kWinZ1;
                    livery(b, sd, sp.y0, sp.y1, kSkirtZ, w0);
                    livery(b, sd, sp.y0, sp.y1, w1, kShoulderZ);
                    windowGlass(b, sd, sp.y0 + 0.03f, sp.y1 - 0.03f, w0, w1, clarity, true);
                    // window frame reveals (short depth faces)
                    if (lod < 2) {
                        float xo = sideX(w0) * sd, xi = xo - 0.06f * sd;
                        b.quad(vec3(xo, sp.y0, w0), vec3(xi, sp.y0, w0), vec3(xi, sp.y1, w0), vec3(xo, sp.y1, w0), col(0.08f), makeMat(MAT_PLASTIC), vec3(0, 0, 1));
                    }
                    break;
                }
                case 2: {
                    // door opening: step plate below the floor, header above the door; leaves are separate draws
                    livery(b, sd, sp.y0, sp.y1, kSkirtZ, kFloorZ - 0.02f);
                    livery(b, sd, sp.y0, sp.y1, kDoorZ1, kShoulderZ);
                    if (lod >= 2) {
                        // far LOD: doors closed and baked in
                        float x = sideX(2.f) * sd;
                        b.quad(vec3(x, sp.y0, kFloorZ), vec3(x, sp.y1, kFloorZ), vec3(x, sp.y1, kDoorZ1), vec3(x, sp.y0, kDoorZ1), kSecondary, makeMat(MAT_CARPAINT),
                               vec3((float)sd, 0, 0));
                        b.quad(vec3(x + 0.01f * sd, sp.y0 + 0.08f, 1.9f), vec3(x + 0.01f * sd, sp.y1 - 0.08f, 1.9f), vec3(x + 0.01f * sd, sp.y1 - 0.08f, 2.85f),
                               vec3(x + 0.01f * sd, sp.y0 + 0.08f, 2.85f), col(0.8f, 0.86f, 0.88f, 0.3f), makeMat(MAT_CAR_WINDOW), vec3((float)sd, 0, 0));
                    } else {
                        // door frame (reveals) and a threshold plate
                        float xo = sideX(2.f) * sd, xi = xo - 0.1f * sd;
                        u32 fc = col(0.55f, 0.57f, 0.6f);
                        b.quad(vec3(xo, sp.y0, kFloorZ), vec3(xi, sp.y0, kFloorZ), vec3(xi, sp.y0, kDoorZ1), vec3(xo, sp.y0, kDoorZ1), fc, makeMat(MAT_METAL_BRUSHED),
                               vec3(0, 1, 0));
                        b.quad(vec3(xi, sp.y1, kFloorZ), vec3(xo, sp.y1, kFloorZ), vec3(xo, sp.y1, kDoorZ1), vec3(xi, sp.y1, kDoorZ1), fc, makeMat(MAT_METAL_BRUSHED),
                               vec3(0, -1, 0));
                        b.quad(vec3(xo, sp.y0, kDoorZ1), vec3(xo, sp.y1, kDoorZ1), vec3(xi, sp.y1, kDoorZ1), vec3(xi, sp.y0, kDoorZ1), fc, makeMat(MAT_METAL_BRUSHED),
                               vec3(0, 0, -1));
                        b.quad(vec3(xi, sp.y0, kFloorZ + 0.005f), vec3(xi, sp.y1, kFloorZ + 0.005f), vec3(xo, sp.y1, kFloorZ + 0.005f), vec3(xo, sp.y0, kFloorZ + 0.005f),
                               col(0.95f, 0.8f, 0.15f), makeMat(MAT_METAL_PAINTED), vec3(0, 0, 1));
                    }
                    break;
                }
            }
        }
        // SkyLine lettering on the lower body (secondary paint) and the car number
        if (lod < 2) {
            float x = sideX(1.25f) * sd + 0.004f * sd;
            vec3 right = sd > 0 ? vec3(0, 1, 0) : vec3(0, -1, 0);
            float w = 1.9f;
            vec3 o(x, sd > 0 ? -3.5f - w * 0.5f : -3.5f + w * 0.5f, 1.12f);
            strokeTextM(m, "SKYLINE", o, right, vec3(0, 0, 1), 0.26f, 0.045f, kSecondary, makeMat(MAT_CARPAINT));
        }
    }
    // ------------------------------------------------------------------ roof: shoulder curve to the crown, AC units
    {
        const int n = lod >= 2 ? 3 : 6;
        std::vector<vec2> prof;   // (x, z) from the right shoulder over the crown to the left shoulder
        for (int i = 0; i <= n; i++) {
            float a = (float)i / n * kPi * 0.5f;
            float x = sideX(kShoulderZ) * cosf(a) * 0.35f + sideX(kShoulderZ) * 0.65f * (1.f - (float)i / n);
            float z = kShoulderZ + (kRoofZ - kShoulderZ) * sinf(a);
            prof.push_back(vec2(x, z));
        }
        for (int sd = -1; sd <= 1; sd += 2)
            for (int i = 0; i < n; i++) {
                vec2 p0 = prof[i], p1 = prof[i + 1];
                vec3 facing(sd * (p1.y - p0.y), 0, p0.x - p1.x);   // outward normal of the profile edge
                u32 rc = i == 0 ? kPrimary : col(0.78f, 0.8f, 0.82f);
                u32 rm = i == 0 ? makeMat(MAT_CARPAINT) : makeMat(MAT_METAL_PAINTED);
                b.quad(vec3(sd * p0.x, -kHalfLen, p0.y), vec3(sd * p0.x, yFront - (cab ? 0.9f : 0.f), p0.y), vec3(sd * p1.x, yFront - (cab ? 0.9f : 0.f), p1.y),
                       vec3(sd * p1.x, -kHalfLen, p1.y), rc, rm, facing);
            }
        if (lod < 2) {
            // air conditioning pods and a pantograph-free roof walkway
            b.box(vec3(0, -4.2f, kRoofZ + 0.13f), vec3(0.85f, 1.3f, 0.15f), col(0.72f, 0.74f, 0.76f), makeMat(MAT_METAL_PAINTED));
            b.box(vec3(0, 4.2f, kRoofZ + 0.13f), vec3(0.85f, 1.3f, 0.15f), col(0.72f, 0.74f, 0.76f), makeMat(MAT_METAL_PAINTED));
            for (int k = -1; k <= 1; k += 2)
                for (int g = 0; g < 3; g++) b.box(vec3(0, k * 4.2f - 0.8f + g * 0.8f, kRoofZ + 0.285f), vec3(0.6f, 0.25f, 0.01f), col(0.2f), makeMat(MAT_METAL_PAINTED));
        }
    }
    // ------------------------------------------------------------------ ends: rear gangway wall, cab nose or front gangway wall
    auto flatEnd = [&](float y, float dirSign) {
        // end wall with the gangway opening framed by bellows
        u32 ec = kPrimary;
        float zs = kSkirtZ, zr = kShoulderZ;
        std::vector<vec2> prof;
        prof.push_back(vec2(-sideX(zs), zs));
        prof.push_back(vec2(sideX(zs), zs));
        prof.push_back(vec2(sideX(zr), zr));
        prof.push_back(vec2(0.f, kRoofZ));
        prof.push_back(vec2(-sideX(zr), zr));
        std::vector<vec3> poly;
        for (auto& p : prof) poly.push_back(vec3(p.x, y, p.y));
        vec3 n(0, dirSign, 0);
        // triangulate the pentagon as a fan (convex)
        for (size_t i = 1; i + 1 < poly.size(); i++) {
            vec3 a = poly[0], bb = poly[i], c = poly[i + 1];
            vec3 fn = cross(bb - a, c - a);
            u32 i0 = m.addVertex(a, n, vec3(1, 0, 0), vec2(a.x, a.z), ec, makeMat(MAT_CARPAINT));
            u32 i1 = m.addVertex(bb, n, vec3(1, 0, 0), vec2(bb.x, bb.z), ec, makeMat(MAT_CARPAINT));
            u32 i2 = m.addVertex(c, n, vec3(1, 0, 0), vec2(c.x, c.z), ec, makeMat(MAT_CARPAINT));
            if (dot(fn, n) >= 0.f) m.tri(i0, i1, i2);
            else m.tri(i0, i2, i1);
        }
        // bellows gangway
        b.box(vec3(0, y + dirSign * 0.16f, 2.12f), vec3(0.78f, 0.16f, 1.02f), col(0.1f), makeMat(MAT_RUBBER));
        if (lod < 2)
            for (int k = 0; k < 5; k++) b.box(vec3(0, y + dirSign * 0.16f, 1.2f + k * 0.45f), vec3(0.8f, 0.17f, 0.03f), col(0.06f), makeMat(MAT_RUBBER));
        // coupler
        b.box(vec3(0, y + dirSign * 0.25f, 0.82f), vec3(0.18f, 0.25f, 0.14f), col(0.2f), makeMat(MAT_METAL_PAINTED));
    };
    flatEnd(-kHalfLen, -1.f);
    if (!cab) flatEnd(kHalfLen, 1.f);
    else {
        // --- cab nose: raked windscreen, lamp clusters, destination display, anti-climber, coupler cover
        float y0 = 8.38f;   // where the nose starts tapering
        float zc = kShoulderZ;
        // side and roof taper from y0 to the front face
        int nz = 8;
        auto noseX = [&](float z) { return sideX(z) * 0.94f; };
        float yf0 = 9.26f, yf1 = 8.95f;   // front face y at the bottom / top (rake)
        auto faceY = [&](float z) { return Lerp(yf0, yf1, Saturate((z - kSkirtZ) / (kRoofZ - kSkirtZ))); };
        for (int sd = -1; sd <= 1; sd += 2)
            for (int i = 0; i < nz; i++) {
                float za = Lerp(kSkirtZ, zc, (float)i / nz), zb = Lerp(kSkirtZ, zc, (float)(i + 1) / nz);
                vec3 a0(sideX(za) * sd, kHalfLen - 0.62f, za), a1(noseX(za) * sd, faceY(za), za), b1(noseX(zb) * sd, faceY(zb), zb), b0(sideX(zb) * sd, kHalfLen - 0.62f, zb);
                u32 cc = (za >= 1.52f && zb <= 1.74f + 0.01f) ? kSecondary : kPrimary;
                b.quad(a0, a1, b1, b0, cc, makeMat(MAT_CARPAINT), vec3((float)sd, 0.35f, 0));
            }
        // roof cap over the cab
        {
            std::vector<vec3> rim;
            for (int i = 0; i <= 6; i++) {
                float a = (float)i / 6 * kPi;
                float x = cosf(a) * noseX(zc);
                float z = zc + (kRoofZ - zc) * sinf(a);
                rim.push_back(vec3(x, faceY(z), z));
            }
            for (int i = 0; i < 6; i++) {
                vec3 p0 = rim[i], p1 = rim[i + 1];
                vec3 q0(p0.x * (sideX(zc) / Max(noseX(zc), 0.1f)), kHalfLen - 0.9f, p0.z), q1(p1.x * (sideX(zc) / Max(noseX(zc), 0.1f)), kHalfLen - 0.9f, p1.z);
                b.quad(q0, p0, p1, q1, col(0.78f, 0.8f, 0.82f), makeMat(MAT_METAL_PAINTED), vec3(0, 0.3f, 1));
            }
        }
        // front face: lower panel (white + teal), windscreen band, destination display strip
        auto facePt = [&](float x, float z) { return vec3(x, faceY(z), z); };
        float fx0 = noseX(kSkirtZ), fxW = noseX(2.f);
        b.quad(facePt(-fx0, kSkirtZ), facePt(fx0, kSkirtZ), facePt(fxW, 1.52f), facePt(-fxW, 1.52f), kPrimary, makeMat(MAT_CARPAINT), vec3(0, 1, 0));
        b.quad(facePt(-fxW, 1.52f), facePt(fxW, 1.52f), facePt(fxW, 1.9f), facePt(-fxW, 1.9f), kSecondary, makeMat(MAT_CARPAINT), vec3(0, 1, 0));
        float wz0 = 1.9f, wz1 = 3.12f;
        b.quad(facePt(-fxW, wz0), facePt(fxW, wz0), facePt(noseX(wz1), wz1), facePt(-noseX(wz1), wz1), col(0.7f, 0.8f, 0.82f, lod >= 2 ? 0.3f : 0.75f),
               makeMat(MAT_CAR_WINDOW), vec3(0, 1, 0.3f));
        b.quad(facePt(-noseX(wz1), wz1), facePt(noseX(wz1), wz1), facePt(noseX(zc), zc + 0.01f), facePt(-noseX(zc), zc + 0.01f), col(0.05f), makeMat(MAT_PLASTIC),
               vec3(0, 1, 0.3f));
        // destination display (amber LEDs)
        vec3 dc = facePt(0.f, 3.2f) + vec3(0, 0.012f, 0);
        m.quadFacing(dc + vec3(-0.75f, 0, -0.07f), dc + vec3(0.75f, 0, -0.07f), dc + vec3(0.75f, 0, 0.07f), dc + vec3(-0.75f, 0, 0.07f), vec2(0), vec2(1, 0), vec2(1, 1),
                     vec2(0, 1), col(1.f, 0.6f, 0.15f, 0.35f), makeMat(MAT_EMISSIVE), vec3(0, 1, 0));
        if (lod < 2) strokeTextM(m, "SKYLINE LOOP", dc + vec3(0.62f, 0.004f, -0.055f), vec3(-1, 0, 0), vec3(0, 0, 1), 0.11f, 0.016f, col(0.1f, 0.05f, 0.f),
                                 makeMat(MAT_PLASTIC));
        // lamp clusters: white head lamp (reverse-lamp slot, lit on the leading cab) + red tail lamp outboard
        for (int sd = -1; sd <= 1; sd += 2) {
            vec3 hc = facePt(sd * 0.82f, 1.3f) + vec3(0, 0.02f, 0);
            b.box(hc, vec3(0.2f, 0.03f, 0.09f), col(0.08f), makeMat(MAT_PLASTIC));
            b.box(hc + vec3(-sd * 0.07f, 0.02f, 0), vec3(0.09f, 0.02f, 0.06f), col(1.f, 1.f, 1.f), makeMat(MAT_LIGHT_TAIL));    // white (green > 0.5)
            b.box(hc + vec3(sd * 0.13f, 0.02f, 0), vec3(0.05f, 0.02f, 0.05f), col(1.f, 0.1f, 0.1f), makeMat(MAT_LIGHT_TAIL));   // red
            // marker lights on the roof edge of the cab
            b.box(facePt(sd * 0.6f, 3.36f) + vec3(0, 0.02f, 0), vec3(0.05f, 0.02f, 0.03f), col(1.f, 1.f, 1.f), makeMat(MAT_LIGHT_TAIL));
        }
        // anti-climber and coupler cover
        b.box(vec3(0, yf0 + 0.05f, 1.0f), vec3(1.05f, 0.08f, 0.09f), col(0.25f), makeMat(MAT_METAL_PAINTED));
        b.box(vec3(0, yf0 + 0.04f, 0.72f), vec3(0.5f, 0.07f, 0.16f), col(0.15f), makeMat(MAT_PLASTIC));
        // wipers
        if (lod < 2)
            for (int sd = -1; sd <= 1; sd += 2)
                b.boxAx(facePt(sd * 0.45f, 2.3f) + vec3(0, 0.03f, 0), vec3(0.3f, 0, 0.9f), vec3(0, 1, 0), vec3(0.4f, 0.01f, 0.012f), col(0.05f), makeMat(MAT_RUBBER));
    }
    // ------------------------------------------------------------------ skirts, underframe, bogies
    {
        u32 dark = col(0.16f, 0.17f, 0.19f);
        b.box(vec3(0, 0, kSkirtZ - 0.02f), vec3(kHalfW - 0.1f, kHalfLen - 0.3f, 0.02f), dark, makeMat(MAT_METAL_PAINTED));
        if (lod < 2) {
            for (int k = -2; k <= 2; k++) {
                float y = k * 1.7f;
                b.box(vec3(0, y, 0.68f), vec3(1.1f, 0.7f, 0.22f), col(0.22f, 0.23f, 0.25f), makeMat(MAT_METAL_PAINTED));
            }
        } else {
            b.box(vec3(0, 0, 0.68f), vec3(1.1f, 4.2f, 0.22f), col(0.22f, 0.23f, 0.25f), makeMat(MAT_METAL_PAINTED));
        }
        for (int bg = -1; bg <= 1; bg += 2) {
            float yb = bg * 6.3f;
            b.box(vec3(0, yb, 0.62f), vec3(1.2f, 1.45f, 0.12f), dark, makeMat(MAT_METAL_PAINTED));
            for (int sd = -1; sd <= 1; sd += 2) {
                b.box(vec3(sd * 1.02f, yb, 0.45f), vec3(0.08f, 1.5f, 0.14f), dark, makeMat(MAT_METAL_PAINTED));
                for (int ax = -1; ax <= 1; ax += 2) {
                    vec3 wc(sd * 0.72f, yb + ax * 1.1f, 0.42f);
                    if (lod == 0) b.cylX(wc, 0.42f, 0.07f, 16, col(0.55f, 0.55f, 0.56f), MAT_METAL_BRUSHED);
                    else if (lod == 1) b.cylX(wc, 0.42f, 0.07f, 8, col(0.5f), MAT_METAL_BRUSHED);
                    else b.box(wc, vec3(0.07f, 0.38f, 0.38f), col(0.4f), makeMat(MAT_METAL_PAINTED));
                    if (lod == 0) b.box(vec3(sd * 1.04f, yb + ax * 1.1f, 0.44f), vec3(0.06f, 0.18f, 0.12f), col(0.3f), makeMat(MAT_METAL_PAINTED));   // axle box
                }
                // collector shoe on the third rail side and a coil spring
                if (lod == 0) {
                    b.box(vec3(sd * 1.5f, yb, 0.16f), vec3(0.14f, 0.25f, 0.02f), col(0.2f), makeMat(MAT_METAL_PAINTED));
                    b.rod(vec3(sd * 1.02f, yb, 0.58f), vec3(sd * 1.02f, yb, 0.84f), 0.1f, 8, col(0.35f), MAT_METAL_PAINTED);
                }
            }
        }
    }
    // ------------------------------------------------------------------ interior (seen through the windows)
    if (lod <= 1) {
        float yMin = -kHalfLen + 0.06f, yMax = cab ? 7.4f : kHalfLen - 0.06f;
        float xi = kHalfW - 0.07f;
        u32 floorC = col(0.2f, 0.25f, 0.3f), wallC = col(0.85f, 0.86f, 0.86f), ceilC = col(0.92f, 0.93f, 0.94f);
        b.quad(vec3(-xi, yMin, kFloorZ), vec3(xi, yMin, kFloorZ), vec3(xi, yMax, kFloorZ), vec3(-xi, yMax, kFloorZ), floorC, makeMat(MAT_RUBBER), vec3(0, 0, 1));
        b.quad(vec3(-xi, yMin, kCeilZ), vec3(xi, yMin, kCeilZ), vec3(xi, yMax, kCeilZ), vec3(-xi, yMax, kCeilZ), ceilC, makeMat(MAT_PLASTIC), vec3(0, 0, -1));
        // inner side walls (below and above the windows; the door openings stay open)
        for (int sd = -1; sd <= 1; sd += 2)
            for (const Span& sp : sideSpans(cab)) {
                if (sp.y1 > yMax + 0.01f && sp.y0 >= yMax) continue;
                float y1 = Min(sp.y1, yMax);
                if (sp.type == 2) continue;
                float zLo = sp.type == 0 ? kCeilZ : kWinZ0, zHi = sp.type == 0 ? kFloorZ : kWinZ1;
                if (sp.type == 0) {
                    b.quad(vec3(sd * xi, sp.y0, kFloorZ), vec3(sd * xi, y1, kFloorZ), vec3(sd * xi, y1, kCeilZ), vec3(sd * xi, sp.y0, kCeilZ), wallC, makeMat(MAT_PLASTIC),
                           vec3((float)-sd, 0, 0));
                } else {
                    b.quad(vec3(sd * xi, sp.y0, kFloorZ), vec3(sd * xi, y1, kFloorZ), vec3(sd * xi, y1, zLo), vec3(sd * xi, sp.y0, zLo), wallC, makeMat(MAT_PLASTIC),
                           vec3((float)-sd, 0, 0));
                    b.quad(vec3(sd * xi, sp.y0, zHi), vec3(sd * xi, y1, zHi), vec3(sd * xi, y1, kCeilZ), vec3(sd * xi, sp.y0, kCeilZ), wallC, makeMat(MAT_PLASTIC),
                           vec3((float)-sd, 0, 0));
                }
            }
        // end walls inside
        b.quad(vec3(-xi, yMin, kFloorZ), vec3(xi, yMin, kFloorZ), vec3(xi, yMin, kCeilZ), vec3(-xi, yMin, kCeilZ), wallC, makeMat(MAT_PLASTIC), vec3(0, 1, 0));
        b.quad(vec3(-xi, yMax, kFloorZ), vec3(xi, yMax, kFloorZ), vec3(xi, yMax, kCeilZ), vec3(-xi, yMax, kCeilZ), cab ? col(0.3f, 0.32f, 0.35f) : wallC,
               makeMat(MAT_PLASTIC), vec3(0, -1, 0));
        // lighting strips (always on) along the ceiling edges
        for (int sd = -1; sd <= 1; sd += 2)
            b.quad(vec3(sd * 0.55f, yMin + 0.4f, kCeilZ - 0.01f), vec3(sd * 0.75f, yMin + 0.4f, kCeilZ - 0.01f), vec3(sd * 0.75f, yMax - 0.4f, kCeilZ - 0.01f),
                   vec3(sd * 0.55f, yMax - 0.4f, kCeilZ - 0.01f), col(1.f, 0.98f, 0.94f, 0.3f), makeMat(MAT_EMISSIVE), vec3(0, 0, -1));
        // seats: forward-facing pairs in every window bay on both sides
        std::vector<float> rows = {-7.35f, -3.95f, -2.35f, 1.65f, 3.25f};
        if (!cab) rows.push_back(7.05f);
        else rows.push_back(6.9f);
        for (float y : rows)
            for (int sd = -1; sd <= 1; sd += 2) seatPair(b, sd * 0.84f, y, lod == 1);
        // aisle seats used by gameplay slots are the rows above (x = +-0.38 sits on the aisle half of the pairs)
        if (lod == 0) {
            u32 chrome = col(0.9f, 0.9f, 0.92f);
            // vertical grab poles in the door vestibules and a ceiling grab rail
            for (float dy : kDoorY) {
                if (cab && dy > 6.f) continue;
                for (int sd = -1; sd <= 1; sd += 2) b.rod(vec3(sd * 0.55f, dy, kFloorZ), vec3(sd * 0.55f, dy, kCeilZ), 0.019f, 8, chrome, MAT_CHROME);
            }
            for (int sd = -1; sd <= 1; sd += 2) b.rod(vec3(sd * 0.5f, yMin + 0.5f, kCeilZ - 0.18f), vec3(sd * 0.5f, yMax - 0.5f, kCeilZ - 0.18f), 0.017f, 6, chrome, MAT_CHROME);
            // line map strips above the windows (backlit)
            for (int sd = -1; sd <= 1; sd += 2)
                b.quad(vec3(sd * (xi - 0.01f), -4.4f, 2.97f), vec3(sd * (xi - 0.01f), -0.9f, 2.97f), vec3(sd * (xi - 0.01f), -0.9f, 3.0f), vec3(sd * (xi - 0.01f), -4.4f, 3.0f),
                       col(0.3f, 0.85f, 0.9f, 0.25f), makeMat(MAT_EMISSIVE), vec3((float)-sd, 0, 0));
            // next-stop display over the middle doors
            b.box(vec3(0, kDoorY[1] + 0.8f, kCeilZ - 0.1f), vec3(0.5f, 0.03f, 0.08f), col(0.05f), makeMat(MAT_PLASTIC));
            b.quad(vec3(-0.46f, kDoorY[1] + 0.766f, kCeilZ - 0.16f), vec3(0.46f, kDoorY[1] + 0.766f, kCeilZ - 0.16f), vec3(0.46f, kDoorY[1] + 0.766f, kCeilZ - 0.04f),
                   vec3(-0.46f, kDoorY[1] + 0.766f, kCeilZ - 0.04f), col(1.f, 0.55f, 0.12f, 0.3f), makeMat(MAT_EMISSIVE), vec3(0, -1, 0));
        }
        if (cab) {
            // driver's cab: console, seat, bulkhead door window
            b.box(vec3(0, 8.55f, kFloorZ + 0.5f), vec3(1.2f, 0.3f, 0.5f), col(0.2f, 0.21f, 0.23f), makeMat(MAT_PLASTIC));
            b.box(vec3(0.45f, 7.95f, kFloorZ + 0.3f), vec3(0.25f, 0.25f, 0.3f), col(0.1f), makeMat(MAT_LEATHER));
            if (lod == 0) {
                b.quad(vec3(-0.7f, 8.58f, kFloorZ + 1.0f), vec3(0.7f, 8.58f, kFloorZ + 1.0f), vec3(0.7f, 8.4f, kFloorZ + 1.08f), vec3(-0.7f, 8.4f, kFloorZ + 1.08f),
                       col(0.2f, 0.6f, 0.9f, 0.2f), makeMat(MAT_EMISSIVE), vec3(0, -0.4f, 1));
            }
            b.box(vec3(0, 7.46f, 2.05f), vec3(xi, 0.04f, 0.95f), col(0.3f, 0.32f, 0.35f), makeMat(MAT_PLASTIC));
        }
    } else {
        // far LOD: dark cabin block behind the tinted glass
        b.box(vec3(0, 0, 2.05f), vec3(kHalfW - 0.12f, kHalfLen - 0.3f, 0.92f), col(0.12f, 0.13f, 0.15f), makeMat(MAT_PLASTIC));
    }
}

// One sliding door leaf (0.7 m wide): origin at the leaf center on the car's side plane, +X outward, +Y along the car.
// Secondary livery with a tall window.
void buildDoorLeaf(MeshData& m) {
    MB b{m, 0};
    float hw = kDoorHalf * 0.5f, z0 = kFloorZ, z1 = kDoorZ1 - 0.01f;
    float zc = (z0 + z1) * 0.5f, hz = (z1 - z0) * 0.5f;
    // frame: bottom panel, top rail, stiles (secondary paint)
    b.box(vec3(0, 0, z0 + 0.4f), vec3(0.025f, hw, 0.4f), kSecondary, makeMat(MAT_CARPAINT));
    b.box(vec3(0, 0, z1 - 0.07f), vec3(0.025f, hw, 0.07f), kSecondary, makeMat(MAT_CARPAINT));
    for (int sd = -1; sd <= 1; sd += 2) b.box(vec3(0, sd * (hw - 0.04f), zc), vec3(0.025f, 0.04f, hz), kSecondary, makeMat(MAT_CARPAINT));
    // window
    for (int sd = -1; sd <= 1; sd += 2)
        b.quad(vec3(sd * 0.01f, -hw + 0.08f, z0 + 0.8f), vec3(sd * 0.01f, hw - 0.08f, z0 + 0.8f), vec3(sd * 0.01f, hw - 0.08f, z1 - 0.14f), vec3(sd * 0.01f, -hw + 0.08f, z1 - 0.14f),
               col(0.86f, 0.93f, 0.95f, 0.9f), makeMat(MAT_CAR_WINDOW), vec3((float)sd, 0, 0));
    // rubber edge seal and a handle bar inside
    b.box(vec3(0, -hw + 0.01f, zc), vec3(0.03f, 0.012f, hz), col(0.04f), makeMat(MAT_RUBBER));
    b.box(vec3(-0.05f, 0, z0 + 1.0f), vec3(0.015f, hw * 0.5f, 0.015f), col(0.85f), makeMat(MAT_CHROME));
}

void carSpec(bool cab, Vehicles::VehicleModel& o) {
    o.name = cab ? "SkyLine cab car" : "SkyLine car";
    o.maker = "Palmera Transit";
    o.cls = Vehicles::VC_BUS;   // gameplay class of large passenger vehicles; never spawned by traffic (spawnWeight 0)
    o.mass = 32000.f;
    o.power = 600.f;
    o.torque = 9000.f;
    o.maxRpm = 3000.f;
    o.topSpeed = 25.f;
    o.gears = 1;
    o.brakeForce = 60000.f;
    o.dragCoef = 0.6f;
    o.frontalArea = 9.f;
    o.centerOfMass = vec3(0, 0, 1.3f);
    o.engineSound = Audio::ENGINE_ELECTRIC;
    o.spawnWeight = 0.f;
    o.price = 0;
    o.fixedLivery = true;
    o.liveryPrimary = vec3(0.86f, 0.87f, 0.86f);
    o.liverySecondary = vec3(0.01f, 0.36f, 0.41f);
    o.paletteColors.push_back(o.liveryPrimary);
    for (int bg = -1; bg <= 1; bg += 2)
        for (int sd = -1; sd <= 1; sd += 2) {
            Vehicles::WheelSpec w;
            w.pos = vec3(sd * 0.72f, bg * 6.3f, 0.42f);
            w.radius = 0.42f;
            w.width = 0.14f;
            w.steer = false;
            w.drive = true;
            w.left = sd < 0;
            o.wheels.push_back(w);
        }
    carSeats(cab, o.seats);
    if (cab) {
        for (int sd = -1; sd <= 1; sd += 2) {
            o.lights.push_back(Vehicles::LightSpec{vec3(sd * 0.8f, 9.2f, 1.3f), vec3(0, 1, -0.05f), Vehicles::LT_HEAD});
            o.lights.push_back(Vehicles::LightSpec{vec3(sd * 0.95f, 9.2f, 1.3f), vec3(0, 1, 0), Vehicles::LT_TAIL});
        }
    }
    o.boxCenter = vec3(0, 0, 2.2f);
    o.boxHalf = vec3(kHalfW, kHalfLen + (cab ? 0.25f : 0.f), 1.55f);
}

}  // namespace TransitModels
}  // namespace Game
