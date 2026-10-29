// worldv's framework and its checks of ids, file paths and references (validate.h).
#include "validate.h"
#include "validate_layout.h"
#include "validate_roads.h"
#include "ntwschema.h"
#include "worldmodel.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <unordered_map>
#include <chrono>
#include <cstdio>
#include <cstring>

namespace World {
namespace Validate {

namespace validate_detail {

// The results cache is only reused by the validator that wrote it: any rebuild (new checks, schema or formatter) and any
// change to the rules' version starts afresh. Raise the version when a check changes what it reports.
const char* const kRulesVersion = "worldv rules 3";
const char* const kBuildStamp = __DATE__ " " __TIME__;

const std::vector<CheckInfo> kChecks = {
    {"file.read", "a file, or the world directory, can't be read"},
    {"file.parse", "the NTW syntax is broken (docs/NTW_FORMAT.md sections 1-4)"},
    {"file.schema", "the file breaks its kind's schema (section 9): records, blocks, ids, fields, value types"},
    {"file.canonical", "the file isn't in canonical form (section 6): run ntwfmt on it"},
    {"id.form", "an id doesn't follow its pattern (section 5), or disagrees with the file's other ids"},
    {"id.unique", "an id is defined twice"},
    {"id.path", "the file isn't where its kind and id put it (section 8)"},
    {"ref.form", "a field that names records doesn't have its shape (<edge>:<side>, a list of edges, ...)"},
    {"ref.missing", "a reference names nothing"},
    {"ref.kind", "a reference names a record of the wrong kind"},
    {"piece.body", "a catalog piece's body= (its solid part) isn't WxDxH, or doesn't fit inside its size="},
    // inside a layout, unit or fit-out (docs/HANDBUILT_WORLD.md 9.1, and the kit's limits of 4.9)
    {"wall.cross", "two walls cross, or run along each other"},
    {"room.closed", "a room's polygon has an edge no wall closes, or no proper shape (outdoor rooms are open)"},
    {"room.overlap", "two rooms overlap"},
    {"room.reach", "a room or slot can't be reached from a way in (warning: nothing leads into the item at all)"},
    {"door.wall", "a door doesn't fit its wall: past its ends, through a wall that meets it, or over another opening"},
    {"door.rooms", "a door doesn't join the two rooms of its leads="},
    {"door.width", "a door under 0.8 m clear (warning: under 0.9 m with a turn right behind it)"},
    {"door.landing", "a stair starts or ends less than 0.9 m in front of a door"},
    {"window.wall", "a window doesn't fit its wall, or isn't on an exterior wall of a layout"},
    {"window.room", "a window's room= isn't the room behind it"},
    {"corridor.width", "a corridor or hall under 1.0 m wide"},
    {"stair.run", "a stair's run has the wrong number of points or no proper shape for its kind"},
    {"stair.tread", "the tread at the walking line is under 0.25 m (warning: outside the kit's defaults)"},
    {"stair.riser", "warning: risers outside 0.15-0.22 m"},
    {"stair.landing", "a landing shallower than its flight is wide or than 0.9 m (warning: an L landing outside 0.9-1.0 m)"},
    {"stair.walkline", "a walking line under 0.5 m from the centre or inner corner (warning: a spiral's under 0.55 m)"},
    {"stair.width", "warning: an L stair's flights outside 0.9-1.0 m, or winders on a flight under 1.2 m"},
    {"stair.radius", "warning: a curved stair walked at a radius under 2 m"},
    {"stair.busy", "warning: winders on a busy stair (in a lobby, shop or office)"},
    // across a plan's floors (docs/HANDBUILT_WORLD.md 9.1)
    {"stair.floors", "a stair doesn't join the floors it claims: not upward, its ends outside the rooms it names, or its treads not climbing the floors' difference in height"},
    {"lift.stops", "a lift stops at a floor whose layout doesn't have the same lift (id, place, size and stops)"},
    {"floor.access", "a floor's hatch doesn't come up from a lower floor into one of its rooms"},
    {"door.height", "a door taller than its floor"},
    {"window.height", "a window whose top (sill + h) is above its floor's height"},
    // the roads, their signal plans and transit, on the world model (validate_roads.h; docs/HANDBUILT_WORLD.md 9.1, 12.6)
    {"value.form", "a compound value doesn't have its shape (lanes=F/B, median=W:barrier, shoulder=both:W, profile=s:z;..., wait=x,y:r, peds= 24 numbers)"},
    {"road.loop", "an edge starts and ends at one node: a loop needs a node of its own along it"},
    {"road.shape", "an edge's shape has fewer than 2 points or two in one place, or its ends miss its nodes by more than 2 cm"},
    {"road.profile", "an edge's profile doesn't rise from 0 to its shape's length, or doesn't start and end at its nodes' heights"},
    {"road.along", "a position or stretch along an edge (at=, from=, to=) is off the edge or runs backwards, or two lanes records of one direction overlap"},
    {"road.overlap", "two carriageways lie on each other for more than 10 m outside the junctions, or cross at grade with no node"},
    {"road.moves", "a moves record doesn't match the lanes arriving at its end, or a lane turns where no road with lanes leads"},
    {"road.sector", "a junction or a signalled node has two legs in one compass sector (n, e, s, w) that its approaches= doesn't tell apart"},
    {"road.legs", "a node's approaches=, stops= or yield=, or its junction's records, name an edge that doesn't meet it, or lanes it doesn't have there"},
    {"road.connected", "a piece of the road network is joined to none of the rest (warning: a node with no edges)"},
    {"signal.plan", "a signal plan's cycle isn't the sum of its phases, a movement or crossing is misnamed, or a walk outlasts its phase"},
    {"signal.node", "a node's signal plan doesn't fit it: control= isn't signal, too few legs, approaches it doesn't have, or its offset (warning: legs it lacks)"},
    {"parking.clash", "a parking bay overlaps a nostop, a curb cut or another bay on its side of the edge"},
    {"busstop.kerb", "a bus stop on a highway or ramp, at a kerb no lanes run along, or with no sidewalk to wait on"},
    {"transit.route", "a line's route edges don't meet, run against a one-way edge, or take a turn the junction doesn't allow"},
    {"transit.stop", "a line stops at a bus stop off its route or on the far kerb (warning: routes= and the line's stops disagree)"},
    // on the lane graph the game builds from the roads (validate_lanes.h)
    {"road.lanes", "a lane leads nowhere at the node it runs into: no road with lanes goes on, or the junction's or the edge's records leave it no movement"},
    {"signal.cover", "a signal plan never gives one of its node's movements a green, or one of its crossings a walk"},
    {"signal.conflict", "a phase lets two movements whose paths meet go together with neither a permissive turn, or gives a walk across traffic that doesn't yield"},
};

// ---------------------------------------------------------------------------------------------------- small helpers

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    size_t from = 0;
    for (size_t i = 0; i <= s.size(); i++)
        if (i == s.size() || s[i] == sep) {
            parts.push_back(s.substr(from, i - from));
            from = i + 1;
        }
    return parts;
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0; }
bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// One part of an id ([a-z0-9_-]+): ids are parts joined by dots
bool isPart(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}

bool allParts(const std::vector<std::string>& parts) {
    for (const std::string& p : parts)
        if (!isPart(p)) return false;
    return !parts.empty();
}

// A district id: a lowercase letter, then letters and digits (cl)
bool isDistrictId(const std::string& s) {
    if (s.empty() || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

// `s` is `prefix` and then a number of at least minDigits digits, zero-padded (cl.n0012, and cl.n12345 past 9999)
bool numbered(const std::string& s, const std::string& prefix, size_t minDigits) {
    if (!startsWith(s, prefix)) return false;
    const size_t n = s.size() - prefix.size();
    if (n < minDigits || n > 12) return false;
    for (size_t i = prefix.size(); i < s.size(); i++)
        if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

// FNV-1a, 64 bits, over the file's bytes as they are on disk
u64 hashBytes(const std::string& s, u64 h = 0xcbf29ce484222325ull) {
    for (unsigned char c : s) h = (h ^ c) * 0x100000001b3ull;
    return h;
}

bool readFile(const std::string& path, std::string& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    const bool ok = !ferror(f);
    fclose(f);
    return ok;
}

const char* article(const std::string& word) { return !word.empty() && strchr("aeio", word[0]) ? "an" : "a"; }

// Reference classes as words: "plan", "parcel or lot" (an item's elements and floor elements without their marks)
// (catalog pieces as "kit piece", "prop or kit piece")
std::string classWords(const std::string& classes) {
    static const char* const kCatalog[] = {"kit", "prop", "veg", "light", "fence"};
    const std::vector<std::string> cs = split(classes, '|');
    std::string out;
    bool pieces = true;
    for (size_t i = 0; i < cs.size(); i++) {
        std::string c = cs[i];
        bool piece = false;
        for (const char* k : kCatalog) piece = piece || c == k;
        pieces = pieces && piece;
        if (startsWith(c, ".")) c = c.substr(1);
        else if (startsWith(c, "f.")) c = c.substr(2);
        out += (i == 0 ? "" : i + 1 == cs.size() ? " or " : ", ") + c;
    }
    return pieces ? out + " piece" : out;
}

std::string clip(const std::string& s, size_t n = 80) { return s.size() <= n ? s : s.substr(0, n - 3) + "..."; }

// --------------------------------------------------------------------------------------------- what a file holds

// An id a file defines. Global ids are unique in the world; scoped ones are a plan's floors ("<plan>/<floor>", linked to
// their layout) and an item's elements ("<item>/<element>"), for the references that name a floor's element (f0.d1)
struct Def {
    std::string id;
    std::string cls;     // what it is: a reference class (plan, node, kit, ...) or, scoped, floor or the element's type
    std::string link;    // a building's plan, a plan floor's layout
    int line = 0;
};

// A reference to resolve across files
struct Ref {
    std::string id;      // the id it names; a floor element "<floor>.<element>", or a floor
    std::string cls;     // the classes it may name, '|' between them
    std::string scope;   // a floor reference: "plan:<id>", "building:<id>" or "layout:<id>" (each plan that uses the layout)
    std::string what;    // where it stands, for messages: "place plan=lib.plan.house.bungalow_b02"
    bool whole = true;   // the id is the field's whole value
    int line = 0;
};

// The results of one file's own checks, and what the checks across files need from it: what the cache keeps
struct FileResult {
    std::vector<Diagnostic> diags;   // (their file is filled in when they are reported)
    std::vector<Def> defs;
    std::vector<Def> scoped;
    std::vector<Ref> refs;
    size_t records = 0;
    size_t localRefs = 0;            // references resolved inside the file
    bool valid = false;              // parsed, and clean against the schema
};

struct File {
    std::string path;          // as found
    std::string rel;           // relative to the world directory, '/' between parts (the path itself in a file run)
    std::string text;
    u64 hash = 0;
    bool readOk = false;
    bool cached = false;       // `res` came from the cache
    bool deep = false;         // `res` holds all of the file's own checks (not only its ids and references)
    bool selected = true;      // its diagnostics are reported (--only)
    FileResult res;
    std::vector<Diagnostic> world;   // found by the checks across files
};

void add(std::vector<Diagnostic>& to, int line, Severity sev, const char* check, const std::string& msg) {
    Diagnostic d;
    d.line = line;
    d.severity = sev;
    d.check = check;
    d.message = msg;
    to.push_back(std::move(d));
}

size_t countRecords(const std::vector<Ntw::Record>& recs) {
    size_t n = recs.size();
    for (const Ntw::Record& r : recs) n += countRecords(r.children);
    return n;
}

// ------------------------------------------------------------------------------------------------ reference patterns

// The schema's reference table, parsed (ntwschema.h refRules and matchRefs): the world model reads its references by the
// same patterns, so worldv's reference checks and the model can't disagree

bool isLocalClass(const std::string& cls) { return startsWith(cls, "."); }
bool isFloorClass(const std::string& cls) { return startsWith(cls, "f.") || cls == "floor" || startsWith(cls, "floor|"); }

// ------------------------------------------------------------------------------------------- one file's ids and refs

// Reads one parsed file: the ids it defines, with their form and their uniqueness inside the file; the references it
// makes, resolving those that stay inside the file; and what the checks across files need
struct Scanner {
    const Ntw::Document& doc;
    FileResult& res;
    std::string self;                                // the file's own id: a library item's, a building's, a block's
    std::map<std::string, std::string> local;        // the item's elements and a plan's options: id -> record type
    std::map<std::string, int> seen;                 // "<namespace>/<id>" -> line, for ids given twice
    std::map<std::string, std::vector<std::string>> options;   // a plan's options and their choices

    Scanner(const Ntw::Document& d, FileResult& r) : doc(d), res(r) {}

    void error(int line, const char* check, const std::string& msg) { add(res.diags, line, SEV_ERROR, check, msg); }
    void form(const Ntw::Record& r, const std::string& msg) { error(r.line, "id.form", r.type + " " + r.id + ": " + msg); }
    // ids local to their file (an item's elements, a plan's floors): a pattern miss is a warning
    void localForm(const Ntw::Record& r, const std::string& msg) { add(res.diags, r.line, SEV_WARNING, "id.form", r.type + " " + r.id + ": " + msg); }

    std::string header(const char* key) const {
        const Ntw::Field* f = doc.headerField(key);
        return f ? f->value : std::string();
    }

    void def(const std::string& id, const char* cls, int line, const std::string& link = std::string()) {
        const auto it = seen.find("#/" + id);   // (twice in other files: the checks across files report it)
        if (it != seen.end()) error(line, "id.unique", std::string(cls) + " " + id + " is defined twice in this file (also at line " + std::to_string(it->second) + ")");
        else seen["#/" + id] = line;
        Def d;
        d.id = id;
        d.cls = cls;
        d.link = link;
        d.line = line;
        res.defs.push_back(d);
    }
    void scoped(const std::string& id, const std::string& cls, int line, const std::string& link = std::string()) {
        Def d;
        d.id = self + "/" + id;
        d.cls = cls;
        d.link = link;
        d.line = line;
        res.scoped.push_back(d);
    }

    // An id unique within `space` in this file
    void once(const std::string& space, const Ntw::Record& r) {
        const auto it = seen.find(space + "/" + r.id);
        if (it != seen.end()) error(r.line, "id.unique", r.type + " " + r.id + " is given twice in this file (also at line " + std::to_string(it->second) + ")");
        else seen[space + "/" + r.id] = r.line;
    }

    void libraryHeader(const char* category, size_t parts) {
        const std::vector<std::string> p = split(self, '.');
        if (p.size() != parts || p[0] != "lib" || p[1] != category || !allParts(p))
            error(1, "id.form", "the id " + self + " should be lib." + category + (parts == 4 ? ".<family>.<name>" : ".<name>"));
    }

    void scanWorld() {
        for (const Ntw::Record& r : doc.records) {
            if (r.id.empty()) continue;   // (the schema reports a missing id)
            if (r.type == "world") {
                if (!isPart(r.id)) form(r, "a world's id is one part (palmera)");
                def(r.id, "world", r.line);
            } else if (r.type == "district") {
                if (!isDistrictId(r.id)) form(r, "a district's id is a lowercase letter, then letters or digits (cl)");
                def(r.id, "district", r.line);
            }
        }
    }

    void scanDistrict() {
        self = header("district");
        if (!isDistrictId(self)) error(1, "id.form", "district=" + self + ": a district's id is a lowercase letter, then letters or digits (cl)");
        for (const Ntw::Record& r : doc.records) {
            if (r.type != "zone" || r.id.empty()) continue;
            if (!startsWith(r.id, self + ".") || !allParts(split(r.id, '.'))) form(r, "a zone's id starts with its district's (" + self + ".)");
            def(r.id, "zone", r.line);
        }
    }

    void scanRoads() {
        const std::string district = header("district");
        const bool trunk = header("scope") == "trunk";
        if (trunk == !district.empty()) {
            error(1, "id.form", trunk ? "a roads file is the trunk's (scope=trunk) or a district's (district=), not both"
                                      : "a roads file names its district (district=) or is the trunk's (scope=trunk)");
            return;
        }
        self = trunk ? "tr" : district;
        const size_t digits = trunk ? 5 : 4;
        const std::string example = trunk ? "tr.n00412" : self + ".n0012";
        for (const Ntw::Record& r : doc.records) {
            if (r.id.empty()) continue;
            if (r.type == "node") {
                if (!numbered(r.id, self + ".n", digits)) form(r, "a node's id is " + self + ".n and at least " + std::to_string(digits) + " digits (" + example + ")");
                def(r.id, "node", r.line);
            } else if (r.type == "edge") {
                if (!numbered(r.id, self + ".e", digits)) form(r, "an edge's id is " + self + ".e and at least " + std::to_string(digits) + " digits");
                def(r.id, "edge", r.line);
                for (const Ntw::Record& c : r.children) {
                    if (c.type != "busstop" || c.id.empty()) continue;
                    if (!numbered(c.id, self + ".bus.", 2)) form(c, "a bus stop's id is " + self + ".bus. and at least two digits (" + self + ".bus.08)");
                    def(c.id, "busstop", c.line);
                }
            } else if (r.type == "junction") {
                once("junction", r);   // (its id names its node: a reference)
            }
        }
    }

    void scanBlock() {
        self = header("id");
        const std::string district = header("district");
        if (!numbered(self, district + ".b", 3))
            error(1, "id.form", "the id " + self + " should be its district's, then .b and at least three digits (" + district + ".b014)");
        def(self, "block", 1);
        for (const Ntw::Record& r : doc.records) {
            if (r.id.empty()) continue;
            if (r.type == "parcel") {
                if (!numbered(r.id, self + ".p", 2)) form(r, "a parcel's id is its block's, then .p and at least two digits (" + self + ".p03)");
                def(r.id, "parcel", r.line);
            } else if (r.type == "path") {
                if (!numbered(r.id, self + ".pa", 1)) form(r, "a path's id is its block's, then .pa and a number (" + self + ".pa1)");
                def(r.id, "path", r.line);
            } else if (r.type == "plaza" || r.type == "lot" || r.type == "spot") {
                if (!startsWith(r.id, self + ".") || !allParts(split(r.id, '.'))) form(r, "the id starts with its block's (" + self + ".)");
                def(r.id, r.type == "plaza" ? "plaza" : r.type == "lot" ? "lot" : "spot", r.line);
            }
        }
    }

    void scanBuilding() {
        self = header("id");
        const std::string block = header("block"), parcel = header("parcel");
        const std::string last = startsWith(self, block + ".") ? self.substr(block.size() + 1) : std::string();
        if (last.size() < 3 || !strchr("hsmwvctdgx", last[0]) || !numbered(last, last.substr(0, 1), 2))
            error(1, "id.form", "the id " + self + " should be its block's (" + block +
                                    "), then a letter for its type (h house, s shop, m midrise, w warehouse, v villa, c condo, t tower, d deco, g garage, x other) "
                                    "and at least two digits (" + block + ".h03)");
        if (!numbered(parcel, block + ".p", 2)) error(1, "id.form", "parcel=" + parcel + " should be a parcel of its block (" + block + ".p03)");
        std::string plan;
        for (const Ntw::Record& r : doc.records) {
            if (r.type == "place" && plan.empty()) plan = r.value("plan", "");
            if (r.id.empty()) continue;
            if (r.type == "outbuilding") {
                if (!isPart(r.id)) localForm(r, "an outbuilding's id is one part (g1)");
                once("outbuilding", r);
            } else if (r.type == "slot") {
                once("slot", r);   // (its id names a slot of the plan's floors: a reference)
            }
        }
        def(self, "building", 1, plan);
    }

    void scanPlan() {
        self = header("id");
        libraryHeader("plan", 4);
        def(self, "plan", 1);
        for (const Ntw::Record& r : doc.records) {
            if (r.id.empty()) continue;
            if (r.type == "floor") {
                if (!numbered(r.id, "f", 1) && !numbered(r.id, "b", 1) && r.id != "attic" && r.id != "roof")
                    localForm(r, "a floor's id is f0, f1, ... b1 (basements), attic or roof");
                once("floor", r);
                scoped(r.id, "floor", r.line, r.value("layout", ""));
                if (r.has("access")) scoped(r.id + "#access", "access", r.line, r.value("layout", ""));   // a hatch: a way in
            } else if (r.type == "option") {
                if (!Ntw::isName(r.id)) localForm(r, "an option's id is a name (porch)");
                once("option", r);
                local[r.id] = "option";
                std::vector<std::string> choices;
                Ntw::toList(r.value("choices", ""), choices);
                options[r.id] = choices;
                res.localRefs++;
                if (!contains(choices, r.value("default", "")))
                    error(r.line, "ref.missing", "option " + r.id + " default=" + r.value("default", "") + ": not one of its choices (" + r.value("choices", "") + ")");
            } else if (r.type == "feature" || r.type == "entrance") {
                once(r.type, r);
            }
        }
    }

    void scanItem() {   // layouts, units and fit-outs
        self = header("id");
        libraryHeader(doc.kind.c_str(), 4);
        def(self, doc.kind.c_str(), 1);
        struct Element {
            const char* type;
            const char* prefix;   // nullptr: rooms, by name
        };
        static const Element kElements[] = {{"wall", "w"},   {"room", nullptr}, {"door", "d"}, {"window", "n"}, {"stair", "st"},
                                            {"lift", "lf"},  {"slot", "u"},     {"counter", "c"}, {"light", "l"}, {"spot", "s"}};
        for (const Ntw::Record& r : doc.records) {
            if (r.id.empty()) continue;
            if (r.type == "furnish") {
                once("furnish", r);   // (its id names its room: a reference)
                continue;
            }
            for (const Element& e : kElements) {
                if (r.type != e.type) continue;
                if (e.prefix ? !numbered(r.id, e.prefix, 1) : !Ntw::isName(r.id))
                    localForm(r, e.prefix ? std::string("the id is ") + e.prefix + " and a number (" + e.prefix + "1)" : std::string("a room's id is a name (kitchen, bed2)"));
                once("element", r);
                local[r.id] = r.type;
                scoped(r.id, r.type, r.line);
            }
        }
        // a furnish block's variants: its variant records and items name variants of its list
        for (const Ntw::Record& r : doc.records) {
            if (r.type != "furnish") continue;
            std::vector<std::string> variants;
            Ntw::toList(r.value("variants", ""), variants);
            for (const Ntw::Record& c : r.children) {
                const std::string v = c.type == "variant" ? c.id : c.value("variant", "");
                if (v.empty()) continue;
                if (c.type == "variant") {
                    if (v.size() != 1 || v[0] < 'a' || v[0] > 'z') localForm(c, "a variant's id is a letter (a, b, ...)");
                    once("variant@" + std::to_string(r.line), c);
                }
                res.localRefs++;
                if (!contains(variants, v))
                    error(c.line, "ref.missing", c.type + (c.type == "variant" ? " " + v : " variant=" + v) + ": the furnish block's variants=" + r.value("variants", "") + " has no " + v);
            }
        }
    }

    // A plan's features: when=<option>:<choice> names one of the option's choices (the option itself is a reference)
    void featureChoices() {
        for (const Ntw::Record& r : doc.records) {
            if (r.type != "feature") continue;
            const std::vector<std::string> w = split(r.value("when", ""), ':');
            const auto o = w.size() == 2 ? options.find(w[0]) : options.end();
            if (o == options.end()) continue;   // (the reference check reports it)
            res.localRefs++;
            if (!contains(o->second, w[1]))
                error(r.line, "ref.missing", "feature " + r.id + " when=" + r.value("when", "") + ": option " + w[0] + " has no choice " + w[1]);
        }
    }

    void scanSet() {
        self = header("id");
        libraryHeader("set", 4);
        const std::vector<std::string> p = split(self, '.');
        if (p.size() == 4 && p[2] != header("room")) error(1, "id.form", "a set's family is its room type: room=" + header("room") + " but the id says " + p[2]);
        def(self, "set", 1);
    }

    void scanPalette() {
        self = header("family");
        if (!isPart(self)) error(1, "id.form", "family=" + self + ": a palette family is one part (bungalow)");
        for (const Ntw::Record& r : doc.records) {
            if (r.type != "palette" || r.id.empty()) continue;
            if (!isPart(r.id)) form(r, "a palette's id is one part (coral); it is named lib.palette." + self + ".<id>");
            def("lib.palette." + self + "." + r.id, "palette", r.line);
        }
    }

    void scanPlaces() {
        for (const Ntw::Record& r : doc.records) {
            if (r.id.empty()) continue;
            const std::vector<std::string> p = split(r.id, '.');
            if (r.type == "place") {
                if (p.size() != 2 || p[0] != "place" || !allParts(p)) form(r, "a place's id is place.<name> (place.mari_apartment)");
                def(r.id, "place", r.line);
            } else if (r.type == "anchor") {
                if (p.size() < 2 || !allParts(p)) form(r, "an anchor's id starts with its district's (cl.pd.station)");
                else globalRef(p[0], "district", "anchor " + r.id, false, r.line);   // its district must exist
                def(r.id, "anchor", r.line);
            }
        }
    }

    void scanTransit() {
        self = header("line");
        const std::string kind = header("kind");
        if (!startsWith(self, kind + ".") || !allParts(split(self, '.'))) error(1, "id.form", "line=" + self + ": a line's id is its kind, then its name (" + kind + ".8)");
        def(self, "line", 1);
        for (const Ntw::Record& r : doc.records)
            if (!r.id.empty()) once("stop", r);
    }

    void scanCatalog() {
        for (const Ntw::Record& r : doc.records) {
            if (r.type != "piece" || r.id.empty()) continue;
            const std::vector<std::string> p = split(r.id, '.');
            static const char* const kCategories[] = {"kit", "prop", "veg", "light", "fence"};
            const char* cls = nullptr;
            for (const char* c : kCategories)
                if (p.size() == 3 && p[0] == "lib" && p[1] == c && allParts(p)) cls = c;
            if (!cls) form(r, "a piece's id is lib.<kit|prop|veg|light|fence>.<name> (lib.kit.sofa_3seat)");
            def(r.id, cls ? cls : "piece", r.line);
            // its solid part fits inside it
            if (const Ntw::Field* b = r.field("body")) {
                const auto dims = [](const std::string& t, float out[3]) {
                    const std::vector<std::string> d = split(t, 'x');
                    if (d.size() != 3) return false;
                    for (int i = 0; i < 3; i++) {
                        char* end = nullptr;
                        out[i] = strtof(d[(size_t)i].c_str(), &end);
                        if (d[(size_t)i].empty() || *end || !(out[i] > 0.f)) return false;
                    }
                    return true;
                };
                float bd[3] = {0.f, 0.f, 0.f}, sd[3] = {0.f, 0.f, 0.f};
                const Ntw::Field* s = r.field("size");
                if (!dims(b->value, bd)) error(b->line, "piece.body", "piece " + r.id + " body=" + b->value + ": not WxDxH, each over 0");
                else if (s && dims(s->value, sd) && (bd[0] > sd[0] || bd[1] > sd[1] || bd[2] > sd[2]))
                    error(b->line, "piece.body", "piece " + r.id + " body=" + b->value + ": doesn't fit inside its size=" + s->value);
            }
        }
    }

    void scanLocalIds() {   // terrain and signal files: ids unique per record type
        for (const Ntw::Record& r : doc.records)
            if (!r.id.empty()) once(r.type, r);
    }

    // ------------------------------------------------------------------ references

    void globalRef(const std::string& id, const std::string& cls, const std::string& what, bool whole, int line, const std::string& scope = std::string()) {
        Ref ref;
        ref.id = id;
        ref.cls = cls;
        ref.scope = scope;
        ref.what = what;
        ref.whole = whole;
        ref.line = line;
        res.refs.push_back(ref);
    }

    // The scope of a floor reference that no <building> before it gives
    std::string floorScope(const Ntw::Record* r, const std::string& what, int line) {
        const std::string& k = doc.kind;
        if (k == "plan") return "plan:" + self;
        if (k == "building") return "building:" + self;
        if (k == "layout" || k == "unit" || k == "fitout") return "layout:" + self;
        if (k == "places" && r) {
            if (const char* b = r->value("building")) return std::string("building:") + b;
            error(line, "ref.form", what + ": a floor's element names a building's floor, and the record has no building=");
        }
        return std::string();
    }

    void resolveLocal(const Ntw::RefName& n, const std::string& what, bool whole, int line) {
        res.localRefs++;
        const std::vector<std::string> allowed = split(n.cls, '|');
        const auto it = local.find(n.id);
        const std::string words = classWords(n.cls);
        if (it == local.end()) {
            error(line, "ref.missing", what + ": no " + (whole ? "such " + words : words + " " + n.id) + " in this " + doc.kind);
            return;
        }
        if (!contains(allowed, "." + it->second))
            error(line, "ref.kind", what + ": " + n.id + " is " + article(it->second) + " " + it->second + ", not " + article(words) + " " + words);
    }

    void applyRules(const Ntw::Record* r, const std::string& type, const std::vector<Ntw::Field>* fields, const std::string& id, int idLine) {
        const std::vector<Ntw::RefRule>* rules = Ntw::refRules(doc.kind, type);
        if (!rules) return;
        for (const Ntw::RefRule& rule : *rules) {
            std::string value, what;
            int line = idLine;
            if (!rule.key) {
                if (id.empty()) continue;
                value = id;
                what = type + " " + id;
            } else {
                const Ntw::Field* f = nullptr;
                for (const Ntw::Field& x : *fields)
                    if (x.key == rule.key) f = &x;
                if (!f) continue;
                value = f->value;
                line = f->line;
                what = (r ? type + (id.empty() ? "" : " " + id) : std::string("the header's")) + " " + rule.key + "=" + value;
            }
            std::vector<std::vector<Ntw::RefName>> groups;
            if (!Ntw::matchRefs(rule.pattern, value, groups)) {
                error(line, "ref.form", what + ": the value should have the shape " + rule.pattern.text);
                continue;
            }
            for (const std::vector<Ntw::RefName>& g : groups) {
                std::string building;   // a <building> earlier in the item gives the floor references after it their scope
                for (const Ntw::RefName& n : g) {
                    const bool whole = n.id == value;
                    if (isLocalClass(n.cls)) {
                        resolveLocal(n, what, whole, line);
                    } else if (isFloorClass(n.cls)) {
                        const bool floorOnly = n.cls == "floor";
                        const std::vector<std::string> p = split(n.id, '.');
                        if (floorOnly ? p.size() != 1 : p.size() != 2 || !allParts(p)) {
                            error(line, "ref.form", what + ": " + n.id + (floorOnly ? " should be a floor (f0)" : " should be a floor's element (f0.d1)"));
                            continue;
                        }
                        const std::string scope = building.empty() ? floorScope(r, what, line) : "building:" + building;
                        if (!scope.empty()) globalRef(n.id, n.cls, what, whole, line, scope);
                    } else {
                        globalRef(n.id, n.cls, what, whole, line);
                        if (n.cls == "building") building = n.id;
                    }
                }
            }
        }
    }

    void refsIn(const std::vector<Ntw::Record>& recs) {
        for (const Ntw::Record& r : recs) {
            applyRules(&r, r.type, &r.fields, r.id, r.line);
            refsIn(r.children);
        }
    }

    void run() {
        const std::string& k = doc.kind;
        if (k == "world") scanWorld();
        else if (k == "district") scanDistrict();
        else if (k == "roads") scanRoads();
        else if (k == "signal") {
            self = header("id");
            libraryHeader("signal", 3);
            def(self, "signal", 1);
            scanLocalIds();
        } else if (k == "block") scanBlock();
        else if (k == "building") scanBuilding();
        else if (k == "plan") {
            scanPlan();
            featureChoices();
        }
        else if (k == "layout" || k == "unit" || k == "fitout") scanItem();
        else if (k == "set") scanSet();
        else if (k == "facade") {
            self = header("id");
            libraryHeader("facade", 3);
            def(self, "facade", 1);
        } else if (k == "palette") scanPalette();
        else if (k == "places") scanPlaces();
        else if (k == "transit") scanTransit();
        else if (k == "catalog") scanCatalog();
        else scanLocalIds();
        applyRules(nullptr, "ntw", &doc.header, std::string(), 1);
        refsIn(doc.records);
    }
};

// Where a file of this kind and these ids lives (docs/NTW_FORMAT.md section 8); empty when its ids are too broken to
// say (their own checks report them)
std::string homeOf(const Ntw::Document& doc) {
    const std::string& k = doc.kind;
    const auto hv = [&doc](const char* key) {
        const Ntw::Field* f = doc.headerField(key);
        return f ? f->value : std::string();
    };
    const std::vector<std::string> p = split(hv("id"), '.');
    if (k == "terrain") return "terrain/terrain.ntw";
    if (k == "world") return "world.ntw";
    if (k == "places") return "places/places.ntw";
    if (k == "catalog") return "lib/catalog.ntw";
    if (k == "district") return isDistrictId(hv("district")) ? "districts/" + hv("district") + "/district.ntw" : std::string();
    if (k == "roads") {
        if (hv("scope") == "trunk") return hv("district").empty() ? "trunk/roads.ntw" : std::string();
        return isDistrictId(hv("district")) ? "districts/" + hv("district") + "/roads.ntw" : std::string();
    }
    if (k == "block")
        return isDistrictId(hv("district")) && numbered(hv("id"), hv("district") + ".b", 3) ? "districts/" + p[0] + "/blocks/" + p[1] + "/block.ntw" : std::string();
    if (k == "building") return p.size() == 3 && allParts(p) ? "districts/" + p[0] + "/blocks/" + p[1] + "/" + p[2] + ".ntw" : std::string();
    if (k == "plan" || k == "layout" || k == "unit" || k == "fitout" || k == "set")
        return p.size() == 4 && p[0] == "lib" && p[1] == k && allParts(p) ? "lib/" + k + "s/" + p[2] + "/" + p[3] + ".ntw" : std::string();
    if (k == "facade" || k == "signal") return p.size() == 3 && p[0] == "lib" && p[1] == k && allParts(p) ? "lib/" + k + "s/" + p[2] + ".ntw" : std::string();
    if (k == "palette") return isPart(hv("family")) ? "lib/palettes/" + hv("family") + ".ntw" : std::string();
    if (k == "transit") return Ntw::isId(hv("line")) ? "transit/" + hv("line") + ".ntw" : std::string();
    return std::string();
}

void checkPath(const Ntw::Document& doc, const std::string& rel, FileResult& r) {
    const std::string home = homeOf(doc);
    if (home.empty() || home == rel) return;
    std::string by;
    for (const char* key : {"id", "district", "family", "line"})
        if (const Ntw::Field* f = doc.headerField(key)) {
            by = std::string(" (by its ") + key + "=" + f->value + ")";
            break;
        }
    add(r.diags, 1, SEV_ERROR, "id.path", "this " + doc.kind + " file belongs at " + home + by + ", not at " + rel);
}

void checkCanonical(const std::string& text, const Ntw::Document& doc, FileResult& r) {
    const std::string canon = Ntw::format(doc, Ntw::schemaFormatRules());
    if (canon == text) return;
    const char* const fix = " (run ntwfmt on it)";
    if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        add(r.diags, 1, SEV_ERROR, "file.canonical", std::string("the file starts with a byte-order mark, which the canonical form doesn't have") + fix);
        return;
    }
    const size_t cr = text.find('\r');
    if (cr != std::string::npos) {
        const int line = 1 + (int)std::count(text.begin(), text.begin() + (std::ptrdiff_t)cr, '\n');
        add(r.diags, line, SEV_ERROR, "file.canonical", std::string("CRLF line ends: the canonical form ends lines with LF only (.gitattributes keeps .ntw files LF)") + fix);
        return;
    }
    int line = 1;
    size_t a = 0, b = 0;
    for (;;) {
        const size_t ea = text.find('\n', a), eb = canon.find('\n', b);
        const std::string la = text.substr(a, ea == std::string::npos ? std::string::npos : ea - a);
        const std::string lb = canon.substr(b, eb == std::string::npos ? std::string::npos : eb - b);
        if (la != lb) {
            add(r.diags, line, SEV_ERROR, "file.canonical", "not in canonical form from here: the line reads \"" + clip(la) + "\", the canonical form has \"" + clip(lb) + "\"" + fix);
            return;
        }
        if (ea == std::string::npos || eb == std::string::npos) {
            add(r.diags, line, SEV_ERROR, "file.canonical", std::string(ea == std::string::npos ? "the last line has no line end" : "the file ends early") + fix);
            return;
        }
        a = ea + 1;
        b = eb + 1;
        line++;
    }
}

// A file's kind, from its header line (after any comments and blank lines): enough to pick the files a pass types
std::string headerKind(const std::string& text) {
    size_t at = text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF ? 3 : 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        const size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;
        std::vector<std::string> words;
        size_t p = first;
        while (p < line.size() && words.size() < 3) {
            const size_t q = line.find_first_of(" \t\r", p);
            words.push_back(line.substr(p, q == std::string::npos ? std::string::npos : q - p));
            if (q == std::string::npos) break;
            p = line.find_first_not_of(" \t\r", q);
            if (p == std::string::npos) break;
        }
        return words.size() == 3 && words[0] == "ntw" ? words[2] : std::string();
    }
    return std::string();
}

// The checks of one file on its own. `deep`: all of them; otherwise only what the checks across files need (a file
// outside --only). `inWorld`: the file has a place in a world, so its path is checked too.
void checkFile(File& f, bool deep, bool inWorld) {
    FileResult& r = f.res;
    r = FileResult();
    f.deep = deep;
    Ntw::Document doc;
    std::vector<Ntw::Error> errs;
    if (!Ntw::parse(f.text.data(), f.text.size(), Ntw::schemaOpensBlock, doc, errs)) {
        for (const Ntw::Error& e : errs) add(r.diags, e.line, SEV_ERROR, "file.parse", e.message);
        return;
    }
    r.records = countRecords(doc.records);
    errs.clear();
    Ntw::checkDocument(doc, errs);
    for (const Ntw::Error& e : errs) add(r.diags, e.line, SEV_ERROR, "file.schema", e.message);
    r.valid = errs.empty();
    if (!Ntw::findKind(doc.kind)) return;
    if (!r.valid) {
        // the ids still count (so other files' references to them don't fail too), but nothing else is reported
        // until the schema errors are fixed
        FileResult quiet;
        Scanner s(doc, quiet);
        s.run();
        r.defs = std::move(quiet.defs);
        r.scoped = std::move(quiet.scoped);
        return;
    }
    if (deep) checkCanonical(f.text, doc, r);
    Scanner s(doc, r);
    s.run();
    if (inWorld) checkPath(doc, f.rel, r);
    if (deep && (doc.kind == "layout" || doc.kind == "unit" || doc.kind == "fitout")) {
        bool noWayIn = false;
        checkLayout(doc, r.diags, &noWayIn);
        // nothing leads in: in a world, a plan may reach a layout's floor through a hatch or another floor's stair or
        // lift (the checks across files decide); a unit or fit-out has only its own doors
        if (noWayIn && inWorld && doc.kind == "layout") s.scoped("#noway", "noway", 1);
        else if (noWayIn) add(r.diags, 1, SEV_WARNING, "room.reach", "nothing leads into this " + doc.kind + ": no door out, no stair and no lift");
    }
    if (deep && fileModelKind(doc.kind)) {
        // the file's records typed on their own, for the checks of roads, signal plans and transit that read one file
        // (validate_roads.h); its references to other files stay unresolved there
        std::vector<Hand::File> one(1);
        one[0].path = f.path;
        one[0].doc = std::move(doc);   // (the checks above are done with it)
        one[0].valid = true;
        Hand::Model m;
        std::vector<Hand::Error> loading;
        Hand::loadParsed(std::move(one), m, loading);
        std::vector<ModelFinding> found;
        checkFileModel(m, loading, found);
        for (const ModelFinding& x : found) add(r.diags, x.src.line, x.severity, x.check, x.message);
    }
}

// ---------------------------------------------------------------------------------------------------------- the cache

// One file per entry, keyed by its path in the world and the hash of its content: its own diagnostics and what it says
// about the world. Text, one fact per line, a field with spaces last:
//   worldv-cache 1 <rules>
//   file <hash> <records> <local refs> <valid> <path>
//   d <line> <e|w> <check> <message>
//   i|s <line> <class> <link or -> <id>                       (a global id; a scoped one)
//   r <line> <classes> <scope or -> <whole> <id> <what>
//   end <hash of all the lines before>
struct CacheEntry {
    u64 hash = 0;
    FileResult res;
};
typedef std::unordered_map<std::string, CacheEntry> Cache;

u64 rulesHash() { return hashBytes(std::string(kRulesVersion) + "|" + kBuildStamp); }

std::string token(const std::string& line, size_t& pos) {
    const size_t start = pos;
    while (pos < line.size() && line[pos] != ' ') pos++;
    const std::string t = line.substr(start, pos - start);
    if (pos < line.size()) pos++;
    return t;
}

bool toLine(const std::string& t, int* out) {
    long long v = 0;
    if (!Ntw::toInt(t, &v) || v < 0 || v > 100000000) return false;
    *out = (int)v;
    return true;
}

// False (and nothing loaded) unless the whole cache is there, intact, and written by this validator
bool loadCache(const std::string& path, Cache& out) {
    std::string text;
    if (!readFile(path, text)) return false;
    // the last line is "end <hash of everything before it>": a cut or damaged cache is dropped whole
    const size_t endAt = text.rfind("end ", text.size() >= 2 ? text.size() - 2 : 0);
    if (endAt == std::string::npos || (endAt > 0 && text[endAt - 1] != '\n') || text.back() != '\n') return false;
    char sum[32];
    snprintf(sum, sizeof(sum), "end %016llx\n", (unsigned long long)hashBytes(text.substr(0, endAt)));
    if (text.compare(endAt, std::string::npos, sum) != 0) return false;
    text.resize(endAt);
    char head[64];
    snprintf(head, sizeof(head), "worldv-cache 1 %016llx", (unsigned long long)rulesHash());
    Cache cache;
    CacheEntry* e = nullptr;
    size_t at = 0;
    bool first = true;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        if (first) {
            if (line != head) return false;
            first = false;
            continue;
        }
        size_t pos = 0;
        const std::string tag = token(line, pos);
        if (tag == "file") {
            const std::string h = token(line, pos), recs = token(line, pos), lrefs = token(line, pos), valid = token(line, pos);
            const std::string rel = line.substr(pos);
            long long a = 0, b = 0;
            if (h.size() != 16 || !Ntw::toInt(recs, &a) || !Ntw::toInt(lrefs, &b) || (valid != "0" && valid != "1") || rel.empty()) return false;
            e = &cache[rel];
            *e = CacheEntry();
            e->hash = strtoull(h.c_str(), nullptr, 16);
            e->res.records = (size_t)a;
            e->res.localRefs = (size_t)b;
            e->res.valid = valid == "1";
            continue;
        }
        if (!e) return false;
        int ln = 0;
        if (!toLine(token(line, pos), &ln)) return false;
        if (tag == "d") {
            Diagnostic d;
            d.line = ln;
            const std::string sev = token(line, pos);
            if (sev != "e" && sev != "w") return false;
            d.severity = sev == "e" ? SEV_ERROR : SEV_WARNING;
            d.check = token(line, pos);
            d.message = line.substr(pos);
            e->res.diags.push_back(d);
        } else if (tag == "i" || tag == "s") {
            Def d;
            d.line = ln;
            d.cls = token(line, pos);
            d.link = token(line, pos);
            if (d.link == "-") d.link.clear();
            d.id = line.substr(pos);
            (tag == "i" ? e->res.defs : e->res.scoped).push_back(d);
        } else if (tag == "r") {
            Ref r;
            r.line = ln;
            r.cls = token(line, pos);
            r.scope = token(line, pos);
            if (r.scope == "-") r.scope.clear();
            r.whole = token(line, pos) == "1";
            r.id = token(line, pos);
            r.what = line.substr(pos);
            e->res.refs.push_back(r);
        } else {
            return false;
        }
    }
    if (first) return false;
    out.swap(cache);
    return true;
}

std::string oneLine(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    return s;
}

bool saveCache(const std::string& path, const std::vector<File>& files) {
    std::string out;
    char line[128];
    snprintf(line, sizeof(line), "worldv-cache 1 %016llx\n", (unsigned long long)rulesHash());
    out += line;
    for (const File& f : files) {
        if (!f.readOk || !f.deep) continue;
        const FileResult& r = f.res;
        snprintf(line, sizeof(line), "file %016llx %zu %zu %d ", (unsigned long long)f.hash, r.records, r.localRefs, r.valid ? 1 : 0);
        out += line + f.rel + "\n";
        for (const Diagnostic& d : r.diags)
            out += "d " + std::to_string(d.line) + (d.severity == SEV_ERROR ? " e " : " w ") + d.check + " " + oneLine(d.message) + "\n";
        for (int s = 0; s < 2; s++)
            for (const Def& d : s ? r.scoped : r.defs)
                out += std::string(s ? "s " : "i ") + std::to_string(d.line) + " " + d.cls + " " + (d.link.empty() ? "-" : d.link) + " " + d.id + "\n";
        for (const Ref& x : r.refs)
            out += "r " + std::to_string(x.line) + " " + x.cls + " " + (x.scope.empty() ? "-" : x.scope) + (x.whole ? " 1 " : " 0 ") + x.id + " " + oneLine(x.what) + "\n";
    }
    char end[32];
    snprintf(end, sizeof(end), "end %016llx\n", (unsigned long long)hashBytes(out));
    out += end;
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir, ec);
    // a temp file beside it, renamed over it: a run that reads it meanwhile sees the old cache or the new one
    const std::string tmp = path + ".tmp" + std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    const bool ok = fwrite(out.data(), 1, out.size(), f) == out.size();
    if (fclose(f) != 0 || !ok) {
        remove(tmp.c_str());
        return false;
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {   // (where a rename doesn't replace a file)
        remove(path.c_str());
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec) remove(tmp.c_str());
    return !ec;
}

// ------------------------------------------------------------------------------------------- the checks across files

struct Index {
    struct At {
        const Def* def;
        size_t file;
    };
    std::map<std::string, At> ids;                       // global ids (ordered: families are prefixes)
    std::unordered_map<std::string, At> scoped;
    std::unordered_map<std::string, std::vector<const Def*>> layoutUsers;   // a layout -> the plan floors built from it
};

struct WorldChecks {
    std::vector<File>& files;
    Index index;
    size_t refs = 0;
    std::set<std::string> reached;   // "<plan>/<floor>": floors that another floor's stairs or lifts name
    RoadCounts counts;               // the roads pass's summary
    bool roadsChecked = false;       // the world has roads, and the roads pass ran

    explicit WorldChecks(std::vector<File>& f) : files(f) {}

    void error(size_t file, int line, const char* check, const std::string& msg) { add(files[file].world, line, SEV_ERROR, check, msg); }
    std::string where(const Index::At& at) const { return files[at.file].path + ":" + std::to_string(at.def->line); }

    // (again after a file's results change: the index points into them)
    void build() {
        index = Index();
        for (File& f : files) f.world.clear();
        for (size_t i = 0; i < files.size(); i++) {
            for (const Def& d : files[i].res.defs) {
                const auto it = index.ids.find(d.id);
                if (it == index.ids.end()) index.ids[d.id] = {&d, i};
                else if (it->second.file != i) error(i, d.line, "id.unique", d.cls + " " + d.id + " is already defined at " + where(it->second));
            }
            for (const Def& d : files[i].res.scoped) {
                index.scoped.insert({d.id, {&d, i}});   // (an item defined twice: its id's check reports it)
                if (d.cls == "floor") index.layoutUsers[d.link].push_back(&d);
            }
        }
    }

    const Def* find(const std::string& id, const char* cls) const {
        const auto it = index.ids.find(id);
        return it != index.ids.end() && it->second.def->cls == cls ? it->second.def : nullptr;
    }

    void global(const Ref& r, size_t file) {
        const std::vector<std::string> allowed = split(r.cls, '|');
        const std::string words = classWords(r.cls);
        if (endsWith(r.id, ".*")) {
            const std::string prefix = r.id.substr(0, r.id.size() - 1);
            for (auto it = index.ids.lower_bound(prefix); it != index.ids.end() && startsWith(it->first, prefix); ++it)
                if (contains(allowed, it->second.def->cls)) return;
            error(file, r.line, "ref.missing", r.what + ": no " + words + " in the family " + r.id);
            return;
        }
        const auto it = index.ids.find(r.id);
        if (it == index.ids.end()) {
            error(file, r.line, "ref.missing", r.what + ": no " + (r.whole ? "such " + words : words + " " + r.id));
            return;
        }
        const Def& d = *it->second.def;
        if (!contains(allowed, d.cls))
            error(file, r.line, "ref.kind", r.what + ": " + (r.whole ? std::string("that") : r.id) + " is " + article(d.cls) + " " + d.cls + " (" + where(it->second) +
                                                "), not " + article(words) + " " + words);
    }

    // A floor reference, in one plan. `context` names the plan for the messages: "plan P", "building B's plan P", "plan P
    // (its floor f0 uses this layout)"
    bool inPlan(const Ref& r, size_t file, const std::string& plan, const std::string& context) {
        if (!find(plan, "plan")) return false;   // (the reference to the plan reports it)
        const size_t dot = r.id.find('.');
        const std::string floor = r.id.substr(0, dot), element = dot == std::string::npos ? std::string() : r.id.substr(dot + 1);
        const auto fl = index.scoped.find(plan + "/" + floor);
        if (fl == index.scoped.end()) {
            error(file, r.line, "ref.missing", r.what + ": " + context + " has no floor " + floor);
            return false;
        }
        if (element.empty()) return true;
        const std::string& layout = fl->second.def->link;
        if (!find(layout, "layout")) return false;   // (the plan's floor reports it)
        const auto el = index.scoped.find(layout + "/" + element);
        const std::string words = classWords(r.cls);
        const std::string at = r.what + ": " + context + ": floor " + floor + " is layout " + layout;
        if (el == index.scoped.end()) {
            error(file, r.line, "ref.missing", at + ", which has no " + words + " " + element);
            return false;
        }
        const std::string& type = el->second.def->cls;
        if (!contains(split(r.cls, '|'), "f." + type)) {
            error(file, r.line, "ref.kind", at + ", where " + element + " is " + article(type) + " " + type + " (" + where(el->second) + "), not " + article(words) + " " + words);
            return false;
        }
        return true;
    }

    void floorRef(const Ref& r, size_t file) {
        const size_t colon = r.scope.find(':');
        const std::string kind = r.scope.substr(0, colon), who = r.scope.substr(colon + 1);
        if (kind == "plan") {
            inPlan(r, file, who, "plan " + who);
        } else if (kind == "building") {
            const Def* b = find(who, "building");
            if (!b) return;   // (the reference to the building reports it)
            const bool own = files[file].res.defs.size() > 0 && files[file].res.defs.front().id == who;
            if (b->link.empty()) error(file, r.line, "ref.missing", r.what + ": building " + who + " has no plan (no place record)");
            else inPlan(r, file, b->link, (own ? std::string() : "building " + who + "'s ") + "plan " + b->link);
        } else if (kind == "layout") {
            const auto users = index.layoutUsers.find(who);
            if (users == index.layoutUsers.end()) return;   // no plan uses it yet: nothing to resolve against
            std::vector<std::string> plans;
            for (const Def* d : users->second) {
                const size_t slash = d->id.find('/');
                const std::string plan = d->id.substr(0, slash);
                if (contains(plans, plan)) continue;
                plans.push_back(plan);
                // a layout's stairs and lifts name the floors they reach: those floors have a way in
                if (inPlan(r, file, plan, "plan " + plan + " (its floor " + d->id.substr(slash + 1) + " uses this layout)"))
                    reached.insert(plan + "/" + r.id.substr(0, r.id.find('.')));
            }
        }
    }

    void resolve() {
        for (size_t i = 0; i < files.size(); i++)
            for (const Ref& r : files[i].res.refs) {
                refs++;
                if (r.scope.empty()) global(r, i);
                else floorRef(r, i);
            }
        planPass();   // (first: relative stairs it resolves reach floors too)
        wayIn();
        roads();
    }

    // The road network's checks read the world model (validate_roads.h): the world, district, roads, signal and transit
    // files parsed again from the text this run read. The other files keep their places in it, untyped, so its file
    // indices are this run's.
    void roads() {
        std::vector<Hand::File> mf(files.size());
        bool any = false;
        for (size_t i = 0; i < files.size(); i++) {
            mf[i].path = files[i].path;
            if (!files[i].readOk || !files[i].res.valid || !worldModelKind(headerKind(files[i].text))) continue;
            std::vector<Ntw::Error> errs;
            mf[i].valid = Ntw::parse(files[i].text.data(), files[i].text.size(), Ntw::schemaOpensBlock, mf[i].doc, errs);
            any = any || (mf[i].valid && mf[i].doc.kind == "roads");
        }
        if (!any) return;   // (no roads: nothing to check, and no summary line)
        Hand::Model m;
        std::vector<Hand::Error> loading;   // (value.form and ref.form: each file's own checks report them)
        Hand::loadParsed(std::move(mf), m, loading);
        std::vector<ModelFinding> found;
        checkWorldModel(m, found, counts);
        roadsChecked = true;
        for (const ModelFinding& x : found)
            if (x.src.file >= 0 && x.src.file < (int)files.size()) add(files[(size_t)x.src.file].world, x.src.line, x.severity, x.check, x.message);
    }

    // The plan pass reads documents, not facts: every plan with the layouts of its floors, parsed again from the text
    // this run read (each once)
    void planPass() {
        std::map<size_t, std::unique_ptr<Ntw::Document>> docs;
        const auto docOf = [this, &docs](size_t i) -> const Ntw::Document* {
            std::unique_ptr<Ntw::Document>& d = docs[i];
            if (!d && files[i].readOk && files[i].res.valid) {
                d.reset(new Ntw::Document());
                std::vector<Ntw::Error> errs;
                if (!Ntw::parse(files[i].text.data(), files[i].text.size(), Ntw::schemaOpensBlock, *d, errs)) d.reset();
            }
            return d.get();
        };
        std::set<std::string> said;   // (a layout several floors or plans use reports a finding once)
        for (size_t i = 0; i < files.size(); i++) {
            const FileResult& r = files[i].res;
            if (!r.valid || r.defs.empty() || r.defs.front().cls != "plan") continue;
            const Ntw::Document* plan = docOf(i);
            if (!plan) continue;
            std::vector<PlanFloor> floors;
            for (const Ntw::Record& rec : plan->records) {
                if (rec.type != "floor" || rec.id.empty()) continue;
                PlanFloor f;
                f.rec = &rec;
                const auto it = index.ids.find(rec.value("layout", ""));
                if (it != index.ids.end() && it->second.def->cls == "layout") {
                    f.layout = docOf(it->second.file);
                    f.layoutKey = it->second.file;
                }
                floors.push_back(f);
            }
            std::vector<PlanFinding> found;
            std::vector<std::string> into;
            checkPlanFloors(r.defs.front().id, floors, found, &into);
            for (const std::string& fl : into) reached.insert(r.defs.front().id + "/" + fl);
            for (const PlanFinding& p : found) {
                const size_t to = p.inPlan ? i : p.layoutKey;
                if (!said.insert(std::to_string(to) + ":" + std::to_string(p.d.line) + ":" + p.d.check + ":" + p.d.message).second) continue;
                add(files[to].world, p.d.line, p.d.severity, p.d.check.c_str(), p.d.message);
            }
        }
    }

    // A layout that nothing leads into (no door out, no stair, no lift) is fine where every plan floor built from it
    // is reached another way: a hatch (access=), or a stair or lift of another floor that names it
    void wayIn() {
        std::set<std::string> hatches;
        for (const File& f : files)
            for (const Def& d : f.res.scoped)
                if (d.cls == "access") hatches.insert(d.id.substr(0, d.id.size() - 7));   // ("#access")
        for (size_t i = 0; i < files.size(); i++)
            for (const Def& d : files[i].res.scoped) {
                if (d.cls != "noway") continue;
                const std::string layout = d.id.substr(0, d.id.find('/'));
                const auto users = index.layoutUsers.find(layout);
                std::string lost;
                if (users == index.layoutUsers.end()) lost = "and no plan builds a floor from it";
                else
                    for (const Def* u : users->second)
                        if (!hatches.count(u->id) && !reached.count(u->id) && lost.empty())
                            lost = "and floor " + u->id.substr(u->id.find('/') + 1) + " of plan " + u->id.substr(0, u->id.find('/')) +
                                   " has no hatch (access=) and no stair or lift from another floor";
                if (!lost.empty())
                    add(files[i].world, d.line, SEV_WARNING, "room.reach", "nothing leads into this layout: no door out, no stair and no lift, " + lost);
            }
    }
};

template <typename Fn>
void forEach(size_t n, int jobs, const Fn& fn) {
    if (jobs <= 1 || n < 2) {
        for (size_t i = 0; i < n; i++) fn(i);
        return;
    }
    std::atomic<size_t> next(0);
    std::vector<std::thread> pool;
    const size_t threads = std::min((size_t)jobs, n);
    for (size_t t = 0; t < threads; t++)
        pool.emplace_back([&next, n, &fn] {
            for (size_t i = next++; i < n; i = next++) fn(i);
        });
    for (std::thread& th : pool) th.join();
}

std::string normalPath(const std::string& p) {
    std::string s = std::filesystem::path(p).lexically_normal().generic_string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

// The same, made absolute: for comparing paths given one way with paths found another
std::string absolutePath(const std::string& p) {
    std::error_code ec;
    const std::filesystem::path a = std::filesystem::absolute(p, ec);
    return normalPath(ec ? p : a.generic_string());
}

// Every .ntw file under `dir`, sorted, as worldc finds them: the world model's discover(), its errors as diagnostics
void listTree(const std::string& dir, std::vector<std::string>& out, std::vector<Diagnostic>& problems) {
    std::vector<Hand::Error> errors;
    const std::vector<std::string> found = Hand::discover(dir, errors);
    for (const Hand::Error& e : errors) {
        Diagnostic d;
        d.file = dir;
        d.check = e.code;
        d.message = startsWith(e.message, dir + ": ") ? e.message.substr(dir.size() + 2) : e.message;
        problems.push_back(d);
    }
    out.insert(out.end(), found.begin(), found.end());
}

bool lessDiag(const Diagnostic& a, const Diagnostic& b) {
    if (a.file != b.file) return a.file < b.file;
    if (a.line != b.line) return a.line < b.line;
    if (a.check != b.check) return a.check < b.check;
    return a.message < b.message;
}

}  // namespace validate_detail

const std::vector<CheckInfo>& checkList() { return validate_detail::kChecks; }

void run(const Options& opt, std::vector<Diagnostic>& out, Summary& summary) {
    using namespace validate_detail;
    const auto t0 = std::chrono::steady_clock::now();
    out.clear();
    summary = Summary();
    const bool inWorld = opt.files.empty();
    summary.world = inWorld;
    std::vector<Diagnostic> problems;
    std::vector<std::string> paths;
    std::string world = inWorld ? normalPath(opt.world) : std::string();
    if (inWorld) {
        std::error_code ec;
        if (!std::filesystem::is_directory(world, ec)) {
            Diagnostic d;
            d.file = opt.world;
            d.check = "file.read";
            d.message = "no world directory here";
            problems.push_back(d);
        } else {
            listTree(world, paths, problems);
        }
    } else {
        for (const std::string& f : opt.files) {
            std::error_code ec;
            if (std::filesystem::is_directory(f, ec)) listTree(f, paths, problems);
            else paths.push_back(f);
        }
    }
    std::vector<File> files(paths.size());
    for (size_t i = 0; i < paths.size(); i++) {
        files[i].path = paths[i];
        files[i].rel = inWorld && startsWith(paths[i], world + "/") ? paths[i].substr(world.size() + 1) : paths[i];
    }

    // --only: a file or a directory, as given or in the world directory; otherwise an id, found once the ids are known
    std::vector<std::string> onlyPaths;
    bool onlyIsPath = false;
    if (!opt.only.empty()) {
        onlyPaths.push_back(absolutePath(opt.only));
        if (inWorld) onlyPaths.push_back(absolutePath(world + "/" + opt.only));
        for (File& f : files) {
            const std::string p = absolutePath(f.path);
            f.selected = false;
            for (const std::string& o : onlyPaths) f.selected = f.selected || p == o || startsWith(p, o + "/");
            onlyIsPath = onlyIsPath || f.selected;
        }
        for (const std::string& o : onlyPaths) {   // (a directory with nothing to check in it is still a path)
            std::error_code ec;
            onlyIsPath = onlyIsPath || std::filesystem::exists(o, ec);
        }
    }

    Cache cache;
    if (inWorld && opt.incremental && !opt.cache.empty()) loadCache(opt.cache, cache);

    // the static tables, built once before the threads share them
    Ntw::schemaKinds();
    Ntw::schemaFormatRules().fieldOrder("terrain", "grid");
    Ntw::refRules("roads", "edge");

    forEach(files.size(), opt.jobs, [&](size_t i) {
        File& f = files[i];
        if (!readFile(f.path, f.text)) {
            add(f.res.diags, 0, SEV_ERROR, "file.read", "can't read it");
            return;
        }
        f.readOk = true;
        f.hash = hashBytes(f.text);
        const auto c = cache.find(f.rel);
        if (c != cache.end() && c->second.hash == f.hash) {
            f.res = c->second.res;
            f.cached = f.deep = true;
            return;
        }
        checkFile(f, f.selected, inWorld);
    });

    WorldChecks wc(files);
    if (inWorld) wc.build();

    if (!opt.only.empty() && !onlyIsPath) {   // an id: the file that defines it (the first, if two do)
        size_t at = files.size();
        for (size_t i = 0; i < files.size() && at == files.size(); i++)
            for (const Def& d : files[i].res.defs)
                if (d.id == opt.only) at = i;
        if (at < files.size()) {
            File& f = files[at];
            f.selected = true;
            if (!f.deep) {
                checkFile(f, true, inWorld);
                if (inWorld) wc.build();
            }
        } else {
            summary.onlyMatched = false;
        }
    }
    if (inWorld) wc.resolve();

    for (File& f : files) {
        summary.files++;
        summary.records += f.res.records;
        summary.refs += f.res.localRefs;
        if (f.cached) summary.cached++;
        else if (f.deep) summary.checked++;
        if (!f.selected) continue;
        summary.reported++;
        for (int part = 0; part < 2; part++)
            for (const Diagnostic& d : part ? f.world : f.res.diags) {
                out.push_back(d);
                out.back().file = f.path;
            }
    }
    summary.ids = wc.index.ids.size();
    summary.refs += wc.refs;
    if (wc.roadsChecked) summary.roads = formatRoadCounts(wc.counts);
    out.insert(out.end(), problems.begin(), problems.end());
    std::sort(out.begin(), out.end(), lessDiag);
    for (const Diagnostic& d : out) (d.severity == SEV_ERROR ? summary.errors : summary.warnings)++;
    // the cache is rewritten unless it already holds exactly these files' results
    bool same = cache.size() == files.size();
    for (const File& f : files) same = same && f.cached;
    if (inWorld && !opt.cache.empty() && !same && !saveCache(opt.cache, files)) summary.cacheWritten = false;
    summary.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

void checkText(const std::string& name, const std::string& text, std::vector<Diagnostic>& out) {
    using namespace validate_detail;
    File f;
    f.path = f.rel = name;
    f.text = text;
    checkFile(f, true, false);
    out = f.res.diags;
    for (Diagnostic& d : out) d.file = name;
    std::sort(out.begin(), out.end(), lessDiag);
}

std::string formatDiagnostic(const Diagnostic& d) {
    std::string s = d.file;
    if (d.line > 0) s += ":" + std::to_string(d.line);
    s += d.severity == SEV_ERROR ? ": error: " : ": warning: ";
    return s + d.message + " [" + d.check + "]";
}

std::string formatSummary(const Summary& s) {
    char buf[512];
    if (s.world)
        snprintf(buf, sizeof(buf), "worldv: %zu file%s (%zu checked, %zu from the cache), %zu records, %zu ids, %zu references", s.files, s.files == 1 ? "" : "s", s.checked,
                 s.cached, s.records, s.ids, s.refs);
    else
        snprintf(buf, sizeof(buf), "worldv: %zu file%s, each on its own, %zu records, %zu references", s.files, s.files == 1 ? "" : "s", s.records, s.refs);
    std::string out = buf;
    if (s.reported != s.files) out += ", reporting on " + std::to_string(s.reported);
    snprintf(buf, sizeof(buf), ": %d error%s, %d warning%s (%.2f s)", s.errors, s.errors == 1 ? "" : "s", s.warnings, s.warnings == 1 ? "" : "s", s.seconds);
    return out + buf;
}

}  // namespace Validate
}  // namespace World
