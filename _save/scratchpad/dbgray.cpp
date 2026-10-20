#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/render/mesh.cpp"
#include "vm/vehicle_models.cpp"
#include <cstdio>
#include <algorithm>
// usage: dbgray idx ex ey ez tx ty tz fov px py   (px,py in the 1680x720 custom-cam image)
int main(int argc, char** argv) {
    int idx = atoi(argv[1]);
    vec3 e((float)atof(argv[2]), (float)atof(argv[3]), (float)atof(argv[4])), t((float)atof(argv[5]), (float)atof(argv[6]), (float)atof(argv[7]));
    float fov = (float)atof(argv[8]);
    float px = (float)atof(argv[9]), py = (float)atof(argv[10]);
    int W = 1680 * 2, H = 720 * 2;
    vec3 fwd = normalize(t - e), right = normalize(cross(fwd, vec3(0, 0, 1))), up = cross(right, fwd);
    float f = (H * 0.5f) / tanf(fov * 0.5f * kDegToRad);
    float sx = px * 2 + 1, sy = py * 2 + 1;
    vec3 d = normalize(fwd * f + right * (sx - W * 0.5f) - up * (sy - H * 0.5f));
    Vehicles::VehicleModel vm;
    Vehicles::buildModel(idx, vm);
    struct Hit { float t; int mat; bool back; vec3 p, n; };
    std::vector<Hit> hits;
    const MeshData& m = vm.body;
    for (size_t k = 0; k + 2 < m.indices.size(); k += 3) {
        vec3 a = m.verts[m.indices[k]].pos, b = m.verts[m.indices[k + 1]].pos, c = m.verts[m.indices[k + 2]].pos;
        vec3 e1 = b - a, e2 = c - a, pv = cross(d, e2);
        float det = dot(e1, pv);
        if (fabsf(det) < 1e-12f) continue;
        float inv = 1.f / det;
        vec3 tv = e - a;
        float u = dot(tv, pv) * inv;
        if (u < 0 || u > 1) continue;
        vec3 qv = cross(tv, e1);
        float v = dot(d, qv) * inv;
        if (v < 0 || u + v > 1) continue;
        float tt = dot(e2, qv) * inv;
        if (tt <= 0) continue;
        vec3 n = normalize(cross(e1, e2));
        hits.push_back({tt, (int)(m.verts[m.indices[k]].mat & 0xff) | (int)((m.verts[m.indices[k]].color & 0xffffff) << 8), dot(n, d) > 0, e + d * tt, n});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.t < b.t; });
    for (auto& h : hits) printf("t=%.3f mat=%d col=%06x %s p=(%.3f %.3f %.3f) n=(%.2f %.2f %.2f)\n", h.t, h.mat & 0xff, (unsigned)(h.mat >> 8), h.back ? "BACK " : "front", h.p.x, h.p.y, h.p.z, h.n.x, h.n.y, h.n.z);
}
