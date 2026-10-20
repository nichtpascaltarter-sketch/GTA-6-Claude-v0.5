// Terrain bake for the hand-built world (docs/HANDBUILT_WORLD.md, section 3.2). Runs today's terrain generator once with
// the special sites cut down to their landforms (gTerrainBakeLandforms: Port Isle's quay walls, ring and berth, the Key
// Coral golf pond) and without the roads, and writes the result as data:
//   world/terrain/terrain.ntw   grid geometry, the layers and their hashes, the water levels, the coast, island, lake and
//                               channel outlines
//   world/terrain/height.ntg    2560^2 heights in cm (int16)              NTG grids: src/world/gridcodec.h
//   world/terrain/water.ntg     2560^2 water class (u8), levels in terrain.ntw
//   world/terrain/region.ntg    2560^2 region (u8): today's district map
//   world/terrain/coast.ntg     640^2 signed coast distance (int16, unit in terrain.ntw)
// Every grid is decoded again from disk and compared before the tool reports success.
// Build from the repo root:  g++ -std=c++17 -O2 -I. tools/terrainbake.cpp -o build/terrainbake -lpthread
// Run from the repo root:    build/terrainbake [--out=world/terrain] [--commit=<the generator's commit>]
// Keep the include list in sync with tools/worldcheck.cpp.
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
#include "src/world/ntw.cpp"
#include "src/world/ntwschema.cpp"
#include <thread>
#include <string>
#include <vector>
#include <algorithm>

using namespace World;

namespace terrainbake {

std::string gOut = "world/terrain";
std::string gCommit = "unknown";

bool writeBytes(const std::string& path, const std::vector<u8>& bytes) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        printf("terrainbake: cannot write %s\n", path.c_str());
        return false;
    }
    const size_t n = fwrite(bytes.data(), 1, bytes.size(), f);
    const bool ok = n == bytes.size() && fclose(f) == 0;
    if (!ok) printf("terrainbake: short write to %s\n", path.c_str());
    return ok;
}

bool readBytes(const std::string& path, std::vector<u8>& bytes) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    bytes.clear();
    u8 buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    fclose(f);
    return true;
}

// Encodes, writes, reads back and decodes one grid; returns its hash as 16 hex digits, or "" on failure
std::string bakeGrid(const char* name, const std::vector<i16>& v, int w, int h, GridType type) {
    std::vector<u8> enc;
    if (!encodeGrid(v.data(), w, h, type, enc)) {
        printf("terrainbake: %s: cannot encode\n", name);
        return "";
    }
    const std::string path = gOut + "/" + name + ".ntg";
    if (!writeBytes(path, enc)) return "";
    std::vector<u8> back;
    std::vector<i16> dec;
    GridInfo gi;
    if (!readBytes(path, back) || back != enc || !decodeGrid(back.data(), back.size(), dec, &gi) || dec != v || gi.width != w || gi.height != h) {
        printf("terrainbake: %s: the file does not decode to the same grid\n", name);
        return "";
    }
    const size_t raw = v.size() * (type == GRID_U8 ? 1 : 2);
    printf("  %-8s %5d x %-5d %s  raw %9zu bytes  ntg %9zu bytes (%.1f%%)  hash %016llx\n", name, w, h, type == GRID_U8 ? "u8 " : "i16", raw, enc.size(),
           100.0 * (double)enc.size() / (double)raw, (unsigned long long)gi.hash);
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)gi.hash);
    return hex;
}

std::string fmtPts(const std::vector<vec2>& pts) {
    std::string s;
    char buf[64];
    for (size_t i = 0; i < pts.size(); i++) {
        snprintf(buf, sizeof(buf), "%s%.2f,%.2f", i ? ";" : "", pts[i].x, pts[i].y);
        s += buf;
    }
    return s;
}

// The smallest of the units that fit every value into int16
float pickUnit(float lo, float hi, const float* units, int count) {
    const float m = Max(fabsf(lo), fabsf(hi));
    for (int i = 0; i < count; i++)
        if (m / units[i] <= 32000.f) return units[i];
    return 0.f;
}

int run() {
    const double t0 = TimeSeconds();
    gTerrainBakeLandforms = true;
    WorldMap* map = new WorldMap;
    map->generate();
    printf("terrainbake: terrain generated in %.2f s\n", TimeSeconds() - t0);
    const int R = kHeightRes;
    const size_t n = (size_t)R * R;
    if (map->height.size() != n || map->waterLevel.size() != n || map->region.size() != n || map->coarseRes <= 0 ||
        map->coarseSdf.size() != (size_t)map->coarseRes * map->coarseRes) {
        printf("terrainbake: unexpected map layout\n");
        return 1;
    }
    // heights in cm
    float hLo = 1e9f, hHi = -1e9f;
    for (float v : map->height) {
        hLo = Min(hLo, v);
        hHi = Max(hHi, v);
    }
    if (hLo < -327.f || hHi > 327.f) {
        printf("terrainbake: heights %.2f..%.2f m do not fit int16 cm\n", hLo, hHi);
        return 1;
    }
    std::vector<i16> height(n);
    for (size_t i = 0; i < n; i++) height[i] = (i16)lrintf(map->height[i] * 100.f);
    // water: a class per texel and a short table of levels (class 0: no water)
    std::vector<float> levels;
    for (float w : map->waterLevel)
        if (w > kNoWater + 1.f && std::find(levels.begin(), levels.end(), w) == levels.end()) {
            levels.push_back(w);
            if (levels.size() > 254) break;
        }
    if (levels.size() > 254) {
        printf("terrainbake: more than 254 water levels; the water needs a level grid\n");
        return 1;
    }
    std::sort(levels.begin(), levels.end());
    std::vector<i16> water(n);
    for (size_t i = 0; i < n; i++) {
        const float w = map->waterLevel[i];
        water[i] = w > kNoWater + 1.f ? (i16)(1 + (std::find(levels.begin(), levels.end(), w) - levels.begin())) : (i16)0;
    }
    std::vector<i16> region(n);
    for (size_t i = 0; i < n; i++) region[i] = map->region[i];
    // coarse coast distance
    const int CR = map->coarseRes;
    float cLo = 1e9f, cHi = -1e9f;
    for (float v : map->coarseSdf) {
        cLo = Min(cLo, v);
        cHi = Max(cHi, v);
    }
    const float units[] = {0.125f, 0.25f, 0.5f, 1.f, 2.f};
    const float cUnit = pickUnit(cLo, cHi, units, 5);
    if (cUnit <= 0.f) {
        printf("terrainbake: coast distance %.1f..%.1f m does not fit int16\n", cLo, cHi);
        return 1;
    }
    std::vector<i16> coast((size_t)CR * CR);
    for (size_t i = 0; i < coast.size(); i++) coast[i] = (i16)lrintf(map->coarseSdf[i] / cUnit);
    printf("terrainbake: heights %.2f..%.2f m; %zu water levels; coast distance %.1f..%.1f m at %.3f m; writing %s\n", hLo, hHi, levels.size(), cLo,
           cHi, cUnit, gOut.c_str());
    const std::string hHash = bakeGrid("height", height, R, R, GRID_I16);
    const std::string wHash = bakeGrid("water", water, R, R, GRID_U8);
    const std::string rHash = bakeGrid("region", region, R, R, GRID_U8);
    const std::string cHash = bakeGrid("coast", coast, CR, CR, GRID_I16);
    if (hHash.empty() || wHash.empty() || rHash.empty() || cHash.empty()) return 1;
    // terrain.ntw
    std::string t;
    char buf[512];
    t += "ntw 1 terrain\n";
    t += "# The baked terrain of Palmera (docs/HANDBUILT_WORLD.md, section 3.2), written by tools/terrainbake.cpp from today's\n";
    t += "# generator: the natural terrain, Port Isle's quay walls, ring and berth, and the Key Coral golf pond. No roads, pads\n";
    t += "# or site grading. Don't edit it by hand: re-bake it, or change the grids with the terrain tools.\n";
    snprintf(buf, sizeof(buf), "source tool=terrainbake generator=procedural seed=0x%08X commit=%s\n", kWorldSeed, gCommit.c_str());
    t += buf;
    snprintf(buf, sizeof(buf), "grid res=%d cell=%.2f origin=%.2f,%.2f\n", R, kHeightCell, -kWorldHalf, -kWorldHalf);
    t += buf;
    snprintf(buf, sizeof(buf), "layer height file=height.ntg type=i16 unit=0.01 hash=%s range=%.2f,%.2f\n", hHash.c_str(), hLo, hHi);
    t += buf;
    snprintf(buf, sizeof(buf), "layer water file=water.ntg type=u8 hash=%s\n", wHash.c_str());
    t += buf;
    t += "water c0 level=none\n";
    for (size_t i = 0; i < levels.size(); i++) {
        snprintf(buf, sizeof(buf), "water c%zu level=%.4f\n", i + 1, levels[i]);
        t += buf;
    }
    snprintf(buf, sizeof(buf), "layer region file=region.ntg type=u8 hash=%s\n", rHash.c_str());
    t += buf;
    snprintf(buf, sizeof(buf), "layer coast file=coast.ntg type=i16 res=%d cell=%.2f unit=%.3f hash=%s\n", CR, map->coarseCell, cUnit, cHash.c_str());
    t += buf;
    auto outline = [&](const char* id, const std::vector<vec2>& pts) {
        if (pts.empty()) return;
        t += std::string("outline ") + id + " pts=" + fmtPts(pts) + "\n";
    };
    outline("mainland", map->mainland);
    outline("sol_beach", map->solBeach);
    outline("key_coral", map->keyCoral);
    outline("port_isle", map->portIsle);
    for (size_t i = 0; i < map->smallIslands.size(); i++) {
        snprintf(buf, sizeof(buf), "island.%02zu", i);
        outline(buf, map->smallIslands[i]);
    }
    outline("lake", map->lakePoly);
    for (size_t i = 0; i < map->channels.size(); i++) {
        const Channel& ch = map->channels[i];
        snprintf(buf, sizeof(buf), "channel ch.%02zu width0=%.2f width1=%.2f depth=%.2f pts=", i, ch.width0, ch.width1, ch.depth);
        t += buf + fmtPts(ch.pts) + "\n";
    }
    // in its canonical form (tools/ntwfmt.cpp), and checked against the schema
    Ntw::Document doc;
    std::vector<Ntw::Error> errs;
    if (!Ntw::parse(t.data(), t.size(), Ntw::schemaOpensBlock, doc, errs)) {
        for (const Ntw::Error& e : errs) printf("terrainbake: terrain.ntw:%d: %s\n", e.line, e.message.c_str());
        return 1;
    }
    Ntw::checkDocument(doc, errs);
    for (const Ntw::Error& e : errs) printf("terrainbake: terrain.ntw:%d: %s\n", e.line, e.message.c_str());
    if (!errs.empty()) return 1;
    t = Ntw::format(doc, Ntw::schemaFormatRules());
    if (!writeBytes(gOut + "/terrain.ntw", std::vector<u8>(t.begin(), t.end()))) return 1;
    printf("terrainbake: terrain.ntw %zu bytes; %zu outlines of islands, %zu channels; done in %.2f s\n", t.size(), map->smallIslands.size(),
           map->channels.size(), TimeSeconds() - t0);
    return 0;
}

}  // namespace terrainbake

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        if (a.rfind("--out=", 0) == 0) terrainbake::gOut = a.substr(6);
        else if (a.rfind("--commit=", 0) == 0) terrainbake::gCommit = a.substr(9);
        else {
            printf("usage: terrainbake [--out=world/terrain] [--commit=<sha>]\n");
            return 2;
        }
    }
    Jobs::init(Max(1, (int)std::thread::hardware_concurrency() - 1));
    const int rc = terrainbake::run();
    Jobs::shutdown();
    return rc;
}
