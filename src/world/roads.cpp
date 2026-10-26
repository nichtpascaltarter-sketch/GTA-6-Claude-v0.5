#include "roads.h"
#include "sites.h"
#include "transit.h"
#include "../core/noise.h"
#include <unordered_map>
#include "roadnet.cpp"   // the network's data and queries (the hand-built roads' compiler links it alone)
#include "roadgrade.cpp"   // the terrain graded under the roads (shared with the compiler too)

namespace World {

RoadNetwork* gRoads = nullptr;

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
                    if (line.size() >= 3) b.add(line, cls, 0, 0, StrFormat("%s %s", kStreetNames[(iy * 13 + 1) % ARRAY_COUNT(kStreetNames)], kStreetSuffix[((iy + 3) % 10 + 10) % 10]).c_str());
                    line.clear();
                }
            }
            if (line.size() >= 3) b.add(line, cls, 0, 0, StrFormat("%s %s", kStreetNames[(iy * 13 + 1) % ARRAY_COUNT(kStreetNames)], kStreetSuffix[((iy + 3) % 10 + 10) % 10]).c_str());
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
            if (gSites && gSites->blocksRoads(p)) return false;   // the prison and the speedway keep the farm roads out
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
            if (gTransit && gTransit->laidOut)
                for (const FerryPier& fp : gTransit->piers)
                    if (length(fp.base - p) < 90.f) return true;
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
    // A two-way arterial that ends where the only way on is a one-way ramp (an interchange whose other ramps could not come
    // down in time) would merge all its lanes into the ramp inside the junction, and its lanes back would carry no traffic.
    // Its last 80 m narrow to a street (a lane drop on the straight, which the lane graph handles), and a stub carries on
    // straight past the ramp mouth to a turning circle, so drivers who do not take the ramp can turn back.
    {
        std::vector<std::vector<int>> inc(nodes.size());
        for (size_t i = 0; i < edges.size(); i++) {
            inc[edges[i].n0].push_back((int)i);
            inc[edges[i].n1].push_back((int)i);
        }
        const size_t origEdges = edges.size(), origNodes = nodes.size();
        std::vector<char> touched(origEdges, 0);
        int fixed = 0;
        for (size_t nd = 0; nd < origNodes; nd++) {
            if (inc[nd].size() != 2) continue;
            int er = -1, es = -1;
            for (int ei : inc[nd]) {
                const RoadEdge& e = edges[ei];
                if (e.cls == RC_RAMP && (e.flags & RF_ONEWAY)) er = ei;
                else if ((e.cls == RC_BOULEVARD || e.cls == RC_AVENUE) && !(e.flags & RF_ONEWAY) && e.n0 != e.n1) es = ei;
            }
            if (er < 0 || es < 0 || (size_t)es >= origEdges || (size_t)er >= origEdges || touched[es] || touched[er]) continue;
            touched[es] = touched[er] = 1;
            // the arterial from the end node back along it
            bool atN1 = edges[es].n1 == (int)nd;
            std::vector<vec2> q;
            for (size_t k = 0; k < edges[es].pts.size(); k++) q.push_back(edges[es].pts[atN1 ? edges[es].pts.size() - 1 - k : k].xy());
            std::vector<float> cum(q.size(), 0.f);
            for (size_t k = 1; k < q.size(); k++) cum[k] = cum[k - 1] + length(q[k] - q[k - 1]);
            const float kNarrow = 80.f, kStub = 30.f;
            if (cum.back() < kNarrow + 30.f) continue;
            vec2 dirIn = normalize(q[0] - q[1]);   // travel toward the end node
            bool straight = true;
            for (size_t k = 1; k < q.size() && cum[k - 1] < kNarrow; k++) straight = straight && dot(normalize(q[k - 1] - q[k]), dirIn) > 0.985f;
            if (!straight) continue;
            // the stub: straight on if that is clear, else to the side away from the ramp; dry ground, outside site
            // structures, not through a site fence (a fence merely passing the turning circle bows round it)
            const RoadEdge& R = edges[er];
            vec2 rampDir = normalize((R.n0 == (int)nd ? R.pts[1] : R.pts[R.pts.size() - 2]).xy() - q[0]);
            vec2 side = dot(perp(dirIn), rampDir) > 0.f ? -perp(dirIn) : perp(dirIn);
            vec2 stubDir(0.f), tip(0.f);
            for (vec2 cdir : {dirIn, side}) {
                vec2 t2 = q[0] + cdir * kStub;
                bool ok = !map.isWater(t2.x, t2.y) && !map.isWater(q[0].x + cdir.x * kStub * 0.5f, q[0].y + cdir.y * kStub * 0.5f);
                if (ok && gSites) {
                    ok = !gSites->blocksRoads(t2);
                    for (const SiteElem& s : gSites->elems)
                        if (ok && s.kind == SK_FENCE && s.isLine() && segmentIntersect2D(q[0], t2, s.a, s.b, nullptr, nullptr)) ok = false;
                }
                if (ok) {
                    stubDir = cdir;
                    tip = t2;
                    break;
                }
            }
            if (length2(stubDir) < 0.5f) continue;
            // split the arterial kNarrow short of the end: far part stays, the near part becomes a street
            size_t k = 1;
            while (k + 1 < q.size() && cum[k] < kNarrow) k++;
            float t = (kNarrow - cum[k - 1]) / Max(cum[k] - cum[k - 1], 1e-4f);
            vec2 D = lerp(q[k - 1], q[k], Saturate(t));
            int nD = (int)nodes.size();
            RoadNode rnD;
            rnD.p = D;
            nodes.push_back(rnD);
            RoadEdge nearE = edges[es];   // end node -> D, as a street
            nearE.cls = RC_STREET;
            nearE.pts.clear();
            for (size_t j = 0; j < k; j++) nearE.pts.push_back(vec3(q[j], 0.f));
            if (length(q[k - 1] - D) > 0.1f) nearE.pts.push_back(vec3(D, 0.f));
            nearE.n0 = (int)nd;
            nearE.n1 = nD;
            RoadEdge& farE = edges[es];    // D -> the arterial's far node
            int farNode = atN1 ? farE.n0 : farE.n1;
            farE.pts.clear();
            farE.pts.push_back(vec3(D, 0.f));
            for (size_t j = (length(q[k] - D) > 0.1f ? k : k + 1); j < q.size(); j++) farE.pts.push_back(vec3(q[j], 0.f));
            farE.n0 = nD;
            farE.n1 = farNode;
            // the turnaround stub straight on past the ramp mouth
            int nT = (int)nodes.size();
            RoadNode rnT;
            rnT.p = tip;
            nodes.push_back(rnT);
            RoadEdge stub = nearE;
            stub.pts = {vec3(q[0], 0.f), vec3(q[0] + stubDir * (kStub * 0.5f), 0.f), vec3(tip, 0.f)};
            stub.n0 = (int)nd;
            stub.n1 = nT;
            edges.push_back(std::move(nearE));
            edges.push_back(std::move(stub));
            fixed++;
        }
        if (fixed) LOG("Road gen: %d arterial ends at a lone on/off ramp given a turnaround", fixed);
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

    // ============================================================= Roads laid along each other
    // The named roads (Bayshore Boulevard on the shore line, Old Cutler Road, the causeways, the county roads) are laid over
    // the districts' grids. Where one runs a few degrees off a grid line, or along it a few metres over, the planariser joins
    // the two at nodes and leaves their carriageways on top of each other for tens of metres. Their lanes then block each
    // other with nothing to resolve it (the AI agent's traffic_sim --overlaps; the deadlocks where 9th Avenue runs inside
    // Bayshore Boulevard and where the Solano Causeway comes in along 35th Street). So the lower road gives way where it lies
    // on top of the higher one: a stretch of it beside the higher road at under 32 degrees, with its centreline on the
    // higher road's carriageway (or within 0.8 of its own half width of the higher road's centreline) for 8 m or more
    // outside both roads' junction areas (the cut-backs the intersection pass sets below), goes. The whole run where the
    // carriageways overlap goes with it, and the rest of the edge too when less than 20 m of it would be left, or less than
    // 40 m running on beside the higher road. A cut end is joined back to the higher road at about 65 degrees, at a new
    // junction on it. What the lower road leaves behind is tidied: a road left with a free end under 20 m goes; a junction
    // left inside the higher road's carriageway becomes the higher road's junction that a short road (under 20 m) joined it
    // to, or else moves onto it (into its junction there when that is close); a dead end left further out gets a link to
    // it; and of two roads then left on top of each other between the same two junctions, the one that gives way goes.
    // Lots, buildings, bus stops and parked cars hang on the edges' seeds and their places in the order, so every other edge
    // keeps both. Of an edge's pieces, the one from its start keeps its slot and seed (its lots stay where they were); the
    // other new edges take the slots of the edges that went, then go last, with seeds made from their edge's. When fewer new
    // edges come than went, the road nearest to a gone edge (a street or lane before a main road) is split just short of
    // the junction at its far end (where no lot fits) until every slot is filled: no edge after a gone one moves.
    {
        const int NE0 = (int)edges.size();
        auto atGrade = [&](const RoadEdge& e) { return e.cls != RC_HIGHWAY && e.cls != RC_RAMP && !(e.flags & RF_ELEVATED) && e.pts.size() >= 2; };
        std::vector<std::vector<vec2>> P2((size_t)NE0);
        std::vector<std::vector<float>> C2((size_t)NE0);
        for (int i = 0; i < NE0; i++) {
            for (const vec3& p : edges[i].pts) P2[i].push_back(p.xy());
            C2[i].assign(P2[i].size(), 0.f);
            for (size_t k = 1; k < P2[i].size(); k++) C2[i][k] = C2[i][k - 1] + length(P2[i][k] - P2[i][k - 1]);
        }
        auto lenOf = [&](int i) { return C2[i].empty() ? 0.f : C2[i].back(); };
        // the junction areas, as the intersection pass will cut them back: a node of 3 or more edges clears the widest
        // road's half width (and some sidewalk) plus 2 m, at most 0.45 of an edge's length
        std::vector<float> nodeR(nodes.size(), 0.f);
        std::vector<int> nodeDeg(nodes.size(), 0);
        for (const RoadEdge& e : edges) {
            float r = e.halfWidth + e.sidewalk * 0.3f;
            nodeR[(size_t)e.n0] = Max(nodeR[(size_t)e.n0], r);
            nodeR[(size_t)e.n1] = Max(nodeR[(size_t)e.n1], r);
            nodeDeg[(size_t)e.n0]++;
            nodeDeg[(size_t)e.n1]++;
        }
        for (size_t n = 0; n < nodes.size(); n++) nodeR[n] = nodeDeg[n] >= 3 ? nodeR[n] + 2.f : 0.f;
        auto cutBack = [&](int i, int end) { return Min(nodeR[(size_t)(end ? edges[i].n1 : edges[i].n0)], lenOf(i) * 0.45f); };
        auto pointAt = [&](int i, float s) {
            const std::vector<vec2>& q = P2[i];
            const std::vector<float>& c = C2[i];
            if (s <= 0.f) return q.front();
            if (s >= c.back()) return q.back();
            size_t k = (size_t)(std::upper_bound(c.begin(), c.end(), s) - c.begin());
            float seg = c[k] - c[k - 1];
            return lerp(q[k - 1], q[k], seg > 1e-5f ? (s - c[k - 1]) / seg : 0.f);
        };
        // the arc length of the point of edge i's stretch [a, b] nearest to p (and its distance)
        auto arcOn = [&](int i, vec2 p, float a, float b, float* dist) {
            float bestD = 1e18f, bestS = a;
            for (size_t k = 0; k + 1 < P2[i].size(); k++) {
                if (C2[i][k + 1] < a || C2[i][k] > b) continue;
                float t;
                distPointSegment2D(p, P2[i][k], P2[i][k + 1], &t);
                float s = Clamp(Lerp(C2[i][k], C2[i][k + 1], t), a, b), d = length(p - pointAt(i, s));
                if (d < bestD) {
                    bestD = d;
                    bestS = s;
                }
            }
            if (dist) *dist = bestD;
            return bestS;
        };
        auto arcOf = [&](int i, vec2 p) { return arcOn(i, p, 0.f, lenOf(i), nullptr); };
        // who gives way: the lower class (a rural road ranks above a street), a bridge before the rest of its class, then
        // the shorter road (all the edges of its name), then the higher index
        static const int kGiveWayRank[RC_COUNT] = {0, 1, 2, 4, 5, 3, 6, 7};   // highway, boulevard, avenue, street, lane, rural, dirt, ramp
        std::unordered_map<std::string, float> roadLen;
        for (int i = 0; i < NE0; i++) roadLen[edges[i].name] += lenOf(i);
        auto wins = [&](int a, int b) {
            const RoadEdge &A = edges[a], &B = edges[b];
            int ra = kGiveWayRank[A.cls] * 2 + ((A.flags & RF_BRIDGE) ? 0 : 1), rb = kGiveWayRank[B.cls] * 2 + ((B.flags & RF_BRIDGE) ? 0 : 1);
            if (ra != rb) return ra < rb;
            float la = A.name.empty() ? lenOf(a) : roadLen[A.name], lb = B.name.empty() ? lenOf(b) : roadLen[B.name];
            if (fabsf(la - lb) > 1.f) return la > lb;
            return a < b;
        };
        const float kG = 32.f;
        auto gkey = [](int x, int y) { return (long long)(y + 100000) * 400000LL + (x + 100000); };
        std::unordered_map<long long, std::vector<std::pair<int, int>>> grid;   // (edge, segment)
        float maxHw = 0.f;
        for (int i = 0; i < NE0; i++) {
            if (!atGrade(edges[i])) continue;
            maxHw = Max(maxHw, edges[i].halfWidth);
            for (size_t k = 0; k + 1 < P2[i].size(); k++) {
                vec2 a = P2[i][k], c = P2[i][k + 1];
                int x0 = (int)floorf(Min(a.x, c.x) / kG), x1 = (int)floorf(Max(a.x, c.x) / kG);
                int y0 = (int)floorf(Min(a.y, c.y) / kG), y1 = (int)floorf(Max(a.y, c.y) / kG);
                for (int y = y0; y <= y1; y++)
                    for (int x = x0; x <= x1; x++) grid[gkey(x, y)].push_back({i, (int)k});
            }
        }
        // ---- find the stretches: every 2 m along each road, the winning road whose carriageway it overlaps, and whether
        // it lies on top of it (deep)
        const float kStep = 2.f, kParallel = 0.85f, kDeepMin = 8.f, kMinPiece = 20.f;
        struct Cut {
            float s0, s1;          // the stretch (arc length from n0), first and last sample inside
            int win0, win1;        // the road it lies in at its two ends
        };
        std::vector<std::vector<Cut>> cuts((size_t)NE0);
        std::vector<int> inside;
        std::vector<char> deep;
        for (int bi = 0; bi < NE0; bi++) {
            const RoadEdge& B = edges[bi];
            if (!atGrade(B)) continue;
            float L = lenOf(bi);
            if (L < 8.f) continue;
            int n = (int)(L / kStep) + 1;
            inside.assign((size_t)n, -1);
            deep.assign((size_t)n, 0);
            bool any = false;
            const float bLo = cutBack(bi, 0) + 1.f, bHi = L - cutBack(bi, 1) - 1.f;
            for (int j = 0; j < n; j++) {
                float s = Min(j * kStep, L);
                vec2 p = pointAt(bi, s);
                vec2 t = pointAt(bi, Min(s + 1.f, L)) - pointAt(bi, Max(s - 1.f, 0.f));
                float tl = length(t);
                if (tl < 1e-4f) continue;
                t = t / tl;
                float r = B.halfWidth + maxHw + 1.f, bestD = 1e9f;
                int x0 = (int)floorf((p.x - r) / kG), x1 = (int)floorf((p.x + r) / kG);
                int y0 = (int)floorf((p.y - r) / kG), y1 = (int)floorf((p.y + r) / kG);
                for (int y = y0; y <= y1; y++)
                    for (int x = x0; x <= x1; x++) {
                        auto it = grid.find(gkey(x, y));
                        if (it == grid.end()) continue;
                        for (const auto& ref : it->second) {
                            int ai = ref.first;
                            if (ai == bi) continue;
                            const RoadEdge& A = edges[ai];
                            size_t k = (size_t)ref.second;
                            vec2 a = P2[ai][k], c = P2[ai][k + 1];
                            float tt, d = distPointSegment2D(p, a, c, &tt);
                            if (d >= A.halfWidth + B.halfWidth || d >= bestD) continue;
                            if ((k == 0 && tt <= 1e-3f) || (k + 2 == P2[ai].size() && tt >= 1.f - 1e-3f)) continue;   // past its end
                            vec2 ad = c - a;
                            float al = length(ad);
                            if (al < 1e-4f || fabsf(dot(ad / al, t)) < kParallel || !wins(ai, bi)) continue;
                            bestD = d;
                            inside[(size_t)j] = ai;
                            float sA = Lerp(C2[ai][k], C2[ai][k + 1], tt);
                            deep[(size_t)j] = d <= Max(A.halfWidth, 0.8f * B.halfWidth) && s > bLo && s < bHi && sA > cutBack(ai, 0) + 1.f &&
                                              sA < lenOf(ai) - cutBack(ai, 1) - 1.f;
                        }
                    }
                any = any || deep[(size_t)j];
            }
            if (!any) continue;
            for (int j = 0; j < n;) {
                if (inside[(size_t)j] < 0) {
                    j++;
                    continue;
                }
                int j1 = j;   // one clear sample between two inside ones does not end the stretch
                while (j1 + 1 < n && (inside[(size_t)j1 + 1] >= 0 || (j1 + 2 < n && inside[(size_t)j1 + 2] >= 0))) j1++;
                int deepN = 0;
                for (int k = j; k <= j1; k++) deepN += deep[(size_t)k];
                Cut c;
                c.s0 = j * kStep;
                c.s1 = Min(j1 * kStep, L);
                c.win0 = inside[(size_t)j];
                c.win1 = inside[(size_t)j1];
                if (deepN * kStep >= kDeepMin) cuts[(size_t)bi].push_back(c);
                j = j1 + 1;
            }
        }
        // ---- a cut that would leave less than 20 m of the edge before or after it, or between it and the next one, takes
        // that too, and so does one that would leave under 40 m running along the bigger road's carriageway to the edge's
        // end (a join would only bring it back beside it): the road then gives way from its junction
        auto alongTo = [&](int bi, float a, float b, int win) {
            int n = Max(1, (int)ceilf((b - a) / 4.f));
            for (int k = 0; k <= n; k++) {
                float d;
                arcOn(win, pointAt(bi, Lerp(a, b, (float)k / n)), 0.f, lenOf(win), &d);
                if (d > edges[win].halfWidth + edges[bi].halfWidth + 5.f) return false;
            }
            return true;
        };
        for (int bi = 0; bi < NE0; bi++) {
            std::vector<Cut>& cs = cuts[(size_t)bi];
            if (cs.empty()) continue;
            float L = lenOf(bi);
            std::vector<Cut> merged;
            for (Cut c : cs) {
                float before = c.s0 - kStep, after = L - (c.s1 + kStep);
                if (before < kMinPiece || (before < 2.f * kMinPiece && alongTo(bi, 0.f, before, c.win0))) c.s0 = 0.f;
                if (after < kMinPiece || (after < 2.f * kMinPiece && alongTo(bi, c.s1 + kStep, L, c.win1))) c.s1 = L;
                if (!merged.empty() && (c.s0 - kStep) - (merged.back().s1 + kStep) < kMinPiece) {
                    if (c.s1 > merged.back().s1) {
                        merged.back().s1 = c.s1;
                        merged.back().win1 = c.win1;
                    }
                } else {
                    merged.push_back(c);
                }
            }
            cs.swap(merged);
        }
        // ---- the joins: each cut end meets the winning road at a node on it, ~65 degrees back toward the stretch
        struct Join {
            int edge;      // the winning road (-1: given up with its cut)
            float s;       // where on it: an arc length, or one of its ends (0 or its length) when that is within 3 m
            int node;
        };
        std::vector<Join> joins;
        auto ownCutAt = [&](int ai, float s) {
            for (const Cut& c : cuts[(size_t)ai])
                if (s > c.s0 - 5.f && s < c.s1 + 5.f) return true;
            return false;
        };
        auto planJoin = [&](int bi, int ai, float sP, float sInside) {
            vec2 p = pointAt(bi, sP), q = pointAt(bi, sInside);
            float sF = arcOf(ai, p), sQ = arcOf(ai, q), LA = lenOf(ai);
            float back = length(p - pointAt(ai, sF)) * 0.4663f;   // cot 65 degrees
            float sM = sQ < sF ? sF - back : sF + back;
            if (sM < 3.f) sM = 0.f;
            else if (sM > LA - 3.f) sM = LA;
            if (ownCutAt(ai, sM)) return -1;   // that road gives way there itself
            joins.push_back({ai, sM, -1});
            return (int)joins.size() - 1;
        };
        // ---- the pieces of road: every edge's stretches between its cuts, with the links from and to their joins
        struct Piece {
            int src;                 // the edge it comes from
            float a, b;              // its stretch of that edge
            int n0, n1;              // its end nodes
            int joinA, joinB;        // a link from a join to its start / from its end to a join (-1: none)
            std::vector<vec2> own;   // a link of its own instead of a stretch (a dead end joined to the bigger road)
            bool alive;
        };
        std::vector<Piece> pcs;
        pcs.reserve((size_t)NE0 + 256);
        auto addPiece = [&](int src, float a, float b, int jA, int jB) {
            Piece p;
            p.src = src;
            p.a = a;
            p.b = b;
            p.joinA = jA;
            p.joinB = jB;
            p.n0 = jA >= 0 ? -1 : edges[src].n0;   // (a join's node comes below)
            p.n1 = jB >= 0 ? -1 : edges[src].n1;
            p.alive = true;
            pcs.push_back(p);
        };
        struct Absorbed {
            int node, winner;   // a node of a cut edge inside its cut, and the road it lay in there
        };
        std::vector<Absorbed> absorbed;
        int cutsMade = 0, cutsLeft = 0;
        float metresCut = 0.f;
        for (int i = 0; i < NE0; i++) {
            float L = lenOf(i), cur = 0.f;
            int curJoin = -1;
            bool alive = true;
            for (const Cut& c : cuts[(size_t)i]) {
                bool atStart = c.s0 <= 0.f, atEnd = c.s1 >= L;
                float sEnd = c.s0 - kStep, sStart = c.s1 + kStep;
                int jEnd = atStart ? -1 : planJoin(i, c.win0, sEnd, c.s0);
                int jStart = atEnd ? -1 : planJoin(i, c.win1, sStart, c.s1);
                if ((!atStart && jEnd < 0) || (!atEnd && jStart < 0)) {
                    if (jEnd >= 0) joins[(size_t)jEnd].edge = -1;
                    if (jStart >= 0) joins[(size_t)jStart].edge = -1;
                    cutsLeft++;
                    continue;
                }
                cutsMade++;
                metresCut += c.s1 - c.s0;
                if (atStart) absorbed.push_back({edges[i].n0, c.win0});
                else addPiece(i, cur, sEnd, curJoin, jEnd);
                if (atEnd) {
                    absorbed.push_back({edges[i].n1, c.win1});
                    alive = false;
                    break;
                }
                cur = sStart;
                curJoin = jStart;
            }
            if (alive) addPiece(i, cur, L, curJoin, -1);
        }
        if (cutsMade > 0) {
            // ---- the join nodes: an end of the winning road within 3 m, a node shared with another join within 3 m, or a
            // new node that splits it
            std::vector<char> joinUsed(joins.size(), 0);
            for (const Piece& p : pcs) {
                if (p.joinA >= 0) joinUsed[(size_t)p.joinA] = 1;
                if (p.joinB >= 0) joinUsed[(size_t)p.joinB] = 1;
            }
            std::vector<std::vector<int>> joinsOn((size_t)NE0);
            for (size_t k = 0; k < joins.size(); k++)
                if (joins[k].edge >= 0 && joinUsed[k]) joinsOn[(size_t)joins[k].edge].push_back((int)k);
            int joined = 0;
            for (int i = 0; i < NE0; i++) {
                std::vector<int>& js = joinsOn[(size_t)i];
                std::sort(js.begin(), js.end(), [&](int x, int y) { return joins[(size_t)x].s < joins[(size_t)y].s; });
                float L = lenOf(i);
                int last = -1;
                for (int k : js) {
                    Join& J = joins[(size_t)k];
                    joined++;
                    if (J.s <= 0.f) J.node = edges[i].n0;
                    else if (J.s >= L) J.node = edges[i].n1;
                    else if (last >= 0 && J.s - joins[(size_t)last].s < 3.f && joins[(size_t)last].s > 0.f) {
                        J.s = joins[(size_t)last].s;
                        J.node = joins[(size_t)last].node;
                    } else {
                        RoadNode rn;
                        rn.p = pointAt(i, J.s);
                        J.node = (int)nodes.size();
                        nodes.push_back(rn);
                    }
                    last = k;
                }
            }
            for (Piece& p : pcs) {
                if (p.joinA >= 0) p.n0 = joins[(size_t)p.joinA].node;
                if (p.joinB >= 0) p.n1 = joins[(size_t)p.joinB].node;
            }
            // splitting a piece at s (of its edge) with a node there; the part after it is a new piece
            auto splitPiece = [&](int pi, float s, int node) {
                Piece q = pcs[(size_t)pi];
                pcs[(size_t)pi].b = s;
                pcs[(size_t)pi].n1 = node;
                pcs[(size_t)pi].joinB = -1;
                q.a = s;
                q.n0 = node;
                q.joinA = -1;
                pcs.push_back(q);
                return (int)pcs.size() - 1;
            };
            auto pieceAt = [&](int i, float s) {
                for (size_t k = 0; k < pcs.size(); k++) {
                    const Piece& p = pcs[k];
                    if (p.alive && p.src == i && p.own.empty() && s > p.a && s < p.b) return (int)k;
                }
                return -1;
            };
            for (size_t k = 0; k < joins.size(); k++) {
                const Join& J = joins[k];
                if (J.edge < 0 || !joinUsed[k] || J.s <= 0.f || J.s >= lenOf(J.edge)) continue;
                int pi = pieceAt(J.edge, J.s);
                if (pi >= 0) splitPiece(pi, J.s, J.node);
            }
            auto pieceLen = [&](const Piece& p) {
                float l = 0.f;
                if (!p.own.empty()) {
                    for (size_t k = 0; k + 1 < p.own.size(); k++) l += length(p.own[k + 1] - p.own[k]);
                    return l;
                }
                l = p.b - p.a;
                if (p.joinA >= 0) l += length(nodes[(size_t)p.n0].p - pointAt(p.src, p.a));
                if (p.joinB >= 0) l += length(nodes[(size_t)p.n1].p - pointAt(p.src, p.b));
                return l;
            };
            // the pieces at each node, and its junction radius as the intersection pass will make it
            std::vector<std::vector<int>> at;
            auto rebuildAt = [&]() {
                at.assign(nodes.size(), std::vector<int>());
                for (size_t k = 0; k < pcs.size(); k++)
                    if (pcs[k].alive) {
                        at[(size_t)pcs[k].n0].push_back((int)k);
                        if (pcs[k].n1 != pcs[k].n0) at[(size_t)pcs[k].n1].push_back((int)k);
                    }
            };
            auto radiusAt = [&](int n) {
                if (at[(size_t)n].size() < 3) return 0.f;
                float r = 0.f;
                for (int k : at[(size_t)n]) r = Max(r, edges[pcs[(size_t)k].src].halfWidth + edges[pcs[(size_t)k].src].sidewalk * 0.3f);
                return r + 2.f;
            };
            // ---- a road the cuts leave with a free end under 20 m (the rest of a cross street inside the bigger road) goes
            std::vector<char> affected(nodes.size(), 0);
            for (const Absorbed& ab : absorbed) affected[(size_t)ab.node] = 1;
            int hanging = 0;
            for (bool again = true; again;) {
                again = false;
                rebuildAt();
                for (size_t k = 0; k < pcs.size(); k++) {
                    Piece& p = pcs[k];
                    if (!p.alive) continue;
                    bool free0 = affected[(size_t)p.n0] && at[(size_t)p.n0].size() == 1, free1 = affected[(size_t)p.n1] && at[(size_t)p.n1].size() == 1;
                    if ((!free0 && !free1) || pieceLen(p) >= kMinPiece) continue;
                    p.alive = false;
                    affected[(size_t)p.n0] = affected[(size_t)p.n1] = 1;
                    hanging++;
                    again = true;
                }
            }
            // ---- a junction the lower road leaves inside the bigger road's carriageway moves onto it (into its junction there
            // when it lies in that junction's area), unless a short road still joins it to it; a dead end the cut leaves
            // further out gets a link to it, square to it
            auto sameRoad = [&](int i, int w) { return i == w || (!edges[w].name.empty() && edges[i].name == edges[w].name && atGrade(edges[i])); };
            int movedOn = 0, linked = 0;
            std::vector<char> seen(nodes.size(), 0);
            for (const Absorbed& ab : absorbed) {
                int N = ab.node, W = ab.winner;
                if (seen[(size_t)N]) continue;
                seen[(size_t)N] = 1;
                rebuildAt();
                const std::vector<int> here = at[(size_t)N];
                if (here.empty()) continue;
                bool onW = false, grade = true;
                for (int k : here) {
                    onW = onW || sameRoad(pcs[(size_t)k].src, W);
                    grade = grade && atGrade(edges[pcs[(size_t)k].src]);
                }
                if (onW || !grade) continue;
                int best = -1;
                float bestD = 1e9f, bestS = 0.f;
                for (size_t k = 0; k < pcs.size(); k++) {
                    const Piece& p = pcs[k];
                    if (!p.alive || !p.own.empty() || !sameRoad(p.src, W)) continue;
                    float d, s = arcOn(p.src, nodes[(size_t)N].p, p.a, p.b, &d);
                    if (d < bestD) {
                        bestD = d;
                        best = (int)k;
                        bestS = s;
                    }
                }
                if (best < 0) continue;
                const Piece P = pcs[(size_t)best];
                // the node on the winning road it goes to: one of the piece's ends when the point is in that end's junction
                // area (or within 3.5 m of it), else a node that splits the piece
                auto landing = [&](int node) {
                    if (P.joinA < 0 && bestS - P.a < Max(3.5f, radiusAt(P.n0) + 5.f)) return P.n0;
                    if (P.joinB < 0 && P.b - bestS < Max(3.5f, radiusAt(P.n1) + 5.f)) return P.n1;
                    if (node < 0) {
                        RoadNode rn;
                        rn.p = pointAt(P.src, bestS);
                        node = (int)nodes.size();
                        nodes.push_back(rn);
                        affected.push_back(0);
                        seen.push_back(0);
                    } else {
                        nodes[(size_t)node].p = pointAt(P.src, bestS);
                    }
                    splitPiece(best, bestS, node);
                    return node;
                };
                if (bestD < edges[W].halfWidth) {
                    // joined to a junction of the bigger road by a short road (the rest of a cross street that crossed both):
                    // the two junctions become that one, and the short road goes
                    int to = -1;
                    float stubLen = kMinPiece;
                    for (int k : here) {
                        const Piece& p = pcs[(size_t)k];
                        float l = pieceLen(p);
                        if (l >= stubLen) continue;
                        int o = p.n0 == N ? p.n1 : p.n0;
                        bool onA = false;
                        for (int q : at[(size_t)o]) onA = onA || sameRoad(pcs[(size_t)q].src, W);
                        if (onA) {
                            to = o;
                            stubLen = l;
                        }
                    }
                    if (to < 0) to = landing(N);
                    affected[(size_t)to] = 1;
                    if (to != N) {
                        for (int k : here) {
                            Piece& p = pcs[(size_t)k];
                            if (p.n0 == N) p.n0 = to;
                            if (p.n1 == N) p.n1 = to;
                            if (p.n0 == p.n1) p.alive = false;
                        }
                    }
                    movedOn++;
                } else if (here.size() == 1) {
                    const Piece rest = pcs[(size_t)here[0]];
                    int to = landing(-1);
                    affected[(size_t)to] = 1;
                    Piece l;
                    l.src = rest.src;
                    l.a = l.b = 0.f;
                    l.joinA = l.joinB = -1;
                    l.alive = true;
                    // (the way the road runs, when it is one way)
                    bool rev = (edges[rest.src].flags & RF_ONEWAY) && rest.n0 == N;
                    l.n0 = rev ? to : N;
                    l.n1 = rev ? N : to;
                    l.own = {nodes[(size_t)l.n0].p, nodes[(size_t)l.n1].p};
                    pcs.push_back(l);
                    linked++;
                }
            }
            // ---- two roads left between the same two junctions on top of each other (a junction moved onto the bigger
            // road brings its roads along): the one that gives way goes (at the junctions the pass changed)
            int doubled = 0;
            {
                std::unordered_map<long long, std::vector<int>> between;
                for (size_t k = 0; k < pcs.size(); k++) {
                    const Piece& p = pcs[k];
                    if (!p.alive || p.n0 == p.n1 || !atGrade(edges[(size_t)p.src]) || (!affected[(size_t)p.n0] && !affected[(size_t)p.n1])) continue;
                    long long key = (long long)Min(p.n0, p.n1) * 1000003LL + Max(p.n0, p.n1);
                    between[key].push_back((int)k);
                }
                auto midOf = [&](const Piece& p) {
                    if (!p.own.empty()) return (p.own.front() + p.own.back()) * 0.5f;
                    return pointAt(p.src, (p.a + p.b) * 0.5f);
                };
                std::vector<long long> keys;
                for (const auto& kv : between)
                    if (kv.second.size() > 1) keys.push_back(kv.first);
                std::sort(keys.begin(), keys.end());
                for (long long key : keys) {
                    std::vector<int>& ks = between[key];
                    for (size_t x = 0; x < ks.size(); x++)
                        for (size_t y = x + 1; y < ks.size(); y++) {
                            Piece &P = pcs[(size_t)ks[x]], &Q = pcs[(size_t)ks[y]];
                            if (!P.alive || !Q.alive || !P.own.empty() || !Q.own.empty()) continue;
                            bool qGoes = wins(P.src, Q.src);
                            Piece& L = qGoes ? Q : P;
                            const Piece& Wn = qGoes ? P : Q;
                            float d;
                            arcOn(Wn.src, midOf(L), Wn.a, Wn.b, &d);
                            if (d >= Max(edges[(size_t)P.src].halfWidth, edges[(size_t)Q.src].halfWidth)) continue;
                            L.alive = false;
                            doubled++;
                        }
                }
            }
            // ---- the slots. An edge's piece from its start (else its first) keeps the edge's slot; the other pieces fill the
            // slots of the edges that went, in order, then go last
            std::vector<int> keeper((size_t)NE0, -1);
            for (size_t k = 0; k < pcs.size(); k++) {
                const Piece& p = pcs[k];
                if (!p.alive || !p.own.empty()) continue;
                int& kp = keeper[(size_t)p.src];
                if (kp < 0 || pcs[(size_t)kp].a > p.a) kp = (int)k;
            }
            std::vector<int> freed, extra;
            for (int i = 0; i < NE0; i++)
                if (keeper[(size_t)i] < 0) freed.push_back(i);
            for (size_t k = 0; k < pcs.size(); k++)
                if (pcs[k].alive && keeper[(size_t)pcs[k].src] != (int)k) extra.push_back((int)k);
            // fewer new edges than went: the road nearest to a gone edge (a street or lane before a main road, which carries
            // less traffic to queue there) is split just short of the junction at its far end, far enough out that the
            // junction keeps its full cut-back and where no lot fits
            int splitForSlots = 0;
            rebuildAt();
            for (size_t f = extra.size(); f < freed.size(); f++) {
                int gone = freed[f];
                vec2 mid = pointAt(gone, lenOf(gone) * 0.5f);
                int best = -1;
                float bestD = 1e18f, bestS = 0.f;
                for (size_t k = 0; k < pcs.size(); k++) {
                    const Piece& p = pcs[k];
                    const RoadEdge& e = edges[p.src];
                    if (!p.alive || !p.own.empty() || p.joinB >= 0 || !atGrade(e) || (e.flags & RF_BRIDGE) || at[(size_t)p.n1].size() < 3) continue;
                    vec2 lo = P2[p.src].front(), hi = lo;
                    for (const vec2& q : P2[p.src]) {
                        lo = vec2(Min(lo.x, q.x), Min(lo.y, q.y));
                        hi = vec2(Max(hi.x, q.x), Max(hi.y, q.y));
                    }
                    float penalty = e.cls <= RC_AVENUE ? 150.f : 0.f;
                    float boxD = length(vec2(Max(Max(lo.x - mid.x, mid.x - hi.x), 0.f), Max(Max(lo.y - mid.y, mid.y - hi.y), 0.f)));
                    if (boxD + penalty >= bestD) continue;
                    float s = p.b - Max(radiusAt(p.n1) / 0.45f + 1.f, 12.f);
                    if (s - p.a < Max(kMinPiece, radiusAt(p.n0) + 10.f)) continue;
                    float d;
                    arcOn(p.src, mid, p.a, p.b, &d);
                    d += penalty;
                    if (d < bestD) {
                        bestD = d;
                        best = (int)k;
                        bestS = s;
                    }
                }
                if (best < 0) break;   // (cannot happen: there are always long roads; the gone edge would come back as it was)
                RoadNode rn;
                rn.p = pointAt(pcs[(size_t)best].src, bestS);
                int node = (int)nodes.size();
                nodes.push_back(rn);
                int q = splitPiece(best, bestS, node);
                extra.push_back(q);
                at.push_back(std::vector<int>());
                at[(size_t)node] = {best, q};
                splitForSlots++;
            }
            // ---- the edges: each piece's stretch of its edge (the edges no cut or join touched come back as they were); seeds:
            // the slot keeper keeps its edge's, the others get new ones made from it
            std::vector<int> partOf(pcs.size(), 0), parts((size_t)NE0, 0);
            {
                std::vector<int> order;
                for (size_t k = 0; k < pcs.size(); k++)
                    if (pcs[k].alive) order.push_back((int)k);
                std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
                    const Piece &X = pcs[(size_t)x], &Y = pcs[(size_t)y];
                    if (X.src != Y.src) return X.src < Y.src;
                    if (X.own.empty() != Y.own.empty()) return X.own.empty();
                    return X.a < Y.a;
                });
                for (int k : order)
                    if (keeper[(size_t)pcs[(size_t)k].src] != k) partOf[(size_t)k] = ++parts[(size_t)pcs[(size_t)k].src];
            }
            auto build = [&](int k) {
                const Piece& p = pcs[(size_t)k];
                RoadEdge e = edges[(size_t)p.src];
                e.pts.clear();
                e.dist.clear();
                std::vector<vec2> q;
                if (!p.own.empty()) {
                    q = p.own;
                } else {
                    q.push_back(nodes[(size_t)p.n0].p);
                    if (p.joinA >= 0) q.push_back(pointAt(p.src, p.a));
                    for (size_t v = 0; v < P2[p.src].size(); v++)
                        if (C2[p.src][v] > p.a + 0.01f && C2[p.src][v] < p.b - 0.01f) q.push_back(P2[p.src][v]);
                    if (p.joinB >= 0) q.push_back(pointAt(p.src, p.b));
                    q.push_back(nodes[(size_t)p.n1].p);
                }
                for (size_t v = 0; v < q.size(); v++) {
                    bool lastPt = v + 1 == q.size();
                    if (!e.pts.empty() && length(q[v] - e.pts.back().xy()) < 0.05f) {
                        if (lastPt && e.pts.size() > 1) e.pts.back() = vec3(q[v], 0.f);
                        continue;
                    }
                    e.pts.push_back(vec3(q[v], 0.f));
                }
                if (e.pts.size() < 2) e.pts.push_back(vec3(q.back(), 0.f));
                e.n0 = p.n0;
                e.n1 = p.n1;
                if (partOf[(size_t)k] > 0) e.seed = hash32(edges[(size_t)p.src].seed ^ (0x9E3779B9u * (u32)partOf[(size_t)k]));
                return e;
            };
            // a piece that is its whole edge, between the edge's own nodes where they were
            auto unchanged = [&](int k) {
                const Piece& p = pcs[(size_t)k];
                const RoadEdge& e = edges[(size_t)p.src];
                return p.own.empty() && p.joinA < 0 && p.joinB < 0 && p.a <= 0.f && p.b >= lenOf(p.src) && p.n0 == e.n0 && p.n1 == e.n1 &&
                       length(nodes[(size_t)p.n0].p - P2[p.src].front()) < 1e-3f && length(nodes[(size_t)p.n1].p - P2[p.src].back()) < 1e-3f;
            };
            std::vector<RoadEdge> out((size_t)NE0);
            std::vector<char> outOld((size_t)NE0, 0);
            for (int i = 0; i < NE0; i++) {
                int k = keeper[(size_t)i];
                if (k < 0) continue;
                if (unchanged(k)) outOld[(size_t)i] = 1;
                else out[(size_t)i] = build(k);
            }
            size_t next = 0;
            for (int i : freed) {
                if (next < extra.size()) out[(size_t)i] = build(extra[next++]);
                else outOld[(size_t)i] = 1;
            }
            int appended = (int)(extra.size() - next);
            for (; next < extra.size(); next++) out.push_back(build(extra[next]));
            for (int i = 0; i < NE0; i++)
                if (outOld[(size_t)i]) out[(size_t)i] = std::move(edges[(size_t)i]);
            edges.swap(out);
            for (auto& n : nodes) n.edges.clear();
            for (size_t i = 0; i < edges.size(); i++) {
                nodes[(size_t)edges[i].n0].edges.push_back((int)i);
                nodes[(size_t)edges[i].n1].edges.push_back((int)i);
            }
            LOG("Road gen: %d stretches of road laid inside a bigger road's carriageway cut out (%.0f m; %d joins back onto it, %d junctions "
                "left inside it moved onto it, %d dead ends linked to it, %d roads left hanging and %d left doubled up dropped; %d edges gone, "
                "%d split to keep the edge order, %d new at the end; %d left where the bigger road gives way itself)",
                cutsMade, metresCut, joined, movedOn, linked, hanging, doubled, (int)freed.size(), splitForSlots, appended, cutsLeft);
        }
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

    // ============================================================= Junction plateaus
    // Every at-grade road is level with its junction across the junction's reach, then eases back onto its own profile:
    // each approach's pavement stands at the node's height out to its half width round its end (surfaceHeight's end caps
    // and the junction disc), so a road that is still climbing or dipping inside that reach comes out from under the cap with
    // a step (the Port Isle Bridge landing at S 2nd Street, where buses hung their front wheels). The easing adds at most 5%
    // to the road's own grade. A link too short for its plateaus and their easing runs straight from one plateau to the other
    // (plateaus shortened so that ramp stays under 10%), so short links over the same ground (a causeway merging into a
    // street between two junctions) keep the same height side by side.
    {
        std::vector<float> nodeReach(nodes.size(), -1.f);
        for (size_t n = 0; n < nodes.size(); n++) {
            const RoadNode& nd = nodes[n];
            if (nd.edges.size() < 2) continue;
            bool hwy = false;
            float reach = 0.f;
            for (int ei : nd.edges) {
                const RoadEdge& e = edges[ei];
                hwy |= e.cls == RC_HIGHWAY || e.cls == RC_RAMP;
                reach = Max(reach, e.halfWidth + (nd.edges.size() >= 3 ? e.sidewalk * 0.3f + 2.f : 0.f));
            }
            if (!hwy) nodeReach[n] = reach + 1.f;
        }
        const float kEase = 0.05f, kShortGrade = 0.10f;
        int changed = 0, shortLinks = 0;
        float worst = 0.f;
        std::vector<float> s2, nz;
        for (auto& e : edges) {
            const size_t cnt = e.pts.size();
            if (cnt < 3 || (e.flags & RF_UNPAVED) || e.n0 == e.n1) continue;
            const bool actA = nodeReach[e.n0] >= 0.f, actB = nodeReach[e.n1] >= 0.f;
            if (!actA && !actB) continue;
            s2.assign(cnt, 0.f);
            for (size_t j = 1; j < cnt; j++) s2[j] = s2[j - 1] + length(e.pts[j].xy() - e.pts[j - 1].xy());
            const float L = s2.back();
            if (L < 1.f) continue;
            auto zAt = [&](float s) {   // the profile as it stands, at distance s along the plan
                size_t j = 1;
                while (j + 1 < cnt && s2[j] < s) j++;
                float t = Clamp((s - s2[j - 1]) / Max(s2[j] - s2[j - 1], 1e-4f), 0.f, 1.f);
                return Lerp(e.pts[j - 1].z, e.pts[j].z, t);
            };
            const float zA = actA ? nodes[e.n0].z : e.pts.front().z, zB = actB ? nodes[e.n1].z : e.pts.back().z;
            float RA = actA ? Min(nodeReach[e.n0], L * 0.45f) : 0.f, RB = actB ? Min(nodeReach[e.n1], L * 0.45f) : 0.f;
            const float offA = actA ? zA - zAt(RA) : 0.f, offB = actB ? zB - zAt(L - RB) : 0.f;
            nz.assign(cnt, 0.f);
            if (RA + fabsf(offA) / kEase + RB + fabsf(offB) / kEase > L) {
                // short link: plateau, straight ramp, plateau
                float room = L - fabsf(zB - zA) / kShortGrade;
                if (room <= 0.f) RA = RB = 0.f;
                else if (RA + RB > room) {
                    float k = room / (RA + RB);
                    RA *= k;
                    RB *= k;
                }
                float ramp = Max(L - RA - RB, 1e-3f);
                for (size_t j = 1; j + 1 < cnt; j++) {
                    float d = s2[j];
                    nz[j] = d <= RA ? zA : (d >= L - RB ? zB : Lerp(zA, zB, (d - RA) / ramp));
                }
                shortLinks++;
            } else {
                for (size_t j = 1; j + 1 < cnt; j++) {
                    float d = s2[j], z = e.pts[j].z;
                    if (actA) z = d <= RA ? zA : z + copysignf(Max(0.f, fabsf(offA) - kEase * (d - RA)), offA);
                    if (actB && L - d <= RB) z = zB;
                    else if (actB) z += copysignf(Max(0.f, fabsf(offB) - kEase * (L - d - RB)), offB);
                    nz[j] = z;
                }
            }
            for (size_t j = 1; j + 1 < cnt; j++) {
                float dz = fabsf(nz[j] - e.pts[j].z);
                if (dz < 0.02f) continue;
                worst = Max(worst, dz);
                e.pts[j].z = nz[j];
                changed++;
            }
        }
        for (auto& e : edges) {
            e.dist[0] = 0;
            for (size_t i = 1; i < e.pts.size(); i++) e.dist[i] = e.dist[i - 1] + length(e.pts[i] - e.pts[i - 1]);
            e.length = e.dist.back();
        }
        LOG("Road gen: junction plateaus, %d profile points levelled (largest change %.2f m, %d short links ramped plateau to plateau)",
            changed, worst, shortLinks);
    }

    // ============================================================= Intersections: radius, cut-backs, control
    finishJunctions();

    // ============================================================= Terrain flattening along at-grade roads (roadgrade.cpp)
    gradeTerrainForRoads(*this, map);
    map.recomputeSplat();
    buildHash();
    int lights = 0;
    for (auto& n : nodes) lights += n.control == 2;
    LOG("Road network: %zu nodes, %zu edges, %d signalized; hwy %.1f km, streets %.1f km (%.2f s)", nodes.size(), edges.size(),
        lights, totalLength(RC_HIGHWAY) / 1000.f,
        (totalLength(RC_STREET) + totalLength(RC_LANE) + totalLength(RC_AVENUE) + totalLength(RC_BOULEVARD)) / 1000.f,
        TimeSeconds() - t0);
}

bool RoadNetwork::surfaceHeight(vec2 p, float* z, float maxZ) const {
    float bestZ = -1e9f;
    bool found = roadSurfaceHeight(p, &bestZ, maxZ);
    float pz;
    if (gSites && gSites->padHeight(p, &pz, maxZ) && pz > bestZ) {
        bestZ = pz;
        found = true;
    }
    if (found) *z = bestZ;
    return found;
}
}  // namespace World
