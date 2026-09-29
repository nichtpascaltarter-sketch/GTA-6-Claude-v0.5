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
