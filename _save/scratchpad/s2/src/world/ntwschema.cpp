// The NTW schema (ntwschema.h). docs/NTW_FORMAT.md describes the same kinds in prose; keep the two in step.
#include "ntwschema.h"
#include <cstring>

namespace World {
namespace Ntw {

namespace ntwschema_detail {

FieldDef field(const char* key, ValueType t, int decimals, bool required, const char* words) {
    FieldDef f;
    f.key = key;
    f.type = t;
    f.decimals = decimals;
    f.required = required;
    f.words = words;
    return f;
}
// F: an optional field, R: a required one (the same arguments: no bool among them, so a word list can't turn into one)
FieldDef F(const char* key, ValueType t, int decimals = -1, const char* words = nullptr) { return field(key, t, decimals, false, words); }
FieldDef R(const char* key, ValueType t, int decimals = -1, const char* words = nullptr) { return field(key, t, decimals, true, words); }

RecordDef Rec(const char* type, int id, bool block, std::vector<FieldDef> fields, std::vector<std::string> children = {}, bool topLevel = true) {
    RecordDef r;
    r.type = type;
    r.id = id;
    r.block = block;
    r.fields = std::move(fields);
    r.children = std::move(children);
    r.topLevel = topLevel;
    return r;
}

// Scenario points, indoors and out (the AI agent's 12.4 and 12.5): a kind, where, how many, when, who and how
std::vector<FieldDef> spotFields(const char* kinds, bool indoor) {
    std::vector<FieldDef> f;
    f.push_back(R("kind", VT_WORD, -1, kinds));
    if (indoor) f.push_back(R("room", VT_REF));
    f.push_back(F("at", VT_POINT, 2));
    f.push_back(F("yaw", VT_NUM, 1));
    f.push_back(F("line", VT_POINTS, 2));
    f.push_back(F("cap", VT_INT));
    f.push_back(F("hours", VT_ANY, 2));
    if (!indoor) f.push_back(F("cover", VT_INT));
    f.push_back(F("roles", VT_LIST));
    f.push_back(F("ages", VT_ANY, 0));
    f.push_back(F("prop", VT_REF));
    f.push_back(F("pose", VT_ID));
    f.push_back(F("for", VT_ANY));
    return f;
}

std::vector<KindDef> build() {
    std::vector<KindDef> kinds;
    {
        // world/terrain/terrain.ntw: the baked terrain (tools/terrainbake.cpp writes it, tools/worldc.cpp compiles it)
        KindDef k;
        k.kind = "terrain";
        k.version = 1;
        k.records.push_back(Rec("source", 0, false, {F("tool", VT_ID), F("generator", VT_ID), F("seed", VT_ANY), F("commit", VT_ANY)}));
        k.records.push_back(Rec("grid", 0, false, {R("res", VT_INT), R("cell", VT_NUM, 2), R("origin", VT_POINT, 2)}));
        k.records.push_back(Rec("layer", 2, false,
                                {R("file", VT_ANY), R("type", VT_WORD, -1, "i16|u8"), F("res", VT_INT), F("cell", VT_NUM, 2), F("unit", VT_NUM), R("hash", VT_HEX),
                                 F("range", VT_POINT, 2)}));
        k.records.push_back(Rec("water", 2, false, {R("level", VT_ANY, 4)}));
        k.records.push_back(Rec("outline", 2, false, {R("pts", VT_POINTS, 2)}));
        k.records.push_back(Rec("channel", 2, false, {R("width0", VT_NUM, 2), R("width1", VT_NUM, 2), R("depth", VT_NUM, 2), R("pts", VT_POINTS, 2)}));
        kinds.push_back(k);
    }
    // The world (docs/NTW_FORMAT.md 9.2-9.6, 9.11-9.13)
    const char* sides = "left|right";
    const char* grounds = "lawn|paving|gravel|dirt|sand|planting";
    const char* scheds = "dusk|evening|night|always|never";
    const char* locks = "none|key|staff|resident|alarm";
    const char* outdoorSpots = "bench|wall_sit|steps_sit|lean|smoke|phone|busstop_wait|queue|window|atm|vending|cafe_seat|bar_stool|busk|vendor|guard|"
                               "door_staff|watch_view|fish|exercise|sunbathe|sleep_rough|talk_ring|cover";
    const char* indoorSpots = "bench|wall_sit|steps_sit|lean|smoke|phone|queue|window|atm|vending|cafe_seat|bar_stool|busk|vendor|guard|door_staff|watch_view|"
                              "exercise|sleep_rough|talk_ring|cover|desk|counter_serve|counter_queue|browse|bed|sofa|kitchen|washroom|bar|dance";
    {
        KindDef k;
        k.kind = "world";
        k.version = 1;
        k.records.push_back(Rec("world", 2, false, {R("name", VT_TEXT)}));
        k.records.push_back(Rec("district", 2, false, {R("name", VT_TEXT), F("label", VT_POINT, 2), F("order", VT_INT)}));
        kinds.push_back(k);
    }
    {
        KindDef k;
        k.kind = "district";
        k.version = 1;
        k.header = {R("district", VT_ID)};
        k.records.push_back(Rec("bounds", 0, false, {R("poly", VT_POINTS, 2)}));
        k.records.push_back(Rec("label", 0, false, {R("at", VT_POINT, 2), F("text", VT_TEXT)}));
        k.records.push_back(Rec("zone", 2, false,
                                {R("kind", VT_WORD, -1, "district|use|turf|density|audio|habitat|police|map"), R("poly", VT_POINTS, 2), F("tags", VT_LIST), F("peds", VT_ANY, 2),
                                 F("traffic", VT_ANY, 2), F("days", VT_ANY)}));
        k.records.push_back(Rec("trunk", 0, false, {R("node", VT_REF)}));
        kinds.push_back(k);
    }
    {
        // the roads, with the AI agent's road-graph requirements (docs/HANDBUILT_WORLD.md section 12): everything the lane
        // graph can derive stays derived, and these records override it where authored
        KindDef k;
        k.kind = "roads";
        k.version = 1;
        k.header = {F("district", VT_ID), F("scope", VT_WORD, -1, "trunk")};
        k.records.push_back(Rec("node", 2, false,
                                {R("at", VT_POINT, 2), R("z", VT_NUM, 2), F("control", VT_WORD, -1, "none|stop|yield|signal"), F("plan", VT_ANY),
                                 F("offset", VT_NUM, 1), F("approaches", VT_ANY), F("stops", VT_ANY), F("yield", VT_LIST), F("bulb", VT_ANY, 2)}));
        k.records.push_back(Rec("edge", 2, true,
                                {R("from", VT_REF), R("to", VT_REF), R("class", VT_WORD, -1, "highway|boulevard|avenue|street|lane|rural|dirt|ramp"),
                                 F("name", VT_TEXT), R("lanes", VT_ANY), F("lanewidth", VT_NUM, 2), F("median", VT_ANY, 2), F("shoulder", VT_ANY, 2),
                                 F("parking", VT_ANY, 2), F("sidewalk", VT_ANY, 2), F("speed", VT_NUM, 1), F("flags", VT_LIST), R("shape", VT_POINTS, 2),
                                 F("profile", VT_ANY, 2)},
                                {"lanes", "lane", "noswitch", "moves", "crossing", "busstop", "parkbay", "nostop", "loading", "taxirank", "curbcut", "nojaywalk",
                                 "deck", "sign"}));
        const char* dirs = "fwd|bwd";
        k.records.push_back(Rec("lanes", 0, false,
                                {F("from", VT_ANY, 2), F("to", VT_ANY, 2), R("dir", VT_WORD, -1, dirs), R("count", VT_INT), F("add", VT_WORD, -1, sides),
                                 F("drop", VT_WORD, -1, sides), F("taper", VT_NUM, 2)}, {}, false));
        k.records.push_back(Rec("lane", 0, false,
                                {R("dir", VT_WORD, -1, dirs), R("index", VT_ANY), R("use", VT_WORD, -1, "bus|streetcar|bike|general"), F("from", VT_ANY, 2),
                                 F("to", VT_ANY, 2)}, {}, false));
        k.records.push_back(Rec("noswitch", 0, false, {R("dir", VT_WORD, -1, dirs), F("from", VT_ANY, 2), F("to", VT_ANY, 2)}, {}, false));
        k.records.push_back(Rec("moves", 0, false, {R("at", VT_WORD, -1, "n0|n1"), R("lanes", VT_LIST)}, {}, false));
        k.records.push_back(Rec("crossing", 0, false, {R("at", VT_NUM, 2), R("kind", VT_WORD, -1, "signal|zebra|plain|unmarked"), F("w", VT_NUM, 2)}, {}, false));
        k.records.push_back(Rec("busstop", 2, false,
                                {R("at", VT_NUM, 2), R("side", VT_WORD, -1, sides), F("kind", VT_WORD, -1, "bus|streetcar"), F("shelter", VT_REF), F("wait", VT_ANY, 2),
                                 F("routes", VT_LIST), F("clear", VT_NUM, 2)}, {}, false));
        k.records.push_back(Rec("parkbay", 0, false,
                                {R("from", VT_NUM, 2), R("to", VT_NUM, 2), R("side", VT_WORD, -1, sides), R("kind", VT_WORD, -1, "parallel|angle|perp"), F("meter", VT_INT),
                                 F("disabled", VT_INT)}, {}, false));
        k.records.push_back(Rec("nostop", 0, false,
                                {R("from", VT_NUM, 2), R("to", VT_NUM, 2), R("side", VT_WORD, -1, sides),
                                 F("why", VT_WORD, -1, "junction|crossing|hydrant|busstop|driveway|bridge|other")}, {}, false));
        k.records.push_back(Rec("loading", 0, false, {R("from", VT_NUM, 2), R("to", VT_NUM, 2), R("side", VT_WORD, -1, sides), F("hours", VT_ANY, 2)}, {}, false));
        k.records.push_back(Rec("taxirank", 0, false, {R("from", VT_NUM, 2), R("to", VT_NUM, 2), R("side", VT_WORD, -1, sides), F("cap", VT_INT)}, {}, false));
        k.records.push_back(Rec("curbcut", 0, false, {R("at", VT_NUM, 2), R("side", VT_WORD, -1, sides), R("w", VT_NUM, 2), F("to", VT_REF)}, {}, false));
        k.records.push_back(Rec("nojaywalk", 0, false, {R("from", VT_NUM, 2), R("to", VT_NUM, 2), F("side", VT_WORD, -1, "left|right|both")}, {}, false));
        k.records.push_back(Rec("deck", 0, false, {R("from", VT_NUM, 2), R("to", VT_NUM, 2), R("kind", VT_WORD, -1, "bridge|viaduct|overpass")}, {}, false));
        k.records.push_back(Rec("sign", 0, false,
                                {R("at", VT_NUM, 2), R("side", VT_WORD, -1, sides), R("kind", VT_WORD, -1, "stop|yield|speed|oneway|noentry|name"), F("value", VT_ANY)}, {},
                                false));
        k.records.push_back(Rec("junction", 2, true,
                                {F("kind", VT_WORD, -1, "plain|roundabout"), F("turns", VT_WORD, -1, "all|no_uturn|custom"), F("box", VT_POINTS, 2)},
                                {"stopline", "connect", "ban", "head"}));
        k.records.push_back(Rec("stopline", 0, false, {R("edge", VT_REF), R("at", VT_NUM, 2)}, {}, false));
        k.records.push_back(Rec("connect", 0, false,
                                {R("from", VT_ANY), R("to", VT_ANY), R("turn", VT_WORD, -1, "left|right|straight|uturn"), F("protected", VT_INT)}, {}, false));
        k.records.push_back(Rec("ban", 0, false, {R("from", VT_REF), R("to", VT_REF)}, {}, false));
        k.records.push_back(Rec("head", 0, false, {R("at", VT_POINT3, 2), R("faces", VT_ANY)}, {}, false));
        kinds.push_back(k);
    }
    {
        // signal plans (lib/signals): phases by movement (approach and turn) and by crossing; plan=auto on a node keeps the
        // derived plan
        KindDef k;
        k.kind = "signal";
        k.version = 1;
        k.header = {R("id", VT_ID)};
        k.records.push_back(Rec("plan", 0, false, {R("cycle", VT_NUM, 1), F("legs", VT_INT)}));
        k.records.push_back(Rec("phase", 2, false,
                                {R("green", VT_NUM, 1), R("amber", VT_NUM, 1), R("allred", VT_NUM, 1), F("moves", VT_LIST), F("walk", VT_LIST), F("walktime", VT_NUM, 1),
                                 F("flash", VT_NUM, 1)}));
        kinds.push_back(k);
    }
    {
        KindDef k;
        k.kind = "block";
        k.version = 1;
        k.header = {R("id", VT_ID), R("district", VT_ID)};
        k.records.push_back(Rec("parcel", 2, true,
                                {R("poly", VT_POINTS, 2), R("use", VT_WORD, -1, "residential|commercial|mixed|industrial|civic|park|parking|vacant"),
                                 F("grade", VT_NUM, 2), F("blend", VT_NUM, 2), F("ground", VT_WORD, -1, grounds), F("front", VT_ANY)},
                                {"paint", "fence", "pad"}));
        k.records.push_back(Rec("paint", 0, false, {R("kind", VT_WORD, -1, grounds), R("poly", VT_POINTS, 2)}, {}, false));
        k.records.push_back(Rec("fence", 0, false, {R("kind", VT_REF), R("line", VT_POINTS, 2), F("gate", VT_ANY, 2)}, {}, false));
        k.records.push_back(Rec("pad", 0, false, {R("poly", VT_POINTS, 2), R("z", VT_NUM, 2), R("kind", VT_WORD, -1, "paving|concrete|deck")}, {}, false));
        k.records.push_back(Rec("plaza", 1, false, {R("poly", VT_POINTS, 2), F("ground", VT_WORD, -1, grounds), F("tags", VT_LIST)}));
        k.records.push_back(Rec("path", 2, false,
                                {R("line", VT_POINTS, 2), R("w", VT_NUM, 2), R("kind", VT_WORD, -1, "walk|plaza|alley|trail|boardwalk|footbridge|underpass"),
                                 F("profile", VT_ANY, 2)}));
        k.records.push_back(Rec("steps", 0, false, {R("line", VT_POINTS, 2), R("w", VT_NUM, 2), F("treads", VT_INT), F("rise", VT_NUM, 2)}));
        k.records.push_back(Rec("tree", 0, false, {R("kind", VT_REF), R("at", VT_POINT, 2), F("h", VT_NUM, 2), F("yaw", VT_NUM, 1), F("canopy", VT_NUM, 2)}));
        k.records.push_back(Rec("plant", 0, false, {R("kind", VT_REF), R("at", VT_POINT, 2), F("yaw", VT_NUM, 1), F("scale", VT_NUM, 2)}));
        k.records.push_back(Rec("prop", 0, false, {R("kind", VT_REF), R("at", VT_POINT, 2), F("z", VT_NUM, 2), F("yaw", VT_NUM, 1), F("look", VT_INT)}));
        k.records.push_back(Rec("light", 0, false, {R("kind", VT_REF), R("at", VT_ANY, 2), F("yaw", VT_NUM, 1), F("sched", VT_WORD, -1, scheds)}));
        k.records.push_back(Rec("spot", 1, false, spotFields(outdoorSpots, false)));
        k.records.push_back(Rec("lot", 2, true, {R("entry", VT_ANY, 2), F("exit", VT_ANY, 2), F("levels", VT_INT)}, {"row", "ramp"}));
        k.records.push_back(Rec("row", 0, false,
                                {R("line", VT_POINTS, 2), R("bays", VT_INT), F("w", VT_NUM, 2), F("len", VT_NUM, 2), F("yaw", VT_NUM, 1), F("level", VT_INT)}, {}, false));
        k.records.push_back(Rec("ramp", 0, false, {R("from", VT_INT), R("to", VT_INT), R("line", VT_POINTS, 2)}, {}, false));
        kinds.push_back(k);
    }
    {
        KindDef k;
        k.kind = "building";
        k.version = 1;
        k.header = {R("id", VT_ID), R("block", VT_ID), R("parcel", VT_ID)};
        k.records.push_back(Rec("place", 0, false, {R("plan", VT_REF), R("at", VT_POINT, 2), R("z", VT_NUM, 2), R("yaw", VT_NUM, 1), F("mirror", VT_INT)}));
        k.records.push_back(Rec("config", 0, false, {F("palette", VT_REF), F("roof", VT_REF), F("facade", VT_REF), F("*", VT_ANY)}));
        k.records.push_back(Rec("slot", 2, false,
                                {F("unit", VT_REF), F("fitout", VT_REF), F("variant", VT_ID), F("household", VT_ID), F("sign", VT_TEXT), F("sign_style", VT_ID)}));
        k.records.push_back(Rec("furnish", 0, false, {F("household", VT_ID), F("variant", VT_ID)}));
        k.records.push_back(Rec("override", 0, false,
                                {F("room", VT_ANY), F("variant", VT_ID), F("add", VT_REF), F("remove", VT_ANY), F("wall", VT_ANY), F("side", VT_WORD, -1, "in|out"),
                                 F("at", VT_NUM, 2), F("h", VT_NUM, 2), F("door", VT_ANY), F("style", VT_REF), F("lock", VT_WORD, -1, locks)}));
        k.records.push_back(Rec("occupancy", 0, false, {F("residents", VT_INT), F("workers", VT_INT), F("schedule", VT_ID), F("hours", VT_ANY, 2)}));
        k.records.push_back(Rec("outbuilding", 2, false, {R("plan", VT_REF), R("at", VT_POINT, 2), F("z", VT_NUM, 2), R("yaw", VT_NUM, 1)}));
        kinds.push_back(k);
    }
    // The library (docs/NTW_FORMAT.md 9.7-9.10)
    const char* roomTypes = "living|kitchen|bed|bath|hall|stair|office|shop|store|lobby|corridor|plant|garage|dining|utility|restaurant|bar";
    const char* windowKinds = "fixed|dhung|casement|slider|storefront|jalousie|glassblock";
    {
        KindDef k;
        k.kind = "plan";
        k.version = 1;
        k.header = {R("id", VT_ID)};
        k.records.push_back(Rec("meta", 0, false,
                                {R("class", VT_WORD, -1,
                                   "tower|midrise|condo|deco|shop|stripmall|house|villa|warehouse|factory|farmhouse|barn|motel|gasstation|garage|church|shack|landmark"),
                                 R("family", VT_ID), F("era", VT_INT), R("floors", VT_INT), F("beds", VT_INT), F("baths", VT_INT), R("size", VT_ANY, 2), F("tags", VT_LIST)}));
        k.records.push_back(Rec("footprint", 0, false, {R("poly", VT_POINTS, 2)}));
        k.records.push_back(Rec("floor", 2, false, {R("layout", VT_REF), R("z", VT_NUM, 2), R("h", VT_NUM, 2), F("access", VT_ANY)}));
        k.records.push_back(Rec("roof", 0, false,
                                {R("form", VT_WORD, -1, "flat|gable|hip|shed|mansard|barrel"), F("pitch", VT_NUM, 1), F("overhang", VT_NUM, 2), F("material", VT_REF)}));
        k.records.push_back(Rec("facade", 0, false, {R("style", VT_REF), F("palette", VT_REF)}));
        k.records.push_back(Rec("option", 2, false, {R("choices", VT_LIST), R("default", VT_ID)}));
        k.records.push_back(Rec("feature", 2, false, {R("when", VT_ANY), R("mesh", VT_REF), F("span", VT_POINTS, 2), F("depth", VT_NUM, 2), F("h", VT_NUM, 2)}));
        k.records.push_back(Rec("entrance", 2, false, {R("door", VT_ANY), F("kind", VT_WORD, -1, "main|service|garage|fire")}));
        kinds.push_back(k);
    }
    for (const char* kindName : {"layout", "unit", "fitout"}) {
        KindDef k;
        k.kind = kindName;
        k.version = 1;
        k.header = {R("id", VT_ID), F("slot_size", VT_ANY, 2), F("kind", VT_ID)};
        k.records.push_back(Rec("wall", 2, false,
                                {R("a", VT_POINT, 2), R("b", VT_POINT, 2), R("t", VT_NUM, 2), F("ext", VT_WORD, -1, "front|back|left|right"), F("finish", VT_REF)}));
        k.records.push_back(Rec("room", 2, false,
                                {R("type", VT_WORD, -1, roomTypes), R("poly", VT_POINTS, 2), F("cap", VT_INT), F("sched", VT_ID), F("lux", VT_INT), F("outdoor", VT_INT)}));
        k.records.push_back(Rec("door", 2, false,
                                {R("wall", VT_REF), R("at", VT_NUM, 2), R("w", VT_NUM, 2), R("h", VT_NUM, 2),
                                 R("kind", VT_WORD, -1, "hinged|pair|sliding|revolving|rollup|lift|opening"), F("hinge", VT_WORD, -1, sides),
                                 F("swing", VT_WORD, -1, "in|out"), F("style", VT_REF), R("leads", VT_ANY), F("lock", VT_WORD, -1, locks), F("hours", VT_ANY, 2)}));
        k.records.push_back(Rec("window", 2, false,
                                {R("wall", VT_REF), R("at", VT_NUM, 2), R("w", VT_NUM, 2), R("h", VT_NUM, 2), R("sill", VT_NUM, 2), R("kind", VT_WORD, -1, windowKinds),
                                 R("room", VT_REF)}));
        k.records.push_back(Rec("stair", 2, false,
                                {R("from", VT_ANY), R("to", VT_ANY), R("run", VT_POINTS, 2), R("w", VT_NUM, 2), R("kind", VT_WORD, -1, "straight|dogleg|spiral"),
                                 F("treads", VT_INT), F("rise", VT_NUM, 2)}));
        k.records.push_back(Rec("lift", 2, false,
                                {R("at", VT_POINT, 2), R("size", VT_ANY, 2), R("stops", VT_LIST), F("door", VT_ANY), F("cap", VT_INT), F("call", VT_POINT, 2)}));
        k.records.push_back(Rec("slot", 2, false, {R("poly", VT_POINTS, 2), R("entry", VT_REF), R("allow", VT_LIST)}));
        k.records.push_back(Rec("furnish", 2, true, {R("variants", VT_LIST)}, {"variant", "item"}));
        k.records.push_back(Rec("variant", 2, false, {F("set", VT_REF), F("anchor", VT_REF)}, {}, false));
        k.records.push_back(Rec("item", 0, false,
                                {R("kind", VT_REF), R("at", VT_POINT, 2), F("z", VT_NUM, 2), F("yaw", VT_NUM, 1), F("look", VT_INT), F("variant", VT_ID)}));
        k.records.push_back(Rec("spot", 1, false, spotFields(indoorSpots, true)));
        k.records.push_back(Rec("light", 2, false,
                                {R("room", VT_REF), R("at", VT_POINT3, 2), R("fixture", VT_REF), F("sched", VT_ID), F("anim", VT_WORD, -1, "none|tv|neon|flicker")}));
        k.records.push_back(Rec("counter", 2, false, {R("line", VT_POINTS, 2), R("h", VT_NUM, 2)}));
        k.records.push_back(Rec("sign", 0, false, {R("wall", VT_REF), R("at", VT_NUM, 2), R("w", VT_NUM, 2), R("h", VT_NUM, 2)}));
        kinds.push_back(k);
    }
    {
        KindDef k;
        k.kind = "set";
        k.version = 1;
        k.header = {R("id", VT_ID), R("room", VT_WORD, -1, roomTypes), F("style", VT_ID), F("income", VT_WORD, -1, "low|mid|high")};
        k.records.push_back(Rec("fits", 0, false, {R("w", VT_ANY, 2), R("d", VT_ANY, 2)}));
        k.records.push_back(Rec("item", 0, false, {R("kind", VT_REF), R("at", VT_POINT, 2), F("z", VT_NUM, 2), F("yaw", VT_NUM, 1), F("look", VT_INT)}));
        kinds.push_back(k);
    }
    {
        KindDef k;
        k.kind = "facade";
        k.version = 1;
        k.header = {R("id", VT_ID)};
        k.records.push_back(Rec("wall", 0, false, {R("material", VT_REF), F("color", VT_ID)}));
        k.records.push_back(Rec("trim", 0, false,
                                {R("kind", VT_WORD, -1, "none|band|cornice|quoin|eyebrow"), F("material", VT_REF), F("color", VT_ID), F("h", VT_NUM, 2)}));
        k.records.push_back(Rec("window", 0, false,
                                {R("kind", VT_WORD, -1, windowKinds), F("frame", VT_REF), F("color", VT_ID), F("shutters", VT_WORD, -1, "none|fixed|bahama")}));
        k.records.push_back(Rec("door", 0, false, {R("style", VT_REF), F("color", VT_ID)}));
        k.records.push_back(Rec("base", 0, false, {F("h", VT_NUM, 2), F("material", VT_REF)}));
        kinds.push_back(k);
    }
    {
        KindDef k;
        k.kind = "palette";
        k.version = 1;
        k.header = {R("family", VT_ID)};
        k.records.push_back(Rec("palette", 2, false, {R("wall", VT_COLOR), R("trim", VT_COLOR), R("door", VT_COLOR), F("accent", VT_COLOR), F("roof", VT_COLOR)}));
        kinds.push_back(k);
    }
    {
        KindDef k;
        k.kind = "places";
        k.version = 1;
        k.records.push_back(Rec("place", 2, false,
                                {F("building", VT_REF), F("door", VT_ANY), R("walk", VT_POINT, 2), F("curb", VT_POINT3, 2), F("dir", VT_POINT, 2), F("water", VT_POINTS, 2),
                                 F("note", VT_TEXT)}));
        k.records.push_back(Rec("anchor", 2, false,
                                {R("kind", VT_WORD, -1,
                                   "police_station|police_bays|garage_exit|booking_door|release|k9|swat|patrol_loop|foot_beat|break|roadblock|helipad|boat_launch|"
                                   "ambulance_bay|er_door|fire_station|tow_yard|event"),
                                 F("event", VT_ID), F("at", VT_POINT, 2), F("z", VT_NUM, 2), F("yaw", VT_NUM, 1), F("poly", VT_POINTS, 2), F("line", VT_POINTS, 2),
                                 F("building", VT_REF), F("door", VT_ANY), F("edge", VT_REF), F("cap", VT_INT), F("tags", VT_LIST)}));
        kinds.push_back(k);
    }
    {
        KindDef k;
        k.kind = "transit";
        k.version = 1;
        k.header = {R("line", VT_ID), R("kind", VT_WORD, -1, "skyline|bus|ferry|streetcar"), F("name", VT_TEXT)};
        k.records.push_back(Rec("route", 0, false, {F("edges", VT_LIST), F("pts", VT_POINTS, 2), F("profile", VT_ANY, 2)}));
        k.records.push_back(Rec("station", 2, false, {R("at", VT_POINT, 2), F("z", VT_NUM, 2), R("name", VT_TEXT), F("side", VT_WORD, -1, "left|right|both")}));
        k.records.push_back(Rec("stop", 2, false, {R("busstop", VT_REF)}));
        k.records.push_back(Rec("pier", 2, false, {R("at", VT_POINT, 2), R("yaw", VT_NUM, 1), R("name", VT_TEXT)}));
        k.records.push_back(Rec("pole", 0, false, {R("at", VT_POINT, 2), F("h", VT_NUM, 2)}));
        k.records.push_back(Rec("service", 0, false, {R("headway", VT_NUM, 1), F("first", VT_NUM, 2), F("last", VT_NUM, 2)}));
        kinds.push_back(k);
    }
    {
        KindDef k;
        k.kind = "catalog";
        k.version = 1;
        k.records.push_back(Rec("piece", 2, false,
                                {R("code", VT_ANY), F("size", VT_ANY, 2), F("variants", VT_INT), F("params", VT_LIST), F("collide", VT_WORD, -1, "box|cylinder|none")}));
        kinds.push_back(k);
    }
    return kinds;
}

bool wordAllowed(const char* words, const std::string& v) {
    if (!words) return false;
    const char* p = words;
    while (*p) {
        const char* e = strchr(p, '|');
        const size_t n = e ? (size_t)(e - p) : strlen(p);
        if (v.size() == n && v.compare(0, n, p, n) == 0) return true;
        if (!e) break;
        p = e + 1;
    }
    return false;
}

bool valueOk(const FieldDef& f, const std::string& v) {
    switch (f.type) {
        case VT_NUM: return toNumber(v, nullptr);
        case VT_INT: return toInt(v, nullptr);
        case VT_ID:
        case VT_REF: return isId(v);
        case VT_WORD: return wordAllowed(f.words, v);
        case VT_POINT: return toPoint(v, nullptr);
        case VT_POINT3: return toPoint3(v, nullptr);
        case VT_POINTS: {
            std::vector<vec2> pts;
            return toPoints(v, pts);
        }
        case VT_LIST: {
            std::vector<std::string> items;
            return toList(v, items);
        }
        case VT_HEX:
            if (v.empty()) return false;
            for (char c : v)
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
            return true;
        case VT_COLOR:
            if (v.size() != 7 || v[0] != '#') return false;
            for (size_t i = 1; i < 7; i++)
                if (!((v[i] >= '0' && v[i] <= '9') || (v[i] >= 'a' && v[i] <= 'f'))) return false;
            return true;
        case VT_TEXT:
        case VT_ANY: return true;
    }
    return true;
}

const char* typeName(ValueType t) {
    switch (t) {
        case VT_NUM: return "a number";
        case VT_INT: return "an integer";
        case VT_ID: return "an id";
        case VT_REF: return "an id reference";
        case VT_WORD: return "one of its words";
        case VT_TEXT: return "text";
        case VT_POINT: return "a point x,y";
        case VT_POINT3: return "a point x,y,z";
        case VT_POINTS: return "points x,y;x,y;...";
        case VT_LIST: return "a list a,b,c";
        case VT_HEX: return "lowercase hex digits";
        case VT_COLOR: return "a colour #rrggbb";
        case VT_ANY: return "a value";
    }
    return "a value";
}

void checkFields(const std::vector<FieldDef>& defs, const std::vector<Field>& fields, const std::string& what, int line, std::vector<Error>& errors) {
    bool wildcard = false;
    for (const FieldDef& fd : defs) wildcard |= strcmp(fd.key, "*") == 0;
    for (const Field& f : fields) {
        const FieldDef* d = nullptr;
        for (const FieldDef& fd : defs)
            if (f.key == fd.key) d = &fd;
        if (!d) {
            if (!wildcard) errors.push_back({f.line, "unknown field '" + f.key + "' in " + what});
            continue;
        }
        if (!valueOk(*d, f.value)) {
            std::string msg = what + ": " + f.key + "=" + f.value + " should be " + typeName(d->type);
            if (d->type == VT_WORD && d->words) msg += std::string(" (") + d->words + ")";
            errors.push_back({f.line, msg});
        }
    }
    for (const FieldDef& fd : defs) {
        if (!fd.required) continue;
        bool found = false;
        for (const Field& f : fields) found |= f.key == fd.key;
        if (!found) errors.push_back({line, what + " needs " + fd.key + "="});
    }
}

void checkRecords(const KindDef& k, const std::vector<Record>& recs, const RecordDef* parent, std::vector<Error>& errors) {
    for (const Record& r : recs) {
        const RecordDef* d = findRecord(k, r.type);
        if (!d) {
            errors.push_back({r.line, "unknown record '" + r.type + "' in a " + std::string(k.kind) + " file"});
            continue;
        }
        if (parent) {
            bool allowed = false;
            for (const std::string& c : parent->children) allowed |= c == r.type;
            if (!allowed) errors.push_back({r.line, "a " + r.type + " record can't stand inside a " + std::string(parent->type) + " block"});
        } else if (!d->topLevel) {
            errors.push_back({r.line, "a " + r.type + " record belongs inside a block"});
        }
        if (d->id == 0 && !r.id.empty()) errors.push_back({r.line, "a " + r.type + " record has no id"});
        if (d->id == 2 && r.id.empty()) errors.push_back({r.line, "a " + r.type + " record needs an id"});
        checkFields(d->fields, r.fields, "the " + r.type + " record", r.line, errors);
        if (!r.children.empty()) checkRecords(k, r.children, d, errors);
    }
}

const std::vector<std::string>* fieldOrder(const std::string& kind, const std::string& type) {
    // the keys in schema order, per kind and type, built once
    static std::vector<std::pair<std::string, std::vector<std::string>>> cache;
    static bool built = false;
    if (!built) {
        for (const KindDef& k : schemaKinds()) {
            std::vector<std::string> head;
            for (const FieldDef& f : k.header) head.push_back(f.key);
            cache.push_back(std::make_pair(std::string(k.kind) + "/ntw", head));
            for (const RecordDef& r : k.records) {
                std::vector<std::string> keys;
                for (const FieldDef& f : r.fields) keys.push_back(f.key);
                cache.push_back(std::make_pair(std::string(k.kind) + "/" + r.type, keys));
            }
        }
        built = true;
    }
    const std::string key = kind + "/" + type;
    for (const auto& c : cache)
        if (c.first == key) return &c.second;
    return nullptr;
}

int decimals(const std::string& kind, const std::string& type, const std::string& key) {
    const KindDef* k = findKind(kind);
    if (!k) return -1;
    const std::vector<FieldDef>* defs = nullptr;
    if (type == "ntw") defs = &k->header;
    else if (const RecordDef* r = findRecord(*k, type)) defs = &r->fields;
    if (!defs) return -1;
    for (const FieldDef& f : *defs)
        if (key == f.key) return f.decimals;
    return -1;
}

}  // namespace ntwschema_detail

const std::vector<KindDef>& schemaKinds() {
    static const std::vector<KindDef> kinds = ntwschema_detail::build();
    return kinds;
}

const KindDef* findKind(const std::string& kind) {
    for (const KindDef& k : schemaKinds())
        if (kind == k.kind) return &k;
    return nullptr;
}

const RecordDef* findRecord(const KindDef& k, const std::string& type) {
    for (const RecordDef& r : k.records)
        if (type == r.type) return &r;
    return nullptr;
}

bool schemaOpensBlock(const std::string& kind, const std::string& type) {
    const KindDef* k = findKind(kind);
    if (!k) return false;
    const RecordDef* r = findRecord(*k, type);
    return r && r->block;
}

FormatRules schemaFormatRules() {
    FormatRules rules;
    rules.fieldOrder = ntwschema_detail::fieldOrder;
    rules.decimals = ntwschema_detail::decimals;
    return rules;
}

void checkDocument(const Document& doc, std::vector<Error>& errors) {
    using namespace ntwschema_detail;
    const KindDef* k = findKind(doc.kind);
    if (!k) {
        errors.push_back({1, "unknown file kind '" + doc.kind + "'"});
        return;
    }
    if (doc.version != k->version) errors.push_back({1, "a " + doc.kind + " file is version " + std::to_string(k->version)});
    checkFields(k->header, doc.header, "the ntw line", 1, errors);
    checkRecords(*k, doc.records, nullptr, errors);
}

}  // namespace Ntw
}  // namespace World
