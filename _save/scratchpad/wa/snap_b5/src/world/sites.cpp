// Site layout: Porto Sol International, Port Isle terminal, Key Coral, landmarks and countryside details.
// Runs on the world generation thread; everything here is deterministic.
#include "sites.h"
#include "buildings.h"
#include "transit.h"
#include "worldtypes.h"
#include "../render/mesh.h"
#include "../core/noise.h"

namespace World {

namespace sites_storage {
SiteSet gSiteSet;
}
SiteSet* gSites = &sites_storage::gSiteSet;

namespace sites_detail {

inline u32 col8(float r, float g, float b) { return packRGBA8(r, g, b, 1.f); }

// Polyline with every interior corner replaced by a circular arc of the given radius (clamped to the legs).
std::vector<vec2> roundedPath(const std::vector<vec2>& ctrl, float radius, float step = 4.f) {
    std::vector<vec2> out;
    if (ctrl.size() < 3) return ctrl;
    out.push_back(ctrl[0]);
    for (size_t i = 1; i + 1 < ctrl.size(); i++) {
        vec2 p = ctrl[i], a = ctrl[i - 1], b = ctrl[i + 1];
        vec2 d0 = normalize(p - a), d1 = normalize(b - p);
        float turn = acosf(Clamp(dot(d0, d1), -1.f, 1.f));
        if (turn < 0.02f) { out.push_back(p); continue; }
        float r = Min(radius, Min(length(p - a), length(b - p)) * 0.49f / Max(tanf(turn * 0.5f), 1e-3f));
        float t = r * tanf(turn * 0.5f);
        vec2 s = p - d0 * t, e = p + d1 * t;
        float side = cross(d0, d1) > 0 ? 1.f : -1.f;
        vec2 cen = s + perp(d0) * (r * side);
        float a0 = atan2f(s.y - cen.y, s.x - cen.x);
        float sweep = turn * side;
        int n = Max(2, (int)ceilf(fabsf(sweep) * r / step));
        for (int k = 0; k <= n; k++) {
            float ang = a0 + sweep * k / n;
            out.push_back(cen + vec2(cosf(ang), sinf(ang)) * r);
        }
        (void)e;
    }
    out.push_back(ctrl.back());
    return out;
}

// Resample a polyline at a fixed spacing (keeps both ends)
std::vector<vec2> resample(const std::vector<vec2>& pts, float step) {
    std::vector<vec2> out;
    if (pts.size() < 2) return pts;
    out.push_back(pts[0]);
    float carry = 0.f;
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        vec2 a = pts[i], b = pts[i + 1];
        float len = length(b - a);
        float s = step - carry;
        while (s < len) {
            out.push_back(lerp(a, b, s / len));
            s += step;
        }
        carry = len - (s - step);
    }
    if (length(out.back() - pts.back()) > step * 0.3f) out.push_back(pts.back());
    else out.back() = pts.back();
    return out;
}

vec2 bezier2(vec2 a, vec2 b, vec2 c, float t) {
    float u = 1.f - t;
    return a * (u * u) + b * (2.f * u * t) + c * (t * t);
}

float meanHeight(const WorldMap& map, vec2 mn, vec2 mx, float step) {
    double s = 0;
    int n = 0;
    for (float y = mn.y; y <= mx.y; y += step)
        for (float x = mn.x; x <= mx.x; x += step) {
            s += map.heightAt(x, y);
            n++;
        }
    return n ? (float)(s / n) : 0.f;
}

// Direct texel edits (terrain shaping for quays, basins and ponds)
template <typename F>
void forTexels(WorldMap& map, vec2 mn, vec2 mx, F fn) {
    int x0 = Max(0, (int)floorf(worldToTexel(mn.x))), x1 = Min(kHeightRes - 1, (int)ceilf(worldToTexel(mx.x)));
    int y0 = Max(0, (int)floorf(worldToTexel(mn.y))), y1 = Min(kHeightRes - 1, (int)ceilf(worldToTexel(mx.y)));
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) fn((size_t)ty * kHeightRes + tx, vec2(texelToWorld(tx), texelToWorld(ty)));
}

// Carve a water basin (rectangle, rounded blend) connected to existing water
void carveBasin(WorldMap& map, vec2 c, vec2 ax, float hx, float hy, float depth) {
    vec2 ay = perp(ax);
    float r = sqrtf(hx * hx + hy * hy) + 16.f;
    forTexels(map, c - vec2(r), c + vec2(r), [&](size_t i, vec2 p) {
        vec2 d = p - c;
        float lx = fabsf(dot(d, ax)) - hx, ly = fabsf(dot(d, ay)) - hy;
        float out = Max(lx, ly);
        if (out > 12.f) return;
        float k = SmoothStep(12.f, 0.f, out);
        map.height[i] = Lerp(map.height[i], Min(map.height[i], -depth), k);
        if (out <= 2.f) map.waterLevel[i] = 0.f;
    });
}

// Water depth probe: march from p along dir until water; returns distance (or -1)
float marchToWater(const WorldMap& map, vec2 p, vec2 dir, float maxDist, float step = 4.f) {
    for (float s = 0; s <= maxDist; s += step) {
        vec2 q = p + dir * s;
        if (map.isWater(q.x, q.y)) return s;
    }
    return -1.f;
}

}  // namespace sites_detail

using namespace sites_detail;

// ---------------------------------------------------------------------------------------------------------------
// Queries

int SiteSet::addPad(const Pad& p) {
    pads.push_back(p);
    return (int)pads.size() - 1;
}

int SiteSet::addElem(const SiteElem& e) {
    elems.push_back(e);
    return (int)elems.size() - 1;
}

void SiteSet::buildPadHash() {
    padRes = (int)(2.f * kWorldHalf / kPadCell);
    padHash.assign((size_t)padRes * padRes, {});
    for (size_t i = 0; i < pads.size(); i++) {
        const Pad& p = pads[i];
        float r = sqrtf(p.hx * p.hx + p.hy * p.hy) + 1.f;
        int x0 = Clamp((int)((p.c.x - r + kWorldHalf) / kPadCell), 0, padRes - 1), x1 = Clamp((int)((p.c.x + r + kWorldHalf) / kPadCell), 0, padRes - 1);
        int y0 = Clamp((int)((p.c.y - r + kWorldHalf) / kPadCell), 0, padRes - 1), y1 = Clamp((int)((p.c.y + r + kWorldHalf) / kPadCell), 0, padRes - 1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                vec2 cc(-kWorldHalf + (x + 0.5f) * kPadCell, -kWorldHalf + (y + 0.5f) * kPadCell);
                if (p.contains(cc, kPadCell * 0.75f)) padHash[(size_t)y * padRes + x].push_back((int)i);
            }
    }
}

void SiteSet::buildRectHash() {
    const std::vector<SiteRect>* lists[3] = {&lotBlocks, &roadBlocks, &vegBlocks};
    int res = (int)(2.f * kWorldHalf / kPadCell);
    for (int w = 0; w < 3; w++) {
        rectHash[w].assign((size_t)res * res, {});
        const auto& L = *lists[w];
        for (size_t i = 0; i < L.size(); i++) {
            const SiteRect& r = L[i];
            float rr = sqrtf(r.hx * r.hx + r.hy * r.hy) + 2.f;
            int x0 = Clamp((int)((r.c.x - rr + kWorldHalf) / kPadCell), 0, res - 1), x1 = Clamp((int)((r.c.x + rr + kWorldHalf) / kPadCell), 0, res - 1);
            int y0 = Clamp((int)((r.c.y - rr + kWorldHalf) / kPadCell), 0, res - 1), y1 = Clamp((int)((r.c.y + rr + kWorldHalf) / kPadCell), 0, res - 1);
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) rectHash[w][(size_t)y * res + x].push_back((int)i);
        }
    }
}

bool SiteSet::rectHit(int which, vec2 p, float margin) const {
    if (rectHash[which].empty()) return false;
    int res = (int)(2.f * kWorldHalf / kPadCell);
    int x = (int)((p.x + kWorldHalf) / kPadCell), y = (int)((p.y + kWorldHalf) / kPadCell);
    if (x < 0 || y < 0 || x >= res || y >= res) return false;
    const std::vector<SiteRect>& L = which == 0 ? lotBlocks : (which == 1 ? roadBlocks : vegBlocks);
    for (int i : rectHash[which][(size_t)y * res + x])
        if (L[i].contains(p, margin)) return true;
    return false;
}

bool SiteSet::blocksLots(vec2 p) const { return rectHit(0, p, 0.f); }
bool SiteSet::blocksRoads(vec2 p) const { return rectHit(1, p, 0.f); }
bool SiteSet::blocksVegetation(vec2 p) const { return rectHit(2, p, 0.f) || padAt(p) != nullptr; }

const Pad* SiteSet::padAt(vec2 p) const {
    if (padHash.empty()) return nullptr;
    int x = (int)((p.x + kWorldHalf) / kPadCell), y = (int)((p.y + kWorldHalf) / kPadCell);
    if (x < 0 || y < 0 || x >= padRes || y >= padRes) return nullptr;
    const Pad* best = nullptr;
    for (int i : padHash[(size_t)y * padRes + x]) {
        const Pad& pd = pads[i];
        if (!pd.contains(p)) continue;
        if (!best || pd.heightAt(p) > best->heightAt(p)) best = &pd;
    }
    return best;
}

bool SiteSet::roofDeckAt(vec2 p, float roofZ) const {
    if (padHash.empty()) return false;
    int x = (int)((p.x + kWorldHalf) / kPadCell), y = (int)((p.y + kWorldHalf) / kPadCell);
    if (x < 0 || y < 0 || x >= padRes || y >= padRes) return false;
    for (int i : padHash[(size_t)y * padRes + x]) {
        const Pad& pd = pads[i];
        if (pd.kind == PAD_PARKING && pd.slope == 0.f && fabsf(pd.z - roofZ) < 0.5f && pd.contains(p)) return true;
    }
    return false;
}

bool SiteSet::padHeight(vec2 p, float* z, float maxZ) const {
    if (padHash.empty()) return false;
    int x = (int)((p.x + kWorldHalf) / kPadCell), y = (int)((p.y + kWorldHalf) / kPadCell);
    if (x < 0 || y < 0 || x >= padRes || y >= padRes) return false;
    bool found = false;
    float bz = -1e9f;
    for (int i : padHash[(size_t)y * padRes + x]) {
        const Pad& pd = pads[i];
        if (!pd.contains(p)) continue;
        float h = pd.heightAt(p);
        if (h <= maxZ && h > bz) { bz = h; found = true; }
    }
    if (found) *z = bz;
    return found;
}

// ---------------------------------------------------------------------------------------------------------------
// Layout helpers shared by the area layouts below

namespace sites_detail {

struct Lay {
    SiteSet& S;
    WorldMap& map;
    Pad& pad(vec2 c, vec2 ax, float hx, float hy, float z, u8 kind, u32 color = 0xffffffffu, bool drawn = true, bool skirt = true) {
        Pad p;
        p.c = c;
        p.ax = normalize(ax);
        p.hx = hx;
        p.hy = hy;
        p.z = z;
        p.slope = 0.f;
        p.kind = kind;
        p.drawn = drawn ? 1 : 0;
        p.skirt = skirt ? 1 : 0;
        p.flags = 0;
        p.color = color;
        S.pads.push_back(p);
        return S.pads.back();
    }
    // Axis-aligned pad from min/max corners
    Pad& padAA(float x0, float y0, float x1, float y1, float z, u8 kind, u32 color = 0xffffffffu, bool drawn = true) {
        return pad(vec2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f), vec2(1, 0), (x1 - x0) * 0.5f, (y1 - y0) * 0.5f, z, kind, color, drawn);
    }
    SiteElem& elem(u16 kind, vec2 c, vec2 ax, float hx, float hy, float z, float h, u32 seed) {
        SiteElem e;
        e.kind = kind;
        e.c = c;
        e.ax = normalize(ax);
        e.hx = hx;
        e.hy = hy;
        e.z = z;
        e.h = h;
        e.seed = seed;
        S.elems.push_back(e);
        return S.elems.back();
    }
    SiteElem& line(u16 kind, vec2 a, vec2 b, float halfW, float z, float h, u32 seed) {
        SiteElem& e = elem(kind, (a + b) * 0.5f, normalize(b - a), length(b - a) * 0.5f, halfW, z, h, seed);
        e.a = a;
        e.b = b;
        return e;
    }
    void block(std::vector<SiteRect>& L, vec2 c, vec2 ax, float hx, float hy) { L.push_back({c, normalize(ax), hx, hy}); }
    void blockAA(std::vector<SiteRect>& L, float x0, float y0, float x1, float y1) {
        L.push_back({vec2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f), vec2(1, 0), (x1 - x0) * 0.5f, (y1 - y0) * 0.5f});
    }
    void road(const std::vector<vec2>& pts, RoadClass cls, u8 flags, const char* name) {
        SiteRoad r;
        r.pts = pts;
        r.cls = (u8)cls;
        r.flags = flags;
        r.layer = 0;
        r.name = name;
        S.roads.push_back(std::move(r));
    }
    void building(vec2 c, vec2 ax, float hx, float hy, BuildingStyle st, RoofType roof, int floors, vec2 front, u8 region, float baseZ = -1000.f) {
        SiteBuildingReq b;
        b.c = c;
        b.ax = normalize(ax);
        b.hx = hx;
        b.hy = hy;
        b.style = (u8)st;
        b.roof = (u8)roof;
        b.floors = (u16)floors;
        b.front = normalize(front);
        b.baseZ = baseZ;
        b.region = region;
        S.buildingReqs.push_back(b);
    }
};

// ---------------------------------------------------------------------------------------------------------------
// Porto Sol International Airport (PSI)
//   Runway 09L/27R: y = 2080, x -1180..820 (2000 m x 60 m)    Runway 09R/27L: y = 780, x -1100..700 (1800 m x 45 m)
//   Taxiway A (north parallel) y = 1900, taxiway B (south parallel) y = 960
//   Terminal x 575..680, y 1160..1720 with three piers (concourses A/B/C) reaching west to x = 210
//   Landside: curb loop (x 706 / 745), garages, surface lot, control tower; Airport Boulevard joins the city grid at x = 1000
namespace ap {
constexpr float RN_Y = 2080.f, RN_X0 = -1180.f, RN_X1 = 820.f, RN_HW = 30.f;
constexpr float RS_Y = 780.f, RS_X0 = -1100.f, RS_X1 = 700.f, RS_HW = 22.5f;
constexpr float TA_Y = 1900.f, TB_Y = 960.f, TW_HW = 15.f;
constexpr float TERM_X0 = 575.f, TERM_X1 = 680.f, TERM_Y0 = 1160.f, TERM_Y1 = 1720.f;
constexpr float PIER_TIP = 210.f, PIER_HW = 12.f;
constexpr float CURB_X = 706.f, LOOP_X = 745.f, BLVD_Y = 1500.f;
const float kPierY[3] = {1630.f, 1440.f, 1250.f};
}  // namespace ap

void layoutAirport(Lay& L) {
    using namespace ap;
    WorldMap& map = L.map;
    SiteSet& S = L.S;
    float zA = meanHeight(map, vec2(kAirportX0, kAirportY0), vec2(kAirportX1, kAirportY1), 40.f);
    zA = roundf(Clamp(zA, 2.5f, 9.f) * 10.f) / 10.f;
    S.airportZ = zA;
    vec2 cA((kAirportX0 + kAirportX1) * 0.5f, (kAirportY0 + kAirportY1) * 0.5f);
    map.flattenRect(cA, vec2(1, 0), (kAirportX1 - kAirportX0) * 0.5f + 12.f, (kAirportY1 - kAirportY0) * 0.5f + 12.f, zA - 0.25f, 110.f);
    // Approach light corridors to the west keep a gentle level too
    map.flattenRect(vec2(RN_X0 - 260.f, RN_Y), vec2(1, 0), 150.f, 20.f, zA - 0.25f, 60.f);
    map.flattenRect(vec2(RS_X0 - 220.f, RS_Y), vec2(1, 0), 120.f, 20.f, zA - 0.25f, 60.f);

    const u32 kW = 0xffffffffu;
    // ---------------------------------------------------------------- pavement
    // Runways + shoulders + blast pads
    struct RW { float y, x0, x1, hw; const char* names; };
    RW rws[2] = {{RN_Y, RN_X0, RN_X1, RN_HW, "09L|27R"}, {RS_Y, RS_X0, RS_X1, RS_HW, "09R|27L"}};
    for (int i = 0; i < 2; i++) {
        const RW& r = rws[i];
        L.padAA(r.x0, r.y - r.hw, r.x1, r.y + r.hw, zA, PAD_RUNWAY);
        L.padAA(r.x0 - 60.f, r.y - r.hw - 7.5f, r.x1 + 60.f, r.y - r.hw, zA, PAD_SHOULDER);
        L.padAA(r.x0 - 60.f, r.y + r.hw, r.x1 + 60.f, r.y + r.hw + 7.5f, zA, PAD_SHOULDER);
        L.padAA(r.x0 - 60.f, r.y - r.hw, r.x0, r.y + r.hw, zA, PAD_SHOULDER);
        L.padAA(r.x1, r.y - r.hw, r.x1 + 60.f, r.y + r.hw, zA, PAD_SHOULDER);
        SiteElem& e = L.line(SK_RUNWAY, vec2(r.x0, r.y), vec2(r.x1, r.y), r.hw + 70.f, zA, 1.f, 100u + i);
        e.p[0] = r.hw * 2.f;
        e.p[1] = 7.5f;
        e.p[2] = 60.f;
        e.text = r.names;
    }
    // Parallel taxiways and connectors
    L.padAA(RN_X0, TA_Y - TW_HW, RN_X1, TA_Y + TW_HW, zA, PAD_TAXIWAY);
    L.padAA(RS_X0, TB_Y - TW_HW, RS_X1, TB_Y + TW_HW, zA, PAD_TAXIWAY);
    {
        SiteElem& ta = L.line(SK_TAXI_MARKS, vec2(RN_X0, TA_Y), vec2(RN_X1, TA_Y), TW_HW + 4.f, zA, 1.f, 201);
        ta.p[0] = 23.f;
        ta.text = "A";
        SiteElem& tb = L.line(SK_TAXI_MARKS, vec2(RS_X0, TB_Y), vec2(RS_X1, TB_Y), TW_HW + 4.f, zA, 1.f, 202);
        tb.p[0] = 23.f;
        tb.text = "B";
    }
    const float connN[5] = {RN_X0 + 15.f, -760.f, -300.f, 250.f, RN_X1 - 15.f};
    const float connS[5] = {RS_X0 + 15.f, -650.f, -150.f, 300.f, RS_X1 - 15.f};
    for (int i = 0; i < 5; i++) {
        float y0 = TA_Y + TW_HW, y1 = RN_Y - RN_HW - 7.5f;
        L.padAA(connN[i] - TW_HW, y0, connN[i] + TW_HW, y1, zA, PAD_TAXIWAY);
        SiteElem& e = L.line(SK_TAXI_MARKS, vec2(connN[i], TA_Y), vec2(connN[i], RN_Y), TW_HW + 4.f, zA, 1.f, 210 + i);
        e.p[0] = 23.f;
        e.p[1] = 1.f;          // enters a runway at b
        e.p[2] = RN_HW + 7.5f;  // runway half width incl. shoulder at b
        e.text = StrFormat("A%d", i + 1);
        float z0 = RS_Y + RS_HW + 7.5f, z1 = TB_Y - TW_HW;
        L.padAA(connS[i] - TW_HW, z0, connS[i] + TW_HW, z1, zA, PAD_TAXIWAY);
        SiteElem& f = L.line(SK_TAXI_MARKS, vec2(connS[i], TB_Y), vec2(connS[i], RS_Y), TW_HW + 4.f, zA, 1.f, 220 + i);
        f.p[0] = 23.f;
        f.p[1] = 1.f;
        f.p[2] = RS_HW + 7.5f;
        f.text = StrFormat("B%d", i + 1);
    }
    // Aprons: terminal apron, west (cargo / maintenance) apron, GA ramp and north service apron
    float apY0 = TB_Y + TW_HW, apY1 = TA_Y - TW_HW;
    L.padAA(-130.f, apY0, TERM_X0, apY1, zA, PAD_APRON);
    L.padAA(-665.f, apY0, -130.f, apY1, zA, PAD_APRON);
    L.padAA(TERM_X0, apY0, 690.f, 1150.f, zA, PAD_APRON);
    L.padAA(TERM_X0, 1730.f, 690.f, apY1, zA, PAD_APRON);
    L.elem(SK_APRON_MARKS, vec2(222.5f, 1430.f), vec2(1, 0), 352.5f, (apY1 - apY0) * 0.5f, zA, 1.f, 300);
    L.elem(SK_APRON_MARKS, vec2(-397.5f, 1430.f), vec2(1, 0), 267.5f, (apY1 - apY0) * 0.5f, zA, 1.f, 301).variant = 1;
    // Landside: curb sidewalk (raised), garage walkways and the surface lot
    {
        Pad& c = L.padAA(TERM_X1, TERM_Y0, CURB_X - 5.5f, 1760.f, zA + 0.15f, PAD_PLAZA);
        c.flags = 2;
        Pad& w0 = L.padAA(LOOP_X + 5.5f, 1190.f, 765.f, 1484.f, zA + 0.15f, PAD_PLAZA);
        w0.flags = 2;
        Pad& w1 = L.padAA(LOOP_X + 5.5f, 1516.f, 765.f, 1735.f, zA + 0.15f, PAD_PLAZA);
        w1.flags = 2;
        L.padAA(751.f, 1010.f, 920.f, 1160.f, zA, PAD_PARKING);
        // Forecourt dressing (airport.cpp genForecourt): kerbside bollards along both drop-off curbs, a taxi rank, trolley
        // corrals, benches and planters, wayfinding pylons, a flag row in front of garage 1, sign gantries over the drives
        const float zc = zA + 0.15f;
        auto fc = [&](int variant, vec2 c, vec2 ax, float hx, float hy, float h, u32 seed) -> SiteElem& {
            SiteElem& e = L.elem(SK_FORECOURT, c, ax, hx, hy, zc, h, seed);
            e.variant = (u16)variant;
            return e;
        };
        auto fcLine = [&](int variant, vec2 a, vec2 b, float p0, u32 seed) -> SiteElem& {
            SiteElem& e = L.line(SK_FORECOURT, a, b, 1.f, zc, 1.f, seed);
            e.variant = (u16)variant;
            e.p[0] = p0;
            return e;
        };
        fcLine(0, vec2(LOOP_X + 6.2f, 1192.f), vec2(LOOP_X + 6.2f, 1482.f), 2.4f, 2100u);
        fcLine(0, vec2(LOOP_X + 6.2f, 1518.f), vec2(LOOP_X + 6.2f, 1733.f), 2.4f, 2101u);
        fcLine(0, vec2(CURB_X - 6.3f, 1164.f), vec2(CURB_X - 6.3f, 1756.f), 2.4f, 2102u);
        fc(1, vec2(755.5f, 1234.f), vec2(-1, 0), 4.f, 3.f, 3.f, 2110u);   // south of garage 1's stair tower (x 758-765, y 1245-1257)
        fc(2, vec2(761.f, 1212.f), vec2(0, 1), 3.5f, 2.f, 1.f, 2120u);
        for (int k = 0; k < 4; k++) fc(2, vec2(689.5f, 1300.f + k * 125.f), vec2(0, 1), 3.5f, 2.f, 1.f, 2121u + (u32)k);
        fc(3, vec2(757.5f, 1203.f), vec2(0, -1), 0.6f, 0.6f, 3.2f, 2130u).text = "TERMINAL|>ARRIVALS|>DEPARTURES|<GARAGE 1";
        fc(3, vec2(757.5f, 1470.f), vec2(0, -1), 0.6f, 0.6f, 3.2f, 2131u).text = "GARAGE 1|LEVELS 1-5|>SHUTTLE BUS";
        fc(3, vec2(694.5f, 1255.f), vec2(0, -1), 0.6f, 0.6f, 3.2f, 2132u).text = "ARRIVALS|BAGGAGE CLAIM|>TAXI|>BUS";
        fc(3, vec2(694.5f, 1640.f), vec2(0, 1), 0.6f, 0.6f, 3.2f, 2133u).text = "DEPARTURES|CHECK-IN|A B C";
        fcLine(4, vec2(763.2f, 1268.f), vec2(763.2f, 1296.f), 5.f, 2140u);
        for (int k = 0; k < 3; k++) fc(5, vec2(763.2f, 1325.f + k * 48.f), vec2(0, 1), 4.f, 1.f, 1.f, 2150u + (u32)k);
        for (int k = 0; k < 3; k++) fc(5, vec2(763.2f, 1590.f + k * 60.f), vec2(0, 1), 4.f, 1.f, 1.f, 2153u + (u32)k);   // north of garage 2's stair tower
        for (int k = 0; k < 5; k++) fc(5, vec2(682.6f, 1200.f + k * 128.f), vec2(0, 1), 4.f, 1.f, 1.f, 2156u + (u32)k);
        {
            SiteElem& g0 = fcLine(6, vec2(LOOP_X - 8.3f, 1300.f), vec2(LOOP_X + 7.3f, 1300.f), 10.f, 2160u);
            g0.ax = vec2(0, -1);   // facing the traffic coming up the drive
            g0.text = ">AIRPORT BOULEVARD|>PORTO SOL CITY|<RETURN TO TERMINAL";
            SiteElem& g1 = fcLine(6, vec2(CURB_X - 6.6f, 1600.f), vec2(CURB_X + 7.5f, 1600.f), 10.f, 2161u);
            g1.ax = vec2(0, 1);
            g1.text = "DEPARTURES|CHECK-IN A B C|>DROP-OFF ONLY";
        }
        SiteElem& pm = L.elem(SK_PARKING_MARKS, vec2(835.5f, 1085.f), vec2(1, 0), 84.5f, 75.f, zA, 1.f, 310);
        pm.p[0] = 5.2f;
        pm.p[1] = 7.f;
    }
    // ---------------------------------------------------------------- buildings and structures
    {
        SiteElem& t = L.elem(SK_TERMINAL, vec2((TERM_X0 + TERM_X1) * 0.5f, (TERM_Y0 + TERM_Y1) * 0.5f), vec2(0, 1), (TERM_Y1 - TERM_Y0) * 0.5f,
                             (TERM_X1 - TERM_X0) * 0.5f, zA, 30.f, 400);
        t.text = "PORTO SOL INTERNATIONAL";
        t.p[0] = 52.f;  // roof overhang toward the curb (east)
        t.p[1] = 16.f;  // overhang toward the apron (west)
        const char* letters[3] = {"A", "B", "C"};
        for (int i = 0; i < 3; i++) {
            SiteElem& c = L.line(SK_CONCOURSE, vec2(TERM_X0 + 2.f, kPierY[i]), vec2(PIER_TIP, kPierY[i]), PIER_HW + 8.f, zA, 16.f, 410 + i);
            c.p[0] = PIER_HW;
            c.text = letters[i];
        }
        L.elem(SK_CONTROL_TOWER, vec2(730.f, 1832.f), vec2(1, 0), 16.f, 16.f, zA, 94.f, 420);
        L.elem(SK_AIRPORT_SIGN, vec2(968.f, 1538.f), vec2(1, 0), 20.f, 6.f, zA, 7.f, 421).text = "PORTO SOL INTERNATIONAL";
        SiteElem& h1 = L.elem(SK_HANGAR, vec2(-710.f, 1620.f), vec2(1, 0), 45.f, 60.f, zA, 30.f, 430);
        h1.text = "PALMERA AIR TECHNICS";
        h1.p[0] = 0.35f;  // door opening fraction
        SiteElem& h2 = L.elem(SK_HANGAR, vec2(-710.f, 1760.f), vec2(1, 0), 45.f, 60.f, zA, 30.f, 431);
        h2.text = "GULFWING MRO";
        h2.p[0] = 0.f;
        L.elem(SK_FUEL_FARM, vec2(-1060.f, 1120.f), vec2(1, 0), 90.f, 80.f, zA, 18.f, 440);
        SiteElem& fs = L.elem(SK_FIRE_STATION, vec2(-1000.f, 1790.f), vec2(0, 1), 32.f, 16.f, zA, 12.f, 441);
        fs.text = "ARFF STATION 1";
        L.elem(SK_HELIPAD, vec2(652.f, 1112.f), vec2(1, 0), 13.f, 13.f, zA, 0.5f, 450);
        L.elem(SK_HELICOPTER, vec2(652.f, 1112.f), normalize(vec2(-1.f, -0.3f)), 7.f, 7.f, zA, 4.f, 451);
        // Generic buildings: garages, cargo sheds, hotel
        L.building(vec2(835.f, 1347.5f), vec2(0, 1), 122.5f, 70.f, BS_GARAGE, ROOF_FLAT, 5, vec2(-1, 0), REG_AIRPORT, zA);
        L.building(vec2(835.f, 1650.f), vec2(0, 1), 115.f, 70.f, BS_GARAGE, ROOF_FLAT, 5, vec2(-1, 0), REG_AIRPORT, zA);
        // Open-air top decks of both garages (garage roof = base + 3.5 m ground floor + 4 x 3 m decks). Each deck is reached by
        // an external two-way ramp along the east facade; its foot opens onto a driveway off the Perimeter Road (x = 940).
        const float deckZ = zA + 0.15f + 15.5f + 0.03f;
        const vec2 gc[2] = {vec2(835.f, 1347.5f), vec2(835.f, 1650.f)};
        const float ghy[2] = {122.5f, 115.f};
        const float rampX = 913.f, rampHW = 4.f, rampLen = 128.f, landLen = 26.f;  // long landing: buses and trucks swing onto the deck
        for (int k = 0; k < 2; k++) {
            L.pad(gc[k], vec2(1, 0), 69.7f, ghy[k] - 0.3f, deckZ, PAD_PARKING, packRGBA8(1.1f, 1.1f, 1.08f, 1.f), true, false);
            SiteElem& pm = L.elem(SK_PARKING_MARKS, gc[k] - vec2(1.f, 0.f), vec2(1, 0), 66.f, ghy[k] - 4.f, deckZ, 1.f, 320u + (u32)k);
            pm.p[0] = 5.2f;
            pm.p[1] = 7.f;
            // turning apron kept free of stalls, cars and light poles where the landing meets the deck (filled in below)
            pm.p[2] = 872.f;
            // the south garage's ramp climbs north from its south end, the north garage's climbs south from its north end
            float dir = k == 0 ? 1.f : -1.f;
            float yFoot = k == 0 ? gc[k].y - ghy[k] + 6.f : gc[k].y + ghy[k] - 6.f;
            float yTop = yFoot + dir * rampLen;
            pm.p[3] = Min(yTop, yTop + dir * landLen) - 8.f;
            pm.p[4] = Max(yTop, yTop + dir * landLen) + 8.f;
            Pad& rp = L.pad(vec2(rampX, (yFoot + yTop) * 0.5f), vec2(0, dir), rampLen * 0.5f, rampHW, (zA + deckZ) * 0.5f, PAD_RAMP, kW, false, false);
            rp.slope = (deckZ - zA) / rampLen;
            L.pad(vec2(910.f, yTop + dir * landLen * 0.5f), vec2(1, 0), 7.f, landLen * 0.5f, deckZ, PAD_PARKING, kW, false, false);
            float dy0 = Min(yFoot - dir * 22.f, yFoot + dir * 4.f), dy1 = Max(yFoot - dir * 22.f, yFoot + dir * 4.f);
            L.padAA(905.f, dy0, 935.f, dy1, zA, PAD_SERVICE);
            SiteElem& gr = L.elem(SK_GARAGE_RAMP, vec2(rampX, yFoot), vec2(0, dir), rampLen, rampHW, zA, deckZ - zA, 330u + (u32)k);
            gr.pts = {vec2(765.f, gc[k].y - ghy[k]), vec2(905.f, gc[k].y + ghy[k]), vec2(935.f, dy0), vec2(935.f, dy1)};
            gr.p[0] = rampX;
            gr.p[1] = yFoot;
            gr.p[2] = dir;
            gr.p[3] = rampLen;
            gr.p[4] = rampHW;
            gr.p[5] = deckZ;
            gr.p[6] = landLen;
            gr.text = k == 0 ? "GARAGE 1" : "GARAGE 2";
            L.blockAA(S.vegBlocks, 905.f, Min(yFoot, yTop + dir * landLen) - 3.f, 922.f, Max(yFoot, yTop + dir * landLen) + 3.f);
        }
        L.building(vec2(-700.f, 1125.f), vec2(0, 1), 125.f, 35.f, BS_WAREHOUSE, ROOF_FLAT, 1, vec2(1, 0), REG_AIRPORT, zA);
        L.building(vec2(-700.f, 1395.f), vec2(0, 1), 125.f, 35.f, BS_WAREHOUSE, ROOF_BARREL, 1, vec2(1, 0), REG_AIRPORT, zA);
        L.building(vec2(878.f, 1812.f), vec2(0, 1), 34.f, 15.f, BS_MIDRISE, ROOF_FLAT, 11, vec2(1, 0), REG_AIRPORT, zA);
    }
    // ---------------------------------------------------------------- aircraft at the gates
    Rng rng(0xA1B0u);
    int standNo = 0;
    for (int pi = 0; pi < 3; pi++) {
        float py = kPierY[pi];
        for (int side = -1; side <= 1; side += 2) {
            // outer sides (north of pier A, south of pier C) take wide bodies
            bool outer = (pi == 0 && side > 0) || (pi == 2 && side < 0);
            int n = outer ? 4 : 6;
            for (int g = 0; g < n; g++) {
                float gx = outer ? 525.f - g * 80.f : 535.f - g * 60.f;
                vec2 nose(gx, py + side * (PIER_HW + 6.f));
                vec2 dir(0.f, (float)-side);  // nose points at the pier
                standNo++;
                SiteElem& sm = L.elem(SK_APRON_MARKS, nose, dir, 4.f, 4.f, zA, 1.f, 1000u + standNo);
                sm.variant = 2;  // single stand lead-in line
                sm.p[0] = outer ? 64.f : 40.f;
                sm.text = StrFormat("%c%d", 'A' + pi, g * 2 + (side > 0 ? 1 : 2));
                if (!rng.chance(outer ? 0.8f : 0.72f)) continue;
                int type = outer ? (rng.chance(0.25f) ? 2 : 1) : (rng.chance(0.3f) ? 3 : 0);
                SiteElem& a = L.elem(SK_AIRLINER, nose, dir, 36.f, 36.f, zA, 18.f, rng.next());
                a.variant = (u16)type;
                a.p[0] = (float)rng.irange(0, 5);  // livery
                a.p[1] = rng.chance(0.75f) ? 1.f : 0.f;  // ground equipment
                // Jet bridge from the pier facade to the forward left door
                vec2 left = perp(dir);
                float noseToDoor = type == 0 ? 6.5f : (type == 3 ? 5.f : 9.f);
                float radius = type == 0 ? 2.0f : (type == 3 ? 1.5f : 3.1f);
                vec2 door = nose - dir * noseToDoor + left * (radius + 0.4f);
                vec2 root(door.x + 9.f + (type == 3 ? -2.f : 0.f), py + side * PIER_HW);
                SiteElem& jb = L.line(SK_GSE, root, door, 4.f, zA, 6.f, rng.next());
                jb.variant = 100;  // jet bridge
                jb.p[0] = type == 3 ? 3.2f : (type == 0 ? 4.3f : 5.4f);  // door sill height
            }
        }
    }
    // Remote stands (wide bodies, nose east) and cargo freighters (nose west at the cargo sheds)
    const float remoteY[4] = {1085.f, 1345.f, 1540.f, 1800.f};
    for (int i = 0; i < 4; i++) {
        vec2 nose(145.f, remoteY[i]);
        SiteElem& sm = L.elem(SK_APRON_MARKS, nose, vec2(1, 0), 4.f, 4.f, zA, 1.f, 1100u + i);
        sm.variant = 2;
        sm.p[0] = 64.f;
        sm.text = StrFormat("R%d", i + 1);
        if (i == 2) continue;
        SiteElem& a = L.elem(SK_AIRLINER, nose, vec2(1, 0), 36.f, 36.f, zA, 18.f, 0x5EED00u + i);
        a.variant = (u16)(i == 0 ? 2 : 1);
        a.p[0] = (float)((i * 2 + 1) % 6);
        a.p[1] = 1.f;
    }
    const float cargoY[3] = {1060.f, 1215.f, 1400.f};
    for (int i = 0; i < 3; i++) {
        vec2 nose(-640.f, cargoY[i]);
        SiteElem& a = L.elem(SK_AIRLINER, nose, vec2(-1, 0), 36.f, 36.f, zA, 18.f, 0xCA260u + i);
        a.variant = (u16)(i == 1 ? 2 : 1);
        a.p[0] = 6.f;  // cargo livery
        a.p[1] = 1.f;
        a.p[2] = 1.f;  // freighter (no windows)
    }
    {
        SiteElem& a = L.elem(SK_AIRLINER, vec2(-640.f, 1690.f), vec2(-1, 0), 36.f, 36.f, zA, 18.f, 0x3A1u);
        a.variant = 0;
        a.p[0] = 2.f;
        // narrow body in maintenance, nose out of the half-open hangar 1 door
        SiteElem& m = L.elem(SK_AIRLINER, vec2(-672.f, 1620.f), vec2(1, 0), 36.f, 36.f, zA, 18.f, 0x3A2u);
        m.variant = 0;
        m.p[0] = 0.f;
    }
    // General aviation ramp: light aircraft tied down south of the terminal
    for (int i = 0; i < 6; i++) {
        SiteElem& p = L.elem(SK_SMALL_PLANE, vec2(592.f + i * 17.f, 1000.f), vec2(0, 1), 6.f, 6.f, zA, 3.f, 0x6A00u + i);
        p.variant = (u16)(i == 4 ? 1 : 0);
        p.p[0] = (float)(i % 5);
    }
    {
        SiteElem& p = L.elem(SK_SMALL_PLANE, vec2(610.f, 1058.f), normalize(vec2(0.8f, 0.6f)), 8.f, 8.f, zA, 3.f, 0x6A10u);
        p.variant = 1;
        p.p[0] = 3.f;
    }
    // ---------------------------------------------------------------- lighting, fence, approach lights, windsocks
    const vec2 masts[] = {vec2(165.f, 985.f), vec2(165.f, 1875.f), vec2(-125.f, 985.f), vec2(-125.f, 1875.f), vec2(-400.f, 985.f),
                          vec2(-400.f, 1875.f), vec2(-650.f, 985.f), vec2(-650.f, 1875.f), vec2(195.f, 1345.f), vec2(195.f, 1535.f),
                          vec2(-130.f, 1430.f), vec2(-400.f, 1430.f), vec2(683.f, 1000.f), vec2(683.f, 1875.f)};
    for (size_t i = 0; i < ARRAY_COUNT(masts); i++) L.elem(SK_FLOOD_MAST, masts[i], vec2(1, 0), 2.f, 2.f, zA, 28.f, 500u + (u32)i);
    const float fx0 = kAirportX0, fx1 = kAirportX1, fy0 = 544.f, fy1 = kAirportY1;
    const vec2 fence[][2] = {{vec2(fx0, fy0), vec2(fx1, fy0)},        {vec2(fx1, fy0), vec2(fx1, 985.f)},
                             {vec2(fx1, 985.f), vec2(952.f, 985.f)},   {vec2(928.f, 985.f), vec2(695.f, 985.f)},
                             {vec2(695.f, 985.f), vec2(695.f, 1155.f)}, {vec2(695.f, 1765.f), vec2(695.f, 1885.f)},
                             {vec2(695.f, 1885.f), vec2(928.f, 1885.f)}, {vec2(952.f, 1885.f), vec2(fx1, 1885.f)},
                             {vec2(fx1, 1885.f), vec2(fx1, fy1)},      {vec2(fx1, fy1), vec2(fx0, fy1)},
                             {vec2(fx0, fy1), vec2(fx0, 1515.f)},      {vec2(fx0, 1485.f), vec2(fx0, fy0)}};
    for (size_t i = 0; i < ARRAY_COUNT(fence); i++) {
        vec2 a = fence[i][0], b = fence[i][1];
        // long runs are split so each piece stays within a few cells
        float len = length(b - a);
        int n = Max(1, (int)ceilf(len / 240.f));
        for (int k = 0; k < n; k++) L.line(SK_FENCE, lerp(a, b, (float)k / n), lerp(a, b, (float)(k + 1) / n), 3.f, zA, 3.2f, 600u + (u32)(i * 16 + k));
    }
    const vec2 gates[] = {vec2(940.f, 985.f), vec2(940.f, 1885.f), vec2(fx0, 1500.f)};
    for (size_t i = 0; i < ARRAY_COUNT(gates); i++) L.elem(SK_GSE, gates[i], i < 2 ? vec2(0, 1) : vec2(1, 0), 12.f, 12.f, zA, 4.f, 700u + (u32)i).variant = 200;
    struct AL { vec2 thr, out; int count; int cross; float rwW; };
    AL als[4] = {{vec2(RN_X0, RN_Y), vec2(-1, 0), 11, 9, 60.f}, {vec2(RN_X1, RN_Y), vec2(1, 0), 4, -1, 60.f},
                 {vec2(RS_X0, RS_Y), vec2(-1, 0), 10, 9, 45.f}, {vec2(RS_X1, RS_Y), vec2(1, 0), 7, -1, 45.f}};
    for (int i = 0; i < 4; i++) {
        float len = als[i].count * 30.f;
        SiteElem& e = L.line(SK_APPROACH_LIGHTS, als[i].thr, als[i].thr + als[i].out * len, 16.f, zA, 3.f, 800u + i);
        e.p[0] = (float)als[i].count;
        e.p[1] = 30.f;
        e.p[2] = (float)als[i].cross;
        e.p[3] = als[i].rwW;
    }
    L.blockAA(S.lotBlocks, RN_X0 - 360.f, RN_Y - 22.f, kAirportX0, RN_Y + 22.f);
    L.blockAA(S.lotBlocks, RS_X0 - 330.f, RS_Y - 22.f, kAirportX0, RS_Y + 22.f);
    L.blockAA(S.vegBlocks, RN_X0 - 360.f, RN_Y - 22.f, kAirportX0, RN_Y + 22.f);
    L.blockAA(S.vegBlocks, RS_X0 - 330.f, RS_Y - 22.f, kAirportX0, RS_Y + 22.f);
    const vec2 socks[] = {vec2(RN_X0 + 80.f, RN_Y + 70.f), vec2(RN_X1 - 80.f, RN_Y + 70.f), vec2(RS_X0 + 80.f, RS_Y - 65.f), vec2(RS_X1 - 80.f, RS_Y - 65.f)};
    for (size_t i = 0; i < ARRAY_COUNT(socks); i++) L.elem(SK_WINDSOCK, socks[i], vec2(1, 0), 2.f, 2.f, zA, 8.f, 900u + (u32)i);
    // Vegetation: no natural scatter on the airfield (landscaping is placed by the site generators)
    L.blockAA(S.vegBlocks, kAirportX0, kAirportY0, kAirportX1, kAirportY1);
    L.blockAA(S.lotBlocks, kAirportX0, kAirportY0, kAirportX1, kAirportY1);
    // ---------------------------------------------------------------- roads: boulevard, terminal loop, perimeter road
    S.airportBoulevardEnd = vec2(LOOP_X, BLVD_Y);
    L.road({vec2(LOOP_X, BLVD_Y), vec2(1000.f, BLVD_Y)}, RC_BOULEVARD, 0, "Airport Boulevard");
    L.road(roundedPath({vec2(LOOP_X, BLVD_Y), vec2(LOOP_X, 1745.f), vec2(CURB_X, 1745.f), vec2(CURB_X, 1470.f)}, 14.f), RC_AVENUE, RF_ONEWAY,
           "Departures Drive");
    L.road(roundedPath({vec2(CURB_X, 1470.f), vec2(CURB_X, 1175.f), vec2(LOOP_X, 1175.f), vec2(LOOP_X, BLVD_Y)}, 14.f), RC_AVENUE, RF_ONEWAY,
           "Arrivals Drive");
    L.road(roundedPath({vec2(940.f, BLVD_Y), vec2(940.f, 2300.f), vec2(-1300.f, 2300.f), vec2(-1300.f, BLVD_Y)}, 30.f), RC_STREET, 0, "Perimeter Road");
    L.road(roundedPath({vec2(940.f, BLVD_Y), vec2(940.f, 562.f), vec2(-1300.f, 562.f), vec2(-1300.f, BLVD_Y)}, 30.f), RC_STREET, 0, "Perimeter Road");
    L.road({vec2(-1300.f, BLVD_Y), vec2(-1600.f, BLVD_Y)}, RC_STREET, 0, "Airport West Gate Road");
    (void)kW;
}


// ---------------------------------------------------------------------------------------------------------------
// Port Isle container terminal
//   Island x 3960..4600, y -1048..104 at quay level 3 m. East quay: berths for a 320 m container ship and a feeder,
//   crane rails at x 4555 / 4585. Yard: two columns of straddle-carrier blocks between Port Boulevard (x 4060) and
//   Quay Road (x 4520). Gate at the bridge landing (y -160), rail yard along the north edge.
namespace pt {
constexpr float RAIL_LAND = 4555.f, RAIL_SEA = 4585.f;
constexpr float BLVD_X = 4060.f, QUAY_RD_X = 4520.f, NORTH_RD_Y = 20.f, SOUTH_RD_Y = -1025.f, GATE_Y = -160.f;
const float kCrossY[3] = {-290.f, -540.f, -790.f};
constexpr float BAY = 12.4f, ROW = 4.3f, LANE = 22.f;
}  // namespace pt

void layoutPort(Lay& L) {
    using namespace pt;
    WorldMap& map = L.map;
    SiteSet& S = L.S;
    const float zP = 3.0f;
    S.portZ = zP;
    // Quay walls: island texels at yard level, a dredged ring of water around it and a deep berth on the east side
    forTexels(map, vec2(kPortX0 - 90.f, kPortY0 - 90.f), vec2(kPortX1 + 110.f, kPortY1 + 90.f), [&](size_t i, vec2 p) {
        bool inside = p.x > kPortX0 && p.x < kPortX1 && p.y > kPortY0 && p.y < kPortY1;
        if (inside) {
            map.height[i] = zP - 0.25f;
            map.waterLevel[i] = kNoWater;
            map.region[i] = REG_PORT;
            return;
        }
        float d = Max(Max(kPortX0 - p.x, p.x - kPortX1), Max(kPortY0 - p.y, p.y - kPortY1));
        if (d > 90.f) return;
        float deep = (p.x > kPortX1 && p.y > kPortY0 - 20.f && p.y < kPortY1 + 20.f) ? -15.f : -9.f;
        float k = SmoothStep(90.f, 40.f, d);
        map.height[i] = Lerp(Min(map.height[i], -0.8f), Min(map.height[i], deep), k);
        map.waterLevel[i] = 0.f;
        map.region[i] = REG_OCEAN;
    });
    L.padAA(kPortX0, kPortY0, kPortX1, kPortY1, zP, PAD_YARD, packRGBA8(0.93f, 0.92f, 0.9f, 1.f));
    L.blockAA(S.lotBlocks, kPortX0 - 5.f, kPortY0 - 5.f, kPortX1 + 5.f, kPortY1 + 5.f);
    L.blockAA(S.vegBlocks, kPortX0 - 5.f, kPortY0 - 5.f, kPortX1 + 5.f, kPortY1 + 5.f);
    // Quay walls (east = container berths with fenders and crane rails)
    struct Q { vec2 a, b, n; int berth; };
    Q qs[4] = {{vec2(kPortX1, kPortY0), vec2(kPortX1, kPortY1), vec2(1, 0), 1},
               {vec2(kPortX0, kPortY1), vec2(kPortX0, kPortY0), vec2(-1, 0), 0},
               {vec2(kPortX1, kPortY1), vec2(kPortX0, kPortY1), vec2(0, 1), 0},
               {vec2(kPortX0, kPortY0), vec2(kPortX1, kPortY0), vec2(0, -1), 0}};
    for (int i = 0; i < 4; i++) {
        SiteElem& e = L.line(SK_QUAY, qs[i].a, qs[i].b, 40.f, zP, 16.f, 1500u + i);
        e.p[0] = qs[i].n.x;
        e.p[1] = qs[i].n.y;
        e.variant = (u16)qs[i].berth;
    }
    // Ships at the east berths
    {
        SiteElem& s = L.elem(SK_SHIP, vec2(kPortX1 + 2.f + 23.f, -800.f), vec2(0, -1), 160.f, 23.f, 0.f, 62.f, 0x5A1Au);
        s.variant = 0;
        s.text = "PALMERA HORIZON";
        SiteElem& f = L.elem(SK_SHIP, vec2(kPortX1 + 2.f + 14.f, -330.f), vec2(0, 1), 100.f, 14.f, 0.f, 42.f, 0xFEEDu);
        f.variant = 1;
        f.text = "CORAL TRADER";
    }
    // Ship-to-shore gantry cranes: y position, boom raised, trolley position (0 = over the yard .. 1 = boom tip)
    struct C { float y; float raised; float trolley; float load; };
    const C cranes[] = {{-910.f, 0, 0.72f, 1}, {-845.f, 0, 0.35f, 0}, {-780.f, 0, 0.9f, 1}, {-700.f, 1, 0.1f, 0},
                        {-380.f, 0, 0.55f, 1}, {-300.f, 0, 0.2f, 0},  {-80.f, 1, 0.1f, 0}};
    for (size_t i = 0; i < ARRAY_COUNT(cranes); i++) {
        SiteElem& e = L.elem(SK_STS_CRANE, vec2((RAIL_LAND + RAIL_SEA) * 0.5f, cranes[i].y), vec2(1, 0), 95.f, 16.f, zP, 82.f, 1600u + (u32)i);
        e.p[0] = cranes[i].raised;
        e.p[1] = cranes[i].trolley;
        e.p[2] = cranes[i].load;
        e.variant = (u16)(i % 2);
    }
    // Container yard blocks between the internal roads
    struct Sec { float top, bot; int n; };
    Sec secs[4] = {{NORTH_RD_Y - 5.6f, kCrossY[0] + 5.6f, 5}, {kCrossY[0] - 5.6f, kCrossY[1] + 5.6f, 4},
                   {kCrossY[1] - 5.6f, kCrossY[2] + 5.6f, 4}, {kCrossY[2] - 5.6f, SOUTH_RD_Y + 5.6f, 4}};
    const float colX0[2] = {4078.f, 4302.f};
    const int bays = 16, rows = 8;
    float blockW = rows * ROW;
    Rng yr(0xC0A7A1u);
    int blockNo = 0;
    std::vector<vec2> laneSpots;
    for (int s = 0; s < 4; s++) {
        float span = secs[s].top - secs[s].bot;
        float used = secs[s].n * blockW + (secs[s].n - 1) * LANE;
        float y = secs[s].top - (span - used) * 0.5f;
        for (int k = 0; k < secs[s].n; k++) {
            float cy = y - blockW * 0.5f;
            for (int c = 0; c < 2; c++) {
                float cx = colX0[c] + bays * BAY * 0.5f;
                SiteElem& e = L.elem(SK_CONTAINER_BLOCK, vec2(cx, cy), vec2(1, 0), bays * BAY * 0.5f, blockW * 0.5f, zP, 17.f, yr.next());
                e.p[0] = (float)bays;
                e.p[1] = (float)rows;
                e.p[2] = c == 0 ? 1.f : 0.f;   // no row numbers painted at column A's boulevard end (the walkway runs there)
                e.text = StrFormat("%c%d", 'A' + c, blockNo / 2 + 1);   // as on the ID boards
                float r = yr.f();
                e.variant = (u16)(r < 0.14f ? 1 : (r < 0.28f ? 2 : (r < 0.36f ? 3 : 0)));
                blockNo++;
            }
            if (k + 1 < secs[s].n) laneSpots.push_back(vec2(0.f, y - blockW - LANE * 0.5f));
            y -= blockW + LANE;
        }
    }
    // Straddle carriers in the lanes and on the quay apron
    for (int i = 0; i < 12; i++) {
        vec2 ls = laneSpots[(size_t)(i * 7) % laneSpots.size()];
        float x = i < 9 ? yr.range(4090.f, 4490.f) : RAIL_LAND - 18.f;
        float yy = i < 9 ? ls.y : -150.f - (i - 9) * 260.f;
        SiteElem& e = L.elem(SK_STRADDLE, vec2(x, yy), i < 9 ? vec2(yr.chance(0.5f) ? 1.f : -1.f, 0.f) : vec2(0, 1), 6.f, 5.f, zP, 15.f, yr.next());
        e.p[0] = yr.chance(0.6f) ? 1.f : 0.f;
    }
    // High-mast lighting: down the gap between the block columns (the one by Terminal Lane 1 stands off the street), on the
    // quay apron, and between Port Boulevard and the barrier line (clear of the junctions); a hatched safety zone round each
    std::vector<vec2> masts;
    for (int i = 0; i < 8; i++) masts.push_back(vec2(4289.f, i == 2 ? -301.f : -30.f - 130.f * i));
    for (int i = 0; i < 5; i++) masts.push_back(vec2(4537.f, -60.f - 235.f * i));
    const float blvdMastY[4] = {-250.f, -500.f, -693.f, -940.f};   // beside a block (behind the barrier line), not a lane mouth
    for (int i = 0; i < 4; i++) masts.push_back(vec2(BLVD_X + 10.5f, blvdMastY[i]));
    for (size_t i = 0; i < masts.size(); i++) {
        bool blvd = i >= 13;
        L.elem(SK_YARD_MAST, masts[i], vec2(1, 0), 3.f, 3.f, zP, blvd ? 30.f : 36.f, (u32)(i < 8 ? 1700 + i : (i < 13 ? 1720 + i - 8 : 1730 + i - 13)));
        SiteElem& hz = L.elem(SK_PORT_DRESS, masts[i], vec2(1, 0), blvd ? 1.5f : 2.f, blvd ? 1.5f : 2.f, zP, 0.1f, 0x5AFEu + (u32)i);
        hz.variant = 8;
    }
    // Working yard dressing along Port Boulevard (column A's west ends): block ID boards, a precast barrier line between the
    // sidewalk and the stacks (open at the lanes), and in the lane mouths trucks waiting on their chassis, a reach stacker
    // carrying a box, lashing cages, cones and drums, oil stains
    {
        Rng dr(0xD7E55u);
        std::vector<vec2> blockSpans;  // (top, bottom) y of each column A block
        for (int s = 0; s < 4; s++) {
            float span = secs[s].top - secs[s].bot;
            float used = secs[s].n * blockW + (secs[s].n - 1) * LANE;
            float y = secs[s].top - (span - used) * 0.5f;
            for (int k = 0; k < secs[s].n; k++) {
                blockSpans.push_back(vec2(y, y - blockW));
                y -= blockW + LANE;
            }
        }
        const float xw = colX0[0] - 4.5f;   // barrier line, between the sidewalk and the block ends
        for (size_t bi = 0; bi < blockSpans.size(); bi++) {
            vec2 sp = blockSpans[bi];
            SiteElem& jb = L.line(SK_PORT_DRESS, vec2(xw, sp.x - 0.6f), vec2(xw, sp.y + 0.6f), 1.f, zP, 1.f, 0xB0B0u + (u32)bi);
            jb.variant = 0;
            // ID boards at both corners facing the boulevard ("A" column, blocks numbered from the north), just behind the
            // barriers so the walkway runs clear
            for (int c = 0; c < 2; c++) {
                SiteElem& id = L.elem(SK_PORT_DRESS, vec2(xw + 0.45f, c ? sp.y + 2.f : sp.x - 2.f), vec2(-1, 0), 1.f, 1.f, zP, 5.f, 0xB1D0u + (u32)bi * 2u + (u32)c);
                id.variant = 5;
                id.text = StrFormat("A%d%c", (int)bi + 1, c ? 'S' : 'N');
            }
            if (bi + 1 >= blockSpans.size()) continue;
            // lane mouth south of this block
            float ly = (sp.y + blockSpans[bi + 1].x) * 0.5f;
            bool road = false;
            for (float cy : kCrossY) road = road || fabsf(cy - ly) < 12.f;
            if (road) continue;   // the terminal lanes are streets: keep them clear
            SiteElem& st = L.elem(SK_PORT_DRESS, vec2(colX0[0] + 24.f, ly), vec2(1, 0), 22.f, 8.f, zP, 1.f, 0x5A1Du + (u32)bi);
            st.variant = 3;
            st.p[0] = 14.f;
            auto clearOfStraddles = [&](vec2 p, float r) {
                for (const SiteElem& o : S.elems)
                    if (o.kind == SK_STRADDLE && length(o.c - p) < r) return false;
                return true;
            };
            if (dr.chance(0.7f) && clearOfStraddles(vec2(colX0[0] + 22.f, ly), 20.f)) {
                SiteElem& tk = L.elem(SK_PORT_TRUCK, vec2(colX0[0] + 22.f, ly + dr.range(3.5f, 5.5f)), vec2(-1, 0), 9.f, 2.f, zP, 4.f, 0x7C0Du + (u32)bi);
                tk.variant = (u16)(dr.chance(0.2f) ? 2 : (dr.chance(0.3f) ? 1 : 0));
                tk.p[0] = (float)dr.irange(0, 5);
                tk.p[1] = dr.chance(0.25f) ? -1.f : (float)dr.irange(0, 9);
            }
            float rsx = colX0[0] + dr.range(52.f, 90.f);
            if (dr.chance(0.45f) && clearOfStraddles(vec2(rsx, ly), 22.f)) {
                SiteElem& rs = L.elem(SK_REACH_STACKER, vec2(rsx, ly - 2.5f), vec2(dr.chance(0.5f) ? -1.f : 1.f, 0), 9.f, 6.5f, zP, 10.f,
                                      0x5EAC4u + (u32)bi);
                rs.p[0] = dr.range(4.f, 9.f);
                rs.p[1] = dr.chance(0.3f) ? -1.f : (float)dr.irange(0, 9);
            }
            if (dr.chance(0.5f)) {
                // lashing cages set down at the lane side, past the crossing and the block's painted ID
                SiteElem& lc = L.elem(SK_PORT_DRESS, vec2(colX0[0] + 11.f, sp.y - 3.f), vec2(1, 0), 4.f, 2.f, zP, 2.4f, 0xCA6Eu + (u32)bi);
                lc.variant = 1;
            }
            if (dr.chance(0.35f)) {
                SiteElem& cn = L.elem(SK_PORT_DRESS, vec2(colX0[0] + 6.f, ly - 7.5f), vec2(1, 0), 5.f, 1.f, zP, 1.f, 0xC0DEu + (u32)bi);
                cn.variant = 4;
            }
        }
        // terminal rules at the boulevard (north of the gate road and by the first lanes)
        const char* rules[] = {"SPEED LIMIT|15", "HARD HAT AND HI-VIS|BEYOND THIS POINT", "STRADDLE CARRIERS|HAVE RIGHT OF WAY", "NO PEDESTRIANS|IN STACK LANES"};
        const float ruleY[4] = {-186.f, -304.f, -420.f, -560.f};   // clear of the terminal lane streets
        for (int k = 0; k < 4; k++) {
            SiteElem& sg = L.elem(SK_PORT_DRESS, vec2(xw + 0.45f, ruleY[k]), vec2(-1, 0), 1.6f, 1.f, zP, 1.2f, 0x5160u + (u32)k);
            sg.variant = 2;
            sg.hx = k == 0 ? 0.9f : 1.6f;
            sg.hy = k == 0 ? 1.1f : 0.9f;
            sg.p[0] = k == 0 ? 0.f : 1.f;
            sg.text = rules[k];
        }
        // Floor paint: stack lanes between the blocks across both columns (edge lines broken over the gap between the
        // columns), that gap's own lane beside each block pair, and the walkway behind the barrier line (it crosses the
        // lane mouths on the lanes' zebras)
        const float colEnd0 = colX0[0] + bays * BAY, colEnd1 = colX0[1] + bays * BAY, gapC = (colEnd0 + colX0[1]) * 0.5f;
        for (size_t bi = 0; bi < blockSpans.size(); bi++) {
            vec2 sp = blockSpans[bi];
            SiteElem& gl = L.line(SK_PORT_DRESS, vec2(gapC, sp.x), vec2(gapC, sp.y), (colX0[1] - colEnd0) * 0.5f, zP, 0.1f, 0x6A9Au + (u32)bi);
            gl.variant = 7;
            SiteElem& wk = L.elem(SK_PORT_DRESS, vec2(xw + 0.6f + 1.45f, (sp.x + sp.y) * 0.5f), vec2(0, 1), (sp.x - sp.y) * 0.5f, 1.45f, zP, 0.1f, 0x3A1Cu + (u32)bi);
            wk.variant = 8;
            wk.p[0] = 1.f;
            wk.p[1] = -1.f;   // perp(+y) is -x: the stacks lie on the -perp side
            if (bi + 1 >= blockSpans.size()) continue;
            float ly = (sp.y + blockSpans[bi + 1].x) * 0.5f;
            bool road = false;
            for (float cy : kCrossY) road = road || fabsf(cy - ly) < 12.f;
            if (road) continue;
            SiteElem& ln = L.line(SK_PORT_DRESS, vec2(colX0[0], ly), vec2(colEnd1, ly), LANE * 0.5f, zP, 0.1f, 0x1A4Eu + (u32)bi);
            ln.variant = 7;
            ln.p[0] = 1.f;
            ln.p[1] = colEnd0 - colX0[0];
            ln.p[2] = colX0[1] - colX0[0];
        }
    }
    // The slab floor everywhere on the island: the four yard sections inside Port Boulevard, the strips outside it and the
    // quay apron (joints and repairs keep off the roads and the quay copings)
    {
        const float e0 = kPortX0 + 0.9f, e1 = kPortX1 - 0.9f, f0 = kPortY0 + 0.9f, f1 = kPortY1 - 0.9f;
        const vec4 areas[] = {vec4(BLVD_X, kCrossY[0], QUAY_RD_X, NORTH_RD_Y),     vec4(BLVD_X, kCrossY[1], QUAY_RD_X, kCrossY[0]),
                              vec4(BLVD_X, kCrossY[2], QUAY_RD_X, kCrossY[1]),     vec4(BLVD_X, SOUTH_RD_Y, QUAY_RD_X, kCrossY[2]),
                              vec4(e0, f0, BLVD_X, f1),                             vec4(QUAY_RD_X, f0, e1, f1),
                              vec4(BLVD_X, NORTH_RD_Y, QUAY_RD_X, f1),              vec4(BLVD_X, f0, QUAY_RD_X, SOUTH_RD_Y)};
        for (size_t i = 0; i < ARRAY_COUNT(areas); i++) {
            const vec4& a = areas[i];
            SiteElem& sf = L.elem(SK_PORT_DRESS, vec2((a.x + a.z) * 0.5f, (a.y + a.w) * 0.5f), vec2(1, 0), (a.z - a.x) * 0.5f, (a.w - a.y) * 0.5f, zP, 0.1f,
                                  0x51ABu + (u32)i);
            sf.variant = 6;
        }
    }
    // Gate complex at the bridge landing, admin office, freight stations
    {
        SiteElem& g = L.elem(SK_PORT_GATE, vec2(4008.f, GATE_Y), vec2(1, 0), 30.f, 17.f, zP, 9.f, 1800);
        g.text = "PORT ISLE TERMINAL";
        // perimeter fence from the gate north and south (gaps where roads pass), gate rules facing the arriving trucks,
        // and a truck holding area south of the gate: rigs waiting on their chassis between precast barriers
        L.line(SK_FENCE, vec2(4042.f, GATE_Y - 19.f), vec2(4042.f, -262.f), 3.f, zP, 2.6f, 1810u);
        L.line(SK_FENCE, vec2(4042.f, GATE_Y + 19.f), vec2(4042.f, -48.f), 3.f, zP, 2.6f, 1811u);
        const char* gateRules[] = {"ALL VEHICLES|STOP AT GATE", "PRESENT ID AND|BOOKING NUMBER", "TRUCK HOLDING AREA|WAIT FOR GATE CALL"};
        const vec2 gatePos[] = {vec2(3968.f, GATE_Y - 20.f), vec2(3968.f, GATE_Y + 20.f), vec2(3981.f, -201.f)};
        const vec2 gateFace[] = {vec2(-1, 0), vec2(-1, 0), vec2(0, 1)};
        for (int k = 0; k < 3; k++) {
            SiteElem& sg = L.elem(SK_PORT_DRESS, gatePos[k], gateFace[k], 1.8f, 1.f, zP, 1.3f, 0x6A7Eu + (u32)k);
            sg.variant = 2;
            sg.p[0] = k == 2 ? 2.f : 0.f;
            sg.text = gateRules[k];
        }
        Rng hr(0x401Du);
        for (int k = 0; k < 5; k++) {
            if (k == 3) continue;   // an empty bay
            SiteElem& tk = L.elem(SK_PORT_TRUCK, vec2(3993.f + k * 5.2f, -226.f + hr.range(-1.f, 1.f)), vec2(0, 1), 9.f, 2.f, zP, 4.f, 0x4E1Du + (u32)k);
            tk.variant = (u16)(k == 1 ? 2 : (k == 4 ? 1 : 0));
            tk.p[0] = (float)hr.irange(0, 5);
            tk.p[1] = hr.chance(0.3f) ? -1.f : (float)hr.irange(0, 9);
        }
        SiteElem& hb0 = L.line(SK_PORT_DRESS, vec2(3985.f, -203.f), vec2(3985.f, -251.f), 1.f, zP, 1.f, 0x4E20u);
        hb0.variant = 0;
        SiteElem& hb1 = L.line(SK_PORT_DRESS, vec2(3987.f, -253.f), vec2(4034.f, -253.f), 1.f, zP, 1.f, 0x4E21u);
        hb1.variant = 0;
        SiteElem& hst = L.elem(SK_PORT_DRESS, vec2(4008.f, -226.f), vec2(1, 0), 22.f, 12.f, zP, 1.f, 0x4E22u);
        hst.variant = 3;
        hst.p[0] = 22.f;
        // the bays painted under the waiting rigs: dividers between them, a stop line ahead of the cabs
        SiteElem& hby = L.line(SK_PORT_DRESS, vec2(3993.f - 2.6f, -214.5f), vec2(3993.f + 4.f * 5.2f + 2.6f, -214.5f), 1.f, zP, 0.1f, 0x4E24u);
        hby.variant = 9;
        hby.p[0] = 5.f;
        hby.p[1] = 21.f;
        hby.text = "WAIT FOR GATE CALL";
        SiteElem& gst = L.elem(SK_PORT_DRESS, vec2(4008.f, GATE_Y), vec2(1, 0), 30.f, 8.f, zP, 1.f, 0x4E23u);
        gst.variant = 3;
        gst.p[0] = 16.f;
        L.building(vec2(4010.f, -80.f), vec2(0, 1), 25.f, 17.f, BS_MIDRISE, ROOF_FLAT, 5, vec2(1, 0), REG_PORT, zP);
        L.building(vec2(4004.f, -820.f), vec2(0, 1), 178.f, 30.f, BS_WAREHOUSE, ROOF_FLAT, 1, vec2(1, 0), REG_PORT, zP);
        L.building(vec2(4004.f, -432.f), vec2(0, 1), 168.f, 30.f, BS_WAREHOUSE, ROOF_BARREL, 1, vec2(1, 0), REG_PORT, zP);
    }
    // Rail: intermodal yard along the north edge, lead track down the west side onto a trestle to the mainland
    {
        SiteElem& ry = L.elem(SK_RAIL_YARD, vec2(4290.f, 60.f), vec2(1, 0), 200.f, 26.f, zP, 6.f, 1900);
        ry.p[0] = 3.f;
        L.elem(SK_RMG_CRANE, vec2(4230.f, 60.f), vec2(0, 1), 32.f, 14.f, zP, 26.f, 1901);
        float shoreX = kPortX0 - 20.f;
        for (float x = kPortX0 - 20.f; x > 3650.f; x -= 4.f)
            if (!map.isWater(x, -135.f)) { shoreX = x; break; }
        std::vector<vec2> lead = roundedPath({vec2(4096.f, 60.f), vec2(3972.f, 60.f), vec2(3972.f, -135.f), vec2(shoreX - 25.f, -135.f)}, 28.f, 6.f);
        lead = resample(lead, 6.f);
        // split into pieces of ~150 m; p[0] = 1 when the piece is a trestle over water
        size_t start = 0;
        float acc = 0.f;
        for (size_t i = 1; i < lead.size(); i++) {
            acc += length(lead[i] - lead[i - 1]);
            if (acc > 150.f || i + 1 == lead.size()) {
                std::vector<vec2> piece(lead.begin() + (long)start, lead.begin() + (long)i + 1);
                SiteElem& e = L.line(SK_RAIL, piece.front(), piece.back(), 8.f, zP, 2.f, 1910u + (u32)start);
                vec2 mn(1e9f), mx(-1e9f);
                for (auto& q : piece) { mn = vmin(mn, q); mx = vmax(mx, q); }
                e.c = (mn + mx) * 0.5f;
                e.hx = (mx.x - mn.x) * 0.5f + 4.f;
                e.hy = (mx.y - mn.y) * 0.5f + 4.f;
                e.ax = vec2(1, 0);
                e.pts = piece;
                e.p[0] = map.isWater(piece[piece.size() / 2].x, piece[piece.size() / 2].y) ? 1.f : 0.f;
                start = i;
                acc = 0.f;
            }
        }
        SiteElem& bs = L.elem(SK_RAIL, vec2(shoreX - 25.f, -135.f), vec2(-1, 0), 3.f, 3.f, map.heightAt(shoreX - 25.f, -135.f), 2.f, 1999);
        bs.variant = 1;  // buffer stop
        L.blockAA(S.lotBlocks, shoreX - 40.f, -150.f, shoreX + 5.f, -120.f);
    }
    // Roads: boulevard loop around the terminal (starts/ends at the bridge landing) and three cross lanes
    L.road(roundedPath({vec2(BLVD_X, GATE_Y), vec2(BLVD_X, NORTH_RD_Y), vec2(QUAY_RD_X, NORTH_RD_Y), vec2(QUAY_RD_X, SOUTH_RD_Y),
                        vec2(BLVD_X, SOUTH_RD_Y), vec2(BLVD_X, GATE_Y - 0.5f)},
                       26.f),
           RC_AVENUE, 0, "Port Boulevard");
    for (int i = 0; i < 3; i++)
        L.road({vec2(BLVD_X, kCrossY[i]), vec2(QUAY_RD_X, kCrossY[i])}, RC_STREET, 0, StrFormat("Terminal Lane %d", i + 1).c_str());
}

// ---------------------------------------------------------------------------------------------------------------
// Key Coral: winding island streets (villas come from the lot system), yacht club, golf links, beach clubs
struct IslandRow { float y, xl, xr; };

bool scanIsland(const WorldMap& map, Region reg, float yTop, float yBot, float xMin, float xMax, std::vector<IslandRow>& rows) {
    rows.clear();
    for (float y = yTop; y >= yBot; y -= 10.f) {
        float bestL = 0, bestR = 0, runL = 0;
        bool in = false;
        for (float x = xMin; x <= xMax; x += 4.f) {
            bool land = map.regionAt(x, y) == reg && !map.isWater(x, y);
            if (land && !in) { in = true; runL = x; }
            if ((!land || x + 4.f > xMax) && in) {
                in = false;
                if (x - runL > bestR - bestL) { bestL = runL; bestR = x; }
            }
        }
        if (bestR - bestL > 60.f) rows.push_back({y, bestL, bestR});
    }
    return rows.size() > 10;
}

struct IslandFrame {
    std::vector<IslandRow> rows;
    float yN = 0, yS = 0;
    // (u,v) in [0,1]^2 -> world, u across (west->east), v along (north->south)
    vec2 at(float u, float v) const {
        float y = Lerp(yN, yS, Clamp(v, 0.f, 1.f));
        float fi = (yN - y) / 10.f;
        int i0 = Clamp((int)floorf(fi), 0, (int)rows.size() - 1), i1 = Min(i0 + 1, (int)rows.size() - 1);
        float t = Clamp(fi - i0, 0.f, 1.f);
        float xl = Lerp(rows[i0].xl, rows[i1].xl, t), xr = Lerp(rows[i0].xr, rows[i1].xr, t);
        return vec2(Lerp(xl, xr, u), y);
    }
    std::vector<vec2> curve(std::initializer_list<vec2> uv, float step = 12.f) const {
        std::vector<vec2> out;
        std::vector<vec2> ctrl(uv);
        for (size_t i = 0; i + 1 < ctrl.size(); i++) {
            int n = Max(2, (int)(length(at(ctrl[i + 1].x, ctrl[i + 1].y) - at(ctrl[i].x, ctrl[i].y)) / step));
            for (int k = 0; k < n; k++) {
                vec2 q = lerp(ctrl[i], ctrl[i + 1], (float)k / n);
                out.push_back(at(q.x, q.y));
            }
        }
        out.push_back(at(ctrl.back().x, ctrl.back().y));
        return out;
    }
};

// Chaikin smoothing (keeps the end points)
std::vector<vec2> chaikin(const std::vector<vec2>& p, int iters) {
    std::vector<vec2> cur = p;
    for (int it = 0; it < iters; it++) {
        if (cur.size() < 3) return cur;
        std::vector<vec2> nx;
        nx.push_back(cur.front());
        for (size_t i = 0; i + 1 < cur.size(); i++) {
            nx.push_back(lerp(cur[i], cur[i + 1], 0.25f));
            nx.push_back(lerp(cur[i], cur[i + 1], 0.75f));
        }
        nx.push_back(cur.back());
        cur.swap(nx);
    }
    return cur;
}

void layoutKeyCoral(Lay& L) {
    WorldMap& map = L.map;
    SiteSet& S = L.S;
    IslandFrame F;
    if (!scanIsland(map, REG_KEY_CORAL, -2800.f, -5400.f, 3800.f, 5200.f, F.rows)) return;
    F.yN = F.rows.front().y;
    F.yS = F.rows.back().y;
    // Rows are contiguous by construction only if no gap: rebuild a dense table by interpolation
    {
        std::vector<IslandRow> dense;
        for (float y = F.yN; y >= F.yS; y -= 10.f) {
            size_t best = 0;
            for (size_t i = 0; i < F.rows.size(); i++)
                if (fabsf(F.rows[i].y - y) < fabsf(F.rows[best].y - y)) best = i;
            dense.push_back({y, F.rows[best].xl, F.rows[best].xr});
        }
        // smooth the outline a little so the streets don't copy every notch of the coast
        for (int it = 0; it < 6; it++)
            for (size_t i = 1; i + 1 < dense.size(); i++) {
                dense[i].xl = (dense[i - 1].xl + dense[i].xl * 2.f + dense[i + 1].xl) * 0.25f;
                dense[i].xr = (dense[i - 1].xr + dense[i].xr * 2.f + dense[i + 1].xr) * 0.25f;
            }
        F.rows.swap(dense);
    }
    // Streets in island coordinates (u across, v along). Inner streets stay inside the 110 m beach band.
    auto road = [&](std::vector<vec2> pts, RoadClass cls, const char* name) { L.road(chaikin(pts, 2), cls, 0, name); };
    vec2 causeway(4400.f, -3350.f);
    // Spine: winding avenue from the causeway landing to the golf clubhouse
    {
        std::vector<vec2> spine;
        spine.push_back(causeway);
        for (float v = 0.30f; v <= 0.745f; v += 0.015f) {
            float u = 0.5f + 0.09f * sinf(v * 23.f) + 0.03f * sinf(v * 61.f);
            spine.push_back(F.at(u, v));
        }
        // bend the first stretch smoothly from the causeway toward the spine
        std::vector<vec2> sp;
        sp.push_back(spine[0]);
        vec2 first = spine[1];
        for (int k = 1; k < 6; k++) sp.push_back(lerp(spine[0], first, k / 6.f) + perp(normalize(first - spine[0])) * (sinf(k / 6.f * kPi) * 25.f));
        sp.insert(sp.end(), spine.begin() + 1, spine.end());
        road(sp, RC_AVENUE, "Coral Key Drive");
    }
    // North loop around Tarpon Point (the northern tip)
    road(F.curve({vec2(0.5f, 0.30f), vec2(0.3f, 0.22f), vec2(0.26f, 0.12f), vec2(0.42f, 0.06f), vec2(0.66f, 0.07f), vec2(0.76f, 0.16f),
                  vec2(0.74f, 0.26f), vec2(0.56f, 0.33f)}),
         RC_STREET, "Tarpon Point Road");
    // East and west drives
    road(F.curve({vec2(0.56f, 0.33f), vec2(0.74f, 0.38f), vec2(0.76f, 0.5f), vec2(0.75f, 0.6f), vec2(0.66f, 0.7f), vec2(0.55f, 0.72f)}), RC_STREET,
         "Ocean Drive");
    road(F.curve({vec2(0.45f, 0.36f), vec2(0.26f, 0.42f), vec2(0.25f, 0.55f), vec2(0.28f, 0.64f), vec2(0.45f, 0.70f)}), RC_STREET, "Bayview Drive");
    // Residential lanes (some end in cul-de-sacs)
    const float laneV[] = {0.15f, 0.40f, 0.47f, 0.53f, 0.59f, 0.65f};
    for (size_t i = 0; i < ARRAY_COUNT(laneV); i++) {
        float v = laneV[i];
        if (i == 0) {
            road(F.curve({vec2(0.3f, v), vec2(0.72f, v + 0.01f)}), RC_LANE, "Sea Grape Lane");
            continue;
        }
        bool westSide = (i & 1) != 0;
        if (westSide) road(F.curve({vec2(0.26f, v), vec2(0.5f, v + 0.004f)}), RC_LANE, StrFormat("%s Court", i < 3 ? "Palm Cove" : "Heron Bay").c_str());
        else road(F.curve({vec2(0.5f, v), vec2(0.755f, v - 0.004f)}), RC_LANE, StrFormat("%s Lane", i < 4 ? "Coquina" : "Sandpiper").c_str());
    }
    // Marina (yacht club) on the bay side
    {
        vec2 inland = F.at(0.26f, 0.5f);
        vec2 shoreDir(-1, 0);
        float d = marchToWater(map, inland, shoreDir, 400.f, 3.f);
        if (d > 0.f) {
            vec2 shore = inland + shoreDir * d;
            road({inland, inland + shoreDir * Max(10.f, d - 45.f)}, RC_STREET, "Yacht Club Way");
            SiteElem& m = L.elem(SK_MARINA, shore, shoreDir, 150.f, 90.f, 0.f, 12.f, 0x3A21A);
            m.p[0] = 150.f;   // main pier length
            m.p[1] = 2.2f;    // deck height above water
            m.p[2] = 1.f;     // yachts (1) or mixed (0)
            m.text = "KEY CORAL YACHT CLUB";
            vec2 club = inland + shoreDir * Max(10.f, d - 25.f) + vec2(0, 42.f);
            L.building(club, vec2(0, 1), 18.f, 12.f, BS_VILLA, ROOF_HIP, 2, vec2(0, -1), REG_KEY_CORAL);
            L.block(S.lotBlocks, shore + vec2(40.f, 0), vec2(1, 0), 75.f, 80.f);
            L.block(S.vegBlocks, shore + vec2(35.f, 0), vec2(1, 0), 60.f, 70.f);
        }
    }
    // Beach clubs on the ocean side
    for (int i = 0; i < 2; i++) {
        vec2 inland = F.at(0.72f, i == 0 ? 0.42f : 0.57f);
        float d = marchToWater(map, inland, vec2(1, 0), 500.f, 3.f);
        if (d < 0.f) continue;
        vec2 shore = inland + vec2(d, 0);
        vec2 c = shore - vec2(58.f, 0);
        SiteElem& b = L.elem(SK_BEACH_CLUB, c, vec2(1, 0), 45.f, 38.f, map.heightAt(c.x, c.y), 8.f, 0xBEAC4u + i);
        b.text = i == 0 ? "SOLACE BEACH CLUB" : "CORAL CABANA CLUB";
        b.variant = (u16)i;
        L.block(S.lotBlocks, c, vec2(1, 0), 60.f, 45.f);
        L.block(S.vegBlocks, c, vec2(1, 0), 50.f, 40.f);
    }
    // Golf links on the southern third: holes as tee -> green pairs in island coordinates
    {
        struct H { vec2 tee, green; };
        H holes[] = {{vec2(0.40f, 0.765f), vec2(0.30f, 0.86f)}, {vec2(0.28f, 0.885f), vec2(0.45f, 0.965f)},
                     {vec2(0.55f, 0.955f), vec2(0.72f, 0.86f)}, {vec2(0.72f, 0.835f), vec2(0.62f, 0.765f)},
                     {vec2(0.50f, 0.80f), vec2(0.52f, 0.90f)}};
        for (size_t i = 0; i < ARRAY_COUNT(holes); i++) {
            vec2 a = F.at(holes[i].tee.x, holes[i].tee.y), b = F.at(holes[i].green.x, holes[i].green.y);
            SiteElem& e = L.line(SK_GOLF_HOLE, a, b, 30.f, 0.f, 1.f, 0x601Fu + (u32)i);
            e.p[0] = i == 4 ? 16.f : 22.f;  // fairway half width
            e.variant = (u16)i;
        }
        vec2 pond = F.at(0.47f, 0.875f);
        SiteElem& p = L.elem(SK_GOLF_POND, pond, vec2(1, 0.3f), 34.f, 22.f, 0.f, 1.f, 0x9000u);
        carveBasin(map, pond, normalize(vec2(1, 0.3f)), 30.f, 18.f, 1.2f);
        (void)p;
        vec2 club = F.at(0.5f, 0.745f) + vec2(0, -18.f);
        L.building(club, vec2(1, 0), 20.f, 13.f, BS_VILLA, ROOF_HIP, 2, vec2(0, 1), REG_KEY_CORAL);
        vec2 g0 = F.at(0.5f, 0.74f), g1 = F.at(0.5f, 1.0f);
        float w = (F.at(1.f, 0.87f).x - F.at(0.f, 0.87f).x) * 0.5f;
        L.block(S.lotBlocks, (g0 + g1) * 0.5f + vec2(0, -10.f), vec2(1, 0), w + 60.f, (g0.y - g1.y) * 0.5f + 10.f);
        L.block(S.vegBlocks, (g0 + g1) * 0.5f + vec2(0, -14.f), vec2(1, 0), w - 60.f, (g0.y - g1.y) * 0.5f - 14.f);
    }
    // Moored boats along the bay side of the northern tip (private docks)
    for (int i = 0; i < 5; i++) {
        vec2 inland = F.at(0.3f, 0.2f + i * 0.04f);
        float d = marchToWater(map, inland, vec2(-1, 0), 300.f, 3.f);
        if (d < 0.f) continue;
        vec2 shore = inland + vec2(-d, 0);
        SiteElem& dk = L.elem(SK_DOCK, shore, vec2(-1, 0), 18.f, 8.f, 0.f, 3.f, 0xD0C0u + i);
        dk.p[0] = 16.f;  // length
        dk.p[1] = 1.f;   // boat moored
        dk.variant = 1;  // private dock with a motor yacht
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Signature landmarks and countryside details
void layoutLandmarks(Lay& L) {
    WorldMap& map = L.map;
    SiteSet& S = L.S;
    // --- Solaris One: supertall on a financial district block
    {
        vec2 c(3350.f, -750.f);
        float z = meanHeight(map, c - vec2(40.f), c + vec2(40.f), 8.f);
        SiteElem& e = L.elem(SK_SOLARIS, c, vec2(1, 0), 34.f, 34.f, z, 606.f, 0x501A415u);
        e.text = "SOLARIS ONE";
        L.blockAA(S.lotBlocks, c.x - 46.f, c.y - 46.f, c.x + 46.f, c.y + 46.f);
        L.blockAA(S.vegBlocks, c.x - 44.f, c.y - 44.f, c.x + 44.f, c.y + 44.f);
        L.blockAA(S.roadBlocks, c.x - 40.f, c.y - 40.f, c.x + 40.f, c.y + 40.f);  // no grid street through the tower
        L.padAA(c.x - 41.f, c.y - 41.f, c.x + 41.f, c.y + 41.f, z + 0.35f, PAD_PLAZA).flags = 2;
        map.flattenRect(c, vec2(1, 0), 40.f, 40.f, z + 0.1f, 6.f);
    }
    // --- Tidewater Stadium on the downtown bayfront
    {
        float x0 = 3440.f, x1 = 3690.f, y0 = 300.f, y1 = 580.f;
        vec2 c((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
        float z = meanHeight(map, vec2(x0, y0), vec2(x1, y1), 10.f) + 0.2f;
        map.flattenRect(c, vec2(1, 0), (x1 - x0) * 0.5f, (y1 - y0) * 0.5f, z - 0.25f, 30.f);
        L.blockAA(S.roadBlocks, x0, y0, x1, y1);
        L.blockAA(S.lotBlocks, x0 - 4.f, y0 - 4.f, x1 + 4.f, y1 + 4.f);
        L.blockAA(S.vegBlocks, x0, y0, x1, y1);
        L.padAA(x0 + 6.f, y0 + 6.f, x1 - 6.f, y1 - 6.f, z, PAD_PLAZA);
        SiteElem& e = L.elem(SK_STADIUM, c + vec2(0, 5.f), vec2(1, 0), 112.f, 94.f, z, 50.f, 0x57AD1u);
        e.text = "TIDEWATER STADIUM";
    }
    // --- City Hall plaza (downtown, facing the river)
    {
        float x0 = 2900.f, x1 = 3100.f, y0 = 230.f, y1 = 430.f;
        vec2 c((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
        float z = meanHeight(map, vec2(x0, y0), vec2(x1, y1), 10.f) + 0.2f;
        map.flattenRect(c, vec2(1, 0), (x1 - x0) * 0.5f, (y1 - y0) * 0.5f, z - 0.25f, 25.f);
        L.blockAA(S.roadBlocks, x0, y0, x1, y1);
        L.blockAA(S.lotBlocks, x0 - 4.f, y0 - 4.f, x1 + 4.f, y1 + 4.f);
        L.blockAA(S.vegBlocks, x0, y0, x1, y1);
        L.padAA(x0 + 5.f, y0 + 5.f, x1 - 5.f, y1 - 5.f, z, PAD_PLAZA);
        SiteElem& e = L.elem(SK_CITY_HALL, vec2(c.x, 372.f), vec2(0, -1), 64.f, 40.f, z, 80.f, 0xC17AAu);
        e.text = "CITY OF PORTO SOL";
        SiteElem& f = L.elem(SK_FOUNTAIN, vec2(c.x, 262.f), vec2(1, 0), 13.f, 13.f, z, 6.f, 0xF0F1u);
        f.p[0] = 11.f;  // basin radius
        f.variant = 1;
    }
    // --- Canvas Park with the Midtown fountain
    {
        float x0 = 2950.f, x1 = 3250.f, y0 = 1650.f, y1 = 1950.f;
        vec2 c((x0 + x1) * 0.5f, (y0 + y1) * 0.5f);
        float z = meanHeight(map, vec2(x0, y0), vec2(x1, y1), 10.f);
        map.flattenRect(c, vec2(1, 0), (x1 - x0) * 0.5f, (y1 - y0) * 0.5f, z, 30.f);
        L.blockAA(S.roadBlocks, x0, y0, x1, y1);
        L.blockAA(S.lotBlocks, x0 - 4.f, y0 - 4.f, x1 + 4.f, y1 + 4.f);
        L.blockAA(S.vegBlocks, x0, y0, x1, y1);
        SiteElem& e = L.elem(SK_PARK, c, vec2(1, 0), (x1 - x0) * 0.5f, (y1 - y0) * 0.5f, z, 10.f, 0xCA7Au);
        e.text = "CANVAS PARK";
        SiteElem& f = L.elem(SK_FOUNTAIN, c, vec2(1, 0), 24.f, 24.f, z + 0.1f, 14.f, 0xF0F2u);
        f.p[0] = 21.f;
        f.variant = 0;
        L.pad(c, vec2(1, 0), 27.f, 27.f, z + 0.1f, PAD_PLAZA, 0xffffffffu, false, false).flags = 1;
    }
    // --- Sol Beach Pier with the amusement platform
    {
        float py = 1250.f;
        float shoreX = 5480.f;
        for (float x = 5000.f; x < 5900.f; x += 2.f)
            if (map.isWater(x, py)) { shoreX = x; break; }
        float deckZ = 5.6f;
        float x0 = shoreX - 26.f, x1 = shoreX + 400.f;
        SiteElem& p = L.line(SK_BEACH_PIER, vec2(x0, py), vec2(x1, py), 40.f, deckZ, 10.f, 0x51E8u);
        p.text = "SOL BEACH PIER";
        p.p[0] = 8.f;              // walkway half width
        p.p[1] = shoreX + 30.f;    // platform start
        p.p[2] = shoreX + 172.f;   // platform end
        p.p[3] = 35.f;             // platform half width
        // Deck pads (walkable / drivable) and the access ramp from the promenade
        L.pad(vec2((x0 + x1) * 0.5f, py), vec2(1, 0), (x1 - x0) * 0.5f, 8.f, deckZ, PAD_DECK, 0xffffffffu, false, false);
        L.pad(vec2((p.p[1] + p.p[2]) * 0.5f, py), vec2(1, 0), (p.p[2] - p.p[1]) * 0.5f, 35.f, deckZ, PAD_DECK, 0xffffffffu, false, false);
        float rampLen = 46.f;
        float zStart = map.heightAt(x0 - rampLen, py) + 0.1f;
        Pad& ramp = L.pad(vec2(x0 - rampLen * 0.5f, py), vec2(1, 0), rampLen * 0.5f, 6.f, (zStart + deckZ) * 0.5f, PAD_RAMP, 0xffffffffu, false, false);
        ramp.slope = (deckZ - zStart) / rampLen;
        SiteElem& r = L.line(SK_BEACH_PIER, vec2(x0 - rampLen, py), vec2(x0, py), 8.f, zStart, 6.f, 0x51E9u);
        r.variant = 1;  // ramp
        r.p[0] = 6.f;
        r.p[1] = zStart;
        r.p[2] = deckZ;
        L.blockAA(S.lotBlocks, x0 - rampLen - 10.f, py - 20.f, x1, py + 20.f);
        L.blockAA(S.vegBlocks, x0 - rampLen - 10.f, py - 40.f, x1, py + 40.f);
        // streets stop where the access ramp starts (a street running on under the rising ramp puts a step in the road)
        L.blockAA(S.roadBlocks, x0 - rampLen + 3.f, py - 9.f, x1, py + 9.f);
        float ps = p.p[1];
        SiteElem& fw = L.elem(SK_FERRIS_WHEEL, vec2(ps + 34.f, py + 20.f), vec2(1, 0), 32.f, 6.f, deckZ, 64.f, 0xFE221u);
        fw.p[0] = 28.f;  // wheel radius
        SiteElem& co = L.elem(SK_COASTER, vec2(ps + 104.f, py + 17.f), vec2(1, 0), 36.f, 16.f, deckZ, 24.f, 0xC0A57u);
        co.p[0] = 34.f;
        co.p[1] = 14.f;
        L.elem(SK_DROP_TOWER, vec2(ps + 128.f, py - 22.f), vec2(1, 0), 5.f, 5.f, deckZ, 66.f, 0xD20Bu);
        L.elem(SK_CAROUSEL, vec2(ps + 26.f, py - 20.f), vec2(1, 0), 10.f, 10.f, deckZ, 9.f, 0xCA20u);
        L.elem(SK_SWING_RIDE, vec2(ps + 90.f, py - 21.f), vec2(1, 0), 11.f, 11.f, deckZ, 18.f, 0x5A1Au);
        SiteElem& g = L.elem(SK_PIER_GAMES, vec2(ps + 60.f, py - 24.f), vec2(1, 0), 16.f, 6.f, deckZ, 6.f, 0x6A3Eu);
        g.text = "ARCADE";
    }
    // --- Lighthouse on a Coral Key
    {
        vec2 A(-400, -6750), B(-4000, -9450), C(-8400, -9200);
        float t = 0.25f;
        vec2 kc = bezier2(A, B, C, t);
        vec2 tan = normalize(bezier2(A, B, C, t + 0.01f) - bezier2(A, B, C, t - 0.01f));
        vec2 side = perp(tan);
        if (side.y > 0) side = -side;  // ocean (south-east) side
        vec2 pos = kc + side * 80.f;
        for (float o = 95.f; o > 20.f; o -= 5.f) {
            vec2 q = kc + side * o;
            if (!map.isWater(q.x, q.y) && !map.isWater(q.x + side.x * 12.f, q.y + side.y * 12.f)) { pos = q; break; }
        }
        float z = map.heightAt(pos.x, pos.y);
        map.flattenRect(pos, tan, 16.f, 16.f, z, 10.f);
        SiteElem& e = L.elem(SK_LIGHTHOUSE, pos, side, 10.f, 10.f, z, 44.f, 0x11647u);
        e.text = "CAYO FARO LIGHT";
        L.building(pos - side * 22.f + tan * 6.f, tan, 7.f, 5.5f, BS_HOUSE, ROOF_HIP, 1, side, REG_KEYS);
        L.block(S.lotBlocks, pos, tan, 30.f, 30.f);
        L.block(S.vegBlocks, pos, tan, 14.f, 14.f);
    }
    // --- Radio / TV mast on the Cypress Ridge summit
    {
        vec2 best(-7000.f, 7250.f);
        float bh = -1e9f;
        for (float y = 5200.f; y < 9400.f; y += 16.f)
            for (float x = -9400.f; x < -4400.f; x += 16.f) {
                if (map.regionAt(x, y) != REG_RIDGE) continue;
                float h = map.heightAt(x, y);
                if (h > bh) { bh = h; best = vec2(x, y); }
            }
        map.flattenRect(best, vec2(1, 0), 22.f, 22.f, bh - 0.4f, 18.f);
        SiteElem& e = L.elem(SK_RADIO_MAST, best, vec2(1, 0), 90.f, 90.f, bh - 0.4f, 262.f, 0x7A57u);
        e.text = "SOLTV 9";
        L.block(S.vegBlocks, best, vec2(1, 0), 30.f, 30.f);
        L.block(S.lotBlocks, best, vec2(1, 0), 95.f, 95.f);
        // gravel access track from the end of Summit Road
        vec2 summitRoadEnd(-7300.f, 7100.f);
        if (length(summitRoadEnd - best) > 60.f && length(summitRoadEnd - best) < 2500.f) {
            std::vector<vec2> tr;
            vec2 d = best - summitRoadEnd;
            int n = Max(4, (int)(length(d) / 40.f));
            for (int k = 0; k <= n; k++) {
                float t = (float)k / n;
                vec2 q = summitRoadEnd + d * t + perp(normalize(d)) * (sinf(t * kPi * 2.f) * Min(60.f, length(d) * 0.08f));
                tr.push_back(q);
            }
            tr.back() = best - normalize(d) * 26.f;
            L.road(chaikin(tr, 2), RC_DIRT, RF_UNPAVED, "Tower Road");
        }
    }
    // --- Palmera Sugar Mill in the cane fields (south of the Fort Castell road)
    {
        vec2 want(1450.f, 7480.f);
        vec2 best = want;
        float bestScore = 1e9f;
        for (float dy = -300.f; dy <= 300.f; dy += 40.f)
            for (float dx = -500.f; dx <= 500.f; dx += 40.f) {
                vec2 c = want + vec2(dx, dy);
                float hmn = 1e9f, hmx = -1e9f;
                bool ok = true;
                for (float oy = -110.f; oy <= 110.f && ok; oy += 22.f)
                    for (float ox = -110.f; ox <= 110.f; ox += 22.f) {
                        vec2 q = c + vec2(ox, oy);
                        if (map.isWater(q.x, q.y) || map.regionAt(q.x, q.y) != REG_FARMLAND) { ok = false; break; }
                        float h = map.heightAt(q.x, q.y);
                        hmn = Min(hmn, h);
                        hmx = Max(hmx, h);
                    }
                if (!ok) continue;
                float score = (hmx - hmn) * 10.f + length(c - want) * 0.01f;
                if (score < bestScore) { bestScore = score; best = c; }
            }
        float z = meanHeight(map, best - vec2(110.f), best + vec2(110.f), 10.f);
        map.flattenRect(best, vec2(1, 0), 115.f, 100.f, z, 40.f);
        SiteElem& e = L.elem(SK_SUGAR_MILL, best, vec2(1, 0), 110.f, 95.f, z, 72.f, 0x5A6A2u);
        e.text = "PALMERA SUGAR CO";
        L.block(S.lotBlocks, best, vec2(1, 0), 125.f, 110.f);
        L.block(S.vegBlocks, best, vec2(1, 0), 120.f, 105.f);
        L.pad(best + vec2(0, -30.f), vec2(1, 0), 108.f, 62.f, z + 0.2f, PAD_YARD, packRGBA8(0.8f, 0.76f, 0.7f, 1.f));
        // access road south to the county road at y = 7200
        vec2 gate = best + vec2(-60.f, -95.f);
        L.road({gate, vec2(gate.x, 7200.f)}, RC_RURAL, 0, "Mill Road");
    }
    // --- Rio Sol Marina (Calle Luna): docks along the north bank of the river
    {
        vec2 a(2100.f, 260.f), b(2600.f, 60.f);
        vec2 t = normalize(b - a), n = perp(t);
        vec2 mid = lerp(a, b, 0.36f);
        // find the bank: march from the centerline along the normal until land
        float bank = 30.f;
        for (float o = 10.f; o < 90.f; o += 1.f) {
            vec2 q = mid + n * o;
            if (!map.isWater(q.x, q.y)) { bank = o; break; }
        }
        SiteElem& m = L.elem(SK_RIVER_MARINA, mid, t, 95.f, 30.f, 0.f, 8.f, 0x21A0u);
        m.p[0] = bank;  // bank offset from the channel centerline (along +n)
        m.p[1] = 1.4f;  // dock height above water
        m.text = "RIO SOL MARINA";
        vec2 landC = mid + n * (bank + 22.f);
        L.block(S.lotBlocks, landC, t, 100.f, 24.f);
        L.block(S.vegBlocks, landC, t, 100.f, 24.f);
    }
    // --- Lake Okahatchee: boat ramp, fishing pier and docks on the town side
    {
        vec2 town(250.f, 6750.f), lakeC(-2400.f, 7000.f);
        vec2 dir = normalize(lakeC - town);
        float d = marchToWater(map, town, dir, 3500.f, 4.f);
        if (d > 0.f) {
            vec2 shore = town + dir * d;
            vec2 landDir = -dir;
            float z = map.heightAt(shore.x + landDir.x * 30.f, shore.y + landDir.y * 30.f);
            SiteElem& r = L.elem(SK_BOAT_RAMP, shore, dir, 40.f, 30.f, z, 5.f, 0x1A4Eu);
            r.text = "OKAHATCHEE PUBLIC LANDING";
            // sloped ramp pad from the parking lot into the water
            float zTop = z + 0.1f, zBot = kLakeLevel - 1.4f;
            vec2 rc = shore + dir * 2.f;
            Pad& rp = L.pad(rc, dir, 18.f, 5.f, (zTop + zBot) * 0.5f, PAD_RAMP, 0xffffffffu, false, false);
            rp.slope = (zBot - zTop) / 36.f;
            vec2 lot = shore + landDir * 36.f;
            map.flattenRect(lot, dir, 26.f, 22.f, z - 0.25f, 12.f);
            L.pad(lot, dir, 24.f, 20.f, z, PAD_PARKING);
            L.block(S.lotBlocks, shore + landDir * 20.f, dir, 60.f, 60.f);
            L.block(S.vegBlocks, shore + landDir * 20.f, dir, 55.f, 50.f);
            SiteElem& fp = L.elem(SK_DOCK, shore + perp(dir) * 40.f, dir, 50.f, 14.f, kLakeLevel, 3.f, 0x1A4Fu);
            fp.p[0] = 70.f;
            fp.p[1] = 0.f;
            fp.variant = 2;  // T-shaped fishing pier
            SiteElem& dk = L.elem(SK_DOCK, shore - perp(dir) * 34.f, dir, 30.f, 14.f, kLakeLevel, 3.f, 0x1A50u);
            dk.p[0] = 30.f;
            dk.p[1] = 1.f;
            dk.variant = 3;  // marina dock with boats on both sides
        }
    }
    // --- Sawgrass: visitor boardwalk loop, observation tower and airboat dock on the Old Trail canal
    {
        vec2 base(-5060.f, 190.f);
        std::vector<vec2> loop;
        for (int k = 0; k <= 40; k++) {
            float a = kTwoPi * k / 40.f;
            float r = 150.f + 35.f * sinf(a * 3.f + 0.7f);
            loop.push_back(base + vec2(0, 230.f) + vec2(cosf(a) * r * 1.3f, sinf(a) * r));
        }
        std::vector<vec2> path;
        path.push_back(base);
        path.push_back(base + vec2(0, 60.f));
        for (auto& q : loop) path.push_back(q);
        SiteElem& bw = L.elem(SK_BOARDWALK, base + vec2(0, 230.f), vec2(1, 0), 260.f, 220.f, 0.f, 16.f, 0xB0A2u);
        bw.pts = path;
        for (size_t i = 0; i + 1 < path.size(); i++) {
            vec2 p0 = path[i], p1 = path[i + 1];
            float z = Max(map.heightAt(p0.x, p0.y), Max(map.heightAt(p1.x, p1.y), 0.f)) + 1.1f;
            Pad& pd = L.pad((p0 + p1) * 0.5f, p1 - p0, length(p1 - p0) * 0.5f + 1.f, 1.3f, z, PAD_DECK, 0xffffffffu, false, false);
            pd.flags = 1;
        }
        vec2 tw = base + vec2(0, 230.f) + vec2(150.f * 1.3f, 0.f) * 0.96f;
        L.elem(SK_OBS_TOWER, tw, vec2(1, 0), 5.f, 5.f, map.heightAt(tw.x, tw.y), 17.f, 0x0B5u);
        L.block(S.vegBlocks, tw, vec2(1, 0), 8.f, 8.f);
        vec2 vc = base + vec2(26.f, -20.f);
        L.building(vc, vec2(1, 0), 12.f, 7.f, BS_SHACK, ROOF_GABLE, 1, vec2(0, 1), REG_SAWGRASS);
        // airboat dock on the canal south of the road
        vec2 canalPt(-5000.f, -100.f);
        float d = marchToWater(map, canalPt + vec2(0, 60.f), vec2(0, -1), 120.f, 2.f);
        if (d > 0.f) {
            vec2 shore = canalPt + vec2(0, 60.f) + vec2(0, -d);
            SiteElem& dk = L.elem(SK_DOCK, shore, vec2(0, -1), 16.f, 12.f, 0.f, 3.f, 0xA1B0u);
            dk.p[0] = 10.f;
            dk.p[1] = 1.f;
            dk.variant = 4;  // airboats
        }
    }
    // --- Fishing shacks and docks on small keys
    {
        int placed = 0;
        for (size_t i = 0; i < map.smallIslands.size() && placed < 9; i++) {
            const auto& isl = map.smallIslands[i];
            vec2 c(0, 0);
            for (auto& q : isl) c += q;
            c = c / (float)isl.size();
            if (c.y > -3000.f) continue;  // bay islands
            if (i + 1 == map.smallIslands.size()) continue;  // Key Solano (town)
            u32 h = hash32((u32)i * 7919u + 11u);
            if ((h & 3) == 0) continue;
            // walk from the center toward the south until water: dock at the shore, shack a little inland
            vec2 dir = normalize(vec2(hashToFloat(h) - 0.5f, -1.f));
            float d = marchToWater(map, c, dir, 600.f, 3.f);
            if (d < 25.f) continue;
            vec2 shore = c + dir * d;
            vec2 shack = shore - dir * 14.f;
            if (map.isWater(shack.x, shack.y)) continue;
            // keep clear of the Overseas Highway corridor
            bool nearHwy = false;
            {
                vec2 A(-400, -6750), B(-4000, -9450), C(-8400, -9200);
                for (int k = 0; k <= 60 && !nearHwy; k++) {
                    vec2 q = bezier2(A, B, C, k / 60.f);
                    if (length(q - shack) < 45.f) nearHwy = true;
                }
            }
            if (nearHwy) continue;
            L.building(shack, perp(dir), 4.5f, 3.8f, BS_SHACK, ROOF_SHED, 1, dir, REG_KEYS);
            SiteElem& dk = L.elem(SK_DOCK, shore - dir * 2.f, dir, 16.f, 8.f, 0.f, 3.f, h);
            dk.p[0] = 14.f + (h % 7);
            dk.p[1] = (h & 4) ? 1.f : 0.f;
            dk.variant = 0;  // weathered fishing dock with a skiff
            placed++;
        }
    }
}

}  // namespace sites_detail

// ---------------------------------------------------------------------------------------------------------------

void SiteSet::layout(WorldMap& map) {
    double t0 = TimeSeconds();
    pads.clear();
    elems.clear();
    lotBlocks.clear();
    roadBlocks.clear();
    vegBlocks.clear();
    buildingReqs.clear();
    roads.clear();
    Lay L{*this, map};
    layoutAirport(L);
    layoutPort(L);
    layoutKeyCoral(L);
    layoutLandmarks(L);
    transitLayout(*this, map);   // SkyLine metro corridor and stations (transit.cpp)
    buildPadHash();
    buildRectHash();
    generated = true;
    LOG("Sites: %zu pads, %zu elements, %zu roads, %zu buildings requested (%.2f s)", pads.size(), elems.size(), roads.size(), buildingReqs.size(),
        TimeSeconds() - t0);
}


void SiteSet::finalize(WorldMap& map, const RoadNetwork& net, const BuildingSet& bs) {
    double t0 = TimeSeconds();
    Lay L{*this, map};
    // ---------------------------------------------------------------- billboards along the highways
    {
        std::vector<vec2> placed;
        int count = 0;
        for (size_t ei = 0; ei < net.edges.size(); ei++) {
            const RoadEdge& e = net.edges[ei];
            if (e.cls != RC_HIGHWAY || e.length < 300.f) continue;
            const float spacing = 470.f;
            float s = 120.f + hashToFloat(e.seed) * spacing;
            while (s < e.length - 100.f) {
                u32 h = hash32(e.seed ^ (u32)(s * 13.f));
                float step = spacing * (0.75f + 0.6f * hashToFloat(h >> 3));
                int side = (h & 1) ? 1 : -1;
                vec3 P = e.posAt(s);
                vec2 t2 = normalize(e.tangentAt(s).xy());
                vec2 R(t2.y, -t2.x);
                bool ok = false;
                vec2 pos;
                for (int tries = 0; tries < 2 && !ok; tries++, side = -side) {
                    pos = P.xy() + R * (side * (e.halfWidth + 15.f));
                    ok = true;
                    Region reg = map.regionAt(pos.x, pos.y);
                    if (map.isWater(pos.x, pos.y) || map.isWater(pos.x + t2.x * 8.f, pos.y + t2.y * 8.f) || reg == REG_OCEAN) ok = false;
                    else if (reg == REG_SAWGRASS && map.heightAt(pos.x, pos.y) < 0.6f) ok = false;
                    else if (blocksLots(pos) || padAt(pos) || net.nearRoad(pos, 5.f) || net.nearRoad(pos + t2 * 7.f, 4.f) || net.nearRoad(pos - t2 * 7.f, 4.f))
                        ok = false;
                    else if (bs.pointInBuilding(pos, 9.f) || bs.pointInBuilding(pos + t2 * 7.f, 6.f) || bs.pointInBuilding(pos - t2 * 7.f, 6.f)) ok = false;
                    for (auto& q : placed)
                        if (length(q - pos) < 280.f) { ok = false; break; }
                }
                if (ok) {
                    float ground = map.heightAt(pos.x, pos.y);
                    float bottom = Max(ground + 7.f, P.z + 4.5f);
                    // faces the traffic in both directions: panel normal mostly along the road, toed in toward it
                    vec2 face = normalize(-t2 * 0.94f + (-R * (float)side) * 0.34f);
                    SiteElem& b = L.elem(SK_BILLBOARD, pos, face, 8.f, 8.f, ground, bottom - ground + 5.2f, h);
                    b.p[0] = bottom - ground;
                    b.p[1] = (float)side;
                    b.variant = (u16)(hash32(h + 77u) % 24u);
                    b.pts.push_back(t2);  // road direction (for the second face)
                    placed.push_back(pos);
                    count++;
                }
                s += step;
            }
        }
        LOG("Sites: %d highway billboards", count);
    }
    // ---------------------------------------------------------------- farm silos and windmills next to barns/farmhouses
    {
        int silos = 0, mills = 0;
        for (size_t i = 0; i < bs.buildings.size(); i++) {
            const Building& b = bs.buildings[i];
            if (b.region != REG_FARMLAND && b.region != REG_REDLAND) continue;
            u32 h = hash32(b.seed ^ 0x5110u);
            auto freeAt = [&](vec2 p, float r) {
                return !map.isWater(p.x, p.y) && !net.nearRoad(p, r) && !bs.pointInBuilding(p, r) && !blocksLots(p);
            };
            if (b.style == BS_BARN && (h & 3) != 0) {
                int n = 1 + (int)((h >> 4) % 3);
                float side = (h & 16) ? 1.f : -1.f;
                vec2 base = b.c + b.ax * (side * (b.hx + 8.f)) - b.front * (b.hy * 0.3f);
                for (int k = 0; k < n; k++) {
                    vec2 p = base + b.ax * (side * k * 9.5f);
                    if (!freeAt(p, 5.f)) break;
                    SiteElem& e = L.elem(SK_SILO, p, b.ax, 5.f, 5.f, map.heightAt(p.x, p.y), 18.f, h + (u32)k * 31u);
                    e.variant = (u16)((h >> 8) % 3);
                    e.p[0] = 3.3f + ((h >> 12) % 3) * 0.5f;  // radius
                    e.p[1] = 14.f + ((h >> 14) % 4) * 2.5f;  // height
                    silos++;
                }
            }
            if ((b.style == BS_FARMHOUSE || b.style == BS_BARN) && ((h >> 20) % 5) == 0) {
                vec2 p = b.c - b.front * (b.hy + 22.f) + b.ax * ((h & 64) ? 14.f : -14.f);
                if (freeAt(p, 6.f)) {
                    SiteElem& e = L.elem(SK_WINDMILL, p, normalize(vec2(0.6f, 0.8f)), 4.f, 4.f, map.heightAt(p.x, p.y), 16.f, h ^ 0x3EEDu);
                    e.p[0] = 11.f + (h % 5);
                    mills++;
                }
            }
        }
        LOG("Sites: %d silos, %d windmills", silos, mills);
    }
    // ---------------------------------------------------------------- beach access where the streets stop short of the sand
    {
        int paths = 0;
        std::vector<vec2> placed;
        for (const BeachEnd& be : net.beachEnds) {
            if (be.node < 0 || be.node >= (int)net.nodes.size()) continue;
            const RoadNode& n = net.nodes[be.node];
            if (n.edges.empty() || length2(be.dir) < 0.5f) continue;
            vec2 dir = normalize(be.dir);
            // from the edge of the turning circle's sidewalk, or of the pavement and sidewalk the street was dropped back to
            float off = 0.f;
            if (be.bulb && n.edges.size() == 1) {
                const RoadEdge& e = net.edges[n.edges[0]];
                float br = net.bulbRadius(n);
                off = (br > 0.f ? br : e.halfWidth) + e.sidewalk + 0.4f;
            } else {
                for (int ei : n.edges) off = Max(off, net.edges[ei].halfWidth + net.edges[ei].sidewalk);
                off += 0.6f;
            }
            vec2 a = n.p + dir * off;
            // out across the sand to a few metres short of the water
            float len = 0.f;
            for (float s = 2.f; s < 160.f; s += 2.f) {
                vec2 q = a + dir * s;
                if (map.isWater(q.x, q.y) || map.coastDistance(q.x, q.y) < 7.f) break;
                len = s;
            }
            if (len < 8.f) continue;
            vec2 b = a + dir * len;
            bool clash = blocksRoads(a) || blocksLots(a + dir * (len * 0.5f)) || bs.pointInBuilding(a, 1.5f) || bs.pointInBuilding(a + dir * (len * 0.5f), 1.5f) ||
                         net.nearRoad(a + dir * 3.f, 0.3f);
            for (const vec2& p : placed) clash = clash || length(p - a) < 12.f;
            if (clash) continue;
            SiteElem& ba = L.line(SK_BEACH_ACCESS, a, b, 1.6f, map.heightAt(a.x, a.y), 3.f, hash32((u32)be.node * 2654435761u + 0xBEAC5u));
            ba.text = be.name;
            L.block(vegBlocks, (a + b) * 0.5f, dir, len * 0.5f + 1.f, 2.4f);
            placed.push_back(a);
            paths++;
        }
        LOG("Sites: %d beach access paths", paths);
    }
    // SkyLine profile, piers, bus stops and ferry piers need the roads (transit.cpp)
    transitFinalize(*this, map, net, bs);
    // ---------------------------------------------------------------- per-cell element lists
    const int cps = kCellsPerSide;
    cellElems.assign((size_t)cps * cps, {});
    for (size_t i = 0; i < elems.size(); i++) {
        const SiteElem& e = elems[i];
        vec2 mn, mx;
        if (!e.pts.empty() && e.kind != SK_BILLBOARD) {
            mn = vec2(1e9f);
            mx = vec2(-1e9f);
            for (auto& q : e.pts) { mn = vmin(mn, q); mx = vmax(mx, q); }
            mn -= vec2(12.f);
            mx += vec2(12.f);
        } else if (e.isLine()) {
            mn = vmin(e.a, e.b) - vec2(e.hy + 2.f);
            mx = vmax(e.a, e.b) + vec2(e.hy + 2.f);
        } else {
            float r = e.radius() + 2.f;
            mn = e.c - vec2(r);
            mx = e.c + vec2(r);
        }
        int x0 = Clamp((int)floorf((mn.x + kWorldHalf) / kCellSize), 0, cps - 1), x1 = Clamp((int)floorf((mx.x + kWorldHalf) / kCellSize), 0, cps - 1);
        int y0 = Clamp((int)floorf((mn.y + kWorldHalf) / kCellSize), 0, cps - 1), y1 = Clamp((int)floorf((mx.y + kWorldHalf) / kCellSize), 0, cps - 1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                if (e.isLine() && e.pts.empty()) {
                    vec2 cc = cellOrigin(x, y) + vec2(kCellSize * 0.5f);
                    if (distPointSegment2D(cc, e.a, e.b) > e.hy + kCellSize * 0.72f) continue;
                }
                cellElems[(size_t)y * cps + x].push_back((int)i);
            }
    }
    // Skyline landmarks stream their far LOD out to farRange
    farCells.clear();
    for (const SiteElem& e : elems) {
        bool tall = e.h >= 40.f && (e.kind == SK_SOLARIS || e.kind == SK_STADIUM || e.kind == SK_FERRIS_WHEEL || e.kind == SK_DROP_TOWER ||
                                    e.kind == SK_RADIO_MAST || e.kind == SK_SUGAR_MILL || e.kind == SK_LIGHTHOUSE || e.kind == SK_STS_CRANE ||
                                    e.kind == SK_CONTROL_TOWER || e.kind == SK_SHIP || e.kind == SK_CITY_HALL);
        if (!tall) continue;
        int cx = Clamp((int)floorf((e.c.x + kWorldHalf) / kCellSize), 0, cps - 1), cy = Clamp((int)floorf((e.c.y + kWorldHalf) / kCellSize), 0, cps - 1);
        int ci = cy * cps + cx;
        if (std::find(farCells.begin(), farCells.end(), ci) == farCells.end()) farCells.push_back(ci);
    }
    // Owner cells of pads for the generic pad renderer are resolved by clipping in the cell builder.
    buildPadHash();
    buildRectHash();
    // Key anchors for missions / camera work
    for (const SiteElem& e : elems) {
        const char* nm = nullptr;
        switch (e.kind) {
            case SK_TERMINAL: nm = "terminal"; break;
            case SK_CONTROL_TOWER: nm = "control tower"; break;
            case SK_SHIP: nm = "container ship"; break;
            case SK_PORT_GATE: nm = "port gate"; break;
            case SK_MARINA: nm = "Key Coral marina"; break;
            case SK_BEACH_CLUB: nm = "beach club"; break;
            case SK_SOLARIS: nm = "Solaris One"; break;
            case SK_STADIUM: nm = "stadium"; break;
            case SK_CITY_HALL: nm = "City Hall"; break;
            case SK_PARK: nm = "Canvas Park"; break;
            case SK_FERRIS_WHEEL: nm = "Ferris wheel"; break;
            case SK_LIGHTHOUSE: nm = "lighthouse"; break;
            case SK_RADIO_MAST: nm = "radio mast"; break;
            case SK_SUGAR_MILL: nm = "sugar mill"; break;
            case SK_RIVER_MARINA: nm = "Rio Sol marina"; break;
            case SK_BOAT_RAMP: nm = "Okahatchee boat ramp"; break;
            case SK_OBS_TOWER: nm = "Sawgrass tower"; break;
            default: break;
        }
        if (nm) LOG("Site %-20s at (%.0f, %.0f, %.1f)", nm, e.c.x, e.c.y, e.z);
        if (e.kind == SK_RUNWAY) LOG("Site runway %-8s from (%.0f, %.0f) to (%.0f, %.0f) z %.1f width %.0f", e.text.c_str(), e.a.x, e.a.y, e.b.x, e.b.y, e.z, e.p[0]);
        if (e.kind == SK_BEACH_PIER && e.variant == 0) LOG("Site Sol Beach Pier from (%.0f, %.0f) to (%.0f, %.0f) deck z %.1f", e.a.x, e.a.y, e.b.x, e.b.y, e.z);
    }
    LOG("Sites finalized: %zu elements (%.2f s)", elems.size(), TimeSeconds() - t0);
}


void SiteSet::makeFacades(BuildingSet& bs) {
    auto make = [&](int style, float floorH, float groundH, float bay, float winW, float winH, float sill, float depth, vec3 wall, vec3 frame, vec3 glass,
                    u32 flags, float lit, MaterialId wallMat, u32 seed) {
        FacadeGPU f = {};
        f.floorH = floorH;
        f.groundH = groundH;
        f.bayW = bay;
        f.winW = winW;
        f.winH = winH;
        f.sillH = sill;
        f.roomDepth = depth;
        f.style = (float)style;
        f.wallColor = packRGBA8(wall.x, wall.y, wall.z, 1.f);
        f.frameColor = packRGBA8(frame.x, frame.y, frame.z, 1.f);
        f.glassColor = packRGBA8(glass.x, glass.y, glass.z, 1.f);
        f.flags = flags;
        f.litFrac = lit;
        f.wallLayer = (float)wallMat;
        f.seed = seed;
        f.signIndex = (float)(seed % 480u);
        bs.facades.push_back(f);
        return (float)(bs.facades.size() - 1);
    };
    float terminal = make(1, 9.5f, 9.5f, 3.0f, 0.95f, 0.95f, 0.05f, 14.f, vec3(0.95f), vec3(0.8f, 0.83f, 0.86f), vec3(0.6f, 0.8f, 0.82f), 1u | 16u, 0.92f,
                          MAT_CONCRETE_PANEL, 0x7E2A1u);
    float concourse = make(1, 5.5f, 5.5f, 3.0f, 0.95f, 0.95f, 0.05f, 10.f, vec3(0.95f), vec3(0.8f, 0.83f, 0.86f), vec3(0.6f, 0.8f, 0.82f), 16u, 0.9f,
                           MAT_CONCRETE_PANEL, 0xC0C0u);
    float solaris = make(1, 4.1f, 9.f, 1.6f, 0.95f, 0.95f, 0.05f, 9.f, vec3(0.8f), vec3(0.72f, 0.75f, 0.78f), vec3(0.42f, 0.58f, 0.66f), 16u, 0.78f,
                         MAT_CONCRETE_PANEL, 0x501Au);
    float cityHall = make(0, 5.2f, 6.5f, 4.2f, 0.42f, 0.6f, 1.1f, 7.f, vec3(1.f, 0.97f, 0.9f), vec3(0.32f, 0.22f, 0.14f), vec3(0.45f, 0.55f, 0.6f), 8u, 0.55f,
                         MAT_STUCCO, 0xC17u);
    float shopfront = make(0, 3.4f, 4.4f, 3.4f, 0.6f, 0.55f, 0.9f, 6.f, vec3(1.f, 0.85f, 0.7f), vec3(0.9f, 0.3f, 0.35f), vec3(0.6f, 0.75f, 0.8f), 1u | 2u | 4u,
                          0.9f, MAT_STUCCO, 0x5A0Fu);
    float stadiumBase = make(1, 6.f, 7.f, 3.2f, 0.95f, 0.9f, 0.05f, 10.f, vec3(0.9f), vec3(0.85f), vec3(0.55f, 0.7f, 0.75f), 1u | 16u, 0.85f, MAT_CONCRETE_PANEL,
                             0x57Au);
    for (SiteElem& e : elems) {
        switch (e.kind) {
            case SK_TERMINAL: e.p[7] = terminal; break;
            case SK_CONCOURSE: e.p[7] = concourse; break;
            case SK_SOLARIS: e.p[7] = solaris; break;
            case SK_CITY_HALL: e.p[7] = cityHall; break;
            case SK_PIER_GAMES: case SK_BEACH_CLUB: case SK_BEACH_PIER: case SK_CLUBHOUSE: case SK_MARINA: case SK_RIVER_MARINA: case SK_BOAT_RAMP:
                e.p[7] = shopfront; break;
            case SK_STADIUM: e.p[7] = stadiumBase; break;
            default: break;
        }
    }
}

}  // namespace World
