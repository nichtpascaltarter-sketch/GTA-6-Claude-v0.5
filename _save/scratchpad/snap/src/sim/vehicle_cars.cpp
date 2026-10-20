// Passenger car assembly: lofted body + projected details + interior + wheels + physical metadata.
namespace Vehicles {
namespace detail {

struct CarDef {
    CarSpec s;
    CarLook L;
    InteriorLayout I;
    WheelDesign wd;
    bool twoDoorSeams = false;
    bool fuelLeft = true;
    bool carSeams = true, fuelDoor = true, mirrors = true, rearPlate = true;
    std::function<void(CarBody&, PMesh&)> extra;  // model-specific additions (full, not mirrored)
};

inline Frame projLeft() { return Frame(vec3(0, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(1, 0, 0)); }

// Door / hood / trunk seams, handles and markers on the right side (mirrored by caller)
inline void carSideDetails(PMesh& m, CarBody& b, const CarDef& d) {
    const CarSpec& s = b.s;
    Frame fr = projRight();
    float yD0 = Min(s.yCowl - 0.04f, b.yWf - b.Ra - 0.10f);
    float zLow = s.zSill + 0.075f;
    m.newGroup(40.f);
    auto vline = [&](float y, float zTop, float zBot) {
        std::vector<vec2> l;
        l.push_back(vec2(y, zTop));
        l.push_back(vec2(y - 0.01f, zBot));
        seam(m, b.proj, fr, l);
    };
    bool pickup = s.style == BS_PICKUP;
    float yRearDoor = Max(s.dloRearBot + 0.02f, b.yWr + b.Ra + 0.12f);
    if (d.s.doors == 2) yRearDoor = Max(s.dloRearBot + 0.02f, b.yWr + b.Ra + 0.15f);
    if (pickup) yRearDoor = s.yDeck + 0.03f;
    float yB = s.bPillar;
    // front edge of the front door
    vline(yD0, b.beltZAt(yD0) - 0.012f, zLow);
    if (d.s.doors >= 4) vline(yB, b.beltZAt(yB) - 0.012f, zLow);
    // rear edge of the last door: straight down, then around the rear arch if close
    {
        std::vector<vec2> l;
        float zt = b.beltZAt(yRearDoor) - 0.012f;
        float archTopZ = s.wheelR + b.Ra + 0.06f;
        float yArchF = b.yWr + b.Ra + 0.06f;
        if (yRearDoor < yArchF + 0.1f) {
            l.push_back(vec2(yRearDoor, zt));
            l.push_back(vec2(yRearDoor, archTopZ + 0.04f));
            // follow the arch offset circle forwards/down to the sill
            float R2 = b.Ra + 0.06f;
            float a0 = acosf(Clamp((yRearDoor - b.yWr) / R2, -1.f, 1.f));
            for (int k = 0; k <= 6; k++) {
                float a = lerp(a0, 0.25f, k / 6.f);
                l.push_back(vec2(b.yWr + cosf(a) * R2, s.wheelR + sinf(a) * R2));
            }
            l.push_back(vec2(b.yWr + R2, zLow));
        } else {
            l.push_back(vec2(yRearDoor, zt));
            l.push_back(vec2(yRearDoor - 0.01f, zLow));
        }
        seam(m, b.proj, fr, l);
    }
    // bottom of the doors
    {
        std::vector<vec2> l;
        l.push_back(vec2(yD0 - 0.01f, zLow));
        l.push_back(vec2(Min(yRearDoor, b.yWr + b.Ra + 0.06f), zLow));
        seam(m, b.proj, fr, l);
    }
    // front bumper / fender seam just in front of the front arch
    {
        std::vector<vec2> l;
        float y = b.yWf + b.Ra + 0.06f;
        l.push_back(vec2(y, s.wheelR + 0.02f));
        l.push_back(vec2(y + 0.05f, s.zHoodF - 0.14f));
        seam(m, b.proj, fr, l);
    }
    // rear bumper seam behind the rear arch
    {
        std::vector<vec2> l;
        float y = b.yWr - b.Ra - 0.06f;
        l.push_back(vec2(y, s.wheelR + 0.02f));
        l.push_back(vec2(y - 0.06f, s.zTailTop - 0.1f));
        l.push_back(vec2(b.yR + 0.05f, s.zTailTop - 0.1f));
        seam(m, b.proj, fr, l);
    }
    // handles
    if (d.s.doors >= 4) {
        doorHandle(m, b, yB + 0.10f, b.beltZAt(yB) - 0.085f, d.L.handlesChrome);
        doorHandle(m, b, yRearDoor + 0.12f, b.beltZAt(yRearDoor + 0.1f) - 0.085f, d.L.handlesChrome);
    } else {
        doorHandle(m, b, yRearDoor + 0.14f, b.beltZAt(yRearDoor + 0.1f) - 0.085f, d.L.handlesChrome);
    }
    // side markers (amber front, red rear)
    if (d.L.sideMarkers) {
        Decal dc;
        dc.pr = &b.proj;
        dc.fr = fr;
        dc.back = 3.f;
        float yf = b.yF - 0.22f, zf = s.zNoseTop - 0.07f;
        decalRange(dc, vec2(yf - 0.1f, zf - 0.1f), vec2(yf + 0.1f, zf + 0.1f));
        m.newGroup(40.f);
        m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.55f, 0.05f, 1.f));
        loopFill(m, dc, shapeRoundRect(vec2(yf, zf), 0.035f, 0.012f, 0.008f, 2), 0.003f, 2);
        float yr = b.yR + 0.2f, zr = s.zTailTop - 0.06f;
        decalRange(dc, vec2(yr - 0.1f, zr - 0.1f), vec2(yr + 0.1f, zr + 0.1f));
        m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));
        loopFill(m, dc, shapeRoundRect(vec2(yr, zr), 0.035f, 0.012f, 0.008f, 2), 0.003f, 2);
    }
    if (d.L.rockerSkirt) {
        Decal dc;
        dc.pr = &b.proj;
        dc.fr = fr;
        dc.back = 3.f;
        float y0 = b.yWr + b.Ra + 0.02f, y1 = b.yWf - b.Ra - 0.02f;
        decalRange(dc, vec2(y0, s.zSill - 0.1f), vec2(y1, s.zSill + 0.2f));
        m.newGroup(40.f);
        m.use(MAT_CAR_GLASS, kCol1);
        std::vector<vec2> l;
        l.push_back(vec2(y0, s.zSill + 0.05f));
        l.push_back(vec2(y1, s.zSill + 0.05f));
        decalBar(m, dc, l, 0.07f, 0.02f, -0.004f, 0.08f);
    }
    if (d.L.sideIntake) {
        Decal dc;
        dc.pr = &b.proj;
        dc.fr = fr;
        dc.back = 3.f;
        float yc = b.yWr + b.Ra + 0.28f, zc = s.zWide + 0.05f;
        std::vector<vec2> c;
        c.push_back(vec2(yc - 0.22f, zc - 0.10f));
        c.push_back(vec2(yc + 0.18f, zc - 0.06f));
        c.push_back(vec2(yc + 0.02f, zc + 0.12f));
        c.push_back(vec2(yc - 0.22f, zc + 0.12f));
        std::vector<vec2> O = resampleClosed(shapeRounded(c, 0.04f, 3), 40);
        decalRange(dc, vec2(yc - 0.4f, zc - 0.3f), vec2(yc + 0.4f, zc + 0.3f));
        LampStyle st;
        st.height = 0.012f;
        st.bezel = 0.01f;
        st.wallMat = MAT_CARPAINT;
        st.wallCol = kCol1;
        st.bezelMat = MAT_CARPAINT;
        st.bezelCol = kCol1;
        st.floorMat = MAT_PLASTIC;
        st.floorCol = col(0.15f, 0.15f, 0.15f);
        st.floorOff = 0.001f;
        lampHousing(m, dc, O, st);
    }
    if (d.L.fenderVent) {
        Decal dc;
        dc.pr = &b.proj;
        dc.fr = fr;
        dc.back = 3.f;
        float yc = b.yWf - b.Ra - 0.12f, zc = s.zWide + 0.1f;
        decalRange(dc, vec2(yc - 0.2f, zc - 0.2f), vec2(yc + 0.2f, zc + 0.2f));
        m.newGroup(40.f);
        m.use(MAT_CAR_GLASS, kCol1);
        for (int k = 0; k < 3; k++) {
            std::vector<vec2> l;
            l.push_back(vec2(yc + 0.08f - k * 0.02f, zc + 0.06f - k * 0.03f));
            l.push_back(vec2(yc - 0.08f - k * 0.02f, zc + 0.03f - k * 0.03f));
            decalBar(m, dc, l, 0.012f, 0.006f, 0.f, 0.04f);
        }
    }
}

// Hood / trunk / tailgate seams (full width, called once)
inline void carTopSeams(PMesh& m, CarBody& b) {
    const CarSpec& s = b.s;
    Frame ft = projTop();
    // hood side seams and leading edge
    float yh0 = s.yCowl + s.cowlLen + 0.005f, yh1 = b.yHF + 0.03f;
    if (s.style != BS_VAN && s.style != BS_BOXY) {
        for (int sg = -1; sg <= 1; sg += 2) {
            std::vector<vec2> l;
            for (int k = 0; k <= 6; k++) {
                float y = lerp(yh0, yh1, k / 6.f);
                int i = b.rowAt(y);
                l.push_back(vec2(sg * (b.rowXw[i] - 0.03f), y));
            }
            seam(m, b.proj, ft, l);
        }
        std::vector<vec2> l;
        int i = b.rowAt(yh1);
        float w = b.rowXw[i] - 0.03f;
        for (int k = 0; k <= 8; k++) l.push_back(vec2(lerp(-w, w, k / 8.f), yh1 + 0.012f * (1.f - Sq(lerp(-1.f, 1.f, k / 8.f)))));
        seam(m, b.proj, ft, l);
    }
    // trunk lid (3-box) or tailgate (2-box)
    if (s.style == BS_SEDAN || s.style == BS_COUPE) {
        float y0 = s.yDeck - 0.03f, y1 = b.yTE;
        for (int sg = -1; sg <= 1; sg += 2) {
            std::vector<vec2> l;
            for (int k = 0; k <= 5; k++) {
                float y = lerp(y0, y1, k / 5.f);
                int i = b.rowAt(y);
                l.push_back(vec2(sg * (b.rowXw[i] - 0.03f), y));
            }
            seam(m, b.proj, ft, l);
        }
        std::vector<vec2> l;
        int i = b.rowAt(y0);
        float w = b.rowXw[i] - 0.03f;
        l.push_back(vec2(-w, y0));
        l.push_back(vec2(w, y0));
        seam(m, b.proj, ft, l);
        // rear edge of the lid on the rear face
        Frame fr = projRear();
        std::vector<vec2> r;
        float z = s.zTailTop + 0.02f;
        float wr = b.planW(b.yR + 0.1f) * 0.78f;
        r.push_back(vec2(-wr, z));
        r.push_back(vec2(wr, z));
        seam(m, b.proj, fr, r);
    } else if (s.style != BS_PICKUP && s.style != BS_ROADSTER) {
        Frame fr = projRear();
        float wr = b.planW(b.yR + 0.05f) * 0.80f;
        float zb = s.zTailTop - 0.02f, zt = s.zDeck - 0.02f;
        std::vector<vec2> r;
        r.push_back(vec2(-wr, zt));
        r.push_back(vec2(-wr, zb));
        r.push_back(vec2(wr, zb));
        r.push_back(vec2(wr, zt));
        seam(m, b.proj, fr, r);
    }
}

inline void setWheels(VehicleModel& out, const CarSpec& s, float driveFront) {
    float r = s.wheelR;
    bool fDrive = driveFront > 0.01f, rDrive = driveFront < 0.99f;
    out.wheels.push_back(WheelSpec{vec3(-s.trackF, s.wb * 0.5f, r), r, s.wheelW, true, fDrive, true});
    out.wheels.push_back(WheelSpec{vec3(s.trackF, s.wb * 0.5f, r), r, s.wheelW, true, fDrive, false});
    out.wheels.push_back(WheelSpec{vec3(-s.trackR, -s.wb * 0.5f, r), r, s.wheelW, false, rDrive, true});
    out.wheels.push_back(WheelSpec{vec3(s.trackR, -s.wb * 0.5f, r), r, s.wheelW, false, rDrive, false});
}

inline void addLight(VehicleModel& out, vec3 p, vec3 d, LightType t) { out.lights.push_back(LightSpec{p, normalize(d), t}); }

// Surface point for a front/rear light centre (for metadata)
inline vec3 lightAnchor(CarBody& b, vec2 xz, bool rear) {
    Frame fr = rear ? projRear() : projFront();
    Decal dc;
    if (decalAt(dc, b.proj, fr, vec3(xz.x, rear ? b.yR - 0.5f : b.yF + 0.5f, xz.y), vec3(0, rear ? 1.f : -1.f, 0))) return dc.fr.o;
    return vec3(xz.x, rear ? b.yR : b.yF, xz.y);
}

// Body shell + all projected details + interior (no wheels / metadata). Returns the built body helper.
inline void carBodyParts(const CarDef& d, CarBody& b, PMesh& m, bool interior = true);

// Lettering shown on the tail: the model name without fleet/package suffixes.
inline std::string badgeText(const std::string& name) {
    const char* const kSuffix[] = {" Patrol", " Pursuit", " Cab", " Sheriff", " Trooper", " Utility"};
    std::string s = name;
    for (const char* suf : kSuffix) {
        size_t n = strlen(suf);
        if (s.size() > n && s.compare(s.size() - n, n, suf) == 0) s.resize(s.size() - n);
    }
    return s;
}

inline void buildCar(const CarDef& def, VehicleModel& out) {
    CarDef d = def;
    d.L.maker = makerId(out.maker);
    CarBody b(d.s);
    PMesh m;
    carBodyParts(d, b, m, true);
    if (d.L.grille == GR_NONE) noseLogo(m, b, d.L);
    rearBadges(m, b, d.L, badgeText(out.name));
    const CarSpec& s = b.s;
    const CarLook& L = d.L;
    finalizeMesh(m, out.body);
    buildWheel(d.wd, out.wheel);
    // ---- metadata
    setWheels(out, s, out.driveFront);
    vec3 hl = lightAnchor(b, L.headC, false);
    vec3 tl = lightAnchor(b, L.tailC, true);
    for (int sg = -1; sg <= 1; sg += 2) {
        vec3 h(hl.x * sg, hl.y, hl.z), t(tl.x * sg, tl.y, tl.z);
        addLight(out, h, vec3(0, 1, -0.03f), LT_HEAD);
        addLight(out, t, vec3(0, -1, 0), LT_TAIL);
        addLight(out, t, vec3(0, -1, 0), LT_BRAKE);
        addLight(out, t + vec3(-sg * 0.08f, 0.01f, -0.03f), vec3(0, -1, 0), LT_REVERSE);
        addLight(out, h + vec3(sg * 0.1f, -0.03f, -0.02f), vec3(sg * 0.3f, 1, 0), sg < 0 ? LT_INDICATOR_L : LT_INDICATOR_R);
        addLight(out, t + vec3(sg * 0.05f, 0, 0), vec3(sg * 0.3f, -1, 0), sg < 0 ? LT_INDICATOR_L : LT_INDICATOR_R);
    }
    float fx = d.I.seatX;
    float hz = d.I.zFloor + d.I.hipH;
    out.seats.push_back(SeatSpec{vec3(-fx, d.I.yHipF, hz), true, true});
    out.seats.push_back(SeatSpec{vec3(fx, d.I.yHipF, hz), false, false});
    if (d.I.rearSeat) {
        out.seats.push_back(SeatSpec{vec3(-fx, d.I.yHipR, hz - 0.02f), false, true});
        out.seats.push_back(SeatSpec{vec3(fx, d.I.yHipR, hz - 0.02f), false, false});
    }
    AABB bb = out.body.bounds;
    out.boxCenter = vec3(0, (bb.mn.y + bb.mx.y) * 0.5f, (s.zSill + bb.mx.z) * 0.5f);
    out.boxHalf = vec3(s.halfW, (bb.mx.y - bb.mn.y) * 0.5f, (bb.mx.z - s.zSill) * 0.5f);
    out.frontalArea = (2.f * s.halfW) * (s.zRoof - s.zSill) * 0.84f;
}

inline void carBodyParts(const CarDef& d, CarBody& b, PMesh& m, bool interior) {
    b.s = d.s;
    b.build(m);
    const CarSpec& s = b.s;
    const CarLook& L = d.L;
    // ---- lamps (right) + mirror
    PMesh::Mark mk = m.mark();
    buildHeadlight(m, b, L);
    buildTaillight(m, b, L);  // light-bar tails keep corner lamps; the bar itself is added below
    if (d.carSeams) carSideDetails(m, b, d);
    if (d.mirrors) sideMirror(m, b, L.mirrorsBlack);
    m.mirrorX(mk);
    if (L.tail == TL_BAR) buildRearBar(m, b, L);
    // ---- centre / full width items
    buildGrille(m, b, L);
    buildIntake(m, b, L);
    if (L.frontPlate) buildPlate(m, b, false, L.plateFZ, L);
    if (d.rearPlate) buildPlate(m, b, true, L.plateRZ, L);
    if (d.carSeams) carTopSeams(m, b);
    if (!s.openTop) wipers(m, b);
    exhausts(m, b, L);
    if (L.antennaFin && !s.openTop && s.style != BS_PICKUP) antennaFin(m, b);
    spoilerWing(m, b, L);
    if (L.spoiler == SP_ROOF) roofSpoiler(m, b);
    if (L.chromeBumpers || L.blackBumpers) {
        bumperBar(m, b, false, L.bumperFZ > 0.f ? L.bumperFZ : s.zNoseBot + 0.12f, s.halfW * 0.93f, L.chromeBumpers);
        bumperBar(m, b, true, L.bumperRZ > 0.f ? L.bumperRZ : s.zTailBot + 0.1f, s.halfW * 0.93f, L.chromeBumpers);
    }
    if (L.spareWheel) spareWheel(m, b, d.wd.R, d.wd.W);
    if (L.roof == RX_POLICE) policeBar(m, b, (s.yRoofF + s.yRoofR) * 0.5f + 0.1f, Min(b.railXAt((s.yRoofF + s.yRoofR) * 0.5f) + 0.05f, 0.62f));
    if (L.roof == RX_TAXI) taxiSign(m, b, (s.yRoofF + s.yRoofR) * 0.5f);
    if (L.towHitch) {
        m.newGroup(40.f);
        m.use(MAT_METAL_PAINTED, col(0.1f, 0.1f, 0.1f));
        roundedBoxAt(m, vec3(0, b.yR - 0.08f, s.zRearLow + 0.02f), vec3(0.035f, 0.14f, 0.035f), 0.005f, 1);
        m.use(MAT_CHROME, kCol1);
        ellipsoid(m, Frame(vec3(0, b.yR - 0.2f, s.zRearLow + 0.08f), vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1)), vec3(0.025f), 10, 6);
        cyl(m, vec3(0, b.yR - 0.2f, s.zRearLow + 0.035f), vec3(0, b.yR - 0.2f, s.zRearLow + 0.06f), 0.012f, 8);
    }
    if (L.hoodScoop) hoodScoop(m, b);
    if (L.roof == RX_RAILS) roofRails(m, b);
    if (L.bullBar) bullBar(m, b);
    if (L.spotLamp) spotLamp(m, b);
    // fuel door (left rear quarter)
    if (d.fuelDoor) {
        Frame fl = projLeft();
        float yf = b.yWr + b.Ra + (s.doors >= 4 ? 0.05f : 0.2f), zf = b.beltZAt(yf) - 0.17f;
        if (s.style == BS_PICKUP) yf = b.yWr + b.Ra + 0.12f;
        std::vector<vec2> fd = shapeRoundRect(vec2(yf, zf), 0.075f, 0.075f, 0.02f, 3);
        fd.push_back(fd[0]);
        seam(m, b.proj, fl, fd, 0.004f);
    }
    // pickup bed: wheel tubs over the rear arches
    if (s.recDepth > 0.f && !s.cockpit) {
        int ir = b.rowAt(b.yWr);
        float floorZ = b.G[ir * b.NP + b.NP - 1].z;
        float archTopZ = s.wheelR + b.Ra + 0.06f;
        if (archTopZ > floorZ) {
            float wallX = b.G[ir * b.NP + b.pRail0].x;
            float xIn = s.trackR - s.wheelW * 0.5f - 0.05f;
            m.newGroup(35.f);
            m.use(MAT_PLASTIC, col(0.7f, 0.7f, 0.7f));
            for (int sg = -1; sg <= 1; sg += 2) {
                float x0 = Min(xIn, wallX - 0.02f), x1 = wallX + 0.01f;
                roundedBoxAt(m, vec3(sg * (x0 + x1) * 0.5f, b.yWr, (floorZ + archTopZ) * 0.5f - 0.01f),
                             vec3((x1 - x0) * 0.5f, b.Ra + 0.04f, (archTopZ - floorZ) * 0.5f + 0.01f), 0.04f, 2);
            }
        }
    }
    // ---- interior
    if (interior) buildInterior(m, b, L, d.I);
    if (d.extra) d.extra(b, m);
}

// ------------------------------------------------------------------------------------------------
// Factory paint palettes (authored in sRGB, stored linear)
inline std::vector<vec3> palette(const char* kind) {
    std::vector<vec3> p;
    auto add = [&](int r, int g, int b) { p.push_back(srgb((float)r, (float)g, (float)b)); };
    std::string k = kind;
    if (k == "common") {
        add(236, 236, 234); add(200, 202, 205); add(150, 152, 156); add(78, 80, 84); add(18, 18, 20); add(120, 20, 24);
        add(28, 44, 86); add(170, 160, 140); add(60, 70, 58); add(90, 120, 150); add(210, 205, 190); add(110, 40, 30);
    } else if (k == "lux") {
        add(12, 12, 14); add(230, 230, 228); add(160, 162, 166); add(52, 54, 60); add(20, 30, 58); add(70, 20, 26);
        add(110, 104, 96); add(40, 48, 44);
    } else if (k == "sport") {
        add(190, 16, 18); add(245, 196, 0); add(20, 60, 150); add(240, 240, 240); add(12, 12, 12); add(230, 100, 10);
        add(40, 140, 80); add(120, 125, 130); add(90, 30, 120); add(0, 150, 170);
    } else if (k == "muscle") {
        add(180, 20, 20); add(20, 20, 20); add(245, 170, 0); add(30, 80, 160); add(80, 110, 60); add(230, 230, 225);
        add(200, 90, 20); add(90, 20, 40);
    } else if (k == "truck") {
        add(240, 240, 238); add(20, 20, 22); add(150, 152, 156); add(120, 20, 22); add(30, 50, 90); add(90, 70, 50);
        add(60, 70, 58); add(200, 170, 110); add(80, 82, 86);
    } else if (k == "classic") {
        add(180, 60, 40); add(90, 140, 160); add(210, 190, 140); add(60, 90, 60); add(230, 225, 210); add(120, 30, 30);
        add(40, 60, 110); add(200, 120, 40);
    } else if (k == "fleet") {
        add(240, 240, 238); add(245, 245, 245); add(200, 202, 205); add(230, 230, 228);
    } else if (k == "bike") {
        add(12, 12, 12); add(190, 16, 18); add(20, 60, 150); add(240, 240, 240); add(245, 196, 0); add(40, 140, 80); add(120, 125, 130);
    } else if (k == "scooter") {
        add(240, 240, 238); add(160, 210, 200); add(240, 150, 150); add(250, 220, 120); add(20, 20, 22); add(120, 170, 220);
    } else if (k == "boat") {
        add(240, 240, 238); add(20, 40, 90); add(150, 20, 20); add(20, 20, 22); add(0, 120, 150); add(200, 200, 205); add(230, 180, 30);
    } else if (k == "air") {
        add(240, 240, 238); add(200, 30, 30); add(20, 50, 120); add(230, 180, 30); add(40, 40, 44);
    } else {
        add(200, 200, 200);
    }
    return p;
}

inline void physics(VehicleModel& o, float mass, float kW, float nm, float rpm, float top, int gears, float driveF, float grip,
                    float travel, float stiff, float cd, float down, vec3 com, int engine) {
    o.mass = mass;
    o.power = kW;
    o.torque = nm;
    o.maxRpm = rpm;
    o.topSpeed = top;
    o.gears = gears;
    o.driveFront = driveF;
    o.grip = grip;
    o.suspensionTravel = travel;
    o.suspensionStiffness = stiff;
    o.dragCoef = cd;
    o.downforce = down;
    o.centerOfMass = com;
    o.engineSound = engine;
    o.brakeForce = mass * (grip > 1.1f ? 8.0f : 6.5f);
}

// ------------------------------------------------------------------------------------------------
// Liveries: text & stripes placed on both sides (not mirrored)
inline void sideText(PMesh& m, CarBody& b, const char* text, float y, float z, float h, u8 mat, u32 c) {
    for (int side = 0; side < 2; side++) {
        Frame fr = side == 0 ? projRight() : projLeft();
        // text must read front-to-back on the left, back-to-front on the right
        if (side == 1) fr.x = vec3(0, -1, 0);
        Decal dc;
        dc.pr = &b.proj;
        dc.fr = fr;
        dc.back = 3.f;
        float u = side == 1 ? -y : y;
        float w = strlen(text) * h * 0.72f;
        decalRange(dc, vec2(u - w * 0.6f - 0.1f, z - h), vec2(u + w * 0.6f + 0.1f, z + h));
        m.newGroup(40.f);
        m.use(mat, c);
        textDecal(m, dc, text, vec2(u, z), h);
    }
}
inline void sideStripe(PMesh& m, CarBody& b, float y0, float y1, float z, float w, u8 mat, u32 c, float off = 0.0012f) {
    for (int side = 0; side < 2; side++) {
        Frame fr = side == 0 ? projRight() : projLeft();
        Decal dc;
        dc.pr = &b.proj;
        dc.fr = fr;
        dc.back = 3.f;
        decalRange(dc, vec2(y0, z - w), vec2(y1, z + w));
        std::vector<vec2> l;
        l.push_back(vec2(y0, z));
        l.push_back(vec2(y1, z));
        m.newGroup(40.f);
        m.use(mat, c);
        stripDecal(m, dc, l, w, off, 0.05f);
    }
}
// Star badge (6-point) on the front doors
inline void doorStar(PMesh& m, CarBody& b, float y, float z, float r, vec3 tint) {
    for (int side = 0; side < 2; side++) {
        Frame fr = side == 0 ? projRight() : projLeft();
        Decal dc;
        dc.pr = &b.proj;
        dc.fr = fr;
        dc.back = 3.f;
        std::vector<vec2> st;
        for (int k = 0; k < 12; k++) {
            float a = kHalfPi + kTwoPi * k / 12.f;
            float rr = (k & 1) ? r * 0.5f : r;
            st.push_back(vec2(y + cosf(a) * rr, z + sinf(a) * rr));
        }
        decalRange(dc, vec2(y - r, z - r), vec2(y + r, z + r));
        m.newGroup(40.f);
        m.use(MAT_METAL_PAINTED, colv(tint));
        loopFill(m, dc, st, 0.0018f, 2);
    }
}

}  // namespace detail
}  // namespace Vehicles
