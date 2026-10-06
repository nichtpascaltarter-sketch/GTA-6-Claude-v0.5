// Macro layout of the state of Palmera and the global terrain/water/biome maps.
#pragma once
#include "../core/math.h"
#include "../core/rng.h"

namespace World {

constexpr float kWorldHalf = 10240.f;        // world spans [-kWorldHalf, kWorldHalf]^2 meters
constexpr int kHeightRes = 2560;             // heightmap texels per side
constexpr float kHeightCell = 8.f;           // meters per heightmap texel
constexpr float kLakeLevel = 4.0f;           // Lake Okahatchee water surface
constexpr float kNoWater = -1000.f;
constexpr u32 kWorldSeed = 0x5EA11A7Au;

enum Region : u8 {
    REG_OCEAN = 0,
    REG_DOWNTOWN,      // Porto Sol downtown towers
    REG_FINANCIAL,     // Solaris financial district (glass towers)
    REG_MIDTOWN,       // Canvas District: arts, murals, mid-rise
    REG_NORTH_CITY,    // mixed residential/commercial north of midtown
    REG_CALLE_LUNA,    // dense latin neighborhood, low-rise shops
    REG_BEACH,         // Sol Beach barrier island: art deco, hotels, condos
    REG_BAY_ISLAND,    // mansions on small bay islands
    REG_KEY_CORAL,     // upscale island south of Sol Beach
    REG_PORT,          // Port Isle container terminal
    REG_GROVE,         // leafy upscale suburbs with Mediterranean villas
    REG_AIRPORT,       // Porto Sol International
    REG_FLATS,         // industrial + working class
    REG_SUBURBS,       // grid suburbs with canals
    REG_REDLAND,       // southern nurseries/farms
    REG_SAWGRASS,      // wetlands
    REG_GULF_TOWN,     // Ten Palms fishing village on stilts
    REG_FARMLAND,      // northern sugar cane, citrus, ranches
    REG_LAKE_TOWN,     // Okahatchee lakeside town
    REG_HARLOW,        // rural town near the ridge
    REG_RIDGE,         // Cypress Ridge forested hills
    REG_FORT_CASTELL,  // northern port/industrial town
    REG_KEYS,          // Coral Keys small islands
    REG_KEY_TOWN,      // Key Solano town at the end of the keys
    REG_COUNT
};

struct RegionInfo {
    const char* name;
    float urban;        // 0 rural .. 1 dense city
    float minFloors, maxFloors;
    float greenery;     // vegetation density multiplier
};
const RegionInfo& regionInfo(Region r);

// Terrain material layers (terrain texture array indices)
enum TerrainLayer : int {
    TL_SAND = 0, TL_GRASS, TL_DIRT, TL_ROCK, TL_MUD, TL_SAWGRASS, TL_FOREST, TL_URBAN, TL_COUNT
};

// A polyline water channel (river / canal) carved to sea level.
struct Channel {
    std::vector<vec2> pts;
    float width0, width1;  // width at start / end
    float depth;
};

struct WorldMap {
    // Global maps, row-major, index = y * kHeightRes + x, texel centers at
    // world = -kWorldHalf + (i + 0.5) * kHeightCell.
    std::vector<float> height;
    std::vector<float> waterLevel;   // kNoWater where no water body
    std::vector<u8> region;
    std::vector<u32> splat0;         // RGBA8 weights for layers 0-3
    std::vector<u32> splat1;         // RGBA8 weights for layers 4-7
    std::vector<Channel> channels;
    std::vector<vec2> mainland, solBeach, keyCoral, portIsle;
    std::vector<std::vector<vec2>> smallIslands;  // bay islands + keys (polygons)
    std::vector<vec2> lakePoly;

    void generate();
    // Queries (bilinear, world meters)
    float heightAt(float x, float y) const;
    float waterAt(float x, float y) const;  // water surface level or kNoWater
    vec3 normalAt(float x, float y) const;
    Region regionAt(float x, float y) const;
    float coastDistance(float x, float y) const;  // signed: + on land, meters (coarse)
    bool isWater(float x, float y) const { float w = waterAt(x, y); return w > kNoWater + 1 && w > heightAt(x, y); }
    // Modify terrain to follow a road surface: sets height inside `halfWidth`, blends to
    // original height over `blend` meters beyond it.
    void flattenAlong(vec2 a, vec2 b, float za, float zb, float halfWidth, float blend);
    void flattenRect(vec2 center, vec2 axisX, float hx, float hy, float z, float blend);
    void recomputeSplat();  // after flattening

    // Internal
    std::vector<float> coarseSdf;  // signed coast distance, 32 m grid
    int coarseRes = 0;
    float coarseCell = 32.f;
    Region classify(float x, float y, float sdf) const;
};

extern WorldMap* gMap;

inline int texelIndex(int x, int y) { return y * kHeightRes + x; }
inline float texelToWorld(int i) { return -kWorldHalf + (i + 0.5f) * kHeightCell; }
inline float worldToTexel(float w) { return (w + kWorldHalf) / kHeightCell - 0.5f; }

}  // namespace World
