#include "buildings.h"
#include "sites.h"
#include "places.h"
#include "interiors.h"
#include "transit.h"
#include "../core/noise.h"
#include "../render/mesh.h"
#include <unordered_map>

namespace World {

BuildingSet* gBuildings = nullptr;

namespace buildings_detail {

struct OBB2 {
    vec2 c, ax;
    float hx, hy;
};

bool obbOverlap(const OBB2& a, const OBB2& b) {
    vec2 axes[4] = {a.ax, perp(a.ax), b.ax, perp(b.ax)};
    vec2 d = b.c - a.c;
    for (vec2 L : axes) {
        float ra = a.hx * fabsf(dot(a.ax, L)) + a.hy * fabsf(dot(perp(a.ax), L));
        float rb = b.hx * fabsf(dot(b.ax, L)) + b.hy * fabsf(dot(perp(b.ax), L));
        if (fabsf(dot(d, L)) > ra + rb) return false;
    }
    return true;
}

struct LotHash {
    float cell = 48.f;
    std::unordered_map<long long, std::vector<int>> map;
    std::vector<OBB2> boxes;
    long long key(int x, int y) const { return (long long)(y + 100000) * 400000LL + (x + 100000); }
    bool overlaps(const OBB2& o) const {
        float r = sqrtf(o.hx * o.hx + o.hy * o.hy);
        int x0 = (int)floorf((o.c.x - r) / cell), x1 = (int)floorf((o.c.x + r) / cell);
        int y0 = (int)floorf((o.c.y - r) / cell), y1 = (int)floorf((o.c.y + r) / cell);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                auto it = map.find(key(x, y));
                if (it == map.end()) continue;
                for (int i : it->second)
                    if (obbOverlap(o, boxes[i])) return true;
            }
        return false;
    }
    void add(const OBB2& o) {
        int id = (int)boxes.size();
        boxes.push_back(o);
        float r = sqrtf(o.hx * o.hx + o.hy * o.hy);
        int x0 = (int)floorf((o.c.x - r) / cell), x1 = (int)floorf((o.c.x + r) / cell);
        int y0 = (int)floorf((o.c.y - r) / cell), y1 = (int)floorf((o.c.y + r) / cell);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) map[key(x, y)].push_back(id);
    }
};

u32 rgb8(float r, float g, float b) { return packRGBA8(r, g, b, 1.f); }

// Palettes (linear-ish tints multiplied onto near-white wall albedo)
const vec3 kPastels[] = {vec3(1.0f, 0.78f, 0.68f), vec3(0.72f, 0.95f, 0.85f), vec3(1.0f, 0.95f, 0.7f), vec3(0.72f, 0.86f, 1.0f),
                         vec3(1.0f, 0.8f, 0.88f), vec3(0.88f, 0.8f, 1.0f), vec3(1.0f, 1.0f, 0.98f), vec3(0.95f, 0.9f, 0.82f),
                         vec3(0.7f, 0.95f, 0.95f), vec3(1.0f, 0.88f, 0.72f)};
const vec3 kBright[] = {vec3(1.0f, 0.55f, 0.2f), vec3(1.0f, 0.85f, 0.25f), vec3(0.2f, 0.75f, 0.75f), vec3(1.0f, 0.45f, 0.6f),
                        vec3(0.55f, 0.85f, 0.3f), vec3(0.35f, 0.55f, 0.95f), vec3(0.95f, 0.35f, 0.3f), vec3(0.9f, 0.9f, 0.85f)};
const vec3 kNeutral[] = {vec3(0.9f, 0.88f, 0.84f), vec3(0.8f, 0.78f, 0.74f), vec3(0.65f, 0.63f, 0.6f), vec3(0.95f, 0.93f, 0.88f),
                         vec3(0.75f, 0.72f, 0.66f), vec3(0.55f, 0.55f, 0.56f)};
const vec3 kGlass[] = {vec3(0.55f, 0.75f, 0.85f), vec3(0.5f, 0.8f, 0.75f), vec3(0.6f, 0.62f, 0.65f), vec3(0.75f, 0.62f, 0.45f),
                       vec3(0.45f, 0.6f, 0.8f), vec3(0.7f, 0.8f, 0.8f)};

const char* kFirst[] = {"Luna", "Rosa", "Tony", "Maria", "Carlos", "Sal", "Dee", "Bobby", "Marisol", "Ana", "Rico", "Nena", "Lupe",
                        "Frankie", "Vic", "Gloria", "Manny", "Tess", "Ruby", "Hector", "Iris", "Omar", "Pearl", "Gus", "Nico", "Dolores",
                        "Benny", "Cora", "Rafa", "Sonia", "Walt", "Yoli"};
const char* kBiz[] = {"Bakery", "Cafe", "Deli", "Barbershop", "Laundromat", "Pharmacy", "Tacos", "Pizza", "Liquors", "Hardware",
                      "Salon", "Jewelry", "Pawn", "Cigars", "Florist", "Books", "Records", "Bodega", "Grill", "Diner", "Market",
                      "Tattoo", "Gym", "Dental", "Insurance", "Travel", "Electronics", "Shoes", "Boutique", "Furniture", "Auto Parts",
                      "Tailor", "Seafood", "Juice Bar", "Donuts", "Nails", "Optical", "Pet Shop", "Vape", "Check Cashing"};
const char* kAdj[] = {"Golden", "Blue", "Sunny", "Neon", "Tropical", "Coral", "Island", "Palm", "Ocean", "Sunset", "Magic", "Royal",
                      "Lucky", "Silver", "Salty", "Pink", "Midnight", "Paradise", "Breeze", "Tidal", "Flamingo", "Pelican"};
const char* kNoun[] = {"Palm", "Marlin", "Wave", "Moon", "Star", "Shell", "Reef", "Harbor", "Pearl", "Lagoon", "Parrot", "Mango",
                       "Dolphin", "Sands", "Tide", "Heron", "Gator", "Orchid"};
const char* kChains[] = {"TIDESTOP", "BURGER BAY", "TACO TIDE", "FRESHMART", "MEGASAVE", "PALM PHARMACY", "BAYSIDE BANK",
                         "PELICAN PIZZA", "WASH N GO", "PALMCELL", "SUNRAY CREDIT", "FLAMINGO LAUNDRY", "CASH CRAB", "GATOR GAS",
                         "CAFE CUBANITO", "LA ESQUINA", "EL FARO", "CASA FRESCA", "PANADERIA SOL", "FARMACIA LUZ"};

std::string makeSignName(Rng& r) {
    int t = r.irange(0, 9);
    char buf[64];
    if (t <= 3) snprintf(buf, sizeof(buf), "%s'S %s", kFirst[r.next() % ARRAY_COUNT(kFirst)], kBiz[r.next() % ARRAY_COUNT(kBiz)]);
    else if (t <= 6) snprintf(buf, sizeof(buf), "%s %s %s", kAdj[r.next() % ARRAY_COUNT(kAdj)], kNoun[r.next() % ARRAY_COUNT(kNoun)], kBiz[r.next() % ARRAY_COUNT(kBiz)]);
    else snprintf(buf, sizeof(buf), "%s", kChains[r.next() % ARRAY_COUNT(kChains)]);
    std::string s = buf;
    for (auto& ch : s) ch = (char)toupper((unsigned char)ch);
    return s;
}

}  // namespace buildings_detail

using namespace buildings_detail;

void BuildingSet::generate(WorldMap& map, const RoadNetwork& roads) {
    double t0 = TimeSeconds();
    buildings.clear();
    facades.clear();
    signNames.clear();
    LotHash lots;
    Rng signRng(0xC0FFEEu);
    for (int i = 0; i < 512; i++) signNames.push_back(makeSignName(signRng));

    // Turning bulbs: roadmesh.cpp paves a circle of halfWidth + 4.5 m (plus its sidewalk ring) around every dead end of a
    // lane, street or rural road (RoadNetwork::bulbRadius), so lots must stay clear of them (with a 1 m margin)
    struct Bulb { vec2 c; float r; };
    std::vector<Bulb> bulbs;
    for (const RoadNode& nd : roads.nodes) {
        float br = roads.bulbRadius(nd);
        if (br <= 0.f) continue;
        bulbs.push_back({nd.p, br + roads.edges[nd.edges[0]].sidewalk + 1.0f});
    }
    const float kBulbCell = 64.f;
    auto bulbKey = [](int x, int y) { return (long long)(y + 100000) * 400000LL + (x + 100000); };
    std::unordered_map<long long, std::vector<int>> bulbGrid;
    for (size_t i = 0; i < bulbs.size(); i++) {
        const Bulb& bb = bulbs[i];
        int x0 = (int)floorf((bb.c.x - bb.r) / kBulbCell), x1 = (int)floorf((bb.c.x + bb.r) / kBulbCell);
        int y0 = (int)floorf((bb.c.y - bb.r) / kBulbCell), y1 = (int)floorf((bb.c.y + bb.r) / kBulbCell);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) bulbGrid[bulbKey(x, y)].push_back((int)i);
    }
    auto hitsBulb = [&](const OBB2& o) {
        float rr = sqrtf(o.hx * o.hx + o.hy * o.hy);
        int x0 = (int)floorf((o.c.x - rr) / kBulbCell), x1 = (int)floorf((o.c.x + rr) / kBulbCell);
        int y0 = (int)floorf((o.c.y - rr) / kBulbCell), y1 = (int)floorf((o.c.y + rr) / kBulbCell);
        vec2 oy = perp(o.ax);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                auto it = bulbGrid.find(bulbKey(x, y));
                if (it == bulbGrid.end()) continue;
                for (int bi : it->second) {
                    const Bulb& bb = bulbs[bi];
                    vec2 d = bb.c - o.c;
                    float qx = Max(fabsf(dot(d, o.ax)) - o.hx, 0.f), qy = Max(fabsf(dot(d, oy)) - o.hy, 0.f);
                    if (qx * qx + qy * qy < bb.r * bb.r) return true;
                }
            }
        return false;
    };

    // Exact lot / road test: no road polyline may come within its paved width + sidewalk + margin of the lot rectangle
    // (the eight probe points above miss ramps and curves that cut through a large lot between them)
    auto lotHitsRoad = [&](const OBB2& o, int ownEdge) {
        thread_local std::vector<int> cand;
        float rr = sqrtf(o.hx * o.hx + o.hy * o.hy);
        roads.edgesInRect(o.c - vec2(rr + 35.f), o.c + vec2(rr + 35.f), cand);
        vec2 oy = perp(o.ax);
        auto toLocal = [&](vec2 q) { vec2 d = q - o.c; return vec2(dot(d, o.ax), dot(d, oy)); };
        auto rectDist = [&](vec2 l) { float qx = Max(fabsf(l.x) - o.hx, 0.f), qy = Max(fabsf(l.y) - o.hy, 0.f); return sqrtf(qx * qx + qy * qy); };
        for (int oi : cand) {
            const RoadEdge& oe = roads.edges[oi];
            float clear = oe.halfWidth + oe.sidewalk + (oi == ownEdge ? 0.3f : 0.8f);
            for (size_t k = 0; k + 1 < oe.pts.size(); k++) {
                vec2 A = toLocal(oe.pts[k].xy()), B = toLocal(oe.pts[k + 1].xy());
                // quick reject: segment bounding box farther than `clear` from the rectangle
                if (Min(A.x, B.x) > o.hx + clear || Max(A.x, B.x) < -o.hx - clear || Min(A.y, B.y) > o.hy + clear || Max(A.y, B.y) < -o.hy - clear) continue;
                // segment crossing the rectangle (slab test)
                vec2 d = B - A;
                float t0 = 0.f, t1 = 1.f;
                bool inside = true;
                for (int ax = 0; ax < 2 && inside; ax++) {
                    float p0 = ax == 0 ? A.x : A.y, dd = ax == 0 ? d.x : d.y, h = ax == 0 ? o.hx : o.hy;
                    if (fabsf(dd) < 1e-6f) { if (p0 < -h || p0 > h) inside = false; continue; }
                    float ta = (-h - p0) / dd, tb = (h - p0) / dd;
                    if (ta > tb) std::swap(ta, tb);
                    t0 = Max(t0, ta);
                    t1 = Min(t1, tb);
                    if (t0 > t1) inside = false;
                }
                if (inside) return true;
                float dist = Min(rectDist(A), rectDist(B));
                const vec2 cs[4] = {vec2(-o.hx, -o.hy), vec2(o.hx, -o.hy), vec2(o.hx, o.hy), vec2(-o.hx, o.hy)};
                for (vec2 c : cs) dist = Min(dist, distPointSegment2D(c, A, B));
                if (dist < clear) return true;
            }
        }
        return false;
    };

    // Reserve special areas: airport runways and port yard are left free of lots
    auto reserved = [&](vec2 p) {
        Region r = map.regionAt(p.x, p.y);
        return r == REG_AIRPORT || r == REG_OCEAN || r == REG_SAWGRASS || r == REG_RIDGE || gSites->blocksLots(p);
    };

    for (size_t ei = 0; ei < roads.edges.size(); ei++) {
        const RoadEdge& e = roads.edges[ei];
        if (e.cls == RC_HIGHWAY || e.cls == RC_RAMP || e.cls == RC_DIRT) continue;
        if (e.flags & RF_BRIDGE) {
            // skip edges that are mostly bridge deck
            vec3 mid = e.posAt(e.length * 0.5f);
            if (mid.z - map.heightAt(mid.x, mid.y) > 2.f) continue;
        }
        Rng rng(e.seed ^ 0x9E3779B9u);
        for (int side = -1; side <= 1; side += 2) {
            float s = e.cut0 + 4.f;
            float limit = e.length - e.cut1 - 4.f;
            while (s < limit) {
                vec3 p3 = e.posAt(Min(s + 10.f, limit));
                vec2 probe = p3.xy();
                Region reg = map.regionAt(probe.x, probe.y);
                const RegionInfo& ri = regionInfo(reg);
                // Lot dimensions by district
                float wMin = 18, wMax = 26, dMin = 28, dMax = 36, setback = 2.f, gap = 1.f;
                bool rural = false;
                switch (reg) {
                    case REG_DOWNTOWN: wMin = 32; wMax = 62; dMin = 34; dMax = 42; setback = 1.0f; gap = 0.5f; break;
                    case REG_FINANCIAL: wMin = 36; wMax = 66; dMin = 36; dMax = 44; setback = 2.5f; gap = 2.f; break;
                    case REG_MIDTOWN: wMin = 16; wMax = 40; dMin = 26; dMax = 40; setback = 0.8f; gap = 0.3f; break;
                    case REG_NORTH_CITY: wMin = 14; wMax = 26; dMin = 22; dMax = 34; setback = 3.f; gap = 2.f; break;
                    case REG_CALLE_LUNA: wMin = 10; wMax = 22; dMin = 20; dMax = 32; setback = 0.6f; gap = 0.2f; break;
                    case REG_BEACH: wMin = 20; wMax = 42; dMin = 26; dMax = 40; setback = 2.f; gap = 1.f; break;
                    case REG_BAY_ISLAND: wMin = 42; wMax = 60; dMin = 40; dMax = 55; setback = 8.f; gap = 8.f; break;
                    case REG_KEY_CORAL: wMin = 30; wMax = 50; dMin = 34; dMax = 46; setback = 7.f; gap = 5.f; break;
                    case REG_PORT: wMin = 60; wMax = 110; dMin = 40; dMax = 60; setback = 6.f; gap = 10.f; break;
                    case REG_GROVE: wMin = 26; wMax = 42; dMin = 32; dMax = 44; setback = 7.f; gap = 3.f; break;
                    case REG_FLATS: wMin = 18; wMax = 60; dMin = 24; dMax = 44; setback = 3.f; gap = 2.f; break;
                    case REG_SUBURBS: wMin = 19; wMax = 24; dMin = 30; dMax = 36; setback = 7.f; gap = 1.5f; break;
                    case REG_FORT_CASTELL: wMin = 16; wMax = 50; dMin = 24; dMax = 40; setback = 3.f; gap = 2.f; break;
                    case REG_KEY_TOWN: case REG_LAKE_TOWN: case REG_HARLOW: case REG_GULF_TOWN:
                        wMin = 14; wMax = 26; dMin = 22; dMax = 32; setback = 3.f; gap = 3.f; break;
                    case REG_REDLAND: case REG_FARMLAND: case REG_KEYS:
                        wMin = 30; wMax = 50; dMin = 30; dMax = 45; setback = 14.f; gap = 60.f; rural = true; break;
                    default: wMin = 20; wMax = 30; dMin = 25; dMax = 35; setback = 4.f; gap = 4.f; break;
                }
                if (e.cls == RC_RURAL && !rural) { setback += 4.f; gap += 20.f; }
                float w = rng.range(wMin, wMax);
                float d = rng.range(dMin, dMax);
                if (rural && rng.chance(0.75f)) { s += rng.range(120.f, 400.f); continue; }
                if (s + w > limit) break;
                float sc = s + w * 0.5f;
                vec3 P = e.posAt(sc);
                vec3 T = e.tangentAt(sc);
                vec2 t2 = normalize(T.xy());
                vec2 R(t2.y, -t2.x);
                vec2 out = R * (float)side;
                float off = e.halfWidth + e.sidewalk + setback;
                OBB2 lot;
                lot.ax = t2;
                lot.hx = w * 0.5f;
                lot.hy = d * 0.5f;
                lot.c = P.xy() + out * (off + d * 0.5f);
                // Validity: region, water, other roads, overlap
                bool ok = !reserved(lot.c);
                vec2 corners[8];
                vec2 ay = perp(lot.ax);
                int k = 0;
                for (int sx = -1; sx <= 1; sx += 2)
                    for (int sy = -1; sy <= 1; sy += 2) corners[k++] = lot.c + lot.ax * (sx * lot.hx) + ay * (sy * lot.hy);
                corners[4] = lot.c;
                corners[5] = lot.c + out * lot.hy;
                corners[6] = lot.c + lot.ax * lot.hx;
                corners[7] = lot.c - lot.ax * lot.hx;
                for (int ci = 0; ci < 8 && ok; ci++) {
                    vec2 q = corners[ci];
                    if (map.isWater(q.x, q.y) || reserved(q)) { ok = false; break; }
                    float ds, dd, dside;
                    int ne = roads.nearestEdge(q, 40.f, &ds, &dd, &dside);
                    if (ne >= 0) {
                        const RoadEdge& o = roads.edges[ne];
                        float clear = o.halfWidth + o.sidewalk + 0.8f;
                        if (ne != (int)ei && dd < clear) ok = false;
                        if (ne == (int)ei && dd < e.halfWidth + e.sidewalk + 0.3f) ok = false;
                    }
                }
                if (ok && lots.overlaps(lot)) ok = false;
                if (ok && lotHitsRoad(lot, (int)ei)) ok = false;
                if (ok && hitsBulb(lot)) ok = false;
                if (!ok) {
                    s += 6.f;
                    continue;
                }
                lots.add(lot);
                // ----------------------------------------------------------- building spec
                Building b;
                b.seed = hashCombine(e.seed, (u32)(s * 13.f) + (side > 0 ? 7u : 0u));
                Rng br(b.seed);
                b.region = (u8)reg;
                b.lotC = lot.c;
                b.lotHy = lot.hy;
                b.lotHx = lot.hx;
                b.ax = lot.ax;
                b.front = -out;
                b.lotKind = 0;
                // Choose style
                BuildingStyle st = BS_HOUSE;
                float r = br.f();
                bool mainRoad = e.cls <= RC_AVENUE;
                switch (reg) {
                    case REG_DOWNTOWN: st = r < 0.72f ? BS_TOWER : (r < 0.9f ? BS_MIDRISE : BS_GARAGE); break;
                    case REG_FINANCIAL: st = r < 0.85f ? BS_TOWER : BS_CONDO; break;
                    case REG_MIDTOWN: st = r < 0.45f ? BS_SHOPS : (r < 0.7f ? BS_MIDRISE : (r < 0.85f ? BS_WAREHOUSE : BS_CONDO)); break;
                    case REG_NORTH_CITY: st = mainRoad ? (r < 0.6f ? BS_SHOPS : BS_MIDRISE) : (r < 0.6f ? BS_HOUSE : (r < 0.85f ? BS_MIDRISE : BS_SHOPS)); break;
                    case REG_CALLE_LUNA: st = mainRoad ? BS_SHOPS : (r < 0.5f ? BS_SHOPS : (r < 0.8f ? BS_MIDRISE : BS_HOUSE)); break;
                    case REG_BEACH: {
                        bool ocean = e.name == "Ocean Promenade";
                        bool collins = e.name == "Collins-Solano Avenue";
                        if (ocean) st = BS_DECO;
                        else if (collins) st = r < 0.55f ? BS_TOWER : (r < 0.8f ? BS_CONDO : BS_DECO);
                        else st = r < 0.55f ? BS_DECO : (r < 0.8f ? BS_CONDO : BS_SHOPS);
                        break;
                    }
                    case REG_BAY_ISLAND: st = BS_VILLA; break;
                    case REG_KEY_CORAL: {
                        // exclusive island: villas throughout, a few low boutique condos only on beachfront lots
                        bool beachfront = map.coastDistance(lot.c.x, lot.c.y) < 140.f;
                        st = (beachfront && r < 0.22f) ? BS_CONDO : BS_VILLA;
                        break;
                    }
                    case REG_PORT: st = BS_WAREHOUSE; break;
                    case REG_GROVE: st = r < 0.45f ? BS_VILLA : (mainRoad && r < 0.6f ? BS_SHOPS : BS_HOUSE); break;
                    case REG_FLATS: st = r < 0.4f ? BS_WAREHOUSE : (r < 0.55f ? BS_FACTORY : (mainRoad ? BS_SHOPS : BS_HOUSE)); break;
                    case REG_SUBURBS:
                        st = (e.cls == RC_BOULEVARD) ? (r < 0.45f ? BS_STRIPMALL : (r < 0.6f ? BS_GASSTATION : (r < 0.7f ? BS_CHURCH : BS_SHOPS))) : BS_HOUSE;
                        break;
                    case REG_FORT_CASTELL: st = r < 0.35f ? BS_WAREHOUSE : (r < 0.5f ? BS_FACTORY : (mainRoad ? BS_SHOPS : BS_HOUSE)); break;
                    case REG_KEY_TOWN: st = mainRoad ? (r < 0.6f ? BS_SHOPS : BS_DECO) : (r < 0.6f ? BS_HOUSE : BS_MOTEL); break;
                    // (a church every thirty-odd houses; Okahatchee's own church and churchyard are a hand-built place)
                    case REG_LAKE_TOWN: case REG_HARLOW: st = mainRoad ? BS_SHOPS : (r < 0.97f ? BS_HOUSE : BS_CHURCH); break;
                    case REG_GULF_TOWN: st = r < 0.6f ? BS_SHACK : BS_HOUSE; break;
                    case REG_REDLAND: case REG_FARMLAND: st = r < 0.6f ? BS_FARMHOUSE : BS_BARN; break;
                    case REG_KEYS: st = r < 0.5f ? BS_SHACK : BS_MOTEL; break;
                    default: st = BS_HOUSE; break;
                }
                if (mainRoad && (reg == REG_SUBURBS || reg == REG_NORTH_CITY || reg == REG_FLATS) && br.chance(0.06f)) st = BS_MOTEL;
                b.style = (u8)st;
                // Footprint within the lot
                float fx = lot.hx, fy = lot.hy;
                switch (st) {
                    case BS_HOUSE: fx = lot.hx * br.range(0.55f, 0.75f); fy = lot.hy * br.range(0.4f, 0.55f); break;
                    case BS_VILLA: fx = lot.hx * br.range(0.55f, 0.72f); fy = lot.hy * br.range(0.45f, 0.6f); break;
                    case BS_FARMHOUSE: fx = br.range(6.f, 9.f); fy = br.range(5.f, 7.f); break;
                    case BS_BARN: fx = br.range(8.f, 13.f); fy = br.range(6.f, 9.f); break;
                    case BS_SHACK: fx = br.range(3.5f, 6.f); fy = br.range(3.f, 5.f); break;
                    case BS_STRIPMALL: fx = lot.hx * 0.95f; fy = lot.hy * 0.35f; break;
                    case BS_GASSTATION: fx = lot.hx * 0.35f; fy = lot.hy * 0.25f; break;
                    case BS_TOWER: fx = lot.hx * 0.96f; fy = lot.hy * 0.94f; break;
                    case BS_CHURCH: fx = lot.hx * 0.45f; fy = lot.hy * 0.7f; break;
                    default: fx = lot.hx * br.range(0.9f, 0.98f); fy = lot.hy * br.range(0.85f, 0.97f); break;
                }
                b.hx = fx;
                b.hy = fy;
                // Place footprint: houses sit toward the street with a front yard; strip malls at the back
                float frontGap = lot.hy - fy;
                vec2 fc = lot.c;
                if (st == BS_HOUSE || st == BS_VILLA) fc = lot.c + b.front * (frontGap * 0.35f);
                else if (st == BS_STRIPMALL || st == BS_GASSTATION || st == BS_CHURCH) fc = lot.c - b.front * (frontGap * 0.9f);
                else if (st == BS_FARMHOUSE || st == BS_BARN || st == BS_SHACK) fc = lot.c + lot.ax * br.range(-lot.hx * 0.3f, lot.hx * 0.3f);
                else fc = lot.c + b.front * (frontGap * 0.95f);
                b.c = fc;
                // Height
                float minF = ri.minFloors, maxF = ri.maxFloors;
                int floors = 1;
                switch (st) {
                    case BS_TOWER: {
                        float t = powf(br.f(), 1.6f);
                        floors = (int)Lerp(Max(minF, 14.f), maxF, t);
                        // taller near the downtown/financial core
                        vec2 core(3300, -300);
                        float cd = length(lot.c - core);
                        floors = (int)(floors * Lerp(1.25f, 0.7f, Saturate(cd / 2500.f)));
                        if (reg == REG_BEACH) floors = br.irange(12, 32);
                        floors = Clamp(floors, 12, 85);
                        break;
                    }
                    case BS_MIDRISE: floors = br.irange((int)Max(3.f, minF), (int)Clamp(maxF, 4.f, 14.f)); break;
                    case BS_CONDO: floors = reg == REG_KEY_CORAL ? br.irange(4, 8) : br.irange(6, 24); break;
                    case BS_DECO: floors = br.irange(2, 5); break;
                    case BS_SHOPS: floors = br.irange(1, reg == REG_MIDTOWN ? 4 : 3); break;
                    case BS_STRIPMALL: case BS_GASSTATION: case BS_FARMHOUSE: case BS_SHACK: floors = 1; break;
                    case BS_HOUSE: floors = br.chance(0.35f) ? 2 : 1; break;
                    case BS_VILLA: floors = br.irange(2, 3); break;
                    case BS_WAREHOUSE: case BS_BARN: floors = 1; break;
                    case BS_FACTORY: floors = br.irange(1, 3); break;
                    case BS_MOTEL: floors = 2; break;
                    case BS_GARAGE: floors = br.irange(5, 9); break;
                    case BS_CHURCH: floors = 1; break;
                    default: floors = 1; break;
                }
                // Neighbours of the same style never repeat: the previous lot on this block face is the neighbour
                const Building* nb = nullptr;
                if (!buildings.empty()) {
                    const Building& pb = buildings.back();
                    if (pb.style == st && length(pb.lotC - lot.c) < lot.hx + pb.lotHx + 8.f) nb = &pb;
                }
                Rng vr(b.seed ^ 0x6E16B0A5u);
                if (nb) {
                    int d = floors - (int)nb->floors, sgn = vr.chance(0.5f) ? 1 : -1;
                    switch (st) {
                        case BS_TOWER:
                            if (abs(d) < Max(3, floors / 6)) floors = Clamp(floors + sgn * vr.irange(Max(4, floors / 5), Max(6, floors / 3)), 12, 85);
                            break;
                        case BS_MIDRISE: case BS_CONDO:
                            if (abs(d) < 2) floors = Max(3, floors + (floors <= 4 ? 1 : sgn) * vr.irange(2, 4));
                            break;
                        case BS_SHOPS: case BS_DECO:
                            if (d == 0 && vr.chance(0.6f)) floors = floors == 1 ? 2 : floors - 1;
                            break;
                        default: break;
                    }
                }
                b.floors = (u16)floors;
                // Facade record
                FacadeGPU f = {};
                f.seed = b.seed;
                f.signIndex = (float)(br.next() % 512);
                f.litFrac = br.range(0.35f, 0.75f);
                f.roomDepth = br.range(4.f, 7.f);
                u32 flags = 0;
                vec3 wall(0.9f), frame(0.2f), glass = kGlass[br.next() % ARRAY_COUNT(kGlass)];
                MaterialId wallMat = MAT_STUCCO;
                float floorH = 3.2f, groundH = 4.2f, bay = 3.2f, winW = 0.55f, winH = 0.55f, sill = 0.9f;
                int style = 0;
                switch (st) {
                    case BS_TOWER: {
                        bool curtain = br.chance(reg == REG_FINANCIAL ? 0.75f : 0.55f);
                        style = curtain ? 1 : (br.chance(0.3f) ? 2 : 0);
                        floorH = br.range(3.5f, 4.1f);
                        groundH = br.range(5.f, 7.f);
                        bay = curtain ? br.range(1.5f, 2.4f) : br.range(2.4f, 3.6f);
                        winW = br.range(0.5f, 0.75f);
                        winH = br.range(0.55f, 0.72f);
                        sill = floorH * 0.22f;
                        wall = kNeutral[br.next() % ARRAY_COUNT(kNeutral)];
                        wallMat = br.chance(0.5f) ? MAT_CONCRETE_PANEL : (br.chance(0.5f) ? MAT_MARBLE : MAT_STUCCO);
                        frame = br.chance(0.5f) ? vec3(0.12f, 0.13f, 0.14f) : vec3(0.55f, 0.57f, 0.6f);
                        flags |= 1u | 16u;  // storefront, office
                        if (br.chance(0.4f)) flags |= 2u;
                        if (reg == REG_BEACH) { flags &= ~16u; style = br.chance(0.5f) ? 3 : 1; wall = kPastels[br.next() % ARRAY_COUNT(kPastels)]; }
                        break;
                    }
                    case BS_MIDRISE:
                        style = br.chance(0.25f) ? 2 : 0;
                        floorH = br.range(3.0f, 3.5f);
                        groundH = br.range(3.8f, 5.f);
                        bay = br.range(2.6f, 4.2f);
                        wall = (reg == REG_CALLE_LUNA || reg == REG_MIDTOWN) ? kBright[br.next() % ARRAY_COUNT(kBright)] : kPastels[br.next() % ARRAY_COUNT(kPastels)];
                        if (br.chance(0.3f)) { wallMat = MAT_BRICK; wall = vec3(1.f); }
                        frame = br.chance(0.6f) ? vec3(0.9f) : vec3(0.15f);
                        flags |= br.chance(0.6f) ? 3u : 0u;
                        break;
                    case BS_CONDO:
                        style = 3;
                        floorH = br.range(3.0f, 3.3f);
                        groundH = br.range(4.5f, 6.f);
                        bay = br.range(3.5f, 5.5f);
                        winW = br.range(0.7f, 0.85f);
                        winH = 0.8f;
                        sill = 0.2f;
                        wall = br.chance(0.6f) ? vec3(0.95f, 0.95f, 0.93f) : kPastels[br.next() % ARRAY_COUNT(kPastels)];
                        frame = vec3(0.8f, 0.85f, 0.88f);
                        break;
                    case BS_DECO:
                        style = 6;
                        floorH = br.range(3.0f, 3.4f);
                        groundH = br.range(4.f, 4.8f);
                        bay = br.range(2.4f, 3.4f);
                        winW = br.range(0.45f, 0.6f);
                        wall = kPastels[br.next() % ARRAY_COUNT(kPastels)];
                        frame = kBright[br.next() % ARRAY_COUNT(kBright)];
                        flags |= 1u | 2u | 4u;
                        break;
                    case BS_SHOPS: case BS_STRIPMALL:
                        style = 0;
                        floorH = 3.2f;
                        groundH = br.range(4.0f, 5.0f);
                        bay = br.range(2.8f, 4.0f);
                        wall = (reg == REG_CALLE_LUNA || reg == REG_KEY_TOWN) ? kBright[br.next() % ARRAY_COUNT(kBright)] : kPastels[br.next() % ARRAY_COUNT(kPastels)];
                        if (st == BS_STRIPMALL) wall = kNeutral[br.next() % 4];
                        if (reg == REG_MIDTOWN && br.chance(0.4f)) { wallMat = MAT_BRICK; wall = vec3(1.f); }
                        frame = kBright[br.next() % ARRAY_COUNT(kBright)];
                        flags |= 1u | 2u;
                        if (reg == REG_BEACH || reg == REG_CALLE_LUNA) flags |= 4u;
                        break;
                    case BS_HOUSE: case BS_VILLA: case BS_FARMHOUSE: case BS_SHACK:
                        style = 5;
                        floorH = 3.0f;
                        groundH = 3.0f;
                        bay = br.range(3.6f, 5.2f);
                        winW = br.range(0.3f, 0.45f);
                        winH = 0.5f;
                        sill = 0.95f;
                        wall = kPastels[br.next() % ARRAY_COUNT(kPastels)];
                        if (st == BS_FARMHOUSE || st == BS_SHACK || (st == BS_HOUSE && br.chance(0.25f))) { wallMat = MAT_WOOD_SIDING; wall = br.chance(0.5f) ? vec3(0.95f) : kPastels[br.next() % ARRAY_COUNT(kPastels)]; }
                        frame = vec3(0.95f);
                        if (st == BS_VILLA) { wall = vec3(0.98f, 0.9f, 0.78f) * br.range(0.9f, 1.05f); bay = br.range(3.2f, 4.2f); winH = 0.6f; }
                        f.litFrac = br.range(0.4f, 0.8f);
                        flags |= 8u;
                        break;
                    case BS_WAREHOUSE: case BS_FACTORY: case BS_BARN:
                        style = 4;
                        floorH = st == BS_BARN ? 6.f : br.range(7.f, 10.f);
                        groundH = floorH;
                        bay = br.range(5.f, 8.f);
                        wallMat = br.chance(0.6f) ? MAT_CORRUGATED : MAT_CONCRETE_PANEL;
                        wall = kNeutral[br.next() % ARRAY_COUNT(kNeutral)];
                        if (st == BS_BARN) { wallMat = MAT_WOOD_SIDING; wall = br.chance(0.6f) ? vec3(0.6f, 0.15f, 0.1f) : vec3(0.55f, 0.52f, 0.48f); }
                        if (st == BS_FACTORY && br.chance(0.5f)) { wallMat = MAT_BRICK; wall = vec3(1.f); }
                        frame = vec3(0.3f);
                        f.litFrac = 0.2f;
                        break;
                    case BS_MOTEL:
                        style = 0;
                        floorH = 3.0f;
                        groundH = 3.0f;
                        bay = 4.2f;
                        winW = 0.35f;
                        wall = kPastels[br.next() % ARRAY_COUNT(kPastels)];
                        frame = kBright[br.next() % ARRAY_COUNT(kBright)];
                        flags |= 8u | 4u;
                        f.litFrac = 0.6f;
                        break;
                    case BS_GASSTATION:
                        style = 0;
                        floorH = 4.2f;
                        groundH = 4.2f;
                        bay = 3.f;
                        flags |= 1u | 2u;
                        wall = vec3(0.95f);
                        frame = kBright[br.next() % ARRAY_COUNT(kBright)];
                        break;
                    case BS_GARAGE:
                        style = 2;
                        floorH = 3.0f;
                        groundH = 3.5f;
                        bay = 6.f;
                        wallMat = MAT_CONCRETE;
                        wall = vec3(0.8f);
                        frame = vec3(0.4f);
                        f.litFrac = 0.9f;
                        break;
                    case BS_CHURCH:
                        style = 0;
                        floorH = 9.f;
                        groundH = 9.f;
                        bay = 4.f;
                        winW = 0.3f;
                        winH = 0.6f;
                        sill = 2.f;
                        wall = vec3(0.97f, 0.96f, 0.93f);
                        frame = vec3(0.5f, 0.35f, 0.2f);
                        break;
                    default: break;
                }
                // per-building tone and rhythm: palette colours drift a little, and a same-style neighbour never shares its colour
                // or its window bay
                if (wallMat != MAT_BRICK && st != BS_WAREHOUSE && st != BS_BARN) {
                    wall = vmin(wall * vr.range(0.92f, 1.04f) + vec3(vr.range(-0.03f, 0.03f), vr.range(-0.03f, 0.03f), vr.range(-0.03f, 0.03f)), vec3(1.f));
                    if (nb) {
                        vec4 pw = unpackRGBA8(facades[nb->facade].wallColor);
                        vec3 pc(pw.x, pw.y, pw.z);
                        if (length(pc - wall) < 0.14f) {
                            const vec3* pal = kPastels;
                            int np = ARRAY_COUNT(kPastels);
                            if (st == BS_TOWER || st == BS_STRIPMALL) { pal = kNeutral; np = ARRAY_COUNT(kNeutral); }
                            else if (reg == REG_CALLE_LUNA || reg == REG_MIDTOWN || reg == REG_KEY_TOWN) { pal = kBright; np = ARRAY_COUNT(kBright); }
                            float bestD = -1.f;
                            for (int k = 0; k < np; k++) {
                                float dk = length(pal[k] - pc) + vr.f() * 0.1f;
                                if (dk > bestD) { bestD = dk; wall = pal[k]; }
                            }
                        }
                    }
                }
                if (nb && st != BS_HOUSE && st != BS_WAREHOUSE && st != BS_BARN) {
                    float pb = facades[nb->facade].bayW;
                    if (fabsf(pb - bay) < 0.35f) bay = bay + (bay < 3.2f ? 1.f : -1.f) * vr.range(0.5f, 0.9f);
                    winW = Clamp(winW * vr.range(0.88f, 1.14f), 0.3f, 0.85f);
                    winH = Clamp(winH * vr.range(0.9f, 1.1f), 0.45f, 0.85f);
                }
                f.floorH = floorH;
                f.groundH = groundH;
                f.bayW = bay;
                f.winW = winW;
                f.winH = winH;
                f.sillH = sill;
                f.style = (float)style;
                f.wallColor = rgb8(wall.x, wall.y, wall.z);
                f.frameColor = rgb8(frame.x, frame.y, frame.z);
                f.glassColor = rgb8(glass.x, glass.y, glass.z);
                f.flags = flags;
                f.wallLayer = (float)wallMat;  // converted to the texture layer at upload
                b.facade = (u32)facades.size();
                facades.push_back(f);
                // Mixed cladding: stone / brick / panel podium under a curtain-wall tower, storefront base band on midrises
                bool clad2 = (st == BS_TOWER && style == 1 && floors > 12 && vr.chance(0.65f)) || (st == BS_MIDRISE && floors >= 4 && vr.chance(0.35f));
                if (clad2) {
                    FacadeGPU g = f;
                    g.seed = b.seed * 747796405u + 2891336453u;
                    g.style = st == BS_TOWER ? (vr.chance(0.5f) ? 0.f : 2.f) : 0.f;
                    g.bayW = st == BS_TOWER ? vr.range(2.8f, 3.8f) : vr.range(3.2f, 4.6f);
                    g.winW = vr.range(0.5f, 0.68f);
                    g.winH = vr.range(0.58f, 0.72f);
                    float cm = vr.f();
                    MaterialId m2 = cm < 0.3f ? MAT_STONE : (cm < 0.55f ? MAT_MARBLE : (cm < 0.8f ? MAT_CONCRETE_PANEL : MAT_BRICK));
                    vec3 w2 = m2 == MAT_BRICK ? vec3(1.f) : kNeutral[vr.next() % ARRAY_COUNT(kNeutral)] * vr.range(0.8f, 1.f);
                    if (st == BS_MIDRISE && m2 != MAT_BRICK) w2 = w2 * 0.8f;   // darker base under a light body
                    g.wallColor = rgb8(w2.x, w2.y, w2.z);
                    vec3 fr2 = vr.chance(0.6f) ? vec3(0.1f, 0.1f, 0.11f) : vec3(0.75f, 0.7f, 0.6f);
                    g.frameColor = rgb8(fr2.x, fr2.y, fr2.z);
                    g.wallLayer = (float)m2;
                    g.flags = f.flags | 1u;
                    b.facade2 = (u32)facades.size();
                    facades.push_back(g);
                }
                b.height = groundH + (floors - 1) * floorH;
                if (st == BS_WAREHOUSE || st == BS_BARN) b.height = floorH;
                // Roof
                switch (st) {
                    case BS_HOUSE: case BS_VILLA: b.roof = br.chance(0.6f) ? ROOF_HIP : ROOF_GABLE; break;
                    case BS_FARMHOUSE: case BS_SHACK: b.roof = br.chance(0.7f) ? ROOF_GABLE : ROOF_SHED; break;
                    case BS_BARN: b.roof = br.chance(0.6f) ? ROOF_GABLE : ROOF_BARREL; break;
                    case BS_WAREHOUSE: b.roof = br.chance(0.25f) ? ROOF_BARREL : ROOF_FLAT; break;
                    case BS_CHURCH: b.roof = ROOF_GABLE; break;
                    default: b.roof = ROOF_FLAT; break;
                }
                vec3 roofC(0.9f);
                b.roofColor = rgb8(roofC.x, roofC.y, roofC.z);
                // Base height: highest terrain point under the footprint (buildings never sink)
                float bz = -1e9f;
                vec2 bay2 = perp(b.ax);
                for (int sx = -1; sx <= 1; sx++)
                    for (int sy = -1; sy <= 1; sy++) {
                        vec2 q = b.c + b.ax * (sx * b.hx) + bay2 * (sy * b.hy);
                        bz = Max(bz, map.heightAt(q.x, q.y));
                    }
                b.baseZ = Max(bz, e.posAt(sc).z) + 0.15f;
                buildings.push_back(b);
                s += w + gap;
            }
        }
    }
    // Buildings requested by the site layout (airport garages and sheds, port offices, clubhouses, shacks)
    for (size_t qi = 0; qi < gSites->buildingReqs.size(); qi++) addSiteBuilding(map, gSites->buildingReqs[qi], hash32((u32)qi * 2246822519u + 0x51E5u));
    // Keep the SkyLine viaduct corridor and its stations clear (transit.cpp)
    transitPruneBuildings(buildings);
    // Places fitted to the finished streets (churchyards, town and suburban hospitals) clear their ground (places.h)
    placesAfterLots(map, roads, *this);
    // Per-cell lists
    const int cps = (int)(2.f * kWorldHalf / 256.f);
    cellLists.assign((size_t)cps * cps, {});
    for (size_t i = 0; i < buildings.size(); i++) {
        int cx = Clamp((int)((buildings[i].c.x + kWorldHalf) / 256.f), 0, cps - 1);
        int cy = Clamp((int)((buildings[i].c.y + kWorldHalf) / 256.f), 0, cps - 1);
        cellLists[(size_t)cy * cps + cx].push_back((int)i);
    }
    int counts[BS_COUNT] = {};
    for (auto& b : buildings) counts[b.style]++;
    LOG("Buildings: %zu (towers %d, midrise %d, condo %d, deco %d, shops %d, strip %d, houses %d, villas %d, warehouses %d) in %.2f s",
        buildings.size(), counts[BS_TOWER], counts[BS_MIDRISE], counts[BS_CONDO], counts[BS_DECO], counts[BS_SHOPS], counts[BS_STRIPMALL],
        counts[BS_HOUSE], counts[BS_VILLA], counts[BS_WAREHOUSE], TimeSeconds() - t0);
    // Sites that depend on roads and buildings (billboards, farm silos) + per-cell site element lists
    gSites->makeFacades(*this);
    gSites->finalize(map, roads, *this);
    // Enterable interiors: picks host buildings (story places, shops) and plans their openings (world/interiors.cpp)
    planInteriors(map, roads, *this);
}

void BuildingSet::addSiteBuilding(WorldMap& map, const SiteBuildingReq& q, u32 seed) {
    Rng br(seed);
    BuildingStyle st = (BuildingStyle)q.style;
    Building b;
    b.seed = seed;
    b.c = q.c;
    b.ax = q.ax;
    b.hx = q.hx;
    b.hy = q.hy;
    b.front = q.front;
    b.region = q.region;
    b.lotKind = 0;
    b.lotC = q.c;
    b.lotHy = q.hy + 2.f;
    b.lotHx = q.hx + 2.f;
    b.style = (u8)st;
    b.roof = q.roof;
    b.floors = q.floors;
    b.siteElem = q.siteElem;
    b.siteHost = q.siteHost;
    FacadeGPU f = {};
    f.seed = seed;
    f.signIndex = (float)(br.next() % 480);
    f.litFrac = br.range(0.45f, 0.8f);
    f.roomDepth = br.range(4.f, 7.f);
    vec3 wall(0.92f), frame(0.25f), glass = kGlass[br.next() % ARRAY_COUNT(kGlass)];
    MaterialId wallMat = MAT_STUCCO;
    float floorH = 3.2f, groundH = 4.2f, bay = 3.2f, winW = 0.55f, winH = 0.55f, sill = 0.9f;
    int style = 0;
    u32 flags = 0;
    switch (st) {
        case BS_GARAGE:
            style = 2; floorH = 3.0f; groundH = 3.5f; bay = 6.f; wallMat = MAT_CONCRETE; wall = vec3(0.86f); frame = vec3(0.45f); f.litFrac = 0.95f;
            break;
        case BS_WAREHOUSE: case BS_FACTORY: case BS_BARN:
            style = 4; floorH = br.range(9.f, 11.f); groundH = floorH; bay = br.range(6.f, 8.f);
            wallMat = br.chance(0.5f) ? MAT_CORRUGATED : MAT_CONCRETE_PANEL; wall = kNeutral[br.next() % ARRAY_COUNT(kNeutral)]; frame = vec3(0.3f);
            f.litFrac = 0.35f;
            break;
        case BS_MIDRISE: case BS_TOWER:
            style = br.chance(0.5f) ? 1 : 2; floorH = 3.4f; groundH = 5.f; bay = style == 1 ? 1.8f : 3.2f; winW = 0.7f; winH = 0.62f; sill = 0.8f;
            wall = kNeutral[br.next() % 4]; wallMat = MAT_CONCRETE_PANEL; frame = vec3(0.15f); flags |= 1u | 16u;
            break;
        case BS_VILLA: case BS_HOUSE: case BS_SHACK:
            style = 5; floorH = 3.2f; groundH = 3.4f; bay = br.range(3.4f, 4.4f); winW = 0.42f; winH = 0.58f; sill = 0.9f;
            wall = st == BS_SHACK ? vec3(0.75f, 0.72f, 0.66f) : vec3(0.98f, 0.9f, 0.8f) * br.range(0.92f, 1.03f);
            if (st == BS_SHACK) wallMat = MAT_WOOD_SIDING;
            frame = vec3(0.95f); flags |= 8u; f.litFrac = 0.7f;
            break;
        case BS_SHOPS:
            style = 0; floorH = 3.2f; groundH = 4.4f; bay = 3.4f; wall = kPastels[br.next() % ARRAY_COUNT(kPastels)]; frame = kBright[br.next() % ARRAY_COUNT(kBright)];
            flags |= 1u | 2u;
            break;
        default: break;
    }
    f.floorH = floorH;
    f.groundH = groundH;
    f.bayW = bay;
    f.winW = winW;
    f.winH = winH;
    f.sillH = sill;
    f.style = (float)style;
    f.wallColor = rgb8(wall.x, wall.y, wall.z);
    f.frameColor = rgb8(frame.x, frame.y, frame.z);
    f.glassColor = rgb8(glass.x, glass.y, glass.z);
    f.flags = flags;
    f.wallLayer = (float)wallMat;
    b.facade = (u32)facades.size();
    facades.push_back(f);
    b.height = groundH + (Max(1, (int)q.floors) - 1) * floorH;
    if (st == BS_WAREHOUSE || st == BS_BARN) b.height = floorH;
    b.roofColor = rgb8(0.9f, 0.9f, 0.9f);
    if (q.baseZ > -100.f) b.baseZ = q.baseZ + 0.15f;
    else {
        float bz = -1e9f;
        vec2 ay = perp(b.ax);
        for (int sx = -1; sx <= 1; sx++)
            for (int sy = -1; sy <= 1; sy++) {
                vec2 p = b.c + b.ax * (sx * b.hx) + ay * (sy * b.hy);
                bz = Max(bz, map.heightAt(p.x, p.y));
            }
        b.baseZ = Max(bz, 0.3f) + 0.15f;
    }
    buildings.push_back(b);
}

void BuildingSet::buildingsNear(vec2 p, float r, std::vector<int>& out) const {
    out.clear();
    const int cps = (int)(2.f * kWorldHalf / 256.f);
    int x0 = Clamp((int)((p.x - r - 80.f + kWorldHalf) / 256.f), 0, cps - 1), x1 = Clamp((int)((p.x + r + 80.f + kWorldHalf) / 256.f), 0, cps - 1);
    int y0 = Clamp((int)((p.y - r - 80.f + kWorldHalf) / 256.f), 0, cps - 1), y1 = Clamp((int)((p.y + r + 80.f + kWorldHalf) / 256.f), 0, cps - 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            for (int i : cellLists[(size_t)y * cps + x]) {
                const Building& b = buildings[i];
                if (length(b.c - p) < r + b.hx + b.hy) out.push_back(i);
            }
}

bool BuildingSet::pointInBuilding(vec2 p, float margin, float* topZ) const {
    std::vector<int> nb;
    buildingsNear(p, 1.f, nb);
    for (int i : nb) {
        const Building& b = buildings[i];
        vec2 d = p - b.c;
        if (fabsf(dot(d, b.ax)) < b.hx + margin && fabsf(dot(d, perp(b.ax))) < b.hy + margin) {
            if (topZ) *topZ = b.baseZ + b.height;
            return true;
        }
    }
    return false;
}

}  // namespace World
