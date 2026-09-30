// Model catalogue: original makes/models with body specs, looks, interiors, wheels and physics.
namespace Vehicles {
namespace detail {

// ------------------------------------------------------------------------------------------------
// Layout helpers. y forward from the wheelbase centre, z up from the ground.
inline void dims(CarDef& d, float L, float W, float wb, float fohFrac, float R, float tireW) {
    CarSpec& s = d.s;
    s.wb = wb;
    s.foh = L * fohFrac;
    s.roh = L - wb - s.foh;
    s.halfW = W * 0.5f;
    s.wheelR = R;
    s.wheelW = tireW;
    s.trackF = s.halfW - tireW * 0.5f - 0.015f;
    s.trackR = s.trackF;
    d.wd.R = R;
    d.wd.W = tireW;
    d.wd.rimR = Clamp(R - 0.11f, R * 0.55f, R * 0.72f);
}
// Stations from the front tip: hood, windshield, roof, rear-glass lengths.
inline void stations(CarSpec& s, float hood, float ws, float roof, float rw) {
    float yF = s.wb * 0.5f + s.foh;
    s.yCowl = yF - hood;
    s.yRoofF = s.yCowl - ws;
    s.yRoofR = s.yRoofF - roof;
    s.yDeck = s.yRoofR - rw;
}
inline float yFront(const CarSpec& s) { return s.wb * 0.5f + s.foh; }
inline float yRear(const CarSpec& s) { return -(s.wb * 0.5f + s.roh); }

// Lamp / grille placement from the body heights
inline void autoLook(CarDef& d) {
    CarSpec& s = d.s;
    CarLook& L = d.L;
    float hw = s.halfW;
    float face = s.zHoodF - s.zNoseTop;
    L.headH = Clamp(face * 0.26f, 0.04f, 0.085f);
    L.headC = vec2(hw * 0.66f, s.zHoodF - Max(face * 0.36f, L.headH + 0.02f));
    L.headW = hw * 0.22f;
    L.grilleTop = s.zHoodF - Max(face * 0.34f, 0.07f);
    L.grilleBot = Max(s.zNoseTop - 0.02f, L.grilleTop - 0.22f);
    L.grilleW = hw * 0.37f;
    L.plateFZ = s.zNoseBot + 0.15f;
    L.intakeTop = s.zNoseBot + 0.10f;
    L.intakeBot = s.zNoseBot + 0.015f;
    L.intakeW = hw * 0.46f;
    L.tailC = vec2(hw * 0.70f, s.zTailTop + Min(0.14f, (s.zTail - s.zTailTop) * 0.5f));
    L.plateRZ = s.zTailTop - 0.11f;
}

inline void archSedan(CarDef& d, float L = 4.85f, float W = 1.84f, float H = 1.45f, float wb = 2.80f, float R = 0.335f) {
    CarSpec& s = d.s;
    s.style = BS_SEDAN;
    s.doors = 4;
    dims(d, L, W, wb, 0.196f, R, 0.225f);
    s.zSill = 0.20f; s.zRoof = H;
    s.zBeltR = 0.705f * H; s.zBeltF = s.zBeltR - 0.05f; s.zCowl = s.zBeltF + 0.03f;
    s.zHoodF = 0.58f * H; s.zNoseTop = 0.415f * H; s.zNoseBot = 0.22f * H; s.zChin = 0.15f * H; s.hoodEdgeBack = 0.18f;
    s.zDeck = s.zBeltR + 0.035f; s.zTail = s.zDeck - 0.02f; s.zTailTop = 0.525f * H; s.zTailBot = 0.29f * H; s.zRearLow = 0.23f * H;
    stations(s, 0.28f * L, 0.195f * L, 0.20f * L, 0.165f * L);
    s.tailEdgeFwd = 0.03f * L;
    float roof = s.yRoofF - s.yRoofR;
    s.bPillar = s.yRoofF - roof * 0.40f;
    s.dloRearBot = s.yRoofR - 0.26f; s.dloRearTop = s.yRoofR + 0.02f;
    s.zChar = s.zBeltR - 0.20f; s.charOut = 0.006f; s.flareOut = 0.008f; s.flareW = 0.08f;
    // crisper plan corners (modern squared-off bumpers) with the corner radius kept by frontD / rearD
    s.frontD = 0.46f; s.frontExp = 3.6f; s.rearD = 0.40f; s.rearExp = 3.9f;
    autoLook(d);
    InteriorLayout& I = d.I;
    I.zFloor = s.zSill + 0.10f; I.hipH = 0.26f;
    I.yHipF = s.yRoofF - 0.28f; I.yHipR = I.yHipF - 0.86f;
    I.dashY0 = s.yCowl - 0.05f; I.dashY1 = s.yCowl - 0.48f; I.seatX = s.halfW * 0.40f;
}

inline void archHatch(CarDef& d, float L = 4.05f, float W = 1.75f, float H = 1.48f, float wb = 2.55f, float R = 0.31f) {
    archSedan(d, L, W, H, wb, R);
    CarSpec& s = d.s;
    s.style = BS_HATCH;
    dims(d, L, W, wb, 0.20f, R, 0.195f);
    s.zSill = 0.19f;
    s.zBeltR = 0.70f * H; s.zBeltF = s.zBeltR - 0.07f; s.zCowl = s.zBeltF + 0.03f;
    s.zHoodF = 0.54f * H; s.zNoseTop = 0.39f * H; s.zNoseBot = 0.215f * H; s.zChin = 0.14f * H; s.hoodEdgeBack = 0.16f;
    s.zDeck = s.zBeltR + 0.02f; s.zTail = s.zDeck - 0.02f; s.zTailTop = 0.55f * H; s.zTailBot = 0.27f * H; s.zRearLow = 0.21f * H;
    stations(s, 0.235f * L, 0.235f * L, 0.34f * L, 0.13f * L);
    s.tailEdgeFwd = 0.02f * L;
    s.rearD = 0.34f; s.rearExp = 3.6f; s.frontD = 0.46f;
    float roof = s.yRoofF - s.yRoofR;
    s.bPillar = s.yRoofF - roof * 0.36f;
    s.dloRearBot = s.yRoofR + 0.06f; s.dloRearTop = s.yRoofR + 0.26f;
    s.zChar = s.zBeltR - 0.19f;
    autoLook(d);
    d.L.tail = TL_VERT; d.L.tailW = 0.075f; d.L.tailH = 0.12f; d.L.tailYaw = 0.75f;
    d.L.tailC = vec2(s.halfW * 0.80f, s.zDeck - 0.10f);
    d.L.spoiler = SP_ROOF;
    InteriorLayout& I = d.I;
    I.zFloor = s.zSill + 0.10f; I.hipH = 0.27f;
    I.yHipF = s.yRoofF - 0.30f; I.yHipR = I.yHipF - 0.82f;
    I.dashY0 = s.yCowl - 0.05f; I.dashY1 = s.yCowl - 0.46f; I.seatX = s.halfW * 0.40f;
}

inline void archSUV(CarDef& d, float L = 5.0f, float W = 1.98f, float H = 1.80f, float wb = 2.95f, float R = 0.39f) {
    archSedan(d, L, W, H, wb, R);
    CarSpec& s = d.s;
    s.style = BS_SUV;
    s.privacyGlass = true;
    dims(d, L, W, wb, 0.19f, R, 0.265f);
    s.archGap = 0.055f;
    s.zSill = 0.30f + (R - 0.35f) * 0.8f;
    s.zBeltR = 0.70f * H; s.zBeltF = s.zBeltR - 0.05f; s.zCowl = s.zBeltF + 0.04f;
    s.zHoodF = 0.60f * H; s.zNoseTop = 0.49f * H; s.zNoseBot = 0.27f * H; s.zChin = 0.20f * H; s.hoodEdgeBack = 0.15f; s.noseRound = 0.25f;
    s.zDeck = s.zBeltR + 0.02f; s.zTail = s.zDeck - 0.02f; s.zTailTop = 0.55f * H; s.zTailBot = 0.29f * H; s.zRearLow = 0.25f * H;
    stations(s, 0.25f * L, 0.15f * L, 0.53f * L, 0.045f * L);
    s.tailEdgeFwd = 0.02f * L;
    s.frontD = 0.42f; s.frontExp = 3.6f; s.rearD = 0.34f; s.rearExp = 4.2f;
    s.tuLow = 0.04f; s.tuTop = 0.03f; s.zWide = 0.45f * H; s.lean = 0.20f; s.shR = 0.05f; s.shRx = 0.07f; s.wsBow = 0.02f;
    float roof = s.yRoofF - s.yRoofR;
    s.bPillar = s.yRoofF - roof * 0.30f; s.cPillar = s.yRoofF - roof * 0.64f; s.cPillarW = 0.10f;
    s.dloRearBot = s.yDeck + 0.16f; s.dloRearTop = s.yRoofR + 0.08f;
    s.zChar = s.zBeltR - 0.22f; s.charOut = 0.006f; s.flareOut = 0.015f; s.flareW = 0.10f;
    s.plasticArches = true; s.plasticSills = true;
    autoLook(d);
    CarLook& Lk = d.L;
    Lk.headYaw = 0.5f;
    Lk.grilleBars = 5;
    Lk.tail = TL_VERT; Lk.tailW = 0.07f; Lk.tailH = 0.16f; Lk.tailYaw = 0.8f; Lk.tailC = vec2(s.halfW * 0.82f, s.zDeck - 0.12f);
    Lk.roof = RX_RAILS; Lk.exhaust = 2; Lk.exhaustX = s.halfW * 0.55f;
    InteriorLayout& I = d.I;
    I.zFloor = s.zSill + 0.12f; I.hipH = 0.30f;
    I.yHipF = s.yRoofF - 0.40f; I.yHipR = I.yHipF - 0.90f;
    I.dashY0 = s.yCowl - 0.05f; I.dashY1 = s.yCowl - 0.45f; I.seatX = s.halfW * 0.40f;
    d.wd.style = RIM_SPOKE; d.wd.spokes = 6; d.wd.lugs = 6;
}

inline void archPickup(CarDef& d, float L = 5.9f, float W = 2.03f, float H = 1.95f, float wb = 3.68f, float R = 0.40f, bool crew = true) {
    archSUV(d, L, W, H, wb, R);
    CarSpec& s = d.s;
    s.style = BS_PICKUP;
    s.privacyGlass = false;
    dims(d, L, W, wb, 0.165f, R, 0.275f);
    s.zSill = 0.36f + (R - 0.38f);
    s.zBeltR = 0.675f * H; s.zBeltF = s.zBeltR - 0.02f; s.zCowl = s.zBeltF + 0.04f;
    s.zHoodF = 0.64f * H; s.zNoseTop = 0.50f * H; s.zNoseBot = 0.265f * H; s.zChin = 0.205f * H; s.hoodEdgeBack = 0.12f; s.noseRound = 0.15f;
    s.frontExp = 4.2f; s.frontD = 0.36f; s.rearExp = 6.0f; s.rearD = 0.14f;
    stations(s, 0.27f * L, 0.10f * L, crew ? 0.22f * L : 0.10f * L, 0.018f * L);
    s.zDeck = s.zBeltR; s.zTail = s.zBeltR - 0.02f; s.zTailTop = s.zBeltR - 0.03f; s.zTailBot = 0.31f * H; s.zRearLow = 0.28f * H;
    s.tailEdgeFwd = 0.03f;
    s.recF = s.yDeck - 0.05f; s.recR = yRear(s) + 0.06f; s.recDepth = s.zBeltR - (s.wheelR + 0.20f);
    float roof = s.yRoofF - s.yRoofR;
    s.bPillar = crew ? s.yRoofF - roof * 0.45f : -100.f;
    s.cPillar = -100.f;
    s.dloRearBot = s.yRoofR + 0.06f; s.dloRearTop = s.yRoofR + 0.06f;
    s.doors = crew ? 4 : 2;
    s.zChar = s.zBeltR - 0.22f; s.flareOut = 0.02f;
    s.ghInset = 0.06f;
    s.plasticArches = false; s.plasticSills = false;
    autoLook(d);
    CarLook& Lk = d.L;
    Lk.headYaw = 0.4f; Lk.headW = s.halfW * 0.2f;
    Lk.grille = GR_TRUCK; Lk.grilleBars = 3; Lk.grilleTaper = 0.02f; Lk.grilleW = s.halfW * 0.50f; Lk.grilleBot = s.zNoseTop - 0.14f;
    Lk.intakeW = s.halfW * 0.48f; Lk.fogs = true;
    Lk.tail = TL_VERT; Lk.tailC = vec2(s.halfW * 0.92f, s.zBeltR - 0.30f); Lk.tailW = 0.05f; Lk.tailH = 0.17f; Lk.tailYaw = 1.0f;
    Lk.plateRZ = s.zTailBot + 0.12f;
    Lk.roof = RX_NONE; Lk.exhaust = 1; Lk.exhaustX = s.halfW * 0.6f; Lk.towHitch = true;
    Lk.blackBumpers = false;
    InteriorLayout& I = d.I;
    I.zFloor = s.zSill + 0.14f; I.hipH = 0.30f;
    I.yHipF = crew ? s.yRoofF - 0.42f : s.yRoofR + 0.34f; I.yHipR = I.yHipF - 0.82f; I.rearSeat = crew;
    I.dashY0 = s.yCowl - 0.05f; I.dashY1 = s.yCowl - 0.42f; I.seatX = s.halfW * 0.41f;
    d.wd.offroad = true;
}

inline void archSports(CarDef& d, float L = 4.5f, float W = 1.92f, float H = 1.25f, float wb = 2.55f, float R = 0.34f) {
    archSedan(d, L, W, H, wb, R);
    CarSpec& s = d.s;
    s.style = BS_COUPE;
    s.doors = 2;
    dims(d, L, W, wb, 0.22f, R, 0.265f);
    s.trackR = s.trackF + 0.01f;
    s.zSill = 0.14f;
    s.zBeltR = 0.74f * H; s.zBeltF = s.zBeltR - 0.08f; s.zCowl = s.zBeltF + 0.02f;
    s.zHoodF = 0.51f * H; s.zNoseTop = 0.35f * H; s.zNoseBot = 0.18f * H; s.zChin = 0.10f * H; s.hoodEdgeBack = 0.28f; s.noseRound = 0.6f;
    s.zDeck = s.zBeltR + 0.01f; s.zTail = s.zDeck - 0.01f; s.zTailTop = 0.56f * H; s.zTailBot = 0.27f * H; s.zRearLow = 0.19f * H;
    stations(s, 0.35f * L, 0.19f * L, 0.12f * L, 0.19f * L);
    s.tailEdgeFwd = 0.025f * L;
    s.frontD = 0.62f; s.frontExp = 2.4f; s.rearD = 0.42f; s.rearExp = 3.0f;
    s.bPillar = -100.f;
    s.dloRearBot = s.yRoofR - 0.35f; s.dloRearTop = s.yRoofR;
    s.lean = 0.42f; s.shR = 0.035f; s.shRx = 0.05f; s.tuLow = 0.06f; s.tuTop = 0.06f; s.zWide = 0.36f * H;
    s.haunch = 0.05f; s.haunchF = 0.03f; s.flareOut = 0.012f; s.zChar = 0.f; s.charOut = 0.f;
    s.cowlLen = 0.06f;
    s.wsBow = 0.03f; s.rwBow = 0.03f;
    autoLook(d);
    CarLook& Lk = d.L;
    Lk.head = HL_SLIM; Lk.headYaw = 0.6f; Lk.headPitch = 0.25f; Lk.headH = 0.045f; Lk.headW = s.halfW * 0.22f;
    Lk.grille = GR_MESH; Lk.grilleChrome = false; Lk.grilleTop = s.zNoseTop; Lk.grilleBot = s.zNoseBot + 0.06f; Lk.grilleW = s.halfW * 0.44f;
    Lk.grilleTaper = 0.08f;
    Lk.intakeW = 0.0f; Lk.frontPlate = false;
    Lk.tail = TL_BAR; Lk.tailW = 0.18f; Lk.tailH = 0.045f; Lk.tailC = vec2(s.halfW * 0.72f, s.zTailTop + 0.10f);
    Lk.plateRZ = s.zTailBot + 0.20f;
    Lk.exhaust = 4; Lk.exhaustX = s.halfW * 0.42f; Lk.exhaustR = 0.045f; Lk.spoiler = SP_DUCK; Lk.diffuser = true; Lk.rockerSkirt = true;
    Lk.splitter = true;
    InteriorLayout& I = d.I;
    I.zFloor = 0.22f; I.hipH = 0.18f; I.yHipF = s.yRoofF - 0.22f; I.rearSeat = false;
    I.dashY0 = s.yCowl - 0.05f; I.dashY1 = s.yCowl - 0.48f; I.seatX = s.halfW * 0.40f;
    d.wd.style = RIM_SPOKE; d.wd.spokes = 5; d.wd.split = true; d.wd.spokeHub = 0.016f; d.wd.spokeRim = 0.012f;
    d.wd.faceTint = vec3(0.25f, 0.25f, 0.27f);
}

inline void archSuper(CarDef& d, float L = 4.7f, float W = 2.03f, float H = 1.14f, float wb = 2.70f, float R = 0.35f) {
    archSports(d, L, W, H, wb, R);
    CarSpec& s = d.s;
    dims(d, L, W, wb, 0.235f, R, 0.30f);
    s.trackR = s.trackF + 0.02f;
    s.zSill = 0.12f;
    s.zBeltR = 0.80f * H; s.zBeltF = 0.62f * H; s.zCowl = s.zBeltF + 0.02f;
    s.zHoodF = 0.50f * H; s.zNoseTop = 0.30f * H; s.zNoseBot = 0.16f * H; s.zChin = 0.09f * H; s.hoodEdgeBack = 0.35f; s.noseRound = 0.7f;
    s.zDeck = 0.83f * H; s.zTail = 0.80f * H; s.zTailTop = 0.64f * H; s.zTailBot = 0.30f * H; s.zRearLow = 0.20f * H;
    stations(s, 0.25f * L, 0.21f * L, 0.10f * L, 0.30f * L);
    s.tailEdgeFwd = 0.03f * L;
    s.frontD = 0.70f; s.frontExp = 2.2f; s.rearD = 0.40f; s.rearExp = 3.4f;
    s.dloRearBot = s.yRoofR - 0.10f; s.dloRearTop = s.yRoofR + 0.05f;
    s.haunch = 0.08f; s.haunchF = 0.05f; s.lean = 0.55f; s.waist = 0.04f;
    s.rearGlass = true;
    autoLook(d);
    CarLook& Lk = d.L;
    Lk.head = HL_SLIM; Lk.headYaw = 0.75f; Lk.headPitch = 0.45f; Lk.headH = 0.035f; Lk.headW = s.halfW * 0.2f;
    Lk.headC = vec2(s.halfW * 0.70f, s.zHoodF - 0.13f);
    Lk.grille = GR_MESH; Lk.grilleChrome = false; Lk.grilleTop = s.zNoseTop + 0.02f; Lk.grilleBot = s.zNoseBot + 0.02f; Lk.grilleW = s.halfW * 0.40f;
    Lk.sideIntake = true; Lk.spoiler = SP_WING; Lk.exhaust = 3; Lk.exhaustR = 0.05f;
    Lk.tail = TL_SLIM; Lk.tailC = vec2(s.halfW * 0.74f, s.zTailTop + 0.06f); Lk.tailW = 0.18f; Lk.tailH = 0.03f;
    Lk.antennaFin = false;
    d.I.zFloor = 0.18f; d.I.hipH = 0.16f; d.I.yHipF = s.yRoofF - 0.18f;
    d.I.dashY0 = s.yCowl - 0.05f; d.I.dashY1 = s.yCowl - 0.55f;
}

inline void archMuscle(CarDef& d, float L = 5.0f, float W = 1.92f, float H = 1.38f, float wb = 2.95f, float R = 0.35f) {
    archSedan(d, L, W, H, wb, R);
    CarSpec& s = d.s;
    s.style = BS_COUPE;
    s.doors = 2;
    dims(d, L, W, wb, 0.20f, R, 0.255f);
    s.zSill = 0.18f;
    s.zBeltR = 0.72f * H; s.zBeltF = s.zBeltR - 0.04f; s.zCowl = s.zBeltF + 0.02f;
    s.zHoodF = 0.63f * H; s.zNoseTop = 0.48f * H; s.zNoseBot = 0.26f * H; s.zChin = 0.16f * H; s.hoodEdgeBack = 0.10f; s.noseRound = 0.2f;
    s.zDeck = s.zBeltR + 0.02f; s.zTail = s.zDeck - 0.005f; s.zTailTop = 0.62f * H; s.zTailBot = 0.30f * H; s.zRearLow = 0.22f * H;
    stations(s, 0.34f * L, 0.15f * L, 0.16f * L, 0.14f * L);
    s.tailEdgeFwd = 0.04f;
    s.frontD = 0.40f; s.frontExp = 4.0f; s.rearD = 0.30f; s.rearExp = 4.5f;
    s.bPillar = -100.f;
    s.dloRearBot = s.yRoofR - 0.12f; s.dloRearTop = s.yRoofR + 0.06f;
    s.lean = 0.36f; s.shR = 0.03f; s.shRx = 0.04f; s.haunch = 0.035f; s.haunchF = 0.01f; s.flareOut = 0.01f;
    s.zChar = s.zBeltR - 0.13f; s.charOut = 0.008f;
    autoLook(d);
    CarLook& Lk = d.L;
    Lk.head = HL_PROJ; Lk.headDomes = 2; Lk.headYaw = 0.25f; Lk.headH = 0.05f; Lk.headW = s.halfW * 0.2f;
    Lk.headC = vec2(s.halfW * 0.68f, (s.zHoodF + s.zNoseTop) * 0.5f + 0.02f);
    Lk.grille = GR_SPLIT; Lk.grilleChrome = false; Lk.grilleTop = s.zHoodF - 0.05f; Lk.grilleBot = s.zNoseTop + 0.01f;
    Lk.grilleW = s.halfW * 0.44f; Lk.grilleTaper = 0.0f; Lk.grilleBars = 2;
    Lk.tail = TL_BAR; Lk.tailC = vec2(s.halfW * 0.72f, s.zTailTop + 0.09f); Lk.tailW = 0.2f; Lk.tailH = 0.05f;
    Lk.exhaust = 2; Lk.exhaustX = s.halfW * 0.5f; Lk.exhaustR = 0.045f; Lk.hoodScoop = true; Lk.spoiler = SP_LIP;
    d.I.rearSeat = true;
    d.I.yHipR = d.I.yHipF - 0.80f;
    d.wd.style = RIM_SPOKE; d.wd.spokes = 5; d.wd.split = false; d.wd.spokeHub = 0.03f; d.wd.spokeRim = 0.022f;
    d.wd.faceTint = vec3(0.3f, 0.3f, 0.32f);
}

inline void archMinivan(CarDef& d, float L = 5.1f, float W = 1.99f, float H = 1.76f, float wb = 3.0f, float R = 0.36f) {
    archSUV(d, L, W, H, wb, R);
    CarSpec& s = d.s;
    s.style = BS_WAGON;
    dims(d, L, W, wb, 0.19f, R, 0.235f);
    s.archGap = 0.05f;
    s.zSill = 0.26f;
    s.zBeltR = 0.62f * H; s.zBeltF = s.zBeltR - 0.03f; s.zCowl = s.zBeltF + 0.03f;
    s.zHoodF = 0.56f * H; s.zNoseTop = 0.42f * H; s.zNoseBot = 0.20f * H; s.zChin = 0.14f * H; s.hoodEdgeBack = 0.15f; s.noseRound = 0.45f;
    s.zDeck = s.zBeltR + 0.02f; s.zTail = s.zDeck - 0.02f; s.zTailTop = 0.46f * H; s.zTailBot = 0.25f * H; s.zRearLow = 0.20f * H;
    stations(s, 0.17f * L, 0.22f * L, 0.53f * L, 0.045f * L);
    s.tailEdgeFwd = 0.02f * L;
    s.frontD = 0.46f; s.frontExp = 2.8f; s.rearD = 0.32f; s.rearExp = 3.8f;
    float roof = s.yRoofF - s.yRoofR;
    s.bPillar = s.yRoofF - roof * 0.18f; s.cPillar = s.yRoofF - roof * 0.62f;
    s.dloRearBot = s.yDeck + 0.18f; s.dloRearTop = s.yRoofR + 0.08f;
    s.plasticArches = false; s.plasticSills = false; s.lean = 0.22f;
    autoLook(d);
    d.L.tail = TL_VERT; d.L.tailC = vec2(s.halfW * 0.84f, s.zDeck - 0.12f); d.L.tailW = 0.07f; d.L.tailH = 0.2f;
    d.L.roof = RX_RAILS; d.L.exhaust = 1;
    InteriorLayout& I = d.I;
    I.zFloor = s.zSill + 0.12f; I.hipH = 0.32f;
    I.yHipF = s.yRoofF - 0.45f; I.yHipR = I.yHipF - 0.95f;
    I.dashY0 = s.yCowl - 0.05f; I.dashY1 = s.yCowl - 0.50f; I.seatX = s.halfW * 0.42f;
}

// ------------------------------------------------------------------------------------------------
// Models: compacts
inline void mdlMinnow(VehicleModel& o) {
    o.name = "Minnow"; o.maker = "Sakaki"; o.cls = VC_COMPACT;
    CarDef d;
    archHatch(d, 4.05f, 1.75f, 1.48f, 2.55f, 0.31f);
    d.L.grille = GR_HBAR; d.L.grilleChrome = false; d.L.grilleBars = 3;
    d.wd.style = RIM_STEEL; d.wd.hubcap = true; d.wd.lugs = 4;
    physics(o, 1180.f, 85.f, 160.f, 6400.f, 50.f, 5, 1.f, 0.95f, 0.17f, 0.9f, 0.31f, 0.f, vec3(0, 0.22f, 0.50f), Audio::ENGINE_I4);
    buildCar(d, o);
    o.paletteColors = palette("common");
    o.spawnWeight = 9.f; o.price = 17500;
}
inline void mdlKiko(VehicleModel& o) {
    o.name = "Kiko"; o.maker = "Hoshida"; o.cls = VC_COMPACT;
    CarDef d;
    archHatch(d, 3.95f, 1.74f, 1.44f, 2.50f, 0.315f);
    CarSpec& s = d.s;
    s.doors = 2; s.bPillar = -100.f; s.dloRearBot = s.yRoofR + 0.04f; s.dloRearTop = s.yRoofR + 0.40f;
    s.haunch = 0.02f; s.flareOut = 0.012f;
    d.L.grille = GR_MESH; d.L.grilleChrome = false; d.L.head = HL_SLIM; d.L.headH = 0.042f;
    d.L.exhaust = 3; d.L.exhaustR = 0.04f; d.L.rockerSkirt = true;
    d.wd.style = RIM_SPOKE; d.wd.spokes = 5; d.wd.faceTint = vec3(0.2f, 0.2f, 0.22f);
    physics(o, 1250.f, 180.f, 330.f, 6800.f, 64.f, 6, 1.f, 1.08f, 0.15f, 1.25f, 0.32f, 0.05f, vec3(0, 0.2f, 0.47f), Audio::ENGINE_I4);
    buildCar(d, o);
    o.paletteColors = palette("sport");
    o.spawnWeight = 5.f; o.price = 29500;
}
inline void mdlGlint(VehicleModel& o) {
    o.name = "Glint"; o.maker = "Aether"; o.cls = VC_COMPACT;
    CarDef d;
    archHatch(d, 4.20f, 1.80f, 1.52f, 2.70f, 0.33f);
    CarSpec& s = d.s;
    s.noseRound = 0.8f; s.zHoodF -= 0.04f; s.frontExp = 2.6f; s.blackRoof = true; s.glossPillars = true;
    d.L.grille = GR_NONE; d.L.head = HL_SLIM; d.L.headH = 0.03f; d.L.headW = s.halfW * 0.28f; d.L.intakeW = s.halfW * 0.36f; d.L.fogs = false;
    d.L.tail = TL_BAR; d.L.tailC = vec2(s.halfW * 0.72f, s.zDeck - 0.05f); d.L.tailW = 0.15f; d.L.tailH = 0.03f; d.L.tailYaw = 0.6f;
    d.L.exhaust = 0; d.L.spoiler = SP_ROOF;
    d.wd.style = RIM_TURBINE; d.wd.spokes = 10; d.wd.faceTint = vec3(0.85f, 0.86f, 0.88f); d.wd.capTint = vec3(0.1f, 0.4f, 0.5f);
    physics(o, 1610.f, 150.f, 310.f, 12000.f, 44.f, 1, 1.f, 1.0f, 0.16f, 1.0f, 0.26f, 0.f, vec3(0, 0.0f, 0.45f), Audio::ENGINE_ELECTRIC);
    buildCar(d, o);
    o.paletteColors = palette("common");
    o.spawnWeight = 4.f; o.price = 34000;
}

// sedans
inline void mdlHarbor(VehicleModel& o) {
    o.name = "Harbor"; o.maker = "Brennan"; o.cls = VC_SEDAN;
    CarDef d;
    archSedan(d);
    d.wd.style = RIM_SPOKE; d.wd.spokes = 5; d.wd.split = true; d.wd.spokeHub = 0.015f; d.wd.spokeRim = 0.012f;
    physics(o, 1540.f, 150.f, 260.f, 6600.f, 58.f, 8, 1.f, 1.0f, 0.18f, 1.0f, 0.29f, 0.f, vec3(0, 0.25f, 0.52f), Audio::ENGINE_I4);
    buildCar(d, o);
    o.paletteColors = palette("common");
    o.spawnWeight = 10.f; o.price = 27000;
}
inline void mdlSeiran(VehicleModel& o) {
    o.name = "Seiran"; o.maker = "Hoshida"; o.cls = VC_SEDAN;
    CarDef d;
    archSedan(d, 4.90f, 1.85f, 1.43f, 2.83f, 0.34f);
    CarSpec& s = d.s;
    stations(s, 0.27f * 4.9f, 0.21f * 4.9f, 0.18f * 4.9f, 0.19f * 4.9f);
    s.bPillar = s.yRoofF - (s.yRoofF - s.yRoofR) * 0.45f;
    s.dloRearBot = s.yRoofR - 0.32f; s.dloRearTop = s.yRoofR + 0.04f;
    s.noseRound = 0.55f;
    d.L.head = HL_SLIM; d.L.headH = 0.04f; d.L.headW = s.halfW * 0.24f;
    d.L.grille = GR_MESH; d.L.grilleChrome = false; d.L.grilleTop -= 0.02f; d.L.grilleBot -= 0.08f; d.L.grilleW = s.halfW * 0.42f;
    d.L.tail = TL_BAR; d.L.tailH = 0.05f; d.L.spoiler = SP_LIP;
    d.wd.style = RIM_SPOKE; d.wd.spokes = 10; d.wd.spokeHub = 0.018f; d.wd.spokeRim = 0.013f; d.wd.faceTint = vec3(0.8f, 0.8f, 0.82f);
    physics(o, 1500.f, 152.f, 250.f, 6800.f, 58.f, 8, 1.f, 1.02f, 0.17f, 1.05f, 0.27f, 0.f, vec3(0, 0.22f, 0.51f), Audio::ENGINE_I4);
    buildCar(d, o);
    o.paletteColors = palette("common");
    o.spawnWeight = 10.f; o.price = 29000;
}
inline void mdlFalke(VehicleModel& o) {
    o.name = "Falke 7"; o.maker = "Nordwerk"; o.cls = VC_SEDAN;
    CarDef d;
    archSedan(d, 5.12f, 1.90f, 1.48f, 3.10f, 0.355f);
    CarSpec& s = d.s;
    stations(s, 0.31f * 5.12f, 0.18f * 5.12f, 0.20f * 5.12f, 0.15f * 5.12f);
    s.bPillar = s.yRoofF - (s.yRoofF - s.yRoofR) * 0.42f;
    s.dloRearBot = s.yRoofR - 0.22f; s.dloRearTop = s.yRoofR + 0.02f;
    d.L.grille = GR_VSLAT; d.L.grilleBars = 11; d.L.grilleW = s.halfW * 0.30f; d.L.grilleTop += 0.01f; d.L.grilleBot -= 0.04f;
    d.L.head = HL_PROJ; d.L.headDomes = 3; d.L.chromeBelt = true; d.L.handlesChrome = true; d.L.exhaust = 2; d.L.badgeShape = 1;
    d.L.leather = true;
    d.wd.style = RIM_MESH; d.wd.spokes = 14; d.wd.faceTint = vec3(0.85f, 0.85f, 0.88f);
    physics(o, 1950.f, 250.f, 450.f, 6500.f, 69.f, 8, 0.f, 1.05f, 0.19f, 1.0f, 0.26f, 0.f, vec3(0, 0.1f, 0.52f), Audio::ENGINE_V6);
    buildCar(d, o);
    o.paletteColors = palette("lux");
    o.spawnWeight = 3.f; o.price = 78000;
}
inline void govBase(CarDef& d) {
    archSedan(d, 5.28f, 1.96f, 1.47f, 2.91f, 0.345f);
    CarSpec& s = d.s;
    stations(s, 0.30f * 5.28f, 0.16f * 5.28f, 0.21f * 5.28f, 0.13f * 5.28f);
    s.frontExp = 4.0f; s.rearExp = 4.5f; s.frontD = 0.40f; s.rearD = 0.36f;
    s.tuTop = 0.025f; s.lean = 0.28f; s.noseRound = 0.2f; s.hoodEdgeBack = 0.12f;
    s.bPillar = s.yRoofF - (s.yRoofF - s.yRoofR) * 0.40f;
    s.dloRearBot = s.yRoofR - 0.10f; s.dloRearTop = s.yRoofR + 0.04f;
    d.L.head = HL_RECT; d.L.headH = 0.055f; d.L.headYaw = 0.3f;
    d.L.grille = GR_CLASSIC; d.L.grilleBars = 3;
    d.L.tail = TL_CLASSIC; d.L.tailW = 0.22f; d.L.tailH = 0.06f; d.L.tailYaw = 0.45f;
    d.L.intakeW = 0.f; d.L.fogs = false; d.L.chromeBumpers = false; d.L.blackBumpers = true;
    d.wd.style = RIM_STEEL; d.wd.hubcap = true;
}
inline void mdlGovernor(VehicleModel& o) {
    o.name = "Governor"; o.maker = "Brennan"; o.cls = VC_SEDAN;
    CarDef d;
    govBase(d);
    d.L.chromeBelt = true;
    physics(o, 1820.f, 185.f, 390.f, 5500.f, 55.f, 6, 0.f, 0.95f, 0.21f, 0.85f, 0.33f, 0.f, vec3(0, 0.15f, 0.55f), Audio::ENGINE_V8);
    buildCar(d, o);
    o.paletteColors = palette("common");
    o.spawnWeight = 6.f; o.price = 31000;
}

// coupes
inline void mdlCavell(VehicleModel& o) {
    o.name = "Cavell GT"; o.maker = "Ardent"; o.cls = VC_COUPE;
    CarDef d;
    archSports(d, 4.80f, 1.92f, 1.34f, 2.85f, 0.345f);
    CarSpec& s = d.s;
    stations(s, 0.34f * 4.8f, 0.18f * 4.8f, 0.12f * 4.8f, 0.20f * 4.8f);
    s.dloRearBot = s.yRoofR - 0.40f; s.dloRearTop = s.yRoofR;
    s.haunch = 0.035f;
    d.L.head = HL_PROJ; d.L.headDomes = 2; d.L.headH = 0.05f;
    d.L.grille = GR_MESH; d.L.grilleChrome = true; d.L.grilleTop = s.zNoseTop + 0.05f;
    d.L.tail = TL_WRAP; d.L.tailH = 0.05f; d.L.tailW = 0.2f; d.L.spoiler = SP_LIP; d.L.exhaust = 2; d.L.diffuser = false;
    d.L.leather = true;
    d.wd.style = RIM_SPOKE; d.wd.spokes = 7; d.wd.split = false; d.wd.spokeHub = 0.02f; d.wd.spokeRim = 0.014f;
    d.wd.faceTint = vec3(0.8f, 0.8f, 0.82f);
    physics(o, 1720.f, 330.f, 560.f, 7000.f, 80.f, 8, 0.f, 1.1f, 0.14f, 1.2f, 0.30f, 0.1f, vec3(0, 0.05f, 0.46f), Audio::ENGINE_V8);
    buildCar(d, o);
    o.paletteColors = palette("lux");
    o.spawnWeight = 2.f; o.price = 96000;
}
inline void mdlKaito(VehicleModel& o) {
    o.name = "Kaito"; o.maker = "Hoshida"; o.cls = VC_COUPE;
    CarDef d;
    archSedan(d, 4.55f, 1.80f, 1.36f, 2.67f, 0.325f);
    CarSpec& s = d.s;
    s.style = BS_COUPE;
    s.doors = 2;
    stations(s, 0.29f * 4.55f, 0.20f * 4.55f, 0.16f * 4.55f, 0.17f * 4.55f);
    s.bPillar = -100.f; s.dloRearBot = s.yRoofR - 0.2f; s.dloRearTop = s.yRoofR + 0.05f;
    s.zSill = 0.17f; s.haunch = 0.02f;
    d.L.head = HL_SLIM; d.L.headH = 0.042f;
    d.L.grille = GR_HBAR; d.L.grilleChrome = false; d.L.grilleBars = 2;
    d.L.tail = TL_BAR; d.L.spoiler = SP_WING; d.L.exhaust = 3; d.L.rockerSkirt = true; d.L.intakeW = s.halfW * 0.5f;
    d.I.rearSeat = true; d.I.yHipR = d.I.yHipF - 0.75f;
    d.wd.style = RIM_SPOKE; d.wd.spokes = 6; d.wd.faceTint = vec3(0.12f, 0.12f, 0.13f); d.wd.lipTint = vec3(0.9f);
    physics(o, 1330.f, 215.f, 360.f, 7400.f, 70.f, 6, 0.f, 1.12f, 0.14f, 1.3f, 0.30f, 0.12f, vec3(0, 0.12f, 0.45f), Audio::ENGINE_I4);
    buildCar(d, o);
    o.paletteColors = palette("sport");
    o.spawnWeight = 4.f; o.price = 38000;
}

// SUVs
inline void sawgrassBase(CarDef& d) {
    archSUV(d, 5.35f, 2.04f, 1.92f, 3.07f, 0.40f);
    CarSpec& s = d.s;
    s.plasticArches = false; s.plasticSills = false; s.frontExp = 4.4f; s.rearExp = 5.0f; s.frontD = 0.36f; s.rearD = 0.30f;
    d.L.grille = GR_TRUCK; d.L.grilleBars = 3; d.L.grilleW = s.halfW * 0.46f; d.L.grilleBot = s.zNoseTop - 0.08f;
    d.L.headW = s.halfW * 0.20f; d.L.headYaw = 0.35f;
    d.L.chromeBelt = true;
    d.wd.style = RIM_SPOKE; d.wd.spokes = 6; d.wd.faceTint = vec3(0.85f);
}
inline void mdlSawgrass(VehicleModel& o) {
    o.name = "Sawgrass"; o.maker = "Brennan"; o.cls = VC_SUV;
    CarDef d;
    sawgrassBase(d);
    physics(o, 2560.f, 265.f, 520.f, 5600.f, 52.f, 10, 0.35f, 0.95f, 0.24f, 1.1f, 0.38f, 0.f, vec3(0, 0.1f, 0.80f), Audio::ENGINE_V8);
    buildCar(d, o);
    o.paletteColors = palette("truck");
    o.spawnWeight = 7.f; o.price = 58000;
}
inline void mdlKumo(VehicleModel& o) {
    o.name = "Kumo"; o.maker = "Hoshida"; o.cls = VC_SUV;
    CarDef d;
    archSUV(d, 4.60f, 1.86f, 1.68f, 2.70f, 0.36f);
    CarSpec& s = d.s;
    stations(s, 0.23f * 4.6f, 0.18f * 4.6f, 0.46f * 4.6f, 0.10f * 4.6f);
    s.dloRearBot = s.yDeck + 0.30f; s.dloRearTop = s.yRoofR + 0.02f; s.cPillar = -100.f;
    s.noseRound = 0.5f; s.frontExp = 3.0f; s.rearExp = 3.4f; s.frontD = 0.5f; s.rearD = 0.4f;
    d.L.head = HL_SLIM; d.L.headH = 0.04f; d.L.grille = GR_MESH; d.L.grilleChrome = false; d.L.roof = RX_RAILS;
    d.L.spoiler = SP_ROOF;
    d.wd.style = RIM_SPOKE; d.wd.spokes = 5; d.wd.split = true; d.wd.spokeHub = 0.016f; d.wd.spokeRim = 0.012f;
    d.wd.faceTint = vec3(0.3f, 0.3f, 0.32f);
    physics(o, 1620.f, 150.f, 245.f, 6600.f, 53.f, 8, 0.6f, 0.98f, 0.20f, 1.0f, 0.33f, 0.f, vec3(0, 0.2f, 0.62f), Audio::ENGINE_I4);
    buildCar(d, o);
    o.paletteColors = palette("common");
    o.spawnWeight = 9.f; o.price = 31000;
}
inline void mdlOstgrat(VehicleModel& o) {
    o.name = "Ostgrat X5"; o.maker = "Nordwerk"; o.cls = VC_SUV;
    CarDef d;
    archSUV(d, 4.95f, 1.98f, 1.76f, 2.98f, 0.38f);
    CarSpec& s = d.s;
    s.plasticArches = false; s.plasticSills = false; s.blackRoof = false;
    s.dloRearBot = s.yDeck + 0.22f; s.dloRearTop = s.yRoofR + 0.06f;
    d.L.grille = GR_VSLAT; d.L.grilleBars = 9; d.L.grilleW = s.halfW * 0.32f; d.L.head = HL_PROJ; d.L.headDomes = 3;
    d.L.chromeBelt = true; d.L.handlesChrome = true; d.L.badgeShape = 1; d.L.leather = true;
    d.wd.style = RIM_MESH; d.wd.spokes = 12; d.wd.faceTint = vec3(0.4f, 0.4f, 0.42f);
    physics(o, 2250.f, 250.f, 450.f, 6500.f, 58.f, 8, 0.4f, 1.0f, 0.22f, 1.1f, 0.33f, 0.f, vec3(0, 0.1f, 0.70f), Audio::ENGINE_V6);
    buildCar(d, o);
    o.paletteColors = palette("lux");
    o.spawnWeight = 3.f; o.price = 72000;
}
inline void mdlTanuki(VehicleModel& o) {
    o.name = "Tanuki"; o.maker = "Sakaki"; o.cls = VC_SUV;
    CarDef d;
    archSUV(d, 4.40f, 1.88f, 1.86f, 2.55f, 0.405f);
    CarSpec& s = d.s;
    stations(s, 0.25f * 4.4f, 0.12f * 4.4f, 0.55f * 4.4f, 0.02f * 4.4f);
    s.frontExp = 6.f; s.rearExp = 7.f; s.frontD = 0.22f; s.rearD = 0.16f; s.noseRound = 0.05f; s.hoodEdgeBack = 0.06f;
    s.zNoseTop = 0.52f * 1.86f; s.zHoodF = 0.57f * 1.86f;
    s.lean = 0.08f; s.railR = 0.05f; s.tuLow = 0.02f; s.tuTop = 0.01f; s.shR = 0.03f; s.shRx = 0.03f;
    s.flareOut = 0.04f; s.flareW = 0.10f; s.archGap = 0.07f;
    s.cPillar = -100.f; s.bPillar = s.yRoofF - 0.95f; s.dloRearBot = s.yDeck + 0.10f; s.dloRearTop = s.yDeck + 0.10f;
    s.zChar = 0.f;
    d.L.head = HL_ROUND; d.L.headH = 0.085f; d.L.headC = vec2(s.halfW * 0.66f, s.zNoseTop + 0.07f); d.L.headYaw = 0.1f;
    d.L.grille = GR_VSLAT; d.L.grilleChrome = false; d.L.grilleBars = 7; d.L.grilleW = s.halfW * 0.36f;
    d.L.grilleTop = s.zHoodF - 0.03f; d.L.grilleBot = s.zNoseTop - 0.05f;
    d.L.blackBumpers = true; d.L.bumperFZ = s.zNoseBot + 0.08f; d.L.bumperRZ = s.zTailBot + 0.08f; d.L.intakeW = 0.f; d.L.fogs = false;
    d.L.spareWheel = true; d.L.roof = RX_RAILS; d.L.tail = TL_VERT; d.L.tailC = vec2(s.halfW * 0.86f, s.zTailTop + 0.18f);
    d.L.mirrorsBlack = true; d.L.plateFZ = s.zNoseBot + 0.25f;
    d.wd.offroad = true; d.wd.style = RIM_STEEL; d.wd.faceTint = vec3(0.25f); d.wd.hubcap = false; d.wd.lugs = 6;
    physics(o, 2100.f, 165.f, 390.f, 5500.f, 46.f, 6, 0.45f, 1.0f, 0.30f, 0.85f, 0.45f, 0.f, vec3(0, 0.05f, 0.82f), Audio::ENGINE_V6);
    buildCar(d, o);
    o.paletteColors = palette("classic");
    o.spawnWeight = 3.f; o.price = 39000;
}

// pickups
inline void mdlPalomino(VehicleModel& o) {
    o.name = "Palomino"; o.maker = "Brennan"; o.cls = VC_PICKUP;
    CarDef d;
    archPickup(d);
    physics(o, 2450.f, 290.f, 560.f, 5600.f, 50.f, 10, 0.f, 0.95f, 0.26f, 1.1f, 0.42f, 0.f, vec3(0, 0.35f, 0.82f), Audio::ENGINE_V8);
    buildCar(d, o);
    o.paletteColors = palette("truck");
    o.spawnWeight = 7.f; o.price = 46000;
}
inline void mdlTekko(VehicleModel& o) {
    o.name = "Tekko"; o.maker = "Hoshida"; o.cls = VC_PICKUP;
    CarDef d;
    archPickup(d, 5.35f, 1.88f, 1.80f, 3.22f, 0.38f, true);
    CarSpec& s = d.s;
    s.plasticArches = true; s.flareOut = 0.03f; s.noseRound = 0.3f; s.frontExp = 3.2f;
    d.L.grille = GR_HBAR; d.L.grilleChrome = false; d.L.grilleBars = 2; d.L.head = HL_SLIM; d.L.headH = 0.05f;
    d.wd.style = RIM_SPOKE; d.wd.spokes = 6; d.wd.faceTint = vec3(0.2f);
    physics(o, 2050.f, 210.f, 420.f, 5800.f, 48.f, 8, 0.f, 0.97f, 0.25f, 1.05f, 0.40f, 0.f, vec3(0, 0.3f, 0.78f), Audio::ENGINE_V6);
    buildCar(d, o);
    o.paletteColors = palette("truck");
    o.spawnWeight = 5.f; o.price = 36000;
}
inline void mdlCutter(VehicleModel& o) {
    o.name = "Cutter '74"; o.maker = "Brennan"; o.cls = VC_PICKUP;
    CarDef d;
    archPickup(d, 5.25f, 2.0f, 1.78f, 3.30f, 0.37f, false);
    CarSpec& s = d.s;
    s.noseRound = 0.1f; s.frontExp = 6.f; s.frontD = 0.25f; s.lean = 0.10f; s.shR = 0.05f; s.flareOut = 0.0f;
    s.zChar = 0.f; s.charOut = 0.f;
    d.L.head = HL_ROUND; d.L.headH = 0.085f; d.L.headYaw = 0.05f; d.L.headC = vec2(s.halfW * 0.72f, (s.zHoodF + s.zNoseTop) * 0.5f);
    d.L.grille = GR_CLASSIC; d.L.grilleBars = 3; d.L.grilleW = s.halfW * 0.52f; d.L.grilleTop = s.zHoodF - 0.04f;
    d.L.grilleBot = s.zNoseTop + 0.02f;
    d.L.chromeBumpers = true; d.L.bumperFZ = s.zNoseBot + 0.10f; d.L.bumperRZ = s.zTailBot + 0.06f; d.L.intakeW = 0.f; d.L.fogs = false;
    d.L.tail = TL_CLASSIC; d.L.tailW = 0.05f; d.L.tailH = 0.10f; d.L.tailYaw = 1.0f;
    d.L.antennaFin = false; d.L.sideMarkers = true; d.L.handlesChrome = true; d.L.mirrorsBlack = false;
    d.I.bench = true;
    d.wd.style = RIM_STEEL; d.wd.hubcap = true; d.wd.whitewall = false; d.wd.offroad = false;
    physics(o, 1950.f, 150.f, 400.f, 4400.f, 44.f, 4, 0.f, 0.85f, 0.25f, 0.8f, 0.48f, 0.f, vec3(0, 0.4f, 0.75f), Audio::ENGINE_V8);
    buildCar(d, o);
    o.paletteColors = palette("classic");
    o.spawnWeight = 2.f; o.price = 18000;
}

// sports
inline void mdlKestrel(VehicleModel& o) {
    o.name = "Kestrel"; o.maker = "Ardent"; o.cls = VC_SPORTS;
    CarDef d;
    archSports(d);
    physics(o, 1480.f, 390.f, 620.f, 7400.f, 85.f, 8, 0.f, 1.2f, 0.12f, 1.4f, 0.31f, 0.25f, vec3(0, -0.05f, 0.42f), Audio::ENGINE_V8);
    buildCar(d, o);
    o.paletteColors = palette("sport");
    o.spawnWeight = 1.5f; o.price = 118000;
}
inline void mdlSpree(VehicleModel& o) {
    o.name = "Spree GT"; o.maker = "Nordwerk"; o.cls = VC_SPORTS;
    CarDef d;
    archSports(d, 4.52f, 1.85f, 1.30f, 2.45f, 0.335f);
    CarSpec& s = d.s;
    stations(s, 0.26f * 4.52f, 0.21f * 4.52f, 0.13f * 4.52f, 0.26f * 4.52f);
    s.zHoodF = 0.55f * 1.30f; s.hoodEdgeBack = 0.35f; s.noseRound = 0.8f; s.frontD = 0.55f;
    s.haunch = 0.06f; s.haunchF = 0.05f; s.dloRearBot = s.yRoofR - 0.30f; s.dloRearTop = s.yRoofR + 0.02f;
    d.L.head = HL_ROUND; d.L.headH = 0.075f; d.L.headC = vec2(s.halfW * 0.70f, s.zHoodF - 0.02f); d.L.headYaw = 0.35f; d.L.headPitch = 0.5f;
    d.L.grille = GR_NONE; d.L.intakeW = s.halfW * 0.5f; d.L.intakeTop = s.zNoseTop - 0.02f; d.L.intakeBot = s.zNoseBot + 0.02f;
    d.L.fogs = false; d.L.tail = TL_BAR; d.L.spoiler = SP_DUCK; d.L.exhaust = 3;
    d.wd.style = RIM_SPOKE; d.wd.spokes = 5; d.wd.split = true; d.wd.faceTint = vec3(0.85f);
    physics(o, 1500.f, 330.f, 500.f, 7500.f, 82.f, 8, 0.f, 1.2f, 0.12f, 1.35f, 0.30f, 0.2f, vec3(0, -0.25f, 0.43f), Audio::ENGINE_V6);
    buildCar(d, o);
    o.paletteColors = palette("sport");
    o.spawnWeight = 1.5f; o.price = 112000;
}
inline void mdlKaze(VehicleModel& o) {
    o.name = "Kaze"; o.maker = "Sakaki"; o.cls = VC_SPORTS;
    CarDef d;
    archSports(d, 3.98f, 1.74f, 1.22f, 2.31f, 0.315f);
    CarSpec& s = d.s;
    s.style = BS_ROADSTER;
    s.openTop = true; s.cockpit = true;
    stations(s, 0.40f * 3.98f, 0.10f * 3.98f, 0.f, 0.f);
    s.zRoof = 1.12f;
    s.recF = s.yRoofF - 0.004f; s.recR = s.yRoofF - 1.05f; s.recDepth = 0.52f;
    s.yDeck = s.recR - 0.3f; s.zDeck = s.zBeltR + 0.02f;
    s.dloRearBot = s.yRoofF - 0.01f; s.dloRearTop = s.yRoofF - 0.01f;
    s.haunch = 0.04f;
    d.L.head = HL_SLIM; d.L.headH = 0.04f; d.L.tail = TL_ROUND; d.L.tailH = 0.05f; d.L.tailC = vec2(s.halfW * 0.72f, s.zTailTop + 0.08f);
    d.L.grille = GR_MESH; d.L.spoiler = SP_NONE; d.L.exhaust = 2; d.L.antennaFin = false; d.L.diffuser = false;
    d.L.leather = true;
    d.I.yHipF = s.yRoofF - 0.62f; d.I.dashY0 = s.yCowl - 0.02f; d.I.dashY1 = s.yRoofF + 0.02f;
    d.wd.style = RIM_SPOKE; d.wd.spokes = 6; d.wd.faceTint = vec3(0.8f);
    physics(o, 1080.f, 135.f, 205.f, 7500.f, 61.f, 6, 0.f, 1.12f, 0.13f, 1.25f, 0.35f, 0.05f, vec3(0, 0.0f, 0.42f), Audio::ENGINE_I4);
    buildCar(d, o);
    o.paletteColors = palette("sport");
    o.spawnWeight = 2.f; o.price = 33000;
}

// supers
inline void mdlOrsa(VehicleModel& o) {
    o.name = "Orsa V12"; o.maker = "Castiglia"; o.cls = VC_SUPER;
    CarDef d;
    archSuper(d);
    physics(o, 1520.f, 566.f, 720.f, 8500.f, 97.f, 7, 0.3f, 1.35f, 0.10f, 1.6f, 0.34f, 0.6f, vec3(0, -0.2f, 0.38f), Audio::ENGINE_V12);
    buildCar(d, o);
    o.paletteColors = palette("sport");
    o.spawnWeight = 0.3f; o.price = 420000;
}
inline void mdlArclight(VehicleModel& o) {
    o.name = "Arclight"; o.maker = "Aether"; o.cls = VC_SUPER;
    CarDef d;
    archSuper(d, 4.65f, 2.0f, 1.16f, 2.75f, 0.35f);
    CarSpec& s = d.s;
    s.noseRound = 0.9f; s.haunch = 0.06f;
    d.L.tail = TL_BAR; d.L.tailC = vec2(s.halfW * 0.72f, s.zTailTop + 0.05f); d.L.tailW = 0.16f; d.L.tailH = 0.025f;
    d.L.exhaust = 0; d.L.spoiler = SP_DUCK; d.L.sideIntake = true; d.L.grille = GR_NONE;
    d.L.intakeW = s.halfW * 0.5f; d.L.intakeTop = s.zNoseTop; d.L.intakeBot = s.zNoseBot + 0.02f; d.L.fogs = false;
    d.wd.style = RIM_TURBINE; d.wd.spokes = 12; d.wd.faceTint = vec3(0.2f, 0.2f, 0.22f);
    physics(o, 1950.f, 1100.f, 1400.f, 16000.f, 100.f, 1, 0.45f, 1.4f, 0.10f, 1.6f, 0.30f, 0.55f, vec3(0, -0.05f, 0.35f), Audio::ENGINE_ELECTRIC);
    buildCar(d, o);
    o.paletteColors = palette("sport");
    o.spawnWeight = 0.2f; o.price = 1850000;
}

// muscle
inline void mdlScorch(VehicleModel& o) {
    o.name = "Scorch 392"; o.maker = "Ridley"; o.cls = VC_MUSCLE;
    CarDef d;
    archMuscle(d);
    physics(o, 1890.f, 360.f, 640.f, 6400.f, 78.f, 8, 0.f, 1.05f, 0.15f, 1.1f, 0.36f, 0.1f, vec3(0, 0.25f, 0.50f), Audio::ENGINE_V8);
    buildCar(d, o);
    o.paletteColors = palette("muscle");
    o.spawnWeight = 2.f; o.price = 52000;
}
inline void mdlGatorback(VehicleModel& o) {
    o.name = "Gatorback '70"; o.maker = "Ridley"; o.cls = VC_MUSCLE;
    CarDef d;
    archMuscle(d, 5.15f, 1.95f, 1.32f, 2.92f, 0.36f);
    CarSpec& s = d.s;
    stations(s, 0.36f * 5.15f, 0.14f * 5.15f, 0.15f * 5.15f, 0.12f * 5.15f);
    s.dloRearBot = s.yRoofR - 0.05f; s.dloRearTop = s.yRoofR + 0.08f;
    s.frontExp = 6.f; s.rearExp = 6.f; s.frontD = 0.3f; s.rearD = 0.25f; s.haunch = 0.05f; s.haunchF = 0.0f;
    d.L.head = HL_QUAD; d.L.headH = 0.06f; d.L.headW = s.halfW * 0.18f; d.L.headYaw = 0.0f;
    d.L.headC = vec2(s.halfW * 0.66f, (s.zHoodF + s.zNoseTop) * 0.5f - 0.01f);
    d.L.grille = GR_CLASSIC; d.L.grilleBars = 4; d.L.grilleW = s.halfW * 0.95f; d.L.grilleTop = s.zHoodF - 0.035f;
    d.L.grilleBot = s.zNoseTop + 0.015f; d.L.grilleTaper = 0.0f; d.L.grilleChrome = false;
    d.L.chromeBumpers = true; d.L.bumperFZ = s.zNoseBot + 0.12f; d.L.bumperRZ = s.zTailBot + 0.12f; d.L.intakeW = 0.f; d.L.fogs = false;
    d.L.tail = TL_CLASSIC; d.L.tailW = 0.24f; d.L.tailH = 0.055f; d.L.tailYaw = 0.1f; d.L.tailC = vec2(s.halfW * 0.62f, s.zTailTop + 0.08f);
    d.L.hoodScoop = true; d.L.spoiler = SP_DUCK; d.L.exhaust = 2; d.L.antennaFin = false; d.L.handlesChrome = true;
    d.wd.style = RIM_CLASSIC; d.wd.faceTint = vec3(0.7f); d.wd.whitewall = false; d.wd.sidewallText = 2;
    physics(o, 1720.f, 320.f, 640.f, 5800.f, 70.f, 4, 0.f, 0.9f, 0.18f, 0.9f, 0.45f, 0.f, vec3(0, 0.3f, 0.50f), Audio::ENGINE_V8);
    buildCar(d, o);
    o.paletteColors = palette("muscle");
    o.spawnWeight = 1.2f; o.price = 64000;
}

// vans (car based)
inline void mdlTomo(VehicleModel& o) {
    o.name = "Tomo"; o.maker = "Hoshida"; o.cls = VC_VAN;
    CarDef d;
    archMinivan(d);
    physics(o, 2020.f, 210.f, 355.f, 6500.f, 52.f, 9, 1.f, 0.95f, 0.18f, 0.95f, 0.33f, 0.f, vec3(0, 0.2f, 0.62f), Audio::ENGINE_V6);
    buildCar(d, o);
    o.paletteColors = palette("common");
    o.spawnWeight = 5.f; o.price = 36000;
}

// police / taxi (Governor & Sawgrass based)
inline void mdlGovPatrol(VehicleModel& o) {
    o.name = "Governor Patrol"; o.maker = "Brennan"; o.cls = VC_POLICE;
    CarDef d;
    govBase(d);
    CarSpec& s = d.s;
    s.liveryDoors = true; s.liveryRoof = true;
    s.liveryY0 = Min(s.yCowl - 0.04f, s.wb * 0.5f - s.archGap - s.wheelR - 0.10f) - 0.02f;
    s.liveryY1 = Max(s.dloRearBot + 0.02f, -s.wb * 0.5f + s.wheelR + s.archGap + 0.12f) + 0.02f;
    d.L.roof = RX_POLICE; d.L.bullBar = true; d.L.spotLamp = true; d.L.mirrorsBlack = true;
    d.wd.style = RIM_STEEL; d.wd.hubcap = false; d.wd.faceTint = vec3(0.12f);
    d.extra = [](CarBody& b, PMesh& m) {
        float yc = (b.s.liveryY0 + b.s.liveryY1) * 0.5f;
        sideText(m, b, "POLICE", yc - 0.05f, b.s.zChar - 0.08f, 0.14f, MAT_METAL_PAINTED, col(0.02f, 0.03f, 0.08f));
        doorStar(m, b, b.s.liveryY1 - 0.35f, b.s.zChar + 0.02f, 0.07f, vec3(0.95f, 0.75f, 0.25f));
        sideText(m, b, "911", b.yWr + b.Ra + 0.02f, b.s.zChar + 0.07f, 0.075f, MAT_METAL_PAINTED, col(0.9f, 0.9f, 0.9f));
    };
    physics(o, 1880.f, 235.f, 440.f, 5800.f, 62.f, 6, 0.f, 1.05f, 0.19f, 1.1f, 0.34f, 0.f, vec3(0, 0.15f, 0.55f), Audio::ENGINE_V8);
    o.sirenMode = 0;
    buildCar(d, o);
    o.fixedLivery = true;
    o.liveryPrimary = srgb(14, 14, 16);
    o.liverySecondary = srgb(242, 242, 240);
    o.paletteColors.push_back(o.liveryPrimary);
    o.spawnWeight = 1.f; o.price = 0;
    for (int sg = -1; sg <= 1; sg += 2) {
        float y = (s.yRoofF + s.yRoofR) * 0.5f + 0.1f;
        vec3 p(sg * 0.35f, y, s.zRoof + 0.12f);
        o.lights.push_back(LightSpec{p, vec3(0, 1, 0), sg < 0 ? LT_SIREN_RED : LT_SIREN_BLUE});
        o.lights.push_back(LightSpec{p, vec3(0, -1, 0), sg < 0 ? LT_SIREN_RED : LT_SIREN_BLUE});
    }
}
inline void mdlSawPursuit(VehicleModel& o) {
    o.name = "Sawgrass Pursuit"; o.maker = "Brennan"; o.cls = VC_POLICE;
    CarDef d;
    sawgrassBase(d);
    CarSpec& s = d.s;
    d.L.roof = RX_POLICE; d.L.bullBar = true; d.L.spotLamp = true; d.L.chromeBelt = false; d.L.mirrorsBlack = true;
    d.L.grille = GR_HBAR; d.L.grilleChrome = false;
    d.wd.style = RIM_STEEL; d.wd.faceTint = vec3(0.12f); d.wd.hubcap = false;
    d.extra = [](CarBody& b, PMesh& m) {
        float y0 = b.yWr + b.Ra + 0.08f, y1 = b.yWf - b.Ra - 0.06f;
        // light blue stripe (secondary paint) with thin white pinstripes
        sideStripe(m, b, b.yR + 0.25f, b.yF - 0.35f, b.s.zChar - 0.05f, 0.09f, MAT_CARPAINT, kCol2);
        sideStripe(m, b, b.yR + 0.25f, b.yF - 0.35f, b.s.zChar + 0.005f, 0.012f, MAT_METAL_PAINTED, col(0.95f, 0.95f, 0.95f), 0.0018f);
        sideStripe(m, b, b.yR + 0.25f, b.yF - 0.35f, b.s.zChar - 0.105f, 0.012f, MAT_METAL_PAINTED, col(0.95f, 0.95f, 0.95f), 0.0018f);
        sideText(m, b, "POLICE", (y0 + y1) * 0.5f, b.s.zChar - 0.20f, 0.13f, MAT_METAL_PAINTED, col(0.95f, 0.95f, 0.95f));
        doorStar(m, b, y1 - 0.25f, b.s.zChar + 0.10f, 0.07f, vec3(0.95f, 0.75f, 0.25f));
    };
    physics(o, 2480.f, 300.f, 540.f, 5800.f, 60.f, 10, 0.35f, 1.0f, 0.23f, 1.2f, 0.38f, 0.f, vec3(0, 0.1f, 0.78f), Audio::ENGINE_V8);
    o.sirenMode = 1;
    buildCar(d, o);
    o.fixedLivery = true;
    o.liveryPrimary = srgb(16, 26, 58);
    o.liverySecondary = srgb(96, 170, 230);
    o.paletteColors.push_back(o.liveryPrimary);
    o.spawnWeight = 1.f; o.price = 0;
    for (int sg = -1; sg <= 1; sg += 2) {
        float y = (s.yRoofF + s.yRoofR) * 0.5f + 0.1f;
        vec3 p(sg * 0.35f, y, s.zRoof + 0.12f);
        o.lights.push_back(LightSpec{p, vec3(0, 1, 0), sg < 0 ? LT_SIREN_RED : LT_SIREN_BLUE});
        o.lights.push_back(LightSpec{p, vec3(0, -1, 0), sg < 0 ? LT_SIREN_RED : LT_SIREN_BLUE});
    }
}
inline void mdlGovCab(VehicleModel& o) {
    o.name = "Governor Cab"; o.maker = "Brennan"; o.cls = VC_TAXI;
    CarDef d;
    govBase(d);
    d.L.roof = RX_TAXI;
    d.extra = [](CarBody& b, PMesh& m) {
        // black checker band on the doors (secondary paint) + TAXI lettering
        float y0 = b.s.yCowl - 0.1f, y1 = b.s.dloRearBot - 0.02f;
        float z = b.s.zChar - 0.02f;
        for (int side = 0; side < 2; side++) {
            Frame fr = side == 0 ? projRight() : projLeft();
            Decal dc;
            dc.pr = &b.proj;
            dc.fr = fr;
            dc.back = 3.f;
            decalRange(dc, vec2(y1, z - 0.1f), vec2(y0, z + 0.1f));
            m.newGroup(40.f);
            m.use(MAT_CARPAINT, kCol2);
            float cs = 0.05f;
            int n = (int)((y0 - y1) / cs);
            for (int k = 0; k < n; k++)
                for (int r = 0; r < 2; r++) {
                    if (((k + r) & 1) == 0) continue;
                    std::vector<vec2> sq = shapeRoundRect(vec2(y1 + (k + 0.5f) * cs, z - cs * 0.5f + r * cs), cs * 0.5f, cs * 0.5f, 0.f, 1);
                    loopFill(m, dc, sq, 0.0012f, 1);
                }
        }
        sideText(m, b, "SOL CAB", (b.s.yCowl + b.s.dloRearBot) * 0.5f, b.s.zChar - 0.15f, 0.10f, MAT_METAL_PAINTED, col(0.02f, 0.02f, 0.02f));
    };
    physics(o, 1820.f, 185.f, 390.f, 5500.f, 55.f, 6, 0.f, 0.95f, 0.21f, 0.85f, 0.33f, 0.f, vec3(0, 0.15f, 0.55f), Audio::ENGINE_V8);
    buildCar(d, o);
    o.fixedLivery = true;
    o.liveryPrimary = srgb(250, 176, 20);
    o.liverySecondary = srgb(16, 16, 16);
    o.paletteColors.push_back(o.liveryPrimary);
    o.spawnWeight = 3.f; o.price = 0;
}

typedef void (*ModelFn)(VehicleModel&);
struct ModelEntry {
    ModelFn fn;
    VehicleClass cls;
};
static const ModelEntry kModels[] = {
    {mdlMinnow, VC_COMPACT},  {mdlKiko, VC_COMPACT},     {mdlGlint, VC_COMPACT},     {mdlHarbor, VC_SEDAN},      {mdlSeiran, VC_SEDAN},
    {mdlFalke, VC_SEDAN},     {mdlGovernor, VC_SEDAN},   {mdlCavell, VC_COUPE},      {mdlKaito, VC_COUPE},       {mdlSawgrass, VC_SUV},
    {mdlKumo, VC_SUV},        {mdlOstgrat, VC_SUV},      {mdlTanuki, VC_SUV},        {mdlPalomino, VC_PICKUP},   {mdlTekko, VC_PICKUP},
    {mdlCutter, VC_PICKUP},   {mdlKestrel, VC_SPORTS},   {mdlSpree, VC_SPORTS},      {mdlKaze, VC_SPORTS},       {mdlOrsa, VC_SUPER},
    {mdlArclight, VC_SUPER},  {mdlScorch, VC_MUSCLE},    {mdlGatorback, VC_MUSCLE},  {mdlTomo, VC_VAN},          {mdlGovPatrol, VC_POLICE},
    {mdlSawPursuit, VC_POLICE}, {mdlGovCab, VC_TAXI},     {mdlStevedore, VC_VAN},     {mdlParcel, VC_SERVICE},   {mdlPackhorse, VC_TRUCK},
    {mdlLongbow, VC_TRUCK},   {mdlCompactor, VC_SERVICE}, {mdlLifeline, VC_AMBULANCE}, {mdlGuardian, VC_FIRETRUCK}, {mdlBoulevard, VC_BUS},
    {mdlRaijin, VC_MOTORBIKE}, {mdlSundowner, VC_MOTORBIKE}, {mdlMochi, VC_SCOOTER},     {mdlSunchaser, VC_BOAT},    {mdlBonefish, VC_BOAT},
    {mdlWavekite, VC_JETSKI},  {mdlSkimmer, VC_AIRBOAT},    {mdlTern, VC_PLANE},        {mdlKite, VC_HELI},         {mdlKitePolice, VC_HELI},
};

}  // namespace detail
}  // namespace Vehicles
