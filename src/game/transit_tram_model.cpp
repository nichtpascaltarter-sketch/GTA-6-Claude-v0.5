// Sol Beach Streetcar: a three-module low-floor tram (front cab module, centre module with the pantograph, rear module
// with a tail cab), built procedurally like the SkyLine cars. Model space +X right (the door side: a one-way loop always
// stops at the right-hand curb), +Y forward, +Z up, origin at top of rail under the module centre. Paint slot 0 (vertex
// alpha 1) = warm white body, slot 1 (alpha 0) = coral skirt, doors and pinstripe. Levels of detail: 0 full interior,
// 1 simple cabin, 2 closed shell. Door leaves are separate models (drawn open or shut by the gameplay layer).
// Included by transit_models.cpp.

namespace Game {
namespace TransitModels {

namespace trm {

constexpr float kTW = 1.2f;          // half width at the waist
constexpr float kTSkirt = 0.16f, kTFloor = 0.36f, kTWin0 = 1.0f, kTWin1 = 2.36f, kTShoulder = 2.92f, kTRoof = 3.2f;
constexpr float kTDoorHalf = 0.66f, kTDoorTop = 2.3f;
constexpr float kTCeil = 2.56f;
constexpr float kTNose = 0.95f;      // plan depth of the cab nose
const u32 kTWhite = 0xffffffffu, kTCoral = 0x00ffffffu;

inline u32 tc(float r, float g, float b, float a = 1.f) { return packRGBA8(r, g, b, a); }
inline u32 tc(float v) { return packRGBA8(v, v, v, 1.f); }

float tHalfLen(int sec) { return World::tram_dims::kSectionLen[sec] * 0.5f; }

// door centres (right side) per module
void tDoorsOf(int sec, std::vector<float>& out) {
    out.clear();
    if (sec == 0) {
        out.push_back(1.95f);
        out.push_back(-2.75f);
    } else if (sec == 1) {
        out.push_back(0.f);
    } else {
        out.push_back(2.75f);
        out.push_back(-1.95f);
    }
}

// side wall half width at height z (tumblehome toward the roof and the skirt)
float tSideX(float z) {
    if (z > 1.8f) {
        float t = Saturate((z - 1.8f) / (kTShoulder - 1.8f));
        return kTW - 0.07f * t * t;
    }
    float t = Saturate((0.55f - z) / (0.55f - kTSkirt));
    return kTW - 0.035f * t * t;
}

struct TB {
    MeshData& m;
    int lod;
    void quad(vec3 a, vec3 b, vec3 c, vec3 d, u32 cl, u32 mat, vec3 facing) {
        float lu = length(b - a), lv = length(d - a);
        m.quadFacing(a, b, c, d, vec2(0, 0), vec2(lu, 0), vec2(lu, lv), vec2(0, lv), cl, mat, facing);
    }
    void box(vec3 c, vec3 he, u32 cl, u32 mat) { m.box(c, vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), he, cl, mat, true); }
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
        // the ring runs counter-clockwise around t (n -> bn): wind each strip quad outward
        for (int k = 0; k < seg; k++) {
            u32 i0 = base + k * 2;
            m.quadIdx(i0, i0 + 2, i0 + 3, i0 + 1);
        }
    }
};

// wall strip on one side between z0..z1 over y0..y1, following the tumblehome
void tWall(TB& b, int sd, float y0, float y1, float z0, float z1, u32 cl, u32 mat) {
    int n = z1 - z0 > 0.5f ? 3 : 1;
    for (int i = 0; i < n; i++) {
        float za = Lerp(z0, z1, (float)i / n), zb = Lerp(z0, z1, (float)(i + 1) / n);
        float xa = tSideX(za) * sd, xb = tSideX(zb) * sd;
        b.quad(vec3(xa, y0, za), vec3(xa, y1, za), vec3(xb, y1, zb), vec3(xb, y0, zb), cl, mat, vec3((float)sd, 0, 0));
    }
}

// the livery bands of a solid wall stretch between z0..z1: coral skirt, white body, coral pinstripe under the roof
void liveryWall(TB& b, int sd, float y0, float y1, float z0, float z1) {
    struct Band {
        float a, c;
        u32 col;
        u32 mat;
    };
    const Band bands[] = {{kTSkirt, 0.62f, kTCoral, makeMat(MAT_CARPAINT)},
                          {0.62f, 0.68f, tc(0.12f, 0.13f, 0.14f), makeMat(MAT_PLASTIC)},
                          {0.68f, kTWin0, kTWhite, makeMat(MAT_CARPAINT)},
                          {kTWin0, kTWin1, tc(0.07f, 0.08f, 0.09f), makeMat(MAT_PLASTIC)},
                          {kTWin1, 2.52f, kTWhite, makeMat(MAT_CARPAINT)},
                          {2.52f, 2.6f, kTCoral, makeMat(MAT_CARPAINT)},
                          {2.6f, kTShoulder, kTWhite, makeMat(MAT_CARPAINT)}};
    for (const Band& bd : bands) {
        float a = Max(bd.a, z0), c = Min(bd.c, z1);
        if (c - a < 1e-3f) continue;
        tWall(b, sd, y0, y1, a, c, bd.col, bd.mat);
    }
}

void tGlass(TB& b, int sd, float y0, float y1, float z0, float z1, float clarity) {
    float x0 = tSideX(z0) * sd - 0.015f * sd, x1 = tSideX(z1) * sd - 0.015f * sd;
    b.quad(vec3(x0, y0, z0), vec3(x0, y1, z0), vec3(x1, y1, z1), vec3(x1, y0, z1), tc(0.84f, 0.92f, 0.94f, clarity), makeMat(MAT_CAR_WINDOW), vec3((float)sd, 0, 0));
}

// span list along one side: 0 wall, 1 window, 2 door opening
struct TSpan {
    float y0, y1;
    int type;
};

void tSideSpans(int sec, int sd, float yA, float yB, std::vector<TSpan>& out) {
    // yA..yB is the straight side between the ends (the cab noses start beyond)
    out.clear();
    std::vector<float> doors;
    if (sd > 0) tDoorsOf(sec, doors);
    std::vector<std::pair<float, float>> cuts;
    for (float d : doors) cuts.push_back({d - kTDoorHalf, d + kTDoorHalf});
    // the left side mirrors the door rhythm with wide pillars instead of openings
    if (sd < 0) {
        tDoorsOf(sec, doors);
        for (float d : doors) cuts.push_back({d - 0.18f, d + 0.18f});
    }
    std::sort(cuts.begin(), cuts.end());
    float y = yA;
    auto windows = [&](float a, float b) {
        // windows between pillars (~1.35 m pitch)
        float len = b - a;
        if (len < 0.5f) {
            out.push_back({a, b, 0});
            return;
        }
        int n = Max(1, (int)roundf(len / 1.45f));
        float pil = 0.12f;
        float w = (len - pil * (n + 1)) / n;
        float q = a;
        for (int i = 0; i < n; i++) {
            out.push_back({q, q + pil, 0});
            out.push_back({q + pil, q + pil + w, 1});
            q += pil + w;
        }
        out.push_back({q, b, 0});
    };
    for (auto& c : cuts) {
        if (c.first > y) windows(y, c.first);
        out.push_back({Max(c.first, y), c.second, sd > 0 ? 2 : 0});
        y = c.second;
    }
    if (yB > y) windows(y, yB);
}

// ---- cab nose: plan outline from the right side wall around the front to the left side (u 0..1)
struct Nose {
    float y0;      // where the straight side ends
    float dir;     // +1 nose toward +Y, -1 toward -Y
    // point on the nose surface at parameter u (0 right side .. 1 left side) and height z
    vec3 at(float u, float z) const {
        float th = u * kPi;
        float c = cosf(th), s = sinf(th);
        float w = tSideX(z);
        // superellipse-ish plan: a broad flat front with rounded corners
        float px = (c >= 0.f ? 1.f : -1.f) * powf(fabsf(c), 0.55f);
        float py = powf(fabsf(s), 0.35f);
        // raked windscreen: the front recedes toward the roof
        float depth = kTNose;
        if (z > 1.05f) depth -= (z - 1.05f) * 0.2f;
        if (z > 2.6f) depth -= (z - 2.6f) * 0.9f;
        float taper = z < 0.4f ? 0.97f : 1.f;
        return vec3(w * px * taper, y0 + dir * depth * py, z);
    }
};

void buildNose(TB& b, float y0, float dir, bool lead, float clarity) {
    Nose N{y0, dir};
    const int nu = b.lod >= 2 ? 8 : 16;
    // horizontal bands of the nose: z breaks and what each band is
    struct Row {
        float z0, z1;
        int kind;   // 0 coral skirt, 1 white, 2 windscreen, 3 display band, 4 roof cap
    };
    const Row rows[] = {{kTSkirt, 0.62f, 0}, {0.62f, 1.05f, 1}, {1.05f, 2.42f, 2}, {2.42f, 2.72f, 3}, {2.72f, kTShoulder, 1}};
    for (const Row& r : rows) {
        int nz = r.kind == 2 ? 3 : 1;
        for (int iz = 0; iz < nz; iz++) {
            float za = Lerp(r.z0, r.z1, (float)iz / nz), zb = Lerp(r.z0, r.z1, (float)(iz + 1) / nz);
            for (int i = 0; i < nu; i++) {
                float ua = (float)i / nu, ub = (float)(i + 1) / nu;
                vec3 p00 = N.at(ua, za), p10 = N.at(ub, za), p11 = N.at(ub, zb), p01 = N.at(ua, zb);
                vec3 mid = (p00 + p11) * 0.5f;
                vec3 outward = normalize(vec3(mid.x, (mid.y - y0) * 1.6f + dir * 0.2f, 0.f));
                float um = (ua + ub) * 0.5f;
                bool front = um > 0.2f && um < 0.8f;
                u32 cl = kTWhite, mat = makeMat(MAT_CARPAINT);
                if (r.kind == 0) cl = kTCoral;
                else if (r.kind == 2) {
                    if (front) {
                        cl = tc(0.8f, 0.88f, 0.9f, clarity * 0.85f);
                        mat = makeMat(MAT_CAR_WINDOW);
                    } else {
                        cl = tc(0.07f, 0.08f, 0.09f);
                        mat = makeMat(MAT_PLASTIC);
                    }
                } else if (r.kind == 3) {
                    cl = tc(0.05f, 0.05f, 0.06f);
                    mat = makeMat(MAT_PLASTIC);
                }
                b.quad(p00, p10, p11, p01, cl, mat, outward);
                // inner skin of the cab (seen through the windscreen)
                if (b.lod < 2 && (mat & 0xffu) != MAT_CAR_WINDOW) b.quad(p00, p10, p11, p01, tc(0.3f, 0.31f, 0.32f), makeMat(MAT_PLASTIC), -outward);
            }
        }
    }
    // close the roof arch where the body roof meets the nose cap (the arch bulges past the cap's straight rim)
    {
        const int n = 6;
        for (int sd = -1; sd <= 1; sd += 2)
            for (int i = 0; i < n; i++) {
                auto arch = [&](int k) {
                    float a = (float)k / n * kPi * 0.5f;
                    float x = tSideX(kTShoulder) * (cosf(a) * 0.4f + 0.6f * (1.f - (float)k / n));
                    return vec3(sd * x, y0, kTShoulder + (kTRoof - kTShoulder) * sinf(a));
                };
                vec3 p0 = arch(i), p1 = arch(i + 1);
                vec3 base(sd * tSideX(kTShoulder), y0, kTShoulder);
                if (i == 0) continue;
                vec3 fn = cross(p0 - base, p1 - base);
                vec3 want(0, dir, 0);
                u32 i0 = b.m.addVertex(base, want, vec3(1, 0, 0), vec2(0), tc(0.8f, 0.81f, 0.82f), makeMat(MAT_METAL_PAINTED));
                u32 i1 = b.m.addVertex(p0, want, vec3(1, 0, 0), vec2(0), tc(0.8f, 0.81f, 0.82f), makeMat(MAT_METAL_PAINTED));
                u32 i2 = b.m.addVertex(p1, want, vec3(1, 0, 0), vec2(0), tc(0.8f, 0.81f, 0.82f), makeMat(MAT_METAL_PAINTED));
                if (dot(fn, want) >= 0.f) b.m.tri(i0, i1, i2);
                else b.m.tri(i0, i2, i1);
            }
    }
    // roof cap over the nose: fan from the crown line
    {
        vec3 crown(0.f, y0, kTRoof);
        for (int i = 0; i < nu; i++) {
            vec3 a = N.at((float)i / nu, kTShoulder), c = N.at((float)(i + 1) / nu, kTShoulder);
            vec3 n = cross(c - a, crown - a);
            if (n.z < 0.f) n = -n;
            u32 i0 = b.m.addVertex(a, normalize(n), vec3(1, 0, 0), vec2(a.x, a.y), tc(0.8f, 0.81f, 0.82f), makeMat(MAT_METAL_PAINTED));
            u32 i1 = b.m.addVertex(c, normalize(n), vec3(1, 0, 0), vec2(c.x, c.y), tc(0.8f, 0.81f, 0.82f), makeMat(MAT_METAL_PAINTED));
            u32 i2 = b.m.addVertex(crown, normalize(n), vec3(1, 0, 0), vec2(0.f, y0), tc(0.8f, 0.81f, 0.82f), makeMat(MAT_METAL_PAINTED));
            vec3 fn = cross(c - a, crown - a);
            if (fn.z >= 0.f) b.m.tri(i0, i1, i2);
            else b.m.tri(i0, i2, i1);
        }
    }
    // underside of the nose
    {
        vec3 base(0.f, y0, kTSkirt);
        for (int i = 0; i < nu; i++) {
            vec3 a = N.at((float)i / nu, kTSkirt), c = N.at((float)(i + 1) / nu, kTSkirt);
            u32 i0 = b.m.addVertex(a, vec3(0, 0, -1), vec3(1, 0, 0), vec2(0), tc(0.1f), makeMat(MAT_PLASTIC));
            u32 i1 = b.m.addVertex(c, vec3(0, 0, -1), vec3(1, 0, 0), vec2(0), tc(0.1f), makeMat(MAT_PLASTIC));
            u32 i2 = b.m.addVertex(base, vec3(0, 0, -1), vec3(1, 0, 0), vec2(0), tc(0.1f), makeMat(MAT_PLASTIC));
            vec3 fn = cross(c - a, base - a);
            if (fn.z <= 0.f) b.m.tri(i0, i1, i2);
            else b.m.tri(i0, i2, i1);
        }
    }
    // lamps: head lamps on the leading nose (white, lit with the lights), tail lamps on both (red)
    for (int sd = -1; sd <= 1; sd += 2) {
        float u = sd > 0 ? 0.3f : 0.7f;
        vec3 p = N.at(u, 0.82f);
        vec3 out = normalize(vec3(p.x * 0.4f, dir, 0.f));
        vec3 right = normalize(cross(vec3(0, 0, 1), out));
        vec3 c = p + out * 0.012f;
        if (lead) {
            b.quad(c - right * 0.14f - vec3(0, 0, 0.06f), c + right * 0.14f - vec3(0, 0, 0.06f), c + right * 0.14f + vec3(0, 0, 0.06f), c - right * 0.14f + vec3(0, 0, 0.06f),
                   tc(0.95f, 0.95f, 0.92f), makeMat(MAT_LIGHT_HEAD), out);
        }
        vec3 t = N.at(sd > 0 ? 0.22f : 0.78f, 0.82f) + out * 0.012f;
        b.quad(t - right * 0.05f - vec3(0, 0, 0.05f), t + right * 0.05f - vec3(0, 0, 0.05f), t + right * 0.05f + vec3(0, 0, 0.05f), t - right * 0.05f + vec3(0, 0, 0.05f),
               tc(0.5f, 0.03f, 0.03f), makeMat(MAT_LIGHT_TAIL), out);
    }
    // destination display text
    if (b.lod < 2) {
        vec3 p = N.at(0.5f, 2.57f);
        vec3 out(0.f, dir, 0.f);
        vec3 right = normalize(cross(vec3(0, 0, 1), out));
        const char* txt = lead ? "SOL BEACH LOOP" : "STREETCAR";
        float h = 0.13f;
        float w = World::sitegeo::textAdvance(txt, h);
        if (w > 1.5f) {
            h *= 1.5f / w;
            w = 1.5f;
        }
        strokeTextM(b.m, txt, p + out * 0.02f - right * (w * 0.5f) - vec3(0, 0, h * 0.5f), right, vec3(0, 0, 1), h, h * 0.17f, tc(1.f, 0.62f, 0.12f, 0.45f),
                    makeMat(MAT_EMISSIVE));
    }
    // coupler cover and a wiper on the leading nose
    if (b.lod < 2) {
        vec3 p = N.at(0.5f, 0.3f);
        b.box(p + vec3(0, dir * 0.03f, 0.f), vec3(0.3f, 0.06f, 0.1f), tc(0.12f), makeMat(MAT_PLASTIC));
        if (lead) {
            vec3 w0 = N.at(0.42f, 1.15f) + vec3(0, dir * 0.03f, 0), w1 = N.at(0.52f, 1.95f) + vec3(0, dir * 0.03f, 0);
            b.rod(w0, w1, 0.012f, 4, tc(0.05f), MAT_RUBBER);
        }
    }
}

// Joint end: flat wall with the bellows ring reaching half the gap toward the next module
void buildJoint(TB& b, float y, float dir) {
    std::vector<vec2> prof = {vec2(-tSideX(kTSkirt), kTSkirt), vec2(tSideX(kTSkirt), kTSkirt), vec2(tSideX(1.8f), 1.8f), vec2(tSideX(kTShoulder), kTShoulder),
                              vec2(0.f, kTRoof), vec2(-tSideX(kTShoulder), kTShoulder), vec2(-tSideX(1.8f), 1.8f)};
    vec3 n(0, dir, 0);
    for (size_t i = 1; i + 1 < prof.size(); i++) {
        vec3 a(prof[0].x, y, prof[0].y), c1(prof[i].x, y, prof[i].y), c2(prof[i + 1].x, y, prof[i + 1].y);
        u32 i0 = b.m.addVertex(a, n, vec3(1, 0, 0), vec2(a.x, a.z), kTWhite, makeMat(MAT_CARPAINT));
        u32 i1 = b.m.addVertex(c1, n, vec3(1, 0, 0), vec2(c1.x, c1.z), kTWhite, makeMat(MAT_CARPAINT));
        u32 i2 = b.m.addVertex(c2, n, vec3(1, 0, 0), vec2(c2.x, c2.z), kTWhite, makeMat(MAT_CARPAINT));
        if (dot(cross(c1 - a, c2 - a), n) >= 0.f) b.m.tri(i0, i1, i2);
        else b.m.tri(i0, i2, i1);
    }
    // bellows: folded rubber ring (a stack of slightly differing boxes)
    float half = World::tram_dims::kSectionGap * 0.5f;
    for (int k = 0; k < (b.lod >= 2 ? 1 : 3); k++) {
        float f = (k + 0.5f) / (b.lod >= 2 ? 1 : 3);
        float inset = (k & 1) ? 0.05f : 0.f;
        b.box(vec3(0, y + dir * half * f, 1.55f), vec3(kTW - 0.1f - inset, half / (b.lod >= 2 ? 1.f : 3.f) * 0.5f + 0.01f, 1.35f - inset), tc(0.08f),
              makeMat(MAT_RUBBER));
    }
}

}  // namespace trm

using namespace trm;

void tramSeats(int sec, std::vector<Vehicles::SeatSpec>& out) {
    out.clear();
    float hl = tHalfLen(sec);
    // seat 0: the driver in the front cab; the other modules keep slot 0 as a jump seat by the joint
    if (sec == 0) out.push_back(Vehicles::SeatSpec{vec3(0.f, hl - 1.05f, kTFloor + 0.55f), true, false});
    else out.push_back(Vehicles::SeatSpec{vec3(-0.75f, hl - 0.9f, kTFloor + 0.5f), false, true});
    // facing bays on the left side (and one on the right between the doors)
    std::vector<vec2> slots;
    if (sec == 1) {
        slots = {vec2(-0.72f, -2.4f), vec2(-0.72f, -1.4f), vec2(-0.72f, 1.5f), vec2(-0.72f, 2.5f)};
    } else {
        float s = sec == 0 ? 1.f : -1.f;   // the rear module mirrors the front
        slots = {vec2(-0.72f, s * 0.7f), vec2(-0.72f, s * -0.3f), vec2(-0.72f, s * -1.5f), vec2(-0.72f, s * -3.6f), vec2(0.72f, s * -0.4f), vec2(0.72f, s * 0.6f)};
    }
    for (vec2 q : slots) out.push_back(Vehicles::SeatSpec{vec3(q.x, q.y, kTFloor + 0.5f), false, q.x < 0.f});
}

void buildTram(int sec, int lod, MeshData& m) {
    TB b{m, lod};
    float clarity = lod >= 2 ? 0.3f : 0.88f;
    float hl = tHalfLen(sec);
    bool noseFront = sec == 0, noseRear = sec == 2;
    float yA = noseRear ? -hl + kTNose : -hl, yB = noseFront ? hl - kTNose : hl;
    // ------------------------------------------------------------------ sides
    std::vector<TSpan> spans;
    std::vector<float> doors;
    tDoorsOf(sec, doors);
    for (int sd = -1; sd <= 1; sd += 2) {
        tSideSpans(sec, sd, yA, yB, spans);
        for (const TSpan& sp : spans) {
            if (sp.type == 0) {
                liveryWall(b, sd, sp.y0, sp.y1, kTSkirt, kTShoulder);
                // inner face of the pillar in the window band (the cabin sees it through the glass opposite)
                if (lod < 2) {
                    float xi = (kTW - 0.08f) * sd;
                    b.quad(vec3(xi, sp.y0, kTWin0), vec3(xi, sp.y1, kTWin0), vec3(xi, sp.y1, kTWin1), vec3(xi, sp.y0, kTWin1), tc(0.84f, 0.85f, 0.83f),
                           makeMat(MAT_PLASTIC), vec3((float)-sd, 0, 0));
                }
            } else if (sp.type == 1) {
                liveryWall(b, sd, sp.y0, sp.y1, kTSkirt, kTWin0);
                liveryWall(b, sd, sp.y0, sp.y1, kTWin1, kTShoulder);
                tGlass(b, sd, sp.y0 + 0.02f, sp.y1 - 0.02f, kTWin0 + 0.02f, kTWin1 - 0.02f, clarity);
                if (lod == 0) {
                    float xo = tSideX(kTWin0) * sd, xi = xo - 0.06f * sd;
                    b.quad(vec3(xo, sp.y0, kTWin0), vec3(xi, sp.y0, kTWin0), vec3(xi, sp.y1, kTWin0), vec3(xo, sp.y1, kTWin0), tc(0.1f), makeMat(MAT_PLASTIC), vec3(0, 0, 1));
                }
            } else {
                // door opening: low step, header; leaves drawn separately (baked shut in the far LOD)
                liveryWall(b, sd, sp.y0, sp.y1, kTSkirt, kTFloor - 0.01f);
                liveryWall(b, sd, sp.y0, sp.y1, kTDoorTop, kTShoulder);
                float xo = tSideX(1.2f) * sd, xi = xo - 0.12f * sd;
                if (lod >= 2) {
                    b.quad(vec3(xo, sp.y0, kTFloor), vec3(xo, sp.y1, kTFloor), vec3(xo, sp.y1, kTDoorTop), vec3(xo, sp.y0, kTDoorTop), kTCoral, makeMat(MAT_CARPAINT),
                           vec3((float)sd, 0, 0));
                    b.quad(vec3(xo + 0.01f * sd, sp.y0 + 0.1f, 0.95f), vec3(xo + 0.01f * sd, sp.y1 - 0.1f, 0.95f), vec3(xo + 0.01f * sd, sp.y1 - 0.1f, 2.15f),
                           vec3(xo + 0.01f * sd, sp.y0 + 0.1f, 2.15f), tc(0.8f, 0.86f, 0.88f, 0.3f), makeMat(MAT_CAR_WINDOW), vec3((float)sd, 0, 0));
                } else {
                    u32 fc = tc(0.55f, 0.57f, 0.6f);
                    b.quad(vec3(xo, sp.y0, kTFloor), vec3(xi, sp.y0, kTFloor), vec3(xi, sp.y0, kTDoorTop), vec3(xo, sp.y0, kTDoorTop), fc, makeMat(MAT_METAL_BRUSHED),
                           vec3(0, 1, 0));
                    b.quad(vec3(xi, sp.y1, kTFloor), vec3(xo, sp.y1, kTFloor), vec3(xo, sp.y1, kTDoorTop), vec3(xi, sp.y1, kTDoorTop), fc, makeMat(MAT_METAL_BRUSHED),
                           vec3(0, -1, 0));
                    b.quad(vec3(xo, sp.y0, kTDoorTop), vec3(xo, sp.y1, kTDoorTop), vec3(xi, sp.y1, kTDoorTop), vec3(xi, sp.y0, kTDoorTop), fc, makeMat(MAT_METAL_BRUSHED),
                           vec3(0, 0, -1));
                    // threshold with a yellow edge, door-open lamp over the opening
                    b.quad(vec3(xi, sp.y0, kTFloor + 0.004f), vec3(xi, sp.y1, kTFloor + 0.004f), vec3(xo, sp.y1, kTFloor + 0.004f), vec3(xo, sp.y0, kTFloor + 0.004f),
                           tc(0.95f, 0.78f, 0.12f), makeMat(MAT_METAL_PAINTED), vec3(0, 0, 1));
                    vec3 lamp(tSideX(kTDoorTop + 0.1f) * sd + 0.01f * sd, (sp.y0 + sp.y1) * 0.5f, kTDoorTop + 0.1f);
                    b.quad(lamp + vec3(0, -0.12f, -0.03f), lamp + vec3(0, 0.12f, -0.03f), lamp + vec3(0, 0.12f, 0.03f), lamp + vec3(0, -0.12f, 0.03f), tc(0.2f, 0.9f, 0.3f, 0.35f),
                           makeMat(MAT_EMISSIVE), vec3((float)sd, 0, 0));
                }
            }
        }
        // line lettering on the coral skirt between the doors
        if (lod < 2) {
            float x = tSideX(0.4f) * sd + 0.004f * sd;
            vec3 right = sd > 0 ? vec3(0, -1, 0) : vec3(0, 1, 0);
            right = -right;   // reads front to back on the right side, back to front on the left
            const char* txt = sec == 1 ? "SOL BEACH" : "STREETCAR";
            float h = 0.2f;
            float w = World::sitegeo::textAdvance(txt, h);
            float yc = sec == 1 ? (sd > 0 ? -2.1f : 2.1f) : (sec == 0 ? -0.4f : 0.4f);
            vec3 o(x, yc - right.y * w * 0.5f, 0.3f);
            strokeTextM(m, txt, o, right * (sd > 0 ? 1.f : 1.f), vec3(0, 0, 1), h, h * 0.18f, kTWhite, makeMat(MAT_CARPAINT));
        }
    }
    // ------------------------------------------------------------------ roof (shoulder to crown) and underside
    {
        const int n = lod >= 2 ? 3 : 6;
        std::vector<vec2> prof;
        for (int i = 0; i <= n; i++) {
            float a = (float)i / n * kPi * 0.5f;
            float x = tSideX(kTShoulder) * (cosf(a) * 0.4f + 0.6f * (1.f - (float)i / n));
            float z = kTShoulder + (kTRoof - kTShoulder) * sinf(a);
            prof.push_back(vec2(x, z));
        }
        for (int sd = -1; sd <= 1; sd += 2)
            for (int i = 0; i < n; i++) {
                vec2 p0 = prof[i], p1 = prof[i + 1];
                vec3 facing(sd * (p1.y - p0.y), 0, p0.x - p1.x);
                u32 rc = i == 0 ? kTWhite : tc(0.8f, 0.81f, 0.82f);
                u32 rm = i == 0 ? makeMat(MAT_CARPAINT) : makeMat(MAT_METAL_PAINTED);
                b.quad(vec3(sd * p0.x, yA, p0.y), vec3(sd * p0.x, yB, p0.y), vec3(sd * p1.x, yB, p1.y), vec3(sd * p1.x, yA, p1.y), rc, rm, facing);
            }
        b.quad(vec3(-tSideX(kTSkirt), yA, kTSkirt), vec3(tSideX(kTSkirt), yA, kTSkirt), vec3(tSideX(kTSkirt), yB, kTSkirt), vec3(-tSideX(kTSkirt), yB, kTSkirt), tc(0.1f),
               makeMat(MAT_PLASTIC), vec3(0, 0, -1));
    }
    // ------------------------------------------------------------------ ends
    if (noseFront) buildNose(b, yB, 1.f, true, clarity);
    else buildJoint(b, yB, 1.f);
    if (noseRear) buildNose(b, yA, -1.f, false, clarity);
    else buildJoint(b, yA, -1.f);
    // ------------------------------------------------------------------ roof equipment
    if (sec == 1) {
        // half pantograph: base frame on insulators, lower arm, upper arm, collector head at the wire
        float zr = kTRoof;
        u32 frame = tc(0.2f, 0.21f, 0.22f), ins = tc(0.45f, 0.22f, 0.12f);
        b.box(vec3(0, -0.5f, zr + 0.2f), vec3(0.55f, 0.6f, 0.05f), frame, makeMat(MAT_METAL_PAINTED));
        for (int i = -1; i <= 1; i += 2)
            for (int j = -1; j <= 1; j += 2) b.rod(vec3(i * 0.45f, -0.5f + j * 0.5f, zr), vec3(i * 0.45f, -0.5f + j * 0.5f, zr + 0.17f), 0.05f, 8, ins, MAT_PLASTIC);
        vec3 pivot(0, -0.95f, zr + 0.3f), knee(0, 0.55f, zr + 1.05f), head(0, -0.35f, World::tram_dims::kWireHeight - 0.08f);
        for (int i = -1; i <= 1; i += 2) b.rod(pivot + vec3(i * 0.35f, 0, 0), knee + vec3(i * 0.05f, 0, 0), 0.035f, 6, frame, MAT_METAL_PAINTED);
        b.rod(knee, head, 0.028f, 6, frame, MAT_METAL_PAINTED);
        b.rod(knee + vec3(0, -0.1f, -0.05f), head + vec3(0, 0.12f, -0.2f), 0.015f, 5, frame, MAT_METAL_PAINTED);
        b.box(head + vec3(0, 0, 0.03f), vec3(0.78f, 0.05f, 0.03f), tc(0.25f), makeMat(MAT_METAL_BRUSHED));
        for (int i = -1; i <= 1; i += 2) b.rod(head + vec3(i * 0.78f, 0, 0.03f), head + vec3(i * 0.95f, 0, -0.12f), 0.018f, 5, frame, MAT_METAL_PAINTED);
        // roof resistor box and cable duct
        b.box(vec3(0, 2.0f, zr + 0.16f), vec3(0.6f, 1.1f, 0.16f), tc(0.7f, 0.72f, 0.74f), makeMat(MAT_METAL_PAINTED));
    } else {
        float yc = sec == 0 ? -0.8f : 0.8f;
        b.box(vec3(0, yc, kTRoof + 0.14f), vec3(0.8f, 1.5f, 0.15f), tc(0.72f, 0.74f, 0.76f), makeMat(MAT_METAL_PAINTED));
        if (lod < 2)
            for (int g = 0; g < 3; g++) b.box(vec3(0, yc - 0.9f + g * 0.9f, kTRoof + 0.295f), vec3(0.55f, 0.3f, 0.01f), tc(0.18f), makeMat(MAT_METAL_PAINTED));
    }
    // skirts over the bogies (wheel wells darkened)
    if (lod < 2) {
        float by = sec == 1 ? 0.f : (sec == 0 ? -1.1f : 1.1f);
        for (int sd = -1; sd <= 1; sd += 2) {
            float x = tSideX(0.3f) * sd + 0.005f * sd;
            b.quad(vec3(x, by - 1.0f, kTSkirt + 0.01f), vec3(x, by + 1.0f, kTSkirt + 0.01f), vec3(x, by + 1.0f, 0.26f), vec3(x, by - 1.0f, 0.26f), tc(0.12f),
                   makeMat(MAT_PLASTIC), vec3((float)sd, 0, 0));
        }
    }
    // ------------------------------------------------------------------ interior
    if (lod >= 2) {
        // dark cabin block behind the glass
        b.box(vec3(0, (yA + yB) * 0.5f, 1.7f), vec3(kTW - 0.12f, (yB - yA) * 0.5f - 0.1f, 0.7f), tc(0.06f), makeMat(MAT_INTERIOR));
        return;
    }
    u32 wallIn = tc(0.84f, 0.85f, 0.83f), floorC = tc(0.3f, 0.32f, 0.33f), seatC = tc(0.9f, 0.4f, 0.28f), shell = tc(0.72f, 0.74f, 0.76f);
    float xi = kTW - 0.08f;
    // floor, ceiling, inner walls below and above the windows
    b.quad(vec3(-xi, yA, kTFloor), vec3(xi, yA, kTFloor), vec3(xi, yB, kTFloor), vec3(-xi, yB, kTFloor), floorC, makeMat(MAT_RUBBER), vec3(0, 0, 1));
    b.quad(vec3(-xi, yA, kTCeil), vec3(xi, yA, kTCeil), vec3(xi, yB, kTCeil), vec3(-xi, yB, kTCeil), tc(0.9f), makeMat(MAT_PLASTIC), vec3(0, 0, -1));
    for (int sd = -1; sd <= 1; sd += 2) {
        // lower inner wall, open at the doors on the right
        std::vector<std::pair<float, float>> runs;
        float y = yA;
        if (sd > 0)
            for (float d : doors) {
                runs.push_back({y, d - kTDoorHalf});
                y = d + kTDoorHalf;
            }
        runs.push_back({y, yB});
        for (auto& r : runs) {
            if (r.second - r.first < 0.01f) continue;
            b.quad(vec3(xi * sd, r.first, kTFloor), vec3(xi * sd, r.second, kTFloor), vec3(xi * sd, r.second, kTWin0), vec3(xi * sd, r.first, kTWin0), wallIn,
                   makeMat(MAT_PLASTIC), vec3((float)-sd, 0, 0));
        }
        b.quad(vec3(xi * sd, yA, kTWin1), vec3(xi * sd, yB, kTWin1), vec3(xi * sd, yB, kTCeil), vec3(xi * sd, yA, kTCeil), wallIn, makeMat(MAT_PLASTIC),
               vec3((float)-sd, 0, 0));
        // ceiling light strip (lit at night)
        b.quad(vec3(sd * 0.55f, yA + 0.3f, kTCeil - 0.01f), vec3(sd * 0.55f, yB - 0.3f, kTCeil - 0.01f), vec3(sd * 0.35f, yB - 0.3f, kTCeil - 0.01f),
               vec3(sd * 0.35f, yA + 0.3f, kTCeil - 0.01f), tc(1.f, 0.96f, 0.9f, 0.3f), makeMat(MAT_EMISSIVE), vec3(0, 0, -1));
    }
    // seats from the gameplay slots: shells and coral cushions facing along the car
    std::vector<Vehicles::SeatSpec> seats;
    tramSeats(sec, seats);
    for (size_t i = 0; i < seats.size(); i++) {
        if (sec == 0 && i == 0) continue;   // the driver's seat is in the cab block below
        vec3 p = seats[i].pos;
        float face = (i % 2 == 0) ? 1.f : -1.f;   // facing pairs
        b.box(vec3(p.x, p.y, kTFloor + 0.42f), vec3(0.23f, 0.24f, 0.04f), seatC, makeMat(MAT_FABRIC));
        b.box(vec3(p.x, p.y - face * 0.24f, kTFloor + 0.78f), vec3(0.23f, 0.04f, 0.32f), seatC, makeMat(MAT_FABRIC));
        if (lod == 0) b.box(vec3(p.x, p.y, kTFloor + 0.19f), vec3(0.2f, 0.15f, 0.19f), shell, makeMat(MAT_PLASTIC));
    }
    // grab poles by the doors (yellow) and a longitudinal rail
    for (float d : doors)
        for (int k = -1; k <= 1; k += 2) b.rod(vec3(0.62f, d + k * 0.85f, kTFloor), vec3(0.62f, d + k * 0.85f, kTCeil), 0.02f, 6, tc(0.95f, 0.75f, 0.1f), MAT_METAL_PAINTED);
    if (lod == 0) b.rod(vec3(-0.35f, yA + 0.4f, 2.05f), vec3(-0.35f, yB - 0.4f, 2.05f), 0.016f, 6, tc(0.85f), MAT_CHROME);
    // driver's cab: console, seat and a partition behind it
    if (sec == 0) {
        float yc = hl - 1.05f;
        b.box(vec3(0, yc + 0.5f, kTFloor + 0.55f), vec3(0.75f, 0.22f, 0.36f), tc(0.12f), makeMat(MAT_PLASTIC));
        b.box(vec3(0, yc, kTFloor + 0.42f), vec3(0.25f, 0.24f, 0.05f), tc(0.1f), makeMat(MAT_LEATHER));
        b.box(vec3(0, yc - 0.24f, kTFloor + 0.8f), vec3(0.25f, 0.04f, 0.34f), tc(0.1f), makeMat(MAT_LEATHER));
        b.box(vec3(0.62f, yc - 0.62f, 1.45f), vec3(0.5f, 0.03f, 1.05f), wallIn, makeMat(MAT_PLASTIC));
        b.box(vec3(-0.62f, yc - 0.62f, 1.45f), vec3(0.5f, 0.03f, 1.05f), wallIn, makeMat(MAT_PLASTIC));
        b.quad(vec3(-0.25f, yc + 0.73f, kTFloor + 0.93f), vec3(0.25f, yc + 0.73f, kTFloor + 0.93f), vec3(0.25f, yc + 0.6f, kTFloor + 1.0f), vec3(-0.25f, yc + 0.6f, kTFloor + 1.0f),
               tc(0.2f, 0.6f, 0.9f, 0.4f), makeMat(MAT_EMISSIVE), vec3(0, -0.4f, 1));
    }
}

// Door leaf: one half of a double plug door (0.64 m), coral frame, big window
void buildTramDoorLeaf(MeshData& m) {
    TB b{m, 0};
    float hw = kTDoorHalf * 0.5f - 0.01f, z0 = kTFloor + 0.01f, z1 = kTDoorTop - 0.02f;
    float zc = (z0 + z1) * 0.5f, hz = (z1 - z0) * 0.5f;
    b.box(vec3(0, 0, z0 + 0.3f), vec3(0.03f, hw, 0.3f), kTCoral, makeMat(MAT_CARPAINT));
    b.box(vec3(0, 0, z1 - 0.08f), vec3(0.03f, hw, 0.08f), kTCoral, makeMat(MAT_CARPAINT));
    for (int sd = -1; sd <= 1; sd += 2) b.box(vec3(0, sd * (hw - 0.04f), zc), vec3(0.03f, 0.04f, hz), kTCoral, makeMat(MAT_CARPAINT));
    for (int sd = -1; sd <= 1; sd += 2)
        b.quad(vec3(sd * 0.012f, -hw + 0.08f, z0 + 0.6f), vec3(sd * 0.012f, hw - 0.08f, z0 + 0.6f), vec3(sd * 0.012f, hw - 0.08f, z1 - 0.16f),
               vec3(sd * 0.012f, -hw + 0.08f, z1 - 0.16f), tc(0.86f, 0.93f, 0.95f, 0.9f), makeMat(MAT_CAR_WINDOW), vec3((float)sd, 0, 0));
    b.box(vec3(0, hw - 0.005f, zc), vec3(0.035f, 0.012f, hz), tc(0.04f), makeMat(MAT_RUBBER));
}

void tramSpec(int sec, Vehicles::VehicleModel& o) {
    const char* names[3] = {"Sol Beach Streetcar", "Sol Beach Streetcar (centre)", "Sol Beach Streetcar (rear)"};
    o.name = names[sec];
    o.maker = "Palmera Transit";
    o.cls = Vehicles::VC_BUS;   // large passenger vehicle class; never spawned by traffic (spawnWeight 0)
    o.mass = 14000.f;
    o.power = 360.f;
    o.torque = 5000.f;
    o.maxRpm = 3000.f;
    o.topSpeed = 20.f;
    o.gears = 1;
    o.brakeForce = 40000.f;
    o.dragCoef = 0.6f;
    o.frontalArea = 7.5f;
    o.centerOfMass = vec3(0, 0, 1.1f);
    o.engineSound = Audio::ENGINE_ELECTRIC;
    o.spawnWeight = 0.f;
    o.price = 0;
    o.fixedLivery = true;
    o.liveryPrimary = vec3(0.9f, 0.89f, 0.85f);
    o.liverySecondary = vec3(0.86f, 0.24f, 0.15f);
    o.paletteColors.push_back(o.liveryPrimary);
    tramSeats(sec, o.seats);
    float hl = tHalfLen(sec);
    if (sec == 0)
        for (int sd = -1; sd <= 1; sd += 2) o.lights.push_back(Vehicles::LightSpec{vec3(sd * 0.75f, hl, 0.82f), vec3(0, 1, -0.05f), Vehicles::LT_HEAD});
    if (sec == 2)
        for (int sd = -1; sd <= 1; sd += 2) o.lights.push_back(Vehicles::LightSpec{vec3(sd * 0.85f, -hl, 0.82f), vec3(0, -1, 0), Vehicles::LT_TAIL});
    o.boxCenter = vec3(0, 0, 1.68f);
    o.boxHalf = vec3(kTW, hl, 1.52f);
}

}  // namespace TransitModels
}  // namespace Game
