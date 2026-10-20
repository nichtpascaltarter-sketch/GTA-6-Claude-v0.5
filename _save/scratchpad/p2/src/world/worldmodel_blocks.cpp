// The blocks compile (worldmodel.h compileBlocks): parcels, plazas and dressing into the game's blocks (blocksbin.h).
#include "worldmodel.h"
#include "catalogcodes.h"
#include "roomlight.h"
#include "../core/rng.h"
#include <cmath>
#include <cstdio>

namespace World {
namespace Hand {

namespace worldmodel_blocks_detail {

std::string fmt2(float v) {
    char b[32];
    snprintf(b, sizeof(b), "%.2f", v);
    return b;
}

// The piece a dressing record names, or nullptr (an unresolved reference: worldv's to report)
const LibItem* piece(const Model& m, const Ref& r) { return r.target >= 0 && r.target < (int)m.library.size() ? &m.library[(size_t)r.target] : nullptr; }

}  // namespace worldmodel_blocks_detail

bool compileBlocks(const Model& model, BlocksOut& out, std::vector<Error>& errors) {
    using namespace worldmodel_blocks_detail;
    const size_t errorsBefore = errors.size();
    out = BlocksOut();
    const auto fail = [&errors](const Src& s, const char* code, const std::string& msg) { errors.push_back(Error{s, code, msg}); };
    const auto polyOk = [&](const std::vector<vec2>& poly, const Src& s, const std::string& what) {
        if (poly.size() >= 3) return true;
        fail(s, "block.shape", what + ": a polygon needs 3 points or more");
        return false;
    };
    const auto ground = [&](const std::string& word, const Src& s, const std::string& what, GroundKind fallback) {
        if (word.empty()) return fallback;
        bool known = false;
        const GroundKind k = groundKind(word, &known);
        if (!known) fail(s, "block.kind", what + ": no ground kind " + word);
        return known ? k : fallback;
    };
    HandBlocks& hb = out.blocks;
    // ---------------------------------------------------------------- the ground: parcels (their paint and pads), plazas
    for (const Block& b : model.blocks) {
        for (int pi : b.parcels) {
            const Parcel& p = model.parcels[(size_t)pi];
            const std::string what = "parcel " + p.id;
            if (!polyOk(p.poly, p.src, what)) continue;
            if (p.graded) out.pads.push_back(BlockPad{p.poly, p.grade, Max(p.blend, 0.f)});
            // the parcel's own ground, then its paint over it: level on its grade, else on the terrain
            const auto add = [&](GroundKind k, const std::vector<vec2>& poly, float lift) {
                GroundPoly g;
                g.kind = (u8)k;
                g.flat = p.graded ? 1 : 0;
                g.z = (p.graded ? p.grade : 0.f) + lift;
                g.poly = poly;
                hb.ground.push_back(g);
                out.groundOnTerrain.push_back(0);
            };
            if (!p.ground.empty()) add(ground(p.ground, p.src, what, GK_LAWN), p.poly, 0.03f);
            for (const GroundPaint& g : p.paints)
                if (polyOk(g.poly, g.src, what + " paint")) add(ground(g.kind, g.src, what + " paint", GK_PAVING), g.poly, 0.05f);
            // pads: level slabs, z above the graded ground (the parcel's grade, else the terrain under the pad)
            for (const Pad& d : p.pads) {
                if (!polyOk(d.poly, d.src, what + " pad")) continue;
                GroundPoly g;
                g.kind = (u8)ground(d.kind, d.src, what + " pad", GK_PAVING);
                g.flat = 1;
                g.z = (p.graded ? p.grade : 0.f) + d.z + 0.06f;
                g.poly = d.poly;
                hb.ground.push_back(g);
                out.groundOnTerrain.push_back(p.graded ? 0 : 1);
            }
        }
        for (int zi : b.plazas) {
            const Plaza& z = model.plazas[(size_t)zi];
            if (!polyOk(z.poly, z.src, "plaza " + z.id)) continue;
            GroundPoly g;
            g.kind = (u8)ground(z.ground, z.src, "plaza " + z.id, GK_PAVING);
            g.z = 0.04f;
            g.poly = z.poly;
            hb.ground.push_back(g);
            out.groundOnTerrain.push_back(0);
        }
    }
    // ---------------------------------------------------------------- the dressing
    for (const Dress& d : model.dressing) {
        const LibItem* it = piece(model, d.kind);
        if (!it) continue;   // (names nothing: worldv reports it)
        const char* rec = d.what == Dress::DRESS_TREE ? "tree" : d.what == Dress::DRESS_PLANT ? "plant" : d.what == Dress::DRESS_PROP ? "prop" : "light";
        const std::string what = std::string(rec) + " " + it->id;
        // trees and plants without a yaw= or a look turn and vary by their position, so a row never repeats
        const u32 h = hash32((u32)(i32)floorf(d.at.x * 100.f) * 0x9e3779b9u ^ (u32)(i32)floorf(d.at.y * 100.f) * 0x85ebca77u);
        const bool living = d.what == Dress::DRESS_TREE || d.what == Dress::DRESS_PLANT;
        const float yaw = (d.hasYaw || !living) ? d.yaw * kDegToRad : (float)(h & 0xffffu) * (kTwoPi / 65536.f);
        const vec2 fwd(-sinf(yaw), cosf(yaw)), right(cosf(yaw), sinf(yaw));   // the piece's +y and +x in the world
        if (d.what == Dress::DRESS_LIGHT) {
            const LampSpec* ls = lampSpec(it->code.c_str());
            if (!ls) {
                fail(d.src, "block.kind", what + ": the game builds no light fixture for code=" + it->code);
                continue;
            }
            if (ls->prop >= 0) {   // a fixture: its prop, and the lamp in its frame
                PropInstance pi;
                pi.pos = vec3(d.at, d.hasZ ? d.z : 0.f);
                pi.yaw = yaw + propYawOffset(ls->prop);
                pi.scale = 1.f;
                pi.type = (u8)ls->prop;
                pi.variant = 0;
                pi.flags = 0;
                hb.props.push_back(pi);
            }
            if (d.sched == "never") continue;   // (a fixture that stays dark: its prop alone)
            LightInstance li;
            const vec2 off = right * ls->at.x + fwd * ls->at.y;
            li.pos = vec3(d.at + off, (d.hasZ ? d.z : 0.f) + ls->at.z);
            li.color = ls->color * ls->intensity;
            li.radius = ls->radius;
            li.cone = ls->cone;
            li.dir = ls->cone > 0.f ? vec3(0.f, 0.f, -1.f) : vec3(0.f);
            li.type = ls->type;
            hb.lamps.push_back(li);
            // when it is lit, as roomlight.h's classes: dusk and night on the photocell, evening on the porch timer
            hb.lampSched.push_back((u8)(d.sched == "always" ? RS_ALWAYS : d.sched == "evening" ? RS_PORCH : RS_DUSK));
            out.lampGroundAt.push_back(d.at);
            continue;
        }
        const int type = propTypeByName(it->code.c_str());
        if (type < 0) {
            fail(d.src, "block.kind", what + ": the game builds no prop for code=" + it->code);
            continue;
        }
        PropInstance pi;
        pi.pos = vec3(d.at, d.what == Dress::DRESS_PROP && d.hasZ ? d.z : 0.f);
        pi.yaw = yaw + propYawOffset(type);
        pi.scale = 1.f;
        if (d.what == Dress::DRESS_TREE && d.h > 0.f && it->size.z > 0.f) pi.scale = d.h / it->size.z;
        if (d.what == Dress::DRESS_PLANT && d.scale > 0.f) pi.scale = d.scale;
        if (pi.scale < 0.2f || pi.scale > 5.f) {
            fail(d.src, "block.size", what + ": a scale of " + fmt2(pi.scale) + " of the piece (h= against its size=)");
            continue;
        }
        pi.type = (u8)type;
        pi.variant = (u8)(d.what == Dress::DRESS_PROP ? Clamp(d.look, 0, 255) : (it->variants > 1 ? (int)((h >> 16) % (u32)it->variants) : 0));
        pi.flags = 0;
        hb.props.push_back(pi);
    }
    return errors.size() == errorsBefore;
}

}  // namespace Hand
}  // namespace World
