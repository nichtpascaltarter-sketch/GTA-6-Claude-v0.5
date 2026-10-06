#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "vm/vehicle_models.cpp"
#include <cstdio>
using namespace Vehicles::detail;
int main() {
    CarDef d;
    sawgrassBase(d);
    CarBody b(d.s);
    PMesh m;
    b.build(m);
    int i = b.rowAt(-1.262f);
    printf("row %d y=%.3f rowL=%.3f pLed0=%d pGh0=%d jSplit=%d pRail0=%d pTop0=%d NP=%d\n", i, b.rows[i], b.rowL[i], b.pLed0, b.pGh0, b.jSplit, b.pRail0, b.pTop0, b.NP);
    for (int j = b.pLed0; j < b.NP; j++) {
        vec3 g = b.G[i * b.NP + j];
        int c = j < b.NP - 1 ? b.cls[i * (b.NP - 1) + j] : -1;
        printf("  j=%2d band=%d cls=%d (%.3f %.3f)\n", j, j < b.NP - 1 ? (int)b.cellBand[j] : -1, c, g.x, g.z);
    }
}
