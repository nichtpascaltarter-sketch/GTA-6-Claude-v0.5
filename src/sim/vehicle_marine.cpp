// Watercraft: speedboat, fishing boat, jet ski, airboat. Local frame: +Y bow, z = 0 is the static waterline
// (the keel sits at negative z; the draft is |keel z|). floatPoints sample the hull bottom for buoyancy.
namespace Vehicles {
namespace detail {

struct HullSpec {
    float yT = -3.3f, yB = 4.0f;          // transom / bow tip
    Curve keel, chineX, sheerX, sheerZ;   // functions of y
    float deadrise = 0.37f;               // tan of the bottom V angle
    float deadriseBow = 0.9f;             // tan near the bow
    float gunwaleW = 0.14f;
    float deckCrown = 0.03f;
    std::vector<vec3> recesses;           // (yFront, yRear, floorZ)
    u8 hullMat = MAT_CARPAINT;
    u32 hullCol = kCol1;
    u8 bottomMat = MAT_METAL_PAINTED;
    u32 bottomCol = 0xffe8e8e8u;
    u8 deckMat = MAT_METAL_PAINTED;
    u32 deckCol = 0xfff0f0f0u;
    u8 wallMat = MAT_METAL_PAINTED;
    u32 wallCol = 0xffe6e6e6u;
    u8 floorMat = MAT_METAL_PAINTED;
    u32 floorCol = 0xffb8b8b8u;
    u8 railMat = MAT_PLASTIC;
    u32 railCol = 0xff606060u;
    bool hardChine = true;
};

struct Hull {
    HullSpec h;
    enum { NP = 18 };
    std::vector<float> rows;
    std::vector<vec3> G;
    std::vector<int> rec;  // recess index per row (-1 none)

    float recessFloor(float y, int& idx) const {
        for (size_t i = 0; i < h.recesses.size(); i++) {
            const vec3& r = h.recesses[i];
            if (y < r.x && y > r.y) { idx = (int)i; return r.z; }
        }
        idx = -1;
        return 0.f;
    }
    void section(float y, vec3* P) const {
        float zk = h.keel(y);
        float xs = Max(h.sheerX(y), 0.f), zs = h.sheerZ(y);
        float xc = Clamp(h.chineX(y), 0.f, xs);
        float t = Saturate((y - (h.yB - 2.2f)) / 2.2f);
        float dr = lerp(h.deadrise, h.deadriseBow, t * t);
        float zc = Min(zk + xc * dr, zs - 0.08f);
        P[0] = vec3(0, y, zk);
        for (int i = 1; i <= 3; i++) {
            float u = i / 3.f;
            P[i] = vec3(xc * u, y, lerp(zk, zc, u) - 0.012f * sinf(u * kPi) * (xc > 0.1f ? 1.f : 0.f));
        }
        float cf = h.hardChine ? Min(0.045f, xc * 0.06f) : 0.f;
        P[4] = vec3(Min(xc + cf, xs), y, zc + 0.01f);
        vec3 Q(xs, y, zs - 0.06f);
        for (int i = 1; i <= 3; i++) {
            float u = i / 3.f;
            vec3 p = lerp(P[4], Q, u);
            p.x += (xs - P[4].x) * 0.12f * sinf(u * kPi);
            P[4 + i] = p;
        }
        float rr = Min(0.025f, xs * 0.1f);
        P[8] = vec3(xs + rr, y, zs - 0.05f);
        P[9] = vec3(xs + rr, y, zs - 0.015f);
        P[10] = vec3(Max(xs - 0.01f, 0.f), y, zs + 0.02f);
        float xin = Max(xs - h.gunwaleW, 0.f);
        P[11] = vec3(xin, y, zs + 0.02f);
        int ri;
        float fz = recessFloor(y, ri);
        auto outerAt = [&](float z) {  // hull outer half width at height z (keel..sheer polyline)
            float best = 0.f;
            for (int j = 0; j < 9; j++) {
                float z0 = P[j].z, z1 = P[j + 1].z;
                if ((z >= z0 && z <= z1) || (z >= z1 && z <= z0)) {
                    float tt = fabsf(z1 - z0) > 1e-5f ? (z - z0) / (z1 - z0) : 0.f;
                    best = Max(best, lerp(P[j].x, P[j + 1].x, Saturate(tt)));
                }
            }
            return best;
        };
        if (ri >= 0 && xin > 0.05f) {
            fz = Max(fz, zk + 0.12f);
            P[12] = vec3(Max(Min(xin - 0.01f, outerAt(zs - 0.04f) - 0.03f), 0.f), y, zs - 0.04f);
            P[13] = vec3(Max(Min(xin - 0.03f, outerAt(fz + 0.06f) - 0.035f), 0.f), y, fz + 0.06f);
            P[14] = vec3(Max(Min(xin - 0.06f, outerAt(fz) - 0.05f), 0.f), y, fz);
            P[13].x = Min(P[13].x, P[12].x);
            P[14].x = Min(P[14].x, P[13].x);
            for (int i = 1; i <= 3; i++) P[14 + i] = vec3(P[14].x * (1.f - i / 3.f), y, fz);
        } else {
            for (int i = 1; i <= 6; i++) {
                float u = i / 6.f;
                float x = xin * (1.f - u);
                float c = xin > 1e-3f ? 1.f - Sq(x / xin) : 0.f;
                P[11 + i] = vec3(x, y, zs + 0.02f + h.deckCrown * c);
            }
        }
        P[0].x = 0.f;
        P[NP - 1].x = 0.f;
    }
    void buildRows() {
        std::vector<float> r;
        int n = (int)((h.yB - h.yT) / (lodLevel() == 0 ? 0.12f : (lodLevel() == 1 ? 0.3f : 0.8f)));
        for (int i = 0; i <= n; i++) {
            float u = (float)i / n;
            // denser towards the bow
            r.push_back(h.yT + (h.yB - h.yT) * (1.f - Sq(1.f - u) * 0.35f - (1.f - u) * 0.65f));
        }
        for (float k = 0.f; k <= 1.001f; k += (lodLevel() == 0 ? 0.125f : 0.5f)) r.push_back(h.yB - 0.5f * (1.f - k) * (1.f - k));
        for (const vec3& rc : h.recesses) {
            r.push_back(rc.x + 0.004f); r.push_back(rc.x - 0.004f);
            r.push_back(rc.y + 0.004f); r.push_back(rc.y - 0.004f);
        }
        r.push_back(h.yT);
        r.push_back(h.yB);
        std::sort(r.begin(), r.end());
        rows.clear();
        for (float y : r) {
            y = Clamp(y, h.yT, h.yB);
            if (rows.empty() || y - rows.back() > 0.002f) rows.push_back(y);
        }
    }
    // emit right half + transom cap, mirrored by the caller
    void emit(PMesh& m) {
        buildRows();
        int nr = (int)rows.size();
        G.resize(nr * NP);
        rec.assign(nr, -1);
        for (int i = 0; i < nr; i++) {
            section(rows[i], &G[i * NP]);
            recessFloor(rows[i], rec[i]);
        }
        m.newGroup(34.f);
        std::vector<u32> id(nr * NP);
        for (int i = 0; i < nr * NP; i++) id[i] = m.add(G[i]);
        for (int i = 0; i + 1 < nr; i++)
            for (int j = 0; j + 1 < NP; j++) {
                bool recessCell = rec[i] >= 0 && rec[i + 1] >= 0;
                bool stepCell = (rec[i] >= 0) != (rec[i + 1] >= 0);
                if (j < 4) m.use(h.bottomMat, h.bottomCol);
                else if (j < 7) m.use(h.hullMat, h.hullCol);
                else if (j < 9) m.use(h.railMat, h.railCol);
                else if (j < 11) m.use(h.deckMat, h.deckCol);
                else if (recessCell || stepCell) m.use(j < 13 ? h.wallMat : h.floorMat, j < 13 ? h.wallCol : h.floorCol);
                else m.use(h.deckMat, h.deckCol);
                if (stepCell && j >= 11) m.use(h.wallMat, h.wallCol);
                m.quad(id[i * NP + j], id[(i + 1) * NP + j], id[(i + 1) * NP + j + 1], id[i * NP + j + 1]);
            }
        // transom (and blunt bow): fill the end section outlines (right half; mirrored with the rest)
        for (int e = 0; e < 2; e++) {
            int row = e == 0 ? 0 : nr - 1;
            const vec3* S = &G[row * NP];
            float maxX = 0.f;
            for (int j = 0; j < NP; j++) maxX = Max(maxX, S[j].x);
            if (maxX < 0.01f) continue;
            m.newGroup(30.f);
            m.use(e == 0 ? h.hullMat : h.deckMat, e == 0 ? h.hullCol : h.deckCol);
            std::vector<vec2> poly;
            for (int j = 0; j < NP; j++) poly.push_back(vec2(S[j].x, S[j].z));
            std::vector<u32> tris;
            triangulatePolygon(poly, tris);
            std::vector<u32> tid(NP);
            for (int j = 0; j < NP; j++) tid[j] = m.add(S[j]);
            float want = e == 0 ? -1.f : 1.f;
            for (size_t k = 0; k + 2 < tris.size(); k += 3) {
                u32 a = tid[tris[k]], b = tid[tris[k + 1]], c = tid[tris[k + 2]];
                vec3 fn = cross(m.P[b] - m.P[a], m.P[c] - m.P[a]);
                if (fn.y * want >= 0.f) m.tri(a, b, c);
                else m.tri(a, c, b);
            }
        }
    }
    float bottomZ(float x, float y) const {
        float zk = h.keel(y), xc = h.chineX(y);
        float t = Saturate((y - (h.yB - 2.2f)) / 2.2f);
        float dr = lerp(h.deadrise, h.deadriseBow, t * t);
        return zk + Min(fabsf(x), xc) * dr;
    }
    float deckZ(float y) const { return h.sheerZ(y) + 0.02f; }
    // outer hull half-width at height z for station y (from the section polyline, bottom..sheer)
    float sideXAt(float y, float z) const {
        vec3 P[NP];
        section(y, P);
        float best = 0.f;
        for (int j = 0; j < 9; j++) {
            float z0 = P[j].z, z1 = P[j + 1].z;
            if ((z >= z0 && z <= z1) || (z >= z1 && z <= z0)) {
                float t = fabsf(z1 - z0) > 1e-5f ? (z - z0) / (z1 - z0) : 0.f;
                best = Max(best, lerp(P[j].x, P[j + 1].x, Saturate(t)));
            }
        }
        return best;
    }
    float innerX(float y) const { return Max(h.sheerX(y) - h.gunwaleW, 0.f); }
};

// Curved glass ribbon (e.g. windshields): base polyline, leaning `up` vectors, height h; double sided + top frame
inline void glassRibbon(PMesh& m, const std::vector<vec3>& base, vec3 up, float h, bool frame) {
    int n = (int)base.size();
    m.newGroup(30.f);
    m.use(MAT_CAR_WINDOW, col(0.86f, 0.94f, 0.95f, 0.9f));
    std::vector<u32> a(n), b(n), a2(n), b2(n);
    for (int i = 0; i < n; i++) {
        a[i] = m.add(base[i]);
        b[i] = m.add(base[i] + up * h);
        a2[i] = m.add(base[i]);
        b2[i] = m.add(base[i] + up * h);
    }
    for (int i = 0; i + 1 < n; i++) {
        vec3 t = base[i + 1] - base[i];
        vec3 nrm = normalize(cross(t, up));
        m.quadFacing(a[i], a[i + 1], b[i + 1], b[i], nrm);
        m.quadFacing(a2[i], a2[i + 1], b2[i + 1], b2[i], -nrm);
    }
    if (frame) {
        m.use(MAT_CHROME, kCol1);
        std::vector<vec3> top;
        for (int i = 0; i < n; i++) top.push_back(base[i] + up * h);
        tube1(m, top, 0.012f, 6, true);
    }
}
// Outboard motor at (0, y, z) (transom top), facing -y; returns the propeller hub position
inline vec3 outboard(PMesh& m, float x, float y, float zTop, float scale, vec3 cowlTint) {
    m.newGroup(40.f);
    float s = scale;
    // bracket
    m.use(MAT_METAL_PAINTED, col(0.15f, 0.15f, 0.15f));
    roundedBoxAt(m, vec3(x, y - 0.08f * s, zTop - 0.05f), vec3(0.12f * s, 0.08f * s, 0.12f * s), 0.02f, 1);
    // cowling
    m.use(MAT_METAL_PAINTED, colv(cowlTint));
    std::vector<LoftSec> c;
    c.push_back({0.f, 0, 0.0f, 0.16f * s, 0.20f * s, 2.4f});
    c.push_back({-0.25f * s, 0, 0.03f * s, 0.23f * s, 0.30f * s, 2.8f});
    c.push_back({-0.55f * s, 0, 0.02f * s, 0.22f * s, 0.30f * s, 2.8f});
    c.push_back({-0.72f * s, 0, 0.0f, 0.14f * s, 0.22f * s, 2.4f});
    loftFrame(m, Frame(vec3(x, y - 0.1f * s, zTop + 0.35f * s), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), c, 16, true, true);
    // midsection + lower unit
    m.use(MAT_METAL_PAINTED, col(0.12f, 0.12f, 0.13f));
    roundedBox(m, Frame(vec3(x, y - 0.35f * s, zTop - 0.30f * s), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), vec3(0.08f * s, 0.14f * s, 0.42f * s), 0.05f * s, 1);
    float zProp = zTop - 0.85f * s;
    std::vector<LoftSec> g;
    g.push_back({0.12f * s, 0, 0.f, 0.02f * s, 0.02f * s, 2.f});
    g.push_back({0.05f * s, 0, 0.f, 0.06f * s, 0.07f * s, 2.f});
    g.push_back({-0.22f * s, 0, 0.f, 0.06f * s, 0.07f * s, 2.f});
    g.push_back({-0.30f * s, 0, 0.f, 0.04f * s, 0.04f * s, 2.f});
    loftFrame(m, Frame(vec3(x, y - 0.35f * s, zProp), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), g, 12, true, true);
    // skeg + cavitation plate
    roundedBoxAt(m, vec3(x, y - 0.40f * s, zProp - 0.14f * s), vec3(0.01f * s, 0.12f * s, 0.07f * s), 0.005f, 1);
    roundedBoxAt(m, vec3(x, y - 0.40f * s, zProp + 0.12f * s), vec3(0.13f * s, 0.17f * s, 0.008f * s), 0.005f, 1);
    return vec3(x, y - 0.35f * s - 0.34f * s, zProp);
}
// Boat propeller as a rotor (spin axis +Y), 3 blades
inline void propRotor(MeshData& out, float r, int blades, float hubR) {
    PMesh m;
    m.newGroup(45.f);
    m.use(MAT_METAL_BRUSHED, col(0.7f, 0.7f, 0.72f));
    std::vector<vec2> hub;
    hub.push_back(vec2(0.05f, 0.f)); hub.push_back(vec2(0.04f, hubR * 0.8f)); hub.push_back(vec2(-0.03f, hubR)); hub.push_back(vec2(-0.06f, hubR * 0.7f)); hub.push_back(vec2(-0.07f, 0.f));
    for (auto& q : hub) q.x = -q.x;
    lathe(m, vec3(0, 0, 0), vec3(0, -1, 0), vec3(1, 0, 0), hub, 12);
    for (int b = 0; b < blades; b++) {
        float a = kTwoPi * b / blades;
        vec3 rd(cosf(a), 0, sinf(a)), tg(-sinf(a), 0, cosf(a));
        std::vector<vec2> blade;
        blade.push_back(vec2(-r * 0.25f, hubR * 0.9f));
        blade.push_back(vec2(r * 0.18f, hubR * 0.9f));
        blade.push_back(vec2(r * 0.32f, r * 0.7f));
        blade.push_back(vec2(r * 0.15f, r));
        blade.push_back(vec2(-r * 0.2f, r * 0.85f));
        // plane of the blade: u along tangent (twisted by pitching the plane), v radial
        Frame bf(vec3(0, 0, 0), normalize(tg + vec3(0, 0.6f, 0)), rd, vec3(0, 0, 0));
        bf.z = normalize(cross(bf.x, bf.y));
        extrude(m, blade, bf, -0.004f, 0.004f);
    }
    finalizeMesh(m, out);
}

inline void boatFloats(VehicleModel& o, const Hull& H, int ny) {
    for (int i = 0; i < ny; i++) {
        float y = lerp(H.h.yT + 0.25f, H.h.yB - 0.9f, (float)i / (ny - 1));
        float xc = H.h.chineX(y) * 0.85f;
        o.floatPoints.push_back(vec3(0, y, H.bottomZ(0, y)));
        o.floatPoints.push_back(vec3(xc, y, H.bottomZ(xc, y)));
        o.floatPoints.push_back(vec3(-xc, y, H.bottomZ(xc, y)));
    }
}
inline void navLights(PMesh& m, vec3 portPos, vec3 stbdPos, vec3 sternPos) {
    m.newGroup(40.f);
    m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));
    roundedBoxAt(m, portPos, vec3(0.02f, 0.05f, 0.02f), 0.01f, 1);
    m.use(MAT_EMISSIVE, col(0.1f, 1.f, 0.3f, 0.25f));
    roundedBoxAt(m, stbdPos, vec3(0.02f, 0.05f, 0.02f), 0.01f, 1);
    m.use(MAT_METAL_BRUSHED, col(0.7f, 0.7f, 0.72f));
    cyl(m, sternPos, sternPos + vec3(0, 0, 0.8f), 0.012f, 6);
    m.use(MAT_EMISSIVE, col(1.f, 1.f, 0.95f, 0.3f));
    ellipsoid(m, Frame(sternPos + vec3(0, 0, 0.82f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), vec3(0.025f), 8, 5);
}
inline void boatSeat(PMesh& m, vec3 hip, float hw, vec3 tint, bool back = true) {
    m.newGroup(45.f);
    m.use(MAT_FABRIC, colv(tint));
    roundedBoxAt(m, hip + vec3(0, 0.12f, -0.08f), vec3(hw, 0.22f, 0.07f), 0.05f, 1);
    if (back) roundedBox(m, Frame(hip + vec3(0, -0.12f, 0.2f), vec3(1, 0, 0), normalize(vec3(0, 0.95f, 0.3f)), normalize(vec3(0, -0.3f, 0.95f))), vec3(hw, 0.06f, 0.24f), 0.05f, 1);
}

// ------------------------------------------------------------------------------------------------
inline void mdlSunchaser(VehicleModel& o) {
    o.name = "Sunchaser 24"; o.maker = "Coralline Marine"; o.cls = VC_BOAT;
    Hull H;
    HullSpec& h = H.h;
    h.yT = -3.3f; h.yB = 4.0f;
    h.keel.add(-3.3f, -0.44f).add(0.8f, -0.44f).add(2.0f, -0.36f).add(3.0f, -0.12f).add(3.6f, 0.30f).add(4.0f, 0.80f).build();
    h.chineX.add(-3.3f, 1.04f).add(-0.5f, 1.12f).add(1.2f, 1.08f).add(2.4f, 0.80f).add(3.4f, 0.30f).add(3.9f, 0.0f).build();
    h.sheerX.add(-3.3f, 1.20f).add(-0.8f, 1.275f).add(1.0f, 1.26f).add(2.5f, 1.08f).add(3.4f, 0.68f).add(3.85f, 0.22f).add(4.0f, 0.0f).build();
    h.sheerZ.add(-3.3f, 0.78f).add(0.0f, 0.82f).add(2.5f, 0.92f).add(4.0f, 1.02f).build();
    h.recesses.push_back(vec3(1.55f, -2.70f, 0.22f));
    h.recesses.push_back(vec3(3.25f, 1.90f, 0.40f));
    h.bottomCol = col(0.9f, 0.9f, 0.9f);
    PMesh m;
    PMesh::Mark mk = m.mark();
    H.emit(m);
    // --- right-side items (mirrored): bow rail, cleats, bow cushions, windshield half
    m.newGroup(40.f);
    m.use(MAT_CHROME, kCol1);
    {
        std::vector<vec3> rail;
        for (int k = 0; k <= 8; k++) {
            float y = lerp(2.0f, 3.9f, k / 8.f);
            rail.push_back(vec3(Max(h.sheerX(y) - 0.06f, 0.f), y, H.deckZ(y) + 0.28f));
        }
        tube1(m, rail, 0.013f, 6, true);
        for (int k = 0; k < 4; k++) {
            float y = lerp(2.1f, 3.6f, k / 3.f);
            float x = Max(h.sheerX(y) - 0.06f, 0.f);
            cyl(m, vec3(x, y, H.deckZ(y)), vec3(x, y, H.deckZ(y) + 0.28f), 0.011f, 6);
        }
        roundedBoxAt(m, vec3(h.sheerX(-2.9f) - 0.06f, -2.9f, H.deckZ(-2.9f) + 0.02f), vec3(0.012f, 0.07f, 0.015f), 0.008f, 1);
    }
    for (int k = 0; k < 3; k++) {
        float y = lerp(2.05f, 2.85f, k / 2.f);
        float hw = 0.13f;
        float x = Min(H.innerX(y), Min(H.sideXAt(y, 0.48f), H.sideXAt(y + 0.34f, 0.48f))) - 0.07f - hw;
        if (x > 0.12f) boatSeat(m, vec3(x, y, 0.60f), hw, vec3(0.92f, 0.92f, 0.9f), false);
    }
    m.mirrorX(mk);
    // --- windshield (wraparound, split in the middle)
    for (int s = -1; s <= 1; s += 2) {
        std::vector<vec3> base;
        for (int k = 0; k <= 8; k++) {
            float a = lerp(0.12f, 1.35f, k / 8.f);
            float x = s * (0.18f + sinf(a) * (H.innerX(1.5f) - 0.12f));
            base.push_back(vec3(x, 1.52f - (1.f - cosf(a)) * 0.45f, H.deckZ(1.5f) + 0.03f));
        }
        glassRibbon(m, base, normalize(vec3(0, -0.45f, 1)), 0.42f, true);
    }
    // --- helm console (starboard), dash, wheel, seats, rear bench, sun pad
    m.newGroup(35.f);
    m.use(MAT_METAL_PAINTED, col(0.95f, 0.95f, 0.95f));
    roundedBoxAt(m, vec3(0.62f, 1.18f, 0.55f), vec3(0.34f, 0.3f, 0.33f), 0.08f, 2);
    roundedBoxAt(m, vec3(-0.62f, 1.18f, 0.55f), vec3(0.34f, 0.3f, 0.33f), 0.08f, 2);
    m.use(MAT_PLASTIC, col(0.25f, 0.25f, 0.25f));
    roundedBox(m, Frame(vec3(0.62f, 0.95f, 0.86f), vec3(1, 0, 0), normalize(vec3(0, 0.5f, 1)), normalize(vec3(0, -1, 0.5f))), vec3(0.3f, 0.12f, 0.02f), 0.02f, 1);
    m.use(MAT_EMISSIVE, col(0.4f, 0.7f, 1.f, 0.06f));
    roundedBox(m, Frame(vec3(0.72f, 0.94f, 0.87f), vec3(1, 0, 0), normalize(vec3(0, 0.5f, 1)), normalize(vec3(0, -1, 0.5f))), vec3(0.1f, 0.07f, 0.024f), 0.01f, 1);
    steeringWheel(m, vec3(0.52f, 0.88f, 0.95f), 0.9f, 0.17f);
    m.use(MAT_CHROME, kCol1);
    cyl(m, vec3(0.9f, 1.0f, 0.88f), vec3(0.9f, 0.9f, 1.05f), 0.01f, 6);
    boatSeat(m, vec3(0.55f, 0.45f, 0.62f), 0.24f, vec3(0.92f, 0.92f, 0.9f));
    boatSeat(m, vec3(-0.55f, 0.45f, 0.62f), 0.24f, vec3(0.92f, 0.92f, 0.9f));
    boatSeat(m, vec3(0.f, -2.35f, 0.62f), 0.95f, vec3(0.92f, 0.92f, 0.9f));
    m.use(MAT_FABRIC, col(0.9f, 0.9f, 0.88f));
    roundedBoxAt(m, vec3(0, -3.0f, H.deckZ(-3.0f) + 0.06f), vec3(1.0f, 0.27f, 0.05f), 0.05f, 1);
    // swim platform and ladder
    m.use(MAT_METAL_PAINTED, col(0.9f, 0.9f, 0.9f));
    roundedBoxAt(m, vec3(0.7f, -3.45f, 0.22f), vec3(0.35f, 0.16f, 0.03f), 0.02f, 1);
    m.use(MAT_CHROME, kCol1);
    for (int s = -1; s <= 1; s += 2) cyl(m, vec3(0.7f + s * 0.18f, -3.5f, 0.22f), vec3(0.7f + s * 0.18f, -3.62f, -0.35f), 0.012f, 6);
    navLights(m, vec3(-h.sheerX(3.2f) + 0.02f, 3.2f, H.deckZ(3.2f) - 0.05f), vec3(h.sheerX(3.2f) - 0.02f, 3.2f, H.deckZ(3.2f) - 0.05f),
              vec3(-0.9f, -3.05f, H.deckZ(-3.05f)));
    // outboard + prop rotor
    vec3 hub = outboard(m, 0.f, -3.32f, 0.72f, 1.0f, vec3(0.08f, 0.08f, 0.09f));
    finalizeMesh(m, o.body);
    propRotor(o.rotor, 0.19f, 3, 0.05f);
    o.rotorPos = hub;
    boatFloats(o, H, 5);
    o.seats.push_back(SeatSpec{vec3(0.55f, 0.45f, 0.62f), true, false});
    o.seats.push_back(SeatSpec{vec3(-0.55f, 0.45f, 0.62f), false, true});
    o.seats.push_back(SeatSpec{vec3(0.45f, -2.35f, 0.62f), false, false});
    o.seats.push_back(SeatSpec{vec3(-0.45f, -2.35f, 0.62f), false, true});
    addLight(o, vec3(0, 3.8f, 0.95f), vec3(0, 1, -0.05f), LT_HEAD);
    addLight(o, vec3(-0.9f, -3.05f, 1.6f), vec3(0, -1, 0), LT_TAIL);
    AABB bb = o.body.bounds;
    o.boxCenter = vec3(0, (bb.mn.y + bb.mx.y) * 0.5f, 0.35f);
    o.boxHalf = vec3(1.28f, (bb.mx.y - bb.mn.y) * 0.5f, 0.75f);
    physics(o, 1900.f, 224.f, 450.f, 6000.f, 24.f, 1, 0.f, 1.f, 0.f, 1.f, 0.35f, 0.f, vec3(0, -0.6f, 0.15f), Audio::ENGINE_V8);
    o.frontalArea = 2.2f;
    o.paletteColors = palette("boat");
    o.spawnWeight = 1.f; o.price = 68000;
}

inline void mdlBonefish(VehicleModel& o) {
    o.name = "Bonefish 28"; o.maker = "Coralline Marine"; o.cls = VC_BOAT;
    Hull H;
    HullSpec& h = H.h;
    h.yT = -3.9f; h.yB = 4.7f;
    h.keel.add(-3.9f, -0.55f).add(1.0f, -0.56f).add(2.6f, -0.45f).add(3.7f, -0.12f).add(4.3f, 0.35f).add(4.7f, 1.05f).build();
    h.chineX.add(-3.9f, 1.18f).add(0.0f, 1.28f).add(1.8f, 1.2f).add(3.0f, 0.85f).add(4.1f, 0.3f).add(4.6f, 0.f).build();
    h.sheerX.add(-3.9f, 1.36f).add(-0.8f, 1.45f).add(1.5f, 1.42f).add(3.2f, 1.1f).add(4.2f, 0.62f).add(4.6f, 0.2f).add(4.7f, 0.f).build();
    h.sheerZ.add(-3.9f, 1.02f).add(0.0f, 1.08f).add(3.0f, 1.22f).add(4.7f, 1.34f).build();
    h.deadrise = 0.42f;
    h.gunwaleW = 0.17f;
    h.recesses.push_back(vec3(0.35f, -3.45f, 0.32f));
    h.bottomCol = col(0.12f, 0.2f, 0.45f);
    PMesh m;
    PMesh::Mark mk = m.mark();
    H.emit(m);
    // bow rail (right)
    m.newGroup(40.f);
    m.use(MAT_CHROME, kCol1);
    {
        std::vector<vec3> rail;
        for (int k = 0; k <= 8; k++) {
            float y = lerp(1.2f, 4.6f, k / 8.f);
            rail.push_back(vec3(Max(h.sheerX(y) - 0.07f, 0.f), y, H.deckZ(y) + 0.55f));
        }
        tube1(m, rail, 0.015f, 6, true);
        for (int k = 0; k < 5; k++) {
            float y = lerp(1.3f, 4.2f, k / 4.f);
            float x = Max(h.sheerX(y) - 0.07f, 0.f);
            cyl(m, vec3(x, y, H.deckZ(y)), vec3(x, y, H.deckZ(y) + 0.55f), 0.012f, 6);
        }
        // rod holders along the gunwale
        m.use(MAT_METAL_BRUSHED, col(0.7f, 0.7f, 0.72f));
        for (int k = 0; k < 3; k++) {
            float y = -3.2f + k * 0.6f;
            float x = h.sheerX(y) - 0.08f;
            cyl(m, vec3(x, y, H.deckZ(y) - 0.2f), vec3(x - 0.02f, y - 0.03f, H.deckZ(y) + 0.02f), 0.025f, 8, false);
        }
    }
    m.mirrorX(mk);
    // cabin superstructure forward: loft with windows
    {
        m.newGroup(36.f);
        m.use(MAT_METAL_PAINTED, col(0.95f, 0.95f, 0.95f));
        std::vector<LoftSec> c;
        c.push_back({0.35f, 0, 1.45f, 1.08f, 0.40f, 5.f});
        c.push_back({1.2f, 0, 1.45f, 1.08f, 0.40f, 5.f});
        c.push_back({2.4f, 0, 1.40f, 0.92f, 0.34f, 4.f});
        c.push_back({3.1f, 0, 1.38f, 0.60f, 0.22f, 3.f});
        c.push_back({3.3f, 0, 1.36f, 0.15f, 0.05f, 2.f});
        loftY(m, c, 24, true, true);
        m.use(MAT_CAR_GLASS, kCol1);
        for (int s = -1; s <= 1; s += 2) {
            roundedBox(m, Frame(vec3(s * 1.03f, 1.55f, 1.62f), vec3(0, (float)-s, 0), vec3(0, 0, 1), vec3((float)s, 0, 0)), vec3(0.55f, 0.11f, 0.03f), 0.05f, 1);
            roundedBox(m, Frame(vec3(s * 0.62f, 2.72f, 1.62f), normalize(vec3(-s * 0.45f, 1, 0)), vec3(0, 0, 1), normalize(vec3(s, 0.45f, 0.))), vec3(0.28f, 0.09f, 0.04f), 0.04f, 1);
        }
        // windshield at the front of the cabin top (to the helm)
        std::vector<vec3> base;
        for (int k = 0; k <= 6; k++) {
            float x = lerp(-0.95f, 0.95f, k / 6.f);
            base.push_back(vec3(x, 0.62f - 0.15f * Sq(x), 1.85f));
        }
        glassRibbon(m, base, normalize(vec3(0, -0.5f, 1)), 0.38f, false);
    }
    // hardtop on posts over the helm
    m.newGroup(40.f);
    m.use(MAT_METAL_BRUSHED, col(0.75f, 0.75f, 0.77f));
    for (int s = -1; s <= 1; s += 2) {
        cyl(m, vec3(s * 0.95f, 0.5f, 1.5f), vec3(s * 0.9f, 0.45f, 2.55f), 0.025f, 8);
        cyl(m, vec3(s * 0.95f, -1.1f, H.deckZ(-1.1f)), vec3(s * 0.9f, -1.1f, 2.55f), 0.025f, 8);
    }
    m.use(MAT_METAL_PAINTED, col(0.95f, 0.95f, 0.95f));
    roundedBoxAt(m, vec3(0, -0.25f, 2.6f), vec3(1.15f, 1.05f, 0.06f), 0.05f, 2);
    m.use(MAT_METAL_BRUSHED, col(0.75f, 0.75f, 0.77f));
    for (int k = 0; k < 5; k++) cyl(m, vec3(-0.6f + k * 0.3f, -1.1f, 2.62f), vec3(-0.6f + k * 0.3f, -1.4f, 3.1f), 0.018f, 6, false);
    // helm station + leaning post
    m.newGroup(35.f);
    m.use(MAT_METAL_PAINTED, col(0.95f, 0.95f, 0.95f));
    roundedBoxAt(m, vec3(0.3f, 0.15f, 0.8f), vec3(0.5f, 0.25f, 0.45f), 0.06f, 2);
    m.use(MAT_PLASTIC, col(0.25f, 0.25f, 0.25f));
    roundedBox(m, Frame(vec3(0.3f, -0.08f, 1.28f), vec3(1, 0, 0), normalize(vec3(0, 0.5f, 1)), normalize(vec3(0, -1, 0.5f))), vec3(0.45f, 0.14f, 0.02f), 0.02f, 1);
    steeringWheel(m, vec3(0.45f, -0.18f, 1.32f), 0.9f, 0.18f);
    boatSeat(m, vec3(0.3f, -0.75f, 0.95f), 0.45f, vec3(0.9f, 0.9f, 0.88f));
    // fish box / bait well
    m.use(MAT_METAL_PAINTED, col(0.93f, 0.93f, 0.93f));
    roundedBoxAt(m, vec3(0, -2.6f, 0.55f), vec3(0.5f, 0.4f, 0.22f), 0.06f, 2);
    navLights(m, vec3(-h.sheerX(3.8f) + 0.02f, 3.8f, H.deckZ(3.8f) - 0.05f), vec3(h.sheerX(3.8f) - 0.02f, 3.8f, H.deckZ(3.8f) - 0.05f),
              vec3(0.f, -0.25f, 2.66f));
    // twin outboards (props static, under water)
    for (int s = -1; s <= 1; s += 2) {
        vec3 hub = outboard(m, s * 0.48f, -3.92f, 0.95f, 1.05f, vec3(0.92f, 0.92f, 0.92f));
        m.use(MAT_METAL_BRUSHED, col(0.7f, 0.7f, 0.72f));
        for (int b = 0; b < 3; b++) {
            float a = kTwoPi * b / 3.f;
            roundedBox(m, Frame(hub + vec3(cosf(a), 0, sinf(a)) * 0.12f, vec3(-sinf(a), 0.4f, cosf(a)), vec3(cosf(a), 0, sinf(a)), vec3(0, 1, 0)),
                       vec3(0.07f, 0.1f, 0.006f), 0.02f, 1);
        }
    }
    finalizeMesh(m, o.body);
    boatFloats(o, H, 6);
    o.seats.push_back(SeatSpec{vec3(0.45f, -0.75f, 1.0f), true, false});
    o.seats.push_back(SeatSpec{vec3(-0.35f, -0.75f, 1.0f), false, true});
    o.seats.push_back(SeatSpec{vec3(0.6f, -2.6f, 0.8f), false, false});
    addLight(o, vec3(0, 3.0f, 1.6f), vec3(0, 1, -0.05f), LT_HEAD);
    addLight(o, vec3(0, -0.25f, 2.7f), vec3(0, 0, 1), LT_BEACON);
    AABB bb = o.body.bounds;
    o.boxCenter = vec3(0, (bb.mn.y + bb.mx.y) * 0.5f, 0.6f);
    o.boxHalf = vec3(1.46f, (bb.mx.y - bb.mn.y) * 0.5f, 1.1f);
    physics(o, 4200.f, 370.f, 700.f, 6000.f, 20.f, 1, 0.f, 1.f, 0.f, 1.f, 0.40f, 0.f, vec3(0, -0.5f, 0.3f), Audio::ENGINE_V8);
    o.frontalArea = 3.4f;
    o.paletteColors = palette("boat");
    o.spawnWeight = 0.8f; o.price = 145000;
}

inline void mdlWavekite(VehicleModel& o) {
    o.name = "Wavekite"; o.maker = "Sakaki"; o.cls = VC_JETSKI;
    Hull H;
    HullSpec& h = H.h;
    h.yT = -1.55f; h.yB = 1.75f;
    h.keel.add(-1.55f, -0.24f).add(0.4f, -0.25f).add(1.1f, -0.16f).add(1.5f, 0.02f).add(1.75f, 0.28f).build();
    h.chineX.add(-1.55f, 0.42f).add(0.0f, 0.5f).add(0.9f, 0.42f).add(1.5f, 0.15f).add(1.72f, 0.f).build();
    h.sheerX.add(-1.55f, 0.52f).add(-0.4f, 0.60f).add(0.6f, 0.58f).add(1.3f, 0.35f).add(1.65f, 0.1f).add(1.75f, 0.f).build();
    h.sheerZ.add(-1.55f, 0.22f).add(0.0f, 0.26f).add(1.2f, 0.36f).add(1.75f, 0.42f).build();
    h.deadrise = 0.42f;
    h.gunwaleW = 0.06f;
    h.recesses.push_back(vec3(0.55f, -1.15f, 0.12f));  // footwells either side of the seat pedestal
    h.hullMat = MAT_CARPAINT; h.hullCol = kCol2;
    h.bottomCol = col(0.2f, 0.2f, 0.22f);
    h.deckMat = MAT_CARPAINT; h.deckCol = kCol1;
    h.wallMat = MAT_CARPAINT; h.wallCol = kCol1;
    h.floorMat = MAT_RUBBER; h.floorCol = kCol1;
    PMesh m;
    PMesh::Mark mk = m.mark();
    H.emit(m);
    m.mirrorX(mk);
    // pedestal + seat
    m.newGroup(38.f);
    m.use(MAT_CARPAINT, kCol1);
    {
        std::vector<LoftSec> p;
        p.push_back({0.62f, 0, 0.26f, 0.08f, 0.12f, 2.5f});
        p.push_back({0.40f, 0, 0.36f, 0.20f, 0.24f, 3.f});
        p.push_back({-0.6f, 0, 0.34f, 0.22f, 0.22f, 3.f});
        p.push_back({-1.12f, 0, 0.30f, 0.20f, 0.18f, 3.f});
        p.push_back({-1.2f, 0, 0.28f, 0.12f, 0.1f, 2.5f});
        loftY(m, p, 16, true, true);
        // hood / front deck dome
        std::vector<LoftSec> d;
        d.push_back({1.72f, 0, 0.42f, 0.04f, 0.04f, 2.f});
        d.push_back({1.45f, 0, 0.48f, 0.30f, 0.12f, 2.4f});
        d.push_back({1.0f, 0, 0.55f, 0.46f, 0.20f, 2.6f});
        d.push_back({0.62f, 0, 0.58f, 0.40f, 0.22f, 2.6f});
        d.push_back({0.48f, 0, 0.56f, 0.24f, 0.18f, 2.4f});
        loftY(m, d, 18, true, true);
    }
    m.newGroup(45.f);
    m.use(MAT_LEATHER, col(0.25f, 0.25f, 0.27f));
    {
        std::vector<LoftSec> s;
        s.push_back({0.35f, 0, 0.62f, 0.12f, 0.05f, 2.5f});
        s.push_back({0.1f, 0, 0.64f, 0.19f, 0.07f, 3.f});
        s.push_back({-0.7f, 0, 0.66f, 0.18f, 0.07f, 3.f});
        s.push_back({-0.95f, 0, 0.62f, 0.13f, 0.05f, 2.5f});
        loftY(m, s, 14, true, true);
    }
    // handlebar pod
    m.newGroup(40.f);
    m.use(MAT_CARPAINT, kCol2);
    roundedBox(m, Frame(vec3(0, 0.55f, 0.82f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.4f)), normalize(vec3(0, -0.4f, 1))), vec3(0.14f, 0.12f, 0.08f), 0.05f, 2);
    m.use(MAT_METAL_PAINTED, col(0.15f, 0.15f, 0.15f));
    cyl(m, vec3(-0.34f, 0.5f, 0.9f), vec3(0.34f, 0.5f, 0.9f), 0.014f, 6);
    m.use(MAT_RUBBER, kCol1);
    for (int s = -1; s <= 1; s += 2) cyl(m, vec3(s * 0.24f, 0.5f, 0.9f), vec3(s * 0.36f, 0.5f, 0.9f), 0.02f, 8);
    m.use(MAT_PLASTIC, col(0.3f, 0.3f, 0.3f));
    roundedBox(m, Frame(vec3(0, 0.62f, 0.9f), vec3(1, 0, 0), vec3(0, 0, 1), normalize(vec3(0, -1, 0.6f))), vec3(0.08f, 0.05f, 0.005f), 0.01f, 1);
    // mirrors, jet nozzle, rear platform grab handle
    m.use(MAT_CARPAINT, kCol2);
    for (int s = -1; s <= 1; s += 2) roundedBoxAt(m, vec3(s * 0.33f, 0.75f, 0.75f), vec3(0.06f, 0.02f, 0.035f), 0.02f, 1);
    m.use(MAT_METAL_PAINTED, col(0.15f, 0.15f, 0.15f));
    {
        std::vector<vec2> nz;
        nz.push_back(vec2(0.f, 0.09f)); nz.push_back(vec2(-0.12f, 0.07f)); nz.push_back(vec2(-0.2f, 0.065f)); nz.push_back(vec2(-0.2f, 0.f));
        for (auto& q : nz) q.x = -q.x;
        lathe(m, vec3(0, -1.5f, -0.08f), vec3(0, -1, 0), vec3(1, 0, 0), nz, 12);
    }
    m.use(MAT_METAL_BRUSHED, col(0.6f, 0.6f, 0.62f));
    cyl(m, vec3(-0.12f, -1.1f, 0.5f), vec3(0.12f, -1.1f, 0.5f), 0.014f, 6);
    m.use(MAT_LIGHT_HEAD, kCol1);
    for (int s = -1; s <= 1; s += 2)
        roundedBox(m, Frame(vec3(s * 0.18f, 1.52f, 0.55f), vec3(1, 0, 0), vec3(0, 0, 1), normalize(vec3(s * 0.5f, 1, 0.3f))), vec3(0.06f, 0.02f, 0.01f), 0.01f, 1);
    finalizeMesh(m, o.body);
    boatFloats(o, H, 4);
    o.seats.push_back(SeatSpec{vec3(0, -0.05f, 0.72f), true, true});
    o.seats.push_back(SeatSpec{vec3(0, -0.6f, 0.74f), false, true});
    addLight(o, vec3(0, 1.55f, 0.55f), vec3(0, 1, 0), LT_HEAD);
    o.boxCenter = vec3(0, 0.1f, 0.3f);
    o.boxHalf = vec3(0.62f, 1.65f, 0.55f);
    physics(o, 390.f, 190.f, 220.f, 7800.f, 30.f, 1, 0.f, 1.f, 0.f, 1.f, 0.45f, 0.f, vec3(0, -0.2f, 0.25f), Audio::ENGINE_BIKE_SPORT);
    o.frontalArea = 0.9f;
    o.paletteColors = palette("sport");
    o.spawnWeight = 0.8f; o.price = 14500;
}

// Airboat: flat aluminium hull, raised driver's seat, big caged propeller (rotor, spin axis +Y) and rudders.
inline void mdlSkimmer(VehicleModel& o) {
    o.name = "Skimmer"; o.maker = "Okahatchee Boatworks"; o.cls = VC_AIRBOAT;
    Hull H;
    HullSpec& h = H.h;
    h.yT = -2.2f; h.yB = 2.75f;
    h.keel.add(-2.2f, -0.12f).add(1.2f, -0.12f).add(2.0f, -0.04f).add(2.45f, 0.14f).add(2.75f, 0.45f).build();
    h.chineX.add(-2.2f, 1.12f).add(1.0f, 1.12f).add(2.1f, 1.0f).add(2.55f, 0.82f).add(2.75f, 0.7f).build();
    h.sheerX.add(-2.2f, 1.2f).add(1.2f, 1.2f).add(2.2f, 1.12f).add(2.6f, 0.95f).add(2.75f, 0.85f).build();
    h.sheerZ.add(-2.2f, 0.34f).add(1.2f, 0.34f).add(2.4f, 0.45f).add(2.75f, 0.58f).build();
    h.deadrise = 0.03f;
    h.deadriseBow = 0.05f;
    h.gunwaleW = 0.06f;
    h.deckCrown = 0.0f;
    h.recesses.push_back(vec3(2.35f, -2.05f, 0.08f));
    h.hullMat = MAT_METAL_BRUSHED; h.hullCol = col(0.75f, 0.76f, 0.78f);
    h.bottomMat = MAT_METAL_BRUSHED; h.bottomCol = col(0.6f, 0.6f, 0.62f);
    h.deckMat = MAT_CARPAINT; h.deckCol = kCol1;
    h.wallMat = MAT_METAL_BRUSHED; h.wallCol = col(0.7f, 0.7f, 0.72f);
    h.floorMat = MAT_METAL_BRUSHED; h.floorCol = col(0.55f, 0.55f, 0.57f);
    h.railMat = MAT_CARPAINT; h.railCol = kCol1;
    h.hardChine = true;
    PMesh m;
    PMesh::Mark mk = m.mark();
    H.emit(m);
    m.mirrorX(mk);
    // passenger benches
    boatSeat(m, vec3(0, 1.25f, 0.55f), 0.95f, vec3(0.25f, 0.22f, 0.2f));
    boatSeat(m, vec3(0, 0.35f, 0.75f), 0.95f, vec3(0.25f, 0.22f, 0.2f));
    m.use(MAT_METAL_BRUSHED, col(0.7f, 0.7f, 0.72f));
    roundedBoxAt(m, vec3(0, 0.45f, 0.35f), vec3(0.95f, 0.25f, 0.28f), 0.02f, 1);
    // driver's tower seat
    m.newGroup(40.f);
    m.use(MAT_METAL_PAINTED, col(0.12f, 0.12f, 0.12f));
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) cyl(m, vec3(sx * 0.32f, -0.45f + sy * 0.3f, 0.1f), vec3(sx * 0.22f, -0.5f + sy * 0.18f, 1.28f), 0.025f, 6);
    for (int sy = -1; sy <= 1; sy += 2) cyl(m, vec3(-0.25f, -0.5f + sy * 0.2f, 0.75f), vec3(0.25f, -0.5f + sy * 0.2f, 0.75f), 0.02f, 6);
    boatSeat(m, vec3(0, -0.52f, 1.42f), 0.3f, vec3(0.2f, 0.18f, 0.16f));
    m.use(MAT_METAL_PAINTED, col(0.12f, 0.12f, 0.12f));
    cyl(m, vec3(-0.35f, -0.25f, 1.1f), vec3(-0.35f, -0.05f, 1.55f), 0.02f, 6);  // rudder stick
    cyl(m, vec3(0.2f, -0.2f, 1.3f), vec3(0.25f, 0.05f, 1.6f), 0.02f, 6);       // steering stick
    // engine (V8) with air cleaner and headers
    m.newGroup(38.f);
    m.use(MAT_METAL_PAINTED, col(0.55f, 0.08f, 0.06f));
    roundedBoxAt(m, vec3(0, -1.25f, 0.75f), vec3(0.3f, 0.35f, 0.25f), 0.06f, 2);
    m.use(MAT_CHROME, kCol1);
    for (int s = -1; s <= 1; s += 2) {
        roundedBox(m, Frame(vec3(s * 0.26f, -1.25f, 1.05f), vec3(0, 1, 0), normalize(vec3(-s * 0.7f, 0, 0.7f)), normalize(vec3(s * 0.7f, 0, 0.7f))),
                   vec3(0.32f, 0.07f, 0.05f), 0.03f, 1);
        for (int k = 0; k < 4; k++) {
            float y = -1.5f + k * 0.16f;
            std::vector<vec3> hp;
            hp.push_back(vec3(s * 0.32f, y, 0.85f));
            hp.push_back(vec3(s * 0.45f, y, 0.95f));
            hp.push_back(vec3(s * 0.47f, y - 0.05f, 1.45f));
            tube1(m, catmull(hp, 2), 0.022f, 6, true);
        }
    }
    std::vector<vec2> ac;
    ac.push_back(vec2(0.f, 0.f)); ac.push_back(vec2(0.f, 0.16f)); ac.push_back(vec2(0.06f, 0.16f)); ac.push_back(vec2(0.08f, 0.1f)); ac.push_back(vec2(0.08f, 0.f));
    lathe(m, vec3(0, -1.2f, 1.08f), vec3(0, 0, 1), vec3(1, 0, 0), ac, 18);
    m.use(MAT_METAL_PAINTED, col(0.2f, 0.2f, 0.2f));
    roundedBoxAt(m, vec3(0, -0.85f, 0.95f), vec3(0.32f, 0.05f, 0.3f), 0.02f, 1);  // radiator
    // prop shaft housing + cage
    vec3 hub(0, -1.88f, 1.45f);
    m.use(MAT_METAL_PAINTED, col(0.15f, 0.15f, 0.15f));
    cyl(m, vec3(0, -1.55f, 1.0f), hub + vec3(0, 0.2f, 0), 0.07f, 10);
    const float cageR = 1.02f;
    m.newGroup(45.f);
    m.use(MAT_METAL_PAINTED, col(0.8f, 0.8f, 0.82f));
    for (int r = 0; r < 3; r++) {
        float y = hub.y + 0.12f - r * 0.22f;
        torus(m, Frame(vec3(0, y, hub.z), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0)), cageR, 0.016f, 40, 5);
    }
    for (int k = 0; k < 12; k++) {
        float a = kTwoPi * k / 12.f;
        vec3 rd(cosf(a), 0, sinf(a));
        cyl(m, vec3(0, hub.y + 0.12f, hub.z) + rd * cageR, vec3(0, hub.y - 0.32f, hub.z) + rd * cageR, 0.012f, 5, false);
        cyl(m, vec3(0, hub.y - 0.32f, hub.z) + rd * cageR, vec3(0, hub.y - 0.32f, hub.z) + rd * 0.08f, 0.008f, 4, false);
    }
    for (int s = -1; s <= 1; s += 2) {
        cyl(m, vec3(s * 0.95f, -1.95f, 0.36f), vec3(0, hub.y - 0.1f, hub.z) + vec3(s * 0.7f, 0, -0.72f), 0.03f, 6);
        cyl(m, vec3(s * 0.4f, -1.2f, 0.36f), vec3(0, hub.y + 0.1f, hub.z) + vec3(s * 0.62f, 0, -0.8f), 0.03f, 6);
    }
    // rudders
    m.use(MAT_METAL_BRUSHED, col(0.72f, 0.72f, 0.74f));
    for (int k = -1; k <= 1; k++) roundedBoxAt(m, vec3(k * 0.55f, -2.42f, 1.45f), vec3(0.01f, 0.22f, 0.8f), 0.01f, 1);
    cyl(m, vec3(-0.6f, -2.42f, 2.3f), vec3(0.6f, -2.42f, 2.3f), 0.015f, 6);
    // spotlight + flag
    m.use(MAT_LIGHT_HEAD, kCol1);
    disk(m, vec3(0, 2.62f, 0.72f), vec3(0, 1, 0.2f), 0.07f, 12);
    m.use(MAT_METAL_PAINTED, col(0.1f, 0.1f, 0.1f));
    cyl(m, vec3(0.95f, -2.1f, 0.36f), vec3(0.95f, -2.1f, 3.2f), 0.008f, 5);
    m.use(MAT_FABRIC, col(0.95f, 0.5f, 0.1f));
    std::vector<vec2> flag;
    flag.push_back(vec2(0.f, 0.f)); flag.push_back(vec2(0.35f, 0.08f)); flag.push_back(vec2(0.f, 0.18f));
    extrude(m, flag, Frame(vec3(0.95f, -2.1f, 3.0f), vec3(0, -1, 0), vec3(0, 0, 1), vec3(1, 0, 0)), -0.002f, 0.002f);
    finalizeMesh(m, o.body);
    // propeller rotor: 2 laminated blades + hub
    {
        PMesh pm;
        pm.newGroup(40.f);
        pm.use(MAT_WOOD, col(0.9f, 0.8f, 0.7f));
        for (int b = 0; b < 2; b++) {
            float s = b == 0 ? 1.f : -1.f;
            std::vector<LoftSec> bl;
            bl.push_back({0.0f, 0, 0, 0.09f, 0.035f, 2.f});
            bl.push_back({0.15f, 0, 0, 0.1f, 0.03f, 2.f});
            bl.push_back({0.6f, 0, 0, 0.085f, 0.018f, 2.f});
            bl.push_back({0.92f, 0, 0, 0.05f, 0.012f, 2.f});
            bl.push_back({0.95f, 0, 0, 0.01f, 0.005f, 2.f});
            // blade along +x (or -x), chord in the x-z plane twisted: frame y = blade axis
            Frame f(vec3(0, 0, 0), normalize(vec3(0, 0.35f, s)), vec3(s, 0, 0), vec3(0, 0, 0));
            f.z = normalize(cross(f.x, f.y));
            loftFrame(pm, f, bl, 10, false, true);
        }
        pm.use(MAT_METAL_PAINTED, col(0.15f, 0.15f, 0.15f));
        std::vector<vec2> hb;
        hb.push_back(vec2(-0.1f, 0.f)); hb.push_back(vec2(-0.1f, 0.1f)); hb.push_back(vec2(0.08f, 0.1f)); hb.push_back(vec2(0.14f, 0.f));
        lathe(pm, vec3(0, 0, 0), vec3(0, -1, 0), vec3(1, 0, 0), hb, 12);
        finalizeMesh(pm, o.rotor);
    }
    o.rotorPos = hub;
    o.rotorRadius = 0.95f;
    boatFloats(o, H, 5);
    o.seats.push_back(SeatSpec{vec3(0, -0.52f, 1.45f), true, true});
    o.seats.push_back(SeatSpec{vec3(-0.45f, 0.35f, 0.75f), false, true});
    o.seats.push_back(SeatSpec{vec3(0.45f, 0.35f, 0.75f), false, false});
    o.seats.push_back(SeatSpec{vec3(-0.45f, 1.25f, 0.55f), false, true});
    o.seats.push_back(SeatSpec{vec3(0.45f, 1.25f, 0.55f), false, false});
    addLight(o, vec3(0, 2.62f, 0.72f), vec3(0, 1, 0.2f), LT_HEAD);
    o.boxCenter = vec3(0, 0.25f, 0.7f);
    o.boxHalf = vec3(1.22f, 2.5f, 0.85f);
    physics(o, 950.f, 300.f, 620.f, 5200.f, 24.f, 1, 0.f, 1.f, 0.f, 1.f, 0.55f, 0.f, vec3(0, -0.5f, 0.55f), Audio::ENGINE_V8);
    o.frontalArea = 3.5f;
    o.paletteColors = palette("boat");
    o.spawnWeight = 0.5f; o.price = 38000;
}

}  // namespace detail
}  // namespace Vehicles
