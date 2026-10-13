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
    printf("yCowl=%.3f zCowl=%.3f cowlLen=%.3f yRoofF=%.3f dloFront=%.3f yGhF=%.3f\n", b.s.yCowl, b.s.zCowl, b.s.cowlLen, b.s.yRoofF, b.s.dloFront, b.yGhF);
    int NC1 = b.NP - 1;
    for (int i = 0; i + 1 < b.nr; i++) {
        if (b.rows[i] < 1.05f || b.rows[i] > 1.45f) continue;
        printf("row %d y=%.3f L=%.3f  top-cells:", i, b.rows[i], b.rowL[i]);
        for (int j = b.pGh0; j < NC1; j++) printf(" %d", b.cls[i * NC1 + j]);
        vec3 g = b.G[i * b.NP + b.NP - 1];
        printf("   topZ=%.3f\n", g.z);
    }
}
