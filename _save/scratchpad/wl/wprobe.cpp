// World probe (scratch tool): generates the world as the game does (map, roads, buildings) and dumps terrain / water /
// roads / buildings around points of interest as 1 m/px images plus text profiles.
// Build: g++ -std=c++17 -O1 -I/home/user/GTA-6-Claude-v0.5 wprobe.cpp -o wprobe -lpthread
#define R_ "/home/user/GTA-6-Claude-v0.5/"
#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/noise.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/jobs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/worldmap.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sites.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roads.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roadmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/buildings.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/buildmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/propmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/cellgen.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sitegeo.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/airport.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/port.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/landmarks.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/leisure.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/rural.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/transit.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/transitmesh.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sitecell.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/facadedetail.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorkit.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorfurniture.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorlayouts.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorhomes.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorvenues.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorshops.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorcivic.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorindustrial.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiortower.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorgarages.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiorresidences.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/interiors.cpp"
#include <thread>
#include <map>
using namespace World;

static void writePPM(const char* fn, int W, int H, const std::vector<vec3>& c) {
    FILE* f = fopen(fn, "wb");
    fprintf(f, "P6 %d %d 255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        unsigned char px[3] = {(unsigned char)(Saturate(c[i].x) * 255), (unsigned char)(Saturate(c[i].y) * 255), (unsigned char)(Saturate(c[i].z) * 255)};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

// top-down view: W x W m at `res` m/px around c
static void view(const WorldMap& m, const RoadNetwork& roads, const BuildingSet& bs, vec2 c, float size, float res, const char* fn) {
    int W = (int)(size / res);
    std::vector<vec3> img((size_t)W * W);
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            vec2 p = c + vec2((x + 0.5f) * res - size * 0.5f, size * 0.5f - (y + 0.5f) * res);
            float h = m.heightAt(p.x, p.y), wl = m.waterAt(p.x, p.y);
            float rz = 0.f;
            bool road = roads.surfaceHeight(p, &rz, h + 8.f);
            float pz = 0.f;
            bool pad = gSites && gSites->padHeight(p, &pz, h + 8.f);
            vec3 col;
            // the renderer draws z = 0 where no water level: flooded when terrain < max(level, 0) inside a water node
            float lvl = wl > kNoWater + 1.f ? wl : 0.f;
            if (road) col = vec3(0.85f, 0.85f, 0.8f) * (0.8f + 0.1f * Saturate(rz / 4.f));
            else if (pad) col = vec3(0.8f, 0.6f, 0.4f);
            else if (h < lvl) col = lerp(vec3(0.3f, 0.8f, 0.9f), vec3(0.0f, 0.1f, 0.5f), Saturate((lvl - h) / 4.f)) * (wl > kNoWater + 1.f ? 1.f : 0.6f) + (wl > kNoWater + 1.f ? vec3(0) : vec3(0.4f, 0, 0));
            else {
                float g = Saturate(h / 4.f);
                col = lerp(vec3(0.15f, 0.35f, 0.1f), vec3(0.9f, 0.9f, 0.5f), g);
                if (fmodf(h, 0.5f) < 0.05f) col *= 0.7f;   // 0.5 m contours
            }
            img[(size_t)y * W + x] = col;
        }
    for (const Building& b : bs.buildings) {
        if (length(b.c - c) > size) continue;
        vec2 ay = perp(b.ax);
        for (float u = -b.hx; u <= b.hx; u += res * 0.5f)
            for (float v = -b.hy; v <= b.hy; v += res * 0.5f) {
                if (fabsf(u) < b.hx - res && fabsf(v) < b.hy - res) continue;
                vec2 q = b.c + b.ax * u + ay * v;
                int px = (int)((q.x - c.x + size * 0.5f) / res), py = (int)((c.y + size * 0.5f - q.y) / res);
                if (px >= 0 && py >= 0 && px < W && py < W) img[(size_t)py * W + px] = vec3(0.9f, 0.2f, 0.9f);
            }
    }
    if (gSites)
        for (const SiteElem& e : gSites->elems) {
            if (length(e.c - c) > size) continue;
            vec2 ay = perp(e.ax);
            for (float u = -e.hx; u <= e.hx; u += res * 0.5f)
                for (float v = -e.hy; v <= e.hy; v += res * 0.5f) {
                    if (fabsf(u) < e.hx - res && fabsf(v) < e.hy - res) continue;
                    vec2 q = e.c + e.ax * u + ay * v;
                    int px = (int)((q.x - c.x + size * 0.5f) / res), py = (int)((c.y + size * 0.5f - q.y) / res);
                    if (px >= 0 && py >= 0 && px < W && py < W) img[(size_t)py * W + px] = vec3(1.f, 0.5f, 0.f);
                }
        }
    // centre mark
    for (int k = -3; k <= 3; k++) {
        int cx = W / 2, cy = W / 2;
        img[(size_t)cy * W + cx + k] = vec3(1, 0, 0);
        img[(size_t)(cy + k) * W + cx] = vec3(1, 0, 0);
    }
    writePPM(fn, W, W, img);
}


// texels drawn under water (the renderer: in a 64 m node with any water texel, the surface sits at the max of the 4
// nearest texels' levels, z 0 where none) that are not water bodies, or water bodies reaching a street's verge
static void floodScan(const WorldMap& m, const RoadNetwork& roads) {
    const int R = kHeightRes;
    const int leaf = 8;   // 64 m / 8 m texels
    int nres = R / leaf;
    std::vector<u8> wnode((size_t)nres * nres, 0);
    for (int ny = 0; ny < nres; ny++)
        for (int nx = 0; nx < nres; nx++) {
            bool any = false;
            for (int y = ny * leaf; y <= Min(R - 1, (ny + 1) * leaf) && !any; y++)
                for (int x = nx * leaf; x <= Min(R - 1, (nx + 1) * leaf) && !any; x++) any = m.waterLevel[(size_t)y * R + x] > kNoWater + 1.f;
            wnode[(size_t)ny * nres + nx] = any;
        }
    struct Cl { int n = 0; float minH = 1e9f; vec2 at; int edge = -1; float d = 1e9f; };
    std::map<long long, Cl> cl[2];
    int total[2] = {0, 0};
    for (int ty = 1; ty < R - 1; ty++)
        for (int tx = 1; tx < R - 1; tx++) {
            if (!wnode[(size_t)(ty / leaf) * nres + tx / leaf]) continue;
            size_t i = (size_t)ty * R + tx;
            float h = m.height[i];
            float lvl = -1e9f;
            for (int k = 0; k < 4; k++) {
                float w = m.waterLevel[(size_t)(ty + (k >> 1)) * R + tx + (k & 1)];
                lvl = Max(lvl, w > kNoWater + 1.f ? w : 0.f);
            }
            if (h >= lvl - 0.02f) continue;
            vec2 p(texelToWorld(tx), texelToWorld(ty));
            bool body = m.waterLevel[i] > kNoWater + 1.f;
            float s = 0, d = 0, side = 0;
            int e = roads.nearestEdge(p, 40.f, &s, &d, &side);
            if (e < 0) continue;
            const RoadEdge& E = roads.edges[e];
            if (E.flags & RF_BRIDGE) continue;
            float verge = d - E.halfWidth - E.sidewalk;
            int cat = body ? 1 : 0;
            if (cat == 0 && verge > 12.f) continue;   // dry land drawn flooded within 12 m of a sidewalk
            if (cat == 1 && verge > 1.5f) continue;   // a water body right up to the sidewalk's edge (or under it)
            total[cat]++;
            long long key = (long long)(int)floorf(p.x / 64.f) * 100000LL + (int)floorf(p.y / 64.f);
            Cl& c = cl[cat][key];
            c.n++;
            if (h < c.minH) { c.minH = h; c.at = p; c.edge = e; c.d = verge; }
        }
    for (int cat = 0; cat < 2; cat++) {
        printf("flood scan %s: %d texels\n", cat ? "B (a water body up to / under a sidewalk)" : "A (dry land drawn under water near a street)", total[cat]);
        for (auto& kv : cl[cat]) {
            const Cl& c = kv.second;
            const RoadEdge& E = roads.edges[c.edge];
            printf("  %4d texels near (%.0f, %.0f) region %d: min h %.2f water %.2f, '%s' cls %d flags %d verge %.1f road z %.2f\n", c.n, c.at.x, c.at.y,
                   (int)m.regionAt(c.at.x, c.at.y), c.minH, m.waterAt(c.at.x, c.at.y), E.name.c_str(), (int)E.cls, (int)E.flags, c.d, E.pts[E.pts.size() / 2].z);
        }
    }
}

static void summary(const WorldMap& m, const RoadNetwork& roads, const BuildingSet& bs, vec2 c, const char* name) {
    static const char* styles[] = {"tower","midrise","condo","deco","shops","stripmall","house","villa","warehouse","factory","farmhouse","barn","motel","gasstation","garage","church","shack"};
    int cnt[BS_COUNT] = {}, cntNear[BS_COUNT] = {};
    for (const Building& b : bs.buildings) {
        float d = length(b.c - c);
        if (d < 250.f) cnt[b.style]++;
        if (d < 120.f) cntNear[b.style]++;
    }
    printf("== %s (%.0f, %.0f) region %d h %.2f coast %.0f\n   buildings <120 m / <250 m:", name, c.x, c.y, (int)m.regionAt(c.x, c.y), m.heightAt(c.x, c.y), m.coastDistance(c.x, c.y));
    for (int s = 0; s < BS_COUNT; s++) if (cnt[s]) printf(" %s %d/%d", styles[s], cntNear[s], cnt[s]);
    printf("\n   roads within 150 m:\n");
    std::vector<int> ed;
    roads.edgesInRect(c - vec2(150.f), c + vec2(150.f), ed);
    std::sort(ed.begin(), ed.end());
    ed.erase(std::unique(ed.begin(), ed.end()), ed.end());
    float len[16] = {}, sw[16] = {};
    for (int e : ed) {
        const RoadEdge& E = roads.edges[e];
        float inside = 0.f;
        for (size_t i = 0; i + 1 < E.pts.size(); i++) {
            vec2 a = E.pts[i].xy(), b = E.pts[i + 1].xy();
            vec2 mid = (a + b) * 0.5f;
            if (length(mid - c) < 150.f) inside += length(b - a);
        }
        if (inside <= 0.f) continue;
        len[E.cls] += inside;
        if (E.sidewalk > 0.f) sw[E.cls] += inside;
        printf("     edge %d '%s' cls %d hw %.1f sidewalk %.1f flags %d: %.0f m inside\n", e, E.name.c_str(), (int)E.cls, E.halfWidth, E.sidewalk, (int)E.flags, inside);
    }
    if (gSites) {
        printf("   site elements within 400 m:");
        std::map<int, int> kinds;
        for (const SiteElem& e : gSites->elems) if (length(e.c - c) < 400.f) kinds[e.kind]++;
        for (auto& kv : kinds) printf(" k%d x%d", kv.first, kv.second);
        printf("\n");
        for (const SiteElem& e : gSites->elems) if (length(e.c - c) < 400.f && e.kind != SK_BILLBOARD && e.kind != SK_BUS_STOP)
            printf("     kind %d variant %d at (%.0f, %.0f) d %.0f z %.1f '%s'\n", e.kind, e.variant, e.c.x, e.c.y, length(e.c - c), e.z, e.text.c_str());
        for (const NamedPlace& p : gSites->places) if (length(p.pos - c) < 700.f) printf("     place '%s' kind %d at (%.0f, %.0f) d %.0f\n", p.name.c_str(), p.kind, p.pos.x, p.pos.y, length(p.pos - c));
    }
}

static void marinaCheck(const WorldMap& m, const RoadNetwork& roads) {
    for (const SiteElem& e : gSites->elems) {
        if (e.kind != SK_MARINA && e.kind != SK_RIVER_MARINA) continue;
        vec2 n = perp(e.ax);
        printf("marina kind %d at (%.0f, %.0f) ax (%.2f, %.2f) p0 %.1f p1 %.1f\n", e.kind, e.c.x, e.c.y, e.ax.x, e.ax.y, e.p[0], e.p[1]);
        if (e.kind == SK_MARINA) {
            vec2 shop = e.c - e.ax * 9.f + n * 3.5f, start = e.c - e.ax * 6.f, pier = start + e.ax * (e.p[0] * 0.4f) + n * 0.9f, hint = e.c - e.ax * 50.f;
            printf("  shop (%.1f, %.1f) h %.2f water %.2f | pier (%.1f, %.1f) h %.2f water %.2f | hint (%.1f, %.1f) h %.2f\n", shop.x, shop.y, m.heightAt(shop.x, shop.y),
                   m.waterAt(shop.x, shop.y), pier.x, pier.y, m.heightAt(pier.x, pier.y), m.waterAt(pier.x, pier.y), hint.x, hint.y, m.heightAt(hint.x, hint.y));
            float s = 0, d = 0, side = 0;
            int ed = roads.nearestEdge(hint, 260.f, &s, &d, &side);
            if (ed >= 0) printf("  street at the hint: edge %d '%s' dist %.1f s %.1f / %.1f\n", ed, roads.edges[ed].name.c_str(), d, s, roads.edges[ed].length);
            for (float t = -60.f; t <= 20.f; t += 4.f) {
                vec2 q = e.c + e.ax * t;
                float rz = 0;
                bool rd = roads.surfaceHeight(q, &rz, 10.f);
                printf("    along %+5.0f: h %6.2f water %8.2f %s\n", t, m.heightAt(q.x, q.y), m.waterAt(q.x, q.y), rd ? "road" : "");
            }
        }
    }
}

static void drivewayCheck(const WorldMap& m, const RoadNetwork& roads, const BuildingSet& bs, vec2 c, float r) {
    int n = 0, garageN = 0, yardOk = 0, bld = 0, slope = 0, water = 0, road = 0, site = 0, ok = 0;
    float yardSum = 0.f;
    for (const Building& b : bs.buildings) {
        if (length(b.c - c) > r) continue;
        if (b.style != BS_HOUSE && b.style != BS_VILLA) continue;
        n++;
        bool garage = (b.style == BS_HOUSE && (b.seed % 10u) < 7u) || b.style == BS_VILLA;
        if (!garage) continue;
        garageN++;
        vec2 F = b.front, A = b.ax;
        float yard = dot(b.lotC - b.c, F) + b.lotHy - b.hy;
        yardSum += yard;
        if (yard <= 6.4f) continue;
        yardOk++;
        float side = (b.seed & 64u) ? 1.f : -1.f;
        vec2 cp = b.c + A * (side * (b.hx + 3.2f)) + F * (b.hy + 3.f);
        vec2 f = -F, rr = perp(f);
        const vec2 offs[5] = {vec2(0.f), f * 2.3f, -f * 2.3f, rr * 0.95f, -rr * 0.95f};
        float z0 = m.heightAt(cp.x, cp.y);
        bool bad = false;
        for (const vec2& o : offs) {
            vec2 q = cp + o;
            if (m.isWater(q.x, q.y)) { water++; bad = true; break; }
            if (bs.pointInBuilding(q, 0.35f)) { bld++; bad = true; break; }
            float z = m.heightAt(q.x, q.y), rz = 0;
            if (roads.surfaceHeight(q, &rz, z + 6.f)) { road++; bad = true; break; }
            if (fabsf(z - z0) > 0.45f) { slope++; bad = true; break; }
            if (siteColliderNear(vec3(q, z + 0.3f), 0.45f, 1.3f)) { site++; bad = true; break; }
        }
        if (!bad) ok++;
    }
    printf("driveways near (%.0f, %.0f) r %.0f: houses/villas %d, with a garage %d (mean yard %.1f m), yard > 6.4: %d -> ok %d | in building %d, on road %d, slope %d, water %d, site %d\n",
           c.x, c.y, r, n, garageN, garageN ? yardSum / garageN : 0.f, yardOk, ok, bld, road, slope, water, site);
}

int main(int argc, char** argv) {
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    WorldMap map;
    map.generate();
    gMap = &map;
    RoadNetwork roads;
    roads.generate(map);
    gRoads = &roads;
    BuildingSet bs;
    bs.generate(map, roads);
    gBuildings = &bs;
    if (argc > 1 && !strcmp(argv[1], "sum")) {
        for (int i = 2; i + 2 < argc; i += 3) summary(map, roads, bs, vec2((float)atof(argv[i]), (float)atof(argv[i + 1])), argv[i + 2]);
        Jobs::shutdown();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "drive")) { for (int i = 2; i + 2 < argc; i += 3) drivewayCheck(map, roads, bs, vec2((float)atof(argv[i]), (float)atof(argv[i + 1])), (float)atof(argv[i + 2])); Jobs::shutdown(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "marina")) { marinaCheck(map, roads); Jobs::shutdown(); return 0; }
    if (argc > 1 && !strcmp(argv[1], "scan")) { floodScan(map, roads); Jobs::shutdown(); return 0; }
    // args: x y size res name  (repeated)
    for (int i = 1; i + 4 < argc; i += 5) {
        vec2 c((float)atof(argv[i]), (float)atof(argv[i + 1]));
        float size = (float)atof(argv[i + 2]), res = (float)atof(argv[i + 3]);
        std::string fn = std::string(argv[i + 4]) + ".ppm";
        view(map, roads, bs, c, size, res, fn.c_str());
        printf("== %s at %.0f %.0f: region %d h %.2f water %.2f coast %.1f sand %.2f\n", argv[i + 4], c.x, c.y, (int)map.regionAt(c.x, c.y), map.heightAt(c.x, c.y),
               map.waterAt(c.x, c.y), map.coastDistance(c.x, c.y), map.beachSand(c.x, c.y));
        float s = 0, d = 0, side = 0;
        int e = roads.nearestEdge(c, 60.f, &s, &d, &side);
        if (e >= 0) {
            const RoadEdge& E = roads.edges[e];
            vec3 p = E.posAt(s), t = E.tangentAt(s);
            vec2 n = perp(normalize(t.xy()));
            printf("   nearest edge %d '%s' cls %d hw %.1f sidewalk %.1f at s %.1f dist %.1f side %.0f road z %.2f\n", e, E.name.c_str(), (int)E.cls, E.halfWidth,
                   E.sidewalk, s, d, side, p.z);
            printf("   profile across (off: terrain / water / road surface):\n");
            for (float o = -30.f; o <= 30.f; o += 1.f) {
                vec2 q = p.xy() + n * o;
                float rz = 0;
                bool rd = roads.surfaceHeight(q, &rz, p.z + 6.f);
                printf("   %+5.0f: %6.2f %8.2f %s%.2f%s\n", o, map.heightAt(q.x, q.y), map.waterAt(q.x, q.y), rd ? "" : "(", rd ? rz : 0.f, rd ? "" : ")");
            }
        }
    }
    Jobs::shutdown();
    return 0;
}
