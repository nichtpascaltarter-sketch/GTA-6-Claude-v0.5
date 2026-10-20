// The road network's data and its queries (roads.h) apart from the generator: the road classes, positions along an
// edge, the junction pass, the spatial hash and the queries on it. roads.cpp includes it, so the game and the tools that
// build the procedural world get it from there. The host tools that compile the hand-built roads (worldmodel_roads.cpp)
// include it on its own, with src/core/math.cpp, and need none of the generator's world. A build includes it once:
// alone or through roads.cpp, never both.
#include "roads.h"

namespace World {

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

// Junction radii, cut-backs and control, from the edges at each node: generate()'s last pass over the graph
void RoadNetwork::finishJunctions() {
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
