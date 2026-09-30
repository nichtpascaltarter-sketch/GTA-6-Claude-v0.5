// Transit network layout (world generation, deterministic): the SkyLine elevated metro loop through the airport,
// Palmetto Flats, the Canvas District, Midtown, the Civic Center, Solaris, Calle Luna and Westbrook; city bus routes
// with their stops; bay ferry piers and routes. Geometry is built per streaming cell in transitmesh.cpp.
#include "transit.h"
#include "worldtypes.h"
#include <queue>

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
// Bus routes: queries

vec2 BusRoute::pointAt(float d, vec2* dirOut) const {
    if (line.size() < 2) {
        if (dirOut) *dirOut = vec2(0, 1);
        return line.empty() ? vec2(0.f) : line[0];
    }
    d = wrap(d);
    size_t i = std::upper_bound(lineDist.begin(), lineDist.end(), d) - lineDist.begin();
    size_t n = line.size();
    size_t a = i == 0 ? n - 1 : i - 1, b = i % n;
    float da = lineDist[a], db = i >= n ? length : lineDist[b];
    float t = db > da ? (d - da) / (db - da) : 0.f;
    vec2 seg = line[b] - line[a];
    if (dirOut) *dirOut = length2(seg) > 1e-6f ? normalize(seg) : vec2(0, 1);
    return lerp(line[a], line[b], Saturate(t));
}

int BusRoute::legAt(float d) const {
    if (legs.empty()) return -1;
    d = wrap(d);
    int lo = 0, hi = (int)legs.size();
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (legs[mid].d0 <= d) lo = mid;
        else hi = mid;
    }
    return lo;
}

// ---------------------------------------------------------------------------------------------------------------------
// Bus routes: layout (world generation). Each route is a closed chain of directed road edges found by a turn-aware
// search between authored waypoints; stops follow every ~400 m on the curb side (right-hand traffic), preferring the
// shelters the road furniture generator already put on the sidewalk, sharing stops between routes, never at the SkyLine
// stairs, junction mouths, decks or other roads' pavement.
namespace transit_bus {

using namespace transit_dims;

struct Via {
    vec2 p;
    vec2 heading;           // travel direction the route passes here with
    bool stop = false;      // force a stop here
    const char* stopName = nullptr;
    bool terminus = false;
};

struct RouteDef {
    const char* number;
    const char* name;
    u32 rgb;                // 0xRRGGBB (sRGB)
    std::vector<Via> via;
};

bool busEdge(const RoadEdge& e) {
    if (e.flags & RF_UNPAVED) return false;
    return e.cls == RC_BOULEVARD || e.cls == RC_AVENUE || e.cls == RC_STREET || e.cls == RC_LANE || e.cls == RC_RURAL;
}
bool canDrive(const RoadEdge& e, int dir) { return dir > 0 ? e.lanesF > 0 : e.lanesB > 0; }
float classCost(RoadClass c) {
    switch (c) {
        case RC_BOULEVARD: return 1.f;
        case RC_AVENUE: return 1.08f;
        case RC_STREET: return 1.5f;
        case RC_RURAL: return 1.3f;
        case RC_LANE: return 4.f;
        default: return 8.f;
    }
}
vec3 edgePos(const RoadEdge& e, int dir, float u) { return e.posAt(dir > 0 ? u : e.length - u); }
vec2 edgeDir(const RoadEdge& e, int dir, float u) {
    vec3 t = e.tangentAt(dir > 0 ? u : e.length - u);
    vec2 t2(t.x, t.y);
    return length2(t2) > 1e-8f ? normalize(t2) * (float)dir : vec2(0, 1);
}
int endNode(const RoadEdge& e, int dir) { return dir > 0 ? e.n1 : e.n0; }
float cutIn(const RoadEdge& e, int dir) { return dir > 0 ? e.cut0 : e.cut1; }
float cutOut(const RoadEdge& e, int dir) { return dir > 0 ? e.cut1 : e.cut0; }

// Nearest drivable edge through p whose travel direction matches `heading`; returns the edge, direction and travel u
bool snapVia(const RoadNetwork& net, vec2 p, vec2 heading, int& edge, int& dir, float& u) {
    std::vector<int> cand;
    net.edgesInRect(p - vec2(80.f), p + vec2(80.f), cand);
    float best = 1e30f;
    edge = -1;
    for (int ei : cand) {
        const RoadEdge& e = net.edges[ei];
        if (!busEdge(e) || e.pts.size() < 2) continue;
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            float t;
            float d = distPointSegment2D(p, e.pts[k].xy(), e.pts[k + 1].xy(), &t);
            if (d > 60.f || d >= best) continue;
            vec2 sd = normalize(e.pts[k + 1].xy() - e.pts[k].xy());
            int dd = dot(sd, heading) >= 0.f ? 1 : -1;
            if (fabsf(dot(sd, heading)) < 0.6f || !canDrive(e, dd)) continue;
            best = d;
            edge = ei;
            dir = dd;
            float s = e.dist[k] + t * (e.dist[k + 1] - e.dist[k]);
            u = dd > 0 ? s : e.length - s;
        }
    }
    return edge >= 0;
}

// Turn-aware shortest path over directed edges (state = edge * 2 + (dir < 0)) from (e0, d0) to (e1, d1), both included
bool legPath(const RoadNetwork& net, int e0, int d0, int e1, int d1, std::vector<RouteLeg>& out) {
    const int NS = (int)net.edges.size() * 2;
    std::vector<float> cost(NS, 1e30f);
    std::vector<int> prev(NS, -1);
    typedef std::pair<float, int> QE;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    auto sid = [](int e, int d) { return e * 2 + (d < 0 ? 1 : 0); };
    const RoadEdge& G = net.edges[e1];
    vec2 goalP = net.nodes[d1 > 0 ? G.n0 : G.n1].p;
    int s0 = sid(e0, d0), sGoal = sid(e1, d1);
    cost[s0] = 0.f;
    open.push(QE(0.f, s0));
    int expanded = 0;
    while (!open.empty() && expanded < 400000) {
        QE top = open.top();
        open.pop();
        int s = top.second;
        int e = s / 2, d = (s & 1) ? -1 : 1;
        const RoadEdge& E = net.edges[e];
        int n = endNode(E, d);
        float h = length(net.nodes[n].p - goalP);
        if (top.first > cost[s] + h + 0.01f) continue;
        expanded++;
        if (s == sGoal && s != s0) break;
        vec2 tIn = edgeDir(E, d, E.length - 1.f);
        for (int e2 : net.nodes[n].edges) {
            const RoadEdge& F = net.edges[e2];
            if (F.n0 == F.n1 || !busEdge(F)) continue;
            int d2 = F.n0 == n ? 1 : -1;
            if (!canDrive(F, d2)) continue;
            if (e2 == e) continue;   // no U-turns
            vec2 tOut = edgeDir(F, d2, 1.f);
            float c = dot(tIn, tOut);
            if (c < -0.55f) continue;   // too sharp for a 12 m bus
            float turn = c > 0.85f ? 0.f : (cross(tIn, tOut) > 0.f ? 45.f : 18.f);   // left turns wait longer
            int s2 = sid(e2, d2);
            float nc = cost[s] + turn + F.length * classCost(F.cls);
            if (nc < cost[s2]) {
                cost[s2] = nc;
                prev[s2] = s;
                int n2 = endNode(F, d2);
                open.push(QE(nc + length(net.nodes[n2].p - goalP) * 0.95f, s2));
            }
        }
    }
    if (prev[sGoal] < 0 && sGoal != s0) return false;
    std::vector<int> chain;
    for (int s = sGoal; s >= 0; s = prev[s]) {
        chain.push_back(s);
        if (s == s0) break;
    }
    if (chain.back() != s0) return false;
    std::reverse(chain.begin(), chain.end());
    for (int s : chain) {
        RouteLeg l;
        l.edge = s / 2;
        l.dir = (s & 1) ? -1 : 1;
        out.push_back(l);
    }
    return true;
}

// Sidewalk furniture of an edge as laid out by roadmesh.cpp (edgeFixtures): the hash sequence is a stable contract shared
// with the AI's scenario points (lanes.cpp). Slots: s along n0 -> n1, side +1 = right of n0 -> n1.
struct Slot {
    float s;
    int side;
    bool shelter;
};
void furnitureSlots(const RoadNetwork& net, const RoadEdge& e, std::vector<Slot>& out) {
    out.clear();
    bool hwy = e.cls == RC_HIGHWAY || e.cls == RC_RAMP;
    if ((e.flags & RF_UNPAVED) || hwy || !(e.sidewalk > 2.f)) return;
    float hw = e.halfWidth, sw = e.sidewalk;
    u32 h = e.seed;
    for (float s = e.cut0 + 15.f; s < e.length - e.cut1 - 15.f; s += 23.f) {
        h = hash32(h + 1u);
        int side = (h & 1) ? 1 : -1;
        float r = hashToFloat(h >> 1);
        if (r >= 0.52f) continue;
        bool shelter = r < 0.08f;
        vec3 c = e.posAt(s);
        vec3 t = e.tangentAt(s);
        vec2 rv = normalize(vec2(t.y, -t.x));
        float off = hw + (shelter ? sw - 1.2f : 0.9f);
        vec2 pp = c.xy() + rv * (side * off);
        if (net.onPavement(pp, c.z + 0.15f, 0.3f)) continue;
        out.push_back({s, side, shelter});
    }
}

// Streetlight stations of an edge (roadmesh.cpp edgeFixtures; present unless the edge carries lamp poles instead)
void lampSlots(const RoadEdge& e, std::vector<Slot>& out) {
    out.clear();
    bool hwy = e.cls == RC_HIGHWAY || e.cls == RC_RAMP;
    if (e.flags & RF_UNPAVED) return;
    float spacing = hwy ? 55.f : (e.cls == RC_RURAL ? 70.f : (e.cls == RC_LANE ? 38.f : 32.f));
    float s0 = e.cut0 + 8.f, s1 = e.length - e.cut1 - 8.f;
    int k = 0;
    for (float s = s0 + fmodf((float)(e.seed % 100), spacing * 0.5f); s < s1; s += spacing, k++) out.push_back({s, (k & 1) ? 1 : -1, false});
}

std::string shortName(const std::string& n) {
    std::string s = n;
    auto rep = [&](const char* a, const char* b) {
        size_t p = s.find(a);
        if (p != std::string::npos) s.replace(p, strlen(a), b);
    };
    rep(" Boulevard", " Blvd");
    rep(" Avenue", " Ave");
    rep(" Street", " St");
    rep(" Drive", " Dr");
    rep(" Causeway", " Cswy");
    return s;
}

struct Builder {
    const RoadNetwork& net;
    WorldMap& map;
    SiteSet& S;
    TransitNet& N;
    std::vector<vec2> avoid;        // metro stair feet, ferry terminals, ... (radius in avoidR)
    std::vector<float> avoidR;
    std::vector<Slot> slots, lamps;

    Builder(const RoadNetwork& n, WorldMap& m, SiteSet& s, TransitNet& t) : net(n), map(m), S(s), N(t) {}

    // geometry of a stop at travel coordinate u on (e, d): shelter center, flag pole, facing
    struct Geo {
        vec2 shelter, flag, face, along;
        float z;
        bool ok;
    };
    Geo geometry(const RoadEdge& e, int d, float u, bool ambient) {
        Geo g;
        vec3 c = edgePos(e, d, u);
        vec2 al = edgeDir(e, d, u);
        vec2 rt(al.y, -al.x);   // curb side (right of travel)
        float sw = e.sidewalk, hw = e.halfWidth;
        g.along = al;
        g.face = -rt;
        g.z = c.z + 0.15f;
        g.shelter = c.xy() + rt * (hw + sw - (ambient ? 1.2f : 1.15f));
        vec3 cf = edgePos(e, d, Min(u + 3.2f, e.length));
        g.flag = cf.xy() + rt * (hw + 0.42f);
        g.ok = true;
        return g;
    }

    // Is travel coordinate u on (e, d) a valid stop spot? (ambient: an existing shelter slot)
    bool valid(int ei, int d, float u, bool ambient) {
        const RoadEdge& e = net.edges[ei];
        if (!(e.sidewalk > 2.f) || e.cls == RC_HIGHWAY || e.cls == RC_RAMP || e.cls == RC_LANE) return false;
        if (u < cutIn(e, d) + 24.f || u > e.length - cutOut(e, d) - 30.f) return false;
        vec3 c = edgePos(e, d, u);
        float gnd = map.heightAt(c.x, c.y);
        if (c.z - gnd > 1.2f || map.isWater(c.x, c.y)) return false;
        vec3 c2 = edgePos(e, d, u + 6.f), c3 = edgePos(e, d, Max(u - 8.f, 0.f));
        if (fabsf(c2.z - c.z) > 0.8f || fabsf(c3.z - c.z) > 0.8f) return false;   // steep ramps
        Geo g = geometry(e, d, u, ambient);
        if (net.onPavement(g.shelter, g.z, 0.9f, -1) || net.onPavement(g.flag, g.z, 0.2f, ei)) return false;
        for (size_t k = 0; k < avoid.size(); k++)
            if (length(g.shelter - avoid[k]) < avoidR[k] || length(g.flag - avoid[k]) < avoidR[k]) return false;
        if (S.padAt(g.shelter) || S.padAt(g.flag)) return false;
        // sidewalk furniture on the curb side (other than the shelter we reuse)
        float s = d > 0 ? u : e.length - u;
        furnitureSlots(net, e, slots);
        for (const Slot& sl : slots) {
            if (sl.side != d) continue;
            if (ambient && fabsf(sl.s - s) < 0.5f) continue;
            if (fabsf(sl.s - s) < (ambient ? 5.5f : 7.5f)) return false;
        }
        // other stops on the same curb
        for (const BusStop& b : N.busStops)
            if (length(b.pos - g.shelter) < 45.f) return false;
        return true;
    }

    // Move the flag pole off a streetlight on the curb
    vec2 flagFor(const RoadEdge& e, int d, float u, vec2 flag) {
        lampSlots(e, lamps);
        float s = d > 0 ? u + 3.2f : e.length - (u + 3.2f);
        for (const Slot& l : lamps)
            if (l.side == d && fabsf(l.s - s) < 1.6f) {
                vec2 al = edgeDir(e, d, u), rt(al.y, -al.x);
                vec3 cf = edgePos(e, d, Max(u - 3.8f, 0.f));
                return cf.xy() + rt * (e.halfWidth + 0.42f);
            }
        return flag;
    }

    std::string stopName(const RoadEdge& e, int d, float u, vec2 pos) {
        // landmarks first
        const MetroLine& L = N.metro;
        for (const MetroStation& st : L.stations)
            if (length(st.pos - pos) < 190.f) return st.name + " SkyLine";
        for (const FerryPier& fp : N.piers)
            if (length(fp.base - pos) < 260.f) return fp.name + " Ferry";
        std::string a = shortName(e.name);
        // cross street at the nearer node (the upcoming one when close)
        int nodes[2] = {d > 0 ? e.n1 : e.n0, d > 0 ? e.n0 : e.n1};
        if (u < e.length * 0.35f) std::swap(nodes[0], nodes[1]);
        for (int k = 0; k < 2; k++) {
            const RoadNode& nd = net.nodes[nodes[k]];
            for (int e2 : nd.edges) {
                const RoadEdge& F = net.edges[e2];
                if (F.name.empty() || F.name == e.name || F.cls == RC_HIGHWAY || F.cls == RC_RAMP) continue;
                std::string b = shortName(F.name);
                return a.empty() ? b : a + " & " + b;
            }
        }
        return a.empty() ? std::string("Bus Stop") : a;
    }

    int addStop(int ei, int d, float u, bool ambient, const char* name, bool terminus) {
        const RoadEdge& e = net.edges[ei];
        Geo g = geometry(e, d, u, ambient);
        BusStop b;
        b.edge = ei;
        b.dir = d;
        b.u = u;
        b.pos = g.shelter;
        b.face = g.face;
        b.along = g.along;
        b.flag = flagFor(e, d, u, g.flag);
        b.z = g.z;
        b.ownShelter = !ambient;
        b.terminus = terminus;
        b.seed = hash32(0xB05u + (u32)ei * 7919u + (u32)(u * 4.f));
        b.name = name ? std::string(name) : stopName(e, d, u, g.shelter);
        N.busStops.push_back(b);
        return (int)N.busStops.size() - 1;
    }

    // Build one route; returns false when the waypoints cannot be connected
    bool route(const RouteDef& def, int routeIdx) {
        BusRoute R;
        R.number = def.number;
        R.name = def.name;
        vec3 srgb(((def.rgb >> 16) & 255) / 255.f, ((def.rgb >> 8) & 255) / 255.f, (def.rgb & 255) / 255.f);
        R.color = srgbToLinear(srgb);
        R.colorSrgb = packRGBA8(srgb.x, srgb.y, srgb.z, 1.f);
        size_t nv = def.via.size();
        std::vector<int> ve(nv), vd(nv);
        std::vector<float> vu(nv);
        for (size_t i = 0; i < nv; i++)
            if (!snapVia(net, def.via[i].p, def.via[i].heading, ve[i], vd[i], vu[i])) {
                LOG("Transit: bus route %s: waypoint %zu (%.0f, %.0f) is not on a drivable road", def.number, i, def.via[i].p.x, def.via[i].p.y);
                return false;
            }
        std::vector<RouteLeg> chain;
        std::vector<int> viaLeg(nv, -1);
        for (size_t i = 0; i < nv; i++) {
            size_t j = (i + 1) % nv;
            std::vector<RouteLeg> part;
            if (ve[i] == ve[j] && vd[i] == vd[j] && vu[j] > vu[i] + 5.f) {
                RouteLeg l;
                l.edge = ve[i];
                l.dir = vd[i];
                part.push_back(l);
            } else if (!legPath(net, ve[i], vd[i], ve[j], vd[j], part)) {
                LOG("Transit: bus route %s: no path from waypoint %zu to %zu", def.number, i, j);
                return false;
            }
            for (size_t k = 0; k < part.size(); k++) {
                if (!chain.empty() && chain.back().edge == part[k].edge && chain.back().dir == part[k].dir) continue;
                if (k == 0 && viaLeg[i] < 0) viaLeg[i] = (int)chain.size();
                chain.push_back(part[k]);
            }
            if (viaLeg[i] < 0) viaLeg[i] = (int)chain.size() - (int)part.size();
        }
        // the chain ends where it started
        if (chain.size() > 1 && chain.back().edge == chain.front().edge && chain.back().dir == chain.front().dir) chain.pop_back();
        float d = 0.f;
        for (RouteLeg& l : chain) {
            l.d0 = d;
            d += net.edges[l.edge].length;
        }
        R.legs = chain;
        R.length = d;
        // map line
        for (const RouteLeg& l : R.legs) {
            const RoadEdge& e = net.edges[l.edge];
            int n = Max(1, (int)ceilf(e.length / 15.f));
            for (int k = 0; k < n; k++) {
                float u = e.length * k / n;
                R.line.push_back(edgePos(e, l.dir, u).xy());
                R.lineDist.push_back(l.d0 + u);
            }
        }
        // ---- stops
        std::vector<float> forced;          // route distance of forced stops (waypoints)
        std::vector<int> forcedVia;
        for (size_t i = 0; i < nv; i++) {
            if (!def.via[i].stop) continue;
            int li = -1;
            for (int k = 0; k < (int)R.legs.size(); k++)
                if (R.legs[k].edge == ve[i] && R.legs[k].dir == vd[i]) {
                    li = k;
                    break;
                }
            if (li < 0) continue;
            forced.push_back(R.legs[li].d0 + vu[i]);
            forcedVia.push_back((int)i);
        }
        auto locate = [&](float rd, int& li, float& u) {
            li = R.legAt(rd);
            u = R.wrap(rd) - R.legs[li].d0;
        };
        // candidate search in a route-distance window; prefers existing shelters, then the spot closest to `target`
        auto search = [&](float lo, float hi, float target, int& outStop) {
            float bestScore = 1e30f;
            int bestLeg = -1;
            float bestU = 0.f;
            bool bestAmb = false;
            for (float rd = lo; rd <= hi; rd += 4.f) {
                int li;
                float u;
                locate(rd, li, u);
                const RouteLeg& l = R.legs[li];
                float score = fabsf(rd - target);
                if (score < bestScore && valid(l.edge, l.dir, u, false)) {
                    bestScore = score;
                    bestLeg = li;
                    bestU = u;
                    bestAmb = false;
                }
            }
            // existing shelters (and stops of earlier routes) in the window
            for (int li = 0; li < (int)R.legs.size(); li++) {
                const RouteLeg& l = R.legs[li];
                const RoadEdge& e = net.edges[l.edge];
                for (float off = -R.length; off <= R.length; off += R.length) {
                    float a = l.d0 + off, b = a + e.length;
                    if (b < lo || a > hi) continue;
                    furnitureSlots(net, e, slots);
                    for (const Slot& sl : slots) {
                        if (!sl.shelter || sl.side != l.dir) continue;
                        float u = l.dir > 0 ? sl.s : e.length - sl.s;
                        float rd = a + u;
                        if (rd < lo || rd > hi) continue;
                        float score = fabsf(rd - target) - 140.f;
                        if (score < bestScore && valid(l.edge, l.dir, u, true)) {
                            bestScore = score;
                            bestLeg = li;
                            bestU = u;
                            bestAmb = true;
                        }
                    }
                    for (int si = 0; si < (int)N.busStops.size(); si++) {
                        const BusStop& b2 = N.busStops[si];
                        if (b2.edge != l.edge || b2.dir != l.dir) continue;
                        float rd = a + b2.u;
                        if (rd < lo || rd > hi) continue;
                        float score = fabsf(rd - target) - 220.f;
                        if (score < bestScore) {
                            bestScore = score;
                            bestLeg = li;
                            bestU = b2.u;
                            bestAmb = true;
                            outStop = si;
                        }
                    }
                }
            }
            if (bestLeg < 0) return false;
            if (outStop >= 0 && N.busStops[outStop].edge == R.legs[bestLeg].edge && fabsf(N.busStops[outStop].u - bestU) < 0.5f) return true;
            outStop = addStop(R.legs[bestLeg].edge, R.legs[bestLeg].dir, bestU, bestAmb, nullptr, false);
            return true;
        };
        auto routeDistOf = [&](int si, float near_) {
            const BusStop& b = N.busStops[si];
            float best = 1e30f, out = near_;
            for (const RouteLeg& l : R.legs)
                if (l.edge == b.edge && l.dir == b.dir) {
                    float rd = l.d0 + b.u;
                    float dd = fabsf(R.wrap(rd - near_ + R.length * 0.5f) - R.length * 0.5f);
                    if (dd < best) {
                        best = dd;
                        out = rd;
                    }
                }
            return out;
        };
        auto push = [&](int si, float rd) {
            if (std::find(R.stops.begin(), R.stops.end(), si) != R.stops.end()) return;
            N.busStops[si].routeMask |= 1u << routeIdx;
            R.stops.push_back(si);
            R.stopDist.push_back(R.wrap(rd));
        };
        // forced stops first (named waypoints: terminals, landmarks)
        std::vector<std::pair<float, int>> placed;
        for (size_t k = 0; k < forced.size(); k++) {
            const Via& v = def.via[forcedVia[k]];
            int li;
            float u;
            locate(forced[k], li, u);
            int si = -1;
            // an existing stop close by on the same curb
            for (int q = 0; q < (int)N.busStops.size(); q++)
                if (N.busStops[q].edge == R.legs[li].edge && N.busStops[q].dir == R.legs[li].dir && fabsf(N.busStops[q].u - u) < 90.f) si = q;
            if (si < 0) {
                // nearest valid spot around the waypoint
                float bestD = 1e30f, bestU = -1.f;
                const RoadEdge& e = net.edges[R.legs[li].edge];
                for (float uu = 0.f; uu <= e.length; uu += 2.f)
                    if (fabsf(uu - u) < bestD && valid(R.legs[li].edge, R.legs[li].dir, uu, false)) {
                        bestD = fabsf(uu - u);
                        bestU = uu;
                    }
                if (bestU >= 0.f) si = addStop(R.legs[li].edge, R.legs[li].dir, bestU, false, v.stopName, v.terminus);
            }
            if (si >= 0) {
                if (v.stopName) N.busStops[si].name = v.stopName;
                N.busStops[si].terminus = N.busStops[si].terminus || v.terminus;
                placed.push_back({routeDistOf(si, forced[k]), si});
            }
        }
        std::sort(placed.begin(), placed.end());
        if (placed.empty()) {
            int si = -1;
            if (search(0.f, 300.f, 0.f, si)) placed.push_back({routeDistOf(si, 0.f), si});
        }
        if (placed.empty()) return false;
        // fill the gaps between consecutive placed stops (~420 m spacing)
        std::vector<std::pair<float, int>> all = placed;
        for (size_t k = 0; k < placed.size(); k++) {
            float a = placed[k].first;
            float b = k + 1 < placed.size() ? placed[k + 1].first : placed[0].first + R.length;
            float gap = b - a;
            int nIns = (int)floorf(gap / 420.f + 0.35f) - 1;
            if (nIns <= 0) continue;
            float step = gap / (nIns + 1);
            float last = a;
            for (int q = 1; q <= nIns; q++) {
                float target = a + step * q;
                float lo = Max(last + 230.f, target - 170.f), hi = Min(b - 230.f, target + 170.f);
                if (hi <= lo) continue;
                int si = -1;
                if (search(lo, hi, target, si)) {
                    float rd = routeDistOf(si, target);
                    if (rd < last) rd += R.length;
                    all.push_back({rd, si});
                    last = rd;
                }
            }
        }
        for (auto& p : all) p.first = R.wrap(p.first);
        std::sort(all.begin(), all.end());
        // service order starts at the first forced stop (the terminus)
        int first = 0;
        for (size_t k = 0; k < all.size(); k++)
            if (all[k].second == placed[0].second) first = (int)k;
        for (size_t k = 0; k < all.size(); k++) {
            auto& p = all[(first + k) % all.size()];
            push(p.second, p.first);
        }
        float cycle = R.length / 7.0f + R.stops.size() * 20.f;
        R.buses = Clamp((int)ceilf(cycle / 420.f), 2, 6);
        R.headway = cycle / R.buses;
        N.busRoutes.push_back(R);
        return true;
    }
};

// Porto Sol Transit bus routes. Headings give the direction of travel through each waypoint (routes are loops).
std::vector<RouteDef> routeDefs() {
    std::vector<RouteDef> r;
    const vec2 N(0, 1), S(0, -1), E(1, 0), W(-1, 0);
    // 3 Airport Express: terminal curb <-> Midtown and the Civic Center
    r.push_back({"3", "Airport", 0xF2B620,
                 {{vec2(706.f, 1400.f), S, true, "PSI Airport Terminal", true},
                  {vec2(870.f, 1500.f), E},
                  {vec2(1600.f, 1050.f), E},
                  {vec2(3000.f, 800.f), S},
                  {vec2(3300.f, 600.f), N, true},
                  {vec2(2500.f, 1050.f), W},
                  {vec2(870.f, 1500.f), W},
                  {vec2(745.f, 1650.f), N}}});
    // 7 Calle Luna: Calle Luna market streets <-> Palmetto Flats
    r.push_back({"7", "Calle Luna", 0xEE6A1C,
                 {{vec2(1800.f, -1300.f), N, true, nullptr, true},
                  {vec2(1800.f, 400.f), N},
                  {vec2(1800.f, 1800.f), N},
                  {vec2(2100.f, 1950.f), E},
                  {vec2(2100.f, 1000.f), S},
                  {vec2(2100.f, -1300.f), S},
                  {vec2(1950.f, -1450.f), W}}});
    // 9 Sol Beach: Civic Center -> Solano Causeway -> Collins-Solano Avenue -> Venetia Causeway -> Bayshore
    r.push_back({"9", "Sol Beach", 0xE0468E,
                 {{vec2(3300.f, 250.f), N, true, nullptr, true},
                  {vec2(3450.f, 750.f), E},
                  {vec2(4300.f, 740.f), E},
                  {vec2(4950.f, 1000.f), N},
                  {vec2(4600.f, 1740.f), W},
                  {vec2(3600.f, 1150.f), S},
                  {vec2(3350.f, 50.f), W}}});
    // 12 Bayshore: Bayshore Boulevard along the downtown waterfront
    r.push_back({"12", "Bayshore", 0x12A39A,
                 {{vec2(3500.f, -1300.f), N, true, nullptr, true},
                  {vec2(3500.f, 0.f), N},
                  {vec2(3500.f, 1200.f), N},
                  {vec2(3500.f, 2300.f), N},
                  {vec2(3500.f, 1200.f), S},
                  {vec2(3500.f, 0.f), S}}});
    return r;
}

}  // namespace transit_bus

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
    // ---- bus routes and their stops
    {
        double tb = TimeSeconds();
        N.busStops.clear();
        N.busRoutes.clear();
        transit_bus::Builder B(net, map, S, N);
        // keep the stops clear of the SkyLine stairs (they land on the sidewalks) and of the ferry terminals
        for (const MetroStation& st : L.stations)
            for (int side = 0; side < 2; side++) {
                float Sg = side == 0 ? 1.f : -1.f;
                int E = st.exitEnd[side];
                for (float a = 0.f; a <= 40.f; a += 8.f) {
                    B.avoid.push_back(st.local(E * (kPlatformHalfLen - 3.4f - a), Sg * 10.f, 0.f).xy());
                    B.avoidR.push_back(9.f);
                }
            }
        for (const FerryPier& fp : N.piers) {
            B.avoid.push_back(fp.base);
            B.avoidR.push_back(24.f);
        }
        std::vector<transit_bus::RouteDef> defs = transit_bus::routeDefs();
        for (const transit_bus::RouteDef& d : defs) B.route(d, (int)N.busRoutes.size());
        int own = 0;
        for (size_t i = 0; i < N.busStops.size(); i++) {
            const BusStop& b = N.busStops[i];
            SiteElem e;
            e.kind = SK_BUS_STOP;
            e.variant = (u16)i;
            e.seed = b.seed;
            e.c = b.pos;
            e.ax = b.along;
            e.hx = 4.f;
            e.hy = 2.5f;
            e.z = b.z;
            e.h = 3.2f;
            e.text = b.name;
            e.pts.push_back(b.pos);
            e.pts.push_back(b.flag);
            S.elems.push_back(e);
            // no street dressing, poles or trees inside the shelter and at the flag
            if (b.ownShelter) S.vegBlocks.push_back({b.pos, b.along, 3.6f, 1.2f});
            S.vegBlocks.push_back({b.flag, b.along, 0.7f, 0.7f});
            own += b.ownShelter;
        }
        LOG("Transit: %zu bus routes, %zu stops (%d new shelters) (%.2f s)", N.busRoutes.size(), N.busStops.size(), own, TimeSeconds() - tb);
        for (const BusRoute& R : N.busRoutes)
            LOG("Transit: bus %-3s %-12s %.1f km, %zu stops, %d buses, headway %.1f min", R.number.c_str(), R.name.c_str(), R.length / 1000.f, R.stops.size(),
                R.buses, R.headway / 60.f);
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
