// Native tool: generates the world map and writes a colored preview image (PPM).
#include "native_stubs.cpp"
#include "../src/core/math.cpp"
#include "../src/core/noise.cpp"
#include "../src/core/jobs.cpp"
#include "../src/world/worldmap.cpp"
#include "../src/world/sites.cpp"
#include "../src/world/transit.cpp"
#include "../src/world/roads.cpp"
#include "../src/world/buildings.cpp"
#include <thread>
int main(int argc, char** argv) {
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    World::WorldMap m;
    m.generate();
    World::RoadNetwork roads;
    roads.generate(m);
    World::BuildingSet bset;
    bset.generate(m, roads);
    const int R = World::kHeightRes;
    int step = argc > 2 ? atoi(argv[2]) : 2;
    int W = R / step;
    FILE* f = fopen(argc > 1 ? argv[1] : "/tmp/map.ppm", "wb");
    fprintf(f, "P6 %d %d 255\n", W, W);
    for (int y = W - 1; y >= 0; y--)
        for (int x = 0; x < W; x++) {
            size_t i = (size_t)(y * step) * R + x * step;
            float h = m.height[i], wl = m.waterLevel[i];
            vec3 c;
            if (wl > World::kNoWater + 1 && wl > h) {
                float d = wl - h;
                c = lerp(vec3(0.3f, 0.7f, 0.75f), vec3(0.02f, 0.12f, 0.35f), Saturate(d / 20.f));
            } else {
                vec4 s0 = unpackRGBA8(m.splat0[i]), s1 = unpackRGBA8(m.splat1[i]);
                vec3 cols[8] = {vec3(0.9f, 0.85f, 0.6f), vec3(0.35f, 0.6f, 0.25f), vec3(0.5f, 0.4f, 0.28f), vec3(0.5f, 0.5f, 0.5f),
                                vec3(0.3f, 0.28f, 0.2f), vec3(0.6f, 0.6f, 0.3f), vec3(0.15f, 0.35f, 0.15f), vec3(0.6f, 0.6f, 0.62f)};
                float w[8] = {s0.x, s0.y, s0.z, s0.w, s1.x, s1.y, s1.z, s1.w};
                for (int k = 0; k < 8; k++) c += cols[k] * w[k];
                c *= 0.75f + 0.25f * Saturate(h / 20.f) + Saturate((h - 20.f) / 150.f) * 0.5f;
                vec3 n = m.normalAt(World::texelToWorld(x * step), World::texelToWorld(y * step));
                c *= 0.7f + 0.3f * Saturate(dot(n, normalize(vec3(-1, 1, 2))) * 1.2f);
            }
            unsigned char px[3] = {(unsigned char)(Saturate(c.x) * 255), (unsigned char)(Saturate(c.y) * 255), (unsigned char)(Saturate(c.z) * 255)};
            fwrite(px, 1, 3, f);
        }
    fclose(f);
    // Overlay roads: rasterize into a second image
    std::vector<unsigned char> img((size_t)W * W * 3);
    {
        FILE* r = fopen(argc > 1 ? argv[1] : "/tmp/map.ppm", "rb");
        char hdr[64]; int ww, hh, mx;
        fscanf(r, "%2s %d %d %d", hdr, &ww, &hh, &mx); fgetc(r);
        fread(img.data(), 1, img.size(), r); fclose(r);
    }
    float scale = (float)W / (2.f * World::kWorldHalf);
    auto plot = [&](float x, float y, vec3 c) {
        int px = (int)((x + World::kWorldHalf) * scale), py = W - 1 - (int)((y + World::kWorldHalf) * scale);
        if (px < 0 || py < 0 || px >= W || py >= W) return;
        unsigned char* d = &img[((size_t)py * W + px) * 3];
        d[0] = (unsigned char)(c.x * 255); d[1] = (unsigned char)(c.y * 255); d[2] = (unsigned char)(c.z * 255);
    };
    // Site pads (runways dark, taxiways/aprons light gray, decks brown) and element anchors
    for (auto& pd : World::gSites->pads) {
        vec3 col = pd.kind == World::PAD_RUNWAY ? vec3(0.12f) : pd.kind == World::PAD_DECK ? vec3(0.55f, 0.4f, 0.25f)
                 : pd.kind == World::PAD_YARD ? vec3(0.78f) : pd.kind == World::PAD_PLAZA ? vec3(0.85f, 0.75f, 0.6f) : vec3(0.62f);
        vec2 ay = perp(pd.ax);
        for (float u = -pd.hx; u <= pd.hx; u += 4.f)
            for (float v = -pd.hy; v <= pd.hy; v += 4.f) { vec2 q = pd.c + pd.ax * u + ay * v; plot(q.x, q.y, col); }
    }
    for (auto& e : World::gSites->elems) {
        vec3 col = e.kind < 40 ? vec3(1, 0.2f, 0.2f) : e.kind < 80 ? vec3(0.2f, 0.9f, 1) : e.kind < 100 ? vec3(0.2f, 1, 0.3f) : vec3(1, 0.3f, 1);
        if (e.kind == World::SK_CONTAINER_BLOCK) col = vec3(0.9f, 0.5f, 0.1f);
        if (e.kind == World::SK_RUNWAY || e.kind == World::SK_TAXI_MARKS || e.kind == World::SK_APRON_MARKS || e.kind == World::SK_FENCE) continue;
        if (!e.pts.empty() && e.kind != World::SK_BILLBOARD) {
            for (size_t i = 0; i + 1 < e.pts.size(); i++)
                for (int k = 0; k <= 8; k++) { vec2 q = lerp(e.pts[i], e.pts[i + 1], k / 8.f); plot(q.x, q.y, col); }
            continue;
        }
        vec2 ay = perp(e.ax);
        float hx = Min(e.hx, 400.f), hy = Min(e.hy, 400.f);
        for (float u = -hx; u <= hx; u += 6.f)
            for (float v = -hy; v <= hy; v += 6.f) {
                if (fabsf(u) < hx - 6.f && fabsf(v) < hy - 6.f) continue;  // outline only
                vec2 q = e.c + e.ax * u + ay * v;
                plot(q.x, q.y, col);
            }
        plot(e.c.x, e.c.y, vec3(1));
    }
    for (auto& e : roads.edges) {
        vec3 col = e.cls == World::RC_HIGHWAY ? vec3(1, 0.55f, 0.1f) : e.cls == World::RC_BOULEVARD ? vec3(1, 1, 0.3f) : e.cls == World::RC_RAMP ? vec3(1, 0.3f, 0.3f)
                 : e.cls == World::RC_RURAL ? vec3(0.9f, 0.85f, 0.7f) : e.cls == World::RC_DIRT ? vec3(0.6f, 0.45f, 0.3f) : vec3(0.95f, 0.95f, 0.95f);
        if (e.flags & World::RF_BRIDGE) col = col * 0.6f + vec3(0.4f, 0, 0.4f);
        for (size_t i = 0; i + 1 < e.pts.size(); i++) {
            vec2 a = e.pts[i].xy(), b = e.pts[i + 1].xy();
            int n = (int)(length(b - a) * scale * 2) + 1;
            for (int k = 0; k <= n; k++) { vec2 p = lerp(a, b, (float)k / n); plot(p.x, p.y, col); }
        }
    }
    for (auto& nd : roads.nodes) if (nd.control == 2) plot(nd.p.x, nd.p.y, vec3(0, 1, 0));
    for (auto& b : bset.buildings) {
        vec3 col = b.style == World::BS_TOWER ? vec3(0.2f, 0.3f, 0.9f) : b.style == World::BS_HOUSE ? vec3(0.9f, 0.5f, 0.4f) : vec3(0.6f, 0.2f, 0.7f);
        vec2 ay = perp(b.ax);
        for (float u = -b.hx; u <= b.hx; u += 2.f)
            for (float v = -b.hy; v <= b.hy; v += 2.f) { vec2 q = b.c + b.ax * u + ay * v; plot(q.x, q.y, col); }
    }
    FILE* o = fopen(argc > 1 ? argv[1] : "/tmp/map.ppm", "wb");
    fprintf(o, "P6 %d %d 255\n", W, W);
    fwrite(img.data(), 1, img.size(), o);
    fclose(o);
    Jobs::shutdown();
    return 0;
}
