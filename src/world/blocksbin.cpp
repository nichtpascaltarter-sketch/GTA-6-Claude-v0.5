// The hand-built world's blocks section (blocksbin.h).
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
    levelByCell.assign(cells, {});
    for (size_t i = 0; i < ground.size(); i++) {
        vec2 c(0.f);
        for (const vec2& p : ground[i].poly) c += p;
        if (ground[i].poly.empty()) continue;
        const int k = cellOf(c * (1.f / (float)ground[i].poly.size()));
        if (k >= 0) groundByCell[(size_t)k].push_back((int)i);
        if (!ground[i].flat || ground[i].poly.size() < 3) continue;
        // the level ground: in every cell its outline's box reaches, a little past its edges
        vec2 lo(1e30f), hi(-1e30f);
        for (const vec2& p : ground[i].poly) {
            lo = vmin(lo, p);
            hi = vmax(hi, p);
        }
        const int x0 = Clamp((int)floorf((lo.x - 0.05f + kWorldHalf) / kCellSize), 0, kCellsPerSide - 1),
                  x1 = Clamp((int)floorf((hi.x + 0.05f + kWorldHalf) / kCellSize), 0, kCellsPerSide - 1);
        const int y0 = Clamp((int)floorf((lo.y - 0.05f + kWorldHalf) / kCellSize), 0, kCellsPerSide - 1),
                  y1 = Clamp((int)floorf((hi.y + 0.05f + kWorldHalf) / kCellSize), 0, kCellsPerSide - 1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) levelByCell[(size_t)y * kCellsPerSide + x].push_back((int)i);
    }
    for (size_t i = 0; i < props.size(); i++) {
        const int k = cellOf(props[i].pos.xy());
        if (k >= 0) propsByCell[(size_t)k].push_back((int)i);
    }
    for (size_t i = 0; i < lamps.size(); i++) {
        const int k = cellOf(lamps[i].pos.xy());
        if (k >= 0) lampsByCell[(size_t)k].push_back((int)i);
    }
}

void writeBlocks(const HandBlocks& b, WorldBinWriter& out) {
    if (b.ground.empty() && b.props.empty() && b.lamps.empty()) return;
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

bool readBlocks(const WorldBinView& bin, HandBlocks& out, std::string* error) {
    using namespace blocksbin_detail;
    out = HandBlocks();   // (left empty on failure: never half the blocks)
    if (!bin.find("blocks")) {
        out.index();
        return true;
    }
    HandBlocks b;
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
    b.index();
    out = std::move(b);
    return true;
}

}  // namespace World
