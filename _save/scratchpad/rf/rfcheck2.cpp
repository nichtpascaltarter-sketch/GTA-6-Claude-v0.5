// rfcheck2: rfcheck plus the terrain's hashes. rfcheck: a census of the generated world for the road-overlap fix (city agent's scratch tool, not for the repo).
// Builds the world as the game does (map, roads, buildings with interiors, sites, transit) and prints the road network,
// every edge, every building, the bus stops, the site places and the story places (mission_util.cpp's resolvers,
// replicated here on the world data), one line each, for a diff between two roads.cpp versions.
// Build from a tree root: g++ -O2 -std=c++17 -I. -I src [-DRF_BASE] rfcheck.cpp -o rfcheck -lpthread
#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/core/noise.cpp"
#include "src/core/jobs.cpp"
#include "src/render/mesh.cpp"
#include "src/world/worldmap.cpp"
#ifdef RF_BASE
#include "src/world/roads_base.cpp"
#else
#include "src/world/roads.cpp"
#endif
#include "src/world/roadmesh.cpp"
#include "src/world/buildings.cpp"
#include "src/world/buildmesh.cpp"
#include "src/world/propmesh.cpp"
#include "src/world/cellgen.cpp"
#include "src/world/sites.cpp"
#include "src/world/transit.cpp"
#include "src/world/sitegeo.cpp"
#include "src/world/airport.cpp"
#include "src/world/port.cpp"
#include "src/world/landmarks.cpp"
#include "src/world/leisure.cpp"
#include "src/world/rural.cpp"
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
#include <thread>
#include <cstdio>

using namespace World;

namespace rfc {

RoadNetwork* R = nullptr;
BuildingSet* BS = nullptr;

// ---- mission_util.cpp's place resolvers, on the world data (z left out: the census compares x, y)
struct Place {
    vec2 pos, door, dir, out;
    int edge = -1;
    float s = 0.f;
};
bool driveable(const RoadEdge& e) { return !(e.flags & RF_ELEVATED) && e.cls != RC_HIGHWAY && e.cls != RC_RAMP; }
int nearestStreet(vec2 p, float maxDist, float* sOut, float* sideOut, bool allowBridge) {
    std::vector<int> cand;
    R->edgesInRect(p - vec2(maxDist), p + vec2(maxDist), cand);
    int best = -1;
    float bestD = maxDist, bestS = 0.f, bestSide = 1.f;
    for (int ei : cand) {
        const RoadEdge& e = R->edges[ei];
        if (!driveable(e) || (!allowBridge && (e.flags & RF_BRIDGE))) continue;
        float acc = 0.f;
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            vec2 a = e.pts[k].xy(), b = e.pts[k + 1].xy();
            float t, d = distPointSegment2D(p, a, b, &t), seg = length(b - a);
            if (d < bestD) {
                bestD = d;
                best = ei;
                bestS = acc + t * seg;
                bestSide = cross(normalize(b - a), p - a) >= 0.f ? 1.f : -1.f;
            }
            acc += seg;
        }
    }
    if (best < 0) return R->nearestEdge(p, maxDist * 2.f, sOut, nullptr, sideOut);
    *sOut = bestS;
    *sideOut = bestSide;
    return best;
}
Place resolvePlace(vec2 hint, float along = 0.f, bool allowBridge = false) {
    Place pl;
    float s = 0.f, side = 1.f;
    int e = nearestStreet(hint, 260.f, &s, &side, allowBridge);
    pl.edge = e;
    if (e < 0) {
        pl.pos = pl.door = hint;
        return pl;
    }
    const RoadEdge& ed = R->edges[e];
    float lo = Min(ed.cut0 + 3.f, ed.length * 0.5f), hi = Max(ed.length - ed.cut1 - 3.f, ed.length * 0.5f);
    s = Clamp(s + along, lo, hi);
    pl.s = s;
    vec3 c = ed.posAt(s), t = ed.tangentAt(s);
    vec2 dir = normalize(vec2(t.x, t.y)), out = perp(dir) * side;
    pl.dir = dir;
    pl.out = out;
    float walk = ed.halfWidth + Max(ed.sidewalk, 1.2f) * 0.5f;
    pl.pos = c.xy() + out * walk;
    pl.door = pl.pos + out * 4.5f;
    return pl;
}
bool hasFrontage(const Place& p) {
    if (p.edge < 0) return false;
    if (BS->pointInBuilding(p.door, 0.3f)) return false;
    for (float d = 1.5f; d <= 7.5f; d += 1.5f)
        if (BS->pointInBuilding(p.door + p.out * d, 0.f)) return true;
    return false;
}
Place resolveFrontage(vec2 hint, float along = 0.f) {
    Place base = resolvePlace(hint, along);
    if (hasFrontage(base)) return base;
    Place best = base;
    float bestD = 1e9f;
    for (float r = 10.f; r <= 220.f && bestD > 1e8f; r += 10.f) {
        int n = Max(6, (int)(r * kTwoPi / 12.f));
        for (int i = 0; i < n; i++) {
            float a = kTwoPi * i / n;
            Place p = resolvePlace(hint + vec2(cosf(a), sinf(a)) * r);
            if (!hasFrontage(p)) continue;
            float d = length(p.pos - hint);
            if (d < bestD) {
                bestD = d;
                best = p;
            }
        }
    }
    return best;
}
bool placeFromInterior(const char* name, int kind, Place& out) {
    if (!gInteriors) return false;
    int i = name ? gInteriors->byName(name) : gInteriors->byKind((u8)kind, 0);
    if (i < 0) return false;
    const InteriorDef& d = gInteriors->defs[i];
    const InteriorMarker* m = d.marker(IM_DOOR_OUT);
    vec3 door = m ? d.toWorld(m->pos) : d.toWorld(vec3(0.f, -2.f, 0.f));
    Place p = resolvePlace(door.xy());
    if (p.edge < 0) return false;
    p.door = door.xy();
    out = p;
    return true;
}
Place resolveBuilding(vec2 hint, int kind, const char* name = nullptr) {
    Place p;
    if (placeFromInterior(name, kind, p)) return p;
    return resolveFrontage(hint);
}

void printPlace(const char* name, const Place& p) {
    const RoadEdge* e = p.edge >= 0 ? &R->edges[p.edge] : nullptr;
    printf("P %-20s pos %.1f %.1f door %.1f %.1f edge (%s) s %.1f\n", name, p.pos.x, p.pos.y, p.door.x, p.door.y, e ? e->name.c_str() : "-", p.s);
}

}  // namespace rfc

int main() {
    using namespace rfc;
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    static WorldMap map;
    map.generate();
    gMap = &map;
    static RoadNetwork roads;
    roads.generate(map);
    gRoads = &roads;
    static BuildingSet bs;
    bs.generate(map, roads);
    gBuildings = &bs;
    R = &roads;
    BS = &bs;
    int lights = 0;
    for (const RoadNode& n : roads.nodes) lights += n.control == 2;
    printf("N nodes %zu edges %zu signalized %d buildings %zu\n", roads.nodes.size(), roads.edges.size(), lights, bs.buildings.size());
    {   // the terrain as the generator left it (heights, water, splat): a hash each
        auto fnv = [](const void* p, size_t n) {
            unsigned long long h = 1469598103934665603ull;
            const unsigned char* c = (const unsigned char*)p;
            for (size_t i = 0; i < n; i++) h = (h ^ c[i]) * 1099511628211ull;
            return h;
        };
        printf("H height %016llx water %016llx splat %016llx %016llx\n", fnv(map.height.data(), map.height.size() * 4), fnv(map.waterLevel.data(), map.waterLevel.size() * 4),
               fnv(map.splat0.data(), map.splat0.size() * 4), fnv(map.splat1.data(), map.splat1.size() * 4));
    }
    for (size_t i = 0; i < roads.nodes.size(); i++) {
        const RoadNode& n = roads.nodes[i];
        printf("V %zu %.1f %.1f z %.1f deg %zu ctl %d\n", i, n.p.x, n.p.y, n.z, n.edges.size(), (int)n.control);
    }
    for (size_t i = 0; i < roads.edges.size(); i++) {
        const RoadEdge& e = roads.edges[i];
        vec2 a = e.pts.front().xy(), b = e.pts.back().xy();
        printf("E %zu %s|cls %d|%.1f %.1f -> %.1f %.1f|len %.1f|seed %08x|lanes %d/%d\n", i, e.name.c_str(), (int)e.cls, a.x, a.y, b.x, b.y, e.length, e.seed, e.lanesF,
               e.lanesB);
    }
    for (size_t i = 0; i < bs.buildings.size(); i++) {
        const Building& b = bs.buildings[i];
        printf("B %.1f %.1f %.1f %.1f ax %.2f %.2f style %d h %.1f seed %08x\n", b.c.x, b.c.y, b.hx, b.hy, b.ax.x, b.ax.y, (int)b.style, b.height, b.seed);
    }
    if (gTransit) {
        for (size_t i = 0; i < gTransit->busStops.size(); i++) {
            const BusStop& s = gTransit->busStops[i];
            printf("S %s|%.1f %.1f|routes %x|seed %08x\n", s.name.c_str(), s.pos.x, s.pos.y, s.routeMask, s.seed);
        }
        // each bus route: its legs (edge and direction) and its stops in service order
        for (const BusRoute& r : gTransit->busRoutes) {
            printf("R %s len %.0f legs", r.number.c_str(), r.length);
            for (const RouteLeg& l : r.legs) printf(" %d%c", l.edge, l.dir > 0 ? '+' : '-');
            printf("\n");
            for (size_t i = 0; i < r.stops.size(); i++) {
                const BusStop& s = gTransit->busStops[(size_t)r.stops[i]];
                printf("T %s %zu %s|%.1f %.1f|d %.0f|edge %d\n", r.number.c_str(), i, s.name.c_str(), s.pos.x, s.pos.y, i < r.stopDist.size() ? r.stopDist[i] : -1.f, s.edge);
            }
        }
    }
    if (gSites)
        for (size_t i = 0; i < gSites->places.size(); i++) {
            const NamedPlace& P = gSites->places[i];
            printf("Q %zu %s|%.1f %.1f|door %.1f %.1f\n", i, P.name.c_str(), P.pos.x, P.pos.y, P.door.x, P.door.y);
        }
    if (gInteriors)
        for (size_t i = 0; i < gInteriors->defs.size(); i++) {
            const InteriorDef& d = gInteriors->defs[i];
            vec3 o = d.toWorld(vec3(0.f));
            printf("I %s|kind %d|%.1f %.1f\n", d.name.c_str(), (int)d.kind, o.x, o.y);
        }
    // the story places of mission_util.cpp computePlaces (the building and street ones)
    printPlace("mariApt", resolveBuilding(vec2(1720, 360), IK_APARTMENT));
    printPlace("boatyard", resolvePlace(vec2(1650, 235)));
    printPlace("diner", resolveBuilding(vec2(1330, 330), IK_DINER));
    printPlace("vargasGarage", resolveFrontage(vec2(2200, -480)));
    printPlace("stashHouse", resolveFrontage(vec2(1150, -620)));
    printPlace("solarisPier", resolvePlace(vec2(2560, 330)));
    printPlace("rookShop", resolveBuilding(vec2(1560, 2260), IK_CHOPSHOP));
    printPlace("dexTrailer", resolveBuilding(vec2(1250, 2650), IK_TRAILER));
    printPlace("pulseFm", resolveFrontage(vec2(3350, 640)));
    printPlace("policeHq", resolveBuilding(vec2(3050, -350), IK_POLICE));
    printPlace("solarisOne", resolvePlace(vec2(3350, -810)));
    printPlace("sandovalOffice", resolveFrontage(vec2(3700, -1000)));
    printPlace("palmMotors", resolveBuilding(vec2(2700, 1300), IK_DEALERSHIP, "Palm Motors"));
    printPlace("gunFlats", resolveBuilding(vec2(800, 2900), IK_GUNSHOP, "Palmetto Arms"));
    printPlace("gunNorth", resolveBuilding(vec2(3300, 3900), IK_GUNSHOP, "Northside Arms"));
    printPlace("resprayCL", resolveBuilding(vec2(2300, -150), IK_MODSHOP, "Tide Customs Calle Luna"));
    printPlace("resprayBeach", resolveBuilding(vec2(5100, 2000), IK_MODSHOP, "Tide Customs Sol Beach"));
    printPlace("threads", resolveBuilding(vec2(5150, -300), IK_CLOTHES, "Threads"));
    printPlace("clubRiptide", resolvePlace(vec2(5380, 900)));
    printPlace("beachCondo", resolveFrontage(vec2(5120, 1500)));
    printPlace("beachPier", resolvePlace(vec2(5380, 1250)));
    printPlace("portGate", resolvePlace(vec2(4060, -200)));
    printPlace("keyCoral", resolveFrontage(vec2(4450, -4200)));
    {
        vec2 hint(4200, -3900);
        if (gSites)
            for (const SiteElem& e : gSites->elems)
                if (e.kind == SK_MARINA) {
                    hint = e.c - e.ax * 50.f;
                    break;
                }
        printPlace("keyCoralMarina", resolvePlace(hint));
    }
    printPlace("sandovalMansion", resolvePlace(vec2(4300, 1300), 0.f, true));
    printPlace("airport", resolvePlace(vec2(760, 1200)));
    printPlace("carwash", resolveBuilding(vec2(1800, 1500), IK_CARWASH, "Sunwash Car Wash"));
    printPlace("taxiDepot", resolveFrontage(vec2(2600, 700)));
    printPlace("courierDepot", resolveFrontage(vec2(1700, -300)));
    printPlace("hospital", resolveBuilding(vec2(1650, 1050), IK_HOSPITAL));
    printPlace("raceCalle", resolvePlace(vec2(1100, -850)));
    printPlace("raceBeach", resolvePlace(vec2(5150, -2300)));
    printPlace("raceHighway", resolvePlace(vec2(300, -6000)));
    printPlace("raceGrove", resolvePlace(vec2(1500, -2500)));
    printPlace("raceKeys", resolvePlace(vec2(-7700, -9150)));
    printPlace("rangeFlats", resolvePlace(vec2(700, 3050)));
    printPlace("overseasStart", resolvePlace(vec2(200, -6100)));
    printPlace("keySolano", resolvePlace(vec2(-7900, -9200)));
    printPlace("sawgrassRoad", resolvePlace(vec2(-5060, 170), 0.f, true));
    printPlace("tenPalms", resolvePlace(vec2(-8600, -2300)));
    printPlace("lakeTown", resolvePlace(vec2(250, 6750)));
    printPlace("fortCastell", resolvePlace(vec2(4200, 7600)));
    printPlace("northCity", resolvePlace(vec2(2600, 4300)));
    printPlace("grove", resolvePlace(vec2(2000, -3500)));
    printPlace("redland", resolveFrontage(vec2(800, -5400)));
    printPlace("harlow", resolvePlace(vec2(-5000, 4850)));
    printPlace("midtownPark", resolvePlace(vec2(3100, 1600)));
    printPlace("cafeBeach", resolveFrontage(vec2(5200, 300)));
    printPlace("flatsYard", resolvePlace(vec2(400, 3700)));
    printPlace("kitStudio", resolveFrontage(vec2(600, 3400)));
    printPlace("downtownPenthouse", resolveBuilding(vec2(3528, -706), IK_CONDO, "Downtown Penthouse"));
    printPlace("stadium", resolvePlace(vec2(3565, 620)));
    fflush(stdout);
    Jobs::shutdown();
    return 0;
}
