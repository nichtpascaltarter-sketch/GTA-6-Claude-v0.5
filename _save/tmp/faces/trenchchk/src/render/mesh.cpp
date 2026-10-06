#include "mesh.h"

void triangulatePolygon(const std::vector<vec2>& polyIn, std::vector<u32>& out) {
    out.clear();
    int n = (int)polyIn.size();
    if (n < 3) return;
    std::vector<int> idx(n);
    // ensure CCW
    float area = polygonArea2D(polyIn.data(), n);
    for (int i = 0; i < n; i++) idx[i] = area >= 0 ? i : n - 1 - i;
    auto isEar = [&](int prev, int cur, int next, const std::vector<int>& list) {
        vec2 a = polyIn[prev], b = polyIn[cur], c = polyIn[next];
        if (cross(b - a, c - b) <= 1e-9f) return false;
        for (int k : list) {
            if (k == prev || k == cur || k == next) continue;
            vec2 p = polyIn[k];
            if (cross(b - a, p - a) > 0 && cross(c - b, p - b) > 0 && cross(a - c, p - c) > 0) return false;
        }
        return true;
    };
    int guard = 0;
    while (idx.size() > 3 && guard < 10000) {
        guard++;
        bool clipped = false;
        int m = (int)idx.size();
        for (int i = 0; i < m; i++) {
            int prev = idx[(i + m - 1) % m], cur = idx[i], next = idx[(i + 1) % m];
            if (isEar(prev, cur, next, idx)) {
                out.push_back((u32)prev);
                out.push_back((u32)cur);
                out.push_back((u32)next);
                idx.erase(idx.begin() + i);
                clipped = true;
                break;
            }
        }
        if (!clipped) {
            // degenerate: fan the rest
            for (size_t i = 1; i + 1 < idx.size(); i++) {
                out.push_back((u32)idx[0]);
                out.push_back((u32)idx[i]);
                out.push_back((u32)idx[i + 1]);
            }
            return;
        }
    }
    if (idx.size() == 3) {
        out.push_back((u32)idx[0]);
        out.push_back((u32)idx[1]);
        out.push_back((u32)idx[2]);
    }
}

void MeshData::polygon(const std::vector<vec3>& poly, vec3 normal, u32 color, u32 mat, float uvScale) {
    std::vector<vec2> p2;
    for (auto& p : poly) p2.push_back(p.xy());
    std::vector<u32> tris;
    triangulatePolygon(p2, tris);
    u32 base = (u32)verts.size();
    vec3 t = normalize(anyPerp(normal));
    if (fabsf(normal.z) > 0.9f) t = vec3(1, 0, 0);
    for (auto& p : poly) addVertex(p, normal, t, p.xy() * uvScale, color, mat);
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        u32 a = base + tris[i], b = base + tris[i + 1], c = base + tris[i + 2];
        // make sure the triangle faces along the normal
        vec3 fn = cross(verts[b].pos - verts[a].pos, verts[c].pos - verts[a].pos);
        if (dot(fn, normal) < 0) tri(a, c, b);
        else tri(a, b, c);
    }
}

void MeshData::extrudeWalls(const std::vector<vec2>& fp, float z0, float z1, u32 color, u32 mat) {
    int n = (int)fp.size();
    float u = 0;
    for (int i = 0; i < n; i++) {
        vec2 a = fp[i], b = fp[(i + 1) % n];
        wall(vec3(a, z0), vec3(b, z0), z1 - z0, color, mat, u, 0);
        u += length(b - a);
    }
}

void MeshData::append(const MeshData& o) {
    u32 base = (u32)verts.size();
    verts.insert(verts.end(), o.verts.begin(), o.verts.end());
    for (u32 i : o.indices) indices.push_back(base + i);
    if (o.bounds.valid()) bounds.add(o.bounds);
}
