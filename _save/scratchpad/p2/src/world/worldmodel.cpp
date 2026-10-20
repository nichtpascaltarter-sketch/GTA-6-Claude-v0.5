// The world model's loader (worldmodel.h): discovery, typing the records, the ids and the references.
#include "worldmodel.h"
#include "ntwschema.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace World {
namespace Hand {

namespace worldmodel_detail {

std::vector<std::string> splitAt(const std::string& s, char sep) {
    std::vector<std::string> parts;
    size_t from = 0;
    for (size_t i = 0; i <= s.size(); i++)
        if (i == s.size() || s[i] == sep) {
            parts.push_back(s.substr(from, i - from));
            from = i + 1;
        }
    return parts;
}

bool readText(const std::string& path, std::string& out) {
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

bool toFloat(const std::string& v, float* out) {
    double d = 0;
    if (!Ntw::toNumber(v, &d)) return false;
    *out = (float)d;
    return true;
}

// A district file's records, kept until the districts are known (world.ntw sorts after the districts/ directory)
struct DistrictPart {
    int file = -1;
    Ref district;
    std::vector<vec2> bounds;
    bool hasLabel = false;
    vec2 label = vec2(0.f);
    std::string labelText;
    std::vector<Zone> zones;
    std::vector<Ref> trunks;
};

// A list item of a field that names records (or the whole value), split at ':', with the first reference in it
struct Item {
    std::vector<std::string> parts;
    Ref ref;
};

struct Loader {
    Model& m;
    std::vector<Error>& errors;
    std::vector<DistrictPart> districtParts;
    int file = -1;
    const Ntw::Document* doc = nullptr;

    Loader(Model& model, std::vector<Error>& errs) : m(model), errors(errs) {}

    Src at(int line) const { return Src{file, line}; }
    void fail(int line, const char* code, const std::string& msg) { errors.push_back(Error{at(line), code, msg}); }

    void define(const std::string& id, const char* cls, int index) {
        if (!id.empty()) m.ids.insert(std::make_pair(id, Def{cls, index}));   // (the first definition stays)
    }

    // ---------------------------------------------------------------------------------------- plain fields

    std::string text(const Ntw::Record& r, const char* key, const char* fallback = "") const { return r.value(key, fallback); }
    float num(const Ntw::Record& r, const char* key, float fallback = 0.f) const {   // (the schema checked the type)
        float v = fallback;
        if (const Ntw::Field* f = r.field(key)) toFloat(f->value, &v);
        return v;
    }
    int integer(const Ntw::Record& r, const char* key, int fallback = 0) const {
        long long v = fallback;
        if (const Ntw::Field* f = r.field(key)) Ntw::toInt(f->value, &v);
        return (int)v;
    }
    vec2 point(const Ntw::Record& r, const char* key) const {
        vec2 p(0.f);
        if (const Ntw::Field* f = r.field(key)) Ntw::toPoint(f->value, &p);
        return p;
    }
    vec3 point3(const Ntw::Record& r, const char* key) const {
        vec3 p(0.f);
        if (const Ntw::Field* f = r.field(key)) Ntw::toPoint3(f->value, &p);
        return p;
    }
    std::vector<vec2> points(const Ntw::Record& r, const char* key) const {
        std::vector<vec2> pts;
        if (const Ntw::Field* f = r.field(key)) Ntw::toPoints(f->value, pts);
        return pts;
    }
    std::vector<std::string> list(const Ntw::Record& r, const char* key) const {
        std::vector<std::string> items;
        if (const Ntw::Field* f = r.field(key)) Ntw::toList(f->value, items);
        return items;
    }
    Side side(const Ntw::Record& r, const char* key, Side fallback) const {
        const std::string v = r.value(key, "");
        return v == "left" ? SIDE_LEFT : v == "right" ? SIDE_RIGHT : v == "both" ? SIDE_BOTH : fallback;
    }
    Dir dir(const Ntw::Record& r) const { return std::string(r.value("dir", "fwd")) == "bwd" ? DIR_BWD : DIR_FWD; }

    // ------------------------------------------------------------------------------------- compound values

    void badValue(const Ntw::Field& f, const std::string& what, const std::string& shape) {
        fail(f.line, "value.form", what + " " + f.key + "=" + f.value + ": not " + shape);
    }

    // number or `end` (distances along an edge)
    EdgeAt edgeAt(const Ntw::Record& r, const char* key, bool endByDefault, const std::string& what) {
        EdgeAt e;
        const Ntw::Field* f = r.field(key);
        if (!f) {
            e.toEnd = endByDefault;
            return e;
        }
        if (f->value == "end") e.toEnd = true;
        else if (!toFloat(f->value, &e.s)) badValue(*f, what, "a distance or end");
        return e;
    }

    // "s:z;s:z..."
    std::vector<vec2> pairs(const Ntw::Record& r, const char* key, const std::string& what) {
        std::vector<vec2> out;
        const Ntw::Field* f = r.field(key);
        if (!f) return out;
        for (const std::string& item : splitAt(f->value, ';')) {
            const std::vector<std::string> p = splitAt(item, ':');
            vec2 v(0.f);
            if (p.size() != 2 || !toFloat(p[0], &v.x) || !toFloat(p[1], &v.y)) {
                badValue(*f, what, "s:z;s:z...");
                out.clear();
                return out;
            }
            out.push_back(v);
        }
        return out;
    }

    // a kerb compound: none, left:W, right:W or both:W, for both sides (they stay -1 when the field isn't written)
    void kerb(const Ntw::Record& r, const char* key, float* left, float* right, const std::string& what) {
        const Ntw::Field* f = r.field(key);
        if (!f) return;
        if (f->value == "none") {
            *left = *right = 0.f;
            return;
        }
        const std::vector<std::string> p = splitAt(f->value, ':');
        float w = 0.f;
        if (p.size() != 2 || (p[0] != "left" && p[0] != "right" && p[0] != "both") || !toFloat(p[1], &w) || w < 0.f) {
            badValue(*f, what, "none, left:W, right:W or both:W");
            return;
        }
        *left = p[0] != "right" ? w : 0.f;   // (the side it doesn't name has none)
        *right = p[0] != "left" ? w : 0.f;
    }

    // numbers in a list: 24 by hour (zones' peds and traffic)
    std::vector<float> hours24(const Ntw::Record& r, const char* key, const std::string& what) {
        std::vector<float> out;
        const Ntw::Field* f = r.field(key);
        if (!f) return out;
        std::vector<std::string> items;
        bool ok = Ntw::toList(f->value, items) && items.size() == 24;
        for (size_t i = 0; ok && i < items.size(); i++) {
            float v = 0.f;
            ok = toFloat(items[i], &v) && v >= 0.f;
            out.push_back(v);
        }
        if (!ok) {
            badValue(*f, what, "24 factors, one per hour");
            out.clear();
        }
        return out;
    }

    // ---------------------------------------------------------------------- references, by the schema's table

    // The references in a value (a field's, or a record's own id with key nullptr), one group per list item; false when
    // the schema has no rule for it, or (ref.form) when the value has none of the rule's shapes
    bool named(const std::string& type, const char* key, const std::string& value, int line, const std::string& what,
               std::vector<std::vector<Ntw::RefName>>& out) {
        const std::vector<Ntw::RefRule>* rules = Ntw::refRules(doc->kind, type);
        const Ntw::RefRule* rule = nullptr;
        for (size_t i = 0; rules && i < rules->size() && !rule; i++) {
            const Ntw::RefRule& x = (*rules)[i];
            if (key ? x.key && strcmp(x.key, key) == 0 : !x.key) rule = &x;
        }
        if (!rule) return false;
        if (!Ntw::matchRefs(rule->pattern, value, out)) {
            fail(line, "ref.form", what + (key ? std::string(" ") + key + "=" + value : std::string()) + ": the value should have the shape " + rule->pattern.text);
            return false;
        }
        return true;
    }

    Ref makeRef(const Ntw::RefName& n, int line) const {
        Ref x;
        x.text = n.id;
        x.classes = n.cls;
        x.src = at(line);
        return x;
    }

    static std::string recordName(const Ntw::Record& r) { return r.id.empty() ? r.type : r.type + " " + r.id; }

    // a field that names one record (or none: auto)
    Ref ref(const Ntw::Record& r, const char* key) {
        const Ntw::Field* f = r.field(key);
        std::vector<std::vector<Ntw::RefName>> g;
        if (!f || !named(r.type, key, f->value, f->line, recordName(r), g) || g.empty() || g[0].empty()) return Ref();
        return makeRef(g[0][0], f->line);
    }
    // a header field that names one record
    Ref headerRef(const char* key) {
        const Ntw::Field* f = doc->headerField(key);
        std::vector<std::vector<Ntw::RefName>> g;
        if (!f || !named("ntw", key, f->value, f->line, "the header's", g) || g.empty() || g[0].empty()) return Ref();
        return makeRef(g[0][0], f->line);
    }
    // a record whose own id names another (a junction's node)
    Ref idRef(const Ntw::Record& r) {
        std::vector<std::vector<Ntw::RefName>> g;
        if (r.id.empty() || !named(r.type, nullptr, r.id, r.line, recordName(r), g) || g.empty() || g[0].empty()) return Ref();
        return makeRef(g[0][0], r.line);
    }
    // a field that names a list of records
    std::vector<Ref> refs(const Ntw::Record& r, const char* key) {
        std::vector<Ref> out;
        const Ntw::Field* f = r.field(key);
        std::vector<std::vector<Ntw::RefName>> g;
        if (!f || !named(r.type, key, f->value, f->line, recordName(r), g)) return out;
        for (const auto& group : g)
            for (const Ntw::RefName& n : group) out.push_back(makeRef(n, f->line));
        return out;
    }
    // a field whose value, or each of its list's items, holds other parts besides the reference (<edge>:<lane>,
    // *:<edge>,...): the parts split at ':'
    std::vector<Item> items(const Ntw::Record& r, const char* key) {
        std::vector<Item> out;
        const Ntw::Field* f = r.field(key);
        std::vector<std::vector<Ntw::RefName>> g;
        if (!f || !named(r.type, key, f->value, f->line, recordName(r), g)) return out;
        std::vector<std::string> texts;
        if (g.size() == 1) texts.assign(1, f->value);   // (one value, or a list of one: the same text)
        else Ntw::toList(f->value, texts);
        for (size_t i = 0; i < g.size() && i < texts.size(); i++) {
            Item it;
            it.parts = splitAt(texts[i], ':');
            if (!g[i].empty()) it.ref = makeRef(g[i][0], f->line);
            out.push_back(it);
        }
        return out;
    }

    // ------------------------------------------------------------------------------------------------ the kinds

    void world() {
        for (const Ntw::Record& r : doc->records) {
            if (r.type == "world") {
                define(r.id, "world", -1);
            } else if (r.type == "district") {
                District d;
                d.id = r.id;
                d.src = at(r.line);
                d.name = text(r, "name");
                d.label = point(r, "label");
                d.order = integer(r, "order");
                define(d.id, "district", (int)m.districts.size());
                m.districts.push_back(d);
            }
        }
    }

    void district() {
        DistrictPart p;
        p.file = file;
        p.district = headerRef("district");
        for (const Ntw::Record& r : doc->records) {
            if (r.type == "bounds") {
                p.bounds = points(r, "poly");
            } else if (r.type == "label") {
                p.hasLabel = true;
                p.label = point(r, "at");
                p.labelText = text(r, "text");
            } else if (r.type == "trunk") {
                p.trunks.push_back(ref(r, "node"));
            } else if (r.type == "zone") {
                Zone z;
                z.id = r.id;
                z.src = at(r.line);
                z.kind = text(r, "kind");
                z.poly = points(r, "poly");
                z.tags = list(r, "tags");
                z.peds = hours24(r, "peds", "zone " + r.id);
                z.traffic = hours24(r, "traffic", "zone " + r.id);
                if (const Ntw::Field* f = r.field("days")) {
                    static const char* const kDays[] = {"mon", "tue", "wed", "thu", "fri", "sat", "sun"};
                    bool ok = Ntw::toList(f->value, z.days);
                    for (size_t i = 0; ok && i < z.days.size(); i++) {
                        bool known = false;
                        for (const char* d : kDays) known = known || z.days[i] == d;
                        ok = known;
                    }
                    if (!ok) {
                        badValue(*f, "zone " + r.id, "a list of mon ... sun");
                        z.days.clear();
                    }
                }
                define(z.id, "zone", -1);
                p.zones.push_back(z);
            }
        }
        districtParts.push_back(p);
    }

    void edgeBlock(const Ntw::Record& r, Edge& e, int edgeIndex) {
        const std::string what = "edge " + e.id;
        for (const Ntw::Record& c : r.children) {
            if (c.type == "lanes") {
                LaneCount x;
                x.src = at(c.line);
                x.from = edgeAt(c, "from", false, what + " lanes");
                x.to = edgeAt(c, "to", true, what + " lanes");
                x.dir = dir(c);
                x.count = integer(c, "count");
                if (c.has("add")) x.add = side(c, "add", SIDE_LEFT);
                if (c.has("drop")) x.drop = side(c, "drop", SIDE_LEFT);
                x.taper = num(c, "taper");
                e.laneCounts.push_back(x);
            } else if (c.type == "lane") {
                LaneUse x;
                x.src = at(c.line);
                x.dir = dir(c);
                const Ntw::Field* f = c.field("index");
                long long n = -1;
                if (f && f->value == "left") x.side = SIDE_LEFT;
                else if (f && f->value == "right") x.side = SIDE_RIGHT;
                else if (f && Ntw::toInt(f->value, &n) && n >= 0) x.index = (int)n;
                else if (f) badValue(*f, what + " lane", "a lane number from 0, left or right");
                x.use = text(c, "use");
                x.from = edgeAt(c, "from", false, what + " lane");
                x.to = edgeAt(c, "to", true, what + " lane");
                e.laneUses.push_back(x);
            } else if (c.type == "noswitch") {
                NoSwitch x;
                x.src = at(c.line);
                x.dir = dir(c);
                x.from = edgeAt(c, "from", false, what + " noswitch");
                x.to = edgeAt(c, "to", true, what + " noswitch");
                e.noSwitch.push_back(x);
            } else if (c.type == "moves") {
                Moves x;
                x.src = at(c.line);
                x.atEnd = std::string(c.value("at", "n1")) == "n0" ? 0 : 1;
                x.lanes = list(c, "lanes");
                e.moves.push_back(x);
            } else if (c.type == "crossing") {
                Crossing x;
                x.src = at(c.line);
                x.at = num(c, "at");
                x.kind = text(c, "kind");
                x.w = num(c, "w");
                e.crossings.push_back(x);
            } else if (c.type == "busstop") {
                BusStop b;
                b.id = c.id;
                b.src = at(c.line);
                b.edge = edgeIndex;
                b.at = num(c, "at");
                b.side = side(c, "side", SIDE_RIGHT);
                b.kind = text(c, "kind", "bus");
                b.shelter = ref(c, "shelter");
                if (const Ntw::Field* f = c.field("wait")) {
                    const std::vector<std::string> p = splitAt(f->value, ':');
                    vec2 pt(0.f);
                    bool ok = false;
                    if (p.size() == 2) {   // x,y:r
                        ok = Ntw::toPoint(p[0], &pt) && toFloat(p[1], &b.waitRadius) && b.waitRadius > 0.f;
                        if (ok) b.wait.push_back(pt);
                    } else if (p.size() == 1) {
                        ok = Ntw::toPoints(f->value, b.wait);
                    }
                    if (!ok) {
                        badValue(*f, "busstop " + b.id, "points or a point and a radius (x,y:r)");
                        b.wait.clear();
                        b.waitRadius = 0.f;
                    }
                }
                b.routes = refs(c, "routes");
                b.clear = num(c, "clear");
                define(b.id, "busstop", (int)m.busStops.size());
                e.busStops.push_back((int)m.busStops.size());
                m.busStops.push_back(b);
            } else if (c.type == "parkbay" || c.type == "nostop" || c.type == "loading" || c.type == "taxirank" || c.type == "nojaywalk") {
                KerbStretch x;
                x.src = at(c.line);
                x.type = c.type;
                x.from = num(c, "from");
                x.to = num(c, "to");
                x.side = side(c, "side", c.type == "nojaywalk" ? SIDE_BOTH : SIDE_RIGHT);
                x.kind = text(c, c.type == "nostop" ? "why" : "kind");
                x.meter = integer(c, "meter");
                x.disabled = integer(c, "disabled");
                x.cap = integer(c, "cap");
                x.hours = text(c, "hours");
                e.kerb.push_back(x);
            } else if (c.type == "curbcut") {
                CurbCut x;
                x.src = at(c.line);
                x.at = num(c, "at");
                x.w = num(c, "w");
                x.side = side(c, "side", SIDE_RIGHT);
                x.to = ref(c, "to");
                e.curbCuts.push_back(x);
            } else if (c.type == "deck") {
                Deck x;
                x.src = at(c.line);
                x.from = num(c, "from");
                x.to = num(c, "to");
                x.kind = text(c, "kind");
                e.decks.push_back(x);
            } else if (c.type == "sign") {
                Sign x;
                x.src = at(c.line);
                x.at = num(c, "at");
                x.side = side(c, "side", SIDE_RIGHT);
                x.kind = text(c, "kind");
                x.value = text(c, "value");
                e.signs.push_back(x);
            }
        }
    }

    void node(const Ntw::Record& r) {
        Node n;
        n.id = r.id;
        n.src = at(r.line);
        n.file = file;
        n.at = point(r, "at");
        n.z = num(r, "z");
        const std::string c = text(r, "control");
        n.control = c == "none" ? Node::CTL_NONE : c == "stop" ? Node::CTL_STOP : c == "yield" ? Node::CTL_YIELD : c == "signal" ? Node::CTL_SIGNAL : Node::CTL_AUTO;
        n.plan = ref(r, "plan");   // (auto: no reference)
        n.offset = num(r, "offset");
        for (const Item& it : items(r, "approaches"))
            if (it.parts.size() == 2) n.approaches.push_back(std::make_pair(it.parts[0], it.ref));
        if (std::string(r.value("stops", "")) == "all") n.stopsAll = true;
        else n.stops = refs(r, "stops");
        n.yields = refs(r, "yield");
        if (const Ntw::Field* f = r.field("bulb"))
            if (!toFloat(f->value, &n.bulb) || n.bulb <= 0.f) {
                badValue(*f, "node " + n.id, "a radius in metres");
                n.bulb = 0.f;
            }
        define(n.id, "node", (int)m.nodes.size());
        m.nodes.push_back(n);
    }

    void edge(const Ntw::Record& r) {
        static const char* const kClasses[RC_COUNT] = {"highway", "boulevard", "avenue", "street", "lane", "rural", "dirt", "ramp"};
        Edge e;
        e.id = r.id;
        e.src = at(r.line);
        e.file = file;
        const std::string what = "edge " + e.id;
        e.from = ref(r, "from");
        e.to = ref(r, "to");
        const std::string cls = text(r, "class", "street");
        for (int c = 0; c < RC_COUNT; c++)
            if (cls == kClasses[c]) e.cls = (RoadClass)c;
        e.name = text(r, "name");
        if (const Ntw::Field* f = r.field("lanes")) {
            const std::vector<std::string> p = splitAt(f->value, '/');
            long long fw = -1, bw = -1;
            if (p.size() != 2 || !Ntw::toInt(p[0], &fw) || !Ntw::toInt(p[1], &bw) || fw < 0 || bw < 0 || fw + bw == 0 || fw > 8 || bw > 8) {
                badValue(*f, what, "F/B, the lanes each way, up to 8 (3/0 is one-way)");
            } else {
                e.lanesF = (int)fw;
                e.lanesB = (int)bw;
            }
        }
        e.laneWidth = num(r, "lanewidth");
        if (const Ntw::Field* f = r.field("median")) {
            const std::vector<std::string> p = splitAt(f->value, ':');
            if ((p.size() != 1 && (p.size() != 2 || p[1] != "barrier")) || !toFloat(p[0], &e.median) || e.median < 0.f) {
                badValue(*f, what, "W or W:barrier");
                e.median = -1.f;
            } else {
                e.medianBarrier = p.size() == 2;
            }
        }
        kerb(r, "shoulder", &e.shoulderL, &e.shoulderR, what);
        kerb(r, "parking", &e.parkingL, &e.parkingR, what);
        kerb(r, "sidewalk", &e.sidewalkL, &e.sidewalkR, what);
        e.speed = num(r, "speed");
        bool badFlag = false;
        for (const std::string& fl : list(r, "flags")) {
            if (fl == "bridge") e.flags |= RF_BRIDGE;
            else if (fl == "elevated") e.flags |= RF_ELEVATED;
            else if (fl == "unpaved") e.flags |= RF_UNPAVED;
            else if (fl == "tunnel") e.tunnel = true;
            else badFlag = true;
        }
        if (badFlag) badValue(*r.field("flags"), what, "a list of bridge, elevated, unpaved, tunnel");
        e.shape = points(r, "shape");
        e.profile = pairs(r, "profile", what);
        const int index = (int)m.edges.size();
        define(e.id, "edge", index);
        m.edges.push_back(e);
        edgeBlock(r, m.edges.back(), index);
    }

    void junction(const Ntw::Record& r) {
        Junction j;
        j.src = at(r.line);
        j.node = idRef(r);
        j.kind = text(r, "kind", "plain");
        j.turns = text(r, "turns", "all");
        j.box = points(r, "box");
        for (const Ntw::Record& c : r.children) {
            if (c.type == "stopline") {
                Junction::StopLine x;
                x.src = at(c.line);
                x.edge = ref(c, "edge");
                x.at = num(c, "at");
                j.stopLines.push_back(x);
            } else if (c.type == "connect") {
                Junction::Connect x;
                x.src = at(c.line);
                for (int k = 0; k < 2; k++) {
                    const char* key = k ? "to" : "from";
                    const std::vector<Item> it = items(c, key);
                    if (it.size() != 1 || it[0].parts.size() != 2) continue;
                    long long lane = -1;
                    if (!Ntw::toInt(it[0].parts[1], &lane) || lane < 0) {
                        badValue(*c.field(key), "connect", "<edge>:<lane>, the lane a number from 0");
                        lane = 0;
                    }
                    (k ? x.toEdge : x.fromEdge) = it[0].ref;
                    (k ? x.toLane : x.fromLane) = (int)lane;
                }
                x.turn = text(c, "turn");
                x.isProtected = integer(c, "protected") != 0;
                j.connects.push_back(x);
            } else if (c.type == "ban") {
                Junction::Ban x;
                x.src = at(c.line);
                x.fromEdge = ref(c, "from");
                x.toEdge = ref(c, "to");
                j.bans.push_back(x);
            } else if (c.type == "head") {
                Junction::Head x;
                x.src = at(c.line);
                x.at = point3(c, "at");
                const std::vector<Item> it = items(c, "faces");
                if (it.size() == 1 && it[0].parts.size() == 2) {
                    x.edge = it[0].ref;
                    if (it[0].parts[1] == "bwd") x.dir = DIR_BWD;
                    else if (it[0].parts[1] != "fwd") badValue(*c.field("faces"), "head", "<edge>:fwd or <edge>:bwd");
                }
                j.heads.push_back(x);
            }
        }
        m.junctions.push_back(j);
    }

    void roads() {
        for (const Ntw::Record& r : doc->records) {
            if (r.type == "node") node(r);
            else if (r.type == "edge") edge(r);
            else if (r.type == "junction") junction(r);
        }
    }

    void signal() {
        SignalPlan p;
        if (const Ntw::Field* h = doc->headerField("id")) p.id = h->value;
        p.src = at(1);
        for (const Ntw::Record& r : doc->records) {
            if (r.type == "plan") {
                p.cycle = num(r, "cycle");
                p.legs = integer(r, "legs");
            } else if (r.type == "phase") {
                Phase ph;
                ph.id = r.id;
                ph.src = at(r.line);
                ph.green = num(r, "green");
                ph.amber = num(r, "amber");
                ph.allred = num(r, "allred");
                ph.moves = list(r, "moves");
                ph.walk = list(r, "walk");
                ph.walkTime = num(r, "walktime");
                ph.flash = num(r, "flash");
                p.phases.push_back(ph);
            }
        }
        define(p.id, "signal", (int)m.signals.size());
        m.signals.push_back(p);
    }

    void transit() {
        TransitLine t;
        if (const Ntw::Field* h = doc->headerField("line")) t.id = h->value;
        if (const Ntw::Field* h = doc->headerField("kind")) t.kind = h->value;
        if (const Ntw::Field* h = doc->headerField("name")) t.name = h->value;
        t.src = at(1);
        for (const Ntw::Record& r : doc->records) {
            if (r.type == "route") {
                t.routeEdges = refs(r, "edges");
                t.routePts = points(r, "pts");
                t.routeProfile = pairs(r, "profile", "route");
            } else if (r.type == "station") {
                TransitLine::Station s;
                s.id = r.id;
                s.src = at(r.line);
                s.at = point(r, "at");
                s.z = num(r, "z");
                s.name = text(r, "name");
                s.side = text(r, "side");
                t.stations.push_back(s);
            } else if (r.type == "stop") {
                TransitLine::Stop s;
                s.id = r.id;
                s.src = at(r.line);
                s.busStop = ref(r, "busstop");
                t.stops.push_back(s);
            } else if (r.type == "pier") {
                TransitLine::Pier p;
                p.id = r.id;
                p.src = at(r.line);
                p.at = point(r, "at");
                p.yaw = num(r, "yaw");
                p.name = text(r, "name");
                t.piers.push_back(p);
            } else if (r.type == "pole") {
                TransitLine::Pole p;
                p.src = at(r.line);
                p.at = point(r, "at");
                p.h = num(r, "h");
                t.poles.push_back(p);
            } else if (r.type == "service") {
                t.headway = num(r, "headway");
                t.first = num(r, "first");
                t.last = num(r, "last");
            }
        }
        define(t.id, "line", (int)m.transit.size());
        m.transit.push_back(t);
    }

    void places() {
        for (const Ntw::Record& r : doc->records) {
            if (r.type == "place") {
                Place p;
                p.id = r.id;
                p.src = at(r.line);
                p.building = ref(r, "building");
                p.door = text(r, "door");
                p.walk = point(r, "walk");
                p.curb = point3(r, "curb");
                p.dir = point(r, "dir");
                p.water = points(r, "water");
                p.note = text(r, "note");
                define(p.id, "place", (int)m.places.size());
                m.places.push_back(p);
            } else if (r.type == "anchor") {
                Anchor a;
                a.id = r.id;
                a.src = at(r.line);
                a.kind = text(r, "kind");
                a.event = text(r, "event");
                a.at = point(r, "at");
                a.z = num(r, "z");
                a.yaw = num(r, "yaw");
                a.poly = points(r, "poly");
                a.line = points(r, "line");
                a.building = ref(r, "building");
                a.edge = ref(r, "edge");
                a.door = text(r, "door");
                a.cap = integer(r, "cap");
                a.tags = list(r, "tags");
                define(a.id, "anchor", (int)m.anchors.size());
                m.anchors.push_back(a);
            }
        }
    }

    void block() {
        Block b;
        if (const Ntw::Field* h = doc->headerField("id")) b.id = h->value;
        b.src = at(1);
        b.file = file;
        b.district = headerRef("district");
        const int bi = (int)m.blocks.size();
        define(b.id, "block", bi);
        for (const Ntw::Record& r : doc->records) {
            if (r.type == "parcel") {
                Parcel p;
                p.id = r.id;
                p.src = at(r.line);
                p.block = bi;
                p.poly = points(r, "poly");
                p.use = text(r, "use");
                const std::vector<Item> it = items(r, "front");
                if (it.size() == 1 && it[0].parts.size() == 2) {
                    p.front = it[0].ref;
                    if (it[0].parts[1] == "left") p.frontSide = SIDE_LEFT;
                    else if (it[0].parts[1] != "right") badValue(*r.field("front"), "parcel " + p.id, "<edge>:left or <edge>:right");
                }
                p.graded = r.has("grade");
                p.grade = num(r, "grade");
                p.blend = num(r, "blend");
                p.ground = text(r, "ground");
                for (const Ntw::Record& c : r.children) {
                    if (c.type == "paint") {
                        GroundPaint g;
                        g.src = at(c.line);
                        g.kind = text(c, "kind");
                        g.poly = points(c, "poly");
                        p.paints.push_back(g);
                    } else if (c.type == "pad") {
                        Pad g;
                        g.src = at(c.line);
                        g.poly = points(c, "poly");
                        g.z = num(c, "z");
                        g.kind = text(c, "kind");
                        p.pads.push_back(g);
                    } else if (c.type == "fence") {
                        Fence g;
                        g.src = at(c.line);
                        g.kind = ref(c, "kind");
                        g.line = points(c, "line");
                        if (const Ntw::Field* f = c.field("gate")) {
                            const std::vector<std::string> parts = splitAt(f->value, ':');
                            float a = 0.f, w = 0.f;
                            if (parts.size() == 2 && toFloat(parts[0], &a) && toFloat(parts[1], &w)) g.gates.push_back(vec2(a, w));
                            else badValue(*f, "fence", "<at>:<width>");
                        }
                        p.fences.push_back(g);
                    }
                }
                define(p.id, "parcel", (int)m.parcels.size());
                b.parcels.push_back((int)m.parcels.size());
                m.parcels.push_back(p);
            } else if (r.type == "lot") {
                Lot l;
                l.id = r.id;
                l.src = at(r.line);
                l.block = bi;
                l.levels = integer(r, "levels", 1);
                define(l.id, "lot", (int)m.lots.size());
                b.lots.push_back((int)m.lots.size());
                m.lots.push_back(l);
            } else if (r.type == "plaza") {
                Plaza z;
                z.id = r.id;
                z.src = at(r.line);
                z.block = bi;
                z.poly = points(r, "poly");
                z.ground = text(r, "ground");
                z.tags = list(r, "tags");
                define(z.id, "plaza", (int)m.plazas.size());
                b.plazas.push_back((int)m.plazas.size());
                m.plazas.push_back(z);
            } else if (r.type == "tree" || r.type == "plant" || r.type == "prop" || r.type == "light") {
                Dress d;
                d.what = r.type == "tree" ? Dress::DRESS_TREE : r.type == "plant" ? Dress::DRESS_PLANT : r.type == "prop" ? Dress::DRESS_PROP : Dress::DRESS_LIGHT;
                d.src = at(r.line);
                d.block = bi;
                d.kind = ref(r, "kind");
                d.hasYaw = r.has("yaw");
                d.yaw = num(r, "yaw");
                if (d.what == Dress::DRESS_LIGHT) {
                    // at=x,y on the ground, or x,y,z
                    const Ntw::Field* f = r.field("at");
                    vec3 p3(0.f);
                    if (f && Ntw::toPoint3(f->value, &p3)) {
                        d.at = p3.xy();
                        d.hasZ = true;
                        d.z = p3.z;
                    } else if (f && !Ntw::toPoint(f->value, &d.at)) {
                        badValue(*f, "light", "x,y or x,y,z");
                    }
                    d.sched = text(r, "sched");
                } else {
                    d.at = point(r, "at");
                }
                if (d.what == Dress::DRESS_TREE) {
                    d.h = num(r, "h");
                    d.canopy = num(r, "canopy");
                } else if (d.what == Dress::DRESS_PLANT) {
                    d.scale = num(r, "scale", 1.f);
                } else if (d.what == Dress::DRESS_PROP) {
                    d.hasZ = r.has("z");
                    d.z = num(r, "z");
                    d.look = integer(r, "look");
                }
                b.dressing.push_back((int)m.dressing.size());
                m.dressing.push_back(d);
            } else if (r.type == "path" || r.type == "spot") {
                define(r.id, r.type == "path" ? "path" : "spot", -1);
            }
        }
        m.blocks.push_back(b);
    }

    void building() {
        Building b;
        if (const Ntw::Field* h = doc->headerField("id")) b.id = h->value;
        b.src = at(1);
        b.file = file;
        b.block = headerRef("block");
        b.parcel = headerRef("parcel");
        bool placed = false;
        for (const Ntw::Record& r : doc->records) {
            if (r.type != "place" || placed) continue;
            placed = true;
            b.plan = ref(r, "plan");
            b.at = point(r, "at");
            b.z = num(r, "z");
            b.yaw = num(r, "yaw");
            b.mirror = integer(r, "mirror") != 0;
        }
        define(b.id, "building", (int)m.buildings.size());
        m.buildings.push_back(b);
    }

    void libItem(const std::string& id, const char* cls, int line, int record) {
        LibItem it;
        it.id = id;
        it.kind = cls;
        it.src = at(line);
        it.file = file;
        it.record = record;
        define(id, cls, (int)m.library.size());
        m.library.push_back(it);
    }

    void library() {
        const std::string& k = doc->kind;
        if (k == "palette") {
            const Ntw::Field* fam = doc->headerField("family");
            for (size_t i = 0; i < doc->records.size(); i++) {
                const Ntw::Record& r = doc->records[i];
                if (r.type == "palette" && !r.id.empty()) libItem("lib.palette." + std::string(fam ? fam->value : "") + "." + r.id, "palette", r.line, (int)i);
            }
        } else if (k == "catalog") {
            static const char* const kCategories[] = {"kit", "prop", "veg", "light", "fence"};
            for (size_t i = 0; i < doc->records.size(); i++) {
                const Ntw::Record& r = doc->records[i];
                if (r.type != "piece" || r.id.empty()) continue;
                const std::vector<std::string> p = splitAt(r.id, '.');
                const char* cls = "piece";
                for (const char* c : kCategories)
                    if (p.size() == 3 && p[0] == "lib" && p[1] == c) cls = c;
                libItem(r.id, cls, r.line, (int)i);
                LibItem& it = m.library.back();
                it.code = text(r, "code");
                it.variants = integer(r, "variants");
                it.collide = text(r, "collide");
                if (const Ntw::Field* f = r.field("size")) {   // WxDxH
                    const std::vector<std::string> d = splitAt(f->value, 'x');
                    if (d.size() != 3 || !toFloat(d[0], &it.size.x) || !toFloat(d[1], &it.size.y) || !toFloat(d[2], &it.size.z)) badValue(*f, "piece " + r.id, "WxDxH");
                }
            }
        } else if (const Ntw::Field* h = doc->headerField("id")) {
            libItem(h->value, k.c_str(), 1, -1);
        }
    }

    void type(int fi) {
        file = fi;
        doc = &m.files[(size_t)fi].doc;
        const std::string& k = doc->kind;
        if (k == "world") world();
        else if (k == "district") district();
        else if (k == "roads") roads();
        else if (k == "signal") signal();
        else if (k == "transit") transit();
        else if (k == "places") places();
        else if (k == "block") block();
        else if (k == "building") building();
        else if (k == "plan" || k == "layout" || k == "unit" || k == "fitout" || k == "set" || k == "facade" || k == "palette" || k == "catalog") library();
        // (terrain: worldc compiles it from its document)
    }

    // --------------------------------------------------------------------------------------------- resolving

    void resolve(Ref& r) const {
        if (r.text.empty()) return;
        const auto it = m.ids.find(r.text);
        if (it == m.ids.end() || it->second.index < 0) return;
        for (const std::string& c : splitAt(r.classes, '|'))
            if (c == it->second.cls) {
                r.cls = c;
                r.target = it->second.index;
                return;
            }
    }

    void resolveAll() {
        for (DistrictPart& p : districtParts) {
            resolve(p.district);
            if (p.district.target < 0) continue;   // (worldv reports the reference)
            District& d = m.districts[(size_t)p.district.target];
            if (d.file >= 0) continue;             // (a second file for the district: worldv reports it)
            d.file = p.file;
            d.bounds = std::move(p.bounds);
            if (p.hasLabel) {
                d.label = p.label;
                d.labelText = p.labelText;
            }
            d.zones = std::move(p.zones);
            d.trunkNodes = std::move(p.trunks);
        }
        for (District& d : m.districts)
            for (Ref& r : d.trunkNodes) resolve(r);
        for (Node& n : m.nodes) {
            resolve(n.plan);
            for (auto& a : n.approaches) resolve(a.second);
            for (Ref& r : n.stops) resolve(r);
            for (Ref& r : n.yields) resolve(r);
        }
        for (size_t i = 0; i < m.edges.size(); i++) {
            Edge& e = m.edges[i];
            resolve(e.from);
            resolve(e.to);
            if (e.from.target >= 0) m.nodes[(size_t)e.from.target].edges.push_back((int)i);
            if (e.to.target >= 0) m.nodes[(size_t)e.to.target].edges.push_back((int)i);
            for (CurbCut& c : e.curbCuts) resolve(c.to);
        }
        for (BusStop& b : m.busStops) {
            resolve(b.shelter);
            for (Ref& r : b.routes) resolve(r);
        }
        for (size_t i = 0; i < m.junctions.size(); i++) {
            Junction& j = m.junctions[i];
            resolve(j.node);
            if (j.node.target >= 0 && m.nodes[(size_t)j.node.target].junction < 0) m.nodes[(size_t)j.node.target].junction = (int)i;
            for (auto& x : j.stopLines) resolve(x.edge);
            for (auto& x : j.connects) {
                resolve(x.fromEdge);
                resolve(x.toEdge);
            }
            for (auto& x : j.bans) {
                resolve(x.fromEdge);
                resolve(x.toEdge);
            }
            for (auto& x : j.heads) resolve(x.edge);
        }
        for (TransitLine& t : m.transit) {
            for (Ref& r : t.routeEdges) resolve(r);
            for (auto& s : t.stops) resolve(s.busStop);
        }
        for (Place& p : m.places) resolve(p.building);
        for (Anchor& a : m.anchors) {
            resolve(a.building);
            resolve(a.edge);
        }
        for (Block& b : m.blocks) resolve(b.district);
        for (Parcel& p : m.parcels) resolve(p.front);
        for (Building& b : m.buildings) {
            resolve(b.block);
            resolve(b.parcel);
            resolve(b.plan);
        }
    }
};

}  // namespace worldmodel_detail

// ------------------------------------------------------------------------------------------------------------ the model

int Model::find(const char* cls, const std::string& id) const {
    const auto it = ids.find(id);
    return it != ids.end() && it->second.cls == cls ? it->second.index : -1;
}

std::string Model::where(const Src& s) const {
    if (s.file < 0 || s.file >= (int)files.size()) return "(no file)";
    const std::string& p = files[(size_t)s.file].path;
    return s.line > 0 ? p + ":" + std::to_string(s.line) : p;
}

std::vector<std::string> discover(const std::string& dir, std::vector<Error>& errors) {
    std::vector<std::string> found;
    std::error_code ec;
    std::filesystem::recursive_directory_iterator it(dir, ec), end;
    while (!ec && it != end) {
        std::error_code e2;
        if (it->is_regular_file(e2) && it->path().extension() == ".ntw") found.push_back(it->path().generic_string());
        it.increment(ec);
    }
    if (ec) errors.push_back(Error{Src{}, "file.read", dir + ": can't list it: " + ec.message()});
    std::sort(found.begin(), found.end());
    return found;
}

bool loadParsed(std::vector<File>&& files, Model& out, std::vector<Error>& errors) {
    using namespace worldmodel_detail;
    const size_t errorsBefore = errors.size();
    out = Model();
    out.files = std::move(files);
    for (File& f : out.files) f.kind = f.doc.kind;
    Loader ld(out, errors);
    for (size_t i = 0; i < out.files.size(); i++)
        if (out.files[i].valid) ld.type((int)i);
    ld.resolveAll();
    return errors.size() == errorsBefore;
}

bool load(const std::string& dir, Model& out, std::vector<Error>& errors) {
    using namespace worldmodel_detail;
    const size_t errorsBefore = errors.size();
    std::vector<File> files;
    for (const std::string& path : discover(dir, errors)) {
        File f;
        f.path = path;
        const int fi = (int)files.size();
        std::string text;
        if (!readText(path, text)) {
            errors.push_back(Error{Src{fi, 0}, "file.read", "can't read it"});
            files.push_back(std::move(f));
            continue;
        }
        std::vector<Ntw::Error> errs;
        if (!Ntw::parse(text.data(), text.size(), Ntw::schemaOpensBlock, f.doc, errs)) {
            for (const Ntw::Error& e : errs) errors.push_back(Error{Src{fi, e.line}, "file.parse", e.message});
        } else {
            Ntw::checkDocument(f.doc, errs);
            for (const Ntw::Error& e : errs) errors.push_back(Error{Src{fi, e.line}, "file.schema", e.message});
            f.valid = errs.empty();
        }
        files.push_back(std::move(f));
    }
    std::vector<Error> typing;
    loadParsed(std::move(files), out, typing);
    errors.insert(errors.end(), typing.begin(), typing.end());
    return errors.size() == errorsBefore;
}

}  // namespace Hand
}  // namespace World
