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
    if (d.L.chromeBelt) {
        // chrome body-side molding low on the doors, between the arches
        Decal dc;
        dc.pr = &b.proj;
        dc.fr = fr;
        dc.back = 3.f;
        float y0 = b.yWr + b.Ra + 0.1f, y1 = b.yWf - b.Ra - 0.1f, z = s.zSill + 0.13f;
        decalRange(dc, vec2(y0 - 0.05f, z - 0.1f), vec2(y1 + 0.05f, z + 0.1f));
        std::vector<vec2> l;
        for (int k = 0; k <= 12; k++) l.push_back(vec2(lerp(y0, y1, k / 12.f), z));
        m.newGroup(40.f);
        m.use(MAT_CHROME, kCol1);
        decalBar(m, dc, l, 0.012f, 0.003f, 0.f, 0.08f);
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

// Rear bumper reflectors, parking sensors (front and rear) and the high-mounted third brake lamp.
inline void rearSmallParts(PMesh& m, CarBody& b, const CarDef& d) {
    const CarSpec& s = b.s;
    const CarLook& L = d.L;
    if (s.style == BS_BOXY) return;
    m.newGroup(40.f);
    // red retro-reflectors low on the rear bumper corners (they light with the tail lamps' red)
    for (int sg = -1; sg <= 1; sg += 2) {
        Frame fr = projRear(0.45f * sg, 0.f);
        Decal dc;
        float x = sg * s.halfW * 0.8f, z = s.zTailBot + 0.07f;
        if (!decalAt(dc, b.proj, fr, vec3(x, b.yR - 0.5f, z), vec3(0, 1, 0))) continue;
        decalRange(dc, vec2(-0.1f, -0.05f), vec2(0.1f, 0.05f));
        m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));
        loopFill(m, dc, shapeRoundRect(vec2(0, 0), 0.055f, 0.011f, 0.006f, 2), 0.0025f, 1);
    }
    // parking sensors: small flush discs with a dark ring (body colour)
    for (int e = 0; e < 2; e++) {
        bool rear = e == 1;
        if (L.chromeBumpers || L.blackBumpers) break;
        float z = rear ? s.zTailBot + 0.13f : s.zNoseBot + 0.07f;
        for (int k = 0; k < 4; k++) {
            float u = (k - 1.5f) * s.halfW * 0.34f;
            Frame fr = rear ? projRear(0.f, 0.f) : projFront(0.f, 0.f);
            Decal dc;
            if (!decalAt(dc, b.proj, fr, vec3(u, rear ? b.yR - 0.5f : b.yF + 0.5f, z), vec3(0, rear ? 1.f : -1.f, 0))) continue;
            decalRange(dc, vec2(-0.03f, -0.03f), vec2(0.03f, 0.03f));
            m.use(MAT_PLASTIC, col(0.1f, 0.1f, 0.1f));
            std::vector<Samp> r0, r1;
            sampleLoop(dc, shapeEllipse(vec2(0, 0), 0.0115f, 0.0115f, 10), r0);
            sampleLoop(dc, shapeEllipse(vec2(0, 0), 0.0095f, 0.0095f, 10), r1);
            loopBand(m, r0, 0.0012f, r1, 0.0012f, FM_NORMAL, dc.fr.o);
            m.use(MAT_CARPAINT, kCol1);
            loopFill(m, dc, shapeEllipse(vec2(0, 0), 0.0095f, 0.0095f, 10), 0.0014f, 1);
        }
    }
    // third brake lamp: a slim red bar just inside the top of the rear window (sedans, hatches, wagons, SUVs)
    if (s.rearGlass && !s.openTop && s.style != BS_PICKUP) {
        float y = s.yRoofR - 0.03f;
        float z = b.roofZAt(y) - 0.028f;
        m.use(MAT_PLASTIC, col(0.3f, 0.3f, 0.3f));
        roundedBoxAt(m, vec3(0, y, z), vec3(0.13f, 0.02f, 0.012f), 0.006f, 1);
        m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));
        roundedBoxAt(m, vec3(0, y - 0.021f, z), vec3(0.115f, 0.002f, 0.006f), 0.f, 1);
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
    plateText(d.L, out.name);
    if (d.L.chromeBelt) d.s.dloTrim = 2;  // chrome window surround
    if (d.L.signature == 0) {
        u32 h = 2166136261u;
        for (char c : out.maker + out.name) h = (h ^ (u8)c) * 16777619u;
        d.L.signature = (u8)(1 + (h >> 7) % 4u);
    }
    CarBody b(d.s);
    PMesh m;
    carBodyParts(d, b, m, true);
    if (lodLevel() == 0) {
        if (d.L.grille == GR_NONE) noseLogo(m, b, d.L);
        rearBadges(m, b, d.L, badgeText(out.name));
    }
    const CarSpec& s = b.s;
    const CarLook& L = d.L;
    finalizeMesh(m, out.body);
    d.wd.maker = d.L.maker;
    buildWheel(d.wd, out.wheel);
    {
        WheelDesign wc = d.wd;
        bool fast = out.cls == VC_SPORTS || out.cls == VC_SUPER;
        if (fast && wc.caliperTint.x < 0.2f) wc.caliperTint = (out.name.size() & 1) ? vec3(0.62f, 0.05f, 0.04f) : vec3(0.85f, 0.62f, 0.05f);
        buildCaliper(wc, out.caliper);
    }
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

// ------------------------------------------------------------------------------------------------
// Distant levels of detail: the same shell at lower resolution with flat lamp / grille patches, no seams, badges or
// small hardware; LOD1 keeps seats, dash and the cabin closure, LOD2 only the closed cabin shell.
inline void lampPatchLod(PMesh& m, CarBody& b, const CarLook& L, bool rear, int lod) {
    Frame fr = rear ? projRear(L.tailYaw, 0.f) : projFront(L.headYaw, L.headPitch);
    vec2 c = rear ? L.tailC : L.headC;
    vec3 guess(c.x, rear ? b.yR - 0.5f : b.yF + 0.5f, c.y);
    if (!rear) fr.o = guess;
    Decal dc;
    if (!decalAt(dc, b.proj, fr, guess, vec3(0, rear ? 1.f : -1.f, 0))) return;
    float hw = rear ? L.tailW : L.headW, hh = rear ? L.tailH : L.headH;
    bool round = rear ? L.tail == TL_ROUND : (L.head == HL_ROUND || L.head == HL_POP || L.head == HL_QUAD);
    int n = lod == 1 ? 14 : 8;
    std::vector<vec2> O = round ? shapeEllipse(vec2(0, 0), hh, hh, n)
                                : resampleClosed(shapeRoundRect(vec2(0, 0), hw, hh, Min(hw, hh) * 0.45f, 2), n);
    decalRange(dc, vec2(-hw - 0.05f, -hh - 0.05f), vec2(hw + 0.05f, hh + 0.05f));
    m.newGroup(30.f);
    if (rear) m.use(MAT_LIGHT_TAIL, col(1.f, 0.f, 0.f));
    else m.use(MAT_LIGHT_HEAD, kCol1);
    loopFill(m, dc, O, 0.004f, 1);
    if (lod == 1 && rear && (L.tail == TL_WRAP || L.tail == TL_SLIM || L.tail == TL_VERT)) {
        // smoked centre as on the close-up lamp, so the lit area matches across the LOD switch
        std::vector<vec2> in = insetClosed(O, Min(hw, hh) * 0.45f);
        m.use(MAT_CAR_GLASS, kCol1);
        loopFill(m, dc, in.size() > 10 ? resampleClosed(in, 10) : in, 0.0052f, 1);
    }
    if (lod == 1 && !rear) {
        // turn signal at the outer end of the lamp
        std::vector<vec2> a = resampleClosed(shapeRoundRect(vec2(hw * 0.78f, -hh * 0.2f), Min(hw * 0.2f, 0.03f), hh * 0.5f, 0.008f, 2), 8);
        m.use(MAT_LIGHT_INDICATOR, col(1.f, 0.55f, 0.05f));
        loopFill(m, dc, a, 0.006f, 1);
    }
}
inline void panelPatchLod(PMesh& m, CarBody& b, bool rear, vec2 c, float hw, float hh, u8 mat, u32 color, float off) {
    Frame fr = rear ? projRear() : projFront();
    Decal dc;
    vec3 guess(c.x, rear ? b.yR - 0.5f : b.yF + 0.5f, c.y);
    if (!decalAt(dc, b.proj, fr, guess, vec3(0, rear ? 1.f : -1.f, 0))) return;
    decalRange(dc, vec2(-hw - 0.05f, -hh - 0.05f), vec2(hw + 0.05f, hh + 0.05f));
    m.use(mat, color);
    loopFill(m, dc, resampleClosed(shapeRoundRect(vec2(0, 0), hw, hh, Min(hw, hh) * 0.3f, 1), 10), off, 1);
}
// Pickup bed: wheel tubs over the rear arches (they hide the arch wells from inside the bed).
inline void bedTubs(PMesh& m, CarBody& b) {
    const CarSpec& s = b.s;
    if (s.recDepth <= 0.f || s.cockpit) return;
    int ir = b.rowAt(b.yWr);
    float floorZ = b.G[ir * b.NP + b.NP - 1].z;
    float archTopZ = s.wheelR + b.Ra + 0.06f;
    if (archTopZ <= floorZ) return;
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

inline void carBodyPartsLod(const CarDef& d, CarBody& b, PMesh& m, bool interior, int lod) {
    const CarSpec& s = b.s;
    const CarLook& L = d.L;
    PMesh::Mark mk = m.mark();
    lampPatchLod(m, b, L, false, lod);
    if (L.tail != TL_BAR) lampPatchLod(m, b, L, true, lod);
    if (d.mirrors && lod == 1) sideMirror(m, b, L.mirrorsBlack);
    m.mirrorX(mk);
    m.newGroup(30.f);
    if (L.tail == TL_BAR) {
        CarLook bar = L;
        bar.tailC = vec2(0.f, L.tailC.y);
        bar.tailW = L.tailC.x + L.tailW * 0.5f;
        bar.tailH = Max(L.tailH * 0.6f, 0.02f);
        bar.tail = TL_SLIM;
        lampPatchLod(m, b, bar, true, lod);
    }
    if (L.grille != GR_NONE)
        panelPatchLod(m, b, false, vec2(0.f, (L.grilleTop + L.grilleBot) * 0.5f), L.grilleW, (L.grilleTop - L.grilleBot) * 0.5f,
                      L.grilleChrome && lod == 1 ? MAT_CHROME : MAT_PLASTIC, col(0.25f, 0.25f, 0.25f), 0.004f);
    if (L.intakeW > 0.f && lod == 1)
        panelPatchLod(m, b, false, vec2(0.f, (L.intakeTop + L.intakeBot) * 0.5f), L.intakeW, (L.intakeTop - L.intakeBot) * 0.5f,
                      MAT_PLASTIC, col(0.2f, 0.2f, 0.2f), 0.003f);
    if (L.frontPlate) panelPatchLod(m, b, false, vec2(0.f, L.plateFZ), 0.16f, 0.075f, MAT_METAL_PAINTED, col(0.9f, 0.9f, 0.86f), 0.006f);
    if (lod == 1) {
        // keep the dark lower aero parts so nothing pops at the LOD switch
        if (L.diffuser) panelPatchLod(m, b, true, vec2(0.f, s.zRearLow + 0.055f), s.halfW * 0.56f, 0.05f, MAT_CAR_GLASS, kCol1, 0.006f);
        splitterLip(m, b, L);
    }
    if (d.rearPlate) panelPatchLod(m, b, true, vec2(0.f, L.plateRZ), 0.16f, 0.075f, MAT_METAL_PAINTED, col(0.9f, 0.9f, 0.86f), 0.006f);
    spoilerWing(m, b, L);
    if (L.spoiler == SP_ROOF) roofSpoiler(m, b);
    if (L.chromeBumpers || L.blackBumpers) {
        bumperBar(m, b, false, L.bumperFZ > 0.f ? L.bumperFZ : s.zNoseBot + 0.12f, s.halfW * 0.93f, L.chromeBumpers);
        bumperBar(m, b, true, L.bumperRZ > 0.f ? L.bumperRZ : s.zTailBot + 0.1f, s.halfW * 0.93f, L.chromeBumpers);
    }
    if (L.roof == RX_POLICE) policeBar(m, b, (s.yRoofF + s.yRoofR) * 0.5f + 0.1f, Min(b.railXAt((s.yRoofF + s.yRoofR) * 0.5f) + 0.05f, 0.62f));
    if (L.roof == RX_TAXI) taxiSign(m, b, (s.yRoofF + s.yRoofR) * 0.5f);
    if (L.roof == RX_RAILS && lod == 1) roofRails(m, b);
    if (L.bullBar) bullBar(m, b);
    if (L.spareWheel && lod == 1) spareWheel(m, b, d.wd.R, d.wd.W);
    if (L.hoodScoop && lod == 1) hoodScoop(m, b);
    bedTubs(m, b);
    if (interior) buildInterior(m, b, L, d.I);  // level-aware: always a closed cabin shell
    if (d.extra) d.extra(b, m);
}

inline void carBodyParts(const CarDef& d, CarBody& b, PMesh& m, bool interior) {
    b.s = d.s;
    b.build(m);
    if (lodLevel() >= 1) {
        carBodyPartsLod(d, b, m, interior, lodLevel());
        return;
    }
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
    splitterLip(m, b, L);
    if (L.frontPlate) buildPlate(m, b, false, L.plateFZ, L);
    if (d.rearPlate) buildPlate(m, b, true, L.plateRZ, L);
    if (d.carSeams) {
        carTopSeams(m, b);
        if (s.style != BS_BOXY && s.style != BS_VAN && !L.chromeBumpers && !L.blackBumpers) {
            // front bumper cover shut line under the headlamps (hidden behind the grille where it crosses it)
            Frame ff = projFront();
            std::vector<vec2> l;
            float zb = L.headC.y - L.headH - 0.03f, xc = Min(L.headC.x + L.headW * 0.85f, s.halfW * 0.9f);
            for (int k = 0; k <= 12; k++) {
                float u = lerp(-xc, xc, k / 12.f);
                l.push_back(vec2(u, zb - 0.012f * Sq(u / xc)));
            }
            seam(m, b.proj, ff, l);
        }
    }
    if (!s.openTop) wipers(m, b);
    rearSmallParts(m, b, d);
    rearDiffuser(m, b, L);
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
    if (L.hoodVents) {
        // two louvred extractor vents on the hood (heat exits of mid/front-engined performance cars)
        Frame ft = projTop();
        ft.o = vec3(0, 0, 5.f);
        Decal dc;
        dc.pr = &b.proj;
        dc.fr = ft;
        dc.back = 0.f;
        float yv = (s.yCowl + b.yHF) * 0.5f + 0.06f;
        decalRange(dc, vec2(-0.7f, yv - 0.3f), vec2(0.7f, yv + 0.3f));
        for (int sg = -1; sg <= 1; sg += 2) {
            float xc = sg * s.halfW * 0.34f;
            LampStyle st;
            st.height = 0.004f; st.bezel = 0.008f; st.wallMat = MAT_CAR_GLASS; st.bezelMat = MAT_CAR_GLASS;
            st.floorMat = MAT_PLASTIC; st.floorCol = col(0.2f, 0.2f, 0.2f); st.floorOff = 0.001f;
            lampHousing(m, dc, resampleClosed(shapeRoundRect(vec2(xc, yv), 0.13f, 0.1f, 0.03f, 3), 28), st);
            m.newGroup(40.f);
            m.use(MAT_CAR_GLASS, kCol1);
            for (int k = 0; k < 5; k++) {
                std::vector<vec2> ln;
                float yy = yv - 0.07f + k * 0.035f;
                ln.push_back(vec2(xc - 0.11f, yy));
                ln.push_back(vec2(xc + 0.11f, yy));
                decalBar(m, dc, ln, 0.012f, 0.006f, 0.001f, 0.05f);
            }
        }
    }
    if (L.bedRails && s.recDepth > 0.f) {
        // chrome tube rails along the bed sides
        m.newGroup(40.f);
        m.use(MAT_CHROME, kCol1);
        for (int sg = -1; sg <= 1; sg += 2) {
            float y0 = s.recR + 0.12f, y1 = s.recF - 0.12f;
            float x = sg * (b.rowXw[b.rowAt((y0 + y1) * 0.5f)] - 0.02f);
            float z = b.rowZsh[b.rowAt((y0 + y1) * 0.5f)] + 0.012f;
            cyl(m, vec3(x, y0, z + 0.07f), vec3(x, y1, z + 0.07f), 0.016f, 8);
            for (int k = 0; k < 3; k++) {
                float y = lerp(y0, y1, k / 2.f);
                cyl(m, vec3(x, y, z - 0.01f), vec3(x, y, z + 0.07f), 0.014f, 8);
            }
        }
    }
    if (L.mudFlaps) {
        // rubber mud flaps hanging behind every wheel
        m.newGroup(40.f);
        m.use(MAT_RUBBER, kCol1);
        for (int a = 0; a < 2; a++)
            for (int sg = -1; sg <= 1; sg += 2) {
                float yw = a == 0 ? b.yWf : b.yWr, track = a == 0 ? s.trackF : s.trackR;
                vec3 c(sg * track, yw - b.Ra - 0.03f, s.wheelR * 0.62f);
                roundedBoxAt(m, c, vec3(s.wheelW * 0.55f, 0.006f, s.wheelR * 0.42f), 0.004f, 1);
            }
    }
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
    bedTubs(m, b);
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
