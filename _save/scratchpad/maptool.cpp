// Native helper: region map + roads + grid + labeled points; point queries.
#include "/home/user/GTA-6-Claude-v0.5/tools/native_stubs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/math.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/noise.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/core/jobs.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/worldmap.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/sites.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/roads.cpp"
#include "/home/user/GTA-6-Claude-v0.5/src/world/buildings.cpp"
#include <thread>
#include <cstring>
using namespace World;
static vec3 regionColor(int r) {
    static const vec3 c[] = {vec3(0.1f,0.2f,0.45f), vec3(0.8f,0.2f,0.2f), vec3(0.9f,0.5f,0.1f), vec3(0.8f,0.3f,0.7f), vec3(0.6f,0.6f,0.3f),
        vec3(0.95f,0.8f,0.2f), vec3(0.95f,0.9f,0.6f), vec3(0.4f,0.9f,0.7f), vec3(0.3f,0.8f,0.9f), vec3(0.5f,0.5f,0.55f),
        vec3(0.3f,0.7f,0.3f), vec3(0.7f,0.7f,0.9f), vec3(0.55f,0.4f,0.3f), vec3(0.6f,0.8f,0.5f), vec3(0.7f,0.6f,0.3f),
        vec3(0.4f,0.55f,0.3f), vec3(0.9f,0.6f,0.6f), vec3(0.8f,0.75f,0.45f), vec3(0.5f,0.7f,0.9f), vec3(0.75f,0.55f,0.75f),
        vec3(0.2f,0.45f,0.2f), vec3(0.6f,0.3f,0.3f), vec3(0.6f,0.9f,0.9f), vec3(1.f,0.4f,0.6f)};
    return c[r % 24];
}
int main(int argc, char** argv) {
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    WorldMap m; m.generate(); gMap = &m;
    RoadNetwork roads; roads.generate(m); gRoads = &roads;
    BuildingSet bset; bset.generate(m, roads); gBuildings = &bset;
    // args: out.ppm x0 y0 x1 y1 pixels  [points file]
    const char* out = argc > 1 ? argv[1] : "map.ppm";
    float x0 = argc > 5 ? atof(argv[2]) : -10240, y0 = argc > 5 ? atof(argv[3]) : -10240, x1 = argc > 5 ? atof(argv[4]) : 10240, y1 = argc > 5 ? atof(argv[5]) : 10240;
    int W = argc > 6 ? atoi(argv[6]) : 1600;
    int H = (int)(W * (y1 - y0) / (x1 - x0));
    std::vector<unsigned char> img((size_t)W * H * 3);
    auto put = [&](int px, int py, vec3 c) { if (px < 0 || py < 0 || px >= W || py >= H) return; unsigned char* d = &img[((size_t)py * W + px) * 3];
        d[0] = (unsigned char)(Saturate(c.x) * 255); d[1] = (unsigned char)(Saturate(c.y) * 255); d[2] = (unsigned char)(Saturate(c.z) * 255); };
    auto toPx = [&](float x, float y, int& px, int& py) { px = (int)((x - x0) / (x1 - x0) * W); py = H - 1 - (int)((y - y0) / (y1 - y0) * H); };
    for (int py = 0; py < H; py++) for (int px = 0; px < W; px++) {
        float x = x0 + (px + 0.5f) / W * (x1 - x0), y = y0 + (H - 1 - py + 0.5f) / H * (y1 - y0);
        vec3 c = m.isWater(x, y) ? vec3(0.05f, 0.15f, 0.35f) : regionColor(m.regionAt(x, y)) * 0.55f;
        put(px, py, c);
    }
    for (auto& b : bset.buildings) {
        vec2 ay = perp(b.ax);
        for (float u = -b.hx; u <= b.hx; u += 3.f) for (float v = -b.hy; v <= b.hy; v += 3.f) { vec2 q = b.c + b.ax * u + ay * v; int px, py; toPx(q.x, q.y, px, py); put(px, py, vec3(0.25f)); }
    }
    for (auto& e : roads.edges) {
        vec3 col = e.cls == RC_HIGHWAY ? vec3(1, 0.55f, 0.1f) : e.cls == RC_BOULEVARD ? vec3(1, 1, 0.3f) : e.cls == RC_RAMP ? vec3(1, 0.3f, 0.3f)
                 : e.cls == RC_RURAL ? vec3(0.9f, 0.85f, 0.7f) : e.cls == RC_DIRT ? vec3(0.6f, 0.45f, 0.3f) : vec3(0.95f, 0.95f, 0.95f);
        if (e.flags & RF_BRIDGE) col = vec3(1.f, 0.2f, 1.f);
        for (size_t i = 0; i + 1 < e.pts.size(); i++) {
            vec2 a = e.pts[i].xy(), b = e.pts[i + 1].xy();
            int n = (int)(length(b - a) / (x1 - x0) * W * 2) + 1;
            for (int k = 0; k <= n; k++) { vec2 p = lerp(a, b, (float)k / n); int px, py; toPx(p.x, p.y, px, py); put(px, py, col); }
        }
    }
    // grid every 500 m
    for (float gx = ceilf(x0 / 500.f) * 500.f; gx <= x1; gx += 500.f) { int px, py; toPx(gx, 0, px, py); for (int y = 0; y < H; y += 2) put(px, y, fmodf(fabsf(gx), 1000.f) < 1 ? vec3(0.f, 1.f, 0.f) : vec3(0.f, 0.5f, 0.f)); }
    for (float gy = ceilf(y0 / 500.f) * 500.f; gy <= y1; gy += 500.f) { int px, py; toPx(0, gy, px, py); for (int x = 0; x < W; x += 2) put(x, py, fmodf(fabsf(gy), 1000.f) < 1 ? vec3(0.f, 1.f, 0.f) : vec3(0.f, 0.5f, 0.f)); }
    // points
    if (argc > 7) {
        FILE* pf = fopen(argv[7], "r");
        char name[128]; float x, y;
        while (pf && fscanf(pf, "%127s %f %f", name, &x, &y) == 3) {
            int px, py; toPx(x, y, px, py);
            for (int dy = -4; dy <= 4; dy++) for (int dx = -4; dx <= 4; dx++) if (dx * dx + dy * dy <= 16) put(px + dx, py + dy, vec3(1, 0, 0));
            float s = 0, d = 0, side = 0; int e = roads.nearestEdge(vec2(x, y), 400.f, &s, &d, &side);
            printf("%-18s (%7.0f,%7.0f) %-22s water=%d h=%.1f edge=%d cls=%d d=%.1f name=%s bridge=%d inBld=%d\n", name, x, y, regionInfo(m.regionAt(x, y)).name,
                   (int)m.isWater(x, y), m.heightAt(x, y), e, e >= 0 ? (int)roads.edges[e].cls : -1, d, e >= 0 ? roads.edges[e].name.c_str() : "-",
                   e >= 0 ? (roads.edges[e].flags & RF_BRIDGE) != 0 : 0, (int)bset.pointInBuilding(vec2(x, y), 1.f));
        }
        if (pf) fclose(pf);
    }
    FILE* o = fopen(out, "wb"); fprintf(o, "P6 %d %d 255\n", W, H); fwrite(img.data(), 1, img.size(), o); fclose(o);
    Jobs::shutdown();
    return 0;
}
