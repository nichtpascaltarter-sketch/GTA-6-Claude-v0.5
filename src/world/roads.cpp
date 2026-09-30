#include "roads.h"
#include "sites.h"
#include "../core/noise.h"
#include <unordered_map>

namespace World {

RoadNetwork* gRoads = nullptr;

static const RoadClassInfo kRoadInfo[RC_COUNT] = {
    // name        laneW lanes median shoulder sidewalk speed  gradeSep
    {"Expressway", 3.6f, 3, 1.2f, 2.6f, 0.f, 33.f, true},
    {"Boulevard", 3.4f, 3, 4.0f, 1.2f, 4.5f, 18.f, false},
    {"Avenue", 3.3f, 2, 0.f, 2.2f, 3.5f, 15.f, false},
    {"Street", 3.2f, 1, 0.f, 2.4f, 3.0f, 12.f, false},
    {"Lane", 3.0f, 1, 0.f, 1.0f, 1.8f, 10.f, false},
    {"Road", 3.3f, 1, 0.f, 1.0f, 0.f, 22.f, false},
    {"Track", 2.8f, 1, 0.f, 0.f, 0.f, 9.f, false},
    {"Ramp", 3.8f, 1, 0.f, 1.4f, 0.f, 20.f, true},
};
const RoadClassInfo& roadInfo(RoadClass c) { return kRoadInfo[c < RC_COUNT ? c : RC_STREET]; }

vec3 RoadEdge::posAt(float s) const {
    if (pts.size() < 2) return pts.empty() ? vec3() : pts[0];
    s = Clamp(s, 0.f, length);
    size_t i = std::upper_bound(dist.begin(), dist.end(), s) - dist.begin();
    if (i == 0) i = 1;
    if (i >= pts.size()) i = pts.size() - 1;
    float seg = dist[i] - dist[i - 1];
    float t = seg > 1e-5f ? (s - dist[i - 1]) / seg : 0.f;
    return lerp(pts[i - 1], pts[i], t);
}
vec3 RoadEdge::tangentAt(float s) const {
    if (pts.size() < 2) return vec3(1, 0, 0);
    s = Clamp(s, 0.f, length);
    size_t i = std::upper_bound(dist.begin(), dist.end(), s) - dist.begin();
    if (i == 0) i = 1;
    if (i >= pts.size()) i = pts.size() - 1;
    return normalize(pts[i] - pts[i - 1]);
}

namespace roads_detail {

struct PolyIn {
    std::vector<vec2> pts;
    RoadClass cls;
    u8 flags;
    int layer;  // 0 at-grade, 1 highway, -1 connector (endpoint snapping only)
    std::string name;
    int attachEnd = -1;   // connectors: the end (0 front, 1 back) that joins the highway; the other lands at grade
};

struct Seg {
    vec2 a, b;
    int poly;
    std::vector<float> splits;
    bool first = false, last = false;   // the polyline's first / last segment (connector ends may join other layers)
};

struct Builder {
    std::vector<PolyIn> polys;
    WorldMap* map = nullptr;

    void add(const std::vector<vec2>& pts, RoadClass cls, int layer, u8 flags = 0, const char* name = "") {
        if (pts.size() < 2) return;
        PolyIn p;
        p.pts = pts;
        p.cls = cls;
        p.flags = flags;
        p.layer = layer;
        p.name = name;
        polys.push_back(std::move(p));
    }
};

// Catmull-Rom smoothing + resampling of a control polyline
std::vector<vec2> smoothPath(const std::vector<vec2>& ctrl, float step) {
    std::vector<vec2> out;
    if (ctrl.size() < 2) return ctrl;
    for (size_t i = 0; i + 1 < ctrl.size(); i++) {
        vec2 p0 = ctrl[i > 0 ? i - 1 : i], p1 = ctrl[i], p2 = ctrl[i + 1], p3 = ctrl[i + 2 < ctrl.size() ? i + 2 : i + 1];
        float len = length(p2 - p1);
        int n = Max(1, (int)(len / step));
        for (int k = 0; k < n; k++) {
            float t = (float)k / n, t2 = t * t, t3 = t2 * t;
            vec2 p = (p1 * 2.f + (p2 - p0) * t + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * t3) * 0.5f;
            out.push_back(p);
        }
    }
    out.push_back(ctrl.back());
    return out;
}

std::vector<vec2> kmPts(std::initializer_list<float> xy) {
    std::vector<vec2> out;
    const float* p = xy.begin();
    for (size_t i = 0; i + 1 < xy.size(); i += 2) out.push_back(vec2(p[i] * 1000.f, p[i + 1] * 1000.f));
    return out;
}

bool isUrbanRegion(Region r) {
    return r == REG_DOWNTOWN || r == REG_FINANCIAL || r == REG_MIDTOWN || r == REG_NORTH_CITY || r == REG_CALLE_LUNA ||
           r == REG_FLATS || r == REG_BEACH || r == REG_FORT_CASTELL || r == REG_KEY_TOWN || r == REG_LAKE_TOWN ||
           r == REG_HARLOW || r == REG_GULF_TOWN;
}

// March along a straight line and emit runs of points satisfying `valid`.
template <typename F>
void emitRuns(Builder& b, vec2 a, vec2 c, float step, RoadClass cls, F valid, float maxWaterGap, const char* name = "") {
    vec2 d = c - a;
    float len = length(d);
    int n = Max(1, (int)(len / step));
    std::vector<vec2> run;
    int waterRun = 0;
    std::vector<vec2> pending;
    for (int i = 0; i <= n; i++) {
        vec2 p = a + d * ((float)i / n);
        bool ok = valid(p);
        bool water = b.map->isWater(p.x, p.y);
        if (ok && !water) {
            if (!pending.empty()) {
                // allow short water gaps (bridges) for bigger roads
                if (!run.empty() && pending.size() * step <= maxWaterGap) run.insert(run.end(), pending.begin(), pending.end());
                else if (run.size() >= 2) { b.add(run, cls, 0, 0, name); run.clear(); }
                else run.clear();
                pending.clear();
            }
            run.push_back(p);
            waterRun = 0;
        } else if (water && ok && !run.empty()) {
            pending.push_back(p);
            waterRun++;
        } else {
            if (run.size() >= 2) b.add(run, cls, 0, 0, name);
            run.clear();
            pending.clear();
        }
    }
    if (run.size() >= 2) b.add(run, cls, 0, 0, name);
}

// ---------------------------------------------------------------------------------------------
// Names
const char* kStreetNames[] = {"Coral",    "Palmetto", "Mango",    "Flamingo", "Pelican", "Heron",    "Egret",     "Magnolia",
                              "Hibiscus", "Orchid",   "Tamarind", "Banyan",   "Cypress", "Mariner",  "Harbor",    "Lighthouse",
                              "Sunset",   "Sunrise",  "Seabreeze","Tarpon",   "Marlin",  "Dolphin",  "Manatee",   "Osprey",
                              "Citrus",   "Key Lime", "Papaya",   "Guava",    "Cedar",   "Laurel",   "Jasmine",   "Bougainvillea",
                              "Majestic", "Del Mar",  "San Tomas","Alhambra", "Granada", "Valencia", "Sevilla",   "Cordova",
                              "Aragon",   "Castile",  "Ponce",    "Almeria",  "Toledo",  "Madeira",  "Biscaya",   "Esmeralda",
                              "Caribe",   "Tropicana","Oceanic",  "Gulfstream","Atlantic","Bayshore","Tidewater", "Saltmarsh"};
const char* kStreetSuffix[] = {"Street", "Avenue", "Drive", "Way", "Road", "Terrace", "Court", "Place", "Lane", "Boulevard"};

std::string ordinal(int n) {
    const char* suf = "th";
    if (n % 100 < 11 || n % 100 > 13) {
        if (n % 10 == 1) suf = "st";
        else if (n % 10 == 2) suf = "nd";
        else if (n % 10 == 3) suf = "rd";
    }
    return StrFormat("%d%s", n, suf);
}

// ---------------------------------------------------------------------------------------------
// Interchange ramp planning. Ramps are laid out before the network is planarised, so the heights they will get are
// estimated from the terrain and the rules of the elevation pass: at-grade roads 0.25 m over the ground, urban highways
// on 9.5 m viaducts and at least 7 m over any road within 26 m, 4.5% highway approaches, and ramps that hold the
// highway's level alongside it and come down to their landing at the grade limit as late as they can (kRampEnvGrade,
// the envelope the elevation pass gives them). A ramp fits when it gets down in time and every road it passes on the
// way is either cleared by kRampClear or is the road it lands on, met at grade.
constexpr float kRampHalfWidth = 3.3f;    // one 3.8 m lane + 1.4 m shoulders (the Ramp class, one way)
constexpr float kRampHoldReach = 17.6f;   // highway half width + ramp half width + 0.3: the ramp still runs on the highway
constexpr float kRampEnvGrade = 0.069f;   // design descent (the elevation pass limits ramps to 7%)
constexpr float kRampClear = 4.75f;       // deck over a road it crosses (the overlap pass settles anything under 4.5 m)
constexpr float kRampOffset = 18.f;       // centreline offset from the highway's while running alongside, clear of its deck

struct RampPlanner {
    struct RSeg {
        vec2 a, b;
        float za, zb;
        float hw;      // pavement half width
        float reach;   // pavement + sidewalk half width
        int poly;      // Builder poly (-1 for planned ramps)
        int layer;     // 0 at grade, 1 highway, -1 ramp
    };
    const WorldMap* map = nullptr;
    std::vector<RSeg> segs;
    std::unordered_map<long long, std::vector<int>> grid;
    std::vector<int> stamp;
    int stampId = 0;
    static constexpr float kCell = 48.f;
    static long long key(int x, int y) { return (long long)(y + 100000) * 400000LL + (x + 100000); }

    float gradeZ(vec2 p) const {
        float g = map->heightAt(p.x, p.y), wl = map->waterAt(p.x, p.y);
        return (wl > kNoWater + 1.f && wl > g - 0.05f) ? Max(g + 0.25f, wl + 6.5f) : g + 0.25f;
    }
    void insert(const RSeg& s) {
        int id = (int)segs.size();
        segs.push_back(s);
        stamp.push_back(0);
        int x0 = (int)floorf(Min(s.a.x, s.b.x) / kCell), x1 = (int)floorf(Max(s.a.x, s.b.x) / kCell);
        int y0 = (int)floorf(Min(s.a.y, s.b.y) / kCell), y1 = (int)floorf(Max(s.a.y, s.b.y) / kCell);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) grid[key(x, y)].push_back(id);
    }
    // every segment with a cell within r of p (each once)
    template <typename F>
    void query(vec2 p, float r, F fn) {
        stampId++;
        int x0 = (int)floorf((p.x - r) / kCell), x1 = (int)floorf((p.x + r) / kCell);
        int y0 = (int)floorf((p.y - r) / kCell), y1 = (int)floorf((p.y + r) / kCell);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                auto it = grid.find(key(x, y));
                if (it == grid.end()) continue;
                for (int id : it->second) {
                    if (stamp[id] == stampId) continue;
                    stamp[id] = stampId;
                    fn(segs[id]);
                }
            }
    }
    // estimated deck profile of a highway path, resampled at <= 10 m (points, arc length, height)
    void addHighway(const std::vector<vec2>& path, int poly, std::vector<vec2>& dp, std::vector<float>& ds, std::vector<float>& dz) {
        dp.clear();
        for (size_t i = 0; i + 1 < path.size(); i++) {
            int n = Max(1, (int)ceilf(length(path[i + 1] - path[i]) / 10.f));
            for (int k = 0; k < n; k++) dp.push_back(lerp(path[i], path[i + 1], (float)k / n));
        }
        dp.push_back(path.back());
        ds.assign(dp.size(), 0.f);
        for (size_t i = 1; i < dp.size(); i++) ds[i] = ds[i - 1] + length(dp[i] - dp[i - 1]);
        dz.assign(dp.size(), 0.f);
        for (size_t i = 0; i < dp.size(); i++) {
            vec2 p = dp[i];
            float g = map->heightAt(p.x, p.y), wl = map->waterAt(p.x, p.y);
            float z = g + 0.25f;
            if (wl > kNoWater + 1.f && wl > g - 0.05f) z = Max(z, wl + 12.f);
            Region r = map->regionAt(p.x, p.y);
            if (isUrbanRegion(r) || r == REG_SUBURBS || r == REG_GROVE || r == REG_AIRPORT) z = Max(z, g + 9.5f);
            float sz = -1e9f;
            query(p, 26.f, [&](const RSeg& s) {
                if (s.layer != 0) return;
                float t;
                if (distPointSegment2D(p, s.a, s.b, &t) < 26.f) sz = Max(sz, Lerp(s.za, s.zb, t));
            });
            if (sz > -1e8f) z = Max(z, Max(g + 8.f, sz + 7.f));
            dz[i] = z;
        }
        for (int pass = 0; pass < 2; pass++) {
            for (size_t i = 1; i < dz.size(); i++) dz[i] = Max(dz[i], dz[i - 1] - 0.045f * (ds[i] - ds[i - 1]));
            for (size_t i = dz.size() - 1; i-- > 0;) dz[i] = Max(dz[i], dz[i + 1] - 0.045f * (ds[i + 1] - ds[i]));
        }
        for (size_t i = 0; i + 1 < dp.size(); i++) insert({dp[i], dp[i + 1], dz[i], dz[i + 1], 14.f, 14.f, poly, 1});
    }

    // A ramp path laid out from its highway end (index 0, on the highway centreline) to its landing (last point, on the
    // landing road's centreline); landR: the landing junction's radius. profile() resamples it at <= 5 m (P) and works out
    // the heights it will get (Z): held on the highway while it runs against the highway's pavement, then as high as it
    // can stay while still reaching the landing at the design grade, never below grade, flat where it lands (across the
    // junction disc and wherever its surface lies over the landing road's), the elevation pass's 7% limit. Returns 0 when
    // the ramp gets down in time, 1 when it is too short to, 2 when it cannot work at all. clear() then checks everything
    // it passes: roads cleared by kRampClear, or met at grade where it lands.
    std::vector<vec2> P;
    std::vector<float> Z, S, G;
    std::vector<char> onTop;   // held or level with the hold's end: as high as the highway turns out (estimated to +-0.5 m)
    size_t held = 0;
    float L = 0.f, flat = 0.f, landZ = 0.f;
    int profile(const std::vector<vec2>& path, int hwyPoly, int landPoly, float landR) {
        P.clear();
        for (size_t i = 0; i + 1 < path.size(); i++) {
            int n = Max(1, (int)ceilf(length(path[i + 1] - path[i]) / 5.f));
            for (int k = 0; k < n; k++) P.push_back(lerp(path[i], path[i + 1], (float)k / n));
        }
        P.push_back(path.back());
        size_t n = P.size();
        if (n < 8) return 2;
        S.assign(n, 0.f);
        for (size_t i = 1; i < n; i++) S[i] = S[i - 1] + length(P[i] - P[i - 1]);
        L = S.back();
        Z.assign(n, 0.f);
        for (held = 0; held < n; held++) {
            float bd = 1e9f, bz = 0.f;
            vec2 p = P[held];
            query(p, kRampHoldReach, [&](const RSeg& s) {
                if (s.layer != 1 || s.poly != hwyPoly) return;
                float t, d = distPointSegment2D(p, s.a, s.b, &t);
                if (d < bd) { bd = d; bz = Lerp(s.za, s.zb, t); }
            });
            if (bd >= kRampHoldReach) break;
            Z[held] = bz;
        }
        if (held == 0 || held + 4 >= n) return 2;
        flat = landR;
        float zl = -1e9f;
        for (size_t i = n; i-- > held;) {
            bool over = false;
            vec2 p = P[i];
            query(p, 30.f, [&](const RSeg& s) {
                if (s.layer != 0 || s.poly != landPoly) return;
                float t, d = distPointSegment2D(p, s.a, s.b, &t);
                if (d < kRampHalfWidth + s.reach + 1.f) {
                    over = true;
                    if (i + 1 == n) zl = Max(zl, Lerp(s.za, s.zb, t));
                }
            });
            if (!over) break;
            flat = Max(flat, L - S[i]);
        }
        if (zl < -1e8f) return 2;
        landZ = zl;
        float zTop = Z[held - 1];
        G.assign(n, 0.f);
        for (size_t i = 0; i < n; i++) {
            G[i] = map->heightAt(P[i].x, P[i].y);
            if (i < held) continue;
            if (L - S[i] <= flat) Z[i] = landZ;
            else Z[i] = Max(gradeZ(P[i]), Min(zTop, landZ + kRampEnvGrade * (L - flat - S[i])));
        }
        std::vector<float> hz(Z.begin(), Z.begin() + held);
        for (int pass = 0; pass < 2; pass++) {
            for (size_t i = 1; i < n; i++) Z[i] = Max(Z[i], Z[i - 1] - 0.07f * (S[i] - S[i - 1]));
            for (size_t i = n - 1; i-- > 0;) Z[i] = Max(Z[i], Z[i + 1] - 0.07f * (S[i + 1] - S[i]));
        }
        for (size_t i = 0; i < held; i++) Z[i] = hz[i];
        for (size_t i = held; i < n; i++)
            if (L - S[i] <= flat && Z[i] > landZ + 0.35f) return 1;
        onTop.assign(n, 0);
        for (size_t i = 0; i < n; i++) onTop[i] = i < held || Z[i] > zTop - 0.6f;
        return 0;
    }
    bool clear(int hwyPoly, int landPoly) {
        size_t n = P.size();
        float parallelRun = 0.f;
        int parallelPoly = -2;
        // from the landing back: most layouts that fail do so where they come down
        for (size_t j = 0; j < n; j++) {
            size_t i = n - 1 - j;
            vec2 p = P[i], dirv = normalize(P[Min(i + 1, n - 1)] - P[i > 0 ? i - 1 : 0]);
            if (i >= held) {
                if (L - S[i] <= flat + 2.f && map->isWater(p.x, p.y)) return false;
                if (gSites && gSites->blocksRoads(p)) return false;
                if (gSites && Z[i] - G[i] < kRampClear && gSites->padAt(p)) return false;   // low over a site's pad
            }
            bool landing = L - S[i] <= flat + 2.f;
            bool fail = false, alongStreet = false;
            int alongPoly = -2;
            query(p, kRampHalfWidth + 19.f, [&](const RSeg& s) {
                if (fail) return;
                float t, d = distPointSegment2D(p, s.a, s.b, &t);
                float dz = Z[i] - Lerp(s.za, s.zb, t);
                if (s.layer == 1 && s.poly == hwyPoly) {
                    // its own highway: held on it, or clear of its pavement
                    if (i >= held && d < kRampHalfWidth + s.hw - 1.f && fabsf(dz) > 0.5f) fail = true;
                    return;
                }
                if (d >= kRampHalfWidth + s.reach + (s.layer == 0 ? 1.f : 0.5f)) return;
                if (s.layer == 0 && s.poly == landPoly && landing) {
                    if (fabsf(dz) > 0.45f) fail = true;
                    return;
                }
                if (s.layer == -1 && fabsf(dz) < 0.45f && landing) return;   // the ramp sharing this landing
                if (onTop[i] && dz > 0.f) dz = Max(0.f, dz - 0.6f);   // over a road on the highway's level: allow for the estimate
                if (fabsf(dz) < kRampClear) {
                    fail = true;
                    return;
                }
                // cleared, but not by running on top of a road (its piers would stand in the lanes)
                vec2 sd = s.b - s.a;
                float sl = length(sd);
                if (s.layer == 0 && sl > 1e-3f && fabsf(dot(sd / sl, dirv)) > 0.85f) {
                    alongStreet = true;
                    alongPoly = s.poly;
                }
            });
            if (fail) return false;
            if (alongStreet && j > 0) {
                parallelRun = alongPoly == parallelPoly ? parallelRun + (S[i + 1] - S[i]) : 0.f;
                parallelPoly = alongPoly;
                if (parallelRun > 24.f) return false;
            } else {
                parallelRun = 0.f;
                parallelPoly = -2;
            }
        }
        return true;
    }
};

}  // namespace roads_detail

using namespace roads_detail;

// ---------------------------------------------------------------------------------------------
// Network generation
void RoadNetwork::generate(WorldMap& map) {
    double t0 = TimeSeconds();
    Builder b;
    b.map = &map;
    auto regionIs = [&](vec2 p, std::initializer_list<Region> regs) {
        Region r = map.regionAt(p.x, p.y);
        for (Region x : regs)
            if (r == x) return true;
        return false;
    };

    // ------------------------------------------------------------- Highways (grade separated)
    struct Hwy { std::vector<vec2> ctrl; const char* name; };
    std::vector<Hwy> hwys;
    hwys.push_back({kmPts({4.6f, 9.25f, 4.2f, 8.2f, 3.3f, 7.0f, 2.8f, 5.6f, 2.55f, 4.2f, 2.42f, 3.0f, 2.36f, 1.6f, 2.38f, 0.95f,
                           2.5f, 0.0f, 2.62f, -0.9f, 2.65f, -1.9f, 2.4f, -2.9f, 2.0f, -3.9f, 1.4f, -4.7f, 0.6f, -5.35f, 0.1f, -6.1f,
                           -0.3f, -6.55f}), "Interstate 38"});
    hwys.push_back({kmPts({-1.1f, 9.35f, -1.0f, 8.4f, -0.2f, 7.2f, -0.6f, 6.0f, -1.4f, 5.2f, -1.6f, 3.6f, -1.55f, 2.0f,
                           -1.7f, 0.2f, -1.8f, -1.6f, -1.5f, -3.2f, -0.9f, -4.4f, 0.2f, -5.1f, 0.6f, -5.35f}), "Palmera Turnpike"});
    {
        // Sol Expressway runs along the airport's south fence and ends on the Palmera Turnpike (T junction)
        std::vector<vec2> tp = smoothPath(hwys[1].ctrl, 30.f);
        float xT = -1680.f;
        for (size_t i = 0; i + 1 < tp.size(); i++)
            if ((tp[i].y - 470.f) * (tp[i + 1].y - 470.f) <= 0.f && fabsf(tp[i + 1].y - tp[i].y) > 1e-3f) {
                xT = Lerp(tp[i].x, tp[i + 1].x, (470.f - tp[i].y) / (tp[i + 1].y - tp[i].y));
                break;
            }
        hwys.push_back({{vec2(xT, 470.f), vec2(-800.f, 468.f), vec2(100.f, 470.f), vec2(820.f, 482.f), vec2(1180.f, 590.f), vec2(1480.f, 975.f),
                         vec2(2380.f, 950.f)},
                        "Sol Expressway"});
    }
    hwys.push_back({kmPts({-1.55f, 1.9f, -2.6f, 1.85f, -4.0f, 1.8f, -6.0f, 1.9f, -8.0f, 2.1f, -9.0f, 2.6f, -9.0f, 4.2f, -8.6f, 5.6f,
                           -7.8f, 5.8f}), "Sawgrass Expressway"});
    hwys.push_back({kmPts({-0.6f, 6.0f, 0.8f, 5.95f, 2.0f, 5.8f, 2.8f, 5.6f}), "Lake Connector"});
    // Keys highway continues from I-38's southern end along the key chain
    {
        std::vector<vec2> ctrl;
        vec2 A(-400, -6750), B(-4000, -9450), C(-8400, -9200);
        ctrl.push_back(vec2(-300, -6550));
        for (int i = 0; i <= 24; i++) {
            float t = 0.02f + 0.96f * i / 24.f;
            float u = 1.f - t;
            ctrl.push_back(A * (u * u) + B * (2.f * u * t) + C * (t * t));
        }
        hwys.push_back({ctrl, "Overseas Highway"});
    }
    std::vector<std::vector<vec2>> hwyPaths;
    for (auto& h : hwys) {
        std::vector<vec2> path = smoothPath(h.ctrl, 30.f);
        hwyPaths.push_back(path);
        b.add(path, RC_HIGHWAY, 1, 0, h.name);
    }

    // ------------------------------------------------------------- City core grid (N-S aligned, 100 m module)
    auto coreValid = [&](vec2 p) {
        Region r = map.regionAt(p.x, p.y);
        return r == REG_DOWNTOWN || r == REG_FINANCIAL || r == REG_MIDTOWN || r == REG_NORTH_CITY || r == REG_CALLE_LUNA ||
               r == REG_FLATS || r == REG_AIRPORT;
    };
    {
        const float x0 = -900.f, x1 = 4700.f, y0 = -2150.f, y1 = 5400.f;
        for (float x = x0; x <= x1; x += 100.f) {
            int ix = (int)lrintf(x / 100.f);
            bool major = ix % 6 == 0;
            RoadClass cls = major ? RC_BOULEVARD : (ix % 3 == 0 ? RC_AVENUE : RC_STREET);
            std::string name = major ? StrFormat("%s Boulevard", kStreetNames[(ix * 7 + 3) % ARRAY_COUNT(kStreetNames)])
                                     : StrFormat("%s Avenue", ordinal(Max(1, 48 - ix)).c_str());
            emitRuns(b, vec2(x, y0), vec2(x, y1), 25.f, cls, [&](vec2 p) {
                Region r = map.regionAt(p.x, p.y);
                if (!coreValid(p) || r == REG_AIRPORT || gSites->blocksRoads(p)) return false;
                // lower density grid in the flats/north: skip every other minor line
                if ((r == REG_FLATS || r == REG_NORTH_CITY) && cls == RC_STREET && (ix & 1)) return false;
                return true;
            }, major ? 160.f : 60.f, name.c_str());
        }
        for (float y = y0; y <= y1; y += 100.f) {
            int iy = (int)lrintf(y / 100.f);
            bool major = (iy % 5 == 0);
            RoadClass cls = major ? RC_BOULEVARD : (iy % 5 == 2 ? RC_AVENUE : RC_STREET);
            std::string name = major ? StrFormat("%s Boulevard", kStreetNames[(iy * 11 + 5) % ARRAY_COUNT(kStreetNames)])
                                     : StrFormat("%s %s Street", iy >= 0 ? "N" : "S", ordinal(Max(1, abs(iy))).c_str());
            emitRuns(b, vec2(x0, y), vec2(x1, y), 25.f, cls, [&](vec2 p) {
                Region r = map.regionAt(p.x, p.y);
                if (!coreValid(p) || r == REG_AIRPORT || gSites->blocksRoads(p)) return false;
                // Airport Boulevard (site road) continues this line into the airport from x = 1000
                if (iy == 15 && p.x < 999.f) return false;
                if ((r == REG_FLATS || r == REG_NORTH_CITY) && cls == RC_STREET && (iy & 1)) return false;
                return true;
            }, major ? 200.f : 60.f, name.c_str());
        }
        // Bayfront boulevard along the downtown shore
        std::vector<vec2> bay;
        float prevShore = -1.f;
        for (float y = -2000.f; y <= 4000.f; y += 25.f) {
            // find the shoreline x by marching east from x=3000
            float xs = 3000.f;
            for (float x = 3000.f; x < 4600.f; x += 10.f) {
                if (map.isWater(x, y)) { xs = x; break; }
                xs = x;
            }
            // a jump of the shoreline is a river mouth: the drive bridges it instead of following the river inland
            if (prevShore > 0.f && fabsf(xs - prevShore) > 150.f) xs = prevShore;
            prevShore = xs;
            bay.push_back(vec2(xs - 55.f, y));
        }
        // no jogs: a step in the shoreline eases in over ~100 m (a bridge approach through a kink would overlap itself)
        for (int pass = 0; pass < 4; pass++) {
            std::vector<vec2> sm = bay;
            for (size_t i = 1; i + 1 < bay.size(); i++) sm[i].x = (bay[i - 1].x + bay[i].x * 2.f + bay[i + 1].x) * 0.25f;
            bay.swap(sm);
        }
        b.add(smoothPath(bay, 25.f), RC_BOULEVARD, 0, 0, "Bayshore Boulevard");
    }

    // ------------------------------------------------------------- The Grove: warped grid with winding roads
    {
        auto grovePt = [&](float x, float y) {
            float wx = perlin2(x / 700.f, y / 700.f, 501) * 60.f, wy = perlin2(x / 700.f + 3.f, y / 700.f, 502) * 60.f;
            return vec2(x + wx, y + wy);
        };
        auto groveValid = [&](vec2 p) { return regionIs(p, {REG_GROVE}); };
        for (float x = 800.f; x <= 3900.f; x += 90.f) {
            std::vector<vec2> line;
            int ix = (int)lrintf(x / 90.f);
            RoadClass cls = (ix % 8 == 0) ? RC_AVENUE : RC_LANE;
            for (float y = -4800.f; y <= -900.f; y += 20.f) {
                vec2 p = grovePt(x, y);
                bool ok = groveValid(p) && !map.isWater(p.x, p.y);
                if (ok && cls == RC_LANE && hashToFloat(hash2i(ix, (int)floorf(y / 400.f))) < 0.18f) ok = false;  // T-junctions
                if (ok) line.push_back(p);
                else {
                    if (line.size() >= 3) b.add(line, cls, 0, 0, StrFormat("%s %s", kStreetNames[(ix * 5) % ARRAY_COUNT(kStreetNames)], kStreetSuffix[ix % 10]).c_str());
                    line.clear();
                }
            }
            if (line.size() >= 3) b.add(line, cls, 0, 0, StrFormat("%s %s", kStreetNames[(ix * 5) % ARRAY_COUNT(kStreetNames)], kStreetSuffix[ix % 10]).c_str());
        }
        for (float y = -4800.f; y <= -900.f; y += 150.f) {
            std::vector<vec2> line;
            int iy = (int)lrintf(y / 150.f);
            RoadClass cls = (iy % 5 == 0) ? RC_AVENUE : RC_LANE;
            for (float x = 800.f; x <= 3900.f; x += 20.f) {
                vec2 p = grovePt(x, y);
                bool ok = groveValid(p) && !map.isWater(p.x, p.y);
                if (ok) line.push_back(p);
                else {
                    if (line.size() >= 3) b.add(line, cls, 0, 0, StrFormat("%s %s", kStreetNames[(iy * 13 + 1) % ARRAY_COUNT(kStreetNames)], kStreetSuffix[(iy + 3) % 10]).c_str());
                    line.clear();
                }
            }
            if (line.size() >= 3) b.add(line, cls, 0, 0, StrFormat("%s %s", kStreetNames[(iy * 13 + 1) % ARRAY_COUNT(kStreetNames)], kStreetSuffix[(iy + 3) % 10]).c_str());
        }
        // Coastal scenic drive through the Grove
        b.add(smoothPath(kmPts({2.72f, -2.0f, 2.9f, -2.6f, 2.95f, -3.2f, 2.7f, -3.9f, 2.3f, -4.4f, 1.8f, -4.85f}), 25.f), RC_AVENUE, 0, 0,
              "Old Cutler Road");
    }

    // ------------------------------------------------------------- Suburbs: mile roads + curvy residential streets
    {
        auto subValid = [&](vec2 p) { return regionIs(p, {REG_SUBURBS, REG_REDLAND}); };
        // Mile roads (1600 m grid) across suburbs, Redland and farmland edges
        for (float x = -3200.f; x <= 1000.f; x += 1600.f)
            emitRuns(b, vec2(x, -6200.f), vec2(x, 5200.f), 25.f, RC_BOULEVARD, [&](vec2 p) { return subValid(p) || regionIs(p, {REG_FLATS}); }, 200.f,
                     StrFormat("%s Boulevard", ordinal((int)lrintf((1000.f - x) / 1600.f) * 10 + 67).c_str()).c_str());
        for (float y = -6400.f; y <= 5200.f; y += 1600.f)
            emitRuns(b, vec2(-3200.f, y), vec2(1000.f, y), 25.f, RC_BOULEVARD, subValid, 200.f,
                     StrFormat("%s Street", ordinal((int)lrintf((-y + 8000.f) / 1600.f) * 20 + 8).c_str()).c_str());
        // Residential streets inside each superblock
        for (float sx = -3200.f; sx < 1000.f; sx += 1600.f)
            for (float sy = -6400.f; sy < 5200.f; sy += 1600.f) {
                u32 h = hash2i((int)(sx / 1600.f), (int)(sy / 1600.f)) ^ 0x51u;
                bool vertical = (h & 1) != 0;
                float along = vertical ? 1600.f : 1600.f;
                float spacingA = 95.f + (h >> 1) % 30;   // between parallel streets
                float warp = 25.f + ((h >> 5) % 40);
                for (float o = spacingA; o < along - 40.f; o += spacingA) {
                    std::vector<vec2> line;
                    int li = (int)(o / spacingA);
                    for (float t = 30.f; t <= 1570.f; t += 20.f) {
                        vec2 p = vertical ? vec2(sx + o, sy + t) : vec2(sx + t, sy + o);
                        p += vec2(perlin2(p.x / 500.f, p.y / 500.f, 601), perlin2(p.x / 500.f + 5.f, p.y / 500.f, 602)) * warp;
                        bool ok = subValid(p) && !map.isWater(p.x, p.y);
                        // break some streets into cul-de-sacs
                        float bh = hashToFloat(hash3i(li, (int)(t / 400.f), (int)h));
                        if (bh < 0.12f && fmodf(t, 400.f) > 330.f) ok = false;
                        if (ok) line.push_back(p);
                        else {
                            if (line.size() >= 4) b.add(line, RC_LANE, 0, 0, StrFormat("%s %s", kStreetNames[(li * 3 + (h % 40)) % ARRAY_COUNT(kStreetNames)], kStreetSuffix[(li + h) % 10]).c_str());
                            line.clear();
                        }
                    }
                    if (line.size() >= 4) b.add(line, RC_LANE, 0, 0, StrFormat("%s %s", kStreetNames[(li * 3 + (h % 40)) % ARRAY_COUNT(kStreetNames)], kStreetSuffix[(li + h) % 10]).c_str());
                }
                // collector streets crossing the residential ones every ~400 m
                for (float o = 400.f; o < 1590.f; o += 400.f) {
                    std::vector<vec2> line;
                    for (float t = 10.f; t <= 1590.f; t += 20.f) {
                        vec2 p = vertical ? vec2(sx + t, sy + o) : vec2(sx + o, sy + t);
                        p += vec2(perlin2(p.x / 500.f, p.y / 500.f, 601), perlin2(p.x / 500.f + 5.f, p.y / 500.f, 602)) * warp;
                        bool ok = subValid(p) && !map.isWater(p.x, p.y);
                        if (ok) line.push_back(p);
                        else {
                            if (line.size() >= 4) b.add(line, RC_STREET, 0, 0, StrFormat("%s Drive", kStreetNames[(int)(o / 400.f + h) % ARRAY_COUNT(kStreetNames)]).c_str());
                            line.clear();
                        }
                    }
                    if (line.size() >= 4) b.add(line, RC_STREET, 0, 0, StrFormat("%s Drive", kStreetNames[(int)(o / 400.f + h) % ARRAY_COUNT(kStreetNames)]).c_str());
                }
            }
    }

    // ------------------------------------------------------------- Sol Beach avenues + cross streets
    {
        auto beachValid = [&](vec2 p) { return regionIs(p, {REG_BEACH}); };
        // Avenues at fixed distances from the ocean shore (east side)
        const float offsets[] = {75.f, 190.f, 320.f, 450.f};
        const char* names[] = {"Ocean Promenade", "Collins-Solano Avenue", "Washington Palms Avenue", "Bay Road"};
        for (int k = 0; k < 4; k++) {
            std::vector<vec2> line;
            for (float y = -2600.f; y <= 4400.f; y += 25.f) {
                float xe = 5500.f;
                for (float x = 5600.f; x > 4700.f; x -= 8.f) {
                    if (!map.isWater(x, y) && map.regionAt(x, y) == REG_BEACH) { xe = x; break; }
                }
                vec2 p(xe - offsets[k], y);
                if (beachValid(p) && !map.isWater(p.x, p.y) && xe < 5500.f) line.push_back(p);
                else {
                    if (line.size() >= 3) b.add(smoothPath(line, 25.f), k == 1 ? RC_AVENUE : RC_STREET, 0, 0, names[k]);
                    line.clear();
                }
            }
            if (line.size() >= 3) b.add(smoothPath(line, 25.f), k == 1 ? RC_AVENUE : RC_STREET, 0, 0, names[k]);
        }
        // cross streets stop short of site structures (the pier access ramp)
        auto crossValid = [&](vec2 p) { return beachValid(p) && !gSites->blocksRoads(p); };
        for (float y = -2550.f; y <= 4300.f; y += 95.f) {
            int iy = (int)lrintf(y / 95.f);
            emitRuns(b, vec2(4700.f, y), vec2(5500.f, y), 10.f, RC_STREET, crossValid, 0.f, StrFormat("%s Street", ordinal(Max(1, iy + 30)).c_str()).c_str());
        }
    }

    // ------------------------------------------------------------- Towns: small grids
    struct Town { Region reg; vec2 c; float sx, sy, ang, radius; };
    Town towns[] = {{REG_FORT_CASTELL, vec2(4500, 8050), 110, 90, 0.35f, 1350.f}, {REG_KEY_TOWN, vec2(-8000, -9300), 80, 70, 0.2f, 1000.f},
                    {REG_LAKE_TOWN, vec2(250, 6750), 90, 90, 0.f, 620.f},          {REG_HARLOW, vec2(-5000, 4850), 100, 85, 0.1f, 520.f},
                    {REG_GULF_TOWN, vec2(-8750, -2350), 85, 85, -0.3f, 560.f}};
    for (const Town& t : towns) {
        vec2 ax(cosf(t.ang), sinf(t.ang)), ay = perp(ax);
        auto valid = [&](vec2 p) {
            if (map.regionAt(p.x, p.y) != t.reg) return false;
            vec2 d = p - t.c;
            float ang = atan2f(d.y, d.x);
            float r = t.radius * (0.72f + 0.28f * (perlin2(cosf(ang) * 1.7f + t.reg, sinf(ang) * 1.7f, 700) + 0.5f));
            return length(d) < r;
        };
        // A grid line running alongside a section-line county road (below) a few metres off would put its junctions right
        // next to the county road's (two stop-controlled nodes 20 m apart gridlock): leave such lines out of the town.
        bool aligned = fabsf(t.ang) < 0.02f;
        auto besideSection = [&](float v, bool column) {
            if (!aligned) return false;
            for (float q = column ? -6400.f : 4000.f; q <= (column ? 4800.f : 9600.f); q += 1600.f) {
                if (column && hashToFloat(hash2i((int)q, 77)) < 0.2f) continue;   // (section roads left out)
                float dd = fabsf(v - q);
                if (dd > 1.f && dd < 40.f) return true;
            }
            return false;
        };
        for (int i = -14; i <= 14; i++) {
            vec2 o = t.c + ax * (i * t.sx);
            RoadClass cls = (i == 0) ? RC_AVENUE : RC_STREET;
            if (i == 0 || !besideSection(o.x, true))
                emitRuns(b, o - ay * 1400.f, o + ay * 1400.f, 15.f, cls, valid, 0.f,
                         StrFormat("%s %s", kStreetNames[(i + 20 + t.reg * 3) % ARRAY_COUNT(kStreetNames)], i == 0 ? "Main Street" : "Street").c_str());
            vec2 o2 = t.c + ay * (i * t.sy);
            if (i == 0 || !besideSection(o2.y, false))
                emitRuns(b, o2 - ax * 1400.f, o2 + ax * 1400.f, 15.f, (i == 0) ? RC_AVENUE : RC_STREET, valid, 0.f,
                         StrFormat("%s Avenue", ordinal(Max(1, i + 15)).c_str()).c_str());
        }
    }

    // ------------------------------------------------------------- Causeways and bridges (at-grade network, bridged over water)
    b.add(smoothPath(kmPts({3.72f, 0.72f, 4.1f, 0.8f, 4.5f, 0.72f, 4.9f, 0.52f, 5.05f, 0.5f}), 20.f), RC_BOULEVARD, 0, RF_BRIDGE, "Solano Causeway");
    b.add(smoothPath(kmPts({3.8f, 1.75f, 4.3f, 1.75f, 4.95f, 1.72f}), 20.f), RC_STREET, 0, RF_BRIDGE, "Venetia Causeway");
    b.add(smoothPath(kmPts({3.9f, 3.3f, 4.4f, 3.32f, 5.0f, 3.3f}), 20.f), RC_BOULEVARD, 0, RF_BRIDGE, "Flamingo Causeway");
    b.add(smoothPath(kmPts({3.45f, -2.55f, 3.8f, -2.75f, 4.15f, -3.05f, 4.4f, -3.35f}), 20.f), RC_AVENUE, 0, RF_BRIDGE, "Coral Key Causeway");
    b.add(smoothPath(kmPts({3.7f, -0.15f, 3.85f, -0.166f, 4.06f, -0.16f}), 20.f), RC_AVENUE, 0, RF_BRIDGE, "Port Isle Bridge");
    b.add(smoothPath(kmPts({5.1f, 4.3f, 5.02f, 4.6f, 4.8f, 4.95f, 4.5f, 5.1f}), 20.f), RC_AVENUE, 0, RF_BRIDGE, "Inlet Bridge");
    // Bay island access roads
    b.add(smoothPath(kmPts({4.2f, 0.8f, 4.28f, 1.1f, 4.3f, 1.35f}), 15.f), RC_LANE, 0, RF_BRIDGE, "Isla Estrella Drive");
    b.add(smoothPath(kmPts({4.3f, 1.75f, 4.36f, 1.95f, 4.38f, 2.2f}), 15.f), RC_LANE, 0, RF_BRIDGE, "Palm Isle Drive");
    b.add(smoothPath(kmPts({4.42f, 3.3f, 4.45f, 3.05f, 4.45f, 2.8f}), 15.f), RC_LANE, 0, RF_BRIDGE, "Tarpon Isle Drive");

    // ------------------------------------------------------------- Rural roads
    {
        auto ruralValid = [&](vec2 p) {
            Region r = map.regionAt(p.x, p.y);
            return r == REG_FARMLAND || r == REG_REDLAND || r == REG_HARLOW || r == REG_LAKE_TOWN || r == REG_FORT_CASTELL;
        };
        // Section-line farm roads every 1600 m, with some missing
        for (float x = -6400.f; x <= 4800.f; x += 1600.f) {
            if (hashToFloat(hash2i((int)x, 77)) < 0.2f) continue;
            emitRuns(b, vec2(x, 3800.f), vec2(x, 9800.f), 25.f, RC_RURAL, ruralValid, 120.f, StrFormat("County Road %d", 800 + (int)(x / 160.f)).c_str());
        }
        for (float y = 4000.f; y <= 9600.f; y += 1600.f)
            emitRuns(b, vec2(-6600.f, y), vec2(5800.f, y), 25.f, RC_RURAL, ruralValid, 120.f, StrFormat("County Road %d", 700 + (int)(y / 160.f)).c_str());
        // Farm tracks between fields (dirt)
        for (float x = -6000.f; x <= 4400.f; x += 800.f)
            if (hashToFloat(hash2i((int)x, 91)) < 0.5f)
                emitRuns(b, vec2(x, 4200.f), vec2(x, 9500.f), 25.f, RC_DIRT, ruralValid, 0.f, "Farm Track");
        // Lakeshore road around Lake Okahatchee
        {
            std::vector<vec2> ring;
            for (int i = 0; i <= 96; i++) {
                float a = kTwoPi * i / 96.f;
                vec2 p = vec2(-2400, 7000) + vec2(cosf(a) * 2000.f, sinf(a) * 1500.f) * 1.16f;
                p = vec2(-2400, 7000) + rotate(p - vec2(-2400, 7000), atan2f(0.25f, 1.f));
                if (!map.isWater(p.x, p.y) && map.regionAt(p.x, p.y) != REG_RIDGE) ring.push_back(p);
                else {
                    if (ring.size() > 3) b.add(ring, RC_RURAL, 0, 0, "Lakeshore Drive");
                    ring.clear();
                }
            }
            if (ring.size() > 3) b.add(ring, RC_RURAL, 0, 0, "Lakeshore Drive");
        }
        // Old Trail road along the canal through the Sawgrass
        b.add(smoothPath(kmPts({-9.3f, -0.05f, -7.0f, 0.02f, -5.0f, 0.1f, -3.6f, 0.25f, -2.9f, 0.45f, -2.2f, 0.5f, -1.7f, 0.45f}), 25.f), RC_RURAL, 0, 0,
              "Old Trail");
        // Ten Palms access and gulf coast road
        b.add(smoothPath(kmPts({-8.75f, -2.35f, -8.8f, -1.5f, -9.0f, -0.6f, -9.1f, -0.05f}), 25.f), RC_RURAL, 0, 0, "Gulf Coast Road");
        // Wetland levee road on the eastern edge of the Sawgrass
        b.add(smoothPath(kmPts({-2.95f, -5.2f, -2.95f, -3.0f, -2.95f, -1.0f, -2.95f, 1.0f, -2.95f, 3.0f, -2.9f, 4.3f}), 25.f), RC_RURAL, 0, 0, "Levee Road");
        // Airboat tracks (dirt) through the marsh
        b.add(smoothPath(kmPts({-5.0f, 0.1f, -5.2f, -1.2f, -5.8f, -2.4f, -6.2f, -3.6f}), 25.f), RC_DIRT, 0, 0, "Gator Trail");
        // Ridge mountain roads (winding)
        {
            std::vector<vec2> ctrl = kmPts({-5.0f, 4.85f, -5.6f, 5.3f, -6.1f, 5.6f, -6.4f, 6.2f, -6.9f, 6.6f, -7.3f, 7.1f, -7.0f, 7.6f,
                                            -7.4f, 8.1f, -8.0f, 8.4f, -8.6f, 8.8f});
            b.add(smoothPath(ctrl, 20.f), RC_RURAL, 0, 0, "Cypress Ridge Road");
            b.add(smoothPath(kmPts({-7.8f, 5.8f, -7.6f, 6.4f, -7.3f, 7.1f}), 20.f), RC_RURAL, 0, 0, "Summit Road");
            b.add(smoothPath(kmPts({-5.6f, 5.3f, -5.0f, 5.9f, -4.4f, 6.3f, -3.9f, 6.35f}), 20.f), RC_RURAL, 0, 0, "Spring Creek Road");
        }
        // Keys local roads (Key Solano has its own grid; others get a spine)
        // Fort Castell coast road
        b.add(smoothPath(kmPts({2.8f, 5.6f, 3.4f, 6.2f, 4.0f, 6.9f, 4.4f, 7.6f, 4.9f, 8.4f, 5.3f, 9.0f}), 25.f), RC_AVENUE, 0, 0, "Castell Coast Road");
        // Connection Harlow - Turnpike
        b.add(smoothPath(kmPts({-5.0f, 4.85f, -4.0f, 4.9f, -2.8f, 5.0f, -1.5f, 5.2f}), 25.f), RC_RURAL, 0, 0, "Harlow Road");
    }

    // ------------------------------------------------------------- Site roads (airport loop + perimeter, Port Isle, Key Coral, access roads)
    for (const SiteRoad& r : gSites->roads) b.add(r.pts, (RoadClass)r.cls, r.layer, r.flags, r.name.c_str());

    // ------------------------------------------------------------- Interchange ramps (highway <-> arterials)
    // Diamond interchanges where a highway crosses a boulevard or a rural road. On each side of the highway an exit ramp
    // leaves it upstream of the crossing and an entrance ramp joins it downstream (traffic keeps right); each runs
    // alongside the highway and swings out to land on the arterial at grade, the two sharing one junction. The layout of
    // every ramp (where it leaves the highway, where it swings out, how far along the arterial it lands) is searched
    // against the estimated heights of everything around it (RampPlanner): it has to come down from the highway at 7% at
    // most and clear every road it passes, or that ramp is left out; a crossing where no ramp fits leaves the interchange
    // to the next crossing.
    {
        RampPlanner plan;
        plan.map = &map;
        for (size_t pi = 0; pi < b.polys.size(); pi++) {
            const PolyIn& p = b.polys[pi];
            if (p.layer != 0) continue;
            const RoadClassInfo& ri = roadInfo(p.cls);
            float hw = ri.laneWidth * ((p.flags & RF_ONEWAY) ? ri.lanes : ri.lanes * 2) * 0.5f + ri.median * 0.5f + ri.shoulder;
            for (size_t i = 0; i + 1 < p.pts.size(); i++)
                plan.insert({p.pts[i], p.pts[i + 1], plan.gradeZ(p.pts[i]), plan.gradeZ(p.pts[i + 1]), hw, hw + ri.sidewalk, (int)pi, 0});
        }
        std::vector<std::vector<vec2>> hwD(hwyPaths.size());
        std::vector<std::vector<float>> hwS(hwyPaths.size()), hwZ(hwyPaths.size());
        for (size_t h = 0; h < hwyPaths.size(); h++) plan.addHighway(hwyPaths[h], (int)h, hwD[h], hwS[h], hwZ[h]);
        // point and direction on highway h at arc length s
        auto hwyAt = [&](size_t h, float s, vec2& p, vec2& t) {
            const std::vector<float>& S = hwS[h];
            size_t k = (size_t)(std::upper_bound(S.begin(), S.end(), s) - S.begin());
            k = Clamp(k, (size_t)1, S.size() - 1);
            float seg = S[k] - S[k - 1];
            float u = seg > 1e-4f ? Saturate((s - S[k - 1]) / seg) : 0.f;
            p = lerp(hwD[h][k - 1], hwD[h][k], u);
            t = normalize(hwD[h][k] - hwD[h][k - 1]);
        };
        struct ASeg { int poly, k; };
        std::vector<ASeg> arterialSegs;
        for (size_t pi = 0; pi < b.polys.size(); pi++) {
            const PolyIn& p = b.polys[pi];
            if (p.layer == 0 && (p.cls == RC_BOULEVARD || p.cls == RC_RURAL))
                for (size_t i = 0; i + 1 < p.pts.size(); i++) arterialSegs.push_back({(int)pi, (int)i});
        }
        // the point G metres along the arterial from the crossing (walking toward adSign), and the arterial's direction there
        auto arterialPoint = [&](const ASeg& as, float tb, int adSign, float G, vec2& PL, vec2& tL) {
            const std::vector<vec2>& pts = b.polys[as.poly].pts;
            vec2 cur = lerp(pts[as.k], pts[as.k + 1], tb);
            int k = adSign > 0 ? as.k + 1 : as.k;
            float left = G;
            while (k >= 0 && k < (int)pts.size()) {
                float d = length(pts[k] - cur);
                if (d >= left && d > 1e-4f) {
                    tL = (pts[k] - cur) / d;
                    PL = cur + tL * left;
                    return true;
                }
                left -= d;
                cur = pts[k];
                k += adSign;
            }
            return false;
        };
        // a landing junction needs its own stretch of the arterial: clear of the highway deck, of other junctions and ramps
        auto landingOk = [&](vec2 PL, int apoly, float landR, int hpoly) {
            if (map.isWater(PL.x, PL.y) || gSites->blocksRoads(PL)) return false;
            // within ~90 m of the highway: a ramp does not wander off through the neighbourhood
            float dh = 1e9f;
            plan.query(PL, 100.f, [&](const RampPlanner::RSeg& s) {
                if (s.layer == 1 && s.poly == hpoly) dh = Min(dh, distPointSegment2D(PL, s.a, s.b));
            });
            if (dh > 92.f) return false;
            bool ok = true;
            plan.query(PL, 60.f, [&](const RampPlanner::RSeg& s) {
                if (!ok || s.poly == apoly) return;
                float d = distPointSegment2D(PL, s.a, s.b);
                if (s.layer == 1 && d < s.hw + 0.35f + landR + 4.f) ok = false;   // the junction disc clear of the deck
                if (s.layer == 0 && d < landR + Max(landR, s.hw + (s.reach - s.hw) * 0.3f + 2.f) + 2.f) ok = false;   // and of the next junction's
                if (s.layer < 0 && d > 1.f && d < 2.f * landR + 2.f) ok = false;   // another ramp: the same junction, or its own
            });
            return ok;
        };
        // ramp path from its highway end (on the centreline A metres from the crossing: the node where it joins) tapering
        // out across the outer lanes to run alongside the highway (in the direction of its traffic) to station S, then a
        // cubic swing out to a straight 25 m approach arriving square to the arterial at PL; ~10 m pieces
        auto buildPath = [&](size_t h, float sx, int sgn, int dir, float A, float S, vec2 PL, vec2 nArt) {
            std::vector<vec2> rp;
            vec2 H, t;
            hwyAt(h, sx + dir * A, H, t);
            rp.push_back(H);   // on the highway centreline, where the planariser joins it
            const float taper[] = {7.f, 11.5f, 14.5f, 16.3f, 17.4f};
            for (int k = 0; k < 5; k++) {
                hwyAt(h, sx + dir * (A - 10.f * (k + 1)), H, t);
                rp.push_back(H + perp(t) * (sgn * taper[k]));
            }
            // alongside, clear of the highway's deck
            float a0 = A - 60.f;
            int np = Max(1, (int)ceilf((a0 - S) / 10.f));
            for (int k = 0; k <= np; k++) {
                float a = a0 - (a0 - S) * k / np;
                hwyAt(h, sx + dir * a, H, t);
                rp.push_back(H + perp(t) * (sgn * kRampOffset));
            }
            vec2 B0 = rp.back(), T0 = t * (float)-dir, Q = PL + nArt * 25.f, T1 = -nArt;
            float chord = length(Q - B0), hl = chord * 0.45f;
            vec2 c1 = B0 + T0 * hl, c2 = Q - T1 * hl;
            int nb = Max(2, (int)ceilf(chord / 10.f));
            for (int k = 1; k <= nb; k++) {
                float u = (float)k / nb, v = 1.f - u;
                rp.push_back(B0 * (v * v * v) + c1 * (3.f * v * v * u) + c2 * (3.f * v * u * u) + Q * (u * u * u));
            }
            for (int k = 1; k <= 3; k++) rp.push_back(lerp(Q, PL, k / 3.f));
            return rp;
        };
        // past the node on the highway, no bend tighter than a 40 m radius
        auto smoothEnough = [&](const std::vector<vec2>& rp) {
            for (size_t k = 2; k + 1 < rp.size(); k++) {
                vec2 u = rp[k] - rp[k - 1], v = rp[k + 1] - rp[k];
                float lu = length(u), lv = length(v);
                if (lu < 1e-3f || lv < 1e-3f) return false;
                float ang = acosf(Clamp(dot(u, v) / (lu * lv), -1.f, 1.f));
                if (ang > 0.5f * (lu + lv) / 40.f) return false;
            }
            return true;
        };
        struct RampPlan {
            std::vector<vec2> path, P;
            std::vector<float> Z;
            bool ok = false;
        };
        // landing distances along the arterial: ~70 m preferred, closer in or further out where the ramps need the room
        std::vector<float> kLandings;
        for (float G = 34.f; G <= 200.f; G += 4.f) kLandings.push_back(G);
        std::stable_sort(kLandings.begin(), kLandings.end(), [](float a, float c) { return fabsf(a - 70.f) < fabsf(c - 70.f); });
        std::vector<vec2> attaches;   // where planned ramps join the highways
        auto planRamp = [&](size_t h, float sx, int sgn, int dir, vec2 PL, vec2 tL, int apoly, float landR, RampPlan& out) {
            vec2 hp, hd;
            hwyAt(h, sx, hp, hd);
            vec2 nArt = perp(tL);
            if (dot(nArt, hd * (float)dir) < 0.f) nArt = -nArt;
            float sQ = dot(PL + nArt * 25.f - hp, hd) * dir;   // station where the square approach starts
            // swing out at station S; the ramp leaves the highway as close to it as lets it come down in time
            for (float S = Max(sQ + 25.f, 30.f); S <= 330.f; S += 20.f) {
                // the swing has to be gentle (a longer run alongside does not change it)
                if (!smoothEnough(buildPath(h, sx, sgn, dir, S + 80.f, S, PL, nArt))) continue;
                for (float A = S + 80.f; A <= 660.f; A += 20.f) {
                    float sA = sx + dir * A;
                    if (sA < 80.f || sA > hwS[h].back() - 80.f) break;
                    vec2 HA, tA;
                    hwyAt(h, sA, HA, tA);
                    bool clash = false;
                    for (vec2 q : attaches) clash |= length(q - HA) > 1.f && length(q - HA) < 90.f;   // one shared node, or well apart
                    // and well clear of another highway (a junction of two highways blends their decks there)
                    plan.query(HA, 250.f, [&](const RampPlanner::RSeg& o) {
                        if (o.layer == 1 && o.poly != (int)h && distPointSegment2D(HA, o.a, o.b) < 250.f) clash = true;
                    });
                    if (clash) continue;
                    std::vector<vec2> rp = buildPath(h, sx, sgn, dir, A, S, PL, nArt);
                    if (!smoothEnough(rp)) continue;
                    int pr = plan.profile(rp, (int)h, apoly, landR);
                    if (pr == 1) continue;   // too short: leave the highway further out
                    if (pr == 2) break;
                    if (plan.clear((int)h, apoly)) {
                        out.path = rp;
                        out.P = plan.P;
                        out.Z = plan.Z;
                        out.ok = true;
                        return;
                    }
                    break;   // what it runs into does not go away with a longer run alongside
                }
            }
        };
        std::vector<vec2> interchanges;
        int planned = 0, dropped = 0;
        for (size_t h = 0; h < hwyPaths.size(); h++) {
            const auto& path = hwyPaths[h];
            float lastIc = -1e9f, acc = 0.f;
            for (size_t i = 0; i + 1 < path.size(); i++) {
                float segLen = length(path[i + 1] - path[i]), acc0 = acc;
                acc += segLen;
                for (const ASeg& as : arterialSegs) {
                    const PolyIn& ap = b.polys[as.poly];
                    float ta, tb;
                    if (!segmentIntersect2D(path[i], path[i + 1], ap.pts[as.k], ap.pts[as.k + 1], &ta, &tb)) continue;
                    if (acc - lastIc < 1300.f) continue;
                    vec2 x = lerp(path[i], path[i + 1], ta);
                    if (map.isWater(x.x, x.y)) continue;
                    float sx = acc0 + ta * segLen;
                    // none right at a highway's ends (the Sawgrass Expressway starts on the turnpike and ends on the ridge
                    // roads; the Sol Expressway starts on the turnpike too, with its first crossings in the junction's reach)
                    float endGap = strcmp(hwys[h].name, "Sol Expressway") == 0 ? 600.f : 300.f;
                    if (sx < endGap || sx > hwS[h].back() - 300.f) continue;
                    vec2 hp, hd;
                    hwyAt(h, sx, hp, hd);
                    vec2 side = perp(hd), ad = normalize(ap.pts[as.k + 1] - ap.pts[as.k]);
                    // the landing junction's radius, as the intersection pass will size it
                    const RoadClassInfo& ari = roadInfo(ap.cls);
                    Region xr = map.regionAt(x.x, x.y);
                    bool town = isUrbanRegion(xr) || xr == REG_SUBURBS || xr == REG_GROVE || xr == REG_BAY_ISLAND || xr == REG_KEY_CORAL;
                    float landR = ari.laneWidth * ari.lanes + ari.median * 0.5f + ari.shoulder + (town ? ari.sidewalk * 0.3f : 0.f) + 2.f;
                    int got = 0, left = 0;
                    for (int sgn = -1; sgn <= 1; sgn += 2) {
                        int adSign = dot(ad, side) * sgn > 0.f ? 1 : -1;
                        // both ramps of this side at one junction where they fit (the landing ~70 m out preferred, closer in
                        // or further out where they need the room); otherwise each at a landing of its own
                        RampPlan pair[2], first;
                        int firstDir = 0;
                        std::vector<float> okOther;   // landings where the other ramp fitted on its own
                        for (float G : kLandings) {
                            vec2 PL, tL;
                            if (!arterialPoint(as, tb, adSign, G, PL, tL) || !landingOk(PL, as.poly, landR, (int)h)) continue;
                            RampPlan cur[2];
                            for (int d = 0; d < 2; d++) planRamp(h, sx, sgn, d == 0 ? -1 : 1, PL, tL, as.poly, landR, cur[d]);
                            if (cur[0].ok && cur[1].ok) {
                                pair[0] = std::move(cur[0]);
                                pair[1] = std::move(cur[1]);
                                break;
                            }
                            for (int d = 0; d < 2; d++) {
                                if (!cur[d].ok) continue;
                                if (!first.ok) {
                                    first = std::move(cur[d]);
                                    firstDir = d == 0 ? -1 : 1;
                                } else if ((d == 0 ? -1 : 1) != firstDir) okOther.push_back(G);
                            }
                        }
                        auto emit = [&](RampPlan& pl, int dir) {
                            bool exitRamp = dir == sgn;   // upstream of the crossing for this side's traffic
                            std::vector<vec2> pts = pl.path;
                            if (!exitRamp) std::reverse(pts.begin(), pts.end());
                            std::string name = ap.name.empty() ? std::string("Ramp") : ap.name + (exitRamp ? " Exit" : " Entrance");
                            b.add(pts, RC_RAMP, -1, RF_ONEWAY, name.c_str());
                            b.polys.back().attachEnd = exitRamp ? 0 : 1;
                            for (size_t k = 0; k + 1 < pl.P.size(); k++)
                                plan.insert({pl.P[k], pl.P[k + 1], pl.Z[k], pl.Z[k + 1], kRampHalfWidth, kRampHalfWidth, -1, -1});
                            attaches.push_back(pl.path.front());
                            got++;
                        };
                        if (pair[0].ok) {
                            emit(pair[0], -1);
                            emit(pair[1], 1);
                        } else if (first.ok) {
                            emit(first, firstDir);
                            // the other one at a junction of its own, clear of the first
                            RampPlan other;
                            for (float G : okOther) {
                                vec2 PL, tL;
                                if (!arterialPoint(as, tb, adSign, G, PL, tL) || !landingOk(PL, as.poly, landR, (int)h)) continue;
                                planRamp(h, sx, sgn, -firstDir, PL, tL, as.poly, landR, other);
                                if (other.ok) break;
                            }
                            if (other.ok) emit(other, -firstDir);
                            else left++;
                        } else left += 2;
                    }
                    if (got == 0) continue;
                    planned += got;
                    dropped += left;
                    lastIc = acc;
                    interchanges.push_back(x);
                }
            }
        }
        LOG("Road gen: %zu interchanges, %d ramps (%d left out where they could not come down in time)", interchanges.size(), planned, dropped);
    }

    // ============================================================= Planarize
    // Collect segments per layer; find intersections within the same layer; endpoints snap across layers.
    std::vector<Seg> segs;
    std::vector<size_t> polySegs(b.polys.size() + 1, 0);   // each poly's segments: [polySegs[pi], polySegs[pi + 1])
    for (size_t pi = 0; pi < b.polys.size(); pi++) {
        const auto& p = b.polys[pi];
        size_t firstSeg = segs.size();
        polySegs[pi] = firstSeg;
        for (size_t i = 0; i + 1 < p.pts.size(); i++) {
            if (length2(p.pts[i + 1] - p.pts[i]) < 0.01f) continue;
            Seg s;
            s.a = p.pts[i];
            s.b = p.pts[i + 1];
            s.poly = (int)pi;
            segs.push_back(s);
        }
        if (segs.size() > firstSeg) {
            segs[firstSeg].first = true;
            segs.back().last = true;
        }
    }
    polySegs[b.polys.size()] = segs.size();
    const float H = 40.f;
    int hres = (int)(2.f * kWorldHalf / H) + 1;
    std::unordered_map<long long, std::vector<int>> segHash;
    auto hkey = [&](int x, int y) { return (long long)y * hres + x; };
    for (size_t i = 0; i < segs.size(); i++) {
        vec2 mn = vmin(segs[i].a, segs[i].b), mx = vmax(segs[i].a, segs[i].b);
        int x0 = (int)((mn.x + kWorldHalf) / H), x1 = (int)((mx.x + kWorldHalf) / H);
        int y0 = (int)((mn.y + kWorldHalf) / H), y1 = (int)((mx.y + kWorldHalf) / H);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) segHash[hkey(x, y)].push_back((int)i);
    }
    // Intersections (same layer >= 0)
    for (size_t i = 0; i < segs.size(); i++) {
        const PolyIn& pa = b.polys[segs[i].poly];
        vec2 mn = vmin(segs[i].a, segs[i].b), mx = vmax(segs[i].a, segs[i].b);
        int x0 = (int)((mn.x + kWorldHalf) / H), x1 = (int)((mx.x + kWorldHalf) / H);
        int y0 = (int)((mn.y + kWorldHalf) / H), y1 = (int)((mx.y + kWorldHalf) / H);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                auto it = segHash.find(hkey(x, y));
                if (it == segHash.end()) continue;
                for (int j : it->second) {
                    if (j <= (int)i) continue;
                    const PolyIn& pb = b.polys[segs[j].poly];
                    if (segs[j].poly == segs[i].poly) continue;
                    if (pa.layer < 0 || pb.layer < 0 || pa.layer != pb.layer) continue;
                    float ta, tb;
                    if (segmentIntersect2D(segs[i].a, segs[i].b, segs[j].a, segs[j].b, &ta, &tb)) {
                        segs[i].splits.push_back(ta);
                        segs[j].splits.push_back(tb);
                    }
                }
            }
    }
    // Endpoint snapping (T-junctions): polyline endpoints lying near another segment split it
    std::vector<size_t> moved;
    for (size_t pi = 0; pi < b.polys.size(); pi++) {
        const PolyIn& p = b.polys[pi];
        for (int end = 0; end < 2; end++) {
            vec2 e = end == 0 ? p.pts.front() : p.pts.back();
            int cx = (int)((e.x + kWorldHalf) / H), cy = (int)((e.y + kWorldHalf) / H);
            float bestD = p.layer < 0 ? 25.f : 14.f;
            int bestSeg = -1;
            float bestT = 0;
            for (int y = cy - 1; y <= cy + 1; y++)
                for (int x = cx - 1; x <= cx + 1; x++) {
                    auto it = segHash.find(hkey(x, y));
                    if (it == segHash.end()) continue;
                    for (int j : it->second) {
                        if (segs[j].poly == (int)pi) continue;
                        const PolyIn& q = b.polys[segs[j].poly];
                        // ramps connect to anything; others only within the same layer, and never onto a ramp (a street
                        // hooked into a ramp beside the highway leaves no room to meet it at grade)
                        if (p.layer >= 0 && q.layer >= 0 && p.layer != q.layer) continue;
                        if (p.layer < 0 && q.layer < 0) continue;
                        if (p.layer >= 0 && q.layer < 0) continue;
                        // a planned ramp joins its highway at one end and lands on an at-grade road at the other
                        if (p.layer < 0 && p.attachEnd >= 0 && (end == p.attachEnd) != (q.layer == 1)) continue;
                        // ramp end at ground connects to at-grade roads, start connects to highway: choose nearest
                        float t;
                        float d = distPointSegment2D(e, segs[j].a, segs[j].b, &t);
                        if (d < bestD) { bestD = d; bestSeg = j; bestT = t; }
                    }
                }
            if (bestSeg >= 0) {
                segs[bestSeg].splits.push_back(bestT);
                // move the polyline endpoint onto the segment
                vec2 snap = lerp(segs[bestSeg].a, segs[bestSeg].b, bestT);
                // find our own segment that has this endpoint and adjust it
                for (size_t si = polySegs[pi]; si < polySegs[pi + 1]; si++) {
                    Seg& s = segs[si];
                    bool hit = false;
                    if (end == 0 && length2(s.a - e) < 1e-4f) { s.a = snap; hit = true; }
                    if (end == 1 && length2(s.b - e) < 1e-4f) { s.b = snap; hit = true; }
                    if (hit) moved.push_back(si);
                }
            }
        }
    }
    // A snapped end moves its last piece by up to 14 m: where that now crosses another road of its layer, split both there
    // too (else the two cross without a junction)
    for (size_t si : moved) {
        const PolyIn& pa = b.polys[segs[si].poly];
        if (pa.layer < 0) continue;
        vec2 mn = vmin(segs[si].a, segs[si].b), mx = vmax(segs[si].a, segs[si].b);
        int x0 = (int)((mn.x + kWorldHalf) / H), x1 = (int)((mx.x + kWorldHalf) / H);
        int y0 = (int)((mn.y + kWorldHalf) / H), y1 = (int)((mx.y + kWorldHalf) / H);
        for (int y = y0 - 1; y <= y1 + 1; y++)
            for (int x = x0 - 1; x <= x1 + 1; x++) {
                auto it = segHash.find(hkey(x, y));
                if (it == segHash.end()) continue;
                for (int j : it->second) {
                    if (segs[j].poly == segs[si].poly || b.polys[segs[j].poly].layer != pa.layer) continue;
                    float ta, tb;
                    if (segmentIntersect2D(segs[si].a, segs[si].b, segs[j].a, segs[j].b, &ta, &tb)) {
                        segs[si].splits.push_back(ta);
                        segs[j].splits.push_back(tb);
                    }
                }
            }
    }
    // Build nodes with merging
    std::unordered_map<long long, std::vector<int>> nodeHash;
    const float NH = 8.f;
    int nres = (int)(2.f * kWorldHalf / NH) + 2;
    auto nkey = [&](int x, int y) { return (long long)y * nres + x; };
    std::vector<vec2> npos;
    std::vector<int> nlayer;  // layer of node: 0 street, 1 highway, -1 connector end, -2 connector interior (for merging rules)
    std::vector<int> npoly;
    auto getNode = [&](vec2 p, int layer, int poly) -> int {
        int cx = (int)((p.x + kWorldHalf) / NH), cy = (int)((p.y + kWorldHalf) / NH);
        float mergeR = 3.5f;
        // the nearest node within reach (two roads' crossing points must land on the same node, not each on a vertex of
        // its own road nearby)
        int best = -1;
        float bestD2 = mergeR * mergeR;
        for (int y = cy - 1; y <= cy + 1; y++)
            for (int x = cx - 1; x <= cx + 1; x++) {
                auto it = nodeHash.find(nkey(x, y));
                if (it == nodeHash.end()) continue;
                for (int n : it->second) {
                    float d2 = length2(npos[n] - p);
                    if (d2 >= bestD2) continue;
                    // a connector's interior points only chain to each other: a ramp passing over a street corner does not
                    // join it
                    bool ok = (layer == -2 || nlayer[n] == -2) ? (layer == nlayer[n] && npoly[n] == poly)
                                                               : (nlayer[n] == layer || layer < 0 || nlayer[n] < 0);
                    if (!ok) continue;
                    best = n;
                    bestD2 = d2;
                }
            }
        if (best >= 0) return best;
        int id = (int)npos.size();
        npos.push_back(p);
        nlayer.push_back(layer);
        npoly.push_back(poly);
        nodeHash[nkey(cx, cy)].push_back(id);
        return id;
    };
    // Crossing and T-junction points first, so both roads' split points (and their own vertices close by) land on one node
    for (auto& s : segs)
        for (float t : s.splits)
            if (t > 1e-4f && t < 1.f - 1e-4f) getNode(lerp(s.a, s.b, t), b.polys[s.poly].layer, s.poly);
    struct Link { int a, b, poly; };
    std::vector<Link> links;
    for (auto& s : segs) {
        const PolyIn& p = b.polys[s.poly];
        std::sort(s.splits.begin(), s.splits.end());
        std::vector<float> ts;
        ts.push_back(0.f);
        for (float t : s.splits)
            if (t > 1e-4f && t < 1.f - 1e-4f) ts.push_back(t);
        ts.push_back(1.f);
        for (size_t k = 0; k + 1 < ts.size(); k++) {
            vec2 pa = lerp(s.a, s.b, ts[k]), pb = lerp(s.a, s.b, ts[k + 1]);
            int la = p.layer, lb = p.layer;
            if (p.layer < 0) {
                if (!(s.first && k == 0)) la = -2;
                if (!(s.last && k + 2 == ts.size())) lb = -2;
            }
            int na = getNode(pa, la, s.poly), nb = getNode(pb, lb, s.poly);
            if (na == nb) continue;
            links.push_back({na, nb, s.poly});
        }
    }
    // Deduplicate links
    {
        std::unordered_map<long long, int> seen;
        std::vector<Link> uniq;
        for (auto& l : links) {
            long long k = (long long)Min(l.a, l.b) * 100000000LL + Max(l.a, l.b);
            if (seen.count(k)) continue;
            seen[k] = 1;
            uniq.push_back(l);
        }
        links.swap(uniq);
    }
    // Adjacency
    std::vector<std::vector<int>> adj(npos.size());
    for (size_t i = 0; i < links.size(); i++) {
        adj[links[i].a].push_back((int)i);
        adj[links[i].b].push_back((int)i);
    }
    // Junction nodes: degree != 2, or the two links belong to different polylines of different class
    std::vector<bool> junction(npos.size(), false);
    for (size_t n = 0; n < npos.size(); n++) {
        if (adj[n].size() != 2) junction[n] = true;
        else {
            const PolyIn& p0 = b.polys[links[adj[n][0]].poly];
            const PolyIn& p1 = b.polys[links[adj[n][1]].poly];
            if (p0.cls != p1.cls || p0.layer != p1.layer || links[adj[n][0]].poly != links[adj[n][1]].poly) junction[n] = true;
        }
    }
    // Chain extraction
    nodes.clear();
    edges.clear();
    std::vector<int> nodeMap(npos.size(), -1);
    auto mapNode = [&](int n) {
        if (nodeMap[n] < 0) {
            nodeMap[n] = (int)nodes.size();
            RoadNode rn;
            rn.p = npos[n];
            rn.highway = nlayer[n] == 1;
            nodes.push_back(rn);
        }
        return nodeMap[n];
    };
    std::vector<bool> used(links.size(), false);
    for (size_t n = 0; n < npos.size(); n++) {
        if (!junction[n]) continue;
        for (int li : adj[n]) {
            if (used[li]) continue;
            std::vector<vec2> chain;
            chain.push_back(npos[n]);
            int cur = (int)n, link = li;
            int poly = links[li].poly;
            while (true) {
                used[link] = true;
                int next = links[link].a == cur ? links[link].b : links[link].a;
                chain.push_back(npos[next]);
                cur = next;
                if (junction[cur]) break;
                int nl = adj[cur][0] == link ? adj[cur][1] : adj[cur][0];
                if (used[nl]) break;
                link = nl;
            }
            if (chain.size() < 2) continue;
            const PolyIn& p = b.polys[poly];
            RoadEdge e;
            e.n0 = mapNode((int)n);
            e.n1 = mapNode(cur);
            if (e.n0 == e.n1 && chain.size() < 4) continue;
            e.cls = p.cls;
            e.flags = p.flags;
            e.name = p.name;
            for (auto& c : chain) e.pts.push_back(vec3(c.x, c.y, 0));
            // Oneway ramps must keep their authored direction: check order against the polyline
            if (p.flags & RF_ONEWAY) {
                // find index of first and last chain points in the polyline
                auto findIdx = [&](vec2 q) {
                    float best = 1e18f;
                    int bi = 0;
                    for (size_t k = 0; k < p.pts.size(); k++) {
                        float d = length2(p.pts[k] - q);
                        if (d < best) { best = d; bi = (int)k; }
                    }
                    return bi;
                };
                if (findIdx(chain.front()) > findIdx(chain.back())) {
                    std::reverse(e.pts.begin(), e.pts.end());
                    std::swap(e.n0, e.n1);
                }
            }
            edges.push_back(std::move(e));
        }
    }
    // Streets do not run out onto the beach: a dead end whose turning circle (with the sidewalk round it) would lie on the
    // sand is pulled back along its street until the circle stays clear of it, or, when too little street would be left,
    // the stretch goes back to the junction it hangs off. The way on to the water is a beach access path (SiteSet::finalize).
    // Roads to the water's own structures (boat ramps, marinas, piers, docks) and the stilt village's lanes stay as they are.
    beachEnds.clear();
    {
        std::vector<int> deg(nodes.size(), 0);
        for (auto& e : edges) { deg[e.n0]++; deg[e.n1]++; }
        std::vector<char> dropEdge(edges.size(), 0);
        auto sandy = [&](vec2 c, float r) {
            if (map.beachSand(c.x, c.y) >= 0.5f) return true;
            for (int k = 0; k < 8; k++) {
                float a = k * (kPi * 0.25f);
                if (map.beachSand(c.x + cosf(a) * r, c.y + sinf(a) * r) >= 0.5f) return true;
            }
            return false;
        };
        auto waterworks = [&](vec2 p) {
            if (!gSites) return false;
            for (const SiteElem& s : gSites->elems) {
                if (s.kind != SK_BOAT_RAMP && s.kind != SK_MARINA && s.kind != SK_RIVER_MARINA && s.kind != SK_DOCK && s.kind != SK_BEACH_PIER &&
                    s.kind != SK_BEACH_CLUB && s.kind != SK_FISH_SHACK && s.kind != SK_FERRY_PIER)
                    continue;
                if (length(s.c - p) < s.radius() + 60.f) return true;
            }
            return false;
        };
        // (a street dropped back to a junction can leave the next stretch inland hanging on the sand: a second pass)
        for (int pass = 0; pass < 3; pass++)
        for (size_t ei = 0; ei < edges.size(); ei++) {
            RoadEdge& e = edges[ei];
            if (dropEdge[ei] || e.cls == RC_HIGHWAY || e.cls == RC_RAMP || e.pts.size() < 2) continue;
            const RoadClassInfo& ri = roadInfo(e.cls);
            float hw = ri.laneWidth * ((e.flags & RF_ONEWAY) ? ri.lanes : ri.lanes * 2) * 0.5f + ri.median * 0.5f + ri.shoulder;
            float clearR = hw + 4.5f + ri.sidewalk + 1.f;   // turning circle (bulbRadius) and its sidewalk
            for (int end = 0; end < 2 && !dropEdge[ei]; end++) {
                int nd = end ? e.n1 : e.n0, inner = end ? e.n0 : e.n1;
                if (deg[nd] != 1 || nd == inner) continue;
                vec2 tip = nodes[nd].p;
                Region reg = map.regionAt(tip.x, tip.y);
                if (reg == REG_GULF_TOWN || reg == REG_SAWGRASS || !sandy(tip, clearR) || waterworks(tip)) continue;
                // the street from the dead end inward
                std::vector<vec2> q;
                for (size_t k = 0; k < e.pts.size(); k++) q.push_back(e.pts[end ? e.pts.size() - 1 - k : k].xy());
                std::vector<float> cum(q.size(), 0.f);
                for (size_t k = 1; k < q.size(); k++) cum[k] = cum[k - 1] + length(q[k] - q[k - 1]);
                float total = cum.back();
                auto at = [&](float s, size_t* seg) {
                    size_t k = 1;
                    while (k + 1 < q.size() && cum[k] < s) k++;
                    *seg = k;
                    float t = (s - cum[k - 1]) / Max(cum[k] - cum[k - 1], 1e-4f);
                    return lerp(q[k - 1], q[k], Saturate(t));
                };
                float cut = -1.f;
                size_t cutSeg = 1;
                for (float s = 2.f; s < total - 1.f; s += 2.f) {
                    size_t sg;
                    vec2 c = at(s, &sg);
                    if (!sandy(c, clearR)) {
                        cut = s;
                        cutSeg = sg;
                        break;
                    }
                }
                vec2 seaward = normalize(tip - q.back());
                if (cut < 0.f || total - cut < clearR * 2.f + 10.f) {
                    // too little street would be left: back to the junction
                    dropEdge[ei] = 1;
                    deg[nd]--;
                    deg[inner]--;
                    if (deg[inner] >= 2) beachEnds.push_back({inner, seaward, false, e.name});
                    continue;
                }
                size_t sg;
                vec2 c = at(cut, &sg);
                std::vector<vec2> kept2(q.begin() + (long)cutSeg, q.end());
                if (length(kept2.front() - c) < 0.1f) kept2.erase(kept2.begin());   // the cut fell on a polyline vertex
                kept2.insert(kept2.begin(), c);   // dead end first, inward after
                if (kept2.size() < 2) continue;
                if (end == 0) {
                    e.pts.clear();
                    for (const vec2& p : kept2) e.pts.push_back(vec3(p, 0.f));
                } else {
                    e.pts.clear();
                    for (size_t k = kept2.size(); k-- > 0;) e.pts.push_back(vec3(kept2[k], 0.f));
                }
                nodes[nd].p = c;
                beachEnds.push_back({nd, normalize(tip - c), true, e.name});
            }
        }
        std::vector<RoadEdge> keptE;
        for (size_t ei = 0; ei < edges.size(); ei++)
            if (!dropEdge[ei]) keptE.push_back(std::move(edges[ei]));
        edges.swap(keptE);
    }
    // Remove tiny dangling stubs (< 12 m, degree-1 end), and street pieces that a ramp runs along on top of (the ramp is still
    // high up where it passes over them: the local street gives way to the interchange)
    {
        std::vector<int> deg(nodes.size(), 0);
        for (auto& e : edges) { deg[e.n0]++; deg[e.n1]++; }
        std::vector<std::pair<vec2, vec2>> rampSegs;
        std::unordered_map<long long, std::vector<int>> rampGrid;   // 64 m cells
        auto rkey = [](int x, int y) { return (long long)(y + 1000) * 4000LL + (x + 1000); };
        for (auto& e : edges)
            if (e.cls == RC_RAMP)
                for (size_t i = 0; i + 1 < e.pts.size(); i++) {
                    vec2 a = e.pts[i].xy(), c = e.pts[i + 1].xy();
                    int id = (int)rampSegs.size();
                    rampSegs.push_back({a, c});
                    int x0 = (int)floorf((Min(a.x, c.x) - 24.f) / 64.f), x1 = (int)floorf((Max(a.x, c.x) + 24.f) / 64.f);
                    int y0 = (int)floorf((Min(a.y, c.y) - 24.f) / 64.f), y1 = (int)floorf((Max(a.y, c.y) + 24.f) / 64.f);
                    for (int y = y0; y <= y1; y++)
                        for (int x = x0; x <= x1; x++) rampGrid[rkey(x, y)].push_back(id);
                }
        const RoadClassInfo& rri = roadInfo(RC_RAMP);
        float rampHw = rri.laneWidth * 0.5f + rri.shoulder;
        auto underRamp = [&](const RoadEdge& e) {
            if (e.cls == RC_RAMP || e.cls == RC_HIGHWAY || e.cls == RC_DIRT) return false;
            const RoadClassInfo& ri = roadInfo(e.cls);
            float hw = ri.laneWidth * ri.lanes + ri.median * 0.5f + ri.shoulder;
            float total = 0.f, covered = 0.f;
            for (size_t i = 0; i + 1 < e.pts.size(); i++) {
                vec2 a = e.pts[i].xy(), c = e.pts[i + 1].xy();
                float len = length(c - a);
                if (len < 1e-3f) continue;
                vec2 d = (c - a) / len;
                int n = Max(1, (int)(len / 4.f));
                for (int k = 0; k < n; k++) {
                    vec2 q = lerp(a, c, (k + 0.5f) / n);
                    float step = len / n;
                    total += step;
                    auto it = rampGrid.find(rkey((int)floorf(q.x / 64.f), (int)floorf(q.y / 64.f)));
                    if (it == rampGrid.end()) continue;
                    for (int id : it->second) {
                        const auto& rs = rampSegs[id];
                        vec2 rd = rs.second - rs.first;
                        float rl = length(rd);
                        if (rl < 1e-3f || fabsf(dot(rd / rl, d)) < 0.8f) continue;
                        if (distPointSegment2D(q, rs.first, rs.second) < hw + rampHw - 1.f) { covered += step; break; }
                    }
                }
            }
            return total > 0.f && covered > 0.5f * total;
        };
        // two roads laid on top of each other between the same nodes (a town street on a county road's line): one is enough
        std::vector<char> dup(edges.size(), 0);
        {
            std::unordered_map<long long, std::vector<int>> byEnds;
            for (size_t i = 0; i < edges.size(); i++)
                byEnds[(long long)Min(edges[i].n0, edges[i].n1) * 1000003LL + Max(edges[i].n0, edges[i].n1)].push_back((int)i);
            auto within = [&](const RoadEdge& a, const RoadEdge& o) {
                for (const vec3& p : a.pts) {
                    float d = 1e9f;
                    for (size_t k = 0; k + 1 < o.pts.size(); k++) d = Min(d, distPointSegment2D(p.xy(), o.pts[k].xy(), o.pts[k + 1].xy()));
                    if (d > 3.f) return false;
                }
                return true;
            };
            for (auto& kv : byEnds)
                for (size_t x = 0; x < kv.second.size(); x++)
                    for (size_t y = x + 1; y < kv.second.size(); y++) {
                        int i = kv.second[x], j = kv.second[y];
                        if (dup[i] || dup[j] || edges[i].n0 == edges[i].n1) continue;
                        if (!within(edges[i], edges[j]) || !within(edges[j], edges[i])) continue;
                        dup[edges[i].cls <= edges[j].cls ? j : i] = 1;   // keep the bigger road
                    }
        }
        // a dead-end stub that does not get clear of the junction it hangs off (its disc would swallow it)
        std::vector<float> nodeReach(nodes.size(), 0.f);
        for (const RoadEdge& e : edges) {
            const RoadClassInfo& ri = roadInfo(e.cls);
            float hw = ri.laneWidth * ((e.flags & RF_ONEWAY) ? ri.lanes : ri.lanes * 2) * 0.5f + ri.median * 0.5f + ri.shoulder + ri.sidewalk * 0.3f + 2.f;
            nodeReach[e.n0] = Max(nodeReach[e.n0], hw);
            nodeReach[e.n1] = Max(nodeReach[e.n1], hw);
        }
        std::vector<RoadEdge> kept;
        for (size_t ei = 0; ei < edges.size(); ei++) {
            RoadEdge& e = edges[ei];
            float len = 0;
            for (size_t i = 0; i + 1 < e.pts.size(); i++) len += length(e.pts[i + 1].xy() - e.pts[i].xy());
            if (len < 12.f && (deg[e.n0] == 1 || deg[e.n1] == 1) && e.cls != RC_RAMP) continue;
            if (e.cls != RC_RAMP && e.cls != RC_HIGHWAY && ((deg[e.n1] == 1 && deg[e.n0] >= 3 && len < nodeReach[e.n0] + 10.f) ||
                                                            (deg[e.n0] == 1 && deg[e.n1] >= 3 && len < nodeReach[e.n1] + 10.f)))
                continue;
            if (dup[ei] || underRamp(e)) continue;
            kept.push_back(std::move(e));
        }
        edges.swap(kept);
    }
    for (auto& n : nodes) n.edges.clear();
    for (size_t i = 0; i < edges.size(); i++) {
        nodes[edges[i].n0].edges.push_back((int)i);
        nodes[edges[i].n1].edges.push_back((int)i);
    }

    // ============================================================= Per-edge attributes
    for (size_t i = 0; i < edges.size(); i++) {
        RoadEdge& e = edges[i];
        const RoadClassInfo& ri = roadInfo(e.cls);
        e.seed = hash32((u32)i * 2654435761u + 17u);
        e.lanesF = e.lanesB = (u8)ri.lanes;
        if (e.flags & RF_ONEWAY) { e.lanesF = (u8)ri.lanes; e.lanesB = 0; }
        e.halfWidth = ri.laneWidth * (e.lanesF + e.lanesB) * 0.5f + ri.median * 0.5f + ri.shoulder;
        vec2 mid = e.pts[e.pts.size() / 2].xy();
        Region reg = map.regionAt(mid.x, mid.y);
        bool town = isUrbanRegion(reg) || reg == REG_SUBURBS || reg == REG_GROVE || reg == REG_BAY_ISLAND || reg == REG_KEY_CORAL;
        e.sidewalk = town ? ri.sidewalk : 0.f;
        if (e.cls == RC_DIRT) e.flags |= RF_UNPAVED;
        if (e.sidewalk <= 0.f) e.flags |= RF_NOSIDEWALK;
    }

    // ============================================================= Elevation
    // At-grade roads that highways must bridge over: their (elevated) segments in a coarse grid, built after the streets'
    // own elevation pass so the highway clears their actual surface
    const float kXCell = 64.f;
    const int xRes = (int)(2.f * kWorldHalf / kXCell) + 1;
    std::vector<std::vector<int>> xGrid((size_t)xRes * xRes);
    std::vector<std::pair<vec3, vec3>> xSegs;
    auto buildStreetGrid = [&]() {
        for (auto& c : xGrid) c.clear();
        xSegs.clear();
        for (const RoadEdge& o : edges) {
            if (o.cls == RC_HIGHWAY || o.cls == RC_RAMP) continue;
            for (size_t k = 0; k + 1 < o.pts.size(); k++) {
                vec2 a = o.pts[k].xy(), c = o.pts[k + 1].xy();
                int id = (int)xSegs.size();
                xSegs.push_back({o.pts[k], o.pts[k + 1]});
                int x0 = Clamp((int)((Min(a.x, c.x) + kWorldHalf) / kXCell), 0, xRes - 1), x1 = Clamp((int)((Max(a.x, c.x) + kWorldHalf) / kXCell), 0, xRes - 1);
                int y0 = Clamp((int)((Min(a.y, c.y) + kWorldHalf) / kXCell), 0, xRes - 1), y1 = Clamp((int)((Max(a.y, c.y) + kWorldHalf) / kXCell), 0, xRes - 1);
                for (int y = y0; y <= y1; y++)
                    for (int x = x0; x <= x1; x++) xGrid[(size_t)y * xRes + x].push_back(id);
            }
        }
    };
    // highest street surface within r of p (-1e9 if none)
    auto streetUnder = [&](vec2 p, float r) {
        float zmax = -1e9f;
        int cx0 = Clamp((int)((p.x - r + kWorldHalf) / kXCell), 0, xRes - 1), cx1 = Clamp((int)((p.x + r + kWorldHalf) / kXCell), 0, xRes - 1);
        int cy0 = Clamp((int)((p.y - r + kWorldHalf) / kXCell), 0, xRes - 1), cy1 = Clamp((int)((p.y + r + kWorldHalf) / kXCell), 0, xRes - 1);
        for (int y = cy0; y <= cy1; y++)
            for (int x = cx0; x <= cx1; x++)
                for (int id : xGrid[(size_t)y * xRes + x]) {
                    float t;
                    if (distPointSegment2D(p, xSegs[id].first.xy(), xSegs[id].second.xy(), &t) < r)
                        zmax = Max(zmax, Lerp(xSegs[id].first.z, xSegs[id].second.z, t));
                }
        return zmax;
    };
    // Highway pavement (after the first pass) for ramps: a ramp holds the highway's level while it still overlaps the
    // highway pavement (auxiliary lane up to the gore), then descends at the grade limit; otherwise the ramp dives under
    // the highway's deck edge and its parapet ends up across the ramp lanes
    std::vector<std::vector<int>> hGrid;
    std::vector<std::pair<int, int>> hSegs;   // (edge, segment)
    auto highwayAt = [&](vec2 p, float rampHw, float refZ, float* hz) {
        // highway pavement under p; where two highways overlap in plan (interchanges), the one nearest in height to refZ
        int cx = Clamp((int)((p.x + kWorldHalf) / kXCell), 0, xRes - 1), cy = Clamp((int)((p.y + kWorldHalf) / kXCell), 0, xRes - 1);
        float best = 1e9f;
        bool found = false;
        for (int y = Max(0, cy - 1); y <= Min(xRes - 1, cy + 1); y++)
            for (int x = Max(0, cx - 1); x <= Min(xRes - 1, cx + 1); x++)
                for (int id : hGrid[(size_t)y * xRes + x]) {
                    const RoadEdge& h = edges[hSegs[id].first];
                    int k = hSegs[id].second;
                    float t;
                    float d = distPointSegment2D(p, h.pts[k].xy(), h.pts[k + 1].xy(), &t);
                    if (d >= h.halfWidth + rampHw + 0.3f) continue;   // the pavements touch or overlap
                    float z = Lerp(h.pts[k].z, h.pts[k + 1].z, t);
                    float score = fabsf(z - refZ) * 4.f + d;
                    if (score < best) {
                        best = score;
                        *hz = z;
                        found = true;
                    }
                }
        return found;
    };
    std::vector<std::vector<char>> held(edges.size());   // ramp points held at highway level (kept by the overlap pass)
    std::vector<std::vector<float>> heldZ(edges.size());
    std::vector<float> rampFlat(edges.size(), -1.f);   // ramps: flat run at the landing (>= 0 once planned from a highway end)
    std::vector<char> rampLandEnd(edges.size(), 0);    // ... and which end lands (0 = n0, 1 = n1)
    auto elevate = [&](RoadEdge& e) {
        // densify to <= 12 m spacing for smooth elevation profiles
        std::vector<vec3> dense;
        for (size_t i = 0; i + 1 < e.pts.size(); i++) {
            vec2 a = e.pts[i].xy(), c = e.pts[i + 1].xy();
            int n = Max(1, (int)ceilf(length(c - a) / 12.f));
            for (int k = 0; k < n; k++) dense.push_back(vec3(lerp(a, c, (float)k / n), 0));
        }
        dense.push_back(e.pts.back());
        e.pts.swap(dense);
        bool hwy = e.cls == RC_HIGHWAY;
        std::vector<float> target(e.pts.size());
        for (size_t i = 0; i < e.pts.size(); i++) {
            vec2 p = e.pts[i].xy();
            float g = map.heightAt(p.x, p.y);
            float wl = map.waterAt(p.x, p.y);
            bool water = wl > kNoWater + 1.f && wl > g - 0.05f;
            float z = g + 0.25f;
            if (water) {
                z = Max(z, wl + (hwy ? 12.f : 6.5f));
                e.flags |= RF_BRIDGE;
            }
            if (hwy) {
                Region r = map.regionAt(p.x, p.y);
                bool urban = isUrbanRegion(r) || r == REG_SUBURBS || r == REG_GROVE || r == REG_AIRPORT;
                if (urban) { z = Max(z, g + 9.5f); e.flags |= RF_ELEVATED; }
            }
            target[i] = z;
        }
        // Ramps: from the end attached to a highway node, hold the highway's level for as long as the ramp still runs against
        // the highway pavement (auxiliary lane up to the gore); from there stay as high as the ramp can while still reaching
        // its landing at the design grade (the envelope the interchange planner laid it out for: it clears the roads it
        // passes that way), flat where it lands: across the junction disc and wherever its surface lies over the landing
        // road's. Where what is left is too short to come down (or up) at 7%, the hold is released early.
        if (e.cls == RC_RAMP && !hGrid.empty()) {
            size_t cnt = e.pts.size();
            size_t idx = &e - &edges[0];
            std::vector<float> sAlong(cnt, 0.f);
            for (size_t i = 1; i < cnt; i++) sAlong[i] = sAlong[i - 1] + length(e.pts[i].xy() - e.pts[i - 1].xy());
            float L = sAlong.back();
            std::vector<char>& hm = held[idx];
            std::vector<float>& hzv = heldZ[idx];
            hm.assign(cnt, 0);
            hzv.assign(cnt, 0.f);
            for (int end = 0; end < 2; end++) {
                int nid = end == 0 ? e.n0 : e.n1;
                bool atHighway = false;
                for (int oe : nodes[nid].edges) atHighway |= edges[oe].cls == RC_HIGHWAY;
                if (!atHighway) continue;
                // reference height: the attach node's highway ends
                float refZ = 0.f;
                int nref = 0;
                for (int oe : nodes[nid].edges)
                    if (edges[oe].cls == RC_HIGHWAY) {
                        refZ += edges[oe].n0 == nid ? edges[oe].pts.front().z : edges[oe].pts.back().z;
                        nref++;
                    }
                refZ /= (float)Max(1, nref);
                std::vector<size_t> run;
                for (size_t j = 0; j < cnt; j++) {
                    size_t i = end == 0 ? j : cnt - 1 - j;
                    float hz;
                    if (!highwayAt(e.pts[i].xy(), e.halfWidth, refZ, &hz)) break;
                    refZ = hz;
                    hzv[i] = hz;
                    run.push_back(i);
                }
                if (run.empty()) continue;
                // the landing: the at-grade roads' level there (already elevated), and the flat run
                int farN = end == 0 ? e.n1 : e.n0;
                size_t farI = end == 0 ? cnt - 1 : 0;
                float zFar = target[farI], flat = 0.f;
                {
                    float zs = 0.f, r = 0.f;
                    int zc = 0;
                    for (int oe : nodes[farN].edges) {
                        const RoadEdge& o = edges[oe];
                        r = Max(r, o.halfWidth + o.sidewalk * 0.3f);
                        if (o.cls == RC_RAMP || o.cls == RC_HIGHWAY) continue;
                        zs += o.n0 == farN ? o.pts.front().z : o.pts.back().z;
                        zc++;
                    }
                    if (zc > 0) {
                        zFar = zs / (float)zc;
                        if (nodes[farN].edges.size() >= 3) flat = r + 2.f;
                        for (size_t j = 0; j < cnt; j++) {
                            size_t i = end == 0 ? cnt - 1 - j : j;
                            bool over = false;
                            for (int oe : nodes[farN].edges) {
                                const RoadEdge& o = edges[oe];
                                if (o.cls == RC_RAMP || o.cls == RC_HIGHWAY) continue;
                                for (size_t k = 0; k + 1 < o.pts.size() && !over; k++)
                                    over = distPointSegment2D(e.pts[i].xy(), o.pts[k].xy(), o.pts[k + 1].xy()) < e.halfWidth + o.halfWidth + o.sidewalk + 1.f;
                            }
                            if (!over) break;
                            flat = Max(flat, end == 0 ? L - sAlong[i] : sAlong[i]);
                        }
                        target[farI] = zFar;
                        rampFlat[idx] = flat;
                        rampLandEnd[idx] = (char)(end == 0 ? 1 : 0);
                    }
                }
                size_t keep = run.size();
                while (keep > 1) {
                    size_t i = run[keep - 1];
                    float left = end == 0 ? L - sAlong[i] : sAlong[i];
                    if (fabsf(hzv[i] - zFar) <= 0.07f * Max(0.f, left - flat)) break;
                    keep--;
                }
                for (size_t r = 0; r < keep; r++) {
                    size_t i = run[r];
                    target[i] = Max(target[i], hzv[i]);
                    hm[i] = 1;
                }
                float zTop = hzv[run[keep - 1]];
                for (size_t i = 0; i < cnt; i++) {
                    if (hm[i]) continue;
                    float left = end == 0 ? L - sAlong[i] : sAlong[i];
                    if (left <= flat) target[i] = zFar;
                    else target[i] = Max(target[i], Min(zTop, zFar + kRampEnvGrade * (left - flat)));
                }
            }
        }
        // Highways: raise over road crossings (deck 7 m over the street surface, at least 8 m over the ground); the grade limit
        // below builds the approaches
        if (hwy) {
            for (size_t i = 0; i < e.pts.size(); i++) {
                vec2 p = e.pts[i].xy();
                float sz = streetUnder(p, 26.f);   // the ramps alongside must clear it too
                if (sz < -1e8f) continue;
                target[i] = Max(target[i], Max(map.heightAt(p.x, p.y) + 8.f, sz + 7.f));
                e.flags |= RF_BRIDGE;
            }
        }
        // Smooth: dilate high points (bridges need ramps), then average; grade limit (over the actual point spacing)
        float maxGrade = hwy ? 0.045f : 0.07f;
        std::vector<float> z = target, gap(e.pts.size(), 0.f);
        for (size_t i = 1; i < e.pts.size(); i++) gap[i] = length(e.pts[i].xy() - e.pts[i - 1].xy());
        auto gradeLimit = [&]() {
            for (int pass = 0; pass < 2; pass++) {
                for (size_t i = 1; i < z.size(); i++) z[i] = Max(z[i], z[i - 1] - maxGrade * gap[i]);
                for (size_t i = z.size() - 1; i-- > 0;) z[i] = Max(z[i], z[i + 1] - maxGrade * gap[i + 1]);
            }
        };
        gradeLimit();
        std::vector<float> zs = z;
        for (int it = 0; it < 3; it++) {
            for (size_t i = 1; i + 1 < z.size(); i++) zs[i] = (z[i - 1] + z[i] * 2.f + z[i + 1]) * 0.25f;
            // (a ramp only rounds its sags: its crests stay on the planned envelope, whose grade is at the limit already)
            for (size_t i = 1; i + 1 < z.size(); i++) z[i] = Max(zs[i], target[i] - (hwy ? 0.5f : (e.cls == RC_RAMP ? 0.f : 0.15f)));
        }
        // the averaging can leave a point next to one pinned at its target too far below it: limit the grade again
        gradeLimit();
        for (size_t i = 0; i < e.pts.size(); i++) e.pts[i].z = z[i];
        // held ramp points sit exactly on the highway (the grade limit may have lifted them toward a hilltop end)
        if (e.cls == RC_RAMP) {
            size_t idx = &e - &edges[0];
            const std::vector<char>& hm = held[idx];
            for (size_t i = 0; i < hm.size() && i < e.pts.size(); i++)
                if (hm[i]) e.pts[i].z = heldZ[idx][i];
        }
    };
    for (auto& e : edges)
        if (e.cls != RC_RAMP && e.cls != RC_HIGHWAY) elevate(e);
    buildStreetGrid();
    // Highways are elevated along whole chains, through the nodes where only ramps join them: the approach grades carry
    // across a ramp's attach node instead of leaving a step there (which the node blend would smear under the ramp's
    // held section)
    {
        std::vector<int> hwyDeg(nodes.size(), 0);
        for (const RoadEdge& e : edges)
            if (e.cls == RC_HIGHWAY) { hwyDeg[e.n0]++; hwyDeg[e.n1]++; }
        // the other highway edge at a pass-through node
        auto nextAt = [&](int node, int from) {
            if (hwyDeg[node] != 2) return -1;
            for (int oe : nodes[node].edges)
                if (oe != from && edges[oe].cls == RC_HIGHWAY) return oe;
            return -1;
        };
        std::vector<char> done(edges.size(), 0);
        for (size_t e0 = 0; e0 < edges.size(); e0++) {
            if (edges[e0].cls != RC_HIGHWAY || done[e0]) continue;
            // back up to the chain's first edge
            int cur = (int)e0, node = edges[e0].n0;
            for (size_t guard = 0; guard < edges.size(); guard++) {
                int prev = nextAt(node, cur);
                if (prev < 0 || prev == (int)e0) break;
                node = edges[prev].n0 == node ? edges[prev].n1 : edges[prev].n0;
                cur = prev;
            }
            // walk forward: (edge, reversed)
            std::vector<std::pair<int, bool>> chain;
            int start = node;
            node = start;
            for (int e = cur; e >= 0 && !done[e];) {
                bool rev = edges[e].n1 == node;
                chain.push_back({e, rev});
                done[e] = 1;
                node = rev ? edges[e].n0 : edges[e].n1;
                e = nextAt(node, e);
            }
            RoadEdge C;
            C.cls = RC_HIGHWAY;
            C.flags = 0;
            std::vector<size_t> startIdx;
            for (auto& ce : chain) {
                std::vector<vec3> p = edges[ce.first].pts;
                if (ce.second) std::reverse(p.begin(), p.end());
                startIdx.push_back(C.pts.empty() ? 0 : C.pts.size() - 1);
                for (size_t i = 0; i + 1 < p.size(); i++) {
                    vec2 a = p[i].xy(), c = p[i + 1].xy();
                    int n = Max(1, (int)ceilf(length(c - a) / 11.5f));
                    for (int k = 0; k < n; k++) {
                        if (k == 0 && i == 0 && !C.pts.empty()) continue;   // the node point the previous edge ended on
                        C.pts.push_back(vec3(lerp(a, c, (float)k / n), 0));
                    }
                }
                C.pts.push_back(vec3(p.back().xy(), 0));
            }
            startIdx.push_back(C.pts.size() - 1);
            elevate(C);
            // back onto the edges (elevate kept every point: all pieces are under its 12 m spacing)
            for (size_t c = 0; c < chain.size(); c++) {
                RoadEdge& e = edges[chain[c].first];
                std::vector<vec3> p(C.pts.begin() + startIdx[c], C.pts.begin() + startIdx[c + 1] + 1);
                if (chain[c].second) std::reverse(p.begin(), p.end());
                e.pts.swap(p);
                for (const vec3& q : e.pts) {
                    float g = map.heightAt(q.x, q.y), wl = map.waterAt(q.x, q.y);
                    if (wl > kNoWater + 1.f && wl > g - 0.05f) e.flags |= RF_BRIDGE;
                    Region r = map.regionAt(q.x, q.y);
                    if (isUrbanRegion(r) || r == REG_SUBURBS || r == REG_GROVE || r == REG_AIRPORT) e.flags |= RF_ELEVATED;
                    if (streetUnder(q.xy(), 26.f) > -1e8f) e.flags |= RF_BRIDGE;
                }
            }
        }
    }
    hGrid.assign((size_t)xRes * xRes, {});
    for (size_t ei = 0; ei < edges.size(); ei++) {
        const RoadEdge& h = edges[ei];
        if (h.cls != RC_HIGHWAY) continue;
        for (size_t k = 0; k + 1 < h.pts.size(); k++) {
            vec2 a = h.pts[k].xy(), c = h.pts[k + 1].xy();
            int id = (int)hSegs.size();
            hSegs.push_back({(int)ei, (int)k});
            int x0 = Clamp((int)((Min(a.x, c.x) + kWorldHalf) / kXCell), 0, xRes - 1), x1 = Clamp((int)((Max(a.x, c.x) + kWorldHalf) / kXCell), 0, xRes - 1);
            int y0 = Clamp((int)((Min(a.y, c.y) + kWorldHalf) / kXCell), 0, xRes - 1), y1 = Clamp((int)((Max(a.y, c.y) + kWorldHalf) / kXCell), 0, xRes - 1);
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) hGrid[(size_t)y * xRes + x].push_back(id);
        }
    }
    for (auto& e : edges)
        if (e.cls == RC_RAMP) elevate(e);
    // Plan-overlapping roads at different heights (a ramp landing along a street, a bridge approach beside a parallel
    // boulevard) share one surface where one road's surface lies over the other's lanes: the higher road is capped to the
    // lower one there and eases back to its own profile at the grade limit. Highways and ramp sections held at highway
    // level keep their height; genuine grade separations (4.5 m or more) are left alone.
        std::vector<std::vector<int>> oGrid((size_t)xRes * xRes);
        std::vector<std::pair<int, int>> oSegs;
        for (size_t ei = 0; ei < edges.size(); ei++) {
            const RoadEdge& o = edges[ei];
            if (o.flags & RF_UNPAVED) continue;
            for (size_t k = 0; k + 1 < o.pts.size(); k++) {
                vec2 a = o.pts[k].xy(), c = o.pts[k + 1].xy();
                int id = (int)oSegs.size();
                oSegs.push_back({(int)ei, (int)k});
                float r = o.halfWidth;
                int x0 = Clamp((int)((Min(a.x, c.x) - r + kWorldHalf) / kXCell), 0, xRes - 1), x1 = Clamp((int)((Max(a.x, c.x) + r + kWorldHalf) / kXCell), 0, xRes - 1);
                int y0 = Clamp((int)((Min(a.y, c.y) - r + kWorldHalf) / kXCell), 0, xRes - 1), y1 = Clamp((int)((Max(a.y, c.y) + r + kWorldHalf) / kXCell), 0, xRes - 1);
                for (int y = y0; y <= y1; y++)
                    for (int x = x0; x <= x1; x++) oGrid[(size_t)y * xRes + x].push_back(id);
            }
        }
        auto isHeld = [&](size_t ei, size_t i) { return i < held[ei].size() && held[ei][i]; };
        // ease a profile back up from its capped points at grade g (only where it is higher than the easing)
        bool easeFixedEnds = false;
        auto ease = [&](RoadEdge& E, size_t ei, const std::vector<char>& fixedPt) {
            size_t n = E.pts.size();
            float g = E.cls == RC_HIGHWAY ? 0.045f : 0.07f;
            for (int dir = 0; dir < 2; dir++) {
                float lim = 1e9f;
                for (size_t j = 0; j < n; j++) {
                    size_t i = dir == 0 ? j : n - 1 - j;
                    if (fixedPt[i]) { lim = E.pts[i].z; continue; }
                    size_t prev = dir == 0 ? i - 1 : i + 1;
                    if (j > 0) lim += g * length(E.pts[i].xy() - E.pts[prev].xy());
                    if (easeFixedEnds && (i == 0 || i + 1 == n)) continue;   // junction ends stay once they are on their nodes
                    if (!isHeld(ei, i) && E.pts[i].z > lim) E.pts[i].z = lim;   // held ramp points stay
                }
            }
        };
        // the junction discs the intersection pass will give the nodes
        std::vector<float> discR(nodes.size(), 0.f);
        for (size_t n = 0; n < nodes.size(); n++) {
            if (nodes[n].edges.size() < 3) continue;
            for (int ei : nodes[n].edges) discR[n] = Max(discR[n], edges[ei].halfWidth + edges[ei].sidewalk * 0.3f + 2.f);
        }
        // candidate pairs (plan geometry does not change): A's segment [i, i+1] passing within reach of B's segment k
        struct Cand { int i, b, k; };
        std::vector<std::vector<Cand>> cands(edges.size());
        for (size_t ei = 0; ei < edges.size(); ei++) {
            const RoadEdge& A = edges[ei];
            if (A.flags & RF_UNPAVED) continue;
            bool hwyA = A.cls == RC_HIGHWAY;   // highways only yield to other highways (interchange overlaps)
            size_t n = A.pts.size();
            for (size_t i = 0; i < n; i++) {
                size_t i1 = Min(i + 1, n - 1);
                vec2 p = A.pts[i].xy();
                int cx = Clamp((int)((p.x + kWorldHalf) / kXCell), 0, xRes - 1), cy = Clamp((int)((p.y + kWorldHalf) / kXCell), 0, xRes - 1);
                for (int gy = Max(0, cy - 1); gy <= Min(xRes - 1, cy + 1); gy++)
                    for (int gx = Max(0, cx - 1); gx <= Min(xRes - 1, cx + 1); gx++)
                        for (int id : oGrid[(size_t)gy * xRes + gx]) {
                            int bi = oSegs[id].first;
                            if (bi == (int)ei) continue;
                            const RoadEdge& B = edges[bi];
                            if (hwyA && B.cls != RC_HIGHWAY) continue;
                            // the same road continuing through a node (a ramp's attach node, a junction) is not an overlap
                            bool adjacent = A.n0 == B.n0 || A.n0 == B.n1 || A.n1 == B.n0 || A.n1 == B.n1;
                            if (adjacent && A.cls == B.cls && A.name == B.name) continue;
                            int k = oSegs[id].second;
                            float lim = A.halfWidth + A.sidewalk + B.halfWidth - 1.f;   // A's whole surface over B's lanes
                            bool nearB = false;
                            for (int q = 0; q <= 3 && !nearB; q++)
                                nearB = distPointSegment2D(lerp(A.pts[i].xy(), A.pts[i1].xy(), q / 3.f), B.pts[k].xy(), B.pts[k + 1].xy()) <= lim;
                            if (nearB) cands[ei].push_back({(int)i, bi, k});
                        }
            }
        }
        auto settleOverlaps = [&](bool fixedEnds) {
        for (int iter = 0; iter < 24; iter++) {   // chains of overlapping roads settle one hop per round
            bool changed = false;
            for (size_t ei = 0; ei < edges.size(); ei++) {
                if (cands[ei].empty()) continue;
                RoadEdge& A = edges[ei];
                bool hwyA = A.cls == RC_HIGHWAY;
                size_t n = A.pts.size();
                std::vector<float> cap(n, 1e9f);
                bool any = false;
                for (const Cand& c : cands[ei]) {
                    size_t i = (size_t)c.i, i1 = Min(i + 1, n - 1);
                    if (isHeld(ei, i) || isHeld(ei, i1)) continue;
                    const RoadEdge& B = edges[c.b];
                    int k = c.k;
                    vec2 b0 = B.pts[k].xy(), b1 = B.pts[k + 1].xy();
                    float lim = A.halfWidth + A.sidewalk + B.halfWidth - 1.f;
                    // samples along A's segment: where A's surface lies over B's lanes 0.3-4.5 m above them (2.5 m where a
                    // highway is involved: more is a flyover)
                    float maxDz = hwyA ? 2.5f : 4.5f;
                    float zbest = 1e9f;
                    for (int q = 0; q <= 3; q++) {
                        float ta = q / 3.f, tb;
                        if (distPointSegment2D(lerp(A.pts[i].xy(), A.pts[i1].xy(), ta), b0, b1, &tb) > lim) continue;
                        float zaq = Lerp(A.pts[i].z, A.pts[i1].z, ta), zbq = Lerp(B.pts[k].z, B.pts[k + 1].z, tb);
                        float dzq = zaq - zbq;
                        if (dzq > 0.3f && dzq < maxDz) zbest = Min(zbest, zbq);
                    }
                    if (zbest > 1e8f) continue;
                    cap[i] = Min(cap[i], zbest);
                    cap[i1] = Min(cap[i1], zbest);
                    any = true;
                }
                if (!any) continue;
                // anchors that do not move (ramp points held on the highway, junction ends once they sit on their nodes):
                // a cap never pulls a point more than 10% below an anchor, so no cliff opens next to one (3% on a link
                // so short that the discs of the junctions at its ends cover it: it stays with them)
                std::vector<float> sA(n, 0.f);
                for (size_t i = 1; i < n; i++) sA[i] = sA[i - 1] + length(A.pts[i].xy() - A.pts[i - 1].xy());
                float anchorGrade = discR[A.n0] > 0.f && discR[A.n1] > 0.f && sA[n - 1] < discR[A.n0] + discR[A.n1] ? 0.03f : 0.06f;
                for (size_t h = 0; h < n; h++) {
                    if (!(isHeld(ei, h) || (fixedEnds && (h == 0 || h + 1 == n)))) continue;
                    for (size_t i = 0; i < n; i++)
                        if (cap[i] < 1e8f) cap[i] = Max(cap[i], A.pts[h].z - anchorGrade * fabsf(sA[i] - sA[h]));
                }
                std::vector<char> fixedPt(n, 0);
                for (size_t i = 0; i < n; i++)
                    if (cap[i] < A.pts[i].z - 0.05f && !isHeld(ei, i) && !(fixedEnds && (i == 0 || i + 1 == n))) {
                        A.pts[i].z = cap[i];
                        fixedPt[i] = 1;
                        changed = true;
                    }
                ease(A, ei, fixedPt);
            }
            if (!changed) break;
        }
        };
        settleOverlaps(false);
    // Node heights: average of incident edge ends; then snap edge ends to node heights (at-grade)
    for (size_t n = 0; n < nodes.size(); n++) {
        float s = 0;
        int c = 0;
        for (int ei : nodes[n].edges) {
            const RoadEdge& e = edges[ei];
            s += (e.n0 == (int)n) ? e.pts.front().z : e.pts.back().z;
            c++;
        }
        nodes[n].z = c ? s / c : map.heightAt(nodes[n].p.x, nodes[n].p.y);
    }
    // Two nodes joined by a short edge cannot differ by more than the grade limit allows over it: the lower one is lifted (a
    // bridge landing right by a junction raises the junction rather than squeezing the difference into the link)
    {
        std::vector<float> len(edges.size(), 0.f);
        for (size_t ei = 0; ei < edges.size(); ei++)
            for (size_t i = 0; i + 1 < edges[ei].pts.size(); i++) len[ei] += length(edges[ei].pts[i + 1].xy() - edges[ei].pts[i].xy());
        for (int it = 0; it < 8; it++) {
            bool changed = false;
            for (size_t ei = 0; ei < edges.size(); ei++) {
                const RoadEdge& e = edges[ei];
                if (e.flags & RF_UNPAVED) continue;
                float lim = (e.cls == RC_HIGHWAY ? 0.04f : 0.05f) * len[ei];   // inside the approach clamp below (6%)
                float& za = nodes[e.n0].z;
                float& zb = nodes[e.n1].z;
                if (za < zb - lim - 0.01f) { za = zb - lim; changed = true; }
                else if (zb < za - lim - 0.01f) { zb = za - lim; changed = true; }
            }
            if (!changed) break;
        }
    }
    for (auto& e : edges) {
        // blend the first/last ~40 m toward the node heights (ramps keep their planned grades: their profile is shifted onto
        // the node's height rather than flattened toward it)
        bool shift = e.cls == RC_RAMP;
        float z0 = nodes[e.n0].z, z1 = nodes[e.n1].z;
        float acc = 0;
        if (!shift) {
            // an end far off its node's height (a bridge landing right at a junction) first comes down (or up) to it at 6%,
            // so the blend does not squeeze the difference into a cliff
            for (size_t i = 0; i < e.pts.size(); i++) {
                if (i > 0) acc += length(e.pts[i].xy() - e.pts[i - 1].xy());
                e.pts[i].z = Clamp(e.pts[i].z, z0 - 0.06f * acc, z0 + 0.06f * acc);
            }
            acc = 0;
            for (size_t i = e.pts.size(); i-- > 0;) {
                if (i + 1 < e.pts.size()) acc += length(e.pts[i].xy() - e.pts[i + 1].xy());
                e.pts[i].z = Clamp(e.pts[i].z, z1 - 0.06f * acc, z1 + 0.06f * acc);
            }
            acc = 0;
        }
        float e0 = e.pts.front().z, e1 = e.pts.back().z;
        float L = 0.f;
        for (size_t i = 1; i < e.pts.size(); i++) L += length(e.pts[i].xy() - e.pts[i - 1].xy());
        if (!shift && L < 80.f && L > 1e-3f) {
            // short enough for both ends' blends to overlap: toward the line between the two node heights
            for (size_t i = 0; i < e.pts.size(); i++) {
                if (i > 0) acc += length(e.pts[i].xy() - e.pts[i - 1].xy());
                float w = Max(SmoothStep(40.f, 0.f, acc), SmoothStep(40.f, 0.f, L - acc));
                e.pts[i].z = Lerp(e.pts[i].z, Lerp(z0, z1, acc / L), w);
            }
        } else {
            for (size_t i = 0; i < e.pts.size(); i++) {
                if (i > 0) acc += length(e.pts[i].xy() - e.pts[i - 1].xy());
                float w = SmoothStep(40.f, 0.f, acc);
                e.pts[i].z = shift ? e.pts[i].z + (z0 - e0) * w : Lerp(e.pts[i].z, z0, w);
            }
            acc = 0;
            for (size_t i = e.pts.size(); i-- > 0;) {
                if (i + 1 < e.pts.size()) acc += length(e.pts[i].xy() - e.pts[i + 1].xy());
                float w = SmoothStep(40.f, 0.f, acc);
                e.pts[i].z = shift ? e.pts[i].z + (z1 - e1) * w : Lerp(e.pts[i].z, z1, w);
            }
        }
        e.pts.front() = vec3(nodes[e.n0].p, z0);
        e.pts.back() = vec3(nodes[e.n1].p, z1);
        e.dist.resize(e.pts.size());
        e.dist[0] = 0;
        for (size_t i = 1; i < e.pts.size(); i++) e.dist[i] = e.dist[i - 1] + length(e.pts[i] - e.pts[i - 1]);
        e.length = e.dist.back();
    }

    // After the junction blend: held ramp sections go back onto the (blended) highway surface, and overlaps near junctions
    // are settled again with the junction ends fixed
    for (size_t ei = 0; ei < edges.size(); ei++) {
        RoadEdge& e = edges[ei];
        if (e.cls != RC_RAMP || held[ei].empty()) continue;
        for (size_t i = 1; i + 1 < e.pts.size() && i < held[ei].size(); i++) {
            if (!held[ei][i]) continue;
            float hz;
            if (highwayAt(e.pts[i].xy(), e.halfWidth, e.pts[i].z, &hz)) e.pts[i].z = hz;
        }
    }
    easeFixedEnds = true;
    settleOverlaps(true);
    // Planned ramps end within the grade limit of both their ends whatever the blends did: the landing node's height (reached
    // across the flat run) and the last point held on the highway
    for (size_t ei = 0; ei < edges.size(); ei++) {
        RoadEdge& e = edges[ei];
        if (e.cls != RC_RAMP || rampFlat[ei] < 0.f || held[ei].size() != e.pts.size()) continue;
        size_t n = e.pts.size();
        std::vector<float> sA(n, 0.f);
        for (size_t i = 1; i < n; i++) sA[i] = sA[i - 1] + length(e.pts[i].xy() - e.pts[i - 1].xy());
        bool landN1 = rampLandEnd[ei] != 0;
        float zLand = nodes[landN1 ? e.n1 : e.n0].z;
        int lastHeld = -1;
        for (size_t j = 0; j < n; j++) {
            size_t i = landN1 ? j : n - 1 - j;   // from the highway end
            if (!held[ei][i]) break;
            lastHeld = (int)i;
        }
        if (lastHeld < 0) continue;
        float zHold = e.pts[lastHeld].z;
        for (size_t i = 0; i < n; i++) {
            if (held[ei][i]) continue;
            float dLand = Max(0.f, (landN1 ? sA[n - 1] - sA[i] : sA[i]) - rampFlat[ei]), dHold = fabsf(sA[i] - sA[lastHeld]);
            float lo = Max(zLand - 0.07f * dLand, zHold - 0.07f * dHold), hi = Min(zLand + 0.07f * dLand, zHold + 0.07f * dHold);
            if (lo <= hi) e.pts[i].z = Clamp(e.pts[i].z, lo, hi);
        }
    }
    for (auto& e : edges) {
        e.dist[0] = 0;
        for (size_t i = 1; i < e.pts.size(); i++) e.dist[i] = e.dist[i - 1] + length(e.pts[i] - e.pts[i - 1]);
        e.length = e.dist.back();
    }

    // ============================================================= Intersections: radius, cut-backs, control
    for (size_t n = 0; n < nodes.size(); n++) {
        RoadNode& nd = nodes[n];
        float r = 0;
        int big = 0, count = (int)nd.edges.size();
        for (int ei : nd.edges) {
            const RoadEdge& e = edges[ei];
            r = Max(r, e.halfWidth + e.sidewalk * 0.3f);
            if (e.cls <= RC_AVENUE) big++;
        }
        nd.radius = count >= 3 ? r + 2.f : 0.f;
        bool hasHighway = false;
        for (int ei : nd.edges) hasHighway |= edges[ei].cls == RC_HIGHWAY || edges[ei].cls == RC_RAMP;
        if (count >= 3 && !hasHighway) nd.control = (big >= 2) ? 2 : (big == 1 ? 1 : (count >= 4 ? 1 : 0));
        if (count >= 3 && hasHighway) nd.control = 0;
    }
    for (auto& e : edges) {
        e.cut0 = Min(nodes[e.n0].radius, e.length * 0.45f);
        e.cut1 = Min(nodes[e.n1].radius, e.length * 0.45f);
    }

    // ============================================================= Terrain flattening along at-grade roads
    for (auto& e : edges) {
        if (e.cls == RC_HIGHWAY && (e.flags & RF_ELEVATED)) continue;
        for (size_t i = 0; i + 1 < e.pts.size(); i++) {
            vec3 a = e.pts[i], c = e.pts[i + 1];
            float ga = map.heightAt(a.x, a.y), gc = map.heightAt(c.x, c.y);
            // skip bridge spans (deck well above ground)
            if (a.z - ga > 3.f && c.z - gc > 3.f) continue;
            float hw = e.halfWidth + e.sidewalk + 1.5f;
            map.flattenAlong(a.xy(), c.xy(), a.z - 0.3f, c.z - 0.3f, hw, 14.f);
        }
    }
    // dead-end turning bulbs sit on the ground (at the lower of the road's heights across them: the road may still climb into it)
    auto bulbGround = [&](const RoadNode& nd) {
        const RoadEdge& e = edges[nd.edges[0]];
        float br = bulbRadius(nd);
        vec3 back = e.n1 == (int)(&nd - &nodes[0]) ? e.posAt(e.length - br) : e.posAt(br);
        return Min(nd.z, back.z) - 0.3f;
    };
    for (auto& nd : nodes) {
        if (nd.radius > 0 && !nd.highway) map.flattenAlong(nd.p, nd.p + vec2(0.01f, 0), nd.z - 0.3f, nd.z - 0.3f, nd.radius + 4.f, 12.f);
        float br = bulbRadius(nd);
        if (br > 0.f) {
            float zg = bulbGround(nd);
            map.flattenAlong(nd.p, nd.p + vec2(0.01f, 0), zg, zg, br + edges[nd.edges[0]].sidewalk + 1.5f, 12.f);
        }
    }
    // Roads are never buried (later flattening, embankment blends and node discs raise the terrain around their neighbours):
    // cut the terrain to just below every road, flat one heightmap texel beyond the sidewalk so the bilinear terrain cannot
    // rise into the verge, then a cut face. Only lowers terrain; where roads at different heights overlap in plan, the higher
    // one ends up on a deck (the road mesher gives it parapets and piers).
    for (auto& e : edges)
        for (size_t i = 0; i + 1 < e.pts.size(); i++) {
            vec3 a = e.pts[i], c = e.pts[i + 1];
            float flat = e.halfWidth + e.sidewalk + kHeightCell;
            map.lowerAlong(a.xy(), c.xy(), a.z - 0.3f, c.z - 0.3f, flat, flat + 10.f, 0.7f);
        }
    for (auto& nd : nodes) {
        if (nd.radius > 0) map.lowerAlong(nd.p, nd.p + vec2(0.01f, 0), nd.z - 0.3f, nd.z - 0.3f, nd.radius + kHeightCell, nd.radius + kHeightCell + 10.f, 0.7f);
        float br = bulbRadius(nd);
        if (br > 0.f) {
            float r = br + edges[nd.edges[0]].sidewalk + kHeightCell, zg = bulbGround(nd);
            map.lowerAlong(nd.p, nd.p + vec2(0.01f, 0), zg, zg, r, r + 10.f, 0.7f);
        }
    }
    map.recomputeSplat();
    buildHash();
    int lights = 0;
    for (auto& n : nodes) lights += n.control == 2;
    LOG("Road network: %zu nodes, %zu edges, %d signalized; hwy %.1f km, streets %.1f km (%.2f s)", nodes.size(), edges.size(),
        lights, totalLength(RC_HIGHWAY) / 1000.f,
        (totalLength(RC_STREET) + totalLength(RC_LANE) + totalLength(RC_AVENUE) + totalLength(RC_BOULEVARD)) / 1000.f,
        TimeSeconds() - t0);
}

void RoadNetwork::buildHash() {
    hashRes = (int)(2.f * kWorldHalf / kHashCell);
    hash.assign((size_t)hashRes * hashRes, {});
    for (size_t i = 0; i < edges.size(); i++) {
        const RoadEdge& e = edges[i];
        float r = e.halfWidth + e.sidewalk + 2.f;
        if (bulbRadius(nodes[e.n0]) > 0.f || bulbRadius(nodes[e.n1]) > 0.f) r += 4.5f;   // the turning bulb reaches past the width
        vec2 mn(1e9f), mx(-1e9f);
        for (auto& p : e.pts) { mn = vmin(mn, p.xy()); mx = vmax(mx, p.xy()); }
        mn -= vec2(r);
        mx += vec2(r);
        int x0 = Clamp((int)((mn.x + kWorldHalf) / kHashCell), 0, hashRes - 1), x1 = Clamp((int)((mx.x + kWorldHalf) / kHashCell), 0, hashRes - 1);
        int y0 = Clamp((int)((mn.y + kWorldHalf) / kHashCell), 0, hashRes - 1), y1 = Clamp((int)((mx.y + kWorldHalf) / kHashCell), 0, hashRes - 1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                // only add if the edge actually passes near this cell
                vec2 cmn(-kWorldHalf + x * kHashCell - r, -kWorldHalf + y * kHashCell - r);
                vec2 cmx = cmn + vec2(kHashCell + 2 * r);
                bool hit = false;
                for (size_t k = 0; k + 1 < e.pts.size() && !hit; k++) {
                    vec2 a = e.pts[k].xy(), c = e.pts[k + 1].xy();
                    vec2 smn = vmin(a, c), smx = vmax(a, c);
                    hit = smx.x >= cmn.x && smn.x <= cmx.x && smx.y >= cmn.y && smn.y <= cmx.y;
                }
                if (hit) hash[(size_t)y * hashRes + x].push_back((int)i);
            }
    }
}

void RoadNetwork::edgesInRect(vec2 mn, vec2 mx, std::vector<int>& out) const {
    out.clear();
    int x0 = Clamp((int)((mn.x + kWorldHalf) / kHashCell), 0, hashRes - 1), x1 = Clamp((int)((mx.x + kWorldHalf) / kHashCell), 0, hashRes - 1);
    int y0 = Clamp((int)((mn.y + kWorldHalf) / kHashCell), 0, hashRes - 1), y1 = Clamp((int)((mx.y + kWorldHalf) / kHashCell), 0, hashRes - 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            for (int e : hash[(size_t)y * hashRes + x]) out.push_back(e);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

int RoadNetwork::nearestEdge(vec2 p, float maxDist, float* outS, float* outDist, float* outSide) const {
    std::vector<int> cand;
    edgesInRect(p - vec2(maxDist), p + vec2(maxDist), cand);
    int best = -1;
    float bestD = maxDist, bestS = 0, bestSide = 0;
    for (int ei : cand) {
        const RoadEdge& e = edges[ei];
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            vec2 a = e.pts[k].xy(), c = e.pts[k + 1].xy();
            float t;
            float d = distPointSegment2D(p, a, c, &t);
            if (d < bestD) {
                bestD = d;
                best = ei;
                bestS = e.dist[k] + t * (e.dist[k + 1] - e.dist[k]);
                bestSide = cross(c - a, p - a) >= 0 ? 1.f : -1.f;
            }
        }
    }
    if (outS) *outS = bestS;
    if (outDist) *outDist = bestD;
    if (outSide) *outSide = bestSide;
    return best;
}

float RoadNetwork::junctionZ(const RoadNode& n, vec2 p) const {
    float best = 1e30f, bz = n.z;
    int ni = (int)(&n - &nodes[0]);
    for (int ei : n.edges) {
        const RoadEdge& e = edges[ei];
        size_t cnt = e.pts.size();
        if (cnt < 2) continue;
        bool fromStart = e.n0 == ni;
        // only the part of the polyline inside the junction (+ one segment)
        for (size_t j = 0; j + 1 < cnt; j++) {
            size_t k = fromStart ? j : cnt - 2 - j;
            float along = fromStart ? e.dist[k] : e.length - e.dist[k + 1];
            if (along > n.radius + 12.f) break;
            float t;
            float d = distPointSegment2D(p, e.pts[k].xy(), e.pts[k + 1].xy(), &t);
            if (d < best) {
                best = d;
                bz = Lerp(e.pts[k].z, e.pts[k + 1].z, t);
            }
        }
    }
    return bz;
}

bool RoadNetwork::surfaceHeight(vec2 p, float* z, float maxZ) const {
    thread_local std::vector<int> cand;
    cand.clear();
    edgesInRect(p - vec2(1.f), p + vec2(1.f), cand);
    bool found = false;
    float bestZ = -1e9f;
    for (int ei : cand) {
        const RoadEdge& e = edges[ei];
        // closest point over the whole polyline first, then decide road vs sidewalk once (per-segment tests
        // misclassified lane points near polyline vertices as the raised sidewalk of the neighboring segment)
        float bestD = 1e30f, bestZe = 0.f, bestS = 0.f;
        bool beyondEnd = false;
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            float t;
            float d = distPointSegment2D(p, e.pts[k].xy(), e.pts[k + 1].xy(), &t);
            if (d < bestD) {
                bestD = d;
                bestZe = Lerp(e.pts[k].z, e.pts[k + 1].z, t);
                bestS = Lerp(e.dist[k], e.dist[k + 1], t);
                beyondEnd = (k == 0 && t <= 0.f) || (k + 2 == e.pts.size() && t >= 1.f);
            }
        }
        // inside a junction (before the cut-backs) only the pavement counts: the corner sidewalks are the junction's own,
        // and an approach's sidewalk band there would reach over the other approaches' lanes; nor does a sidewalk reach
        // past the edge's end (over the road that continues from there)
        bool inJunction = bestS < e.cut0 || bestS > e.length - e.cut1;
        if (bestD <= e.halfWidth + ((inJunction || beyondEnd) ? 0.f : e.sidewalk)) {
            float zz = bestZe + (bestD > e.halfWidth ? 0.15f : 0.f);
            if (zz <= maxZ && zz > bestZ) {
                bestZ = zz;
                found = true;
            }
        }
    }
    for (int ei : cand) {
        const RoadEdge& e = edges[ei];
        const RoadNode* ns[2] = {&nodes[e.n0], &nodes[e.n1]};
        for (auto* n : ns) {
            float dn = length(p - n->p);
            // a dead end's turning bulb (and its sidewalk ring) follows the road into it like a junction disc
            float br = bulbRadius(*n);
            if (br > 0.f && dn < br + e.sidewalk) {
                float nz = junctionZ(*n, p) + (dn > br ? 0.15f : 0.f);
                if (nz <= maxZ && nz > bestZ) { bestZ = nz; found = true; }
            }
            if (n->radius <= 0 || dn >= n->radius) continue;
            // the junction surface follows its roads (height of the nearest incident centreline), so a junction on a grade
            // or at a ramp start meets every approach without a step
            float nz = junctionZ(*n, p);
            if (nz <= maxZ && nz > bestZ) { bestZ = nz; found = true; }
        }
    }
    float pz;
    if (gSites && gSites->padHeight(p, &pz, maxZ) && pz > bestZ) {
        bestZ = pz;
        found = true;
    }
    if (found) *z = bestZ;
    return found;
}

bool RoadNetwork::onPavement(vec2 p, float z, float margin, int ignoreEdge, float zTol) const {
    thread_local std::vector<int> cand;
    cand.clear();
    edgesInRect(p - vec2(margin + 30.f), p + vec2(margin + 30.f), cand);
    for (int ei : cand) {
        const RoadEdge& e = edges[ei];
        for (int nn : {e.n0, e.n1}) {
            float br = bulbRadius(nodes[nn]);
            if (br > 0.f && length(p - nodes[nn].p) < br + margin && fabsf(nodes[nn].z - z) < zTol) return true;
        }
        if (ei == ignoreEdge) continue;
        if (e.flags & RF_UNPAVED) continue;
        float r = e.halfWidth + margin;
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            float t;
            float d = distPointSegment2D(p, e.pts[k].xy(), e.pts[k + 1].xy(), &t);
            if (d < r && fabsf(Lerp(e.pts[k].z, e.pts[k + 1].z, t) - z) < zTol) return true;
        }
    }
    return false;
}

bool RoadNetwork::nearRoad(vec2 p, float margin) const {
    std::vector<int> cand;
    edgesInRect(p - vec2(margin + 30.f), p + vec2(margin + 30.f), cand);
    for (int ei : cand) {
        const RoadEdge& e = edges[ei];
        float r = e.halfWidth + e.sidewalk + margin;
        for (size_t k = 0; k + 1 < e.pts.size(); k++)
            if (distPointSegment2D(p, e.pts[k].xy(), e.pts[k + 1].xy()) < r) return true;
        for (int nn : {e.n0, e.n1}) {
            float br = bulbRadius(nodes[nn]);
            if (br > 0.f && length(p - nodes[nn].p) < br + e.sidewalk + margin) return true;
        }
    }
    return false;
}

float RoadNetwork::bulbRadius(const RoadNode& n) const {
    if (n.edges.size() != 1) return 0.f;
    const RoadEdge& e = edges[n.edges[0]];
    if ((e.flags & (RF_UNPAVED | RF_ONEWAY)) || e.lanesF == 0 || e.lanesB == 0) return 0.f;
    if (e.cls != RC_LANE && e.cls != RC_STREET && e.cls != RC_RURAL) return 0.f;
    return e.halfWidth + 4.5f;
}

float RoadNetwork::totalLength(RoadClass c) const {
    float s = 0;
    for (auto& e : edges)
        if (e.cls == c) s += e.length;
    return s;
}

}  // namespace World
