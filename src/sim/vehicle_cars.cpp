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
    float zh = b.beltZAt(yD0) - 0.085f;
    if (d.s.doors >= 4) {
        doorHandle(m, b, yB + 0.10f, b.beltZAt(yB) - 0.085f, d.L.handlesChrome);
        doorHandle(m, b, yRearDoor + 0.12f, b.beltZAt(yRearDoor + 0.1f) - 0.085f, d.L.handlesChrome);
    } else {
        doorHandle(m, b, yRearDoor + 0.14f, b.beltZAt(yRearDoor + 0.1f) - 0.085f, d.L.handlesChrome);
    }
    (void)zh;
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

inline void buildCar(const CarDef& d, VehicleModel& out) {
    CarBody b(d.s);
    PMesh m;
    b.build(m);
    size_t f0 = m.F.size();
    auto stage = [&](const char* nm) { if (getenv("VM_STAGES")) printf("  stage %-12s %6zu faces\n", nm, m.F.size() - f0); f0 = m.F.size(); };
    stage("shell");
    const CarSpec& s = b.s;
    const CarLook& L = d.L;
    // ---- lamps (right) + mirror
    PMesh::Mark mk = m.mark();
    buildHeadlight(m, b, L);
    if (L.tail != TL_BAR || true) buildTaillight(m, b, L);
    carSideDetails(m, b, d);
    sideMirror(m, b, L.mirrorsBlack);
    m.mirrorX(mk);
    stage("lamps+side");
    if (L.tail == TL_BAR) buildRearBar(m, b, L);
    // ---- centre / full width items
    buildGrille(m, b, L);
    stage("grille");
    buildIntake(m, b, L);
    stage("intake");
    if (L.frontPlate) buildPlate(m, b, false, L.plateFZ, L);
    buildPlate(m, b, true, L.plateRZ, L);
    carTopSeams(m, b);
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
    {
        Frame fl = projLeft();
        float yf = b.yWr + b.Ra + (s.doors >= 4 ? 0.05f : 0.2f), zf = b.beltZAt(yf) - 0.17f;
        if (s.style == BS_PICKUP) yf = b.yWr + b.Ra + 0.12f;
        std::vector<vec2> fd = shapeRoundRect(vec2(yf, zf), 0.075f, 0.075f, 0.02f, 3);
        fd.push_back(fd[0]);
        seam(m, b.proj, fl, fd, 0.004f);
    }
    stage("misc");
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
    buildInterior(m, b, L, d.I);
    stage("interior");
    if (d.extra) d.extra(b, m);
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

}  // namespace detail
}  // namespace Vehicles
