// The terrain under the roads (docs/HANDBUILT_WORLD.md section 3.3): flattened along every at-grade edge, under
// junction discs and turning circles, then cut to just below every road so none is buried. RoadNetwork::generate
// grades today's world with it, and tools/worldc.cpp the hand-built world's baked terrain, at compile time. The
// hand-built roads' decks (`deck` records) also leave the ground under them alone, wherever they are.
#include "roads.h"
#include "worldmap.h"

namespace World {

namespace roadgrade_detail {

// Whether the segment of edge ei from plan distance s0 to s1 runs on one of its decks
inline bool onDeck(const std::vector<std::vector<vec2>>* decks, size_t ei, float s0, float s1) {
    if (!decks || ei >= decks->size()) return false;
    const float s = (s0 + s1) * 0.5f;
    for (const vec2& d : (*decks)[ei])
        if (s >= d.x && s <= d.y) return true;
    return false;
}

}  // namespace roadgrade_detail

void gradeTerrainForRoads(const RoadNetwork& net, WorldMap& map, const std::vector<std::vector<vec2>>* decks) {
    using namespace roadgrade_detail;
    // ============================================================= Terrain flattening along at-grade roads
    for (size_t ei = 0; ei < net.edges.size(); ei++) {
        const RoadEdge& e = net.edges[ei];
        if (e.cls == RC_HIGHWAY && (e.flags & RF_ELEVATED)) continue;
        float s0 = 0.f;
        for (size_t i = 0; i + 1 < e.pts.size(); i++) {
            vec3 a = e.pts[i], c = e.pts[i + 1];
            const float s1 = s0 + length(c.xy() - a.xy());
            const bool deck = onDeck(decks, ei, s0, s1);
            s0 = s1;
            if (deck) continue;
            float ga = map.heightAt(a.x, a.y), gc = map.heightAt(c.x, c.y);
            // skip bridge spans (deck well above ground)
            if (a.z - ga > 3.f && c.z - gc > 3.f) continue;
            float hw = e.halfWidth + e.sidewalk + 1.5f;
            map.flattenAlong(a.xy(), c.xy(), a.z - 0.3f, c.z - 0.3f, hw, 14.f);
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
            vec3 a = e.pts[i], c = e.pts[i + 1];
            const float s1 = s0 + length(c.xy() - a.xy());
            const bool deck = onDeck(decks, ei, s0, s1);
            s0 = s1;
            if (deck) continue;
            float flat = e.halfWidth + e.sidewalk + kHeightCell;
            map.lowerAlong(a.xy(), c.xy(), a.z - 0.3f, c.z - 0.3f, flat, flat + 10.f, 0.7f);
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
