// Heavy / commercial vehicles: vans, step van, box truck, semi tractor, garbage truck, ambulance, fire truck, bus.
// Cabs and bodies reuse the lofted body (cab-only lofts disable the rear arch); cargo boxes, chassis parts,
// glazing and liveries are built as flat panels or surface-projected patches.
namespace Vehicles {
namespace detail {

inline WheelDesign truckWheel(float R, float W) {
    WheelDesign w;
    w.R = R;
    w.W = W;
    w.rimR = R * 0.56f;
    w.style = RIM_TRUCK;
    w.lugs = 10;
    w.seg = 28;
    w.faceTint = vec3(0.82f, 0.83f, 0.85f);
    w.capTint = vec3(0.6f);
    return w;
}
inline void wheelPair(VehicleModel& o, float y, float track, float R, float W, bool steer, bool drive) {
    o.wheels.push_back(WheelSpec{vec3(-track, y, R), R, W, steer, drive, true});
    o.wheels.push_back(WheelSpec{vec3(track, y, R), R, W, steer, drive, false});
}
inline void dualAxle(VehicleModel& o, float y, float track, float R, float W, bool drive) {
    float off = W * 0.5f + 0.012f;
    wheelPair(o, y, track + off, R, W, false, drive);
    wheelPair(o, y, track - off, R, W, false, drive);
}

// Glazing / panels projected onto flat-ish body sides
inline void glassPatch(PMesh& m, const Decal& dc, const std::vector<vec2>& O, float border = 0.022f) {
    LampStyle st;
    st.height = 0.006f;
    st.bezel = border;
    st.wallMat = MAT_PLASTIC;
    st.wallCol = col(0.4f, 0.4f, 0.4f);
    st.bezelMat = MAT_PLASTIC;
    st.bezelCol = col(0.3f, 0.3f, 0.3f);
    st.floorMat = MAT_CAR_GLASS;
    st.floorCol = kCol1;
    st.floorOff = 0.005f;
    st.rings = 3;
    lampHousing(m, dc, O, st);
}
inline Decal sideDecal(CarBody& b, bool left, vec2 mn, vec2 mx) {
    Decal dc;
    dc.pr = &b.proj;
    dc.fr = left ? projLeft() : projRight();
    dc.back = 4.f;
    decalRange(dc, mn, mx);
    return dc;
}
inline Decal endDecal(CarBody& b, bool rear, vec2 mn, vec2 mx) {
    Decal dc;
    dc.pr = &b.proj;
    dc.fr = rear ? projRear() : projFront();
    dc.fr.o = vec3(0, 0, 0);
    dc.back = 20.f;
    decalRange(dc, mn, mx);
    return dc;
}
// Rectangle outline in plane coords
inline std::vector<vec2> rectO(float u0, float u1, float v0, float v1, float r) {
    return resampleClosed(shapeRoundRect(vec2((u0 + u1) * 0.5f, (v0 + v1) * 0.5f), fabsf(u1 - u0) * 0.5f, fabsf(v1 - v0) * 0.5f, r, 3), 28);
}

// Chassis bits
inline void frameRails(PMesh& m, float y0, float y1, float x, float z0, float h) {
    m.newGroup(30.f);
    m.use(MAT_METAL_PAINTED, col(0.07f, 0.07f, 0.08f));
    for (int s = -1; s <= 1; s += 2) roundedBoxAt(m, vec3(s * x, (y0 + y1) * 0.5f, z0 + h * 0.5f), vec3(0.045f, (y0 - y1) * 0.5f, h * 0.5f), 0.004f, 1);
    int n = (int)((y0 - y1) / 1.3f);
    for (int k = 1; k < n; k++) roundedBoxAt(m, vec3(0, lerp(y0, y1, (float)k / n), z0 + h * 0.5f), vec3(x, 0.035f, h * 0.35f), 0.004f, 1);
}
inline void tankY(PMesh& m, vec3 c, float len, float r, bool chrome) {
    m.newGroup(40.f);
    m.use(chrome ? MAT_CHROME : MAT_METAL_PAINTED, chrome ? kCol1 : col(0.15f, 0.15f, 0.16f));
    std::vector<vec2> p;
    float h = len * 0.5f;
    p.push_back(vec2(-h, 0.f));
    p.push_back(vec2(-h, r * 0.8f));
    p.push_back(vec2(-h + 0.03f, r));
    p.push_back(vec2(h - 0.03f, r));
    p.push_back(vec2(h, r * 0.8f));
    p.push_back(vec2(h, 0.f));
    lathe(m, c, vec3(0, 1, 0), vec3(1, 0, 0), p, 20);
    m.use(MAT_PLASTIC, col(0.4f, 0.4f, 0.4f));
    for (int s = -1; s <= 1; s += 2) torus(m, Frame(c + vec3(0, s * h * 0.6f, 0), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0)), r + 0.004f, 0.008f, 20, 4);
}
// Mud flap behind a wheel
inline void mudFlap(PMesh& m, float x, float y, float w, float zTop) {
    m.newGroup(30.f);
    m.use(MAT_RUBBER, kCol1);
    roundedBoxAt(m, vec3(x, y, zTop * 0.5f + 0.08f), vec3(w * 0.5f, 0.006f, zTop * 0.5f - 0.06f), 0.004f, 1);
}
// Half fender over a wheel: arc band of radius R around the axle (x = centre of band)
inline void fenderArc(PMesh& m, float x, float yw, float zw, float R, float w, float a0, float a1, u8 mat, u32 c) {
    m.newGroup(35.f);
    m.use(mat, c);
    std::vector<vec2> prof;  // lathe around the axle (+x): a = x offset, r = radius
    prof.push_back(vec2(-w * 0.5f, R - 0.02f));
    prof.push_back(vec2(-w * 0.5f, R));
    prof.push_back(vec2(w * 0.5f, R));
    prof.push_back(vec2(w * 0.5f, R - 0.02f));
    lathe(m, vec3(x, yw, zw), vec3(1, 0, 0), vec3(0, 1, 0), prof, 14, a0, a1);
    // underside (visible from below / behind)
    std::vector<vec2> in;
    in.push_back(vec2(w * 0.5f, R - 0.02f));
    in.push_back(vec2(-w * 0.5f, R - 0.02f));
    lathe(m, vec3(x, yw, zw), vec3(1, 0, 0), vec3(0, 1, 0), in, 14, a0, a1);
}
// Large truck mirrors on arms (both sides)
inline void truckMirrors(PMesh& m, float y, float xDoor, float z, float reach) {
    m.newGroup(40.f);
    for (int s = -1; s <= 1; s += 2) {
        m.use(MAT_METAL_PAINTED, col(0.1f, 0.1f, 0.1f));
        cyl(m, vec3(s * xDoor, y, z + 0.35f), vec3(s * (xDoor + reach), y + 0.05f, z + 0.35f), 0.012f, 6);
        cyl(m, vec3(s * xDoor, y, z - 0.25f), vec3(s * (xDoor + reach), y + 0.05f, z - 0.25f), 0.012f, 6);
        m.use(MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
        roundedBoxAt(m, vec3(s * (xDoor + reach + 0.03f), y + 0.05f, z + 0.05f), vec3(0.05f, 0.03f, 0.25f), 0.02f, 1);
        m.use(MAT_CHROME, col(0.8f, 0.85f, 0.9f));
        roundedBox(m, Frame(vec3(s * (xDoor + reach + 0.03f), y + 0.017f, z + 0.05f), vec3(1, 0, 0), vec3(0, 0, 1), vec3(0, -1, 0)), vec3(0.042f, 0.235f, 0.003f), 0.015f, 1);
    }
}
// Rounded cargo box with roll-up door, corner posts and marker lights; flat faces for liveries
inline void cargoBox(PMesh& m, float yF, float yR, float hw, float z0, float z1, u8 mat, u32 c, bool rollDoor, bool markers = true) {
    float cy = (yF + yR) * 0.5f, hl = (yF - yR) * 0.5f, cz = (z0 + z1) * 0.5f, hh = (z1 - z0) * 0.5f;
    m.newGroup(30.f);
    m.use(mat, c);
    roundedBoxAt(m, vec3(0, cy, cz), vec3(hw, hl, hh), 0.035f, 2);
    // aluminium corner posts & rails
    m.use(MAT_METAL_BRUSHED, col(0.7f, 0.7f, 0.72f));
    for (int s = -1; s <= 1; s += 2) {
        roundedBoxAt(m, vec3(s * (hw - 0.02f), yR + 0.03f, cz), vec3(0.035f, 0.045f, hh + 0.005f), 0.01f, 1);
        roundedBoxAt(m, vec3(s * (hw + 0.004f), cy, z0 + 0.05f), vec3(0.012f, hl, 0.05f), 0.004f, 1);
        roundedBoxAt(m, vec3(s * (hw + 0.004f), cy, z1 - 0.03f), vec3(0.012f, hl, 0.03f), 0.004f, 1);
    }
    roundedBoxAt(m, vec3(0, yR - 0.005f, z1 - 0.04f), vec3(hw, 0.012f, 0.045f), 0.004f, 1);
    roundedBoxAt(m, vec3(0, yR - 0.005f, z0 + 0.06f), vec3(hw, 0.012f, 0.06f), 0.004f, 1);
    if (rollDoor) {
        m.use(MAT_PLASTIC, col(0.4f, 0.4f, 0.4f));
        int n = (int)((z1 - z0 - 0.2f) / 0.28f);
        for (int k = 1; k <= n; k++) {
            float z = z0 + 0.12f + k * 0.28f;
            roundedBoxAt(m, vec3(0, yR - 0.004f, z), vec3(hw - 0.07f, 0.005f, 0.006f), 0.f, 1);
        }
        m.use(MAT_CHROME, kCol1);
        roundedBoxAt(m, vec3(0, yR - 0.02f, z0 + 0.25f), vec3(0.12f, 0.02f, 0.02f), 0.008f, 1);
    }
    if (markers) {
        m.newGroup(40.f);
        m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.55f, 0.05f, 1.f));
        for (int s = -1; s <= 1; s += 2)
            for (int k = 0; k < 3; k++) {
                float y = lerp(yF - 0.1f, yR + 0.15f, k / 2.f);
                roundedBoxAt(m, vec3(s * (hw + 0.012f), y, z1 - 0.08f), vec3(0.01f, 0.04f, 0.02f), 0.006f, 1);
            }
        m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));
        for (int k = 0; k < 3; k++) roundedBoxAt(m, vec3((k - 1) * 0.12f, yR - 0.02f, z1 - 0.1f), vec3(0.035f, 0.012f, 0.018f), 0.006f, 1);
    }
}
// Rear lamp cluster on a flat rear face (right side, mirrored by caller using x sign)
inline void rearLampBlock(PMesh& m, float x, float y, float z, float w, float h) {
    m.newGroup(40.f);
    m.use(MAT_PLASTIC, col(0.4f, 0.4f, 0.4f));
    roundedBoxAt(m, vec3(x, y + 0.01f, z), vec3(w * 0.5f + 0.012f, 0.015f, h * 0.5f + 0.012f), 0.01f, 1);
    m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));
    roundedBoxAt(m, vec3(x, y - 0.006f, z + h * 0.2f), vec3(w * 0.5f, 0.008f, h * 0.3f), 0.01f, 1);
    m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.55f, 0.05f, 1.f));
    roundedBoxAt(m, vec3(x, y - 0.006f, z - h * 0.18f), vec3(w * 0.5f, 0.008f, h * 0.12f), 0.008f, 1);
    m.use(MAT_LIGHT_TAIL, col(1.f, 1.f, 1.f));
    roundedBoxAt(m, vec3(x, y - 0.006f, z - h * 0.4f), vec3(w * 0.5f, 0.008f, h * 0.09f), 0.008f, 1);
}
// Emergency light bar in 3D: segments alternating colours; siren colour in vertex rgb (alpha 0 = siren lens)
inline void lightBar3D(PMesh& m, vec3 c, float halfLen, float depth, int segs, vec3 colA, vec3 colB, bool white = true) {
    m.newGroup(40.f);
    m.use(MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
    roundedBoxAt(m, c - vec3(0, 0, 0.035f), vec3(halfLen + 0.01f, depth * 0.5f + 0.01f, 0.02f), 0.01f, 1);
    for (int k = 0; k < segs; k++) {
        float x0 = -halfLen + 2.f * halfLen * k / segs + 0.01f, x1 = -halfLen + 2.f * halfLen * (k + 1) / segs - 0.01f;
        bool left = (x0 + x1) < 0.f;
        bool mid = white && (k == segs / 2 || k == segs / 2 - 1) && segs >= 4;
        vec3 cc = mid ? vec3(1.f) : (left ? colA : colB);
        m.use(MAT_LIGHT_INDICATOR, colv(cc, 0.f));
        roundedBoxAt(m, vec3((x0 + x1) * 0.5f, c.y, c.z + 0.01f), vec3((x1 - x0) * 0.5f, depth * 0.5f, 0.045f), 0.02f, 1);
    }
}
inline void flatText(PMesh& m, vec3 c, vec3 right, vec3 up, const char* t, float h, u8 mat, u32 cl) {
    m.newGroup(40.f);
    m.use(mat, cl);
    strokeText3D(m, c, right, up, t, h, 0.004f);
}
// Seats (driver left) inside a cab, simple
inline void cabSeats(PMesh& m, float yHip, float zFloor, float hipH, float seatX, bool bench) {
    CarLook L;
    L.seatTint = vec3(0.12f, 0.12f, 0.13f);
    PMesh::Mark mk = m.mark();
    seat(m, vec3(seatX, yHip, zFloor + hipH), 0.25f, L, !bench, 0.25f);
    m.mirrorX(mk);
}

inline void heavyLights(VehicleModel& o, vec3 head, vec3 tail) {
    for (int sg = -1; sg <= 1; sg += 2) {
        vec3 h(head.x * sg, head.y, head.z), t(tail.x * sg, tail.y, tail.z);
        addLight(o, h, vec3(0, 1, -0.03f), LT_HEAD);
        addLight(o, t, vec3(0, -1, 0), LT_TAIL);
        addLight(o, t, vec3(0, -1, 0), LT_BRAKE);
        addLight(o, t + vec3(0, 0, -0.1f), vec3(0, -1, 0), LT_REVERSE);
        addLight(o, h + vec3(sg * 0.12f, 0, -0.08f), vec3(sg * 0.3f, 1, 0), sg < 0 ? LT_INDICATOR_L : LT_INDICATOR_R);
        addLight(o, t + vec3(0, 0, -0.05f), vec3(sg * 0.3f, -1, 0), sg < 0 ? LT_INDICATOR_L : LT_INDICATOR_R);
    }
}
inline void setBox(VehicleModel& o, float zMin) {
    AABB bb = o.body.bounds;
    o.boxCenter = vec3(0, (bb.mn.y + bb.mx.y) * 0.5f, (zMin + bb.mx.z) * 0.5f);
    o.boxHalf = vec3((bb.mx.x - bb.mn.x) * 0.5f, (bb.mx.y - bb.mn.y) * 0.5f, (bb.mx.z - zMin) * 0.5f);
}

// ------------------------------------------------------------------------------------------------
// Van-style cab front shared by the cargo van and the ambulance
inline void vanFront(CarDef& d, float wb, float foh, float roh, float W, float R) {
    CarSpec& s = d.s;
    s.style = BS_BOXY;
    s.doors = 2;
    s.wb = wb; s.foh = foh; s.roh = roh;
    s.halfW = W * 0.5f; s.wheelR = R; s.wheelW = 0.235f; s.trackF = s.halfW - 0.16f; s.trackR = s.trackF; s.archGap = 0.05f;
    s.zSill = 0.44f; s.zNoseTop = 0.84f; s.zNoseBot = 0.44f; s.zChin = 0.38f; s.zHoodF = 1.12f; s.hoodEdgeBack = 0.14f; s.noseRound = 0.35f;
    float yF = wb * 0.5f + foh;
    s.yCowl = yF - 0.98f; s.zCowl = 1.24f; s.zBeltF = 1.20f; s.zBeltR = 1.24f;
    s.frontD = 0.36f; s.frontExp = 3.2f; s.rearD = 0.12f; s.rearExp = 7.f;
    s.tuLow = 0.02f; s.tuTop = 0.02f; s.zWide = 0.8f; s.lean = 0.08f; s.shR = 0.03f; s.shRx = 0.03f; s.railR = 0.10f; s.ghInset = 0.02f;
    s.roofCrown = 0.03f; s.crownHood = 0.03f; s.fenderDrop = 0.04f;
    s.zChar = 0.f; s.charOut = 0.f; s.flareOut = 0.f;
    s.rearGlass = false;
    CarLook& L = d.L;
    L.head = HL_PROJ; L.headC = vec2(s.halfW * 0.72f, 1.0f); L.headW = 0.17f; L.headH = 0.07f; L.headYaw = 0.45f; L.headDomes = 2;
    L.grille = GR_HBAR; L.grilleChrome = false; L.grilleBars = 3; L.grilleTop = 1.04f; L.grilleBot = 0.86f; L.grilleW = s.halfW * 0.44f;
    L.intakeW = s.halfW * 0.5f; L.intakeTop = 0.62f; L.intakeBot = 0.50f; L.fogs = false; L.plateFZ = 0.66f;
    L.blackBumpers = true; L.bumperFZ = 0.55f; L.bumperRZ = 0.55f; L.antennaFin = false; L.mirrorsBlack = true; L.exhaust = 1;
    InteriorLayout& I = d.I;
    I.zFloor = 0.60f; I.hipH = 0.38f; I.yHipF = s.yCowl - 1.0f; I.rearSeat = false; I.dashY0 = s.yCowl - 0.05f; I.dashY1 = s.yCowl - 0.52f;
    I.seatX = s.halfW * 0.44f;
    d.wd = WheelDesign();
    d.wd.R = R; d.wd.W = 0.235f; d.wd.rimR = 0.203f; d.wd.style = RIM_STEEL; d.wd.hubcap = true; d.wd.lugs = 5;
    d.carSeams = false; d.fuelDoor = false;
}

inline void mdlStevedore(VehicleModel& o) {
    o.name = "Stevedore"; o.maker = "Dunmore"; o.cls = VC_VAN;
    CarDef d;
    vanFront(d, 3.66f, 1.04f, 1.23f, 2.02f, 0.365f);
    CarSpec& s = d.s;
    float yR = -(s.wb * 0.5f + s.roh);
    s.zRoof = 2.55f;
    s.yRoofF = s.yCowl - 0.78f;
    s.yRoofR = yR + 0.06f;
    s.yDeck = yR + 0.035f; s.zDeck = 1.26f; s.zTail = 1.24f; s.tailEdgeFwd = 0.02f; s.zTailTop = 1.22f; s.zTailBot = 0.52f; s.zRearLow = 0.45f;
    s.roofKeys = {vec2(s.yDeck, s.zDeck), vec2(yR + 0.06f, 2.48f), vec2(yR + 0.25f, 2.55f), vec2(s.yRoofF - 0.55f, 2.55f),
                  vec2(s.yRoofF - 0.18f, 2.30f), vec2(s.yRoofF, 2.02f), vec2((s.yRoofF + s.yCowl) * 0.5f, 1.66f), vec2(s.yCowl, s.zCowl)};
    s.dloFront = s.yCowl - 0.06f;
    s.bPillar = -100.f;
    s.dloRearBot = s.yRoofF - 0.62f; s.dloRearTop = s.yRoofF - 0.62f;
    d.L.tail = TL_VERT; d.L.tailC = vec2(s.halfW * 0.9f, 1.02f); d.L.tailW = 0.06f; d.L.tailH = 0.2f; d.L.tailYaw = 1.0f;
    d.L.plateRZ = 0.72f;
    d.extra = [](CarBody& b, PMesh& m) {
        const CarSpec& s = b.s;
        m.newGroup(40.f);
        // sliding door (right) and rear barn doors: seams
        Frame fr = projRight();
        float y0 = s.dloRearBot - 0.05f, y1 = y0 - 1.30f;
        std::vector<vec2> sd;
        sd.push_back(vec2(y0, s.zSill + 0.08f)); sd.push_back(vec2(y0, 2.22f)); sd.push_back(vec2(y1, 2.22f)); sd.push_back(vec2(y1, s.zSill + 0.08f));
        seam(m, b.proj, fr, sd, 0.005f);
        for (int side = 0; side < 2; side++) {
            std::vector<vec2> fd;
            fd.push_back(vec2(s.dloFront + 0.04f, s.zSill + 0.10f));
            fd.push_back(vec2(s.dloRearBot + 0.02f, s.zSill + 0.10f));
            fd.push_back(vec2(s.dloRearBot + 0.02f, b.beltZAt(s.dloRearBot) + 0.4f));
            seam(m, b.proj, side == 0 ? projRight() : projLeft(), fd, 0.005f);
        }
        Frame rr = projRear();
        std::vector<vec2> rd;
        rd.push_back(vec2(0, 0.62f)); rd.push_back(vec2(0, 2.38f));
        seam(m, b.proj, rr, rd, 0.006f);
        // rear door windows
        Decal dc = endDecal(b, true, vec2(-0.9f, 1.5f), vec2(0.9f, 2.3f));
        for (int sg = -1; sg <= 1; sg += 2) glassPatch(m, dc, rectO(sg * 0.08f, sg * 0.78f, 1.62f, 2.20f, 0.05f));
        // door handles (rear)
        m.use(MAT_PLASTIC, col(0.4f, 0.4f, 0.4f));
        roundedBoxAt(m, vec3(0.1f, b.yR - 0.01f, 1.25f), vec3(0.06f, 0.015f, 0.015f), 0.005f, 1);
        // roof ribs
        m.use(MAT_CARPAINT, kCol1);
        for (int k = 0; k < 5; k++) {
            float y = lerp(b.s.yRoofF - 0.6f, b.yR + 0.4f, k / 4.f);
            roundedBoxAt(m, vec3(0, y, b.roofZAt(y) + 0.005f), vec3(b.railXAt(y) - 0.02f, 0.03f, 0.012f), 0.01f, 1);
        }
        // partition behind the seats
        m.use(MAT_INTERIOR, kCol1);
        roundedBoxAt(m, vec3(0, s.yCowl - 1.45f, 1.5f), vec3(b.s.halfW - 0.08f, 0.02f, 0.9f), 0.01f, 1);
    };
    physics(o, 2350.f, 140.f, 360.f, 4200.f, 44.f, 6, 0.f, 0.92f, 0.20f, 1.2f, 0.37f, 0.f, vec3(0, 0.3f, 0.85f), Audio::ENGINE_TRUCK_DIESEL);
    buildCar(d, o);
    o.paletteColors = palette("fleet");
    o.spawnWeight = 4.f; o.price = 42000;
    o.frontalArea = 2.02f * 2.2f * 0.9f;
    o.seats.resize(2);
}

// Step van (delivery)
inline void mdlParcel(VehicleModel& o) {
    o.name = "Parcel P70"; o.maker = "Dunmore"; o.cls = VC_SERVICE;
    CarDef d;
    CarSpec& s = d.s;
    s.style = BS_BOXY;
    s.doors = 2;
    s.wb = 4.0f; s.foh = 0.95f; s.roh = 1.75f;
    s.halfW = 1.18f; s.wheelR = 0.43f; s.wheelW = 0.245f; s.trackF = 1.0f; s.trackR = 0.86f; s.archGap = 0.06f;
    float yF = s.wb * 0.5f + s.foh, yR = -(s.wb * 0.5f + s.roh);
    s.zSill = 0.55f; s.zNoseTop = 1.0f; s.zNoseBot = 0.55f; s.zChin = 0.48f; s.zHoodF = 1.30f; s.hoodEdgeBack = 0.12f; s.noseRound = 0.3f;
    s.yCowl = yF - 0.62f; s.zCowl = 1.42f; s.zBeltF = 1.40f; s.zBeltR = 1.42f;
    s.zRoof = 2.95f;
    s.yRoofF = s.yCowl - 0.26f; s.yRoofR = yR + 0.05f;
    s.yDeck = yR + 0.03f; s.zDeck = 1.42f; s.zTail = 1.40f; s.tailEdgeFwd = 0.02f; s.zTailTop = 1.38f; s.zTailBot = 0.62f; s.zRearLow = 0.56f;
    s.roofKeys = {vec2(s.yDeck, s.zDeck), vec2(yR + 0.05f, 2.88f), vec2(yR + 0.3f, 2.95f), vec2(s.yCowl - 0.55f, 2.95f),
                  vec2(s.yCowl - 0.30f, 2.64f), vec2(s.yRoofF, 2.52f), vec2(s.yCowl - 0.10f, 1.95f), vec2(s.yCowl, s.zCowl)};
    s.frontD = 0.30f; s.frontExp = 5.f; s.rearD = 0.10f; s.rearExp = 8.f;
    s.tuLow = 0.02f; s.tuTop = 0.01f; s.zWide = 1.0f; s.lean = 0.02f; s.shR = 0.03f; s.shRx = 0.03f; s.railR = 0.14f; s.ghInset = 0.02f;
    s.zChar = 0.f; s.charOut = 0.f; s.flareOut = 0.f; s.cowlLen = 0.05f;
    s.rearGlass = false;
    s.dloFront = s.yCowl - 0.06f; s.bPillar = -100.f; s.dloRearBot = s.yCowl - 0.80f; s.dloRearTop = s.yCowl - 0.80f;
    CarLook& L = d.L;
    L.head = HL_ROUND; L.headC = vec2(0.78f, 1.08f); L.headH = 0.09f; L.headYaw = 0.1f;
    L.grille = GR_HBAR; L.grilleChrome = false; L.grilleBars = 4; L.grilleTop = 1.22f; L.grilleBot = 0.98f; L.grilleW = 0.5f;
    L.intakeW = 0.f; L.fogs = false; L.plateFZ = 0.75f; L.blackBumpers = true; L.bumperFZ = 0.62f; L.bumperRZ = 0.68f;
    L.tail = TL_VERT; L.tailC = vec2(1.02f, 1.0f); L.tailW = 0.07f; L.tailH = 0.18f; L.tailYaw = 1.0f; L.plateRZ = 0.95f;
    L.antennaFin = false; L.mirrorsBlack = true; L.exhaust = 1;
    d.I.zFloor = 0.75f; d.I.hipH = 0.42f; d.I.yHipF = s.yCowl - 0.95f; d.I.rearSeat = false; d.I.dashY0 = s.yCowl - 0.05f;
    d.I.dashY1 = s.yCowl - 0.5f; d.I.seatX = 0.55f;
    d.wd = truckWheel(s.wheelR, s.wheelW);
    d.wd.lugs = 8;
    d.carSeams = false; d.fuelDoor = false; d.mirrors = false;
    d.extra = [](CarBody& b, PMesh& m) {
        const CarSpec& s = b.s;
        // stripe + lettering on both sides (secondary paint band)
        sideStripe(m, b, b.yR + 0.1f, s.yCowl - 0.9f, 1.78f, 0.16f, MAT_CARPAINT, kCol2, 0.0015f);
        for (int side = 0; side < 2; side++) {
            float sx = side == 0 ? 1.f : -1.f;
            vec3 right = side == 0 ? vec3(0, 1, 0) : vec3(0, -1, 0);
            flatText(m, vec3(sx * (s.halfW + 0.004f), -1.0f, 2.28f), right, vec3(0, 0, 1), "SOL EXPRESS", 0.24f, MAT_METAL_PAINTED, col(0.9f, 0.45f, 0.05f));
        }
        // side doors (open doorway look: dark sliding door seams) and roll-up rear door
        for (int side = 0; side < 2; side++) {
            std::vector<vec2> sd;
            float y0 = s.yCowl - 0.12f, y1 = s.yCowl - 0.85f;
            sd.push_back(vec2(y0, s.zSill + 0.08f)); sd.push_back(vec2(y0, 2.45f)); sd.push_back(vec2(y1, 2.45f)); sd.push_back(vec2(y1, s.zSill + 0.08f));
            seam(m, b.proj, side == 0 ? projRight() : projLeft(), sd, 0.006f);
        }
        m.newGroup(40.f);
        m.use(MAT_PLASTIC, col(0.4f, 0.4f, 0.4f));
        for (int k = 0; k < 6; k++) roundedBoxAt(m, vec3(0, b.yR - 0.004f, 1.2f + k * 0.26f), vec3(0.95f, 0.005f, 0.006f), 0.f, 1);
        m.use(MAT_METAL_BRUSHED, col(0.7f, 0.7f, 0.72f));
        roundedBoxAt(m, vec3(0, b.yR - 0.12f, 0.55f), vec3(0.9f, 0.12f, 0.03f), 0.01f, 1);  // rear step
        // tall mirrors
        truckMirrors(m, s.yCowl - 0.05f, s.halfW - 0.02f, 2.0f, 0.18f);
    };
    physics(o, 5600.f, 150.f, 580.f, 3600.f, 33.f, 6, 0.f, 0.85f, 0.18f, 1.6f, 0.55f, 0.f, vec3(0, -0.2f, 1.0f), Audio::ENGINE_TRUCK_DIESEL);
    CarBody b(d.s);
    PMesh m;
    d.L.maker = makerId(o.maker);
    plateText(d.L, o.name);
    d.L.logoR = Max(d.L.logoR, 0.05f);
    carBodyParts(d, b, m, true);
    finalizeMesh(m, o.body);
    buildWheel(d.wd, o.wheel);
    wheelPair(o, s.wb * 0.5f, s.trackF, s.wheelR, s.wheelW, true, false);
    dualAxle(o, -s.wb * 0.5f, s.trackR, s.wheelR, s.wheelW, true);
    heavyLights(o, vec3(0.78f, yF - 0.02f, 1.08f), vec3(1.02f, yR, 1.0f));
    o.seats.push_back(SeatSpec{vec3(-0.55f, d.I.yHipF, d.I.zFloor + d.I.hipH), true, true});
    o.seats.push_back(SeatSpec{vec3(0.55f, d.I.yHipF, d.I.zFloor + d.I.hipH), false, false});
    setBox(o, s.zSill);
    o.frontalArea = 2.36f * 2.5f * 0.9f;
    o.fixedLivery = true;
    o.liveryPrimary = srgb(245, 245, 242);
    o.liverySecondary = srgb(230, 110, 20);
    o.paletteColors.push_back(o.liveryPrimary);
    o.spawnWeight = 2.f; o.price = 65000;
}

// Box truck: conventional cab + cargo box
inline void mdlPackhorse(VehicleModel& o) {
    o.name = "Packhorse 26"; o.maker = "Dunmore"; o.cls = VC_TRUCK;
    CarDef d;
    CarSpec& s = d.s;
    s.style = BS_BOXY;
    s.doors = 2;
    s.wb = 5.0f; s.foh = 1.15f;
    float yF = s.wb * 0.5f + s.foh;
    float yCab = yF - 2.85f;
    s.roh = -(yCab + s.wb * 0.5f);
    s.rearArch = false;
    s.halfW = 1.11f; s.wheelR = 0.47f; s.wheelW = 0.26f; s.trackF = 0.98f; s.trackR = 0.93f; s.archGap = 0.07f;
    s.zSill = 0.72f; s.zNoseTop = 1.08f; s.zNoseBot = 0.62f; s.zChin = 0.56f; s.zHoodF = 1.50f; s.hoodEdgeBack = 0.10f; s.noseRound = 0.3f;
    s.yCowl = yF - 1.35f; s.zCowl = 1.64f; s.zBeltF = 1.62f; s.zBeltR = 1.66f;
    s.zRoof = 2.62f; s.yRoofF = s.yCowl - 0.62f; s.yRoofR = yCab + 0.10f;
    s.yDeck = yCab + 0.02f; s.zDeck = 1.66f; s.zTail = 1.64f; s.tailEdgeFwd = 0.01f; s.zTailTop = 1.62f; s.zTailBot = 0.76f; s.zRearLow = 0.74f;
    s.frontD = 0.45f; s.frontExp = 3.4f; s.rearD = 0.08f; s.rearExp = 9.f; s.hoodNarrow = 0.12f;
    s.tuLow = 0.03f; s.tuTop = 0.02f; s.zWide = 1.1f; s.lean = 0.10f; s.shR = 0.05f; s.shRx = 0.05f; s.railR = 0.08f; s.ghInset = 0.03f;
    s.zChar = 0.f; s.charOut = 0.f; s.flareOut = 0.03f; s.flareW = 0.12f;
    s.dloFront = s.yCowl - 0.06f; s.bPillar = -100.f; s.dloRearBot = yCab + 0.20f; s.dloRearTop = yCab + 0.22f;
    s.rearGlass = true;
    CarLook& L = d.L;
    L.head = HL_RECT; L.headC = vec2(0.78f, 1.28f); L.headW = 0.16f; L.headH = 0.07f; L.headYaw = 0.35f;
    L.grille = GR_TRUCK; L.grilleChrome = true; L.grilleBars = 3; L.grilleTop = 1.42f; L.grilleBot = 1.02f; L.grilleW = 0.55f;
    L.intakeW = 0.f; L.fogs = false; L.plateFZ = 0.82f; L.chromeBumpers = true; L.bumperFZ = 0.72f;
    L.tail = TL_VERT; L.tailC = vec2(0.9f, 1.3f); L.tailW = 0.05f; L.tailH = 0.08f; L.antennaFin = false; L.exhaust = 0;
    d.I.zFloor = 0.95f; d.I.hipH = 0.40f; d.I.yHipF = yCab + 0.55f; d.I.rearSeat = false; d.I.dashY0 = s.yCowl - 0.05f;
    d.I.dashY1 = s.yCowl - 0.5f; d.I.seatX = 0.48f; d.I.bench = true;
    d.wd = truckWheel(s.wheelR, s.wheelW);
    d.carSeams = false; d.fuelDoor = false; d.mirrors = false; d.rearPlate = false;
    float boxF = yCab - 0.12f, boxR = -(s.wb * 0.5f + 2.55f);
    d.extra = [=](CarBody& b, PMesh& m) {
        const CarSpec& s = b.s;
        // door seams both sides
        for (int side = 0; side < 2; side++) {
            std::vector<vec2> sd;
            sd.push_back(vec2(s.dloFront + 0.03f, b.beltZAt(s.dloFront) + 0.02f));
            sd.push_back(vec2(s.dloFront + 0.03f, s.zSill + 0.1f));
            sd.push_back(vec2(yCab + 0.18f, s.zSill + 0.1f));
            sd.push_back(vec2(yCab + 0.18f, b.beltZAt(yCab + 0.18f) + 0.02f));
            seam(m, b.proj, side == 0 ? projRight() : projLeft(), sd, 0.005f);
        }
        cargoBox(m, boxF, boxR, 1.26f, 1.06f, 3.66f, MAT_METAL_PAINTED, col(0.92f, 0.92f, 0.9f), true);
        for (int side = 0; side < 2; side++) {
            float sx = side == 0 ? 1.f : -1.f;
            vec3 right = side == 0 ? vec3(0, 1, 0) : vec3(0, -1, 0);
            flatText(m, vec3(sx * 1.266f, (boxF + boxR) * 0.5f, 2.55f), right, vec3(0, 0, 1), "DUNMORE", 0.42f, MAT_METAL_PAINTED, col(0.1f, 0.25f, 0.5f));
            flatText(m, vec3(sx * 1.266f, (boxF + boxR) * 0.5f, 2.0f), right, vec3(0, 0, 1), "MOVING", 0.22f, MAT_METAL_PAINTED, col(0.9f, 0.3f, 0.1f));
        }
        frameRails(m, s.wb * 0.5f + 0.9f, boxR, 0.44f, 0.72f, 0.26f);
        tankY(m, vec3(-1.02f, -0.6f, 0.88f), 1.0f, 0.27f, false);
        m.use(MAT_METAL_PAINTED, col(0.12f, 0.12f, 0.12f));
        roundedBoxAt(m, vec3(1.0f, -0.6f, 0.9f), vec3(0.2f, 0.3f, 0.2f), 0.02f, 1);  // battery box
        // side under-ride guards
        m.use(MAT_METAL_BRUSHED, col(0.6f, 0.6f, 0.62f));
        for (int sg = -1; sg <= 1; sg += 2) {
            roundedBoxAt(m, vec3(sg * 1.2f, (-0.2f - 1.9f) * 0.5f - 0.2f, 0.72f), vec3(0.02f, 0.9f, 0.04f), 0.01f, 1);
        }
        // rear ICC bumper
        m.use(MAT_METAL_PAINTED, col(0.1f, 0.1f, 0.1f));
        roundedBoxAt(m, vec3(0, boxR + 0.12f, 0.62f), vec3(1.1f, 0.06f, 0.06f), 0.01f, 1);
        for (int sg = -1; sg <= 1; sg += 2) {
            roundedBoxAt(m, vec3(sg * 0.6f, boxR + 0.2f, 0.85f), vec3(0.04f, 0.04f, 0.2f), 0.01f, 1);
            rearLampBlock(m, sg * 0.95f, boxR - 0.02f, 0.85f, 0.18f, 0.22f);
            mudFlap(m, sg * 0.93f, -s.wb * 0.5f - 0.62f, 0.6f, 0.72f);
        }
        // rear fenders over the duals
        for (int sg = -1; sg <= 1; sg += 2) fenderArc(m, sg * 0.93f, -s.wb * 0.5f, s.wheelR, s.wheelR + 0.1f, 0.62f, 0.1f, kPi - 0.1f, MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
        truckMirrors(m, s.yCowl - 0.1f, s.halfW - 0.02f, 2.05f, 0.22f);
        // exhaust stack behind the cab (right)
        m.use(MAT_CHROME, kCol1);
        cyl(m, vec3(0.95f, yCab - 0.05f, 1.2f), vec3(0.95f, yCab - 0.05f, 3.3f), 0.05f, 10, true);
    };
    physics(o, 8200.f, 220.f, 900.f, 2600.f, 30.f, 6, 0.f, 0.85f, 0.18f, 1.8f, 0.60f, 0.f, vec3(0, 0.3f, 1.3f), Audio::ENGINE_TRUCK_DIESEL);
    CarBody b(d.s);
    PMesh m;
    d.L.maker = makerId(o.maker);
    plateText(d.L, o.name);
    d.L.logoR = Max(d.L.logoR, 0.05f);
    carBodyParts(d, b, m, true);
    finalizeMesh(m, o.body);
    buildWheel(d.wd, o.wheel);
    wheelPair(o, s.wb * 0.5f, s.trackF, s.wheelR, s.wheelW, true, false);
    dualAxle(o, -s.wb * 0.5f, s.trackR, s.wheelR, s.wheelW, true);
    heavyLights(o, vec3(0.78f, yF - 0.05f, 1.28f), vec3(0.95f, boxR - 0.03f, 0.85f));
    o.seats.push_back(SeatSpec{vec3(-0.48f, d.I.yHipF, d.I.zFloor + d.I.hipH), true, true});
    o.seats.push_back(SeatSpec{vec3(0.48f, d.I.yHipF, d.I.zFloor + d.I.hipH), false, false});
    setBox(o, 0.6f);
    o.frontalArea = 2.52f * 3.2f * 0.95f;
    o.paletteColors = palette("truck");
    o.spawnWeight = 1.5f; o.price = 95000;
}

// Semi tractor (long nose conventional with sleeper)
inline void mdlLongbow(VehicleModel& o) {
    o.name = "Longbow 9"; o.maker = "Dunmore"; o.cls = VC_TRUCK;
    CarDef d;
    CarSpec& s = d.s;
    s.style = BS_BOXY;
    s.doors = 2;
    s.wb = 5.9f; s.foh = 1.12f;
    float yF = s.wb * 0.5f + s.foh;
    float yCab = -1.55f;
    s.roh = -(yCab + s.wb * 0.5f);
    s.rearArch = false;
    s.halfW = 1.22f; s.wheelR = 0.52f; s.wheelW = 0.29f; s.trackF = 1.03f; s.trackR = 0.93f; s.archGap = 0.08f;
    s.zSill = 1.02f; s.zNoseTop = 1.20f; s.zNoseBot = 0.62f; s.zChin = 0.56f; s.zHoodF = 1.86f; s.hoodEdgeBack = 0.18f; s.noseRound = 0.35f;
    s.yCowl = yF - 2.30f; s.zCowl = 2.10f; s.zBeltF = 2.08f; s.zBeltR = 2.12f;
    s.zRoof = 3.9f; s.yRoofF = s.yCowl - 0.55f; s.yRoofR = yCab + 0.03f;
    s.yDeck = yCab + 0.012f; s.zDeck = 2.12f; s.zTail = 2.10f; s.tailEdgeFwd = 0.008f; s.zTailTop = 2.08f; s.zTailBot = 1.12f; s.zRearLow = 1.06f;
    s.roofKeys = {vec2(s.yDeck, s.zDeck), vec2(yCab + 0.03f, 3.80f), vec2(yCab + 0.18f, 3.90f), vec2(s.yCowl - 2.15f, 3.88f),
                  vec2(s.yCowl - 1.55f, 3.08f), vec2(s.yCowl - 0.62f, 3.02f), vec2(s.yRoofF, 2.96f), vec2(s.yCowl - 0.25f, 2.52f), vec2(s.yCowl, s.zCowl)};
    s.frontD = 0.55f; s.frontExp = 3.2f; s.rearD = 0.06f; s.rearExp = 9.f; s.hoodNarrow = 0.14f;
    s.tuLow = 0.03f; s.tuTop = 0.02f; s.zWide = 1.4f; s.lean = 0.10f; s.shR = 0.08f; s.shRx = 0.08f; s.railR = 0.14f; s.ghInset = 0.03f;
    s.zChar = 0.f; s.charOut = 0.f; s.flareOut = 0.05f; s.flareW = 0.18f; s.crownHood = 0.08f;
    s.dloFront = s.yCowl - 0.08f; s.bPillar = -100.f; s.dloRearBot = s.yCowl - 1.25f; s.dloRearTop = s.yCowl - 1.05f;
    s.rearGlass = false;
    CarLook& L = d.L;
    L.head = HL_PROJ; L.headDomes = 3; L.headC = vec2(0.86f, 1.42f); L.headW = 0.22f; L.headH = 0.08f; L.headYaw = 0.55f;
    L.grille = GR_TRUCK; L.grilleChrome = true; L.grilleBars = 4; L.grilleTop = 1.80f; L.grilleBot = 1.10f; L.grilleW = 0.58f; L.grilleTaper = 0.04f;
    L.intakeW = 0.f; L.fogs = false; L.plateFZ = 0.95f; L.chromeBumpers = true; L.bumperFZ = 0.78f; L.frontPlate = true;
    L.tail = TL_VERT; L.tailC = vec2(1.0f, 1.6f); L.tailW = 0.05f; L.tailH = 0.1f; L.antennaFin = false; L.exhaust = 0;
    d.I.zFloor = 1.25f; d.I.hipH = 0.42f; d.I.yHipF = s.yCowl - 1.05f; d.I.rearSeat = false; d.I.dashY0 = s.yCowl - 0.05f;
    d.I.dashY1 = s.yCowl - 0.55f; d.I.seatX = 0.52f;
    d.wd = truckWheel(s.wheelR, s.wheelW);
    d.carSeams = false; d.fuelDoor = false; d.mirrors = false; d.rearPlate = false;
    d.extra = [=](CarBody& b, PMesh& m) {
        const CarSpec& s = b.s;
        for (int side = 0; side < 2; side++) {
            std::vector<vec2> sd;
            sd.push_back(vec2(s.dloFront + 0.03f, b.beltZAt(s.dloFront) + 0.02f));
            sd.push_back(vec2(s.dloFront + 0.03f, s.zSill + 0.1f));
            sd.push_back(vec2(s.dloRearBot - 0.05f, s.zSill + 0.1f));
            sd.push_back(vec2(s.dloRearBot - 0.05f, b.beltZAt(s.dloRearBot) + 0.02f));
            seam(m, b.proj, side == 0 ? projRight() : projLeft(), sd, 0.005f);
            // sleeper window
            Decal dc = sideDecal(b, side == 1, vec2(yCab + 0.2f, 2.2f), vec2(yCab + 1.2f, 3.2f));
            glassPatch(m, dc, rectO(yCab + 0.45f, yCab + 0.95f, 2.45f, 2.85f, 0.06f));
        }
        // sun visor over the windshield
        m.newGroup(35.f);
        m.use(MAT_CARPAINT, kCol1);
        float yv = s.yRoofF + 0.02f;
        roundedBox(m, Frame(vec3(0, yv + 0.1f, b.roofZAt(yv) + 0.02f), vec3(1, 0, 0), normalize(vec3(0, 1, -0.35f)), normalize(vec3(0, 0.35f, 1))),
                   vec3(b.railXAt(yv) + 0.05f, 0.16f, 0.012f), 0.01f, 1);
        // roof marker lights
        m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.55f, 0.05f, 1.f));
        for (int k = 0; k < 5; k++) roundedBoxAt(m, vec3((k - 2) * 0.22f, yv + 0.22f, b.roofZAt(yv) - 0.02f), vec3(0.035f, 0.02f, 0.018f), 0.008f, 1);
        // chassis: rails, fifth wheel, tanks, steps, stacks, fenders, flaps, deck plate
        float yT = -s.wb * 0.5f;
        frameRails(m, s.wb * 0.5f + 0.9f, yT - 1.05f, 0.43f, 0.92f, 0.28f);
        m.use(MAT_METAL_PAINTED, col(0.06f, 0.06f, 0.06f));
        roundedBoxAt(m, vec3(0, yT + 0.25f, 1.28f), vec3(0.6f, 0.55f, 0.05f), 0.03f, 1);
        m.use(MAT_METAL_BRUSHED, col(0.5f, 0.5f, 0.52f));
        roundedBoxAt(m, vec3(0, yCab - 0.35f, 1.22f), vec3(0.55f, 0.3f, 0.02f), 0.005f, 1);  // deck plate
        for (int sg = -1; sg <= 1; sg += 2) {
            tankY(m, vec3(sg * 1.02f, yCab + 1.25f, 0.78f), 1.3f, 0.33f, true);
            m.use(MAT_METAL_PAINTED, col(0.1f, 0.1f, 0.1f));
            roundedBoxAt(m, vec3(sg * 1.05f, s.dloRearBot + 0.35f, 0.62f), vec3(0.2f, 0.25f, 0.03f), 0.01f, 1);  // step
            m.use(MAT_CHROME, kCol1);
            std::vector<vec3> st;
            st.push_back(vec3(sg * 1.05f, yCab - 0.12f, 1.05f));
            st.push_back(vec3(sg * 1.05f, yCab - 0.12f, 4.35f));
            tube1(m, st, 0.075f, 14, true);
            m.use(MAT_METAL_BRUSHED, col(0.6f, 0.6f, 0.62f));
            roundedBoxAt(m, vec3(sg * 1.05f, yCab - 0.12f + 0.0f, 2.7f), vec3(0.09f, 0.09f, 0.6f), 0.05f, 1);
            // quarter fenders over the tandems and mud flaps
            for (int a = -1; a <= 1; a += 2) fenderArc(m, sg * 0.93f, yT + a * 0.66f, s.wheelR, s.wheelR + 0.08f, 0.65f, a > 0 ? 0.9f : 0.25f, a > 0 ? 2.9f : 2.25f, MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
            mudFlap(m, sg * 0.93f, yT - 0.66f - 0.62f, 0.62f, 0.8f);
            // horn
            m.use(MAT_CHROME, kCol1);
            std::vector<vec2> hp;
            hp.push_back(vec2(-0.35f, 0.025f)); hp.push_back(vec2(0.2f, 0.03f)); hp.push_back(vec2(0.3f, 0.07f)); hp.push_back(vec2(0.3f, 0.f));
            lathe(m, vec3(sg * 0.35f, s.yRoofF - 0.35f, b.roofZAt(s.yRoofF - 0.35f) + 0.05f), vec3(0, 1, 0), vec3(1, 0, 0), hp, 10);
        }
        truckMirrors(m, s.yCowl - 0.12f, s.halfW - 0.02f, 2.55f, 0.25f);
    };
    physics(o, 9000.f, 373.f, 2500.f, 2100.f, 34.f, 10, 0.f, 0.9f, 0.16f, 2.0f, 0.62f, 0.f, vec3(0, 0.6f, 1.25f), Audio::ENGINE_TRUCK_DIESEL);
    CarBody b(d.s);
    PMesh m;
    d.L.maker = makerId(o.maker);
    plateText(d.L, o.name);
    d.L.logoR = Max(d.L.logoR, 0.05f);
    carBodyParts(d, b, m, true);
    finalizeMesh(m, o.body);
    buildWheel(d.wd, o.wheel);
    wheelPair(o, s.wb * 0.5f, s.trackF, s.wheelR, s.wheelW, true, false);
    dualAxle(o, -s.wb * 0.5f + 0.66f, s.trackR, s.wheelR, s.wheelW, true);
    dualAxle(o, -s.wb * 0.5f - 0.66f, s.trackR, s.wheelR, s.wheelW, true);
    heavyLights(o, vec3(0.86f, yF - 0.1f, 1.42f), vec3(0.9f, -s.wb * 0.5f - 1.2f, 1.1f));
    o.seats.push_back(SeatSpec{vec3(-0.52f, d.I.yHipF, d.I.zFloor + d.I.hipH), true, true});
    o.seats.push_back(SeatSpec{vec3(0.52f, d.I.yHipF, d.I.zFloor + d.I.hipH), false, false});
    setBox(o, 0.6f);
    o.frontalArea = 2.5f * 3.3f * 0.9f;
    o.paletteColors = palette("truck");
    o.spawnWeight = 1.5f; o.price = 165000;
}

// Garbage truck (cab-over + packer body + rear loader)
inline void mdlCompactor(VehicleModel& o) {
    o.name = "Compactor"; o.maker = "Dunmore"; o.cls = VC_SERVICE;
    CarDef d;
    CarSpec& s = d.s;
    s.style = BS_BOXY;
    s.doors = 2;
    s.wb = 5.3f; s.foh = 1.5f;
    float yF = s.wb * 0.5f + s.foh;
    float yCab = yF - 2.3f;
    s.roh = -(yCab + s.wb * 0.5f);
    s.rearArch = false;
    s.halfW = 1.24f; s.wheelR = 0.52f; s.wheelW = 0.29f; s.trackF = 1.03f; s.trackR = 0.93f; s.archGap = 0.08f;
    s.zSill = 0.52f; s.zNoseTop = 1.20f; s.zNoseBot = 0.48f; s.zChin = 0.44f; s.zHoodF = 1.30f; s.hoodEdgeBack = 0.05f; s.noseRound = 0.2f;
    s.yCowl = yF - 0.10f; s.zCowl = 1.38f; s.zBeltF = 1.36f; s.zBeltR = 1.40f;
    s.zRoof = 2.75f; s.yRoofF = s.yCowl - 0.22f; s.yRoofR = yCab + 0.06f;
    s.yDeck = yCab + 0.015f; s.zDeck = 1.40f; s.zTail = 1.38f; s.tailEdgeFwd = 0.01f; s.zTailTop = 1.36f; s.zTailBot = 0.55f; s.zRearLow = 0.50f;
    s.roofKeys = {vec2(s.yDeck, s.zDeck), vec2(yCab + 0.04f, 2.70f), vec2(yCab + 0.2f, 2.75f), vec2(s.yCowl - 0.4f, 2.75f),
                  vec2(s.yRoofF, 2.60f), vec2(s.yCowl - 0.1f, 2.0f), vec2(s.yCowl, s.zCowl)};
    s.frontD = 0.20f; s.frontExp = 7.f; s.rearD = 0.06f; s.rearExp = 9.f;
    s.tuLow = 0.03f; s.tuTop = 0.02f; s.zWide = 1.0f; s.lean = 0.06f; s.shR = 0.05f; s.shRx = 0.05f; s.railR = 0.12f; s.ghInset = 0.02f;
    s.zChar = 0.f; s.charOut = 0.f; s.flareOut = 0.0f; s.cowlLen = 0.02f;
    s.dloFront = s.yCowl - 0.12f; s.bPillar = -100.f; s.dloRearBot = yCab + 0.25f; s.dloRearTop = yCab + 0.25f;
    s.rearGlass = false;
    CarLook& L = d.L;
    L.head = HL_RECT; L.headC = vec2(0.92f, 0.95f); L.headW = 0.14f; L.headH = 0.07f; L.headYaw = 0.25f;
    L.grille = GR_HBAR; L.grilleChrome = false; L.grilleBars = 5; L.grilleTop = 1.18f; L.grilleBot = 0.82f; L.grilleW = 0.62f; L.grilleTaper = 0.f;
    L.intakeW = 0.f; L.fogs = false; L.plateFZ = 0.66f; L.blackBumpers = true; L.bumperFZ = 0.55f;
    L.tail = TL_VERT; L.antennaFin = false; L.exhaust = 0;
    d.I.zFloor = 0.75f; d.I.hipH = 0.42f; d.I.yHipF = yCab + 0.6f; d.I.rearSeat = false; d.I.dashY0 = s.yCowl - 0.05f;
    d.I.dashY1 = s.yCowl - 0.5f; d.I.seatX = 0.55f;
    d.wd = truckWheel(s.wheelR, s.wheelW);
    d.carSeams = false; d.fuelDoor = false; d.mirrors = false; d.rearPlate = false;
    float bodyF = yCab - 0.10f, bodyR = -3.15f, hopR = -(s.wb * 0.5f + 2.7f);
    d.extra = [=](CarBody& b, PMesh& m) {
        const CarSpec& s = b.s;
        for (int side = 0; side < 2; side++) {
            std::vector<vec2> sd;
            sd.push_back(vec2(s.dloFront + 0.03f, b.beltZAt(s.dloFront) + 0.02f));
            sd.push_back(vec2(s.dloFront + 0.03f, s.zSill + 0.1f));
            sd.push_back(vec2(yCab + 0.3f, s.zSill + 0.1f));
            sd.push_back(vec2(yCab + 0.3f, b.beltZAt(yCab + 0.3f) + 0.02f));
            seam(m, b.proj, side == 0 ? projRight() : projLeft(), sd, 0.005f);
        }
        // packer body (secondary colour) with ribs
        m.newGroup(30.f);
        m.use(MAT_CARPAINT, kCol2);
        float cy = (bodyF + bodyR) * 0.5f, hl = (bodyF - bodyR) * 0.5f;
        roundedBoxAt(m, vec3(0, cy, 2.3f), vec3(1.25f, hl, 1.25f), 0.28f, 3);
        m.use(MAT_CARPAINT, kCol1);
        for (int k = 0; k < 5; k++) {
            float y = lerp(bodyF - 0.3f, bodyR + 0.3f, k / 4.f);
            roundedBoxAt(m, vec3(0, y, 2.3f), vec3(1.27f, 0.05f, 1.2f), 0.04f, 1);
        }
        // hopper / tailgate: side profile extruded across
        m.use(MAT_CARPAINT, kCol2);
        std::vector<vec2> hp;  // (y, z) CCW
        hp.push_back(vec2(bodyR, 1.0f));
        hp.push_back(vec2(hopR + 0.35f, 0.95f));
        hp.push_back(vec2(hopR, 1.25f));
        hp.push_back(vec2(hopR + 0.15f, 2.55f));
        hp.push_back(vec2(bodyR - 0.25f, 3.45f));
        hp.push_back(vec2(bodyR, 3.50f));
        extrude(m, hp, Frame(vec3(0, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1, 0, 0)), -1.22f, 1.22f);
        // hopper mouth (dark) and sill
        m.use(MAT_PLASTIC, col(0.08f, 0.08f, 0.08f));
        {
            vec3 a(0, hopR + 0.02f, 1.3f), c2(0, hopR + 0.14f, 2.35f);
            vec3 up = normalize(c2 - a);
            roundedBox(m, Frame((a + c2) * 0.5f + vec3(0, -0.02f, 0), vec3(1, 0, 0), up, normalize(cross(vec3(1, 0, 0), up))), vec3(1.0f, length(c2 - a) * 0.45f, 0.02f), 0.02f, 1);
        }
        m.use(MAT_METAL_BRUSHED, col(0.6f, 0.6f, 0.62f));
        roundedBoxAt(m, vec3(0, hopR + 0.1f, 1.05f), vec3(1.2f, 0.12f, 0.06f), 0.02f, 1);
        // rear lights + beacon
        for (int sg = -1; sg <= 1; sg += 2) {
            rearLampBlock(m, sg * 1.1f, hopR + 0.2f, 1.9f, 0.1f, 0.3f);
            m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.6f, 0.05f, 0.f));
            ellipsoid(m, Frame(vec3(sg * 1.0f, bodyR - 0.2f, 3.52f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), vec3(0.07f, 0.07f, 0.09f), 10, 5, 0.f, kHalfPi);
            fenderArc(m, sg * 0.93f, -s.wb * 0.5f + 0.68f, s.wheelR, s.wheelR + 0.08f, 0.64f, 0.9f, 2.9f, MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
            fenderArc(m, sg * 0.93f, -s.wb * 0.5f - 0.68f, s.wheelR, s.wheelR + 0.08f, 0.64f, 0.25f, 2.25f, MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
            m.use(MAT_METAL_BRUSHED, col(0.6f, 0.6f, 0.62f));
            roundedBoxAt(m, vec3(sg * 1.05f, hopR + 0.25f, 1.2f), vec3(0.2f, 0.15f, 0.02f), 0.01f, 1);  // riding step
            cyl(m, vec3(sg * 1.26f, hopR + 0.3f, 1.4f), vec3(sg * 1.26f, hopR + 0.3f, 2.3f), 0.018f, 6);  // grab bar
        }
        frameRails(m, s.wb * 0.5f + 1.0f, hopR + 0.3f, 0.43f, 0.72f, 0.28f);
        tankY(m, vec3(-1.02f, yCab - 0.6f, 0.82f), 1.0f, 0.28f, false);
        for (int side = 0; side < 2; side++) {
            float sx = side == 0 ? 1.f : -1.f;
            vec3 right = side == 0 ? vec3(0, 1, 0) : vec3(0, -1, 0);
            flatText(m, vec3(sx * 1.272f, cy, 2.6f), right, vec3(0, 0, 1), "PORTO SOL", 0.30f, MAT_METAL_PAINTED, col(0.95f, 0.95f, 0.95f));
            flatText(m, vec3(sx * 1.272f, cy, 2.12f), right, vec3(0, 0, 1), "SANITATION", 0.18f, MAT_METAL_PAINTED, col(0.95f, 0.95f, 0.95f));
        }
        truckMirrors(m, s.yCowl - 0.08f, s.halfW - 0.02f, 1.9f, 0.2f);
    };
    physics(o, 15500.f, 250.f, 1600.f, 2200.f, 25.f, 6, 0.f, 0.85f, 0.16f, 2.2f, 0.75f, 0.f, vec3(0, -0.4f, 1.35f), Audio::ENGINE_TRUCK_DIESEL);
    CarBody b(d.s);
    PMesh m;
    d.L.maker = makerId(o.maker);
    plateText(d.L, o.name);
    d.L.logoR = Max(d.L.logoR, 0.05f);
    carBodyParts(d, b, m, true);
    finalizeMesh(m, o.body);
    buildWheel(d.wd, o.wheel);
    wheelPair(o, s.wb * 0.5f, s.trackF, s.wheelR, s.wheelW, true, false);
    dualAxle(o, -s.wb * 0.5f + 0.68f, s.trackR, s.wheelR, s.wheelW, true);
    dualAxle(o, -s.wb * 0.5f - 0.68f, s.trackR, s.wheelR, s.wheelW, true);
    heavyLights(o, vec3(0.92f, yF - 0.02f, 0.95f), vec3(1.1f, hopR + 0.18f, 1.9f));
    o.seats.push_back(SeatSpec{vec3(-0.55f, d.I.yHipF, d.I.zFloor + d.I.hipH), true, true});
    o.seats.push_back(SeatSpec{vec3(0.55f, d.I.yHipF, d.I.zFloor + d.I.hipH), false, false});
    setBox(o, 0.5f);
    o.frontalArea = 2.5f * 3.3f * 0.95f;
    o.fixedLivery = true;
    o.liveryPrimary = srgb(240, 240, 236);
    o.liverySecondary = srgb(30, 105, 70);
    o.paletteColors.push_back(o.liveryPrimary);
    o.spawnWeight = 1.2f; o.price = 180000;
}

// Ambulance (van cab + module)
inline void mdlLifeline(VehicleModel& o) {
    o.name = "Lifeline"; o.maker = "Dunmore"; o.cls = VC_AMBULANCE;
    CarDef d;
    float wb = 3.9f, foh = 1.0f;
    float yF = wb * 0.5f + foh;
    float yCab = yF - 2.25f;
    vanFront(d, wb, foh, -(yCab + wb * 0.5f), 2.02f, 0.39f);
    CarSpec& s = d.s;
    s.rearArch = false;
    s.zRoof = 2.12f;
    s.yRoofF = s.yCowl - 0.72f; s.yRoofR = yCab + 0.08f;
    s.yDeck = yCab + 0.02f; s.zDeck = 1.26f; s.zTail = 1.24f; s.tailEdgeFwd = 0.01f; s.zTailTop = 1.22f; s.zTailBot = 0.52f; s.zRearLow = 0.46f;
    s.roofKeys = {vec2(s.yDeck, s.zDeck), vec2(yCab + 0.04f, 2.05f), vec2(yCab + 0.2f, 2.12f), vec2(s.yRoofF - 0.2f, 2.12f),
                  vec2(s.yRoofF, 2.02f), vec2((s.yRoofF + s.yCowl) * 0.5f, 1.66f), vec2(s.yCowl, s.zCowl)};
    s.dloFront = s.yCowl - 0.06f; s.dloRearBot = yCab + 0.15f; s.dloRearTop = yCab + 0.15f;
    s.wheelW = 0.235f;
    d.wd = truckWheel(0.39f, 0.235f);
    d.wd.lugs = 8;
    d.L.tail = TL_VERT; d.L.exhaust = 0;
    d.rearPlate = false; d.mirrors = false;
    float modF = yCab - 0.05f, modR = -(wb * 0.5f + 1.35f);
    d.extra = [=](CarBody& b, PMesh& m) {
        const CarSpec& s = b.s;
        for (int side = 0; side < 2; side++) {
            std::vector<vec2> sd;
            sd.push_back(vec2(s.dloFront + 0.03f, b.beltZAt(s.dloFront) + 0.02f));
            sd.push_back(vec2(s.dloFront + 0.03f, s.zSill + 0.1f));
            sd.push_back(vec2(yCab + 0.2f, s.zSill + 0.1f));
            sd.push_back(vec2(yCab + 0.2f, b.beltZAt(yCab + 0.2f) + 0.02f));
            seam(m, b.proj, side == 0 ? projRight() : projLeft(), sd, 0.005f);
        }
        // module
        float cy = (modF + modR) * 0.5f, hl = (modF - modR) * 0.5f;
        m.newGroup(30.f);
        m.use(MAT_CARPAINT, kCol1);
        roundedBoxAt(m, vec3(0, cy, 1.92f), vec3(1.18f, hl, 1.10f), 0.06f, 2);
        // red band + lettering + star of life
        m.use(MAT_CARPAINT, kCol2);
        roundedBoxAt(m, vec3(0, cy, 1.38f), vec3(1.186f, hl + 0.006f, 0.08f), 0.01f, 1);
        roundedBoxAt(m, vec3(0, cy, 2.86f), vec3(1.186f, hl + 0.006f, 0.05f), 0.01f, 1);
        for (int side = 0; side < 2; side++) {
            float sx = side == 0 ? 1.f : -1.f;
            vec3 right = side == 0 ? vec3(0, 1, 0) : vec3(0, -1, 0);
            flatText(m, vec3(sx * 1.186f, cy - 0.35f, 1.72f), right, vec3(0, 0, 1), "AMBULANCE", 0.2f, MAT_METAL_PAINTED, col(0.75f, 0.05f, 0.05f));
            // star of life: 3 crossed bars (blue)
            m.use(MAT_METAL_PAINTED, col(0.05f, 0.25f, 0.8f));
            vec3 sc(sx * 1.19f, cy + 0.95f, 2.25f);
            for (int k = 0; k < 3; k++) {
                float a = kPi * k / 3.f;
                vec3 ax = right * cosf(a) + vec3(0, 0, 1) * sinf(a);
                roundedBox(m, Frame(sc, ax, normalize(cross(vec3(sx, 0, 0), ax)), vec3(sx, 0, 0)), vec3(0.22f, 0.06f, 0.003f), 0.01f, 1);
            }
            // side door + window + grab rail
            if (side == 0) {
                m.use(MAT_PLASTIC, col(0.4f, 0.4f, 0.4f));
                for (int e = 0; e < 2; e++) roundedBoxAt(m, vec3(1.182f, modF - 0.2f - e * 0.85f, 1.8f), vec3(0.004f, 0.005f, 0.9f), 0.f, 1);
                roundedBoxAt(m, vec3(1.182f, modF - 0.625f, 2.7f), vec3(0.004f, 0.425f, 0.005f), 0.f, 1);
                m.use(MAT_CAR_GLASS, kCol1);
                roundedBoxAt(m, vec3(1.185f, modF - 0.625f, 2.3f), vec3(0.004f, 0.3f, 0.2f), 0.03f, 1);
            }
            m.use(MAT_CHROME, kCol1);
            cyl(m, vec3(sx * 1.2f, modR + 0.5f, 2.65f), vec3(sx * 1.2f, modF - 1.2f, 2.65f), 0.015f, 6);
        }
        // rear doors with windows, step bumper, rear flood lights
        m.use(MAT_PLASTIC, col(0.4f, 0.4f, 0.4f));
        roundedBoxAt(m, vec3(0, modR - 0.004f, 1.85f), vec3(0.004f, 0.005f, 0.95f), 0.f, 1);
        m.use(MAT_CAR_GLASS, kCol1);
        for (int sg = -1; sg <= 1; sg += 2) roundedBoxAt(m, vec3(sg * 0.42f, modR - 0.006f, 2.35f), vec3(0.28f, 0.004f, 0.22f), 0.03f, 1);
        m.use(MAT_METAL_BRUSHED, col(0.6f, 0.6f, 0.62f));
        roundedBoxAt(m, vec3(0, modR - 0.15f, 0.62f), vec3(1.0f, 0.15f, 0.04f), 0.02f, 1);
        for (int sg = -1; sg <= 1; sg += 2) rearLampBlock(m, sg * 0.98f, modR - 0.01f, 1.25f, 0.14f, 0.34f);
        // warning lights: front top bar (red/white), rear top corners, side flashers
        lightBar3D(m, vec3(0, modF - 0.02f + 0.06f, 3.06f), 1.05f, 0.12f, 6, vec3(1, 0.02f, 0.02f), vec3(1, 0.02f, 0.02f), true);
        m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.02f, 0.02f, 0.f));
        for (int sg = -1; sg <= 1; sg += 2) {
            roundedBoxAt(m, vec3(sg * 1.0f, modR - 0.015f, 2.88f), vec3(0.12f, 0.02f, 0.06f), 0.02f, 1);
            roundedBoxAt(m, vec3(sg * 1.19f, modR + 0.25f, 2.88f), vec3(0.02f, 0.12f, 0.06f), 0.02f, 1);
            roundedBoxAt(m, vec3(sg * 1.19f, modF - 0.25f, 2.88f), vec3(0.02f, 0.12f, 0.06f), 0.02f, 1);
        }
        // grille flashers
        m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.02f, 0.02f, 0.f));
        for (int sg = -1; sg <= 1; sg += 2) roundedBoxAt(m, vec3(sg * 0.25f, b.yF + 0.005f, 0.95f), vec3(0.06f, 0.01f, 0.025f), 0.01f, 1);
        m.use(MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
        roundedBoxAt(m, vec3(0, cy - 0.6f, 3.08f), vec3(0.45f, 0.4f, 0.08f), 0.05f, 2);  // AC unit
        // hood lettering (mirrored for rear-view mirrors)
        flatText(m, vec3(0, (b.yHF + s.yCowl) * 0.5f, b.centreZ((b.yHF + s.yCowl) * 0.5f) + 0.012f), vec3(1, 0, 0), vec3(0, 1, 0), "AMBULANCE", 0.12f,
                 MAT_METAL_PAINTED, col(0.75f, 0.05f, 0.05f));
        frameRails(m, s.wb * 0.5f + 0.6f, modR + 0.3f, 0.45f, 0.55f, 0.2f);
        for (int sg = -1; sg <= 1; sg += 2) fenderArc(m, sg * 0.84f, -s.wb * 0.5f, s.wheelR, s.wheelR + 0.07f, 0.55f, 0.15f, kPi - 0.15f, MAT_CARPAINT, kCol1);
        truckMirrors(m, s.yCowl - 0.1f, s.halfW - 0.02f, 1.62f, 0.14f);
    };
    physics(o, 5200.f, 220.f, 640.f, 4800.f, 40.f, 6, 0.f, 0.9f, 0.18f, 1.5f, 0.50f, 0.f, vec3(0, -0.3f, 1.05f), Audio::ENGINE_V8);
    o.sirenMode = 3;
    CarBody b(d.s);
    PMesh m;
    d.L.maker = makerId(o.maker);
    plateText(d.L, o.name);
    d.L.logoR = Max(d.L.logoR, 0.05f);
    carBodyParts(d, b, m, true);
    finalizeMesh(m, o.body);
    buildWheel(d.wd, o.wheel);
    wheelPair(o, s.wb * 0.5f, s.trackF, s.wheelR, s.wheelW, true, false);
    dualAxle(o, -s.wb * 0.5f, s.trackR - 0.04f, s.wheelR, s.wheelW, true);
    heavyLights(o, vec3(s.halfW * 0.72f, yF - 0.05f, 1.0f), vec3(0.98f, modR - 0.02f, 1.25f));
    for (int sg = -1; sg <= 1; sg += 2) {
        o.lights.push_back(LightSpec{vec3(sg * 0.6f, modF + 0.05f, 3.07f), vec3(0, 1, 0), LT_SIREN_RED});
        o.lights.push_back(LightSpec{vec3(sg * 1.0f, modR - 0.02f, 2.88f), vec3(0, -1, 0), LT_SIREN_RED});
    }
    o.seats.push_back(SeatSpec{vec3(-d.I.seatX, d.I.yHipF, d.I.zFloor + d.I.hipH), true, true});
    o.seats.push_back(SeatSpec{vec3(d.I.seatX, d.I.yHipF, d.I.zFloor + d.I.hipH), false, false});
    o.seats.push_back(SeatSpec{vec3(0.6f, modF - 1.0f, 1.3f), false, false});
    o.seats.push_back(SeatSpec{vec3(-0.6f, modR + 0.8f, 1.3f), false, true});
    setBox(o, 0.45f);
    o.frontalArea = 2.36f * 2.6f * 0.9f;
    o.fixedLivery = true;
    o.liveryPrimary = srgb(246, 246, 244);
    o.liverySecondary = srgb(200, 20, 24);
    o.paletteColors.push_back(o.liveryPrimary);
    o.spawnWeight = 0.3f; o.price = 0;
}

// Fire engine (custom cab pumper)
inline void mdlGuardian(VehicleModel& o) {
    o.name = "Guardian Pumper"; o.maker = "Dunmore"; o.cls = VC_FIRETRUCK;
    CarDef d;
    CarSpec& s = d.s;
    s.style = BS_BOXY;
    s.doors = 4;
    s.wb = 5.0f; s.foh = 1.55f;
    float yF = s.wb * 0.5f + s.foh;
    float yCab = yF - 3.1f;
    s.roh = -(yCab + s.wb * 0.5f);
    s.rearArch = false;
    s.halfW = 1.25f; s.wheelR = 0.53f; s.wheelW = 0.33f; s.trackF = 1.02f; s.trackR = 0.92f; s.archGap = 0.08f;
    s.zSill = 0.78f; s.zNoseTop = 1.30f; s.zNoseBot = 0.50f; s.zChin = 0.46f; s.zHoodF = 1.45f; s.hoodEdgeBack = 0.06f; s.noseRound = 0.2f;
    s.yCowl = yF - 0.16f; s.zCowl = 1.52f; s.zBeltF = 1.50f; s.zBeltR = 1.58f;
    s.zRoof = 2.98f; s.yRoofF = s.yCowl - 0.30f; s.yRoofR = yCab + 0.10f;
    s.yDeck = yCab + 0.015f; s.zDeck = 1.58f; s.zTail = 1.56f; s.tailEdgeFwd = 0.01f; s.zTailTop = 1.55f; s.zTailBot = 0.85f; s.zRearLow = 0.8f;
    s.roofKeys = {vec2(s.yDeck, s.zDeck), vec2(yCab + 0.04f, 2.92f), vec2(yCab + 0.2f, 2.98f), vec2(s.yCowl - 0.55f, 2.98f),
                  vec2(s.yRoofF, 2.72f), vec2(s.yCowl - 0.15f, 2.1f), vec2(s.yCowl, s.zCowl)};
    s.frontD = 0.22f; s.frontExp = 7.f; s.rearD = 0.06f; s.rearExp = 9.f;
    s.tuLow = 0.02f; s.tuTop = 0.02f; s.zWide = 1.1f; s.lean = 0.04f; s.shR = 0.04f; s.shRx = 0.04f; s.railR = 0.12f; s.ghInset = 0.02f;
    s.zChar = 0.f; s.charOut = 0.f; s.flareOut = 0.0f; s.cowlLen = 0.02f;
    s.dloFront = s.yCowl - 0.12f; s.bPillar = s.yCowl - 1.45f; s.bPillarW = 0.14f; s.dloRearBot = yCab + 0.2f; s.dloRearTop = yCab + 0.2f;
    s.rearGlass = false;
    s.liveryRoof = true;
    CarLook& L = d.L;
    L.head = HL_RECT; L.headC = vec2(0.95f, 1.05f); L.headW = 0.13f; L.headH = 0.09f; L.headYaw = 0.2f;
    L.grille = GR_TRUCK; L.grilleChrome = true; L.grilleBars = 4; L.grilleTop = 1.30f; L.grilleBot = 0.78f; L.grilleW = 0.55f; L.grilleTaper = 0.f;
    L.intakeW = 0.f; L.fogs = false; L.plateFZ = 0.6f; L.chromeBumpers = true; L.bumperFZ = 0.55f; L.frontPlate = false;
    L.tail = TL_VERT; L.antennaFin = false; L.exhaust = 0;
    d.I.zFloor = 0.95f; d.I.hipH = 0.42f; d.I.yHipF = s.yCowl - 1.05f; d.I.yHipR = s.yCowl - 2.1f; d.I.rearSeat = true;
    d.I.dashY0 = s.yCowl - 0.05f; d.I.dashY1 = s.yCowl - 0.5f; d.I.seatX = 0.55f;
    d.wd = truckWheel(s.wheelR, s.wheelW);
    d.wd.faceTint = vec3(0.9f);
    d.carSeams = false; d.fuelDoor = false; d.mirrors = false; d.rearPlate = false;
    float bodyF = yCab - 0.08f, bodyR = -(s.wb * 0.5f + 2.2f);
    d.extra = [=](CarBody& b, PMesh& m) {
        const CarSpec& s = b.s;
        for (int side = 0; side < 2; side++) {
            for (int dd = 0; dd < 2; dd++) {
                float y0 = dd == 0 ? s.dloFront + 0.03f : s.bPillar - 0.08f, y1 = dd == 0 ? s.bPillar + 0.08f : yCab + 0.25f;
                std::vector<vec2> sd;
                sd.push_back(vec2(y0, b.beltZAt(y0) + 0.02f)); sd.push_back(vec2(y0, s.zSill + 0.1f));
                sd.push_back(vec2(y1, s.zSill + 0.1f)); sd.push_back(vec2(y1, b.beltZAt(y1) + 0.02f));
                seam(m, b.proj, side == 0 ? projRight() : projLeft(), sd, 0.005f);
            }
        }
        // cab stripe (white) and lettering
        sideStripe(m, b, yCab + 0.05f, s.yCowl - 0.05f, 1.22f, 0.10f, MAT_CARPAINT, kCol2, 0.0015f);
        sideText(m, b, "RESCUE", (s.bPillar + s.yCowl) * 0.5f, 1.02f, 0.11f, MAT_METAL_PAINTED, col(0.95f, 0.8f, 0.2f));
        // body: compartments
        float cy = (bodyF + bodyR) * 0.5f, hl = (bodyF - bodyR) * 0.5f;
        m.newGroup(30.f);
        m.use(MAT_CARPAINT, kCol1);
        roundedBoxAt(m, vec3(0, cy, 1.95f), vec3(1.24f, hl, 1.0f), 0.04f, 2);
        m.use(MAT_CARPAINT, kCol2);
        roundedBoxAt(m, vec3(0, cy, 1.25f), vec3(1.246f, hl + 0.004f, 0.06f), 0.01f, 1);
        int nc = 3;
        for (int side = 0; side < 2; side++) {
            float sx = side == 0 ? 1.f : -1.f;
            for (int k = 0; k < nc; k++) {
                float y0 = lerp(bodyF - 0.1f, bodyR + 0.1f, (float)k / nc) - 0.05f, y1 = lerp(bodyF - 0.1f, bodyR + 0.1f, (float)(k + 1) / nc) + 0.05f;
                // roll-up door (brushed aluminium with slats)
                m.use(MAT_METAL_BRUSHED, col(0.72f, 0.72f, 0.74f));
                roundedBoxAt(m, vec3(sx * 1.245f, (y0 + y1) * 0.5f, 2.15f), vec3(0.006f, (y0 - y1) * 0.5f - 0.03f, 0.72f), 0.01f, 1);
                m.use(MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
                for (int sl = 0; sl < 8; sl++) roundedBoxAt(m, vec3(sx * 1.252f, (y0 + y1) * 0.5f, 1.52f + sl * 0.16f), vec3(0.002f, (y0 - y1) * 0.5f - 0.04f, 0.004f), 0.f, 1);
                m.use(MAT_CHROME, kCol1);
                roundedBoxAt(m, vec3(sx * 1.26f, (y0 + y1) * 0.5f, 1.5f), vec3(0.01f, 0.1f, 0.012f), 0.005f, 1);
            }
        }
        // top: hose bed, ladder rack, light bar on the cab
        m.use(MAT_PLASTIC, col(0.3f, 0.3f, 0.3f));
        roundedBoxAt(m, vec3(0, cy - 0.4f, 2.97f), vec3(0.75f, hl - 0.6f, 0.04f), 0.02f, 1);
        m.use(MAT_METAL_BRUSHED, col(0.75f, 0.75f, 0.77f));
        for (int sg = -1; sg <= 1; sg += 2) cyl(m, vec3(0.9f + sg * 0.2f, bodyF + 0.3f, 3.1f), vec3(0.9f + sg * 0.2f, bodyR - 0.25f, 3.1f), 0.022f, 6);
        for (int k = 0; k < 16; k++) {
            float y = lerp(bodyF + 0.2f, bodyR - 0.15f, k / 15.f);
            cyl(m, vec3(0.7f, y, 3.1f), vec3(1.1f, y, 3.1f), 0.012f, 5, false);
        }
        for (int k = 0; k < 3; k++) roundedBoxAt(m, vec3(0.9f, lerp(bodyF, bodyR, k / 2.f), 3.02f), vec3(0.25f, 0.03f, 0.06f), 0.01f, 1);
        lightBar3D(m, vec3(0, s.yRoofF - 0.35f, 3.08f), 1.0f, 0.24f, 6, vec3(1, 0.02f, 0.02f), vec3(1, 0.02f, 0.02f), true);
        // pump panel behind the cab
        m.use(MAT_METAL_BRUSHED, col(0.8f, 0.8f, 0.82f));
        for (int sg = -1; sg <= 1; sg += 2) {
            roundedBoxAt(m, vec3(sg * 1.25f, bodyF - 0.02f, 1.95f), vec3(0.01f, 0.07f, 0.95f), 0.01f, 1);
            m.use(MAT_CHROME, kCol1);
            for (int k = 0; k < 3; k++) {
                vec3 c(sg * 1.27f, bodyF - 0.03f, 1.45f + k * 0.35f);
                disk(m, c, vec3((float)sg, 0, 0), 0.05f, 12);
            }
            m.use(MAT_METAL_BRUSHED, col(0.8f, 0.8f, 0.82f));
        }
        // rear: tailboard, lights, flashers
        m.use(MAT_METAL_BRUSHED, col(0.7f, 0.7f, 0.72f));
        roundedBoxAt(m, vec3(0, bodyR - 0.22f, 0.9f), vec3(1.2f, 0.22f, 0.04f), 0.02f, 1);
        for (int sg = -1; sg <= 1; sg += 2) {
            rearLampBlock(m, sg * 1.0f, bodyR - 0.01f, 1.4f, 0.16f, 0.36f);
            m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.02f, 0.02f, 0.f));
            roundedBoxAt(m, vec3(sg * 1.05f, bodyR - 0.015f, 2.75f), vec3(0.1f, 0.02f, 0.08f), 0.02f, 1);
            m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.02f, 0.02f, 0.f));
            roundedBoxAt(m, vec3(sg * 1.1f, b.yF + 0.005f, 1.55f), vec3(0.08f, 0.01f, 0.04f), 0.01f, 1);
            fenderArc(m, sg * 0.92f, -s.wb * 0.5f, s.wheelR, s.wheelR + 0.09f, 0.72f, 0.12f, kPi - 0.12f, MAT_METAL_BRUSHED, col(0.75f, 0.75f, 0.77f));
            mudFlap(m, sg * 0.92f, -s.wb * 0.5f - 0.62f, 0.7f, 0.8f);
        }
        frameRails(m, s.wb * 0.5f + 1.0f, bodyR + 0.3f, 0.44f, 0.72f, 0.28f);
        // bumper siren speaker
        m.use(MAT_CHROME, kCol1);
        disk(m, vec3(-0.35f, b.yF + 0.06f, 0.62f), vec3(0, 1, 0), 0.09f, 14);
        truckMirrors(m, s.yCowl - 0.1f, s.halfW - 0.02f, 2.0f, 0.2f);
    };
    physics(o, 18500.f, 336.f, 1700.f, 2200.f, 31.f, 6, 0.f, 0.85f, 0.18f, 2.2f, 0.72f, 0.f, vec3(0, 0.1f, 1.35f), Audio::ENGINE_TRUCK_DIESEL);
    o.sirenMode = 0;
    CarBody b(d.s);
    PMesh m;
    d.L.maker = makerId(o.maker);
    plateText(d.L, o.name);
    d.L.logoR = Max(d.L.logoR, 0.05f);
    carBodyParts(d, b, m, true);
    finalizeMesh(m, o.body);
    buildWheel(d.wd, o.wheel);
    wheelPair(o, s.wb * 0.5f, s.trackF, s.wheelR, s.wheelW, true, false);
    dualAxle(o, -s.wb * 0.5f, s.trackR, s.wheelR, s.wheelW, true);
    heavyLights(o, vec3(0.95f, yF - 0.02f, 1.05f), vec3(1.0f, bodyR - 0.02f, 1.4f));
    for (int sg = -1; sg <= 1; sg += 2) {
        o.lights.push_back(LightSpec{vec3(sg * 0.6f, s.yRoofF - 0.35f, 3.1f), vec3(0, 1, 0), LT_SIREN_RED});
        o.lights.push_back(LightSpec{vec3(sg * 1.05f, bodyR - 0.03f, 2.75f), vec3(0, -1, 0), LT_SIREN_RED});
    }
    for (int k = 0; k < 4; k++)
        o.seats.push_back(SeatSpec{vec3(k % 2 == 0 ? -0.55f : 0.55f, k < 2 ? d.I.yHipF : d.I.yHipR, d.I.zFloor + d.I.hipH), k == 0, k % 2 == 0});
    setBox(o, 0.5f);
    o.frontalArea = 2.5f * 3.0f * 0.95f;
    o.fixedLivery = true;
    o.liveryPrimary = srgb(190, 14, 16);
    o.liverySecondary = srgb(245, 245, 242);
    o.paletteColors.push_back(o.liveryPrimary);
    o.spawnWeight = 0.2f; o.price = 0;
}

// City bus: rounded box loft with projected flush glazing
inline void mdlBoulevard(VehicleModel& o) {
    o.name = "Boulevard"; o.maker = "Civitas"; o.cls = VC_BUS;
    CarDef d;
    CarSpec& s = d.s;
    s.style = BS_BOXY;
    s.doors = 2;
    s.wb = 6.1f; s.foh = 2.7f; s.roh = 3.4f;
    float yF = s.wb * 0.5f + s.foh, yR = -(s.wb * 0.5f + s.roh);
    s.halfW = 1.275f; s.wheelR = 0.50f; s.wheelW = 0.29f; s.trackF = 1.06f; s.trackR = 0.93f; s.archGap = 0.07f;
    s.zSill = 0.32f; s.zNoseTop = 2.86f; s.zNoseBot = 0.40f; s.zChin = 0.34f; s.zHoodF = 3.10f; s.hoodEdgeBack = 0.07f; s.noseRound = 0.3f;
    s.yCowl = yF + 2.f;  // no greenhouse: everything is the lower body
    s.yRoofF = yF + 3.f; s.yRoofR = yF + 3.f; s.yDeck = yF + 1.f;
    s.zCowl = 3.10f; s.zBeltF = 2.95f; s.zBeltR = 2.95f; s.zRoof = 3.10f;
    s.zDeck = 3.10f; s.zTail = 3.08f; s.tailEdgeFwd = 0.06f; s.zTailTop = 2.9f; s.zTailBot = 0.45f; s.zRearLow = 0.40f;
    s.crownHood = 0.12f; s.crownDeck = 0.15f; s.fenderDrop = 0.15f;
    s.frontD = 0.28f; s.frontExp = 6.f; s.rearD = 0.25f; s.rearExp = 6.f;
    s.tuLow = 0.02f; s.tuTop = 0.04f; s.zWide = 1.2f; s.shR = 0.16f; s.shRx = 0.14f; s.cornerR = 0.06f;
    s.zChar = 0.f; s.charOut = 0.f; s.flareOut = 0.f; s.cowlLen = 0.f;
    CarLook& L = d.L;
    L.head = HL_RECT; L.headC = vec2(0.95f, 0.72f); L.headW = 0.18f; L.headH = 0.06f; L.headYaw = 0.2f;
    L.grille = GR_NONE; L.intakeW = 0.f; L.fogs = false; L.plateFZ = 0.55f; L.blackBumpers = true; L.bumperFZ = 0.45f; L.bumperRZ = 0.5f;
    L.tail = TL_VERT; L.tailC = vec2(1.1f, 0.95f); L.tailW = 0.06f; L.tailH = 0.2f; L.tailYaw = 0.9f; L.plateRZ = 0.75f;
    L.antennaFin = false; L.exhaust = 0; L.sideMarkers = false;
    d.wd = truckWheel(s.wheelR, s.wheelW);
    d.wd.faceTint = vec3(0.85f);
    d.carSeams = false; d.fuelDoor = false; d.mirrors = false;
    d.extra = [=](CarBody& b, PMesh& m) {
        // windshield and destination sign on the front face
        {
            Decal dc = endDecal(b, false, vec2(-1.3f, 0.8f), vec2(1.3f, 3.0f));
            glassPatch(m, dc, rectO(-1.16f, 1.16f, 0.98f, 2.58f, 0.10f), 0.03f);
            LampStyle st;
            st.height = 0.008f; st.bezel = 0.02f; st.wallMat = MAT_PLASTIC; st.wallCol = col(0.3f, 0.3f, 0.3f);
            st.floorMat = MAT_PLASTIC; st.floorCol = col(0.2f, 0.2f, 0.2f); st.floorOff = 0.006f;
            lampHousing(m, dc, rectO(-1.0f, 1.0f, 2.64f, 2.88f, 0.03f), st);
            m.newGroup(40.f);
            m.use(MAT_EMISSIVE, col(1.f, 0.6f, 0.1f, 0.45f));
            vec3 p, n;
            if (dc.at(vec2(0, 2.76f), p, n)) strokeText3D(m, p + n * 0.012f, vec3(-1, 0, 0), vec3(0, 0, 1), "12 BAYSIDE", 0.14f, 0.003f);
        }
        // side windows (both sides) between pillars; doors on the right (curb) side
        float yw0 = yF - 1.9f, yw1 = yR + 0.5f;
        int nw = 6;
        for (int side = 0; side < 2; side++) {
            Decal dc = sideDecal(b, side == 1, vec2(yR, 0.3f), vec2(yF, 3.0f));
            for (int k = 0; k < nw; k++) {
                float y1 = lerp(yw0, yw1, (float)k / nw) - 0.05f, y0 = lerp(yw0, yw1, (float)(k + 1) / nw) + 0.05f;
                if (side == 0 && k == 2) continue;  // middle door position
                glassPatch(m, dc, rectO(y0, y1, 1.15f, 2.50f, 0.06f), 0.025f);
            }
            // front side window by the driver / door
            glassPatch(m, dc, rectO(yF - 1.75f, yF - 0.35f, 1.15f, 2.50f, 0.06f), 0.025f);
            if (side == 0) {
                // front door (glass bi-fold) and middle door
                float dy[2][2] = {{yF - 1.62f, yF - 0.42f}, {lerp(yw0, yw1, 3.f / nw) + 0.05f, lerp(yw0, yw1, 2.f / nw) - 0.05f}};
                for (int dd = 0; dd < 2; dd++) {
                    LampStyle st;
                    st.height = 0.008f; st.bezel = 0.05f; st.wallMat = MAT_PLASTIC; st.wallCol = col(0.3f, 0.3f, 0.3f);
                    st.bezelMat = MAT_METAL_BRUSHED; st.bezelCol = col(0.5f, 0.5f, 0.52f); st.floorMat = MAT_CAR_GLASS; st.floorCol = kCol1;
                    st.floorOff = 0.006f;
                    lampHousing(m, dc, rectO(dy[dd][0], dy[dd][1], 0.36f, 2.56f, 0.04f), st);
                    std::vector<vec2> ln;
                    ln.push_back(vec2((dy[dd][0] + dy[dd][1]) * 0.5f, 0.4f));
                    ln.push_back(vec2((dy[dd][0] + dy[dd][1]) * 0.5f, 2.52f));
                    m.use(MAT_METAL_BRUSHED, col(0.5f, 0.5f, 0.52f));
                    decalBar(m, dc, ln, 0.04f, 0.012f, 0.004f, 0.2f);
                }
            }
            // livery band along the skirt (secondary paint)
            sideStripe(m, b, yR + 0.3f, yF - 0.3f, 0.95f, 0.22f, MAT_CARPAINT, kCol2, 0.0012f);
        }
        // rear window + engine grille
        {
            Decal dc = endDecal(b, true, vec2(-1.3f, 0.5f), vec2(1.3f, 3.0f));
            glassPatch(m, dc, rectO(-0.9f, 0.9f, 2.0f, 2.62f, 0.06f), 0.03f);
            LampStyle st;
            st.height = 0.01f; st.bezel = 0.02f; st.wallMat = MAT_PLASTIC; st.wallCol = col(0.3f, 0.3f, 0.3f);
            st.floorMat = MAT_PLASTIC; st.floorCol = col(0.2f, 0.2f, 0.2f); st.floorOff = 0.004f;
            lampHousing(m, dc, rectO(-0.8f, 0.8f, 0.7f, 1.55f, 0.03f), st);
            m.use(MAT_PLASTIC, col(0.6f, 0.6f, 0.6f));
            for (int k = 0; k < 6; k++) {
                std::vector<vec2> ln;
                ln.push_back(vec2(-0.75f, 0.78f + k * 0.13f));
                ln.push_back(vec2(0.75f, 0.78f + k * 0.13f));
                decalBar(m, dc, ln, 0.03f, 0.01f, 0.004f, 0.2f);
            }
        }
        // roof: AC pod and hatches
        m.newGroup(35.f);
        m.use(MAT_CARPAINT, kCol1);
        roundedBoxAt(m, vec3(0, yF - 4.5f, 3.2f), vec3(0.9f, 1.3f, 0.14f), 0.12f, 2);
        m.use(MAT_PLASTIC, col(0.4f, 0.4f, 0.4f));
        for (int k = 0; k < 2; k++) roundedBoxAt(m, vec3(0, yR + 1.5f + k * 2.5f, 3.12f), vec3(0.35f, 0.35f, 0.03f), 0.03f, 1);
        // bus mirrors on arms
        m.use(MAT_PLASTIC, col(0.4f, 0.4f, 0.4f));
        for (int sg = -1; sg <= 1; sg += 2) {
            std::vector<vec3> arm;
            arm.push_back(vec3(sg * 1.2f, yF - 0.15f, 2.7f));
            arm.push_back(vec3(sg * 1.35f, yF + 0.25f, 2.75f));
            arm.push_back(vec3(sg * 1.42f, yF + 0.3f, 2.55f));
            tube1(m, catmull(arm, 3), 0.02f, 6, true);
            roundedBoxAt(m, vec3(sg * 1.42f, yF + 0.3f, 2.35f), vec3(0.05f, 0.05f, 0.2f), 0.03f, 1);
            m.use(MAT_CHROME, col(0.8f, 0.85f, 0.9f));
            roundedBoxAt(m, vec3(sg * 1.42f, yF + 0.25f, 2.35f), vec3(0.04f, 0.004f, 0.18f), 0.01f, 1);
            m.use(MAT_PLASTIC, col(0.4f, 0.4f, 0.4f));
        }
        // interior hint: a few seat backs and poles visible through the windows are skipped (glass is opaque)
    };
    physics(o, 12500.f, 220.f, 1200.f, 2300.f, 25.f, 6, 0.f, 0.85f, 0.16f, 2.0f, 0.70f, 0.f, vec3(0, -0.4f, 1.2f), Audio::ENGINE_TRUCK_DIESEL);
    CarBody b(d.s);
    PMesh m;
    d.L.maker = makerId(o.maker);
    plateText(d.L, o.name);
    d.L.logoR = 0.075f;
    carBodyParts(d, b, m, false);
    noseLogo(m, b, d.L);
    finalizeMesh(m, o.body);
    buildWheel(d.wd, o.wheel);
    wheelPair(o, s.wb * 0.5f, s.trackF, s.wheelR, s.wheelW, true, false);
    dualAxle(o, -s.wb * 0.5f, s.trackR, s.wheelR, s.wheelW, true);
    heavyLights(o, vec3(0.95f, yF, 0.72f), vec3(1.1f, yR, 0.95f));
    o.seats.push_back(SeatSpec{vec3(-0.75f, yF - 1.1f, 1.25f), true, true});
    for (int k = 0; k < 6; k++) o.seats.push_back(SeatSpec{vec3(k % 2 == 0 ? -0.7f : 0.7f, yF - 3.0f - k * 1.0f, 1.0f), false, false});
    setBox(o, 0.3f);
    o.frontalArea = 2.55f * 2.8f * 0.95f;
    o.fixedLivery = true;
    o.liveryPrimary = srgb(236, 236, 232);
    o.liverySecondary = srgb(0, 140, 150);
    o.paletteColors.push_back(o.liveryPrimary);
    o.spawnWeight = 1.f; o.price = 0;
}

}  // namespace detail
}  // namespace Vehicles
