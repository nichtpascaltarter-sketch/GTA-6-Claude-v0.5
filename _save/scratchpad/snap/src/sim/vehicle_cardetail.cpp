// Car details placed on the lofted body through surface projection: lamps, grilles, intakes, plates,
// panel seams, handles, mirrors, wipers, exhausts, interior and optional extras.
namespace Vehicles {
namespace detail {

enum HeadStyle : u8 { HL_PROJ = 0, HL_SLIM, HL_ROUND, HL_QUAD, HL_RECT, HL_POP };
enum TailStyle : u8 { TL_WRAP = 0, TL_BAR, TL_ROUND, TL_VERT, TL_SLIM, TL_CLASSIC };
enum GrilleStyle : u8 { GR_HBAR = 0, GR_MESH, GR_VSLAT, GR_NONE, GR_TRUCK, GR_SPLIT, GR_CLASSIC };
enum SpoilerKind : u8 { SP_NONE = 0, SP_LIP, SP_WING, SP_DUCK, SP_ROOF };
enum RoofExtra : u8 { RX_NONE = 0, RX_RAILS, RX_POLICE, RX_TAXI, RX_RACK };

struct CarLook {
    // headlights (right side, front view coordinates x,z of the centre; half extents in the lamp plane)
    HeadStyle head = HL_PROJ;
    vec2 headC = vec2(0.62f, 0.70f);
    float headW = 0.19f, headH = 0.065f, headYaw = 0.55f, headPitch = 0.0f;
    float headSlant = 0.35f;   // upper inner corner drop (fraction of height)
    float headTaper = 0.25f;   // outer end height reduction
    int headDomes = 2;
    bool drl = true;
    // taillights
    TailStyle tail = TL_WRAP;
    vec2 tailC = vec2(0.66f, 0.88f);
    float tailW = 0.20f, tailH = 0.07f, tailYaw = 0.6f;
    bool tailAmber = false;
    // grille (full width, centred)
    GrilleStyle grille = GR_HBAR;
    float grilleW = 0.36f, grilleTop = 0.68f, grilleBot = 0.54f, grilleTaper = 0.06f;
    bool grilleChrome = true;
    int grilleBars = 4;
    // lower intake
    float intakeW = 0.46f, intakeTop = 0.40f, intakeBot = 0.28f;
    bool fogs = true;
    // plates
    float plateFZ = 0.46f, plateRZ = 0.64f;
    bool frontPlate = true;
    // exhaust
    int exhaust = 1;           // 0 none, 1 single (left), 2 dual, 4 quad, 3 centre
    float exhaustX = 0.50f, exhaustR = 0.035f, exhaustZ = 0.0f;
    // side details
    bool mirrorsBlack = false, handlesChrome = false, chromeBelt = false, sideMarkers = true;
    bool rockerSkirt = false, sideIntake = false, fenderVent = false;
    // extras
    SpoilerKind spoiler = SP_NONE;
    bool hoodScoop = false, hoodVents = false, antennaFin = true, diffuser = false, bullBar = false, spotLamp = false;
    RoofExtra roof = RX_NONE;
    bool bedRails = false, towHitch = false, mudFlaps = false, spareWheel = false;
    bool chromeBumpers = false, blackBumpers = false;
    float bumperFZ = 0.f, bumperRZ = 0.f;
    // interior
    vec3 seatTint = vec3(0.14f, 0.14f, 0.15f);
    bool leather = false;
    vec3 badgeTint = vec3(0.8f, 0.8f, 0.82f);
    int badgeShape = 0;
    u8 maker = 0;          // MakerId: original maker emblem on the grille / nose / tail (0 = generic badge)
    float logoR = 0.034f;  // emblem radius
    vec3 plateBand = vec3(0.05f, 0.45f, 0.5f);
};

struct Samp {
    vec3 p, n;
    u8 ok;
};
inline void strokeText3D(PMesh& m, vec3 origin, vec3 right, vec3 up, const char* text, float h, float depth);
inline void buildLogo(PMesh& m, const Frame& F, float r, u8 maker);
inline Frame surfaceFrame(vec3 p, vec3 n, float lift);
enum FaceMode { FM_NORMAL = 0, FM_OUT, FM_IN };

inline void sampleLoop(const Decal& dc, const std::vector<vec2>& loop, std::vector<Samp>& out) {
    out.resize(loop.size());
    for (size_t i = 0; i < loop.size(); i++) out[i].ok = dc.at(loop[i], out[i].p, out[i].n) ? 1 : 0;
}
inline void loopBand(PMesh& m, const std::vector<Samp>& A, float oa, const std::vector<Samp>& B, float ob, int mode, vec3 ctr, bool closed = true) {
    int n = (int)A.size();
    int cnt = closed ? n : n - 1;
    for (int i = 0; i < cnt; i++) {
        int j = (i + 1) % n;
        if (!(A[i].ok && A[j].ok && B[i].ok && B[j].ok)) continue;
        vec3 pa0 = A[i].p + A[i].n * oa, pa1 = A[j].p + A[j].n * oa, pb1 = B[j].p + B[j].n * ob, pb0 = B[i].p + B[i].n * ob;
        u32 a0 = m.add(pa0), a1 = m.add(pa1), b1 = m.add(pb1), b0 = m.add(pb0);
        vec3 nrm = normalize(A[i].n + A[j].n + B[i].n + B[j].n);
        vec3 f = nrm;
        if (mode != FM_NORMAL) {
            vec3 mid = (pa0 + pa1 + pb0 + pb1) * 0.25f;
            vec3 o = mid - ctr;
            o -= nrm * dot(o, nrm);
            f = mode == FM_OUT ? o : -o;
        }
        m.quadFacing(a0, a1, b1, b0, f);
    }
}
// Fill a closed loop with concentric rings (surface following); dome raises the centre.
inline void loopFill(PMesh& m, const Decal& dc, const std::vector<vec2>& loop, float off, int rings, float dome = 0.f) {
    vec2 c = centroid(loop);
    int n = (int)loop.size();
    std::vector<std::vector<Samp>> R(rings);
    for (int r = 0; r < rings; r++) {
        float s = (float)(r + 1) / rings;
        std::vector<vec2> l(n);
        for (int i = 0; i < n; i++) l[i] = c + (loop[i] - c) * s;
        sampleLoop(dc, l, R[r]);
    }
    Samp cs;
    cs.ok = dc.at(c, cs.p, cs.n) ? 1 : 0;
    auto offAt = [&](int r) { float s = (float)(r + 1) / rings; return off + dome * (1.f - s * s); };
    if (cs.ok) {
        u32 ci = m.add(cs.p + cs.n * (off + dome));
        std::vector<u32> id(n);
        for (int i = 0; i < n; i++) id[i] = m.add(R[0][i].p + R[0][i].n * offAt(0));
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            if (!R[0][i].ok || !R[0][j].ok) continue;
            vec3 fn = cross(m.P[id[i]] - m.P[ci], m.P[id[j]] - m.P[ci]);
            if (dot(fn, cs.n) >= 0.f) m.tri(ci, id[i], id[j]);
            else m.tri(ci, id[j], id[i]);
        }
    }
    for (int r = 0; r + 1 < rings; r++) loopBand(m, R[r], offAt(r), R[r + 1], offAt(r + 1), FM_NORMAL, cs.p);
}

// Frames for projections. Plane axes (x,y) and projection direction z (into the body).
inline Frame projFront(float yaw = 0.f, float pitch = 0.f) {  // yaw > 0 turns the projection to hit the right (+x) corner
    vec3 d = normalize(vec3(-sinf(yaw), -cosf(yaw), -sinf(pitch)));
    vec3 u = normalize(cross(vec3(0, 0, 1), d));
    vec3 v = cross(d, u);
    return Frame(vec3(0, 0, 0), u, v, d);
}
inline Frame projRear(float yaw = 0.f, float pitch = 0.f) {  // yaw > 0 hits the right rear corner
    vec3 d = normalize(vec3(-sinf(yaw), cosf(yaw), -sinf(pitch)));
    vec3 u = normalize(cross(d, vec3(0, 0, 1)));
    vec3 v = cross(u, d);
    return Frame(vec3(0, 0, 0), u, v, d);
}
inline Frame projRight() { return Frame(vec3(0, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(-1, 0, 0)); }
inline Frame projTop() { return Frame(vec3(0, 0, 0), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, -1)); }

// Decal whose plane passes through the surface point found by probing from `guess` along `probeDir`;
// the decal keeps the axes/projection direction of `fr`.
inline bool decalAt(Decal& dc, Projector& pr, Frame fr, vec3 guess, vec3 probeDir) {
    Frame pf;
    pf.z = normalize(probeDir);
    pf.x = normalize(anyPerp(pf.z));
    pf.y = cross(pf.z, pf.x);
    pf.o = guess;
    Decal probe;
    probe.back = 4.f;
    probe.begin(pr, pf, vec2(-0.02f, -0.02f), vec2(0.02f, 0.02f));
    vec3 p, n;
    if (!probe.at(vec2(0, 0), p, n)) return false;
    fr.o = p;
    dc.fr = fr;
    dc.pr = &pr;
    dc.back = 0.6f;
    return true;
}
inline void decalRange(Decal& dc, vec2 mn, vec2 mx) { dc.pr->begin(dc.fr, mn - vec2(0.06f, 0.06f), mx + vec2(0.06f, 0.06f)); }

// Rectangular bar swept along points with per-point normals (thin raised slats)
inline void surfBar(PMesh& m, const std::vector<vec3>& P, const std::vector<vec3>& N, float w, float h, bool caps = true) {
    int n = (int)P.size();
    if (n < 2) return;
    std::vector<u32> ring(n * 4);
    for (int i = 0; i < n; i++) {
        vec3 t = normalize(P[Min(i + 1, n - 1)] - P[Max(i - 1, 0)]);
        vec3 nn = normalize(N[i] - t * dot(N[i], t));
        vec3 b = cross(t, nn);
        vec3 c = P[i];
        ring[i * 4 + 0] = m.add(c + b * (w * 0.5f));
        ring[i * 4 + 1] = m.add(c + b * (w * 0.5f) + nn * h);
        ring[i * 4 + 2] = m.add(c - b * (w * 0.5f) + nn * h);
        ring[i * 4 + 3] = m.add(c - b * (w * 0.5f));
    }
    for (int i = 0; i + 1 < n; i++)
        for (int k = 0; k < 3; k++) {
            int k1 = k + 1;
            u32 a = ring[i * 4 + k], b = ring[(i + 1) * 4 + k], c = ring[(i + 1) * 4 + k1], d = ring[i * 4 + k1];
            vec3 ctr = (P[i] + P[i + 1]) * 0.5f;
            vec3 mid = (m.P[a] + m.P[b] + m.P[c] + m.P[d]) * 0.25f;
            m.quadFacing(a, b, c, d, mid - ctr + normalize(N[i]) * 0.0001f);
        }
    if (caps) {
        for (int e = 0; e < 2; e++) {
            int i = e == 0 ? 0 : n - 1;
            vec3 dir = e == 0 ? P[0] - P[1] : P[n - 1] - P[n - 2];
            m.quadFacing(ring[i * 4], ring[i * 4 + 1], ring[i * 4 + 2], ring[i * 4 + 3], dir);
        }
    }
}
// Bar along a decal-plane polyline, projected, raised by `off` then height h
inline void decalBar(PMesh& m, const Decal& dc, const std::vector<vec2>& line, float w, float h, float off, float step = 0.03f) {
    std::vector<vec2> pts;
    for (size_t i = 0; i + 1 < line.size(); i++) {
        float l = length(line[i + 1] - line[i]);
        int n = Max(1, (int)ceilf(l / step));
        for (int k = 0; k < n; k++) pts.push_back(lerp(line[i], line[i + 1], (float)k / n));
    }
    pts.push_back(line.back());
    std::vector<vec3> P, N;
    for (auto& q : pts) {
        vec3 p, n;
        if (!dc.at(q, p, n)) continue;
        P.push_back(p + n * off);
        N.push_back(n);
    }
    surfBar(m, P, N, w, h);
}

// Oriented rounded box placed on the surface: centre point p, surface normal n, along-direction t
inline void surfBox(PMesh& m, vec3 p, vec3 n, vec3 t, vec3 half, float r, int rs = 2) {
    vec3 z = normalize(n);
    vec3 y = normalize(t - z * dot(t, z));
    vec3 x = cross(y, z);
    roundedBox(m, Frame(p, x, y, z), vec3(half.x, half.y, half.z), r, rs);
}

// ------------------------------------------------------------------------------------------------
// Lamp housing: outer wall, bezel ring, inner wall, reflector floor. Returns the inner outline.
struct LampStyle {
    float height = 0.009f, bezel = 0.006f, floorOff = 0.004f;
    u8 wallMat = MAT_CARPAINT, bezelMat = MAT_PLASTIC, floorMat = MAT_CAR_GLASS;
    u32 wallCol = kCol1, bezelCol = 0xff404040u, floorCol = kCol1;
    int rings = 2;
};
inline std::vector<vec2> lampHousing(PMesh& m, const Decal& dc, const std::vector<vec2>& O, const LampStyle& st) {
    std::vector<vec2> I = insetClosed(O, st.bezel);
    std::vector<Samp> sO, sI;
    sampleLoop(dc, O, sO);
    sampleLoop(dc, I, sI);
    vec3 cp, cn;
    dc.at(centroid(O), cp, cn);
    m.newGroup(40.f);
    m.use(st.wallMat, st.wallCol);
    loopBand(m, sO, -0.004f, sO, st.height, FM_OUT, cp);
    m.use(st.bezelMat, st.bezelCol);
    loopBand(m, sO, st.height, sI, st.height, FM_NORMAL, cp);
    m.use(MAT_PLASTIC, col(0.35f, 0.35f, 0.35f));
    loopBand(m, sI, st.height, sI, st.floorOff, FM_IN, cp);
    m.newGroup(50.f);
    m.use(st.floorMat, st.floorCol);
    loopFill(m, dc, I, st.floorOff, st.rings);
    return I;
}
// hemisphere lamp element on the surface
inline void lampDome(PMesh& m, const Decal& dc, vec2 at, float r, float off, u8 mat, u32 c, float depth = 1.f) {
    vec3 p, n;
    if (!dc.at(at, p, n)) return;
    vec3 x = normalize(anyPerp(n)), y = cross(n, x);
    m.newGroup(60.f);
    m.use(MAT_CHROME, kCol1);
    torus(m, Frame(p + n * (off + 0.001f), x, y, n), r * 1.25f, r * 0.28f, 12, 4);
    m.use(mat, c);
    ellipsoid(m, Frame(p + n * off, x, y, n), vec3(r, r, r * 0.6f * depth), 12, 3, 0.f, kHalfPi);
}

// Headlight (right side). The caller mirrors it.
inline void buildHeadlight(PMesh& m, CarBody& b, const CarLook& L) {
    Frame fr = projFront(L.headYaw, L.headPitch);
    Decal dc;
    vec3 guess(L.headC.x, b.yF + 0.5f, L.headC.y);
    fr.o = guess;
    if (!decalAt(dc, b.proj, fr, guess, vec3(0, -1, 0))) return;
    float hw = L.headW, hh = L.headH;
    std::vector<vec2> O;
    if (L.head == HL_ROUND || L.head == HL_POP) {
        O = shapeEllipse(vec2(0, 0), hh, hh, 24);
    } else if (L.head == HL_QUAD) {
        O = shapeEllipse(vec2(-hw * 0.45f, 0), hh * 0.95f, hh * 0.95f, 24);
    } else {
        std::vector<vec2> c;
        // inner end at -hw (towards the grille), outer end at +hw
        c.push_back(vec2(-hw, -hh));
        c.push_back(vec2(hw, -hh + hh * 2.f * L.headTaper * 0.4f));
        c.push_back(vec2(hw * 0.92f, hh - hh * 2.f * L.headTaper * 0.3f));
        c.push_back(vec2(-hw, hh - hh * 2.f * L.headSlant * 0.5f));
        float rr = Min(hh * 0.7f, 0.035f);
        if (L.head == HL_RECT) rr = 0.015f;
        O = resampleClosed(shapeRounded(c, rr, 4), 32);
    }
    decalRange(dc, vec2(-hw - 0.05f, -hh - 0.05f), vec2(hw + 0.05f, hh + 0.05f));
    LampStyle st;
    st.height = 0.010f;
    if (L.head == HL_ROUND || L.head == HL_QUAD || L.head == HL_RECT || L.head == HL_POP) {
        st.bezelMat = MAT_CHROME;
        st.bezelCol = kCol1;
        st.height = 0.012f;
        st.bezel = 0.012f;
        st.floorMat = MAT_CHROME;
    }
    std::vector<vec2> I = lampHousing(m, dc, O, st);
    if (L.head == HL_ROUND || L.head == HL_POP) {
        m.newGroup(50.f);
        m.use(MAT_LIGHT_HEAD, kCol1);
        loopFill(m, dc, insetClosed(I, 0.004f), st.floorOff + 0.004f, 4, 0.012f);
    } else if (L.head == HL_QUAD) {
        m.newGroup(50.f);
        m.use(MAT_LIGHT_HEAD, kCol1);
        loopFill(m, dc, insetClosed(I, 0.004f), st.floorOff + 0.004f, 3, 0.010f);
        std::vector<vec2> O2 = shapeEllipse(vec2(hw * 0.45f, 0), hh * 0.85f, hh * 0.85f, 24);
        std::vector<vec2> I2 = lampHousing(m, dc, O2, st);
        m.newGroup(50.f);
        m.use(MAT_LIGHT_HEAD, kCol1);
        loopFill(m, dc, insetClosed(I2, 0.004f), st.floorOff + 0.004f, 3, 0.010f);
    } else if (L.head == HL_RECT) {
        m.newGroup(50.f);
        m.use(MAT_LIGHT_HEAD, kCol1);
        loopFill(m, dc, insetClosed(I, 0.003f), st.floorOff + 0.004f, 3, 0.006f);
    } else {
        // projector domes + DRL strip
        int nd = L.head == HL_SLIM ? 1 : L.headDomes;
        float dr = Min(hh * 0.42f, 0.026f);
        if (L.head == HL_SLIM) dr = Min(hh * 0.5f, 0.018f);
        for (int k = 0; k < nd; k++) {
            float u = nd == 1 ? -hw * 0.15f : lerp(-hw * 0.45f, hw * 0.35f, (float)k / (nd - 1));
            lampDome(m, dc, vec2(u, -hh * 0.08f), dr, st.floorOff, MAT_LIGHT_HEAD, kCol1);
        }
        if (L.drl) {
            // DRL along the lower/upper edge of the inner outline
            std::vector<vec2> line;
            int n = (int)I.size();
            for (int i = 0; i < n; i++) {
                vec2 q = I[i];
                if (L.head == HL_SLIM ? (q.y > -hh * 0.2f) : (q.y > hh * 0.25f)) line.push_back(q);
            }
            // order the collected points by u
            std::sort(line.begin(), line.end(), [](vec2 a, vec2 b2) { return a.x < b2.x; });
            if (line.size() >= 2) {
                for (auto& q : line) q.y -= 0.009f;
                m.newGroup(50.f);
                m.use(MAT_LIGHT_HEAD, kCol1);
                decalBar(m, dc, line, 0.010f, 0.004f, st.floorOff, 0.02f);
            }
        }
        // amber side marker / indicator at the outer end
        std::vector<vec2> ind = shapeRoundRect(vec2(hw * 0.78f, -hh * 0.35f), hw * 0.12f, hh * 0.28f, 0.008f, 2);
        m.newGroup(50.f);
        m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.55f, 0.05f, 1.f));
        loopFill(m, dc, ind, st.floorOff + 0.002f, 2);
    }
}

// Taillight (right side)
inline void buildTaillight(PMesh& m, CarBody& b, const CarLook& L) {
    Frame fr = projRear(L.tailYaw, 0.f);
    Decal dc;
    vec3 guess(L.tailC.x, b.yR - 0.5f, L.tailC.y);
    if (!decalAt(dc, b.proj, fr, guess, vec3(0, 1, 0))) return;
    float hw = L.tailW, hh = L.tailH;
    std::vector<vec2> O;
    // plane u axis points to the car's +x at the rear (right)
    if (L.tail == TL_ROUND) {
        O = shapeEllipse(vec2(0, 0), hh, hh, 26);
    } else if (L.tail == TL_VERT) {
        O = resampleClosed(shapeRoundRect(vec2(0, 0), hw, hh, Min(hw, hh) * 0.35f, 3), 36);
    } else if (L.tail == TL_CLASSIC) {
        O = resampleClosed(shapeRoundRect(vec2(0, 0), hw, hh, 0.01f, 2), 36);
    } else {
        std::vector<vec2> c;
        c.push_back(vec2(-hw, -hh * 0.55f));
        c.push_back(vec2(hw * 0.9f, -hh));
        c.push_back(vec2(hw, hh));
        c.push_back(vec2(-hw, hh * 0.8f));
        O = resampleClosed(shapeRounded(c, Min(hh * 0.6f, 0.03f), 4), 32);
    }
    decalRange(dc, vec2(-hw - 0.05f, -hh - 0.05f), vec2(hw + 0.05f, hh + 0.05f));
    LampStyle st;
    st.height = 0.009f;
    st.bezel = 0.005f;
    st.floorMat = MAT_LIGHT_TAIL;
    st.floorCol = col(1.f, 0.f, 0.f);
    st.floorOff = 0.007f;
    std::vector<vec2> I = lampHousing(m, dc, O, st);
    // inner dark structure lines + reverse lamp + optional amber
    m.newGroup(50.f);
    vec2 c = centroid(I);
    if (L.tail == TL_ROUND) {
        m.use(MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
        std::vector<Samp> r1, r2;
        std::vector<vec2> a = shapeEllipse(vec2(0, 0), hh * 0.55f, hh * 0.55f, 20), bb = shapeEllipse(vec2(0, 0), hh * 0.47f, hh * 0.47f, 20);
        sampleLoop(dc, a, r1);
        sampleLoop(dc, bb, r2);
        vec3 cp, cn;
        dc.at(vec2(0, 0), cp, cn);
        loopBand(m, r1, st.floorOff + 0.002f, r2, st.floorOff + 0.002f, FM_NORMAL, cp);
    } else {
        m.use(MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
        std::vector<vec2> ln;
        ln.push_back(vec2(-hw * 0.85f, c.y + hh * 0.05f));
        ln.push_back(vec2(hw * 0.85f, c.y + hh * 0.05f));
        decalBar(m, dc, ln, 0.006f, 0.002f, st.floorOff, 0.03f);
        // reverse lamp (white when reversing: vertex green > 0.5)
        std::vector<vec2> rv = shapeRoundRect(vec2(-hw * 0.45f, c.y - hh * 0.35f), hw * 0.28f, hh * 0.25f, 0.008f, 2);
        m.use(MAT_LIGHT_TAIL, col(1.f, 1.f, 1.f));
        loopFill(m, dc, rv, st.floorOff + 0.002f, 2);
        if (L.tailAmber) {
            std::vector<vec2> am = shapeRoundRect(vec2(hw * 0.35f, c.y - hh * 0.35f), hw * 0.25f, hh * 0.22f, 0.008f, 2);
            m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.55f, 0.05f, 1.f));
            loopFill(m, dc, am, st.floorOff + 0.002f, 2);
        }
    }
}

// Centre light bar across the rear (TL_BAR): thin strip between the lamps
inline void buildRearBar(PMesh& m, CarBody& b, const CarLook& L) {
    Frame fr = projRear(0.f, 0.f);
    Decal dc;
    vec3 guess(0.f, b.yR - 0.5f, L.tailC.y + L.tailH * 0.5f);
    if (!decalAt(dc, b.proj, fr, guess, vec3(0, 1, 0))) return;
    float hw = L.tailC.x - L.tailW * 0.6f;
    std::vector<vec2> O = resampleClosed(shapeRoundRect(vec2(0, 0), hw, 0.012f, 0.008f, 2), 48);
    decalRange(dc, vec2(-hw - 0.05f, -0.05f), vec2(hw + 0.05f, 0.05f));
    LampStyle st;
    st.height = 0.008f;
    st.bezel = 0.004f;
    st.floorMat = MAT_LIGHT_TAIL;
    st.floorCol = col(1.f, 0.f, 0.f);
    st.floorOff = 0.006f;
    lampHousing(m, dc, O, st);
}

inline void buildGrille(PMesh& m, CarBody& b, const CarLook& L) {
    if (L.grille == GR_NONE) return;
    Frame fr = projFront(0.f, 0.f);
    Decal dc;
    float zc = (L.grilleTop + L.grilleBot) * 0.5f, hh = (L.grilleTop - L.grilleBot) * 0.5f;
    if (!decalAt(dc, b.proj, fr, vec3(0, b.yF + 0.5f, zc), vec3(0, -1, 0))) return;
    float hw = L.grilleW;
    std::vector<vec2> c;
    c.push_back(vec2(-hw + L.grilleTaper, -hh));
    c.push_back(vec2(hw - L.grilleTaper, -hh));
    c.push_back(vec2(hw, hh));
    c.push_back(vec2(-hw, hh));
    float rr = L.grille == GR_CLASSIC ? 0.01f : Min(hh * 0.5f, 0.04f);
    std::vector<vec2> O = resampleClosed(shapeRounded(c, rr, 4), 40);
    decalRange(dc, vec2(-hw - 0.05f, -hh - 0.05f), vec2(hw + 0.05f, hh + 0.05f));
    LampStyle st;
    st.height = L.grille == GR_TRUCK ? 0.03f : 0.014f;
    st.bezel = L.grille == GR_TRUCK ? 0.03f : 0.014f;
    st.rings = 2;
    st.wallMat = L.grilleChrome ? MAT_CHROME : MAT_PLASTIC;
    st.bezelMat = L.grilleChrome ? MAT_CHROME : MAT_CAR_GLASS;
    st.bezelCol = kCol1;
    st.floorMat = MAT_PLASTIC;
    st.floorCol = col(0.3f, 0.3f, 0.3f);
    st.floorOff = 0.003f;
    std::vector<vec2> I = lampHousing(m, dc, O, st);
    m.newGroup(40.f);
    u8 barMat = L.grilleChrome ? MAT_CHROME : MAT_PLASTIC;
    u32 barCol = L.grilleChrome ? kCol1 : col(0.8f, 0.8f, 0.8f);
    m.use(barMat, barCol);
    float ihw = hw - st.bezel, ihh = hh - st.bezel;
    if (L.grille == GR_HBAR || L.grille == GR_TRUCK || L.grille == GR_SPLIT || L.grille == GR_CLASSIC) {
        int nb = L.grilleBars;
        for (int k = 0; k < nb; k++) {
            float v = lerp(-ihh, ihh, (k + 0.5f) / nb);
            float t = (v + hh) / (2 * hh);
            float w = lerp(hw - L.grilleTaper, hw, t) - st.bezel - 0.004f;
            std::vector<vec2> ln;
            ln.push_back(vec2(-w, v));
            ln.push_back(vec2(w, v));
            float bw = L.grille == GR_TRUCK ? 0.03f : Min(ihh * 2.f / nb * 0.45f, 0.022f);
            decalBar(m, dc, ln, bw, 0.008f, st.floorOff, 0.04f);
        }
        if (L.grille == GR_TRUCK || L.grille == GR_CLASSIC) {
            int nv = L.grille == GR_TRUCK ? 5 : 9;
            for (int k = 0; k < nv; k++) {
                float u = lerp(-ihw * 0.9f, ihw * 0.9f, (k + 0.5f) / nv);
                std::vector<vec2> ln;
                ln.push_back(vec2(u, -ihh + 0.004f));
                ln.push_back(vec2(u, ihh - 0.004f));
                decalBar(m, dc, ln, L.grille == GR_TRUCK ? 0.02f : 0.008f, 0.006f, st.floorOff, 0.04f);
            }
        }
        if (L.grille == GR_SPLIT) {
            std::vector<vec2> ln;
            ln.push_back(vec2(0, -ihh));
            ln.push_back(vec2(0, ihh));
            m.use(MAT_CARPAINT, kCol1);
            decalBar(m, dc, ln, 0.05f, 0.012f, st.floorOff, 0.03f);
        }
    } else if (L.grille == GR_VSLAT) {
        int nv = Max(L.grilleBars, 7);
        for (int k = 0; k < nv; k++) {
            float u = lerp(-ihw * 0.92f, ihw * 0.92f, (k + 0.5f) / nv);
            std::vector<vec2> ln;
            ln.push_back(vec2(u, -ihh + 0.006f));
            ln.push_back(vec2(u, ihh - 0.006f));
            decalBar(m, dc, ln, 0.012f, 0.008f, st.floorOff, 0.03f);
        }
    } else if (L.grille == GR_MESH) {
        m.use(MAT_PLASTIC, col(0.9f, 0.9f, 0.9f));
        float cell = 0.045f;
        int nu = (int)(ihw * 2.f / cell) + 1;
        for (int k = -nu; k <= nu; k++) {
            float u0 = k * cell;
            for (int sgn = -1; sgn <= 1; sgn += 2) {
                std::vector<vec2> ln;
                // diagonal from bottom to top, clipped to the inner rectangle
                vec2 a(u0 - ihh * sgn, -ihh), bq(u0 + ihh * sgn, ihh);
                vec2 d = bq - a;
                float t0 = 0.f, t1 = 1.f;
                float lim = ihw - 0.01f;
                for (int ax = 0; ax < 1; ax++) {
                    if (fabsf(d.x) > 1e-6f) {
                        float ta = (-lim - a.x) / d.x, tb = (lim - a.x) / d.x;
                        if (ta > tb) std::swap(ta, tb);
                        t0 = Max(t0, ta);
                        t1 = Min(t1, tb);
                    }
                }
                if (t1 - t0 < 0.05f) continue;
                ln.push_back(a + d * t0);
                ln.push_back(a + d * t1);
                decalBar(m, dc, ln, 0.006f, 0.006f, st.floorOff, 0.03f);
            }
        }
    }
    // badge
    vec3 p, n;
    if (dc.at(vec2(0, L.grille == GR_SPLIT ? 0.f : ihh * 0.1f), p, n)) {
        m.newGroup(45.f);
        m.use(MAT_CHROME, colv(L.badgeTint));
        vec3 x = normalize(cross(vec3(0, 0, 1), -n)), y = cross(-n, x);
        if (length2(x) < 0.5f) x = vec3(1, 0, 0);
        Frame bf(p + n * (st.floorOff + 0.012f), x, vec3(0, 0, 1), n);
        if (L.maker != 0) {
            float lr = L.grille == GR_TRUCK ? Max(L.logoR, 0.075f) : L.logoR;
            buildLogo(m, surfaceFrame(p, n, st.floorOff + 0.009f), lr, L.maker);
        } else if (L.badgeShape == 1) {
            std::vector<vec2> dia;
            dia.push_back(vec2(0, -0.035f)); dia.push_back(vec2(0.045f, 0)); dia.push_back(vec2(0, 0.035f)); dia.push_back(vec2(-0.045f, 0));
            extrude(m, dia, bf, 0.f, 0.008f);
        } else if (L.badgeShape == 2) {
            std::vector<vec2> sh;
            sh.push_back(vec2(-0.03f, 0.03f)); sh.push_back(vec2(-0.03f, -0.005f)); sh.push_back(vec2(0, -0.04f));
            sh.push_back(vec2(0.03f, -0.005f)); sh.push_back(vec2(0.03f, 0.03f));
            extrude(m, sh, bf, 0.f, 0.008f);
        } else {
            torus(m, Frame(p + n * (st.floorOff + 0.014f), x, y, n), 0.03f, 0.006f, 16, 5);
            disk(m, p + n * (st.floorOff + 0.012f), n, 0.026f, 16);
        }
    }
}

inline void buildIntake(PMesh& m, CarBody& b, const CarLook& L) {
    if (L.intakeW <= 0.f) return;
    Frame fr = projFront(0.f, 0.f);
    Decal dc;
    float zc = (L.intakeTop + L.intakeBot) * 0.5f, hh = (L.intakeTop - L.intakeBot) * 0.5f;
    if (!decalAt(dc, b.proj, fr, vec3(0, b.yF + 0.5f, zc), vec3(0, -1, 0))) return;
    float hw = L.intakeW;
    std::vector<vec2> c;
    c.push_back(vec2(-hw * 0.92f, -hh));
    c.push_back(vec2(hw * 0.92f, -hh));
    c.push_back(vec2(hw, hh));
    c.push_back(vec2(-hw, hh));
    std::vector<vec2> O = resampleClosed(shapeRounded(c, Min(hh * 0.6f, 0.03f), 3), 36);
    decalRange(dc, vec2(-hw - 0.05f, -hh - 0.05f), vec2(hw + 0.05f, hh + 0.05f));
    LampStyle st;
    st.height = 0.006f;
    st.bezel = 0.008f;
    st.wallMat = MAT_PLASTIC;
    st.wallCol = col(0.5f, 0.5f, 0.5f);
    st.floorMat = MAT_PLASTIC;
    st.floorCol = col(0.25f, 0.25f, 0.25f);
    st.floorOff = 0.002f;
    std::vector<vec2> I = lampHousing(m, dc, O, st);
    m.newGroup(40.f);
    m.use(MAT_PLASTIC, col(0.7f, 0.7f, 0.7f));
    for (int k = 0; k < 2; k++) {
        float v = lerp(-hh, hh, (k + 1.f) / 3.f);
        std::vector<vec2> ln;
        ln.push_back(vec2(-hw * 0.85f, v));
        ln.push_back(vec2(hw * 0.85f, v));
        decalBar(m, dc, ln, 0.008f, 0.006f, st.floorOff, 0.04f);
    }
    // fog lamps at the outer ends of the bumper (right one here, mirrored with the lamp group by caller? no: both)
    if (L.fogs) {
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            Frame ff = projFront(0.35f * sgn, 0.f);
            Decal fd;
            float fx = sgn * (hw + 0.13f);
            if (!decalAt(fd, b.proj, ff, vec3(fx, b.yF + 0.5f, zc), vec3(0, -1, 0))) continue;
            std::vector<vec2> fo = shapeEllipse(vec2(0, 0), 0.035f, 0.035f, 16);
            decalRange(fd, vec2(-0.1f, -0.1f), vec2(0.1f, 0.1f));
            LampStyle fs;
            fs.height = 0.008f;
            fs.bezel = 0.008f;
            fs.wallMat = MAT_PLASTIC;
            fs.wallCol = col(0.5f, 0.5f, 0.5f);
            fs.bezelMat = MAT_CHROME;
            fs.bezelCol = kCol1;
            fs.floorMat = MAT_LIGHT_HEAD;
            fs.floorCol = kCol1;
            fs.floorOff = 0.004f;
            lampHousing(m, fd, fo, fs);
        }
    }
}

// Licence plate on the surface (front or rear)
inline void buildPlate(PMesh& m, CarBody& b, bool rear, float z, const CarLook& L) {
    Frame fr = rear ? projRear(0.f, 0.f) : projFront(0.f, 0.f);
    Decal dc;
    vec3 guess(0.f, rear ? b.yR - 0.5f : b.yF + 0.5f, z);
    if (!decalAt(dc, b.proj, fr, guess, vec3(0, rear ? 1.f : -1.f, 0))) return;
    decalRange(dc, vec2(-0.2f, -0.12f), vec2(0.2f, 0.12f));
    // flat plate: use the centre normal (with fallback), avoid penetrating the curved surface
    vec3 p0, n0;
    if (!dc.at(vec2(0, 0), p0, n0)) return;
    float maxPen = 0.f;
    for (int i = -1; i <= 1; i += 2)
        for (int j = -1; j <= 1; j += 2) {
            vec3 p, n;
            if (dc.at(vec2(i * 0.16f, j * 0.08f), p, n)) maxPen = Max(maxPen, dot(p - p0, n0));
        }
    vec3 nz = normalize(vec3(n0.x * 0.3f, n0.y, n0.z * 0.4f));
    vec3 x = normalize(cross(vec3(0, 0, 1), -nz));
    if (rear) x = -x;
    vec3 y = normalize(cross(nz, x));
    if (y.z < 0.f) y = -y;
    vec3 c = p0 + nz * (maxPen + 0.012f);
    m.newGroup(35.f);
    m.use(MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
    roundedBox(m, Frame(c - nz * 0.004f, x, y, nz), vec3(0.168f, 0.085f, 0.008f), 0.006f, 1);
    m.use(MAT_METAL_PAINTED, col(0.92f, 0.92f, 0.88f));
    roundedBox(m, Frame(c + nz * 0.005f, x, y, nz), vec3(0.153f, 0.076f, 0.0025f), 0.002f, 1);
    // banner and characters
    m.use(MAT_METAL_PAINTED, colv(L.plateBand));
    roundedBox(m, Frame(c + nz * 0.0078f + y * 0.056f, x, y, nz), vec3(0.14f, 0.012f, 0.0006f), 0.f, 1);
    m.use(MAT_METAL_PAINTED, col(0.05f, 0.08f, 0.2f));
    for (int k = 0; k < 7; k++) {
        if (k == 3) continue;
        float u = -0.105f + k * 0.035f;
        roundedBox(m, Frame(c + nz * 0.0078f + x * u - y * 0.008f, x, y, nz), vec3(0.011f, 0.03f, 0.0006f), 0.f, 1);
    }
}

// Panel seam: thin dark strip projected on the surface
inline void seam(PMesh& m, Projector& pr, Frame fr, const std::vector<vec2>& line, float w = 0.0045f) {
    Decal dc;
    dc.pr = &pr;
    dc.fr = fr;
    dc.back = 12.f;
    vec2 mn(1e9f, 1e9f), mx(-1e9f, -1e9f);
    for (auto& q : line) { mn = vmin(mn, q); mx = vmax(mx, q); }
    decalRange(dc, mn, mx);
    m.use(MAT_PLASTIC, col(0.35f, 0.35f, 0.35f));
    stripDecal(m, dc, line, w, 0.0025f, 0.04f);
}

// Door pull handle on the right side at (y, z)
inline void doorHandle(PMesh& m, CarBody& b, float y, float z, bool chrome) {
    Frame fr = projRight();
    fr.o = vec3(3.f, 0, 0);
    Decal dc;
    dc.pr = &b.proj;
    dc.fr = fr;
    dc.back = 0.f;
    decalRange(dc, vec2(y - 0.2f, z - 0.1f), vec2(y + 0.2f, z + 0.1f));
    vec3 p, n;
    if (!dc.at(vec2(y, z), p, n)) return;
    m.newGroup(40.f);
    m.use(MAT_PLASTIC, col(0.4f, 0.4f, 0.4f));
    std::vector<vec2> cup = shapeEllipse(vec2(y, z), 0.085f, 0.022f, 16);
    loopFill(m, dc, cup, 0.0015f, 2);
    m.use(chrome ? MAT_CHROME : MAT_CARPAINT, kCol1);
    surfBox(m, p + n * 0.012f, n, vec3(0, 1, 0), vec3(0.012f, 0.078f, 0.008f), 0.007f, 2);
}

// Side mirror on the right side
inline void sideMirror(PMesh& m, CarBody& b, bool black, float scale = 0.82f) {
    float ym = b.s.dloFront - 0.09f;
    float zb = b.beltZAt(ym);
    float xb = b.beltXAt(ym);
    vec3 base(xb + 0.01f, ym, zb + 0.03f);
    vec3 hc(xb + 0.15f * scale, ym - 0.02f, zb + 0.09f * scale);
    m.newGroup(45.f);
    m.use(black ? MAT_PLASTIC : MAT_CARPAINT, black ? col(0.6f, 0.6f, 0.6f) : kCol1);
    // arm
    roundedBox(m, Frame((base + hc) * 0.5f - vec3(0, 0.01f, 0.02f), normalize(hc - base), vec3(0, 1, 0), normalize(cross(normalize(hc - base), vec3(0, 1, 0)))),
               vec3(length(hc - base) * 0.5f, 0.035f, 0.012f), 0.008f, 1);
    // housing
    Frame hf(hc, vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1));
    roundedBox(m, hf, vec3(0.105f, 0.055f, 0.065f) * scale, 0.045f * scale, 2);
    // mirror glass facing -y
    m.newGroup(30.f);
    m.use(MAT_CHROME, col(0.8f, 0.85f, 0.9f));
    roundedBox(m, Frame(hc - vec3(0, 0.054f * scale, 0), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0)), vec3(0.092f, 0.052f, 0.003f) * scale, 0.02f * scale, 1);
    // indicator repeater
    m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.55f, 0.05f, 1.f));
    roundedBox(m, Frame(hc + vec3(0.03f, 0.052f * scale, -0.035f * scale), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0)), vec3(0.06f, 0.006f, 0.004f) * scale, 0.f, 1);
}

// Wipers on the windshield (both, not mirrored)
inline void wipers(PMesh& m, CarBody& b) {
    Frame fr = projTop();
    fr.o = vec3(0, 0, 4.f);
    Decal dc;
    dc.pr = &b.glassProj;
    dc.fr = fr;
    dc.back = 0.f;
    float yb = b.s.yCowl - 0.045f;
    decalRange(dc, vec2(-1.f, yb - 0.4f), vec2(1.f, yb + 0.1f));
    m.newGroup(40.f);
    m.use(MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
    float w = b.beltXAt(b.s.yCowl - 0.1f);
    for (int k = 0; k < 2; k++) {
        float x0 = k == 0 ? -w * 0.72f : w * 0.02f;
        float x1 = k == 0 ? w * 0.05f : w * 0.72f;
        std::vector<vec2> ln;
        ln.push_back(vec2(x0, yb));
        ln.push_back(vec2(x1, yb - 0.035f));
        decalBar(m, dc, ln, 0.014f, 0.012f, 0.004f, 0.05f);
    }
}

// Exhaust tip(s): pipes along +/-y under the rear bumper
inline void exhausts(PMesh& m, CarBody& b, const CarLook& L) {
    if (L.exhaust == 0) return;
    std::vector<float> xs;
    if (L.exhaust == 1) xs.push_back(-L.exhaustX);
    else if (L.exhaust == 2) { xs.push_back(-L.exhaustX); xs.push_back(L.exhaustX); }
    else if (L.exhaust == 3) { xs.push_back(-0.06f); xs.push_back(0.06f); }
    else { xs.push_back(-L.exhaustX - 0.05f); xs.push_back(-L.exhaustX + 0.05f); xs.push_back(L.exhaustX - 0.05f); xs.push_back(L.exhaustX + 0.05f); }
    float z = L.exhaustZ > 0.f ? L.exhaustZ : b.s.zRearLow + L.exhaustR + 0.01f;
    Frame fr = projRear();
    for (float x : xs) {
        Decal dc;
        vec3 p, n;
        float ys = b.yR + 0.1f;
        if (decalAt(dc, b.proj, fr, vec3(x, b.yR - 0.5f, z + L.exhaustR * 1.6f), vec3(0, 1, 0))) ys = dc.fr.o.y;
        ys = Min(ys, b.yR + 0.25f);
        m.newGroup(40.f);
        m.use(MAT_CHROME, kCol1);
        std::vector<vec2> prof;  // (a along -y, r)
        float r = L.exhaustR;
        prof.push_back(vec2(-0.25f, r * 0.8f));
        prof.push_back(vec2(0.0f, r));
        prof.push_back(vec2(0.045f, r * 1.05f));
        prof.push_back(vec2(0.05f, r * 0.9f));
        prof.push_back(vec2(0.02f, r * 0.85f));
        lathe(m, vec3(x, ys + 0.02f, z), vec3(0, -1, 0), vec3(1, 0, 0), prof, 14);
        m.use(MAT_PLASTIC, col(0.05f, 0.05f, 0.05f));
        disk(m, vec3(x, ys - 0.015f, z), vec3(0, -1, 0), r * 0.86f, 14);
    }
}

// Roof-mounted police light bar (red left / blue right), siren colour in vertex rgb
inline void policeBar(PMesh& m, CarBody& b, float y, float halfLen) {
    float z = b.roofZAt(y);
    m.newGroup(40.f);
    m.use(MAT_PLASTIC, col(0.6f, 0.6f, 0.6f));
    // feet
    for (int s = -1; s <= 1; s += 2) roundedBoxAt(m, vec3(s * halfLen * 0.8f, y, z + 0.02f), vec3(0.04f, 0.1f, 0.03f), 0.01f, 1);
    roundedBoxAt(m, vec3(0, y, z + 0.06f), vec3(halfLen, 0.14f, 0.025f), 0.02f, 2);
    // lens segments
    int segs = 6;
    for (int k = 0; k < segs; k++) {
        float x0 = -halfLen + (2.f * halfLen) * k / segs + 0.012f, x1 = -halfLen + (2.f * halfLen) * (k + 1) / segs - 0.012f;
        bool left = (x0 + x1) < 0.f;
        bool centre = k == segs / 2 - 1 || k == segs / 2;
        vec3 sc = left ? vec3(1.f, 0.02f, 0.02f) : vec3(0.05f, 0.2f, 1.f);
        m.use(MAT_LIGHT_INDICATOR, centre ? col(1.f, 1.f, 1.f, 0.f) : colv(sc, 0.f));
        roundedBoxAt(m, vec3((x0 + x1) * 0.5f, y, z + 0.115f), vec3((x1 - x0) * 0.5f, 0.12f, 0.035f), 0.03f, 2);
    }
    m.use(MAT_PLASTIC, col(0.3f, 0.3f, 0.3f));
    roundedBoxAt(m, vec3(0, y, z + 0.155f), vec3(halfLen + 0.01f, 0.125f, 0.008f), 0.006f, 1);
}

inline void taxiSign(PMesh& m, CarBody& b, float y) {
    float z = b.roofZAt(y);
    m.newGroup(40.f);
    m.use(MAT_PLASTIC, col(0.6f, 0.6f, 0.6f));
    roundedBoxAt(m, vec3(0, y, z + 0.02f), vec3(0.3f, 0.1f, 0.025f), 0.01f, 1);
    m.use(MAT_CARPAINT, kCol1);
    std::vector<vec2> prof;
    prof.push_back(vec2(-0.13f, 0.f));
    prof.push_back(vec2(0.13f, 0.f));
    prof.push_back(vec2(0.10f, 0.20f));
    prof.push_back(vec2(-0.10f, 0.20f));
    extrude(m, prof, Frame(vec3(0, y, z + 0.04f), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1, 0, 0)), -0.36f, 0.36f);
    // lit panels front/back: warm white emissive
    m.use(MAT_EMISSIVE, col(1.f, 0.92f, 0.7f, 0.35f));
    for (int s = -1; s <= 1; s += 2) {
        Frame pf(vec3(0, y + s * 0.118f, z + 0.14f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, (float)s, 0));
        std::vector<vec2> r = shapeRoundRect(vec2(0, 0), 0.3f, 0.055f, 0.01f, 2);
        extrude(m, r, Frame(pf.o, pf.x, pf.y, normalize(cross(pf.x, pf.y))), -0.002f, 0.004f, false, true);
    }
    // letters on both faces (dark), readable from each side
    m.use(MAT_PLASTIC, col(0.2f, 0.2f, 0.2f));
    strokeText3D(m, vec3(0, y + 0.12f, z + 0.14f), vec3(-1, 0, 0), vec3(0, 0, 1), "TAXI", 0.075f, 0.004f);
    strokeText3D(m, vec3(0, y - 0.12f, z + 0.14f), vec3(1, 0, 0), vec3(0, 0, 1), "TAXI", 0.075f, 0.004f);
}

inline void roofRails(PMesh& m, CarBody& b) {
    float y0 = b.s.yRoofF - 0.12f, y1 = b.s.yRoofR + 0.1f;
    m.newGroup(40.f);
    m.use(MAT_METAL_BRUSHED, col(0.35f, 0.35f, 0.37f));
    for (int s = 1; s >= -1; s -= 2) {
        std::vector<vec3> path;
        for (int k = 0; k <= 10; k++) {
            float y = lerp(y0, y1, k / 10.f);
            float x = b.railXAt(y) - 0.02f;
            float z = b.topZAt(y, x) + 0.055f;
            path.push_back(vec3(x * s, y, z));
        }
        tube1(m, path, 0.014f, 8, true);
        m.use(MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
        for (int k = 0; k < 3; k++) {
            float y = lerp(y0 + 0.03f, y1 - 0.03f, k / 2.f);
            float x = b.railXAt(y) - 0.02f;
            float z = b.topZAt(y, x);
            roundedBoxAt(m, vec3(x * s, y, z + 0.03f), vec3(0.018f, 0.05f, 0.03f), 0.01f, 1);
        }
        m.use(MAT_METAL_BRUSHED, col(0.35f, 0.35f, 0.37f));
    }
}

inline void antennaFin(PMesh& m, CarBody& b) {
    float y = b.s.yRoofR + 0.1f;
    float z = b.roofZAt(y);
    m.newGroup(35.f);
    m.use(MAT_CAR_GLASS, kCol1);
    std::vector<vec2> prof;
    prof.push_back(vec2(-0.09f, -0.01f));
    prof.push_back(vec2(0.07f, -0.01f));
    prof.push_back(vec2(0.02f, 0.055f));
    prof.push_back(vec2(-0.02f, 0.06f));
    extrude(m, prof, Frame(vec3(0, y, z), vec3(0, -1, 0), vec3(0, 0, 1), vec3(-1, 0, 0)), -0.022f, 0.022f);
}

inline void spoilerWing(PMesh& m, CarBody& b, const CarLook& L) {
    if (L.spoiler == SP_NONE || L.spoiler == SP_ROOF) return;
    float y = b.yTE + 0.02f;
    float zc = b.centreZ(y);
    float hw = b.planW(y) * 0.9f;
    m.newGroup(35.f);
    if (L.spoiler == SP_LIP || L.spoiler == SP_DUCK) {
        m.use(L.spoiler == SP_LIP ? MAT_CAR_GLASS : MAT_CARPAINT, kCol1);
        std::vector<vec3> P, N;
        Frame fr = projTop();
        fr.o = vec3(0, 0, 4.f);
        Decal dc;
        dc.pr = &b.proj;
        dc.fr = fr;
        dc.back = 0.f;
        decalRange(dc, vec2(-hw, y - 0.1f), vec2(hw, y + 0.1f));
        std::vector<vec2> ln;
        for (int k = 0; k <= 16; k++) ln.push_back(vec2(lerp(-hw * 0.95f, hw * 0.95f, k / 16.f), y));
        decalBar(m, dc, ln, L.spoiler == SP_DUCK ? 0.09f : 0.05f, L.spoiler == SP_DUCK ? 0.035f : 0.018f, -0.002f, 0.1f);
        return;
    }
    // wing on two stands
    hw *= 0.86f;
    float wz = zc + 0.17f, wy = b.yR + 0.16f;
    m.use(MAT_CAR_GLASS, kCol1);
    for (int s = -1; s <= 1; s += 2) {
        float x = s * hw * 0.6f;
        float zs = b.topZAt(wy + 0.08f, fabsf(x));
        std::vector<vec2> st;
        st.push_back(vec2(-0.05f, 0.f));
        st.push_back(vec2(0.07f, 0.f));
        st.push_back(vec2(0.04f, wz - zs));
        st.push_back(vec2(-0.03f, wz - zs));
        extrude(m, st, Frame(vec3(x, wy + 0.06f, zs - 0.01f), vec3(0, -1, 0), vec3(0, 0, 1), vec3(1, 0, 0)), -0.008f, 0.008f);
    }
    std::vector<vec2> foil;  // chord along -y (rearwards), thickness z
    int nf = 8;
    float chord = 0.22f;
    for (int i = 0; i <= nf; i++) {
        float t = (float)i / nf;
        float th = 0.12f * chord * (1.f - t) * sqrtf(t + 0.02f) * 2.f;
        foil.push_back(vec2(t * chord, th * 0.3f + 0.02f * t));
    }
    for (int i = nf - 1; i >= 1; i--) {
        float t = (float)i / nf;
        float th = 0.12f * chord * (1.f - t) * sqrtf(t + 0.02f) * 2.f;
        foil.push_back(vec2(t * chord, -th * 0.7f + 0.02f * t));
    }
    m.use(MAT_CARPAINT, kCol1);
    extrude(m, foil, Frame(vec3(0, wy + 0.12f, wz), vec3(0, -1, 0), vec3(0, 0, 1), vec3(1, 0, 0)), -hw * 0.98f, hw * 0.98f);
    m.use(MAT_CAR_GLASS, kCol1);
    for (int s = -1; s <= 1; s += 2) {
        std::vector<vec2> ep = shapeRoundRect(vec2(0.13f, 0.01f), 0.17f, 0.07f, 0.03f, 2);
        extrude(m, ep, Frame(vec3(s * hw * 0.99f, wy + 0.12f, wz), vec3(0, -1, 0), vec3(0, 0, 1), vec3(1, 0, 0)), -0.005f, 0.005f);
    }
}

inline void hoodScoop(PMesh& m, CarBody& b) {
    float y = (b.yHF + b.s.yCowl) * 0.5f;
    float z = b.centreZ(y);
    m.newGroup(35.f);
    m.use(MAT_CARPAINT, kCol1);
    std::vector<vec2> prof;  // side profile (y, z) of the scoop
    prof.push_back(vec2(-0.30f, -0.03f));
    prof.push_back(vec2(0.22f, -0.03f));
    prof.push_back(vec2(0.22f, 0.07f));
    prof.push_back(vec2(0.10f, 0.08f));
    prof.push_back(vec2(-0.30f, 0.01f));
    extrude(m, prof, Frame(vec3(0, y, z), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1, 0, 0)), -0.22f, 0.22f);
    m.use(MAT_PLASTIC, col(0.1f, 0.1f, 0.1f));
    roundedBoxAt(m, vec3(0, y + 0.223f, z + 0.025f), vec3(0.19f, 0.004f, 0.035f), 0.004f, 1);
}

inline void bullBar(PMesh& m, CarBody& b) {
    float yf = b.yF + 0.07f;
    m.newGroup(40.f);
    m.use(MAT_METAL_PAINTED, col(0.08f, 0.08f, 0.08f));
    float w = b.s.halfW * 0.55f;
    float z0 = b.s.zNoseBot - 0.05f, z1 = b.s.zHoodF - 0.03f;
    for (int s = -1; s <= 1; s += 2) {
        std::vector<vec3> p;
        p.push_back(vec3(s * w, yf - 0.12f, z0));
        p.push_back(vec3(s * w, yf, z0 + 0.05f));
        p.push_back(vec3(s * w, yf + 0.02f, z1 - 0.05f));
        p.push_back(vec3(s * w * 0.85f, yf - 0.02f, z1));
        tube1(m, catmull(p, 3), 0.025f, 8, true);
    }
    std::vector<vec3> cross1, cross2;
    cross1.push_back(vec3(-w, yf + 0.005f, z0 + 0.12f));
    cross1.push_back(vec3(w, yf + 0.005f, z0 + 0.12f));
    tube1(m, cross1, 0.02f, 8, true);
    cross2.push_back(vec3(-w * 0.85f, yf - 0.02f, z1));
    cross2.push_back(vec3(w * 0.85f, yf - 0.02f, z1));
    tube1(m, cross2, 0.022f, 8, true);
    m.use(MAT_RUBBER, kCol1);
    roundedBoxAt(m, vec3(0, yf + 0.03f, z0 + 0.22f), vec3(w * 0.8f, 0.03f, 0.1f), 0.02f, 1);
}

inline void spotLamp(PMesh& m, CarBody& b) {
    float y = b.s.yCowl - 0.1f;
    float x = -(b.beltXAt(y) + 0.07f);
    float z = b.beltZAt(y) + 0.05f;
    m.newGroup(40.f);
    m.use(MAT_CHROME, kCol1);
    cyl(m, vec3(x + 0.05f, y, z - 0.02f), vec3(x, y, z), 0.01f, 6);
    std::vector<vec2> prof;
    prof.push_back(vec2(-0.08f, 0.03f));
    prof.push_back(vec2(0.0f, 0.06f));
    prof.push_back(vec2(0.03f, 0.065f));
    prof.push_back(vec2(0.035f, 0.055f));
    lathe(m, vec3(x, y, z + 0.05f), vec3(0, 1, 0), vec3(1, 0, 0), prof, 14);
    m.use(MAT_LIGHT_HEAD, kCol1);
    disk(m, vec3(x, y + 0.033f, z + 0.05f), vec3(0, 1, 0), 0.056f, 14);
}

// ------------------------------------------------------------------------------------------------
// Interior: seats, dashboard, steering wheel (driver on the left), console, floor, door cards, headliner
struct InteriorLayout {
    float zFloor = 0.32f, yHipF = -0.05f, yHipR = -0.92f, hipH = 0.27f, seatX = 0.37f;
    bool rearSeat = true, bench = false;
    float dashY0 = 0.9f, dashY1 = 0.55f;
};

inline void seat(PMesh& m, vec3 hip, float halfW, const CarLook& L, bool bucket, float recline = 0.32f, float backScale = 1.f,
                 bool headrest = true) {
    u8 mat = L.leather ? MAT_LEATHER : MAT_FABRIC;
    u32 c = colv(L.leather ? vec3(1.f) : L.seatTint);
    m.newGroup(45.f);
    m.use(mat, c);
    // cushion
    Frame cf(hip + vec3(0, 0.2f, -0.07f), vec3(1, 0, 0), normalize(vec3(0, 1, 0.12f)), normalize(vec3(0, -0.12f, 1)));
    roundedBox(m, cf, vec3(halfW, 0.24f, 0.065f), 0.05f, 1);
    // backrest
    vec3 bdir = normalize(vec3(0, -sinf(recline), cosf(recline)));
    Frame bf(hip + vec3(0, -0.05f, 0.02f) + bdir * 0.32f, vec3(1, 0, 0), normalize(cross(bdir, vec3(1, 0, 0))) * -1.f, bdir);
    bf.y = normalize(cross(bf.z, bf.x));
    roundedBox(m, bf, vec3(halfW * (bucket ? 0.96f : 1.f), 0.07f, 0.31f * backScale), 0.055f, 1);
    if (bucket) {
        // side bolsters
        for (int s = -1; s <= 1; s += 2)
            roundedBox(m, Frame(bf.o + bf.x * (s * halfW * 0.92f) + bf.y * 0.05f, bf.x, bf.y, bf.z), vec3(0.045f, 0.06f, 0.26f * backScale), 0.035f, 1);
    }
    // headrest
    vec3 hp = bf.o + bdir * (0.31f * backScale + 0.09f) + bf.y * 0.02f;
    if (headrest) {
        if (bucket) roundedBox(m, Frame(hp, bf.x, bf.y, bf.z), vec3(0.13f, 0.05f, 0.09f), 0.04f, 1);
        else
            for (int s = -1; s <= 1; s += 2) roundedBox(m, Frame(hp + vec3(s * halfW * 0.6f, 0, 0), bf.x, bf.y, bf.z), vec3(0.12f, 0.05f, 0.07f), 0.03f, 1);
    }
}

inline void steeringWheel(PMesh& m, vec3 c, float tilt, float r = 0.185f) {
    vec3 ax = normalize(vec3(0, -cosf(tilt), sinf(tilt)));  // wheel axis towards the driver
    vec3 x(1, 0, 0);
    vec3 y = normalize(cross(ax, x));
    m.newGroup(50.f);
    m.use(MAT_LEATHER, col(0.35f, 0.35f, 0.35f));
    torus(m, Frame(c, x, y, ax), r, 0.016f, 20, 5);
    m.use(MAT_INTERIOR, kCol1);
    ellipsoid(m, Frame(c - ax * 0.01f, x, y, ax), vec3(0.07f, 0.06f, 0.04f), 12, 5);
    for (int k = 0; k < 3; k++) {
        float a = kPi * 0.5f + kTwoPi * k / 3.f + kPi;
        vec3 d = x * cosf(a) + y * sinf(a);
        cyl(m, c + d * 0.05f, c + d * (r - 0.01f) - ax * 0.01f, 0.012f, 6, false);
    }
    // column
    cyl(m, c - ax * 0.03f, c - ax * 0.38f, 0.03f, 8, true);
}

// Quad visible from both sides (cabin closing panels are seen from inside and, through the glass, from outside).
inline void quad2(PMesh& m, vec3 a, vec3 b, vec3 c, vec3 d, vec3 facing) {
    m.quadFacing(m.add(a), m.add(b), m.add(c), m.add(d), facing);
    m.quadFacing(m.add(a), m.add(b), m.add(c), m.add(d), -facing);
}

// Inner cabin skin seen through the see-through windows: pillar trims (inward copies of the non-glass greenhouse
// cells), cargo side walls / floor / tailgate lining for hatch-type bodies, and a bulkhead + parcel shelf for sedans
// and cabs, so no culled outer panel reveals the outside. Right half, mirrored.
inline void cabinTrim(PMesh& m, CarBody& b, const InteriorLayout& I, float y0, float y1, float zPan) {
    const CarSpec& s = b.s;
    if (s.openTop) return;
    PMesh::Mark mk = m.mark();
    int NC1 = b.NP - 1;
    // pillar / rail trims
    m.newGroup(38.f);
    m.use(MAT_FABRIC, col(0.55f, 0.55f, 0.53f));
    for (int i = 0; i + 1 < b.nr; i++) {
        if (b.rowL[i] < 0.01f || b.rowL[i + 1] < 0.01f) continue;
        for (int j = b.pGh0; j < b.pRail0; j++) {  // pillars; the door-card ledge meets the glass, the headliner lines the rail
            u8 cc = b.cls[i * NC1 + j];
            if (cc == CC_GLASS || cc == CC_HOLE || cc == CC_SKIP || cc == CC_INTERIOR || cc == CC_BED) continue;
            int ii[4] = {i, i + 1, i + 1, i}, jj[4] = {j, j, j + 1, j + 1};
            u32 v[4];
            vec3 nsum(0, 0, 0);
            for (int k = 0; k < 4; k++) {
                vec3 g = b.G[ii[k] * b.NP + jj[k]], n = b.GN[ii[k] * b.NP + jj[k]];
                v[k] = m.add(g - n * 0.012f);
                nsum += n;
            }
            m.quadFacing(v[0], v[1], v[2], v[3], -nsum);
        }
    }
    // lower cabin: side walls, floors and end panels
    m.newGroup(40.f);
    m.use(MAT_INTERIOR, kCol1);
    // wheel houses where a wheel well rises into the cabin floor (cab-overs, vans): boxes over the liners
    for (int a = 0; a < 2; a++) {
        if (!b.archOn(a)) continue;
        float yw = a == 0 ? b.yWf : b.yWr;
        float zTop = s.wheelR + b.Ra + 0.04f;
        float ya = Max(yw - b.Ra - 0.03f, y1), yz = Min(yw + b.Ra + 0.03f, y0 + 0.2f);
        if (zTop < I.zFloor + 0.03f || yz - ya < 0.1f) continue;
        float track = a == 0 ? s.trackF : s.trackR;
        float xa = track - s.wheelW * 0.5f - 0.07f, xb = b.beltXAt(yw) - 0.065f;
        if (xb - xa < 0.05f) continue;
        roundedBoxAt(m, vec3((xa + xb) * 0.5f, (ya + yz) * 0.5f, (I.zFloor + zTop) * 0.5f),
                     vec3((xb - xa) * 0.5f, (yz - ya) * 0.5f, (zTop - I.zFloor) * 0.5f), 0.03f, 1);
    }
    auto wallX = [&](float y) { return b.beltXAt(y) - 0.045f; };
    auto beltZ = [&](float y) { return b.beltZAt(y) - 0.01f; };
    bool cargo = s.style == BS_HATCH || s.style == BS_WAGON || s.style == BS_SUV || s.style == BS_VAN;
    if (cargo) {
        float yC = b.yR + 0.16f;
        if (yC < y1 - 0.05f) {
            int n = 8;
            std::vector<u32> lo(n + 1), hi(n + 1);
            for (int k = 0; k <= n; k++) {
                float y = lerp(yC, y1, k / (float)n);
                lo[k] = m.add(vec3(wallX(y) - 0.03f, y, Min(zPan, beltZ(y) - 0.05f)));
                hi[k] = m.add(vec3(wallX(y), y, beltZ(y)));
            }
            for (int k = 0; k < n; k++) m.quadFacing(lo[k], lo[k + 1], hi[k + 1], hi[k], vec3(-1, 0, 0));
            // cargo floor at the height of the rear axle pan
            float xa = wallX(yC) - 0.03f, xb = wallX(y1) - 0.03f;
            m.quadFacing(m.add(vec3(0, yC, zPan)), m.add(vec3(xa, yC, zPan)), m.add(vec3(xb, y1, zPan)), m.add(vec3(0, y1, zPan)), vec3(0, 0, 1));
            // tailgate lining up to the rear window
            quad2(m, vec3(0, yC, zPan), vec3(xa, yC, zPan), vec3(wallX(yC), yC, beltZ(yC)), vec3(0, yC, beltZ(yC)), vec3(0, 1, 0));
        }
    } else if (s.style == BS_BOXY) {
        // vans and truck cabs: full-height partition behind the last seat row (inside the shell), following the section
        float yb = Max(y1, b.yR + s.rearD * 0.6f + 0.04f);
        int ir = b.rowAt(yb);
        const vec3* sec = &b.G[ir * b.NP];
        auto secX = [&](float z) {
            for (int j = b.pSide0; j + 1 < b.NP; j++)
                if ((sec[j].z - z) * (sec[j + 1].z - z) <= 0.f && fabsf(sec[j + 1].z - sec[j].z) > 1e-5f)
                    return lerp(sec[j].x, sec[j + 1].x, (z - sec[j].z) / (sec[j + 1].z - sec[j].z));
            return sec[b.pGh0].x;
        };
        float zTop = sec[b.NP - 1].z - 0.014f;
        const int nl = 8;
        for (int k = 0; k < nl; k++) {
            float za = lerp(I.zFloor, zTop, k / (float)nl), zb = lerp(I.zFloor, zTop, (k + 1) / (float)nl);
            float xa = Max(secX(za) - 0.014f, 0.f), xb = Max(secX(zb) - 0.014f, 0.f);
            quad2(m, vec3(0, yb, za), vec3(xa, yb, za), vec3(xb, yb, zb), vec3(0, yb, zb), vec3(0, 1, 0));
        }
    } else {
        // bulkhead behind the last seat row, up to the belt (cabs keep the rear window clear above it)
        float yb = y1;
        float xw = wallX(yb) - 0.02f;
        quad2(m, vec3(0, yb, I.zFloor), vec3(xw, yb, I.zFloor), vec3(xw, yb, beltZ(yb)), vec3(0, yb, beltZ(yb)), vec3(0, 1, 0));
        if (s.style == BS_SEDAN || s.style == BS_COUPE) {
            // parcel shelf from the bulkhead top back to the lower edge of the rear window (closes the box under the glass)
            float yr = Max(s.yDeck + 0.01f, b.yR + 0.15f), zr = b.roofZAt(yr) - 0.014f;
            for (int i = 0; i + 1 < b.nr; i++) {
                if (b.rows[i] > s.yRoofR) break;
                if (b.cls[i * NC1 + NC1 - 1] == CC_GLASS) {
                    yr = b.rows[i];
                    zr = b.G[i * b.NP + b.NP - 1].z - 0.014f;
                    break;
                }
            }
            if (yr < yb - 0.05f) {
                // the shelf reaches out to the glass / C-pillar trim line so nothing shows beside it
                quad2(m, vec3(0, yr, zr), vec3(b.beltXAt(yr) - 0.012f, yr, zr), vec3(b.beltXAt(yb) - 0.012f, yb, beltZ(yb) + 0.006f),
                      vec3(0, yb, beltZ(yb) + 0.006f), vec3(0, 0, 1));
            }
        }
    }
    m.mirrorX(mk);
}

inline void buildInterior(PMesh& m, CarBody& b, const CarLook& L, const InteriorLayout& I) {
    // floor + door cards (+ mirrored)
    PMesh::Mark mk = m.mark();
    m.newGroup(40.f);
    m.use(MAT_INTERIOR, kCol1);
    float y0 = I.dashY0, y1 = I.rearSeat ? I.yHipR - 0.35f : I.yHipF - 0.4f;
    float xin = b.beltXAt((y0 + y1) * 0.5f) - 0.05f;
    // door card: vertical panel from the floor to the belt following the belt line
    int n = 14;
    std::vector<u32> lo(n + 1), hi(n + 1), ledge(n + 1);
    auto floorZ = [&](float y) {
        float z = I.zFloor;
        for (int a = 0; a < 2; a++) {
            float yw = a == 0 ? b.yWf : b.yWr;
            float dy = fabsf(y - yw);
            if (dy < b.Ra + 0.05f) z = Max(z, b.s.wheelR + sqrtf(Max(Sq(b.Ra + 0.05f) - dy * dy, 0.f)) + 0.03f);
        }
        return z;
    };
    float yDoorF = Max(y0, b.s.yCowl);  // run the trim up to the windscreen base (firewall)
    for (int k = 0; k <= n; k++) {
        float y = lerp(y1, yDoorF, k / (float)n);
        float bx = b.beltXAt(y) - 0.045f, bz = b.beltZAt(y) - 0.01f;
        lo[k] = m.add(vec3(bx - 0.03f, y, Min(floorZ(y), bz - 0.05f)));
        hi[k] = m.add(vec3(bx, y, bz));
        ledge[k] = m.add(vec3(b.beltXAt(y) - 0.012f, y, bz + 0.006f));
    }
    for (int k = 0; k < n; k++) {
        m.quadFacing(lo[k], lo[k + 1], hi[k + 1], hi[k], vec3(-1, 0, 0));
        m.quadFacing(hi[k], hi[k + 1], ledge[k + 1], ledge[k], vec3(0, 0, 1));
    }
    // floor half (stops in front of the rear wheel well; a raised pan covers the axle)
    float xWell = b.s.trackR - b.s.wheelW * 0.5f - 0.06f;
    float yfr = Max(y1, b.yWr + b.Ra + 0.03f);
    m.quadFacing(m.add(vec3(0, yfr, I.zFloor)), m.add(vec3(xin, yfr, I.zFloor)), m.add(vec3(xin, y0, I.zFloor)), m.add(vec3(0, y0, I.zFloor)), vec3(0, 0, 1));
    if (y1 < yfr) {
        float zk = floorZ(b.yWr) - 0.02f;
        float xk = Min(xin, xWell);
        m.quadFacing(m.add(vec3(0, y1, zk)), m.add(vec3(xk, y1, zk)), m.add(vec3(xk, yfr, zk)), m.add(vec3(0, yfr, zk)), vec3(0, 0, 1));
        m.quadFacing(m.add(vec3(0, yfr, zk)), m.add(vec3(xk, yfr, zk)), m.add(vec3(xk, yfr, I.zFloor)), m.add(vec3(0, yfr, I.zFloor)), vec3(0, 1, 0));
    }
    // front passenger seat (right) and rear right half
    seat(m, vec3(I.seatX, I.yHipF, I.zFloor + I.hipH), 0.25f, L, !I.bench);
    m.mirrorX(mk);
    // rear bench
    if (I.rearSeat) {
        // back height limited by the glass/roof above the backrest
        float yb = I.yHipR - 0.22f;
        float room = b.roofZAt(yb) - 0.10f - (I.zFloor + I.hipH);
        float bs = Clamp((room - 0.12f) / 0.62f, 0.55f, 1.f);
        bool hr = room > 0.62f * bs + 0.2f;
        seat(m, vec3(0, I.yHipR, I.zFloor + I.hipH - 0.02f), Min(xin - 0.05f, 0.68f), L, false, 0.36f, bs, hr);
    }
    // dashboard
    m.newGroup(35.f);
    m.use(MAT_INTERIOR, kCol1);
    float dw = b.beltXAt(I.dashY1) - 0.05f;
    float dz = b.beltZAt(I.dashY1);
    std::vector<vec2> dp;  // side profile (y, z) extruded across x
    dp.push_back(vec2(I.dashY0, dz - 0.02f));
    dp.push_back(vec2(I.dashY0, dz - 0.06f));
    dp.push_back(vec2(I.dashY1 + 0.08f, dz - 0.35f));
    dp.push_back(vec2(I.dashY1, dz - 0.25f));
    dp.push_back(vec2(I.dashY1 - 0.02f, dz - 0.02f));
    dp.push_back(vec2(I.dashY1 + 0.06f, dz + 0.035f));
    extrude(m, dp, Frame(vec3(0, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1, 0, 0)), -dw, dw);
    // defroster deck from the dash top to the windscreen base, and the firewall below it: the cabin is closed at the
    // front so the see-through glass never reveals the inside of the cowl or engine bay
    {
        float yf = Max(b.s.yCowl - 0.005f, I.dashY0), zf = b.s.zCowl - 0.015f;
        float xc = Min(b.beltXAt(yf), b.sideXAt(yf, zf)) - 0.03f;
        if (yf > I.dashY0 + 0.004f)
            quad2(m, vec3(-dw, I.dashY0, dz - 0.02f), vec3(dw, I.dashY0, dz - 0.02f), vec3(xc, yf, zf), vec3(-xc, yf, zf), vec3(0, 0, 1));
        // firewall following the body section so its edges reach the side panels (no gap beside it)
        const int nl = 5;
        float zl[nl + 1], xl[nl + 1];
        for (int k = 0; k <= nl; k++) {
            zl[k] = lerp(I.zFloor, zf, k / (float)nl);
            xl[k] = Min(b.sideXAt(yf, zl[k]), zl[k] > b.beltZAt(yf) - 0.02f ? b.beltXAt(yf) : 9.f) - 0.025f;
        }
        for (int k = 0; k < nl; k++)
            quad2(m, vec3(-xl[k], yf, zl[k]), vec3(xl[k], yf, zl[k]), vec3(xl[k + 1], yf, zl[k + 1]), vec3(-xl[k + 1], yf, zl[k + 1]), vec3(0, -1, 0));
    }
    // instrument binnacle + centre screen
    roundedBoxAt(m, vec3(-I.seatX, I.dashY1 + 0.05f, dz + 0.04f), vec3(0.17f, 0.07f, 0.04f), 0.03f, 2);
    m.use(MAT_EMISSIVE, col(0.35f, 0.55f, 0.9f, 0.08f));
    roundedBox(m, Frame(vec3(0, I.dashY1 + 0.005f, dz - 0.05f), vec3(1, 0, 0), vec3(0, 0, 1), normalize(vec3(0, -1, 0.3f))), vec3(0.13f, 0.08f, 0.005f), 0.005f, 1);
    // centre console
    m.use(MAT_INTERIOR, kCol1);
    roundedBoxAt(m, vec3(0, (I.dashY1 + I.yHipF) * 0.5f - 0.05f, I.zFloor + 0.12f), vec3(0.1f, (I.dashY1 - I.yHipF) * 0.5f + 0.05f, 0.12f), 0.04f, 2);
    // steering wheel (left)
    steeringWheel(m, vec3(-I.seatX, I.yHipF + 0.42f, dz - 0.02f), 0.42f);
    // headliner under the roof (both halves): offset copy of the roof cells, facing down
    m.newGroup(40.f);
    m.use(MAT_FABRIC, col(0.55f, 0.55f, 0.53f));
    PMesh::Mark hk = m.mark();
    int NC1 = b.NP - 1;
    for (int i = 0; i + 1 < b.nr; i++) {
        if (b.rowL[i] < 0.01f || b.rowL[i + 1] < 0.01f) continue;
        for (int j = b.pRail0; j < NC1; j++) {
            u8 cc = b.cls[i * NC1 + j];
            float yc = (b.rows[i] + b.rows[i + 1]) * 0.5f;
            // windscreen / rear window stay clear; a sunroof keeps the lining under it (closed blind)
            bool sunroof = yc < b.s.yRoofF - 0.01f && yc > b.s.yRoofR + 0.01f;
            if ((cc == CC_GLASS && !sunroof) || cc == CC_HOLE || cc == CC_SKIP || cc == CC_INTERIOR || cc == CC_BED) continue;
            int ii[4] = {i, i + 1, i + 1, i}, jj[4] = {j, j, j + 1, j + 1};
            u32 v[4];
            vec3 nsum(0, 0, 0);
            for (int k = 0; k < 4; k++) {
                // just inside the glass inset, so the lining meets every window edge without a gap
                vec3 g = b.G[ii[k] * b.NP + jj[k]], n = b.GN[ii[k] * b.NP + jj[k]];
                v[k] = m.add(g - n * 0.012f);
                nsum += n;
            }
            m.quadFacing(v[0], v[1], v[2], v[3], -nsum);
        }
    }
    m.mirrorX(hk);
    cabinTrim(m, b, I, y0, y1, floorZ(b.yWr) - 0.02f);
}

}  // namespace detail
}  // namespace Vehicles

namespace Vehicles {
namespace detail {

// ------------------------------------------------------------------------------------------------
// Stroke font (4 x 6 grid per glyph) for livery lettering, rendered as raised flat bars.
inline const char* glyphStrokes(char c) {
    // strokes separated by '|', points "x y" pairs separated by spaces
    switch (c) {
        case 'A': return "0 0 0 4 2 6 4 4 4 0|0 3 4 3";
        case 'B': return "0 0 0 6 3 6 4 5 4 4 3 3 0 3|3 3 4 2 4 1 3 0 0 0";
        case 'C': return "4 5 3 6 1 6 0 5 0 1 1 0 3 0 4 1";
        case 'D': return "0 0 0 6 3 6 4 5 4 1 3 0 0 0";
        case 'E': return "4 6 0 6 0 0 4 0|0 3 3 3";
        case 'F': return "4 6 0 6 0 0|0 3 3 3";
        case 'G': return "4 5 3 6 1 6 0 5 0 1 1 0 3 0 4 1 4 3 2 3";
        case 'H': return "0 0 0 6|4 0 4 6|0 3 4 3";
        case 'I': return "2 0 2 6|1 6 3 6|1 0 3 0";
        case 'K': return "0 0 0 6|4 6 0 2|1 3 4 0";
        case 'L': return "0 6 0 0 4 0";
        case 'M': return "0 0 0 6 2 3 4 6 4 0";
        case 'N': return "0 0 0 6 4 0 4 6";
        case 'O': return "1 0 0 1 0 5 1 6 3 6 4 5 4 1 3 0 1 0";
        case 'P': return "0 0 0 6 3 6 4 5 4 4 3 3 0 3";
        case 'R': return "0 0 0 6 3 6 4 5 4 4 3 3 0 3|2 3 4 0";
        case 'S': return "4 5 3 6 1 6 0 5 0 4 1 3 3 3 4 2 4 1 3 0 1 0 0 1";
        case 'T': return "0 6 4 6|2 6 2 0";
        case 'U': return "0 6 0 1 1 0 3 0 4 1 4 6";
        case 'V': return "0 6 2 0 4 6";
        case 'W': return "0 6 1 0 2 4 3 0 4 6";
        case 'X': return "0 0 4 6|0 6 4 0";
        case 'Y': return "0 6 2 3 4 6|2 3 2 0";
        case '0': return "1 0 0 1 0 5 1 6 3 6 4 5 4 1 3 0 1 0";
        case '1': return "1 5 2 6 2 0|1 0 3 0";
        case '2': return "0 5 1 6 3 6 4 5 4 4 0 0 4 0";
        case '3': return "0 5 1 6 3 6 4 5 4 4 3 3 1 3|3 3 4 2 4 1 3 0 1 0 0 1";
        case '4': return "3 0 3 6 0 2 4 2";
        case '5': return "4 6 0 6 0 3 3 3 4 2 4 1 3 0 0 0";
        case '6': return "3 6 1 6 0 5 0 1 1 0 3 0 4 1 4 2 3 3 0 3";
        case '7': return "0 6 4 6 1 0";
        case '8': return "1 3 0 4 0 5 1 6 3 6 4 5 4 4 3 3 1 3 0 2 0 1 1 0 3 0 4 1 4 2 3 3";
        case '9': return "4 3 1 3 0 4 0 5 1 6 3 6 4 5 4 1 3 0 1 0";
        case '-': return "1 3 3 3";
        case 'J': return "4 6 4 1 3 0 1 0 0 1";
        case 'Q': return "1 0 0 1 0 5 1 6 3 6 4 5 4 1 3 0 1 0|2 2 4 0";
        case 'Z': return "0 6 4 6 0 0 4 0";
        case '\'': return "2 6 1.6 4.6";
        case '.': return "2 0 2 0.6";
        default: return "";
    }
}
// Text centred at (u, v) in the decal plane, running along +u; height h.
inline void textDecal(PMesh& m, const Decal& dc, const char* text, vec2 at, float h, float off = 0.0015f) {
    int n = (int)strlen(text);
    float cw = h * 0.72f;  // advance
    float sc = h / 6.f;
    float x0 = at.x - (n * cw - (cw - 4.f * sc)) * 0.5f;
    for (int i = 0; i < n; i++) {
        const char* st = glyphStrokes(text[i]);
        std::vector<vec2> line;
        const char* p = st;
        while (true) {
            if (*p == '|' || *p == 0) {
                if (line.size() >= 2) decalBar(m, dc, line, h * 0.14f, 0.0012f, off, 0.02f);
                line.clear();
                if (*p == 0) break;
                p++;
                continue;
            }
            char* e;
            float x = strtof(p, &e);
            p = e;
            float y = strtof(p, &e);
            p = e;
            while (*p == ' ') p++;
            line.push_back(vec2(x0 + i * cw + x * sc, at.y - h * 0.5f + y * sc));
        }
    }
}

// Text built from thin boxes in 3D: origin = centre, `right` = reading direction, `up`, `out` = face normal.
inline void strokeText3D(PMesh& m, vec3 origin, vec3 right, vec3 up, const char* text, float h, float depth) {
    vec3 out = normalize(cross(right, up));
    int n = (int)strlen(text);
    float cw = h * 0.72f, sc = h / 6.f;
    float x0 = -(n * cw - (cw - 4.f * sc)) * 0.5f;
    for (int i = 0; i < n; i++) {
        const char* p = glyphStrokes(text[i]);
        std::vector<vec2> line;
        while (true) {
            if (*p == '|' || *p == 0) {
                for (size_t k = 0; k + 1 < line.size(); k++) {
                    vec3 a = origin + right * line[k].x + up * line[k].y, bq = origin + right * line[k + 1].x + up * line[k + 1].y;
                    vec3 d = bq - a;
                    float l = length(d);
                    if (l < 1e-4f) continue;
                    vec3 ax = d / l;
                    vec3 ay = normalize(cross(out, ax));
                    // flat-topped bar standing on the surface (no bottom face: it lies on the panel)
                    float hl = l * 0.5f + h * 0.06f, hw = h * 0.07f;
                    std::vector<vec2> bar;
                    bar.push_back(vec2(-hl, -hw)); bar.push_back(vec2(hl, -hw)); bar.push_back(vec2(hl, hw)); bar.push_back(vec2(-hl, hw));
                    extrude(m, bar, Frame((a + bq) * 0.5f, ax, ay, out), 0.f, depth, false, true);
                }
                line.clear();
                if (*p == 0) break;
                p++;
                continue;
            }
            char* e;
            float x = strtof(p, &e);
            p = e;
            float y = strtof(p, &e);
            p = e;
            while (*p == ' ') p++;
            line.push_back(vec2(x0 + i * cw + x * sc, -h * 0.5f + y * sc));
        }
    }
}

// Chrome (or black) bumper bar hugging the front/rear surface at height z
inline void bumperBar(PMesh& m, CarBody& b, bool rear, float z, float hw, bool chrome, float height = 0.11f) {
    Frame fr = rear ? projRear() : projFront();
    Decal dc;
    if (!decalAt(dc, b.proj, fr, vec3(0, rear ? b.yR - 0.5f : b.yF + 0.5f, z), vec3(0, rear ? 1.f : -1.f, 0))) return;
    decalRange(dc, vec2(-hw - 0.1f, -0.2f), vec2(hw + 0.1f, 0.2f));
    std::vector<vec2> line;
    for (int k = 0; k <= 16; k++) line.push_back(vec2(lerp(-hw, hw, k / 16.f), 0.f));
    m.newGroup(45.f);
    m.use(chrome ? MAT_CHROME : MAT_PLASTIC, chrome ? kCol1 : col(0.6f, 0.6f, 0.6f));
    decalBar(m, dc, line, height, 0.045f, -0.005f, 0.06f);
}

inline void roofSpoiler(PMesh& m, CarBody& b) {
    float y = b.s.yRoofR + 0.10f;
    float z = b.roofZAt(y) - 0.008f;
    float hw = b.railXAt(y) - 0.005f;
    m.newGroup(35.f);
    m.use(MAT_CARPAINT, kCol1);
    std::vector<vec2> prof;  // (along -y, z)
    prof.push_back(vec2(0.0f, 0.0f));
    prof.push_back(vec2(0.10f, -0.012f));
    prof.push_back(vec2(0.20f, -0.035f));
    prof.push_back(vec2(0.215f, -0.02f));
    prof.push_back(vec2(0.12f, 0.022f));
    prof.push_back(vec2(0.02f, 0.016f));
    extrude(m, prof, Frame(vec3(0, y, z), vec3(0, -1, 0), vec3(0, 0, 1), vec3(1, 0, 0)), -hw, hw);
    m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));
    roundedBoxAt(m, vec3(0, y - 0.19f, z - 0.028f), vec3(0.16f, 0.012f, 0.006f), 0.004f, 1);
}

// Spare wheel cover on the tailgate (off-roaders)
inline void spareWheel(PMesh& m, CarBody& b, float R, float W) {
    Frame fr = projRear();
    Decal dc;
    float zc = (b.s.zTailTop + b.s.zDeck) * 0.5f;
    if (!decalAt(dc, b.proj, fr, vec3(0, b.yR - 0.5f, zc), vec3(0, 1, 0))) return;
    vec3 c = dc.fr.o - vec3(0, W * 0.5f + 0.03f, 0);
    m.newGroup(50.f);
    m.use(MAT_TIRE, kCol1);
    std::vector<vec2> prof;  // tire as a torus-like lathe around the y axis
    prof.push_back(vec2(W * 0.5f, R * 0.62f));
    prof.push_back(vec2(W * 0.5f, R * 0.9f));
    prof.push_back(vec2(W * 0.4f, R));
    prof.push_back(vec2(-W * 0.4f, R));
    prof.push_back(vec2(-W * 0.5f, R * 0.9f));
    prof.push_back(vec2(-W * 0.5f, R * 0.62f));
    for (auto& q : prof) q.x = -q.x;
    lathe(m, c, vec3(0, -1, 0), vec3(1, 0, 0), prof, 24);
    m.use(MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
    disk(m, c - vec3(0, W * 0.5f, 0), vec3(0, -1, 0), R * 0.64f, 20);
    m.use(MAT_METAL_PAINTED, col(0.2f, 0.2f, 0.2f));
    disk(m, c - vec3(0, W * 0.5f + 0.004f, 0), vec3(0, -1, 0), R * 0.25f, 16);
}

}  // namespace detail
}  // namespace Vehicles
