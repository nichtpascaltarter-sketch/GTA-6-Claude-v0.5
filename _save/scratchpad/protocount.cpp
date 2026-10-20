#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/core/noise.cpp"
#include "src/render/mesh.cpp"
#include "src/world/propmesh.cpp"
using namespace World;
int main() {
    const char* names[] = {"streetlight", "streetlight2", "traffic", "stop", "palm", "palm_tall", "oak", "pine", "bush", "bench", "bin", "hydrant",
                           "busstop", "bollard", "powerpole", "mangrove", "cypress", "sawgrass"};
    int nv[] = {2, 1, 2, 1, 4, 3, 4, 3, 4, 1, 1, 1, 1, 1, 1, 3, 3, 3};
    for (int t = 0; t < 18; t++)
        for (int v = 0; v < nv[t]; v++) {
            PropPrototype p;
            buildPropPrototype((PropType)t, v, p);
            printf("%-12s v%d: %5zu verts %5zu tris radius %.1f\n", names[t], v, p.mesh.verts.size(), p.mesh.indices.size() / 3, p.radius);
        }
}
