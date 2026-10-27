// The hand-built world's buildings in world.bin (docs/HANDBUILT_WORLD.md 4.6-4.7, 6.1): tools/worldc.cpp compiles the
// library's plans, with their floor layouts and looks, into shells, and the building files into placements of them
// (worldmodel.h compileBuildings); hand mode reads both (handworld.cpp) into gHandBuildings and draws the shells into
// the cells (handshells.cpp). A shell is what the outside of a building needs: its floors' heights, its exterior walls
// with their windows and doors, its roof and its look. The interiors (rooms, stairs, slots, furnishing) come later,
// from the same plans.
//
// Section "library" (WSK_LZ), little-endian: version u32 (kLibrarySectionVersion), then
//   looks (a facade style each): count u32, then per look: id (string), wall material, wall colour (u32: MaterialId,
//     HandPaletteSlot), trims (u32 count, then per trim: kind (HandTrimKind), material, colour (u32), h (f32)),
//     window kind (HandWindowKind), frame material, frame colour, shutters (HandShutterKind), door colour (u32 each),
//     base h (f32), base material (u32);
//   shells (a plan each): count u32, then per shell: id (string), class (u32: kShellClasses' index), size (xy),
//     footprint (points), look (u32; 0xffffffff: none), palette (5 u32: wall, trim, door, accent, roof as rgb8 packs
//     them; 0xffffffff: none), roof form (HandRoofForm), pitch (degrees), overhang (f32), roof material (u32), floors
//     (u32 count, then per floor: id (string), z, h (f32)), walls (u32 count, then per wall: floor (u32), a, b (xy: its
//     centreline, in the plan's frame), t (f32), side (u32: 0 front, 1 right, 2 back, 3 left), openings (u32 count,
//     then per opening: kind (u32: a HandWindowKind, or a HandDoorKind | 0x100 for a door), at, w, h, sill (f32),
//     schedule (u32: the RoomSchedule of the room behind a window, or inside a door from outside; 0xffffffff: none),
//     room (u32: that room's index among its floor's layout's rooms; 0xffffffff: none)))
// Section "buildings" (WSK_LZ): version u32 (kBuildingsSectionVersion), count u32, then per building: id (string),
//   shell (u32), x, y, z (its ground floor's absolute height), yaw (radians) (f32), mirror (u32), palette (5 u32, as a
//   shell's), roof material (u32; 0xffffffff: the shell's), look (u32; 0xffffffff: the shell's)
#pragma once
#include "worldbin.h"
#include <string>
#include <vector>

namespace World {

constexpr u32 kLibrarySectionVersion = 2;   // (2: an opening's room)
constexpr u32 kBuildingsSectionVersion = 1;
constexpr u32 kNone32 = 0xffffffffu;

// A plan's class (NTW_FORMAT.md 9.7 meta class=), today's building styles and landmark, by index
constexpr int kShellClassCount = 18;
const char* shellClassName(int c);
int shellClass(const std::string& word);   // -1: none
enum HandPaletteSlot : u8 { PAL_WALL = 0, PAL_TRIM, PAL_DOOR, PAL_ACCENT, PAL_ROOF, PAL_COUNT };
enum HandTrimKind : u8 { TRIM_NONE = 0, TRIM_BAND, TRIM_CORNICE, TRIM_QUOIN, TRIM_EYEBROW, TRIM_COUNT };
enum HandWindowKind : u8 { WIN_FIXED = 0, WIN_DHUNG, WIN_CASEMENT, WIN_SLIDER, WIN_STOREFRONT, WIN_JALOUSIE, WIN_GLASSBLOCK, WIN_COUNT };
enum HandDoorKind : u8 { DOOR_HINGED = 0, DOOR_PAIR, DOOR_SLIDING, DOOR_REVOLVING, DOOR_ROLLUP, DOOR_LIFT, DOOR_OPENING, DOOR_COUNT };
enum HandShutterKind : u8 { SHUTTER_NONE = 0, SHUTTER_FIXED, SHUTTER_BAHAMA, SHUTTER_COUNT };
enum HandRoofForm : u8 { RFORM_FLAT = 0, RFORM_GABLE, RFORM_HIP, RFORM_SHED, RFORM_MANSARD, RFORM_BARREL, RFORM_COUNT };
// The words for each (the format's): the index of `word` in `kind`'s list, or -1
int buildingWord(const char* kind, const std::string& word);   // kind: "slot", "trim", "window", "door", "shutters", "roof"

struct HandLook {                      // a facade style
    std::string id;
    u8 wallMaterial = 0, wallColor = PAL_WALL;
    struct Trim {
        u8 kind = TRIM_BAND, material = 0, color = PAL_TRIM;
        float h = 0.f;
    };
    std::vector<Trim> trims;
    u8 windowKind = WIN_DHUNG, frameMaterial = 0, frameColor = PAL_TRIM, shutters = SHUTTER_NONE;
    u8 doorColor = PAL_DOOR;
    float baseH = 0.f;
    u8 baseMaterial = 0;
};

struct HandShell {                     // a plan's outside, in its own frame (front-left corner, +x along the front)
    struct Floor {
        std::string id;
        float z = 0.f, h = 0.f;
    };
    struct Opening {
        u16 kind = 0;                  // a HandWindowKind, or a HandDoorKind | 0x100
        float at = 0.f, w = 0.f, h = 0.f, sill = 0.f;
        u32 schedule = kNone32;        // the RoomSchedule (roomlight.h) of the room behind a window or inside a door
        u32 room = kNone32;            // that room: its index among the floor's layout's rooms
        bool door() const { return (kind & 0x100u) != 0; }
    };
    struct Wall {
        int floor = 0;
        vec2 a = vec2(0.f), b = vec2(0.f);
        float t = 0.f;
        u8 side = 0;                   // 0 front, 1 right, 2 back, 3 left
        std::vector<Opening> openings;
    };
    std::string id;
    int cls = 0;
    vec2 size = vec2(0.f);
    std::vector<vec2> footprint;
    int look = -1;
    u32 palette[PAL_COUNT] = {kNone32, kNone32, kNone32, kNone32, kNone32};
    u8 roofForm = RFORM_FLAT;
    float pitch = 0.f, overhang = 0.f;
    u8 roofMaterial = 0;
    std::vector<Floor> floors;
    std::vector<Wall> walls;
};

struct HandPlacement {
    std::string id;
    int shell = -1;
    vec2 at = vec2(0.f);
    float z = 0.f, yaw = 0.f;          // yaw in radians, counter-clockwise
    bool mirror = false;
    u32 palette[PAL_COUNT] = {kNone32, kNone32, kNone32, kNone32, kNone32};
    u32 roofMaterial = kNone32;        // the shell's where none
    int look = -1;                     // the shell's where -1
};

struct HandBuildings {
    std::vector<HandLook> looks;
    std::vector<HandShell> shells;
    std::vector<HandPlacement> placements;
    // Not in world.bin: what the game derives at load (handshells.cpp prepareHandBuildings). Per streaming cell, the
    // placements whose footprint's centre lies in it; per placement, where its openings start in openingFacades, which
    // holds the facade entry of each opening of each wall in turn (kNone32: none, a door's leaf)
    std::vector<std::vector<int>> placementsByCell;
    std::vector<u32> firstOpening;
    std::vector<u32> openingFacades;
};

// Adds "library" and "buildings" (each only when it has something)
void writeBuildings(const HandBuildings& b, WorldBinWriter& out);

// Reads them into `out` (replacing it). False, with a reason and `out` left empty, when a section is damaged,
// truncated, over-long or of another version, or names a shell or look the library doesn't have; true with the lists
// empty where there are none.
bool readBuildings(const WorldBinView& bin, HandBuildings& out, std::string* error);

// The game's copy in hand mode (nullptr in the procedural world). Inline, so the tools that build cells without the
// hand-built world link without buildingsbin.cpp
inline HandBuildings* gHandBuildings = nullptr;

}  // namespace World
