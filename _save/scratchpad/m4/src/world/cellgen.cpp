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

// Open ground of the blocks (BuildingSet::openLots, blockstyle.cpp infill): rear surface parking - asphalt following
// the ground, painted stalls in one or two rows, wheel stops, a lamp post - and yards with trees
void buildOpenLot(const OpenLot& L, const WorldMap& map, bool detail, vec3 org, MeshData& m, std::vector<PropInstance>* props, std::vector<LightInstance>* lights,
                  std::vector<CollisionBox>* col) {
    Rng r(L.seed);
    vec2 ay = perp(L.ax);
    vec3 X(L.ax, 0.f), Y(ay, 0.f), Z(0, 0, 1);
    if (L.kind == OL_PARKING) {
        // asphalt on a grid that follows the ground
        int nx = Clamp((int)(L.hx / 5.f), 1, 6), ny = Clamp((int)(L.hy / 5.f), 1, 6);
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
        if (props && r.chance(0.5f)) {
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
        int nx = Clamp((int)(L.hx / 6.f), 1, 5), ny = Clamp((int)(L.hy / 6.f), 1, 5);
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
