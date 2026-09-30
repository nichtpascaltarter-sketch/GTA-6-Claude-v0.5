// Transit network layout (world generation, deterministic): the SkyLine elevated metro loop through the airport,
// Palmetto Flats, the Canvas District, Midtown, the Civic Center, Solaris, Calle Luna and Westbrook; city bus routes
// with their stops; bay ferry piers and routes. Geometry is built per streaming cell in transitmesh.cpp.
#include "transit.h"
#include "worldtypes.h"

namespace World {

namespace transit_storage {
TransitNet gNet;
}
TransitNet* gTransit = &transit_storage::gNet;

// ---------------------------------------------------------------------------------------------------------------------
// MetroLine queries

void MetroLine::frame(float s, vec2& pos, vec2& tan, float* zOut, float* bankOut, float* curvOut) const {
    int n = count();
    if (n < 2) {
        pos = vec2(0.f);
        tan = vec2(1, 0);
        if (zOut) *zOut = 0.f;
        if (bankOut) *bankOut = 0.f;
        if (curvOut) *curvOut = 0.f;
        return;
    }
    s = wrap(s);
    float f = s / ds;
    int i = (int)floorf(f);
    float u = f - (float)i;
    if (i >= n) i = n - 1;
    int j = (i + 1) % n;
    pos = lerp(p[i], p[j], u);
    tan = normalize(lerp(t[i], t[j], u));
    if (zOut) *zOut = z.empty() ? 0.f : Lerp(z[i], z[j], u);
    if (bankOut) *bankOut = bank.empty() ? 0.f : Lerp(bank[i], bank[j], u);
    if (curvOut) *curvOut = Lerp(k[i], k[j], u);
}

vec3 MetroLine::center(float s) const {
    vec2 pos, tan;
    float zz;
    frame(s, pos, tan, &zz);
    return vec3(pos, zz);
}

float MetroLine::railZ(float s) const {
    vec2 pos, tan;
    float zz;
    frame(s, pos, tan, &zz);
    return zz;
}

vec3 MetroLine::railPoint(float s, float lateral) const {
    vec2 pos, tan;
    float zz, bk;
    frame(s, pos, tan, &zz, &bk);
    vec2 r(tan.y, -tan.x);
    return vec3(pos + r * (lateral * cosf(bk)), zz + sinf(bk) * lateral);
}

int MetroLine::stationAt(float s, float margin) const {
    for (int i = 0; i < (int)stations.size(); i++)
        if (fabsf(delta(stations[i].s, s)) <= transit_dims::kPlatformHalfLen + margin) return i;
    return -1;
}

float MetroLine::project(vec2 q, float* lateral, float* dist) const {
    int n = count();
    float best = 1e30f, bestS = 0.f, bestLat = 0.f;
    for (int i = 0; i < n; i++) {
        vec2 a = p[i], b = p[(i + 1) % n];
        float tt;
        float d = distPointSegment2D(q, a, b, &tt);
        if (d < best) {
            best = d;
            bestS = (i + tt) * ds;
            vec2 tg = normalize(b - a);
            bestLat = dot(q - a, vec2(tg.y, -tg.x));
        }
    }
    if (lateral) *lateral = bestLat;
    if (dist) *dist = best;
    return wrap(bestS);
}

namespace transit_layout {

using namespace transit_dims;

// The loop is a rounded rectangle whose sides run beside the city grid streets: the viaduct stands on a reserved strip
// next to one sidewalk (side-running, like a rail right-of-way along an avenue) so that the station stairs land on
// that sidewalk. Offsets: street centerline + half width + sidewalk + 12.8 m (room for the stairs).
constexpr float kWestX = 978.6f;     // beside the x = 1000 street (Flats / Calle Luna edge, along the airport)
constexpr float kEastX = 3274.9f;    // beside the x = 3300 avenue (Midtown / Downtown)
constexpr float kSouthY = -628.6f;   // beside the y = -650 street (Calle Luna / Solaris)
constexpr float kNorthY = 1528.6f;   // beside the y = 1550 street (Flats / Canvas District)
constexpr float kCornerR = 180.f;

struct StationDef {
    const char* name;
    const char* code;
    vec2 at;             // a point on the corridor (platform center, mid-block between cross streets)
    float dwell;
};
// Counter-clockwise order (the outer track's service): east along the south side, north along the east side, west
// along the north side, south along the west side.
const StationDef kStations[] = {
    {"Calle Luna", "CLN", vec2(1850.f, kSouthY), 20.f},
    {"Solaris", "SOL", vec2(3050.f, kSouthY), 22.f},
    {"Civic Center", "CVC", vec2(kEastX, 400.f), 24.f},
    {"Midtown", "MDT", vec2(kEastX, 1200.f), 20.f},
    {"Canvas District", "CNV", vec2(2750.f, kNorthY), 20.f},
    {"Palmetto Flats", "PFL", vec2(1650.f, kNorthY), 18.f},
    {"Airport", "PSI", vec2(kWestX, 1300.f), 25.f},
    {"Westbrook", "WBK", vec2(kWestX, -200.f), 18.f},
};

void appendArc(std::vector<vec2>& out, vec2 c, float r, float a0, float a1, float step) {
    int n = Max(2, (int)ceilf(fabsf(a1 - a0) * r / step));
    for (int k = 1; k <= n; k++) {
        float a = a0 + (a1 - a0) * k / n;
        out.push_back(c + vec2(cosf(a), sinf(a)) * r);
    }
}

// Dense closed centerline (counter-clockwise), s = 0 at the west end of the south side's straight
std::vector<vec2> loopPolyline() {
    const float x0 = kWestX, x1 = kEastX, y0 = kSouthY, y1 = kNorthY, R = kCornerR;
    const float h = kPi * 0.5f;
    std::vector<vec2> pts;
    pts.push_back(vec2(x0 + R, y0));
    pts.push_back(vec2(x1 - R, y0));
    appendArc(pts, vec2(x1 - R, y0 + R), R, -h, 0.f, 1.f);
    pts.push_back(vec2(x1, y1 - R));
    appendArc(pts, vec2(x1 - R, y1 - R), R, 0.f, h, 1.f);
    pts.push_back(vec2(x0 + R, y1));
    appendArc(pts, vec2(x0 + R, y1 - R), R, h, 2.f * h, 1.f);
    pts.push_back(vec2(x0, y0 + R));
    appendArc(pts, vec2(x0 + R, y0 + R), R, 2.f * h, 3.f * h, 1.f);
    pts.pop_back();  // the arc ends on the first point
    return pts;
}

// Resample a closed polyline at an exact even spacing (the last sample connects back to the first)
void resampleClosed(const std::vector<vec2>& in, float step, std::vector<vec2>& out, float& total, float& ds) {
    size_t n = in.size();
    std::vector<float> cum(n + 1, 0.f);
    for (size_t i = 0; i < n; i++) cum[i + 1] = cum[i] + length(in[(i + 1) % n] - in[i]);
    total = cum[n];
    int m = Max(8, (int)roundf(total / step));
    ds = total / (float)m;
    out.resize((size_t)m);
    size_t seg = 0;
    for (int k = 0; k < m; k++) {
        float s = k * ds;
        while (seg + 1 < n && cum[seg + 1] < s) seg++;
        float len = Max(cum[seg + 1] - cum[seg], 1e-5f);
        out[(size_t)k] = lerp(in[seg], in[(seg + 1) % n], (s - cum[seg]) / len);
    }
}

// Highway double streetlights (roadmesh.cpp placeFurniture): median posts every 55 m with lamps 11.4 m above the deck.
// The viaduct flies over them with room to spare when one stands under its footprint.
bool highwayLampNear(const RoadEdge& e, vec2 q, float radius) {
    if (e.cls != RC_HIGHWAY || e.lanesB == 0 || e.lanesF == 0) return false;
    const float spacing = 55.f;
    float s0 = e.cut0 + 8.f, s1 = e.length - e.cut1 - 8.f;
    for (float s = s0 + fmodf((float)(e.seed % 100), spacing * 0.5f); s < s1; s += spacing)
        if (length(e.posAt(s).xy() - q) < radius) return true;
    return false;
}

// Minimum top-of-rail height imposed by the roads under point q (deck footprint sample)
float roadRequirement(const RoadNetwork& net, const WorldMap& map, vec2 q) {
    thread_local std::vector<int> cand;
    net.edgesInRect(q - vec2(30.f), q + vec2(30.f), cand);
    float req = -1e9f;
    for (int ei : cand) {
        const RoadEdge& e = net.edges[ei];
        float bestD = 1e30f, bestZ = 0.f;
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            float tt;
            float d = distPointSegment2D(q, e.pts[k].xy(), e.pts[k + 1].xy(), &tt);
            if (d < bestD) {
                bestD = d;
                bestZ = Lerp(e.pts[k].z, e.pts[k + 1].z, tt);
            }
        }
        if (bestD > e.halfWidth + e.sidewalk + 1.0f) continue;
        vec2 cp = q;
        float g = map.heightAt(cp.x, cp.y);
        bool raised = bestZ - Max(g, 0.f) > 2.5f || (e.flags & (RF_ELEVATED | RF_BRIDGE)) != 0;
        float need = bestZ + 5.2f + kDeckTopBelowRail + kGirderDepth;         // trucks under the girder
        if (raised) need = bestZ + 0.95f + 5.2f + kDeckTopBelowRail + kGirderDepth;   // deck barrier + trucks
        if (raised && highwayLampNear(e, q, 9.f)) need = Max(need, bestZ + 0.85f + 11.9f + 0.6f + kDeckTopBelowRail + kGirderDepth);
        req = Max(req, need);
    }
    return req;
}

float groundAt(const WorldMap& map, vec2 q) {
    float g = map.heightAt(q.x, q.y);
    float w = map.waterAt(q.x, q.y);
    if (w > kNoWater + 1.f && w > g) g = w;
    return g;
}

}  // namespace transit_layout

using namespace transit_layout;

// ---------------------------------------------------------------------------------------------------------------------
void transitLayout(SiteSet& S, WorldMap& map) {
    (void)map;
    TransitNet& N = *gTransit;
    MetroLine& L = N.metro;
    std::vector<vec2> dense = loopPolyline();
    resampleClosed(dense, 2.f, L.p, L.length, L.ds);
    int n = L.count();
    L.t.resize(n);
    L.k.resize(n);
    for (int i = 0; i < n; i++) {
        vec2 a = L.p[(i + n - 1) % n], b = L.p[(i + 1) % n];
        L.t[i] = normalize(b - a);
    }
    for (int i = 0; i < n; i++) {
        vec2 ta = L.t[(i + n - 1) % n], tb = L.t[(i + 1) % n];
        L.k[i] = cross(ta, tb) / (2.f * L.ds);   // dtheta / ds
    }
    // stations
    L.stations.clear();
    for (size_t i = 0; i < ARRAY_COUNT(kStations); i++) {
        const StationDef& d = kStations[i];
        MetroStation st;
        st.name = d.name;
        st.code = d.code;
        st.s = L.project(d.at);
        vec2 tan;
        L.frame(st.s, st.pos, tan);
        st.dir = tan;
        st.dwell = d.dwell;
        st.seed = hash32(0x57A7u + (u32)i * 7919u);
        L.stations.push_back(st);
    }
    std::sort(L.stations.begin(), L.stations.end(), [](const MetroStation& a, const MetroStation& b) { return a.s < b.s; });
    // coarse corridor polyline for the building prune (every 10 m)
    N.corridor.clear();
    for (float s = 0.f; s < L.length; s += 10.f) {
        vec2 pos, tan;
        L.frame(s, pos, tan);
        N.corridor.push_back(pos);
    }
    N.corridorHalf = kDeckHalf + 2.2f;
    // Reservations: no building lots and no natural trees under the viaduct and around the stations
    for (float s = 0.f; s < L.length; s += 20.f) {
        vec2 pos, tan;
        L.frame(s + 10.f, pos, tan);
        S.lotBlocks.push_back({pos, tan, 11.f, 12.f});
        S.vegBlocks.push_back({pos, tan, 11.f, 9.5f});
    }
    for (const MetroStation& st : L.stations) {
        S.lotBlocks.push_back({st.pos, st.dir, kPlatformHalfLen + 6.f, 15.5f});
        S.vegBlocks.push_back({st.pos, st.dir, kPlatformHalfLen + 6.f, 14.5f});
    }
    N.laidOut = true;
    LOG("Transit: SkyLine loop %.2f km, %zu stations, %d samples", L.length / 1000.f, L.stations.size(), n);
}

// ---------------------------------------------------------------------------------------------------------------------
void transitPruneBuildings(std::vector<Building>& buildings) {
    TransitNet& N = *gTransit;
    if (!N.laidOut || N.corridor.size() < 2) return;
    const MetroLine& L = N.metro;
    size_t before = buildings.size();
    auto hits = [&](const Building& b) {
        vec2 ay = perp(b.ax);
        // footprint corners + edge midpoints + center against the corridor capsules
        float r = sqrtf(b.hx * b.hx + b.hy * b.hy);
        vec2 samples[9];
        int k = 0;
        for (int sx = -1; sx <= 1; sx++)
            for (int sy = -1; sy <= 1; sy++) samples[k++] = b.c + b.ax * (sx * b.hx) + ay * (sy * b.hy);
        size_t n = N.corridor.size();
        for (size_t i = 0; i < n; i++) {
            vec2 a = N.corridor[i], c = N.corridor[(i + 1) % n];
            if (distPointSegment2D(b.c, a, c) > r + 16.f) continue;
            // exact test: oriented footprint vs segment capsule via sampling the segment against the box
            for (int q = 0; q <= 8; q++) {
                vec2 sp = lerp(a, c, q / 8.f);
                vec2 d = sp - b.c;
                float lx = Max(fabsf(dot(d, b.ax)) - b.hx, 0.f), ly = Max(fabsf(dot(d, ay)) - b.hy, 0.f);
                if (lx * lx + ly * ly < N.corridorHalf * N.corridorHalf) return true;
            }
            for (int q = 0; q < 9; q++)
                if (distPointSegment2D(samples[q], a, c) < N.corridorHalf) return true;
        }
        // station footprints (platforms, stairs and their landings)
        for (const MetroStation& st : L.stations) {
            vec2 d = b.c - st.pos;
            if (length(d) > r + transit_dims::kPlatformHalfLen + 20.f) continue;
            for (int q = 0; q < 9; q++) {
                vec2 e = samples[q] - st.pos;
                if (fabsf(dot(e, st.dir)) < transit_dims::kPlatformHalfLen + 5.f && fabsf(dot(e, st.right())) < 13.5f) return true;
            }
            // footprint containing the station corners
            for (int cx = -1; cx <= 1; cx += 2)
                for (int cy = -1; cy <= 1; cy += 2) {
                    vec2 corner = st.pos + st.dir * (cx * (transit_dims::kPlatformHalfLen + 5.f)) + st.right() * (cy * 13.5f);
                    vec2 e = corner - b.c;
                    if (fabsf(dot(e, b.ax)) < b.hx && fabsf(dot(e, ay)) < b.hy) return true;
                }
        }
        return false;
    };
    buildings.erase(std::remove_if(buildings.begin(), buildings.end(), hits), buildings.end());
    LOG("Transit: %zu buildings cleared from the SkyLine corridor", before - buildings.size());
}

// ---------------------------------------------------------------------------------------------------------------------
void transitFinalize(SiteSet& S, WorldMap& map, const RoadNetwork& net, const BuildingSet& bs) {
    (void)bs;
    TransitNet& N = *gTransit;
    if (!N.laidOut) return;
    double t0 = TimeSeconds();
    MetroLine& L = N.metro;
    int n = L.count();
    // ---- street level under the centerline (smoothed: the girder should not follow every bump)
    std::vector<float> ground(n), base(n);
    for (int i = 0; i < n; i++) ground[i] = groundAt(map, L.p[i]);
    {
        int w = 20;   // +-40 m
        for (int i = 0; i < n; i++) {
            float acc = 0.f;
            for (int k = -w; k <= w; k++) acc += ground[(i + k + n) % n];
            base[i] = acc / (2 * w + 1);
        }
    }
    L.ground = ground;
    // ---- required height: clearance above the street plus roads, bridges and flyovers under the deck footprint
    std::vector<float> req(n);
    for (int i = 0; i < n; i++) {
        float r = Max(base[i], ground[i] - 0.5f) + kClearance;
        vec2 rt(L.t[i].y, -L.t[i].x);
        for (int q = -1; q <= 1; q++) {
            vec2 sp = L.p[i] + rt * (q * kStationHalf);
            r = Max(r, roadRequirement(net, map, sp));
        }
        req[i] = r;
    }
    // ---- vertical profile: stations level (platform zone + margin), grade limit 3.5 % by dilation, vertical curves by
    // smoothing that never dips below the requirement
    std::vector<u8> flat(n, 0);
    std::vector<float> zz = req;
    const float maxStep = 0.035f * L.ds;
    auto sampleIdx = [&](float s) { return ((int)roundf(L.wrap(s) / L.ds)) % n; };
    auto dilate = [&]() {
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i < 2 * n; i++) {
                int a = i % n, b = (i + 1) % n;
                zz[b] = Max(zz[b], zz[a] - maxStep);
            }
            for (int i = 2 * n; i > 0; i--) {
                int a = i % n, b = (i - 1 + n) % n;
                zz[b] = Max(zz[b], zz[a] - maxStep);
            }
        }
    };
    auto levelStations = [&]() {
        for (MetroStation& st : L.stations) {
            float zone = kPlatformHalfLen + 12.f;
            float hMax = -1e9f;
            for (float d = -zone; d <= zone; d += L.ds) hMax = Max(hMax, zz[sampleIdx(st.s + d)]);
            for (float d = -zone; d <= zone; d += L.ds) {
                zz[sampleIdx(st.s + d)] = hMax;
                flat[sampleIdx(st.s + d)] = 1;
            }
            st.railZ = hMax;
        }
    };
    for (int it = 0; it < 2; it++) {
        dilate();
        levelStations();
    }
    for (int it = 0; it < 24; it++) {
        std::vector<float> sm = zz;
        for (int i = 0; i < n; i++) {
            if (flat[i]) continue;
            float v = (zz[(i + n - 2) % n] + zz[(i + n - 1) % n] * 2.f + zz[i] * 2.f + zz[(i + 1) % n] * 2.f + zz[(i + 2) % n]) / 8.f;
            sm[i] = Max(v, req[i]);
        }
        zz.swap(sm);
    }
    for (int it = 0; it < 3; it++) {
        dilate();
        levelStations();
    }
    for (MetroStation& st : L.stations) {
        float g = 0.f;
        int cnt = 0;
        for (float d = -kPlatformHalfLen; d <= kPlatformHalfLen; d += 4.f) {
            g += ground[sampleIdx(st.s + d)];
            cnt++;
        }
        st.streetZ = g / Max(cnt, 1);
    }
    L.z = zz;
    // ---- superelevation: outer (right) rail raised in the left-hand corners, eased in over the transitions
    std::vector<float> bk(n);
    for (int i = 0; i < n; i++) bk[i] = Clamp(L.k[i] * 14.f * 14.f / 9.81f, -0.065f, 0.065f);
    {
        std::vector<float> sm(n);
        int w = 12;
        for (int i = 0; i < n; i++) {
            float acc = 0.f;
            for (int k = -w; k <= w; k++) acc += bk[(i + k + n) % n];
            sm[i] = acc / (2 * w + 1);
        }
        L.bank = sm;
    }
    // ---- stair exits: the stair of each platform descends from one platform end toward the middle; pick the end whose
    // landing stays clear of the roads (prefer the end away from the other platform's stair)
    for (MetroStation& st : L.stations) {
        for (int side = 0; side < 2; side++) {
            float lat = (side == 0 ? 1.f : -1.f) * 10.1f;
            int best = side == 0 ? -1 : 1;
            for (int tryEnd = 0; tryEnd < 2; tryEnd++) {
                int end = tryEnd == 0 ? best : -best;
                vec2 foot = st.pos + st.dir * (end * (kPlatformHalfLen - 27.5f)) + st.right() * lat;
                vec2 top = st.pos + st.dir * (end * (kPlatformHalfLen - 1.f)) + st.right() * lat;
                if (!net.nearRoad(foot, 1.5f) && !net.nearRoad(lerp(foot, top, 0.3f), 0.5f)) {
                    best = end;
                    break;
                }
            }
            st.exitEnd[side] = best;
        }
    }
    // ---- piers: ~32 m spans, never on a road or sidewalk; straddle bents where no single column fits
    L.piers.clear();
    auto freeAt = [&](vec2 q, float r) { return !net.nearRoad(q, r) && !S.padAt(q); };
    auto pierAt = [&](float s, MetroPier& out) {
        vec2 pos, tan;
        L.frame(s, pos, tan);
        out.s = L.wrap(s);
        out.groundZ = map.heightAt(pos.x, pos.y);
        out.bent = false;
        out.lateral = 0.f;
        out.station = L.stationAt(s, 3.f) >= 0;
        return freeAt(pos, 1.9f);
    };
    int bents = 0;
    // walk the loop once in an unwrapped coordinate u (s = sStart + u), starting at the first station's platform end so
    // the station piers line up with the platforms
    float sStart = L.stations.empty() ? 0.f : L.wrap(L.stations[0].s - kPlatformHalfLen + 2.f);
    float uLast = 0.f;
    {
        MetroPier pr;
        pierAt(sStart, pr);
        L.piers.push_back(pr);
    }
    const float slot = (kPlatformHalfLen - 2.f) * 2.f / 3.f;   // station piers: four per platform
    while (true) {
        float want = uLast + 30.f;
        if (want > L.length - 20.f) break;
        int stIdx = L.stationAt(sStart + want, 4.f);
        if (stIdx >= 0) {
            const MetroStation& st = L.stations[stIdx];
            float rel = L.delta(st.s, sStart + want);
            float snapped = Clamp(roundf((rel + kPlatformHalfLen - 2.f) / slot), 0.f, 3.f) * slot - (kPlatformHalfLen - 2.f);
            if (want + (snapped - rel) - uLast > 12.f) want += snapped - rel;
        }
        // best free spot for a span of 12..50 m, closest to the wanted position
        MetroPier best;
        bool found = false;
        float bestScore = 1e9f, bestU = want;
        for (float u = uLast + 12.f; u <= uLast + 50.f && u <= L.length - 12.f; u += 1.f) {
            float score = fabsf(u - want);
            if (score >= bestScore) continue;
            MetroPier pr;
            if (pierAt(sStart + u, pr)) {
                best = pr;
                found = true;
                bestScore = score;
                bestU = u;
            }
        }
        // long span over a wide crossing (road pairs, river banks): the nearest free spot up to 90 m on
        for (float u = uLast + 51.f; !found && u <= uLast + 90.f && u <= L.length - 12.f; u += 1.f) {
            MetroPier pr;
            if (pierAt(sStart + u, pr)) {
                best = pr;
                found = true;
                bestU = u;
            }
        }
        if (found) want = bestU;
        if (!found) {
            // straddle bent: legs outside the road on both sides of the corridor
            MetroPier pr;
            pierAt(sStart + want, pr);
            vec2 pos, tan;
            L.frame(sStart + want, pos, tan);
            vec2 rt(tan.y, -tan.x);
            for (float lat = 6.f; lat <= 24.f; lat += 1.f)
                if (freeAt(pos + rt * lat, 1.2f) && freeAt(pos - rt * lat, 1.2f)) {
                    pr.bent = true;
                    pr.lateral = lat;
                    break;
                }
            if (!pr.bent) LOG("Transit: pier at s=%.0f (%.0f, %.0f) stands on a road", pr.s, pos.x, pos.y);
            else bents++;
            best = pr;
        }
        L.piers.push_back(best);
        uLast = want;
    }
    std::sort(L.piers.begin(), L.piers.end(), [](const MetroPier& a, const MetroPier& b) { return a.s < b.s; });
    // ---- site elements: viaduct chunks (64 m) and one element per station
    const float chunk = 64.f;
    int chunks = (int)ceilf(L.length / chunk);
    for (int c = 0; c < chunks; c++) {
        float s0 = c * chunk, s1 = Min(L.length, s0 + chunk);
        SiteElem e;
        e.kind = SK_METRO_VIADUCT;
        e.variant = 0;
        e.seed = hash32(0xA1Du + (u32)c * 2654435761u);
        vec2 pos, tan;
        L.frame((s0 + s1) * 0.5f, pos, tan);
        e.c = pos;
        e.ax = tan;
        e.hx = (s1 - s0) * 0.5f;
        e.hy = kStationHalf + 4.f;
        e.z = L.railZ((s0 + s1) * 0.5f);
        e.h = 16.f;
        e.p[0] = s0;
        e.p[1] = s1;
        for (float s = s0 - 4.f; s <= s1 + 4.f; s += 4.f) {
            L.frame(s, pos, tan);
            vec2 rt(tan.y, -tan.x);
            e.pts.push_back(pos + rt * 12.f);
            e.pts.push_back(pos - rt * 12.f);
        }
        S.elems.push_back(e);
    }
    for (size_t i = 0; i < L.stations.size(); i++) {
        const MetroStation& st = L.stations[i];
        SiteElem e;
        e.kind = SK_METRO_STATION;
        e.variant = (u16)i;
        e.seed = st.seed;
        e.c = st.pos;
        e.ax = st.dir;
        e.hx = kPlatformHalfLen + 6.f;
        e.hy = 14.f;
        e.z = st.streetZ;
        e.h = st.railZ - st.streetZ + 8.f;
        e.text = st.name;
        for (int cx = -1; cx <= 1; cx += 2)
            for (int cy = -1; cy <= 1; cy += 2) e.pts.push_back(st.local(cx * (kPlatformHalfLen + 8.f), cy * 16.f, 0.f).xy());
        S.elems.push_back(e);
    }
    N.ready = true;
    float zMin = 1e9f, zMax = -1e9f;
    for (int i = 0; i < n; i++) {
        float h = L.z[i] - ground[i];
        zMin = Min(zMin, h);
        zMax = Max(zMax, h);
    }
    LOG("Transit: SkyLine profile %.1f..%.1f m above the street, %zu piers (%d straddle bents) (%.2f s)", zMin, zMax, L.piers.size(), bents,
        TimeSeconds() - t0);
    for (const MetroStation& st : L.stations)
        LOG("Transit: station %-16s s %6.0f at (%.0f, %.0f) rail %.1f street %.1f exits %+d/%+d", st.name.c_str(), st.s, st.pos.x, st.pos.y, st.railZ,
            st.streetZ, st.exitEnd[0], st.exitEnd[1]);
}

}  // namespace World
