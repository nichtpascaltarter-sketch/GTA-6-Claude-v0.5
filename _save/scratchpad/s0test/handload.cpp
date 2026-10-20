// Host test for stage 0 step 2 (scratch only): load world.bin through the game's loader, compare it with the bake's
// source (WorldMap::generate with gTerrainBakeLandforms), and check that hand mode generates empty cells and that empty
// roads and buildings answer queries. Build from a tree root: g++ -std=c++17 -O2 -I. handload.cpp -lpthread
#include "tools/native_stubs.cpp"
#include "src/core/math.cpp"
#include "src/core/noise.cpp"
#include "src/core/jobs.cpp"
#include "src/render/mesh.cpp"
#include "src/world/worldmap.cpp"
#include "src/world/sites.cpp"
#include "src/world/roads.cpp"
#include "src/world/roadmesh.cpp"
#include "src/world/buildings.cpp"
#include "src/world/buildmesh.cpp"
#include "src/world/propmesh.cpp"
#include "src/world/cellgen.cpp"
#include "src/world/sitegeo.cpp"
#include "src/world/airport.cpp"
#include "src/world/port.cpp"
#include "src/world/landmarks.cpp"
#include "src/world/leisure.cpp"
#include "src/world/rural.cpp"
#include "src/world/transit.cpp"
#include "src/world/transitmesh.cpp"
#include "src/world/sitecell.cpp"
#include "src/world/facadedetail.cpp"
#include "src/world/interiorkit.cpp"
#include "src/world/interiorfurniture.cpp"
#include "src/world/interiorlayouts.cpp"
#include "src/world/interiorhomes.cpp"
#include "src/world/interiorvenues.cpp"
#include "src/world/interiorshops.cpp"
#include "src/world/interiorcivic.cpp"
#include "src/world/interiorindustrial.cpp"
#include "src/world/interiortower.cpp"
#include "src/world/interiorgarages.cpp"
#include "src/world/interiorresidences.cpp"
#include "src/world/interiors.cpp"
#include "src/world/gridcodec.cpp"
#if __has_include("src/world/lz.cpp")
#include "src/world/lz.cpp"
#endif
#include "src/world/worldbin.cpp"
#include "src/world/handworld.cpp"
#include <thread>

using namespace World;

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static float maxOutlineDiff(const std::vector<vec2>& a, const std::vector<vec2>& b) {
    if (a.size() != b.size()) return 1e9f;
    float m = 0.f;
    for (size_t i = 0; i < a.size(); i++) m = Max(m, Max(fabsf(a[i].x - b[i].x), fabsf(a[i].y - b[i].y)));
    return m;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) { printf("usage: handload <world.bin>\n"); return 2; }
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    // 1. load through the game's loader (the --worldfile path)
    WorldMap* A = new WorldMap;
    RoadNetwork roads;
    BuildingSet buildings;
    std::string err;
    double t0 = TimeSeconds();
    const bool ok = loadHandWorld(argv[1], *A, roads, buildings, &err);
    const double tLoad = TimeSeconds() - t0;
    CHECK(ok, "loadHandWorld: %s", err.c_str());
    if (!ok) return 1;
    printf("loaded in %.2f s\n", tLoad);
    // 2. the bake's source
    gTerrainBakeLandforms = true;
    WorldMap* B = new WorldMap;
    B->generate();
    gTerrainBakeLandforms = false;
    const size_t n = (size_t)kHeightRes * kHeightRes;
    double hMax = 0;
    size_t wDiff = 0, rDiff = 0, s0Diff = 0, s1Diff = 0;
    for (size_t i = 0; i < n; i++) {
        hMax = Max(hMax, (double)fabsf(A->height[i] - B->height[i]));
        wDiff += A->waterLevel[i] != B->waterLevel[i];
        rDiff += A->region[i] != B->region[i];
        s0Diff += A->splat0[i] != B->splat0[i];
        s1Diff += A->splat1[i] != B->splat1[i];
    }
    double cMax = 0;
    CHECK(A->coarseRes == B->coarseRes && A->coarseCell == B->coarseCell, "coast grid %d/%g vs %d/%g", A->coarseRes, A->coarseCell, B->coarseRes, B->coarseCell);
    for (size_t i = 0; i < A->coarseSdf.size() && i < B->coarseSdf.size(); i++) cMax = Max(cMax, (double)fabsf(A->coarseSdf[i] - B->coarseSdf[i]));
    printf("heights max diff %.4f m; water differs on %zu texels; region on %zu; coast distance max diff %.3f m; splat differs on %zu / %zu texels (%.3f%%)\n",
           hMax, wDiff, rDiff, cMax, s0Diff, s1Diff, 100.0 * (double)Max(s0Diff, s1Diff) / (double)n);
    CHECK(hMax <= 0.0051, "heights differ by %.4f m", hMax);
    CHECK(wDiff == 0, "water differs on %zu texels", wDiff);
    CHECK(rDiff == 0, "regions differ on %zu texels", rDiff);
    CHECK(cMax <= 0.51, "coast distance differs by %.3f m", cMax);
    const float om = Max(Max(maxOutlineDiff(A->mainland, B->mainland), maxOutlineDiff(A->solBeach, B->solBeach)),
                         Max(maxOutlineDiff(A->keyCoral, B->keyCoral), Max(maxOutlineDiff(A->portIsle, B->portIsle), maxOutlineDiff(A->lakePoly, B->lakePoly))));
    float im = 0.f;
    CHECK(A->smallIslands.size() == B->smallIslands.size(), "islands %zu vs %zu", A->smallIslands.size(), B->smallIslands.size());
    for (size_t i = 0; i < A->smallIslands.size() && i < B->smallIslands.size(); i++) im = Max(im, maxOutlineDiff(A->smallIslands[i], B->smallIslands[i]));
    float chm = 0.f;
    CHECK(A->channels.size() == B->channels.size(), "channels %zu vs %zu", A->channels.size(), B->channels.size());
    for (size_t i = 0; i < A->channels.size() && i < B->channels.size(); i++) {
        chm = Max(chm, maxOutlineDiff(A->channels[i].pts, B->channels[i].pts));
        CHECK(A->channels[i].width0 == B->channels[i].width0 && A->channels[i].width1 == B->channels[i].width1 && A->channels[i].depth == B->channels[i].depth,
              "channel %zu widths or depth differ", i);
    }
    printf("outlines max diff %.4f m, islands %.4f m (%zu), channels %.4f m (%zu)\n", om, im, A->smallIslands.size(), chm, A->channels.size());
    CHECK(om <= 0.0051f && im <= 0.0051f && chm <= 0.0051f, "outlines differ by more than the 2-decimal rounding");
    // 3. the globals and the empty sets
    CHECK(gMap == A && gRoads == &roads && gBuildings == &buildings, "globals not set");
    gWorldKind = WorldKind::Hand;
    int nonEmpty = 0;
    for (int cy = 0; cy < kCellsPerSide; cy += 7)
        for (int cx = 0; cx < kCellsPerSide; cx += 7)
            for (int d = 0; d < 2; d++) {
                CellGeometry geo;
                generateCell(cx, cy, d == 1, geo);
                nonEmpty += !geo.opaque.verts.empty() || !geo.decals.verts.empty() || !geo.props.empty() || !geo.lights.empty() || !geo.collision.empty();
            }
    CHECK(nonEmpty == 0, "%d hand-mode cells were not empty", nonEmpty);
    float s = 0.f, z = 0.f;
    CHECK(roads.nearestEdge(vec2(1780.f, 160.f), 400.f, &s) == -1, "an empty road network found an edge");
    CHECK(!roads.surfaceHeight(vec2(1780.f, 160.f), &z), "an empty road network has a surface");
    CHECK(!roads.nearRoad(vec2(0.f, 0.f), 50.f), "an empty road network is near");
    std::vector<int> cand;
    roads.edgesInRect(vec2(-10000.f), vec2(10000.f), cand);
    CHECK(cand.empty(), "edgesInRect found %zu", cand.size());
    CHECK(!buildings.pointInBuilding(vec2(1780.f, 160.f), 5.f), "an empty building set has a building");
    std::vector<int> near2;
    buildings.buildingsNear(vec2(1780.f, 160.f), 500.f, near2);
    CHECK(near2.empty(), "buildingsNear found %zu", near2.size());
    printf("heightAt(1780,160) = %.2f m, water %s, region %s, coast distance %.1f m\n", A->heightAt(1780.f, 160.f), A->isWater(1780.f, 160.f) ? "yes" : "no",
           regionInfo(A->regionAt(1780.f, 160.f)).name, A->coastDistance(1780.f, 160.f));
    // 4. a damaged file is refused
    {
        FILE* f = fopen(argv[1], "rb");
        std::vector<u8> bytes;
        if (f) {
            u8 buf[65536];
            size_t k;
            while ((k = fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + k);
            fclose(f);
        }
        bytes[bytes.size() / 2] ^= 0x10;
        WorldBinView v;
        std::string why;
        CHECK(!v.open(bytes.data(), bytes.size(), &why), "a damaged world.bin opened");
        printf("damaged world.bin refused: %s\n", why.c_str());
        WorldBinView v2;
        CHECK(!v2.open(bytes.data(), 100, &why), "a truncated world.bin opened");
        bytes[bytes.size() / 2] ^= 0x10;   // undo; then damage an unused name byte of the first table entry, which
        bytes[24 + 20] ^= 0x01;            // nothing but the hash reads (a hash from the data start would miss it)
        WorldBinView v3;
        const bool tableHit = !v3.open(bytes.data(), bytes.size(), &why) && why.find("hash") != std::string::npos;
        CHECK(tableHit, "a damaged section table opened");
        bytes[24 + 20] ^= 0x01;
        WorldBinView v4;
        CHECK(v4.open(bytes.data(), bytes.size(), &why), "the repaired world.bin did not open");
        printf("damaged table entry %s\n", tableHit ? ("refused: " + why).c_str() : "OPENED");
    }
    printf(failures ? "handload: %d FAILURES\n" : "handload: ALL OK\n", failures);
    Jobs::shutdown();
    return failures ? 1 : 0;
}
