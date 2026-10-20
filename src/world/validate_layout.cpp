// worldv's checks inside one layout, unit or fit-out (validate_layout.h). Everything is in the item's own frame
// (docs/NTW_FORMAT.md section 4):
// - walls are their centre lines, from a to b, t thick;
// - room polygons are drawn to the walls' centre lines;
// - a door's or window's `at` is the centre of its opening along its wall, measured from a, and `w` its clear width;
// - a stair's `run` is its flight's centre line, from the bottom: 2 points for straight flights and escalators, 4 for a
//   dogleg and 3 for a quarter landing (the middle points at the landing's mid-depth), 3 for winders (the middle one at
//   the turn), and points on the arc for spirals and curved stairs.
#include "validate_layout.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>

namespace World {
namespace Validate {

namespace layout_detail {

const double kTol = 0.02;        // closer than this is touching: on a line, covered, at a corner
const double kFrame = 0.05;      // an opening's frame, past its clear width at each end
const double kPiD = 3.14159265358979323846;

struct V {
    double x = 0.0, y = 0.0;
};
V operator+(V a, V b) { return {a.x + b.x, a.y + b.y}; }
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y}; }
V operator*(V a, double s) { return {a.x * s, a.y * s}; }
double vdot(V a, V b) { return a.x * b.x + a.y * b.y; }
double vcross(V a, V b) { return a.x * b.y - a.y * b.x; }
double vlen(V a) { return std::sqrt(vdot(a, a)); }
V vunit(V a) {
    const double l = vlen(a);
    return l > 0.0 ? a * (1.0 / l) : V();
}
V vperp(V a) { return {-a.y, a.x}; }   // turned 90 degrees counter-clockwise

std::string fmt(double v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}
std::string pt(V p) { return "(" + fmt(p.x) + "," + fmt(p.y) + ")"; }
std::string fmt3(double v) {   // (measures checked against a limit to the centimetre: 0.247 isn't 0.25)
    char buf[32];
    snprintf(buf, sizeof(buf), "%.3f", v);
    return buf;
}

// The distance from p to the segment ab
double segDist(V p, V a, V b) {
    const V ab = b - a;
    const double l2 = vdot(ab, ab);
    const double s = l2 > 0.0 ? std::max(0.0, std::min(1.0, vdot(p - a, ab) / l2)) : 0.0;
    return vlen(p - (a + ab * s));
}

// Where p lies against a polygon: 1 inside, -1 outside, 0 within kTol of its edges
int where(V p, const std::vector<V>& poly) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const V a = poly[j], b = poly[i];
        if (segDist(p, a, b) <= kTol) return 0;
        if ((a.y > p.y) != (b.y > p.y) && p.x < a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y)) in = !in;
    }
    return in ? 1 : -1;
}

double area(const std::vector<V>& poly) {   // signed: positive counter-clockwise
    double s = 0.0;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) s += vcross(poly[j], poly[i]);
    return s * 0.5;
}

V centroid(const std::vector<V>& poly) {
    const double a = area(poly);
    V c;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const double k = vcross(poly[j], poly[i]);
        c = c + (poly[j] + poly[i]) * k;
    }
    return a != 0.0 ? c * (1.0 / (6.0 * a)) : poly[0];
}

// Whether the segments ab and cd cross at a point inside both and more than kTol from all four ends
bool crosses(V a, V b, V c, V d, V* at) {
    const V r = b - a, s = d - c;
    const double den = vcross(r, s);
    if (std::fabs(den) < 1e-12) return false;
    const double t = vcross(c - a, s) / den, u = vcross(c - a, r) / den;
    if (t <= 0.0 || t >= 1.0 || u <= 0.0 || u >= 1.0) return false;
    const V x = a + r * t;
    if (vlen(x - a) <= kTol || vlen(x - b) <= kTol || vlen(x - c) <= kTol || vlen(x - d) <= kTol) return false;
    if (at) *at = x;
    return true;
}

// The parameter along the ray o + d * l (l > 0) where it meets the segment ab, or a negative number
double rayHit(V o, V d, V a, V b) {
    const V s = b - a;
    const double den = vcross(d, s);
    if (std::fabs(den) < 1e-12) return -1.0;
    const double l = vcross(a - o, s) / den, u = vcross(a - o, d) / den;
    return u >= -1e-9 && u <= 1.0 + 1e-9 && l > 1e-6 ? l : -1.0;
}

// A circle through points, by least squares (the algebraic fit); false when they lie on a line
bool fitCircle(const std::vector<V>& ps, V* centre, double* radius) {
    V m;
    for (const V& p : ps) m = m + p;
    m = m * (1.0 / (double)ps.size());
    double suu = 0, suv = 0, svv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
    for (const V& p : ps) {
        const double u = p.x - m.x, v = p.y - m.y;
        suu += u * u;
        suv += u * v;
        svv += v * v;
        suuu += u * u * u;
        svvv += v * v * v;
        suvv += u * v * v;
        svuu += v * u * u;
    }
    const double det = suu * svv - suv * suv;
    if (std::fabs(det) < 1e-12) return false;
    const double bu = 0.5 * (suuu + suvv), bv = 0.5 * (svvv + svuu);
    const double uc = (bu * svv - bv * suv) / det, vc = (suu * bv - suv * bu) / det;
    *centre = {m.x + uc, m.y + vc};
    *radius = std::sqrt(uc * uc + vc * vc + (suu + svv) / (double)ps.size());
    return true;
}

bool numberOf(const std::string& s, double* out) { return Ntw::toNumber(s, out); }

std::vector<V> pointsOf(const std::string& v) {
    std::vector<V> out;
    size_t from = 0;
    for (size_t i = 0; i <= v.size(); i++) {
        if (i < v.size() && v[i] != ';') continue;
        const std::string p = v.substr(from, i - from);
        const size_t comma = p.find(',');
        V q;
        if (comma == std::string::npos || !numberOf(p.substr(0, comma), &q.x) || !numberOf(p.substr(comma + 1), &q.y)) return std::vector<V>();
        out.push_back(q);
        from = i + 1;
    }
    return out;
}

double field(const Ntw::Record& r, const char* key, double fallback) {
    double v = fallback;
    if (const Ntw::Field* f = r.field(key)) numberOf(f->value, &v);
    return v;
}

struct Wall {
    const Ntw::Record* rec;
    V a, b, dir;
    double t = 0.0, length = 0.0;
    bool ext = false;
};
struct Room {
    const Ntw::Record* rec;
    std::vector<V> poly;
    bool proper = true;   // a polygon with an area that doesn't cross itself (rooms without one: room.closed says so)
};
struct Opening {
    const Ntw::Record* rec;
    const Wall* wall = nullptr;
    double at = 0.0, w = 0.0;
    double from() const { return at - w * 0.5; }
    double to() const { return at + w * 0.5; }
    V centre() const { return wall->a + wall->dir * at; }
};
struct Stair {
    const Ntw::Record* rec;
    std::string kind;
    std::vector<V> run;
    double w = 0.0;
    bool arc = false;    // spiral and curved: run is an arc
    V centre;
    double radius = 0.0;
    double walkLength = -1.0;   // the walking line's length on the flights; negative: unknown (a bad run, an escalator)
    bool straightFlights = false;
};

struct Item {
    const Ntw::Document& doc;
    std::vector<Diagnostic>* out;
    bool unit = false;            // a unit or fit-out: it fills a slot of slot_size
    double slotW = 0.0, slotD = 0.0;
    std::vector<Wall> walls;
    std::vector<Room> rooms;
    std::vector<Room> slots;
    std::vector<Opening> doors, windows;
    std::vector<Stair> stairs;
    std::vector<const Ntw::Record*> lifts;

    Item(const Ntw::Document& d, std::vector<Diagnostic>* o) : doc(d), out(o) {}

    void report(int line, Severity sev, const char* check, const std::string& msg) {
        Diagnostic d;
        d.line = line;
        d.severity = sev;
        d.check = check;
        d.message = msg;
        out->push_back(d);
    }
    void error(const Ntw::Record& r, const char* check, const std::string& msg) { report(r.line, SEV_ERROR, check, name(r) + ": " + msg); }
    void warning(const Ntw::Record& r, const char* check, const std::string& msg) { report(r.line, SEV_WARNING, check, name(r) + ": " + msg); }
    static std::string name(const Ntw::Record& r) { return r.type + (r.id.empty() ? std::string() : " " + r.id); }

    const Wall* wall(const std::string& id) const {
        for (const Wall& w : walls)
            if (w.rec->id == id) return &w;
        return nullptr;
    }
    const Room* room(const std::string& id) const {
        for (const Room& r : rooms)
            if (r.rec->id == id) return &r;
        return nullptr;
    }
    // A name a door's leads= may give: a room, a slot, or the outside
    bool known(const std::string& id) const {
        if (id == "out" || room(id)) return true;
        for (const Room& s : slots)
            if (s.rec->id == id) return true;
        return false;
    }

    void read() {
        unit = doc.kind != "layout";
        if (const Ntw::Field* f = doc.headerField("slot_size")) {
            const size_t x = f->value.find('x');
            if (x != std::string::npos) {
                numberOf(f->value.substr(0, x), &slotW);
                numberOf(f->value.substr(x + 1), &slotD);
            }
        }
        for (const Ntw::Record& r : doc.records) {
            if (r.type == "wall") {
                Wall w;
                w.rec = &r;
                const std::vector<V> a = pointsOf(r.value("a", "")), b = pointsOf(r.value("b", ""));
                if (a.size() != 1 || b.size() != 1) continue;
                w.a = a[0];
                w.b = b[0];
                w.length = vlen(w.b - w.a);
                w.dir = vunit(w.b - w.a);
                w.t = field(r, "t", 0.0);
                w.ext = r.has("ext");
                if (w.length <= kTol) {
                    error(r, "wall.cross", "a wall of no length");
                    continue;
                }
                walls.push_back(w);
            } else if (r.type == "room" || r.type == "slot") {
                Room rm;
                rm.rec = &r;
                rm.poly = pointsOf(r.value("poly", ""));
                (r.type == "room" ? rooms : slots).push_back(rm);
            } else if (r.type == "stair") {
                Stair s;
                s.rec = &r;
                s.kind = r.value("kind", "");
                s.run = pointsOf(r.value("run", ""));
                s.w = field(r, "w", 0.0);
                stairs.push_back(s);
            } else if (r.type == "lift") {
                lifts.push_back(&r);
            }
        }
        // (after the walls: openings point into the vector)
        for (const Ntw::Record& r : doc.records) {
            if (r.type != "door" && r.type != "window") continue;
            Opening o;
            o.rec = &r;
            o.wall = wall(r.value("wall", ""));
            if (!o.wall) continue;   // (the reference check reports it)
            o.at = field(r, "at", 0.0);
            o.w = field(r, "w", 0.0);
            (r.type == "door" ? doors : windows).push_back(o);
        }
    }

    // ------------------------------------------------------------------------------------------------ walls

    void wallsCross() {
        for (size_t i = 0; i < walls.size(); i++)
            for (size_t j = 0; j < i; j++) {
                const Wall& a = walls[j];
                const Wall& b = walls[i];
                V x;
                if (std::fabs(vcross(a.dir, b.dir)) < 0.02) {
                    if (std::fabs(vcross(a.dir, b.a - a.a)) > kTol) continue;   // parallel, apart
                    const double s0 = vdot(b.a - a.a, a.dir), s1 = vdot(b.b - a.a, a.dir);
                    const double over = std::min(a.length, std::max(s0, s1)) - std::max(0.0, std::min(s0, s1));
                    if (over > kTol) error(*b.rec, "wall.cross", "lies along " + a.rec->id + " for " + fmt(over) + " m");
                } else if (crosses(a.a, a.b, b.a, b.b, &x)) {
                    error(*b.rec, "wall.cross", "crosses " + a.rec->id + " at " + pt(x));
                }
            }
    }

    // The thickest wall that touches p, other than `self`
    double thickestAt(V p, const Wall* self) const {
        double t = 0.0;
        for (const Wall& w : walls)
            if (&w != self && segDist(p, w.a, w.b) <= kTol) t = std::max(t, w.t);
        return t;
    }

    // ------------------------------------------------------------------------------------------------ rooms

    // The parts of the edge pq that walls along it cover, as distances from p
    std::vector<std::pair<double, double>> covered(V p, V q) const {
        const V d = vunit(q - p);
        const double l = vlen(q - p);
        std::vector<std::pair<double, double>> parts;
        for (const Wall& w : walls) {
            if (std::fabs(vcross(d, w.dir)) > 0.02 || std::fabs(vcross(d, w.a - p)) > kTol || std::fabs(vcross(d, w.b - p)) > kTol) continue;
            const double s0 = vdot(w.a - p, d), s1 = vdot(w.b - p, d);
            const double lo = std::max(0.0, std::min(s0, s1)), hi = std::min(l, std::max(s0, s1));
            if (hi > lo) parts.push_back(std::make_pair(lo, hi));
        }
        // a unit's or fit-out's edges on its slot's boundary: the layout's walls close them
        const auto onSide = [this](V a, V b) {
            return (std::fabs(a.x) <= kTol && std::fabs(b.x) <= kTol) || (std::fabs(a.y) <= kTol && std::fabs(b.y) <= kTol) ||
                   (std::fabs(a.x - slotW) <= kTol && std::fabs(b.x - slotW) <= kTol) || (std::fabs(a.y - slotD) <= kTol && std::fabs(b.y - slotD) <= kTol);
        };
        if (unit && slotW > 0.0 && onSide(p, q)) parts.push_back(std::make_pair(0.0, l));
        std::sort(parts.begin(), parts.end());
        return parts;
    }

    bool selfCrossing(const std::vector<V>& poly) const {
        const size_t n = poly.size();
        for (size_t i = 0; i < n; i++)
            for (size_t j = i + 2; j < n; j++) {
                if (i == 0 && j == n - 1) continue;   // (neighbours)
                if (crosses(poly[i], poly[(i + 1) % n], poly[j], poly[(j + 1) % n], nullptr)) return true;
            }
        return false;
    }

    void roomsClosed() {
        for (Room& r : rooms) {
            const std::vector<V>& poly = r.poly;
            r.proper = false;
            if (poly.size() < 3 || std::fabs(area(poly)) < kTol * kTol) {
                error(*r.rec, "room.closed", "its polygon has no area");
                continue;
            }
            if (selfCrossing(poly)) {
                error(*r.rec, "room.closed", "its polygon crosses itself");
                continue;
            }
            r.proper = true;
            if (field(*r.rec, "outdoor", 0.0) != 0.0) continue;   // balconies, porches and terraces are open
            for (size_t i = 0; i < poly.size(); i++) {
                const V p = poly[i], q = poly[(i + 1) % poly.size()];
                const double l = vlen(q - p);
                if (l <= kTol) continue;
                double reach = 0.0;   // covered from p up to here
                for (const std::pair<double, double>& part : covered(p, q)) {
                    if (part.first > reach + kTol) break;
                    reach = std::max(reach, part.second);
                }
                if (reach < l - kTol) {
                    double end = l;
                    for (const std::pair<double, double>& part : covered(p, q))
                        if (part.first > reach + kTol) {
                            end = part.first;
                            break;
                        }
                    const V d = vunit(q - p);
                    error(*r.rec, "room.closed", "no wall closes its edge from " + pt(p + d * reach) + " to " + pt(p + d * end));
                    break;
                }
            }
        }
    }

    void roomsOverlap() {
        for (size_t i = 0; i < rooms.size(); i++)
            for (size_t j = 0; j < i; j++) {
                const std::vector<V>& a = rooms[j].poly;
                const std::vector<V>& b = rooms[i].poly;
                if (!rooms[i].proper || !rooms[j].proper) continue;
                bool over = where(centroid(a), b) == 1 || where(centroid(b), a) == 1;
                for (const V& p : a) over = over || where(p, b) == 1;
                for (const V& p : b) over = over || where(p, a) == 1;
                for (size_t k = 0; k < a.size() && !over; k++)
                    for (size_t m = 0; m < b.size() && !over; m++) over = crosses(a[k], a[(k + 1) % a.size()], b[m], b[(m + 1) % b.size()], nullptr);
                if (over) error(*rooms[i].rec, "room.overlap", "overlaps room " + rooms[j].rec->id);
            }
    }

    // ------------------------------------------------------------------------------------- doors and windows

    void openingsFit() {
        for (const Wall& w : walls) {
            std::vector<const Opening*> on;
            for (const std::vector<Opening>* list : {&doors, &windows})
                for (const Opening& o : *list)
                    if (o.wall == &w) on.push_back(&o);
            if (on.empty()) continue;
            const double lo = thickestAt(w.a, &w) * 0.5 + kFrame, hi = w.length - thickestAt(w.b, &w) * 0.5 - kFrame;
            // walls that end on this one's length: an opening can't cut through them
            std::vector<std::pair<double, const Wall*>> joints;
            for (const Wall& x : walls) {
                if (&x == &w) continue;
                for (const V& e : {x.a, x.b}) {
                    const double s = vdot(e - w.a, w.dir);
                    if (s > kTol && s < w.length - kTol && segDist(e, w.a, w.b) <= kTol) joints.push_back(std::make_pair(s, &x));
                }
            }
            std::sort(on.begin(), on.end(), [](const Opening* a, const Opening* b) { return a->from() < b->from(); });
            for (size_t i = 0; i < on.size(); i++) {
                const Opening& o = *on[i];
                const char* check = o.rec->type == "door" ? "door.wall" : "window.wall";
                const std::string span = fmt(o.from()) + "-" + fmt(o.to()) + " m along " + w.rec->id;
                if (o.from() < lo - 1e-9 || o.to() > hi + 1e-9)
                    error(*o.rec, check, "its opening runs past the end of " + w.rec->id + " (" + span + "; the frames leave " + fmt(lo) + "-" + fmt(hi) + " m)");
                for (const std::pair<double, const Wall*>& j : joints) {
                    const double half = j.second->t * 0.5 + kFrame;
                    if (o.from() < j.first + half && o.to() > j.first - half) error(*o.rec, check, "its opening (" + span + ") cuts through " + j.second->rec->id + ", which meets the wall at " + fmt(j.first) + " m");
                }
                if (i > 0 && o.from() < on[i - 1]->to() - 1e-9) error(*o.rec, check, "its opening (" + span + ") overlaps " + Item::name(*on[i - 1]->rec) + "'s");
            }
        }
    }

    // What lies at p: a room's id, a slot's id, or "" (nothing)
    std::string at(V p) const {
        for (const Room& r : rooms)
            if (r.poly.size() >= 3 && where(p, r.poly) == 1) return r.rec->id;
        for (const Room& s : slots)
            if (s.poly.size() >= 3 && where(p, s.poly) == 1) return s.rec->id;
        return std::string();
    }

    // Whether the empty side at p of an opening on wall w is the outside: a layout's exterior wall, or for a unit or
    // fit-out the outside of its slot, through a wall on the slot's edge
    bool outside(V p, const Wall& w) const {
        if (!unit) return w.ext;
        if (slotW <= 0.0) return false;
        const bool inSlot = p.x > kTol && p.y > kTol && p.x < slotW - kTol && p.y < slotD - kTol;
        const auto onEdge = [this](V q) { return std::fabs(q.x) <= kTol || std::fabs(q.y) <= kTol || std::fabs(q.x - slotW) <= kTol || std::fabs(q.y - slotD) <= kTol; };
        return !inSlot && onEdge(w.a) && onEdge(w.b);
    }

    // The two sides of an opening: what lies just across its wall each way ("out" outside, "" a gap)
    std::pair<std::string, std::string> sides(const Opening& o) const {
        const Wall& w = *o.wall;
        const V n = vperp(w.dir) * (w.t * 0.5 + kFrame);
        const V c = o.centre();
        std::string s[2];
        for (int k = 0; k < 2; k++) {
            const V p = k ? c - n : c + n;
            s[k] = at(p);
            if (s[k].empty() && outside(p, w)) s[k] = "out";
        }
        return std::make_pair(s[0], s[1]);
    }

    void doorsJoin() {
        for (const Opening& o : doors) {
            const std::pair<std::string, std::string> s = sides(o);
            const std::string leads = o.rec->value("leads", "");
            if (s.first.empty() || s.second.empty()) {
                error(*o.rec, "door.rooms", "one side of it lies in no room" + std::string(unit ? ", inside its slot" : ", and " + o.wall->rec->id + " isn't an exterior wall"));
                continue;
            }
            if (s.first == s.second) {
                error(*o.rec, "door.rooms", "it joins " + s.first + " to itself");
                continue;
            }
            const size_t colon = leads.find(':');
            const std::string l0 = leads.substr(0, colon), l1 = colon == std::string::npos ? std::string() : leads.substr(colon + 1);
            if (!known(l0) || !known(l1)) continue;   // (the reference check reports it)
            if (!((l0 == s.first && l1 == s.second) || (l0 == s.second && l1 == s.first)))
                error(*o.rec, "door.rooms", "leads=" + leads + ", but its sides are " + s.first + " and " + s.second);
        }
        for (const Opening& o : windows) {
            if (unit) {
                error(*o.rec, "window.wall", "windows belong to the layout, on its exterior walls, not to a " + doc.kind);
                continue;
            }
            if (!o.wall->ext) {
                error(*o.rec, "window.wall", o.wall->rec->id + " isn't an exterior wall");
                continue;
            }
            const std::pair<std::string, std::string> s = sides(o);
            const std::string inside = s.first == "out" ? s.second : s.first;
            const std::string named = o.rec->value("room", "");
            if (!room(named)) continue;   // (the reference check reports it)
            if (inside != named) error(*o.rec, "window.room", "room=" + named + ", but the room behind it is " + (inside.empty() || inside == "out" ? std::string("none") : inside));
        }
    }

    // The free depth behind an opening on one side: from the wall's face along its normal to the next wall (or edge
    // of the room on that side)
    double freeDepth(const Opening& o, double sideSign, const std::string& roomId) const {
        const Room* r = room(roomId);
        if (!r || r->poly.size() < 3) return 1e9;
        const Wall& w = *o.wall;
        const V n = vperp(w.dir) * sideSign;
        const V start = o.centre() + n * (w.t * 0.5);
        double best = 1e9;
        V hit;
        for (size_t i = 0; i < r->poly.size(); i++) {
            const double l = rayHit(start, n, r->poly[i], r->poly[(i + 1) % r->poly.size()]);
            if (l > 0.0 && l < best) {
                best = l;
                hit = start + n * l;
            }
        }
        return best < 1e9 ? best - thickestAt(hit, &w) * 0.5 : best;
    }

    void doorWidths() {
        for (const Opening& o : doors) {
            if (o.w < 0.8 - 1e-9) {
                error(*o.rec, "door.width", fmt(o.w) + " m clear, under the kit's 0.8 m");
                continue;
            }
            if (o.w >= 0.9 - 1e-9) continue;
            const std::pair<std::string, std::string> s = sides(o);
            for (int k = 0; k < 2; k++) {
                const std::string& side = k ? s.second : s.first;
                const double depth = freeDepth(o, k ? -1.0 : 1.0, side);
                if (depth < 1.2 - 1e-9) {
                    warning(*o.rec, "door.width", fmt(o.w) + " m clear with a turn right behind it (" + fmt(depth) + " m free on the " + side +
                                                      " side): the kit wants 0.9 m there");
                    break;
                }
            }
        }
    }

    // A flight end's nosing line, w wide: square to the flight, or along the radius on spirals and curved stairs
    void nosing(const Stair& s, bool top, V* q0, V* q1) const {
        const V e = top ? s.run.back() : s.run.front();
        V across;
        if (s.arc) {
            const V u = vunit(e - s.centre);
            *q0 = s.centre + u * (s.radius - s.w * 0.5);
            *q1 = s.centre + u * (s.radius + s.w * 0.5);
            return;
        }
        const V d = top ? vunit(s.run.back() - s.run[s.run.size() - 2]) : vunit(s.run[1] - s.run[0]);
        across = vperp(d) * (s.w * 0.5);
        *q0 = e - across;
        *q1 = e + across;
    }

    void doorLandings() {
        for (const Opening& o : doors) {
            const Wall& w = *o.wall;
            const V n = vperp(w.dir);
            const double lo = o.from() - 0.3, hi = o.to() + 0.3;
            for (const Stair& s : stairs) {
                if (s.kind == "escalator" || s.run.size() < 2 || (s.arc && s.radius <= 0.0)) continue;
                for (int top = 0; top < 2; top++) {
                    V q0, q1;
                    nosing(s, top != 0, &q0, &q1);
                    // the part of the nosing line in front of the door: along the wall within its width and 0.3 m
                    const double s0 = vdot(q0 - w.a, w.dir), s1 = vdot(q1 - w.a, w.dir);
                    double l0 = 0.0, l1 = 1.0;
                    if (std::fabs(s1 - s0) > 1e-9) {
                        const double a = (lo - s0) / (s1 - s0), b = (hi - s0) / (s1 - s0);
                        l0 = std::max(0.0, std::min(a, b));
                        l1 = std::min(1.0, std::max(a, b));
                    } else if (s0 < lo || s0 > hi) {
                        continue;
                    }
                    if (l0 > l1) continue;
                    const double d0 = vdot(q0 + (q1 - q0) * l0 - w.a, n), d1 = vdot(q0 + (q1 - q0) * l1 - w.a, n);
                    const double dist = (d0 > 0.0) != (d1 > 0.0) ? 0.0 : std::min(std::fabs(d0), std::fabs(d1));
                    const double clear = dist - w.t * 0.5;
                    if (clear < 0.9 - 1e-9)
                        error(*o.rec, "door.landing", "the " + std::string(top ? "top" : "bottom") + " of stair " + s.rec->id + " is " + fmt3(std::max(0.0, clear)) +
                                                          " m in front of it: a level landing of 0.9 m or more");
                }
            }
        }
    }

    // ------------------------------------------------------------------------------------------------ corridors

    // The thickness of the walls along a room's edge (the thickest of them)
    double edgeWall(V p, V q) const {
        double t = 0.0;
        const V d = vunit(q - p);
        const double l = vlen(q - p);
        for (const Wall& w : walls) {
            if (std::fabs(vcross(d, w.dir)) > 0.02 || std::fabs(vcross(d, w.a - p)) > kTol || std::fabs(vcross(d, w.b - p)) > kTol) continue;
            const double s0 = vdot(w.a - p, d), s1 = vdot(w.b - p, d);
            if (std::min(l, std::max(s0, s1)) - std::max(0.0, std::min(s0, s1)) > kTol) t = std::max(t, w.t);
        }
        return t;
    }

    void corridors() {
        for (const Room& r : rooms) {
            const std::string type = r.rec->value("type", "");
            if ((type != "corridor" && type != "hall") || !r.proper) continue;
            const std::vector<V>& p = r.poly;
            double best = 1e9;
            std::string between;
            for (size_t i = 0; i < p.size(); i++)
                for (size_t j = 0; j < i; j++) {
                    const V a0 = p[i], a1 = p[(i + 1) % p.size()], b0 = p[j], b1 = p[(j + 1) % p.size()];
                    const V da = vunit(a1 - a0), db = vunit(b1 - b0);
                    if (vdot(da, db) > -0.9998) continue;   // facing edges run opposite ways
                    const double gap = std::fabs(vcross(da, b0 - a0));
                    const double s0 = vdot(b0 - a0, da), s1 = vdot(b1 - a0, da);
                    const double lo = std::max(0.0, std::min(s0, s1)), hi = std::min(vlen(a1 - a0), std::max(s0, s1));
                    if (hi - lo <= kTol || gap <= kTol) continue;
                    const V mid = a0 + da * ((lo + hi) * 0.5) + vperp(da) * (vcross(da, b0 - a0) > 0.0 ? gap * 0.5 : -gap * 0.5);
                    if (where(mid, p) != 1) continue;   // the room isn't between them
                    const double clear = gap - edgeWall(a0, a1) * 0.5 - edgeWall(b0, b1) * 0.5;
                    if (clear < best) {
                        best = clear;
                        between = pt(a0) + "-" + pt(a1) + " and " + pt(b0) + "-" + pt(b1);
                    }
                }
            if (best < 1.0 - 1e-9) error(*r.rec, "corridor.width", "a " + type + " " + fmt3(best) + " m wide between " + between + ": 1.0 m or more, to turn round in");
        }
    }

    // ------------------------------------------------------------------------------------------------ stairs

    void stairLimits() {
        for (Stair& s : stairs) {
            const Ntw::Record& r = *s.rec;
            const std::string& k = s.kind;
            const size_t n = s.run.size();
            const size_t need = k == "dogleg" ? 4 : k == "quarter" || k == "winder" ? 3 : k == "spiral" || k == "curved" ? 0 : 2;
            if ((need && n != need) || (!need && n < 3)) {
                error(r, "stair.run", "a " + k + " stair's run has " + (need ? std::to_string(need) : std::string("3 or more")) + " points, not " + std::to_string(n));
                continue;
            }
            if (k == "escalator") continue;   // a moving floor: no walking limits
            const double w = s.w;
            const double landing = field(r, "landing", w), walk = field(r, "walk", -1.0);
            double walkLength = 0.0;   // the walking line's length on the flights
            bool straightFlights = false;
            if (k == "straight" || k == "dogleg" || k == "quarter") {
                straightFlights = true;
                const double half = k == "straight" ? 0.0 : landing * 0.5;
                std::vector<double> flights;
                if (k == "dogleg") flights = {vlen(s.run[1] - s.run[0]) - half, vlen(s.run[3] - s.run[2]) - half};
                else if (k == "quarter") flights = {vlen(s.run[1] - s.run[0]) - half, vlen(s.run[2] - s.run[1]) - half};
                else flights = {vlen(s.run[1] - s.run[0])};
                bool bad = false;
                for (double f : flights) bad = bad || f <= kTol;
                if (bad) {
                    error(r, "stair.run", "its run leaves a flight no length past the landing (landing=" + fmt(landing) + ")");
                    continue;
                }
                for (double f : flights) walkLength += f;
                if (k != "straight") {
                    if (landing < w - 1e-9 || landing < 0.9 - 1e-9)
                        error(r, "stair.landing", "a landing " + fmt(landing) + " m deep: at least the flight's width (" + fmt(w) + " m) and 0.9 m");
                    else if (k == "quarter" && (landing < 0.9 - 1e-9 || landing > 1.0 + 1e-9))
                        warning(r, "stair.landing", "an L stair's landing " + fmt(landing) + " m deep: 0.9-1.0 m walks best");
                    if (k == "quarter" && (w < 0.9 - 1e-9 || w > 1.0 + 1e-9)) warning(r, "stair.width", "an L stair's flights " + fmt(w) + " m wide: 0.9-1.0 m walks best");
                }
            } else if (k == "winder") {
                const V d0 = vunit(s.run[1] - s.run[0]), d1 = vunit(s.run[2] - s.run[1]);
                const double turn = std::acos(std::max(-1.0, std::min(1.0, vdot(d0, d1))));
                const double line = walk >= 0.0 ? walk : w * 0.5;
                walkLength = vlen(s.run[1] - s.run[0]) + vlen(s.run[2] - s.run[1]) - w * std::tan(turn * 0.5) + line * turn;
                if (line < 0.5 - 1e-9) error(r, "stair.walkline", "its walking line is " + fmt3(line) + " m from the inner corner: 0.5 m or more");
                if (w < 1.2 - 1e-9) warning(r, "stair.width", "winders on a flight " + fmt(w) + " m wide: 1.2 m keeps the walking line 0.6 m from the corner");
                const std::string busy = at(s.run[0]);
                const Room* rm = room(busy);
                const std::string type = rm ? rm->rec->value("type", "") : std::string();
                if (type == "lobby" || type == "shop" || type == "office")
                    warning(r, "stair.busy", "winders on a busy stair (it starts in the " + type + (busy == type ? std::string() : " " + busy) + "): a landing walks better");
            } else if (k == "spiral" || k == "curved") {
                if (!fitCircle(s.run, &s.centre, &s.radius) || s.radius <= kTol) {
                    error(r, "stair.run", "its run isn't an arc");
                    continue;
                }
                double sweep = 0.0, sign = 0.0, worst = 0.0;
                bool turnsBack = false;
                for (size_t i = 0; i < n; i++) worst = std::max(worst, std::fabs(vlen(s.run[i] - s.centre) - s.radius));
                for (size_t i = 0; i + 1 < n; i++) {
                    double a = std::atan2(s.run[i + 1].y - s.centre.y, s.run[i + 1].x - s.centre.x) - std::atan2(s.run[i].y - s.centre.y, s.run[i].x - s.centre.x);
                    while (a > kPiD) a -= 2.0 * kPiD;
                    while (a <= -kPiD) a += 2.0 * kPiD;
                    if (sign == 0.0) sign = a > 0.0 ? 1.0 : -1.0;
                    turnsBack = turnsBack || a * sign < 0.0;
                    sweep += std::fabs(a);
                }
                if (worst > 0.05 || turnsBack) {
                    error(r, "stair.run", turnsBack ? std::string("its run turns back on itself") : "its run strays " + fmt(worst) + " m from an arc");
                    continue;
                }
                s.arc = true;
                const double line = walk >= 0.0 ? walk : s.radius;
                walkLength = sweep * line;
                if (line < 0.5 - 1e-9) error(r, "stair.walkline", "its walking line is " + fmt3(line) + " m from the centre: 0.5 m or more");
                else if (k == "spiral" && line < 0.55 - 1e-9) warning(r, "stair.walkline", "its walking line is " + fmt3(line) + " m from the centre: 0.55-0.6 m walks best");
                if (k == "curved" && line < 2.0 - 1e-9) warning(r, "stair.radius", "a curved stair walked at a radius of " + fmt(line) + " m: 2 m or more walks like a straight flight");
            }
            s.walkLength = walkLength;
            s.straightFlights = straightFlights;
            // the tread at the walking line, and the risers (without treads= and rise= the plan's floors give them:
            // the plan pass checks those)
            treadRules(s, field(r, "treads", 0.0), field(r, "rise", 0.0), std::string());
        }
    }

    // The 4.9 limits on the tread at the walking line and on the risers, for `treads` treads of `rise` (0: unknown);
    // `context` ends the messages (the plan pass names the floors that gave a derived value)
    void treadRules(const Stair& s, double treads, double rise, const std::string& context) {
        const Ntw::Record& r = *s.rec;
        const std::string& k = s.kind;
        if (treads > 0.0 && s.walkLength >= 0.0 && k != "winder") {
            const double tread = s.walkLength / treads;
            if (tread < 0.25 - 1e-9) error(r, "stair.tread", fmt3(tread) + " m of tread at the walking line: 0.25 m or more" + context);
            else if (k == "spiral" && tread < 0.28 - 1e-9) warning(r, "stair.tread", fmt3(tread) + " m of tread at the walking line: 0.28 m or more walks best" + context);
            else if (s.straightFlights && (tread < 0.26 - 1e-9 || tread > 0.33 + 1e-9)) warning(r, "stair.tread", fmt3(tread) + " m treads: 0.26-0.33 m walk best" + context);
        }
        if (rise > 0.0 && (s.straightFlights || k == "winder") && (rise < 0.15 - 1e-9 || rise > 0.22 + 1e-9))
            warning(r, "stair.riser", fmt3(rise) + " m risers: 0.15-0.22 m walk best" + context);
    }

    // ------------------------------------------------------------------------------------------------ reaching

    // Every room and slot is reached from a way in: the outside, a stair, a lift. False when the item has no way in.
    bool reach() {
        std::map<std::string, std::vector<std::string>> links;
        std::vector<const Ntw::Record*> allDoors;   // (all of them: a door whose wall is wrong still joins its rooms)
        for (const Ntw::Record& r : doc.records)
            if (r.type == "door") allDoors.push_back(&r);
        for (const Ntw::Record* o : allDoors) {
            const std::string leads = o->value("leads", "");
            const size_t colon = leads.find(':');
            if (colon == std::string::npos) continue;
            const std::string a = leads.substr(0, colon), b = leads.substr(colon + 1);
            links[a].push_back(b);
            links[b].push_back(a);
        }
        std::set<std::string> entered;   // slots whose entry is a door (others: the reference check reports them)
        for (const Room& s : slots) {   // a slot's entry joins it to the rooms its door joins
            for (const Ntw::Record* o : allDoors) {
                if (o->id != s.rec->value("entry", "")) continue;
                entered.insert(s.rec->id);
                const std::string leads = o->value("leads", "");
                for (const std::string& x : {leads.substr(0, leads.find(':')), leads.substr(leads.find(':') + 1)}) {
                    if (x == s.rec->id) continue;
                    links[x].push_back(s.rec->id);
                    links[s.rec->id].push_back(x);
                }
            }
        }
        std::vector<std::string> todo;
        if (links.count("out")) todo.push_back("out");
        for (const Stair& s : stairs)
            for (const V& e : {s.run.empty() ? V() : s.run.front(), s.run.empty() ? V() : s.run.back()}) {
                if (s.run.empty()) break;
                const std::string x = at(e);
                if (!x.empty()) todo.push_back(x);
            }
        for (const Ntw::Record* l : lifts) {
            std::vector<V> p = pointsOf(l->value("call", ""));
            if (p.size() != 1) p = pointsOf(l->value("at", ""));
            if (p.size() != 1) continue;
            const std::string x = at(p[0]);
            if (!x.empty()) todo.push_back(x);
        }
        if (todo.empty()) return false;
        std::set<std::string> seen(todo.begin(), todo.end());
        while (!todo.empty()) {
            const std::string x = todo.back();
            todo.pop_back();
            for (const std::string& y : links[x])
                if (seen.insert(y).second) todo.push_back(y);
        }
        for (const std::vector<Room>* list : {&rooms, &slots})
            for (const Room& r : *list)
                if (!seen.count(r.rec->id) && (list == &rooms || entered.count(r.rec->id))) error(*r.rec, "room.reach", "no door, stair or lift leads to it from a way in");
        return true;
    }
};

// The plan pass: one plan's floors and their layouts, each layout read once
struct PlanPass {
    const std::string& plan;
    const std::vector<PlanFloor>& floors;
    std::vector<PlanFinding>& out;
    std::vector<std::string>* reached;   // the floors that relative stair ends of other floors reach
    std::vector<Diagnostic> scratch;   // (the layouts' own findings: their file's checks report those)
    std::map<const Ntw::Document*, std::unique_ptr<Item>> items;

    PlanPass(const std::string& p, const std::vector<PlanFloor>& f, std::vector<PlanFinding>& o, std::vector<std::string>* r)
        : plan(p), floors(f), out(o), reached(r) {}

    Item* item(const PlanFloor& f) {
        if (!f.layout) return nullptr;
        std::unique_ptr<Item>& i = items[f.layout];
        if (!i) {
            i.reset(new Item(*f.layout, &scratch));
            i->read();
            i->roomsClosed();   // (marks the rooms with a proper polygon)
            i->stairLimits();   // (the walking lines' lengths)
        }
        return i.get();
    }
    const PlanFloor* floor(const std::string& id) const {
        for (const PlanFloor& f : floors)
            if (f.rec->id == id) return &f;
        return nullptr;
    }
    static double num(const PlanFloor& f, const char* key) { return field(*f.rec, key, 0.0); }

    void report(const PlanFloor& f, bool inPlan, int line, Severity sev, const char* check, const std::string& msg) {
        PlanFinding p;
        p.inPlan = inPlan;
        p.layoutKey = f.layoutKey;
        p.d.line = line;
        p.d.severity = sev;
        p.d.check = check;
        p.d.message = msg;
        out.push_back(p);
    }

    // Splits "f0.hall" into the floor and the room
    static bool floorRoom(const std::string& v, std::string* fl, std::string* room) {
        const size_t dot = v.find('.');
        if (dot == std::string::npos) return false;
        *fl = v.substr(0, dot);
        *room = v.substr(dot + 1);
        return true;
    }

    // Whether p lies in the room `room` of the floor's layout
    bool inRoom(const PlanFloor& f, const std::string& room, V p) {
        Item* i = item(f);
        const Room* r = i ? i->room(room) : nullptr;
        return !r || !r->proper || where(p, r->poly) >= 0;   // (a missing or broken room: other checks report it)
    }

    // The next floor up (dir +1) or down (-1) from f by height; nullptr at the top or the bottom
    const PlanFloor* neighbour(const PlanFloor& f, int dir) const {
        const double z = num(f, "z");
        const PlanFloor* best = nullptr;
        for (const PlanFloor& g : floors) {
            const double gz = num(g, "z");
            if (&g == &f || (dir > 0 ? gz <= z + kTol : gz >= z - kTol)) continue;
            if (!best || (dir > 0 ? gz < num(*best, "z") : gz > num(*best, "z"))) best = &g;
        }
        return best;
    }

    // A stair's end, on floor f (one that uses the layout): "f0.hall" names its floor; in a layout several floors share,
    // "0:hall" is a room of f, and "+1:hall" / "-1:hall" one on the next floor up / down by height
    struct End {
        const PlanFloor* floor = nullptr;
        std::string room;
        bool relative = false;
        int offset = 0;   // relative: 0, +1 or -1
    };
    // False when the end names no floor: `why` says so for a relative end past the top or the bottom floor; a missing
    // floor or another form is the reference check's
    bool stairEnd(const PlanFloor& f, const std::string& v, End& e, std::string& why) const {
        const size_t colon = v.find(':');
        if (colon == std::string::npos) {
            std::string fl;
            if (!floorRoom(v, &fl, &e.room)) return false;
            e.floor = floor(fl);
            return e.floor != nullptr;
        }
        const std::string off = v.substr(0, colon);
        e.relative = true;
        e.room = v.substr(colon + 1);
        if (off == "0") e.offset = 0;
        else if (off == "+1") e.offset = 1;
        else if (off == "-1") e.offset = -1;
        else return false;
        e.floor = e.offset == 0 ? &f : neighbour(f, e.offset);
        if (!e.floor)
            why = "floor " + f.rec->id + " is the " + (e.offset > 0 ? "top" : "bottom") + " floor of plan " + plan + ", with no floor " +
                  (e.offset > 0 ? "above" : "below") + " it";
        return e.floor != nullptr;
    }

    void stairs(const PlanFloor& f, Item& it) {
        for (Stair& s : it.stairs) {
            const Ntw::Record& r = *s.rec;
            const std::string from = r.value("from", ""), to = r.value("to", "");
            End a, b;
            std::string whyA, whyB;
            const bool okA = stairEnd(f, from, a, whyA), okB = stairEnd(f, to, b, whyB);
            const bool relative = a.relative || b.relative;
            const std::string ctx = " (plan " + plan + ")";
            // relative ends that don't climb, whichever floor uses the layout
            if (a.relative && b.relative && b.offset <= a.offset) {
                report(f, false, r.line, SEV_ERROR, "stair.floors", "stair " + r.id + ": to=" + to + " isn't above from=" + from + ctx);
                continue;
            }
            if (!whyA.empty() || !whyB.empty()) {
                report(f, false, r.line, SEV_ERROR, "stair.floors",
                       "stair " + r.id + ": " + (whyA.empty() ? "to=" + to + " names the floor " + (b.offset > 0 ? "above" : "below") + ", but " + whyB
                                                              : "from=" + from + " names the floor " + (a.offset > 0 ? "above" : "below") + ", but " + whyA));
                continue;
            }
            if (!okA || !okB) continue;   // (the reference check reports it)
            const std::string fa = a.floor->rec->id, fb = b.floor->rec->id;
            // a relative end's room is on the floor it reaches (the reference check only sees this layout)
            bool rooms = true;
            for (const End* e : {&a, &b}) {
                Item* other = e->relative ? item(*e->floor) : nullptr;
                if (!other) continue;
                if (other->room(e->room)) {
                    if (e->floor != &f && reached) reached->push_back(e->floor->rec->id);
                    continue;
                }
                report(f, false, r.line, SEV_ERROR, "stair.floors",
                       "stair " + r.id + ": " + (e == &a ? "from=" + from : "to=" + to) + ": floor " + e->floor->rec->id + " (layout " +
                           e->floor->rec->value("layout", "") + ") has no room " + e->room + ctx);
                rooms = false;
            }
            if (!rooms) continue;
            const double dz = num(*b.floor, "z") - num(*a.floor, "z");
            if (dz <= kTol) {
                report(f, false, r.line, SEV_ERROR, "stair.floors",
                       "stair " + r.id + ": to=" + to + " isn't above from=" + from + (relative ? " (floor " + f.rec->id + " of plan " + plan + ")" : ctx));
                continue;
            }
            // the run's ends stand in the rooms it names, on their floors
            if (s.run.size() >= 2) {
                if (!inRoom(*a.floor, a.room, s.run.front()))
                    report(f, false, r.line, SEV_ERROR, "stair.floors", "stair " + r.id + ": its bottom " + pt(s.run.front()) + " isn't in room " + a.room + " of floor " + fa + ctx);
                if (!inRoom(*b.floor, b.room, s.run.back()))
                    report(f, false, r.line, SEV_ERROR, "stair.floors", "stair " + r.id + ": its top " + pt(s.run.back()) + " isn't in room " + b.room + " of floor " + fb + ctx);
            }
            if (s.kind == "escalator") continue;
            // its treads climb the floors' difference in height: treads x rise, or derived from it. (Relative ends say the
            // same for every pair of floors as far apart: one finding for all of them.)
            double treads = field(r, "treads", 0.0), rise = field(r, "rise", 0.0);
            const std::string between = relative ? ", for floors " + fmt(dz) + " m apart" + ctx : " between floors " + fa + " and " + fb + ", " + fmt(dz) + " m apart" + ctx;
            const std::string derived = relative ? " (derived from the " + fmt(dz) + " m between its floors, plan " + plan + ")"
                                                 : " (derived from the " + fmt(dz) + " m between floors " + fa + " and " + fb + ", plan " + plan + ")";
            if (treads > 0.0 && rise > 0.0) {
                if (std::fabs(treads * rise - dz) > 0.05 + 1e-9)
                    report(f, false, r.line, SEV_ERROR, "stair.floors", "stair " + r.id + ": treads x rise = " + fmt(treads * rise) + " m" + between);
                continue;   // (the layout's own checks ran the limits on these)
            }
            if (treads > 0.0) {
                rise = dz / treads;
            } else if (rise > 0.0) {
                const double n = dz / rise;
                if (std::fabs(n - std::floor(n + 0.5)) * rise > 0.05 + 1e-9) {
                    report(f, false, r.line, SEV_ERROR, "stair.floors", "stair " + r.id + ": risers of " + fmt(rise) + " m don't climb in whole steps" + between);
                    continue;
                }
                treads = std::floor(n + 0.5);
            } else {
                treads = std::max(1.0, std::floor(dz / 0.175 + 0.5));
                rise = dz / treads;
            }
            // the 4.9 limits on the derived values, reported once whatever floor uses the layout
            std::vector<Diagnostic> found;
            std::vector<Diagnostic>* keep = it.out;
            it.out = &found;
            it.treadRules(s, treads, rise, derived);
            it.out = keep;
            for (const Diagnostic& d : found) report(f, false, d.line, d.severity, d.check.c_str(), d.message);
        }
    }

    // A lift is one shaft: every floor it stops at has it, with the same id, place, size and stops. One finding per
    // difference, naming the floors it holds for
    void lifts(const PlanFloor& f, Item& it) {
        for (const Ntw::Record* l : it.lifts) {
            std::vector<std::string> stops;
            Ntw::toList(l->value("stops", ""), stops);
            const std::vector<V> at = pointsOf(l->value("at", ""));
            std::vector<std::pair<std::string, std::vector<std::string>>> wrong;   // what's wrong -> the floors
            for (const std::string& stop : stops) {
                const PlanFloor* g = floor(stop);
                if (!g || g->layout == f.layout) continue;   // (a missing floor: the reference check reports it)
                Item* other = item(*g);
                if (!other) continue;
                const Ntw::Record* same = nullptr;
                for (const Ntw::Record* x : other->lifts)
                    if (x->id == l->id) same = x;
                std::string why;
                if (!same) {
                    why = "the layout has no lift " + l->id;
                } else {
                    const std::vector<V> at2 = pointsOf(same->value("at", ""));
                    if (at.size() == 1 && at2.size() == 1 && vlen(at[0] - at2[0]) > kTol) why = "it stands at " + pt(at2[0]) + ", not " + pt(at[0]);
                    else if (std::string(same->value("size", "")) != l->value("size", "")) why = "its size is " + std::string(same->value("size", "")) + ", not " + l->value("size", "");
                    else if (std::string(same->value("stops", "")) != l->value("stops", "")) why = "it stops at " + std::string(same->value("stops", "")) + ", not " + l->value("stops", "");
                }
                if (why.empty()) continue;
                bool known = false;
                for (auto& w : wrong)
                    if (w.first == why) {
                        w.second.push_back(stop);
                        known = true;
                    }
                if (!known) wrong.push_back(std::make_pair(why, std::vector<std::string>(1, stop)));
            }
            for (const auto& w : wrong) {
                std::string where = w.second.size() == 1 ? "floor " : "floors ";
                for (size_t i = 0; i < w.second.size(); i++) where += (i == 0 ? "" : i + 1 == w.second.size() ? " and " : ", ") + w.second[i];
                report(f, false, l->line, SEV_ERROR, "lift.stops", "lift " + l->id + ": on " + where + " " + w.first + " (plan " + plan + ")");
            }
        }
    }

    // A hatch (access=hatch:<floor>.<room>) comes up from a room on a lower floor, into a room of this one
    void hatch(const PlanFloor& f) {
        const std::string access = f.rec->value("access", "");
        const size_t colon = access.find(':');
        if (access.compare(0, colon, "hatch") != 0 || colon == std::string::npos) return;
        std::string fa, ra;
        if (!floorRoom(access.substr(colon + 1), &fa, &ra)) return;
        const PlanFloor* a = floor(fa);
        if (!a) return;   // (the reference check reports it)
        if (num(*a, "z") >= num(f, "z") - kTol) {
            report(f, true, f.rec->line, SEV_ERROR, "floor.access", "floor " + f.rec->id + ": its hatch comes up from floor " + fa + ", which isn't below it");
            return;
        }
        Item* below = item(*a);
        Item* here = item(f);
        const Room* r = below ? below->room(ra) : nullptr;
        if (!here || !r || !r->proper) return;
        const V c = centroid(r->poly);
        if (here->at(c).empty())
            report(f, true, f.rec->line, SEV_ERROR, "floor.access", "floor " + f.rec->id + ": the hatch from " + fa + "." + ra + " comes up at " + pt(c) + ", in no room of its layout");
    }

    // Doors and windows fit their floor's height
    void heights(const PlanFloor& f, Item& it) {
        const double h = num(f, "h");
        const std::string ctx = ": floor " + f.rec->id + " of plan " + plan + " is " + fmt(h) + " m high";
        for (const Opening& o : it.doors) {
            const double dh = field(*o.rec, "h", 0.0);
            if (dh > h + 1e-9) report(f, false, o.rec->line, SEV_ERROR, "door.height", "door " + o.rec->id + ": " + fmt(dh) + " m high" + ctx);
        }
        for (const Opening& o : it.windows) {
            const double top = field(*o.rec, "sill", 0.0) + field(*o.rec, "h", 0.0);
            if (top > h + 1e-9) report(f, false, o.rec->line, SEV_ERROR, "window.height", "window " + o.rec->id + ": its top is " + fmt(top) + " m up" + ctx);
        }
    }

    void run() {
        for (const PlanFloor& f : floors) {
            hatch(f);
            Item* it = item(f);
            if (!it) continue;
            stairs(f, *it);
            lifts(f, *it);
            heights(f, *it);
        }
    }
};

}  // namespace layout_detail

void checkPlanFloors(const std::string& plan, const std::vector<PlanFloor>& floors, std::vector<PlanFinding>& out, std::vector<std::string>* reached) {
    layout_detail::PlanPass pass(plan, floors, out, reached);
    pass.run();
}

void checkLayout(const Ntw::Document& doc, std::vector<Diagnostic>& out, bool* noWayIn) {
    using namespace layout_detail;
    Item item(doc, &out);
    item.read();
    item.wallsCross();
    item.roomsClosed();
    item.roomsOverlap();
    item.openingsFit();
    item.doorsJoin();
    item.doorWidths();
    item.corridors();
    item.stairLimits();   // (before the landings: it fits the arcs of spirals and curved stairs)
    item.doorLandings();
    const bool way = item.reach();
    if (noWayIn) *noWayIn = !way;
}

}  // namespace Validate
}  // namespace World
