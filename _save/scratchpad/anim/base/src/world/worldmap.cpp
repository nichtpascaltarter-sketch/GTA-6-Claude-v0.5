#include "worldmap.h"
#include "sites.h"
#include "../core/noise.h"
#include "../core/jobs.h"

namespace World {

WorldMap* gMap = nullptr;

static const RegionInfo kRegionInfo[REG_COUNT] = {
    {"Atlantic Ocean", 0.f, 0, 0, 0.f},
    {"Downtown Porto Sol", 1.0f, 8, 70, 0.3f},
    {"Solaris", 1.0f, 12, 80, 0.3f},
    {"Canvas District", 0.8f, 2, 12, 0.4f},
    {"North Porto Sol", 0.7f, 1, 8, 0.6f},
    {"Calle Luna", 0.8f, 1, 4, 0.5f},
    {"Sol Beach", 0.85f, 2, 30, 0.7f},
    {"Bay Isles", 0.3f, 1, 3, 1.0f},
    {"Key Coral", 0.35f, 1, 12, 1.0f},
    {"Port Isle", 0.6f, 1, 3, 0.1f},
    {"The Grove", 0.45f, 1, 3, 1.3f},
    {"Porto Sol International", 0.4f, 1, 3, 0.3f},
    {"Palmetto Flats", 0.65f, 1, 4, 0.4f},
    {"Westbrook", 0.5f, 1, 2, 0.9f},
    {"Redland", 0.12f, 1, 2, 0.9f},
    {"The Sawgrass", 0.0f, 1, 1, 1.0f},
    {"Ten Palms", 0.2f, 1, 2, 0.8f},
    {"Palmera Farmlands", 0.05f, 1, 2, 0.7f},
    {"Okahatchee", 0.3f, 1, 3, 0.9f},
    {"Harlow", 0.3f, 1, 2, 0.9f},
    {"Cypress Ridge", 0.02f, 1, 2, 1.5f},
    {"Fort Castell", 0.6f, 1, 6, 0.5f},
    {"Coral Keys", 0.1f, 1, 2, 0.8f},
    {"Key Solano", 0.55f, 1, 3, 0.9f},
};
const RegionInfo& regionInfo(Region r) { return kRegionInfo[r < REG_COUNT ? r : 0]; }

namespace worldmap_detail {

std::vector<vec2> km(std::initializer_list<float> xy) {
    std::vector<vec2> out;
    const float* p = xy.begin();
    for (size_t i = 0; i + 1 < xy.size(); i += 2) out.push_back(vec2(p[i] * 1000.f, p[i + 1] * 1000.f));
    return out;
}

std::vector<vec2> ellipse(vec2 c, vec2 axis, float rx, float ry, int n, u32 seed, float jag) {
    std::vector<vec2> out;
    vec2 ax = normalize(axis), ay = perp(ax);
    for (int i = 0; i < n; i++) {
        float a = kTwoPi * i / n;
        float r = 1.f + jag * perlin2(cosf(a) * 1.5f + seed * 0.37f, sinf(a) * 1.5f, seed);
        out.push_back(c + ax * (cosf(a) * rx * r) + ay * (sinf(a) * ry * r));
    }
    return out;
}

vec2 bezier(vec2 a, vec2 b, vec2 c, float t) {
    float u = 1.f - t;
    return a * (u * u) + b * (2.f * u * t) + c * (t * t);
}

// Felzenszwalb 1D squared distance transform
void dt1d(const float* f, int n, float* d, int* v, float* z) {
    int k = 0;
    v[0] = 0;
    z[0] = -1e20f;
    z[1] = 1e20f;
    for (int q = 1; q < n; q++) {
        float s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) / (2.f * q - 2.f * v[k]);
        while (s <= z[k]) {
            k--;
            s = ((f[q] + (float)q * q) - (f[v[k]] + (float)v[k] * v[k])) / (2.f * q - 2.f * v[k]);
        }
        k++;
        v[k] = q;
        z[k] = s;
        z[k + 1] = 1e20f;
    }
    k = 0;
    for (int q = 0; q < n; q++) {
        while (z[k + 1] < q) k++;
        d[q] = (float)(q - v[k]) * (q - v[k]) + f[v[k]];
    }
}

void edt2d(std::vector<float>& grid, int res) {
    std::vector<float> f(res), d(res), z(res + 1);
    std::vector<int> v(res);
    for (int x = 0; x < res; x++) {
        for (int y = 0; y < res; y++) f[y] = grid[y * res + x];
        dt1d(f.data(), res, d.data(), v.data(), z.data());
        for (int y = 0; y < res; y++) grid[y * res + x] = d[y];
    }
    for (int y = 0; y < res; y++) {
        dt1d(&grid[y * res], res, d.data(), v.data(), z.data());
        for (int x = 0; x < res; x++) grid[y * res + x] = d[x];
    }
}

bool inPoly(vec2 p, const std::vector<vec2>& poly) { return pointInPolygon2D(p, poly.data(), (int)poly.size()); }

float boxMask(vec2 pkm, float x0, float x1, float y0, float y1) {
    return (pkm.x >= x0 && pkm.x <= x1 && pkm.y >= y0 && pkm.y <= y1) ? 1.f : 0.f;
}

}  // namespace worldmap_detail

using namespace worldmap_detail;

Region WorldMap::classify(float x, float y, float sdf) const {
    if (sdf < 0.f) return REG_OCEAN;
    vec2 p(x, y);
    if (inPoly(p, solBeach)) return REG_BEACH;
    if (inPoly(p, keyCoral)) return REG_KEY_CORAL;
    if (inPoly(p, portIsle)) return REG_PORT;
    if (x >= kAirportX0 && x <= kAirportX1 && y >= kAirportY0 && y <= kAirportY1) return REG_AIRPORT;
    for (size_t i = 0; i < smallIslands.size(); i++) {
        if (inPoly(p, smallIslands[i])) {
            // Bay islands first (x > 3.9 km), keys have negative y below -6 km
            if (y > -3000.f) return REG_BAY_ISLAND;
            return i + 1 == smallIslands.size() ? REG_KEY_TOWN : REG_KEYS;
        }
    }
    // Perturbed boundaries so districts don't meet on ruler lines (less in the grid city)
    float nx = perlin2(x / 900.f, y / 900.f, 71) * 160.f + perlin2(x / 250.f, y / 250.f, 72) * 40.f;
    float ny = perlin2(x / 900.f + 9.1f, y / 900.f, 73) * 160.f + perlin2(x / 250.f, y / 250.f + 3.3f, 74) * 40.f;
    vec2 q((x + nx * 0.5f) / 1000.f, (y + ny * 0.5f) / 1000.f);
    vec2 qr((x + nx) / 1000.f, (y + ny) / 1000.f);
    // Towns defined by distance
    if (length(qr - vec2(-8.75f, -2.35f)) < 0.9f) return REG_GULF_TOWN;
    if (length(qr - vec2(0.25f, 6.75f)) < 0.75f) return REG_LAKE_TOWN;
    if (length(qr - vec2(-5.0f, 4.85f)) < 0.62f) return REG_HARLOW;
    vec2 rd = (qr - vec2(-7.0f, 7.25f)) / vec2(2.7f, 2.3f);
    if (length(rd) < 1.f) return REG_RIDGE;
    if (qr.x > 3.25f && qr.y > 6.35f) return REG_FORT_CASTELL;
    if (boxMask(q, 2.62f, 4.2f, -0.62f, 0.92f) > 0) return REG_DOWNTOWN;
    if (boxMask(q, 2.72f, 4.2f, -2.05f, -0.62f) > 0) return REG_FINANCIAL;
    if (boxMask(q, 2.3f, 4.6f, 0.92f, 3.0f) > 0) return REG_MIDTOWN;
    if (boxMask(q, 0.95f, 2.62f, -0.92f, 0.92f) > 0) return REG_CALLE_LUNA;
    if (boxMask(q, 1.0f, 5.0f, 3.0f, 5.4f) > 0) return REG_NORTH_CITY;
    if (boxMask(q, -0.8f, 1.0f, 2.3f, 5.1f) > 0 || boxMask(q, 0.95f, 2.3f, 0.92f, 3.0f) > 0) return REG_FLATS;
    if (boxMask(q, 0.9f, 3.9f, -4.7f, -2.05f) > 0 || boxMask(q, 1.8f, 2.72f, -2.05f, -0.92f) > 0) return REG_GROVE;
    if (qr.y < -4.6f && qr.x > -2.95f) return REG_REDLAND;
    if (qr.x < -2.95f && qr.y < 4.3f) return REG_SAWGRASS;
    if (boxMask(q, -2.95f, 0.95f, -4.7f, 0.55f) > 0 || boxMask(q, -2.95f, -0.8f, 0.55f, 5.1f) > 0) return REG_SUBURBS;
    if (boxMask(q, 0.9f, 1.8f, -2.05f, -0.92f) > 0) return REG_SUBURBS;
    return REG_FARMLAND;
}

void WorldMap::generate() {
    double t0 = TimeSeconds();
    // ------------------------------------------------------------------ layout
    mainland = km({5.6f, 9.4f, 5.9f, 8.6f, 6.1f, 7.7f, 5.8f, 6.8f, 5.3f, 6.0f, 4.9f, 5.4f, 4.55f, 4.75f, 4.12f, 4.3f,
                   3.96f, 3.6f, 3.9f, 2.8f, 3.86f, 2.0f, 3.8f, 1.2f, 3.76f, 0.6f, 3.8f, 0.1f, 3.76f, -0.5f, 3.7f, -1.2f,
                   3.6f, -1.9f, 3.5f, -2.6f, 3.3f, -3.3f, 3.0f, -4.0f, 2.6f, -4.6f, 2.1f, -5.2f, 1.5f, -5.7f, 0.8f, -6.1f,
                   0.2f, -6.4f, -0.5f, -6.2f, -1.2f, -5.75f, -2.0f, -5.3f, -3.0f, -4.95f, -4.1f, -4.75f, -5.2f, -4.55f,
                   -6.1f, -4.2f, -6.9f, -3.75f, -7.6f, -3.35f, -8.3f, -2.75f, -8.8f, -2.0f, -9.2f, -1.1f, -9.35f, -0.2f,
                   -9.55f, 0.9f, -9.4f, 1.8f, -9.62f, 2.7f, -9.45f, 3.8f, -9.55f, 4.9f, -9.3f, 5.8f, -9.4f, 7.0f,
                   -9.0f, 8.4f, -8.4f, 9.3f, -7.2f, 9.7f, -5.6f, 9.8f, -4.0f, 9.6f, -2.4f, 9.7f, -0.8f, 9.5f, 0.8f, 9.6f,
                   2.4f, 9.5f, 4.0f, 9.6f, 5.0f, 9.6f});
    solBeach = km({4.95f, -2.65f, 5.3f, -2.55f, 5.4f, -1.5f, 5.45f, 0.0f, 5.45f, 1.5f, 5.4f, 3.0f, 5.3f, 4.2f, 5.12f, 4.45f,
                   4.96f, 4.3f, 4.9f, 3.0f, 4.85f, 1.5f, 4.8f, 0.0f, 4.8f, -1.5f, 4.85f, -2.3f});
    keyCoral = km({4.30f, -2.97f, 4.62f, -3.0f, 4.84f, -3.22f, 4.9f, -3.7f, 4.84f, -4.35f, 4.64f, -4.95f, 4.3f, -5.12f, 4.06f, -4.78f,
                   3.99f, -4.1f, 4.03f, -3.55f, 4.12f, -3.17f});
    // Port Isle: man-made rectangular island with quay walls (see sites.h)
    portIsle = {vec2(kPortX0, kPortY0), vec2(kPortX1, kPortY0), vec2(kPortX1, kPortY1), vec2(kPortX0, kPortY1)};
    smallIslands.clear();
    // Bay islands (mansions)
    smallIslands.push_back(ellipse(vec2(4300, 1300), vec2(1, 0.2f), 190, 120, 20, 11, 0.08f));
    smallIslands.push_back(ellipse(vec2(4380, 2150), vec2(1, -0.1f), 210, 130, 20, 12, 0.08f));
    for (int i = 0; i < 4; i++) smallIslands.push_back(ellipse(vec2(4060.f + i * 200.f, 1750), vec2(1, 0), 70, 55, 14, 20 + i, 0.05f));
    smallIslands.push_back(ellipse(vec2(4450, 2750), vec2(1, 0.3f), 150, 100, 16, 13, 0.1f));
    // Coral Keys along a curve; the last is Key Solano
    {
        vec2 A(-400, -6750), B(-4000, -9450), C(-8400, -9200);
        const float ts[] = {0.035f, 0.10f, 0.17f, 0.25f, 0.35f, 0.46f, 0.54f, 0.72f, 0.79f, 0.865f, 0.955f};
        const float ls[] = {230, 190, 260, 330, 520, 290, 210, 250, 210, 240, 900};
        const float ws[] = {110, 95, 140, 130, 170, 120, 100, 125, 105, 120, 480};
        for (int i = 0; i < 11; i++) {
            float t = ts[i];
            vec2 c = bezier(A, B, C, t);
            vec2 tan = bezier(A, B, C, t + 0.01f) - bezier(A, B, C, t - 0.01f);
            smallIslands.push_back(ellipse(c, tan, ls[i], ws[i], 28, 40 + i, 0.12f));
        }
    }
    {
        Rng mr(777);
        int placed = 0;
        for (int tries = 0; tries < 4000 && placed < 70; tries++) {
            vec2 c(mr.range(-9400.f, -1200.f), mr.range(-6600.f, -1200.f));
            // keep them in the shallow coastal band: just offshore of the southern/western coast
            bool inLand = inPoly(c, mainland);
            if (inLand) continue;
            float bestD = 1e9f;
            for (size_t i = 0; i < mainland.size(); i++) {
                float d = distPointSegment2D(c, mainland[i], mainland[(i + 1) % mainland.size()]);
                bestD = Min(bestD, d);
            }
            if (bestD < 180.f || bestD > 1700.f) continue;
            float r = mr.range(45.f, 170.f);
            smallIslands.insert(smallIslands.begin(), ellipse(c, vec2(mr.range(-1.f, 1.f), mr.range(-1.f, 1.f)), r * mr.range(1.f, 2.2f), r, 14, 200 + placed, 0.25f));
            placed++;
        }
    }
    lakePoly = ellipse(vec2(-2400, 7000), vec2(1, 0.25f), 2000, 1500, 48, 99, 0.10f);
    channels.clear();
    {
        Channel river;
        river.pts = km({-2.9f, 0.25f, -2.1f, 0.3f, -1.5f, 0.38f, -0.9f, 0.28f, -0.3f, 0.3f, 0.35f, 0.42f, 1.0f, 0.18f,
                        1.6f, 0.08f, 2.1f, 0.26f, 2.6f, 0.06f, 3.2f, 0.16f, 3.95f, 0.1f});
        river.width0 = 36;
        river.width1 = 95;
        river.depth = 4.5f;
        channels.push_back(river);
        Channel trail;  // Old Trail canal through the Sawgrass
        trail.pts = km({-9.4f, -0.25f, -7.0f, -0.18f, -5.0f, -0.1f, -3.6f, 0.05f, -2.9f, 0.25f});
        trail.width0 = trail.width1 = 22;
        trail.depth = 3.f;
        channels.push_back(trail);
        Channel c1;
        c1.pts = km({-2.95f, -2.0f, -1.0f, -2.1f, 1.0f, -2.3f, 2.0f, -2.6f, 2.9f, -3.3f});
        c1.width0 = c1.width1 = 18;
        c1.depth = 2.5f;
        channels.push_back(c1);
        Channel c2;
        c2.pts = km({-2.95f, -3.6f, -1.0f, -3.7f, 0.6f, -3.85f});
        c2.width0 = c2.width1 = 16;
        c2.depth = 2.5f;
        channels.push_back(c2);
        Channel c3;
        c3.pts = km({-2.95f, 2.6f, -1.9f, 2.65f, -0.8f, 2.72f});
        c3.width0 = c3.width1 = 16;
        c3.depth = 2.5f;
        channels.push_back(c3);
        Channel c4;  // Little river in the north city draining to the bay
        c4.pts = km({1.2f, 4.1f, 2.2f, 4.0f, 3.1f, 4.15f, 4.05f, 4.1f});
        c4.width0 = 18;
        c4.width1 = 40;
        c4.depth = 3.f;
        channels.push_back(c4);
        Channel c5;  // Ridge creek flowing from the hills to the lake
        c5.pts = km({-6.6f, 6.3f, -5.8f, 6.6f, -5.0f, 6.75f, -4.35f, 6.9f});
        c5.width0 = 10;
        c5.width1 = 22;
        c5.depth = 1.5f;
        channels.push_back(c5);
    }

    // ------------------------------------------------------------------ coarse land mask + SDF
    coarseRes = (int)(2.f * kWorldHalf / coarseCell);
    int cr = coarseRes;
    std::vector<u8> land((size_t)cr * cr, 0);
    Jobs::parallelFor(cr, [&](int y) {
        for (int x = 0; x < cr; x++) {
            vec2 p(-kWorldHalf + (x + 0.5f) * coarseCell, -kWorldHalf + (y + 0.5f) * coarseCell);
            bool l = inPoly(p, mainland) || inPoly(p, solBeach) || inPoly(p, keyCoral) || inPoly(p, portIsle);
            if (!l)
                for (auto& isl : smallIslands)
                    if (inPoly(p, isl)) { l = true; break; }
            land[(size_t)y * cr + x] = l ? 1 : 0;
        }
    }, 16);
    std::vector<float> dWater((size_t)cr * cr), dLand((size_t)cr * cr);
    for (size_t i = 0; i < land.size(); i++) {
        dWater[i] = land[i] ? 1e20f : 0.f;
        dLand[i] = land[i] ? 0.f : 1e20f;
    }
    edt2d(dWater, cr);
    edt2d(dLand, cr);
    coarseSdf.resize((size_t)cr * cr);
    for (size_t i = 0; i < land.size(); i++)
        coarseSdf[i] = land[i] ? (sqrtf(dWater[i]) - 0.5f) * coarseCell : -(sqrtf(dLand[i]) - 0.5f) * coarseCell;

    // ------------------------------------------------------------------ fine maps
    const int R = kHeightRes;
    height.assign((size_t)R * R, 0.f);
    waterLevel.assign((size_t)R * R, kNoWater);
    region.assign((size_t)R * R, 0);
    Jobs::parallelFor(R, [&](int ty) {
        for (int tx = 0; tx < R; tx++) {
            float x = texelToWorld(tx), y = texelToWorld(ty);
            // Domain warp for natural coastlines; weaker in the city (seawalls)
            float wx = perlin2(x / 1100.f, y / 1100.f, 3) * 140.f + perlin2(x / 190.f, y / 190.f, 4) * 26.f + perlin2(x / 45.f, y / 45.f, 5) * 6.f;
            float wy = perlin2(x / 1100.f + 7.3f, y / 1100.f, 6) * 140.f + perlin2(x / 190.f, y / 190.f + 2.1f, 7) * 26.f + perlin2(x / 45.f, y / 45.f + 1.7f, 8) * 6.f;
            vec2 pkm(x / 1000.f, y / 1000.f);
            float cityMask = (pkm.x > 2.4f && pkm.x < 4.3f && pkm.y > -2.2f && pkm.y < 4.4f) ? 1.f : 0.f;
            float warpAmt = 1.f - 0.8f * cityMask;
            {
                // Port Isle keeps straight quay lines
                float dPort = Max(Max(kPortX0 - x, x - kPortX1), Max(kPortY0 - y, y - kPortY1));
                warpAmt *= SmoothStep(0.f, 180.f, dPort);
            }
            float sx = (x + wx * warpAmt + kWorldHalf) / coarseCell - 0.5f;
            float sy = (y + wy * warpAmt + kWorldHalf) / coarseCell - 0.5f;
            int ix = Clamp((int)floorf(sx), 0, cr - 2), iy = Clamp((int)floorf(sy), 0, cr - 2);
            float fx = Clamp(sx - ix, 0.f, 1.f), fy = Clamp(sy - iy, 0.f, 1.f);
            const float* g = coarseSdf.data();
            float sdf = Lerp(Lerp(g[iy * cr + ix], g[iy * cr + ix + 1], fx), Lerp(g[(iy + 1) * cr + ix], g[(iy + 1) * cr + ix + 1], fx), fy);
            Region reg = classify(x, y, sdf);
            float h;
            if (sdf < 0.f) {
                float d = -sdf;
                bool bay = pkm.x > 3.7f && pkm.x < 5.0f && pkm.y > -2.8f && pkm.y < 4.6f;
                h = -0.6f - 2.2f * SmoothStep(0.f, 120.f, d) - (bay ? 1.2f : 10.f) * SmoothStep(120.f, 1800.f, d) -
                    (bay ? 0.f : 35.f) * SmoothStep(1800.f, 9000.f, d);
                h += perlin2(x / 300.f, y / 300.f, 9) * (bay ? 0.8f : 2.0f) + perlin2(x / 60.f, y / 60.f, 10) * 0.3f;
            } else {
                float base = 1.3f + 3.4f * SmoothStep(0.f, 3500.f, sdf) + 5.5f * SmoothStep(3500.f, 9000.f, y);
                float n1 = fbm2(x / 1600.f, y / 1600.f, 4, 2.f, 0.5f, 11) * 2.0f;
                float n2 = fbm2(x / 220.f, y / 220.f, 3, 2.f, 0.5f, 12) * 0.45f;
                float urban = regionInfo(reg).urban;
                float amp = 1.f - 0.75f * urban;
                h = base + (n1 + n2) * amp;
                // Beaches: gentle slope into the sea
                float beachW = (reg == REG_BEACH || reg == REG_KEY_CORAL || reg == REG_KEYS || reg == REG_KEY_TOWN) ? 110.f : 45.f;
                if (urban > 0.7f && reg != REG_BEACH) beachW = 14.f;  // seawalls downtown
                float bt = SmoothStep(0.f, beachW, sdf);
                h = Lerp(0.25f, h, bt);
                // Dunes on the barrier island (ocean side)
                if (reg == REG_BEACH || reg == REG_KEY_CORAL) {
                    float dune = ridged2(x / 70.f, y / 140.f, 3, 2.f, 0.5f, 13) * 2.2f;
                    h += dune * SmoothStep(20.f, 70.f, sdf) * (1.f - SmoothStep(120.f, 200.f, sdf));
                }
                if (reg == REG_SAWGRASS || reg == REG_GULF_TOWN) {
                    // Very flat marsh: flow-aligned (NNE-SSW) sloughs slightly below sea level and
                    // teardrop-shaped tree islands (hammocks) rising above.
                    float slough = fbm2(x / 700.f, y / 2600.f, 5, 2.f, 0.55f, 14);
                    float wet = 0.1f + slough * 2.4f + perlin2(x / 70.f, y / 140.f, 15) * 0.12f;
                    vec2 cell;
                    float ax = x + perlin2(x / 500.f, y / 500.f, 25) * 180.f, ay = y + perlin2(x / 500.f, y / 500.f, 26) * 180.f;
                    float wd = worley2(ax / 380.f, ay / 900.f, 16, &cell);
                    float hr = hashToFloat(hash2i((int)floorf(cell.x * 13.f), (int)floorf(cell.y * 13.f)));
                    float hammock = hr > 0.55f ? SmoothStep(0.30f, 0.08f, wd) * (0.8f + hr * 1.3f) : 0.f;
                    // mangrove fringe near the coast sits just above water
                    float fringe = SmoothStep(700.f, 150.f, sdf) * 0.55f;
                    float target = fringe > 0.01f ? Max(wet + hammock, fringe * (0.6f + 0.4f * perlin2(x / 40.f, y / 40.f, 27))) : wet + hammock;
                    float edge = SmoothStep(0.f, 700.f, (pkm.x < -2.95f ? (-2.95f - pkm.x) * 1000.f : 0.f));
                    h = Lerp(h, Min(h, target), Min(1.f, edge + 0.3f));
                }
                if (reg == REG_RIDGE) {
                    vec2 rd = (pkm - vec2(-7.0f, 7.25f)) / vec2(2.7f, 2.3f);
                    float m = SmoothStep(1.0f, 0.35f, length(rd));
                    float rid = ridged2(x / 1400.f, y / 1400.f, 5, 2.1f, 0.5f, 17);
                    h += m * (rid * 170.f + fbm2(x / 400.f, y / 400.f, 3, 2.f, 0.5f, 18) * 12.f);
                }
            }
            // Lake Okahatchee with a surrounding dike
            float wl = sdf < 0.f ? 0.f : kNoWater;
            {
                vec2 lp(x, y);
                float ld = length((lp - vec2(-2400, 7000)) / vec2(2000, 1500));
                bool inLake = ld < 1.35f && inPoly(lp, lakePoly);
                if (inLake) {
                    h = kLakeLevel - 0.6f - 2.4f * SmoothStep(0.f, 0.6f, 1.f - ld) + perlin2(x / 200.f, y / 200.f, 19) * 0.3f;
                    wl = kLakeLevel;
                } else if (ld < 1.12f) {
                    h = Max(h, Lerp(kLakeLevel + 2.6f, h, SmoothStep(1.07f, 1.12f, ld)));
                }
            }
            // Rivers and canals carve down to sea level
            if (sdf > -50.f) {
                for (const Channel& ch : channels) {
                    vec2 cmn(1e9f), cmx(-1e9f);
                    for (auto& cp : ch.pts) { cmn = vmin(cmn, cp); cmx = vmax(cmx, cp); }
                    float margin = Max(ch.width0, ch.width1) + 40.f;
                    if (x < cmn.x - margin || x > cmx.x + margin || y < cmn.y - margin || y > cmx.y + margin) continue;
                    float best = 1e9f, bestT = 0.f;
                    float accLen = 0.f, total = 0.f, bestAcc = 0.f;
                    for (size_t i = 0; i + 1 < ch.pts.size(); i++) total += length(ch.pts[i + 1] - ch.pts[i]);
                    for (size_t i = 0; i + 1 < ch.pts.size(); i++) {
                        float t;
                        float d = distPointSegment2D(vec2(x, y), ch.pts[i], ch.pts[i + 1], &t);
                        float segLen = length(ch.pts[i + 1] - ch.pts[i]);
                        if (d < best) { best = d; bestT = t; bestAcc = accLen + t * segLen; }
                        accLen += segLen;
                    }
                    (void)bestT;
                    // meander the channel slightly
                    float w = Lerp(ch.width0, ch.width1, total > 0 ? bestAcc / total : 0.f) * 0.5f;
                    if (best < w + 25.f) {
                        float bank = SmoothStep(w - 4.f, w + 18.f, best);
                        float bed = -ch.depth;
                        h = Lerp(Min(h, bed), h, bank);
                        if (best < w && wl < -1.f) wl = 0.f;
                    }
                }
            }
            size_t idx = (size_t)ty * R + tx;
            height[idx] = h;
            waterLevel[idx] = (wl > kNoWater + 1.f && wl > h - 0.01f) ? wl : (sdf < 0.f ? 0.f : (h < 0.f ? 0.f : kNoWater));
            region[idx] = (u8)reg;
        }
    }, 8);
    // Special sites shape the terrain (airfield, quays, basins) and reserve their areas before roads are built
    gSites->layout(*this);
    recomputeSplat();
    LOG("World map generated in %.2f s", TimeSeconds() - t0);
}

void WorldMap::recomputeSplat() {
    const int R = kHeightRes;
    splat0.assign((size_t)R * R, 0);
    splat1.assign((size_t)R * R, 0);
    Jobs::parallelFor(R, [&](int ty) {
        for (int tx = 0; tx < R; tx++) {
            size_t idx = (size_t)ty * R + tx;
            float x = texelToWorld(tx), y = texelToWorld(ty);
            float h = height[idx];
            Region reg = (Region)region[idx];
            int x0 = Max(tx - 1, 0), x1 = Min(tx + 1, R - 1), y0 = Max(ty - 1, 0), y1 = Min(ty + 1, R - 1);
            float dx = (height[(size_t)ty * R + x1] - height[(size_t)ty * R + x0]) / ((x1 - x0) * kHeightCell);
            float dy = (height[(size_t)y1 * R + tx] - height[(size_t)y0 * R + tx]) / ((y1 - y0) * kHeightCell);
            float slope = sqrtf(dx * dx + dy * dy);
            float w[TL_COUNT] = {};
            float n = perlin2(x / 60.f, y / 60.f, 30) * 0.5f + 0.5f;
            float n2 = perlin2(x / 400.f, y / 400.f, 31) * 0.5f + 0.5f;
            const RegionInfo& ri = regionInfo(reg);
            if (h < 0.35f) {
                // Seafloor / shoreline
                bool bay = x > 3700 && x < 5000 && y > -2800 && y < 4600;
                w[TL_SAND] = bay ? 0.4f : 1.f;
                w[TL_MUD] = bay ? 0.6f : 0.15f * n;
                if (reg == REG_SAWGRASS || reg == REG_GULF_TOWN) { w[TL_MUD] = 1.f; w[TL_SAND] = 0.1f; w[TL_SAWGRASS] = 0.5f * n; }
                if (waterLevel[idx] > kNoWater + 1 && waterLevel[idx] > 1.f) { w[TL_MUD] = 1.f; w[TL_SAND] = 0.3f; }
            } else if (reg == REG_SAWGRASS || reg == REG_GULF_TOWN) {
                float wet = SmoothStep(0.6f, 0.15f, h);
                w[TL_SAWGRASS] = 1.f - wet * 0.5f;
                w[TL_MUD] = wet;
                w[TL_FOREST] = SmoothStep(0.9f, 1.4f, h) * 1.5f;
                w[TL_DIRT] = 0.15f * n;
            } else if (reg == REG_RIDGE) {
                w[TL_FOREST] = 1.f;
                w[TL_GRASS] = 0.35f * n2;
                w[TL_DIRT] = 0.25f * n;
                w[TL_ROCK] = SmoothStep(0.35f, 0.75f, slope) * 3.f;
            } else if (reg == REG_FARMLAND || reg == REG_REDLAND) {
                // Rectangular fields on a section-line grid (800 m), subdivided into plots
                int gx = (int)floorf(x / 800.f), gy = (int)floorf(y / 800.f);
                u32 gh = hash2i(gx, gy);
                float lx = x - gx * 800.f, ly = y - gy * 800.f;
                int sub = (gh & 1) ? (int)floorf(lx / (800.f / (2 + (gh >> 1) % 3))) : (int)floorf(ly / (800.f / (2 + (gh >> 3) % 3)));
                u32 ch = hashCombine(gh, (u32)sub);
                float fieldType = hashToFloat(ch);
                float edgeD = Min(Min(lx, 800.f - lx), Min(ly, 800.f - ly));
                if (fieldType < 0.35f) { w[TL_GRASS] = 1.f; w[TL_DIRT] = 0.15f * n; }                       // pasture
                else if (fieldType < 0.6f) { w[TL_SAWGRASS] = 1.f; w[TL_GRASS] = 0.3f; }                    // sugar cane
                else if (fieldType < 0.8f) { w[TL_DIRT] = 1.f; w[TL_GRASS] = 0.15f * n; }                   // plowed
                else { w[TL_GRASS] = 0.7f; w[TL_DIRT] = 0.5f; }                                             // groves
                if (edgeD < 10.f) { w[TL_DIRT] += 1.f; }
                w[TL_ROCK] = SmoothStep(0.5f, 0.9f, slope) * 2.f;
            } else {
                // Towns and cities: mix of lawns, bare ground and paved lots by urban density
                float urban = ri.urban;
                w[TL_GRASS] = Lerp(1.f, 0.35f, urban) * (0.6f + 0.4f * n2);
                w[TL_URBAN] = urban * (0.5f + 0.8f * n);
                w[TL_DIRT] = 0.18f * (1.f - n) * (1.f - urban * 0.5f);
                if (reg == REG_BEACH || reg == REG_KEY_CORAL || reg == REG_KEYS || reg == REG_KEY_TOWN) w[TL_SAND] = 0.35f * n;
                if (reg == REG_GROVE || reg == REG_BAY_ISLAND) w[TL_FOREST] = 0.3f * n2;
                if (reg == REG_PORT) { w[TL_URBAN] = 1.f; w[TL_GRASS] = 0.1f * n2; }
                if (reg == REG_AIRPORT) { w[TL_URBAN] = 0.08f; w[TL_GRASS] = 1.f; w[TL_DIRT] = 0.12f * n; }
            }
            // Beach sand near the coast line
            float sandT = beachSand(x, y);
            if (h > 0.35f) for (int k = 0; k < TL_COUNT; k++) w[k] *= (1.f - sandT);
            w[TL_SAND] += sandT * 1.5f;
            float s = 0.f;
            for (int k = 0; k < TL_COUNT; k++) s += w[k];
            if (s < 1e-4f) { w[TL_GRASS] = 1.f; s = 1.f; }
            for (int k = 0; k < TL_COUNT; k++) w[k] /= s;
            splat0[idx] = packRGBA8(w[0], w[1], w[2], w[3]);
            splat1[idx] = packRGBA8(w[4], w[5], w[6], w[7]);
        }
    }, 8);
}

float WorldMap::beachSand(float x, float y) const {
    float sdf = coastDistance(x, y);
    if (sdf <= -40.f) return 0.f;
    Region reg = regionAt(x, y);
    float beachW = (reg == REG_BEACH || reg == REG_KEY_CORAL || reg == REG_KEYS || reg == REG_KEY_TOWN) ? 95.f : 30.f;
    if (regionInfo(reg).urban > 0.7f && reg != REG_BEACH) beachW = 8.f;
    if (reg == REG_SAWGRASS) beachW = 0.f;
    return SmoothStep(beachW + 10.f, beachW * 0.5f, sdf);
}

float WorldMap::coastDistance(float x, float y) const {
    float sx = (x + kWorldHalf) / coarseCell - 0.5f, sy = (y + kWorldHalf) / coarseCell - 0.5f;
    int cr = coarseRes;
    int ix = Clamp((int)floorf(sx), 0, cr - 2), iy = Clamp((int)floorf(sy), 0, cr - 2);
    float fx = Clamp(sx - ix, 0.f, 1.f), fy = Clamp(sy - iy, 0.f, 1.f);
    const float* g = coarseSdf.data();
    return Lerp(Lerp(g[iy * cr + ix], g[iy * cr + ix + 1], fx), Lerp(g[(iy + 1) * cr + ix], g[(iy + 1) * cr + ix + 1], fx), fy);
}

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

}  // namespace World
