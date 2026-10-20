// The terrain grids' queries and edits (WorldMap, worldmap.h): heights, water levels and regions by position, and the
// grading that roads and pads apply. worldmap.cpp includes it, and so does tools/worldc.cpp, which grades the
// hand-built world's terrain under its roads at compile time (docs/HANDBUILT_WORLD.md section 3.3).
#include "worldmap.h"

namespace World {

static float sampleBilinear(const std::vector<float>& m, float x, float y) {
    float sx = worldToTexel(x), sy = worldToTexel(y);
    int ix = Clamp((int)floorf(sx), 0, kHeightRes - 2), iy = Clamp((int)floorf(sy), 0, kHeightRes - 2);
    float fx = Clamp(sx - ix, 0.f, 1.f), fy = Clamp(sy - iy, 0.f, 1.f);
    const float* g = m.data();
    const int R = kHeightRes;
    return Lerp(Lerp(g[iy * R + ix], g[iy * R + ix + 1], fx), Lerp(g[(iy + 1) * R + ix], g[(iy + 1) * R + ix + 1], fx), fy);
}

float WorldMap::heightAt(float x, float y) const { return sampleBilinear(height, x, y); }

float WorldMap::waterAt(float x, float y) const {
    float sx = worldToTexel(x), sy = worldToTexel(y);
    int ix = Clamp((int)floorf(sx + 0.5f), 0, kHeightRes - 1), iy = Clamp((int)floorf(sy + 0.5f), 0, kHeightRes - 1);
    return waterLevel[(size_t)iy * kHeightRes + ix];
}

vec3 WorldMap::normalAt(float x, float y) const {
    const float e = 2.f;
    float hx = heightAt(x + e, y) - heightAt(x - e, y);
    float hy = heightAt(x, y + e) - heightAt(x, y - e);
    return normalize(vec3(-hx, -hy, 2.f * e));
}

Region WorldMap::regionAt(float x, float y) const {
    float sx = worldToTexel(x), sy = worldToTexel(y);
    int ix = Clamp((int)floorf(sx + 0.5f), 0, kHeightRes - 1), iy = Clamp((int)floorf(sy + 0.5f), 0, kHeightRes - 1);
    return (Region)region[(size_t)iy * kHeightRes + ix];
}

void WorldMap::flattenAlong(vec2 a, vec2 b, float za, float zb, float halfWidth, float blend) {
    float reach = halfWidth + blend;
    vec2 mn = vmin(a, b) - vec2(reach), mx = vmax(a, b) + vec2(reach);
    int x0 = Max(0, (int)floorf(worldToTexel(mn.x))), x1 = Min(kHeightRes - 1, (int)ceilf(worldToTexel(mx.x)));
    int y0 = Max(0, (int)floorf(worldToTexel(mn.y))), y1 = Min(kHeightRes - 1, (int)ceilf(worldToTexel(mx.y)));
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            vec2 p(texelToWorld(tx), texelToWorld(ty));
            float t;
            float d = distPointSegment2D(p, a, b, &t);
            if (d > reach) continue;
            float z = Lerp(za, zb, t);
            size_t idx = (size_t)ty * kHeightRes + tx;
            float k = SmoothStep(reach, halfWidth, d);
            // Never raise terrain into the water plane (bridges handle crossings)
            if (waterLevel[idx] > kNoWater + 1.f && height[idx] < waterLevel[idx] && z > waterLevel[idx] + 1.f) continue;
            height[idx] = Lerp(height[idx], z, k);
        }
}

void WorldMap::lowerAlong(vec2 a, vec2 b, float za, float zb, float flat, float reach, float slope) {
    vec2 mn = vmin(a, b) - vec2(reach), mx = vmax(a, b) + vec2(reach);
    int x0 = Max(0, (int)floorf(worldToTexel(mn.x))), x1 = Min(kHeightRes - 1, (int)ceilf(worldToTexel(mx.x)));
    int y0 = Max(0, (int)floorf(worldToTexel(mn.y))), y1 = Min(kHeightRes - 1, (int)ceilf(worldToTexel(mx.y)));
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            vec2 p(texelToWorld(tx), texelToWorld(ty));
            float t;
            float d = distPointSegment2D(p, a, b, &t);
            if (d > reach) continue;
            float z = Lerp(za, zb, t) + Max(0.f, d - flat) * slope;
            size_t idx = (size_t)ty * kHeightRes + tx;
            if (height[idx] > z) height[idx] = z;
        }
}

void WorldMap::flattenRect(vec2 c, vec2 ax, float hx, float hy, float z, float blend) {
    vec2 ay = perp(ax);
    float reach = sqrtf(hx * hx + hy * hy) + blend;
    int x0 = Max(0, (int)floorf(worldToTexel(c.x - reach))), x1 = Min(kHeightRes - 1, (int)ceilf(worldToTexel(c.x + reach)));
    int y0 = Max(0, (int)floorf(worldToTexel(c.y - reach))), y1 = Min(kHeightRes - 1, (int)ceilf(worldToTexel(c.y + reach)));
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            vec2 p = vec2(texelToWorld(tx), texelToWorld(ty)) - c;
            float lx = fabsf(dot(p, ax)) - hx, ly = fabsf(dot(p, ay)) - hy;
            float d = length(vec2(Max(lx, 0.f), Max(ly, 0.f)));
            if (d > blend) continue;
            size_t idx = (size_t)ty * kHeightRes + tx;
            if (waterLevel[idx] > kNoWater + 1.f && height[idx] < waterLevel[idx]) continue;
            float k = SmoothStep(blend, 0.f, d);
            height[idx] = Lerp(height[idx], z, k);
        }
}

namespace terrainops_detail {

inline bool insidePoly(const std::vector<vec2>& poly, vec2 p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const vec2 a = poly[i], b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

inline float distToPoly(const std::vector<vec2>& poly, vec2 p) {
    float d = 1e30f;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) d = Min(d, distPointSegment2D(p, poly[j], poly[i]));
    return d;
}

}  // namespace terrainops_detail

void WorldMap::flattenPoly(const std::vector<vec2>& poly, float z, float blend) {
    using namespace terrainops_detail;
    if (poly.size() < 3) return;
    vec2 mn = poly[0], mx = poly[0];
    for (const vec2& p : poly) {
        mn = vmin(mn, p);
        mx = vmax(mx, p);
    }
    mn -= vec2(blend);
    mx += vec2(blend);
    int x0 = Max(0, (int)floorf(worldToTexel(mn.x))), x1 = Min(kHeightRes - 1, (int)ceilf(worldToTexel(mx.x)));
    int y0 = Max(0, (int)floorf(worldToTexel(mn.y))), y1 = Min(kHeightRes - 1, (int)ceilf(worldToTexel(mx.y)));
    for (int ty = y0; ty <= y1; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            const vec2 p(texelToWorld(tx), texelToWorld(ty));
            const size_t idx = (size_t)ty * kHeightRes + tx;
            if (waterLevel[idx] > kNoWater + 1.f && height[idx] < waterLevel[idx] && z > waterLevel[idx]) continue;
            if (insidePoly(poly, p)) {
                height[idx] = z;
                continue;
            }
            if (blend <= 0.f) continue;
            const float d = distToPoly(poly, p);
            if (d < blend) height[idx] = Lerp(height[idx], z, SmoothStep(blend, 0.f, d));
        }
}


}  // namespace World
