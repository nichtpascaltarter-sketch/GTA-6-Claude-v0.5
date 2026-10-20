// Lofted car body: smooth parametric cross-sections swept along the length (+Y), with side-view profile
// curves (rocker, shoulder/beltline, hood/deck centre line, roofline), plan-view rounding at both ends,
// wheel arches cut exactly into the grid, an inset glass greenhouse and optional recesses (pickup bed,
// open cockpit). Right half is generated and mirrored.
namespace Vehicles {
namespace detail {

// Monotone cubic (Fritsch-Carlson) 1D curve; duplicate x keys produce a step.
struct Curve {
    std::vector<vec2> k;
    std::vector<float> t;
    Curve& add(float x, float y) { k.push_back(vec2(x, y)); return *this; }
    void clear() { k.clear(); t.clear(); }
    void build() {
        std::stable_sort(k.begin(), k.end(), [](vec2 a, vec2 b) { return a.x < b.x; });
        int n = (int)k.size();
        t.assign(n, 0.f);
        if (n < 2) return;
        std::vector<float> d(n - 1, 0.f);
        std::vector<u8> sg(n - 1, 0);
        for (int i = 0; i + 1 < n; i++) {
            float dx = k[i + 1].x - k[i].x;
            sg[i] = dx > 1e-6f;
            d[i] = sg[i] ? (k[i + 1].y - k[i].y) / dx : 0.f;
        }
        for (int i = 0; i < n; i++) {
            bool L = i > 0 && sg[i - 1], R = i + 1 < n && sg[i];
            if (L && R) t[i] = (d[i - 1] * d[i] <= 0.f) ? 0.f : (d[i - 1] + d[i]) * 0.5f;
            else if (L) t[i] = d[i - 1];
            else if (R) t[i] = d[i];
        }
        for (int i = 0; i + 1 < n; i++) {
            if (!sg[i]) continue;
            if (fabsf(d[i]) < 1e-9f) { t[i] = 0.f; t[i + 1] = 0.f; continue; }
            float a = t[i] / d[i], b = t[i + 1] / d[i];
            if (a < 0.f) { t[i] = 0.f; a = 0.f; }
            if (b < 0.f) { t[i + 1] = 0.f; b = 0.f; }
            float s = a * a + b * b;
            if (s > 9.f) {
                float tau = 3.f / sqrtf(s);
                t[i] = tau * a * d[i];
                t[i + 1] = tau * b * d[i];
            }
        }
    }
    float operator()(float x) const {
        int n = (int)k.size();
        if (n == 0) return 0.f;
        if (x <= k[0].x) return k[0].y;
        if (x >= k[n - 1].x) return k[n - 1].y;
        int lo = 0, hi = n - 1;
        while (hi - lo > 1) {
            int mid = (lo + hi) / 2;
            if (k[mid].x <= x) lo = mid;
            else hi = mid;
        }
        int i = lo;
        float h = k[i + 1].x - k[i].x;
        if (h < 1e-6f) return k[i + 1].y;
        float u = (x - k[i].x) / h, u2 = u * u, u3 = u2 * u;
        return (2 * u3 - 3 * u2 + 1) * k[i].y + (u3 - 2 * u2 + u) * h * t[i] + (-2 * u3 + 3 * u2) * k[i + 1].y +
               (u3 - u2) * h * t[i + 1];
    }
};

enum BodyStyle : u8 { BS_SEDAN = 0, BS_HATCH, BS_WAGON, BS_SUV, BS_COUPE, BS_PICKUP, BS_ROADSTER, BS_VAN, BS_BOXY };
enum CellClass : u8 { CC_PAINT = 0, CC_PAINT2, CC_PLASTIC, CC_GLOSS, CC_GLASS, CC_HOLE, CC_BED, CC_INTERIOR, CC_CHROME, CC_SKIP };
enum Band : u8 { BD_BOTTOM = 0, BD_CORNER, BD_SIDE, BD_SHOULDER, BD_LEDGE, BD_GHSIDE, BD_RAIL, BD_TOP };

struct CarSpec {
    BodyStyle style = BS_SEDAN;
    int doors = 4;
    // chassis
    float wb = 2.80f, foh = 0.95f, roh = 1.05f;
    float halfW = 0.915f;
    float wheelR = 0.335f, wheelW = 0.225f;
    float trackF = 0.79f, trackR = 0.79f;  // wheel centre x (half track)
    float archGap = 0.045f;
    // side-view profile
    float zSill = 0.20f;
    float zNoseTop = 0.62f, zNoseBot = 0.34f;  // vertical extent of the very front
    float zChin = 0.22f;                       // underside just behind the front tip
    float zHoodF = 0.84f, hoodEdgeBack = 0.20f;
    float noseRound = 0.35f;                    // 0 = sharp hood edge, 1 = very round nose
    float zCowl = 0.99f, yCowl = 1.00f;
    float zBeltF = 0.95f, zBeltR = 1.00f;
    float yRoofF = 0.05f, yRoofR = -0.95f, zRoof = 1.44f;
    float wsBow = 0.03f, rwBow = 0.02f;        // windshield / rear window convexity
    float yDeck = -1.70f, zDeck = 1.04f;       // rear window base (greenhouse rear end)
    float zTail = 1.02f, tailEdgeFwd = 0.16f;  // trunk rear edge
    float zTailTop = 0.74f, zTailBot = 0.42f;  // vertical extent of the very rear
    float zRearLow = 0.32f;
    float fenderDrop = 0.05f;                  // fender line below the hood centre near the front
    float crownHood = 0.035f, crownDeck = 0.03f, crownCabin = 0.012f;
    // plan view
    float frontD = 0.50f, frontExp = 3.0f, rearD = 0.42f, rearExp = 3.2f;
    float waist = 0.0f, hips = 0.0f;
    // section shape
    float tuLow = 0.05f, tuTop = 0.045f, zWide = 0.52f;
    float shR = 0.04f, shRx = 0.06f, cornerR = 0.07f;
    float ghInset = 0.04f, lean = 0.34f, roofCrown = 0.035f, railR = 0.07f, roofPinch = 0.0f;
    float zChar = 0.0f, charOut = 0.0f;
    // greenhouse details
    float dloFront = -100.f;                   // default yCowl - 0.07
    float bPillar = 0.f, bPillarW = 0.10f;
    float cPillar = -100.f, cPillarW = 0.10f;  // extra pillar (SUV quarter window) if > -99
    float dloRearBot = -100.f, dloRearTop = -100.f;  // slanted rear edge (default: at the rear window)
    bool glossPillars = true, blackRoof = false, sunroof = false, blackAPillar = false;
    float cowlLen = 0.08f;
    // arches
    float flareW = 0.06f, flareOut = 0.f, haunch = 0.f, haunchF = -1.f;
    bool plasticArches = false, plasticSills = false, plasticBumpers = false;
    // recess (pickup bed / cockpit)
    float recF = 0.f, recR = 0.f, recDepth = 0.f;
    bool cockpit = false;
    bool openTop = false;                      // no roof: greenhouse only over the windshield
    // livery zones (secondary paint)
    bool liveryDoors = false, liveryRoof = false;
    float liveryY0 = 0.f, liveryY1 = 0.f;
    // generic extra pillars (y centre, width) and glass switches
    std::vector<vec2> pillars;
    std::vector<vec2> roofKeys;   // optional explicit roofline (y, z) replacing the generated one
    bool rearGlass = true, sideGlass = true;
    bool frontArch = true, rearArch = true;
    float hoodNarrow = 0.f;  // plan half-width reduction ahead of the cowl (conventional truck noses)
};

struct CarBody {
    CarSpec s;
    float yF = 0, yR = 0, yWf = 0, yWr = 0, Ra = 0, yHF = 0, yTE = 0, yGhR = 0, yGhF = 0;
    Curve cBot, cSh, cCen, cRoof;
    std::vector<float> rows;
    // column layout
    enum { NB = 3, NC = 2, NSA = 2, NSF = 1, NSC = 3, NST = 3, NSH = 4, NL = 1, NG1 = 2, NG2 = 2, NR = 3, NT = 6 };
    int pCor0 = 0, pSide0 = 0, jArch = 0, jFlare = 0, jChar = 0, pSh0 = 0, pLed0 = 0, pGh0 = 0, jSplit = 0, pRail0 = 0,
        pTop0 = 0, NP = 0;
    std::vector<u8> cellBand;
    std::vector<vec3> G, GN;
    std::vector<float> rowL, rowXw, rowZsh;
    std::vector<u8> cls;
    int nr = 0;
    Projector proj, glassProj;

    CarBody() {}
    explicit CarBody(const CarSpec& sp) : s(sp) {}

    // -------------------------------------------------------------------------------------------
    void setup() {
        yF = s.wb * 0.5f + s.foh;
        yR = -(s.wb * 0.5f + s.roh);
        yWf = s.wb * 0.5f;
        yWr = -s.wb * 0.5f;
        Ra = s.wheelR + s.archGap;
        yHF = yF - s.hoodEdgeBack;
        yTE = yR + s.tailEdgeFwd;
        if (s.dloFront < -99.f) s.dloFront = s.yCowl - 0.07f;
        if (s.dloRearBot < -99.f) { s.dloRearBot = s.yRoofR - 0.02f; s.dloRearTop = s.yRoofR + 0.02f; }
        // bottom
        cBot.clear();
        cBot.add(yR, s.zTailBot).add(yR + 0.07f, s.zRearLow).add(yR + s.roh * 0.55f, s.zSill + 0.035f).add(yWr, s.zSill);
        cBot.add(yWf, s.zSill).add(yF - s.foh * 0.5f, s.zSill + 0.03f).add(yF - 0.09f, s.zChin).add(yF, s.zNoseBot);
        cBot.build();
        // side top (shoulder / beltline)
        cSh.clear();
        float ydr = Max(s.yDeck + 0.12f, Min(s.dloRearBot, s.yRoofR));
        cSh.add(yR, s.zTailTop);
        if (yTE > yR + 0.02f) cSh.add(yTE, s.zTail - s.crownDeck * 0.8f);
        if (s.yDeck > yTE + 0.05f) cSh.add(s.yDeck, s.zDeck - s.crownDeck);
        cSh.add(ydr, s.zBeltR);
        cSh.add(s.yCowl, s.zBeltF);
        cSh.add(yHF, s.zHoodF - s.fenderDrop);
        cSh.add(yF - 0.05f, lerp(s.zNoseTop, s.zHoodF - s.fenderDrop, 0.35f));
        cSh.add(yF, s.zNoseTop);
        cSh.build();
        // centre line of the lower body top (hood / cabin floor of the greenhouse / trunk)
        cCen.clear();
        cCen.add(yR, s.zTailTop);
        if (yTE > yR + 0.02f) {
            cCen.add(yR + s.tailEdgeFwd * 0.35f, lerp(s.zTailTop, s.zTail, 0.8f));
            cCen.add(yTE, s.zTail);
        }
        if (s.yDeck > yTE + 0.05f) cCen.add(s.yDeck, s.zDeck);
        cCen.add(s.yCowl, s.zCowl);
        cCen.add(yHF, s.zHoodF);
        float dz = s.zHoodF - s.zNoseTop, dy = yF - yHF;
        cCen.add(yHF + dy * 0.45f, s.zHoodF - dz * lerp(0.35f, 0.12f, s.noseRound));
        cCen.add(yHF + dy * 0.85f, s.zNoseTop + dz * lerp(0.25f, 0.55f, s.noseRound));
        cCen.add(yF, s.zNoseTop);
        cCen.build();
        // roofline (absolute), greenhouse between yGhR and yGhF
        yGhF = s.yCowl;
        yGhR = s.yDeck;
        cRoof.clear();
        if (!s.roofKeys.empty()) {
            for (const vec2& k : s.roofKeys) cRoof.add(k.x, k.y);
        } else if (s.openTop) {
            cRoof.add(s.yRoofF, s.zRoof);
            cRoof.add(lerp(s.yRoofF, s.yCowl, 0.5f), lerp(s.zRoof, s.zCowl, 0.5f) + s.wsBow);
            cRoof.add(s.yCowl, s.zCowl);
            yGhR = s.yRoofF;
        } else {
            cRoof.add(s.yDeck, s.zDeck);
            float ymr = lerp(s.yDeck, s.yRoofR, 0.5f);
            cRoof.add(ymr, lerp(s.zDeck, s.zRoof - 0.05f, 0.5f) + s.rwBow);
            cRoof.add(s.yRoofR, s.zRoof - 0.045f);
            cRoof.add(s.yRoofR + Min(0.22f, (s.yRoofF - s.yRoofR) * 0.25f), s.zRoof - 0.008f);
            cRoof.add((s.yRoofR + s.yRoofF) * 0.5f, s.zRoof);
            cRoof.add(s.yRoofF - Min(0.2f, (s.yRoofF - s.yRoofR) * 0.25f), s.zRoof - 0.008f);
            cRoof.add(s.yRoofF, s.zRoof - 0.035f);
            cRoof.add(lerp(s.yRoofF, s.yCowl, 0.5f), lerp(s.zRoof - 0.035f, s.zCowl, 0.5f) + s.wsBow);
            cRoof.add(s.yCowl, s.zCowl);
        }
        cRoof.build();
        // columns
        pCor0 = NB;
        pSide0 = pCor0 + NC;
        jArch = pSide0 + NSA;
        jFlare = jArch + NSF;
        jChar = jFlare + NSC;
        pSh0 = jChar + NST;
        pLed0 = pSh0 + NSH;
        pGh0 = pLed0 + NL;
        jSplit = pGh0 + NG1;
        pRail0 = jSplit + NG2;
        pTop0 = pRail0 + NR;
        NP = pTop0 + NT + 1;
        cellBand.resize(NP - 1);
        for (int c = 0; c < NP - 1; c++) {
            Band b = BD_TOP;
            if (c < pCor0) b = BD_BOTTOM;
            else if (c < pSide0) b = BD_CORNER;
            else if (c < pSh0) b = BD_SIDE;
            else if (c < pLed0) b = BD_SHOULDER;
            else if (c < pGh0) b = BD_LEDGE;
            else if (c < pRail0) b = BD_GHSIDE;
            else if (c < pTop0) b = BD_RAIL;
            cellBand[c] = (u8)b;
        }
    }

    // plan half width at the widest point
    float planW(float y) const {
        float w = s.halfW;
        float t0 = yF - s.frontD;
        if (y > t0) {
            float t = Saturate((y - t0) / s.frontD);
            float u = 1.f - powf(t, s.frontExp);
            w *= u <= 2e-5f ? 0.f : powf(u, 1.f / s.frontExp);
        }
        float t1 = yR + s.rearD;
        if (y < t1) {
            float t = Saturate((t1 - y) / s.rearD);
            float u = 1.f - powf(t, s.rearExp);
            w *= u <= 2e-5f ? 0.f : powf(u, 1.f / s.rearExp);
        }
        if (y >= yF - 1e-5f || y <= yR + 1e-5f) return 0.f;
        if (s.waist != 0.f) w -= s.waist * expf(-Sq((y - (s.yCowl + s.yRoofR) * 0.5f) / 0.8f));
        if (s.hoodNarrow != 0.f) w -= s.hoodNarrow * smooth01((y - (s.yCowl - 0.05f)) / 0.35f) * (w / Max(s.halfW, 1e-3f));
        if (s.hips != 0.f) w += s.hips * expf(-Sq((y - yWr) / 0.55f)) * Saturate((y - yR) / 0.3f);
        return Max(w, 0.f);
    }
    // arch helpers: returns arch index (0 front, 1 rear) whose zone contains y (with margin), else -1
    bool archOn(int a) const { return a == 0 ? s.frontArch : s.rearArch; }
    int archAt(float y, float margin) const {
        if (s.frontArch && fabsf(y - yWf) <= Ra + margin) return 0;
        if (s.rearArch && fabsf(y - yWr) <= Ra + margin) return 1;
        return -1;
    }
    float archTop(float y) const {  // z of the arch edge at y (only valid inside |dy| <= Ra)
        int a = archAt(y, 0.f);
        if (a < 0) return s.wheelR;
        float dy = y - (a == 0 ? yWf : yWr);
        return s.wheelR + sqrtf(Max(Ra * Ra - dy * dy, 0.f));
    }
    float flareTop(float y) const {
        int a = archAt(y, s.flareW);
        if (a < 0) return s.wheelR;
        float dy = y - (a == 0 ? yWf : yWr);
        float R2 = Ra + s.flareW;
        return s.wheelR + sqrtf(Max(R2 * R2 - dy * dy, 0.f));
    }
    bool inOpening(float y, float z) const {
        for (int a = 0; a < 2; a++) {
            if (!archOn(a)) continue;
            float yw = a == 0 ? yWf : yWr;
            float dy = y - yw, dz = z - s.wheelR;
            if (dy * dy + dz * dz < Ra * Ra) return true;
            if (fabsf(dy) < Ra && dz < 0.f) return true;
        }
        return false;
    }
    // distance outside the opening boundary (0 at the edge)
    float archDist(float y, float z) const {
        float best = 1e9f;
        for (int a = 0; a < 2; a++) {
            if (!archOn(a)) continue;
            float yw = a == 0 ? yWf : yWr;
            float dy = y - yw, dz = z - s.wheelR;
            float d = dz >= 0.f ? sqrtf(dy * dy + dz * dz) - Ra : fabsf(dy) - Ra;
            best = Min(best, d);
        }
        return best;
    }
    // shoulder line including fender bumps and arch clearance
    float shoulderZ(float y) const {
        float z = cSh(y);
        if (s.haunch > 0.f) {
            float hf = s.haunchF >= 0.f ? s.haunchF : s.haunch;
            z += s.haunch * expf(-Sq((y - yWr) / 0.62f)) + hf * expf(-Sq((y - yWf) / 0.55f));
        }
        // keep the arch (plus flare band and shoulder radius) below the shoulder
        int a = archAt(y, s.flareW + 0.25f);
        if (a >= 0) {
            float yw = a == 0 ? yWf : yWr;
            float dy = fabsf(y - yw);
            float need = s.wheelR + sqrtf(Max(Sq(Ra + s.flareW + 0.02f) - Sq(Min(dy, Ra + s.flareW + 0.02f)), 0.f)) + s.shR + 0.03f;
            float blend = 1.f - smooth01((dy - (Ra + s.flareW)) / 0.25f);
            if (need > z) z = lerp(z, need, blend);
        }
        return z;
    }
    float centreZ(float y) const {
        float c = cCen(y);
        float sh = shoulderZ(y);
        float extra = sh - cSh(y);
        c += extra;
        return Max(c, sh);
    }
    // greenhouse lift above the deck centre (0 outside; < 0 in recesses)
    float lift(float y, float zc) const {
        if (s.recDepth > 0.f && y > s.recR && y < s.recF) return -s.recDepth;
        if (y >= yGhR && y <= yGhF) return cRoof(y) - zc;
        return 0.f;
    }

    // -------------------------------------------------------------------------------------------
    void buildRows() {
        std::vector<float> r;
        // plan rounding zones (superellipse angle spacing)
        int nf = 11;
        for (int i = 0; i <= nf; i++) {
            float th = kHalfPi * i / nf;
            r.push_back(yF - s.frontD + s.frontD * powf(sinf(th), 2.f / s.frontExp));
            r.push_back(yR + s.rearD - s.rearD * powf(sinf(th), 2.f / s.rearExp));
        }
        // nose / tail profile detail
        for (int i = 1; i < 5; i++) {
            r.push_back(lerp(yHF, yF, i / 5.f));
            if (yTE > yR + 0.05f) r.push_back(lerp(yR, yTE, i / 5.f));
        }
        r.push_back(yHF);
        r.push_back(yTE);
        // arches: uniform in angle, plus edges of the flare zone
        for (int a = 0; a < 2; a++) {
            if (!archOn(a)) continue;
            float yw = a == 0 ? yWf : yWr;
            int na = 12;
            for (int i = 0; i <= na; i++) r.push_back(yw + Ra * cosf(kPi * i / na));
            r.push_back(yw + Ra + s.flareW);
            r.push_back(yw - Ra - s.flareW);
            r.push_back(yw + Ra + s.flareW * 0.5f);
            r.push_back(yw - Ra - s.flareW * 0.5f);
        }
        // greenhouse stations
        float gh[] = {s.yCowl, s.yCowl + s.cowlLen, s.yRoofF, s.yRoofR, s.yDeck, s.dloFront, s.dloRearBot, s.dloRearTop,
                      s.bPillar - s.bPillarW * 0.5f, s.bPillar + s.bPillarW * 0.5f};
        for (float g : gh) r.push_back(g);
        if (s.cPillar > -99.f) { r.push_back(s.cPillar - s.cPillarW * 0.5f); r.push_back(s.cPillar + s.cPillarW * 0.5f); }
        for (const vec2& pl : s.pillars) { r.push_back(pl.x - pl.y * 0.5f); r.push_back(pl.x + pl.y * 0.5f); }
        for (int i = 1; i < 5; i++) {
            r.push_back(lerp(s.yRoofF, s.yCowl, i / 5.f));
            if (!s.openTop) r.push_back(lerp(s.yDeck, s.yRoofR, i / 5.f));
        }
        if (s.dloRearTop != s.dloRearBot)
            for (int i = 1; i < 4; i++) r.push_back(lerp(s.dloRearBot, s.dloRearTop, i / 4.f));
        if (s.liveryDoors) { r.push_back(s.liveryY0); r.push_back(s.liveryY1); }
        if (s.recDepth > 0.f) {
            r.push_back(s.recF + 0.004f); r.push_back(s.recF - 0.004f);
            r.push_back(s.recR + 0.004f); r.push_back(s.recR - 0.004f);
        }
        if (s.openTop) { r.push_back(s.yRoofF - 0.004f); }
        // hard rows must survive deduplication exactly
        std::vector<float> hard;
        hard.push_back(yR);
        hard.push_back(yF);
        for (int a = 0; a < 2; a++) {
            if (!archOn(a)) continue;
            float yw = a == 0 ? yWf : yWr;
            hard.push_back(yw + Ra);
            hard.push_back(yw - Ra);
        }
        if (s.recDepth > 0.f) {
            hard.push_back(s.recF + 0.004f); hard.push_back(s.recF - 0.004f);
            hard.push_back(s.recR + 0.004f); hard.push_back(s.recR - 0.004f);
        }
        // sort, clamp, dedupe (hard rows win), fill gaps
        std::vector<vec2> v;  // (y, hard)
        for (float y : r) if (y >= yR - 1e-4f && y <= yF + 1e-4f) v.push_back(vec2(Clamp(y, yR, yF), 0.f));
        for (float y : hard) if (y >= yR - 1e-4f && y <= yF + 1e-4f) v.push_back(vec2(Clamp(y, yR, yF), 1.f));
        std::sort(v.begin(), v.end(), [](vec2 a, vec2 b) { return a.x < b.x; });
        std::vector<vec2> uu;
        for (vec2 y : v) {
            if (!uu.empty() && y.x - uu.back().x <= 0.0025f) {
                if (y.y > uu.back().y) uu.back() = y;
                continue;
            }
            uu.push_back(y);
        }
        std::vector<float> u;
        for (vec2 y : uu) u.push_back(y.x);
        rows.clear();
        const float maxGap = 0.1f;
        for (size_t i = 0; i < u.size(); i++) {
            if (i > 0) {
                float g = u[i] - u[i - 1];
                int n = (int)ceilf(g / maxGap);
                for (int k = 1; k < n; k++) rows.push_back(u[i - 1] + g * k / n);
            }
            rows.push_back(u[i]);
        }
        nr = (int)rows.size();
    }

    // side band x at height z for row y (base shape + flare displacement)
    float sideX(float y, float z, float W, float sc, float z0, float z1, float zw) const {
        float x = W;
        if (z < zw) {
            float t = Saturate((zw - z) / Max(zw - z0, 1e-3f));
            x -= s.tuLow * sc * t * t;
        } else {
            float t = Saturate((z - zw) / Max(z1 - zw, 1e-3f));
            x -= s.tuTop * sc * t * t;
        }
        if (s.charOut != 0.f && s.zChar > 0.f) {
            float d = fabsf(z - s.zChar);
            x += s.charOut * sc * Max(0.f, 1.f - d / 0.05f);
        }
        if (s.flareOut != 0.f) {
            float d = archDist(y, z);
            if (d > -0.001f) x += s.flareOut * sc * (1.f - smooth01(d / Max(s.flareW * 1.6f, 0.02f)));
        }
        return x;
    }

    void computeSection(int ri, vec3* out) {
        float y = rows[ri];
        float zb = cBot(y);
        float W = planW(y);
        float sc = Saturate(W / s.halfW);
        float zSh = shoulderZ(y);
        float zC = centreZ(y);
        float H = Max(zSh - zb, 0.02f);
        float rb = Min(s.cornerR, H * 0.22f), rs = Min(s.shR, H * 0.3f);
        float rbx = s.cornerR * sc, rsx = s.shRx * sc;
        // bottom
        float xbEnd = Max(W - s.tuLow * sc - rbx, 0.f);
        float fr[NB + 1] = {0.f, 0.45f, 0.8f, 1.f};
        for (int i = 0; i <= NB; i++) out[i] = vec3(xbEnd * fr[i], y, zb);
        // corner
        for (int i = 0; i <= NC; i++) {
            float ph = kHalfPi * i / NC;
            out[pCor0 + i] = vec3(xbEnd + rbx * sinf(ph), y, zb + rb - rb * cosf(ph));
        }
        // side band anchors
        float z0 = zb + rb, z1 = Max(zSh - rs, z0 + 0.01f);
        float span = z1 - z0;
        float aA = archAt(y, 0.f) >= 0 ? archTop(y) : s.wheelR;
        float aF = archAt(y, s.flareW) >= 0 ? flareTop(y) : s.wheelR;
        aA = Clamp(aA, z0 + span * 0.02f, z1 - span * 0.12f);
        aF = Clamp(Max(aF, aA), aA, z1 - span * 0.08f);
        float aC = s.zChar > 0.f ? s.zChar : (aF + z1) * 0.5f;
        aC = Clamp(aC, aF + span * 0.03f, z1 - span * 0.04f);
        if (aC < aF) aC = aF;
        float zw = Clamp(s.zWide, z0 + span * 0.2f, z1 - span * 0.2f);
        float anchors[5] = {z0, aA, aF, aC, z1};
        int counts[4] = {NSA, NSF, NSC, NST};
        int j = pSide0;
        for (int sgi = 0; sgi < 4; sgi++)
            for (int k = 0; k < counts[sgi]; k++) {
                float z = lerp(anchors[sgi], anchors[sgi + 1], (float)k / counts[sgi]);
                out[j++] = vec3(sideX(y, z, W, sc, z0, z1, zw), y, z);
            }
        out[j] = vec3(sideX(y, z1, W, sc, z0, z1, zw), y, z1);
        // shoulder
        float x1 = out[pSh0].x;
        float xw = Max(x1 - rsx, 0.f);
        for (int i = 0; i <= NSH; i++) {
            float ph = kHalfPi * i / NSH;
            out[pSh0 + i] = vec3(xw + rsx * cosf(ph), y, z1 + (zSh - z1) * sinf(ph));
        }
        // deck parabola
        auto deckZ = [&](float x) {
            float t = xw > 1e-4f ? x / xw : 0.f;
            return zSh + (zC - zSh) * (1.f - t * t);
        };
        float wg0 = Max(xw - s.ghInset * sc, 0.f);
        out[pLed0 + 1] = vec3(wg0, y, deckZ(wg0));
        float zg0 = deckZ(wg0);
        float L = lift(y, zC);
        rowL[ri] = L;
        rowXw[ri] = xw;
        rowZsh[ri] = zSh;
        vec3* gh = out + pGh0;
        if (L > 1e-4f && wg0 > 0.02f) {
            float eC = Min(s.roofCrown, L * 0.3f) * smooth01(L / 0.15f);
            float hEdge = L - eC;
            float cx = wg0 - hEdge * s.lean - s.roofPinch * smooth01(hEdge / 0.3f);
            cx = Max(cx, wg0 * 0.3f);
            vec2 C(cx, zg0 + hEdge);
            vec2 B(wg0, zg0);
            vec2 gd = normalize(C - B);
            float rr = Min(s.railR, Min(hEdge * 0.45f, cx * 0.3f));
            vec2 S = C - gd * rr, E(C.x - rr, C.y);
            // side glass with split
            float tsp = 0.5f;
            if (s.dloRearTop != s.dloRearBot) {
                float lo = Min(s.dloRearBot, s.dloRearTop), hi = Max(s.dloRearBot, s.dloRearTop);
                if (y > lo && y < hi) tsp = (y - s.dloRearBot) / (s.dloRearTop - s.dloRearBot);
            }
            tsp = Clamp(tsp, 0.04f, 0.96f);
            for (int i = 0; i <= NG1; i++) {
                vec2 p = lerp(B, S, tsp * i / NG1);
                gh[i] = vec3(p.x, y, p.y);
            }
            for (int i = 1; i <= NG2; i++) {
                vec2 p = lerp(B, S, tsp + (1.f - tsp) * i / NG2);
                gh[NG1 + i] = vec3(p.x, y, p.y);
            }
            for (int i = 1; i <= NR; i++) {
                float u = (float)i / NR;
                vec2 p = S * ((1 - u) * (1 - u)) + C * (2 * u * (1 - u)) + E * (u * u);
                out[pRail0 + i] = vec3(p.x, y, p.y);
            }
            float wg1 = E.x;
            for (int i = 1; i <= NT; i++) {
                float f = 1.f - (float)i / NT;
                float x = wg1 * f;
                float xp = wg1 > 1e-4f ? x * wg0 / wg1 : 0.f;
                float z = deckZ(xp) + L - eC * f * f;
                out[pTop0 + i] = vec3(x, y, z);
            }
        } else if (L < -1e-4f && wg0 > 0.02f) {
            float dp = -L;
            vec2 B(wg0, zg0);
            vec2 C(wg0 - dp * 0.04f, zg0 - dp);
            vec2 gd = normalize(C - B);
            float rr = Min(0.025f, dp * 0.3f);
            vec2 S = C - gd * rr, E(C.x - rr, C.y);
            for (int i = 0; i <= NG1 + NG2; i++) {
                vec2 p = lerp(B, S, (float)i / (NG1 + NG2));
                gh[i] = vec3(p.x, y, p.y);
            }
            for (int i = 1; i <= NR; i++) {
                float u = (float)i / NR;
                vec2 p = S * ((1 - u) * (1 - u)) + C * (2 * u * (1 - u)) + E * (u * u);
                out[pRail0 + i] = vec3(p.x, y, p.y);
            }
            for (int i = 1; i <= NT; i++) {
                float f = 1.f - (float)i / NT;
                out[pTop0 + i] = vec3(E.x * f, y, C.y);
            }
        } else {
            for (int i = 0; i <= NG1 + NG2 + NR; i++) gh[i] = vec3(wg0, y, zg0);
            for (int i = 1; i <= NT; i++) {
                float f = 1.f - (float)i / NT;
                float x = wg0 * f;
                out[pTop0 + i] = vec3(x, y, deckZ(x));
            }
        }
        out[0].x = 0.f;
        out[NP - 1].x = 0.f;
    }

    void buildGrid() {
        G.resize(nr * NP);
        rowL.assign(nr, 0.f);
        rowXw.assign(nr, 0.f);
        rowZsh.assign(nr, 0.f);
        for (int i = 0; i < nr; i++) computeSection(i, &G[i * NP]);
        // grid normals (angle weighted over adjacent quads)
        GN.assign(nr * NP, vec3(0, 0, 0));
        for (int i = 0; i + 1 < nr; i++)
            for (int j = 0; j + 1 < NP; j++) {
                vec3 a = G[i * NP + j], b = G[(i + 1) * NP + j], c = G[(i + 1) * NP + j + 1], d = G[i * NP + j + 1];
                vec3 n = cross(c - a, d - b);
                float l = length(n);
                if (l < 1e-10f) continue;
                n = n / l;
                GN[i * NP + j] += n;
                GN[(i + 1) * NP + j] += n;
                GN[(i + 1) * NP + j + 1] += n;
                GN[i * NP + j + 1] += n;
            }
        for (int i = 0; i < nr; i++) {
            // centre line normals: remove x component (symmetry)
            GN[i * NP].x = 0.f;
            GN[i * NP + NP - 1].x = 0.f;
            for (int j = 0; j < NP; j++) {
                vec3& n = GN[i * NP + j];
                n = length2(n) > 1e-12f ? normalize(n) : vec3(0, 0, 1);
            }
        }
    }

    vec3 cellCenter(int i, int j) const {
        return (G[i * NP + j] + G[(i + 1) * NP + j] + G[(i + 1) * NP + j + 1] + G[i * NP + j + 1]) * 0.25f;
    }
    bool cellDegenerate(int i, int j) const {
        vec3 a = G[i * NP + j], b = G[(i + 1) * NP + j], c = G[(i + 1) * NP + j + 1], d = G[i * NP + j + 1];
        return length(cross(c - a, d - b)) < 2e-7f;
    }

    // -------------------------------------------------------------------------------------------
    // Classification of every grid cell (right half)
    std::function<void(CarBody&, int, int, vec3, u8&)> liveryHook;

    void classify() {
        cls.assign((nr - 1) * (NP - 1), CC_PAINT);
        float xWell = Min(s.trackF, s.trackR) - s.wheelW * 0.5f - 0.055f;
        for (int i = 0; i + 1 < nr; i++) {
            float y0 = rows[i], y1 = rows[i + 1], yc = (y0 + y1) * 0.5f;
            float L = (rowL[i] + rowL[i + 1]) * 0.5f;
            float Lmin = Min(rowL[i], rowL[i + 1]), Lmax = Max(rowL[i], rowL[i + 1]);
            for (int j = 0; j + 1 < NP; j++) {
                u8& c = cls[i * (NP - 1) + j];
                Band b = (Band)cellBand[j];
                vec3 cc = cellCenter(i, j);
                if (cellDegenerate(i, j)) { c = CC_SKIP; continue; }
                c = CC_PAINT;
                bool archRow = archAt(yc, 0.f) >= 0;
                if (b == BD_BOTTOM) {
                    float xo = Max(G[i * NP + j + 1].x, G[(i + 1) * NP + j + 1].x);
                    c = (archRow && xo > xWell) ? CC_HOLE : CC_PLASTIC;
                    continue;
                }
                if (b == BD_CORNER || b == BD_SIDE) {
                    if (inOpening(yc, cc.z)) { c = CC_HOLE; continue; }
                    bool flareZone = archAt(yc, s.flareW) >= 0 && j >= jArch && j < jFlare;
                    if (s.plasticArches && (flareZone || (archAt(yc, s.flareW) >= 0 && j < jArch))) c = CC_PLASTIC;
                    if (s.plasticSills && (b == BD_CORNER || j < jArch)) c = CC_PLASTIC;
                    if (s.plasticBumpers && (b == BD_CORNER || j < jArch) && (yc > yWf + Ra || yc < yWr - Ra)) c = CC_PLASTIC;
                } else if (b == BD_SHOULDER || b == BD_LEDGE) {
                    c = CC_PAINT;
                    if (b == BD_LEDGE && yc > s.yCowl && yc < s.yCowl + s.cowlLen) c = CC_PLASTIC;
                } else if (b == BD_GHSIDE) {
                    if (Lmax < -1e-4f || Lmin < -1e-4f) { c = s.cockpit ? CC_INTERIOR : CC_BED; }
                    else if (Lmin <= 1e-4f) c = CC_PAINT;
                    else {
                        // DLO rules
                        bool glass = yc < s.dloFront && yc > Min(s.dloRearBot, s.dloRearTop);
                        if (glass && fabsf(s.dloRearTop - s.dloRearBot) > 1e-4f && yc < Max(s.dloRearBot, s.dloRearTop))
                            glass = (s.dloRearTop > s.dloRearBot) ? (j < jSplit) : (j >= jSplit);
                        bool inPillar = fabsf(yc - s.bPillar) < s.bPillarW * 0.5f || (s.cPillar > -99.f && fabsf(yc - s.cPillar) < s.cPillarW * 0.5f);
                        for (const vec2& pl : s.pillars) if (fabsf(yc - pl.x) < pl.y * 0.5f) inPillar = true;
                        if (inPillar || !s.sideGlass) glass = false;
                        if (glass) c = CC_GLASS;
                        else {
                            bool pillarB = inPillar && s.sideGlass;
                            if (pillarB && s.glossPillars) c = CC_GLOSS;
                            else if (yc >= s.dloFront) c = CC_GLOSS;
                            else c = s.blackRoof ? CC_GLOSS : CC_PAINT;
                        }
                        if (s.openTop) c = CC_PAINT;
                    }
                } else if (b == BD_RAIL) {
                    if (Lmin < -1e-4f) c = s.cockpit ? CC_INTERIOR : CC_BED;
                    else if (Lmax <= 1e-4f) c = CC_PAINT;
                    else {
                        c = (s.blackRoof || (s.blackAPillar && yc > s.yRoofF)) ? CC_GLOSS : CC_PAINT;
                    }
                } else {  // top
                    if (Lmin < -1e-4f) c = s.cockpit ? CC_INTERIOR : CC_BED;
                    else if (Lmax <= 1e-4f) {
                        c = CC_PAINT;
                        if (yc > s.yCowl && yc < s.yCowl + s.cowlLen) c = CC_PLASTIC;
                    } else {
                        if (yc > s.yRoofF) c = CC_GLASS;
                        else if (yc < s.yRoofR) c = s.openTop ? CC_INTERIOR : (s.rearGlass ? CC_GLASS : CC_PAINT);
                        else {
                            c = s.blackRoof ? CC_GLOSS : CC_PAINT;
                            if (s.sunroof && j >= pTop0 + 2 && yc < s.yRoofF - 0.12f && yc > s.yRoofF - 0.12f - 0.75f) c = CC_GLASS;
                        }
                    }
                }
                if (s.liveryDoors && (b == BD_SIDE || b == BD_SHOULDER || b == BD_CORNER) && c == CC_PAINT &&
                    yc > Min(s.liveryY0, s.liveryY1) && yc < Max(s.liveryY0, s.liveryY1))
                    c = CC_PAINT2;
                if (s.liveryRoof && (b == BD_TOP || b == BD_RAIL) && c == CC_PAINT && L > 0.2f) c = CC_PAINT2;
                if (liveryHook) liveryHook(*this, i, j, cc, c);
            }
        }
    }

    static void matFor(u8 c, u8& mat, u32& color) {
        switch (c) {
            case CC_PAINT2: mat = MAT_CARPAINT; color = kCol2; break;
            case CC_PLASTIC: mat = MAT_PLASTIC; color = col(0.9f, 0.9f, 0.9f); break;
            case CC_GLOSS: mat = MAT_CAR_GLASS; color = kCol1; break;
            case CC_GLASS: mat = MAT_CAR_GLASS; color = kCol1; break;
            case CC_BED: mat = MAT_PLASTIC; color = col(0.7f, 0.7f, 0.7f); break;
            case CC_INTERIOR: mat = MAT_INTERIOR; color = kCol1; break;
            case CC_CHROME: mat = MAT_CHROME; color = kCol1; break;
            default: mat = MAT_CARPAINT; color = kCol1; break;
        }
    }

    // -------------------------------------------------------------------------------------------
    // Emit the right half of the shell (+ glass insets and seals), then arch wells. Caller mirrors.
    void emitShell(PMesh& m) {
        const float inset = 0.011f;
        m.newGroup(32.f);
        int NC1 = NP - 1;
        // shared vertex ids for outer surface
        std::vector<u32> vid(nr * NP, 0xffffffffu), gid(nr * NP, 0xffffffffu);
        auto V = [&](int i, int j) {
            u32& v = vid[i * NP + j];
            if (v == 0xffffffffu) v = m.add(G[i * NP + j]);
            return v;
        };
        auto GV = [&](int i, int j) {
            u32& v = gid[i * NP + j];
            if (v == 0xffffffffu) v = m.add(G[i * NP + j] - GN[i * NP + j] * inset);
            return v;
        };
        for (int i = 0; i + 1 < nr; i++)
            for (int j = 0; j < NC1; j++) {
                u8 c = cls[i * NC1 + j];
                if (c == CC_HOLE || c == CC_SKIP) continue;
                u8 mt;
                u32 cl;
                matFor(c, mt, cl);
                m.use(mt, cl);
                if (c == CC_GLASS) m.quad(GV(i, j), GV(i + 1, j), GV(i + 1, j + 1), GV(i, j + 1));
                else m.quad(V(i, j), V(i + 1, j), V(i + 1, j + 1), V(i, j + 1));
            }
        // glass seals: walls between glass cells and non-glass neighbours
        m.newGroup(30.f);
        m.use(MAT_PLASTIC, col(0.6f, 0.6f, 0.6f));
        auto isGlass = [&](int i, int j) {
            if (i < 0 || i + 1 >= nr) return false;
            if (j < 0 || j >= NC1) return true;  // across the centre line: mirrored cell is glass too
            u8 c = cls[i * NC1 + j];
            return c == CC_GLASS;
        };
        for (int i = 0; i + 1 < nr; i++)
            for (int j = 0; j < NC1; j++) {
                if (cls[i * NC1 + j] != CC_GLASS) continue;
                vec3 ctr = cellCenter(i, j);
                auto wall = [&](int ia, int ja, int ib, int jb) {
                    u32 o0 = m.add(G[ia * NP + ja]), o1 = m.add(G[ib * NP + jb]);
                    u32 g0 = m.add(G[ia * NP + ja] - GN[ia * NP + ja] * inset), g1 = m.add(G[ib * NP + jb] - GN[ib * NP + jb] * inset);
                    vec3 mid = (G[ia * NP + ja] + G[ib * NP + jb]) * 0.5f;
                    m.quadFacing(o0, o1, g1, g0, ctr - mid);
                };
                if (!isGlass(i - 1, j)) wall(i, j, i, j + 1);
                if (!isGlass(i + 1, j)) wall(i + 1, j, i + 1, j + 1);
                if (j > 0 && !isGlass(i, j - 1)) wall(i, j, i + 1, j);
                if (j + 1 < NC1 && !isGlass(i, j + 1)) wall(i, j + 1, i + 1, j + 1);
            }
        emitArches(m);
    }

    // wheel wells: lip + liner along the opening boundary, inner wall
    void emitArches(PMesh& m) {
        for (int a = 0; a < 2; a++) {
            if (!archOn(a)) continue;
            float yw = a == 0 ? yWf : yWr;
            if (yw - Ra < yR + 0.01f || yw + Ra > yF - 0.01f) continue;
            float track = a == 0 ? s.trackF : s.trackR;
            float xIn = track - s.wheelW * 0.5f - 0.05f;
            // rows of the arch
            int i0 = -1, i1 = -1;
            for (int i = 0; i < nr; i++) {
                if (fabsf(rows[i] - (yw - Ra)) < 1e-4f && i0 < 0) i0 = i;
                if (fabsf(rows[i] - (yw + Ra)) < 1e-4f) i1 = i;
            }
            if (i0 < 0 || i1 < 0) {
                i0 = rowAt(yw - Ra);
                i1 = rowAt(yw + Ra);
            }
            if (i0 < 0 || i1 < 0) continue;
            // boundary polyline: rear edge (bottom -> up), arc, front edge (down)
            std::vector<vec3> B;
            std::vector<u8> isArc;
            for (int j = pCor0; j <= jArch; j++) { B.push_back(G[i0 * NP + j]); isArc.push_back(0); }
            for (int i = i0 + 1; i < i1; i++) { B.push_back(G[i * NP + jArch]); isArc.push_back(1); }
            for (int j = jArch; j >= pCor0; j--) { B.push_back(G[i1 * NP + j]); isArc.push_back(0); }
            int n = (int)B.size();
            vec3 wc(0, yw, s.wheelR);
            // inner points
            std::vector<vec3> I(n), Lp(n);
            for (int k = 0; k < n; k++) {
                vec3 p = B[k];
                vec2 rad(p.y - yw, p.z - s.wheelR);
                vec2 off;
                if (isArc[k] || p.z >= s.wheelR) off = normalize(rad) * 0.012f;
                else off = vec2(p.y > yw ? 0.012f : -0.012f, 0.f);
                float lipIn = Min(0.03f, Max(p.x - xIn, 0.f) * 0.3f);
                Lp[k] = vec3(p.x - lipIn, p.y + off.x * 0.5f, p.z + off.y * 0.5f);
                I[k] = vec3(xIn, p.y + off.x, p.z + off.y);
            }
            m.newGroup(35.f);
            u8 lipMat = s.plasticArches ? MAT_PLASTIC : MAT_CARPAINT;
            for (int k = 0; k + 1 < n; k++) {
                vec3 ctr = (B[k] + B[k + 1] + I[k] + I[k + 1]) * 0.25f;
                vec3 facing = vec3(0, wc.y, wc.z) - vec3(0, ctr.y, ctr.z);
                if (length2(facing) < 1e-6f) facing = vec3(0, 0, -1);
                m.use(lipMat, s.plasticArches ? col(0.9f, 0.9f, 0.9f) : kCol1);
                u32 a0 = m.add(B[k]), a1 = m.add(B[k + 1]), l0 = m.add(Lp[k]), l1 = m.add(Lp[k + 1]);
                m.quadFacing(a0, a1, l1, l0, facing);
                m.use(MAT_PLASTIC, col(0.55f, 0.55f, 0.55f));
                u32 b0 = m.add(Lp[k]), b1 = m.add(Lp[k + 1]), c0 = m.add(I[k]), c1 = m.add(I[k + 1]);
                m.quadFacing(b0, b1, c1, c0, facing);
            }
            // inner wall polygon in (y,z), facing +x (extended below the underbody)
            I.insert(I.begin(), vec3(xIn, I[0].y, I[0].z - 0.04f));
            I.push_back(vec3(xIn, I.back().y, I.back().z - 0.04f));
            n = (int)I.size();
            std::vector<vec2> poly;
            for (int k = 0; k < n; k++) poly.push_back(vec2(I[k].y, I[k].z));
            std::vector<u32> tris;
            triangulatePolygon(poly, tris);
            m.use(MAT_PLASTIC, col(0.5f, 0.5f, 0.5f));
            std::vector<u32> ids(n);
            for (int k = 0; k < n; k++) ids[k] = m.add(I[k]);
            for (size_t t = 0; t + 2 < tris.size(); t += 3) {
                u32 p0 = ids[tris[t]], p1 = ids[tris[t + 1]], p2 = ids[tris[t + 2]];
                vec3 fn = cross(m.P[p1] - m.P[p0], m.P[p2] - m.P[p0]);
                if (fn.x >= 0.f) m.tri(p0, p1, p2);
                else m.tri(p0, p2, p1);
            }
        }
    }

    // Projector over the full (mirrored) shell: non-glass cells, and a separate one for glass
    void buildProjectors() {
        int NC1 = NP - 1;
        proj.tris.clear();
        glassProj.tris.clear();
        for (int side = 0; side < 2; side++) {
            float sx = side == 0 ? 1.f : -1.f;
            for (int i = 0; i + 1 < nr; i++)
                for (int j = 0; j < NC1; j++) {
                    u8 c = cls[i * NC1 + j];
                    if (c == CC_HOLE || c == CC_SKIP) continue;
                    vec3 p[4] = {G[i * NP + j], G[(i + 1) * NP + j], G[(i + 1) * NP + j + 1], G[i * NP + j + 1]};
                    vec3 n[4] = {GN[i * NP + j], GN[(i + 1) * NP + j], GN[(i + 1) * NP + j + 1], GN[i * NP + j + 1]};
                    for (int k = 0; k < 4; k++) { p[k].x *= sx; n[k].x *= sx; }
                    Projector& pr = c == CC_GLASS ? glassProj : proj;
                    if (side == 0) {
                        pr.addTri(p[0], p[1], p[2], n[0], n[1], n[2]);
                        pr.addTri(p[0], p[2], p[3], n[0], n[2], n[3]);
                    } else {
                        pr.addTri(p[0], p[2], p[1], n[0], n[2], n[1]);
                        pr.addTri(p[0], p[3], p[2], n[0], n[3], n[2]);
                    }
                }
        }
    }

    // row index nearest to y
    int rowAt(float y) const {
        int best = 0;
        for (int i = 1; i < nr; i++) if (fabsf(rows[i] - y) < fabsf(rows[best] - y)) best = i;
        return best;
    }
    // surface point of the side (right) at (y, z) via the grid section (side band + shoulder)
    float sideXAt(float y, float z) const {
        int i = rowAt(y);
        const vec3* sec = &G[i * NP];
        for (int j = pCor0; j < pLed0; j++) {
            if ((sec[j].z <= z && sec[j + 1].z >= z) || (sec[j].z >= z && sec[j + 1].z <= z)) {
                float t = (z - sec[j].z) / Max(fabsf(sec[j + 1].z - sec[j].z), 1e-5f);
                return lerp(sec[j].x, sec[j + 1].x, Saturate(fabsf(t)));
            }
        }
        return sec[pSh0].x;
    }
    float topZAt(float y, float x) const {
        int i = rowAt(y);
        const vec3* sec = &G[i * NP];
        for (int j = pSh0; j + 1 < NP; j++) {
            if ((sec[j].x >= x && sec[j + 1].x <= x)) {
                float t = (sec[j].x - x) / Max(sec[j].x - sec[j + 1].x, 1e-5f);
                return lerp(sec[j].z, sec[j + 1].z, Saturate(t));
            }
        }
        return sec[NP - 1].z;
    }
    // greenhouse helpers for a row: base (belt) and rail z
    float beltZAt(float y) const { int i = rowAt(y); return G[i * NP + pGh0].z; }
    float beltXAt(float y) const { int i = rowAt(y); return G[i * NP + pGh0].x; }
    float railZAt(float y) const { int i = rowAt(y); return G[i * NP + pRail0 + NR].z; }
    float railXAt(float y) const { int i = rowAt(y); return G[i * NP + pRail0 + NR].x; }
    float roofZAt(float y) const { int i = rowAt(y); return G[i * NP + NP - 1].z; }

    void build(PMesh& m) {
        setup();
        buildRows();
        buildGrid();
        classify();
        PMesh::Mark mk = m.mark();
        emitShell(m);
        m.mirrorX(mk);
        buildProjectors();
    }
};

}  // namespace detail
}  // namespace Vehicles
