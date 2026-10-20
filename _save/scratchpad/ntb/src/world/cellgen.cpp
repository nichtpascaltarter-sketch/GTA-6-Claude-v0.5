// Per-cell world content generation (runs on worker threads).
#include "buildings.h"
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

void generateCell(int cx, int cy, bool detail, CellGeometry& out) {
    const WorldMap& map = *gMap;
    vec2 org2 = cellOrigin(cx, cy);
    vec3 org(org2, 0);
    RoadCellOutput roads;
    buildRoadCell(*gRoads, map, cx, cy, roads);
    out.opaque.append(roads.road);
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
    if (detail) scatterVegetation(cx, cy, out.props);
    // Far cells still need collision for distant physics (vehicles spawned far) - kept cheap: building boxes only
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

}  // namespace World
