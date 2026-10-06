// Shared world content types (cells, props, lights, collision).
#pragma once
#include "../core/math.h"
#include "worldmap.h"

namespace World {

constexpr float kCellSize = 256.f;
constexpr int kCellsPerSide = (int)(2.f * kWorldHalf / kCellSize);

inline vec2 cellOrigin(int cx, int cy) { return vec2(-kWorldHalf + cx * kCellSize, -kWorldHalf + cy * kCellSize); }
inline bool inCell(vec2 p, int cx, int cy) {
    vec2 o = cellOrigin(cx, cy);
    return p.x >= o.x && p.x < o.x + kCellSize && p.y >= o.y && p.y < o.y + kCellSize;
}

// Prop instance produced by world generation (streetlights, traffic lights, signs, trees...)
enum PropType : u8 {
    PROP_STREETLIGHT = 0, PROP_STREETLIGHT_DOUBLE, PROP_TRAFFIC_LIGHT, PROP_STOP_SIGN, PROP_PALM, PROP_PALM_TALL, PROP_TREE_OAK,
    PROP_TREE_PINE, PROP_BUSH, PROP_BENCH, PROP_BIN, PROP_HYDRANT, PROP_BUS_STOP, PROP_BOLLARD, PROP_POWER_POLE, PROP_MANGROVE,
    PROP_CYPRESS, PROP_SAWGRASS, PROP_PARKING_METER, PROP_NEWS_BOX, PROP_PHONE_BOOTH, PROP_TRASH_BAGS, PROP_DUMPSTER,
    PROP_AC_UNIT, PROP_BARRIER, PROP_HIGHWAY_SIGN, PROP_PLANTER, PROP_UMBRELLA, PROP_LIFEGUARD_TOWER, PROP_COUNT
};

struct PropInstance {
    vec3 pos;     // world
    float yaw;
    float scale;
    u8 type;
    u8 variant;
    u16 flags;
};

struct LightInstance {
    vec3 pos;        // world
    vec3 color;      // linear rgb * intensity (candela-ish)
    float radius;
    vec3 dir;        // spot direction (0,0,0 = point)
    float cone;      // cos of outer angle
    u8 type;         // 0 street, 1 building, 2 neon, 3 traffic signal
};

struct CollisionBox {
    vec3 c;      // world center
    vec2 ax;     // unit axis (x)
    vec3 he;     // half extents (x along ax, y along perp, z)
};


}  // namespace World
