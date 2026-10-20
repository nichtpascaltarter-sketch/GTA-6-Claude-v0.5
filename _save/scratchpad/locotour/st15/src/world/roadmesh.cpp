// Road geometry generation per streaming cell.
#include "roads.h"
#include "buildings.h"
#include "sites.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace roadmesh_detail {

struct Section {
    vec3 c;       // center
    vec3 right;   // unit right vector (horizontal)
    float s;      // distance along edge
    float miter;  // width multiplier
};

inline u32 colorGray(float g) { return packRGBA8(g, g, g, 1.f); }

// Road surface z + lateral offset (road is flat across)
inline vec3 sectionPoint(const Section& sc, float lateral, float dz = 0.f) {
    return sc.c + sc.right * (lateral * sc.miter) + vec3(0, 0, dz);
}

void buildSections(const RoadEdge& e, std::vector<Section>& out) {
    out.clear();
    float s0 = e.cut0, s1 = e.length - e.cut1;
    if (s1 - s0 < 0.5f) return;
    std::vector<float> ss;
    ss.push_back(s0);
    for (size_t i = 1; i + 1 < e.pts.size(); i++)
        if (e.dist[i] > s0 + 0.3f && e.dist[i] < s1 - 0.3f) ss.push_back(e.dist[i]);
    ss.push_back(s1);
    for (float s : ss) {
        Section sc;
        sc.s = s;
        sc.c = e.posAt(s);
        vec3 t0 = e.tangentAt(Max(0.f, s - 0.5f)), t1 = e.tangentAt(Min(e.length, s + 0.5f));
        vec2 th = normalize(t0.xy() + t1.xy());
        vec2 tseg = normalize(t1.xy());
        sc.right = vec3(th.y, -th.x, 0);
        float cosHalf = Max(0.5f, dot(th, tseg));
        sc.miter = 1.f / cosHalf;
        out.push_back(sc);
    }
}

// Paint stripe along the strip between two sections at lateral offset (meters from center)
void stripe(MeshData& m, const Section& a, const Section& b, float lat, float width, float s0, float s1, u32 mat, vec2 org) {
    // Interpolate along the segment for dashed ranges
    float la = a.s, lb = b.s;
    if (s1 <= la || s0 >= lb) return;
    float t0 = Saturate((s0 - la) / Max(lb - la, 1e-4f)), t1 = Saturate((s1 - la) / Max(lb - la, 1e-4f));
    auto P = [&](float t, float off) {
        vec3 ca = sectionPoint(a, off, 0.02f), cb = sectionPoint(b, off, 0.02f);
        return lerp(ca, cb, t) - vec3(org, 0);
    };
    vec3 p0 = P(t0, lat - width * 0.5f), p1 = P(t0, lat + width * 0.5f), p2 = P(t1, lat + width * 0.5f), p3 = P(t1, lat - width * 0.5f);
    m.quadFacing(p0, p1, p2, p3, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), 0xffffffffu, mat, vec3(0, 0, 1));
}

}  // namespace roadmesh_detail

using namespace roadmesh_detail;

struct RoadCellOutput {
    MeshData road;      // asphalt, sidewalks, curbs, bridge structures
    MeshData decals;    // paint (depth-biased)
    std::vector<PropInstance> props;
    std::vector<LightInstance> lights;
    std::vector<CollisionBox> collision;  // deck parapets, guardrails, railings, street furniture
    MeshData street;    // LOD0-only street dressing: overhead lines, span wires, strain poles, tree grates, road plates
    bool detail = true; // LOD0 cell: furniture, props, lights and street dressing are built
};

// Oriented collision slab along a road segment: lateral band [lat0, lat1] (right = +), from z0 to z0 + h above the road
// Collision matching a barrier quad strip exactly: inner line p0 -> p1, outer line q0 -> q1 (world space, road-level z)
inline void roadRailCollisionQuad(RoadCellOutput& out, vec3 p0, vec3 p1, vec3 q0, vec3 q1, float h) {
    vec3 m0 = (p0 + q0) * 0.5f, m1 = (p1 + q1) * 0.5f;
    vec2 d = m1.xy() - m0.xy();
    float L = length(d);
    if (L < 0.05f) return;
    float w = (length(q0.xy() - p0.xy()) + length(q1.xy() - p1.xy())) * 0.5f;
    float zlo = Min(Min(p0.z, p1.z), Min(q0.z, q1.z)), zhi = Max(Max(p0.z, p1.z), Max(q0.z, q1.z));
    CollisionBox cb;
    vec3 c = (m0 + m1) * 0.5f;
    cb.c = vec3(c.x, c.y, (zlo - 0.3f + zhi + h) * 0.5f);
    cb.ax = d / L;
    cb.he = vec3(L * 0.5f + 0.05f, Max(0.08f, w * 0.5f), (zhi + h - zlo + 0.3f) * 0.5f);
    out.collision.push_back(cb);
}

inline void roadRailCollision(RoadCellOutput& out, vec3 a, vec3 b, vec3 right, float lat0, float lat1, float h) {
    vec2 d = b.xy() - a.xy();
    float L = length(d);
    if (L < 0.05f) return;
    CollisionBox cb;
    vec3 m = (a + b) * 0.5f + right * ((lat0 + lat1) * 0.5f);
    float zlo = Min(a.z, b.z), zhi = Max(a.z, b.z);
    cb.c = vec3(m.x, m.y, (zlo - 0.3f + zhi + h) * 0.5f);
    cb.ax = d / L;
    cb.he = vec3(L * 0.5f + 0.05f, Max(0.08f, (lat1 - lat0) * 0.5f), (zhi + h - zlo + 0.3f) * 0.5f);
    out.collision.push_back(cb);
}


// ------------------------------------------------------------------------------------------------ street dressing
// Street furniture, trees, utility lines, span-wire signals and road works by district. Everything that other cells or the
// gameplay layer must agree on (fixtures, utility poles, work zones) is derived per edge from the edge seed, independent
// of the cell being built; only the emission is clipped to the cell.
namespace street_dressing {

struct Profile {
    bool poles = false;        // wooden utility poles with overhead lines (they carry the street lamps as well)
    bool poleAvenues = false;  // ... also along avenues
    bool span = false;         // span-wire signals at signalised junctions (mast arms otherwise)
    bool oaks = false;         // shade oaks in the verge instead of trees in grates
    float trees = 0.f;         // share of curb slots with a street tree
    float palms = 0.f;         // share of curb slots with a royal palm (boulevards and avenues; every street when > 0.7)
    float meters = 0.f;        // share of block faces with a run of parking meters
    float news = 0.f;          // share of block corners with news boxes
    float mail = 0.f;          // share of block corners with a collection box
    float bins = 0.f;          // share of block corners with a litter bin
    float bikes = 0.f;         // share of block faces with a bike rack
    float planters = 0.f;      // share of frontage slots with a planter
    float bags = 0.f;          // share of block faces with trash bags out at the curb
    float works = 0.f;         // share of block faces with a road-works zone in the parking strip
};

Profile profileFor(Region r) {
    Profile p;
    switch (r) {
        case REG_DOWNTOWN:
            p.trees = 0.7f; p.palms = 0.45f; p.meters = 0.55f; p.news = 0.35f; p.mail = 0.3f; p.bins = 0.5f; p.bikes = 0.25f; p.planters = 0.12f; p.works = 0.012f;
            break;
        case REG_FINANCIAL:
            p.trees = 0.85f; p.meters = 0.3f; p.news = 0.25f; p.mail = 0.25f; p.bins = 0.5f; p.bikes = 0.2f; p.planters = 0.3f; p.works = 0.012f;
            break;
        case REG_MIDTOWN:
            p.trees = 0.6f; p.meters = 0.45f; p.news = 0.25f; p.mail = 0.2f; p.bins = 0.4f; p.bikes = 0.45f; p.planters = 0.14f; p.bags = 0.12f; p.works = 0.012f;
            break;
        case REG_NORTH_CITY:
            p.poles = true; p.span = true; p.trees = 0.35f; p.meters = 0.3f; p.news = 0.18f; p.mail = 0.15f; p.bins = 0.3f; p.bikes = 0.1f; p.bags = 0.15f;
            p.works = 0.01f;
            break;
        case REG_CALLE_LUNA:
            p.poles = true; p.span = true; p.trees = 0.2f; p.palms = 0.45f; p.meters = 0.45f; p.news = 0.45f; p.mail = 0.25f; p.bins = 0.45f; p.bikes = 0.1f;
            p.bags = 0.35f; p.works = 0.012f;
            break;
        case REG_BEACH:
            p.palms = 0.85f; p.meters = 0.5f; p.news = 0.25f; p.mail = 0.15f; p.bins = 0.5f; p.bikes = 0.45f; p.planters = 0.2f; p.works = 0.008f;
            break;
        case REG_KEY_CORAL: case REG_BAY_ISLAND:
            p.palms = 0.8f; p.planters = 0.1f; p.bins = 0.2f;
            break;
        case REG_GROVE:
            p.poles = true; p.span = true; p.oaks = true; p.trees = 0.55f; p.bins = 0.15f;
            break;
        case REG_FLATS:
            p.poles = true; p.poleAvenues = true; p.span = true; p.trees = 0.05f; p.news = 0.08f; p.bins = 0.25f; p.bags = 0.45f; p.works = 0.015f;
            break;
        case REG_SUBURBS:
            p.poles = true; p.span = true; p.palms = 0.25f; p.bins = 0.1f; p.works = 0.005f;
            break;
        case REG_LAKE_TOWN: case REG_HARLOW: case REG_FORT_CASTELL: case REG_KEY_TOWN: case REG_GULF_TOWN:
            p.poles = true; p.poleAvenues = true; p.span = true; p.news = 0.12f; p.mail = 0.12f; p.bins = 0.3f; p.bags = 0.1f;
            p.works = 0.008f;
            break;
        case REG_REDLAND: case REG_FARMLAND: case REG_RIDGE: case REG_SAWGRASS: case REG_KEYS:
            p.poles = true; p.poleAvenues = true; p.span = true;
            break;
        default: break;
    }
    return p;
}

Region edgeRegion(const RoadEdge& e, const WorldMap& map) {
    vec3 m = e.posAt(e.length * 0.5f);
    return map.regionAt(m.x, m.y);
}

// Edges lined with utility poles; the poles carry the street lamps there, so the regular streetlights are left out
bool poleEdge(const RoadEdge& e, const Profile& p) {
    if (!p.poles || (e.flags & (RF_UNPAVED | RF_ELEVATED))) return false;
    switch (e.cls) {
        case RC_STREET: case RC_LANE: case RC_RURAL: return true;
        case RC_AVENUE: return p.poleAvenues;
        default: return false;
    }
}

// Occupancy of the sidewalks around one cell (8 m buckets, cell +- 16 m): items keep their distance from each other
struct Occupancy {
    static constexpr int kN = 36;
    static constexpr float kC = 8.f;
    vec2 org;
    std::vector<std::vector<vec3>> g;
    explicit Occupancy(vec2 cellOrg) : org(cellOrg - vec2(16.f)), g(kN * kN) {}
    bool free(vec2 p, float r) const {
        int x0 = Max(0, (int)floorf((p.x - r - 3.f - org.x) / kC)), x1 = Min(kN - 1, (int)floorf((p.x + r + 3.f - org.x) / kC));
        int y0 = Max(0, (int)floorf((p.y - r - 3.f - org.y) / kC)), y1 = Min(kN - 1, (int)floorf((p.y + r + 3.f - org.y) / kC));
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++)
                for (const vec3& q : g[(size_t)y * kN + x])
                    if (length2(q.xy() - p) < (q.z + r) * (q.z + r)) return false;
        return true;
    }
    void add(vec2 p, float r) {
        int x = (int)floorf((p.x - org.x) / kC), y = (int)floorf((p.y - org.y) / kC);
        if (x >= 0 && y >= 0 && x < kN && y < kN) g[(size_t)y * kN + x].push_back(vec3(p, r));
    }
};

// A fixture along an edge (streetlight, median palm, sidewalk bench / shelter / bin / hydrant) with its lamps
struct Fixture {
    PropInstance pi;
    int nLights = 0;
    LightInstance li[2];
    float r = 0.8f;   // occupancy radius
    int side = 0;     // sidewalk side (+1 right of n0->n1), 0 = median
    float s = 0.f;
};

inline float yawFacing(vec2 d) { return atan2f(d.x, -d.y); }   // prop local -y faces d

// Streetlights, median palms and sidewalk details along the whole edge. The sidewalk hash sequence (bus shelters, benches)
// is mirrored by the AI's bench and bus-stop scenario points (game/lanes.cpp): keep it unchanged.
void edgeFixtures(const RoadNetwork& net, int ei, const WorldMap& map, bool poleLit, std::vector<Fixture>& fx) {
    const RoadEdge& e = net.edges[ei];
    const RoadClassInfo& ri = roadInfo(e.cls);
    bool hwy = e.cls == RC_HIGHWAY || e.cls == RC_RAMP;
    if (e.flags & RF_UNPAVED) return;
    bool twoWay = e.lanesB > 0 && e.lanesF > 0;
    float hw = e.halfWidth, sw = e.sidewalk;
    float spacing = hwy ? 55.f : (e.cls == RC_RURAL ? 70.f : (e.cls == RC_LANE ? 38.f : 32.f));
    bool lit = !poleLit;
    if (e.cls == RC_RURAL) lit = lit && (e.seed & 1) == 0;
    float s0 = e.cut0 + 8.f, s1 = e.length - e.cut1 - 8.f;
    int k = 0;
    for (float s = s0 + fmodf((float)(e.seed % 100), spacing * 0.5f); s < s1 && lit; s += spacing, k++) {
        vec3 c = e.posAt(s);
        vec3 t = e.tangentAt(s);
        vec3 rv = normalize(vec3(t.y, -t.x, 0));
        float ground = map.heightAt(c.x, c.y);
        bool deck = c.z - ground > 2.2f;
        int side = (k & 1) ? 1 : -1;
        if (hwy && !twoWay) side = 1;
        Fixture f;
        PropInstance& pi = f.pi;
        pi.flags = 0;
        pi.variant = (u8)(e.seed % 3);
        f.s = s;
        if (hwy && twoWay) {
            pi.pos = c + vec3(0, 0, 0.85f);
            pi.type = PROP_STREETLIGHT_DOUBLE;
            pi.yaw = atan2f(rv.y, rv.x);
            pi.scale = 1.f;
        } else {
            float off = hw + (sw > 0 ? 0.6f : (deck ? 0.6f : 1.4f));
            pi.pos = c + rv * (side * off) + vec3(0, 0, sw > 0 ? 0.15f : 0.f);
            pi.type = PROP_STREETLIGHT;
            pi.yaw = atan2f(-rv.y * side, -rv.x * side);
            pi.scale = hwy ? 1.3f : (e.cls <= RC_AVENUE ? 1.1f : 1.f);
            f.side = side;
        }
        // never on the pavement of another road (junction flares, ramps and parallel roads crowd the verge), never under a deck
        if (net.onPavement(pi.pos.xy(), pi.pos.z, 0.3f, pi.type == PROP_STREETLIGHT_DOUBLE ? ei : -1)) continue;
        if (net.onPavement(pi.pos.xy(), pi.pos.z + 7.f, 1.0f, -1, 6.3f)) continue;
        float armLen = 2.2f * pi.scale, poleH = 8.6f * pi.scale;
        LightInstance li;
        li.dir = vec3(0, 0, -1);
        li.cone = 0.2f;
        li.type = 0;
        if (pi.type == PROP_STREETLIGHT_DOUBLE) {
            for (int s2 = -1; s2 <= 1; s2 += 2) {
                li.pos = pi.pos + rv * (s2 * 2.9f) + vec3(0, 0, 11.4f);
                li.color = vec3(1.0f, 0.78f, 0.52f) * 9000.f;
                li.radius = 38.f;
                f.li[f.nLights++] = li;
            }
        } else {
            vec3 armDir(cosf(pi.yaw), sinf(pi.yaw), 0);
            li.pos = pi.pos + armDir * armLen + vec3(0, 0, poleH);
            bool warm = (e.seed >> 3) % 3 != 0;
            li.color = (warm ? vec3(1.0f, 0.72f, 0.42f) : vec3(0.85f, 0.9f, 1.0f)) * 7000.f;
            li.radius = 30.f;
            f.li[f.nLights++] = li;
        }
        f.r = 0.9f;
        fx.push_back(f);
    }
    // Median palms on boulevards. Where the median ends at a node that turns traffic across it (an on/off ramp, a lane drop
    // onto a road without a median, a dead end's U-turn), the lane graph's connectors sweep over the last stretch of the
    // median: keep its trunks well clear of those curves.
    auto sweepEnd = [&](int ni) {
        const RoadNode& nd = net.nodes[ni];
        if (nd.edges.size() <= 1) return true;
        for (int oe : nd.edges) {
            if (oe == ei) continue;
            const RoadEdge& o = net.edges[oe];
            if (o.cls == RC_RAMP || o.cls == RC_HIGHWAY) return true;
            if (nd.edges.size() == 2 && roadInfo(o.cls).median <= 0.f) return true;
        }
        return false;
    };
    const float kMedianClear = 45.f;
    float palmS0 = e.cut0 + (sweepEnd(e.n0) ? kMedianClear : 10.f), palmS1 = e.length - e.cut1 - (sweepEnd(e.n1) ? kMedianClear : 10.f);
    if (ri.median > 0.f && !hwy && twoWay) {
        for (float s = e.cut0 + 10.f; s < e.length - e.cut1 - 10.f; s += 14.f) {
            if (s < palmS0 || s > palmS1) continue;
            vec3 c = e.posAt(s);
            if (c.z - map.heightAt(c.x, c.y) > 2.f) continue;
            if (net.onPavement(c.xy(), c.z, 0.5f, ei)) continue;
            Fixture f;
            PropInstance& pi = f.pi;
            pi.pos = c + vec3(0, 0, 0.18f);
            pi.yaw = hashToFloat(hash2i((int)s, (int)e.seed)) * kTwoPi;
            pi.scale = 0.85f + hashToFloat(hash2i((int)s, (int)e.seed + 1)) * 0.35f;
            pi.type = PROP_PALM_TALL;
            pi.variant = (u8)(hash2i((int)s, 3) % 4);
            pi.flags = 0;
            f.s = s;
            f.r = 0.f;
            fx.push_back(f);
        }
    }
    // Sidewalk details: bus shelters, benches, bins, hydrants
    if (sw > 2.f && !hwy) {
        u32 h = e.seed;
        for (float s = e.cut0 + 15.f; s < e.length - e.cut1 - 15.f; s += 23.f) {
            h = hash32(h + 1u);
            int side = (h & 1) ? 1 : -1;
            vec3 c = e.posAt(s);
            vec3 t = e.tangentAt(s);
            vec3 rv = normalize(vec3(t.y, -t.x, 0));
            float r = hashToFloat(h >> 1);
            PropType type = r < 0.08f ? PROP_BUS_STOP : (r < 0.22f ? PROP_BENCH : (r < 0.4f ? PROP_BIN : (r < 0.52f ? PROP_HYDRANT : PROP_COUNT)));
            if (type == PROP_COUNT) continue;
            Fixture f;
            PropInstance& pi = f.pi;
            float off = hw + (type == PROP_BUS_STOP ? sw - 1.2f : 0.9f);
            pi.pos = c + rv * (side * off) + vec3(0, 0, 0.15f);
            if (net.onPavement(pi.pos.xy(), pi.pos.z, 0.3f)) continue;
            pi.yaw = atan2f(t.y, t.x) + (side > 0 ? kPi : 0.f);
            pi.scale = 1.f;
            pi.type = (u8)type;
            pi.variant = type == PROP_BUS_STOP ? (u8)((h >> 9) & 3u) : 0;
            pi.flags = 0;
            f.side = side;
            f.s = s;
            f.r = type == PROP_BUS_STOP ? 2.3f : (type == PROP_BENCH ? 1.1f : 0.5f);
            fx.push_back(f);
        }
    }
}

// Wooden utility poles along one side of the edge (every ~38 m), each with its crossarm across the street
struct Pole {
    vec3 pos;
    vec2 x;        // local +x (toward the street)
    u8 variant;    // bit 0 transformer, bit 1 street lamp
    bool ok;
    float s;
};

void edgePoles(const RoadNetwork& net, int ei, const WorldMap& map, const std::vector<Fixture>& fx, std::vector<Pole>& poles) {
    const RoadEdge& e = net.edges[ei];
    int side = ((e.seed >> 5) & 1u) ? 1 : -1;
    bool walk = e.sidewalk > 0.5f;
    float lat = walk ? e.halfWidth + 0.45f : e.halfWidth + 1.8f;
    float end = e.length - e.cut1 - 3.f;
    int k = 0;
    for (float s = e.cut0 + 4.f + (float)(e.seed % 5u); s < end; k++) {
        u32 h = hash32(e.seed * 31u + (u32)k * 7919u + 17u);
        vec3 c = e.posAt(s);
        vec3 t = e.tangentAt(s);
        vec2 rv = normalize(vec2(t.y, -t.x));
        Pole P;
        P.s = s;
        vec2 p2 = c.xy() + rv * (side * lat);
        P.pos = vec3(p2, walk ? c.z + 0.15f : Min(c.z, map.heightAt(p2.x, p2.y)));
        P.x = -rv * (float)side;
        bool lamp = e.cls == RC_LANE ? (k & 1) == 0 : (e.cls == RC_RURAL ? ((e.seed & 1u) == 0 && k % 3 == 0) : true);
        P.variant = (u8)((hashToFloat(h) < 0.22f ? 1 : 0) | (lamp ? 2 : 0));
        P.ok = c.z - map.heightAt(c.x, c.y) < 1.5f && fabsf(P.pos.z - c.z) < 1.2f;
        if (P.ok && net.onPavement(p2, P.pos.z, 0.3f)) P.ok = false;
        if (P.ok && gSites && gSites->blocksVegetation(p2)) P.ok = false;
        if (P.ok && gBuildings && gBuildings->pointInBuilding(p2, 0.4f)) P.ok = false;
        if (P.ok)
            for (const Fixture& f : fx)
                if (f.side == side && fabsf(f.s - s) < 2.8f) P.ok = false;
        poles.push_back(P);
        s += 34.f + hashToFloat(hash32(h)) * 8.f;
    }
}

// Overhead line: two crossed ribbons (double sided) hanging in a parabola between a and b
void wire(MeshData& m, vec3 a, vec3 b, float sag, float r, u32 col, u32 mat, vec3 o3, int segs) {
    vec3 d = b - a;
    float L = length(d);
    if (L < 0.5f) return;
    vec3 dir = d / L;
    vec3 side = cross(dir, vec3(0, 0, 1));
    side = length2(side) < 1e-6f ? vec3(1, 0, 0) : normalize(side);
    vec3 up = cross(side, dir);
    for (int rb = 0; rb < 2; rb++) {
        vec3 w = normalize(rb ? side + up : side - up) * r;
        vec3 n = normalize(cross(w, dir));
        u32 base = (u32)m.verts.size();
        for (int k = 0; k <= segs; k++) {
            float t = (float)k / segs;
            vec3 p = a + d * t - vec3(0, 0, 4.f * sag * t * (1.f - t)) - o3;
            m.addVertex(p - w, n, dir, vec2(0, t * L), col, mat);
            m.addVertex(p + w, n, dir, vec2(1, t * L), col, mat);
        }
        for (int k = 0; k < segs; k++) {
            u32 i = base + 2u * (u32)k;
            m.quadIdx(i, i + 2, i + 3, i + 1);
            m.quadIdx(i, i + 1, i + 3, i + 2);
        }
    }
}

// Tapered square concrete strain pole (span-wire signals)
void strainPole(RoadCellOutput& out, vec3 base, float h, vec3 o3) {
    u32 col = packRGBA8(0.66f, 0.65f, 0.62f, 1), mat = makeMat(MAT_CONCRETE);
    float r0 = 0.19f, r1 = 0.11f;
    vec3 c[4] = {vec3(-1, -1, 0), vec3(1, -1, 0), vec3(1, 1, 0), vec3(-1, 1, 0)};
    vec3 b0 = base - o3 - vec3(0, 0, 0.4f), b1 = base - o3 + vec3(0, 0, h);
    for (int k = 0; k < 4; k++) {
        vec3 p0 = b0 + c[k] * r0, p1 = b0 + c[(k + 1) & 3] * r0, p2 = b1 + c[(k + 1) & 3] * r1, p3 = b1 + c[k] * r1;
        vec3 n = normalize(vec3((c[k] + c[(k + 1) & 3]).xy(), 0));
        out.street.quadFacing(p0, p1, p2, p3, vec2(0, 0), vec2(r0 * 2, 0), vec2(r0 * 2, h), vec2(0, h), col, mat, n);
    }
    out.street.quadFacing(b1 + c[0] * r1, b1 + c[1] * r1, b1 + c[2] * r1, b1 + c[3] * r1, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), col, mat, vec3(0, 0, 1));
    CollisionBox cb;
    cb.c = base + vec3(0, 0, h * 0.5f);
    cb.ax = vec2(1, 0);
    cb.he = vec3(0.2f, 0.2f, h * 0.5f);
    out.collision.push_back(cb);
}

// Flat plate / patch on the ground (steel plates, fresh asphalt, tree grates), axis ax along the street
void groundPlate(MeshData& m, vec3 c, vec2 ax, float hx, float hy, float thick, u32 col, u32 mat, vec3 o3) {
    vec3 X(ax * hx, 0), Y(perp(ax) * hy, 0), up(0, 0, thick);
    vec3 p0 = c - X - Y - o3, p1 = c + X - Y - o3, p2 = c + X + Y - o3, p3 = c - X + Y - o3;
    m.quadFacing(p0 + up, p1 + up, p2 + up, p3 + up, vec2(0, 0), vec2(hx * 2, 0), vec2(hx * 2, hy * 2), vec2(0, hy * 2), col, mat, vec3(0, 0, 1));
    if (thick > 0.01f) {
        vec3 q[4] = {p0, p1, p2, p3};
        for (int k = 0; k < 4; k++) {
            vec3 a = q[k], b = q[(k + 1) & 3];
            vec3 n = normalize(vec3((a + b - p0 - p2).xy(), 0));
            m.quadFacing(a, b, b + up, a + up, vec2(0, 0), vec2(1, 0), vec2(1, thick), vec2(0, thick), col, mat, n);
        }
    }
}

// Road works in the parking strip: deterministic per edge side so the parked-car spawner can stay clear (roadWorkZoneAt)
bool edgeWorkZone(const RoadNetwork& net, int ei, const Profile& pf, int side, float* sc) {
    const RoadEdge& e = net.edges[ei];
    if (pf.works <= 0.f || (e.cls != RC_STREET && e.cls != RC_AVENUE) || (e.flags & (RF_UNPAVED | RF_BRIDGE | RF_ELEVATED))) return false;
    const RoadClassInfo& ri = roadInfo(e.cls);
    if (ri.shoulder < 2.f || e.lanesF == 0 || e.lanesB == 0) return false;
    float a = e.cut0 + 26.f, b = e.length - e.cut1 - 26.f;
    if (b - a < 12.f) return false;
    u32 h = hash32(e.seed * 5u + (side > 0 ? 101u : 202u));
    if (hashToFloat(h) >= pf.works) return false;
    float c = Lerp(a, b, 0.2f + 0.6f * hashToFloat(hash32(h)));
    if (dot(e.tangentAt(c - 13.f).xy(), e.tangentAt(c + 13.f).xy()) < 0.97f) return false;   // straight stretches only
    // the closed strip must be this road's own: no other road's pavement (merges, parallel service roads) across it
    float laneEdge = ri.median * 0.5f + (side > 0 ? e.lanesF : e.lanesB) * ri.laneWidth;
    for (int k = -2; k <= 2; k++) {
        float s = c + k * 6.5f;
        vec3 p = e.posAt(s), t = e.tangentAt(s);
        vec2 rv = normalize(vec2(t.y, -t.x)) * (float)side;
        for (float lat : {laneEdge + 0.3f, e.halfWidth, e.halfWidth + 1.f})
            if (net.onPavement(p.xy() + rv * lat, p.z, 0.2f, ei)) return false;
    }
    *sc = c;
    return true;
}

// Span-wire signals (South Florida style): a messenger and a tether strung diagonally between two concrete strain poles,
// one signal head per approach hanging where the wire crosses the approach's incoming lanes, facing that traffic.
struct SpanApproach {
    vec2 dir;
    vec3 cutPt;
    float hw, sw;
    int edge;
    bool outgoing;
    float radius;
};

bool spanWireSignals(const RoadNetwork& net, const RoadNode& nd, const std::vector<SpanApproach>& ap, const WorldMap& map, RoadCellOutput& out, vec2 org) {
    int n = (int)ap.size();
    if (n < 3) return false;
    vec3 o3(org, 0);
    std::vector<vec3> corner;
    for (int i = 0; i < n; i++) {
        const SpanApproach& A = ap[i];
        const SpanApproach& B = ap[(i + 1) % n];
        vec2 lA = perp(A.dir), lB = perp(B.dir);
        vec2 p1 = A.cutPt.xy() + lA * A.hw, p2 = B.cutPt.xy() - lB * B.hw;
        vec2 C = (p1 + p2) * 0.5f;
        float den = cross(A.dir, B.dir);
        if (fabsf(den) > 0.15f) {
            float t = cross(p2 - p1, B.dir) / den;
            vec2 X = p1 + A.dir * t;
            if (length(X - nd.p) < A.radius * 2.5f) C = X;
        }
        vec2 q = p1 * 0.25f + C * 0.5f + p2 * 0.25f;
        vec2 outw = q - nd.p;
        outw = length2(outw) > 1e-4f ? normalize(outw) : A.dir;
        float swc = Min(A.sw, B.sw);
        vec2 pp = q + outw * (swc > 0.5f ? Min(swc * 0.5f, 1.3f) : 1.8f);
        float z = Lerp(A.cutPt.z, B.cutPt.z, 0.5f) + (swc > 0.5f ? 0.15f : 0.f);
        bool ok = !net.onPavement(pp, z, 0.35f) && !(gBuildings && gBuildings->pointInBuilding(pp, 0.5f)) && !(gSites && gSites->blocksVegetation(pp));
        corner.push_back(ok ? vec3(pp, z) : vec3(1e9f));
    }
    int bi = -1, bj = -1;
    float best = 0.f;
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n; j++) {
            if (corner[i].x > 1e8f || corner[j].x > 1e8f) continue;
            float d = length(corner[i].xy() - corner[j].xy());
            if (d > best && d < 75.f) { best = d; bi = i; bj = j; }
        }
    if (bi < 0 || best < 14.f) return false;
    vec3 P1 = corner[bi], P2 = corner[bj];
    const float poleH = 10.6f, attach = 10.1f, tether = 1.8f;
    vec3 T1 = P1 + vec3(0, 0, attach), T2 = P2 + vec3(0, 0, attach);
    float L = length(T2.xy() - T1.xy());
    float sag = 0.3f + 0.035f * L;
    // an elevated road over the junction takes the space of the wires: keep mast arms there
    for (int k = 0; k <= 8; k++) {
        float t = k / 8.f;
        vec3 w = lerp(T1, T2, t) - vec3(0, 0, 4.f * sag * t * (1.f - t));
        if (net.onPavement(w.xy(), w.z, 2.5f, -1, 5.5f)) return false;
    }
    // heads: parameter along the wire for each approach with incoming lanes
    struct Head { float t; int a; int lanes; };
    std::vector<Head> heads;
    vec2 D = T2.xy() - T1.xy();
    for (int i = 0; i < n; i++) {
        const SpanApproach& A = ap[i];
        const RoadEdge& e = net.edges[A.edge];
        if (e.cls == RC_HIGHWAY || e.cls == RC_RAMP) continue;
        int nIn = A.outgoing ? e.lanesB : e.lanesF;
        if (nIn <= 0) continue;
        const RoadClassInfo& ri = roadInfo(e.cls);
        bool twoWay = e.lanesF > 0 && e.lanesB > 0;
        float inLat = twoWay ? ri.median * 0.5f + nIn * ri.laneWidth * 0.5f : 0.f;
        vec2 l = perp(A.dir);
        vec2 Q = A.cutPt.xy() + l * inLat;
        float den = cross(D, -A.dir);
        float t;
        if (fabsf(den) > 0.25f * L) t = cross(Q - T1.xy(), -A.dir) / den;
        else t = dot(nd.p + l * inLat - T1.xy(), D) / Max(L * L, 1e-3f);
        heads.push_back({Clamp(t, 0.12f, 0.88f), i, nIn});
    }
    if (heads.empty()) return false;
    std::sort(heads.begin(), heads.end(), [](const Head& a, const Head& b) { return a.t < b.t; });
    float minGap = 1.1f / Max(L, 1.f);
    for (size_t k = 1; k < heads.size(); k++) heads[k].t = Max(heads[k].t, heads[k - 1].t + minGap);
    strainPole(out, P1, poleH, o3);
    strainPole(out, P2, poleH, o3);
    u32 cable = packRGBA8(0.07f, 0.07f, 0.07f, 1), rubber = makeMat(MAT_RUBBER);
    wire(out.street, T1, T2, sag, 0.03f, cable, rubber, o3, 12);
    wire(out.street, T1 - vec3(0, 0, tether), T2 - vec3(0, 0, tether), sag, 0.02f, cable, rubber, o3, 12);
    for (const Head& hd : heads) {
        const SpanApproach& A = ap[hd.a];
        float t = Min(hd.t, 0.92f);
        vec3 p = lerp(T1, T2, t) - vec3(0, 0, 4.f * sag * t * (1.f - t) + 0.03f);
        PropInstance pi;
        pi.pos = p;
        pi.yaw = yawFacing(A.dir);
        pi.scale = 1.f;
        pi.type = PROP_SIGNAL_SPAN;
        pi.variant = (u8)(hd.lanes >= 2 ? 1 : 0);
        pi.flags = (u16)A.edge;
        out.props.push_back(pi);
    }
    (void)map;
    return true;
}

// Street dressing of one edge (both sidewalks), after the fixtures and poles of every candidate edge are reserved
void dressEdge(const RoadNetwork& net, int ei, const WorldMap& map, int cx, int cy, const Profile& pf, const std::vector<Fixture>& fx, Occupancy& occ,
               RoadCellOutput& out) {
    const RoadEdge& e = net.edges[ei];
    if (e.flags & (RF_UNPAVED | RF_ELEVATED)) return;
    if (e.cls == RC_HIGHWAY || e.cls == RC_RAMP || e.cls == RC_DIRT) return;
    const RoadClassInfo& ri = roadInfo(e.cls);
    float hw = e.halfWidth, sw = e.sidewalk;
    float sA = e.cut0 + 6.f, sB = e.length - e.cut1 - 6.f;
    if (sB - sA < 4.f) return;
    vec2 org = cellOrigin(cx, cy);
    vec3 o3(org, 0);
    // quick reject: edge bounding box against the cell (+16 m)
    {
        vec2 mn(1e9f), mx(-1e9f);
        for (const vec3& p : e.pts) { mn = vmin(mn, p.xy()); mx = vmax(mx, p.xy()); }
        if (mx.x < org.x - 16.f || mx.y < org.y - 16.f || mn.x > org.x + kCellSize + 16.f || mn.y > org.y + kCellSize + 16.f) return;
    }
    auto inReach = [&](vec2 p) { return p.x > org.x - 16.f && p.y > org.y - 16.f && p.x < org.x + kCellSize + 16.f && p.y < org.y + kCellSize + 16.f; };
    struct Frame { vec3 c; vec2 t, rv; };
    auto frame = [&](float s) {
        Frame f;
        f.c = e.posAt(s);
        vec3 t = e.tangentAt(s);
        f.t = normalize(t.xy());
        f.rv = vec2(f.t.y, -f.t.x);
        return f;
    };
    // sidewalk spot at (s, side, lateral): position on the walk, validity against other roads, buildings, sites and decks
    auto spot = [&](float s, int side, float lat, float r, vec3* pos) {
        Frame f = frame(s);
        vec2 p = f.c.xy() + f.rv * (side * lat);
        if (!inReach(p)) return false;
        if (f.c.z - map.heightAt(f.c.x, f.c.y) > 1.5f) return false;
        vec3 q(p, f.c.z + (sw > 0.5f ? 0.15f : 0.f));
        if (sw <= 0.5f) q.z = Min(f.c.z, map.heightAt(p.x, p.y));
        if (!occ.free(p, r)) return false;
        // no other road's pavement within reach, including ramps and decks passing a few metres above or below
        if (net.onPavement(p, q.z, 0.15f + r * 0.5f, -1, 6.f)) return false;
        if (gBuildings && gBuildings->pointInBuilding(p, 0.3f)) return false;
        if (gSites && gSites->blocksVegetation(p)) return false;
        *pos = q;
        return true;
    };
    auto put = [&](PropType type, vec3 pos, float yaw, int variant, float scale, float r) {
        occ.add(pos.xy(), r);
        if (!inCell(pos.xy(), cx, cy)) return false;
        PropInstance pi;
        pi.pos = pos;
        pi.yaw = yaw;
        pi.scale = scale;
        pi.type = (u8)type;
        pi.variant = (u8)variant;
        pi.flags = 0;
        out.props.push_back(pi);
        return true;
    };
    auto box = [&](vec3 base, vec2 ax, vec3 he) {
        CollisionBox cb;
        cb.c = base + vec3(0, 0, he.z);
        cb.ax = ax;
        cb.he = he;
        out.collision.push_back(cb);
    };
    const u32 iron = packRGBA8(0.1f, 0.1f, 0.11f, 1), ironMat = makeMat(MAT_METAL_BRUSHED);
    for (int side = -1; side <= 1; side += 2) {
        u32 hs = hash32(e.seed * 977u + (side > 0 ? 11u : 23u));
        auto rnd = [&](u32 salt) { return hashToFloat(hash32(hs + salt * 2654435761u)); };
        float workC = 0.f;
        bool works = edgeWorkZone(net, ei, pf, side, &workC);
        // ---- corner clusters: news boxes, collection box, bin (curb zone, facing the walk)
        if (sw >= 2.4f) {
            for (int end = 0; end < 2; end++) {
                float s = end == 0 ? sA + 0.5f : sB - 0.5f, dirS = end == 0 ? 1.f : -1.f;
                u32 salt = 100u + (u32)end * 10u;
                int nNews = rnd(salt) < pf.news ? (rnd(salt + 1) < 0.7f ? 2 : 3) : 0;
                bool mail = rnd(salt + 2) < pf.mail, bin = rnd(salt + 3) < pf.bins;
                for (int k = 0; k < nNews + (mail ? 1 : 0) + (bin ? 1 : 0); k++) {
                    PropType type = k < nNews ? PROP_NEWS_BOX : ((mail && k == nNews) ? PROP_MAILBOX : PROP_BIN);
                    float r = type == PROP_NEWS_BOX ? 0.3f : 0.36f;
                    vec3 pos;
                    if (spot(s, side, hw + 0.55f, r, &pos)) {
                        Frame f = frame(s);
                        float yaw = yawFacing(f.rv * (float)side);
                        int var = type == PROP_NEWS_BOX ? (int)(rnd(salt + 5 + k) * 4.f) & 3 : 0;
                        if (put(type, pos, yaw, var, 1.f, r) && type == PROP_MAILBOX) box(pos, f.t, vec3(0.3f, 0.27f, 0.62f));
                    }
                    s += dirS * (type == PROP_NEWS_BOX ? 0.66f : 0.9f);
                }
            }
        }
        // ---- trees / palms at the curb (regular rhythm)
        bool palmStreet = pf.palms > 0.f && (e.cls == RC_BOULEVARD || e.cls == RC_AVENUE || pf.palms > 0.7f);
        bool treeStreet = !palmStreet && pf.trees > 0.f && (sw >= 2.9f || (pf.oaks && sw >= 1.5f));
        if ((palmStreet && sw >= 2.9f) || treeStreet) {
            float step = palmStreet ? 12.f : (pf.oaks ? 17.f : 14.f);
            float share = palmStreet ? pf.palms : pf.trees;
            int k = 0;
            for (float s = sA + 3.f + rnd(300) * step; s < sB - 3.f; s += step, k++) {
                if (rnd(400 + (u32)k) >= share) continue;
                if (works && fabsf(s - workC) < 13.f) continue;
                float lat = hw + (palmStreet ? 0.9f : (pf.oaks ? Min(sw * 0.5f, 1.4f) : 0.8f));
                vec3 pos;
                if (!spot(s, side, lat, 1.05f, &pos)) continue;
                Frame f = frame(s);
                u32 h = hash32(hs + (u32)k * 131u);
                if (palmStreet) {
                    if (put(PROP_PALM_TALL, pos, hashToFloat(h) * kTwoPi, (int)(h >> 8) & 3, 0.8f + hashToFloat(hash32(h)) * 0.3f, 1.05f))
                        groundPlate(out.street, pos + vec3(0, 0, 0.004f), f.t, 0.7f, 0.7f, 0.f, iron, ironMat, o3);
                } else if (pf.oaks) {
                    put(PROP_TREE_OAK, vec3(pos.xy(), pos.z - 0.02f), hashToFloat(h) * kTwoPi, (int)(h >> 8) & 3, 0.95f + hashToFloat(hash32(h)) * 0.35f, 1.4f);
                } else if (put(PROP_STREET_TREE, pos, hashToFloat(h) * kTwoPi, (int)(h >> 8) % 3, 0.9f + hashToFloat(hash32(h)) * 0.25f, 1.05f)) {
                    box(pos, f.t, vec3(0.17f, 0.17f, 2.2f));
                }
            }
        }
        // ---- parking meters along the parking strip (runs of 3-6, two-headed, facing the walk)
        if (pf.meters > 0.f && (e.cls == RC_STREET || e.cls == RC_AVENUE) && ri.shoulder >= 2.f && sw >= 2.4f && rnd(500) < pf.meters) {
            int cnt = 3 + (int)(rnd(501) * 4.f);
            float len = (cnt - 1) * 6.2f;
            float s = Lerp(sA + 4.f, Max(sA + 4.f, sB - 4.f - len), rnd(502));
            for (int k = 0; k < cnt; k++, s += 6.2f) {
                if (s > sB) break;
                if (works && fabsf(s - workC) < 12.f) continue;
                vec3 pos;
                if (!spot(s, side, hw + 0.42f, 0.3f, &pos)) continue;
                Frame f = frame(s);
                put(PROP_PARKING_METER, pos, yawFacing(f.rv * (float)side), 0, 1.f, 0.3f);
            }
        }
        // ---- bike rack (hoops across the walk)
        if (pf.bikes > 0.f && sw >= 3.f && rnd(600) < pf.bikes) {
            float s = Lerp(sA + 6.f, sB - 6.f, rnd(601));
            vec3 pos;
            if (spot(s, side, hw + 1.05f, 1.15f, &pos)) {
                Frame f = frame(s);
                if (put(PROP_BIKE_RACK, pos, atan2f(f.t.y, f.t.x), 0, 1.f, 1.15f)) box(pos, f.t, vec3(1.0f, 0.3f, 0.43f));
            }
        }
        // ---- planters in the frontage zone
        if (pf.planters > 0.f && sw >= 3.4f) {
            int k = 0;
            for (float s = sA + 4.f + rnd(700) * 9.f; s < sB - 4.f; s += 18.f, k++) {
                if (rnd(710 + (u32)k) >= pf.planters) continue;
                int var = rnd(760 + (u32)k) < 0.5f ? 0 : 1;
                float r = var ? 0.62f : 0.98f;
                vec3 pos;
                if (!spot(s, side, hw + sw - 0.62f, r, &pos)) continue;
                Frame f = frame(s);
                if (put(PROP_PLANTER, pos, atan2f(f.t.y, f.t.x), var, 1.f, r)) box(pos, f.t, var ? vec3(0.5f, 0.5f, 0.37f) : vec3(0.9f, 0.4f, 0.3f));
            }
        }
        // ---- trash bags out at the curb
        if (pf.bags > 0.f && sw >= 1.5f && rnd(800) < pf.bags) {
            int piles = 1 + (rnd(801) < 0.4f ? 1 : 0);
            for (int k = 0; k < piles; k++) {
                float s = Lerp(sA + 4.f, sB - 4.f, rnd(802 + (u32)k));
                vec3 pos;
                if (spot(s, side, hw + 0.55f, 0.65f, &pos)) put(PROP_TRASH_BAGS, pos, rnd(810 + (u32)k) * kTwoPi, 0, 0.9f + rnd(820 + (u32)k) * 0.3f, 0.65f);
            }
        }
        // ---- road works in the parking strip: barrier line on the lane edge, cone tapers, A-frame sign, steel plates
        if (works) {
            bool twoWay = e.lanesF > 0 && e.lanesB > 0;
            int nl = side > 0 ? e.lanesF : e.lanesB;
            float laneEdge = twoWay ? ri.median * 0.5f + nl * ri.laneWidth : (e.lanesF + e.lanesB) * ri.laneWidth * 0.5f;
            float latB = laneEdge + 0.42f;
            float up = side > 0 ? -1.f : 1.f;   // upstream along s (right-hand traffic on this side moves +s when side > 0)
            float z0 = 0.f;
            for (int k = -4; k <= 4; k++) {
                float s = workC + k * 2.f;
                Frame f = frame(s);
                vec3 pos(f.c.xy() + f.rv * (side * latB), f.c.z);
                z0 = f.c.z;
                if (put(PROP_BARRIER, pos, atan2f(f.t.y, f.t.x), k & 1, 1.f, 0.f)) box(pos, f.t, vec3(0.97f, 0.27f, 0.5f));
            }
            for (int end = -1; end <= 1; end += 2) {
                float s = workC + end * 9.2f;
                Frame f = frame(s);
                float latM = (latB + hw) * 0.5f;
                vec3 pos(f.c.xy() + f.rv * (side * latM), f.c.z);
                put(PROP_BARRIER, pos, atan2f(f.rv.y, f.rv.x), end > 0 ? 1 : 0, Min(1.f, (hw - latB) / 1.9f + 0.05f), 0.f);
            }
            for (int k = 1; k <= 4; k++) {
                float s = workC + up * (9.6f + 2.6f * k);
                Frame f = frame(s);
                float lat = Lerp(latB, hw - 0.35f, k / 4.f);
                put(PROP_CONE, vec3(f.c.xy() + f.rv * (side * lat), f.c.z), 0.f, 0, 1.f, 0.f);
            }
            for (int k = 1; k <= 2; k++) {
                float s = workC - up * (9.6f + 1.8f * k);
                Frame f = frame(s);
                put(PROP_CONE, vec3(f.c.xy() + f.rv * (side * (latB + 0.1f)), f.c.z), 0.f, 0, 1.f, 0.f);
            }
            {
                float s = workC + up * 24.f;
                vec3 pos;
                if (spot(s, side, hw + 0.75f, 0.6f, &pos)) {
                    Frame f = frame(s);
                    put(PROP_WORK_SIGN, pos, atan2f(-f.t.x, f.t.y), 0, 1.f, 0.6f);
                }
            }
            Frame fc = frame(workC);
            if (inCell(fc.c.xy(), cx, cy)) {
                float latP = (latB + 0.45f + hw) * 0.5f, hyP = Min(0.7f, (hw - latB - 0.45f) * 0.5f);
                u32 steel = packRGBA8(0.32f, 0.3f, 0.28f, 1);
                for (int k = 0; k < 2; k++) {
                    Frame f = frame(workC - 3.2f + k * 2.5f);
                    groundPlate(out.street, vec3(f.c.xy() + f.rv * (side * latP), f.c.z), f.t, 1.2f, hyP, 0.025f, steel, makeMat(MAT_METAL_BRUSHED), o3);
                }
                Frame f = frame(workC + 4.5f);
                groundPlate(out.street, vec3(f.c.xy() + f.rv * (side * latP), f.c.z + 0.008f), f.t, 1.8f, hyP, 0.f, packRGBA8(0.35f, 0.3f, 0.25f, 1), makeMat(MAT_DIRT), o3);
            }
            (void)z0;
        }
    }
}

// All street furniture of a cell: fixtures and utility poles of every candidate edge (reserved along the whole edge, so
// neighbouring cells agree), then the district dressing.
void dressCell(const RoadNetwork& net, const WorldMap& map, int cx, int cy, const std::vector<int>& cand, RoadCellOutput& out) {
    vec2 org = cellOrigin(cx, cy);
    vec3 o3(org, 0);
    Occupancy occ(org);
    for (const PropInstance& p : out.props) occ.add(p.pos.xy(), 0.8f);   // junction signals and signs
    struct EdgeData {
        int ei;
        Profile pf;
        std::vector<Fixture> fx;
    };
    std::vector<EdgeData> eds;
    eds.reserve(cand.size());
    std::vector<Pole> poles;
    const u32 cable = packRGBA8(0.06f, 0.06f, 0.06f, 1), rubber = makeMat(MAT_RUBBER);
    for (int ei : cand) {
        const RoadEdge& e = net.edges[ei];
        EdgeData ed;
        ed.ei = ei;
        ed.pf = profileFor(edgeRegion(e, map));
        bool pl = poleEdge(e, ed.pf);
        edgeFixtures(net, ei, map, pl, ed.fx);
        for (const Fixture& f : ed.fx) {
            if (f.r > 0.f) occ.add(f.pi.pos.xy(), f.r);
            if (!inCell(f.pi.pos.xy(), cx, cy)) continue;
            out.props.push_back(f.pi);
            for (int k = 0; k < f.nLights; k++) out.lights.push_back(f.li[k]);
        }
        if (pl) {
            poles.clear();
            edgePoles(net, ei, map, ed.fx, poles);
            for (size_t k = 0; k < poles.size(); k++) {
                const Pole& P = poles[k];
                if (!P.ok) continue;
                occ.add(P.pos.xy(), 0.7f);
                if (!inCell(P.pos.xy(), cx, cy)) continue;
                PropInstance pi;
                pi.pos = P.pos;
                pi.yaw = atan2f(P.x.y, P.x.x);
                pi.scale = 1.f;
                pi.type = PROP_POWER_POLE;
                pi.variant = P.variant;
                pi.flags = 0;
                out.props.push_back(pi);
                if (P.variant & 2) {
                    LightInstance li;
                    li.pos = P.pos + vec3(P.x * 1.95f, 7.5f);
                    li.dir = vec3(0, 0, -1);
                    li.cone = 0.2f;
                    li.type = 0;
                    li.color = vec3(1.0f, 0.7f, 0.4f) * 6500.f;
                    li.radius = 28.f;
                    out.lights.push_back(li);
                }
                // lines to the next pole: three conductors on the crossarm, one telecom cable lower on the field side
                size_t j = k + 1;
                if (j >= poles.size() || !poles[j].ok) continue;
                const Pole& Q = poles[j];
                float L = length(Q.pos.xy() - P.pos.xy());
                if (L > 60.f) continue;
                for (int c = -1; c <= 1; c++) {
                    vec3 a = P.pos + vec3(P.x * (1.1f * c), 9.5f), b = Q.pos + vec3(Q.x * (1.1f * c), 9.5f);
                    wire(out.street, a, b, 0.25f + 0.012f * L, 0.018f, cable, rubber, o3, 8);
                }
                vec3 a = P.pos + vec3(P.x * -0.22f, 7.2f), b = Q.pos + vec3(Q.x * -0.22f, 7.2f);
                wire(out.street, a, b, 0.35f + 0.02f * L, 0.03f, cable, rubber, o3, 8);
            }
        }
        eds.push_back(std::move(ed));
    }
    for (const EdgeData& ed : eds) dressEdge(net, ed.ei, map, cx, cy, ed.pf, ed.fx, occ, out);
}

}  // namespace street_dressing

// Road works query for the gameplay layer (parked-car spawns, traffic): true inside a works zone's closed parking strip.
bool roadWorkZoneAt(vec2 p) {
    if (!gRoads || !gMap) return false;
    float s = 0.f, d = 0.f, sideL = 0.f;
    int ei = gRoads->nearestEdge(p, 14.f, &s, &d, &sideL);
    if (ei < 0) return false;
    const RoadEdge& e = gRoads->edges[ei];
    int side = sideL > 0.f ? -1 : 1;   // nearestEdge reports +1 on the left
    street_dressing::Profile pf = street_dressing::profileFor(street_dressing::edgeRegion(e, *gMap));
    float sc = 0.f;
    if (!street_dressing::edgeWorkZone(*gRoads, ei, pf, side, &sc)) return false;
    return fabsf(s - sc) < 13.f && d < e.halfWidth + 0.5f;
}

void buildRoadCell(const RoadNetwork& net, const WorldMap& map, int cx, int cy, RoadCellOutput& out) {
    vec2 org = cellOrigin(cx, cy);
    std::vector<int> cand;
    net.edgesInRect(org - vec2(4.f), org + vec2(kCellSize + 4.f), cand);
    std::vector<Section> secs;
    const u32 white = 0xffffffffu;
    const u32 matAsphalt = makeMat(MAT_ASPHALT), matAsphaltOld = makeMat(MAT_ASPHALT_OLD);
    const u32 matSidewalk = makeMat(MAT_SIDEWALK), matCurb = makeMat(MAT_CURB), matConcrete = makeMat(MAT_CONCRETE);
    const u32 matWhite = makeMat(MAT_PAINT_WHITE), matYellow = makeMat(MAT_PAINT_YELLOW), matGrass = makeMat(MAT_GRASS);
    const u32 matDirt = makeMat(MAT_DIRT);

    for (int ei : cand) {
        const RoadEdge& e = net.edges[ei];
        const RoadClassInfo& ri = roadInfo(e.cls);
        buildSections(e, secs);
        if (secs.size() < 2) continue;
        bool unpaved = (e.flags & RF_UNPAVED) != 0;
        u32 surf = unpaved ? matDirt : ((e.seed & 3) == 0 ? matAsphaltOld : matAsphalt);
        float hw = e.halfWidth;
        float sw = e.sidewalk;
        bool hwy = e.cls == RC_HIGHWAY || e.cls == RC_RAMP;
        bool twoWay = e.lanesB > 0 && e.lanesF > 0;
        float lanesW = ri.laneWidth;
        for (size_t i = 0; i + 1 < secs.size(); i++) {
            const Section& a = secs[i];
            const Section& b = secs[i + 1];
            vec2 mid = (a.c.xy() + b.c.xy()) * 0.5f;
            if (!inCell(mid, cx, cy)) continue;
            vec3 o3(org, 0);
            float groundA = map.heightAt(a.c.x, a.c.y), groundB = map.heightAt(b.c.x, b.c.y);
            float wA = map.waterAt(a.c.x, a.c.y), wB = map.waterAt(b.c.x, b.c.y);
            float baseA = Max(groundA, wA > kNoWater + 1 ? wA - 3.f : groundA);
            float baseB = Max(groundB, wB > kNoWater + 1 ? wB - 3.f : groundB);
            bool deck = (a.c.z - groundA > 2.2f) || (b.c.z - groundB > 2.2f);
            // ---------- road surface
            {
                vec3 l0 = sectionPoint(a, -hw) - o3, r0 = sectionPoint(a, hw) - o3;
                vec3 l1 = sectionPoint(b, -hw) - o3, r1 = sectionPoint(b, hw) - o3;
                // uv: u across (meters from left), v along (meters); colour alpha = carriageway width / 64 m, so
                // the terrain shader can place the right-hand kerb's drains and gutter grime at u = 2 hw
                u32 surfCol = packRGBA8(1.f, 1.f, 1.f, Saturate(hw * (2.f / 64.f)));
                out.road.quadFacing(r0, r1, l1, l0, vec2(2 * hw, a.s), vec2(2 * hw, b.s), vec2(0, b.s), vec2(0, a.s), surfCol, surf, vec3(0, 0, 1));
            }
            // ---------- sides: sidewalks+curbs, or shoulders/skirts, or bridge barriers
            for (int side = -1; side <= 1; side += 2) {
                float sgn = (float)side;
                if (sw > 0.f && !(hwy)) {
                    // curb face
                    vec3 c0 = sectionPoint(a, sgn * hw) - o3, c1 = sectionPoint(b, sgn * hw) - o3;
                    vec3 up(0, 0, 0.15f);
                    out.road.quadFacing(c0, c1, c1 + up, c0 + up, vec2(a.s, 0), vec2(b.s, 0), vec2(b.s, 0.15f), vec2(a.s, 0.15f), white, matCurb, a.right * -sgn);
                    // sidewalk top
                    vec3 i0 = sectionPoint(a, sgn * hw, 0.15f) - o3, i1 = sectionPoint(b, sgn * hw, 0.15f) - o3;
                    vec3 x0 = sectionPoint(a, sgn * (hw + sw), 0.15f) - o3, x1 = sectionPoint(b, sgn * (hw + sw), 0.15f) - o3;
                    out.road.quadFacing(i0, x0, x1, i1, vec2(0, a.s), vec2(sw, a.s), vec2(sw, b.s), vec2(0, b.s), white, matSidewalk, vec3(0, 0, 1));
                    // outer skirt down into the terrain
                    vec3 s0 = x0 - vec3(0, 0, 0.9f), s1 = x1 - vec3(0, 0, 0.9f);
                    out.road.quadFacing(x0, x1, s1, s0, vec2(a.s, 0), vec2(b.s, 0), vec2(b.s, 1), vec2(a.s, 1), white, matConcrete, a.right * sgn);
                } else if (!deck) {
                    // shoulder skirt so the terrain never shows a gap
                    vec3 x0 = sectionPoint(a, sgn * hw) - o3, x1 = sectionPoint(b, sgn * hw) - o3;
                    vec3 s0 = sectionPoint(a, sgn * (hw + 1.2f), -0.6f) - o3, s1 = sectionPoint(b, sgn * (hw + 1.2f), -0.6f) - o3;
                    u32 m = unpaved ? matDirt : matDirt;
                    out.road.quadFacing(x0, s0, s1, x1, vec2(0, a.s), vec2(1.2f, a.s), vec2(1.2f, b.s), vec2(0, b.s), white, m, vec3(0, 0, 1));
                }
                if (!deck && !unpaved) {
                    // Falls beside embankments and approaches: steel W-beam guardrail (no sidewalk) or a pedestrian railing at
                    // the outer sidewalk edge, per sub-span where the ground drops more than 1 m below the road edge
                    float edgeLat = hw + (sw > 0.f && !hwy ? sw : 0.f);
                    float segLen = b.s - a.s;
                    int nsub = Max(1, (int)ceilf(segLen / 6.f));   // short sub-spans: a merge beside one end opens only its own
                    for (int q = 0; q < nsub; q++) {
                        float t0 = (float)q / nsub, t1 = (float)(q + 1) / nsub, tm = (t0 + t1) * 0.5f;
                        vec3 cm = lerp(a.c, b.c, tm);
                        vec3 rm = normalize(lerp(a.right, b.right, tm));
                        float drop = -1e9f;
                        for (int smp = 0; smp <= 4; smp++) {   // every ~3 m and 1.5-2.5 m out: a dip between samples still gets its rail
                            float ts = t0 + (t1 - t0) * (smp * 0.25f);
                            vec3 cs = lerp(a.c, b.c, ts);
                            for (float out2 = 1.5f; out2 <= 2.51f; out2 += 0.5f) {
                                vec3 outP = cs + rm * (sgn * (edgeLat + out2));
                                float dd = cs.z - map.heightAt(outP.x, outP.y);
                                float wl = map.waterAt(outP.x, outP.y);
                                if (wl > kNoWater + 1.f) dd = Max(dd, cs.z - wl + 0.5f);
                                drop = Max(drop, dd);
                            }
                        }
                        if (drop < 1.0f) continue;
                        // another road continues beside this edge here (merge, junction flare): leave it open
                        bool merge = false;
                        for (int smp = 0; smp < 3 && !merge; smp++) {
                            float ts = t0 + (t1 - t0) * (0.5f * smp);
                            vec3 railP = lerp(a.c, b.c, ts) + rm * (sgn * (edgeLat + 0.3f));
                            merge = net.onPavement(railP.xy(), railP.z - 0.1f, 0.f, ei, 1.1f);
                        }
                        if (merge) continue;
                        vec3 A0 = lerp(sectionPoint(a, sgn * (edgeLat + 0.3f)), sectionPoint(b, sgn * (edgeLat + 0.3f)), t0) - o3;
                        vec3 A1 = lerp(sectionPoint(a, sgn * (edgeLat + 0.3f)), sectionPoint(b, sgn * (edgeLat + 0.3f)), t1) - o3;
                        vec3 rs = a.right * sgn;
                        bool railing = sw > 0.f && !hwy;
                        float zb = railing ? 0.15f : 0.f;
                        vec3 lo = vec3(0, 0, zb + (railing ? 0.95f : 0.55f)), hi = vec3(0, 0, zb + (railing ? 1.05f : 0.87f));
                        // rail band facing the road and its back
                        out.road.quadFacing(A0 + lo, A1 + lo, A1 + hi, A0 + hi, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), colorGray(0.78f),
                                            makeMat(MAT_METAL_BRUSHED), -rs);
                        out.road.quadFacing(A1 + lo, A0 + lo, A0 + hi, A1 + hi, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), colorGray(0.7f),
                                            makeMat(MAT_METAL_BRUSHED), rs);
                        if (railing) {
                            vec3 m0 = vec3(0, 0, zb + 0.5f), m1 = vec3(0, 0, zb + 0.55f);
                            out.road.quadFacing(A0 + m0, A1 + m0, A1 + m1, A0 + m1, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), colorGray(0.7f),
                                                makeMat(MAT_METAL_PAINTED), -rs);
                        }
                        // posts
                        float spanLen = length(A1 - A0);
                        int np = Max(1, (int)(spanLen / (railing ? 2.5f : 4.f)));
                        vec3 fwd = normalize(A1 - A0);
                        for (int k = 0; k <= np; k++) {
                            if (k == np && q + 1 < nsub) continue;
                            vec3 pp = lerp(A0, A1, (float)k / np) + rs * 0.08f;
                            out.road.box(pp + vec3(0, 0, zb + (railing ? 0.53f : 0.4f) - 0.25f), fwd, rs, vec3(0, 0, 1),
                                         vec3(0.05f, 0.06f, (railing ? 0.53f : 0.45f) + 0.25f), colorGray(railing ? 0.55f : 0.62f), makeMat(MAT_METAL_PAINTED));
                        }
                        // collision along the mitred rail line (a box square to the centreline would leave a wedge open on the
                        // outside of every bend)
                        auto railAt = [&](float lat, float t) { return lerp(sectionPoint(a, sgn * lat), sectionPoint(b, sgn * lat), t) + vec3(0, 0, zb); };
                        roadRailCollisionQuad(out, railAt(edgeLat + 0.15f, t0), railAt(edgeLat + 0.15f, t1), railAt(edgeLat + 0.45f, t0),
                                              railAt(edgeLat + 0.45f, t1), railing ? 1.1f : 0.9f);
                    }
                }
                if (deck) {
                    // Barrier walls on the deck edges, in sub-spans; open where another road's pavement continues beside the deck
                    float bw = 0.35f, bh = 0.95f;
                    float lat = sgn * (hw + (sw > 0 && !hwy ? sw : 0.f) + bw * 0.5f);
                    int nsub = Max(1, (int)ceilf((b.s - a.s) / 5.f));
                    vec3 rAB = normalize(a.right + b.right);
                    for (int q = 0; q < nsub; q++) {
                        float t0 = (float)q / nsub, t1 = (float)(q + 1) / nsub;
                        bool merge = false;
                        for (int smp = 0; smp < 3 && !merge; smp++) {
                            vec3 cs = lerp(a.c, b.c, t0 + (t1 - t0) * (0.5f * smp));
                            vec3 railP = cs + rAB * (lat + sgn * 0.3f);
                            merge = net.onPavement(railP.xy(), cs.z - 0.1f, 0.f, ei, 1.1f);
                        }
                        if (merge) continue;
                        vec3 c0 = lerp(a.c, b.c, t0), c1 = lerp(a.c, b.c, t1);
                        float s0 = Lerp(a.s, b.s, t0), s1 = Lerp(a.s, b.s, t1);
                        vec3 p0 = lerp(sectionPoint(a, lat - bw * 0.5f), sectionPoint(b, lat - bw * 0.5f), t0) - o3;
                        vec3 p1 = lerp(sectionPoint(a, lat - bw * 0.5f), sectionPoint(b, lat - bw * 0.5f), t1) - o3;
                        vec3 q0 = lerp(sectionPoint(a, lat + bw * 0.5f), sectionPoint(b, lat + bw * 0.5f), t0) - o3;
                        vec3 q1 = lerp(sectionPoint(a, lat + bw * 0.5f), sectionPoint(b, lat + bw * 0.5f), t1) - o3;
                        roadRailCollisionQuad(out, p0 + o3, p1 + o3, q0 + o3, q1 + o3, bh);
                        (void)c0;
                        (void)c1;
                        vec3 up(0, 0, bh);
                        out.road.quadFacing(p0 + up, q0 + up, q1 + up, p1 + up, vec2(0, s0), vec2(bw, s0), vec2(bw, s1), vec2(0, s1), white, matConcrete, vec3(0, 0, 1));
                        out.road.quadFacing(q1, q0, q0 + up, q1 + up, vec2(s1, 0), vec2(s0, 0), vec2(s0, bh), vec2(s1, bh), white, matConcrete, a.right * sgn);
                        out.road.quadFacing(p0, p1, p1 + up, p0 + up, vec2(s0, 0), vec2(s1, 0), vec2(s1, bh), vec2(s0, bh), white, matConcrete, a.right * -sgn);
                    }
                    // Deck side face down to the slab bottom
                    float outerLat = sgn * (hw + (sw > 0 && !hwy ? sw : 0.f) + bw);
                    vec3 d0 = sectionPoint(a, outerLat) - o3, d1 = sectionPoint(b, outerLat) - o3;
                    vec3 dn(0, 0, -1.3f);
                    out.road.quadFacing(d0, d1, d1 + dn, d0 + dn, vec2(a.s, 0), vec2(b.s, 0), vec2(b.s, 1.3f), vec2(a.s, 1.3f), white, matConcrete, a.right * sgn);
                }
            }
            if (deck) {
                // Deck underside
                float outer = hw + (sw > 0 && !hwy ? sw : 0.f) + 0.35f;
                vec3 l0 = sectionPoint(a, -outer, -1.3f) - o3, r0 = sectionPoint(a, outer, -1.3f) - o3;
                vec3 l1 = sectionPoint(b, -outer, -1.3f) - o3, r1 = sectionPoint(b, outer, -1.3f) - o3;
                out.road.quadFacing(l0, l1, r1, r0, vec2(0, a.s), vec2(0, b.s), vec2(2 * outer, b.s), vec2(2 * outer, a.s), white, matConcrete, vec3(0, 0, -1));
                // Pillars every ~32 m
                float spacing = hwy ? 34.f : 28.f;
                int k0 = (int)ceilf(a.s / spacing), k1 = (int)floorf(b.s / spacing);
                for (int k = k0; k <= k1; k++) {
                    float s = k * spacing;
                    if (s < a.s || s >= b.s) continue;
                    float t = (s - a.s) / Max(b.s - a.s, 1e-4f);
                    vec3 c = lerp(a.c, b.c, t);
                    float g = Lerp(baseA, baseB, t);
                    if (c.z - g < 3.f) continue;
                    vec3 rv = normalize(lerp(a.right, b.right, t));
                    vec3 fw = normalize(cross(vec3(0, 0, 1), rv));
                    float top = c.z - 1.3f;
                    float h = top - g + 1.f;
                    if (hwy || outer > 7.f) {
                        // Hammerhead pier: column + cap beam
                        out.road.box(vec3(c.x, c.y, g - 1.f + h * 0.5f - 0.6f) - o3, rv, fw, vec3(0, 0, 1), vec3(1.1f, 1.1f, h * 0.5f - 0.6f), white, matConcrete);
                        out.road.box(vec3(c.x, c.y, top - 0.6f) - o3, rv, fw, vec3(0, 0, 1), vec3(outer * 0.85f, 1.0f, 0.6f), white, matConcrete, true);
                    } else {
                        for (int sgn = -1; sgn <= 1; sgn += 2) {
                            vec3 pc = c + rv * (sgn * outer * 0.55f);
                            out.road.cylinder(vec3(pc.x, pc.y, g - 1.f) - o3, 0.55f, 0.55f, h, 10, white, matConcrete, false);
                        }
                    }
                }
            }
            // ---------- Median for boulevards: raised planted strip
            if (ri.median > 0.f && !hwy && twoWay) {
                float mw = ri.median * 0.5f;
                vec3 up(0, 0, 0.18f);
                for (int side = -1; side <= 1; side += 2) {
                    vec3 c0 = sectionPoint(a, side * mw) - o3, c1 = sectionPoint(b, side * mw) - o3;
                    out.road.quadFacing(c0, c1, c1 + up, c0 + up, vec2(a.s, 0), vec2(b.s, 0), vec2(b.s, 0.18f), vec2(a.s, 0.18f), white, matCurb, a.right * (float)side);
                }
                vec3 l0 = sectionPoint(a, -mw, 0.18f) - o3, r0 = sectionPoint(a, mw, 0.18f) - o3;
                vec3 l1 = sectionPoint(b, -mw, 0.18f) - o3, r1 = sectionPoint(b, mw, 0.18f) - o3;
                out.road.quadFacing(r0, r1, l1, l0, vec2(2 * mw, a.s), vec2(2 * mw, b.s), vec2(0, b.s), vec2(0, a.s), white, matGrass, vec3(0, 0, 1));
            } else if (hwy && twoWay) {
                // Jersey barrier
                float bw = 0.3f, bh = 0.85f;
                vec3 p0 = sectionPoint(a, -bw) - o3, p1 = sectionPoint(b, -bw) - o3, q0 = sectionPoint(a, bw) - o3, q1 = sectionPoint(b, bw) - o3;
                vec3 up(0, 0, bh);
                out.road.quadFacing(p0 + up, q0 + up, q1 + up, p1 + up, vec2(0, a.s), vec2(0.6f, a.s), vec2(0.6f, b.s), vec2(0, b.s), white, matConcrete, vec3(0, 0, 1));
                out.road.quadFacing(q1, q0, q0 + up, q1 + up, vec2(b.s, 0), vec2(a.s, 0), vec2(a.s, bh), vec2(b.s, bh), white, matConcrete, a.right);
                out.road.quadFacing(p0, p1, p1 + up, p0 + up, vec2(a.s, 0), vec2(b.s, 0), vec2(b.s, bh), vec2(a.s, bh), white, matConcrete, a.right * -1.f);
                roadRailCollision(out, a.c, b.c, normalize(a.right + b.right), -bw, bw, bh);
            }
            // ---------- Lane markings
            if (!unpaved && e.cls != RC_LANE) {
                float medianHalf = (ri.median > 0.f && twoWay) ? ri.median * 0.5f : 0.f;
                float laneStart = medianHalf;  // lanes start next to the median / center line
                if (!twoWay) laneStart = -lanesW * e.lanesF * 0.5f;
                // Center line (two-way without median)
                if (twoWay && medianHalf == 0.f && !hwy) {
                    if (e.cls == RC_STREET) {
                        // dashed yellow
                        for (float s = floorf(a.s / 12.f) * 12.f; s < b.s; s += 12.f) stripe(out.decals, a, b, 0.f, 0.12f, s, s + 4.f, matYellow, org);
                    } else {
                        stripe(out.decals, a, b, -0.12f, 0.12f, a.s, b.s, matYellow, org);
                        stripe(out.decals, a, b, 0.12f, 0.12f, a.s, b.s, matYellow, org);
                    }
                } else if (hwy && twoWay) {
                    stripe(out.decals, a, b, -0.5f, 0.15f, a.s, b.s, matYellow, org);
                    stripe(out.decals, a, b, 0.5f, 0.15f, a.s, b.s, matYellow, org);
                    laneStart = 0.6f;
                }
                // Lane dividers
                for (int dir = -1; dir <= 1; dir += 2) {
                    int lanes = dir > 0 ? e.lanesF : e.lanesB;
                    if (!twoWay) { lanes = e.lanesF; if (dir < 0) continue; }
                    for (int l = 1; l < lanes; l++) {
                        float lat = twoWay ? dir * (laneStart + l * lanesW) : laneStart + l * lanesW;
                        for (float s = floorf(a.s / 12.f) * 12.f; s < b.s; s += 12.f) stripe(out.decals, a, b, lat, 0.13f, s, s + 3.f, matWhite, org);
                    }
                    // edge line
                    if (hwy || e.cls == RC_RURAL || e.cls == RC_BOULEVARD) {
                        float lat = twoWay ? dir * (laneStart + lanes * lanesW + 0.15f) : laneStart + lanes * lanesW + 0.15f;
                        stripe(out.decals, a, b, lat, 0.15f, a.s, b.s, matWhite, org);
                        if (!twoWay) stripe(out.decals, a, b, laneStart - 0.15f, 0.15f, a.s, b.s, matYellow, org);
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- Intersections
    // Iterate nodes near the cell via incident edges of candidate edges
    std::vector<int> nodeCand;
    for (int ei : cand) {
        nodeCand.push_back(net.edges[ei].n0);
        nodeCand.push_back(net.edges[ei].n1);
    }
    std::sort(nodeCand.begin(), nodeCand.end());
    nodeCand.erase(std::unique(nodeCand.begin(), nodeCand.end()), nodeCand.end());
    for (int ni : nodeCand) {
        const RoadNode& nd = net.nodes[ni];
        if (!inCell(nd.p, cx, cy)) continue;
        int deg = (int)nd.edges.size();
        if (deg == 1) {
            // Dead end: turning bulb (lanes, streets, rural roads)
            const RoadEdge& e = net.edges[nd.edges[0]];
            float r = net.bulbRadius(nd);
            if (r > 0.f) {
                vec3 c(nd.p, nd.z);
                std::vector<vec3> ring;
                for (int k = 0; k < 20; k++) {
                    float ang = kTwoPi * k / 20.f;
                    ring.push_back(c + vec3(cosf(ang) * r, sinf(ang) * r, 0.01f) - vec3(org, 0));
                }
                out.road.polygon(ring, vec3(0, 0, 1), white, matAsphalt, 1.f);
                if (e.sidewalk > 0) {
                    for (int k = 0; k < 20; k++) {
                        float a0 = kTwoPi * k / 20.f, a1 = kTwoPi * (k + 1) / 20.f;
                        vec3 i0 = c + vec3(cosf(a0) * r, sinf(a0) * r, 0.15f) - vec3(org, 0), i1 = c + vec3(cosf(a1) * r, sinf(a1) * r, 0.15f) - vec3(org, 0);
                        vec3 x0 = c + vec3(cosf(a0) * (r + e.sidewalk), sinf(a0) * (r + e.sidewalk), 0.15f) - vec3(org, 0);
                        vec3 x1 = c + vec3(cosf(a1) * (r + e.sidewalk), sinf(a1) * (r + e.sidewalk), 0.15f) - vec3(org, 0);
                        out.road.quadFacing(i0, x0, x1, i1, vec2(0, 0), vec2(e.sidewalk, 0), vec2(e.sidewalk, 1), vec2(0, 1), white, matSidewalk, vec3(0, 0, 1));
                        vec3 inward = normalize(vec3(c.xy() - (i0 + vec3(org, 0)).xy(), 0));
                        out.road.quadFacing(i1 - vec3(0, 0, 0.15f), i0 - vec3(0, 0, 0.15f), i0, i1, vec2(0, 0), vec2(1, 0), vec2(1, 0.15f), vec2(0, 0.15f), white, matCurb, inward);
                    }
                }
            }
            continue;
        }
        if (nd.radius <= 0.f || deg < 3) continue;
        // Gather approach geometry
        struct Approach {
            float ang;
            vec2 dir;
            vec3 cutPt;
            float hw, sw;
            int edge;
            bool outgoing;  // edge starts at this node
        };
        std::vector<Approach> ap;
        for (int ei : nd.edges) {
            const RoadEdge& e = net.edges[ei];
            bool out0 = e.n0 == ni;
            float s = out0 ? e.cut0 : e.length - e.cut1;
            vec3 p = e.posAt(s);
            vec2 d = normalize(p.xy() - nd.p);
            if (length2(p.xy() - nd.p) < 1e-4f) d = normalize((out0 ? e.pts[1] : e.pts[e.pts.size() - 2]).xy() - nd.p);
            Approach A;
            A.ang = atan2f(d.y, d.x);
            A.dir = d;
            A.cutPt = p;
            A.hw = e.halfWidth;
            A.sw = e.sidewalk;
            A.edge = ei;
            A.outgoing = out0;
            ap.push_back(A);
        }
        std::sort(ap.begin(), ap.end(), [](const Approach& x, const Approach& y) { return x.ang < y.ang; });
        int n = (int)ap.size();
        std::vector<vec3> poly;
        std::vector<std::pair<std::vector<vec3>, std::vector<vec3>>> corners;  // inner curve, outer curve (for sidewalks)
        vec3 o3(org, 0);
        for (int i = 0; i < n; i++) {
            const Approach& A = ap[i];
            const Approach& B = ap[(i + 1) % n];
            vec2 lA = perp(A.dir), lB = perp(B.dir);
            // approach i: right point then left point (CCW around the node)
            vec3 Ar = A.cutPt + vec3(-lA * A.hw, 0.f), Al = A.cutPt + vec3(lA * A.hw, 0.f);
            vec3 Br = B.cutPt + vec3(-lB * B.hw, 0.f);
            poly.push_back(Ar);
            poly.push_back(Al);
            // Corner curve from Al to Br via the intersection of the two road edge lines
            vec2 p1 = Al.xy(), d1 = A.dir, p2 = Br.xy(), d2 = B.dir;
            float den = cross(d1, d2);
            vec2 C = (p1 + p2) * 0.5f;
            bool curved = false;
            if (fabsf(den) > 0.15f) {
                float t = cross(p2 - p1, d2) / den;
                vec2 X = p1 + d1 * t;
                if (length(X - nd.p) < nd.radius * 2.5f) { C = X; curved = true; }
            }
            std::vector<vec3> inner, outer;
            int segs = curved ? 6 : 1;
            float swc = Min(A.sw, B.sw);
            for (int k = 1; k < segs; k++) {
                float t = (float)k / segs;
                vec2 q = p1 * ((1 - t) * (1 - t)) + C * (2 * (1 - t) * t) + p2 * (t * t);
                float z = Lerp(Al.z, Br.z, t);
                inner.push_back(vec3(q, z));
            }
            for (auto& q : inner) poly.push_back(q);
            if (swc > 0.f) {
                // outer sidewalk curve offset by sidewalk width
                vec2 po1 = p1 + lA * swc, po2 = p2 - lB * swc;
                vec2 Co = C;
                if (curved) {
                    float den2 = cross(d1, d2);
                    float t2 = cross(po2 - po1, d2) / den2;
                    Co = po1 + d1 * t2;
                } else Co = (po1 + po2) * 0.5f;
                std::vector<vec3> in2, out2;
                in2.push_back(Al);
                for (auto& q : inner) in2.push_back(q);
                in2.push_back(Br);
                for (int k = 0; k <= segs; k++) {
                    float t = (float)k / segs;
                    vec2 q = po1 * ((1 - t) * (1 - t)) + Co * (2 * (1 - t) * t) + po2 * (t * t);
                    out2.push_back(vec3(q, Lerp(Al.z, Br.z, t)));
                }
                corners.push_back({in2, out2});
            }
        }
        for (auto& p : poly) p = p - o3 + vec3(0, 0, 0.005f);
        out.road.polygon(poly, vec3(0, 0, 1), white, matAsphalt, 1.f);
        // Where only the expressway and its ramps meet (a ramp merging or leaving), the median barrier and the yellow lines
        // beside it run straight on through the node: no gap, no blunt barrier end facing the traffic
        {
            int hA = -1, hB = -1;
            bool hwyOnly = true;
            for (int i = 0; i < n; i++) {
                const RoadEdge& e = net.edges[ap[i].edge];
                if (e.cls == RC_HIGHWAY && e.lanesF > 0 && e.lanesB > 0) {
                    if (hA < 0) hA = i;
                    else if (hB < 0) hB = i;
                    else hwyOnly = false;
                } else if (e.cls != RC_RAMP) hwyOnly = false;
            }
            if (hwyOnly && hB >= 0 && dot(ap[hA].dir, ap[hB].dir) < -0.9f) {
                const float bw = 0.3f, bh = 0.85f;
                vec3 mid(nd.p, (ap[hA].cutPt.z + ap[hB].cutPt.z) * 0.5f);
                const vec3 run[3] = {ap[hA].cutPt, mid, ap[hB].cutPt};
                for (int k = 0; k < 2; k++) {
                    vec3 a3 = run[k], b3 = run[k + 1];
                    vec2 d = b3.xy() - a3.xy();
                    float L = length(d);
                    if (L < 0.05f) continue;
                    d = d / L;
                    vec3 r(d.y, -d.x, 0.f);
                    vec3 p0 = a3 - r * bw - o3, p1 = b3 - r * bw - o3, q0 = a3 + r * bw - o3, q1 = b3 + r * bw - o3, up(0, 0, bh);
                    out.road.quadFacing(p0 + up, q0 + up, q1 + up, p1 + up, vec2(0, 0), vec2(0.6f, 0), vec2(0.6f, L), vec2(0, L), white, matConcrete, vec3(0, 0, 1));
                    out.road.quadFacing(q1, q0, q0 + up, q1 + up, vec2(L, 0), vec2(0, 0), vec2(0, bh), vec2(L, bh), white, matConcrete, r);
                    out.road.quadFacing(p0, p1, p1 + up, p0 + up, vec2(0, 0), vec2(L, 0), vec2(L, bh), vec2(0, bh), white, matConcrete, r * -1.f);
                    roadRailCollision(out, a3, b3, r, -bw, bw, bh);
                    for (int s = -1; s <= 1; s += 2) {
                        vec3 l0 = a3 + r * (s * 0.5f - 0.075f) - o3 + vec3(0, 0, 0.025f), l1 = a3 + r * (s * 0.5f + 0.075f) - o3 + vec3(0, 0, 0.025f);
                        vec3 m1 = b3 + r * (s * 0.5f + 0.075f) - o3 + vec3(0, 0, 0.025f), m0 = b3 + r * (s * 0.5f - 0.075f) - o3 + vec3(0, 0, 0.025f);
                        out.decals.quadFacing(l0, l1, m1, m0, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), white, matYellow, vec3(0, 0, 1));
                    }
                }
            }
        }
        // Sidewalk corners with curb faces
        for (auto& cr : corners) {
            auto& in2 = cr.first;
            auto& out2 = cr.second;
            size_t m = Min(in2.size(), out2.size());
            for (size_t k = 0; k + 1 < m; k++) {
                vec3 i0 = in2[k] - o3 + vec3(0, 0, 0.15f), i1 = in2[k + 1] - o3 + vec3(0, 0, 0.15f);
                vec3 x0 = out2[k] - o3 + vec3(0, 0, 0.15f), x1 = out2[k + 1] - o3 + vec3(0, 0, 0.15f);
                out.road.quadFacing(i0, x0, x1, i1, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), white, matSidewalk, vec3(0, 0, 1));
                vec3 dn(0, 0, -0.15f);
                vec3 toRoad = normalize(vec3((i0 - x0).xy() + (i1 - x1).xy(), 0));
                out.road.quadFacing(i1 + dn, i0 + dn, i0, i1, vec2(0, 0), vec2(1, 0), vec2(1, 0.15f), vec2(0, 0.15f), white, matCurb, toRoad);
                out.road.quadFacing(x0, x1, x1 + vec3(0, 0, -0.9f), x0 + vec3(0, 0, -0.9f), vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), white, matConcrete, toRoad * -1.f);
            }
        }
        // Crosswalks + stop lines at controlled intersections
        if (nd.control >= 1) {
            for (const Approach& A : ap) {
                const RoadEdge& e = net.edges[A.edge];
                if (e.cls == RC_HIGHWAY || e.cls == RC_RAMP) continue;
                vec2 l = perp(A.dir);
                float w = A.hw;
                // zebra: stripes 0.5 m wide, along the approach direction, 3 m long
                for (float x = -w + 0.6f; x < w - 0.3f; x += 1.1f) {
                    vec3 c0 = A.cutPt + vec3(l * x + A.dir * 0.8f, 0.03f) - o3;
                    vec3 c1 = A.cutPt + vec3(l * (x + 0.55f) + A.dir * 0.8f, 0.03f) - o3;
                    vec3 c2 = c1 + vec3(A.dir * 3.f, 0), c3 = c0 + vec3(A.dir * 3.f, 0);
                    out.decals.quadFacing(c0, c1, c2, c3, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), white, matWhite, vec3(0, 0, 1));
                }
                // stop line on the incoming half (right side when driving toward the node)
                vec3 s0 = A.cutPt + vec3(A.dir * 4.4f, 0.03f) - o3;
                vec3 s1 = s0 + vec3(A.dir * 0.45f, 0);
                // incoming lanes are on the -l side of the outward direction
                vec3 a0 = s0 + vec3(l * 0.2f, 0), a1 = s0 + vec3(l * (-(w - 0.3f)), 0);
                vec3 b0 = s1 + vec3(l * 0.2f, 0), b1 = s1 + vec3(l * (-(w - 0.3f)), 0);
                out.decals.quadFacing(a0, a1, b1, b0, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), white, matWhite, vec3(0, 0, 1));
            }
            // Span-wire signals outside the downtown cores (South Florida style); mast arms and stop signs otherwise
            bool spanDone = false;
            if (nd.control == 2 && out.detail && street_dressing::profileFor(map.regionAt(nd.p.x, nd.p.y)).span) {
                std::vector<street_dressing::SpanApproach> sa;
                for (const Approach& A : ap) sa.push_back({A.dir, A.cutPt, A.hw, A.sw, A.edge, A.outgoing, nd.radius});
                spanDone = street_dressing::spanWireSignals(net, nd, sa, map, out, org);
            }
            // Traffic lights / stop signs at corners
            for (int i = 0; i < n && !spanDone; i++) {
                const Approach& A = ap[i];
                const RoadEdge& e = net.edges[A.edge];
                if (e.cls == RC_HIGHWAY || e.cls == RC_RAMP) continue;
                vec2 l = perp(A.dir);
                PropInstance pi;
                // right side of the incoming traffic (which drives toward the node on the lane at -l side)
                vec2 pos = A.cutPt.xy() + A.dir * 1.5f - l * (A.hw + Max(A.sw * 0.5f, 0.8f));
                pi.pos = vec3(pos, A.cutPt.z + (A.sw > 0 ? 0.15f : 0.f));
                pi.yaw = atan2f(-A.dir.y, -A.dir.x);
                pi.scale = 1.f;
                pi.type = nd.control == 2 ? PROP_TRAFFIC_LIGHT : PROP_STOP_SIGN;
                pi.variant = (u8)(e.cls <= RC_AVENUE ? 1 : 0);
                pi.flags = (u16)A.edge;
                if (net.onPavement(pi.pos.xy(), pi.pos.z, 0.2f)) continue;
                out.props.push_back(pi);
            }
        }
    }
    // ---------------------------------------------------------------- Street furniture, trees, utility lines (LOD0)
    // (wider candidate set: a boulevard just outside the cell still has its outer sidewalk inside it)
    if (out.detail) {
        std::vector<int> dcand;
        net.edgesInRect(org - vec2(24.f), org + vec2(kCellSize + 24.f), dcand);
        street_dressing::dressCell(net, map, cx, cy, dcand, out);
    }
}

}  // namespace World
