// Native tool: generates the world map and writes a colored preview image (PPM).
#include "native_stubs.cpp"
#include "../src/core/math.cpp"
#include "../src/core/noise.cpp"
#include "../src/core/jobs.cpp"
#include "../src/world/worldmap.cpp"
#include <thread>
int main(int argc, char** argv) {
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    World::WorldMap m;
    m.generate();
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
    Jobs::shutdown();
    return 0;
}
