#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "vm/vehicle_models.cpp"
using namespace Vehicles::detail;
int main() {
    CarDef d;
    archSports(d);
    CarBody b(d.s);
    PMesh m;
    b.setup(); b.buildRows(); b.buildGrid(); b.classify();
    // print rear-most rows' centre column data
    for (int i = 0; i < 6; i++) {
        printf("row %d y=%.4f L=%.3f: ", i, b.rows[i], b.rowL[i]);
        for (int j = 0; j < b.NP; j += 1) printf("(%.3f,%.3f) ", b.G[i * b.NP + j].x, b.G[i * b.NP + j].z);
        printf("\n");
    }
    // count classes near the centre at the rear
    int NC1 = b.NP - 1;
    for (int i = 0; i < 6; i++) {
        printf("row %d classes: ", i);
        for (int j = 0; j < NC1; j++) printf("%d", b.cls[i * NC1 + j]);
        printf("\n");
    }
}
