// The catalog's codes that today's game builds (lib/catalog.ntw, code=; docs/NTW_FORMAT.md 9.13), for the world
// compiler: props by their PropType's name, and the lamps of light fixtures, the start of the photometric table of
// docs/HANDBUILT_WORLD.md 7.8 (the pieces the streets and yards use so far).
#pragma once
#include "worldtypes.h"
#include <cstring>

namespace World {

// A PropType by its name in the enum (PROP_PALM), or -1
inline int propTypeByName(const char* name) {
    static const char* const kNames[] = {
        "PROP_STREETLIGHT", "PROP_STREETLIGHT_DOUBLE", "PROP_TRAFFIC_LIGHT", "PROP_STOP_SIGN", "PROP_PALM", "PROP_PALM_TALL", "PROP_TREE_OAK",
        "PROP_TREE_PINE", "PROP_BUSH", "PROP_BENCH", "PROP_BIN", "PROP_HYDRANT", "PROP_BUS_STOP", "PROP_BOLLARD", "PROP_POWER_POLE", "PROP_MANGROVE",
        "PROP_CYPRESS", "PROP_SAWGRASS", "PROP_PARKING_METER", "PROP_NEWS_BOX", "PROP_PHONE_BOOTH", "PROP_TRASH_BAGS", "PROP_DUMPSTER",
        "PROP_AC_UNIT", "PROP_BARRIER", "PROP_HIGHWAY_SIGN", "PROP_PLANTER", "PROP_UMBRELLA", "PROP_LIFEGUARD_TOWER",
        "PROP_BIKE_RACK", "PROP_CONE", "PROP_MAILBOX", "PROP_STREET_TREE", "PROP_SIGNAL_SPAN", "PROP_WORK_SIGN"};
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == (size_t)PROP_COUNT, "a PropType was added or removed: name it here too");
    for (int i = 0; i < (int)PROP_COUNT; i++)
        if (strcmp(name, kNames[i]) == 0) return i;
    return -1;
}

// The yaw to add to a piece's (the catalog's frame faces +y, NTW_FORMAT 4) for the game's prop: most props face their
// local -y; a street light hangs its arm along its local +x
inline float propYawOffset(int type) { return type == PROP_STREETLIGHT || type == PROP_STREETLIGHT_DOUBLE ? kPi * 0.5f : kPi; }

// A light fixture: the prop it is, if any, and its lamp in the piece's frame (origin on the ground, +y forward)
struct LampSpec {
    const char* code;                  // the catalog's code=
    int prop;                          // the fixture's PropType; -1: a lamp alone (on a wall, a porch, in a garden)
    vec3 at;                           // the lamp, in the piece's frame; a lamp alone sits at the record's at=
    vec3 color;                        // linear rgb
    float intensity, radius;
    float cone;                        // cos of the outer angle; 0: a point light
    u8 type;                           // LightInstance::type (0 street, 1 building)
};

// The lamps the catalog can name, by code=, or nullptr. Today's road dresser lights its street lights the same way
// (roadmesh.cpp edgeFixtures: the arm 2.2 m, the lamp 8.6 m up, 7000 at 30 m).
inline const LampSpec* lampSpec(const char* code) {
    static const LampSpec kLamps[] = {
        {"PROP_STREETLIGHT", PROP_STREETLIGHT, vec3(0.f, 2.2f, 8.6f), vec3(1.f, 0.72f, 0.42f), 7000.f, 30.f, 0.2f, 0},
        {"LIGHT_PORCH", -1, vec3(0.f), vec3(1.f, 0.75f, 0.45f), 400.f, 8.f, 0.f, 1},
        {"LIGHT_WALL_FLOOD", -1, vec3(0.f), vec3(0.95f, 0.97f, 1.f), 6000.f, 18.f, 0.1f, 1},
        {"LIGHT_GARDEN", -1, vec3(0.f), vec3(1.f, 0.78f, 0.5f), 150.f, 5.f, 0.f, 1},
    };
    for (const LampSpec& s : kLamps)
        if (strcmp(code, s.code) == 0) return &s;
    return nullptr;
}

}  // namespace World
