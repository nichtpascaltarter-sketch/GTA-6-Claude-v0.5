// Real-world validation: drives vehicles along the generated road network of Palmera with streamed LOD0 collision
// (buildings, streetlights, hydrants, trees), and aims them at real buildings and curbs.
// Included by test_vehicle.cpp after the engine sources.
#pragma once
#include <set>

namespace VT {

struct RealWorld {
    World::WorldMap map;
    World::RoadNetwork roads;
    World::BuildingSet bs;
    Phys::CollisionWorld cw;
    std::set<int> loaded;
    double cellGenSeconds = 0.0;

    void build() {
        double t0 = TimeSeconds();
        map.generate();
        World::gMap = &map;
        roads.generate(map);
        World::gRoads = &roads;
        bs.generate(map, roads);
        World::gBuildings = &bs;
        Phys::gCollision = &cw;
        printf("real world generated in %.1f s (%zu roads, %zu buildings)\n", TimeSeconds() - t0, roads.edges.size(), bs.buildings.size());
    }
    // Keep the 3x3 LOD0 cells around p loaded (collision only), drop far cells.
    void ensureCells(vec2 p) {
        int cx = (int)floorf((p.x + World::kWorldHalf) / World::kCellSize), cy = (int)floorf((p.y + World::kWorldHalf) / World::kCellSize);
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= World::kCellsPerSide || y >= World::kCellsPerSide) continue;
                int key = y * World::kCellsPerSide + x;
                if (loaded.count(key)) continue;
                double t0 = TimeSeconds();
                World::CellGeometry g;
                World::generateCell(x, y, true, g);
                cw.addCell(key, g.collision, g.props);
                cellGenSeconds += TimeSeconds() - t0;
                loaded.insert(key);
            }
        std::vector<int> drop;
        for (int key : loaded) {
            int x = key % World::kCellsPerSide, y = key / World::kCellsPerSide;
            if (abs(x - cx) > 2 || abs(y - cy) > 2) drop.push_back(key);
        }
        for (int key : drop) {
            cw.removeCell(key);
            loaded.erase(key);
        }
    }
};

// A random drive along the road graph: polyline (with lane offset), per-point road info.
struct Route {
    std::vector<vec3> pts;       // lane center points (z = road deck)
    std::vector<float> limit;    // speed limit (m/s)
    std::vector<u8> bridge;      // point lies on a bridge / elevated edge
    std::vector<float> dist;
    float totalLen = 0.f;

    void build(const World::RoadNetwork& R, Rng& rng, float wantLength, bool allowHighway) {
        pts.clear();
        limit.clear();
        bridge.clear();
        // start edge: paved, reasonably long
        int e = -1;
        for (int tries = 0; tries < 20000 && e < 0; tries++) {
            int c = (int)(rng.next() % R.edges.size());
            const World::RoadEdge& ed = R.edges[c];
            if (ed.flags & World::RF_UNPAVED || ed.cls == World::RC_DIRT || ed.length < 80.f) continue;
            if (!allowHighway && (ed.cls == World::RC_HIGHWAY || ed.cls == World::RC_RAMP)) continue;
            if (ed.lanesF == 0) continue;
            e = c;
        }
        int dir = 1;
        float total = 0.f;
        int guard = 0;
        while (e >= 0 && total < wantLength && guard++ < 400) {
            const World::RoadEdge& ed = R.edges[e];
            bool twoWay = ed.lanesF > 0 && ed.lanesB > 0;
            float off = twoWay ? Clamp(ed.halfWidth * 0.45f, 1.5f, 5.5f) : 0.f;
            const World::RoadClassInfo& ri = World::roadInfo(ed.cls);
            int n = (int)ed.pts.size();
            for (int k = 0; k < n; k++) {
                int i = dir > 0 ? k : n - 1 - k;
                int i0 = dir > 0 ? Max(i - 1, 0) : Min(i + 1, n - 1), i1 = dir > 0 ? Min(i + 1, n - 1) : Max(i - 1, 0);
                vec2 d = normalize(ed.pts[i1].xy() - ed.pts[i0].xy());
                vec2 right(d.y, -d.x);
                vec3 p = ed.pts[i] + vec3(right * off, 0.f);
                if (!pts.empty() && length(p.xy() - pts.back().xy()) < 1.f) continue;
                if (!pts.empty()) total += length(p.xy() - pts.back().xy());
                pts.push_back(p);
                limit.push_back(ri.speed);
                bridge.push_back((ed.flags & (World::RF_BRIDGE | World::RF_ELEVATED)) ? 1 : 0);
            }
            // next edge at the end node: continue roughly straight, random choice among similar directions
            int node = dir > 0 ? ed.n1 : ed.n0;
            vec2 inDir = normalize((dir > 0 ? ed.pts.back().xy() - ed.pts[Max(n - 2, 0)].xy() : ed.pts.front().xy() - ed.pts[Min(1, n - 1)].xy()));
            int best = -1;
            float bestScore = -1e9f;
            for (int ne : R.nodes[node].edges) {
                if (ne == e) continue;
                const World::RoadEdge& en = R.edges[ne];
                if (en.flags & World::RF_UNPAVED || en.cls == World::RC_DIRT) continue;
                if (!allowHighway && (en.cls == World::RC_HIGHWAY || en.cls == World::RC_RAMP)) continue;
                int nd = en.n0 == node ? 1 : -1;
                if ((nd > 0 && en.lanesF == 0) || (nd < 0 && en.lanesB == 0)) continue;
                int m = (int)en.pts.size();
                vec2 outDir = nd > 0 ? normalize(en.pts[Min(1, m - 1)].xy() - en.pts[0].xy()) : normalize(en.pts[Max(m - 2, 0)].xy() - en.pts.back().xy());
                float score = dot(inDir, outDir) + rng.range(0.f, 1.2f);
                if (dot(inDir, outDir) < -0.3f) score -= 5.f;  // no U-turns
                if (score > bestScore) {
                    bestScore = score;
                    best = ne;
                    dir = nd;
                }
            }
            e = best;  // dead end: the route ends here (the test starts a new one)
        }
        dist.assign(pts.size(), 0.f);
        for (size_t i = 1; i < pts.size(); i++) dist[i] = dist[i - 1] + length(pts[i].xy() - pts[i - 1].xy());
        totalLen = dist.empty() ? 0.f : dist.back();
    }
    // closest point index at or after `hint`
    int advance(vec2 p, int hint) const {
        int best = hint;
        float bd = 1e30f;
        for (int i = hint; i < Min(hint + 40, (int)pts.size()); i++) {
            float d = length2(pts[i].xy() - p);
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
        return best;
    }
    vec3 at(float s) const {
        s = Clamp(s, 0.f, totalLen);
        size_t i = std::upper_bound(dist.begin(), dist.end(), s) - dist.begin();
        if (i == 0) i = 1;
        if (i >= pts.size()) return pts.back();
        float t = (s - dist[i - 1]) / Max(dist[i] - dist[i - 1], 1e-3f);
        return lerp(pts[i - 1], pts[i], t);
    }
    // speed allowed by curvature over the next `span` meters (lateral acceleration budget aLat)
    float cornerSpeed(float s, float span, float aLat) const {
        float vmin = 1e9f;
        for (float d = 5.f; d < span; d += 5.f) {
            vec3 a = at(s + d - 5.f), b = at(s + d), c = at(s + d + 5.f);
            vec2 u = b.xy() - a.xy(), w = c.xy() - b.xy();
            float lu = length(u), lw = length(w);
            if (lu < 0.1f || lw < 0.1f) continue;
            float ang = fabsf(atan2f(cross(u, w), dot(u, w)));
            float kappa = ang / (0.5f * (lu + lw));
            float v = sqrtf(aLat / Max(kappa, 1e-4f));
            // allow braking distance: v^2 = v_corner^2 + 2*a*d
            v = sqrtf(v * v + 2.f * 5.f * (d - 5.f));
            vmin = Min(vmin, v);
        }
        return vmin;
    }
};

struct RealReport {
    std::string name;
    VehicleClass cls;
    float km = 0.f, avgSpeed = 0.f, bridgeKm = 0.f, maxRoll = 0.f, maxPitch = 0.f;
    int flips = 0, stuck = 0, blocked = 0, impacts = 0, broken = 0, nans = 0, fellThrough = 0, offDeck = 0;
    float bldgStop = -1.f, bldgInside = 0.f, bldgHealth = 0.f, curbCrossed = -1.f;
    double usPerStep = 0.0;
};

}  // namespace VT
