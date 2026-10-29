// The terrain under the roads (docs/HANDBUILT_WORLD.md section 3.3): flattened along every at-grade edge, under
// junction discs and turning circles, then cut to just below every road so none is buried. RoadNetwork::generate
// grades today's world with it, and tools/worldc.cpp the hand-built world's baked terrain, at compile time. The
// hand-built roads' decks (`deck` records) also leave the ground under them alone, wherever they are: a segment is
// graded up to a deck's end, wherever that falls in it, and square there, so nothing under the deck changes.
#include "roads.h"
#include "worldmap.h"
#include <algorithm>

namespace World {

namespace roadgrade_detail {

// A stretch of a segment to grade: its ends, and whether each is round (the segment's own) or square (where a deck
// begins, so that nothing under the deck changes)
struct Piece {
    vec3 a, c;
    bool capA, capC;
};

// The stretches of the segment a-c (plan distances s0 to s1 along edge ei) off its decks, with their heights
// interpolated, a deck's ends cutting them wherever they fall in the segment. Without decks there, the segment whole.
inline void offDeck(const std::vector<std::vector<vec2>>* decks, size_t ei, vec3 a, vec3 c, float s0, float s1, std::vector<Piece>& out) {
    out.clear();
    std::vector<vec2> on;   // the decks' stretches overlapping [s0, s1], and those touching its ends
    bool touchA = false, touchC = false;
    if (decks && ei < decks->size())
        for (const vec2& d : (*decks)[ei]) {
            const float lo = Max(d.x, s0), hi = Min(d.y, s1);
            if (hi > lo) on.push_back(vec2(lo, hi));
            touchA = touchA || (d.x <= s0 && d.y >= s0);
            touchC = touchC || (d.x <= s1 && d.y >= s1);
        }
    if (on.empty()) {
        out.push_back(Piece{a, c, !touchA, !touchC});
        return;
    }
    std::sort(on.begin(), on.end(), [](vec2 x, vec2 y) { return x.x < y.x; });
    const float len = s1 - s0;
    const auto point = [&](float s) { return len > 1e-6f ? a + (c - a) * ((s - s0) / len) : a; };
    float from = s0;
    bool capFrom = !touchA;
    for (const vec2& k : on) {
        if (k.x > from + 0.01f) out.push_back(Piece{from == s0 ? a : point(from), point(k.x), capFrom, false});
        from = Max(from, k.y);
        capFrom = false;
    }
    if (s1 > from + 0.01f) out.push_back(Piece{point(from), c, capFrom, !touchC});
}

}  // namespace roadgrade_detail

void gradeTerrainForRoads(const RoadNetwork& net, WorldMap& map, const std::vector<std::vector<vec2>>* decks) {
    using namespace roadgrade_detail;
    std::vector<Piece> pieces;
    // ============================================================= Terrain flattening along at-grade roads
    for (size_t ei = 0; ei < net.edges.size(); ei++) {
        const RoadEdge& e = net.edges[ei];
        if (e.cls == RC_HIGHWAY && (e.flags & RF_ELEVATED)) continue;
        float s0 = 0.f;
        for (size_t i = 0; i + 1 < e.pts.size(); i++) {
            const float s1 = s0 + length(e.pts[i + 1].xy() - e.pts[i].xy());
            offDeck(decks, ei, e.pts[i], e.pts[i + 1], s0, s1, pieces);
            s0 = s1;
            for (const Piece& pc : pieces) {
                const vec3 a = pc.a, c = pc.c;
                float ga = map.heightAt(a.x, a.y), gc = map.heightAt(c.x, c.y);
                // skip bridge spans (deck well above ground)
                if (a.z - ga > 3.f && c.z - gc > 3.f) continue;
                float hw = e.halfWidth + e.sidewalk + 1.5f;
                map.flattenAlong(a.xy(), c.xy(), a.z - 0.3f, c.z - 0.3f, hw, 14.f, pc.capA, pc.capC);
            }
        }
    }
    // dead-end turning bulbs sit on the ground (at the lower of the road's heights across them: the road may still climb into it)
    auto bulbGround = [&](const RoadNode& nd) {
        const RoadEdge& e = net.edges[nd.edges[0]];
        float br = net.bulbRadius(nd);
        vec3 back = e.n1 == (int)(&nd - &net.nodes[0]) ? e.posAt(e.length - br) : e.posAt(br);
        return Min(nd.z, back.z) - 0.3f;
    };
    for (auto& nd : net.nodes) {
        if (nd.radius > 0 && !nd.highway) map.flattenAlong(nd.p, nd.p + vec2(0.01f, 0), nd.z - 0.3f, nd.z - 0.3f, nd.radius + 4.f, 12.f);
        float br = net.bulbRadius(nd);
        if (br > 0.f) {
            float zg = bulbGround(nd);
            map.flattenAlong(nd.p, nd.p + vec2(0.01f, 0), zg, zg, br + net.edges[nd.edges[0]].sidewalk + 1.5f, 12.f);
        }
    }
    // Roads are never buried (later flattening, embankment blends and node discs raise the terrain around their neighbours):
    // cut the terrain to just below every road, flat one heightmap texel beyond the sidewalk so the bilinear terrain cannot
    // rise into the verge, then a cut face. Only lowers terrain; where roads at different heights overlap in plan, the higher
    // one ends up on a deck (the road mesher gives it parapets and piers).
    for (size_t ei = 0; ei < net.edges.size(); ei++) {
        const RoadEdge& e = net.edges[ei];
        float s0 = 0.f;
        for (size_t i = 0; i + 1 < e.pts.size(); i++) {
            const float s1 = s0 + length(e.pts[i + 1].xy() - e.pts[i].xy());
            offDeck(decks, ei, e.pts[i], e.pts[i + 1], s0, s1, pieces);
            s0 = s1;
            for (const Piece& pc : pieces) {
                float flat = e.halfWidth + e.sidewalk + kHeightCell;
                map.lowerAlong(pc.a.xy(), pc.c.xy(), pc.a.z - 0.3f, pc.c.z - 0.3f, flat, flat + 10.f, 0.7f, pc.capA, pc.capC);
            }
        }
    }
    for (auto& nd : net.nodes) {
        if (nd.radius > 0) map.lowerAlong(nd.p, nd.p + vec2(0.01f, 0), nd.z - 0.3f, nd.z - 0.3f, nd.radius + kHeightCell, nd.radius + kHeightCell + 10.f, 0.7f);
        float br = net.bulbRadius(nd);
        if (br > 0.f) {
            float r = br + net.edges[nd.edges[0]].sidewalk + kHeightCell, zg = bulbGround(nd);
            map.lowerAlong(nd.p, nd.p + vec2(0.01f, 0), zg, zg, r, r + 10.f, 0.7f);
        }
    }
}

}  // namespace World
