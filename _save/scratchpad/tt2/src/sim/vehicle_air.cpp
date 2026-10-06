// Aircraft: light high-wing plane and light helicopters (civil + police). Fuselages are half-section lofts with
// per-cell glazing; wings/tails are cambered airfoil lofts. Rotor meshes: propeller spins about +Y (plane),
// main rotor about +Z and tail rotor about +X (helicopters).
namespace Vehicles {
namespace detail {

// Half-section loft (right half, bottom centre -> top centre), classified per cell, mirrored by the caller.
struct HalfLoft {
    std::vector<float> rows;
    int NS = 0;
    std::vector<vec3> G;
    std::function<void(float y, vec3* out)> section;
    std::function<void(vec3 c, int j, u8& mat, u32& color)> classify;
    bool capFirst = false, capLast = false;
    u8 capMat = MAT_CARPAINT;
    u32 capCol = kCol1;
    void build(PMesh& m) {
        int nr = (int)rows.size();
        G.resize(nr * NS);
        for (int i = 0; i < nr; i++) {
            section(rows[i], &G[i * NS]);
            G[i * NS].x = 0.f;
            G[i * NS + NS - 1].x = 0.f;
        }
        for (int i = 0; i + 1 < nr; i++)
            for (int j = 0; j + 1 < NS; j++) {
                vec3 a = G[i * NS + j], b = G[(i + 1) * NS + j], c = G[(i + 1) * NS + j + 1], d = G[i * NS + j + 1];
                if (length(cross(c - a, d - b)) < 1e-8f) continue;
                vec3 ctr = (a + b + c + d) * 0.25f;
                u8 mt = MAT_CARPAINT;
                u32 cl = kCol1;
                classify(ctr, j, mt, cl);
                m.use(mt, cl);
                u32 ia = m.add(a), ib = m.add(b), ic = m.add(c), id = m.add(d);
                // outward = away from the section's axis (x = 0, z = section mid)
                vec3 mid = (G[i * NS + NS / 2] + G[(i + 1) * NS + NS / 2]) * 0.5f;
                vec3 axisP((0.f), ctr.y, (G[i * NS].z + G[i * NS + NS - 1].z) * 0.5f);
                (void)mid;
                vec3 out = ctr - axisP;
                out.y = 0.f;
                if (length2(out) < 1e-10f) out = vec3(1, 0, 0);
                m.quadFacing(ia, ib, ic, id, out);
            }
        for (int e = 0; e < 2; e++) {
            if ((e == 0 && !capFirst) || (e == 1 && !capLast)) continue;
            int i = e == 0 ? 0 : nr - 1;
            const vec3* S = &G[i * NS];
            vec3 ctr(0.f, S[0].y, (S[0].z + S[NS - 1].z) * 0.5f);
            vec3 want(0, e == 0 ? -1.f : 1.f, 0);
            m.use(capMat, capCol);
            u32 ic = m.add(ctr);
            for (int j = 0; j + 1 < NS; j++) {
                u32 a = m.add(S[j]), b = m.add(S[j + 1]);
                vec3 fn = cross(S[j] - ctr, S[j + 1] - ctr);
                if (length2(fn) < 1e-12f) continue;
                if (dot(fn, want) >= 0.f) m.tri(ic, a, b);
                else m.tri(ic, b, a);
            }
        }
    }
};

// Cambered airfoil (NACA-like) outline in (chord 0..1, thickness), CCW from the trailing edge along the top.
inline std::vector<vec2> airfoil(float th, float camber, int n) {
    std::vector<vec2> up, lo;
    for (int i = 0; i <= n; i++) {
        float x = 0.5f - 0.5f * cosf(kPi * i / n);  // cosine spacing
        float t = 5.f * th * (0.2969f * sqrtf(x) - 0.126f * x - 0.3516f * x * x + 0.2843f * x * x * x - 0.1036f * x * x * x * x);
        float c = camber * (x < 0.4f ? (2.f * 0.4f * x - x * x) / 0.16f : ((1.f - 0.8f) + 2.f * 0.4f * x - x * x) / 0.36f);
        up.push_back(vec2(x, c + t));
        lo.push_back(vec2(x, c - t));
    }
    std::vector<vec2> o;
    for (int i = n; i >= 0; i--) o.push_back(up[i]);   // TE -> LE along the top
    for (int i = 1; i < n; i++) o.push_back(lo[i]);    // LE -> TE along the bottom
    return o;
}
// Loft an airfoil along stations: each station = position of the leading edge, chord direction (unit), up (unit),
// chord length, thickness scale. Caps both ends.
struct FoilSt {
    vec3 le, chordDir, up;
    float chord;
};
inline void foilLoft(PMesh& m, const std::vector<FoilSt>& st, const std::vector<vec2>& prof, bool capA, bool capB) {
    int n = (int)prof.size(), rows = (int)st.size();
    std::vector<u32> id(rows * n);
    for (int i = 0; i < rows; i++)
        for (int k = 0; k < n; k++) {
            const FoilSt& s = st[i];
            id[i * n + k] = m.add(s.le + s.chordDir * (prof[k].x * s.chord) + s.up * (prof[k].y * s.chord));
        }
    for (int i = 0; i + 1 < rows; i++)
        for (int k = 0; k < n; k++) {
            int k1 = (k + 1) % n;
            u32 a = id[i * n + k], b = id[(i + 1) * n + k], c = id[(i + 1) * n + k1], d = id[i * n + k1];
            vec3 ctrLine = (st[i].le + st[i].chordDir * (0.35f * st[i].chord) + st[i + 1].le + st[i + 1].chordDir * (0.35f * st[i + 1].chord)) * 0.5f;
            vec3 mid = (m.P[a] + m.P[b] + m.P[c] + m.P[d]) * 0.25f;
            vec3 span = normalize(st[i + 1].le - st[i].le);
            vec3 out = mid - ctrLine;
            out -= span * dot(out, span);
            m.quadFacing(a, b, c, d, out);
        }
    for (int e = 0; e < 2; e++) {
        if ((e == 0 && !capA) || (e == 1 && !capB)) continue;
        int i = e == 0 ? 0 : rows - 1;
        vec3 outDir = e == 0 ? st[0].le - st[1].le : st[rows - 1].le - st[rows - 2].le;
        std::vector<u32> tris;
        triangulatePolygon(prof, tris);
        for (size_t t = 0; t + 2 < tris.size(); t += 3) {
            u32 a = id[i * n + tris[t]], b = id[i * n + tris[t + 1]], c = id[i * n + tris[t + 2]];
            vec3 fn = cross(m.P[b] - m.P[a], m.P[c] - m.P[a]);
            if (dot(fn, outDir) >= 0.f) m.tri(a, b, c);
            else m.tri(a, c, b);
        }
    }
}

// ------------------------------------------------------------------------------------------------
inline void mdlTern(VehicleModel& o) {
    o.name = "Tern 180"; o.maker = "Corbett Aero"; o.cls = VC_PLANE;
    PMesh m;
    const float R = 0.21f, W = 0.14f;
    // fuselage profile functions (y from tail -6.55 to spinner 1.62)
    Curve hw, zt, zb, ex;
    hw.add(-6.55f, 0.06f).add(-5.5f, 0.14f).add(-3.5f, 0.32f).add(-1.85f, 0.54f).add(-0.8f, 0.57f).add(0.55f, 0.56f).add(1.0f, 0.50f).add(1.3f, 0.36f).add(1.46f, 0.17f).build();
    zt.add(-6.55f, 1.58f).add(-5.5f, 1.62f).add(-3.5f, 1.75f).add(-1.85f, 2.0f).add(0.0f, 2.03f).add(0.55f, 1.66f).add(1.0f, 1.58f).add(1.3f, 1.47f).add(1.46f, 1.40f).build();
    zb.add(-6.55f, 1.42f).add(-5.5f, 1.32f).add(-3.5f, 1.08f).add(-1.85f, 0.78f).add(-0.8f, 0.72f).add(0.55f, 0.74f).add(1.0f, 0.86f).add(1.3f, 0.98f).add(1.46f, 1.08f).build();
    ex.add(-6.55f, 2.0f).add(-1.85f, 2.6f).add(0.55f, 2.8f).add(1.46f, 2.1f).build();
    HalfLoft fl;
    fl.NS = 13;
    for (float y = -6.55f; y <= 1.461f; y += 0.12f) fl.rows.push_back(Min(y, 1.46f));
    fl.rows.push_back(1.46f);
    fl.capFirst = fl.capLast = true;
    const float winY[] = {0.55f, 0.02f, -0.02f, -0.85f, -0.95f, -1.75f};
    for (float y : winY) fl.rows.push_back(y);
    std::sort(fl.rows.begin(), fl.rows.end());
    fl.rows.erase(std::unique(fl.rows.begin(), fl.rows.end(), [](float a, float b) { return fabsf(a - b) < 0.004f; }), fl.rows.end());
    fl.section = [&](float y, vec3* out) {
        float w = hw(y), top = zt(y), bot = zb(y), e = ex(y);
        float cz = (top + bot) * 0.5f, hh = (top - bot) * 0.5f;
        for (int j = 0; j < 13; j++) {
            float a = -kHalfPi + kPi * j / 12.f;
            vec2 p = superEll(a, w, hh, e);
            out[j] = vec3(p.x, y, cz + p.y);
        }
    };
    fl.classify = [&](vec3 c, int j, u8& mt, u32& cl) {
        mt = MAT_CARPAINT;
        cl = kCol1;
        float top = zt(c.y);
        bool side = j >= 5 && j <= 9;
        if (c.y < 0.55f && c.y > 0.02f && c.z > lerp(1.66f, 2.0f, (0.55f - c.y) / 0.53f) - 0.02f && j >= 8) { mt = MAT_CAR_GLASS; return; }  // windshield
        if (side && c.z > 1.42f && c.z < top - 0.1f && ((c.y < 0.02f && c.y > -0.85f) || (c.y < -0.95f && c.y > -1.75f))) { mt = MAT_CAR_GLASS; return; }
        if (j == 5 && c.y < 1.1f) { cl = kCol2; return; }  // cheat line along the widest part of the fuselage
        if (c.z < zb(c.y) + 0.12f && c.y > -1.8f) { mt = MAT_CARPAINT; cl = kCol2; }
    };
    PMesh::Mark mk = m.mark();
    m.newGroup(35.f);
    fl.build(m);
    // cheat line stripe along the tail cone (secondary paint)
    m.mirrorX(mk);
    // cowling air inlets and exhaust
    m.newGroup(40.f);
    m.use(MAT_PLASTIC, col(0.1f, 0.1f, 0.1f));
    for (int s = -1; s <= 1; s += 2) {
        vec3 nrm = normalize(vec3(s * 0.35f, 1.f, 0.25f));
        roundedBox(m, Frame(vec3(s * 0.2f, 1.37f, 1.33f), normalize(cross(vec3(0, 0, 1), nrm)), normalize(cross(nrm, normalize(cross(vec3(0, 0, 1), nrm)))), nrm),
                   vec3(0.075f, 0.045f, 0.02f), 0.02f, 1);
    }
    m.use(MAT_METAL_BRUSHED, col(0.5f, 0.45f, 0.4f));
    cyl(m, vec3(0.18f, 0.9f, 0.78f), vec3(0.2f, 0.75f, 0.70f), 0.03f, 8);
    // wing (high, strut braced) with dihedral
    std::vector<vec2> wf = airfoil(0.12f, 0.02f, 10);
    m.newGroup(30.f);
    m.use(MAT_CARPAINT, kCol1);
    for (int s = -1; s <= 1; s += 2) {
        std::vector<FoilSt> st;
        float dih = 0.03f;
        float xs[4] = {0.0f, 2.6f, 4.9f, 5.5f};
        float ch[4] = {1.63f, 1.63f, 1.25f, 1.12f};
        float le[4] = {0.02f, 0.02f, -0.12f, -0.16f};
        for (int k = 0; k < 4; k++) {
            FoilSt f;
            f.le = vec3(s * xs[k], le[k], 2.04f + xs[k] * dih);
            f.chordDir = vec3(0, -1, 0);
            f.up = vec3(0, 0, 1);
            f.chord = ch[k];
            st.push_back(f);
        }
        foilLoft(m, st, wf, false, true);
        // strut
        m.use(MAT_CARPAINT, kCol1);
        std::vector<LoftSec> sl;
        sl.push_back({0.f, 0, 0, 0.055f, 0.018f, 2.f});
        sl.push_back({1.f, 0, 0, 0.055f, 0.018f, 2.f});
        vec3 a0(s * 0.52f, -0.35f, 0.86f), a1(s * 2.6f, -0.5f, 2.06f + 2.6f * dih);
        Frame sf = frameFY(a0, a1 - a0, vec3(0, 1, 0));
        sl[1].y = length(a1 - a0);
        loftFrame(m, sf, sl, 8, true, true);
        // wingtip lights
        m.use(s < 0 ? MAT_LIGHT_TAIL : MAT_EMISSIVE, s < 0 ? col(1.f, 0.f, 0.f) : col(0.1f, 1.f, 0.3f, 0.25f));
        roundedBoxAt(m, vec3(s * 5.52f, -0.12f, 2.04f + 5.5f * dih + 0.02f), vec3(0.02f, 0.05f, 0.025f), 0.01f, 1);
        m.use(MAT_CARPAINT, kCol1);
    }
    m.use(MAT_LIGHT_HEAD, kCol1);
    roundedBox(m, Frame(vec3(-2.0f, 0.03f, 2.07f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0)), vec3(0.09f, 0.03f, 0.008f), 0.02f, 1);
    // horizontal stabiliser + elevator
    std::vector<vec2> sf = airfoil(0.10f, 0.f, 8);
    for (int s = -1; s <= 1; s += 2) {
        std::vector<FoilSt> st;
        float xs[2] = {0.0f, 1.72f};
        float ch[2] = {1.12f, 0.72f};
        float le[2] = {-5.42f, -5.62f};
        for (int k = 0; k < 2; k++) {
            FoilSt f;
            f.le = vec3(s * xs[k], le[k], 1.46f);
            f.chordDir = vec3(0, -1, 0);
            f.up = vec3(0, 0, 1);
            f.chord = ch[k];
            st.push_back(f);
        }
        m.use(MAT_CARPAINT, kCol1);
        foilLoft(m, st, sf, false, true);
    }
    // vertical fin + rudder (stations along z)
    {
        std::vector<FoilSt> st;
        float zs[3] = {1.55f, 2.3f, 2.85f};
        float ch[3] = {1.45f, 1.0f, 0.72f};
        float le[3] = {-5.15f, -5.55f, -5.78f};
        for (int k = 0; k < 3; k++) {
            FoilSt f;
            f.le = vec3(0, le[k], zs[k]);
            f.chordDir = vec3(0, -1, 0);
            f.up = vec3(1, 0, 0);
            f.chord = ch[k];
            st.push_back(f);
        }
        m.use(MAT_CARPAINT, kCol2);
        foilLoft(m, st, sf, false, true);
        m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));  // red anti-collision beacon (glows with the lights)
        ellipsoid(m, Frame(vec3(0, -5.95f, 2.87f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), vec3(0.03f, 0.05f, 0.03f), 8, 4);
    }
    // landing gear: sprung main legs + wheel pants, nose oleo + pant
    m.newGroup(40.f);
    for (int s = -1; s <= 1; s += 2) {
        m.use(MAT_CARPAINT, kCol1);
        std::vector<LoftSec> lg;
        lg.push_back({0.f, 0, 0, 0.05f, 0.012f, 2.f});
        lg.push_back({1.f, 0, 0, 0.04f, 0.01f, 2.f});
        vec3 a0(s * 0.45f, -0.83f, 0.76f), a1(s * 1.2f, -0.83f, R + 0.06f);
        Frame gf = frameFY(a0, a1 - a0, vec3(0, 1, 0));
        lg[1].y = length(a1 - a0);
        loftFrame(m, gf, lg, 8, true, true);
        std::vector<LoftSec> pant;
        pant.push_back({0.42f, 0, 0.03f, 0.02f, 0.02f, 2.f});
        pant.push_back({0.25f, 0, 0.04f, 0.10f, 0.19f, 2.2f});
        pant.push_back({-0.05f, 0, 0.05f, 0.11f, 0.22f, 2.2f});
        pant.push_back({-0.35f, 0, 0.08f, 0.07f, 0.13f, 2.2f});
        pant.push_back({-0.5f, 0, 0.1f, 0.02f, 0.03f, 2.f});
        loftFrame(m, Frame(vec3(s * 1.2f, -0.83f, R), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), pant, 14, true, true);
    }
    m.use(MAT_CHROME, kCol1);
    cyl(m, vec3(0, 0.78f, 0.9f), vec3(0, 0.83f, R + 0.05f), 0.035f, 10);
    m.use(MAT_CARPAINT, kCol1);
    {
        std::vector<LoftSec> pant;
        pant.push_back({0.32f, 0, 0.03f, 0.02f, 0.02f, 2.f});
        pant.push_back({0.18f, 0, 0.04f, 0.09f, 0.17f, 2.2f});
        pant.push_back({-0.12f, 0, 0.06f, 0.08f, 0.16f, 2.2f});
        pant.push_back({-0.3f, 0, 0.08f, 0.02f, 0.03f, 2.f});
        loftFrame(m, Frame(vec3(0, 0.83f, R), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), pant, 12, true, true);
    }
    // interior: seats, panel, yokes
    {
        CarLook L;
        L.seatTint = vec3(0.5f, 0.45f, 0.38f);
        PMesh::Mark sm = m.mark();
        seat(m, vec3(0.27f, -0.25f, 1.12f), 0.22f, L, true, 0.25f);
        seat(m, vec3(0.27f, -1.15f, 1.12f), 0.22f, L, true, 0.25f);
        m.mirrorX(sm);
        m.newGroup(35.f);
        m.use(MAT_INTERIOR, kCol1);
        roundedBoxAt(m, vec3(0, 0.42f, 1.42f), vec3(0.52f, 0.1f, 0.18f), 0.03f, 1);
        for (int s = -1; s <= 1; s += 2) {
            cyl(m, vec3(s * 0.27f, 0.32f, 1.35f), vec3(s * 0.27f, 0.16f, 1.35f), 0.015f, 6);
            roundedBoxAt(m, vec3(s * 0.27f, 0.15f, 1.35f), vec3(0.14f, 0.015f, 0.04f), 0.015f, 1);
        }
    }
    // door outlines + registration
    {
        float slope = (hw(-2.9f) - hw(-3.1f)) / 0.2f, zc = (zt(-3.0f) + zb(-3.0f)) * 0.5f + 0.12f;
        for (int side = 0; side < 2; side++) {
            float sx = side == 0 ? 1.f : -1.f;
            vec3 right = normalize(vec3(sx * slope, 1.f, 0.f)) * (side == 0 ? 1.f : -1.f);
            flatText(m, vec3(sx * (hw(-3.0f) + 0.012f), -3.0f, zc), right, vec3(0, 0, 1), "PS-172T", 0.17f, MAT_METAL_PAINTED, col(0.9f, 0.9f, 0.88f));
        }
    }
    finalizeMesh(m, o.body);
    // propeller rotor (2 blades) + spinner, spin axis +Y
    {
        PMesh pm;
        pm.newGroup(40.f);
        pm.use(MAT_METAL_PAINTED, col(0.12f, 0.12f, 0.12f));
        std::vector<vec2> pr = airfoil(0.10f, 0.03f, 6);
        for (int b = 0; b < 2; b++) {
            float s = b == 0 ? 1.f : -1.f;
            std::vector<FoilSt> st;
            float rr[4] = {0.12f, 0.35f, 0.8f, 0.95f};
            float ch[4] = {0.10f, 0.13f, 0.09f, 0.05f};
            float tw[4] = {0.9f, 0.55f, 0.28f, 0.22f};
            for (int k = 0; k < 4; k++) {
                FoilSt f;
                vec3 radial(s, 0, 0);
                vec3 cd = normalize(vec3(0, -sinf(tw[k]), -s * cosf(tw[k])));
                f.chordDir = cd;
                f.up = normalize(cross(radial, cd)) * s;
                f.le = radial * rr[k] - cd * (ch[k] * 0.3f);
                f.chord = ch[k];
                st.push_back(f);
            }
            foilLoft(pm, st, pr, false, true);
        }
        pm.use(MAT_METAL_PAINTED, col(0.9f, 0.9f, 0.9f));
        std::vector<vec2> sp;
        sp.push_back(vec2(0.20f, 0.f)); sp.push_back(vec2(0.14f, 0.07f)); sp.push_back(vec2(0.04f, 0.13f)); sp.push_back(vec2(-0.02f, 0.14f)); sp.push_back(vec2(-0.02f, 0.f));
        for (auto& q : sp) q.x = -q.x;
        lathe(pm, vec3(0, 0, 0), vec3(0, -1, 0), vec3(1, 0, 0), sp, 16);
        finalizeMesh(pm, o.rotor);
    }
    o.rotorPos = vec3(0, 1.45f, 1.24f);
    WheelDesign wd;
    wd.R = R; wd.W = W; wd.rimR = 0.11f; wd.style = RIM_STEEL; wd.faceTint = vec3(0.85f); wd.lugs = 0; wd.seg = 24; wd.disc = false;
    buildWheel(wd, o.wheel);
    o.wheels.push_back(WheelSpec{vec3(0, 0.83f, R), R, W, true, false, false});
    o.wheels.push_back(WheelSpec{vec3(-1.2f, -0.83f, R), R, W, false, false, true});
    o.wheels.push_back(WheelSpec{vec3(1.2f, -0.83f, R), R, W, false, false, false});
    o.seats.push_back(SeatSpec{vec3(-0.27f, -0.25f, 1.12f), true, true});
    o.seats.push_back(SeatSpec{vec3(0.27f, -0.25f, 1.12f), false, false});
    o.seats.push_back(SeatSpec{vec3(-0.27f, -1.15f, 1.12f), false, true});
    o.seats.push_back(SeatSpec{vec3(0.27f, -1.15f, 1.12f), false, false});
    addLight(o, vec3(-2.0f, 0.05f, 2.07f), vec3(0, 1, -0.1f), LT_HEAD);
    addLight(o, vec3(-5.52f, -0.12f, 2.23f), vec3(-1, 0, 0), LT_TAIL);
    addLight(o, vec3(5.52f, -0.12f, 2.23f), vec3(1, 0, 0), LT_TAIL);
    addLight(o, vec3(0, -5.95f, 2.9f), vec3(0, 0, 1), LT_BEACON);
    // fuselage from spinner to tail cone (wings and gear legs stay outside: gear contact is through the wheels)
    o.boxCenter = vec3(0, -2.49f, 1.35f);
    o.boxHalf = vec3(0.58f, 4.11f, 0.68f);
    physics(o, 1100.f, 134.f, 470.f, 2700.f, 67.f, 1, 0.f, 0.8f, 0.12f, 1.2f, 0.34f, 0.f, vec3(0, -0.45f, 1.3f), Audio::ENGINE_I4);
    o.frontalArea = 1.9f;
    o.wingArea = 16.2f;
    o.liftSlope = 4.9f;
    o.paletteColors = palette("air");
    o.spawnWeight = 0.2f; o.price = 385000;
}

// Helicopter (police variant adds searchlight, FLIR, livery)
inline void heliBuild(VehicleModel& o, bool police) {
    PMesh m;
    Curve hw, zt, zb, ex;
    // cabin pod + boom handled separately; pod from y -1.3 to nose 1.9
    hw.add(-1.55f, 0.32f).add(-1.3f, 0.45f).add(-0.9f, 0.70f).add(0.2f, 0.78f).add(1.0f, 0.74f).add(1.55f, 0.55f).add(1.85f, 0.25f).add(1.93f, 0.02f).build();
    zt.add(-1.55f, 1.8f).add(-1.3f, 1.85f).add(-0.9f, 1.95f).add(0.3f, 1.98f).add(1.0f, 1.78f).add(1.55f, 1.45f).add(1.85f, 1.12f).add(1.93f, 0.98f).build();
    zb.add(-1.55f, 1.12f).add(-1.3f, 0.95f).add(-0.9f, 0.62f).add(0.2f, 0.56f).add(1.0f, 0.60f).add(1.55f, 0.72f).add(1.85f, 0.86f).add(1.93f, 0.95f).build();
    ex.add(-1.55f, 2.4f).add(-1.3f, 2.6f).add(0.2f, 3.0f).add(1.2f, 2.4f).add(1.93f, 2.0f).build();
    HalfLoft fl;
    fl.NS = 15;
    for (float y = -1.55f; y <= 1.931f; y += 0.1f) fl.rows.push_back(Min(y, 1.93f));
    fl.rows.push_back(1.93f);
    fl.capFirst = fl.capLast = true;
    fl.rows.push_back(0.85f);
    fl.rows.push_back(-0.05f);
    std::sort(fl.rows.begin(), fl.rows.end());
    fl.rows.erase(std::unique(fl.rows.begin(), fl.rows.end(), [](float a, float b) { return fabsf(a - b) < 0.004f; }), fl.rows.end());
    fl.section = [&](float y, vec3* out) {
        float w = hw(y), top = zt(y), bot = zb(y), e = ex(y);
        float cz = (top + bot) * 0.5f, hh = (top - bot) * 0.5f;
        for (int j = 0; j < 15; j++) {
            float a = -kHalfPi + kPi * j / 14.f;
            vec2 p = superEll(a, w, hh, e);
            out[j] = vec3(p.x, y, cz + p.y);
        }
    };
    fl.classify = [&](vec3 c, int j, u8& mt, u32& cl) {
        mt = MAT_CARPAINT;
        cl = kCol1;
        // bubble canopy: nose above the chin, and big side windows in the doors
        bool nose = c.y > 0.85f && c.z > 0.78f + (c.y - 0.85f) * 0.1f;
        bool doors = c.y < 0.8f && c.y > -0.9f && c.z > 1.12f && c.z < zt(c.y) - 0.12f && j >= 4 && j <= 11 && fabsf(c.y + 0.05f) > 0.04f;
        if (nose || doors) { mt = MAT_CAR_GLASS; return; }
        if (c.z < 0.9f || (police && c.z < 1.1f)) { cl = kCol2; }
    };
    PMesh::Mark mk = m.mark();
    m.newGroup(35.f);
    fl.build(m);
    m.mirrorX(mk);
    // canopy centre post (roof to nose tip)
    {
        std::vector<vec3> post;
        for (size_t i = 0; i < fl.rows.size(); i++) {
            float y = fl.rows[i];
            if (y < 0.8f) continue;
            vec3 p = fl.G[i * fl.NS + fl.NS - 1];
            vec3 q = i + 1 < fl.rows.size() ? fl.G[(i + 1) * fl.NS + fl.NS - 1] : p + vec3(0, 0.01f, -0.05f);
            vec3 t = normalize(q - p);
            vec3 nrm = normalize(cross(vec3(1, 0, 0), t));
            post.push_back(p + nrm * 0.008f);
        }
        m.newGroup(40.f);
        m.use(MAT_CARPAINT, kCol1);
        if (post.size() >= 2) tube1(m, catmull(post, 2), 0.028f, 8, true);
    }
    // engine / transmission fairing on top
    m.newGroup(36.f);
    m.use(MAT_CARPAINT, kCol1);
    {
        std::vector<LoftSec> t;
        t.push_back({0.75f, 0, 1.92f, 0.12f, 0.04f, 2.f});
        t.push_back({0.5f, 0, 2.02f, 0.42f, 0.14f, 2.6f, 0.5f});
        t.push_back({-0.3f, 0, 2.1f, 0.48f, 0.22f, 2.8f, 0.5f});
        t.push_back({-1.2f, 0, 2.02f, 0.44f, 0.20f, 2.8f, 0.5f});
        t.push_back({-1.9f, 0, 1.78f, 0.30f, 0.14f, 2.4f});
        t.push_back({-2.1f, 0, 1.72f, 0.2f, 0.1f, 2.2f});
        loftY(m, t, 18, true, true);
    }
    // tail boom
    {
        std::vector<LoftSec> b;
        b.push_back({-1.0f, 0, 1.45f, 0.42f, 0.42f, 2.2f});
        b.push_back({-1.9f, 0, 1.55f, 0.28f, 0.30f, 2.1f});
        b.push_back({-4.0f, 0, 1.64f, 0.17f, 0.18f, 2.f});
        b.push_back({-6.6f, 0, 1.72f, 0.11f, 0.12f, 2.f});
        b.push_back({-7.05f, 0, 1.74f, 0.09f, 0.10f, 2.f});
        loftY(m, b, 16, true, true);
    }
    // fin, ventral fin, stabiliser
    std::vector<vec2> sf = airfoil(0.12f, 0.f, 8);
    {
        std::vector<FoilSt> st;
        float zs[2] = {1.75f, 2.95f};
        float ch[2] = {0.95f, 0.55f};
        float le[2] = {-6.3f, -6.9f};
        for (int k = 0; k < 2; k++) {
            FoilSt f;
            f.le = vec3(0, le[k], zs[k]);
            f.chordDir = vec3(0, -1, 0);
            f.up = vec3(1, 0, 0);
            f.chord = ch[k];
            st.push_back(f);
        }
        m.use(MAT_CARPAINT, kCol2);
        foilLoft(m, st, sf, false, true);
        std::vector<FoilSt> vt;
        float zv[2] = {1.62f, 1.0f};
        float cv[2] = {0.7f, 0.45f};
        float lv[2] = {-6.4f, -6.75f};
        for (int k = 0; k < 2; k++) {
            FoilSt f;
            f.le = vec3(0, lv[k], zv[k]);
            f.chordDir = vec3(0, -1, 0);
            f.up = vec3(1, 0, 0);
            f.chord = cv[k];
            vt.push_back(f);
        }
        foilLoft(m, vt, sf, false, true);
        for (int s = -1; s <= 1; s += 2) {
            std::vector<FoilSt> ht;
            float xh[2] = {0.08f, 1.05f};
            float chh[2] = {0.55f, 0.42f};
            for (int k = 0; k < 2; k++) {
                FoilSt f;
                f.le = vec3(s * xh[k], -5.0f - k * 0.08f, 1.62f);
                f.chordDir = vec3(0, -1, 0);
                f.up = vec3(0, 0, 1);
                f.chord = chh[k];
                ht.push_back(f);
            }
            m.use(MAT_CARPAINT, kCol1);
            foilLoft(m, ht, sf, false, true);
            m.use(MAT_CARPAINT, kCol2);
            roundedBoxAt(m, vec3(s * 1.07f, -5.3f, 1.66f), vec3(0.016f, 0.17f, 0.1f), 0.015f, 1);  // end plates
        }
    }
    // tail rotor gearbox + mast/hub fairing
    m.use(MAT_METAL_PAINTED, col(0.2f, 0.2f, 0.2f));
    roundedBoxAt(m, vec3(-0.08f, -6.9f, 2.28f), vec3(0.1f, 0.12f, 0.1f), 0.05f, 1);
    cyl(m, vec3(0, -0.15f, 2.2f), vec3(0, -0.15f, 2.9f), 0.07f, 10);
    // skids
    m.newGroup(40.f);
    m.use(MAT_METAL_PAINTED, col(0.25f, 0.25f, 0.26f));
    for (int s = -1; s <= 1; s += 2) {
        std::vector<vec3> sk;
        sk.push_back(vec3(s * 1.12f, -1.55f, 0.08f));
        sk.push_back(vec3(s * 1.12f, 1.1f, 0.06f));
        sk.push_back(vec3(s * 1.12f, 1.45f, 0.12f));
        sk.push_back(vec3(s * 1.12f, 1.62f, 0.32f));
        tube1(m, catmull(sk, 4), 0.04f, 10, true);
        for (int k = 0; k < 2; k++) {
            float y = k == 0 ? 0.75f : -0.95f;
            std::vector<vec3> ct;
            ct.push_back(vec3(s * 1.12f, y, 0.08f));
            ct.push_back(vec3(s * 1.05f, y, 0.42f));
            ct.push_back(vec3(s * 0.78f, y, 0.62f));
            ct.push_back(vec3(s * 0.3f, y, 0.66f));
            tube1(m, catmull(ct, 3), 0.035f, 8, false);
        }
        // step plate hung from the front cross tube
        roundedBoxAt(m, vec3(s * 1.07f, 0.6f, 0.36f), vec3(0.08f, 0.15f, 0.012f), 0.01f, 1);
        // exhaust stacks
        m.use(MAT_METAL_BRUSHED, col(0.45f, 0.42f, 0.4f));
        cyl(m, vec3(s * 0.28f, -1.5f, 2.05f), vec3(s * 0.34f, -1.95f, 2.25f), 0.07f, 10);
        m.use(MAT_METAL_PAINTED, col(0.25f, 0.25f, 0.26f));
    }
    // interior: seats + panel
    {
        CarLook L;
        L.seatTint = police ? vec3(0.12f) : vec3(0.35f, 0.3f, 0.25f);
        PMesh::Mark sm = m.mark();
        seat(m, vec3(0.38f, 0.65f, 1.0f), 0.22f, L, true, 0.2f);
        m.mirrorX(sm);
        seat(m, vec3(0.0f, -0.55f, 1.0f), 0.6f, L, false, 0.2f);
        m.newGroup(35.f);
        m.use(MAT_INTERIOR, kCol1);
        roundedBoxAt(m, vec3(0, 1.28f, 1.1f), vec3(0.55f, 0.12f, 0.12f), 0.05f, 1);
        roundedBoxAt(m, vec3(0, 1.0f, 0.75f), vec3(0.12f, 0.35f, 0.16f), 0.04f, 1);
    }
    // lights: beacons (siren-style lens, red), nav, landing
    m.newGroup(40.f);
    m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));  // anti-collision beacons
    ellipsoid(m, Frame(vec3(0, -1.6f, 1.98f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), vec3(0.05f, 0.05f, 0.04f), 10, 5);
    ellipsoid(m, Frame(vec3(0, -0.3f, 0.55f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), vec3(0.05f, 0.05f, 0.04f), 10, 5);
    m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));
    roundedBoxAt(m, vec3(-1.07f, -5.3f, 1.78f), vec3(0.02f, 0.04f, 0.02f), 0.01f, 1);
    m.use(MAT_EMISSIVE, col(0.1f, 1.f, 0.3f, 0.25f));
    roundedBoxAt(m, vec3(1.07f, -5.3f, 1.78f), vec3(0.02f, 0.04f, 0.02f), 0.01f, 1);
    m.use(MAT_LIGHT_HEAD, kCol1);
    disk(m, vec3(0, 1.75f, 0.83f), normalize(vec3(0, 1, -0.4f)), 0.06f, 12);
    if (police) {
        // searchlight (right) and FLIR ball (left) under the nose
        m.use(MAT_METAL_PAINTED, col(0.15f, 0.15f, 0.15f));
        cyl(m, vec3(0.45f, 1.2f, 0.62f), vec3(0.45f, 1.2f, 0.45f), 0.03f, 8);
        std::vector<vec2> sl;
        sl.push_back(vec2(-0.14f, 0.05f)); sl.push_back(vec2(-0.08f, 0.13f)); sl.push_back(vec2(0.1f, 0.14f)); sl.push_back(vec2(0.12f, 0.12f));
        lathe(m, vec3(0.45f, 1.25f, 0.36f), vec3(0, 1, 0), vec3(1, 0, 0), sl, 16);
        m.use(MAT_LIGHT_HEAD, kCol1);
        disk(m, vec3(0.45f, 1.37f, 0.36f), vec3(0, 1, 0), 0.125f, 16);
        m.use(MAT_METAL_PAINTED, col(0.55f, 0.55f, 0.57f));
        cyl(m, vec3(-0.45f, 1.25f, 0.62f), vec3(-0.45f, 1.25f, 0.5f), 0.03f, 8);
        ellipsoid(m, Frame(vec3(-0.45f, 1.25f, 0.4f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), vec3(0.14f), 16, 8);
        m.use(MAT_CAR_GLASS, kCol1);
        ellipsoid(m, Frame(vec3(-0.45f, 1.36f, 0.4f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0)), vec3(0.07f, 0.07f, 0.04f), 12, 4, 0.f, kHalfPi);
        // loudspeaker + lettering on the boom
        m.use(MAT_METAL_PAINTED, col(0.2f, 0.2f, 0.2f));
        roundedBoxAt(m, vec3(0.8f, -0.4f, 0.55f), vec3(0.08f, 0.2f, 0.06f), 0.03f, 1);
        for (int side = 0; side < 2; side++) {
            float sx = side == 0 ? 1.f : -1.f;
            vec3 right = side == 0 ? vec3(0, 1, 0) : vec3(0, -1, 0);
            flatText(m, vec3(sx * 0.2f, -3.6f, 1.66f), right, vec3(0, 0, 1), "POLICE", 0.19f, MAT_METAL_PAINTED, col(0.95f, 0.95f, 0.95f));
        }
        m.use(MAT_LIGHT_INDICATOR, col(0.05f, 0.2f, 1.f, 0.f));
        roundedBoxAt(m, vec3(0.3f, -0.9f, 0.58f), vec3(0.04f, 0.06f, 0.03f), 0.02f, 1);
        m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.02f, 0.02f, 0.f));
        roundedBoxAt(m, vec3(-0.3f, -0.9f, 0.58f), vec3(0.04f, 0.06f, 0.03f), 0.02f, 1);
    } else {
        for (int side = 0; side < 2; side++) {
            float sx = side == 0 ? 1.f : -1.f;
            vec3 right = side == 0 ? vec3(0, 1, 0) : vec3(0, -1, 0);
            flatText(m, vec3(sx * 0.19f, -3.8f, 1.66f), right, vec3(0, 0, 1), "PS-KT4", 0.16f, MAT_METAL_PAINTED, col(0.1f, 0.1f, 0.12f));
        }
    }
    finalizeMesh(m, o.body);
    // main rotor: 4 blades + hub, spin axis +Z (centred at the hub)
    {
        PMesh rm;
        rm.newGroup(40.f);
        std::vector<vec2> bp = airfoil(0.11f, 0.01f, 6);
        for (int b = 0; b < 4; b++) {
            float a = kTwoPi * b / 4.f;
            vec3 rd(cosf(a), sinf(a), 0), tg(-sinf(a), cosf(a), 0);
            std::vector<FoilSt> st;
            float rr[3] = {0.35f, 4.95f, 5.35f};
            for (int k = 0; k < 3; k++) {
                FoilSt f;
                f.chordDir = -tg;
                f.up = vec3(0, 0, 1);
                f.le = rd * rr[k] + tg * 0.08f - vec3(0, 0, rr[k] * 0.004f);
                f.chord = 0.28f;
                st.push_back(f);
            }
            rm.use(MAT_METAL_PAINTED, col(0.16f, 0.16f, 0.17f));
            std::vector<FoilSt> inner(st.begin(), st.begin() + 2);
            foilLoft(rm, inner, bp, true, false);
            rm.use(MAT_METAL_PAINTED, col(0.95f, 0.8f, 0.1f));
            std::vector<FoilSt> tip(st.begin() + 1, st.end());
            foilLoft(rm, tip, bp, false, true);
            rm.use(MAT_METAL_BRUSHED, col(0.6f, 0.6f, 0.62f));
            roundedBox(rm, Frame(rd * 0.28f, rd, tg, vec3(0, 0, 1)), vec3(0.2f, 0.06f, 0.04f), 0.02f, 1);
        }
        rm.use(MAT_METAL_BRUSHED, col(0.55f, 0.55f, 0.57f));
        std::vector<vec2> hub;
        hub.push_back(vec2(-0.12f, 0.f)); hub.push_back(vec2(-0.12f, 0.16f)); hub.push_back(vec2(0.08f, 0.16f)); hub.push_back(vec2(0.14f, 0.07f)); hub.push_back(vec2(0.15f, 0.f));
        lathe(rm, vec3(0, 0, 0), vec3(0, 0, 1), vec3(1, 0, 0), hub, 14);
        finalizeMesh(rm, o.rotor);
    }
    o.rotorPos = vec3(0, -0.15f, 2.95f);
    o.rotorRadius = 5.35f;
    // tail rotor: 2 blades in the y-z plane, spin axis +X
    {
        PMesh tm;
        tm.newGroup(40.f);
        std::vector<vec2> bp = airfoil(0.10f, 0.f, 5);
        for (int b = 0; b < 2; b++) {
            float s = b == 0 ? 1.f : -1.f;
            std::vector<FoilSt> st;
            float rr[2] = {0.06f, 0.8f};
            for (int k = 0; k < 2; k++) {
                FoilSt f;
                f.chordDir = vec3(0, -s, 0);
                f.up = vec3(1, 0, 0);
                f.le = vec3(0, s * 0.06f, s * rr[k]);
                f.chord = 0.14f;
                st.push_back(f);
            }
            tm.use(MAT_METAL_PAINTED, col(0.9f, 0.9f, 0.9f));
            foilLoft(tm, st, bp, true, true);
        }
        tm.use(MAT_METAL_BRUSHED, col(0.5f, 0.5f, 0.52f));
        std::vector<vec2> hub;
        hub.push_back(vec2(-0.04f, 0.f)); hub.push_back(vec2(-0.04f, 0.06f)); hub.push_back(vec2(0.05f, 0.06f)); hub.push_back(vec2(0.07f, 0.f));
        lathe(tm, vec3(0, 0, 0), vec3(-1, 0, 0), vec3(0, 1, 0), hub, 10);
        finalizeMesh(tm, o.tailRotor);
    }
    o.tailRotorPos = vec3(-0.22f, -6.9f, 2.28f);
    o.seats.push_back(SeatSpec{vec3(0.38f, 0.65f, 1.02f), true, false});
    o.seats.push_back(SeatSpec{vec3(-0.38f, 0.65f, 1.02f), false, true});
    o.seats.push_back(SeatSpec{vec3(-0.35f, -0.55f, 1.02f), false, true});
    o.seats.push_back(SeatSpec{vec3(0.35f, -0.55f, 1.02f), false, false});
    addLight(o, vec3(0, 1.76f, 0.82f), vec3(0, 1, -0.4f), LT_HEAD);
    addLight(o, vec3(0, -1.6f, 2.02f), vec3(0, 0, 1), LT_BEACON);
    addLight(o, vec3(0, -0.3f, 0.5f), vec3(0, 0, -1), LT_BEACON);
    if (police) {
        addLight(o, vec3(0.45f, 1.38f, 0.36f), vec3(0, 0.3f, -1), LT_HEAD);
        addLight(o, vec3(-0.3f, -0.9f, 0.52f), vec3(0, 0, -1), LT_SIREN_RED);
        addLight(o, vec3(0.3f, -0.9f, 0.52f), vec3(0, 0, -1), LT_SIREN_BLUE);
    }
    // skids to cabin roof, nose to tail rotor; the box bottom is the skid contact plane
    o.boxCenter = vec3(0, -2.4f, 1.16f);
    o.boxHalf = vec3(1.12f, 4.7f, 1.14f);
    physics(o, police ? 1420.f : 1350.f, 485.f, 1100.f, 6000.f, 64.f, 1, 0.f, 0.8f, 0.10f, 1.4f, 0.45f, 0.f, vec3(0, 0.05f, 1.25f), Audio::ENGINE_TRUCK_DIESEL);
    o.frontalArea = 2.3f;
}
inline void mdlKite(VehicleModel& o) {
    o.name = "Kite 4"; o.maker = "Vesper Rotorcraft"; o.cls = VC_HELI;
    heliBuild(o, false);
    o.paletteColors = palette("air");
    o.spawnWeight = 0.2f; o.price = 1250000;
}
inline void mdlKitePolice(VehicleModel& o) {
    o.name = "Kite 4 Patrol"; o.maker = "Vesper Rotorcraft"; o.cls = VC_HELI;
    heliBuild(o, true);
    o.fixedLivery = true;
    o.liveryPrimary = srgb(16, 26, 58);
    o.liverySecondary = srgb(240, 240, 238);
    o.paletteColors.push_back(o.liveryPrimary);
    o.spawnWeight = 0.f; o.price = 0;
    o.sirenMode = -1;
}

}  // namespace detail
}  // namespace Vehicles
