#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "vm/vehicle_models.cpp"
using namespace Vehicles::detail;
int main() {
    CarDef d;
    archPickup(d, 5.25f, 2.0f, 1.78f, 3.30f, 0.37f, false);
    CarSpec& s = d.s;
    s.noseRound = 0.1f; s.frontExp = 6.f; s.frontD = 0.25f; s.lean = 0.10f; s.shR = 0.05f;
    CarBody b(s);
    PMesh m;
    b.build(m);
    printf("yF %.3f yHF %.3f zHoodF %.3f zNoseTop %.3f zNoseBot %.3f\n", b.yF, b.yHF, s.zHoodF, s.zNoseTop, s.zNoseBot);
    for (float z = 0.5f; z < 1.3f; z += 0.1f) {
        Decal dc;
        bool ok = decalAt(dc, b.proj, projFront(), vec3(0, b.yF + 0.5f, z), vec3(0, -1, 0));
        printf("z %.2f ok %d hit (%.3f %.3f %.3f)\n", z, ok, dc.fr.o.x, dc.fr.o.y, dc.fr.o.z);
    }
    for (int i = b.nr - 8; i < b.nr; i++) printf("row y %.3f centre z %.3f bottom %.3f\n", b.rows[i], b.G[i * b.NP + b.NP - 1].z, b.G[i * b.NP].z);
}
