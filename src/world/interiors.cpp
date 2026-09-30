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

struct Target {
    u8 kind;
    const char* name;
    vec2 hint;
    u32 styles;
    float minW, minD;
    float maxW;          // region width cap (0 = full building width)
    bool allowBridge;
};

// Building closest to the place whose front faces the place's street
int pickBuilding(const BuildingSet& bs, const PlaceLite& pl, const Target& tg, const std::vector<u8>& used, int pass) {
    std::vector<int> cand;
    bs.buildingsNear(pl.sp, pass < 2 ? 70.f : 160.f, cand);
    int best = -1;
    float bestScore = 1e30f;
    for (int i : cand) {
        const Building& b = bs.buildings[i];
        if (used[i] || b.interior >= 0) continue;
        bool styleOk = (tg.styles & styleBit(b.style)) != 0;
        if (pass < 2 && !styleOk) continue;
        if (b.style == BS_TOWER && !towerGroundRect(b)) continue;
        if (b.style == BS_CHURCH || (b.style == BS_GASSTATION && !(tg.styles & styleBit(BS_GASSTATION)))) continue;
        if (2.f * b.hx < tg.minW || 2.f * b.hy < tg.minD) continue;
        float facing = dot(b.front, -pl.out);
        if (pass == 0 && facing < 0.85f) continue;
        if (facing < 0.3f) continue;
        vec2 fc = b.c + b.front * b.hy;
        float along = fabsf(dot(fc - pl.sp, pl.dir));
        float across = dot(fc - pl.sp, pl.out);
        if (pass == 0 && (across < -2.f || across > 18.f)) continue;
        float score = Max(0.f, along - b.hx * 0.5f) + fabsf(across) * 0.4f + (styleOk ? 0.f : 40.f) + (1.f - facing) * 30.f;
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
    for (size_t i = 0; i < defs.size(); i++)
        if (defs[i].name == name) return (int)i;
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
void planInteriors(WorldMap& map, const RoadNetwork& roads, BuildingSet& bs) {
    double t0 = TimeSeconds();
    static InteriorSet set;
    set.defs.clear();
    gInteriors = &set;
    for (Building& b : bs.buildings) b.interior = -1;
    std::vector<u8> used(bs.buildings.size(), 0);
    (void)map;
    const u32 kRetailWide = kRetail | styleBit(BS_TOWER) | styleBit(BS_CONDO);
    const Target targets[] = {
        // story places (mission_util.cpp computePlaces) and shops (shops.cpp)
        {IK_CONVENIENCE, "TideStop Mart", vec2(1480.f, -260.f), kRetail, 8.f, 12.f, 16.f, false},
        {IK_CONVENIENCE, "Bodega La Luna", vec2(2250.f, 520.f), kRetail, 8.f, 12.f, 14.f, false},
        {IK_CONVENIENCE, "Canvas Corner Market", vec2(3000.f, 1500.f), kRetail, 8.f, 12.f, 16.f, false},
        {IK_CONVENIENCE, "Sunrise Food Mart", vec2(5230.f, 600.f), kRetailWide, 8.f, 12.f, 16.f, false},
        {IK_CONVENIENCE, "Northside Quick Stop", vec2(2300.f, 3800.f), kRetail, 8.f, 12.f, 16.f, false},
        {IK_CONVENIENCE, "Flats Food & Fuel", vec2(420.f, 3350.f), kRetail | styleBit(BS_WAREHOUSE), 8.f, 12.f, 16.f, false},
        {IK_CONVENIENCE, "Grove Pantry", vec2(2150.f, -3100.f), kRetail | styleBit(BS_STRIPMALL), 8.f, 12.f, 16.f, false},
    };
    for (const Target& tg : targets) {
        PlaceLite pl = resolve(roads, tg.hint, tg.allowBridge);
        if (!pl.ok) continue;
        int bi = -1;
        for (int pass = 0; pass < 3 && bi < 0; pass++) bi = pickBuilding(bs, pl, tg, used, pass);
        if (bi < 0) {
            LOG("Interiors: no building for %s near (%.0f, %.0f)", tg.name, tg.hint.x, tg.hint.y);
            continue;
        }
        Building& b = bs.buildings[bi];
        const FacadeGPU& f = bs.facades[b.facade];
        InteriorDef d;
        d.kind = tg.kind;
        d.name = tg.name;
        d.building = bi;
        d.seed = hash32(b.seed ^ ((u32)tg.kind * 0x9E3779B9u) ^ hashString(tg.name));
        setFrame(d, b, f);
        // hollow region: the whole ground floor width (capped around the entrance bay), a kind-specific depth
        float w = 2.f * b.hx;
        float regionW = tg.maxW > 0.f ? Min(w, tg.maxW) : w;
        int doorBay = d.bays / 2;
        d.doorBay = doorBay;
        float doorX = d.bayX0 + (doorBay + 0.5f) * d.bw;
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
        (void)doorX;
        d.depth = Min(2.f * b.hy, tg.kind == IK_CONVENIENCE ? 17.f : 20.f);
        float gH = f.groundH;
        d.shellTop = Max(gH - 0.25f, 3.0f);
        if (b.floors <= 1) d.shellTop = Max(b.height - 0.4f, 3.0f);
        d.ceil = Clamp(gH - 0.6f, 2.8f, 4.2f);
        if (d.storefront) d.ceil = Max(d.ceil, frontWindow(d, f, 0).z1 + 0.12f);
        d.ceil = Min(d.ceil, d.shellTop - 0.1f);
        d.radius = length(vec2((d.x1 - d.x0) * 0.5f, d.depth * 0.5f)) + 2.f;
        // facade openings: storefront glass in every front bay of the region + the entrance door
        int firstBay = (int)roundf((d.x0 - d.bayX0) / d.bw), lastBay = (int)roundf((d.x1 - d.bayX0) / d.bw) - 1;
        for (int k = firstBay; k <= lastBay; k++) {
            FrontWindow fw = frontWindow(d, f, k);
            if (d.storefront) addOpening(d, fw.x0, fw.x1, fw.z0, fw.z1, OP_GLASS);
            else if (k != doorBay) addOpening(d, fw.x0, fw.x1, fw.z0, fw.z1, OP_GLASS);
        }
        {
            float s0 = (doorBay + 0.04f) * d.bw, s1 = (doorBay + 0.96f) * d.bw;
            float dw = d.storefront ? Min(1.9f, (s1 - s0) * 0.8f) : Min(1.1f, d.bw * 0.7f);
            float glassH = gH - 1.3f;
            float dh = d.storefront ? Min(2.3f, glassH) : 2.2f;
            float dc = d.bayX0 + (doorBay + 0.5f) * d.bw;
            addOpening(d, dc - dw * 0.5f, dc + dw * 0.5f, 0.f, dh, OP_DOOR);
            if (!d.storefront) {
                // the window of the door bay goes too (a transom lite above the door)
                FrontWindow fw = frontWindow(d, f, doorBay);
                addOpening(d, Max(fw.x0, dc - dw * 0.5f), Min(fw.x1, dc + dw * 0.5f), dh, Max(fw.z1, dh + 0.05f), OP_GLASS);
            }
        }
        // rooms, doors, scenario points, markers (layout in plan mode)
        {
            ikit::IB ib;
            ib.d = &d;
            ikit::runLayout(ib);
        }
        computePortals(d);
        b.interior = (i16)set.defs.size();
        used[bi] = 1;
        set.defs.push_back(std::move(d));
    }
    int counts[IK_COUNT] = {};
    for (auto& d : set.defs) counts[d.kind]++;
    LOG("Interiors: %zu planned (convenience %d) in %.1f ms", set.defs.size(), counts[IK_CONVENIENCE], (TimeSeconds() - t0) * 1000.0);
    for (auto& d : set.defs)
        LOG("  interior '%s' kind %d building %d at (%.1f, %.1f, %.1f) region %.1f x %.1f, %zu rooms, %zu openings, %zu doors, %zu npc points", d.name.c_str(),
            (int)d.kind, d.building, d.origin.x, d.origin.y, d.origin.z, d.x1 - d.x0, d.depth, d.rooms.size(), d.openings.size(), d.doors.size(),
            d.scenarios.size());
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
    for (const InteriorOpening& op : d.openings) {
        if (fabsf(dot(op.a - a, n)) > 0.06f || fabsf(dot(op.b - a, n)) > 0.06f) continue;
        float s0 = dot(op.a - a, t), s1 = dot(op.b - a, t);
        if (s0 > s1) std::swap(s0, s1);
        if (s1 <= 0.01f || s0 >= len - 0.01f) continue;
        if (op.z1 <= z0 || op.z0 >= z1) continue;
        holes.push_back({Max(s0, 0.f), Min(s1, len), op.z0 - z0, op.z1 - z0});
    }
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

bool interiorShellCollision(int interior, vec2 c, vec2 ax, float hx, float hy, float z0, float z1, std::vector<CollisionBox>& out) {
    if (!gInteriors || interior < 0 || interior >= (int)gInteriors->defs.size()) return false;
    const InteriorDef& d = gInteriors->defs[interior];
    if (d.ownShell) return true;   // the structure streams with the interior (collision included)
    float floorZ = d.origin.z;
    if (z0 > floorZ + 0.3f || z1 < floorZ + 2.2f) return false;
    vec3 cl = d.toLocal(vec3(c, 0.f));
    bool alongX = fabsf(dot(ax, d.ax)) > 0.7f;
    float ex = alongX ? hx : hy, ey = alongX ? hy : hx;
    float bx0 = cl.x - ex, bx1 = cl.x + ex, by0 = cl.y - ey, by1 = cl.y + ey;
    float rx0 = Max(d.x0, bx0), rx1 = Min(d.x1, bx1), ry0 = Max(0.f, by0), ry1 = Min(d.depth, by1);
    if (rx1 - rx0 < 1.f || ry1 - ry0 < 1.f) return false;
    auto emit = [&](float x0, float x1, float y0, float y1, float za, float zb) {
        if (x1 - x0 < 0.02f || y1 - y0 < 0.02f || zb - za < 0.02f) return;
        CollisionBox cb;
        cb.c = d.toWorld(vec3((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, 0.f));
        cb.c.z = (za + zb) * 0.5f;
        cb.ax = d.ax;
        cb.he = vec3((x1 - x0) * 0.5f, (y1 - y0) * 0.5f, (zb - za) * 0.5f);
        out.push_back(cb);
    };
    float top = floorZ + d.shellTop;
    // solid remainder of the mass around the hollow region
    emit(bx0, rx0, by0, by1, z0, z1);
    emit(rx1, bx1, by0, by1, z0, z1);
    emit(rx0, rx1, ry1, by1, z0, z1);
    emit(rx0, rx1, by0, ry0, z0, z1);
    // floor slab and the upper floors above the hollow part
    emit(rx0, rx1, ry0, ry1, z0, floorZ);
    emit(rx0, rx1, ry0, ry1, top, z1);
    // exterior walls of the hollow part (front with door gaps)
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
        emit(cur, Min(g.first, rx1), ry0, ry0 + T, floorZ, top);
        cur = Max(cur, g.second);
    }
    emit(cur, rx1, ry0, ry0 + T, floorZ, top);
    if (rx0 <= bx0 + 0.01f) emit(rx0, rx0 + T, ry0, ry1, floorZ, top);
    if (rx1 >= bx1 - 0.01f) emit(rx1 - T, rx1, ry0, ry1, floorZ, top);
    if (ry1 >= by1 - 0.01f) emit(rx0, rx1, ry1 - T, ry1, floorZ, top);
    return true;
}

bool interiorOwnsShell(int interior) {
    return gInteriors && interior >= 0 && interior < (int)gInteriors->defs.size() && gInteriors->defs[interior].ownShell;
}

int interiorDoorBay(const Building& b) {
    if (!gInteriors || b.interior < 0 || b.interior >= (int)gInteriors->defs.size()) return -1;
    return gInteriors->defs[b.interior].doorBay;
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
