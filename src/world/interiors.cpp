// Enterable interiors: planning at world generation (which buildings host which interior, facade openings, rooms,
// doors, daylight portals, NPC scenario points, gameplay markers) and the building shell hooks used by
// buildmesh.cpp (facade cut-outs, hollow collision). Room geometry: interiorkit.cpp + interiorlayouts.cpp.
#include "interiors.h"
#include "sites.h"

namespace World {

InteriorSet* gInteriors = nullptr;

namespace ikit {
void runLayout(IB& b);                                 // interiorlayouts.cpp
void doorLeaves(const InteriorDef& d, InteriorMesh& out);
}  // namespace ikit

namespace interior_plan {

const float kWallT = 0.3f;   // exterior wall thickness inside the facade plane (rooms start here)

// ---- story place resolution: mirrors mission_util.cpp resolvePlace() so interiors sit at the story places
bool driveable(const RoadEdge& e) { return !(e.flags & RF_ELEVATED) && e.cls != RC_HIGHWAY && e.cls != RC_RAMP; }

int nearestStreet(const RoadNetwork& R, vec2 p, float maxDist, float* sOut, float* sideOut, bool allowBridge) {
    std::vector<int> cand;
    R.edgesInRect(p - vec2(maxDist), p + vec2(maxDist), cand);
    int best = -1;
    float bestD = maxDist, bestS = 0.f, bestSide = 1.f;
    for (int ei : cand) {
        const RoadEdge& e = R.edges[ei];
        if (!driveable(e)) continue;
        if (!allowBridge && (e.flags & RF_BRIDGE)) continue;
        float acc = 0.f;
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            vec2 a = e.pts[k].xy(), b = e.pts[k + 1].xy();
            float t;
            float d = distPointSegment2D(p, a, b, &t);
            float seg = length(b - a);
            if (d < bestD) {
                bestD = d;
                best = ei;
                bestS = acc + t * seg;
                vec2 dir = normalize(b - a);
                bestSide = cross(dir, p - a) >= 0.f ? 1.f : -1.f;
            }
            acc += seg;
        }
    }
    if (best < 0) return R.nearestEdge(p, maxDist * 2.f, sOut, nullptr, sideOut);
    if (sOut) *sOut = bestS;
    if (sideOut) *sideOut = bestSide;
    return best;
}

struct PlaceLite {
    bool ok = false;
    vec2 sp;     // sidewalk point
    vec2 dir;    // street direction
    vec2 out;    // from the street toward the sidewalk / buildings
    vec3 c;      // road center point
};

PlaceLite resolve(const RoadNetwork& R, vec2 hint, bool allowBridge = false) {
    PlaceLite pl;
    float s = 0.f, side = 1.f;
    int e = nearestStreet(R, hint, 260.f, &s, &side, allowBridge);
    if (e < 0) return pl;
    const RoadEdge& ed = R.edges[e];
    float lo = Min(ed.cut0 + 3.f, ed.length * 0.5f), hi = Max(ed.length - ed.cut1 - 3.f, ed.length * 0.5f);
    s = Clamp(s, lo, hi);
    vec3 c = ed.posAt(s);
    vec3 t = ed.tangentAt(s);
    vec2 dir = normalize(vec2(t.x, t.y));
    vec2 out = perp(dir) * side;
    float walk = ed.halfWidth + Max(ed.sidewalk, 1.2f) * 0.5f;
    pl.ok = true;
    pl.sp = c.xy() + out * walk;
    pl.dir = dir;
    pl.out = out;
    pl.c = c;
    return pl;
}

inline u32 styleBit(int s) { return 1u << (u32)s; }
const u32 kRetail = styleBit(BS_SHOPS) | styleBit(BS_STRIPMALL) | styleBit(BS_MIDRISE) | styleBit(BS_DECO);

// Tower buildings are only usable when their ground floor is a rectangular podium or a rectangular shaft. Mirrors the
// first random draws of buildmesh.cpp (BS_TOWER) for the building's seed.
bool towerGroundRect(const Building& b) {
    Rng r(b.seed ^ 0xB111D1u);
    int podiumFloors = Min((int)b.floors, r.irange(2, 6));
    bool hasPodium = b.floors > podiumFloors + 3 && r.chance(0.75f);
    if (hasPodium) return true;
    int shape = r.irange(0, 9);
    return shape <= 4;
}

enum TargetFlags : u16 {
    TF_NOSTORE = 1,     // prefer buildings without a storefront facade (homes)
    TF_STORE = 2,       // prefer storefront facades
    TF_BRIDGE = 4,      // the story place may resolve onto a bridge
    TF_OWNSHELL = 8,    // the interior replaces the whole building (trailer)
    TF_TALL = 16,       // exposed-structure ceiling up to the top of the facade glass (garages, lobbies, warehouses)
    TF_ROLLUP = 32,     // roll-up vehicle doors (at the building's loading doors)
    TF_ONEFLOOR = 64,   // single-storey buildings only
    TF_WIDEDOOR = 128,  // double entrance doors (lobbies)
};

struct Target {
    u8 kind;
    const char* name;
    vec2 hint;
    u32 styles;
    float minW, minD;
    float maxW, maxD;    // region caps (0 = building size)
    u16 flags;
    const char* alias;
    int upper = 0;       // residential tower unit: floor number (> 0) or from the top (-1 = top floor); the ground
                         // floor then holds its lobby (IK_RES_LOBBY, "<name> Lobby"), linked by the elevator
};

// Painted loading doors of warehouse meshes (buildmesh.cpp): count and footprint coordinate along Building::ax
bool hasLoadingDoors(const Building& b) { return b.style == BS_WAREHOUSE || b.style == BS_FACTORY || b.style == BS_BARN; }
int loadingDoorCount(const Building& b) { return Max(1, (int)(b.hx / 7.f)); }
float loadingDoorU(const Building& b, int k) { return -b.hx + (k + 0.5f) * (2.f * b.hx / loadingDoorCount(b)); }

// A neighbour's attached garage wing (buildmesh.cpp: suburban houses and villas) reaching into a building's footprint:
// its walls, roof and collision would stand inside the interior
bool garageIntrudes(const BuildingSet& bs, int bi) {
    const Building& b = bs.buildings[bi];
    std::vector<int> around;
    bs.buildingsNear(b.c, b.hx + b.hy + 20.f, around);
    vec2 by = perp(b.ax);
    for (int j : around) {
        if (j == bi) continue;
        const Building& h = bs.buildings[j];
        bool garage = (h.style == BS_HOUSE && (h.seed % 10u) < 7u) || h.style == BS_VILLA;
        if (!garage) continue;
        float side = (h.seed & 64u) ? 1.f : -1.f;
        float gw = 3.2f, gd = Min(h.hy, 3.4f);
        vec2 gc = h.c + h.ax * (side * (h.hx + gw)) + h.front * (h.hy - gd);
        vec2 hy2 = perp(h.ax);
        // separating axis test between the two rectangles (garage wing vs footprint, 0.2 m tolerance)
        vec2 axes[4] = {b.ax, by, h.ax, hy2};
        bool overlap = true;
        for (vec2 a : axes) {
            float rb = b.hx * fabsf(dot(b.ax, a)) + b.hy * fabsf(dot(by, a));
            float rg = gw * fabsf(dot(h.ax, a)) + gd * fabsf(dot(hy2, a));
            if (fabsf(dot(gc - b.c, a)) > rb + rg - 0.2f) {
                overlap = false;
                break;
            }
        }
        if (overlap) return true;
    }
    return false;
}

// Building closest to the place whose front faces the place's street
int pickBuilding(const BuildingSet& bs, const PlaceLite& pl, const Target& tg, const std::vector<u8>& used, int pass) {
    std::vector<int> cand;
    bs.buildingsNear(pl.sp, pass == 0 ? 70.f : (pass < 3 ? 160.f : 700.f), cand);   // pass 3: upper units only, farther out
    int best = -1;
    float bestScore = 1e30f;
    for (int i : cand) {
        const Building& b = bs.buildings[i];
        if (used[i] || b.interior >= 0) continue;
        bool styleOk = (tg.styles & styleBit(b.style)) != 0;
        if (pass < 2 && !styleOk) continue;
        if (b.style == BS_TOWER && !towerGroundRect(b)) continue;
        if (!(tg.flags & TF_OWNSHELL) && garageIntrudes(bs, i)) continue;
        if (tg.upper < 0 && b.floors < 14) continue;                     // penthouses: tall towers with a view
        if (tg.upper > 0 && b.floors < tg.upper + 2) continue;
        // upper units need a plain extruded mass: towers inset above their podium and step back in tiers, so a unit
        // planned from the ground-floor frontage would hang outside the upper floors (condos / midrises hosting an
        // interior are single boxes, see buildmesh.cpp)
        if (tg.upper != 0 && b.style != BS_CONDO && b.style != BS_MIDRISE) continue;
        if (b.style == BS_CHURCH || (b.style == BS_GASSTATION && !(tg.styles & styleBit(BS_GASSTATION)))) continue;
        if (2.f * b.hx < tg.minW || 2.f * b.hy < tg.minD) continue;
        bool store = (bs.facades[b.facade].flags & 1u) != 0;
        if (pass < 2 && (tg.flags & TF_NOSTORE) && store) continue;
        if (pass < 2 && (tg.flags & TF_STORE) && !store) continue;
        if ((tg.flags & (TF_ONEFLOOR | TF_OWNSHELL)) && b.floors > 1 && pass < 2) continue;
        float facing = dot(b.front, -pl.out);
        if (pass == 0 && facing < 0.85f) continue;
        if (pass < 3 && facing < 0.3f) continue;
        vec2 fc = b.c + b.front * b.hy;
        float along = fabsf(dot(fc - pl.sp, pl.dir));
        float across = dot(fc - pl.sp, pl.out);
        if (pass == 0 && (across < -2.f || across > 18.f)) continue;
        float score = Max(0.f, along - b.hx * 0.5f) + fabsf(across) * 0.4f + (styleOk ? 0.f : 40.f) + (1.f - facing) * 30.f;
        if (pass == 3) score = length(b.c - pl.sp) + (styleOk ? 0.f : 40.f);
        if (hasLoadingDoors(b) && !(tg.flags & TF_ROLLUP)) score += 80.f;   // painted dock doors would cover the entrance
        if (score < bestScore) {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

}  // namespace interior_plan

using namespace interior_plan;

// ------------------------------------------------------------------------------------------------ queries
int InteriorSet::at(vec3 p, int* roomOut) const {
    for (size_t i = 0; i < defs.size(); i++) {
        const InteriorDef& d = defs[i];
        vec2 dd = p.xy() - d.origin.xy();
        if (dot(dd, dd) > (d.radius + 5.f) * (d.radius + 5.f)) continue;
        int r = d.roomAt(d.toLocal(p), 0.05f);
        if (r >= 0 && !d.rooms[r].outdoor) {
            if (roomOut) *roomOut = r;
            return (int)i;
        }
    }
    if (roomOut) *roomOut = -1;
    return -1;
}
int InteriorSet::byName(const char* name) const {
    if (!name) return -1;
    for (size_t i = 0; i < defs.size(); i++)
        if (defs[i].name == name || (!defs[i].alias.empty() && defs[i].alias == name)) return (int)i;
    return -1;
}
int InteriorSet::byKind(u8 kind, int nth) const {
    for (size_t i = 0; i < defs.size(); i++)
        if (defs[i].kind == kind && nth-- == 0) return (int)i;
    return -1;
}

bool interiorMarkerWorld(const char* name, u8 kind, vec3& outPos, float* outYaw) {
    if (!gInteriors) return false;
    int i = gInteriors->byName(name);
    if (i < 0) return false;
    const InteriorDef& d = gInteriors->defs[i];
    const InteriorMarker* m = d.marker(kind);
    if (!m) return false;
    outPos = d.toWorld(m->pos);
    if (outYaw) *outYaw = d.yawToWorld(m->yaw);
    return true;
}

bool interiorDoorOutside(u8 kind, int nth, vec3& outPos, float* outYaw) {
    if (!gInteriors) return false;
    int i = gInteriors->byKind(kind, nth);
    if (i < 0) return false;
    const InteriorDef& d = gInteriors->defs[i];
    const InteriorMarker* m = d.marker(IM_DOOR_OUT);
    if (!m) return false;
    outPos = d.toWorld(m->pos);
    if (outYaw) *outYaw = d.yawToWorld(m->yaw);
    return true;
}

// ------------------------------------------------------------------------------------------------ plan helpers
namespace interior_plan {

// Frame of a building: origin at the front facade center on the ground floor, x = right looking in, y = in.
void setFrame(InteriorDef& d, const Building& b, const FacadeGPU& f) {
    vec2 yin = -b.front;
    d.ax = vec2(yin.y, -yin.x);
    d.ay = yin;
    d.origin = vec3(b.c + b.front * b.hy, b.baseZ);
    float len = 2.f * b.hx;
    d.bays = Max(1, (int)roundf(len / Max(f.bayW, 0.5f)));
    d.bw = len / (float)d.bays;
    d.bayX0 = -b.hx;
    d.storefront = (f.flags & 1u) != 0;
}

void addOpening(InteriorDef& d, float xa, float xb, float z0, float z1, u8 kind) {
    InteriorOpening op;
    op.a = d.toWorld(vec3(xa, 0.f, 0.f)).xy();
    op.b = d.toWorld(vec3(xb, 0.f, 0.f)).xy();
    op.z0 = d.origin.z + z0;
    op.z1 = d.origin.z + z1;
    op.kind = kind;
    d.openings.push_back(op);
}

}  // namespace interior_plan

namespace interior_plan {

// Front facade window rectangle of the ground floor (as the facade shader draws it), in the interior frame
struct FrontWindow {
    float x0, x1, z0, z1;
};
FrontWindow frontWindow(const InteriorDef& d, const FacadeGPU& f, int bay) {
    float fh = f.groundH;
    int style = (int)f.style;
    float w = f.winW, h = fh * f.winH, sill = f.sillH;
    bool store = (f.flags & 1u) != 0;
    if (store) {
        w = 0.92f;
        sill = 0.35f;
        h = fh - 1.3f - ((f.flags & 2u) ? 0.1f : 0.f);
    } else if (style == 1) {
        w = 1.f - 0.12f / Max(f.bayW, 0.2f);
        h = fh - 0.1f;
        sill = 0.05f;
    } else if (style == 2) {
        w = 1.f;
        sill = fh * 0.38f;
        h = fh * 0.45f;
    } else if (style == 4) {
        h = fh * 0.25f;
        sill = fh * 0.6f;
        w = 0.5f;
    }
    FrontWindow fw;
    float x0 = (1.f - w) * 0.5f;
    fw.x0 = d.bayX0 + (bay + x0) * d.bw;
    fw.x1 = d.bayX0 + (bay + x0 + w) * d.bw;
    fw.z0 = sill;
    fw.z1 = sill + h;
    return fw;
}

// ------------------------------------------------------------------------------------------------ portals
void computePortals(InteriorDef& d) {
    d.portals.clear();
    for (size_t ri = 0; ri < d.rooms.size(); ri++) {
        const InteriorRoom& r = d.rooms[ri];
        if (r.outdoor) continue;
        for (int side = 0; side < 4; side++) {
            vec3 o, u, n;
            float w;
            ikit::faceFrame(r, side, o, u, n, w);
            for (const InteriorOpening& op : d.openings) {
                ikit::Hole h;
                float dep;
                if (!ikit::openingOnFace(d, op, r, side, h, dep)) continue;
                InteriorPortal p;
                p.p0 = o + u * h.s0 + vec3(0, 0, r.mn.z + Max(h.z0, 0.f));
                p.u = u * (h.s1 - h.s0);
                p.v = vec3(0, 0, Min(h.z1, r.mx.z - r.mn.z) - Max(h.z0, 0.f));
                p.room = (int)ri;
                p.transmission = op.kind == OP_GLASS ? 0.8f : 1.f;
                p.door = -1;
                if (op.kind == OP_DOOR || op.kind == OP_ROLLUP) {
                    vec3 mid = d.toLocal(vec3((op.a + op.b) * 0.5f, op.z0));
                    float bestD = 3.f;
                    for (size_t di = 0; di < d.doors.size(); di++) {
                        if (!d.doors[di].exterior) continue;
                        float dd = length(d.doors[di].c.xy() - mid.xy());
                        if (dd < bestD) {
                            bestD = dd;
                            p.door = (int)di;
                        }
                    }
                }
                if (length(p.u) > 0.05f && length(p.v) > 0.05f) d.portals.push_back(p);
            }
        }
    }
}

}  // namespace interior_plan

// ------------------------------------------------------------------------------------------------ planning
namespace interior_plan {

// Hollow ground-floor region, facade openings and ceiling of a building-hosted interior
void planShell(InteriorDef& d, const Building& b, const FacadeGPU& f, const Target& tg) {
    float w = 2.f * b.hx;
    float regionW = tg.maxW > 0.f ? Min(w, tg.maxW) : w;
    int doorBay = d.bays / 2;
    if (regionW < w - 0.01f) {
        // snap the region to whole bays around the entrance bay
        int nb = Max(1, (int)floorf(regionW / d.bw));
        int first = Clamp(doorBay - nb / 2, 0, d.bays - nb);
        d.x0 = d.bayX0 + first * d.bw;
        d.x1 = d.x0 + nb * d.bw;
    } else {
        d.x0 = -b.hx;
        d.x1 = b.hx;
    }
    d.depth = Min(2.f * b.hy, tg.maxD > 0.f ? tg.maxD : 20.f);
    float gH = f.groundH;
    d.shellTop = Max(gH - 0.25f, 3.0f);
    if (b.floors <= 1) d.shellTop = Max(b.height - 0.4f, 3.0f);
    if (f.flags & 2u) {
        d.signZ0 = gH - 1.05f;
        d.signZ1 = gH - 0.15f;
    }
    int firstBay = (int)roundf((d.x0 - d.bayX0) / d.bw), lastBay = (int)roundf((d.x1 - d.bayX0) / d.bw) - 1;
    float topWin = 0.f;
    for (int k = firstBay; k <= lastBay; k++) topWin = Max(topWin, frontWindow(d, f, k).z1);
    // roll-up doors: at the painted loading doors of warehouse meshes, else one in the entrance bay
    std::vector<float> rollX;
    float rollW = 3.6f, rollH = Min(4.2f, gH - 0.6f);   // matches the painted loading doors (dock bumpers flank them)
    if (tg.flags & TF_ROLLUP) {
        if (hasLoadingDoors(b)) {
            float sgn = dot(b.ax, d.ax) >= 0.f ? 1.f : -1.f;
            for (int k = 0; k < loadingDoorCount(b); k++) {
                float x = loadingDoorU(b, k) * sgn;
                if (x - rollW * 0.5f < d.x0 + 0.5f || x + rollW * 0.5f > d.x1 - 0.5f) continue;
                rollX.push_back(x);
            }
        }
        if (rollX.empty()) {
            rollW = Min(rollW, d.bw - 0.5f);
            rollX.push_back(d.bayX0 + (doorBay + 0.5f) * d.bw);
        }
        std::sort(rollX.begin(), rollX.end());
        if (rollX.size() > 3) rollX.resize(3);
    }
    // ceiling
    d.ceil = Clamp(gH - 0.6f, 2.8f, 4.2f);
    if (d.storefront) d.ceil = Max(d.ceil, frontWindow(d, f, 0).z1 + 0.12f);
    if (tg.flags & TF_TALL) d.ceil = Max(Max(topWin, rollX.empty() ? 0.f : rollH + 0.7f), 4.6f) + 0.15f;
    d.ceil = Min(d.ceil, d.shellTop - 0.1f);
    // pedestrian entrance: the entrance bay, or beside the roll-up doors
    float dc = d.bayX0 + (doorBay + 0.5f) * d.bw;
    bool wide = (tg.flags & TF_WIDEDOOR) != 0;
    float dw = d.storefront ? Min(wide ? 2.4f : 1.9f, d.bw * 0.92f * 0.8f) : Min(wide ? 2.0f : 1.1f, d.bw * 0.7f);
    for (float rx : rollX)
        if (fabsf(dc - rx) < rollW * 0.5f + dw * 0.5f + 0.6f) {
            float right = rx + rollW * 0.5f + 0.9f + dw * 0.5f, left = rx - rollW * 0.5f - 0.9f - dw * 0.5f;
            dc = right + dw * 0.5f < d.x1 - 0.6f ? right : left;
        }
    doorBay = Clamp((int)floorf((dc - d.bayX0) / d.bw), 0, d.bays - 1);
    d.doorBay = doorBay;
    float glassH = gH - 1.3f;
    float dh = d.storefront && rollX.empty() ? Min(2.3f, glassH) : 2.2f;
    // glass: every front bay of the region (storefronts), or every window but the entrance's; clipped to the ceiling
    for (int k = firstBay; k <= lastBay; k++) {
        FrontWindow fw = frontWindow(d, f, k);
        fw.z1 = Min(fw.z1, d.ceil - 0.02f);
        if (fw.z1 - fw.z0 < 0.4f) continue;
        bool blocked = false;
        for (float rx : rollX)
            if (fw.x1 > rx - rollW * 0.5f - 0.3f && fw.x0 < rx + rollW * 0.5f + 0.3f && fw.z0 < rollH + 0.3f) blocked = true;
        if (blocked) continue;
        if (d.storefront && rollX.empty()) addOpening(d, fw.x0, fw.x1, fw.z0, fw.z1, OP_GLASS);
        else if (k != doorBay || fw.z0 > dh + 0.3f) addOpening(d, fw.x0, fw.x1, fw.z0, fw.z1, OP_GLASS);
    }
    for (float rx : rollX) addOpening(d, rx - rollW * 0.5f, rx + rollW * 0.5f, 0.f, rollH, OP_ROLLUP);
    addOpening(d, dc - dw * 0.5f, dc + dw * 0.5f, 0.f, dh, OP_DOOR);
    if (!d.storefront || !rollX.empty()) {
        // the window of the entrance bay above the door (a transom lite) when it reaches down to the door head
        FrontWindow fw = frontWindow(d, f, doorBay);
        fw.z1 = Min(fw.z1, d.ceil - 0.02f);
        if (fw.z0 < dh + 0.3f && fw.z1 > dh + 0.25f && fw.x1 > dc - dw * 0.5f && fw.x0 < dc + dw * 0.5f)
            addOpening(d, Max(fw.x0, dc - dw * 0.5f), Min(fw.x1, dc + dw * 0.5f), dh, fw.z1, OP_GLASS);
    }
    d.radius = length(vec2((d.x1 - d.x0) * 0.5f, d.depth * 0.5f)) + 2.f;
}

// Club Riptide: open-air beach club laid out like the story mission (story_act2.cpp buildClub): 34 m inland from
// the shoreline at the club place's latitude, x = -(north), y = east toward the sea
bool planClub(const WorldMap& map, const RoadNetwork& roads, InteriorDef& d) {
    PlaceLite pl = resolve(roads, vec2(5380.f, 900.f));
    if (!pl.ok) return false;
    vec2 cp = pl.sp;
    float shoreX = cp.x + 60.f;
    for (float x = cp.x; x < cp.x + 400.f; x += 2.f)
        if (map.isWater(x, cp.y)) {
            shoreX = x;
            break;
        }
    vec2 anchor(shoreX - 34.f, cp.y);
    d.kind = IK_CLUB;
    d.name = "Club Riptide";
    d.building = -1;
    d.seed = hashString("Club Riptide") ^ 0x5EA5u;
    d.ay = vec2(1.f, 0.f);
    d.ax = vec2(d.ay.y, -d.ay.x);
    d.origin = vec3(anchor, map.heightAt(anchor.x, anchor.y));
    d.x0 = -55.f;
    d.x1 = 17.f;
    d.depth = Max(24.f, shoreX - anchor.x - 2.f);
    d.ceil = 4.f;
    d.shellTop = 6.f;
    d.bw = d.x1 - d.x0;
    d.bayX0 = d.x0;
    d.bays = 1;
    d.ownShell = true;
    d.radius = length(vec2((d.x1 - d.x0) * 0.5f, d.depth * 0.5f)) + 4.f;
    return true;
}

// Upper-floor unit of a residential tower above its lobby: same frame and bay grid, the floor flush with the condo
// balcony slabs (buildmesh.cpp BS_CONDO), a region of whole bays around the center of the front facade
bool planUpperUnit(InteriorDef& u, const InteriorDef& lobby, const Building& b, const FacadeGPU& f, const Target& tg) {
    if (b.floors < 3) return false;
    int fl = tg.upper > 0 ? Min(tg.upper, (int)b.floors - 1) : Max(1, (int)b.floors + tg.upper);
    u.kind = tg.kind;
    u.name = tg.name;
    if (tg.alias) u.alias = tg.alias;
    u.building = lobby.building;
    u.seed = hash32(lobby.seed ^ 0x0917u ^ (u32)fl);
    u.ax = lobby.ax;
    u.ay = lobby.ay;
    u.bw = lobby.bw;
    u.bayX0 = lobby.bayX0;
    u.bays = lobby.bays;
    u.storefront = false;
    float lineZ = b.baseZ + f.groundH + (fl - 1) * f.floorH;
    u.origin = vec3(lobby.origin.x, lobby.origin.y, lineZ + 0.12f);
    float regionW = Min(2.f * b.hx - 1.f, tg.maxW > 0.f ? tg.maxW : 18.f);
    int nb = Clamp((int)floorf(regionW / u.bw), 3, u.bays);
    int first = Clamp(u.bays / 2 - nb / 2, 0, u.bays - nb);
    u.x0 = u.bayX0 + first * u.bw;
    u.x1 = u.x0 + nb * u.bw;
    u.depth = Min(2.f * b.hy - 1.f, tg.maxD > 0.f ? tg.maxD : 12.f);
    u.ceil = Clamp(f.floorH - 0.45f, 2.6f, 3.4f);
    u.shellTop = f.floorH - 0.16f;
    u.radius = length(vec2((u.x1 - u.x0) * 0.5f, u.depth * 0.5f)) + 2.f;
    return true;
}

// Solaris One (sites.cpp SK_SOLARIS, drawn by landmarks.cpp genSolaris): the lobby behind the south face of the 72 m
// podium (its bay grid starts at x = -28 m; floor on the plaza pad) and Sandoval's penthouse in the top tower segment
// of the detailed model (30 segments; the floor on the facade's 4.1 m floor grid above its 9 m ground floor). The
// landmark cuts both into its facade rings and hollows its collision (interiorFacadeRing / interiorShellCollision).
bool planSolaris(InteriorDef& lobby, InteriorDef& ph) {
    if (!gSites) return false;
    const SiteElem* se = nullptr;
    for (const SiteElem& e : gSites->elems)
        if (e.kind == SK_SOLARIS) {
            se = &e;
            break;
        }
    if (!se) return false;
    const vec2 c = se->c;
    const float z0 = se->z;
    lobby.kind = IK_TOWER_LOBBY;
    lobby.name = "Solaris One";
    lobby.building = -1;
    lobby.seed = hashString("Solaris One lobby");
    lobby.ay = vec2(0.f, 1.f);
    lobby.ax = vec2(lobby.ay.y, -lobby.ay.x);
    lobby.origin = vec3(c.x, c.y - 36.f, z0 + 0.45f);
    lobby.x0 = -18.4f;
    lobby.x1 = 18.4f;
    lobby.depth = 31.f;
    lobby.ceil = 8.3f;
    lobby.shellTop = 8.45f;
    lobby.bw = 1.6f;
    lobby.bayX0 = lobby.x0;
    lobby.bays = 23;
    lobby.doorBay = 11;
    lobby.storefront = true;
    lobby.radius = length(vec2(18.4f, 15.5f)) + 2.f;
    // top segment of genSolaris (towerTop 468 m, 30 segments in the detailed model)
    const float towerTop = 468.f;
    const int segs = 30, k = segs - 1;
    const float segH = (towerTop - 26.f) / segs;
    const float t0 = (float)k / segs, t1 = (float)(k + 1) / segs, tm = (t0 + t1) * 0.5f;
    const float za = z0 + 26.f + k * segH;
    const float half = Lerp(30.f, 18.f, powf(tm, 1.2f));
    const float rot = tm * 58.f * kDegToRad;
    const float v = ceilf((za + 0.9f - z0 - 9.f) / 4.1f) * 4.1f + 9.f;
    ph.kind = IK_PENTHOUSE;
    ph.name = "Solaris One Penthouse";
    ph.building = -1;
    ph.seed = hashString("Solaris One penthouse");
    ph.ay = vec2(-sinf(rot), cosf(rot));
    ph.ax = vec2(ph.ay.y, -ph.ay.x);
    ph.origin = vec3(c - ph.ay * half, z0 + v);
    ph.x0 = -half;
    ph.x1 = half;
    ph.depth = 2.f * half;
    ph.ceil = 8.2f;
    ph.shellTop = 8.35f;
    ph.bw = 1.6f;
    ph.bayX0 = -half;
    ph.bays = Max(1, (int)roundf(2.f * half / 1.6f));
    ph.radius = half * 1.42f + 1.f;
    return true;
}

}  // namespace interior_plan

void planInteriors(WorldMap& map, const RoadNetwork& roads, BuildingSet& bs) {
    double t0 = TimeSeconds();
    static InteriorSet set;
    set.defs.clear();
    gInteriors = &set;
    ikit::tPlanBuildings = &bs;   // layouts read their host building in plan mode
    for (Building& b : bs.buildings) b.interior = -1;
    std::vector<u8> used(bs.buildings.size(), 0);
    const u32 kRetailWide = kRetail | styleBit(BS_TOWER) | styleBit(BS_CONDO);
    const u32 kHomes = styleBit(BS_MIDRISE) | styleBit(BS_CONDO) | styleBit(BS_DECO) | styleBit(BS_SHOPS);
    const u32 kIndustrial = styleBit(BS_WAREHOUSE) | styleBit(BS_FACTORY);
    const u32 kCivic = styleBit(BS_TOWER) | styleBit(BS_MIDRISE) | styleBit(BS_DECO) | styleBit(BS_CONDO) | styleBit(BS_SHOPS);
    // Story places (mission_util.cpp computePlaces), shops (shops.cpp) and extra city interiors. Order matters for
    // byKind(): Mari's apartment is the first IK_APARTMENT.
    const Target targets[] = {
        {IK_APARTMENT, "Mari's Apartment", vec2(1720.f, 360.f), kHomes, 9.f, 11.f, 13.f, 11.5f, TF_NOSTORE, nullptr},
        {IK_DINER, "Mama Lucha's", vec2(1330.f, 330.f), kRetail, 11.f, 14.f, 17.f, 16.f, TF_STORE, "Lucha's Diner Partnership"},
        {IK_CHOPSHOP, "Rook's Garage", vec2(1560.f, 2260.f), kIndustrial, 14.f, 16.f, 22.f, 20.f, TF_TALL | TF_ROLLUP, nullptr},
        {IK_TRAILER, "Dex's Trailer", vec2(1250.f, 2650.f), styleBit(BS_HOUSE), 11.f, 9.f, 0.f, 0.f, TF_OWNSHELL, nullptr},
        {IK_POLICE, "Police Headquarters", vec2(3050.f, -350.f), kCivic, 14.f, 14.f, 24.f, 16.f, TF_TALL | TF_WIDEDOOR, nullptr},
        {IK_HOSPITAL, "Tidewater General Hospital", vec2(1650.f, 1050.f), kCivic, 12.f, 14.f, 20.f, 16.f, TF_STORE | TF_WIDEDOOR, nullptr},
        {IK_GUNSHOP, "Palmetto Arms", vec2(800.f, 2900.f), kRetail, 9.f, 12.f, 14.f, 14.f, TF_STORE, nullptr},
        {IK_GUNSHOP, "Northside Arms", vec2(3300.f, 3900.f), kRetail, 9.f, 12.f, 14.f, 14.f, TF_STORE, nullptr},
        {IK_CLOTHES, "Threads", vec2(5150.f, -300.f), kRetail, 10.f, 12.f, 16.f, 15.f, TF_STORE, nullptr},
        {IK_WAREHOUSE, "Port Isle Warehouse", vec2(4004.f, -820.f), styleBit(BS_WAREHOUSE), 30.f, 30.f, 47.f, 38.f, TF_TALL | TF_ROLLUP, nullptr},
        {IK_CONVENIENCE, "TideStop Mart", vec2(1480.f, -260.f), kRetail, 8.f, 12.f, 16.f, 17.f, 0, nullptr},
        {IK_CONVENIENCE, "Bodega La Luna", vec2(2250.f, 520.f), kRetail, 8.f, 12.f, 14.f, 17.f, 0, nullptr},
        {IK_CONVENIENCE, "Canvas Corner Market", vec2(3000.f, 1500.f), kRetail, 8.f, 12.f, 16.f, 17.f, 0, nullptr},
        {IK_CONVENIENCE, "Sunrise Food Mart", vec2(5230.f, 600.f), kRetailWide, 8.f, 12.f, 16.f, 17.f, 0, nullptr},
        {IK_CONVENIENCE, "Northside Quick Stop", vec2(2300.f, 3800.f), kRetail, 8.f, 12.f, 16.f, 17.f, 0, nullptr},
        {IK_CONVENIENCE, "Flats Food & Fuel", vec2(420.f, 3350.f), kRetail | styleBit(BS_WAREHOUSE), 8.f, 12.f, 16.f, 17.f, 0, nullptr},
        {IK_CONVENIENCE, "Grove Pantry", vec2(2150.f, -3100.f), kRetail | styleBit(BS_STRIPMALL), 8.f, 12.f, 16.f, 17.f, 0, nullptr},
        {IK_DEALERSHIP, "Palm Motors", vec2(2700.f, 1300.f), kRetail, 14.f, 14.f, 24.f, 20.f, TF_STORE | TF_TALL | TF_ROLLUP | TF_WIDEDOOR, nullptr},
        {IK_MODSHOP, "Tide Customs Calle Luna", vec2(2300.f, -150.f), kRetail | kIndustrial, 11.f, 13.f, 18.f, 18.f, TF_TALL | TF_ROLLUP, nullptr},
        {IK_MODSHOP, "Tide Customs Sol Beach", vec2(5100.f, 2000.f), kRetail | kIndustrial, 11.f, 13.f, 18.f, 18.f, TF_TALL | TF_ROLLUP, nullptr},
        {IK_CARWASH, "Sunwash Car Wash", vec2(1800.f, 1500.f), kIndustrial | kRetail, 10.f, 14.f, 16.f, 20.f, TF_TALL | TF_ROLLUP, nullptr},
        {IK_APARTMENT, "Key Coral Villa", vec2(4450.f, -4200.f), styleBit(BS_VILLA) | styleBit(BS_HOUSE), 12.f, 11.f, 15.f, 12.f, TF_NOSTORE, nullptr},
        // residential towers: lobby downstairs, the unit upstairs (upper: floor, -1 = top floor)
        {IK_CONDO, "Sol Beach Condo", vec2(5120.f, 1500.f), styleBit(BS_CONDO), 18.f, 16.f, 16.f, 12.f, TF_NOSTORE, nullptr, 9},
        {IK_CONDO, "Downtown Penthouse", vec2(3528.f, -706.f), styleBit(BS_CONDO), 20.f, 16.f, 22.f, 14.f, TF_NOSTORE, nullptr, -1},
    };
    auto finish = [&](InteriorDef& d) {
        // rooms, doors, scenario points, markers (layout in plan mode), daylight portals
        ikit::IB ib;
        ib.d = &d;
        ikit::runLayout(ib);
        computePortals(d);
    };
    for (const Target& tg : targets) {
        PlaceLite pl = resolve(roads, tg.hint, (tg.flags & TF_BRIDGE) != 0);
        if (!pl.ok) continue;
        int bi = -1;
        for (int pass = 0; pass < (tg.upper != 0 ? 4 : 3) && bi < 0; pass++) bi = pickBuilding(bs, pl, tg, used, pass);
        if (bi < 0) {
            LOG("Interiors: no building for %s near (%.0f, %.0f)", tg.name, tg.hint.x, tg.hint.y);
            continue;
        }
        Building& b = bs.buildings[bi];
        const FacadeGPU& f = bs.facades[b.facade];
        InteriorDef d;
        d.kind = tg.kind;
        d.name = tg.name;
        if (tg.alias) d.alias = tg.alias;
        d.building = bi;
        d.seed = hash32(b.seed ^ ((u32)tg.kind * 0x9E3779B9u) ^ hashString(tg.name));
        setFrame(d, b, f);
        if (tg.upper != 0) {
            // residential tower: the lobby on the ground floor, the unit upstairs (openings: layouts, plan mode)
            InteriorDef u;
            Target lt = tg;
            lt.maxW = Min(2.f * b.hx, 12.f);
            lt.maxD = 10.f;
            lt.flags = (u16)(TF_WIDEDOOR | TF_NOSTORE);
            d.kind = IK_RES_LOBBY;
            d.name = std::string(tg.name) + " Lobby";
            d.alias.clear();
            planShell(d, b, f, lt);
            if (!planUpperUnit(u, d, b, f, tg)) {
                LOG("Interiors: %s: no upper floor in building %d", tg.name, bi);
                continue;
            }
            finish(d);
            finish(u);
            int li = (int)set.defs.size();
            d.link = li + 1;
            u.link = li;
            if (const InteriorMarker* out = d.marker(IM_DOOR_OUT)) {
                // the unit's way out is the lobby's street door (story "door" points by name)
                InteriorMarker mk = *out;
                mk.pos = u.toLocal(d.toWorld(out->pos));
                u.markers.push_back(mk);
            }
            b.interior = (i16)li;
            used[bi] = 1;
            set.defs.push_back(std::move(d));
            set.defs.push_back(std::move(u));
            continue;
        }
        if (tg.flags & TF_OWNSHELL) {
            // the whole lot: the layout builds the structure and its yard
            d.ownShell = true;
            d.x0 = -b.hx;
            d.x1 = b.hx;
            d.depth = 2.f * b.hy;
            d.ceil = 2.25f;
            d.shellTop = 4.f;
            d.radius = length(vec2(b.hx, b.hy)) + 3.f;
            if (gSites) gSites->vegBlocks.push_back({b.c, b.ax, b.hx + 1.f, b.hy + 1.f});
        } else {
            planShell(d, b, f, tg);
        }
        finish(d);
        b.interior = (i16)set.defs.size();
        used[bi] = 1;
        set.defs.push_back(std::move(d));
    }
    {
        InteriorDef d;
        if (planClub(map, roads, d)) {
            finish(d);
            if (gSites) gSites->vegBlocks.push_back({d.center().xy(), d.ax, (d.x1 - d.x0) * 0.5f + 3.f, d.depth * 0.5f + 3.f});
            set.defs.push_back(std::move(d));
        }
    }
    {
        InteriorDef lobby, ph;
        if (planSolaris(lobby, ph)) {
            finish(lobby);
            finish(ph);
            int li = (int)set.defs.size();
            lobby.link = li + 1;
            ph.link = li;
            set.defs.push_back(std::move(lobby));
            set.defs.push_back(std::move(ph));
        }
    }
    if (gSites) gSites->buildRectHash();   // vegetation and beach furniture keep off whole-structure interiors
    int counts[IK_COUNT] = {};
    for (auto& d : set.defs) counts[d.kind]++;
    ikit::tPlanBuildings = nullptr;
    LOG("Interiors: %zu planned (convenience %d, gun shops %d) in %.1f ms", set.defs.size(), counts[IK_CONVENIENCE], counts[IK_GUNSHOP],
        (TimeSeconds() - t0) * 1000.0);
    for (auto& d : set.defs)
        LOG("  interior '%s' kind %d building %d at (%.2f, %.2f, %.2f) ax (%.4f, %.4f) x %.2f..%.2f depth %.2f ceil %.2f: %zu rooms, %zu openings, %zu doors, "
            "%zu portals, %zu npc points",
            d.name.c_str(), (int)d.kind, d.building, d.origin.x, d.origin.y, d.origin.z, d.ax.x, d.ax.y, d.x0, d.x1, d.depth, d.ceil, d.rooms.size(),
            d.openings.size(), d.doors.size(), d.portals.size(), d.scenarios.size());
}

// ------------------------------------------------------------------------------------------------ shell hooks
bool interiorFacadeWall(int interior, MeshData& m, vec3 org, vec2 a, vec2 b, float z0, float z1, float u0, float uLen, float vBase, u32 color, u32 mat) {
    if (!gInteriors || interior < 0 || interior >= (int)gInteriors->defs.size()) return false;
    const InteriorDef& d = gInteriors->defs[interior];
    vec2 t = b - a;
    float len = length(t);
    if (len < 0.05f) return false;
    t = t / len;
    vec2 n(t.y, -t.x);
    std::vector<ikit::Hole> holes;
    auto collect = [&](const InteriorDef& dd) {
        for (const InteriorOpening& op : dd.openings) {
            if (fabsf(dot(op.a - a, n)) > 0.06f || fabsf(dot(op.b - a, n)) > 0.06f) continue;
            float s0 = dot(op.a - a, t), s1 = dot(op.b - a, t);
            if (s0 > s1) std::swap(s0, s1);
            if (s1 <= 0.01f || s0 >= len - 0.01f) continue;
            if (op.z1 <= z0 || op.z0 >= z1) continue;
            holes.push_back({Max(s0, 0.f), Min(s1, len), op.z0 - z0, op.z1 - z0});
        }
    };
    collect(d);
    if (d.link >= 0 && d.building >= 0 && gInteriors->defs[d.link].building == d.building) collect(gInteriors->defs[d.link]);   // upper unit
    if (holes.empty()) return false;
    float H = z1 - z0;
    float xs[48], zs[48];
    int nx = 0, nz = 0;
    xs[nx++] = 0.f;
    xs[nx++] = len;
    zs[nz++] = 0.f;
    zs[nz++] = H;
    for (const ikit::Hole& h : holes) {
        if (nx < 46) {
            xs[nx++] = h.s0;
            xs[nx++] = h.s1;
        }
        if (nz < 46) {
            zs[nz++] = Clamp(h.z0, 0.f, H);
            zs[nz++] = Clamp(h.z1, 0.f, H);
        }
    }
    std::sort(xs, xs + nx);
    std::sort(zs, zs + nz);
    nx = (int)(std::unique(xs, xs + nx, [](float p, float q) { return fabsf(p - q) < 1e-4f; }) - xs);
    nz = (int)(std::unique(zs, zs + nz, [](float p, float q) { return fabsf(p - q) < 1e-4f; }) - zs);
    for (int i = 0; i + 1 < nx; i++)
        for (int k = 0; k + 1 < nz; k++) {
            float xa = xs[i], xb = xs[i + 1], za = zs[k], zb = zs[k + 1];
            float xm = (xa + xb) * 0.5f, zm = (za + zb) * 0.5f;
            bool cut = false;
            for (const ikit::Hole& h : holes)
                if (xm > h.s0 && xm < h.s1 && zm > h.z0 && zm < h.z1) {
                    cut = true;
                    break;
                }
            if (cut) continue;
            vec3 p0 = vec3(a + t * xa, z0 + za) - org, p1 = vec3(a + t * xb, z0 + za) - org;
            vec3 p2 = vec3(a + t * xb, z0 + zb) - org, p3 = vec3(a + t * xa, z0 + zb) - org;
            float ua = u0 + uLen * (xa / len), ub = u0 + uLen * (xb / len);
            float va = z0 + za - vBase, vb = z0 + zb - vBase;
            m.quad(p0, p1, p2, p3, vec2(ua, va), vec2(ub, va), vec2(ub, vb), vec2(ua, vb), color, mat);
        }
    return true;
}

// Hollow band of one interior inside a mass box: the solid remainder around its region and its exterior walls (front
// with door gaps) from its floor to the top of its shell
void hollowBand(const InteriorDef& d, vec2 c, vec2 ax, float hx, float hy, float za, float zb, std::vector<CollisionBox>& out) {
    vec3 cl = d.toLocal(vec3(c, 0.f));
    bool alongX = fabsf(dot(ax, d.ax)) > 0.7f;
    float ex = alongX ? hx : hy, ey = alongX ? hy : hx;
    float bx0 = cl.x - ex, bx1 = cl.x + ex, by0 = cl.y - ey, by1 = cl.y + ey;
    float rx0 = Max(d.x0, bx0), rx1 = Min(d.x1, bx1), ry0 = Max(0.f, by0), ry1 = Min(d.depth, by1);
    auto emit = [&](float x0, float x1, float y0, float y1, float z0e, float z1e) {
        if (x1 - x0 < 0.02f || y1 - y0 < 0.02f || z1e - z0e < 0.02f) return;
        CollisionBox cb;
        cb.c = d.toWorld(vec3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0.f));
        cb.c.z = (z0e + z1e) * 0.5f;
        cb.ax = d.ax;
        cb.he = vec3((x1 - x0) * 0.5f, (y1 - y0) * 0.5f, (z1e - z0e) * 0.5f);
        out.push_back(cb);
    };
    emit(bx0, rx0, by0, by1, za, zb);
    emit(rx1, bx1, by0, by1, za, zb);
    emit(rx0, rx1, ry1, by1, za, zb);
    emit(rx0, rx1, by0, ry0, za, zb);
    float T = kWallT;
    std::vector<std::pair<float, float>> gaps;
    for (const InteriorOpening& op : d.openings) {
        if (op.kind == OP_GLASS) continue;
        vec3 a = d.toLocal(vec3(op.a, 0.f)), b2 = d.toLocal(vec3(op.b, 0.f));
        if (fabsf(a.y) > 0.1f || fabsf(b2.y) > 0.1f) continue;   // front facade only
        gaps.push_back({Min(a.x, b2.x), Max(a.x, b2.x)});
    }
    std::sort(gaps.begin(), gaps.end());
    float cur = rx0;
    for (auto& g : gaps) {
        emit(cur, Min(g.first, rx1), ry0, ry0 + T, za, zb);
        cur = Max(cur, g.second);
    }
    emit(cur, rx1, ry0, ry0 + T, za, zb);
    if (rx0 <= bx0 + 0.01f) emit(rx0, rx0 + T, ry0, ry1, za, zb);
    if (rx1 >= bx1 - 0.01f) emit(rx1 - T, rx1, ry0, ry1, za, zb);
    if (ry1 >= by1 - 0.01f) emit(rx0, rx1, ry1 - T, ry1, za, zb);
}

bool interiorShellCollision(int interior, vec2 c, vec2 ax, float hx, float hy, float z0, float z1, std::vector<CollisionBox>& out) {
    if (!gInteriors || interior < 0 || interior >= (int)gInteriors->defs.size()) return false;
    const InteriorDef& d0 = gInteriors->defs[interior];
    if (d0.ownShell) return true;   // the structure streams with the interior (collision included)
    // the interior and, in a residential tower, its unit upstairs: hollow bands stacked in the mass box
    const InteriorDef* ds[2] = {&d0, nullptr};
    int n = 1;
    if (d0.link >= 0 && d0.building >= 0 && gInteriors->defs[d0.link].building == d0.building) ds[n++] = &gInteriors->defs[d0.link];
    std::vector<const InteriorDef*> hit;
    for (int k = 0; k < n; k++) {
        float fz = ds[k]->origin.z;
        if (z0 > fz + 0.3f || z1 < fz + 2.2f) continue;
        vec3 cl = ds[k]->toLocal(vec3(c, 0.f));
        bool alongX = fabsf(dot(ax, ds[k]->ax)) > 0.7f;
        float ex = alongX ? hx : hy, ey = alongX ? hy : hx;
        if (Min(ds[k]->x1, cl.x + ex) - Max(ds[k]->x0, cl.x - ex) < 1.f || Min(ds[k]->depth, cl.y + ey) - Max(0.f, cl.y - ey) < 1.f) continue;
        hit.push_back(ds[k]);
    }
    if (hit.empty()) return false;
    std::sort(hit.begin(), hit.end(), [](const InteriorDef* p, const InteriorDef* q) { return p->origin.z < q->origin.z; });
    auto solid = [&](float za, float zb) {
        if (zb - za < 0.02f) return;
        CollisionBox cb;
        cb.c = vec3(c, (za + zb) * 0.5f);
        cb.ax = ax;
        cb.he = vec3(hx, hy, (zb - za) * 0.5f);
        out.push_back(cb);
    };
    float zc = z0;
    for (const InteriorDef* d : hit) {
        float fz = d->origin.z, top = Min(fz + d->shellTop, z1);
        solid(zc, fz);   // slab (and the floors in between)
        hollowBand(*d, c, ax, hx, hy, Max(zc, fz), top, out);
        zc = top;
    }
    solid(zc, z1);
    return true;
}

int interiorForLandmark(u8 kind) { return gInteriors ? gInteriors->byKind(kind, 0) : -1; }

void interiorFacadeRing(int interior, MeshData& m, vec3 org, const std::vector<vec2>& fp, float z0, float z1, float vBase, u32 facadeId, float bay, u32 col) {
    int n = (int)fp.size();
    u32 mat = makeMat(MAT_FACADE, facadeId);
    for (int i = 0; i < n; i++) {
        vec2 a = fp[i], b = fp[(i + 1) % n];
        float len = length(b - a);
        if (len < 0.05f) continue;
        float uLen = Max(1.f, roundf(len / bay)) * bay, u0 = (float)i * 1000.f;
        if (interiorFacadeWall(interior, m, org, a, b, z0, z1, u0, uLen, vBase, col, mat)) continue;
        vec3 p0 = vec3(a, z0) - org, p1 = vec3(b, z0) - org, up(0, 0, z1 - z0);
        vec2 on(b.y - a.y, a.x - b.x);
        m.quadFacing(p0, p1, p1 + up, p0 + up, vec2(u0, z0 - vBase), vec2(u0 + uLen, z0 - vBase), vec2(u0 + uLen, z1 - vBase), vec2(u0, z1 - vBase), col, mat,
                     vec3(on, 0));
    }
}

bool interiorOwnsShell(int interior) {
    return gInteriors && interior >= 0 && interior < (int)gInteriors->defs.size() && gInteriors->defs[interior].ownShell;
}

int interiorDoorBay(const Building& b) {
    if (!gInteriors || b.interior < 0 || b.interior >= (int)gInteriors->defs.size()) return -1;
    return gInteriors->defs[b.interior].doorBay;
}

bool interiorHidesLoadingDoor(const Building& b, float u) {
    if (!gInteriors || b.interior < 0 || b.interior >= (int)gInteriors->defs.size()) return false;
    const InteriorDef& d = gInteriors->defs[b.interior];
    vec3 p = d.toLocal(vec3(b.c + b.front * b.hy + b.ax * u, d.origin.z));
    for (const InteriorOpening& op : d.openings) {
        if (op.kind == OP_GLASS) continue;
        vec3 a = d.toLocal(vec3(op.a, op.z0)), c = d.toLocal(vec3(op.b, op.z0));
        if (fabsf(a.y) > 0.1f) continue;
        if (p.x + 1.8f > Min(a.x, c.x) - 0.2f && p.x - 1.8f < Max(a.x, c.x) + 0.2f) return true;   // painted door is 3.6 m wide
    }
    return false;
}

// ------------------------------------------------------------------------------------------------ build
void buildInterior(int index, InteriorMesh& out) {
    double t0 = TimeSeconds();
    if (!gInteriors || index < 0 || index >= (int)gInteriors->defs.size()) return;
    InteriorDef& d = gInteriors->defs[index];   // read only in build mode
    ikit::IB b;
    b.d = &d;
    b.out = &out;
    for (int i = 0; i < IP_COUNT; i++) b.parts[i] = &out.parts[i];
    ikit::tExtraHoles.clear();
    ikit::runLayout(b);
    ikit::doorLeaves(d, out);
    int tris = 0;
    for (int i = 0; i < IP_COUNT; i++) tris += (int)out.parts[i].indices.size() / 3;
    for (auto& l : out.leaves) tris += (int)l.mesh.indices.size() / 3;
    out.triangles = tris;
    out.ms = (TimeSeconds() - t0) * 1000.0;
}

}  // namespace World
