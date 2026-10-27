// The buildings compile (worldmodel.h compileBuildings): the library's plans, with their floor layouts and looks, into
// shells, and the building files into placements of them (buildingsbin.h).
#include "worldmodel.h"
#include "buildingsbin.h"
#include "roomlight.h"
#include "../render/mesh.h"
#include <cstring>

namespace World {
namespace Hand {

namespace worldmodel_buildings_detail {

// A MaterialId by its name in the enum (MAT_STUCCO), or -1: what a catalog material's code= names
int materialByName(const std::string& name) {
    static const char* const kNames[] = {
        "MAT_ASPHALT", "MAT_ASPHALT_OLD", "MAT_CONCRETE", "MAT_SIDEWALK", "MAT_CURB", "MAT_PAINT_WHITE", "MAT_PAINT_YELLOW", "MAT_BRICK", "MAT_STUCCO",
        "MAT_PLASTER", "MAT_WOOD_SIDING", "MAT_ROOF_TILE", "MAT_ROOF_SHINGLE", "MAT_ROOF_METAL", "MAT_ROOF_GRAVEL", "MAT_GLASS", "MAT_METAL_PAINTED",
        "MAT_METAL_BRUSHED", "MAT_PAVERS", "MAT_MARBLE", "MAT_STONE", "MAT_CORRUGATED", "MAT_FABRIC", "MAT_WOOD", "MAT_TILE_POOL", "MAT_GRASS",
        "MAT_DIRT", "MAT_RUBBER", "MAT_EMISSIVE", "MAT_CHROME", "MAT_CONCRETE_PANEL", "MAT_FACADE", "MAT_LEAVES", "MAT_BARK", "MAT_PALM_FROND",
        "MAT_SAND", "MAT_CARPAINT", "MAT_PLASTIC", "MAT_LEATHER", "MAT_LIGHT_HEAD", "MAT_LIGHT_TAIL", "MAT_SKIN", "MAT_HAIR", "MAT_CLOTH", "MAT_DENIM",
        "MAT_EYE", "MAT_TIRE", "MAT_RIM", "MAT_CAR_GLASS", "MAT_INTERIOR", "MAT_LIGHT_INDICATOR", "MAT_DECAL_TEXT", "MAT_CAR_WINDOW", "MAT_TILE",
        "MAT_CEILING_TILE", "MAT_CARPET", "MAT_WOOD_FLOOR"};
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == (size_t)MAT_COUNT, "a MaterialId was added or removed: name it here too");
    for (int i = 0; i < (int)MAT_COUNT; i++)
        if (name == kNames[i]) return i;
    return -1;
}

// A layout room's light schedule (roomlight.h): its sched= where that names a class (always, home_kitchen, shop, ...),
// else by its type, as a home's rooms keep a household's evening
u32 roomScheduleOf(const LayoutRoom& r) {
    for (u32 s = 0; s < (u32)RS_COUNT; s++)
        if (r.sched == roomScheduleName(s)) return s;
    const std::string& t = r.type;
    if (t == "kitchen") return RS_HOME_KITCHEN;
    if (t == "bed") return RS_HOME_BED;
    if (t == "bath") return RS_HOME_BATH;
    if (t == "living" || t == "dining" || t == "hall") return RS_HOME_LIVING;
    if (t == "shop") return RS_SHOP;
    if (t == "office") return RS_OFFICE;
    if (t == "restaurant") return RS_RESTAURANT;
    if (t == "bar") return RS_NIGHTLIFE;
    if (t == "stair" || t == "corridor" || t == "lobby") return RS_ALWAYS;
    if (t == "garage") return RS_GARAGE;
    return RS_NEVER;   // store, plant, utility
}

// The room a door opens into from outside: leads=out:<room> or <room>:out; empty for a door between rooms
std::string insideRoom(const std::string& leads) {
    const size_t c = leads.find(':');
    if (c == std::string::npos) return std::string();
    const std::string a = leads.substr(0, c), b = leads.substr(c + 1);
    return a == "out" && b != "out" ? b : b == "out" && a != "out" ? a : std::string();
}

}  // namespace worldmodel_buildings_detail

bool compileBuildings(const Model& model, HandBuildings& out, std::vector<Error>& errors) {
    using namespace worldmodel_buildings_detail;
    const size_t errorsBefore = errors.size();
    out = HandBuildings();
    const auto fail = [&errors](const Src& s, const char* code, const std::string& msg) { errors.push_back(Error{s, code, msg}); };
    // the library item a reference names, of the table it is typed into; -1 where it names nothing (worldv's to report)
    const auto table = [&](const Ref& r) { return r.target >= 0 && (size_t)r.target < model.library.size() ? model.library[(size_t)r.target].table : -1; };
    // a material piece's MaterialId; `fallback` without one, or (block.kind's way: reported) for a code the game lacks
    const auto material = [&](const Ref& r, const Src& s, const std::string& what, int fallback) {
        if (r.target < 0 || (size_t)r.target >= model.library.size()) return (u8)fallback;
        const LibItem& it = model.library[(size_t)r.target];
        const int m = materialByName(it.code);
        if (m < 0) fail(s, "building.kind", what + ": " + it.id + "'s code=" + it.code + " is no material the game has (a MaterialId's name: MAT_STUCCO)");
        return (u8)(m < 0 ? fallback : m);
    };
    const auto slot = [](const std::string& word, int fallback) {
        const int s = word.empty() ? -1 : buildingWord("slot", word);
        return (u8)(s < 0 ? fallback : s);
    };
    const auto palette = [&](const Ref& r, u32 (&p)[PAL_COUNT]) {
        const int i = table(r);
        if (i < 0 || (size_t)i >= model.palettes.size() || model.library[(size_t)r.target].kind != "palette") return;
        const PaletteDef& d = model.palettes[(size_t)i];
        p[PAL_WALL] = d.wall;
        p[PAL_TRIM] = d.trim;
        p[PAL_DOOR] = d.door;
        p[PAL_ACCENT] = d.accent;
        p[PAL_ROOF] = d.roof;
    };
    const auto lookOf = [&](const Ref& r) {
        const int i = table(r);
        return i >= 0 && r.target >= 0 && model.library[(size_t)r.target].kind == "facade" ? i : -1;
    };
    // ---------------------------------------------------------------- the looks (facade styles)
    for (const FacadeStyle& fs : model.facadeStyles) {
        const std::string what = "facade " + fs.id;
        HandLook l;
        l.id = fs.id;
        l.wallMaterial = material(fs.wallMaterial, fs.src, what, MAT_STUCCO);
        l.wallColor = slot(fs.wallColor, PAL_WALL);
        for (const FacadeTrim& t : fs.trims) {
            const int k = buildingWord("trim", t.kind);
            if (k <= TRIM_NONE) continue;
            HandLook::Trim tr;
            tr.kind = (u8)k;
            tr.material = material(t.material, fs.src, what + " trim", l.wallMaterial);
            tr.color = slot(t.color, PAL_TRIM);
            tr.h = t.h > 0.f ? t.h : 0.2f;
            l.trims.push_back(tr);
        }
        const int wk = buildingWord("window", fs.windowKind);
        l.windowKind = (u8)(wk < 0 ? WIN_DHUNG : wk);
        l.frameMaterial = material(fs.windowFrame, fs.src, what + " window frame", MAT_METAL_PAINTED);
        l.frameColor = slot(fs.windowColor, PAL_TRIM);
        const int sh = buildingWord("shutters", fs.shutters);
        l.shutters = (u8)(sh < 0 ? SHUTTER_NONE : sh);
        l.doorColor = slot(fs.doorColor, PAL_DOOR);
        l.baseH = fs.baseH;
        l.baseMaterial = material(fs.baseMaterial, fs.src, what + " base", l.wallMaterial);
        out.looks.push_back(l);
    }
    // ---------------------------------------------------------------- the shells (plans)
    for (const Plan& pl : model.plans) {
        const std::string what = "plan " + pl.id;
        HandShell s;
        s.id = pl.id;
        s.cls = shellClass(pl.cls) >= 0 ? shellClass(pl.cls) : 0;
        s.size = pl.size;
        s.footprint = pl.footprint;
        s.look = lookOf(pl.facade);
        palette(pl.palette, s.palette);
        const int rf = buildingWord("roof", pl.roofForm);
        s.roofForm = (u8)(rf < 0 ? RFORM_FLAT : rf);
        s.pitch = pl.pitch;
        s.overhang = pl.overhang;
        s.roofMaterial = material(pl.roofMaterial, pl.src, what + " roof", s.roofForm == RFORM_FLAT ? MAT_ROOF_GRAVEL : MAT_ROOF_SHINGLE);
        if (pl.floors.empty() || pl.footprint.size() < 3) {
            fail(pl.src, "building.shape", what + ": a plan needs a floor and a footprint of 3 points or more");
            out.shells.push_back(s);   // (kept, so the placements' shell indices stay the plans')
            continue;
        }
        // the main entrance (the plan's entrance kind=main, else its first): which of the shell's walls and openings
        const PlanEntrance* entrance = nullptr;
        for (const PlanEntrance& e : pl.entrances)
            if (!entrance || (e.kind == "main" && entrance->kind != "main")) entrance = &e;
        for (size_t fi = 0; fi < pl.floors.size(); fi++) {
            const PlanFloor& f = pl.floors[fi];
            s.floors.push_back(HandShell::Floor{f.id, f.z, f.h});
            const int li = table(f.layout);
            if (li < 0 || model.library[(size_t)f.layout.target].kind != "layout" || (size_t)li >= model.layouts.size()) continue;   // (worldv's)
            const Layout& lay = model.layouts[(size_t)li];
            for (const LayoutWall& lw : lay.walls) {
                if (lw.ext.empty()) continue;
                HandShell::Wall w;
                w.floor = (int)fi;
                w.a = lw.a;
                w.b = lw.b;
                w.t = lw.t;
                w.side = (u8)(lw.ext == "front" ? 0 : lw.ext == "right" ? 1 : lw.ext == "back" ? 2 : 3);
                for (const LayoutOpening& o : lay.openings) {
                    if (o.wall != lw.id) continue;
                    if (entrance && o.door && o.id == entrance->door && f.id == entrance->floor) {
                        s.entranceWall = (int)s.walls.size();
                        s.entranceOpening = (int)w.openings.size();
                    }
                    HandShell::Opening op;
                    const int k = buildingWord(o.door ? "door" : "window", o.kind);
                    op.kind = (u16)((k < 0 ? 0 : k) | (o.door ? 0x100 : 0));
                    op.at = o.at;
                    op.w = o.w;
                    op.h = o.h;
                    op.sill = o.door ? 0.f : o.sill;
                    // the room behind it: a window's room=, a door's room inside from outside (leads=out:<room>)
                    const std::string room = o.door ? insideRoom(o.leads) : o.room;
                    for (size_t ri = 0; ri < lay.rooms.size(); ri++)
                        if (!room.empty() && lay.rooms[ri].id == room) {
                            op.schedule = roomScheduleOf(lay.rooms[ri]);
                            op.room = (u32)ri;
                        }
                    w.openings.push_back(op);
                }
                s.walls.push_back(w);
            }
        }
        out.shells.push_back(s);
    }
    // ---------------------------------------------------------------- the placements (building files)
    for (const Building& b : model.buildings) {
        const int pi = table(b.plan);
        if (pi < 0 || model.library[(size_t)b.plan.target].kind != "plan" || (size_t)pi >= model.plans.size()) continue;   // (names nothing: worldv's)
        // the config's options: each the plan's, with one of its choices
        const Plan& pl = model.plans[(size_t)pi];
        for (const auto& o : b.options) {
            const PlanOption* po = nullptr;
            for (const PlanOption& x : pl.options)
                if (x.id == o.first) po = &x;
            bool chosen = false;
            if (po)
                for (const std::string& c : po->choices) chosen = chosen || c == o.second;
            if (!po) fail(b.src, "building.option", "building " + b.id + " config " + o.first + "=" + o.second + ": " + pl.id + " has no option " + o.first);
            else if (!chosen) fail(b.src, "building.option", "building " + b.id + " config " + o.first + "=" + o.second + ": not one of its choices in " + pl.id);
        }
        HandPlacement p;
        p.id = b.id;
        p.shell = pi;
        p.at = b.at;
        p.z = b.z;
        p.yaw = b.yaw * kDegToRad;
        p.mirror = b.mirror;
        palette(b.palette, p.palette);
        if (b.roof.target >= 0) p.roofMaterial = material(b.roof, b.src, "building " + b.id + " roof", MAT_ROOF_SHINGLE);
        p.look = lookOf(b.facade);
        out.placements.push_back(p);
    }
    return errors.size() == errorsBefore;
}

}  // namespace Hand
}  // namespace World
