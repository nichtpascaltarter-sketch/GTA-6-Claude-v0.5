// Maker emblems (original designs, chrome over coloured enamel) and chrome model lettering on the tail.
// Emblems are built in a local frame: x = viewer's right, y = up, z = out of the surface (base plane z = 0).
namespace Vehicles {
namespace detail {

enum MakerId : u8 {
    MK_NONE = 0, MK_SAKAKI, MK_HOSHIDA, MK_AETHER, MK_BRENNAN, MK_NORDWERK, MK_ARDENT, MK_CASTIGLIA, MK_RIDLEY, MK_DUNMORE,
    MK_CIVITAS, MK_COUNT
};

inline u8 makerId(const std::string& s) {
    static const char* const kNames[MK_COUNT] = {"", "Sakaki", "Hoshida", "Aether", "Brennan", "Nordwerk",
                                                 "Ardent", "Castiglia", "Ridley", "Dunmore", "Civitas"};
    for (int i = 1; i < MK_COUNT; i++)
        if (s == kNames[i]) return (u8)i;
    return MK_NONE;
}

// Frame on a surface point: x = viewer's right (horizontal), y = up along the surface, z = surface normal.
inline Frame surfaceFrame(vec3 p, vec3 n, float lift) {
    vec3 r = cross(vec3(0, 0, 1), n);
    if (length2(r) < 1e-6f) r = vec3(1, 0, 0);
    r = normalize(r);
    vec3 u = normalize(cross(n, r));
    return Frame(p + n * lift, r, u, n);
}

// Rounded chrome loop along a closed outline lying on the base plane.
inline void logoLoop(PMesh& m, const Frame& F, const std::vector<vec2>& pts, float t, int seg = 5) {
    std::vector<vec3> path;
    for (const vec2& q : pts) path.push_back(F.at(vec3(q.x, q.y, t)));
    std::vector<float> rad(1, t);
    tube(m, path, rad, seg, false, false, true, F.z);
}
inline std::vector<vec2> starOutline(float ro, float ri, int points) {
    std::vector<vec2> s;
    for (int i = 0; i < points * 2; i++) {
        float a = kHalfPi + kPi * i / points;
        float r = (i & 1) ? ri : ro;
        s.push_back(vec2(cosf(a) * r, sinf(a) * r));
    }
    return s;
}
inline std::vector<vec2> scaled(const std::vector<vec2>& s, float k, vec2 c = vec2(0, 0)) {
    std::vector<vec2> o;
    for (const vec2& q : s) o.push_back(c + (q - c) * k);
    return o;
}

// Emblem of radius ~r centred on F.o.
inline void buildLogo(PMesh& m, const Frame& F, float r, u8 maker) {
    if (maker == MK_NONE) return;
    float t = r * 0.07f;
    Frame Fr = F;
    Fr.o = F.o + F.z * t;  // torus centre lifted so the ring sits on the base plane
    m.newGroup(28.f);
    auto enamel = [&](const std::vector<vec2>& s, u32 c) {
        m.use(MAT_METAL_PAINTED, c);
        extrude(m, s, F, 0.f, t * 0.8f);
    };
    auto glyph = [&](const char* txt, vec2 c, float h, float depth) {
        m.use(MAT_CHROME, kCol1);
        strokeText3D(m, F.at(vec3(c.x, c.y, t * 0.8f)), F.x, F.y, txt, h, depth);
    };
    switch (maker) {
        case MK_SAKAKI: {  // oval ring, single-period wave (the "tide") on dark red
            enamel(shapeEllipse(vec2(0, 0), r * 0.95f, r * 0.58f, 22), col(0.42f, 0.03f, 0.04f));
            m.use(MAT_CHROME, kCol1);
            logoLoop(m, F, shapeEllipse(vec2(0, 0), r, r * 0.63f, 26), t);
            std::vector<vec3> wave;
            for (int k = 0; k <= 14; k++) {
                float x = lerp(-0.7f, 0.7f, k / 14.f) * r;
                wave.push_back(F.at(vec3(x, -0.22f * r * sinf(kPi * x / (0.7f * r)), t * 1.6f)));
            }
            tube1(m, wave, t * 0.95f, 6, true, F.z);
            break;
        }
        case MK_HOSHIDA: {  // ring with a five-point star on navy
            enamel(shapeEllipse(vec2(0, 0), r * 0.94f, r * 0.94f, 22), col(0.03f, 0.06f, 0.2f));
            m.use(MAT_CHROME, kCol1);
            torus(m, Fr, r, t, 24, 5);
            extrude(m, starOutline(r * 0.66f, r * 0.27f, 5), F, t * 0.8f, t * 2.2f);
            break;
        }
        case MK_AETHER: {  // ring, open chevron and an orbit bar crossing the ring
            enamel(shapeEllipse(vec2(0, 0), r * 0.94f, r * 0.94f, 22), col(0.02f, 0.02f, 0.025f));
            m.use(MAT_CHROME, kCol1);
            torus(m, Fr, r, t * 0.75f, 24, 5);
            std::vector<vec2> ch;
            ch.push_back(vec2(0.f, 0.64f * r)); ch.push_back(vec2(0.54f * r, -0.5f * r)); ch.push_back(vec2(0.36f * r, -0.5f * r));
            ch.push_back(vec2(0.f, 0.26f * r)); ch.push_back(vec2(-0.36f * r, -0.5f * r)); ch.push_back(vec2(-0.54f * r, -0.5f * r));
            extrude(m, ch, F, t * 0.8f, t * 2.2f);
            roundedBox(m, Frame(F.at(vec3(0, -0.12f * r, t * 1.3f)), F.x, F.y, F.z), vec3(1.22f * r, 0.045f * r, t * 0.6f), t * 0.4f, 1);
            break;
        }
        case MK_BRENNAN: {  // wide crest, chrome border and B on deep blue
            std::vector<vec2> cr;
            cr.push_back(vec2(-1.0f, 0.46f)); cr.push_back(vec2(-0.72f, 0.62f)); cr.push_back(vec2(0.72f, 0.62f)); cr.push_back(vec2(1.0f, 0.46f));
            cr.push_back(vec2(0.82f, -0.18f)); cr.push_back(vec2(0.f, -0.7f)); cr.push_back(vec2(-0.82f, -0.18f));
            std::vector<vec2> crest = resampleClosed(shapeRounded(scaled(cr, r), r * 0.08f, 2), 30);
            enamel(crest, col(0.04f, 0.1f, 0.32f));
            m.use(MAT_CHROME, kCol1);
            logoLoop(m, F, crest, t * 0.8f);
            glyph("B", vec2(0, 0.0f), r * 0.66f, t * 1.4f);
            break;
        }
        case MK_NORDWERK: {  // double ring roundel with an N on graphite
            enamel(shapeEllipse(vec2(0, 0), r * 0.94f, r * 0.94f, 22), col(0.06f, 0.065f, 0.07f));
            m.use(MAT_CHROME, kCol1);
            torus(m, Fr, r, t, 24, 5);
            Frame Fi = Fr;
            Fi.o = F.o + F.z * (t * 0.5f);
            torus(m, Fi, r * 0.8f, t * 0.4f, 24, 4);
            glyph("N", vec2(0, 0), r * 0.74f, t * 1.4f);
            break;
        }
        case MK_ARDENT: {  // chrome delta with a red inlay
            std::vector<vec2> d;
            d.push_back(vec2(0.f, 0.92f)); d.push_back(vec2(0.92f, -0.62f)); d.push_back(vec2(0.f, -0.26f)); d.push_back(vec2(-0.92f, -0.62f));
            m.use(MAT_CHROME, kCol1);
            extrude(m, scaled(d, r), F, 0.f, t * 1.6f);
            m.use(MAT_METAL_PAINTED, col(0.55f, 0.03f, 0.03f));
            extrude(m, scaled(scaled(d, r), 0.52f, vec2(0, 0.05f * r)), F, t * 1.6f, t * 2.0f);
            break;
        }
        case MK_CASTIGLIA: {  // yellow shield, chrome border, black tower
            std::vector<vec2> sh;
            sh.push_back(vec2(-0.62f, 0.82f)); sh.push_back(vec2(0.62f, 0.82f)); sh.push_back(vec2(0.62f, 0.05f)); sh.push_back(vec2(0.f, -0.88f));
            sh.push_back(vec2(-0.62f, 0.05f));
            std::vector<vec2> shield = resampleClosed(shapeRounded(scaled(sh, r), r * 0.1f, 2), 28);
            enamel(shield, col(0.85f, 0.62f, 0.03f));
            m.use(MAT_CHROME, kCol1);
            logoLoop(m, F, shield, t * 0.8f);
            const float tw[][2] = {{-0.24f, -0.46f}, {0.24f, -0.46f}, {0.24f, 0.42f}, {0.12f, 0.42f}, {0.12f, 0.3f}, {0.05f, 0.3f},
                                   {0.05f, 0.42f}, {-0.05f, 0.42f}, {-0.05f, 0.3f}, {-0.12f, 0.3f}, {-0.12f, 0.42f}, {-0.24f, 0.42f}};
            std::vector<vec2> tower;
            for (const auto& q : tw) tower.push_back(vec2(q[0] * r, q[1] * r + 0.02f * r));
            m.use(MAT_METAL_PAINTED, col(0.02f, 0.02f, 0.02f));
            extrude(m, tower, F, t * 0.8f, t * 1.4f);
            break;
        }
        case MK_RIDLEY: {  // wide diamond with an R on black
            std::vector<vec2> dm;
            dm.push_back(vec2(0.f, 0.64f * r)); dm.push_back(vec2(1.05f * r, 0.f)); dm.push_back(vec2(0.f, -0.64f * r)); dm.push_back(vec2(-1.05f * r, 0.f));
            std::vector<vec2> dia = resampleClosed(shapeRounded(dm, r * 0.06f, 2), 28);
            enamel(dia, col(0.015f, 0.015f, 0.015f));
            m.use(MAT_CHROME, kCol1);
            logoLoop(m, F, dia, t * 0.8f);
            glyph("R", vec2(0, 0), r * 0.62f, t * 1.4f);
            break;
        }
        case MK_DUNMORE: {  // nameplate
            std::vector<vec2> pl = resampleClosed(shapeRoundRect(vec2(0, 0), r * 1.45f, r * 0.42f, r * 0.2f, 3), 36);
            enamel(pl, col(0.03f, 0.05f, 0.14f));
            m.use(MAT_CHROME, kCol1);
            logoLoop(m, F, pl, t * 0.7f);
            glyph("DUNMORE", vec2(0, 0), r * 0.36f, t * 1.2f);
            break;
        }
        case MK_CIVITAS: {  // ring with a C on teal
            enamel(shapeEllipse(vec2(0, 0), r * 0.94f, r * 0.94f, 22), col(0.0f, 0.3f, 0.34f));
            m.use(MAT_CHROME, kCol1);
            torus(m, Fr, r, t, 24, 5);
            glyph("C", vec2(0, 0), r * 0.72f, t * 1.4f);
            break;
        }
        default: break;
    }
}

// Emblem on the nose for grille-less fronts (EVs, supercars): between the headlamps, where the nose faces forward.
inline void noseLogo(PMesh& m, CarBody& b, const CarLook& L) {
    if (L.maker == MK_NONE) return;
    const float dz[] = {0.f, -0.05f, 0.05f, -0.1f, 0.1f};
    for (float z0 : dz) {
        Decal dc;
        float z = L.headC.y + z0;
        if (!decalAt(dc, b.proj, projFront(), vec3(0, b.yF + 0.5f, z), vec3(0, -1, 0))) continue;
        vec3 p, n;
        if (!dc.at(vec2(0, 0), p, n) || n.y < 0.45f) continue;
        buildLogo(m, surfaceFrame(p, n, 0.002f), L.logoR, L.maker);
        return;
    }
}

// Tail emblem above the plate and chrome model lettering to its right (pickups: big lettering across the tailgate).
inline void rearBadges(PMesh& m, CarBody& b, const CarLook& L, const std::string& name) {
    if (L.maker == MK_NONE) return;
    const CarSpec& s = b.s;
    auto surf = [&](float x, float z, vec3& p, vec3& n) -> bool {
        Decal dc;
        if (!decalAt(dc, b.proj, projRear(), vec3(x, b.yR - 0.5f, z), vec3(0, 1, 0))) return false;
        if (!dc.at(vec2(0, 0), p, n)) return false;
        return n.y < -0.6f;
    };
    std::string up;
    for (char c : name) up += (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    float plateTop = L.plateRZ + 0.085f;
    bool bar = L.tail == TL_BAR;
    auto lampHit = [&](float x0, float x1, float z0, float z1) {
        float lz0 = L.tailC.y - L.tailH - 0.02f, lz1 = L.tailC.y + L.tailH + 0.02f;
        if (z1 < lz0 || z0 > lz1) return false;
        if (bar) return true;
        float lx0 = L.tailC.x - L.tailW - 0.04f;
        return x1 > lx0 || -x0 > lx0;
    };
    vec3 p, n;
    if (s.style == BS_PICKUP) {
        float h = 0.055f;
        float z = (Max(plateTop, s.zTailBot) + s.zTailTop) * 0.5f;
        float w = up.size() * h * 0.72f;
        vec3 pa, na, pb, nb;
        if (!surf(0.f, z, p, n) || !surf(-w * 0.5f, z, pa, na) || !surf(w * 0.5f, z, pb, nb)) return;
        Frame F = surfaceFrame(p, n, 0.0015f);
        m.newGroup(28.f);
        m.use(MAT_CHROME, kCol1);
        strokeText3D(m, F.o, F.x, F.y, up.c_str(), h, 0.004f);
        return;
    }
    // emblem centred above the plate (or above a full-width lamp bar)
    float r = 0.03f;
    float zl = plateTop + r + 0.022f;
    if (lampHit(-r, r, zl - r, zl + r)) zl = L.tailC.y + L.tailH + r + 0.03f;
    if (!lampHit(-r, r, zl - r, zl + r) && surf(0.f, zl, p, n)) {
        vec3 pa, na, pb, nb, pc, nc, pd, nd;
        if (surf(-r, zl, pa, na) && surf(r, zl, pb, nb) && surf(0.f, zl + r + 0.01f, pc, nc) && surf(0.f, zl - r, pd, nd))
            buildLogo(m, surfaceFrame(p, n, 0.0015f), r, L.maker);
    }
    // model lettering to the right of the plate, at the height of the plate's upper half
    float h = 0.024f;
    float w = up.size() * h * 0.72f;
    float x0 = 0.215f, zt = plateTop - 0.02f;
    if (lampHit(x0, x0 + w, zt - h * 0.5f, zt + h * 0.5f)) return;
    if (x0 + w > s.halfW * 0.8f) return;
    vec3 pa, na, pb, nb;
    if (!surf(x0 + w * 0.5f, zt, p, n) || !surf(x0, zt, pa, na) || !surf(x0 + w, zt, pb, nb)) return;
    Frame F = surfaceFrame(p, n, 0.0015f);
    m.newGroup(28.f);
    m.use(MAT_CHROME, kCol1);
    strokeText3D(m, F.o, F.x, F.y, up.c_str(), h, 0.003f);
}

}  // namespace detail
}  // namespace Vehicles
