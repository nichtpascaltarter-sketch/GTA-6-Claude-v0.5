#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "vm/vehicle_models.cpp"
#include <cstdio>
using namespace Vehicles::detail;
int main() {
    Vehicles::VehicleModel o;
    // rebuild the Kestrel spec by calling the model and capturing its CarDef is not exposed; print generic spec info via archetype
    CarDef d;
    archSports(d);
    CarBody b(d.s);
    PMesh m;
    b.build(m);
    printf("style=%d yDeck=%.3f zDeck=%.3f yRoofR=%.3f yR=%.3f yb(yHipF-0.4)=%.3f belt(yb)=%.3f roofZ(yDeck+.01)=%.3f\n", (int)b.s.style, b.s.yDeck, b.s.zDeck, b.s.yRoofR, b.yR, d.I.yHipF - 0.4f, b.beltZAt(d.I.yHipF - 0.4f), b.roofZAt(b.s.yDeck + 0.01f));
}
