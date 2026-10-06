#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/core/noise.cpp"
#include "src/core/jobs.cpp"
#include "src/render/mesh.cpp"
#include "src/world/worldmap.cpp"
#include "src/world/sites.cpp"
#include "src/world/roads.cpp"
#include "src/world/roadmesh.cpp"
#include "src/world/buildings.cpp"
#include "src/world/buildmesh.cpp"
#include "src/world/propmesh.cpp"
#include "src/world/cellgen.cpp"
#include "src/world/sitegeo.cpp"
#include "src/world/airport.cpp"
#include "src/world/port.cpp"
#include "src/world/landmarks.cpp"
#include "src/world/leisure.cpp"
#include "src/world/rural.cpp"
#include "src/world/transit.cpp"
#include "src/world/transitmesh.cpp"
#include "src/world/sitecell.cpp"
#include "src/world/facadedetail.cpp"
#include "src/world/interiorkit.cpp"
#include "src/world/interiorfurniture.cpp"
#include "src/world/interiorlayouts.cpp"
#include "src/world/interiorhomes.cpp"
#include "src/world/interiorvenues.cpp"
#include "src/world/interiorshops.cpp"
#include "src/world/interiorcivic.cpp"
#include "src/world/interiorindustrial.cpp"
#include "src/world/interiortower.cpp"
#include "src/world/interiorgarages.cpp"
#include "src/world/interiorresidences.cpp"
#include "src/world/interiors.cpp"
using namespace World;
// Road geometry around points: nearby edges (surface z, lateral offset, class, ends), deck state, collision boxes
int main(int argc, char** argv) {
    Jobs::init(2);
    WorldMap map; map.generate(); gMap = &map;
    RoadNetwork roads; roads.generate(map); gRoads = &roads;
    BuildingSet bs; bs.generate(map, roads); gBuildings = &bs;
    if (getenv("RAMPGRADE")) {
        // steepest grade on every ramp (over >= 6 m) and on all other roads
        float worstRamp = 0.f, worstOther = 0.f;
        int over7 = 0;
        for (const RoadEdge& e : roads.edges) {
            float w = 0.f;
            for (size_t i = 0; i < e.pts.size(); i++)
                for (size_t j = i + 1; j < e.pts.size(); j++) {
                    float d = e.dist[j] - e.dist[i];
                    if (d < 6.f) continue;
                    w = Max(w, fabsf(e.pts[j].z - e.pts[i].z) / d);
                    break;
                }
            if (e.cls == RC_RAMP) {
                worstRamp = Max(worstRamp, w);
                over7 += w > 0.0705f;
                for (size_t i = 0; i < e.pts.size(); i++)
                    for (size_t j = i + 1; j < e.pts.size(); j++) {
                        float d = e.dist[j] - e.dist[i];
                        if (d < 6.f) continue;
                        float g = fabsf(e.pts[j].z - e.pts[i].z) / d;
                        if (g > 0.0705f) {
                            bool hwyN0 = false;
                            for (int oe : roads.nodes[e.n0].edges) hwyN0 |= roads.edges[oe].cls == RC_HIGHWAY;
                            printf("  ramp %d %.1f%% at s %.0f-%.0f of %.0f (%s end at n0) z %.2f->%.2f node z %.2f / %.2f\n", (int)(&e - &roads.edges[0]), g * 100.f, e.dist[i], e.dist[j], e.length,
                                   hwyN0 ? "highway" : "landing", e.pts[i].z, e.pts[j].z, roads.nodes[e.n0].z, roads.nodes[e.n1].z);
                        }
                        break;
                    }
            }
            else if (!(e.flags & RF_UNPAVED)) worstOther = Max(worstOther, w);
        }
        printf("ramps: steepest %.1f%%, %d above 7.05%%; other paved roads steepest %.1f%%\n", worstRamp * 100.f, over7, worstOther * 100.f);
        fflush(stdout);
        Jobs::shutdown();
        return 0;
    }
    if (getenv("ICPLOTS")) {
        // one top-down plot per interchange (ramp clusters): $SP/ic/icNN.ppm
        std::vector<vec2> centers;
        std::vector<int> counts;
        for (const RoadEdge& e : roads.edges) {
            if (e.cls != RC_RAMP) continue;
            vec2 m = e.pts[e.pts.size() / 2].xy();
            bool found = false;
            for (size_t c = 0; c < centers.size() && !found; c++)
                if (length(centers[c] - m) < 700.f) { centers[c] = (centers[c] * (float)counts[c] + m) / (float)(counts[c] + 1); counts[c]++; found = true; }
            if (!found) { centers.push_back(m); counts.push_back(1); }
        }
        for (size_t c = 0; c < centers.size(); c++) {
            float cxp = centers[c].x, cyp = centers[c].y, size = 900.f;
            const int R = 600;
            std::vector<vec3> img((size_t)R * R, vec3(0.05f));
            auto toPix = [&](vec2 p, int& px, int& py) { px = (int)((p.x - (cxp - size * 0.5f)) / size * R); py = (int)(((cyp + size * 0.5f) - p.y) / size * R); };
            auto dot2 = [&](vec2 p, float rad, vec3 col) {
                int px, py; toPix(p, px, py);
                int rr = Max(1, (int)(rad / size * R));
                for (int y = py - rr; y <= py + rr; y++)
                    for (int x = px - rr; x <= px + rr; x++)
                        if (x >= 0 && y >= 0 && x < R && y < R && (x - px) * (x - px) + (y - py) * (y - py) <= rr * rr) img[(size_t)y * R + x] = col;
            };
            std::vector<int> cand;
            roads.edgesInRect(vec2(cxp, cyp) - vec2(size * 0.6f), vec2(cxp, cyp) + vec2(size * 0.6f), cand);
            for (int pass = 0; pass < 3; pass++)
                for (int ei : cand) {
                    const RoadEdge& e = roads.edges[ei];
                    int want = e.cls == RC_HIGHWAY ? 1 : (e.cls == RC_RAMP ? 2 : 0);
                    if (want != pass) continue;
                    for (size_t k = 0; k + 1 < e.pts.size(); k++) {
                        vec3 a = e.pts[k], d = e.pts[k + 1];
                        float len = length(d.xy() - a.xy());
                        int n = Max(1, (int)(len / (size / R * 0.5f)));
                        for (int q = 0; q <= n; q++) {
                            vec3 pp = lerp(a, d, (float)q / n);
                            float h = Clamp((pp.z - map.heightAt(pp.x, pp.y)) / 10.f, 0.f, 1.f);
                            vec3 col = pass == 0 ? (e.cls <= RC_AVENUE ? vec3(0.6f) : vec3(0.38f)) : (pass == 1 ? vec3(0.2f, 0.35f, 0.9f) * (0.5f + 0.5f * h) : vec3(0.3f + 0.7f * h, 0.9f - 0.7f * h, 0.1f));
                            dot2(pp.xy(), pass == 2 ? 3.f : e.halfWidth, col);
                        }
                    }
                }
            for (int ei : cand)
                if (roads.edges[ei].cls != RC_RAMP)
                    for (int nn : {roads.edges[ei].n0, roads.edges[ei].n1}) dot2(roads.nodes[nn].p, 2.5f, vec3(1.f, 1.f, 0.3f));
            Region rg = map.regionAt(cxp, cyp);
            printf("ic %zu center (%.0f, %.0f) ramps %d region %d\n", c, cxp, cyp, counts[c], (int)rg);
            std::string path = StrFormat("%s/ic%02zu.ppm", getenv("ICPLOTS"), c);
            FILE* fo = fopen(path.c_str(), "wb");
            fprintf(fo, "P6 %d %d 255\n", R, R);
            for (auto& col : img) { unsigned char b3[3] = {(unsigned char)(Saturate(col.x) * 255), (unsigned char)(Saturate(col.y) * 255), (unsigned char)(Saturate(col.z) * 255)}; fwrite(b3, 1, 3, fo); }
            fclose(fo);
        }
        fflush(stdout);
        Jobs::shutdown();
        return 0;
    }
    if (const char* pv = getenv("PLOT")) {
        // PLOT="x,y,size": top-down plot of the road network around (x, y): highways blue, ramps red (brighter = higher),
        // other roads grey, nodes as dots; writes plot.ppm in the scratchpad
        float cxp, cyp, size;
        sscanf(pv, "%f,%f,%f", &cxp, &cyp, &size);
        const int R = 900;
        std::vector<vec3> img((size_t)R * R, vec3(0.05f));
        auto toPix = [&](vec2 p, int& px, int& py) {
            px = (int)((p.x - (cxp - size * 0.5f)) / size * R);
            py = (int)(((cyp + size * 0.5f) - p.y) / size * R);
        };
        auto dot2 = [&](vec2 p, float rad, vec3 col) {
            int px, py;
            toPix(p, px, py);
            int rr = Max(1, (int)(rad / size * R));
            for (int y = py - rr; y <= py + rr; y++)
                for (int x = px - rr; x <= px + rr; x++)
                    if (x >= 0 && y >= 0 && x < R && y < R && (x - px) * (x - px) + (y - py) * (y - py) <= rr * rr) img[(size_t)y * R + x] = col;
        };
        std::vector<int> cand;
        roads.edgesInRect(vec2(cxp, cyp) - vec2(size * 0.6f), vec2(cxp, cyp) + vec2(size * 0.6f), cand);
        for (int pass = 0; pass < 3; pass++)
            for (int ei : cand) {
                const RoadEdge& e = roads.edges[ei];
                int want = e.cls == RC_HIGHWAY ? 1 : (e.cls == RC_RAMP ? 2 : 0);
                if (want != pass) continue;
                for (size_t k = 0; k + 1 < e.pts.size(); k++) {
                    vec3 a = e.pts[k], c = e.pts[k + 1];
                    float len = length(c.xy() - a.xy());
                    int n = Max(1, (int)(len / (size / R * 0.5f)));
                    for (int q = 0; q <= n; q++) {
                        vec3 pp = lerp(a, c, (float)q / n);
                        float h = Clamp((pp.z - map.heightAt(pp.x, pp.y)) / 12.f, 0.f, 1.f);
                        vec3 col = pass == 0 ? vec3(0.45f) : (pass == 1 ? vec3(0.2f, 0.35f, 0.9f) * (0.5f + 0.5f * h) : vec3(0.5f + 0.5f * h, 0.1f, 0.1f));
                        dot2(pp.xy(), pass == 2 ? 2.f : e.halfWidth, col);
                    }
                }
            }
        for (int ei : cand)
            for (int nn : {roads.edges[ei].n0, roads.edges[ei].n1}) dot2(roads.nodes[nn].p, 2.5f, vec3(1.f, 1.f, 0.3f));
        std::string path = std::string(getenv("PLOTOUT") ? getenv("PLOTOUT") : "plot.ppm");
        FILE* fo = fopen(path.c_str(), "wb");
        fprintf(fo, "P6 %d %d 255\n", R, R);
        for (auto& c : img) { unsigned char b3[3] = {(unsigned char)(Saturate(c.x) * 255), (unsigned char)(Saturate(c.y) * 255), (unsigned char)(Saturate(c.z) * 255)}; fwrite(b3, 1, 3, fo); }
        fclose(fo);
        fflush(stdout);
        Jobs::shutdown();
        return 0;
    }
    if (const char* nv = getenv("NODES")) {
        std::string lst(nv);
        size_t pos = 0;
        while (pos < lst.size()) {
            size_t q = lst.find(',', pos);
            int ni = atoi(lst.substr(pos, q == std::string::npos ? std::string::npos : q - pos).c_str());
            const RoadNode& nd = roads.nodes[ni];
            printf("node %d at (%.1f, %.1f) z %.2f r %.1f ctrl %d:\n", ni, nd.p.x, nd.p.y, nd.z, nd.radius, nd.control);
            for (int ei : nd.edges) {
                const RoadEdge& e = roads.edges[ei];
                printf("   edge %d %s '%s' len %.1f n0 %d n1 %d z %.2f..%.2f\n", ei, roadInfo(e.cls).name, e.name.c_str(), e.length, e.n0, e.n1, e.pts.front().z, e.pts.back().z);
            }
            if (q == std::string::npos) break;
            pos = q + 1;
        }
        fflush(stdout);
        Jobs::shutdown();
        return 0;
    }
    if (getenv("SHELTERS")) {
        for (int i = 1; i + 1 < argc; i += 2) {
            float px = atof(argv[i]), py = atof(argv[i + 1]);
            int cx0 = (int)floorf((px + kWorldHalf) / kCellSize), cy0 = (int)floorf((py + kWorldHalf) / kCellSize);
            int k = 0;
            for (int dy = -2; dy <= 2 && k < 6; dy++)
                for (int dx = -2; dx <= 2 && k < 6; dx++) {
                    CellGeometry geo;
                    generateCell(cx0 + dx, cy0 + dy, true, geo);
                    for (auto& pr : geo.props)
                        if (pr.type == PROP_BUS_STOP && k++ < 6)
                            printf("shelter at %.1f %.1f %.2f yaw %.1f deg variant %d\n", pr.pos.x, pr.pos.y, pr.pos.z, pr.yaw / kDegToRad, pr.variant);
                }
        }
        fflush(stdout);
        Jobs::shutdown();
        return 0;
    }
    if (const char* ev = getenv("EDGE")) {
        int ei = atoi(ev);
        const RoadEdge& e = roads.edges[ei];
        printf("edge %d %s len %.1f n0 %d n1 %d\n", ei, roadInfo(e.cls).name, e.length, e.n0, e.n1);
        for (size_t i = 0; i < e.pts.size(); i++) {
            float best = 1e9f, hz = 0.f;
            for (const RoadEdge& h : roads.edges) {
                if (h.cls != RC_HIGHWAY) continue;
                for (size_t k = 0; k + 1 < h.pts.size(); k++) {
                    float t;
                    float d = distPointSegment2D(e.pts[i].xy(), h.pts[k].xy(), h.pts[k + 1].xy(), &t);
                    if (d < best) { best = d; hz = Lerp(h.pts[k].z, h.pts[k + 1].z, t); }
                }
            }
            printf("  %2zu s %6.1f (%.1f, %.1f) z %.2f ground %.2f  highway dist %.1f z %.2f\n", i, e.dist[i], e.pts[i].x, e.pts[i].y, e.pts[i].z,
                   map.heightAt(e.pts[i].x, e.pts[i].y), best, hz);
        }
    }
    for (int i = 1; i + 1 < argc; i += 2) {
        float px = atof(argv[i]), py = atof(argv[i + 1]);
        vec2 P(px, py);
        printf("=== point (%.1f, %.1f) ground %.1f\n", px, py, map.heightAt(px, py));
        std::vector<int> cand;
        roads.edgesInRect(P - vec2(40.f), P + vec2(40.f), cand);
        for (int ei : cand) {
            const RoadEdge& e = roads.edges[ei];
            float best = 1e9f, bs2 = 0.f, bz = 0.f, side = 0.f;
            for (size_t k = 0; k + 1 < e.pts.size(); k++) {
                float t;
                float d = distPointSegment2D(P, e.pts[k].xy(), e.pts[k + 1].xy(), &t);
                if (d < best) {
                    best = d;
                    bs2 = e.dist[k] + t * (e.dist[k + 1] - e.dist[k]);
                    bz = Lerp(e.pts[k].z, e.pts[k + 1].z, t);
                    side = cross(e.pts[k + 1].xy() - e.pts[k].xy(), P - e.pts[k].xy()) >= 0 ? -1.f : 1.f;
                }
            }
            if (best > e.halfWidth + 12.f) continue;
            vec3 c = e.posAt(bs2);
            printf("  edge %5d %-10s '%s' lanes %d/%d hw %.1f flags %2d  dist %5.1f (side %+.0f)  s %5.1f/%5.1f cut %.0f/%.0f  z %.2f ground %.1f  n0 %d(z %.1f r %.0f) n1 %d(z %.1f r %.0f)\n",
                   ei, roadInfo(e.cls).name, e.name.c_str(), e.lanesF, e.lanesB, e.halfWidth, e.flags, best, side, bs2, e.length, e.cut0, e.cut1, bz, map.heightAt(c.x, c.y), e.n0,
                   roads.nodes[e.n0].z, roads.nodes[e.n0].radius, e.n1, roads.nodes[e.n1].z, roads.nodes[e.n1].radius);
        }
        float sz;
        if (roads.surfaceHeight(P, &sz)) printf("  surface %.2f\n", sz);
        if (getenv("BOXES")) {
            int cx = (int)floorf((px + kWorldHalf) / kCellSize), cy = (int)floorf((py + kWorldHalf) / kCellSize);
            CellGeometry geo;
            generateCell(cx, cy, true, geo);
            for (auto& b : geo.collision)
                if (length(b.c.xy() - P) < 8.f)
                    printf("  box c (%.1f, %.1f, %.1f) ax (%.2f, %.2f) he (%.2f, %.2f, %.2f)\n", b.c.x, b.c.y, b.c.z, b.ax.x, b.ax.y, b.he.x, b.he.y, b.he.z);
        }
    }
    Jobs::shutdown();
    fflush(stdout);
    return 0;
}
