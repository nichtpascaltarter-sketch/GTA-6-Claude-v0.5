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
int main() {
    int total = 0;
    for (int t = 0; t < PROP_COUNT; t++) {
        int nv = 1;
        switch (t) {
            case PROP_PALM: case PROP_TREE_OAK: case PROP_BUSH: nv = 4; break;
            case PROP_PALM_TALL: case PROP_TREE_PINE: case PROP_MANGROVE: case PROP_CYPRESS: case PROP_SAWGRASS: nv = 3; break;
            case PROP_STREETLIGHT: case PROP_TRAFFIC_LIGHT: case PROP_DUMPSTER: nv = 2; break;
            case PROP_BUS_STOP: case PROP_NEWS_BOX: nv = 4; break;
            case PROP_POWER_POLE: case PROP_PLANTER: case PROP_BARRIER: case PROP_SIGNAL_SPAN: nv = 2; break;
            case PROP_STREET_TREE: nv = 3; break;
        }
        for (int v = 0; v < nv; v++) {
            PropPrototype p;
            buildPropPrototype((PropType)t, v, p);
            total += (int)p.mesh.verts.size();
            AABB b; for (auto& q : p.mesh.verts) b.add(q.pos);
            printf("type %2d v%d: %5zu verts %5zu tris r %.1f  bounds (%.2f %.2f %.2f)-(%.2f %.2f %.2f)\n", t, v, p.mesh.verts.size(), p.mesh.indices.size() / 3, p.radius,
                   b.mn.x, b.mn.y, b.mn.z, b.mx.x, b.mx.y, b.mx.z);
        }
    }
    printf("total verts %d\n", total);
}
