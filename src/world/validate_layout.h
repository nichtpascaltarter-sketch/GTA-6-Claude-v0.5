// worldv's checks inside one library item: a layout, a unit or a fit-out (docs/HANDBUILT_WORLD.md 9.1, and the kit's
// stair and door limits of 4.9). Walls, rooms, doors and windows, stairs, corridors, and whether every room can be
// reached. Host tools only, like validate.h.
#pragma once
#include "validate.h"

namespace World {
namespace Validate {

// Appends the item's findings to `out` (their file left empty). `noWayIn` is set when nothing leads into the item at
// all: no door out, no stair and no lift. A plan may still reach such a floor through a hatch or another floor's stair,
// so the caller decides what to report.
void checkLayout(const Ntw::Document& doc, std::vector<Diagnostic>& out, bool* noWayIn);

// One floor of a plan, for the plan pass
struct PlanFloor {
    const Ntw::Record* rec = nullptr;          // the plan's floor record (its id, z, h and access=)
    const Ntw::Document* layout = nullptr;     // the floor's layout; nullptr when it's missing or doesn't parse
    size_t layoutKey = 0;                      // the caller's key for the layout's file
};

// A finding of the plan pass: in the plan's file (at its floor records) or in a layout's
struct PlanFinding {
    bool inPlan = false;
    size_t layoutKey = 0;
    Diagnostic d;
};

// The checks across a plan's floors (docs/HANDBUILT_WORLD.md 9.1): stairs and lifts join the floors they claim at the
// floors' heights, a hatch lands in a room, and doors and windows fit their floor's height
void checkPlanFloors(const std::string& plan, const std::vector<PlanFloor>& floors, std::vector<PlanFinding>& out);

}  // namespace Validate
}  // namespace World
