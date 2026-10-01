// scratch: the headliner (lowest body surface over the head) ahead of and behind each seat's hip point
#define PREVIEW_NO_MAIN
#include "/home/user/GTA-6-Claude-v0.5/tests/anim/preview.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/sim/vehicle_models.cpp"
using namespace Vehicles;
static u32 gMat, gCol;
static float ceilAt(const MeshData& m, float x, float y, float zmin) {
    float best = 9.f;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        vec3 a = m.verts[m.indices[i]].pos, b = m.verts[m.indices[i + 1]].pos, c = m.verts[m.indices[i + 2]].pos;
        u32 mt = m.verts[m.indices[i]].mat & 0xff;
        if (mt == MAT_LEATHER || (mt == MAT_FABRIC && (m.verts[m.indices[i]].color & 0xffu) <= 100u)) continue;
        // barycentric in xy
        float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
        if (fabsf(d) < 1e-9f) continue;
        float l1 = ((b.y - c.y) * (x - c.x) + (c.x - b.x) * (y - c.y)) / d;
        float l2 = ((c.y - a.y) * (x - c.x) + (a.x - c.x) * (y - c.y)) / d;
        float l3 = 1.f - l1 - l2;
        if (l1 < 0 || l2 < 0 || l3 < 0) continue;
        float z = l1 * a.z + l2 * b.z + l3 * c.z;
        if (z > zmin && z < best) { best = z; gMat = mt; gCol = m.verts[m.indices[i]].color; }
    }
    return best;
}
int main() {
    for (int mi : {3, 6, 8, 22, 13}) {
        VehicleModel vm;
        buildModel(mi, vm);
        for (size_t si = 0; si < vm.seats.size(); si++) {
            const SeatSpec& ss = vm.seats[si];
            if (si != 2) continue;
            if (ss.headZ > 5.f) continue;
            printf("%-12s seat %zu hip %.3f room %.3f  ceiling-hip at dy:", vm.name.c_str(), si, ss.pos.z, ss.headZ - ss.pos.z);
            for (float dy : {-0.35f, -0.25f, -0.15f, -0.10f, -0.05f, 0.f, 0.05f, 0.10f}) {
                float z = ceilAt(vm.body, ss.pos.x, ss.pos.y + dy, ss.pos.z + 0.45f);
                printf(" %+.2f:%.3f(m%u)", dy, z - ss.pos.z, gMat);
            }
            printf("\n");
        }
    }
}
