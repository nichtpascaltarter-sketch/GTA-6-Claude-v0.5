// The world model (worldmodel.h): loading the .ntw files into typed records, and compiling the roads.
#include "worldmodel.h"
#include "ntwschema.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace World {
namespace Hand {

namespace {

// ------------------------------------------------------------------------------------------------- reading the fields

struct Reader {
    Model& m;
    int file;
    std::vector<Error>& errors;

    Src at(const Ntw::Record& r) const { return Src{file, r.line}; }
    Src at(const Ntw::Field& f) const { return Src{file, f.line}; }
    void fail(const Src& s, const std::string& msg) const { errors.push_back(Error{s, msg}); }

    std::string text(const Ntw::Record& r, const char* key, const char* fallback = "") const { return r.value(key, fallback); }
    float num(const Ntw::Record& r, const char* key, float fallback = 0.f) const {
        double v = fallback;
        if (const Ntw::Field* f = r.field(key)) Ntw::toNumber(f->value, &v);   // (the schema checked the type)
        return (float)v;
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
    Ref ref(const Ntw::Record& r, const char* key) const {
        Ref out;
        if (const Ntw::Field* f = r.field(key)) {
            out.text = f->value;
            out.src = at(*f);
        }
        return out;
    }
    std::vector<Ref> refs(const Ntw::Record& r, const char* key) const {
        std::vector<Ref> out;
        const Ntw::Field* f = r.field(key);
        if (!f) return out;
        std::vector<std::string> items;
        Ntw::toList(f->value, items);
        for (const std::string& s : items) {
            Ref x;
            x.text = s;
            x.src = at(*f);
            out.push_back(x);
        }
        return out;
    }
    // "s:z;s:z..." (profiles): pairs of numbers
    std::vector<vec2> pairs(const Ntw::Record& r, const char* key) const {
        std::vector<vec2> out;
        const Ntw::Field* f = r.field(key);
        if (!f) return out;
        std::stringstream ss(f->value);
        std::string item;
        while (std::getline(ss, item, ';')) {
            const size_t c = item.find(':');
            double a = 0, b = 0;
            if (c == std::string::npos || !Ntw::toNumber(item.substr(0, c), &a) || !Ntw::toNumber(item.substr(c + 1), &b)) {
                fail(at(*f), std::string(key) + ": '" + item + "' is not s:z");
                continue;
            }
            out.push_back(vec2((float)a, (float)b));
        }
        return out;
    }
    EdgeAt edgeAt(const Ntw::Record& r, const char* key, bool endByDefault) const {
        EdgeAt e;
        const Ntw::Field* f = r.field(key);
        if (!f) {
            e.toEnd = endByDefault;
            return e;
        }
        if (f->value == "end") {
            e.toEnd = true;
            return e;
        }
        double v = 0;
        Ntw::toNumber(f->value, &v);
        e.s = (float)v;
        return e;
    }
    Side side(const Ntw::Record& r, const char* key = "side") const {
        const std::string v = r.value(key, "right");
        return v == "left" ? SIDE_LEFT : (v == "both" ? SIDE_BOTH : SIDE_RIGHT);
    }
    Dir dir(const Ntw::Record& r) const { return std::string(r.value("dir", "fwd")) == "bwd" ? DIR_BWD : DIR_FWD; }
};

// A kerb compound: "none", "left:W", "right:W" or "both:W"
void kerbWidths(const std::string& v, float* left, float* right) {
    if (v.empty()) return;
    if (v == "none") {
        *left = *right = 0.f;
        return;
    }
    const size_t c = v.find(':');
    double w = 0;
    if (c == std::string::npos || !Ntw::toNumber(v.substr(c + 1), &w)) return;
    const std::string s = v.substr(0, c);
    if (s == "left" || s == "both") *left = (float)w;
    if (s == "right" || s == "both") *right = (float)w;
}

RoadClass roadClass(const std::string& w) {
    static const char* names[RC_COUNT] = {"highway", "boulevard", "avenue", "street", "lane", "rural", "dirt", "ramp"};
    for (int c = 0; c < RC_COUNT; c++)
        if (w == names[c]) return (RoadClass)c;
    return RC_STREET;
}

// ------------------------------------------------------------------------------------------------------- the kinds

void readWorld(Reader& rd, const Ntw::Document& doc) {
    for (const Ntw::Record& r : doc.records) {
        if (r.type != "district") continue;
        District d;
        d.id = r.id;
        d.src = rd.at(r);
        d.name = rd.text(r, "name");
        d.label = rd.point(r, "label");
        d.order = rd.integer(r, "order");
        rd.m.districts.push_back(d);
    }
}

void readDistrict(Reader& rd, const Ntw::Document& doc) {
    const Ntw::Field* h = doc.headerField("district");
    const std::string id = h ? h->value : std::string();
    District* d = nullptr;
    for (District& x : rd.m.districts)
        if (x.id == id) d = &x;
    if (!d) {   // (worldv reports a district file that world.ntw doesn't list; the model keeps it)
        District x;
        x.id = id;
        x.src = Src{rd.file, 1};
        rd.m.districts.push_back(x);
        d = &rd.m.districts.back();
    }
    d->file = rd.file;
    for (const Ntw::Record& r : doc.records) {
        if (r.type == "bounds") d->bounds = rd.points(r, "poly");
        else if (r.type == "label") d->label = rd.point(r, "at");
        else if (r.type == "trunk") d->trunkNodes.push_back(rd.ref(r, "node"));
        else if (r.type == "zone") {
            Zone z;
            z.id = r.id;
            z.src = rd.at(r);
            z.kind = rd.text(r, "kind");
            z.poly = rd.points(r, "poly");
            z.tags = rd.list(r, "tags");
            for (const std::string& v : rd.list(r, "peds")) z.peds.push_back((float)atof(v.c_str()));
            for (const std::string& v : rd.list(r, "traffic")) z.traffic.push_back((float)atof(v.c_str()));
            z.days = rd.list(r, "days");
            d->zones.push_back(z);
        }
    }
}

void readEdgeBlock(Reader& rd, const Ntw::Record& r, Edge& e, int edgeIndex) {
    for (const Ntw::Record& c : r.children) {
        if (c.type == "lanes") {
            LaneCount x;
            x.src = rd.at(c);
            x.from = rd.edgeAt(c, "from", false);
            x.to = rd.edgeAt(c, "to", true);
            x.dir = rd.dir(c);
            x.count = rd.integer(c, "count");
            if (c.has("add")) x.add = rd.side(c, "add");
            if (c.has("drop")) x.drop = rd.side(c, "drop");
            x.taper = rd.num(c, "taper");
            e.laneCounts.push_back(x);
        } else if (c.type == "lane") {
            LaneUse x;
            x.src = rd.at(c);
            x.dir = rd.dir(c);
            x.index = c.has("index") ? rd.integer(c, "index") : -1;
            x.side = c.has("left") ? SIDE_LEFT : SIDE_RIGHT;
            x.use = rd.text(c, "use");
            x.from = rd.edgeAt(c, "from", false);
            x.to = rd.edgeAt(c, "to", true);
            e.laneUses.push_back(x);
        } else if (c.type == "noswitch") {
            NoSwitch x;
            x.src = rd.at(c);
            x.dir = rd.dir(c);
            x.from = rd.edgeAt(c, "from", false);
            x.to = rd.edgeAt(c, "to", true);
            e.noSwitch.push_back(x);
        } else if (c.type == "moves") {
            Moves x;
            x.src = rd.at(c);
            x.atEnd = std::string(c.value("at", "n1")) == "n0" ? 0 : 1;
            x.lanes = rd.list(c, "lanes");
            e.moves.push_back(x);
        } else if (c.type == "crossing") {
            Crossing x;
            x.src = rd.at(c);
            x.at = rd.num(c, "at");
            x.kind = rd.text(c, "kind");
            x.w = rd.num(c, "w");
            e.crossings.push_back(x);
        } else if (c.type == "busstop") {
            BusStop b;
            b.id = c.id;
            b.src = rd.at(c);
            b.edge = edgeIndex;
            b.at = rd.num(c, "at");
            b.side = rd.side(c);
            b.kind = rd.text(c, "kind", "bus");
            b.shelter = rd.ref(c, "shelter");
            const std::string w = rd.text(c, "wait");
            const size_t colon = w.find(':');
            if (colon != std::string::npos) {   // x,y:r
                vec2 p(0.f);
                double rr = 0;
                if (Ntw::toPoint(w.substr(0, colon), &p) && Ntw::toNumber(w.substr(colon + 1), &rr)) {
                    b.wait.push_back(p);
                    b.waitRadius = (float)rr;
                } else {
                    rd.fail(b.src, "busstop " + b.id + ": wait= is neither points nor x,y:r");
                }
            } else if (!w.empty()) {
                Ntw::toPoints(w, b.wait);
            }
            b.routes = rd.refs(c, "routes");
            b.clear = rd.num(c, "clear");
            e.busStops.push_back((int)rd.m.busStops.size());
            rd.m.busStops.push_back(b);
        } else if (c.type == "parkbay" || c.type == "nostop" || c.type == "loading" || c.type == "taxirank" || c.type == "nojaywalk") {
            KerbStretch x;
            x.src = rd.at(c);
            x.type = c.type;
            x.from = rd.num(c, "from");
            x.to = rd.num(c, "to");
            x.side = rd.side(c);
            x.kind = c.type == "nostop" ? rd.text(c, "why") : rd.text(c, "kind");
            x.meter = rd.integer(c, "meter");
            x.disabled = rd.integer(c, "disabled");
            x.cap = rd.integer(c, "cap");
            x.hours = rd.text(c, "hours");
            e.kerb.push_back(x);
        } else if (c.type == "curbcut") {
            CurbCut x;
            x.src = rd.at(c);
            x.at = rd.num(c, "at");
            x.w = rd.num(c, "w");
            x.side = rd.side(c);
            x.to = rd.ref(c, "to");
            e.curbCuts.push_back(x);
        } else if (c.type == "deck") {
            Deck x;
            x.src = rd.at(c);
            x.from = rd.num(c, "from");
            x.to = rd.num(c, "to");
            x.kind = rd.text(c, "kind");
            e.decks.push_back(x);
        } else if (c.type == "sign") {
            Sign x;
            x.src = rd.at(c);
            x.at = rd.num(c, "at");
            x.side = rd.side(c);
            x.kind = rd.text(c, "kind");
            x.value = rd.text(c, "value");
            e.signs.push_back(x);
        }
    }
}

void readRoads(Reader& rd, const Ntw::Document& doc) {
    for (const Ntw::Record& r : doc.records) {
        if (r.type == "node") {
            Node n;
            n.id = r.id;
            n.src = rd.at(r);
            n.file = rd.file;
            n.at = rd.point(r, "at");
            n.z = rd.num(r, "z");
            const std::string c = rd.text(r, "control", "none");
            n.control = c == "stop" ? Node::CTL_STOP : (c == "yield" ? Node::CTL_YIELD : (c == "signal" ? Node::CTL_SIGNAL : Node::CTL_NONE));
            if (std::string(r.value("plan", "auto")) != "auto") n.plan = rd.ref(r, "plan");
            n.offset = rd.num(r, "offset");
            if (const Ntw::Field* f = r.field("approaches")) {
                std::vector<std::string> items;
                Ntw::toList(f->value, items);
                for (const std::string& it : items) {
                    const size_t colon = it.find(':');
                    if (colon != 1) {
                        rd.fail(rd.at(*f), "approaches: '" + it + "' is not <letter>:<edge>");
                        continue;
                    }
                    Ref x;
                    x.text = it.substr(2);
                    x.src = rd.at(*f);
                    n.approaches.push_back(std::make_pair(it[0], x));
                }
            }
            if (std::string(r.value("stops", "")) == "all") n.stopsAll = true;
            else n.stops = rd.refs(r, "stops");
            n.yields = rd.refs(r, "yield");
            n.bulb = rd.num(r, "bulb");
            rd.m.nodes.push_back(n);
        } else if (r.type == "edge") {
            Edge e;
            e.id = r.id;
            e.src = rd.at(r);
            e.file = rd.file;
            e.from = rd.ref(r, "from");
            e.to = rd.ref(r, "to");
            e.cls = roadClass(rd.text(r, "class"));
            e.name = rd.text(r, "name");
            const std::string lanes = rd.text(r, "lanes", "1/1");
            const size_t slash = lanes.find('/');
            long long f = 1, b = 1;
            if (slash == std::string::npos || !Ntw::toInt(lanes.substr(0, slash), &f) || !Ntw::toInt(lanes.substr(slash + 1), &b) || f < 0 || b < 0 || f + b == 0)
                rd.fail(e.src, "edge " + e.id + ": lanes=" + lanes + " is not F/B");
            e.lanesF = (int)f;
            e.lanesB = (int)b;
            e.laneWidth = rd.num(r, "lanewidth");
            const std::string median = rd.text(r, "median");
            if (!median.empty()) {
                const size_t colon = median.find(':');
                double w = 0;
                Ntw::toNumber(median.substr(0, colon), &w);
                e.median = (float)w;
                e.medianBarrier = colon != std::string::npos && median.substr(colon + 1) == "barrier";
            }
            kerbWidths(rd.text(r, "shoulder"), &e.shoulderL, &e.shoulderR);
            kerbWidths(rd.text(r, "parking"), &e.parkingL, &e.parkingR);
            kerbWidths(rd.text(r, "sidewalk"), &e.sidewalkL, &e.sidewalkR);
            e.speed = rd.num(r, "speed");
            for (const std::string& fl : rd.list(r, "flags")) {
                if (fl == "bridge") e.flags |= RF_BRIDGE;
                else if (fl == "elevated") e.flags |= RF_ELEVATED;
                else if (fl == "unpaved") e.flags |= RF_UNPAVED;
                else if (fl == "tunnel") e.tunnel = true;
            }
            if (e.lanesB == 0 || e.lanesF == 0) e.flags |= RF_ONEWAY;
            e.shape = rd.points(r, "shape");
            e.profile = rd.pairs(r, "profile");
            const int index = (int)rd.m.edges.size();
            rd.m.edges.push_back(e);
            readEdgeBlock(rd, r, rd.m.edges.back(), index);
        } else if (r.type == "junction") {
            Junction j;
            j.src = rd.at(r);
            j.node.text = r.id;
            j.node.src = j.src;
            j.kind = rd.text(r, "kind", "plain");
            j.turns = rd.text(r, "turns", "all");
            j.box = rd.points(r, "box");
            for (const Ntw::Record& c : r.children) {
                if (c.type == "stopline") {
                    Junction::StopLine x;
                    x.src = rd.at(c);
                    x.edge = rd.ref(c, "edge");
                    x.at = rd.num(c, "at");
                    j.stopLines.push_back(x);
                } else if (c.type == "connect" || c.type == "ban") {
                    // from=<edge>:<lane> (connect) or from=<edge> (ban)
                    auto split = [&](const char* key, Ref& edge, int* lane) {
                        edge = rd.ref(c, key);
                        const size_t colon = edge.text.find(':');
                        if (colon == std::string::npos) return;
                        long long l = 0;
                        if (lane && Ntw::toInt(edge.text.substr(colon + 1), &l)) *lane = (int)l;
                        edge.text = edge.text.substr(0, colon);
                    };
                    if (c.type == "connect") {
                        Junction::Connect x;
                        x.src = rd.at(c);
                        split("from", x.fromEdge, &x.fromLane);
                        split("to", x.toEdge, &x.toLane);
                        x.turn = rd.text(c, "turn");
                        x.isProtected = rd.integer(c, "protected") != 0;
                        j.connects.push_back(x);
                    } else {
                        Junction::Ban x;
                        x.src = rd.at(c);
                        split("from", x.fromEdge, nullptr);
                        split("to", x.toEdge, nullptr);
                        j.bans.push_back(x);
                    }
                } else if (c.type == "head") {
                    Junction::Head x;
                    x.src = rd.at(c);
                    if (const Ntw::Field* f = c.field("at")) Ntw::toPoint3(f->value, &x.at);
                    x.edge = rd.ref(c, "faces");
                    const size_t colon = x.edge.text.find(':');
                    if (colon != std::string::npos) {
                        x.dir = x.edge.text.substr(colon + 1) == "bwd" ? DIR_BWD : DIR_FWD;
                        x.edge.text = x.edge.text.substr(0, colon);
                    }
                    j.heads.push_back(x);
                }
            }
            rd.m.junctions.push_back(j);
        }
    }
}

void readSignal(Reader& rd, const Ntw::Document& doc) {
    SignalPlan p;
    const Ntw::Field* h = doc.headerField("id");
    p.id = h ? h->value : std::string();
    p.src = Src{rd.file, 1};
    for (const Ntw::Record& r : doc.records) {
        if (r.type == "plan") {
            p.cycle = rd.num(r, "cycle");
            p.legs = rd.integer(r, "legs");
        } else if (r.type == "phase") {
            Phase ph;
            ph.id = r.id;
            ph.src = rd.at(r);
            ph.green = rd.num(r, "green");
            ph.amber = rd.num(r, "amber");
            ph.allred = rd.num(r, "allred");
            ph.moves = rd.list(r, "moves");
            ph.walk = rd.list(r, "walk");
            ph.walkTime = rd.num(r, "walktime");
            ph.flash = rd.num(r, "flash");
            p.phases.push_back(ph);
        }
    }
    rd.m.signals.push_back(p);
}

void readTransit(Reader& rd, const Ntw::Document& doc) {
    TransitLine t;
    const Ntw::Field* h = doc.headerField("line");
    t.id = h ? h->value : std::string();
    t.src = Src{rd.file, 1};
    if (const Ntw::Field* k = doc.headerField("kind")) t.kind = k->value;
    if (const Ntw::Field* n = doc.headerField("name")) t.name = n->value;
    for (const Ntw::Record& r : doc.records) {
        if (r.type == "route") {
            t.routeEdges = rd.refs(r, "edges");
            t.routePts = rd.points(r, "pts");
            t.routeProfile = rd.pairs(r, "profile");
        } else if (r.type == "station") {
            TransitLine::Station s;
            s.id = r.id;
            s.src = rd.at(r);
            s.at = rd.point(r, "at");
            s.z = rd.num(r, "z");
            s.name = rd.text(r, "name");
            s.side = rd.text(r, "side");
            t.stations.push_back(s);
        } else if (r.type == "stop") {
            TransitLine::Stop s;
            s.id = r.id;
            s.src = rd.at(r);
            s.busStop = rd.ref(r, "busstop");
            t.stops.push_back(s);
        } else if (r.type == "pier") {
            TransitLine::Pier p;
            p.id = r.id;
            p.src = rd.at(r);
            p.at = rd.point(r, "at");
            p.yaw = rd.num(r, "yaw");
            p.name = rd.text(r, "name");
            t.piers.push_back(p);
        } else if (r.type == "pole") {
            TransitLine::Pole p;
            p.src = rd.at(r);
            p.at = rd.point(r, "at");
            p.h = rd.num(r, "h");
            t.poles.push_back(p);
        } else if (r.type == "service") {
            t.headway = rd.num(r, "headway");
            t.first = rd.num(r, "first");
            t.last = rd.num(r, "last");
        }
    }
    rd.m.transit.push_back(t);
}

void readPlaces(Reader& rd, const Ntw::Document& doc) {
    for (const Ntw::Record& r : doc.records) {
        if (r.type == "place") {
            Place p;
            p.id = r.id;
            p.src = rd.at(r);
            p.building = rd.ref(r, "building");
            p.door = rd.text(r, "door");
            p.walk = rd.point(r, "walk");
            if (const Ntw::Field* f = r.field("curb")) Ntw::toPoint3(f->value, &p.curb);
            p.dir = rd.point(r, "dir");
            p.water = rd.points(r, "water");
            p.note = rd.text(r, "note");
            rd.m.places.push_back(p);
        } else if (r.type == "anchor") {
            Anchor a;
            a.id = r.id;
            a.src = rd.at(r);
            a.kind = rd.text(r, "kind");
            a.event = rd.text(r, "event");
            a.at = rd.point(r, "at");
            a.z = rd.num(r, "z");
            a.yaw = rd.num(r, "yaw");
            a.poly = rd.points(r, "poly");
            a.line = rd.points(r, "line");
            a.building = rd.ref(r, "building");
            a.edge = rd.ref(r, "edge");
            a.door = rd.text(r, "door");
            a.cap = rd.integer(r, "cap");
            a.tags = rd.list(r, "tags");
            rd.m.anchors.push_back(a);
        }
    }
}

void readBlock(Reader& rd, const Ntw::Document& doc) {
    Block b;
    const Ntw::Field* h = doc.headerField("id");
    b.id = h ? h->value : std::string();
    b.src = Src{rd.file, 1};
    b.file = rd.file;
    if (const Ntw::Field* d = doc.headerField("district")) b.district = d->value;
    for (const Ntw::Record& r : doc.records) {
        if (r.type == "parcel") b.parcels.push_back(r.id);
        else if (r.type == "lot") b.lots.push_back(r.id);
    }
    rd.m.blocks.push_back(b);
}

void readBuilding(Reader& rd, const Ntw::Document& doc) {
    Building b;
    const Ntw::Field* h = doc.headerField("id");
    b.id = h ? h->value : std::string();
    b.src = Src{rd.file, 1};
    b.file = rd.file;
    if (const Ntw::Field* f = doc.headerField("block")) {
        b.block.text = f->value;
        b.block.src = Src{rd.file, 1};
    }
    if (const Ntw::Field* f = doc.headerField("parcel")) b.parcel = f->value;
    for (const Ntw::Record& r : doc.records) {
        if (r.type != "place") continue;
        b.plan = rd.ref(r, "plan");
        b.at = rd.point(r, "at");
        b.z = rd.num(r, "z");
        b.yaw = rd.num(r, "yaw");
        b.mirror = rd.integer(r, "mirror") != 0;
    }
    rd.m.buildings.push_back(b);
}

void readLibrary(Reader& rd, const Ntw::Document& doc) {
    if (doc.kind == "palette") {
        const Ntw::Field* fam = doc.headerField("family");
        for (size_t i = 0; i < doc.records.size(); i++) {
            const Ntw::Record& r = doc.records[i];
            if (r.type != "palette") continue;
            LibItem it;
            it.id = "lib.palette." + std::string(fam ? fam->value : "") + "." + r.id;
            it.kind = "palette";
            it.src = rd.at(r);
            it.file = rd.file;
            it.record = (int)i;
            rd.m.library.push_back(it);
        }
        return;
    }
    if (doc.kind == "catalog") {
        for (size_t i = 0; i < doc.records.size(); i++) {
            const Ntw::Record& r = doc.records[i];
            if (r.type != "piece") continue;
            LibItem it;
            it.id = r.id;
            it.kind = "piece";
            it.src = rd.at(r);
            it.file = rd.file;
            it.record = (int)i;
            rd.m.library.push_back(it);
        }
        return;
    }
    LibItem it;
    const Ntw::Field* h = doc.headerField("id");
    it.id = h ? h->value : std::string();
    it.kind = doc.kind;
    it.src = Src{rd.file, 1};
    it.file = rd.file;
    rd.m.library.push_back(it);
}

bool readText(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// The class a library id belongs to: lib.plan.house.x -> plan; catalog pieces by their category
std::string libClass(const LibItem& it) {
    if (it.kind != "piece") return it.kind;
    const std::string& id = it.id;   // lib.<category>.<name>
    const size_t a = id.find('.'), b = a == std::string::npos ? a : id.find('.', a + 1);
    return a == std::string::npos || b == std::string::npos ? std::string("piece") : id.substr(a + 1, b - a - 1);
}

void resolve(const Model& m, const char* cls, Ref& r) {
    if (!r.text.empty()) r.target = m.find(cls, r.text);
}

}  // namespace

// ------------------------------------------------------------------------------------------------------------ the model

int Model::find(const char* cls, const std::string& id) const {
    auto c = index.find(cls);
    if (c == index.end()) return -1;
    auto it = c->second.find(id);
    return it == c->second.end() ? -1 : it->second;
}

std::string Model::where(const Src& s) const {
    if (s.file < 0 || s.file >= (int)files.size()) return "(compiled)";
    return s.line > 0 ? files[(size_t)s.file].path + ":" + std::to_string(s.line) : files[(size_t)s.file].path;
}

void discover(const std::string& dir, std::vector<std::string>& paths) {
    std::error_code ec;
    paths.clear();
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir, ec))
        if (e.is_regular_file() && e.path().extension() == ".ntw") paths.push_back(e.path().generic_string());
    std::sort(paths.begin(), paths.end());
}

bool load(const std::string& dir, Model& out, std::vector<Error>& errors) {
    out = Model();
    const size_t errorsBefore = errors.size();
    std::vector<std::string> paths;
    discover(dir, paths);
    if (paths.empty()) errors.push_back(Error{Src{}, "no .ntw files under " + dir});
    for (const std::string& path : paths) {
        File f;
        f.path = path;
        std::string text;
        const int fi = (int)out.files.size();
        if (!readText(path, text)) {
            errors.push_back(Error{Src{fi, 0}, "cannot read " + path});
            continue;
        }
        std::vector<Ntw::Error> errs;
        const bool parsed = Ntw::parse(text.data(), text.size(), Ntw::schemaOpensBlock, f.doc, errs);
        if (parsed) Ntw::checkDocument(f.doc, errs);
        f.kind = f.doc.kind;
        out.files.push_back(std::move(f));
        for (const Ntw::Error& e : errs) errors.push_back(Error{Src{fi, e.line}, e.message});
        if (!errs.empty()) continue;   // (its records are left out: a file with errors would only add noise)
        Reader rd{out, fi, errors};
        const Ntw::Document& doc = out.files[(size_t)fi].doc;
        const std::string& k = doc.kind;
        if (k == "world") readWorld(rd, doc);
        else if (k == "district") readDistrict(rd, doc);
        else if (k == "roads") readRoads(rd, doc);
        else if (k == "signal") readSignal(rd, doc);
        else if (k == "transit") readTransit(rd, doc);
        else if (k == "places") readPlaces(rd, doc);
        else if (k == "block") readBlock(rd, doc);
        else if (k == "building") readBuilding(rd, doc);
        else if (k == "plan" || k == "layout" || k == "unit" || k == "fitout" || k == "set" || k == "facade" || k == "palette" || k == "catalog")
            readLibrary(rd, doc);
        // (terrain is worldc's: compileTerrain reads its document)
    }
    // the ids, by class (a second definition keeps the first: worldv reports the clash)
    auto add = [&](const char* cls, const std::string& id, int i) {
        if (!id.empty()) out.index[cls].insert(std::make_pair(id, i));
    };
    for (size_t i = 0; i < out.districts.size(); i++) add("district", out.districts[i].id, (int)i);
    for (size_t i = 0; i < out.nodes.size(); i++) add("node", out.nodes[i].id, (int)i);
    for (size_t i = 0; i < out.edges.size(); i++) add("edge", out.edges[i].id, (int)i);
    for (size_t i = 0; i < out.busStops.size(); i++) add("busstop", out.busStops[i].id, (int)i);
    for (size_t i = 0; i < out.signals.size(); i++) add("signal", out.signals[i].id, (int)i);
    for (size_t i = 0; i < out.transit.size(); i++) add("line", out.transit[i].id, (int)i);
    for (size_t i = 0; i < out.places.size(); i++) add("place", out.places[i].id, (int)i);
    for (size_t i = 0; i < out.anchors.size(); i++) add("anchor", out.anchors[i].id, (int)i);
    for (size_t i = 0; i < out.blocks.size(); i++) add("block", out.blocks[i].id, (int)i);
    for (size_t i = 0; i < out.buildings.size(); i++) add("building", out.buildings[i].id, (int)i);
    for (size_t i = 0; i < out.library.size(); i++) add(libClass(out.library[i]).c_str(), out.library[i].id, (int)i);
    // the references the model follows itself
    for (District& d : out.districts)
        for (Ref& r : d.trunkNodes) resolve(out, "node", r);
    for (Node& n : out.nodes) {
        resolve(out, "signal", n.plan);
        for (auto& a : n.approaches) resolve(out, "edge", a.second);
        for (Ref& r : n.stops) resolve(out, "edge", r);
        for (Ref& r : n.yields) resolve(out, "edge", r);
    }
    for (Edge& e : out.edges) {
        resolve(out, "node", e.from);
        resolve(out, "node", e.to);
    }
    for (BusStop& b : out.busStops)
        for (Ref& r : b.routes) resolve(out, "line", r);
    for (Junction& j : out.junctions) {
        resolve(out, "node", j.node);
        for (auto& x : j.stopLines) resolve(out, "edge", x.edge);
        for (auto& x : j.connects) {
            resolve(out, "edge", x.fromEdge);
            resolve(out, "edge", x.toEdge);
        }
        for (auto& x : j.bans) {
            resolve(out, "edge", x.fromEdge);
            resolve(out, "edge", x.toEdge);
        }
        for (auto& x : j.heads) resolve(out, "edge", x.edge);
    }
    for (TransitLine& t : out.transit) {
        for (Ref& r : t.routeEdges) resolve(out, "edge", r);
        for (auto& s : t.stops) resolve(out, "busstop", s.busStop);
    }
    for (Place& p : out.places) resolve(out, "building", p.building);
    for (Anchor& a : out.anchors) {
        resolve(out, "building", a.building);
        resolve(out, "edge", a.edge);
    }
    for (Building& b : out.buildings) {
        resolve(out, "block", b.block);
        resolve(out, "plan", b.plan);
    }
    return errors.size() == errorsBefore;
}

// ------------------------------------------------------------------------------------------------------- roads compile

bool compileRoads(const Model& model, RoadNetwork& out, RoadIndex& index, std::vector<Error>& errors) {
    const size_t errorsBefore = errors.size();
    out.nodes.clear();
    out.edges.clear();
    out.beachEnds.clear();
    index = RoadIndex();
    index.roadNode.assign(model.nodes.size(), -1);
    index.roadEdge.assign(model.edges.size(), -1);
    for (size_t i = 0; i < model.nodes.size(); i++) {
        RoadNode n;
        n.p = model.nodes[i].at;
        n.z = model.nodes[i].z;
        index.roadNode[i] = (int)out.nodes.size();
        index.nodeOf.push_back((int)i);
        out.nodes.push_back(n);
    }
    for (size_t i = 0; i < model.edges.size(); i++) {
        const Edge& m = model.edges[i];
        if (m.from.target < 0 || m.to.target < 0) {
            errors.push_back(Error{m.src, "edge " + m.id + ": its " + (m.from.target < 0 ? "from=" + m.from.text : "to=" + m.to.text) + " names no node"});
            continue;
        }
        const Node &a = model.nodes[(size_t)m.from.target], &b = model.nodes[(size_t)m.to.target];
        if (m.shape.size() < 2 || length(m.shape.front() - a.at) > 0.05f || length(m.shape.back() - b.at) > 0.05f) {
            errors.push_back(Error{m.src, "edge " + m.id + ": its shape must run from its from node to its to node"});
            continue;
        }
        RoadEdge e;
        e.n0 = index.roadNode[(size_t)m.from.target];
        e.n1 = index.roadNode[(size_t)m.to.target];
        e.cls = m.cls;
        e.flags = m.flags;
        e.name = m.name;
        e.lanesF = (u8)m.lanesF;
        e.lanesB = (u8)m.lanesB;
        const RoadClassInfo& ri = roadInfo(m.cls);
        const float laneW = m.laneWidth > 0.f ? m.laneWidth : ri.laneWidth;
        const float shoulder = Max(Max(m.shoulderL, m.shoulderR), Max(m.parkingL, m.parkingR));
        e.halfWidth = laneW * (m.lanesF + m.lanesB) * 0.5f + m.median * 0.5f + (shoulder >= 0.f ? shoulder : ri.shoulder);
        const float walk = Max(m.sidewalkL, m.sidewalkR);
        e.sidewalk = walk >= 0.f ? walk : ri.sidewalk;
        if (e.sidewalk <= 0.f) e.flags |= RF_NOSIDEWALK;
        e.seed = hash32((u32)out.edges.size() * 2654435761u + 17u);
        // heights: the profile where given, else straight from node to node
        float L = 0.f;
        for (size_t k = 0; k < m.shape.size(); k++) {
            if (k) L += length(m.shape[k] - m.shape[k - 1]);
            float z = 0.f;
            if (m.profile.size() >= 2) {
                size_t j = 1;
                while (j + 1 < m.profile.size() && m.profile[j].x < L) j++;
                const vec2 p0 = m.profile[j - 1], p1 = m.profile[j];
                z = p1.x > p0.x ? Lerp(p0.y, p1.y, Clamp((L - p0.x) / (p1.x - p0.x), 0.f, 1.f)) : p0.y;
            } else {
                z = 0.f;   // (set below, once the length is known)
            }
            e.pts.push_back(vec3(m.shape[k], z));
        }
        if (m.profile.size() < 2) {
            float acc = 0.f;
            for (size_t k = 0; k < e.pts.size(); k++) {
                if (k) acc += length(e.pts[k].xy() - e.pts[k - 1].xy());
                e.pts[k].z = Lerp(a.z, b.z, L > 0.f ? acc / L : 0.f);
            }
        }
        e.dist.resize(e.pts.size());
        e.dist[0] = 0.f;
        for (size_t k = 1; k < e.pts.size(); k++) e.dist[k] = e.dist[k - 1] + length(e.pts[k] - e.pts[k - 1]);
        e.length = e.dist.back();
        index.roadEdge[i] = (int)out.edges.size();
        index.edgeOf.push_back((int)i);
        out.edges.push_back(e);
    }
    for (RoadNode& n : out.nodes) n.edges.clear();
    for (size_t i = 0; i < out.edges.size(); i++) {
        out.nodes[(size_t)out.edges[i].n0].edges.push_back((int)i);
        out.nodes[(size_t)out.edges[i].n1].edges.push_back((int)i);
    }
    // junction radii, cut-backs and control, as generate() derives them; an authored control wins
    for (size_t n = 0; n < out.nodes.size(); n++) {
        RoadNode& nd = out.nodes[n];
        float r = 0.f;
        int big = 0;
        const int count = (int)nd.edges.size();
        bool hasHighway = false;
        for (int ei : nd.edges) {
            const RoadEdge& e = out.edges[(size_t)ei];
            r = Max(r, e.halfWidth + e.sidewalk * 0.3f);
            if (e.cls <= RC_AVENUE) big++;
            hasHighway = hasHighway || e.cls == RC_HIGHWAY || e.cls == RC_RAMP;
        }
        nd.radius = count >= 3 ? r + 2.f : 0.f;
        nd.highway = hasHighway;
        nd.control = 0;
        if (count >= 3 && !hasHighway) nd.control = (big >= 2) ? 2 : (big == 1 ? 1 : (count >= 4 ? 1 : 0));
        const int mi = index.nodeOf[n];
        if (mi >= 0) {
            const Node& m = model.nodes[(size_t)mi];
            if (m.control == Node::CTL_SIGNAL) nd.control = 2;
            else if (m.control == Node::CTL_STOP || m.control == Node::CTL_YIELD) nd.control = 1;
            else if (m.control == Node::CTL_NONE && count >= 3) nd.control = 0;
        }
    }
    for (RoadEdge& e : out.edges) {
        e.cut0 = Min(out.nodes[(size_t)e.n0].radius, e.length * 0.45f);
        e.cut1 = Min(out.nodes[(size_t)e.n1].radius, e.length * 0.45f);
    }
    return errors.size() == errorsBefore;
}

}  // namespace Hand
}  // namespace World
