// The hand-built world at runtime (handworld.h). Stage 0: the baked terrain from world.bin.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "handworld.h"
#include "gridcodec.h"
#include "roads.h"
#include "buildings.h"
#include "worldtypes.h"
#include "../core/jobs.h"
#include <cstdio>
#include <cstring>

namespace World {

namespace handworld_detail {

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

bool readFile(const char* path, std::vector<u8>& out) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    out.clear();
    u8 buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
    fclose(f);
    return !out.empty();
}

// The terrain record ("terrain" section, written by tools/worldc.cpp)
struct TerrainRecord {
    u32 res = 0;
    float cell = 0.f, originX = 0.f, originY = 0.f;
    float heightUnit = 0.f;
    std::vector<float> waterLevels;   // water class c >= 1 has level waterLevels[c - 1]; class 0 is dry
    u32 coastRes = 0;
    float coastCell = 0.f, coastUnit = 0.f;
};

}  // namespace handworld_detail

bool selectWorldKind(const char* value, std::string* error) {
    gWorldKind = WorldKind::Procedural;
    if (!value || !*value || strcmp(value, "procedural") == 0) return true;
    if (strcmp(value, "hand") == 0) {
        gWorldKind = WorldKind::Hand;
        return true;
    }
    if (error) *error = std::string("unknown --world '") + value + "' (hand or procedural)";
    return false;
}

const u8* embeddedWorldBin(size_t* size) {
    *size = 0;
#ifdef _WIN32
    HRSRC res = FindResourceA(nullptr, "NT_WORLD", MAKEINTRESOURCEA(10));   // RT_RCDATA
    if (!res) return nullptr;
    HGLOBAL mem = LoadResource(nullptr, res);
    const DWORD n = SizeofResource(nullptr, res);
    const void* p = mem ? LockResource(mem) : nullptr;
    if (!p || n == 0) return nullptr;
    *size = n;
    return (const u8*)p;
#else
    return nullptr;
#endif
}

bool loadHandTerrain(const WorldBinView& bin, WorldMap& map, std::string* error) {
    using namespace handworld_detail;
    std::vector<u8> meta;
    if (!bin.bytes("terrain", meta)) return fail(error, "world.bin has no terrain record");
    ByteReader r(meta.data(), meta.size());
    if (r.getU32() != 1) return fail(error, "unknown terrain record version");
    TerrainRecord t;
    t.res = r.getU32();
    t.cell = r.getF32();
    t.originX = r.getF32();
    t.originY = r.getF32();
    t.heightUnit = r.getF32();
    const u32 levels = r.getU32();
    if (levels > 254) return fail(error, "too many water levels");
    for (u32 i = 0; i < levels; i++) t.waterLevels.push_back(r.getF32());
    t.coastRes = r.getU32();
    t.coastCell = r.getF32();
    t.coastUnit = r.getF32();
    if (r.bad) return fail(error, "truncated terrain record");
    if ((int)t.res != kHeightRes || t.cell != kHeightCell || t.originX != -kWorldHalf || t.originY != -kWorldHalf)
        return fail(error, "the baked terrain's grid does not match this build's world size");
    if (t.heightUnit <= 0.f || t.coastUnit <= 0.f || t.coastRes < 2 || t.coastRes * t.coastCell != 2.f * kWorldHalf)
        return fail(error, "bad units or coast grid in the terrain record");
    // outlines and channels
    std::vector<vec2> mainland, solBeach, keyCoral, portIsle, lake;
    std::vector<std::vector<vec2>> islands;
    std::vector<Channel> channels;
    const u32 outlines = r.getU32();
    for (u32 i = 0; i < outlines && !r.bad; i++) {
        const std::string id = r.getString();
        std::vector<vec2> pts;
        r.getPoints(pts);
        if (id == "mainland") mainland = pts;
        else if (id == "sol_beach") solBeach = pts;
        else if (id == "key_coral") keyCoral = pts;
        else if (id == "port_isle") portIsle = pts;
        else if (id == "lake") lake = pts;
        else if (id.compare(0, 7, "island.") == 0) islands.push_back(pts);
        else return fail(error, "unknown outline '" + id + "' in the terrain record");
    }
    const u32 nch = r.getU32();
    for (u32 i = 0; i < nch && !r.bad; i++) {
        r.getString();   // the channel's id
        Channel ch;
        ch.width0 = r.getF32();
        ch.width1 = r.getF32();
        ch.depth = r.getF32();
        r.getPoints(ch.pts);
        channels.push_back(ch);
    }
    if (!r.done()) return fail(error, "malformed terrain record");
    // the four grids, decoded in parallel
    struct Layer {
        const char* name;
        int res;
        GridType type;
        std::vector<i16> values;
        bool ok;
    };
    Layer layers[4] = {{"terrain.height", kHeightRes, GRID_I16, {}, false},
                       {"terrain.water", kHeightRes, GRID_U8, {}, false},
                       {"terrain.region", kHeightRes, GRID_U8, {}, false},
                       {"terrain.coast", (int)t.coastRes, GRID_I16, {}, false}};
    Jobs::parallelFor(4, [&](int i) {
        Layer& L = layers[i];
        const WorldBinView::Section* s = bin.find(L.name);
        GridInfo gi;
        L.ok = s && s->kind == WSK_GRID && decodeGrid(s->data, s->size, L.values, &gi) && gi.width == L.res && gi.height == L.res && gi.type == L.type;
    });
    for (const Layer& L : layers)
        if (!L.ok) return fail(error, std::string("world.bin section ") + L.name + " is missing or damaged");
    const size_t n = (size_t)kHeightRes * kHeightRes;
    map.height.resize(n);
    map.waterLevel.resize(n);
    map.region.resize(n);
    for (size_t i = 0; i < n; i++) {
        map.height[i] = (float)layers[0].values[i] * t.heightUnit;
        const int c = layers[1].values[i];
        if (c > (int)t.waterLevels.size()) return fail(error, "a water class without a level");
        map.waterLevel[i] = c == 0 ? kNoWater : t.waterLevels[(size_t)c - 1];
        const int g = layers[2].values[i];
        if (g >= (int)REG_COUNT) return fail(error, "a region this build doesn't know");
        map.region[i] = (u8)g;
    }
    map.coarseRes = (int)t.coastRes;
    map.coarseCell = t.coastCell;
    map.coarseSdf.resize(layers[3].values.size());
    for (size_t i = 0; i < map.coarseSdf.size(); i++) map.coarseSdf[i] = (float)layers[3].values[i] * t.coastUnit;
    map.mainland = mainland;
    map.solBeach = solBeach;
    map.keyCoral = keyCoral;
    map.portIsle = portIsle;
    map.smallIslands = islands;
    map.lakePoly = lake;
    map.channels = channels;
    map.recomputeSplat();
    return true;
}

bool loadHandWorld(const char* worldFile, WorldMap& map, RoadNetwork& roads, BuildingSet& buildings, std::string* error) {
    using namespace handworld_detail;
    const double t0 = TimeSeconds();
    static std::vector<u8> fileBytes;   // keeps a --worldfile alive for the run, as the exe image keeps the embedded one
    const u8* data = nullptr;
    size_t size = 0;
    if (worldFile && *worldFile) {
        if (!readFile(worldFile, fileBytes)) return fail(error, std::string("cannot read the world file ") + worldFile);
        data = fileBytes.data();
        size = fileBytes.size();
    } else {
        data = embeddedWorldBin(&size);
        if (!data) return fail(error, "this exe has no hand-built world (no NT_WORLD resource); build it with build.sh or build.bat");
    }
    WorldBinView bin;
    if (!bin.open(data, size, error)) return false;
    if (!loadHandTerrain(bin, map, error)) return false;
    gMap = &map;
    // stage 0 has no roads or buildings: empty sets with their lookup tables, so every query finds nothing
    roads = RoadNetwork();
    roads.buildHash();
    gRoads = &roads;
    buildings = BuildingSet();
    buildings.cellLists.assign((size_t)kCellsPerSide * kCellsPerSide, {});
    buildings.openCellLists.assign((size_t)kCellsPerSide * kCellsPerSide, {});
    gBuildings = &buildings;
    LOG("Hand world: terrain from %s (%zu KB, %zu sections) in %.2f s", worldFile && *worldFile ? worldFile : "the exe", size / 1024,
        bin.sections.size(), TimeSeconds() - t0);
    return true;
}

}  // namespace World
