#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "vm/vehicle_models.cpp"
int main() {
    Vehicles::VehicleModel vm;
    Vehicles::buildModel(0, vm);
    const MeshData& m = vm.body;
    float yw = -1.4f, zw = 0.335f, Ra = 0.38f;
    int bad = 0, good = 0;
    for (size_t t = 0; t < m.indices.size() / 3; t++) {
        const VtxStatic* v[3] = {&m.verts[m.indices[t * 3]], &m.verts[m.indices[t * 3 + 1]], &m.verts[m.indices[t * 3 + 2]]};
        vec3 c = (v[0]->pos + v[1]->pos + v[2]->pos) / 3.f;
        if (fabsf(c.y - yw) > 0.5f || c.x > -0.55f || c.z > 0.8f) continue;
        float r = sqrtf((c.y - yw) * (c.y - yw) + (c.z - zw) * (c.z - zw));
        if (r < Ra - 0.02f || r > Ra + 0.03f) continue;
        vec3 n = cross(v[1]->pos - v[0]->pos, v[2]->pos - v[0]->pos);
        vec3 toC = vec3(0, yw - c.y, zw - c.z);
        u32 mat = v[0]->mat & 0xff;
        if (dot(n, toC) < 0) { bad++; if (bad < 12) printf("away mat=%u c=(%.3f %.3f %.3f) r=%.3f n=(%.2f %.2f %.2f)\n", mat, c.x, c.y, c.z, r, n.x, n.y, n.z); }
        else good++;
    }
    printf("good %d bad %d\n", good, bad);
}
