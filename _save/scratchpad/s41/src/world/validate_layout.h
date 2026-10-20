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

}  // namespace Validate
}  // namespace World
