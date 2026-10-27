// The hand-built buildings' interiors (docs/HANDBUILT_WORLD.md 4.8, 4.9, 7.3, 7.8). In hand mode each placement whose
// plan has rooms gets an IK_HAND interior in a set of its own (prepareHandInteriors): its rooms, its doors with their
// leaves, the daylight through its windows and doors, and the markers at its main entrance. Its geometry, collision and
// lights are built on demand (layoutHand, from buildInterior on a worker) from the library's interiors' plans
// (buildingsbin.h HandShell: the exterior and inside walls with their doors and windows, the rooms, the stairs) with the
// interior kit's helpers; interiors_game.cpp streams, draws and animates them as it does the generator's.
//
// The frame (interiors.h "model space"): the plan's, mirrored first where the placement is, with its origin in the
// middle of the front wall's centreline: model x = (mirror ? W - x : x) - W / 2, y = y, z above the ground floor. The
// shell (handshells.cpp) draws the outer faces, the windows' glass and the doors' outer reveals, and collides the
// exterior walls (their doorways open), the ground floor, the roof and the doors' stoops; the interior draws and
// collides the rest: the walls' inner faces with the windows' inner reveals and glass, the inside walls with their
// doorways, the floors and ceilings with their stairwells, the stairs (a box per tread, climbed on a ramp of thin
// colliders as the warehouse mezzanine's), the stairwells' balustrades and the upper floors' slabs. A room's floor,
// ceiling and walls are its polygon's bounds less half the walls round it; its volume (its ambient, and the lamps that
// light it) less half the exterior walls only, so the leaf of a door in an inside wall is lit, from its floor to its
// ceiling, kSlab under the floor above; a stair's room reaches that floor, so the volumes a stair climbs through meet.
// Its lamps follow its windows' light state (handRoomLight).
#include "interiors.h"
#include "buildingsbin.h"
#include "roomlight.h"

namespace World {

namespace interiorhand_detail {

using handshells_detail::kSlab;
const float kRail = 1.f;           // a stairwell's balustrade, above its floor
const float kTreadUnder = 0.12f;   // a tread's box reaches this far below the tread under it (the flight's soffit)
const float kLandingT = 0.25f;     // a landing's thickness
const float kSnap = 0.4f;          // a stairwell reaches a wall this close (no ledge to fall from)

// A room's light state's inputs (roomlight.h roomLight): its class, its seed, whether it has windows. A room with a
// window takes its window's facade entry's (facade.hlsli: a hand-built window's room hash), so its lamps are on when its
// windows show it lit
struct RoomLightKey {
    u8 schedule = RS_NEVER, windows = 0;
    u32 seed = 0;
};
struct HandInteriorSet {
    InteriorSet set;
    std::vector<int> placement;                   // per interior, its placement in gHandBuildings
    std::vector<std::vector<RoomLightKey>> light;  // per interior, per room
};
HandInteriorSet gHandSet;   // (gInteriors points at gHandSet.set in hand mode)

// A placement's plan into its interior's model space
struct ToModel {
    float w = 0.f;
    bool mirror = false;
    vec2 P(vec2 l) const { return vec2((mirror ? w - l.x : l.x) - w * 0.5f, l.y); }
    vec2 D(vec2 d) const { return vec2(mirror ? -d.x : d.x, d.y); }
};

struct Rect {
    vec2 lo = vec2(0.f), hi = vec2(0.f);
    bool contains(vec2 p, float m = 0.f) const { return p.x > lo.x - m && p.x < hi.x + m && p.y > lo.y - m && p.y < hi.y + m; }
    vec2 centre() const { return (lo + hi) * 0.5f; }
    float area() const { return Max(hi.x - lo.x, 0.f) * Max(hi.y - lo.y, 0.f); }
};

// `r` less the rectangles `holes`, as a grid of pieces (axis-aligned throughout)
std::vector<Rect> minus(const Rect& r, const std::vector<Rect>& holes) {
    std::vector<float> xs = {r.lo.x, r.hi.x}, ys = {r.lo.y, r.hi.y};
    bool any = false;
    for (const Rect& h : holes) {
        if (h.hi.x <= r.lo.x + 1e-3f || h.lo.x >= r.hi.x - 1e-3f || h.hi.y <= r.lo.y + 1e-3f || h.lo.y >= r.hi.y - 1e-3f) continue;
        any = true;
        for (float x : {h.lo.x, h.hi.x})
            if (x > r.lo.x && x < r.hi.x) xs.push_back(x);
        for (float y : {h.lo.y, h.hi.y})
            if (y > r.lo.y && y < r.hi.y) ys.push_back(y);
    }
    if (!any) return {r};
    std::sort(xs.begin(), xs.end());
    std::sort(ys.begin(), ys.end());
    std::vector<Rect> out;
    for (size_t i = 0; i + 1 < xs.size(); i++)
        for (size_t k = 0; k + 1 < ys.size(); k++) {
            Rect c;
            c.lo = vec2(xs[i], ys[k]);
            c.hi = vec2(xs[i + 1], ys[k + 1]);
            if (c.hi.x - c.lo.x < 1e-3f || c.hi.y - c.lo.y < 1e-3f) continue;
            bool cut = false;
            for (const Rect& h : holes) cut = cut || h.contains(c.centre());
            if (!cut) out.push_back(c);
        }
    return out;
}

// A wall in model space
struct MWall {
    const HandShell::Wall* src = nullptr;
    bool exterior = false;
    int floor = 0;
    vec2 a = vec2(0.f), b = vec2(0.f), dir = vec2(1.f, 0.f);
    vec2 n = vec2(0.f, 1.f);   // exterior walls: toward the inside; inside walls: to the left of a -> b
    float len = 0.f, t = 0.f;
    float trimA = 0.f, trimB = 0.f;   // its faces stop this short of its ends: half the wall each end meets
    bool freeA = false, freeB = false;
};
// A room in model space
struct MRoom {
    const HandShell::Room* src = nullptr;
    int floor = 0, local = 0;   // (local: its index among its floor's rooms, as the openings count them)
    Rect box;                   // its polygon's bounds, to the walls' centrelines
    Rect space;                 // less half the walls round it
    Rect volume;                // less half the exterior walls round it only: its ambient volume (InteriorRoom), which
                                // reaches the inside walls' centrelines, so a door's leaf there is lit by a room's lamps
};
// A flight of a stair in model space: from its foot s to its top e (the centreline), `risers` of the stair's rise from z0
struct Flight {
    vec2 s = vec2(0.f), e = vec2(0.f), dir = vec2(0.f, 1.f);
    float len = 0.f, w = 0.f, z0 = 0.f;
    int risers = 1;
};
struct Landing {
    vec2 c = vec2(0.f), ax = vec2(1.f, 0.f);
    float hx = 0.f, hy = 0.f, z = 0.f;
};
struct MStair {
    const HandShell::Stair* src = nullptr;
    std::vector<vec2> run;      // (model)
    std::vector<Flight> flights;
    std::vector<Landing> landings;
    float rise = 0.18f;
    Rect well;                  // the hole it climbs through in its top floor's slab
    bool walled[4] = {};        // the well's sides on walls: -x, +x, -y, +y
};
// A placement's interior's plan in model space
struct MPlan {
    const HandShell* s = nullptr;
    const HandPlacement* p = nullptr;
    ToModel M;
    std::vector<MWall> walls;   // the exterior walls, then the inside walls
    std::vector<MRoom> rooms;   // as the shell's (the interior's room index)
    std::vector<MStair> stairs;
    std::vector<std::vector<int>> roomOf;   // per floor, its rooms' indices by local index
    float floorZ(int f) const { return s->floors[(size_t)f].z; }
    float ceilZ(int f) const { return s->floors[(size_t)f].z + s->floors[(size_t)f].h - kSlab; }
    int room(int floor, u32 local) const {
        return floor >= 0 && (size_t)floor < roomOf.size() && local < roomOf[(size_t)floor].size() ? roomOf[(size_t)floor][local] : -1;
    }
};

float pointSegment(vec2 p, vec2 a, vec2 b) {
    const vec2 d = b - a;
    const float l2 = dot(d, d);
    const float t = l2 > 1e-8f ? Clamp(dot(p - a, d) / l2, 0.f, 1.f) : 0.f;
    return length(p - (a + d * t));
}

MPlan planOf(const HandShell& s, const HandPlacement& p) {
    MPlan P;
    P.s = &s;
    P.p = &p;
    P.M.w = s.size.x;
    P.M.mirror = p.mirror;
    // the walls: an exterior wall's inside is away from the footprint's outside
    for (int pass = 0; pass < 2; pass++)
        for (const HandShell::Wall& w : pass == 0 ? s.walls : s.partitions) {
            MWall m;
            m.src = &w;
            m.exterior = pass == 0;
            m.floor = w.floor;
            m.a = P.M.P(w.a);
            m.b = P.M.P(w.b);
            m.len = length(m.b - m.a);
            if (m.len < 0.05f) continue;
            m.dir = (m.b - m.a) * (1.f / m.len);
            m.t = w.t;
            m.n = perp(m.dir);
            if (m.exterior) {
                const vec2 e = normalize(w.b - w.a);
                vec2 out(e.y, -e.x);   // (plan: to the right of a -> b)
                if (handshells_detail::inside(s.footprint, (w.a + w.b) * 0.5f + out * 0.05f)) out = -out;
                m.n = -P.M.D(out);
            }
            P.walls.push_back(m);
        }
    // each end meets another wall of its floor (its faces stop half that wall short), or is free (an end cap)
    for (MWall& m : P.walls)
        for (int end = 0; end < 2; end++) {
            const vec2 q = end == 0 ? m.a : m.b;
            float meet = -1.f;
            for (const MWall& o : P.walls) {
                if (&o == &m || o.floor != m.floor || fabsf(dot(o.dir, m.dir)) > 0.9f) continue;
                if (pointSegment(q, o.a, o.b) < 0.03f) meet = Max(meet, o.t * 0.5f);
            }
            (end == 0 ? m.trimA : m.trimB) = Max(meet, 0.f);
            (end == 0 ? m.freeA : m.freeB) = meet < 0.f;
        }
    // the rooms, and each one's space within the walls round it
    P.roomOf.assign(s.floors.size(), {});
    for (const HandShell::Room& r : s.rooms) {
        MRoom m;
        m.src = &r;
        m.floor = r.floor;
        m.local = (int)P.roomOf[(size_t)r.floor].size();
        P.roomOf[(size_t)r.floor].push_back((int)P.rooms.size());
        m.box.lo = m.box.hi = P.M.P(r.poly[0]);
        for (vec2 q : r.poly) {
            const vec2 v = P.M.P(q);
            m.box.lo = vec2(Min(m.box.lo.x, v.x), Min(m.box.lo.y, v.y));
            m.box.hi = vec2(Max(m.box.hi.x, v.x), Max(m.box.hi.y, v.y));
        }
        m.space = m.volume = m.box;
        const auto inset = [&](bool vertical, float at, float lo, float hi, bool exteriorOnly) {   // the thickest wall along that edge
            float t = 0.f;
            for (const MWall& w : P.walls) {
                if (w.floor != r.floor || (exteriorOnly && !w.exterior)) continue;
                if (vertical ? fabsf(w.dir.x) > 0.02f : fabsf(w.dir.y) > 0.02f) continue;
                const float c = vertical ? w.a.x : w.a.y;
                const float w0 = vertical ? Min(w.a.y, w.b.y) : Min(w.a.x, w.b.x), w1 = vertical ? Max(w.a.y, w.b.y) : Max(w.a.x, w.b.x);
                if (fabsf(c - at) < 0.03f && w1 > lo + 0.05f && w0 < hi - 0.05f) t = Max(t, w.t);
            }
            return t * 0.5f;
        };
        for (int pass = 0; pass < 2; pass++) {
            Rect& q = pass == 0 ? m.space : m.volume;
            q.lo.x += inset(true, m.box.lo.x, m.box.lo.y, m.box.hi.y, pass == 1);
            q.hi.x -= inset(true, m.box.hi.x, m.box.lo.y, m.box.hi.y, pass == 1);
            q.lo.y += inset(false, m.box.lo.y, m.box.lo.x, m.box.hi.x, pass == 1);
            q.hi.y -= inset(false, m.box.hi.y, m.box.lo.x, m.box.hi.x, pass == 1);
        }
        P.rooms.push_back(m);
    }
    // the stairs: their flights and landings, and the well each climbs through
    for (const HandShell::Stair& st : s.stairs) {
        MStair m;
        m.src = &st;
        for (vec2 q : st.run) m.run.push_back(P.M.P(q));
        m.rise = st.rise > 0.f ? st.rise : 0.18f;
        const float L = st.landing > 0.f ? st.landing : st.w;
        std::vector<std::pair<vec2, vec2>> segs;
        const size_t n = m.run.size();
        if (st.kind == STAIR_DOGLEG && n == 4) {
            const vec2 d01 = normalize(m.run[1] - m.run[0]), d23 = normalize(m.run[3] - m.run[2]);
            segs = {{m.run[0], m.run[1] - d01 * (L * 0.5f)}, {m.run[2] + d23 * (L * 0.5f), m.run[3]}};
            Landing l;
            l.c = (m.run[1] + m.run[2]) * 0.5f;
            l.ax = d01;
            l.hx = L * 0.5f;
            l.hy = (length(m.run[2] - m.run[1]) + st.w) * 0.5f;
            m.landings.push_back(l);
        } else if (st.kind == STAIR_QUARTER && n == 3) {
            const vec2 d01 = normalize(m.run[1] - m.run[0]), d12 = normalize(m.run[2] - m.run[1]);
            segs = {{m.run[0], m.run[1] - d01 * (L * 0.5f)}, {m.run[1] + d12 * (L * 0.5f), m.run[2]}};
            Landing l;
            l.c = m.run[1];
            l.ax = d01;
            l.hx = l.hy = Max(L, st.w) * 0.5f;
            m.landings.push_back(l);
        } else {   // straight, and the other kinds as straight flights along their run (none of b005's)
            for (size_t i = 0; i + 1 < n; i++) segs.push_back({m.run[i], m.run[i + 1]});
        }
        // the risers, shared out by the flights' lengths
        float total = 0.f;
        for (const auto& sg : segs) total += length(sg.second - sg.first);
        const int risers = Max((int)st.treads, (int)segs.size());
        int given = 0;
        for (size_t k = 0; k < segs.size(); k++) {
            Flight f;
            f.s = segs[k].first;
            f.e = segs[k].second;
            f.len = length(f.e - f.s);
            f.dir = f.len > 1e-4f ? (f.e - f.s) * (1.f / f.len) : vec2(0.f, 1.f);
            f.w = st.w;
            f.risers = k + 1 == segs.size() ? risers - given : Max(1, (int)floorf(risers * (total > 0.f ? f.len / total : 1.f) + 0.5f));
            f.risers = Max(1, Min(f.risers, risers - given - (int)(segs.size() - 1 - k)));
            f.z0 = P.floorZ(st.fromFloor) + m.rise * (float)given;
            given += f.risers;
            m.flights.push_back(f);
            if (k < m.landings.size()) m.landings[k].z = P.floorZ(st.fromFloor) + m.rise * (float)given;
        }
        // the well: round the flights and landings, out to a wall that close (no ledge beside it)
        bool first = true;
        const auto grow = [&](vec2 v) {
            if (first) m.well.lo = m.well.hi = v;
            first = false;
            m.well.lo = vec2(Min(m.well.lo.x, v.x), Min(m.well.lo.y, v.y));
            m.well.hi = vec2(Max(m.well.hi.x, v.x), Max(m.well.hi.y, v.y));
        };
        for (const Flight& f : m.flights) {
            const vec2 a = perp(f.dir) * (f.w * 0.5f);
            for (vec2 v : {f.s + a, f.s - a, f.e + a, f.e - a}) grow(v);
        }
        for (const Landing& l : m.landings) {
            const vec2 ay = perp(l.ax);
            for (float sx : {-1.f, 1.f})
                for (float sy : {-1.f, 1.f}) grow(l.c + l.ax * (l.hx * sx) + ay * (l.hy * sy));
        }
        for (int side = 0; side < 4; side++) {
            const bool vertical = side < 2;   // (the sides at x lo, x hi, y lo, y hi)
            const float edge = side == 0 ? m.well.lo.x : side == 1 ? m.well.hi.x : side == 2 ? m.well.lo.y : m.well.hi.y;
            const float sign = (side & 1) ? 1.f : -1.f;   // (outward)
            float best = 1e9f;
            for (const MWall& w : P.walls) {
                if (w.floor != st.toFloor) continue;
                if (vertical ? fabsf(w.dir.x) > 0.02f : fabsf(w.dir.y) > 0.02f) continue;
                const float c = vertical ? w.a.x : w.a.y;
                const float w0 = vertical ? Min(w.a.y, w.b.y) : Min(w.a.x, w.b.x), w1 = vertical ? Max(w.a.y, w.b.y) : Max(w.a.x, w.b.x);
                const float lo = vertical ? m.well.lo.y : m.well.lo.x, hi = vertical ? m.well.hi.y : m.well.hi.x;
                if (w1 < lo + 0.05f || w0 > hi - 0.05f) continue;
                const float face = c - sign * w.t * 0.5f;   // (the face toward the well)
                const float gap = (face - edge) * sign;
                if (gap > -0.03f && gap < kSnap && gap < best) best = gap;
            }
            if (best < 1e8f) {
                m.walled[side] = true;
                if (side == 0) m.well.lo.x -= best;
                if (side == 1) m.well.hi.x += best;
                if (side == 2) m.well.lo.y -= best;
                if (side == 3) m.well.hi.y += best;
            }
        }
        P.stairs.push_back(m);
    }
    return P;
}

// The wells cut into floor f's slab: the stairs that climb to it
std::vector<Rect> wellsOn(const MPlan& P, int f) {
    std::vector<Rect> out;
    for (const MStair& st : P.stairs)
        if (st.src->toFloor == f) out.push_back(st.well);
    return out;
}

// facade.hlsli's hash of a window's room (common.hlsli hashU, hash3u)
u32 shaderHash(u32 x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
u32 shaderHash3(u32 a, u32 b, u32 c) { return shaderHash(a * 0x8da6b343u ^ b * 0xd8163841u ^ shaderHash(c + 0x7f4a7c15u)); }

// Light schedule for a room's (roomlight.h) class: homes keep a household's evening, shops and offices their hours
u8 scheduleOf(u32 rs) {
    switch (rs) {
        case RS_ALWAYS: return LS_ALWAYS;
        case RS_SHOP: case RS_OFFICE: case RS_RESTAURANT: case RS_GARAGE: case RS_NEVER: return LS_BUSINESS;
        case RS_NIGHTLIFE: return LS_NIGHTLIFE;
        default: return LS_EVENING;
    }
}

// A room's finishes and light, by its type
struct RoomFinish {
    u32 floorCol = 0, wallCol = 0, ceilCol = 0;
    u32 floorMat = 0, wallMat = 0, ceilMat = 0;
    float tiles = 0.f;     // a tiled dado this high (0: none)
    vec3 ambient = vec3(10.f);
    vec3 tint = vec3(1.f, 0.86f, 0.68f);
    int fixture = 0;       // 0 downlight, 1 pendant, 2 troffers
};
RoomFinish finishOf(u8 type, u32 seed) {
    using namespace ikit;
    RoomFinish f;
    static const vec3 kHomeWalls[] = {vec3(0.93f, 0.9f, 0.84f), vec3(0.9f, 0.91f, 0.86f), vec3(0.94f, 0.88f, 0.8f), vec3(0.86f, 0.89f, 0.9f), vec3(0.92f, 0.86f, 0.84f)};
    const vec3 home = kHomeWalls[seed % 5u];
    f.wallCol = C(home);
    f.wallMat = M(MAT_PLASTER);
    f.ceilCol = Gy(0.95f);
    f.ceilMat = M(MAT_PLASTER);
    f.floorCol = C(0.6f, 0.43f, 0.29f);
    f.floorMat = M(MAT_WOOD_FLOOR);
    switch (type) {
        case ROOM_KITCHEN:
            f.floorCol = C(0.82f, 0.8f, 0.75f);
            f.floorMat = M(MAT_TILE);
            f.ambient = vec3(13.f, 12.f, 10.f);
            f.tint = vec3(1.f, 0.93f, 0.82f);
            f.fixture = 2;
            break;
        case ROOM_BATH:
            f.floorCol = C(0.74f, 0.78f, 0.8f);
            f.floorMat = M(MAT_TILE);
            f.tiles = 1.2f;
            f.ambient = vec3(12.f, 12.f, 11.f);
            f.tint = vec3(1.f, 0.95f, 0.88f);
            break;
        case ROOM_SHOP: case ROOM_RESTAURANT: case ROOM_BAR: case ROOM_OFFICE:
            f.wallCol = Gy(0.94f);
            f.floorCol = C(0.8f, 0.78f, 0.74f);
            f.floorMat = M(MAT_TILE);
            f.ceilCol = Gy(0.92f);
            f.ceilMat = M(MAT_CEILING_TILE);
            f.ambient = vec3(40.f, 41.f, 42.f);
            f.tint = vec3(0.95f, 0.98f, 1.f);
            f.fixture = 2;
            break;
        case ROOM_STORE: case ROOM_PLANT: case ROOM_UTILITY: case ROOM_GARAGE:
            f.wallCol = Gy(0.8f);
            f.floorCol = Gy(0.55f);
            f.floorMat = M(MAT_CONCRETE);
            f.ceilCol = Gy(0.88f);
            f.ambient = vec3(14.f, 14.f, 13.f);
            f.tint = vec3(0.95f, 0.97f, 1.f);
            f.fixture = 2;
            break;
        case ROOM_STAIR: case ROOM_LOBBY: case ROOM_CORRIDOR:
            f.wallCol = C(0.9f, 0.88f, 0.82f);
            f.floorCol = C(0.76f, 0.72f, 0.66f);
            f.floorMat = M(MAT_TILE);
            f.ambient = vec3(14.f, 14.f, 13.f);
            f.tint = vec3(1.f, 0.92f, 0.8f);
            break;
        case ROOM_BED:
            f.ambient = vec3(9.f, 7.5f, 6.f);
            f.fixture = 1;
            break;
        default:   // living, dining, hall
            f.ambient = vec3(11.f, 9.f, 7.f);
            f.fixture = type == ROOM_HALL ? 0 : 1;
            break;
    }
    return f;
}

// The kinds of door the runtime swings, slides or rolls (an opening, a revolving door or a lift's: none, a doorway)
int doorKindOf(u8 kind) {
    switch (kind) {
        case DOOR_HINGED: return DK_HINGED;
        case DOOR_PAIR: return DK_HINGED_PAIR;
        case DOOR_SLIDING: return DK_SLIDING_PAIR;
        case DOOR_ROLLUP: return DK_ROLLUP;
        default: return -1;
    }
}

// A face of a wall in model space: origin o (its left end seen from the room it faces, at z 0), u along it, its
// length, and where an opening's centre `at` (from the wall's a) falls along it
struct Face {
    vec2 o = vec2(0.f), u = vec2(1.f, 0.f), n = vec2(0.f, 1.f);
    float len = 0.f;
    float s0 = 0.f, sk = 1.f;   // s = s0 + sk * at
    float s(float at) const { return s0 + sk * at; }
};
Face faceOf(const MWall& w, vec2 n) {
    Face f;
    f.n = n;
    f.u = vec2(-n.y, n.x);   // (cross(u, z) = n: the quads face n)
    const vec2 off = n * (w.t * 0.5f);
    const vec2 a = w.a + w.dir * w.trimA + off, b = w.b - w.dir * w.trimB + off;
    const bool along = dot(f.u, w.dir) > 0.f;
    f.o = along ? a : b;
    f.len = Max(w.len - w.trimA - w.trimB, 0.f);
    f.s0 = along ? -w.trimA : w.len - w.trimB;
    f.sk = along ? 1.f : -1.f;
    return f;
}

}  // namespace interiorhand_detail

namespace ikit {
void layoutHand(IB& b);
}

// ------------------------------------------------------------------------------------------------ the set
void prepareHandInteriors(const HandBuildings& hb, const WorldMap& map) {
    using namespace interiorhand_detail;
    gHandSet.set.defs.clear();
    gHandSet.placement.clear();
    gHandSet.light.clear();
    int rooms = 0, doors = 0, stairs = 0;
    float worstGround = -1e9f;
    std::string worstAt;
    for (size_t pi = 0; pi < hb.placements.size(); pi++) {
        const HandPlacement& p = hb.placements[pi];
        if (p.shell < 0 || (size_t)p.shell >= hb.shells.size()) continue;
        const HandShell& s = hb.shells[(size_t)p.shell];
        if (s.rooms.empty() || s.floors.empty() || s.footprint.size() < 3 || s.size.x <= 0.f || s.size.y <= 0.f) continue;
        const MPlan P = planOf(s, p);
        InteriorDef d;
        d.kind = IK_HAND;
        d.building = pi < hb.placementBuilding.size() ? hb.placementBuilding[pi] : -1;
        d.seed = hash32(hashString(p.id.c_str()) ^ 0x51a7e5u);
        d.name = p.id;
        d.ax = vec2(cosf(p.yaw), sinf(p.yaw));
        d.ay = perp(d.ax);
        d.origin = vec3(p.at + d.ax * (s.size.x * 0.5f), p.z);
        d.x0 = -s.size.x * 0.5f;
        d.x1 = s.size.x * 0.5f;
        d.depth = s.size.y;
        d.ceil = s.floors[0].h - kSlab;
        d.shellTop = handshells_detail::shellTop(s, nullptr);
        d.bw = s.size.x;
        d.bayX0 = d.x0;
        d.bays = 1;
        d.radius = sqrtf(s.size.x * s.size.x * 0.25f + s.size.y * s.size.y) + 1.f;   // (round the origin: InteriorSet::at)
        // the rooms: their space, from the floor to the ceiling (a stair's to the floor above)
        for (const MRoom& r : P.rooms) {
            InteriorRoom ir;
            const bool up = r.src->type == ROOM_STAIR && (size_t)r.floor + 1 < s.floors.size();
            ir.mn = vec3(r.volume.lo, P.floorZ(r.floor));
            ir.mx = vec3(r.volume.hi, up ? P.floorZ(r.floor + 1) : P.ceilZ(r.floor));
            const RoomFinish fin = finishOf(r.src->type, d.seed + (u32)rooms);
            ir.lampAmbient = fin.ambient;
            ir.schedule = scheduleOf(r.src->schedule);
            ir.dayBounce = 0.02f;
            d.rooms.push_back(ir);
            rooms++;
        }
        // the doors with leaves, and the daylight through the windows and the exterior doors
        const handshells_detail::Paint pt = handshells_detail::paintOf(hb, p, s);
        for (const MWall& w : P.walls)
            for (const HandShell::Opening& o : w.src->openings) {
                const vec2 c = w.a + w.dir * o.at;
                if (o.door()) {
                    const int kind = doorKindOf((u8)(o.kind & 255u));
                    if (kind < 0) continue;
                    // its leaf: hinged at the end toward the wall's a or b, opening to the plan's left or right of a -> b
                    // (the model's other side where the placement is mirrored)
                    const bool left = (o.swing == 0) != P.M.mirror;
                    InteriorDoor dr;
                    dr.n = left ? perp(w.dir) : -perp(w.dir);
                    const vec2 hingeEnd = c + w.dir * (o.hinge ? o.w * 0.5f : -o.w * 0.5f);
                    dr.t = normalize(c - hingeEnd);
                    dr.c = vec3(c, P.floorZ(w.floor));
                    dr.w = o.w;
                    dr.h = o.h;
                    dr.depth = w.exterior ? Max(w.t * 0.5f - 0.03f, 0.f) : 0.f;   // (an exterior door's leaf near the face it opens to)
                    dr.kind = (u8)kind;
                    dr.exterior = w.exterior;
                    dr.style = w.exterior ? ((o.kind & 255u) == DOOR_PAIR ? 1 : 0) : 0;
                    dr.color = w.exterior ? pt.door() : ikit::Gy(0.93f);
                    d.doors.push_back(dr);
                    doors++;
                }
                // daylight: a window, or an exterior door, into the room behind it
                const int room = w.exterior ? P.room(w.floor, o.room) : -1;
                if (room < 0) continue;
                InteriorPortal pr;
                const vec2 u = vec2(-w.n.y, w.n.x);   // (cross(u, v) into the room)
                const vec2 face = c + w.n * (w.t * 0.5f);
                const float z0 = P.floorZ(w.floor) + (o.door() ? 0.f : o.sill);
                pr.p0 = vec3(face - u * (o.w * 0.5f), z0);
                pr.u = vec3(u * o.w, 0.f);
                pr.v = vec3(0.f, 0.f, Min(o.h, P.ceilZ(w.floor) - z0));
                pr.room = room;
                pr.transmission = o.door() ? 1.f : 0.8f;
                pr.door = o.door() && doorKindOf((u8)(o.kind & 255u)) >= 0 ? (int)d.doors.size() - 1 : -1;
                d.portals.push_back(pr);
                d.rooms[(size_t)room].dayBounce = 0.1f;
            }
        stairs += (int)P.stairs.size();
        // the main entrance's markers: just outside it (--interiortest starts there) and just inside
        if (s.entranceWall >= 0 && (size_t)s.entranceWall < s.walls.size()) {
            for (const MWall& w : P.walls)
                if (w.src == &s.walls[(size_t)s.entranceWall] && (size_t)s.entranceOpening < w.src->openings.size()) {
                    const vec2 c = w.a + w.dir * w.src->openings[(size_t)s.entranceOpening].at;
                    InteriorMarker out, in;
                    out.kind = IM_DOOR_OUT;
                    out.pos = vec3(c - w.n * 1.3f, 0.f);
                    out.yaw = atan2f(w.n.x, -w.n.y);   // (facing out)
                    in.kind = IM_ENTRY;
                    in.pos = vec3(c + w.n * 1.f, 0.f);
                    in.yaw = atan2f(-w.n.x, w.n.y);
                    d.markers.push_back(out);
                    d.markers.push_back(in);
                }
        }
        // (the ground under the ground floor: above it, it would show through the floor and be stood on)
        for (const MRoom& r : P.rooms) {
            if (r.floor != 0) continue;
            for (int k = 0; k < 9; k++) {
                const vec3 q = d.toWorld(vec3(Lerp(r.space.lo.x, r.space.hi.x, 0.1f + 0.4f * (k % 3)), Lerp(r.space.lo.y, r.space.hi.y, 0.1f + 0.4f * (k / 3)), 0.f));
                const float above = map.heightAt(q.x, q.y) - p.z;
                if (above > worstGround) {
                    worstGround = above;
                    worstAt = p.id;
                }
            }
        }
        // the rooms' light states: a room with a lit window as that window (its facade entry's seed: a storefront's shop
        // hash, else its room hash), the others from their own seed
        std::vector<RoomLightKey> keys(P.rooms.size());
        for (size_t r = 0; r < P.rooms.size(); r++) {
            keys[r].schedule = (u8)(P.rooms[r].src->schedule <= 15u ? P.rooms[r].src->schedule : (u32)RS_NEVER);
            keys[r].seed = roomSeed(d.seed, (u32)P.rooms[r].floor, (u32)P.rooms[r].local);
        }
        if (gBuildings && pi < hb.firstOpening.size() && hb.firstOpening[pi] != kNone32) {
            size_t idx = hb.firstOpening[pi];
            for (const HandShell::Wall& w : s.walls)
                for (const HandShell::Opening& o : w.openings) {
                    const u32 e = idx < hb.openingFacades.size() ? hb.openingFacades[idx] : kNone32;
                    idx++;
                    const int r = P.room(w.floor, o.room);
                    if (r < 0 || keys[(size_t)r].windows || e == kNone32 || e >= gBuildings->facades.size()) continue;
                    const FacadeGPU& f = gBuildings->facades[e];
                    const bool shop = (f.flags & 1u) != 0u;
                    keys[(size_t)r].schedule = (u8)handWindowSchedule(f.flags);
                    keys[(size_t)r].seed = (shop ? shaderHash3(2000u, 77u, f.seed) : shaderHash3(1000u, 1u, f.seed)) >> 20;
                    keys[(size_t)r].windows = 1;
                }
        }
        gHandSet.light.push_back(keys);
        gHandSet.set.defs.push_back(std::move(d));
        gHandSet.placement.push_back((int)pi);
    }
    gInteriors = gHandSet.set.defs.empty() ? nullptr : &gHandSet.set;
    ikit::gLayoutHand = &ikit::layoutHand;
    if (!gHandSet.set.defs.empty())
        LOG("Hand interiors: %zu (%d rooms, %d doors with leaves, %d stairs); the ground under their floors stays %.2f m below them or more (%s)",
            gHandSet.set.defs.size(), rooms, doors, stairs, -worstGround, worstAt.c_str());
}

float handRoomLight(int interior, int room, u32 day, float hour, u32 night) {
    using namespace interiorhand_detail;
    if (gInteriors != &gHandSet.set || interior < 0 || (size_t)interior >= gHandSet.light.size()) return -1.f;
    const std::vector<RoomLightKey>& keys = gHandSet.light[(size_t)interior];
    if (room < 0 || (size_t)room >= keys.size()) return 0.f;
    const RoomLightKey& k = keys[(size_t)room];
    return (float)roomLight(k.schedule, k.seed, k.windows, day, roomMinute(hour), night) * (1.f / 222.f);
}

// ------------------------------------------------------------------------------------------------ the tour
bool handInteriorTour(int interior, std::vector<vec3>& points, std::vector<int>& roomsOut) {
    using namespace interiorhand_detail;
    points.clear();
    roomsOut.clear();
    if (gInteriors != &gHandSet.set || interior < 0 || (size_t)interior >= gHandSet.placement.size() || !gHandBuildings) return false;
    const HandBuildings& hb = *gHandBuildings;
    const HandPlacement& p = hb.placements[(size_t)gHandSet.placement[(size_t)interior]];
    const HandShell& s = hb.shells[(size_t)p.shell];
    const InteriorDef& d = gHandSet.set.defs[(size_t)interior];
    const MPlan P = planOf(s, p);
    // the rooms' graph, with the outside as one more node: a door between the rooms (or the outside) each side of its
    // wall, a stair from its foot's room to its top's
    struct Edge {
        int to;
        std::vector<vec3> path;   // (model)
        int mark;                 // its waypoints' room: -1 a doorway's, -2 a stair's
        bool main;
    };
    const int outside = (int)P.rooms.size();
    std::vector<std::vector<Edge>> next(P.rooms.size() + 1);
    const auto side = [&](const MWall& w, vec2 c, int room) {   // +1: the room is on the left of the wall's a -> b
        return room >= 0 && dot(P.rooms[(size_t)room].space.centre() - c, perp(w.dir)) > 0.f ? 1.f : -1.f;
    };
    for (const MWall& w : P.walls)
        for (size_t k = 0; k < w.src->openings.size(); k++) {
            const HandShell::Opening& o = w.src->openings[k];
            if (!o.door()) continue;
            const vec2 c = w.a + w.dir * o.at;
            const float z = P.floorZ(w.floor);
            const vec2 nl = perp(w.dir);
            const int ra = P.room(w.floor, o.left), rb = P.room(w.floor, o.right);
            if (w.exterior) {   // (n: inward)
                const int in = ra >= 0 ? ra : rb;
                if (in < 0 || w.floor != 0) continue;
                const bool main = w.src == &s.walls[(size_t)Max(s.entranceWall, 0)] && (int)k == s.entranceOpening;
                std::vector<vec3> path = {vec3(c - w.n * 1.3f, z), vec3(c + w.n * 0.8f, z)};
                next[(size_t)outside].push_back(Edge{in, path, -1, main});
                std::reverse(path.begin(), path.end());
                next[(size_t)in].push_back(Edge{outside, path, -1, main});
                continue;
            }
            if (ra < 0 || rb < 0) continue;
            for (int dirn = 0; dirn < 2; dirn++) {
                const int from = dirn ? rb : ra, to = dirn ? ra : rb;
                next[(size_t)from].push_back(Edge{to, {vec3(c + nl * (0.75f * side(w, c, from)), z), vec3(c + nl * (0.75f * side(w, c, to)), z)}, -1, false});
            }
        }
    std::stable_sort(next[(size_t)outside].begin(), next[(size_t)outside].end(), [](const Edge& a, const Edge& b) { return a.main && !b.main; });
    if (next[(size_t)outside].empty() || !next[(size_t)outside].front().main) return false;
    for (const MStair& st : P.stairs) {
        const int from = P.room(st.src->fromFloor, st.src->fromRoom), to = P.room(st.src->toFloor, st.src->toRoom);
        if (from < 0 || to < 0 || st.flights.empty()) continue;
        std::vector<vec3> up;
        const Flight &f0 = st.flights.front(), &f1 = st.flights.back();
        up.push_back(vec3(f0.s - f0.dir * 0.6f, P.floorZ(st.src->fromFloor)));
        for (size_t i = 0; i < st.run.size(); i++) up.push_back(vec3(st.run[i], P.floorZ(st.src->fromFloor)));
        up.push_back(vec3(f1.e + f1.dir * 0.6f, P.floorZ(st.src->toFloor)));
        next[(size_t)from].push_back(Edge{to, up, -2, false});
        std::reverse(up.begin(), up.end());
        next[(size_t)to].push_back(Edge{from, up, -2, false});
    }
    // outside, from one street door to the next: round the footprint's corners where the straight way crosses it
    const vec2 lo(d.x0 - 0.3f, -0.3f), hi(d.x1 + 0.3f, d.depth + 0.3f);
    const auto crosses = [&](vec2 a, vec2 b) {   // (the segment meets the footprint's box: a slab clip)
        float t0 = 0.f, t1 = 1.f;
        const vec2 dd = b - a;
        for (int ax = 0; ax < 2; ax++) {
            const float da = ax ? dd.y : dd.x, pa = ax ? a.y : a.x, l = ax ? lo.y : lo.x, h = ax ? hi.y : hi.x;
            if (fabsf(da) < 1e-6f) {
                if (pa < l || pa > h) return false;
                continue;
            }
            float u0 = (l - pa) / da, u1 = (h - pa) / da;
            if (u0 > u1) std::swap(u0, u1);
            t0 = Max(t0, u0);
            t1 = Min(t1, u1);
            if (t0 > t1) return false;
        }
        return true;
    };
    const auto around = [&](vec3 a, vec3 b) {   // the corners to pass between a and b (none, one or two)
        std::vector<vec3> best;
        if (!crosses(a.xy(), b.xy())) return best;
        const float E = 1.3f;
        const vec2 cs[4] = {vec2(d.x0 - E, -E), vec2(d.x1 + E, -E), vec2(d.x1 + E, d.depth + E), vec2(d.x0 - E, d.depth + E)};
        float bestLen = 1e9f;
        for (int i = 0; i < 4; i++)
            for (int n = 1; n <= 2; n++) {
                for (int dirn = -1; dirn <= 1; dirn += 2) {
                    std::vector<vec2> via = {cs[i]};
                    if (n == 2) via.push_back(cs[(i + dirn + 4) % 4]);
                    vec2 prev = a.xy();
                    float len = 0.f;
                    bool ok = true;
                    for (vec2 v : via) {
                        ok = ok && !crosses(prev, v);
                        len += length(v - prev);
                        prev = v;
                    }
                    ok = ok && !crosses(prev, b.xy());
                    len += length(b.xy() - prev);
                    if (ok && len < bestLen) {
                        bestLen = len;
                        best.clear();
                        for (vec2 v : via) best.push_back(vec3(v, a.z));
                    }
                }
            }
        return best;
    };
    // depth first from the outside, through the main entrance first: into each room not yet seen, to its middle, and
    // back the way it came. A stair's room has no middle to walk to (its middle is on its flight, or over its well): the
    // way goes from one of its doors or stairs straight to the next
    std::vector<vec3> path;
    std::vector<int> pathRoom;
    std::vector<bool> seen(P.rooms.size() + 1, false);
    const auto middle = [&](int r) { return vec3(P.rooms[(size_t)r].space.centre(), P.floorZ(P.rooms[(size_t)r].floor)); };
    const auto hasMiddle = [&](int r) { return r >= 0 && r < outside && P.rooms[(size_t)r].src->type != ROOM_STAIR; };
    const auto add = [&](vec3 q, int r) {
        path.push_back(q);
        pathRoom.push_back(r);
    };
    std::function<void(int)> visit = [&](int r) {
        seen[(size_t)r] = true;
        for (const Edge& e : next[(size_t)r]) {
            if (seen[(size_t)e.to]) continue;
            if (r == outside && !path.empty())
                for (const vec3& v : around(path.back(), e.path.front())) add(v, -1);
            for (const vec3& q : e.path) add(q, e.mark);
            if (hasMiddle(e.to)) add(middle(e.to), e.to);
            visit(e.to);
            for (size_t i = e.path.size(); i-- > 0;) add(e.path[i], e.mark);
            if (hasMiddle(r)) add(middle(r), r);
        }
    };
    visit(outside);
    for (size_t i = 0; i < path.size(); i++) {
        points.push_back(d.toWorld(path[i]));
        roomsOut.push_back(pathRoom[i]);
    }
    return true;
}

namespace ikit {

// ------------------------------------------------------------------------------------------------ the build
void layoutHand(IB& b) {
    using namespace interiorhand_detail;
    if (b.plan() || gInteriors != &gHandSet.set || !gHandBuildings) return;   // (the set holds the plan: prepareHandInteriors)
    const size_t di = (size_t)(b.d - gHandSet.set.defs.data());
    if (di >= gHandSet.placement.size()) return;
    const HandBuildings& hb = *gHandBuildings;
    const HandPlacement& p = hb.placements[(size_t)gHandSet.placement[di]];
    const HandShell& s = hb.shells[(size_t)p.shell];
    const InteriorDef& d = *b.d;
    const MPlan P = planOf(s, p);
    const vec3 Z(0.f, 0.f, 1.f);
    const u32 trim = Gy(0.95f), trimMat = M(MAT_PAINT_WHITE);
    std::vector<RoomFinish> fin;
    for (size_t r = 0; r < P.rooms.size(); r++) fin.push_back(finishOf(P.rooms[r].src->type, d.seed + (u32)r));
    // which room a wall's face looks into: the room whose space is just in front of it (its finish)
    const auto roomAt = [&](int floor, vec2 q) {
        for (size_t r = 0; r < P.rooms.size(); r++)
            if (P.rooms[r].floor == floor && P.rooms[r].space.contains(q, 0.02f)) return (int)r;
        return -1;
    };
    // ---- floors and ceilings, cut by the stairwells
    for (size_t ri = 0; ri < P.rooms.size(); ri++) {
        const MRoom& r = P.rooms[ri];
        const RoomFinish& f = fin[ri];
        const float z0 = P.floorZ(r.floor), zc = P.ceilZ(r.floor);
        InPart ip(b, IP_SHELL);
        for (const Rect& q : minus(r.space, wellsOn(P, r.floor)))
            quad(b, vec3(q.lo.x, q.lo.y, z0), vec3(q.hi.x, q.lo.y, z0), vec3(q.hi.x, q.hi.y, z0), vec3(q.lo.x, q.hi.y, z0), q.lo, vec2(q.hi.x, q.lo.y), q.hi,
                 vec2(q.lo.x, q.hi.y), f.floorCol, f.floorMat);
        const std::vector<Rect> above = (size_t)r.floor + 1 < s.floors.size() ? wellsOn(P, r.floor + 1) : std::vector<Rect>();
        for (const Rect& q : minus(r.space, above))
            quad(b, vec3(q.lo.x, q.hi.y, zc), vec3(q.hi.x, q.hi.y, zc), vec3(q.hi.x, q.lo.y, zc), vec3(q.lo.x, q.lo.y, zc), vec2(q.lo.x, q.hi.y), q.hi,
                 vec2(q.hi.x, q.lo.y), q.lo, f.ceilCol, f.ceilMat);
    }
    // ---- the walls' faces (each floor's from the slab under it, so a stairwell sees no gap), their openings' inner
    // reveals, the windows' inner glass and boards, the doorways' casings and frames, the baseboards
    for (const MWall& w : P.walls) {
        const float z0 = P.floorZ(w.floor), zc = P.ceilZ(w.floor), zb = w.floor > 0 ? z0 - kSlab : z0;
        const HandShell::Wall& src = *w.src;
        for (int faceSide = 0; faceSide < (w.exterior ? 1 : 2); faceSide++) {
            const vec2 n = faceSide == 0 ? w.n : -w.n;
            const Face fc = faceOf(w, n);
            if (fc.len < 0.02f) continue;
            // the face room by room: each room's finish along the part of the face that bounds it (the parts where inside
            // walls meet it are hidden in them)
            struct Seg {
                float s0, s1;
                int room;
            };
            std::vector<Seg> segs;
            const float plane = dot(fc.o, n);
            for (size_t r = 0; r < P.rooms.size(); r++) {
                const MRoom& m = P.rooms[r];
                if (m.floor != w.floor) continue;
                float lo = 1e9f, hi = -1e9f, nearest = 1e9f;
                for (int k = 0; k < 4; k++) {
                    const vec2 c = vec2(k & 1 ? m.space.hi.x : m.space.lo.x, k & 2 ? m.space.hi.y : m.space.lo.y);
                    lo = Min(lo, dot(c - fc.o, fc.u));
                    hi = Max(hi, dot(c - fc.o, fc.u));
                    nearest = Min(nearest, dot(c, n) - plane);
                }
                lo = Max(lo, 0.f);
                hi = Min(hi, fc.len);
                if (fabsf(nearest) < 0.04f && hi - lo > 0.02f) segs.push_back({lo, hi, (int)r});
            }
            if (segs.empty()) segs.push_back({0.f, fc.len, roomAt(w.floor, fc.o + fc.u * (fc.len * 0.5f) + n * 0.1f)});
            const auto finishAt = [&](float s) -> const RoomFinish& {
                for (const Seg& sg : segs)
                    if (s >= sg.s0 - 0.01f && s <= sg.s1 + 0.01f && sg.room >= 0) return fin[(size_t)sg.room];
                return segs[0].room >= 0 ? fin[(size_t)segs[0].room] : fin[0];
            };
            std::vector<Hole> holes, doors;
            for (const HandShell::Opening& o : src.openings) {
                const float sc = fc.s(o.at);
                Hole h;
                h.s0 = sc - o.w * 0.5f;
                h.s1 = sc + o.w * 0.5f;
                h.z0 = (o.door() ? z0 : z0 + o.sill) - zb;
                h.z1 = Min((o.door() ? z0 : z0 + o.sill) + o.h, zc) - zb;
                if (h.s0 > h.s1) std::swap(h.s0, h.s1);
                holes.push_back(h);
                if (o.door()) doors.push_back(h);
            }
            const vec3 o3(fc.o, zb), u3(fc.u, 0.f), n3(n, 0.f);
            for (const Seg& sg : segs) {
                const RoomFinish& f = sg.room >= 0 ? fin[(size_t)sg.room] : fin[0];
                std::vector<Hole> hs;
                for (Hole h : holes) {
                    h.s0 -= sg.s0;
                    h.s1 -= sg.s0;
                    hs.push_back(h);
                }
                const vec3 so = o3 + u3 * sg.s0;
                const float uOff = dot(fc.o, fc.u) + sg.s0, len = sg.s1 - sg.s0;
                InPart ip(b, IP_SHELL);
                if (f.tiles > 0.f) {   // a tiled dado, the plaster above it
                    const float th = Min(f.tiles + (z0 - zb), zc - zb);
                    std::vector<Hole> upper = hs;
                    for (Hole& h : upper) {
                        h.z0 -= th;
                        h.z1 -= th;
                    }
                    wallWithHoles(b, so, u3, n3, len, th, hs, Gy(0.9f), M(MAT_TILE), uOff);
                    wallWithHoles(b, so + Z * th, u3, n3, len, zc - zb - th, upper, f.wallCol, f.wallMat, uOff);
                } else {
                    wallWithHoles(b, so, u3, n3, len, zc - zb, hs, f.wallCol, f.wallMat, uOff);
                }
            }
            // a free end's cap, across the wall
            for (int end = 0; end < 2 && !w.exterior && faceSide == 0; end++) {
                if (!(end == 0 ? w.freeA : w.freeB)) continue;
                InPart ip(b, IP_SHELL);
                const RoomFinish& f = finishAt(end == 0 ? fc.s(0.f) : fc.s(w.len));
                const vec2 q = end == 0 ? w.a : w.b, out = end == 0 ? -w.dir : w.dir;
                const vec2 l = q + perp(w.dir) * (w.t * 0.5f), r2 = q - perp(w.dir) * (w.t * 0.5f);
                quadF(b, vec3(l, zb), vec3(r2, zb), vec3(r2, zc), vec3(l, zc), vec3(out, 0.f), f.wallCol, f.wallMat);
            }
            // the windows' inner reveals (to the shell's glass), their glass and boards; an exterior door's casing
            for (const HandShell::Opening& o : src.openings) {
                const vec2 c = w.a + w.dir * o.at;
                const float sc = fc.s(o.at), s0 = sc - o.w * 0.5f, s1 = sc + o.w * 0.5f;
                if (o.door()) {
                    if (w.exterior) {
                        InPart ip(b, IP_DETAIL);
                        casing(b, vec3(c, z0), w.dir, n, o.w, o.h, w.t * 0.5f, trim, trimMat);
                    }
                    continue;
                }
                if (!w.exterior) continue;
                const RoomFinish& f = finishAt(sc);
                const float depth = Max(w.t - Min(handshells_detail::kRecess, w.t * 0.6f), 0.02f);   // (to the shell's glass)
                const float wz0 = z0 + o.sill - zb, wz1 = Min(z0 + o.sill + o.h, zc) - zb;
                {
                    InPart ip(b, IP_SHELL);
                    reveal(b, o3, u3, -n3, s0, s1, wz0, wz1, 0.f, depth, f.wallCol, f.wallMat, true);
                    const vec3 g = o3 - n3 * (depth - 0.006f);
                    quadF(b, g + u3 * s0 + Z * wz0, g + u3 * s1 + Z * wz0, g + u3 * s1 + Z * wz1, g + u3 * s0 + Z * wz1, n3, glassCol(0.9f), kGlassMat);
                }
                InPart ip(b, IP_DETAIL);
                At at(b, o3 + u3 * sc + Z * (wz0 - 0.015f), atan2f(fc.u.y, fc.u.x));   // (local x along the face, y away from the room)
                box(b, vec3(0.f, -0.02f, 0.f), vec3(o.w * 0.5f + 0.05f, 0.04f, 0.015f), trim, trimMat, SK_NONE);
            }
            // the doorways of an inside wall: their jambs, threshold and casings (once per wall)
            if (!w.exterior && faceSide == 0)
                for (const HandShell::Opening& o : src.openings)
                    if (o.door()) doorFrameInner(b, vec3(w.a + w.dir * o.at, z0), w.dir, perp(w.dir), o.w, o.h, w.t, trim);
            // the baseboards, room by room between the doorways, and none along a stairwell (no floor there)
            InPart ip(b, IP_DETAIL);
            std::vector<std::pair<float, float>> runs;
            for (const Seg& sg : segs) runs.push_back({sg.s0, sg.s1});
            std::vector<Hole> gaps = doors;
            for (const Rect& wl : wellsOn(P, w.floor)) {
                float lo = 1e9f, hi = -1e9f, nearest = 1e9f;
                for (int k = 0; k < 4; k++) {
                    const vec2 c = vec2(k & 1 ? wl.hi.x : wl.lo.x, k & 2 ? wl.hi.y : wl.lo.y);
                    lo = Min(lo, dot(c - fc.o, fc.u));
                    hi = Max(hi, dot(c - fc.o, fc.u));
                    nearest = Min(nearest, dot(c, n) - plane);
                }
                if (fabsf(nearest) < 0.05f) gaps.push_back(Hole{lo + 0.07f, hi - 0.07f, 0.f, 1.f});
            }
            for (const Hole& h : gaps) {
                std::vector<std::pair<float, float>> kept;
                for (const auto& rn : runs) {
                    if (h.s1 <= rn.first || h.s0 >= rn.second) {
                        kept.push_back(rn);
                        continue;
                    }
                    if (h.s0 > rn.first) kept.push_back({rn.first, h.s0 - 0.07f});
                    if (h.s1 < rn.second) kept.push_back({h.s1 + 0.07f, rn.second});
                }
                runs = kept;
            }
            for (const auto& rn : runs) {
                if (rn.second - rn.first < 0.05f) continue;
                At at(b, vec3(fc.o + fc.u * ((rn.first + rn.second) * 0.5f), z0), atan2f(fc.u.y, fc.u.x));
                box(b, vec3(0.f, -0.008f, 0.045f), vec3((rn.second - rn.first) * 0.5f, 0.008f, 0.045f), trim, trimMat, SK_NZ);
            }
        }
        // collision: an inside wall between its doorways, and over them (the shell collides the exterior walls)
        if (!w.exterior) {
            std::vector<std::pair<float, float>> cuts;
            for (const HandShell::Opening& o : src.openings)
                if (o.door()) cuts.push_back({o.at - o.w * 0.5f, o.at + o.w * 0.5f});
            std::sort(cuts.begin(), cuts.end());
            float a0 = w.trimA;
            const float a1 = w.len - w.trimB;
            const float ang = atan2f(w.dir.y, w.dir.x);
            const auto seg = [&](float u0, float u1, float bz, float tz) {
                if (u1 - u0 < 0.02f || tz - bz < 0.02f) return;
                At at(b, vec3(w.a + w.dir * ((u0 + u1) * 0.5f), (bz + tz) * 0.5f), ang);
                collide(b, vec3(0.f), vec3((u1 - u0) * 0.5f, w.t * 0.5f, (tz - bz) * 0.5f));
            };
            for (const auto& c : cuts) {
                seg(a0, c.first, z0, zc);
                for (const HandShell::Opening& o : src.openings)
                    if (o.door() && fabsf(o.at - (c.first + c.second) * 0.5f) < 1e-3f) seg(c.first, c.second, z0 + o.h, zc);
                a0 = Max(a0, c.second);
            }
            seg(a0, a1, z0, zc);
        }
    }
    // ---- the stairs: a box per tread, a ramp of thin colliders three to a tread (the walk climbs it as the warehouse
    // mezzanine's: a collider's top stops it 0.35 m over its feet, and two treads of 0.18 m are more), the landings, and
    // the wells' edges and balustrades
    for (const MStair& st : P.stairs) {
        const int fromRoom = P.room(st.src->fromFloor, st.src->fromRoom);
        const bool common = fromRoom >= 0 && (P.rooms[(size_t)fromRoom].src->type == ROOM_LOBBY || P.rooms[(size_t)fromRoom].src->type == ROOM_CORRIDOR ||
                                              P.rooms[(size_t)fromRoom].src->type == ROOM_STAIR) &&
                            s.cls != shellClass("house") && s.cls != shellClass("shop");
        const u32 col = common ? C(0.74f, 0.71f, 0.66f) : C(0.55f, 0.38f, 0.24f), mat = common ? M(MAT_TILE) : M(MAT_WOOD);   // (terrazzo, or wood)
        for (const Flight& f : st.flights) {
            const float g = f.len / (float)f.risers, ang = atan2f(f.dir.y, f.dir.x);
            for (int i = 1; i <= f.risers; i++) {
                const float top = f.z0 + st.rise * (float)i, bot = top - st.rise - kTreadUnder;
                At at(b, vec3(f.s + f.dir * (g * ((float)i - 0.5f)), (top + bot) * 0.5f), ang);
                InPart ip(b, IP_SHELL);
                box(b, vec3(0.f), vec3(g * 0.5f, f.w * 0.5f, (top - bot) * 0.5f), col, mat, SK_NONE);
            }
            // (the ramp: a collider a third of a tread long, its top on the line through the treads' middles, so
            // within a third of a riser of the tread it lies on; the top ones level with the landing or floor above)
            for (int j = 1; j <= 3 * f.risers; j++) {
                const float top = f.z0 + st.rise * Min((float)(j + 1) / 3.f, (float)f.risers);
                At at(b, vec3(f.s + f.dir * (g * ((float)j - 0.5f) / 3.f), top - 0.1f), ang);
                collide(b, vec3(0.f), vec3(g / 6.f, f.w * 0.5f, 0.1f));
            }
        }
        for (const Landing& l : st.landings) {
            At at(b, vec3(l.c, l.z - kLandingT * 0.5f), atan2f(l.ax.y, l.ax.x));
            const vec3 he(l.hx, l.hy, kLandingT * 0.5f);
            {
                InPart ip(b, IP_SHELL);
                box(b, vec3(0.f), he, col, mat, SK_NONE);
            }
            collide(b, vec3(0.f), he);
        }
        // the well's edges in the slab, and a balustrade where the floor stops at it with no flight arriving or leaving
        const int F = st.src->toFloor;
        const float zF = P.floorZ(F);
        const Rect& wl = st.well;
        for (int side = 0; side < 4; side++) {
            const bool vertical = side < 2;
            const float edge = side == 0 ? wl.lo.x : side == 1 ? wl.hi.x : side == 2 ? wl.lo.y : wl.hi.y;
            const float lo = vertical ? wl.lo.y : wl.lo.x, hi = vertical ? wl.hi.y : wl.hi.x;
            const vec2 inward = vertical ? vec2(side == 0 ? 1.f : -1.f, 0.f) : vec2(0.f, side == 2 ? 1.f : -1.f);   // (into the well)
            const auto pt = [&](float s, float off) { return vertical ? vec2(edge + off, s) : vec2(s, edge + off); };
            if (!st.walled[side]) {
                InPart ip(b, IP_SHELL);
                quadF(b, vec3(pt(lo, 0.f), zF - kSlab), vec3(pt(hi, 0.f), zF - kSlab), vec3(pt(hi, 0.f), zF), vec3(pt(lo, 0.f), zF), vec3(inward, 0.f), Gy(0.9f),
                      M(MAT_PLASTER));
            } else {
                continue;
            }
            // the spans where a flight arrives at this floor or leaves it from this edge stay open
            std::vector<std::pair<float, float>> open;
            for (const MStair& o : P.stairs) {
                const bool arrives = o.src->toFloor == F, leaves = o.src->fromFloor == F;
                if (!arrives && !leaves) continue;
                const Flight& f = arrives ? o.flights.back() : o.flights.front();
                for (int end = 0; end < 2; end++) {
                    if ((end == 0 && !arrives) || (end == 1 && !leaves)) continue;
                    const vec2 q = end == 0 ? f.e : f.s;
                    if (fabsf((vertical ? q.x : q.y) - edge) > 0.06f) continue;
                    const float c = vertical ? q.y : q.x, hw = f.w * 0.5f;
                    open.push_back({c - hw, c + hw});
                }
            }
            std::sort(open.begin(), open.end());
            std::vector<std::pair<float, float>> rails;
            float a0 = lo;
            for (const auto& o : open) {
                if (o.first > a0) rails.push_back({a0, o.first});
                a0 = Max(a0, o.second);
            }
            if (hi > a0) rails.push_back({a0, hi});
            for (const auto& r : rails) {
                if (r.second - r.first < 0.25f) continue;
                const vec2 c = pt((r.first + r.second) * 0.5f, -0.03f) - inward * 0.f;
                const vec2 ax = vertical ? vec2(0.f, 1.f) : vec2(1.f, 0.f);
                At at(b, vec3(c - inward * 0.0f, zF + kRail * 0.5f), atan2f(ax.y, ax.x));
                const vec3 he((r.second - r.first) * 0.5f, 0.025f, kRail * 0.5f);
                {
                    InPart ip(b, IP_SHELL);
                    box(b, vec3(0.f), he, Gy(0.92f), trimMat, SK_NZ);
                    box(b, vec3(0.f, 0.f, kRail * 0.5f + 0.02f), vec3(he.x + 0.02f, 0.04f, 0.02f), C(0.5f, 0.35f, 0.22f), M(MAT_WOOD), SK_NONE);
                }
                collide(b, vec3(0.f), he);
            }
        }
    }
    // ---- the upper floors' slabs (the shell's foundation is the ground floor's), cut by the wells
    for (const MRoom& r : P.rooms) {
        if (r.floor == 0) continue;
        const float z0 = P.floorZ(r.floor);
        for (const Rect& q : minus(r.box, wellsOn(P, r.floor))) collideMM(b, vec3(q.lo, z0 - kSlab), vec3(q.hi, z0));
    }
    // ---- a room's lights: its fixtures under the ceiling, clear of a well above
    for (size_t ri = 0; ri < P.rooms.size(); ri++) {
        const MRoom& r = P.rooms[ri];
        const RoomFinish& f = fin[ri];
        const float zc = P.ceilZ(r.floor);
        const std::vector<Rect> above = (size_t)r.floor + 1 < s.floors.size() ? wellsOn(P, r.floor + 1) : std::vector<Rect>();
        Rect best = r.space;
        float bestA = -1.f;
        for (const Rect& q : minus(r.space, above))
            if (q.area() > bestA) {
                bestA = q.area();
                best = q;
            }
        const vec2 sz = best.hi - best.lo;
        const float radius = Max(4.5f, length(r.space.hi - r.space.lo) * 0.7f);
        const int count = sz.x * sz.y > 24.f ? 2 : 1;
        for (int k = 0; k < count; k++) {
            vec2 at = best.centre();
            if (count == 2) at = sz.x > sz.y ? vec2(Lerp(best.lo.x, best.hi.x, k ? 0.72f : 0.28f), at.y) : vec2(at.x, Lerp(best.lo.y, best.hi.y, k ? 0.72f : 0.28f));
            const vec3 top(at, zc);
            if (f.fixture == 2) troffer(b, top, 1.2f, 0.6f, (int)ri, 420.f, f.tint, radius);
            else if (f.fixture == 1) pendant(b, top, 0.55f, (int)ri, C(0.92f, 0.9f, 0.84f), 200.f, f.tint, radius, 2);
            else downlight(b, top, (int)ri, 260.f, f.tint, radius);
        }
    }
}

}  // namespace ikit
}  // namespace World
