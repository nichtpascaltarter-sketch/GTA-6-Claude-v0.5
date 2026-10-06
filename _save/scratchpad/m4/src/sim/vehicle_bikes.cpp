// Two-wheelers: sport bike, cruiser and scooter. Local frame as cars: +Y forward, origin on the ground at the
// centre of the wheelbase. Both wheels use the single wheel mesh (no mirroring).
namespace Vehicles {
namespace detail {

// Small motorcycle registration: characters embossed on the plate whose centre is `c`, facing backwards (-y), `up`
// along the plate.
inline void bikePlateText(PMesh& m, vec3 c, vec3 up, const std::string& name) {
    if (lodLevel() > 0) return;
    CarLook L;
    plateText(L, name);
    L.plate[3] = 0;  // two rows on a small plate: letters over digits
    vec3 right(1, 0, 0);
    vec3 out = normalize(cross(right, up));
    m.use(MAT_METAL_PAINTED, col(0.05f, 0.08f, 0.2f));
    strokeText3D(m, c + out * 0.0035f + up * 0.019f, right, up, L.plate, 0.034f, 0.001f);
    strokeText3D(m, c + out * 0.0035f - up * 0.021f, right, up, L.plate + 4, 0.034f, 0.001f);
}

// Rectangular beam swept along a path (centred), `up` hint orients the section.
inline void beamPath(PMesh& m, const std::vector<vec3>& pts, vec3 up, float w, float h) {
    int n = (int)pts.size();
    std::vector<u32> ring(n * 4);
    for (int i = 0; i < n; i++) {
        vec3 t = normalize(pts[Min(i + 1, n - 1)] - pts[Max(i - 1, 0)]);
        vec3 u = normalize(up - t * dot(up, t));
        vec3 s = cross(t, u);
        vec3 c = pts[i];
        ring[i * 4 + 0] = m.add(c + s * (w * 0.5f) - u * (h * 0.5f));
        ring[i * 4 + 1] = m.add(c + s * (w * 0.5f) + u * (h * 0.5f));
        ring[i * 4 + 2] = m.add(c - s * (w * 0.5f) + u * (h * 0.5f));
        ring[i * 4 + 3] = m.add(c - s * (w * 0.5f) - u * (h * 0.5f));
    }
    for (int i = 0; i + 1 < n; i++)
        for (int k = 0; k < 4; k++) {
            int k1 = (k + 1) % 4;
            u32 a = ring[i * 4 + k], b = ring[(i + 1) * 4 + k], c = ring[(i + 1) * 4 + k1], d = ring[i * 4 + k1];
            vec3 ctr = (pts[i] + pts[i + 1]) * 0.5f;
            m.quadFacing(a, b, c, d, (m.P[a] + m.P[b] + m.P[c] + m.P[d]) * 0.25f - ctr);
        }
    for (int e = 0; e < 2; e++) {
        int i = e == 0 ? 0 : n - 1;
        vec3 dir = e == 0 ? pts[0] - pts[1] : pts[n - 1] - pts[n - 2];
        m.quadFacing(ring[i * 4], ring[i * 4 + 1], ring[i * 4 + 2], ring[i * 4 + 3], dir);
    }
}
// Finned cylinder (air-cooled): lathe along `axis` from base
inline void finnedCylinder(PMesh& m, vec3 base, vec3 axis, float len, float r, int fins, u8 mat, u32 c) {
    std::vector<vec2> p;
    p.push_back(vec2(0.f, 0.f));
    p.push_back(vec2(0.f, r * 0.8f));
    for (int k = 0; k < fins; k++) {
        float a0 = len * (k + 0.2f) / fins, a1 = len * (k + 0.5f) / fins, a2 = len * (k + 0.8f) / fins;
        p.push_back(vec2(a0, r * 0.8f));
        p.push_back(vec2(a0 + 0.002f, r));
        p.push_back(vec2(a1, r));
        p.push_back(vec2(a2 - 0.002f, r * 0.8f));
    }
    p.push_back(vec2(len, r * 0.8f));
    p.push_back(vec2(len, 0.f));
    m.use(mat, c);
    lathe(m, base, axis, anyPerp(axis), p, 14);
}
// Fender: a band of radius R around a wheel axle (x axis), from angle a0..a1 (0 = +y forward, pi/2 = up)
inline void wheelFender(PMesh& m, vec3 axle, float R, float w, float a0, float a1, float lip, u8 mat, u32 c) {
    m.use(mat, c);
    std::vector<vec2> prof;  // (a along +x, r)
    prof.push_back(vec2(-w * 0.5f, R - lip));
    prof.push_back(vec2(-w * 0.5f, R));
    prof.push_back(vec2(-w * 0.35f, R + 0.015f));
    prof.push_back(vec2(w * 0.35f, R + 0.015f));
    prof.push_back(vec2(w * 0.5f, R));
    prof.push_back(vec2(w * 0.5f, R - lip));
    lathe(m, axle, vec3(1, 0, 0), vec3(0, 1, 0), prof, 16, a0, a1);
    std::vector<vec2> in;
    in.push_back(vec2(w * 0.5f, R - lip));
    in.push_back(vec2(-w * 0.5f, R - lip));
    lathe(m, axle, vec3(1, 0, 0), vec3(0, 1, 0), in, 16, a0, a1);
}
inline void bikeMeta(VehicleModel& o, float yF, float yR, float r, float w, vec3 head, vec3 tail, vec3 rider, vec3 pillion, bool hasPillion) {
    o.wheels.push_back(WheelSpec{vec3(0, yF, r), r, w, true, false, false});
    o.wheels.push_back(WheelSpec{vec3(0, yR, r), r, w, false, true, false});
    addLight(o, head, vec3(0, 1, -0.05f), LT_HEAD);
    addLight(o, tail, vec3(0, -1, 0), LT_TAIL);
    addLight(o, tail, vec3(0, -1, 0), LT_BRAKE);
    for (int s = -1; s <= 1; s += 2) {
        addLight(o, head + vec3(s * 0.14f, -0.05f, -0.05f), vec3(s * 0.3f, 1, 0), s < 0 ? LT_INDICATOR_L : LT_INDICATOR_R);
        addLight(o, tail + vec3(s * 0.12f, 0.f, -0.08f), vec3(s * 0.3f, -1, 0), s < 0 ? LT_INDICATOR_L : LT_INDICATOR_R);
    }
    o.seats.push_back(SeatSpec{rider, true, true});
    if (hasPillion) o.seats.push_back(SeatSpec{pillion, false, true});
}

// ------------------------------------------------------------------------------------------------
inline void mdlRaijin(VehicleModel& o) {
    o.name = "Raijin 1000"; o.maker = "Sakaki"; o.cls = VC_MOTORBIKE;
    PMesh m;
    const float R = 0.31f, W = 0.16f, yFw = 0.705f, yRw = -0.705f;
    const u32 dark = col(0.18f, 0.18f, 0.19f);
    // fork geometry (rake 24 deg)
    vec3 axleF(0, yFw, R);
    vec3 forkDir = normalize(vec3(0, -sinf(24.f * kDegToRad), cosf(24.f * kDegToRad)));
    vec3 head = axleF + forkDir * 0.66f, topClamp = axleF + forkDir * 0.72f, botClamp = axleF + forkDir * 0.56f;
    // --- frame spars (aluminium twin beam) + swingarm
    m.newGroup(35.f);
    m.use(MAT_METAL_BRUSHED, col(0.72f, 0.72f, 0.75f));
    for (int s = -1; s <= 1; s += 2) {
        std::vector<vec3> sp;
        sp.push_back(vec3(s * 0.05f, head.y - 0.02f, head.z - 0.05f));
        sp.push_back(vec3(s * 0.12f, 0.28f, 0.80f));
        sp.push_back(vec3(s * 0.14f, 0.05f, 0.70f));
        sp.push_back(vec3(s * 0.13f, -0.12f, 0.56f));
        sp.push_back(vec3(s * 0.12f, -0.17f, 0.42f));
        beamPath(m, catmull(sp, 3), vec3(0, 0.3f, 1), 0.045f, 0.10f);
        std::vector<vec3> sw;
        sw.push_back(vec3(s * 0.11f, -0.16f, 0.42f));
        sw.push_back(vec3(s * 0.12f, -0.40f, 0.40f));
        sw.push_back(vec3(s * 0.10f, yRw, R));
        beamPath(m, catmull(sw, 3), vec3(0, 0.2f, 1), 0.04f, 0.085f);
    }
    // subframe under the tail
    m.use(MAT_METAL_PAINTED, dark);
    for (int s = -1; s <= 1; s += 2) cyl(m, vec3(s * 0.1f, -0.16f, 0.58f), vec3(s * 0.08f, -0.62f, 0.8f), 0.013f, 6);
    // chain (left)
    m.use(MAT_METAL_PAINTED, col(0.25f, 0.25f, 0.25f));
    for (int k = 0; k < 2; k++) {
        std::vector<vec3> ch;
        ch.push_back(vec3(-0.085f, 0.02f, 0.40f + (k ? -0.05f : 0.05f)));
        ch.push_back(vec3(-0.085f, yRw, R + (k ? -0.09f : 0.09f)));
        beamPath(m, ch, vec3(0, 0, 1), 0.012f, 0.012f);
    }
    // --- engine (inline four, canted forward)
    m.newGroup(35.f);
    m.use(MAT_METAL_PAINTED, col(0.12f, 0.12f, 0.13f));
    roundedBoxAt(m, vec3(0, 0.02f, 0.40f), vec3(0.16f, 0.17f, 0.12f), 0.05f, 2);
    Frame cf(vec3(0, 0.16f, 0.60f), vec3(1, 0, 0), normalize(vec3(0, 0.9f, 0.45f)), normalize(vec3(0, -0.45f, 0.9f)));
    roundedBox(m, cf, vec3(0.17f, 0.09f, 0.13f), 0.03f, 1);
    m.use(MAT_METAL_BRUSHED, col(0.5f, 0.5f, 0.52f));
    roundedBox(m, Frame(cf.o + cf.z * 0.14f, cf.x, cf.y, cf.z), vec3(0.15f, 0.075f, 0.03f), 0.02f, 1);  // cam cover
    m.use(MAT_METAL_BRUSHED, col(0.6f, 0.6f, 0.62f));
    for (int s = -1; s <= 1; s += 2) {
        std::vector<vec2> cv;
        cv.push_back(vec2(0.f, 0.f)); cv.push_back(vec2(0.f, 0.09f)); cv.push_back(vec2(0.02f, 0.085f)); cv.push_back(vec2(0.025f, 0.f));
        lathe(m, vec3(s * 0.16f, s < 0 ? -0.05f : 0.05f, 0.42f), vec3((float)s, 0, 0), vec3(0, 1, 0), cv, 16);
    }
    // radiator
    m.use(MAT_PLASTIC, col(0.3f, 0.3f, 0.3f));
    roundedBox(m, Frame(vec3(0, 0.34f, 0.62f), vec3(1, 0, 0), vec3(0, 0.26f, 0.97f), normalize(vec3(0, 0.97f, -0.26f))), vec3(0.19f, 0.15f, 0.03f), 0.01f, 1);
    // --- exhaust: headers under the engine to a side can
    m.newGroup(40.f);
    m.use(MAT_METAL_BRUSHED, col(0.55f, 0.52f, 0.5f));
    for (int k = 0; k < 4; k++) {
        float x = -0.09f + k * 0.06f;
        std::vector<vec3> hp;
        hp.push_back(vec3(x, 0.26f, 0.55f));
        hp.push_back(vec3(x, 0.33f, 0.40f));
        hp.push_back(vec3(x * 0.6f, 0.25f, 0.22f));
        hp.push_back(vec3(x * 0.3f, 0.02f, 0.18f));
        hp.push_back(vec3(0.04f, -0.12f, 0.22f));
        tube1(m, catmull(hp, 3), 0.018f, 6, false);
    }
    {
        std::vector<vec3> lk;
        lk.push_back(vec3(0.04f, -0.12f, 0.22f));
        lk.push_back(vec3(0.15f, -0.30f, 0.34f));
        tube1(m, lk, 0.03f, 8, false);
        m.use(MAT_METAL_BRUSHED, col(0.3f, 0.3f, 0.32f));
        std::vector<LoftSec> can;
        can.push_back({0.f, 0.f, 0.f, 0.04f, 0.04f, 2.f});
        can.push_back({0.03f, 0.f, 0.f, 0.065f, 0.055f, 3.f});
        can.push_back({0.30f, 0.f, 0.f, 0.07f, 0.06f, 3.f});
        can.push_back({0.34f, 0.f, 0.f, 0.05f, 0.045f, 3.f});
        Frame ef = frameFY(vec3(0.17f, -0.32f, 0.36f), normalize(vec3(0.05f, -1.f, 0.35f)), vec3(0, 0, 1));
        loftFrame(m, ef, can, 12, true, true);
    }
    // --- bodywork: tank, fairing, belly pan, tail
    m.newGroup(38.f);
    m.use(MAT_CARPAINT, kCol1);
    {
        std::vector<LoftSec> t;
        t.push_back({0.44f, 0, 0.88f, 0.10f, 0.05f, 2.2f});
        t.push_back({0.36f, 0, 0.93f, 0.17f, 0.09f, 2.4f});
        t.push_back({0.20f, 0, 0.97f, 0.20f, 0.11f, 2.4f});
        t.push_back({0.05f, 0, 0.95f, 0.18f, 0.10f, 2.4f});
        t.push_back({-0.04f, 0, 0.90f, 0.13f, 0.06f, 2.4f});
        loftY(m, t, 16, true, true);
    }
    {
        std::vector<LoftSec> f;  // upper fairing around the headlights (pointed nose, slim flanks)
        f.push_back({1.00f, 0, 0.82f, 0.012f, 0.012f, 2.f});
        f.push_back({0.965f, 0, 0.83f, 0.065f, 0.05f, 2.f});
        f.push_back({0.90f, 0, 0.84f, 0.115f, 0.08f, 2.1f, 1.3f});
        f.push_back({0.80f, 0, 0.845f, 0.155f, 0.105f, 2.3f, 1.7f});
        f.push_back({0.68f, 0, 0.84f, 0.17f, 0.115f, 2.4f, 2.1f});
        f.push_back({0.56f, 0, 0.85f, 0.15f, 0.08f, 2.4f, 2.2f});
        loftY(m, f, 18, true, true);
    }
    for (int s = -1; s <= 1; s += 2) {
        // side fairings: long curved panels from the nose back over the engine, in the body colour, with a
        // gloss-black intake vent (the engine shows below and behind them)
        std::vector<LoftSec> sp;
        // (thin curved shells: a narrow superellipse section swept along a flank line that bows outwards)
        sp.push_back({0.70f, s * 0.13f, 0.68f, 0.012f, 0.08f, 2.2f, 1.2f});
        sp.push_back({0.60f, s * 0.175f, 0.61f, 0.016f, 0.15f, 2.4f, 1.3f});
        sp.push_back({0.46f, s * 0.195f, 0.56f, 0.018f, 0.18f, 2.6f, 1.2f});
        sp.push_back({0.30f, s * 0.19f, 0.53f, 0.016f, 0.15f, 2.6f, 1.1f});
        sp.push_back({0.16f, s * 0.17f, 0.54f, 0.012f, 0.08f, 2.2f, 1.f});
        m.use(MAT_CARPAINT, kCol1);
        loftY(m, sp, 16, true, true);
        m.use(MAT_CAR_GLASS, kCol1);
        Frame vf(vec3(s * 0.214f, 0.47f, 0.60f), vec3(0, -1, 0) * (float)s, normalize(vec3(0, 0.35f, 1)), vec3((float)s, 0, 0));
        vf.x = normalize(cross(vf.y, vf.z));
        roundedBox(m, vf, vec3(0.07f, 0.035f, 0.006f), 0.006f, 1);
    }
    m.use(MAT_CARPAINT, kCol2);
    roundedBoxAt(m, vec3(0, 0.05f, 0.20f), vec3(0.14f, 0.22f, 0.04f), 0.03f, 1);  // belly pan
    m.use(MAT_CARPAINT, kCol1);
    {
        std::vector<LoftSec> t;
        t.push_back({-0.12f, 0, 0.74f, 0.15f, 0.08f, 2.4f});
        t.push_back({-0.35f, 0, 0.84f, 0.14f, 0.09f, 2.4f});
        t.push_back({-0.62f, 0, 0.92f, 0.10f, 0.07f, 2.4f});
        t.push_back({-0.84f, 0, 0.96f, 0.035f, 0.035f, 2.2f});
        loftY(m, t, 14, true, true);
    }
    // seats
    m.newGroup(45.f);
    m.use(MAT_LEATHER, col(0.3f, 0.3f, 0.3f));
    {
        std::vector<LoftSec> t;
        t.push_back({0.02f, 0, 0.85f, 0.10f, 0.03f, 2.5f});
        t.push_back({-0.10f, 0, 0.84f, 0.15f, 0.04f, 3.f});
        t.push_back({-0.30f, 0, 0.87f, 0.13f, 0.04f, 3.f});
        t.push_back({-0.36f, 0, 0.89f, 0.08f, 0.03f, 2.5f});
        loftY(m, t, 12, true, true);
        std::vector<LoftSec> p;
        p.push_back({-0.40f, 0, 0.93f, 0.08f, 0.025f, 2.5f});
        p.push_back({-0.56f, 0, 0.97f, 0.07f, 0.025f, 2.5f});
        loftY(m, p, 10, true, true);
    }
    // windscreen
    m.newGroup(30.f);
    m.use(MAT_CAR_WINDOW, col(0.55f, 0.6f, 0.66f, 0.55f));  // smoked screen
    {
        std::vector<LoftSec> w;
        w.push_back({0.80f, 0, 0.99f, 0.13f, 0.012f, 2.f});
        w.push_back({0.68f, 0, 1.07f, 0.15f, 0.012f, 2.f});
        w.push_back({0.58f, 0, 1.11f, 0.13f, 0.012f, 2.f});
        loftY(m, w, 10, true, true);
    }
    // lights
    m.newGroup(50.f);
    m.use(MAT_LIGHT_HEAD, kCol1);
    for (int s = -1; s <= 1; s += 2)
        roundedBox(m, Frame(vec3(s * 0.075f, 0.955f, 0.87f), vec3(1, 0, 0), vec3(0, 0, 1), normalize(vec3(s * 0.4f, 1, 0.1f))), vec3(0.05f, 0.018f, 0.012f), 0.01f, 1);
    m.use(MAT_PLASTIC, col(0.1f, 0.1f, 0.1f));
    roundedBox(m, Frame(vec3(0, 0.965f, 0.80f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0)), vec3(0.06f, 0.025f, 0.012f), 0.01f, 1);  // ram air
    m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));
    roundedBox(m, Frame(vec3(0, -0.85f, 0.95f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0)), vec3(0.05f, 0.02f, 0.01f), 0.008f, 1);
    // fork, clamps, bars, mirrors
    m.newGroup(40.f);
    for (int s = -1; s <= 1; s += 2) {
        vec3 off(s * 0.1f, 0, 0);
        m.use(MAT_METAL_PAINTED, col(0.85f, 0.65f, 0.2f));
        cyl(m, topClamp + off, axleF + forkDir * 0.30f + off, 0.029f, 10);
        m.use(MAT_CHROME, kCol1);
        cyl(m, axleF + forkDir * 0.30f + off, axleF + off, 0.024f, 10);
        m.use(MAT_METAL_PAINTED, col(0.8f, 0.1f, 0.1f));
        roundedBox(m, Frame(axleF + off + vec3(s * 0.01f, -0.10f, 0.08f), vec3(1, 0, 0), forkDir, cross(vec3(1, 0, 0), forkDir)), vec3(0.018f, 0.07f, 0.035f), 0.01f, 1);
        // clip-on bar
        m.use(MAT_METAL_PAINTED, dark);
        cyl(m, topClamp + off - vec3(0, 0.02f, 0.03f), topClamp + vec3(s * 0.31f, -0.08f, -0.07f), 0.012f, 6);
        m.use(MAT_RUBBER, kCol1);
        cyl(m, topClamp + vec3(s * 0.22f, -0.06f, -0.06f), topClamp + vec3(s * 0.33f, -0.085f, -0.075f), 0.017f, 8);
        // mirror
        m.use(MAT_CARPAINT, kCol1);
        cyl(m, vec3(s * 0.2f, 0.72f, 0.98f), vec3(s * 0.28f, 0.72f, 1.03f), 0.008f, 5);
        roundedBoxAt(m, vec3(s * 0.31f, 0.71f, 1.04f), vec3(0.05f, 0.025f, 0.03f), 0.02f, 1);
        // foot pegs
        m.use(MAT_METAL_BRUSHED, col(0.6f, 0.6f, 0.62f));
        cyl(m, vec3(s * 0.12f, -0.24f, 0.36f), vec3(s * 0.22f, -0.25f, 0.36f), 0.011f, 6);
        // indicators
        m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.55f, 0.05f, 1.f));
        roundedBoxAt(m, vec3(s * 0.12f, -0.93f, 0.78f), vec3(0.03f, 0.015f, 0.012f), 0.008f, 1);
    }
    m.use(MAT_METAL_BRUSHED, col(0.6f, 0.6f, 0.62f));
    roundedBox(m, Frame(topClamp, vec3(1, 0, 0), forkDir, cross(vec3(1, 0, 0), forkDir)), vec3(0.14f, 0.05f, 0.018f), 0.01f, 1);
    roundedBox(m, Frame(botClamp, vec3(1, 0, 0), forkDir, cross(vec3(1, 0, 0), forkDir)), vec3(0.14f, 0.05f, 0.018f), 0.01f, 1);
    // front hugger fender & rear hugger
    m.newGroup(40.f);
    wheelFender(m, axleF, R + 0.03f, 0.13f, 0.35f, 1.9f, 0.01f, MAT_CARPAINT, kCol2);
    wheelFender(m, vec3(0, yRw, R), R + 0.04f, 0.15f, 1.7f, 2.6f, 0.01f, MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
    // licence plate hanger
    m.use(MAT_PLASTIC, col(0.4f, 0.4f, 0.4f));
    cyl(m, vec3(0, -0.8f, 0.9f), vec3(0, -0.95f, 0.76f), 0.012f, 6);
    m.use(MAT_METAL_PAINTED, col(0.9f, 0.9f, 0.86f));
    roundedBox(m, Frame(vec3(0, -0.96f, 0.72f), vec3(1, 0, 0), vec3(0, 0.2f, 1), normalize(vec3(0, -1, 0.2f))), vec3(0.09f, 0.055f, 0.003f), 0.004f, 1);
    bikePlateText(m, vec3(0, -0.96f, 0.72f), normalize(vec3(0, 0.2f, 1)), o.name);
    // clear lens over the twin headlamps
    m.newGroup(50.f);
    m.use(MAT_CAR_WINDOW, col(0.97f, 0.98f, 1.f, 1.f));
    for (int s = -1; s <= 1; s += 2)
        roundedBox(m, Frame(vec3(s * 0.075f, 0.957f, 0.87f), vec3(1, 0, 0), vec3(0, 0, 1), normalize(vec3(s * 0.4f, 1, 0.1f))), vec3(0.058f, 0.024f, 0.017f), 0.014f, 1);
    finalizeMesh(m, o.body);
    WheelDesign wd;
    wd.R = R; wd.W = W; wd.rimR = 0.216f; wd.moto = true; wd.style = RIM_BIKE; wd.spokes = 5; wd.spokeHub = 0.012f; wd.spokeRim = 0.009f;
    wd.faceTint = vec3(0.12f, 0.12f, 0.13f); wd.lipTint = vec3(0.15f); wd.lugs = 0; wd.seg = 28;
    buildWheel(wd, o.wheel);
    bikeMeta(o, yFw, yRw, R, W, vec3(0, 0.96f, 0.86f), vec3(0, -0.87f, 0.95f), vec3(0, -0.14f, 0.87f), vec3(0, -0.48f, 0.97f), true);
    o.boxCenter = vec3(0, 0.0f, 0.62f);
    o.boxHalf = vec3(0.33f, 1.02f, 0.56f);
    physics(o, 205.f, 150.f, 115.f, 14000.f, 83.f, 6, 0.f, 1.2f, 0.12f, 1.3f, 0.60f, 0.f, vec3(0, -0.02f, 0.58f), Audio::ENGINE_BIKE_SPORT);
    o.frontalArea = 0.62f;
    o.brakeForce = 3200.f;
    o.paletteColors = palette("bike");
    o.spawnWeight = 1.5f; o.price = 17000;
}

inline void mdlSundowner(VehicleModel& o) {
    o.name = "Sundowner 1800"; o.maker = "Ridley"; o.cls = VC_MOTORBIKE;
    PMesh m;
    const float R = 0.32f, W = 0.14f, yFw = 0.83f, yRw = -0.83f;
    const u32 black = col(0.15f, 0.15f, 0.16f);
    vec3 axleF(0, yFw, R);
    vec3 forkDir = normalize(vec3(0, -sinf(32.f * kDegToRad), cosf(32.f * kDegToRad)));
    vec3 head = axleF + forkDir * 0.76f, topClamp = axleF + forkDir * 0.82f;
    // --- frame (black steel tubes)
    m.newGroup(35.f);
    m.use(MAT_METAL_PAINTED, black);
    {
        std::vector<vec3> bb;
        bb.push_back(head);
        bb.push_back(vec3(0, 0.1f, 0.88f));
        bb.push_back(vec3(0, -0.35f, 0.72f));
        tube1(m, catmull(bb, 3), 0.028f, 8, true);
        for (int s = -1; s <= 1; s += 2) {
            std::vector<vec3> dt;
            dt.push_back(head + vec3(0, 0, -0.06f));
            dt.push_back(vec3(s * 0.07f, 0.32f, 0.40f));
            dt.push_back(vec3(s * 0.08f, 0.20f, 0.14f));
            dt.push_back(vec3(s * 0.10f, -0.30f, 0.16f));
            dt.push_back(vec3(s * 0.11f, -0.45f, 0.35f));
            dt.push_back(vec3(s * 0.10f, -0.35f, 0.72f));
            tube1(m, catmull(dt, 3), 0.018f, 6, true);
            std::vector<vec3> sa;  // swingarm
            sa.push_back(vec3(s * 0.11f, -0.45f, 0.35f));
            sa.push_back(vec3(s * 0.11f, yRw, R));
            tube1(m, sa, 0.022f, 8, true);
            m.use(MAT_CHROME, kCol1);
            cyl(m, vec3(s * 0.11f, -0.70f, 0.40f), vec3(s * 0.11f, -0.50f, 0.66f), 0.025f, 8);  // shocks
            m.use(MAT_METAL_PAINTED, black);
        }
    }
    // --- V-twin engine
    m.newGroup(40.f);
    m.use(MAT_METAL_PAINTED, col(0.2f, 0.2f, 0.21f));
    roundedBoxAt(m, vec3(0, -0.05f, 0.34f), vec3(0.12f, 0.2f, 0.12f), 0.06f, 2);
    for (int c = -1; c <= 1; c += 2) {
        vec3 ax = normalize(vec3(0, c * 0.40f, 1.f));
        vec3 b0 = vec3(0, -0.05f + c * 0.05f, 0.44f);
        finnedCylinder(m, b0, ax, 0.26f, 0.075f, 9, MAT_METAL_PAINTED, col(0.25f, 0.25f, 0.26f));
        m.use(MAT_CHROME, kCol1);
        roundedBox(m, Frame(b0 + ax * 0.30f, vec3(1, 0, 0), normalize(cross(ax, vec3(1, 0, 0))) * -1.f, ax), vec3(0.08f, 0.08f, 0.045f), 0.03f, 1);
    }
    m.use(MAT_CHROME, kCol1);
    {
        std::vector<vec2> ac;  // air cleaner (right)
        ac.push_back(vec2(0.f, 0.f)); ac.push_back(vec2(0.f, 0.11f)); ac.push_back(vec2(0.03f, 0.10f)); ac.push_back(vec2(0.045f, 0.06f)); ac.push_back(vec2(0.05f, 0.f));
        lathe(m, vec3(0.13f, -0.03f, 0.62f), vec3(1, 0, 0), vec3(0, 1, 0), ac, 18);
    }
    roundedBoxAt(m, vec3(-0.14f, -0.12f, 0.30f), vec3(0.03f, 0.2f, 0.09f), 0.04f, 2);  // primary cover (left)
    // exhaust: two chrome pipes along the right side
    m.newGroup(40.f);
    m.use(MAT_CHROME, kCol1);
    for (int k = 0; k < 2; k++) {
        std::vector<vec3> pp;
        float c = k == 0 ? 1.f : -1.f;
        pp.push_back(vec3(0.05f, -0.05f + c * 0.14f, 0.62f));
        pp.push_back(vec3(0.17f, -0.05f + c * 0.10f, 0.50f));
        pp.push_back(vec3(0.20f, -0.15f, 0.34f - k * 0.08f));
        pp.push_back(vec3(0.22f, -0.60f, 0.34f - k * 0.08f));
        pp.push_back(vec3(0.22f, -1.10f, 0.40f - k * 0.08f));
        std::vector<float> rr;
        rr.push_back(0.02f); rr.push_back(0.022f); rr.push_back(0.025f); rr.push_back(0.045f); rr.push_back(0.045f);
        std::vector<vec3> sm = catmull(pp, 3);
        std::vector<float> rs;
        for (size_t i = 0; i < sm.size(); i++) rs.push_back(lerp(0.022f, 0.045f, Saturate((0.f - sm[i].y - 0.2f) / 0.4f)));
        tube(m, sm, rs, 10, false, true);
    }
    // --- tank, seat, fenders
    m.newGroup(38.f);
    m.use(MAT_CARPAINT, kCol1);
    {
        std::vector<LoftSec> t;
        t.push_back({0.44f, 0, 0.93f, 0.09f, 0.05f, 2.f});
        t.push_back({0.34f, 0, 0.97f, 0.17f, 0.10f, 2.2f});
        t.push_back({0.14f, 0, 0.98f, 0.20f, 0.12f, 2.2f});
        t.push_back({-0.04f, 0, 0.92f, 0.16f, 0.09f, 2.2f});
        t.push_back({-0.12f, 0, 0.86f, 0.08f, 0.05f, 2.f});
        loftY(m, t, 16, true, true);
    }
    m.use(MAT_CHROME, kCol1);
    roundedBoxAt(m, vec3(0, 0.18f, 1.10f), vec3(0.05f, 0.18f, 0.015f), 0.012f, 1);
    m.newGroup(45.f);
    m.use(MAT_LEATHER, col(0.6f, 0.6f, 0.6f));
    {
        std::vector<LoftSec> t;
        t.push_back({-0.10f, 0, 0.74f, 0.12f, 0.04f, 2.5f});
        t.push_back({-0.25f, 0, 0.70f, 0.19f, 0.05f, 2.8f});
        t.push_back({-0.40f, 0, 0.76f, 0.17f, 0.06f, 2.8f});
        t.push_back({-0.46f, 0, 0.80f, 0.12f, 0.05f, 2.4f});
        loftY(m, t, 14, true, true);
        std::vector<LoftSec> p;
        p.push_back({-0.50f, 0, 0.82f, 0.10f, 0.03f, 2.5f});
        p.push_back({-0.66f, 0, 0.86f, 0.09f, 0.03f, 2.5f});
        loftY(m, p, 10, true, true);
    }
    m.newGroup(40.f);
    wheelFender(m, vec3(0, yRw, R), R + 0.06f, 0.24f, 0.9f, 2.95f, 0.10f, MAT_CARPAINT, kCol1);
    wheelFender(m, axleF, R + 0.05f, 0.18f, 0.25f, 2.3f, 0.08f, MAT_CARPAINT, kCol1);
    // saddlebags
    m.use(MAT_LEATHER, col(0.45f, 0.45f, 0.45f));
    for (int s = -1; s <= 1; s += 2) roundedBoxAt(m, vec3(s * 0.27f, -0.72f, 0.58f), vec3(0.09f, 0.25f, 0.16f), 0.06f, 2);
    // --- forks, headlight, bars
    m.newGroup(40.f);
    for (int s = -1; s <= 1; s += 2) {
        vec3 off(s * 0.11f, 0, 0);
        m.use(MAT_CHROME, kCol1);
        cyl(m, axleF + off, topClamp + off, 0.022f, 10);
        m.use(MAT_CARPAINT, kCol1);
        cyl(m, axleF + forkDir * 0.48f + off, topClamp + off, 0.042f, 12);
        m.use(MAT_CHROME, kCol1);
        std::vector<vec3> hb;
        hb.push_back(topClamp + vec3(s * 0.06f, 0.02f, 0.04f));
        hb.push_back(topClamp + vec3(s * 0.12f, 0.0f, 0.16f));
        hb.push_back(topClamp + vec3(s * 0.40f, -0.08f, 0.18f));
        hb.push_back(topClamp + vec3(s * 0.42f, -0.20f, 0.14f));
        tube1(m, catmull(hb, 3), 0.013f, 7, true);
        m.use(MAT_RUBBER, kCol1);
        cyl(m, topClamp + vec3(s * 0.42f, -0.12f, 0.16f), topClamp + vec3(s * 0.42f, -0.24f, 0.13f), 0.018f, 8);
        m.use(MAT_CHROME, kCol1);
        cyl(m, topClamp + vec3(s * 0.25f, -0.05f, 0.18f), topClamp + vec3(s * 0.33f, -0.05f, 0.30f), 0.007f, 5);
        roundedBoxAt(m, topClamp + vec3(s * 0.345f, -0.05f, 0.33f), vec3(0.05f, 0.015f, 0.035f), 0.02f, 1);
        // floorboards
        m.use(MAT_RUBBER, kCol1);
        roundedBoxAt(m, vec3(s * 0.28f, 0.22f, 0.36f), vec3(0.07f, 0.16f, 0.012f), 0.01f, 1);
        m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.55f, 0.05f, 1.f));
        roundedBoxAt(m, vec3(s * 0.2f, head.y + 0.05f, head.z - 0.06f), vec3(0.03f, 0.03f, 0.03f), 0.02f, 1);
        roundedBoxAt(m, vec3(s * 0.2f, -1.1f, 0.62f), vec3(0.025f, 0.025f, 0.025f), 0.015f, 1);
    }
    m.use(MAT_CHROME, kCol1);
    roundedBox(m, Frame(topClamp, vec3(1, 0, 0), forkDir, cross(vec3(1, 0, 0), forkDir)), vec3(0.15f, 0.06f, 0.02f), 0.01f, 1);
    vec3 hlc = head + vec3(0, 0.14f, -0.02f);
    std::vector<vec2> bucket;
    bucket.push_back(vec2(-0.14f, 0.02f)); bucket.push_back(vec2(-0.10f, 0.09f)); bucket.push_back(vec2(0.0f, 0.115f)); bucket.push_back(vec2(0.03f, 0.115f));
    lathe(m, hlc, vec3(0, 1, 0), vec3(1, 0, 0), bucket, 20);
    m.use(MAT_LIGHT_HEAD, kCol1);
    {
        std::vector<vec2> lens;
        lens.push_back(vec2(0.03f, 0.108f)); lens.push_back(vec2(0.05f, 0.08f)); lens.push_back(vec2(0.058f, 0.0f));
        lathe(m, hlc, vec3(0, 1, 0), vec3(1, 0, 0), lens, 20);
    }
    m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));
    roundedBox(m, Frame(vec3(0, -1.12f, 0.66f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0.2f)), vec3(0.07f, 0.025f, 0.012f), 0.01f, 1);
    finalizeMesh(m, o.body);
    WheelDesign wd;
    wd.R = R; wd.W = W; wd.rimR = 0.203f; wd.moto = true; wd.style = RIM_WIRE; wd.lipTint = vec3(1.f); wd.lugs = 0; wd.seg = 28;
    buildWheel(wd, o.wheel);
    bikeMeta(o, yFw, yRw, R, W, hlc + vec3(0, 0.06f, 0), vec3(0, -1.12f, 0.66f), vec3(0, -0.28f, 0.74f), vec3(0, -0.6f, 0.86f), true);
    o.boxCenter = vec3(0, -0.1f, 0.6f);
    o.boxHalf = vec3(0.45f, 1.2f, 0.56f);
    physics(o, 340.f, 72.f, 155.f, 5600.f, 55.f, 6, 0.f, 1.05f, 0.11f, 1.0f, 0.75f, 0.f, vec3(0, -0.05f, 0.52f), Audio::ENGINE_BIKE_CRUISER);
    o.frontalArea = 0.8f;
    o.brakeForce = 3600.f;
    o.paletteColors = palette("muscle");
    o.spawnWeight = 1.2f; o.price = 24000;
}

inline void mdlMochi(VehicleModel& o) {
    o.name = "Mochi 125"; o.maker = "Sakaki"; o.cls = VC_SCOOTER;
    PMesh m;
    const float R = 0.23f, W = 0.11f, yFw = 0.66f, yRw = -0.64f;
    vec3 axleF(0, yFw, R);
    vec3 forkDir = normalize(vec3(0, -sinf(26.f * kDegToRad), cosf(26.f * kDegToRad)));
    vec3 top = axleF + forkDir * 0.78f;
    m.newGroup(40.f);
    m.use(MAT_CARPAINT, kCol1);
    // front shield (curved) and apron
    {
        std::vector<LoftSec> sh;  // along z (use frame), sections are wide flat ellipses
        sh.push_back({0.0f, 0, 0.f, 0.17f, 0.04f, 2.6f});
        sh.push_back({0.25f, 0, 0.01f, 0.22f, 0.06f, 2.6f});
        sh.push_back({0.50f, 0, 0.0f, 0.23f, 0.06f, 2.6f});
        sh.push_back({0.62f, 0, -0.02f, 0.14f, 0.05f, 2.4f});
        Frame f = frameFY(vec3(0, 0.50f, 0.30f), normalize(vec3(0, 0.16f, 1.f)), vec3(0, 1, 0));
        loftFrame(m, f, sh, 16, true, true);
    }
    // floorboard
    m.use(MAT_RUBBER, kCol1);
    roundedBoxAt(m, vec3(0, 0.12f, 0.31f), vec3(0.16f, 0.3f, 0.025f), 0.02f, 1);
    m.use(MAT_CARPAINT, kCol1);
    roundedBoxAt(m, vec3(0, 0.12f, 0.26f), vec3(0.17f, 0.31f, 0.04f), 0.03f, 1);
    // rear body
    {
        std::vector<LoftSec> t;
        t.push_back({-0.12f, 0, 0.38f, 0.13f, 0.08f, 2.4f});
        t.push_back({-0.25f, 0, 0.50f, 0.19f, 0.18f, 2.6f});
        t.push_back({-0.50f, 0, 0.56f, 0.21f, 0.20f, 2.6f});
        t.push_back({-0.74f, 0, 0.62f, 0.16f, 0.14f, 2.4f});
        t.push_back({-0.86f, 0, 0.66f, 0.07f, 0.07f, 2.2f});
        loftY(m, t, 16, true, true);
    }
    // seat
    m.newGroup(45.f);
    m.use(MAT_LEATHER, col(0.55f, 0.45f, 0.4f));
    {
        std::vector<LoftSec> t;
        t.push_back({-0.12f, 0, 0.77f, 0.12f, 0.04f, 2.5f});
        t.push_back({-0.28f, 0, 0.78f, 0.17f, 0.05f, 2.8f});
        t.push_back({-0.58f, 0, 0.81f, 0.15f, 0.05f, 2.8f});
        t.push_back({-0.68f, 0, 0.82f, 0.10f, 0.04f, 2.5f});
        loftY(m, t, 12, true, true);
    }
    // headset with headlight, handlebars, mirrors
    m.newGroup(40.f);
    m.use(MAT_CARPAINT, kCol1);
    roundedBox(m, Frame(top + vec3(0, 0.02f, 0.02f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), vec3(0.14f, 0.1f, 0.06f), 0.05f, 2);
    m.use(MAT_LIGHT_HEAD, kCol1);
    ellipsoid(m, Frame(top + vec3(0, 0.115f, 0.01f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0)), vec3(0.06f, 0.04f, 0.02f), 14, 4, 0.f, kHalfPi);
    m.use(MAT_PLASTIC, col(0.3f, 0.3f, 0.3f));
    roundedBox(m, Frame(top + vec3(0, -0.07f, 0.07f), vec3(1, 0, 0), vec3(0, 0, 1), normalize(vec3(0, -1, 0.5f))), vec3(0.07f, 0.04f, 0.005f), 0.01f, 1);
    for (int s = -1; s <= 1; s += 2) {
        m.use(MAT_METAL_BRUSHED, col(0.6f, 0.6f, 0.62f));
        cyl(m, top + vec3(s * 0.1f, 0, 0.02f), top + vec3(s * 0.32f, -0.04f, 0.0f), 0.011f, 6);
        m.use(MAT_RUBBER, kCol1);
        cyl(m, top + vec3(s * 0.24f, -0.03f, 0.005f), top + vec3(s * 0.35f, -0.05f, -0.003f), 0.017f, 8);
        m.use(MAT_CHROME, kCol1);
        cyl(m, top + vec3(s * 0.16f, 0, 0.04f), top + vec3(s * 0.25f, -0.02f, 0.19f), 0.006f, 5);
        roundedBoxAt(m, top + vec3(s * 0.26f, -0.02f, 0.22f), vec3(0.045f, 0.012f, 0.03f), 0.02f, 1);
        m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.55f, 0.05f, 1.f));
        roundedBoxAt(m, vec3(s * 0.16f, 0.66f, 0.80f), vec3(0.025f, 0.02f, 0.015f), 0.01f, 1);
        roundedBoxAt(m, vec3(s * 0.12f, -0.86f, 0.62f), vec3(0.02f, 0.015f, 0.012f), 0.008f, 1);
        // fork legs
        m.use(MAT_METAL_PAINTED, col(0.2f, 0.2f, 0.2f));
        cyl(m, axleF + vec3(s * 0.07f, 0, 0), axleF + forkDir * 0.42f + vec3(s * 0.07f, 0, 0), 0.02f, 8);
    }
    m.use(MAT_METAL_PAINTED, col(0.2f, 0.2f, 0.2f));
    cyl(m, axleF + forkDir * 0.40f, top, 0.025f, 8);
    // front fender
    wheelFender(m, axleF, R + 0.035f, 0.12f, 0.4f, 1.9f, 0.02f, MAT_CARPAINT, kCol1);
    // engine / CVT case (left) + exhaust (right) + rack + tail light
    m.use(MAT_METAL_PAINTED, col(0.35f, 0.35f, 0.37f));
    roundedBoxAt(m, vec3(-0.10f, -0.48f, 0.30f), vec3(0.05f, 0.24f, 0.10f), 0.05f, 2);
    m.use(MAT_METAL_PAINTED, col(0.15f, 0.15f, 0.15f));
    roundedBoxAt(m, vec3(0.0f, -0.28f, 0.32f), vec3(0.09f, 0.12f, 0.1f), 0.04f, 1);
    {
        std::vector<vec3> ex;
        ex.push_back(vec3(0.04f, -0.2f, 0.25f));
        ex.push_back(vec3(0.12f, -0.4f, 0.28f));
        ex.push_back(vec3(0.14f, -0.72f, 0.36f));
        m.use(MAT_METAL_PAINTED, col(0.12f, 0.12f, 0.12f));
        std::vector<float> rr;
        rr.push_back(0.02f); rr.push_back(0.045f); rr.push_back(0.04f);
        tube(m, catmull(ex, 3), std::vector<float>(1, 0.035f), 10, true, true);
        m.use(MAT_CHROME, kCol1);
        roundedBoxAt(m, vec3(0.175f, -0.55f, 0.33f), vec3(0.01f, 0.1f, 0.03f), 0.01f, 1);
    }
    m.use(MAT_CHROME, kCol1);
    for (int s = -1; s <= 1; s += 2) cyl(m, vec3(s * 0.1f, -0.66f, 0.83f), vec3(s * 0.1f, -0.9f, 0.84f), 0.009f, 6);
    cyl(m, vec3(-0.1f, -0.9f, 0.84f), vec3(0.1f, -0.9f, 0.84f), 0.009f, 6);
    m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));
    roundedBox(m, Frame(vec3(0, -0.875f, 0.66f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0)), vec3(0.06f, 0.025f, 0.012f), 0.01f, 1);
    m.use(MAT_METAL_PAINTED, col(0.9f, 0.9f, 0.86f));
    roundedBox(m, Frame(vec3(0, -0.88f, 0.55f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0)), vec3(0.085f, 0.05f, 0.003f), 0.004f, 1);
    bikePlateText(m, vec3(0, -0.88f, 0.55f), vec3(0, 0, 1), o.name);
    finalizeMesh(m, o.body);
    WheelDesign wd;
    wd.R = R; wd.W = W; wd.rimR = 0.152f; wd.moto = true; wd.style = RIM_BIKE; wd.spokes = 5; wd.spokeHub = 0.012f; wd.spokeRim = 0.01f;
    wd.faceTint = vec3(0.8f); wd.lugs = 0; wd.seg = 24;
    buildWheel(wd, o.wheel);
    bikeMeta(o, yFw, yRw, R, W, top + vec3(0, 0.13f, 0), vec3(0, -0.88f, 0.66f), vec3(0, -0.3f, 0.79f), vec3(0, -0.6f, 0.83f), true);
    o.boxCenter = vec3(0, -0.05f, 0.55f);
    o.boxHalf = vec3(0.34f, 0.95f, 0.55f);
    physics(o, 118.f, 9.f, 12.f, 8500.f, 27.f, 1, 0.f, 0.95f, 0.09f, 0.9f, 0.75f, 0.f, vec3(0, -0.1f, 0.45f), Audio::ENGINE_SCOOTER);
    o.frontalArea = 0.65f;
    o.brakeForce = 1400.f;
    o.paletteColors = palette("scooter");
    o.spawnWeight = 2.f; o.price = 3200;
}

}  // namespace detail
}  // namespace Vehicles
