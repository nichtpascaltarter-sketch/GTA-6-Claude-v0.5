// The hand-built world's buildings sections (buildingsbin.h).
#include "buildingsbin.h"
#include <cmath>
#include <cstring>
#include <utility>

namespace World {

namespace buildingsbin_detail {

const char* const kClasses[kShellClassCount] = {"tower", "midrise", "condo", "deco", "shop", "stripmall", "house", "villa", "warehouse",
                                                "factory", "farmhouse", "barn", "motel", "gasstation", "garage", "church", "shack", "landmark"};

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

void putV2(ByteWriter& w, vec2 v) {
    w.putF32(v.x);
    w.putF32(v.y);
}
vec2 getV2(ByteReader& r) {
    const float x = r.getF32(), y = r.getF32();
    return vec2(x, y);
}
void putPalette(ByteWriter& w, const u32 (&p)[PAL_COUNT]) {
    for (u32 c : p) w.putU32(c);
}
void getPalette(ByteReader& r, u32 (&p)[PAL_COUNT]) {
    for (u32& c : p) c = r.getU32();
}
bool colourOk(u32 c) { return c == kNone32 || c <= 0xffffffu; }

}  // namespace buildingsbin_detail

const char* shellClassName(int c) { return c >= 0 && c < kShellClassCount ? buildingsbin_detail::kClasses[c] : ""; }
int shellClass(const std::string& word) {
    for (int i = 0; i < kShellClassCount; i++)
        if (word == buildingsbin_detail::kClasses[i]) return i;
    return -1;
}

int buildingWord(const char* kind, const std::string& word) {
    static const char* const kSlot[] = {"wall", "trim", "door", "accent", "roof"};
    static const char* const kTrim[] = {"none", "band", "cornice", "quoin", "eyebrow"};
    static const char* const kWindow[] = {"fixed", "dhung", "casement", "slider", "storefront", "jalousie", "glassblock"};
    static const char* const kDoor[] = {"hinged", "pair", "sliding", "revolving", "rollup", "lift", "opening"};
    static const char* const kShutters[] = {"none", "fixed", "bahama"};
    static const char* const kRoof[] = {"flat", "gable", "hip", "shed", "mansard", "barrel"};
    struct List {
        const char* name;
        const char* const* words;
        int n;
    };
    static const List kLists[] = {{"slot", kSlot, PAL_COUNT},          {"trim", kTrim, TRIM_COUNT},          {"window", kWindow, WIN_COUNT},
                                  {"door", kDoor, DOOR_COUNT},         {"shutters", kShutters, SHUTTER_COUNT}, {"roof", kRoof, RFORM_COUNT}};
    for (const List& l : kLists)
        if (strcmp(l.name, kind) == 0)
            for (int i = 0; i < l.n; i++)
                if (word == l.words[i]) return i;
    return -1;
}

void writeBuildings(const HandBuildings& b, WorldBinWriter& out) {
    using namespace buildingsbin_detail;
    if (!b.looks.empty() || !b.shells.empty()) {
        ByteWriter w;
        w.putU32(kLibrarySectionVersion);
        w.putU32((u32)b.looks.size());
        for (const HandLook& l : b.looks) {
            w.putString(l.id);
            w.putU32(l.wallMaterial);
            w.putU32(l.wallColor);
            w.putU32((u32)l.trims.size());
            for (const HandLook::Trim& t : l.trims) {
                w.putU32(t.kind);
                w.putU32(t.material);
                w.putU32(t.color);
                w.putF32(t.h);
            }
            w.putU32(l.windowKind);
            w.putU32(l.frameMaterial);
            w.putU32(l.frameColor);
            w.putU32(l.shutters);
            w.putU32(l.doorColor);
            w.putF32(l.baseH);
            w.putU32(l.baseMaterial);
        }
        w.putU32((u32)b.shells.size());
        for (const HandShell& s : b.shells) {
            w.putString(s.id);
            w.putU32((u32)s.cls);
            putV2(w, s.size);
            w.putPoints(s.footprint);
            w.putU32(s.look < 0 ? kNone32 : (u32)s.look);
            putPalette(w, s.palette);
            w.putU32(s.roofForm);
            w.putF32(s.pitch);
            w.putF32(s.overhang);
            w.putU32(s.roofMaterial);
            w.putU32((u32)s.floors.size());
            for (const HandShell::Floor& f : s.floors) {
                w.putString(f.id);
                w.putF32(f.z);
                w.putF32(f.h);
            }
            w.putU32((u32)s.walls.size());
            for (const HandShell::Wall& wl : s.walls) {
                w.putU32((u32)wl.floor);
                putV2(w, wl.a);
                putV2(w, wl.b);
                w.putF32(wl.t);
                w.putU32(wl.side);
                w.putU32((u32)wl.openings.size());
                for (const HandShell::Opening& o : wl.openings) {
                    w.putU32(o.kind);
                    w.putF32(o.at);
                    w.putF32(o.w);
                    w.putF32(o.h);
                    w.putF32(o.sill);
                    w.putU32(o.schedule);
                    w.putU32(o.room);
                }
            }
        }
        out.addCompressed("library", w.bytes);
    }
    if (!b.placements.empty()) {
        ByteWriter w;
        w.putU32(kBuildingsSectionVersion);
        w.putU32((u32)b.placements.size());
        for (const HandPlacement& p : b.placements) {
            w.putString(p.id);
            w.putU32((u32)p.shell);
            putV2(w, p.at);
            w.putF32(p.z);
            w.putF32(p.yaw);
            w.putU32(p.mirror ? 1u : 0u);
            putPalette(w, p.palette);
            w.putU32(p.roofMaterial);
            w.putU32(p.look < 0 ? kNone32 : (u32)p.look);
        }
        out.addCompressed("buildings", w.bytes);
    }
}

bool readBuildings(const WorldBinView& bin, HandBuildings& out, std::string* error) {
    using namespace buildingsbin_detail;
    out = HandBuildings();   // (left empty on failure: never half the buildings)
    HandBuildings b;
    const auto left = [](const ByteReader& r, u32 n, u32 each) { return !r.bad && n <= (u32)(r.end - r.p) / each; };
    if (bin.find("library")) {
        std::vector<u8> bytes;
        if (!bin.bytes("library", bytes)) return fail(error, "world.bin's library section is damaged");
        ByteReader r(bytes.data(), bytes.size());
        const u32 version = r.getU32();
        if (version != kLibrarySectionVersion)
            return fail(error, "world.bin's library section is version " + std::to_string(version) + ", not " + std::to_string(kLibrarySectionVersion));
        u32 n = r.getU32();
        if (!left(r, n, 44u)) return fail(error, "world.bin's library section is truncated (looks)");   // (44: the least a look takes)
        b.looks.resize(n);
        for (HandLook& l : b.looks) {
            l.id = r.getString();
            const u32 wm = r.getU32(), wc = r.getU32(), trims = r.getU32();
            if (!left(r, trims, 16u) || wc >= PAL_COUNT || wm > 255u) return fail(error, "world.bin's library section: look " + l.id + " is damaged");
            l.wallMaterial = (u8)wm;
            l.wallColor = (u8)wc;
            l.trims.resize(trims);
            for (HandLook::Trim& t : l.trims) {
                const u32 k = r.getU32(), m = r.getU32(), c = r.getU32();
                t.h = r.getF32();
                if (r.bad || k >= TRIM_COUNT || m > 255u || c >= PAL_COUNT) return fail(error, "world.bin's library section: look " + l.id + " is damaged");
                t.kind = (u8)k;
                t.material = (u8)m;
                t.color = (u8)c;
            }
            const u32 wk = r.getU32(), fm = r.getU32(), fc = r.getU32(), sh = r.getU32(), dc = r.getU32();
            l.baseH = r.getF32();
            const u32 bm = r.getU32();
            if (r.bad || wk >= WIN_COUNT || fm > 255u || fc >= PAL_COUNT || sh >= SHUTTER_COUNT || dc >= PAL_COUNT || bm > 255u)
                return fail(error, "world.bin's library section: look " + l.id + " is damaged");
            l.windowKind = (u8)wk;
            l.frameMaterial = (u8)fm;
            l.frameColor = (u8)fc;
            l.shutters = (u8)sh;
            l.doorColor = (u8)dc;
            l.baseMaterial = (u8)bm;
        }
        n = r.getU32();
        if (!left(r, n, 68u)) return fail(error, "world.bin's library section is truncated (shells)");   // (68: the least a shell takes)
        b.shells.resize(n);
        for (HandShell& s : b.shells) {
            const std::string what = "world.bin's library section: shell " + (s.id = r.getString()) + " is damaged";
            const u32 cls = r.getU32();
            s.cls = (int)cls;
            s.size = getV2(r);
            r.getPoints(s.footprint);
            const u32 look = r.getU32();
            s.look = look == kNone32 ? -1 : (int)look;
            getPalette(r, s.palette);
            const u32 rf = r.getU32();
            s.pitch = r.getF32();
            s.overhang = r.getF32();
            const u32 rm = r.getU32();
            bool ok = !r.bad && cls < (u32)kShellClassCount && (look == kNone32 || look < b.looks.size()) && rf < RFORM_COUNT && rm <= 255u;
            for (u32 c : s.palette) ok = ok && colourOk(c);
            if (!ok) return fail(error, what);
            s.roofForm = (u8)rf;
            s.roofMaterial = (u8)rm;
            u32 floors = r.getU32();
            if (!left(r, floors, 12u)) return fail(error, what);
            s.floors.resize(floors);
            for (HandShell::Floor& f : s.floors) {
                f.id = r.getString();
                f.z = r.getF32();
                f.h = r.getF32();
            }
            const u32 walls = r.getU32();
            if (!left(r, walls, 32u)) return fail(error, what);
            s.walls.resize(walls);
            for (HandShell::Wall& wl : s.walls) {
                const u32 floor = r.getU32();
                wl.a = getV2(r);
                wl.b = getV2(r);
                wl.t = r.getF32();
                const u32 side = r.getU32(), openings = r.getU32();
                if (!left(r, openings, 28u) || floor >= floors || side > 3u) return fail(error, what);
                wl.floor = (int)floor;
                wl.side = (u8)side;
                wl.openings.resize(openings);
                for (HandShell::Opening& o : wl.openings) {
                    const u32 kind = r.getU32();
                    o.at = r.getF32();
                    o.w = r.getF32();
                    o.h = r.getF32();
                    o.sill = r.getF32();
                    o.schedule = r.getU32();
                    o.room = r.getU32();
                    const bool door = (kind & 0x100u) != 0;
                    if (r.bad || (kind & ~0x1ffu) || (kind & 255u) >= (door ? (u32)DOOR_COUNT : (u32)WIN_COUNT) || (o.schedule != kNone32 && o.schedule > 15u) ||
                        (o.room != kNone32 && o.room > 0xffffu))
                        return fail(error, what);
                    o.kind = (u16)kind;
                }
            }
        }
        if (!r.done()) return fail(error, "world.bin's library section has bytes past its end, or is truncated");
    }
    if (bin.find("buildings")) {
        std::vector<u8> bytes;
        if (!bin.bytes("buildings", bytes)) return fail(error, "world.bin's buildings section is damaged");
        ByteReader r(bytes.data(), bytes.size());
        const u32 version = r.getU32();
        if (version != kBuildingsSectionVersion)
            return fail(error, "world.bin's buildings section is version " + std::to_string(version) + ", not " + std::to_string(kBuildingsSectionVersion));
        const u32 n = r.getU32();
        if (!left(r, n, 56u)) return fail(error, "world.bin's buildings section is truncated");
        b.placements.resize(n);
        for (HandPlacement& p : b.placements) {
            p.id = r.getString();
            const u32 shell = r.getU32();
            p.at = getV2(r);
            p.z = r.getF32();
            p.yaw = r.getF32();
            const u32 mirror = r.getU32();
            getPalette(r, p.palette);
            p.roofMaterial = r.getU32();
            const u32 look = r.getU32();
            bool ok = !r.bad && shell < b.shells.size() && mirror <= 1u && (p.roofMaterial == kNone32 || p.roofMaterial <= 255u) &&
                      (look == kNone32 || look < b.looks.size()) && std::isfinite(p.yaw) && std::isfinite(p.z);
            for (u32 c : p.palette) ok = ok && colourOk(c);
            if (!ok) return fail(error, "world.bin's buildings section: building " + p.id + " is damaged");
            p.shell = (int)shell;
            p.mirror = mirror != 0;
            p.look = look == kNone32 ? -1 : (int)look;
        }
        if (!r.done()) return fail(error, "world.bin's buildings section has bytes past its end, or is truncated");
    }
    out = std::move(b);
    return true;
}

}  // namespace World
