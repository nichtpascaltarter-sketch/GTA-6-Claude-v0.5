// Per-cell world content generation (runs on worker threads).
#include "buildings.h"
#include "sites.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

struct CellGeometry {
    MeshData opaque;
    MeshData decals;
    std::vector<PropInstance> props;
    std::vector<LightInstance> lights;
    std::vector<CollisionBox> collision;
};

void buildRoadCell(const RoadNetwork& net, const WorldMap& map, int cx, int cy, RoadCellOutput& out);
void buildBuildingMesh(const Building& b, const FacadeGPU& fac, const WorldMap& map, bool detail, vec3 org, MeshData& m,
                       std::vector<CollisionBox>* col, std::vector<PropInstance>* props, std::vector<LightInstance>* lights);

// Natural vegetation scattered from the biome maps (near cells only)
void scatterVegetation(int cx, int cy, std::vector<PropInstance>& props) {
    const WorldMap& map = *gMap;
    vec2 org = cellOrigin(cx, cy);
    // rear parking lots of the blocks (blockstyle.cpp infill) stay clear of trees
    std::vector<const OpenLot*> parking;
    if (gBuildings && !gBuildings->openCellLists.empty())
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++) {
                int nx = cx + dx, ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= kCellsPerSide || ny >= kCellsPerSide) continue;
                for (int li : gBuildings->openCellLists[(size_t)ny * kCellsPerSide + nx])
                    if (gBuildings->openLots[li].kind == OL_PARKING) parking.push_back(&gBuildings->openLots[li]);
            }
    auto onParking = [&](vec2 p) {
        for (const OpenLot* L : parking) {
            vec2 d = p - L->c;
            if (fabsf(dot(d, L->ax)) < L->hx + 1.5f && fabsf(dot(d, perp(L->ax))) < L->hy + 1.5f) return true;
        }
        return false;
    };
    // paved ground (BuildingSet::pavedGrid, 2 m squares): forecourts, driveways, pool decks, paved front yards, parking
    thread_local std::vector<u8> paved;
    const int pn = Max(1, (int)(kCellSize / 2.f));
    if (gBuildings) gBuildings->pavedGrid(cx, cy, pn, paved);
    else paved.assign((size_t)pn * pn, 0);
    auto onPaved = [&](vec2 p) {
        int ix = (int)((p.x - org.x) / kCellSize * pn), iy = (int)((p.y - org.y) / kCellSize * pn);
        return ix >= 0 && iy >= 0 && ix < pn && iy < pn && paved[(size_t)iy * pn + ix] != 0;
    };
    const float step = 7.f;
    int n = (int)(kCellSize / step);
    for (int iy = 0; iy < n; iy++)
        for (int ix = 0; ix < n; ix++) {
            u32 h = hash3i(cx * 64 + ix, cy * 64 + iy, 0x7EE5);
            vec2 p = org + vec2((ix + hashToFloat(h)) * step, (iy + hashToFloat(hash32(h))) * step);
            float roll = hashToFloat(hash32(h ^ 0x55u));
            Region reg = map.regionAt(p.x, p.y);
            float gz = map.heightAt(p.x, p.y);
            float wl = map.waterAt(p.x, p.y);
            bool underwater = wl > kNoWater + 1.f && wl > gz;
            PropType type = PROP_COUNT;
            float density = 0.f;
            switch (reg) {
                case REG_SAWGRASS:
                case REG_GULF_TOWN: {
                    float coast = map.coastDistance(p.x, p.y);
                    if (coast < 500.f && gz > -0.4f) { type = PROP_MANGROVE; density = 0.45f; }
                    else if (gz > 0.85f) { type = roll < 0.5f ? PROP_CYPRESS : PROP_TREE_OAK; density = 0.5f; }
                    else if (gz > -0.35f) { type = PROP_SAWGRASS; density = 0.85f; }
                    if (underwater && type != PROP_MANGROVE && type != PROP_SAWGRASS) type = PROP_COUNT;
                    break;
                }
                case REG_RIDGE: type = roll < 0.7f ? PROP_TREE_PINE : PROP_TREE_OAK; density = 0.55f; break;
                case REG_FARMLAND:
                case REG_REDLAND: type = roll < 0.6f ? PROP_TREE_OAK : PROP_BUSH; density = 0.02f; break;
                case REG_KEYS:
                case REG_KEY_CORAL: type = roll < 0.5f ? PROP_PALM : (roll < 0.75f ? PROP_BUSH : PROP_MANGROVE); density = 0.18f; break;
                case REG_BEACH: type = PROP_PALM; density = 0.05f; break;
                case REG_GROVE:
                case REG_BAY_ISLAND: type = roll < 0.4f ? PROP_TREE_OAK : (roll < 0.75f ? PROP_PALM : PROP_BUSH); density = 0.12f; break;
                case REG_SUBURBS: type = roll < 0.5f ? PROP_PALM : (roll < 0.8f ? PROP_TREE_OAK : PROP_BUSH); density = 0.035f; break;
                default: type = roll < 0.6f ? PROP_PALM : PROP_BUSH; density = 0.02f; break;
            }
            if (type == PROP_COUNT) continue;
            if (hashToFloat(hash32(h ^ 0xABCDu)) > density) continue;
            if (underwater && type != PROP_MANGROVE && type != PROP_SAWGRASS) continue;
            if (gRoads->nearRoad(p, type == PROP_SAWGRASS ? 1.f : 2.5f)) continue;
            if (gBuildings->pointInBuilding(p, 2.5f)) continue;
            if (gSites->blocksVegetation(p)) continue;
            if (!parking.empty() && onParking(p)) continue;
            // (nor on a strip mall's or gas station's forecourt - a palm came up through the pump canopy - a driveway, a
            // pool deck or a paved front yard: BuildingSet::pavedGrid)
            if (type != PROP_SAWGRASS && type != PROP_MANGROVE && onPaved(p)) continue;
            PropInstance pi;
            pi.pos = vec3(p, gz);
            pi.yaw = hashToFloat(hash32(h ^ 0x1234u)) * kTwoPi;
            pi.scale = 0.75f + hashToFloat(hash32(h ^ 0x4321u)) * 0.5f;
            pi.type = (u8)type;
            pi.variant = (u8)(h >> 24);
            pi.flags = 0;
            props.push_back(pi);
        }
    // Beach furniture on the sand of Sol Beach
    if (gMap->regionAt(org.x + 128.f, org.y + 128.f) == REG_BEACH) {
        for (int k = 0; k < 40; k++) {
            u32 h = hash3i(cx, cy, 900 + k);
            vec2 p = org + vec2(hashToFloat(h), hashToFloat(hash32(h))) * kCellSize;
            float cd = map.coastDistance(p.x, p.y);
            if (cd < 8.f || cd > 60.f || map.isWater(p.x, p.y)) continue;
            if (gSites->blocksVegetation(p) || gRoads->nearRoad(p, 1.5f)) continue;
            PropInstance pi;
            pi.pos = vec3(p, map.heightAt(p.x, p.y));
            pi.yaw = hashToFloat(h ^ 7u) * kTwoPi;
            pi.scale = 1.f;
            pi.type = (u8)((k % 13 == 0) ? PROP_LIFEGUARD_TOWER : PROP_UMBRELLA);
            pi.variant = 0;
            pi.flags = 0;
            props.push_back(pi);
        }
    }
}

// (sitegeo.cpp: the for-sale board and the parking sign of the frontage lots, with the stroke font)
void openLotSign(MeshData& m, vec3 org, vec2 p, vec2 fw, float z, int kind, u32 seed);

// A vacant lot on the street frontage (blockstyle.cpp fillFrontage): rough grass drifting between green and dry straw,
// patches of bare earth (sand in the Keys), sometimes the slab and driveway apron of a house or shop long gone, a
// for-sale board near the sidewalk, a tree or two and scrub. Nothing on it collides but the trees.
void buildVacantLot(const OpenLot& L, const WorldMap& map, bool detail, vec3 org, MeshData& m, std::vector<PropInstance>* props) {
    Rng r(L.seed);
    const vec2 ay = perp(L.ax);
    const bool keys = L.region == REG_KEY_TOWN || L.region == REG_GULF_TOWN;
    const bool city = L.region == REG_CALLE_LUNA || L.region == REG_NORTH_CITY || L.region == REG_FLATS || L.region == REG_MIDTOWN;
    const float fs = dot(L.front, ay) >= 0.f ? 1.f : -1.f;   // the street side along ay
    auto at = [&](float u, float v) { return L.c + L.ax * u + ay * v; };
    auto gz = [&](vec2 p) { return map.heightAt(p.x, p.y); };
    // the slab of a building long gone (and its driveway apron to the street): decided first, the grass keeps off it
    const bool slab = r.chance(keys ? 0.2f : 0.35f) && L.hx >= 5.5f && L.hy >= 6.f;
    const float sw = Min(L.hx * 1.4f, r.range(7.f, 11.f)) * 0.5f, sd = Min(L.hy * 1.1f, r.range(7.f, 10.f)) * 0.5f;
    const float su = r.range(-1.f, 1.f) * Max(0.f, L.hx - sw - 1.f), sv = -fs * Max(0.f, L.hy - sd - 2.5f) * r.range(0.3f, 1.f);
    const bool apron = slab && r.chance(0.55f);
    const float au = su + r.range(-1.f, 1.f) * Max(0.f, sw - 1.6f);
    // ground: rough grass on a grid following the terrain, its tint drifting between green and dry straw vertex to vertex
    const int nx = detail ? Clamp((int)(L.hx / 2.5f + 0.5f), 1, 6) : 1, ny = detail ? Clamp((int)(L.hy / 2.5f + 0.5f), 1, 6) : 1;
    const float dryShare = keys ? 0.6f : r.range(0.3f, 0.7f);
    const u32 grass = makeMat(MAT_GRASS);
    const u32 v0 = (u32)m.verts.size();
    for (int j = 0; j <= ny; j++)
        for (int i = 0; i <= nx; i++) {
            float u = -L.hx + 0.15f + (2.f * L.hx - 0.3f) * i / nx, v = -L.hy + 0.15f + (2.f * L.hy - 0.3f) * j / ny;
            vec2 p = at(u, v);
            float dry = detail ? (r.f() < dryShare ? r.range(0.6f, 1.f) : r.range(0.f, 0.35f)) : dryShare;
            vec3 t = lerp(vec3(0.85f, 1.05f, 0.72f), vec3(1.16f, 1.05f, 0.62f), dry) * r.range(0.92f, 1.06f);
            m.addVertex(vec3(p, gz(p) + 0.04f) - org, vec3(0, 0, 1), vec3(L.ax, 0.f), p * 0.3f, packRGBA8(Saturate(t.x), Saturate(t.y), Saturate(t.z), 1), grass);
        }
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            u32 a = v0 + (u32)(j * (nx + 1) + i), b = a + 1, c = a + (u32)(nx + 1) + 1, d = a + (u32)(nx + 1);
            m.tri(a, b, c);
            m.tri(a, c, d);
        }
    // (the grid's triangles face up when ay = perp(ax) runs counter-clockwise from ax, which perp does)
    if (!detail) return;
    if (slab) {
        // the slab stands on its highest corner, its edges skirted down to the ground
        vec2 cs[4] = {at(su - sw, sv - sd), at(su + sw, sv - sd), at(su + sw, sv + sd), at(su - sw, sv + sd)};
        float top = -1e9f, low = 1e9f;
        for (vec2 q : cs) top = Max(top, gz(q)), low = Min(low, gz(q));
        top += 0.12f;
        const float t = r.range(0.78f, 0.9f);
        const u32 sc = packRGBA8(t, t * 0.98f, t * 0.94f, 1), cm = makeMat(MAT_CONCRETE);
        m.quadFacing(vec3(cs[0], top) - org, vec3(cs[1], top) - org, vec3(cs[2], top) - org, vec3(cs[3], top) - org, cs[0] * 0.5f, cs[1] * 0.5f, cs[2] * 0.5f,
                     cs[3] * 0.5f, sc, cm, vec3(0, 0, 1));
        for (int k = 0; k < 4; k++) {
            vec2 qa = cs[k], qb = cs[(k + 1) % 4];
            vec2 on = normalize(vec2(qb.y - qa.y, qa.x - qb.x));
            if (dot(on, qa - at(su, sv)) < 0.f) on = -on;
            m.quadFacing(vec3(qa, top) - org, vec3(qb, top) - org, vec3(qb, low - 0.1f) - org, vec3(qa, low - 0.1f) - org, vec2(0, 0), vec2(length(qb - qa), 0),
                         vec2(length(qb - qa), top - low + 0.1f), vec2(0, top - low + 0.1f), sc, cm, vec3(on, 0.f));
        }
        if (apron) {
            // the driveway apron from the slab's street edge to the sidewalk, following the ground
            float va = sv + fs * sd, vb = fs * L.hy;
            int n = Max(1, (int)(fabsf(vb - va) / 3.f));
            for (int k = 0; k < n; k++) {
                float v0 = va + (vb - va) * k / n, v1 = va + (vb - va) * (k + 1) / n;
                vec2 qa = at(au - 1.5f, v0), qb = at(au + 1.5f, v0), qc = at(au + 1.5f, v1), qd = at(au - 1.5f, v1);
                vec3 A(qa, gz(qa) + 0.09f), B(qb, gz(qb) + 0.09f), C(qc, gz(qc) + 0.09f), D(qd, gz(qd) + 0.09f);
                m.quadFacing(A - org, B - org, C - org, D - org, qa * 0.5f, qb * 0.5f, qc * 0.5f, qd * 0.5f, sc, cm, vec3(0, 0, 1));
            }
        }
    }
    // bare patches of packed earth (sand in the Keys), irregular, off the slab
    const int np = r.irange(keys ? 1 : 0, 2);
    for (int k = 0; k < np; k++) {
        float rad = r.range(1.2f, Min(2.8f, Min(L.hx, L.hy) * 0.45f));
        float u = r.range(-L.hx + rad + 0.3f, L.hx - rad - 0.3f), v = r.range(-L.hy + rad + 0.3f, L.hy - rad - 0.3f);
        if (slab && fabsf(u - su) < sw + rad && fabsf(v - sv) < sd + rad) continue;
        vec3 t = keys ? vec3(0.95f, 0.92f, 0.85f) : vec3(0.66f, 0.56f, 0.44f) * r.range(0.9f, 1.08f);
        const u32 pc = packRGBA8(Saturate(t.x), Saturate(t.y), Saturate(t.z), 1), pm = makeMat(keys ? MAT_SAND : MAT_ROOF_GRAVEL);
        vec2 pcen = at(u, v);
        const u32 ci = m.addVertex(vec3(pcen, gz(pcen) + 0.06f) - org, vec3(0, 0, 1), vec3(L.ax, 0.f), pcen * 0.5f, pc, pm);
        const int ns = 9;
        float ph = r.f() * kTwoPi;
        for (int q = 0; q < ns; q++) {
            float an = ph + kTwoPi * q / ns, rr = rad * r.range(0.65f, 1.f);
            vec2 pq = pcen + L.ax * (cosf(an) * rr) + ay * (sinf(an) * rr * r.range(0.7f, 1.f));
            m.addVertex(vec3(pq, gz(pq) + 0.06f) - org, vec3(0, 0, 1), vec3(L.ax, 0.f), pq * 0.5f, pc, pm);
        }
        for (int q = 0; q < ns; q++) m.tri(ci, ci + 1 + (u32)q, ci + 1 + (u32)((q + 1) % ns));
    }
    // the for-sale board near the sidewalk, beside the apron
    vec2 signP(0.f);
    bool sign = r.chance(city ? 0.45f : 0.35f);
    if (sign) {
        float u = r.range(-1.f, 1.f) * Max(0.f, L.hx - 1.5f);
        if (apron && fabsf(u - au) < 2.5f) u = au + (u >= au ? 2.6f : -2.6f);
        u = Clamp(u, -L.hx + 0.9f, L.hx - 0.9f);
        signP = at(u, fs * (L.hy - 0.9f));
        openLotSign(m, org, signP, L.front, gz(signP), 0, L.seed ^ 0x5A1Eu);
    }
    // a tree or two and scrub, off the slab and the board
    if (props) {
        int nt = r.irange(0, Clamp((int)(L.hx * L.hy / 40.f), 1, 4));
        for (int k = 0; k < nt; k++) {
            float u = r.range(-L.hx + 1.2f, L.hx - 1.2f), v = r.range(-L.hy + 1.2f, L.hy - 1.2f);
            if (slab && fabsf(u - su) < sw + 1.2f && fabsf(v - sv) < sd + 1.2f) continue;
            vec2 p = at(u, v);
            if (sign && length(p - signP) < 2.5f) continue;
            if (gBuildings && gBuildings->pointInBuilding(p, 1.8f)) continue;
            float roll = r.f();
            PropInstance pi;
            pi.pos = vec3(p, gz(p));
            pi.yaw = r.f() * kTwoPi;
            pi.scale = r.range(0.7f, 1.1f);
            pi.type = (u8)(keys ? (roll < 0.4f ? PROP_PALM : PROP_BUSH) : (roll < (city ? 0.3f : 0.5f) ? PROP_TREE_OAK : (roll < 0.85f ? PROP_BUSH : PROP_PALM)));
            pi.variant = (u8)r.irange(0, 3);
            pi.flags = 0;
            props->push_back(pi);
        }
    }
}

// Open ground of the blocks (BuildingSet::openLots, blockstyle.cpp infill): rear surface parking - asphalt following
// the ground, painted stalls in one or two rows, wheel stops, a lamp post - and yards with trees
void buildOpenLot(const OpenLot& L, const WorldMap& map, bool detail, vec3 org, MeshData& m, std::vector<PropInstance>* props, std::vector<LightInstance>* lights,
                  std::vector<CollisionBox>* col) {
    if (L.kind == OL_VACANT) {
        buildVacantLot(L, map, detail, org, m, props);
        return;
    }
    Rng r(L.seed);
    vec2 ay = perp(L.ax);
    vec3 X(L.ax, 0.f), Y(ay, 0.f), Z(0, 0, 1);
    if (L.kind == OL_PARKING) {
        // asphalt on a grid that follows the ground
        int nx = detail ? Clamp((int)(L.hx / 5.f), 1, 6) : 1, ny = detail ? Clamp((int)(L.hy / 5.f), 1, 6) : 1;   // (one quad in the far LOD)
        std::vector<vec3> g((size_t)(nx + 1) * (ny + 1));
        for (int j = 0; j <= ny; j++)
            for (int i = 0; i <= nx; i++) {
                vec2 p = L.c + L.ax * (-L.hx + 2.f * L.hx * i / nx) + ay * (-L.hy + 2.f * L.hy * j / ny);
                g[(size_t)j * (nx + 1) + i] = vec3(p, map.heightAt(p.x, p.y) + 0.05f) - org;
            }
        u32 asph = packRGBA8(0.95f, 0.95f, 0.95f, 1), am = makeMat(MAT_ASPHALT_OLD);
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                vec3 a = g[(size_t)j * (nx + 1) + i], b = g[(size_t)j * (nx + 1) + i + 1], c = g[(size_t)(j + 1) * (nx + 1) + i + 1], d = g[(size_t)(j + 1) * (nx + 1) + i];
                m.quadFacing(a, b, c, d, vec2(a.x, a.y) * 0.25f, vec2(b.x, b.y) * 0.25f, vec2(c.x, c.y) * 0.25f, vec2(d.x, d.y) * 0.25f, asph, am, vec3(0, 0, 1));
            }
        if (!detail) return;
        // stalls: rows along the long side, 2.6 m wide and 5 m deep, a 6 m aisle between two rows
        bool alongX = L.hx >= L.hy;
        vec2 R = alongX ? L.ax : ay, Dn = alongX ? ay : L.ax;
        float halfLen = alongX ? L.hx : L.hy, halfDep = alongX ? L.hy : L.hx;
        int rows = halfDep * 2.f >= 15.f ? 2 : 1;
        const u32 paint = makeMat(MAT_PAINT_WHITE), white = packRGBA8(0.9f, 0.9f, 0.88f, 1);
        for (int row = 0; row < rows; row++) {
            float side = row == 0 ? -1.f : 1.f;
            float edge = side * (halfDep - 0.3f);   // the stalls' far end at the lot edge
            int ns = (int)((2.f * halfLen - 1.f) / 2.6f);
            for (int k = 0; k <= ns; k++) {
                float a = -halfLen + 0.5f + k * 2.6f;
                vec2 p0 = L.c + R * a + Dn * edge, p1 = L.c + R * a + Dn * (edge - side * 5.f);
                float z0 = map.heightAt(p0.x, p0.y) + 0.07f, z1 = map.heightAt(p1.x, p1.y) + 0.07f;
                vec2 w = R * 0.06f;
                m.quadFacing(vec3(p0 - w, z0) - org, vec3(p0 + w, z0) - org, vec3(p1 + w, z1) - org, vec3(p1 - w, z1) - org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), white,
                             paint, vec3(0, 0, 1));
                // wheel stop at the head of the stall
                if (k < ns && r.chance(0.8f)) {
                    vec2 ws = L.c + R * (a + 1.3f) + Dn * (edge - side * 0.7f);
                    m.box(vec3(ws, map.heightAt(ws.x, ws.y) + 0.1f) - org, vec3(R, 0.f), vec3(Dn, 0.f), Z, vec3(0.8f, 0.1f, 0.06f), packRGBA8(0.75f, 0.74f, 0.7f, 1),
                          makeMat(MAT_CONCRETE), false);
                }
            }
        }
        // a lamp post at one corner, a dumpster at another
        vec2 lp = L.c + L.ax * (r.chance(0.5f) ? L.hx - 0.6f : -L.hx + 0.6f) + ay * (r.chance(0.5f) ? L.hy - 0.6f : -L.hy + 0.6f);
        if (L.street && dot(lp - L.c, L.front) > 0.f) lp -= L.front * (2.f * dot(lp - L.c, L.front));   // (a street lot's lamp at the back, off the wall)
        float lz = map.heightAt(lp.x, lp.y);
        m.cylinder(vec3(lp, lz) - org, 0.09f, 0.07f, 7.f, 6, packRGBA8(0.45f, 0.46f, 0.48f, 1), makeMat(MAT_METAL_PAINTED), false);
        vec2 arm = normalize(L.c - lp);
        m.box(vec3(lp + arm * 0.7f, lz + 7.f) - org, vec3(arm, 0.f), vec3(perp(arm), 0.f), Z, vec3(0.75f, 0.16f, 0.07f), packRGBA8(0.4f, 0.4f, 0.42f, 1), makeMat(MAT_METAL_PAINTED),
              true);
        if (col) {
            CollisionBox cb;
            cb.c = vec3(lp, lz + 3.5f);
            cb.ax = L.ax;
            cb.he = vec3(0.12f, 0.12f, 3.5f);
            col->push_back(cb);
        }
        if (lights) {
            LightInstance li;
            li.pos = vec3(lp + arm * 1.2f, lz + 6.8f);
            li.color = vec3(1.f, 0.88f, 0.7f) * 2500.f;
            li.radius = 16.f;
            li.dir = vec3(0, 0, -1);
            li.cone = 0.3f;
            li.type = 0;
            lights->push_back(li);
        }
        if (L.street) {
            // on the street frontage: a low wall along the sidewalk with the entrance at one end, and the sign beside it
            Rng sr(L.seed ^ 0x5EE7u);
            const float fs = dot(L.front, ay) >= 0.f ? 1.f : -1.f;
            const float gapEnd = sr.chance(0.5f) ? 1.f : -1.f, gapW = Min(6.f, L.hx);
            const float u0 = gapEnd > 0.f ? -L.hx + 0.3f : -L.hx + gapW, u1 = gapEnd > 0.f ? L.hx - gapW : L.hx - 0.3f;
            const float wt = sr.range(0.8f, 0.95f);
            const u32 wcol = packRGBA8(wt, wt * 0.98f, wt * 0.93f, 1), wmat = makeMat(sr.chance(0.6f) ? MAT_STUCCO : MAT_CONCRETE);
            const float H = sr.range(0.55f, 0.75f), v = fs * (L.hy - 0.5f);
            if (u1 - u0 > 1.f) {
                int nseg = Max(1, (int)ceilf((u1 - u0) / 6.f));
                for (int k = 0; k < nseg; k++) {
                    float a = u0 + (u1 - u0) * k / nseg, b = u0 + (u1 - u0) * (k + 1) / nseg;
                    vec2 p0 = L.c + L.ax * a + ay * v, p1 = L.c + L.ax * b + ay * v, mc = (p0 + p1) * 0.5f;
                    float gzl = Min(map.heightAt(p0.x, p0.y), map.heightAt(p1.x, p1.y));
                    m.box(vec3(mc, gzl + H * 0.5f - 0.1f) - org, X, Y, Z, vec3((b - a) * 0.5f, 0.1f, H * 0.5f + 0.1f), wcol, wmat, false);
                    m.box(vec3(mc, gzl + H + 0.03f) - org, X, Y, Z, vec3((b - a) * 0.5f + 0.02f, 0.14f, 0.04f), wcol, makeMat(MAT_CONCRETE), false);   // cap
                    if (col) {
                        CollisionBox cb;
                        cb.c = vec3(mc, gzl + H * 0.5f);
                        cb.ax = L.ax;
                        cb.he = vec3((b - a) * 0.5f, 0.12f, H * 0.5f + 0.05f);
                        col->push_back(cb);
                    }
                }
            }
            // the sign at the end of the wall, beside the entrance
            vec2 sp = L.c + L.ax * (gapEnd > 0.f ? u1 - 0.4f : u0 + 0.4f) + ay * (v - fs * 0.6f);
            openLotSign(m, org, sp, L.front, map.heightAt(sp.x, sp.y), 1, L.seed ^ 0x9A2Bu);
        }
        if (props && !L.street && r.chance(0.5f)) {
            vec2 dp = L.c - L.ax * (L.hx - 1.2f) * (lp.x > L.c.x ? 1.f : -1.f) - ay * (L.hy - 1.f);
            PropInstance pi;
            pi.pos = vec3(dp, map.heightAt(dp.x, dp.y));
            pi.yaw = atan2f(L.ax.y, L.ax.x);
            pi.scale = 1.f;
            pi.type = PROP_DUMPSTER;
            pi.variant = (u8)(r.next() & 1u);
            pi.flags = 0;
            props->push_back(pi);
        }
        return;
    }
    // yards: lawn (houses) or a concrete pad (shops, sheds) following the ground, a fence or block wall on the back and one
    // side (the neighbour's yard has the other), trees and shrubs, a dumpster and condensers in service yards
    {
        bool home = L.home != 0 || L.kind == OL_YARD;
        int nx = detail ? Clamp((int)(L.hx / 6.f), 1, 5) : 1, ny = detail ? Clamp((int)(L.hy / 6.f), 1, 5) : 1;
        std::vector<vec3> g((size_t)(nx + 1) * (ny + 1));
        float lift = home ? 0.04f : 0.06f;
        for (int j = 0; j <= ny; j++)
            for (int i = 0; i <= nx; i++) {
                vec2 p = L.c + L.ax * (-L.hx + 0.2f + (2.f * L.hx - 0.4f) * i / nx) + ay * (-L.hy + 0.2f + (2.f * L.hy - 0.4f) * j / ny);
                g[(size_t)j * (nx + 1) + i] = vec3(p, map.heightAt(p.x, p.y) + lift) - org;
            }
        u32 gcol, gmat;
        if (home) {
            vec3 t = vec3(r.range(0.85f, 1.1f), r.range(0.95f, 1.2f), r.range(0.75f, 0.95f));
            if (r.chance(0.25f)) t = vec3(1.15f, 1.08f, 0.7f);   // dry, patchy
            gcol = packRGBA8(Saturate(t.x), Saturate(t.y), Saturate(t.z), 1);
            gmat = makeMat(MAT_GRASS);
        } else {
            float t = r.range(0.85f, 1.f);
            gcol = packRGBA8(t, t, t * 0.97f, 1);
            gmat = makeMat(r.chance(0.6f) ? MAT_CONCRETE : MAT_ASPHALT_OLD);
        }
        for (int j = 0; j < ny; j++)
            for (int i = 0; i < nx; i++) {
                vec3 a = g[(size_t)j * (nx + 1) + i], b = g[(size_t)j * (nx + 1) + i + 1], c = g[(size_t)(j + 1) * (nx + 1) + i + 1], d = g[(size_t)(j + 1) * (nx + 1) + i];
                m.quadFacing(a, b, c, d, vec2(a.x, a.y) * 0.3f, vec2(b.x, b.y) * 0.3f, vec2(c.x, c.y) * 0.3f, vec2(d.x, d.y) * 0.3f, gcol, gmat, vec3(0, 0, 1));
            }
        if (detail) {
            // fence / wall on the far side (away from the street) and the +ax side
            float fr = r.f();
            int fk = home ? (fr < 0.55f ? 0 : (fr < 0.88f ? 1 : 2)) : (fr < 0.6f ? 1 : 2);   // 0 wood boards, 1 block wall, 2 none
            if (fk != 2) {
                float H = fk == 0 ? 1.8f : (home ? 1.7f : 2.2f);
                vec2 fb = -L.front;   // the back
                vec2 corners[3] = {L.c + fb * L.hy - L.ax * L.hx, L.c + fb * L.hy + L.ax * L.hx, L.c - fb * L.hy + L.ax * L.hx};
                vec4 wc = vec4(0.f);
                u32 fcol = fk == 0 ? packRGBA8(r.range(0.55f, 0.75f), r.range(0.42f, 0.55f), r.range(0.3f, 0.4f), 1)
                                   : packRGBA8(r.range(0.8f, 0.95f), r.range(0.78f, 0.92f), r.range(0.72f, 0.88f), 1);
                (void)wc;
                u32 fmat = makeMat(fk == 0 ? MAT_WOOD : MAT_STUCCO);
                for (int sgi = 0; sgi < 2; sgi++) {
                    vec2 a = corners[sgi], b = corners[sgi + 1];
                    vec2 t = b - a;
                    float len = length(t);
                    if (len < 0.5f) continue;
                    t = t / len;
                    int nseg = Max(1, (int)ceilf(len / 6.f));
                    for (int k = 0; k < nseg; k++) {
                        vec2 p0 = a + t * (len * k / nseg), p1 = a + t * (len * (k + 1) / nseg);
                        vec2 mc = (p0 + p1) * 0.5f;
                        float gz = Min(map.heightAt(p0.x, p0.y), map.heightAt(p1.x, p1.y));
                        m.box(vec3(mc, gz + H * 0.5f - 0.15f) - org, vec3(t, 0.f), vec3(perp(t), 0.f), Z, vec3(length(p1 - p0) * 0.5f, fk == 0 ? 0.04f : 0.1f, H * 0.5f + 0.15f), fcol,
                              fmat, false);
                        if (col) {
                            CollisionBox cb;
                            cb.c = vec3(mc, gz + H * 0.5f);
                            cb.ax = t;
                            cb.he = vec3(length(p1 - p0) * 0.5f, 0.1f, H * 0.5f);
                            col->push_back(cb);
                        }
                    }
                    // posts / pillars every 2.4 m
                    for (float u = 0.f; u <= len + 0.01f; u += fk == 0 ? 2.4f : 3.f) {
                        vec2 pp = a + t * Min(u, len);
                        float gz = map.heightAt(pp.x, pp.y);
                        m.box(vec3(pp, gz + H * 0.5f) - org, vec3(t, 0.f), vec3(perp(t), 0.f), Z, vec3(0.07f, fk == 0 ? 0.07f : 0.14f, H * 0.5f + (fk == 0 ? 0.05f : 0.08f)), fcol, fmat,
                              false);
                    }
                }
            }
            if (!home && props) {
                // service yard: a dumpster against the back, condensers
                vec2 dp = L.c - L.front * (L.hy - 1.3f) + L.ax * r.range(-L.hx + 1.5f, L.hx - 1.5f);
                if (!(gBuildings && gBuildings->pointInBuilding(dp, 1.2f))) {
                    PropInstance pi;
                    pi.pos = vec3(dp, map.heightAt(dp.x, dp.y));
                    pi.yaw = atan2f(L.ax.y, L.ax.x);
                    pi.scale = 1.f;
                    pi.type = PROP_DUMPSTER;
                    pi.variant = (u8)(r.next() & 1u);
                    pi.flags = 0;
                    props->push_back(pi);
                }
            }
        }
    }
    if (L.kind == OL_YARD && props) {
        // yard trees: palms, a shade tree, shrubs, kept off the back-yard buildings
        int nt = Clamp((int)(L.hx * L.hy / 45.f), 1, 5);
        bool rural = L.region == REG_FARMLAND || L.region == REG_REDLAND || L.region == REG_RIDGE || L.region == REG_HARLOW || L.region == REG_LAKE_TOWN;
        for (int k = 0; k < nt; k++) {
            vec2 p = L.c + L.ax * r.range(-L.hx + 1.5f, L.hx - 1.5f) + ay * r.range(-L.hy + 1.5f, L.hy - 1.5f);
            if (gBuildings && gBuildings->pointInBuilding(p, 1.8f)) continue;
            float roll = r.f();
            PropInstance pi;
            pi.pos = vec3(p, map.heightAt(p.x, p.y));
            pi.yaw = r.f() * kTwoPi;
            pi.scale = r.range(0.75f, 1.15f);
            pi.type = (u8)(rural ? (roll < 0.6f ? PROP_TREE_OAK : PROP_BUSH) : (roll < 0.45f ? PROP_PALM : (roll < 0.7f ? PROP_TREE_OAK : PROP_BUSH)));
            pi.variant = (u8)r.irange(0, 3);
            pi.flags = 0;
            props->push_back(pi);
        }
    }
}

void generateCell(int cx, int cy, bool detail, CellGeometry& out) {
    const WorldMap& map = *gMap;
    vec2 org2 = cellOrigin(cx, cy);
    vec3 org(org2, 0);
    RoadCellOutput roads;
    roads.detail = detail;
    buildRoadCell(*gRoads, map, cx, cy, roads);
    out.opaque.append(roads.road);
    if (detail) out.opaque.append(roads.street);
    // deck parapets, median barriers and guardrails collide at every LOD (vehicles far from the camera stay on the decks)
    out.collision.insert(out.collision.end(), roads.collision.begin(), roads.collision.end());
    if (detail) {
        out.decals.append(roads.decals);
        out.props.insert(out.props.end(), roads.props.begin(), roads.props.end());
        out.lights.insert(out.lights.end(), roads.lights.begin(), roads.lights.end());
    }
    const int cps = kCellsPerSide;
    const BuildingSet& bs = *gBuildings;
    for (int bi : bs.cellLists[(size_t)cy * cps + cx]) {
        const Building& b = bs.buildings[bi];
        buildBuildingMesh(b, bs.facades[b.facade], map, detail, org, out.opaque, detail ? &out.collision : nullptr,
                          detail ? &out.props : nullptr, detail ? &out.lights : nullptr);
    }
    // open ground of the blocks: rear parking and yards (blockstyle.cpp infill)
    if ((size_t)cy * cps + cx < bs.openCellLists.size())
        for (int li : bs.openCellLists[(size_t)cy * cps + cx])
            buildOpenLot(bs.openLots[li], map, detail, org, out.opaque, detail ? &out.props : nullptr, detail ? &out.lights : nullptr, detail ? &out.collision : nullptr);
    if (detail) scatterVegetation(cx, cy, out.props);
    // Far cells still need collision for distant physics (vehicles spawned far) - kept cheap: building boxes only
    // Airport, port, Key Coral and landmark geometry (streams with the cell like everything else)
    buildSiteCell(cx, cy, detail, out);
    if (!detail) {
        for (int bi : bs.cellLists[(size_t)cy * cps + cx]) {
            const Building& b = bs.buildings[bi];
            CollisionBox c;
            c.c = vec3(b.c, b.baseZ + b.height * 0.5f);
            c.ax = b.ax;
            c.he = vec3(b.hx, b.hy, b.height * 0.5f + 1.5f);
            out.collision.push_back(c);
        }
    }
}

// Site structures (walls, signs, fences, parked aircraft, stops...) standing within margin of p, between p.z and p.z + height:
// for placing what drivers use, such as dead-end turning circles. The colliders are those the site cells stream with,
// built once per cell on first use.
bool siteColliderNear(vec3 p, float margin, float height) {
    if (!gSites || gSites->cellElems.empty()) return false;
    static std::mutex mtx;
    static std::unordered_map<int, std::vector<CollisionBox>> cache;
    const int cps = kCellsPerSide;
    int cx0 = (int)floorf((p.x - margin + kWorldHalf) / kCellSize), cx1 = (int)floorf((p.x + margin + kWorldHalf) / kCellSize);
    int cy0 = (int)floorf((p.y - margin + kWorldHalf) / kCellSize), cy1 = (int)floorf((p.y + margin + kWorldHalf) / kCellSize);
    for (int cy = Max(cy0, 0); cy <= Min(cy1, cps - 1); cy++)
        for (int cx = Max(cx0, 0); cx <= Min(cx1, cps - 1); cx++) {
            int idx = cy * cps + cx;
            if (gSites->cellElems[(size_t)idx].empty()) continue;
            std::lock_guard<std::mutex> lock(mtx);
            auto it = cache.find(idx);
            if (it == cache.end()) {
                CellGeometry geo;
                buildSiteCell(cx, cy, true, geo);
                it = cache.emplace(idx, std::move(geo.collision)).first;
            }
            for (const CollisionBox& b : it->second) {
                if (b.c.z + b.he.z < p.z || b.c.z - b.he.z > p.z + height) continue;
                vec2 d = p.xy() - b.c.xy();
                float ex = Max(0.f, fabsf(dot(d, b.ax)) - b.he.x), ey = Max(0.f, fabsf(dot(d, perp(b.ax))) - b.he.y);
                if (ex * ex + ey * ey <= margin * margin) return true;
            }
        }
    return false;
}

}  // namespace World
