// The hand-built world's blocks sections (blocksbin.h).
#include "blocksbin.h"
#include <cmath>
#include <utility>

namespace World {

GroundKind groundKind(const std::string& w, bool* known) {
    static const char* const kNames[GK_COUNT] = {"lawn", "paving", "gravel", "dirt", "sand", "planting", "concrete", "deck"};
    for (int i = 0; i < (int)GK_COUNT; i++)
        if (w == kNames[i]) {
            if (known) *known = true;
            return (GroundKind)i;
        }
    if (known) *known = false;
    return GK_LAWN;
}

ParcelUse parcelUse(const std::string& w, bool* known) {
    static const char* const kNames[PU_COUNT] = {"residential", "commercial", "mixed", "industrial", "civic", "park", "parking", "vacant"};
    for (int i = 0; i < (int)PU_COUNT; i++)
        if (w == kNames[i]) {
            if (known) *known = true;
            return (ParcelUse)i;
        }
    if (known) *known = false;
    return PU_VACANT;
}

PathKind pathKind(const std::string& w, bool* known) {
    static const char* const kNames[WK_COUNT] = {"walk", "plaza", "alley", "trail", "boardwalk", "footbridge", "underpass"};
    for (int i = 0; i < (int)WK_COUNT; i++)
        if (w == kNames[i]) {
            if (known) *known = true;
            return (PathKind)i;
        }
    if (known) *known = false;
    return WK_WALK;
}

namespace blocksbin_detail {
const char* const kSpotNames[SPOT_COUNT] = {"bench",     "wall_sit",  "steps_sit",  "lean",        "smoke",         "phone",         "busstop_wait",
                                          "queue",     "window",    "atm",        "vending",     "cafe_seat",     "bar_stool",     "busk",
                                          "vendor",    "guard",     "door_staff", "watch_view",  "fish",          "exercise",      "sunbathe",
                                          "sleep_rough", "talk_ring", "cover",    "desk",        "counter_serve", "counter_queue", "browse",
                                          "bed",       "sofa",      "kitchen",    "washroom",    "bar",           "dance"};
}  // namespace blocksbin_detail

SpotKind spotKind(const std::string& w, bool* known) {
    for (int i = 0; i < (int)SPOT_COUNT; i++)
        if (w == blocksbin_detail::kSpotNames[i]) {
            if (known) *known = true;
            return (SpotKind)i;
        }
    if (known) *known = false;
    return SPOT_BENCH;
}
const char* spotKindName(int kind) { return kind >= 0 && kind < (int)SPOT_COUNT ? blocksbin_detail::kSpotNames[kind] : "?"; }

namespace blocksbin_detail {

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

int cellOf(vec2 p) {
    const int cx = (int)floorf((p.x + kWorldHalf) / kCellSize), cy = (int)floorf((p.y + kWorldHalf) / kCellSize);
    if (cx < 0 || cy < 0 || cx >= kCellsPerSide || cy >= kCellsPerSide) return -1;
    return cy * kCellsPerSide + cx;
}

}  // namespace blocksbin_detail

void HandBlocks::index() {
    using namespace blocksbin_detail;
    const size_t cells = (size_t)kCellsPerSide * kCellsPerSide;
    groundByCell.assign(cells, {});
    propsByCell.assign(cells, {});
    lampsByCell.assign(cells, {});
    for (size_t i = 0; i < ground.size(); i++) {
        vec2 c(0.f);
        for (const vec2& p : ground[i].poly) c += p;
        if (ground[i].poly.empty()) continue;
        const int k = cellOf(c * (1.f / (float)ground[i].poly.size()));
        if (k >= 0) groundByCell[(size_t)k].push_back((int)i);
    }
    // the level ground by tile (padHeight), over the box of all of it with a metre to spare
    levelByTile.clear();
    levelNx = levelNy = 0;
    vec2 lo(1e30f), hi(-1e30f);
    for (const GroundPoly& g : ground)
        if (g.flat && g.poly.size() >= 3)
            for (const vec2& p : g.poly) {
                lo = vmin(lo, p);
                hi = vmax(hi, p);
            }
    if (lo.x <= hi.x) {
        levelOrg = lo - vec2(1.f);
        levelTile = Max(8.f, Max(hi.x - lo.x, hi.y - lo.y) / 512.f);
        levelNx = (int)floorf((hi.x + 1.f - levelOrg.x) / levelTile) + 1;
        levelNy = (int)floorf((hi.y + 1.f - levelOrg.y) / levelTile) + 1;
        levelByTile.assign((size_t)levelNx * (size_t)levelNy, {});
        for (size_t i = 0; i < ground.size(); i++) {
            const GroundPoly& g = ground[i];
            if (!g.flat || g.poly.size() < 3) continue;
            vec2 a(1e30f), b(-1e30f);
            for (const vec2& p : g.poly) {
                a = vmin(a, p);
                b = vmax(b, p);
            }
            const int x0 = Clamp((int)floorf((a.x - 0.05f - levelOrg.x) / levelTile), 0, levelNx - 1),
                      x1 = Clamp((int)floorf((b.x + 0.05f - levelOrg.x) / levelTile), 0, levelNx - 1);
            const int y0 = Clamp((int)floorf((a.y - 0.05f - levelOrg.y) / levelTile), 0, levelNy - 1),
                      y1 = Clamp((int)floorf((b.y + 0.05f - levelOrg.y) / levelTile), 0, levelNy - 1);
            for (int y = y0; y <= y1; y++)
                for (int x = x0; x <= x1; x++) levelByTile[(size_t)y * (size_t)levelNx + (size_t)x].push_back((int)i);
        }
    }
    for (size_t i = 0; i < props.size(); i++) {
        const int k = cellOf(props[i].pos.xy());
        if (k >= 0) propsByCell[(size_t)k].push_back((int)i);
    }
    for (size_t i = 0; i < lamps.size(); i++) {
        const int k = cellOf(lamps[i].pos.xy());
        if (k >= 0) lampsByCell[(size_t)k].push_back((int)i);
    }
    // a collider in every cell its footprint's circle reaches
    collidersByCell.assign(colliders.empty() ? 0 : cells, {});
    for (size_t i = 0; i < colliders.size(); i++) {
        const HandCollider& c = colliders[i];
        const float r = c.kind == HandCollider::CYLINDER ? c.he.x : length(c.he);
        const int x0 = Clamp((int)floorf((c.c.x - r + kWorldHalf) / kCellSize), 0, kCellsPerSide - 1), x1 = Clamp((int)floorf((c.c.x + r + kWorldHalf) / kCellSize), 0, kCellsPerSide - 1);
        const int y0 = Clamp((int)floorf((c.c.y - r + kWorldHalf) / kCellSize), 0, kCellsPerSide - 1), y1 = Clamp((int)floorf((c.c.y + r + kWorldHalf) / kCellSize), 0, kCellsPerSide - 1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) collidersByCell[(size_t)y * kCellsPerSide + x].push_back((int)i);
    }
}

namespace blocksbin_detail {

void putV3(ByteWriter& w, vec3 v) {
    w.putF32(v.x);
    w.putF32(v.y);
    w.putF32(v.z);
}
vec3 getV3(ByteReader& r) {
    const float x = r.getF32(), y = r.getF32(), z = r.getF32();
    return vec3(x, y, z);
}
void putCut(ByteWriter& w, const HandLot::Cut& c) {
    w.putU32(c.edge < 0 ? 0xffffffffu : (u32)c.edge);
    w.putF32(c.s);
    w.putU32(c.side);
}
bool getCut(ByteReader& r, HandLot::Cut& c, int roadEdges) {
    const u32 e = r.getU32();
    c.edge = e == 0xffffffffu ? -1 : (int)e;
    c.s = r.getF32();
    const u32 side = r.getU32();
    c.side = (u8)side;
    return !r.bad && side <= 1u && (e == 0xffffffffu || (e < 0x7fffffffu && (roadEdges < 0 || (int)e < roadEdges))) && std::isfinite(c.s);
}

void writeParcels(const std::vector<HandParcel>& parcels, WorldBinWriter& out) {
    if (parcels.empty()) return;
    ByteWriter w;
    w.putU32(kBlockParcelsVersion);
    w.putU32((u32)parcels.size());
    for (const HandParcel& p : parcels) {
        w.putString(p.id);
        w.putU32((u32)p.use | ((u32)p.frontSide << 8));
        w.putU32(p.frontEdge < 0 ? 0xffffffffu : (u32)p.frontEdge);
        w.putF32(p.grade);
        w.putPoints(p.poly);
    }
    out.addCompressed("blocks.parcels", w.bytes);
}

void writeLots(const std::vector<HandLot>& lots, WorldBinWriter& out) {
    if (lots.empty()) return;
    ByteWriter w;
    w.putU32(kBlockLotsVersion);
    w.putU32((u32)lots.size());
    for (const HandLot& l : lots) {
        w.putString(l.id);
        w.putU32((u32)l.levels);
        putCut(w, l.entry);
        putCut(w, l.exit);
        w.putU32((u32)l.rows.size());
        for (const HandLot::Row& r : l.rows) {
            putV3(w, r.a);
            putV3(w, r.b);
            w.putU32((u32)r.bays);
            w.putF32(r.w);
            w.putF32(r.len);
            w.putF32(r.dir.x);
            w.putF32(r.dir.y);
            w.putU32((u32)r.level);
            w.putU32((u32)r.aisle);
        }
        w.putU32((u32)l.nodes.size());
        for (const HandLot::Node& n : l.nodes) {
            putV3(w, n.p);
            w.putU32((u32)n.level | ((u32)n.kind << 8));
        }
        w.putU32((u32)l.links.size());
        for (const HandLot::Link& k : l.links) {
            w.putU32((u32)k.a);
            w.putU32((u32)k.b);
            w.putF32(k.width);
            w.putU32((u32)k.oneway | ((u32)k.kind << 8));
            w.putU32((u32)k.pts.size());
            for (const vec3& q : k.pts) putV3(w, q);
        }
        w.putU32((u32)l.bays.size());
        for (const HandLot::Bay& b : l.bays) {
            putV3(w, b.c);
            w.putF32(b.dir.x);
            w.putF32(b.dir.y);
            w.putU32((u32)b.row);
            w.putU32((u32)b.link);
            w.putF32(b.s);
        }
    }
    out.addCompressed("blocks.lots", w.bytes);
}

bool readParcels(const WorldBinView& bin, std::vector<HandParcel>& out, std::string* error, int roadEdges) {
    out.clear();
    if (!bin.find("blocks.parcels")) return true;
    std::vector<u8> bytes;
    if (!bin.bytes("blocks.parcels", bytes)) return fail(error, "world.bin's blocks.parcels section is damaged");
    ByteReader r(bytes.data(), bytes.size());
    const u32 version = r.getU32();
    if (version != kBlockParcelsVersion)
        return fail(error, "world.bin's blocks.parcels section is version " + std::to_string(version) + ", not " + std::to_string(kBlockParcelsVersion));
    const u32 count = r.getU32();
    if (r.bad || count > (u32)(r.end - r.p) / 20u) return fail(error, "world.bin's blocks.parcels section is truncated");
    std::vector<HandParcel> v(count);
    for (u32 i = 0; i < count; i++) {
        HandParcel& p = v[i];
        p.id = r.getString();
        const u32 bits = r.getU32();
        p.use = (u8)(bits & 255u);
        p.frontSide = (u8)((bits >> 8) & 255u);
        const u32 e = r.getU32();
        p.frontEdge = e == 0xffffffffu ? -1 : (int)e;
        p.grade = r.getF32();
        r.getPoints(p.poly);
        if (r.bad || p.use >= PU_COUNT || p.frontSide > 1u || p.poly.size() < 3 ||
            (e != 0xffffffffu && (e >= 0x7fffffffu || (roadEdges >= 0 && (int)e >= roadEdges))))
            return fail(error, "world.bin's blocks.parcels section: parcel " + std::to_string(i) + " is damaged");
    }
    if (!r.done()) return fail(error, "world.bin's blocks.parcels section has bytes past its end, or is truncated");
    out = std::move(v);
    return true;
}

bool readLots(const WorldBinView& bin, std::vector<HandLot>& out, std::string* error, int roadEdges) {
    out.clear();
    if (!bin.find("blocks.lots")) return true;
    std::vector<u8> bytes;
    if (!bin.bytes("blocks.lots", bytes)) return fail(error, "world.bin's blocks.lots section is damaged");
    ByteReader r(bytes.data(), bytes.size());
    const u32 version = r.getU32();
    if (version != kBlockLotsVersion) return fail(error, "world.bin's blocks.lots section is version " + std::to_string(version) + ", not " + std::to_string(kBlockLotsVersion));
    const u32 count = r.getU32();
    if (r.bad || count > (u32)(r.end - r.p) / 48u) return fail(error, "world.bin's blocks.lots section is truncated");
    std::vector<HandLot> v(count);
    const auto left = [&r](u32 n, u32 each) { return !r.bad && n <= (u32)(r.end - r.p) / each; };
    for (u32 i = 0; i < count; i++) {
        HandLot& l = v[i];
        const std::string what = "world.bin's blocks.lots section: lot " + std::to_string(i);
        l.id = r.getString();
        l.levels = (int)r.getU32();
        if (!getCut(r, l.entry, roadEdges) || !getCut(r, l.exit, roadEdges) || l.levels < 1 || l.levels > 64) return fail(error, what + " is damaged");
        u32 n = r.getU32();
        if (!left(n, 52u)) return fail(error, what + " is truncated (rows)");
        l.rows.resize(n);
        for (HandLot::Row& row : l.rows) {
            row.a = getV3(r);
            row.b = getV3(r);
            row.bays = (int)r.getU32();
            row.w = r.getF32();
            row.len = r.getF32();
            row.dir.x = r.getF32();
            row.dir.y = r.getF32();
            row.level = (int)r.getU32();
            row.aisle = (int)r.getU32();
        }
        n = r.getU32();
        if (!left(n, 16u)) return fail(error, what + " is truncated (nodes)");
        l.nodes.resize(n);
        for (HandLot::Node& nd : l.nodes) {
            nd.p = getV3(r);
            const u32 bits = r.getU32();
            nd.level = (int)(bits & 255u);
            nd.kind = (u8)((bits >> 8) & 255u);
        }
        n = r.getU32();
        if (!left(n, 20u)) return fail(error, what + " is truncated (links)");
        l.links.resize(n);
        for (HandLot::Link& k : l.links) {
            k.a = (int)r.getU32();
            k.b = (int)r.getU32();
            k.width = r.getF32();
            const u32 bits = r.getU32();
            k.oneway = (u8)(bits & 255u);
            k.kind = (u8)((bits >> 8) & 255u);
            const u32 m = r.getU32();
            if (!left(m, 12u) || m < 2u) return fail(error, what + ": a link is damaged");
            k.pts.resize(m);
            for (vec3& q : k.pts) q = getV3(r);
            if (k.a < 0 || k.b < 0 || k.a >= (int)l.nodes.size() || k.b >= (int)l.nodes.size() || k.oneway > 1u || k.kind > HandLot::LL_RAMP)
                return fail(error, what + ": a link is damaged");
        }
        n = r.getU32();
        if (!left(n, 32u)) return fail(error, what + " is truncated (bays)");
        l.bays.resize(n);
        for (HandLot::Bay& b : l.bays) {
            b.c = getV3(r);
            b.dir.x = r.getF32();
            b.dir.y = r.getF32();
            b.row = (int)r.getU32();
            b.link = (int)r.getU32();
            b.s = r.getF32();
            if (r.bad || b.row < 0 || b.row >= (int)l.rows.size() || b.link < 0 || b.link >= (int)l.links.size()) return fail(error, what + ": a bay is damaged");
        }
        size_t bays = 0;
        for (const HandLot::Row& row : l.rows) {
            if (r.bad || row.bays < 1 || row.aisle < 0 || row.aisle >= (int)l.links.size() || row.level < 0 || row.level >= l.levels) return fail(error, what + ": a row is damaged");
            bays += (size_t)row.bays;
        }
        if (bays != l.bays.size()) return fail(error, what + ": its rows have " + std::to_string(bays) + " bays, and it " + std::to_string(l.bays.size()));
        for (const HandLot::Node& nd : l.nodes)
            if (nd.kind > HandLot::LN_RAMP || nd.level >= l.levels) return fail(error, what + ": a node is damaged");
    }
    if (!r.done()) return fail(error, "world.bin's blocks.lots section has bytes past its end, or is truncated");
    out = std::move(v);
    return true;
}

void putPts(ByteWriter& w, const std::vector<vec3>& pts) {
    w.putU32((u32)pts.size());
    for (const vec3& q : pts) putV3(w, q);
}
bool getPts(ByteReader& r, std::vector<vec3>& pts) {
    const u32 n = r.getU32();
    if (r.bad || n > (u32)(r.end - r.p) / 12u) return false;
    pts.resize(n);
    for (vec3& q : pts) q = getV3(r);
    return !r.bad;
}
void putStrings(ByteWriter& w, const std::vector<std::string>& v) {
    w.putU32((u32)v.size());
    for (const std::string& t : v) w.putString(t);
}
bool getStrings(ByteReader& r, std::vector<std::string>& v) {
    const u32 n = r.getU32();
    if (r.bad || n > (u32)(r.end - r.p) / 4u) return false;
    v.resize(n);
    for (std::string& t : v) t = r.getString();
    return !r.bad;
}

void writeWalk(const HandBlocks& b, WorldBinWriter& out) {
    if (b.paths.empty() && b.plazas.empty() && b.steps.empty()) return;
    ByteWriter w;
    w.putU32(kBlockWalkVersion);
    w.putU32((u32)b.paths.size());
    for (const HandPath& p : b.paths) {
        w.putString(p.id);
        w.putU32(p.kind);
        w.putF32(p.w);
        putPts(w, p.pts);
    }
    w.putU32((u32)b.plazas.size());
    for (const HandPlaza& p : b.plazas) {
        w.putString(p.id);
        w.putF32(p.z);
        w.putPoints(p.poly);
        putStrings(w, p.tags);
    }
    w.putU32((u32)b.steps.size());
    for (const HandSteps& t : b.steps) {
        putV3(w, t.bottom);
        putV3(w, t.top);
        w.putF32(t.w);
        w.putU32((u32)t.treads);
        w.putF32(t.rise);
    }
    out.addCompressed("blocks.walk", w.bytes);
}

void writeSpots(const std::vector<HandSpot>& spots, WorldBinWriter& out) {
    if (spots.empty()) return;
    ByteWriter w;
    w.putU32(kBlockSpotsVersion);
    w.putU32((u32)spots.size());
    for (const HandSpot& s : spots) {
        w.putU32((u32)s.kind | ((u32)s.flags << 8));
        w.putString(s.id);
        putV3(w, s.at);
        w.putF32(s.dir.x);
        w.putF32(s.dir.y);
        putPts(w, s.line);
        w.putU32((u32)s.cap);
        w.putU32((u32)(s.hours.size() / 2));
        for (u16 m : s.hours) w.putU32(m);
        putStrings(w, s.roles);
        w.putU32((u32)s.ageMin | ((u32)s.ageMax << 8));
        w.putU32(s.prop < 0 ? 0xffffffffu : (u32)s.prop);
        w.putString(s.pose);
        w.putString(s.target);
    }
    out.addCompressed("blocks.spots", w.bytes);
}

bool readWalk(const WorldBinView& bin, HandBlocks& out, std::string* error) {
    if (!bin.find("blocks.walk")) return true;
    std::vector<u8> bytes;
    if (!bin.bytes("blocks.walk", bytes)) return fail(error, "world.bin's blocks.walk section is damaged");
    ByteReader r(bytes.data(), bytes.size());
    const u32 version = r.getU32();
    if (version != kBlockWalkVersion) return fail(error, "world.bin's blocks.walk section is version " + std::to_string(version) + ", not " + std::to_string(kBlockWalkVersion));
    std::vector<HandPath> paths;
    std::vector<HandPlaza> plazas;
    std::vector<HandSteps> steps;
    u32 n = r.getU32();
    if (r.bad || n > (u32)(r.end - r.p) / 16u) return fail(error, "world.bin's blocks.walk section is truncated (paths)");
    paths.resize(n);
    for (HandPath& p : paths) {
        p.id = r.getString();
        const u32 kind = r.getU32();
        p.kind = (u8)kind;
        p.w = r.getF32();
        if (!getPts(r, p.pts) || kind >= WK_COUNT || p.pts.size() < 2) return fail(error, "world.bin's blocks.walk section: path " + p.id + " is damaged");
    }
    n = r.getU32();
    if (r.bad || n > (u32)(r.end - r.p) / 16u) return fail(error, "world.bin's blocks.walk section is truncated (plazas)");
    plazas.resize(n);
    for (HandPlaza& p : plazas) {
        p.id = r.getString();
        p.z = r.getF32();
        r.getPoints(p.poly);
        if (r.bad || p.poly.size() < 3 || !getStrings(r, p.tags)) return fail(error, "world.bin's blocks.walk section: a plaza is damaged");
    }
    n = r.getU32();
    if (r.bad || n > (u32)(r.end - r.p) / 36u) return fail(error, "world.bin's blocks.walk section is truncated (steps)");
    steps.resize(n);
    for (HandSteps& t : steps) {
        t.bottom = getV3(r);
        t.top = getV3(r);
        t.w = r.getF32();
        t.treads = (int)r.getU32();
        t.rise = r.getF32();
        if (r.bad || t.treads < 0 || t.treads > 10000) return fail(error, "world.bin's blocks.walk section: a flight of steps is damaged");
    }
    if (!r.done()) return fail(error, "world.bin's blocks.walk section has bytes past its end, or is truncated");
    out.paths = std::move(paths);
    out.plazas = std::move(plazas);
    out.steps = std::move(steps);
    return true;
}

bool readSpots(const WorldBinView& bin, std::vector<HandSpot>& out, std::string* error) {
    out.clear();
    if (!bin.find("blocks.spots")) return true;
    std::vector<u8> bytes;
    if (!bin.bytes("blocks.spots", bytes)) return fail(error, "world.bin's blocks.spots section is damaged");
    ByteReader r(bytes.data(), bytes.size());
    const u32 version = r.getU32();
    if (version != kBlockSpotsVersion)
        return fail(error, "world.bin's blocks.spots section is version " + std::to_string(version) + ", not " + std::to_string(kBlockSpotsVersion));
    const u32 count = r.getU32();
    if (r.bad || count > (u32)(r.end - r.p) / 60u) return fail(error, "world.bin's blocks.spots section is truncated");   // (60: the least a spot takes)
    std::vector<HandSpot> v(count);
    for (u32 i = 0; i < count; i++) {
        HandSpot& s = v[i];
        const std::string what = "world.bin's blocks.spots section: spot " + std::to_string(i) + " is damaged";
        const u32 bits = r.getU32();
        s.kind = (u8)(bits & 255u);
        s.flags = (u8)((bits >> 8) & 255u);
        s.id = r.getString();
        s.at = getV3(r);
        s.dir.x = r.getF32();
        s.dir.y = r.getF32();
        if (!getPts(r, s.line) || s.kind >= SPOT_COUNT || (s.flags & ~(HandSpot::IMPLIED | HandSpot::COVER)) || s.line.size() == 1) return fail(error, what);
        s.cap = (int)r.getU32();
        const u32 ranges = r.getU32();
        if (r.bad || s.cap < 1 || ranges > (u32)(r.end - r.p) / 8u) return fail(error, what);
        s.hours.resize((size_t)ranges * 2);
        for (u16& m : s.hours) {
            const u32 x = r.getU32();
            m = (u16)x;
            if (x > 24u * 60u) return fail(error, what);
        }
        if (!getStrings(r, s.roles)) return fail(error, what);
        const u32 ages = r.getU32();
        s.ageMin = (u8)(ages & 255u);
        s.ageMax = (u8)((ages >> 8) & 255u);
        const u32 prop = r.getU32();
        s.prop = prop == 0xffffffffu ? -1 : (int)prop;
        s.pose = r.getString();
        s.target = r.getString();
        if (r.bad || s.ageMin > s.ageMax || (prop != 0xffffffffu && prop >= 0x7fffffffu)) return fail(error, what);
    }
    if (!r.done()) return fail(error, "world.bin's blocks.spots section has bytes past its end, or is truncated");
    out = std::move(v);
    return true;
}

void writeColliders(const std::vector<HandCollider>& colliders, WorldBinWriter& out) {
    if (colliders.empty()) return;
    ByteWriter w;
    w.putU32(kBlockCollidersVersion);
    w.putU32((u32)colliders.size());
    for (const HandCollider& c : colliders) {
        w.putU32(c.kind);
        w.putU32((u32)c.prop);
        const float f[8] = {c.c.x, c.c.y, c.z0, c.z1, c.he.x, c.he.y, c.ax.x, c.ax.y};
        for (float x : f) w.putF32(x);
    }
    out.addCompressed("blocks.colliders", w.bytes);
}

bool readColliders(const WorldBinView& bin, std::vector<HandCollider>& out, std::string* error) {
    out.clear();
    if (!bin.find("blocks.colliders")) return true;
    std::vector<u8> bytes;
    if (!bin.bytes("blocks.colliders", bytes)) return fail(error, "world.bin's blocks.colliders section is damaged");
    ByteReader r(bytes.data(), bytes.size());
    const u32 version = r.getU32();
    if (version != kBlockCollidersVersion)
        return fail(error, "world.bin's blocks.colliders section is version " + std::to_string(version) + ", not " + std::to_string(kBlockCollidersVersion));
    const u32 count = r.getU32();
    if (r.bad || count > (u32)(r.end - r.p) / 40u) return fail(error, "world.bin's blocks.colliders section is truncated");
    std::vector<HandCollider> v(count);
    for (u32 i = 0; i < count; i++) {
        HandCollider& c = v[i];
        const u32 kind = r.getU32(), prop = r.getU32();
        c.kind = (u8)kind;
        c.prop = (int)prop;
        c.c.x = r.getF32();
        c.c.y = r.getF32();
        c.z0 = r.getF32();
        c.z1 = r.getF32();
        c.he.x = r.getF32();
        c.he.y = r.getF32();
        c.ax.x = r.getF32();
        c.ax.y = r.getF32();
        if (r.bad || kind > HandCollider::CYLINDER || prop >= 0x7fffffffu || !(c.z1 >= c.z0) || !(c.he.x >= 0.f) || !(c.he.y >= 0.f) || fabsf(length(c.ax) - 1.f) > 0.01f)
            return fail(error, "world.bin's blocks.colliders section: collider " + std::to_string(i) + " is damaged");
    }
    if (!r.done()) return fail(error, "world.bin's blocks.colliders section has bytes past its end, or is truncated");
    out = std::move(v);
    return true;
}

// "blocks": the ground and the dressing
void writeDressing(const HandBlocks& b, WorldBinWriter& out) {
    ByteWriter w;
    w.putU32(kBlocksSectionVersion);
    w.putU32((u32)b.ground.size());
    for (const GroundPoly& g : b.ground) {
        w.putU32((u32)g.kind | ((u32)g.flat << 8));
        w.putF32(g.z);
        w.putU32((u32)g.poly.size());
        for (const vec2& p : g.poly) {
            w.putF32(p.x);
            w.putF32(p.y);
        }
    }
    w.putU32((u32)b.props.size());
    for (const PropInstance& p : b.props) {
        w.putU32((u32)p.type | ((u32)p.variant << 8) | ((u32)p.flags << 16));
        w.putF32(p.pos.x);
        w.putF32(p.pos.y);
        w.putF32(p.pos.z);
        w.putF32(p.yaw);
        w.putF32(p.scale);
    }
    w.putU32((u32)b.lamps.size());
    for (size_t i = 0; i < b.lamps.size(); i++) {
        const LightInstance& l = b.lamps[i];
        w.putU32((u32)l.type | ((u32)(i < b.lampSched.size() ? b.lampSched[i] : 0) << 8));
        const float f[11] = {l.pos.x, l.pos.y, l.pos.z, l.color.x, l.color.y, l.color.z, l.radius, l.dir.x, l.dir.y, l.dir.z, l.cone};
        for (float v : f) w.putF32(v);
    }
    out.addCompressed("blocks", w.bytes);
}

}  // namespace blocksbin_detail

void writeBlocks(const HandBlocks& b, WorldBinWriter& out) {
    using namespace blocksbin_detail;
    if (!b.ground.empty() || !b.props.empty() || !b.lamps.empty()) writeDressing(b, out);
    writeParcels(b.parcels, out);
    writeLots(b.lots, out);
    writeWalk(b, out);
    writeSpots(b.spots, out);
    writeColliders(b.colliders, out);
}

bool readBlocks(const WorldBinView& bin, HandBlocks& out, std::string* error, int roadEdges) {
    using namespace blocksbin_detail;
    out = HandBlocks();   // (left empty on failure: never half the blocks)
    HandBlocks b;   // (the sections beside "blocks" first: all of them, or nothing)
    if (!readParcels(bin, b.parcels, error, roadEdges) || !readLots(bin, b.lots, error, roadEdges) || !readWalk(bin, b, error) || !readSpots(bin, b.spots, error) ||
        !readColliders(bin, b.colliders, error))
        return false;
    // a spot's prop, and a collider's, is one of the blocks' props
    const auto propsOk = [&](size_t props) {
        for (const HandSpot& s : b.spots)
            if (s.prop >= 0 && (size_t)s.prop >= props) return fail(error, "world.bin's blocks.spots section names prop " + std::to_string(s.prop) + " of " + std::to_string(props));
        for (const HandCollider& c : b.colliders)
            if (c.prop < 0 || (size_t)c.prop >= props) return fail(error, "world.bin's blocks.colliders section names prop " + std::to_string(c.prop) + " of " + std::to_string(props));
        return true;
    };
    if (!bin.find("blocks")) {
        if (!propsOk(0)) return false;
        b.index();
        out = std::move(b);
        return true;
    }
    std::vector<u8> bytes;
    if (!bin.bytes("blocks", bytes)) return fail(error, "world.bin's blocks section is damaged");
    ByteReader r(bytes.data(), bytes.size());
    const u32 version = r.getU32();
    if (version != kBlocksSectionVersion) return fail(error, "world.bin's blocks section is version " + std::to_string(version) + ", not " + std::to_string(kBlocksSectionVersion));
    const u32 groundCount = r.getU32();
    if (r.bad || groundCount > (u32)(r.end - r.p) / 12u) return fail(error, "world.bin's blocks section is truncated (ground)");
    b.ground.resize(groundCount);
    for (u32 i = 0; i < groundCount; i++) {
        GroundPoly& g = b.ground[i];
        const u32 bits = r.getU32();
        g.kind = (u8)(bits & 255u);
        g.flat = (u8)((bits >> 8) & 1u);
        g.z = r.getF32();
        const u32 n = r.getU32();
        if (r.bad || n < 3u || n > (u32)(r.end - r.p) / 8u || g.kind >= GK_COUNT) return fail(error, "world.bin's blocks section: ground polygon " + std::to_string(i) + " is damaged");
        g.poly.resize(n);
        for (vec2& p : g.poly) {
            p.x = r.getF32();
            p.y = r.getF32();
        }
    }
    const u32 propCount = r.getU32();
    if (r.bad || propCount > (u32)(r.end - r.p) / 24u) return fail(error, "world.bin's blocks section is truncated (props)");
    b.props.resize(propCount);
    for (PropInstance& p : b.props) {
        const u32 bits = r.getU32();
        p.type = (u8)(bits & 255u);
        p.variant = (u8)((bits >> 8) & 255u);
        p.flags = (u16)(bits >> 16);
        p.pos.x = r.getF32();
        p.pos.y = r.getF32();
        p.pos.z = r.getF32();
        p.yaw = r.getF32();
        p.scale = r.getF32();
        if (p.type >= PROP_COUNT) return fail(error, "world.bin's blocks section: a prop of a type this build doesn't know");
    }
    const u32 lampCount = r.getU32();
    if (r.bad || lampCount > (u32)(r.end - r.p) / 48u) return fail(error, "world.bin's blocks section is truncated (lamps)");
    b.lamps.resize(lampCount);
    b.lampSched.resize(lampCount);
    for (u32 i = 0; i < lampCount; i++) {
        LightInstance& l = b.lamps[i];
        const u32 bits = r.getU32();
        l.type = (u8)(bits & 255u);
        b.lampSched[i] = (u8)((bits >> 8) & 255u);
        l.pos.x = r.getF32();
        l.pos.y = r.getF32();
        l.pos.z = r.getF32();
        l.color.x = r.getF32();
        l.color.y = r.getF32();
        l.color.z = r.getF32();
        l.radius = r.getF32();
        l.dir.x = r.getF32();
        l.dir.y = r.getF32();
        l.dir.z = r.getF32();
        l.cone = r.getF32();
    }
    if (!r.done()) return fail(error, "world.bin's blocks section has bytes past its end, or is truncated");
    if (!propsOk(b.props.size())) return false;
    b.index();
    out = std::move(b);
    return true;
}

}  // namespace World
