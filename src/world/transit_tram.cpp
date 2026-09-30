// Sol Beach Streetcar layout (world generation): the loop's directed edges (right turns only), the track along the
// traffic's curb lanes and junction connectors (the construction of game/lanes.cpp: lane offsets, mitered edge normals,
// junction cut-backs, cubic Bezier connectors), stops on the curb side, overhead line poles and wire support points, and
// the site elements that build track, wire and stops per streaming cell (transitmesh_tram.cpp). Included by transit.cpp.

namespace World {

vec3 TramLine::at(float s) const {
    int n = (int)p.size();
    if (n == 0) return vec3(0.f);
    if (n == 1) return p[0];
    float f = wrap(s) / ds;
    int i = (int)f;
    float t = f - (float)i;
    i = i % n;
    return lerp(p[i], p[(i + 1) % n], t);
}

vec2 TramLine::dirAt(float s) const {
    int n = (int)t.size();
    if (n == 0) return vec2(0, 1);
    float f = wrap(s) / ds;
    int i = (int)f;
    float u = f - (float)i;
    i = i % n;
    vec2 d = t[i] * (1.f - u) + t[(i + 1) % n] * u;
    return length2(d) > 1e-8f ? normalize(d) : t[i];
}

namespace transit_tram {

using transit_bus::edgePos;
using transit_bus::edgeDir;
using transit_bus::endNode;

struct Via {
    vec2 p;
    vec2 heading;
    const char* road;
};

// Clockwise loop (right turns only): south on Collins-Solano Avenue, west on 17th Street past the ferry terminal,
// north on Bay Road along the bay, east on 47th Street (a block short of the Venetia Causeway landing, whose junction
// is too cramped for a tram)
const Via kVia[] = {
    {{5206.f, 1560.f}, {0.f, -1.f}, "Collins-Solano Avenue"},
    {{5206.f, 250.f}, {0.f, -1.f}, "Collins-Solano Avenue"},
    {{5206.f, -1160.f}, {0.f, -1.f}, "Collins-Solano Avenue"},
    {{5080.f, -1220.f}, {-1.f, 0.f}, "17th Street"},
    {{4946.f, -1160.f}, {0.f, 1.f}, "Bay Road"},
    {{4946.f, 250.f}, {0.f, 1.f}, "Bay Road"},
    {{4936.f, 1560.f}, {0.f, 1.f}, "Bay Road"},
    {{5080.f, 1630.f}, {1.f, 0.f}, "47th Street"},
};
constexpr u32 kLineRgb = 0xFF6A4D;       // streetcar coral
constexpr float kStopSpacing = 400.f;
constexpr float kFrontMin = 30.f;        // a stopped streetcar's front this far past the lane start (rear clear of the junction)
constexpr float kFrontEndGap = 11.f;     // ... and this far before the lane end (clear of the next crosswalk)
constexpr float kPoleSpacing = 30.f;

// ---------------------------------------------------------------------------------------------------------------------
// Lane geometry, mirroring game/lanes.cpp
struct LaneGeo {
    const RoadNetwork& net;
    std::vector<std::vector<vec2>> nrm;
    std::vector<char> have;
    std::vector<float> tmp;
    explicit LaneGeo(const RoadNetwork& n) : net(n), nrm(n.edges.size()), have(n.edges.size(), 0) {}

    // mitered right normals of the n0 -> n1 polyline
    const std::vector<vec2>& normals(int ei) {
        if (have[ei]) return nrm[ei];
        have[ei] = 1;
        const RoadEdge& e = net.edges[ei];
        size_t n = e.pts.size();
        std::vector<vec2>& vn = nrm[ei];
        vn.assign(n, vec2(0.f));
        if (n < 2) return vn;
        for (size_t k = 0; k < n; k++) {
            vec2 tPrev = k > 0 ? e.pts[k].xy() - e.pts[k - 1].xy() : vec2(0, 0);
            vec2 tNext = k + 1 < n ? e.pts[k + 1].xy() - e.pts[k].xy() : vec2(0, 0);
            float lp = length(tPrev), ln = length(tNext);
            vec2 a = lp > 1e-5f ? tPrev / lp : (ln > 1e-5f ? tNext / ln : vec2(1, 0));
            vec2 b = ln > 1e-5f ? tNext / ln : a;
            vec2 th = normalize(a + b);
            float miter = 1.f / Max(0.5f, dot(th, b));
            vn[k] = vec2(th.y, -th.x) * miter;
        }
        return vn;
    }
    // lane center offsets of one direction (relative to the n0 -> n1 right normal), index 0 = next to the center line
    static void offsets(const RoadEdge& e, int dir, std::vector<float>& offs) {
        const RoadClassInfo& ri = roadInfo(e.cls);
        offs.clear();
        int cnt = dir > 0 ? e.lanesF : e.lanesB;
        bool twoWay = e.lanesF > 0 && e.lanesB > 0;
        float W = ri.laneWidth;
        for (int k = 0; k < cnt; k++) {
            float off;
            if (twoWay) {
                float laneStart = ri.median > 0.f ? ri.median * 0.5f : 0.f;
                off = (float)dir * (laneStart + (k + 0.5f) * W);
            } else {
                off = -W * cnt * 0.5f + (k + 0.5f) * W;
                if (dir < 0) off = -off;
            }
            offs.push_back(off);
        }
    }
    float curbOffset(const RoadEdge& e, int dir) {
        offsets(e, dir, tmp);
        return tmp.empty() ? 0.f : tmp.back();
    }
    // lane end cut-backs at the n0 / n1 ends
    void cuts(int ei, float& c0, float& c1) {
        const RoadEdge& e = net.edges[ei];
        float c[2] = {0.f, 0.f};
        for (int end = 0; end < 2; end++) {
            int ni = end == 0 ? e.n0 : e.n1;
            const RoadNode& rn = net.nodes[ni];
            float cut = end == 0 ? e.cut0 : e.cut1;
            int deg = (int)rn.edges.size();
            if (deg >= 3 && rn.radius > 0.f) {
                c[end] = cut;
            } else if (deg == 1) {
                c[end] = Min(9.f, e.length * 0.3f);
            } else if (deg == 2) {
                int other = rn.edges[0] == ei ? rn.edges[1] : rn.edges[0];
                const RoadEdge& o = net.edges[other];
                std::vector<float> a, b;
                offsets(e, 1, a);
                offsets(o, 1, b);
                float shift = 0.f;
                size_t m = Max(a.size(), b.size());
                for (size_t k = 0; k < m && !a.empty() && !b.empty(); k++)
                    shift = Max(shift, fabsf(fabsf(a[Min(k, a.size() - 1)]) - fabsf(b[Min(k, b.size() - 1)])));
                bool sameLayout = e.lanesF == o.lanesF && e.lanesB == o.lanesB && shift < 0.3f;
                vec2 da = normalize((end == 0 ? e.pts[1] : e.pts[e.pts.size() - 2]).xy() - rn.p);
                vec2 db = normalize((o.n0 == ni ? o.pts[1] : o.pts[o.pts.size() - 2]).xy() - rn.p);
                float bend = 1.f + dot(da, db);
                float cc = sameLayout ? 1.5f + bend * 10.f : 4.f + shift * 2.5f + bend * 10.f;
                c[end] = Min(cc, e.length * 0.3f);
            }
        }
        float total = c[0] + c[1];
        if (total > e.length - 1.f && total > 0.f) {
            float k = Max(0.f, e.length - 1.f) / total;
            c[0] *= k;
            c[1] *= k;
        }
        c0 = c[0];
        c1 = c[1];
    }
    // curb lane range in travel coordinates
    void laneRange(int ei, int dir, float& u0, float& u1) {
        float c0, c1;
        cuts(ei, c0, c1);
        const RoadEdge& e = net.edges[ei];
        float cs = dir > 0 ? c0 : c1, ce = dir > 0 ? c1 : c0;
        u0 = cs;
        u1 = Max(e.length - ce, cs + 0.5f);
    }
    // curb lane center at travel coordinate u (plus `extra` metres further right)
    vec3 pos(int ei, int dir, float u, float extra = 0.f) {
        const RoadEdge& e = net.edges[ei];
        const std::vector<vec2>& vn = normals(ei);
        float off = curbOffset(e, dir) + extra * (float)dir;
        float s = Clamp(dir > 0 ? u : e.length - u, 0.f, e.length);
        size_t n = e.pts.size();
        size_t i = std::upper_bound(e.dist.begin(), e.dist.end(), s) - e.dist.begin();
        if (i == 0) i = 1;
        if (i >= n) i = n - 1;
        float seg = e.dist[i] - e.dist[i - 1];
        float t = seg > 1e-5f ? (s - e.dist[i - 1]) / seg : 0.f;
        vec3 c = lerp(e.pts[i - 1], e.pts[i], t);
        vec2 nr = lerp(vn[i - 1], vn[i], t);
        return c + vec3(nr * off, 0.f);
    }
    vec2 tangent(int ei, int dir, float u) {
        const RoadEdge& e = net.edges[ei];
        float s = Clamp(dir > 0 ? u : e.length - u, 0.f, e.length);
        size_t n = e.pts.size();
        size_t i = std::upper_bound(e.dist.begin(), e.dist.end(), s) - e.dist.begin();
        if (i == 0) i = 1;
        if (i >= n) i = n - 1;
        vec2 d = normalize(e.pts[i].xy() - e.pts[i - 1].xy());
        float seg = e.dist[i] - e.dist[i - 1];
        float t = seg > 1e-5f ? (s - e.dist[i - 1]) / seg : 0.f;
        if (t > 0.7f && i + 1 < n) d = normalize(lerp(d, normalize(e.pts[i + 1].xy() - e.pts[i].xy()), (t - 0.7f) / 0.6f));
        else if (t < 0.3f && i >= 2) d = normalize(lerp(d, normalize(e.pts[i - 1].xy() - e.pts[i - 2].xy()), (0.3f - t) / 0.6f));
        return dir > 0 ? d : -d;
    }
};

// Junction connector between two lane ends (cubic Bezier as in lanes.cpp; z blends linearly)
void connector(vec3 p0, vec2 t0, vec3 p3, vec2 t3, std::vector<vec3>& out) {
    float D = length(p3.xy() - p0.xy());
    vec3 p1, p2;
    float den = cross(t0, t3);
    bool arc = false;
    if (fabsf(den) > 0.15f) {
        vec2 w = p3.xy() - p0.xy();
        float a = cross(w, t3) / den;
        float b = cross(w, t0) / den;
        if (a > 0.15f * D && b > 0.15f * D && a < 2.5f * D && b < 2.5f * D) {
            p1 = p0 + vec3(t0 * (a * 0.56f), 0.f);
            p2 = p3 - vec3(t3 * (b * 0.56f), 0.f);
            arc = true;
        }
    }
    if (!arc) {
        float k = Max(D * 0.38f, 0.5f);
        p1 = p0 + vec3(t0 * k, 0.f);
        p2 = p3 - vec3(t3 * k, 0.f);
    }
    float approx = length(p1 - p0) + length(p2 - p1) + length(p3 - p2);
    int n = Clamp((int)(approx / 1.0f), 6, 48);
    out.clear();
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n, u = 1.f - t;
        vec3 p = p0 * (u * u * u) + p1 * (3.f * u * u * t) + p2 * (3.f * u * t * t) + p3 * (t * t * t);
        p.z = Lerp(p0.z, p3.z, t);
        out.push_back(p);
    }
}

int classRank(RoadClass c) {
    switch (c) {
        case RC_HIGHWAY: return 6;
        case RC_BOULEVARD: return 5;
        case RC_AVENUE: return 4;
        case RC_STREET: return 3;
        case RC_RURAL: return 2;
        default: return 1;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
struct Builder {
    const RoadNetwork& net;
    WorldMap& map;
    SiteSet& S;
    TransitNet& N;
    LaneGeo geo;
    std::vector<vec2> avoid;
    std::vector<float> avoidR;
    std::vector<transit_bus::Slot> slots, lamps;
    Builder(const RoadNetwork& n, WorldMap& m, SiteSet& s, TransitNet& t) : net(n), map(m), S(s), N(t), geo(n) {}

    // ---- the loop's directed edges
    bool chain(std::vector<RouteLeg>& out) {
        size_t nv = ARRAY_COUNT(kVia);
        std::vector<int> ve(nv), vd(nv);
        std::vector<float> vu(nv);
        for (size_t i = 0; i < nv; i++)
            if (!transit_bus::snapVia(net, kVia[i].p, kVia[i].heading, kVia[i].road, ve[i], vd[i], vu[i])) {
                LOG("Transit: streetcar waypoint %zu (%.0f, %.0f) on %s not found", i, kVia[i].p.x, kVia[i].p.y, kVia[i].road);
                return false;
            }
        std::vector<const char*> prefer;
        for (size_t i = 0; i < nv; i++) prefer.push_back(kVia[i].road);
        out.clear();
        for (size_t i = 0; i < nv; i++) {
            size_t j = (i + 1) % nv;
            std::vector<RouteLeg> part;
            if (ve[i] == ve[j] && vd[i] == vd[j] && vu[j] > vu[i]) {
                RouteLeg l;
                l.edge = ve[i];
                l.dir = vd[i];
                part.push_back(l);
            } else if (!transit_bus::legPath(net, ve[i], vd[i], ve[j], vd[j], prefer, part)) {
                LOG("Transit: streetcar: no path from waypoint %zu to %zu", i, j);
                return false;
            }
            for (const RouteLeg& l : part) {
                if (!out.empty() && out.back().edge == l.edge && out.back().dir == l.dir) continue;
                out.push_back(l);
            }
        }
        if (out.size() > 1 && out.back().edge == out.front().edge && out.back().dir == out.front().dir) out.pop_back();
        // right turns and straight on only; plain streets with sidewalks, no decks
        for (size_t i = 0; i < out.size(); i++) {
            const RoadEdge& e = net.edges[out[i].edge];
            if ((e.flags & (RF_BRIDGE | RF_ELEVATED | RF_UNPAVED)) || e.pts.size() < 2) {
                LOG("Transit: streetcar: edge %d (%s) is not a street the track can run on", out[i].edge, e.name.c_str());
                return false;
            }
            const RouteLeg& b = out[(i + 1) % out.size()];
            if (endNode(e, out[i].dir) != (b.dir > 0 ? net.edges[b.edge].n0 : net.edges[b.edge].n1)) {
                LOG("Transit: streetcar: legs %zu and %zu do not meet", i, (i + 1) % out.size());
                return false;
            }
            vec2 tIn = edgeDir(e, out[i].dir, e.length - 1.f), tOut = edgeDir(net.edges[b.edge], b.dir, 1.f);
            if (cross(tIn, tOut) > 0.35f || dot(tIn, tOut) < -0.6f) {
                vec3 at = edgePos(e, out[i].dir, e.length);
                LOG("Transit: streetcar: left turn or reversal from %s onto %s at (%.0f, %.0f): in (%.2f, %.2f) out (%.2f, %.2f)", e.name.c_str(),
                    net.edges[b.edge].name.c_str(), at.x, at.y, tIn.x, tIn.y, tOut.x, tOut.y);
                return false;
            }
        }
        return true;
    }

    // ---- sidewalk checks
    bool furnitureNear(const RoadEdge& e, int dir, float u, float reach, float lampReach) {
        float s = dir > 0 ? u : e.length - u;
        transit_bus::furnitureSlots(net, e, slots);
        for (const transit_bus::Slot& sl : slots)
            if (sl.side == dir && fabsf(sl.s - s) < (sl.shelter ? reach + 3.f : reach)) return true;
        transit_bus::lampSlots(e, lamps);
        for (const transit_bus::Slot& l : lamps)
            if (l.side == dir && fabsf(l.s - s) < lampReach) return true;
        return false;
    }
    bool avoided(vec2 p, float r) const {
        for (size_t k = 0; k < avoid.size(); k++)
            if (length(p - avoid[k]) < avoidR[k] + r) return true;
        return false;
    }
    bool sidewalkFree(vec2 p, float z, float r) const {
        if (net.onPavement(p, z, r, -1)) return false;
        if (gBuildings && gBuildings->pointInBuilding(p, r)) return false;
        const Pad* pd = S.padAt(p);
        if (pd && pd->kind != PAD_PLAZA) return false;
        return !avoided(p, r);
    }

    // ---- stop name: the cross street just passed (causeways and the ferry terminal by name)
    std::string stopName(const RouteLeg& l, vec2 pos) {
        for (const FerryPier& fp : N.piers)
            if (length(fp.base - pos) < 260.f) return "Ferry Terminal";
        const RoadEdge& e = net.edges[l.edge];
        int nodes[2] = {l.dir > 0 ? e.n0 : e.n1, l.dir > 0 ? e.n1 : e.n0};
        for (int k = 0; k < 2; k++)
            for (int e2 : net.nodes[nodes[k]].edges) {
                const RoadEdge& F = net.edges[e2];
                if (F.name.find("Causeway") != std::string::npos) return F.name;
            }
        for (int k = 0; k < 2; k++)
            for (int e2 : net.nodes[nodes[k]].edges) {
                const RoadEdge& F = net.edges[e2];
                if (F.name.empty() || F.name == e.name || F.cls == RC_HIGHWAY || F.cls == RC_RAMP) continue;
                return transit_bus::shortName(F.name);
            }
        return transit_bus::shortName(e.name);
    }

    bool build(TramLine& T) {
        std::vector<RouteLeg> legs;
        if (!chain(legs)) return false;
        int nl = (int)legs.size();
        // ---- raw track: curb lane, connector, curb lane, ...
        std::vector<vec3> raw;
        std::vector<int> laneStartIdx(nl), laneEndIdx(nl);
        std::vector<float> u0(nl), u1(nl);
        std::vector<vec3> conn;
        for (int i = 0; i < nl; i++) geo.laneRange(legs[i].edge, legs[i].dir, u0[i], u1[i]);
        // lane stretches the track follows: the curb lanes, except where a car's connector is too tight for a tram (an
        // acute corner): there the curve starts a few metres earlier and ends later, easing the radius
        std::vector<float> ua = u0, ub = u1;
        auto minRadius = [](const std::vector<vec3>& c) {
            float r = 1e9f;
            for (size_t k = 1; k + 1 < c.size(); k++) {
                vec2 a = c[k].xy() - c[k - 1].xy(), b = c[k + 1].xy() - c[k].xy();
                float la = length(a), lb = length(b);
                if (la < 1e-4f || lb < 1e-4f) continue;
                float ang = fabsf(atan2f(cross(a, b), dot(a, b)));
                if (ang > 1e-5f) r = Min(r, 0.5f * (la + lb) / ang);
            }
            return r;
        };
        for (int i = 0; i < nl; i++) {
            int bi = (i + 1) % nl;
            const RouteLeg& l = legs[i];
            const RouteLeg& b = legs[bi];
            connector(geo.pos(l.edge, l.dir, u1[i]), geo.tangent(l.edge, l.dir, u1[i]), geo.pos(b.edge, b.dir, u0[bi]), geo.tangent(b.edge, b.dir, u0[bi]), conn);
            for (float ext = 1.5f; ext <= 9.f && minRadius(conn) < 9.f; ext += 1.5f) {
                float ea = Min(ext, (ub[i] - ua[i]) * 0.25f), eb = Min(ext, (ub[bi] - ua[bi]) * 0.25f);
                ub[i] = u1[i] - ea;
                ua[bi] = u0[bi] + eb;
                connector(geo.pos(l.edge, l.dir, ub[i]), geo.tangent(l.edge, l.dir, ub[i]), geo.pos(b.edge, b.dir, ua[bi]), geo.tangent(b.edge, b.dir, ua[bi]), conn);
            }
        }
        for (int i = 0; i < nl; i++) {
            const RouteLeg& l = legs[i];
            int nSeg = Max(1, (int)ceilf((ub[i] - ua[i]) / 2.f));
            laneStartIdx[i] = (int)raw.size();
            for (int k = 0; k <= nSeg; k++) raw.push_back(geo.pos(l.edge, l.dir, Lerp(ua[i], ub[i], (float)k / nSeg)));
            laneEndIdx[i] = (int)raw.size() - 1;
            int bi = (i + 1) % nl;
            const RouteLeg& b = legs[bi];
            connector(geo.pos(l.edge, l.dir, ub[i]), geo.tangent(l.edge, l.dir, ub[i]), geo.pos(b.edge, b.dir, ua[bi]), geo.tangent(b.edge, b.dir, ua[bi]), conn);
            for (size_t k = 1; k + 1 < conn.size(); k++) raw.push_back(conn[k]);
        }
        // cumulative arc length (closed)
        int nr = (int)raw.size();
        std::vector<float> acc(nr + 1, 0.f);
        for (int i = 0; i < nr; i++) acc[i + 1] = acc[i] + length(raw[(i + 1) % nr].xy() - raw[i].xy());
        float total = acc[nr];
        if (total < 200.f) return false;
        int n = Max(8, (int)roundf(total));
        T.ds = total / n;
        T.length = total;
        T.p.resize(n);
        T.t.resize(n);
        int j = 0;
        for (int i = 0; i < n; i++) {
            float s = i * T.ds;
            while (j + 1 < nr && acc[j + 1] <= s) j++;
            float seg = acc[j + 1] - acc[j];
            float f = seg > 1e-5f ? (s - acc[j]) / seg : 0.f;
            vec3 q = lerp(raw[j], raw[(j + 1) % nr], f);
            float z;
            if (net.surfaceHeight(q.xy(), &z, q.z + 2.5f) && fabsf(z - q.z) < 2.5f) q.z = z;
            T.p[i] = q;
        }
        for (int i = 0; i < n; i++) {
            vec2 d = T.p[(i + 1) % n].xy() - T.p[(i + n - 1) % n].xy();
            T.t[i] = length2(d) > 1e-8f ? normalize(d) : vec2(0, 1);
        }
        // ---- legs and junctions in track coordinates
        T.legs.clear();
        T.legU0 = u0;
        T.legU1 = u1;
        for (int i = 0; i < nl; i++) {
            RouteLeg l = legs[i];
            l.d0 = acc[laneStartIdx[i]] - (ua[i] - u0[i]);   // track position of travel coordinate u0
            T.legs.push_back(l);
        }
        T.junctions.clear();
        for (int i = 0; i < nl; i++) {
            const RouteLeg& a = legs[i];
            const RouteLeg& b = legs[(i + 1) % nl];
            const RoadEdge& ea = net.edges[a.edge];
            int node = endNode(ea, a.dir);
            const RoadNode& nd = net.nodes[node];
            if ((int)nd.edges.size() < 3) continue;
            TramJunction J;
            J.sIn = acc[laneEndIdx[i]] + (u1[i] - ub[i]);
            J.sOut = acc[laneStartIdx[(i + 1) % nl]] - (ua[(i + 1) % nl] - u0[(i + 1) % nl]);
            if (J.sOut < J.sIn) J.sOut += total;
            J.node = node;
            J.fromEdge = a.edge;
            J.fromDir = a.dir;
            J.toEdge = b.edge;
            J.toDir = b.dir;
            vec2 tIn = edgeDir(ea, a.dir, ea.length - 1.f), tOut = edgeDir(net.edges[b.edge], b.dir, 1.f);
            J.turn = cross(tIn, tOut) < -0.35f ? 1 : 0;
            J.control = nd.control;
            int best = 0;
            for (int e2 : nd.edges)
                if (e2 != a.edge && e2 != b.edge) best = Max(best, classRank(net.edges[e2].cls));
            J.minor = nd.control == 0 && best > classRank(ea.cls);
            T.junctions.push_back(J);
        }
        std::sort(T.junctions.begin(), T.junctions.end(), [](const TramJunction& x, const TramJunction& y) { return x.sIn < y.sIn; });
        // ---- stops: one candidate per block (far side, just past the junction), then ~400 m apart
        struct Cand {
            float s;
            int leg;
            float u;
            TramStop st;
        };
        std::vector<Cand> cands;
        for (int i = 0; i < nl; i++) {
            const RouteLeg& l = legs[i];
            const RoadEdge& e = net.edges[l.edge];
            if (!(e.sidewalk > 2.9f) || u1[i] - u0[i] < kFrontMin + kFrontEndGap + 2.f) continue;
            for (float uf = u0[i] + kFrontMin; uf <= u1[i] - kFrontEndGap; uf += 4.f) {
                float um = uf - tram_dims::kLength * 0.5f;
                vec3 c = edgePos(e, l.dir, um);
                vec2 al = edgeDir(e, l.dir, um), rt(al.y, -al.x);
                vec2 shelter = c.xy() + rt * (e.halfWidth + e.sidewalk - 1.25f);
                float z = c.z + 0.15f;
                if (c.z - map.heightAt(c.x, c.y) > 1.2f || map.isWater(c.x, c.y)) continue;
                if (!sidewalkFree(shelter, z, 1.6f) || !sidewalkFree(shelter + al * 3.2f, z, 0.8f) || !sidewalkFree(shelter - al * 3.2f, z, 0.8f)) continue;
                if (furnitureNear(e, l.dir, um, 5.5f, 0.f)) continue;
                vec3 cf = edgePos(e, l.dir, uf);
                vec2 sign = cf.xy() + rt * (e.halfWidth + 0.5f);
                if (!sidewalkFree(sign, z, 0.3f)) continue;
                bool nearBus = false;
                for (const BusStop& b : N.busStops)
                    if (dot(b.along, al) > 0.3f && (length(b.pos - shelter) < 34.f || length(b.flag - sign) < 30.f)) nearBus = true;
                if (nearBus) continue;
                Cand cd;
                cd.s = acc[laneStartIdx[i]] + (uf - ua[i]);
                cd.leg = i;
                cd.u = uf;
                cd.st.s = cd.s;
                cd.st.pos = shelter;
                cd.st.face = -rt;
                cd.st.along = al;
                cd.st.z = z;
                cd.st.edge = l.edge;
                cd.st.dir = l.dir;
                cd.st.u = uf;
                cd.st.curbLat = e.halfWidth - geo.curbOffset(e, l.dir) * (float)l.dir;
                cands.push_back(cd);
                break;   // the first (far side) spot of the block
            }
        }
        if (cands.size() < 4) {
            LOG("Transit: streetcar: only %zu stop sites", cands.size());
            return false;
        }
        // start at the ferry terminal stop, then greedy at ~kStopSpacing
        int first = 0;
        float bestD = 1e30f;
        for (int k = 0; k < (int)cands.size(); k++)
            for (const FerryPier& fp : N.piers) {
                float d = length(cands[k].st.pos - fp.base);
                if (d < bestD) {
                    bestD = d;
                    first = k;
                }
            }
        std::vector<int> chosen;
        chosen.push_back(first);
        float s0 = cands[first].s;
        float last = 0.f;   // distance ahead of s0 of the last chosen stop
        while (true) {
            int best = -1;
            float score = 1e30f;
            for (int k = 0; k < (int)cands.size(); k++) {
                float a = T.ahead(s0, cands[k].s);
                if (a <= last + 250.f || a > T.length - 250.f) continue;
                float sc = fabsf(a - (last + kStopSpacing));
                if (sc < score) {
                    score = sc;
                    best = k;
                }
            }
            if (best < 0 || T.ahead(s0, cands[best].s) > last + kStopSpacing * 1.6f) break;
            chosen.push_back(best);
            last = T.ahead(s0, cands[best].s);
        }
        T.stops.clear();
        for (int k : chosen) {
            TramStop st = cands[k].st;
            st.name = stopName(legs[cands[k].leg], st.pos);
            st.seed = hash32(0x7A3u + (u32)st.edge * 7919u + (u32)(st.u * 4.f));
            T.stops.push_back(st);
        }
        // unique names (two stops on the same cross street: add the avenue)
        for (size_t a = 0; a < T.stops.size(); a++)
            for (size_t b = a + 1; b < T.stops.size(); b++)
                if (T.stops[a].name == T.stops[b].name) {
                    T.stops[b].name = transit_bus::shortName(net.edges[T.stops[b].edge].name) + " & " + T.stops[b].name;
                    T.stops[a].name = transit_bus::shortName(net.edges[T.stops[a].edge].name) + " & " + T.stops[a].name;
                }
        std::sort(T.stops.begin(), T.stops.end(), [](const TramStop& x, const TramStop& y) { return x.s < y.s; });
        // ---- overhead line: curbside poles along the lanes, corner poles on the right-hand curves
        T.poles.clear();
        T.wire.clear();
        for (int i = 0; i < nl; i++) {
            const RouteLeg& l = legs[i];
            const RoadEdge& e = net.edges[l.edge];
            float len = ub[i] - ua[i];
            int cnt = Max(1, (int)roundf(len / kPoleSpacing));
            float step = len / cnt;
            for (int k = 0; k <= cnt; k++) {
                float u = Clamp(ua[i] + step * k, ua[i] + 2.5f, ub[i] - 2.5f);
                if (k > 0 && k < cnt) u = ua[i] + step * k;
                // slide off lamps, furniture, stops
                bool placed = false;
                for (int tryk = 0; tryk < 7 && !placed; tryk++) {
                    float du = (tryk == 0 ? 0.f : ((tryk & 1) ? 1.f : -1.f) * (float)((tryk + 1) / 2) * 1.8f);
                    float uu = Clamp(u + du, ua[i] + 1.f, ub[i] - 1.f);
                    vec3 c = edgePos(e, l.dir, uu);
                    vec2 al = edgeDir(e, l.dir, uu), rt(al.y, -al.x);
                    float lat = e.sidewalk > 1.f ? e.halfWidth + 0.5f : e.halfWidth + 1.2f;
                    vec2 pp = c.xy() + rt * lat;
                    float z = c.z + (e.sidewalk > 1.f ? 0.15f : 0.f);
                    if (!sidewalkFree(pp, z, 0.35f)) continue;
                    if (furnitureNear(e, l.dir, uu, 1.6f, 2.2f)) continue;
                    bool clash = false;
                    for (const TramStop& st : T.stops)
                        if (length(st.pos - pp) < 5.5f) clash = true;
                    for (const BusStop& b : N.busStops)
                        if (length(b.pos - pp) < 4.5f || length(b.flag - pp) < 1.5f) clash = true;
                    for (const TramPole& q : T.poles)
                        if (length(q.pos - pp) < 12.f) clash = true;
                    if (clash) continue;
                    TramPole P;
                    P.pos = pp;
                    P.z = z;
                    P.holds.push_back(acc[laneStartIdx[i]] + (uu - ua[i]));
                    T.poles.push_back(P);
                    placed = true;
                }
            }
            // right-hand curve into the next leg: a corner pole inside the curve holds pull-offs along the connector
            const RouteLeg& b = legs[(i + 1) % nl];
            int node = endNode(e, l.dir);
            if ((int)net.nodes[node].edges.size() < 3) continue;
            vec2 tIn = edgeDir(e, l.dir, e.length - 1.f), tOut = edgeDir(net.edges[b.edge], b.dir, 1.f);
            if (cross(tIn, tOut) > -0.35f) continue;
            float sa = acc[laneEndIdx[i]], sb = acc[laneStartIdx[(i + 1) % nl]];
            if (sb < sa) sb += total;
            vec3 pa = T.at(sa), pb = T.at(sb), pm = T.at((sa + sb) * 0.5f);
            // corner pole where the two curb lines meet, pushed onto the corner sidewalk
            const RoadEdge& eb = net.edges[b.edge];
            float reach = Max(e.halfWidth, eb.halfWidth);
            TramPole P;
            bool ok = false;
            for (float extra = 1.2f; extra <= 5.f && !ok; extra += 0.9f) {
                // corner of the two curb lines, pushed onto the walk
                vec2 cIn = pa.xy() + vec2(tIn.y, -tIn.x) * (e.halfWidth - geo.curbOffset(e, l.dir) * (float)l.dir + extra);
                vec2 cOut = pb.xy() + vec2(tOut.y, -tOut.x) * (eb.halfWidth - geo.curbOffset(eb, b.dir) * (float)b.dir + extra);
                // intersection of the offset lines cIn + tIn * a and cOut - tOut * b
                float den = cross(tIn, tOut);
                if (fabsf(den) < 1e-3f) break;
                float a = cross(cOut - cIn, tOut) / den;
                vec2 X = cIn + tIn * a;
                if (length(X - pm.xy()) > reach * 2.5f + 8.f) continue;
                float z = net.nodes[node].z + 0.15f;
                if (sidewalkFree(X, z, 0.35f)) {
                    P.pos = X;
                    P.z = z;
                    ok = true;
                }
            }
            int nh = Max(2, (int)((sb - sa) / 5.5f));
            for (int k = 1; k < nh; k++) {
                float s = T.wrap(sa + (sb - sa) * k / nh);
                if (ok) P.holds.push_back(s);
                else T.wire.push_back(s);
            }
            if (ok) T.poles.push_back(P);
        }
        for (const TramPole& P : T.poles)
            for (float s : P.holds) T.wire.push_back(T.wrap(s));
        std::sort(T.wire.begin(), T.wire.end());
        // ---- colour, fleet
        vec3 srgb(((kLineRgb >> 16) & 255) / 255.f, ((kLineRgb >> 8) & 255) / 255.f, (kLineRgb & 255) / 255.f);
        T.color = srgbToLinear(srgb);
        T.colorSrgb = packRGBA8(srgb.x, srgb.y, srgb.z, 1.f);
        float cycle = T.length / 7.5f + T.stops.size() * 22.f;
        T.trams = Clamp((int)ceilf(cycle / 330.f), 3, 6);
        T.headway = cycle / T.trams;
        return true;
    }
};

// Site elements: track chunks (rails, wire, poles) and stops; keep street dressing off the poles and the platforms
void emitElements(SiteSet& S, const TramLine& T, int line) {
    const float chunk = 64.f;
    int nc = Max(1, (int)ceilf(T.length / chunk));
    for (int c = 0; c < nc; c++) {
        float s0 = T.length * c / nc, s1 = T.length * (c + 1) / nc;
        SiteElem e;
        e.kind = SK_TRAM_TRACK;
        e.variant = (u16)line;
        e.seed = hash32(0x7A1u + (u32)c);
        e.p[0] = s0;
        e.p[1] = s1;
        vec3 m = T.at((s0 + s1) * 0.5f);
        e.c = m.xy();
        e.ax = T.dirAt((s0 + s1) * 0.5f);
        e.hx = (s1 - s0) * 0.5f;
        e.hy = 8.f;
        e.z = m.z;
        e.h = tram_dims::kWireHeight + 1.f;
        for (float s = s0; s < s1 + 0.5f; s += 8.f) e.pts.push_back(T.at(Min(s, s1)).xy());
        for (const TramPole& P : T.poles)
            if (!P.holds.empty() && P.holds[0] >= s0 && P.holds[0] < s1) e.pts.push_back(P.pos);
        S.elems.push_back(e);
    }
    for (size_t i = 0; i < T.stops.size(); i++) {
        const TramStop& st = T.stops[i];
        SiteElem e;
        e.kind = SK_TRAM_STOP;
        e.variant = (u16)(line * 256 + (int)i);
        e.seed = st.seed;
        e.c = st.pos;
        e.ax = st.along;
        e.hx = 15.f;
        e.hy = 3.f;
        e.z = st.z;
        e.h = 3.4f;
        e.text = st.name;
        vec2 front = T.at(st.s).xy(), rear = T.at(st.s - tram_dims::kLength).xy();
        e.pts.push_back(st.pos);
        e.pts.push_back(front);
        e.pts.push_back(rear);
        S.elems.push_back(e);
        S.vegBlocks.push_back({st.pos, st.along, 3.8f, 1.3f});
        // boarding strip along the curb: no meters, bins or palms where the doors open
        vec2 mid = (front + rear) * 0.5f;
        S.vegBlocks.push_back({mid - st.face * (st.curbLat + 0.7f), st.along, tram_dims::kLength * 0.5f + 1.f, 0.8f});
    }
    for (const TramPole& P : T.poles) {
        vec2 ax = T.dirAt(P.holds.empty() ? 0.f : P.holds[0]);
        S.vegBlocks.push_back({P.pos, ax, 1.6f, 0.9f});
    }
}

}  // namespace transit_tram

// Builds the streetcar line(s); called from transitFinalize after the bus routes
void buildTramLines(SiteSet& S, WorldMap& map, const RoadNetwork& net, TransitNet& N, const std::vector<vec2>& avoid, const std::vector<float>& avoidR) {
    double t0 = TimeSeconds();
    N.trams.clear();
    transit_tram::Builder B(net, map, S, N);
    B.avoid = avoid;
    B.avoidR = avoidR;
    TramLine T;
    if (!B.build(T)) {
        LOG("Transit: no streetcar line");
        return;
    }
    N.trams.push_back(T);
    transit_tram::emitElements(S, N.trams.back(), 0);
    const TramLine& L = N.trams.back();
    int signals = 0, stopSigns = 0;
    for (const TramJunction& J : L.junctions) {
        signals += J.control == 2;
        stopSigns += J.control == 1;
    }
    LOG("Transit: %s %.2f km, %zu stops, %zu junctions (%d signalled, %d stop signs), %zu poles, %d streetcars, headway %.1f min (%.2f s)", L.name.c_str(),
        L.length / 1000.f, L.stops.size(), L.junctions.size(), signals, stopSigns, L.poles.size(), L.trams, L.headway / 60.f, TimeSeconds() - t0);
}

}  // namespace World
