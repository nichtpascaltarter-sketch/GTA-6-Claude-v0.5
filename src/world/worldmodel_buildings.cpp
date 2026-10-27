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

bool inPoly(const std::vector<vec2>& poly, vec2 q) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const vec2 a = poly[i], b = poly[j];
        if ((a.y > q.y) != (b.y > q.y) && q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

// A room of a layout by its id: its index (kNone32: none, or out)
u32 roomIndex(const Layout& lay, const std::string& id) {
    for (size_t i = 0; i < lay.rooms.size(); i++)
        if (lay.rooms[i].id == id) return (u32)i;
    return kNone32;
}

// A door's leaf and the rooms each side of its wall (buildingsbin.h Opening): leads=<a>:<b> names them, and the room
// polygons (drawn to the walls' centrelines) say which is on which side. swing=in opens into the building through an
// exterior wall and into <b> between rooms, out the other way (in without one); hinge= is the side of the opening its
// hinges are on seen from where it is pushed open (left without one).
void doorLeaf(const Layout& lay, const LayoutWall& lw, const LayoutOpening& o, HandShell::Opening& op) {
    const vec2 d = lw.b - lw.a;
    const float len = length(d);
    if (len < 1e-4f) return;
    const vec2 dir = d * (1.f / len), nl(-dir.y, dir.x);   // (nl: to the left of a -> b)
    const vec2 c = lw.a + dir * o.at, pl = c + nl * (lw.t * 0.5f + 0.05f), pr = c - nl * (lw.t * 0.5f + 0.05f);
    const size_t colon = o.leads.find(':');
    const std::string na = colon == std::string::npos ? o.leads : o.leads.substr(0, colon), nb = colon == std::string::npos ? std::string() : o.leads.substr(colon + 1);
    const u32 ra = roomIndex(lay, na), rb = roomIndex(lay, nb);
    for (u32 r : {ra, rb}) {
        if (r == kNone32) continue;
        const std::vector<vec2>& poly = lay.rooms[r].poly;
        if (poly.size() >= 3 && inPoly(poly, pl) && op.left == kNone32) op.left = r;
        else if (poly.size() >= 3 && inPoly(poly, pr) && op.right == kNone32) op.right = r;
        else if (op.left == kNone32) op.left = r;   // (on neither side: worldv's to report; the free side)
        else if (op.right == kNone32) op.right = r;
    }
    // the side it opens to: the room (or the outside) it swings into
    const bool out = o.swing == "out";
    const bool exterior = ra == kNone32 || rb == kNone32;
    const u32 into = exterior ? (out ? kNone32 : (ra == kNone32 ? rb : ra)) : (out ? ra : rb);
    op.swing = into == op.left ? 0 : 1;
    if (op.left == op.right) op.swing = out ? 1 : 0;   // (both sides outside or unknown)
    // pushed from the other side, looking across the wall: a left hinge is toward a when it opens to the left of a -> b
    op.hinge = (u8)((op.swing == 1) != (o.hinge == "right") ? 1 : 0);
}

// A stair's end on a plan's floors: "f1.hall" (a floor and its room), or "+1:hall", "0:hall", "-1:hall" relative to
// floor `fi`; false where it names no floor or room (worldv's to report)
bool stairEnd(const Model& model, const Plan& pl, size_t fi, const std::string& v, int& floor, u32& room) {
    std::string fl, rm;
    const size_t colon = v.find(':'), dot = v.find('.');
    if (colon != std::string::npos) {
        const std::string off = v.substr(0, colon);
        const int k = off == "0" ? 0 : off == "+1" ? 1 : off == "-1" ? -1 : 99;
        if (k == 99 || (int)fi + k < 0 || (int)fi + k >= (int)pl.floors.size()) return false;
        floor = (int)fi + k;
        rm = v.substr(colon + 1);
    } else if (dot != std::string::npos) {
        fl = v.substr(0, dot);
        rm = v.substr(dot + 1);
        floor = -1;
        for (size_t i = 0; i < pl.floors.size(); i++)
            if (pl.floors[i].id == fl) floor = (int)i;
        if (floor < 0) return false;
    } else {
        return false;
    }
    const Ref& lr = pl.floors[(size_t)floor].layout;
    if (lr.target < 0 || (size_t)lr.target >= model.library.size() || model.library[(size_t)lr.target].kind != "layout") return false;
    const int li = model.library[(size_t)lr.target].table;
    if (li < 0 || (size_t)li >= model.layouts.size()) return false;
    room = roomIndex(model.layouts[(size_t)li], rm);
    return room != kNone32;
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
            for (const LayoutRoom& r : lay.rooms) {
                HandShell::Room rm;
                rm.floor = (int)fi;
                const int type = buildingWord("room", r.type);
                rm.type = (u8)(type < 0 ? ROOM_LIVING : type);
                rm.schedule = roomScheduleOf(r);
                rm.lux = (u32)Max(r.lux, 0);
                rm.poly = r.poly;
                s.rooms.push_back(rm);
            }
            for (const LayoutWall& lw : lay.walls) {
                if (lw.ext.empty()) {   // an inside wall, with its doors
                    HandShell::Wall w;
                    w.floor = (int)fi;
                    w.a = lw.a;
                    w.b = lw.b;
                    w.t = lw.t;
                    for (const LayoutOpening& o : lay.openings) {
                        if (o.wall != lw.id || !o.door) continue;
                        HandShell::Opening op;
                        const int k = buildingWord("door", o.kind);
                        op.kind = (u16)((k < 0 ? 0 : k) | 0x100);
                        op.at = o.at;
                        op.w = o.w;
                        op.h = o.h;
                        doorLeaf(lay, lw, o, op);
                        w.openings.push_back(op);
                    }
                    s.partitions.push_back(w);
                    continue;
                }
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
                    if (o.door) doorLeaf(lay, lw, o, op);
                    w.openings.push_back(op);
                }
                s.walls.push_back(w);
            }
        }
        // the stairs: each floor's layout's, from its foot to its top (a layout several floors share gives each of
        // them its own where the ends are relative; one only where they name floors)
        for (size_t fi = 0; fi < pl.floors.size(); fi++) {
            const int li = table(pl.floors[fi].layout);
            if (li < 0 || model.library[(size_t)pl.floors[fi].layout.target].kind != "layout" || (size_t)li >= model.layouts.size()) continue;
            for (const LayoutStair& ls : model.layouts[(size_t)li].stairs) {
                HandShell::Stair st;
                if (!stairEnd(model, pl, fi, ls.from, st.fromFloor, st.fromRoom) || !stairEnd(model, pl, fi, ls.to, st.toFloor, st.toRoom)) continue;   // (worldv's)
                const float dz = pl.floors[(size_t)st.toFloor].z - pl.floors[(size_t)st.fromFloor].z;
                const int kind = buildingWord("stair", ls.kind);
                if (dz <= 0.f || kind < 0 || ls.run.size() < 2) continue;   // (worldv's)
                st.kind = (u8)kind;
                st.run = ls.run;
                st.w = ls.w;
                // its risers climb the floors' difference in height: treads x rise as written, or derived from it (as
                // worldv derives them)
                if (ls.treads > 0) {
                    st.treads = (u32)ls.treads;
                    st.rise = ls.rise > 0.f ? ls.rise : dz / (float)ls.treads;
                } else {
                    st.treads = (u32)Max(1, (int)floorf(dz / (ls.rise > 0.f ? ls.rise : 0.175f) + 0.5f));
                    st.rise = ls.rise > 0.f ? ls.rise : dz / (float)st.treads;
                }
                st.landing = ls.landing > 0.f ? ls.landing : ls.w;
                st.walk = ls.walk;
                bool seen = false;
                for (const HandShell::Stair& o : s.stairs)
                    seen = seen || (o.fromFloor == st.fromFloor && o.fromRoom == st.fromRoom && o.toFloor == st.toFloor && o.toRoom == st.toRoom && o.run.size() == st.run.size() &&
                                    o.run[0].x == st.run[0].x && o.run[0].y == st.run[0].y);
                if (!seen) s.stairs.push_back(st);
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
