// Port Isle container terminal meshes: quay walls, ship-to-shore gantry cranes, container stacks, straddle carriers,
// docked container ships (original designs), rail yard and lead track, gate complex.
#include "sites.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace port_mesh {

using namespace sitegeo;

// Shipping lines (original): name, box colour, emblem colour, emblem style, owner code on the doors
struct Line {
    const char* name;
    vec3 color, mark;
    int emblem;  // 0 wave, 1 diamond, 2 sun disc, 3 band, 4 chevron
    const char* code;
};
const Line kLines[] = {
    {"TIDELINE", vec3(0.08f, 0.45f, 0.5f), vec3(0.95f), 0, "TDLU"},        {"CORALCO", vec3(0.78f, 0.25f, 0.18f), vec3(0.95f), 1, "CRLU"},
    {"SOLMAR", vec3(0.95f, 0.72f, 0.12f), vec3(0.2f, 0.15f, 0.1f), 2, "SOMU"}, {"MERIDIAN", vec3(0.12f, 0.2f, 0.5f), vec3(0.95f), 3, "MRDU"},
    {"GREENWAKE", vec3(0.2f, 0.45f, 0.22f), vec3(0.95f), 4, "GWKU"},       {"HARBORLINK", vec3(0.55f, 0.57f, 0.58f), vec3(0.1f, 0.25f, 0.55f), 3, "HBLU"},
    {"ORCALINE", vec3(0.92f, 0.45f, 0.1f), vec3(0.1f), 0, "ORCU"},         {"BAYCRATE", vec3(0.45f, 0.12f, 0.14f), vec3(0.95f), 2, "BYCU"},
    {"ATLAS BLUE", vec3(0.25f, 0.5f, 0.75f), vec3(0.95f), 1, "ATBU"},      {"SUNSHIP", vec3(0.92f, 0.92f, 0.9f), vec3(0.85f, 0.2f, 0.1f), 2, "SNSU"},
};
const int kLineCount = (int)(sizeof(kLines) / sizeof(kLines[0]));
// Leasing pools: plain boxes without a line livery
const vec3 kLeaseCol[] = {vec3(0.74f, 0.67f, 0.54f), vec3(0.34f, 0.38f, 0.45f), vec3(0.52f, 0.31f, 0.2f), vec3(0.67f, 0.68f, 0.67f), vec3(0.22f, 0.34f, 0.27f),
                          vec3(0.62f, 0.21f, 0.13f)};
const char* const kLeaseCode[] = {"PSLU", "KYTU", "BRVU", "MNTU", "QRLU", "ZNCU"};
const int kLeaseCount = (int)(sizeof(kLeaseCol) / sizeof(kLeaseCol[0]));

// ------------------------------------------------------------------------------------------------ shipping containers
// ISO boxes at real size: 40 ft (12.192 m) and 20 ft (6.058 m) long, 2.438 m wide, 2.591 m standard or 2.896 m high cube.
// A face is seen from outside with u to the right and v up; d is the depth out of the box envelope (the outer faces of the
// corner castings), negative inward. Detail per face:
//   CL_HERO   the outside of a stack block at eye level: trapezoidal corrugation in geometry (278 mm pitch, 36 mm deep)
//             with the grooves darkened in the vertex colours, proud rails, corner posts and castings with their apertures,
//             door ends with two leaves, hinges, four locking bars with guides, cam keepers and handles, plates, ID codes
//   CL_MID    exposed faces higher up or down the alleys: a flat panel whose shading columns tilt the normals the way the
//             ribs do and darken the grooves (so the ribs read under any light), frame members as strips, castings proud
//   CL_FLATC  faces above lower neighbours: one panel with frame shading and the corner castings
//   CL_FLAT   faces in the narrow alleys and gaps between boxes: one dark panel
// Wear: faded and repainted paint, sun-bleached tops, rust streaks running down from the top rail, grime along the bottom.
enum CtrLod : u8 { CL_NONE = 0, CL_FLAT, CL_FLATC, CL_MID, CL_HERO };

struct Ctr {
    vec3 base;                    // centre of the floor (world)
    vec2 ax = vec2(1, 0);         // unit length axis
    float len = 12.192f, wid = 2.438f, hgt = 2.591f;
    vec3 col = vec3(0.6f);        // paint (vertex colour over MAT_METAL_PAINTED)
    u8 lod[5] = {0, 0, 0, 0, 0};  // faces -Y, +Y, -X, +X, roof
    u8 marks = 0;                 // long faces (bit 0 -Y, bit 1 +Y) carrying the line name / logo
    bool doorsPlus = true;        // door end at +X
    bool stacked = false;         // stands on another box: twistlock gap below
    bool reefer = false;          // refrigeration unit in the front end
    bool coarse = false;          // mid faces high up: one shading column per rib (half the vertices)
    int line = -1;                // kLines index (-1: leasing box)
    const char* code = "PSLU";    // owner code on the doors
    u32 seed = 0;
    float fade = 0.f, rust = 0.f, grime = 0.3f;
};

namespace ctr {
constexpr float kCastW = 0.178f, kCastH = 0.118f, kPostW = 0.15f, kRailB = 0.16f, kRailT = 0.1f;
constexpr float kDPost = -0.006f, kDRail = -0.012f, kDOut = -0.026f, kDIn = -0.062f, kDFlat = -0.014f, kDBack = -0.1f;
constexpr float kPitch = 0.278f, kGap = 0.025f, kTilt = 0.42f;
const vec3 kRust(0.5f, 0.24f, 0.11f), kCast(0.2f, 0.19f, 0.18f);
}  // namespace ctr

inline u32 pkc(vec3 c) { return packRGBA8(c.x, c.y, c.z, 1.f); }
inline vec3 bleachC(vec3 c, float a) {
    float l = dot(c, vec3(0.3f, 0.55f, 0.15f));
    return lerp(c, vec3(l * 1.1f + 0.06f), a);
}

// One container face seen from outside: o = bottom-left corner on the envelope, r = right (cross(up, n)), n = outward
struct CFace {
    vec3 o, r, n;
    float w, h, uv0;
    vec3 P(float u, float v, float d) const { return o + r * u + vec3(0, 0, v) + n * d; }
};

// Paint of one face: base colour, a repainted patch, bleach at the top edge, rust streaks per rib, grime at the bottom
struct CPaint {
    vec3 base, patch;
    float p0 = 1e9f, p1 = -1e9f;
    float bleach = 0.1f;
    vec3 grime = vec3(1.f);
    float streakP = 0.f;
    u32 seed = 0;
    vec3 at(float u, bool top) const {
        vec3 c = (u >= p0 && u <= p1) ? patch : base;
        if (!top) return c * grime;
        c = bleachC(c, bleach);
        if (streakP > 0.f) {
            u32 h = hash2i((int)floorf(u / ctr::kPitch), (int)seed);
            if (hashToFloat(h) < streakP) c = lerp(c, ctr::kRust, 0.3f + 0.45f * hashToFloat(hash32(h)));
        }
        return c;
    }
};

// Corrugation profile column: u, depth, normal tilt toward +u (radians), groove shading
struct PCol {
    float u, d, tilt, ao;
};

// Columns of a corrugated panel across [u0, u1]. geo: trapezoidal ribs in geometry (outer flat, slope in, inner flat,
// slope out; 4 columns per pitch). Otherwise a flat panel at dOut with 2 shading columns per pitch.
int corrColumns(PCol* out, int cap, float u0, float u1, float pitch, float dOut, float dIn, bool geo) {
    int n = 0;
    auto add = [&](float u, float d, float t, float ao) {
        if (n < cap) out[n++] = {u, d, t, ao};
    };
    float span = u1 - u0;
    int np = (int)floorf(span / pitch + 1e-3f);
    add(u0, dOut, 0.f, 1.f);
    if (np < 1 || n + np * 4 + 2 > cap) {
        add(u1, dOut, 0.f, 1.f);
        return n;
    }
    float x = u0 + Max(0.f, span - np * pitch) * 0.5f;
    const float T = ctr::kTilt, fa = 0.26f, fs = 0.235f, fb = 0.27f;
    for (int k = 0; k < np; k++, x += pitch) {
        if (geo) {
            add(x, dOut, k == 0 ? 0.f : -T, 1.f);
            add(x + pitch * fa, dOut, T, 1.f);
            add(x + pitch * (fa + fs), dIn, T, 0.8f);
            add(x + pitch * (fa + fs + fb), dIn, -T, 0.8f);
        } else {
            add(x + pitch * fa, dOut, T * 0.8f, 1.f);
            add(x + pitch * (fa + fs + fb), dOut, -T * 0.8f, 0.8f);
        }
    }
    if (geo) add(x, dOut, -T * 0.5f, 1.f);
    add(u1, dOut, 0.f, 1.f);
    return n;
}

// Emits the corrugated strip (shared vertices per column) between heights v0 and v1
void corrStrip(G& g, const CFace& f, const PCol* c, int n, float v0, float v1, const CPaint& pt, u32 mat) {
    MeshData& m = *g.m;
    u32 pb = 0, ptp = 0;
    for (int i = 0; i < n; i++) {
        float cs = cosf(c[i].tilt), sn = sinf(c[i].tilt);
        vec3 nr = f.n * cs + f.r * sn, tg = f.r * cs - f.n * sn;
        u32 cb = pkc(pt.at(c[i].u, false) * c[i].ao), ct = pkc(pt.at(c[i].u, true) * c[i].ao);
        u32 b = m.addVertex(f.P(c[i].u, v0, c[i].d) - g.org, nr, tg, vec2(f.uv0 + c[i].u, v0), cb, mat);
        u32 t = m.addVertex(f.P(c[i].u, v1, c[i].d) - g.org, nr, tg, vec2(f.uv0 + c[i].u, v1), ct, mat);
        if (i > 0 && c[i].u - c[i - 1].u > 1e-4f) m.quadIdx(pb, b, t, ptp);
        pb = b;
        ptp = t;
    }
}

// Flat quad on a face with per-edge colours (bottom / top)
void fpanel(G& g, const CFace& f, float u0, float u1, float v0, float v1, float d, u32 cb, u32 ct, u32 mat) {
    MeshData& m = *g.m;
    vec3 tg = f.r;
    u32 a = m.addVertex(f.P(u0, v0, d) - g.org, f.n, tg, vec2(f.uv0 + u0, v0), cb, mat);
    u32 b = m.addVertex(f.P(u1, v0, d) - g.org, f.n, tg, vec2(f.uv0 + u1, v0), cb, mat);
    u32 c = m.addVertex(f.P(u1, v1, d) - g.org, f.n, tg, vec2(f.uv0 + u1, v1), ct, mat);
    u32 e = m.addVertex(f.P(u0, v1, d) - g.org, f.n, tg, vec2(f.uv0 + u0, v1), ct, mat);
    m.quadIdx(a, b, c, e);
}

// Box on a face: u0..u1, v0..v1, from depth d0 (back) to d1 (front). faces: 1 front, 2 left, 4 right, 8 top, 16 bottom
void fbox(G& g, const CFace& f, float u0, float u1, float v0, float v1, float d0, float d1, u32 col, u32 mat, u32 faces) {
    MeshData& m = *g.m;
    if (faces & 1) quad(g, m, f.P(u0, v0, d1), f.P(u1, v0, d1), f.P(u1, v1, d1), f.P(u0, v1, d1), col, mat, f.n);
    if (faces & 2) quad(g, m, f.P(u0, v0, d0), f.P(u0, v0, d1), f.P(u0, v1, d1), f.P(u0, v1, d0), col, mat, -f.r);
    if (faces & 4) quad(g, m, f.P(u1, v0, d1), f.P(u1, v0, d0), f.P(u1, v1, d0), f.P(u1, v1, d1), col, mat, f.r);
    if (faces & 8) quad(g, m, f.P(u0, v1, d1), f.P(u1, v1, d1), f.P(u1, v1, d0), f.P(u0, v1, d0), col, mat, vec3(0, 0, 1));
    if (faces & 16) quad(g, m, f.P(u0, v0, d0), f.P(u1, v0, d0), f.P(u1, v0, d1), f.P(u0, v0, d1), col, mat, vec3(0, 0, -1));
}

// Clips a polygon in (u, v) to u >= lo (keepAbove) or u <= lo
int clipU(const vec2* in, int n, vec2* out, float lo, bool keepAbove) {
    int k = 0;
    for (int i = 0; i < n; i++) {
        vec2 a = in[i], b = in[(i + 1) % n];
        bool ia = keepAbove ? a.x >= lo : a.x <= lo, ib = keepAbove ? b.x >= lo : b.x <= lo;
        if (ia) out[k++] = a;
        if (ia != ib) out[k++] = lerp(a, b, (lo - a.x) / (b.x - a.x));
    }
    return k;
}

// Paints a convex polygon given in face (u, v) coordinates onto the surface: over a profile (columns c) it is split at
// every column so each piece lies on its rib facet; without one it sits at depth dFlat. lift keeps it off the surface.
void paintPoly(G& g, const CFace& f, const PCol* c, int nc, float dFlat, const vec2* poly, int np, u32 col, u32 mat, float lift = 0.004f) {
    if (np < 3 || np > 12) return;
    vec2 P0[12];
    float area = 0.f;
    for (int i = 0; i < np; i++) area += cross(poly[i], poly[(i + 1) % np]);
    for (int i = 0; i < np; i++) P0[i] = area >= 0.f ? poly[i] : poly[np - 1 - i];
    float umin = 1e9f, umax = -1e9f;
    for (int i = 0; i < np; i++) {
        umin = Min(umin, P0[i].x);
        umax = Max(umax, P0[i].x);
    }
    auto surf = [&](float u, float& d, float& tilt) {
        if (!c || nc < 2) {
            d = dFlat;
            tilt = 0.f;
            return;
        }
        if (u <= c[0].u) { d = c[0].d; tilt = c[0].tilt; return; }
        if (u >= c[nc - 1].u) { d = c[nc - 1].d; tilt = c[nc - 1].tilt; return; }
        int lo = 0, hi = nc - 1;
        while (hi - lo > 1) {
            int mid = (lo + hi) / 2;
            if (c[mid].u <= u) lo = mid;
            else hi = mid;
        }
        float t = (u - c[lo].u) / Max(c[hi].u - c[lo].u, 1e-6f);
        d = Lerp(c[lo].d, c[hi].d, t);
        tilt = Lerp(c[lo].tilt, c[hi].tilt, t);
    };
    int k = 0;
    if (c && nc >= 2)
        while (k < nc && c[k].u <= umin) k++;
    float ua = umin;
    MeshData& m = *g.m;
    for (;;) {
        float ub = (c && k < nc && c[k].u < umax) ? c[k].u : umax;
        if (ub - ua > 1e-5f) {
            vec2 A[16], B[16];
            int na = clipU(P0, np, A, ua, true);
            int nb = na >= 3 ? clipU(A, na, B, ub, false) : 0;
            if (nb >= 3) {
                u32 b0 = (u32)m.verts.size();
                for (int i = 0; i < nb; i++) {
                    float d, tl;
                    surf(Clamp(B[i].x, ua, ub), d, tl);
                    float cs = cosf(tl), sn = sinf(tl);
                    m.addVertex(f.P(B[i].x, B[i].y, d + lift) - g.org, f.n * cs + f.r * sn, f.r * cs - f.n * sn, B[i], col, mat);
                }
                for (int i = 1; i + 1 < nb; i++) m.tri(b0, b0 + i, b0 + i + 1);
            }
        }
        if (ub >= umax) break;
        ua = ub;
        k++;
    }
}

// Stroke-font text painted over a face (see strokeText); origin = bottom-left of the first glyph in (u, v)
void paintText(G& g, const CFace& f, const PCol* c, int nc, float dFlat, const char* txt, vec2 origin, float height, float strokeW, u32 col, u32 mat,
               float spacing = 0.3f) {
    float unit = height / 9.f, x = 0.f;
    for (const char* ch = txt; *ch; ch++) {
        if (*ch == ' ') {
            x += 4.5f * unit;
            continue;
        }
        for (auto& st : glyphStrokes(*ch))
            for (size_t i = 0; i + 1 < st.pts.size(); i++) {
                vec2 a = st.pts[i] * unit + vec2(x, 0), b = st.pts[i + 1] * unit + vec2(x, 0);
                vec2 d = b - a;
                float len = length(d);
                d = len < 1e-4f ? vec2(0, 1) : d / len;
                vec2 e = perp(d) * (strokeW * 0.5f), ext = d * (strokeW * 0.5f);
                vec2 q[4] = {origin + a - ext - e, origin + b + ext - e, origin + b + ext + e, origin + a - ext + e};
                paintPoly(g, f, c, nc, dFlat, q, 4, col, mat);
            }
        x += (6.f + 6.f * spacing) * unit;
    }
}

// Line logo painted on a face around centre o (u, v), size ~ half width
void paintEmblem(G& g, const CFace& f, const PCol* c, int nc, float dFlat, int style, vec2 o, float size, u32 col, u32 mat) {
    switch (style) {
        case 0:  // wave: three strokes
            for (int k = 0; k < 3; k++) {
                vec2 a = o + vec2(-size + k * size * 0.66f, -size * 0.1f), b = a + vec2(size * 0.33f, size * 0.35f), e = b + vec2(size * 0.33f, -size * 0.35f);
                for (int s = 0; s < 2; s++) {
                    vec2 p0 = s ? b : a, p1 = s ? e : b;
                    vec2 w = perp(normalize(p1 - p0)) * (size * 0.07f);
                    vec2 q[4] = {p0 - w, p1 - w, p1 + w, p0 + w};
                    paintPoly(g, f, c, nc, dFlat, q, 4, col, mat);
                }
            }
            break;
        case 1: {  // diamond
            vec2 q[4] = {o - vec2(size, 0), o - vec2(0, size * 0.6f), o + vec2(size, 0), o + vec2(0, size * 0.6f)};
            paintPoly(g, f, c, nc, dFlat, q, 4, col, mat);
            break;
        }
        case 2: {  // sun disc
            vec2 q[10];
            for (int k = 0; k < 10; k++) q[k] = o + vec2(cosf(kTwoPi * k / 10.f), sinf(kTwoPi * k / 10.f)) * (size * 0.55f);
            paintPoly(g, f, c, nc, dFlat, q, 10, col, mat);
            break;
        }
        case 3: {  // band
            vec2 q[4] = {o + vec2(-size * 2.6f, -size * 0.15f), o + vec2(size * 2.6f, -size * 0.15f), o + vec2(size * 2.6f, size * 0.15f), o + vec2(-size * 2.6f, size * 0.15f)};
            paintPoly(g, f, c, nc, dFlat, q, 4, col, mat);
            break;
        }
        default:  // chevron
            for (int s = -1; s <= 1; s += 2) {
                vec2 a = o + vec2(s * size * 0.8f, size * 0.4f), b = o - vec2(0, size * 0.4f);
                vec2 w = perp(normalize(b - a)) * (size * 0.13f);
                vec2 q[4] = {a - w, b - w, b + w, a + w};
                paintPoly(g, f, c, nc, dFlat, q, 4, col, mat);
            }
            break;
    }
}

// Corner castings (with their apertures), corner posts and top / bottom rails of one face.
// hero: members at their real depths with the side faces that close them against the panel; otherwise coplanar strips.
void ctrFrame(G& g, const CFace& f, const CPaint& pt, int lod, float railB, float railT, bool roofOpen) {
    using namespace ctr;
    u32 mat = M(MAT_METAL_PAINTED);
    vec3 b = pt.base;
    u32 castC = pkc(lerp(b * 0.5f, kCast, 0.55f)), holeC = pkc(vec3(0.03f));
    u32 postC = pkc(bleachC(b * 0.84f, pt.bleach * 0.5f)), railTC = pkc(bleachC(b * 0.9f, pt.bleach));
    u32 railBC = pkc(lerp(b * pt.grime * 0.78f, kRust * 0.7f, 0.25f));
    float w = f.w, h = f.h;
    if (lod == CL_HERO) {
        fbox(g, f, 0.f, kCastW, 0.f, kCastH, -0.05f, 0.f, castC, mat, 1 | 4 | 8);
        fbox(g, f, w - kCastW, w, 0.f, kCastH, -0.05f, 0.f, castC, mat, 1 | 2 | 8);
        fbox(g, f, 0.f, kCastW, h - kCastH, h, -0.05f, 0.f, castC, mat, 1 | 4 | 16 | (roofOpen ? 8 : 0));
        fbox(g, f, w - kCastW, w, h - kCastH, h, -0.05f, 0.f, castC, mat, 1 | 2 | 16 | (roofOpen ? 8 : 0));
        fbox(g, f, 0.f, kPostW, kCastH, h - kCastH, kDBack, kDPost, postC, mat, 1 | 4);
        fbox(g, f, w - kPostW, w, kCastH, h - kCastH, kDBack, kDPost, postC, mat, 1 | 2);
        fbox(g, f, kPostW, w - kPostW, 0.f, railB, kDBack, kDRail, railBC, mat, 1 | 8);
        fbox(g, f, kPostW, w - kPostW, h - railT, h, kDBack, kDRail, railTC, mat, 1 | 16 | (roofOpen ? 8 : 0));
    } else if (lod == CL_MID) {
        fpanel(g, f, 0.f, kPostW, 0.f, h, kDFlat, postC, postC, mat);
        fpanel(g, f, w - kPostW, w, 0.f, h, kDFlat, postC, postC, mat);
        fpanel(g, f, kPostW, w - kPostW, 0.f, railB, kDFlat, railBC, railBC, mat);
        fpanel(g, f, kPostW, w - kPostW, h - railT, h, kDFlat, railTC, railTC, mat);
        fpanel(g, f, 0.f, kCastW, 0.f, kCastH, 0.f, castC, castC, mat);
        fpanel(g, f, w - kCastW, w, 0.f, kCastH, 0.f, castC, castC, mat);
        fpanel(g, f, 0.f, kCastW, h - kCastH, h, 0.f, castC, castC, mat);
        fpanel(g, f, w - kCastW, w, h - kCastH, h, 0.f, castC, castC, mat);
    } else if (lod == CL_FLATC) {
        fpanel(g, f, 0.f, kCastW, 0.f, kCastH, 0.f, castC, castC, mat);
        fpanel(g, f, w - kCastW, w, 0.f, kCastH, 0.f, castC, castC, mat);
        fpanel(g, f, 0.f, kCastW, h - kCastH, h, 0.f, castC, castC, mat);
        fpanel(g, f, w - kCastW, w, h - kCastH, h, 0.f, castC, castC, mat);
        return;
    } else {
        return;
    }
    // side apertures of the castings
    for (int k = 0; k < 4; k++) {
        float uc = (k & 1) ? w - kCastW * 0.5f : kCastW * 0.5f, vc = (k & 2) ? h - kCastH * 0.5f : kCastH * 0.5f;
        fpanel(g, f, uc - 0.04f, uc + 0.04f, vc - 0.03f, vc + 0.03f, 0.002f, holeC, holeC, mat);
    }
}

// Line name and logo on a long face (over the ribs when the face has them)
void ctrMarks(G& g, const CFace& f, const Ctr& k, const PCol* c, int nc, float dFlat, u32 roll) {
    if (k.line < 0) return;
    const Line& ln = kLines[k.line];
    const vec3 lw(0.3f, 0.55f, 0.15f);
    vec3 mcol = ln.mark;
    if (dot(k.col, lw) > 0.6f && dot(mcol, lw) > 0.6f) mcol = ln.color * 0.85f;   // white reefer: the line's colour
    if (dot(k.col, lw) > 0.6f && dot(mcol, lw) > 0.6f) mcol = vec3(0.1f, 0.25f, 0.55f);
    u32 mc = pkc(mcol), mat = M(MAT_METAL_PAINTED);
    float h = f.h, w = f.w;
    bool big = w > 9.f;
    bool doorRight = f.r.x * k.ax.x + f.r.y * k.ax.y > 0.f ? k.doorsPlus : !k.doorsPlus;   // is the door end on the right?
    float th = big ? 0.82f : 0.52f;
    float tw = textAdvance(ln.name, th, 0.28f);
    int kind = (int)(roll % 10u);   // 0-3 name + logo, 4-6 logo only, 7-9 plain
    if (kind >= 7) return;
    float logoS = big ? 0.5f : 0.36f;
    if (kind <= 3 && tw < w - 2.2f) {
        float u0 = (w - tw) * 0.5f + (big ? 0.f : 0.f);
        paintText(g, f, c, nc, dFlat, ln.name, vec2(u0, h * 0.52f - th * 0.5f), th, th * 0.15f, mc, mat, 0.28f);
        if (ln.emblem != 3) {
            float ul = doorRight ? u0 - logoS * 1.6f : u0 + tw + logoS * 1.6f;
            if (ul - logoS > 0.3f && ul + logoS < w - 0.3f) paintEmblem(g, f, c, nc, dFlat, ln.emblem, vec2(ul, h * 0.52f), logoS, mc, mat);
        } else {
            paintEmblem(g, f, c, nc, dFlat, 3, vec2(w * 0.5f, h * 0.52f - th * 0.9f), Min(logoS, (w - 1.f) / 5.4f), mc, mat);
        }
    } else {
        float ul = doorRight ? w * 0.72f : w * 0.28f;
        paintEmblem(g, f, c, nc, dFlat, ln.emblem, vec2(ul, h * 0.55f), logoS * 1.4f, mc, mat);
    }
}

// Door end: frame with header and sill, two corrugated leaves, hinges, four locking bars with guides, cam keepers and
// handles, plates, the ID and type code on the right leaf
void ctrDoorEnd(G& g, const CFace& f, const Ctr& k, const CPaint& pt, int lod) {
    using namespace ctr;
    u32 mat = M(MAT_METAL_PAINTED);
    float w = f.w, h = f.h;
    vec3 b = pt.base;
    const float hdr = 0.24f, sill = 0.1f;
    u32 barC = pkc(lerp(b * 0.62f, vec3(0.42f, 0.42f, 0.41f), 0.45f)), dark = pkc(vec3(0.03f));
    float uL0 = kPostW, uL1 = w * 0.5f - 0.006f, uR0 = w * 0.5f + 0.006f, uR1 = w - kPostW;
    float lw = uL1 - uL0;
    float barU[4] = {uL0 + lw * 0.27f, uL0 + lw * 0.73f, uR0 + lw * 0.27f, uR0 + lw * 0.73f};
    if (lod == CL_HERO) {
        const float dO = -0.07f, dI = -0.095f;
        ctrFrame(g, f, pt, CL_HERO, sill, hdr, k.lod[4] != CL_NONE);
        fpanel(g, f, uL1, uR0, sill, h - hdr, dI - 0.008f, dark, dark, mat);
        PCol cl[40], cr[40];
        int nl = corrColumns(cl, 40, uL0, uL1, lw / 4.f, dO, dI, true);
        int nr = corrColumns(cr, 40, uR0, uR1, lw / 4.f, dO, dI, true);
        corrStrip(g, f, cl, nl, sill, h - hdr, pt, mat);
        corrStrip(g, f, cr, nr, sill, h - hdr, pt, mat);
        // hinges on the outer edges
        for (int s = 0; s < 2; s++)
            for (int j = 0; j < 4; j++) {
                float v = sill + (h - hdr - sill) * (0.08f + 0.28f * j);
                float u0 = s ? uR1 - 0.07f : uL0 - 0.02f;
                fbox(g, f, u0, u0 + 0.09f, v - 0.05f, v + 0.05f, dO - 0.005f, dO + 0.04f, barC, mat, 1 | 2 | 4);
            }
        // locking bars with guides, cam keepers top and bottom, handles
        for (int j = 0; j < 4; j++) {
            float u = barU[j];
            fbox(g, f, u - 0.014f, u + 0.014f, 0.04f, h - 0.06f, dO, dO + 0.035f, barC, mat, 1 | 2 | 4);
            for (float v : {0.08f, h - hdr - 0.1f}) fbox(g, f, u - 0.045f, u + 0.045f, v, v + 0.11f, dO - 0.01f, dO + 0.05f, barC, mat, 1 | 8 | 16);
            float dir = (j & 1) ? -1.f : 1.f;
            float hu0 = dir > 0 ? u : u - 0.3f;
            fbox(g, f, hu0, hu0 + 0.3f, 1.08f, 1.12f, dO + 0.035f, dO + 0.06f, barC, mat, 1 | 8);
        }
        // CSC and customs plates on the left leaf, ID and type code on the right one
        u32 plateC = pkc(vec3(0.82f, 0.82f, 0.8f)), plateM = M(MAT_METAL_BRUSHED);
        fbox(g, f, uL0 + 0.36f, uL0 + 0.6f, 1.36f, 1.5f, dO, dO + 0.006f, plateC, plateM, 1);
        fbox(g, f, uL0 + 0.36f, uL0 + 0.52f, 1.22f, 1.31f, dO, dO + 0.006f, plateC, plateM, 1);
        float lum = dot(b, vec3(0.3f, 0.55f, 0.15f));
        u32 tc = lum > 0.5f ? pkc(vec3(0.06f)) : pkc(vec3(0.93f));
        char serial[16];
        snprintf(serial, sizeof(serial), "%06u %u", hash32(k.seed ^ 0x51Du) % 1000000u, hash32(k.seed ^ 0xC0Du) % 10u);
        const char* type = k.len > 9.f ? (k.hgt > 2.7f ? "45G1" : "42G1") : "22G1";
        if (k.reefer) type = "45R1";
        float tu = uR0 + 0.14f, tv = h - hdr - 0.2f;
        // (small lettering sits on the outer ribs: splitting it over every rib would cost more than it shows)
        paintText(g, f, nullptr, 0, dO, k.code, vec2(tu, tv), 0.12f, 0.02f, tc, mat, 0.3f);
        paintText(g, f, nullptr, 0, dO, serial, vec2(tu, tv - 0.19f), 0.12f, 0.02f, tc, mat, 0.3f);
        paintText(g, f, nullptr, 0, dO, type, vec2(tu, tv - 0.38f), 0.1f, 0.017f, tc, mat, 0.3f);
        // dangerous goods placard on some boxes
        if ((hash32(k.seed ^ 0xD6u) & 15u) == 0u) {
            vec2 cc(uR0 + lw * 0.5f, 0.9f);
            float s = 0.13f;
            vec2 top[3] = {cc + vec2(-s, 0), cc + vec2(s, 0), cc + vec2(0, s)};
            vec2 bot[3] = {cc + vec2(s, 0), cc + vec2(-s, 0), cc + vec2(0, -s)};
            paintPoly(g, f, nullptr, 0, dO, top, 3, pkc(vec3(0.85f, 0.12f, 0.08f)), mat);
            paintPoly(g, f, nullptr, 0, dO, bot, 3, pkc(vec3(0.92f)), mat);
        }
        return;
    }
    if (lod == CL_MID || lod == CL_FLATC) {
        if (lod == CL_MID) {
            ctrFrame(g, f, pt, CL_MID, sill, hdr, false);
            PCol cl[40], cr[40];
            int nl = corrColumns(cl, 40, uL0, uL1, lw / 4.f, kDFlat, kDFlat, false);
            int nr = corrColumns(cr, 40, uR0, uR1, lw / 4.f, kDFlat, kDFlat, false);
            corrStrip(g, f, cl, nl, sill, h - hdr, pt, mat);
            corrStrip(g, f, cr, nr, sill, h - hdr, pt, mat);
            fpanel(g, f, uL1 - 0.004f, uR0 + 0.004f, sill, h - hdr, kDFlat + 0.002f, dark, dark, mat);
        } else {
            fpanel(g, f, 0.f, w, 0.f, h, kDFlat, pkc(pt.at(w * 0.5f, false) * 0.8f), pkc(pt.at(w * 0.5f, true)), mat);
        }
        for (int j = 0; j < 4; j++) fpanel(g, f, barU[j] - 0.015f, barU[j] + 0.015f, 0.05f, h - 0.06f, kDFlat + 0.025f, barC, barC, mat);
        return;
    }
    fpanel(g, f, 0.f, w, 0.f, h, kDFlat, pkc(pt.at(w * 0.5f, false) * 0.72f), pkc(pt.at(w * 0.5f, true) * 0.9f), mat);
}

// Front (blind) end, or a reefer's machinery end
void ctrFrontEnd(G& g, const CFace& f, const Ctr& k, const CPaint& pt, int lod) {
    using namespace ctr;
    u32 mat = M(MAT_METAL_PAINTED);
    float w = f.w, h = f.h;
    if (lod == CL_HERO || lod == CL_MID) {
        bool hero = lod == CL_HERO;
        ctrFrame(g, f, pt, lod, kRailB, kRailT, hero && k.lod[4] != CL_NONE);
        if (k.reefer) {
            // refrigeration unit: grey casing, louvred condenser grille, fan ring, control box with a display
            u32 casing = pkc(vec3(0.62f, 0.63f, 0.64f)), grille = pkc(vec3(0.14f)), box = pkc(vec3(0.75f, 0.76f, 0.77f));
            float d0 = hero ? -0.09f : kDFlat;
            fpanel(g, f, kPostW, w - kPostW, kRailB, h - kRailT, d0, casing, casing, mat);
            fpanel(g, f, 0.35f, 1.35f, 0.5f, 1.6f, d0 + 0.004f, grille, grille, mat);
            if (hero)
                for (int j = 0; j < 7; j++) {
                    float v = 0.55f + j * 0.15f;
                    fbox(g, f, 0.35f, 1.35f, v, v + 0.04f, d0, d0 + 0.03f, casing, mat, 1 | 8 | 16);
                }
            fpanel(g, f, 1.5f, 2.05f, 1.1f, 1.75f, d0 + 0.004f, box, box, mat);
            fpanel(g, f, 1.62f, 1.93f, 1.45f, 1.62f, d0 + 0.008f, pkc(vec3(0.05f, 0.25f, 0.12f)), pkc(vec3(0.05f, 0.25f, 0.12f)), mat);
            fpanel(g, f, 0.45f, 1.9f, 1.95f, 2.25f, d0 + 0.004f, grille, grille, mat);
            return;
        }
        PCol cc[48];
        int n = corrColumns(cc, 48, kPostW, w - kPostW, (w - 2.f * kPostW) / 7.f, hero ? kDOut : kDFlat, kDIn, hero);
        corrStrip(g, f, cc, n, kRailB, h - kRailT, pt, mat);
        return;
    }
    if (lod == CL_FLATC) {
        fpanel(g, f, 0.f, w, 0.f, h, kDFlat, pkc(pt.at(w * 0.5f, false) * 0.8f), pkc(pt.at(w * 0.5f, true)), mat);
        return;
    }
    fpanel(g, f, 0.f, w, 0.f, h, kDFlat, pkc(pt.at(w * 0.5f, false) * 0.72f), pkc(pt.at(w * 0.5f, true) * 0.9f), mat);
}

CPaint ctrPaint(const Ctr& k, int face) {
    CPaint p;
    u32 h = hash2i((int)(k.seed & 0x7fffffffu), face + 17);
    float fade = Saturate(k.fade + (face == 4 ? 0.3f : 0.f) + (hashToFloat(h) - 0.5f) * 0.12f);
    p.base = bleachC(k.col, fade * 0.5f) * (1.f - fade * 0.08f);
    if (face < 2 && hashToFloat(hash32(h + 1)) < 0.2f) {
        // a repainted section (fresher paint over a repair)
        float a = hashToFloat(hash32(h + 2)), b = hashToFloat(hash32(h + 3));
        p.p0 = Lerp(0.4f, k.len * 0.6f, a);
        p.p1 = p.p0 + Lerp(0.8f, k.len * 0.35f, b);
        p.patch = k.col * Lerp(0.86f, 1.04f, hashToFloat(hash32(h + 4)));
    }
    p.bleach = 0.1f + fade * 0.3f;
    p.grime = lerp(vec3(1.f), vec3(0.6f, 0.57f, 0.5f), 0.3f + k.grime * 0.6f);
    p.streakP = k.rust * 0.3f;
    p.seed = hash32(h + 5);
    return p;
}

// Draws one container: every face at its own detail level (see CtrLod); far cells get one shaded quad per visible face
void drawContainer(G& g, const Ctr& k) {
    using namespace ctr;
    vec3 X(k.ax, 0.f), Y(perp(k.ax), 0.f), Z(0, 0, 1);
    float hx = k.len * 0.5f, hy = k.wid * 0.5f, H = k.hgt;
    vec3 b0 = k.base;
    u32 mat = M(MAT_METAL_PAINTED);
    // face frames: -Y, +Y, -X, +X (r = cross(up, n))
    CFace fs[4];
    fs[0] = {b0 - X * hx - Y * hy, X, -Y, k.len, H, 0.f};
    fs[1] = {b0 + X * hx + Y * hy, -X, Y, k.len, H, 30.f};
    fs[2] = {b0 - X * hx + Y * hy, -Y, -X, k.wid, H, 60.f};
    fs[3] = {b0 + X * hx - Y * hy, Y, X, k.wid, H, 70.f};
    if (!g.detail) {
        for (int fi = 0; fi < 4; fi++) {
            if (k.lod[fi] <= CL_FLAT) continue;
            CPaint pt = ctrPaint(k, fi);
            vec3 cb = pt.at(fs[fi].w * 0.5f, false) * 0.72f, ct = pt.at(fs[fi].w * 0.5f, true);
            fpanel(g, fs[fi], 0.f, fs[fi].w, 0.f, H, 0.f, pkc(cb), pkc(ct), mat);
        }
        if (k.lod[4]) {
            CPaint pt = ctrPaint(k, 4);
            u32 c = pkc(bleachC(pt.base, 0.25f) * 0.95f);
            quad(g, *g.m, b0 + Z * H - X * hx - Y * hy, b0 + Z * H + X * hx - Y * hy, b0 + Z * H + X * hx + Y * hy, b0 + Z * H - X * hx + Y * hy, c, mat, Z);
        }
        return;
    }
    for (int fi = 0; fi < 4; fi++) {
        int lod = k.lod[fi];
        if (!lod) continue;
        const CFace& f = fs[fi];
        CPaint pt = ctrPaint(k, fi);
        if (k.stacked && lod >= CL_FLATC) fpanel(g, f, 0.f, f.w, -kGap, 0.f, -0.05f, pkc(vec3(0.03f)), pkc(vec3(0.03f)), mat);
        if (fi >= 2) {
            bool door = (fi == 3) == k.doorsPlus;
            if (door) ctrDoorEnd(g, f, k, pt, lod);
            else ctrFrontEnd(g, f, k, pt, lod);
            continue;
        }
        if (lod == CL_HERO || lod == CL_MID) {
            bool hero = lod == CL_HERO;
            ctrFrame(g, f, pt, lod, kRailB, kRailT, hero && k.lod[4] != CL_NONE);
            PCol cc[200];
            int n = corrColumns(cc, 200, kPostW, f.w - kPostW, (!hero && k.coarse) ? kPitch * 2.f : kPitch, hero ? kDOut : kDFlat, kDIn, hero);
            corrStrip(g, f, cc, n, kRailB, H - kRailT, pt, mat);
            // line names and logos go on the flat mid panels (one quad per stroke); the ribbed eye-level faces stay plain
            if (!hero && (k.marks & (1u << fi))) ctrMarks(g, f, k, nullptr, 0, kDFlat, hash32(k.seed ^ (0xA5u + fi)));
        } else if (lod == CL_FLATC) {
            ctrFrame(g, f, pt, CL_FLATC, kRailB, kRailT, false);
            fpanel(g, f, 0.f, f.w, 0.f, H, kDFlat, pkc(pt.at(f.w * 0.5f, false) * 0.8f), pkc(pt.at(f.w * 0.5f, true)), mat);
            if (k.marks & (1u << fi)) ctrMarks(g, f, k, nullptr, 0, kDFlat, hash32(k.seed ^ (0xA5u + fi)));
        } else {
            fpanel(g, f, 0.f, f.w, 0.f, H, kDFlat, pkc(pt.at(f.w * 0.5f, false) * 0.72f), pkc(pt.at(f.w * 0.5f, true) * 0.9f), mat);
        }
    }
    if (k.lod[4]) {
        // roof: panels along the length (bleached, dirt and rust patches); hero faces add their casting tops
        CPaint pt = ctrPaint(k, 4);
        vec3 top = b0 + Z * (H - 0.006f);
        const int ns = 4;
        vec3 cols[ns + 1][2];
        for (int i = 0; i <= ns; i++)
            for (int s = 0; s < 2; s++) {
                u32 hh = hash3i((int)(k.seed & 0xffffu), i, s + 40);
                vec3 c = bleachC(pt.base, 0.2f + 0.2f * hashToFloat(hh));
                if (hashToFloat(hash32(hh)) < k.rust * 0.5f) c = lerp(c, kRust, 0.4f);
                else if (hashToFloat(hash32(hh + 1)) < 0.3f) c = c * vec3(0.8f, 0.78f, 0.72f);
                cols[i][s] = c;
            }
        MeshData& m = *g.m;
        u32 base = (u32)m.verts.size();
        for (int i = 0; i <= ns; i++)
            for (int s = 0; s < 2; s++) {
                float x = -hx + k.len * i / ns, y = s ? hy : -hy;
                m.addVertex(top + X * x + Y * y - g.org, Z, X, vec2(x, y), pkc(cols[i][s]), mat);
            }
        for (int i = 0; i < ns; i++) m.quadIdx(base + i * 2, base + i * 2 + 2, base + i * 2 + 3, base + i * 2 + 1);
    }
}

// Legacy entry (cranes, carriers, rail cars, ships): c = box centre; faces bitmask 1 -Y, 2 +Y, 4 -X, 8 +X, 16 roof
void container(G& g, vec3 c, vec2 ax, float len, float wid, float hgt, vec3 col, u32 faces, bool doorsAtPlusX, u8 sideLod = CL_MID,
               u8 endLod = CL_FLATC, u32 seed = 0) {
    Ctr k;
    k.base = c - vec3(0, 0, hgt * 0.5f);
    k.ax = normalize(ax);
    k.len = len > 9.f ? 12.192f : 6.058f;
    k.wid = 2.438f;
    k.hgt = hgt;
    k.col = col;
    k.doorsPlus = doorsAtPlusX;
    k.seed = seed ? seed : hash3i((int)(c.x * 4.f), (int)(c.y * 4.f), (int)(c.z * 4.f));
    k.fade = hashToFloat(hash32(k.seed + 1)) * 0.4f;
    k.rust = hashToFloat(hash32(k.seed + 2)) * 0.7f;
    k.grime = hashToFloat(hash32(k.seed + 3));
    for (int i = 0; i < 5; i++) k.lod[i] = (faces & (1u << i)) ? (i < 2 ? sideLod : (i < 4 ? endLod : (u8)1)) : (u8)CL_NONE;
    for (int i = 0; i < kLineCount; i++)
        if (kLines[i].color == col) {
            k.line = i;
            k.code = kLines[i].code;
        }
    drawContainer(g, k);
}

// ------------------------------------------------------------------------------------------------ quay walls
void genQuay(const SiteElem& e, G& g) {
    vec2 a = e.a, b = e.b;
    vec2 mn, mx;
    cellBounds(g, mn, mx);
    if (!clipSegment(a, b, mn, mx)) return;
    vec2 d = normalize(e.b - e.a), on(e.p[0], e.p[1]);
    float z = e.z;
    float L = length(b - a);
    if (L < 0.5f) return;
    // wall face: dry concrete, a dark tidal band and weed-covered concrete below the waterline (reads dark through the water),
    // capping beam, safety line
    quad(g, *g.m, vec3(a, z), vec3(b, z), vec3(b, 0.9f), vec3(a, 0.9f), rgb(0.62f, 0.6f, 0.56f), M(MAT_CONCRETE), vec3(on, 0));
    quad(g, *g.m, vec3(a, 0.9f), vec3(b, 0.9f), vec3(b, -0.4f), vec3(a, -0.4f), rgb(0.27f, 0.26f, 0.21f), M(MAT_CONCRETE), vec3(on, 0));
    quad(g, *g.m, vec3(a, -0.4f), vec3(b, -0.4f), vec3(b, -10.f), vec3(a, -10.f), rgb(0.06f, 0.08f, 0.065f), M(MAT_CONCRETE), vec3(on, 0));
    quad(g, *g.m, vec3(a, z + 0.35f), vec3(b, z + 0.35f), vec3(b, z), vec3(a, z), rgb(0.85f, 0.84f, 0.8f), M(MAT_CONCRETE), vec3(on, 0));
    quad(g, *g.m, vec3(a - on * 0.8f, z + 0.35f), vec3(b - on * 0.8f, z + 0.35f), vec3(b, z + 0.35f), vec3(a, z + 0.35f), rgb(0.85f, 0.84f, 0.8f), M(MAT_CONCRETE),
         vec3(0, 0, 1));
    quad(g, *g.m, vec3(a - on * 0.8f, z), vec3(b - on * 0.8f, z), vec3(b - on * 0.8f, z + 0.35f), vec3(a - on * 0.8f, z + 0.35f), rgb(0.85f, 0.84f, 0.8f),
         M(MAT_CONCRETE), vec3(-on, 0));
    if (g.detail) {
        paintLine(g, a - on * 1.4f, b - on * 1.4f, 0.25f, z + 0.012f, rgb(1.f, 0.85f, 0.1f), M(MAT_PAINT_YELLOW));
        // bollards and (on berths) fenders every 18 m, placed on a global grid so neighbouring cells line up
        float s0 = ceilf(dot(a - e.a, d) / 18.f) * 18.f, s1 = dot(b - e.a, d);
        for (float s = s0; s <= s1; s += 18.f) {
            vec2 p = e.a + d * s - on * 0.45f;
            cyl(g, vec3(p, z + 0.35f), 0.28f, 0.22f, 0.6f, 8, rgb(0.15f, 0.15f, 0.16f), M(MAT_METAL_PAINTED), false);
            cyl(g, vec3(p, z + 0.95f), 0.36f, 0.36f, 0.12f, 8, rgb(0.15f, 0.15f, 0.16f), M(MAT_METAL_PAINTED), true);
            if (e.variant == 1) {
                vec2 fp = e.a + d * (s + 9.f) + on * 0.9f;
                if (dot(fp - e.a, d) <= s1) {
                    boxY(g, vec3(fp, z - 1.5f), d, vec3(1.1f, 0.9f, 2.4f), rgb(0.08f, 0.08f, 0.08f), M(MAT_RUBBER));
                }
            }
        }
        collide(g, vec3((a + b) * 0.5f - on * 0.4f, z + 0.17f), d, vec3(L * 0.5f, 0.4f, 0.17f));
    }
    if (e.variant == 1) {
        // crane rails on concrete runway beams (landside 45 m, seaside 15 m from the quay edge)
        for (float off : {15.f, 45.f}) {
            vec2 ra = a - on * off, rb = b - on * off;
            paintLine(g, ra, rb, 1.4f, z + 0.01f, rgb(0.7f, 0.68f, 0.64f), M(MAT_CONCRETE));
            beam(g, vec3(ra, z + 0.07f), vec3(rb, z + 0.07f), 0.16f, 0.14f, rgb(0.4f, 0.38f, 0.36f), M(MAT_METAL_BRUSHED));
        }
    }
}

// ------------------------------------------------------------------------------------------------ ship-to-shore crane
void genStsCrane(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool detail = g.detail;
    vec2 X2 = e.ax, Y2 = perp(e.ax);
    vec3 X(X2, 0), Y(Y2, 0), Z(0, 0, 1);
    vec3 o(e.c, e.z);
    auto P = [&](float x, float y, float z) { return o + X * x + Y * y + Z * z; };
    u32 red = e.variant == 0 ? rgb(0.78f, 0.12f, 0.08f) : rgb(0.93f, 0.93f, 0.92f);
    u32 white = e.variant == 0 ? rgb(0.93f, 0.93f, 0.92f) : rgb(0.78f, 0.12f, 0.08f);
    u32 steel = M(MAT_METAL_PAINTED);
    const float gauge = 15.f, legY = 9.f, portalZ = 46.f, girderZ = 49.f;
    // legs (tapering slightly) + sill beams + bogies
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            beam(g, P(sx * gauge, sy * legY, 2.2f), P(sx * gauge, sy * legY * 0.92f, portalZ), 1.7f, 1.7f, red, steel, X);
            boxY(g, P(sx * gauge, sy * legY, 1.1f), Y2, vec3(3.2f, 1.f, 0.9f), rgb(0.25f), steel);
            if (detail)
                for (int w = -1; w <= 1; w += 2) {
                    vec3 wc = P(sx * gauge, sy * legY + w * 2.2f, 0.45f);
                    rod(g, wc - X * 0.3f, wc + X * 0.3f, 0.42f, 8, rgb(0.15f), M(MAT_METAL_BRUSHED));
                }
            collide(g, P(sx * gauge, sy * legY, portalZ * 0.5f), X2, vec3(1.f, 1.f, portalZ * 0.5f));
        }
    for (int sx = -1; sx <= 1; sx += 2) {
        beam(g, P(sx * gauge, -legY, 12.f), P(sx * gauge, legY, 12.f), 1.2f, 1.6f, red, steel);   // sill / portal tie
        beam(g, P(sx * gauge, -legY, portalZ), P(sx * gauge, legY, portalZ), 1.6f, 2.2f, red, steel);
        if (detail) {
            beam(g, P(sx * gauge, -legY, 12.f), P(sx * gauge, legY * 0.92f, portalZ), 0.6f, 0.6f, red, steel);
            beam(g, P(sx * gauge, legY, 12.f), P(sx * gauge, -legY * 0.92f, portalZ), 0.6f, 0.6f, red, steel);
        }
    }
    for (int sy = -1; sy <= 1; sy += 2) beam(g, P(-gauge, sy * legY * 0.92f, portalZ + 0.8f), P(gauge, sy * legY * 0.92f, portalZ + 0.8f), 1.4f, 2.4f, red, steel);
    // fixed girder over the land side (backreach) with the machinery house
    const float backX = -38.f, outX = 80.f, hingeX = gauge + 1.f;
    for (int sy = -1; sy <= 1; sy += 2) beam(g, P(backX, sy * 3.2f, girderZ), P(hingeX, sy * 3.2f, girderZ), 1.3f, 3.2f, white, steel);
    boxY(g, P(-31.f, 0.f, girderZ + 4.2f), X2, vec3(6.5f, 5.f, 3.f), white, steel, true);
    if (detail) {
        float th = 1.6f;
        const char* t = "PORT ISLE";
        float tw = textAdvance(t, th, 0.3f);
        strokeText(g, *g.m, t, P(-31.f - tw * 0.5f, -5.05f, girderZ + 3.3f), X, Z, th, 0.25f, rgb(0.1f, 0.2f, 0.45f), steel, 0.f, 0.3f);
    }
    // boom: lowered (horizontal over the ship) or raised (about the hinge)
    bool raised = e.p[0] > 0.5f;
    float ang = raised ? 78.f * kDegToRad : 0.f;
    vec3 hinge = P(hingeX, 0.f, girderZ);
    vec3 bdir = X * cosf(ang) + Z * sinf(ang);
    vec3 bup = -X * sinf(ang) + Z * cosf(ang);
    float boomLen = outX - hingeX;
    for (int sy = -1; sy <= 1; sy += 2) {
        vec3 a = hinge + Y * (sy * 3.2f), b = a + bdir * boomLen;
        beam(g, a, b, 1.2f, 2.8f, white, steel, bup);
        if (detail)
            for (float s = 6.f; s < boomLen; s += 8.f)
                beam(g, hinge + bdir * s + Y * (sy * 3.2f) - bup * 1.4f, hinge + bdir * (s + 4.f) + Y * (sy * 3.2f) + bup * 1.4f, 0.25f, 0.25f, white, steel, Y);
    }
    if (detail)
        for (float s = 4.f; s < boomLen; s += 12.f) beam(g, hinge + bdir * s - Y * 3.2f, hinge + bdir * s + Y * 3.2f, 0.4f, 0.4f, white, steel);
    // A-frame apex with fore- and backstays
    vec3 apex = P(-4.f, 0.f, 80.f);
    for (int sy = -1; sy <= 1; sy += 2) {
        beam(g, P(-gauge, sy * 4.f, portalZ + 1.f), apex + Y * (sy * 1.2f), 1.3f, 1.3f, red, steel, X);
        beam(g, P(hingeX - 2.f, sy * 4.f, portalZ + 1.f), apex + Y * (sy * 1.2f), 1.3f, 1.3f, red, steel, X);
        vec3 fs = hinge + bdir * (boomLen * 0.62f) + Y * (sy * 3.2f) + bup * 1.4f;
        rod(g, apex + Y * (sy * 1.2f), fs, 0.18f, 6, rgb(0.3f), M(MAT_METAL_BRUSHED));
        rod(g, apex + Y * (sy * 1.2f), P(backX + 2.f, sy * 3.2f, girderZ + 1.6f), 0.18f, 6, rgb(0.3f), M(MAT_METAL_BRUSHED));
    }
    beam(g, apex - Y * 1.8f, apex + Y * 1.8f, 1.4f, 1.4f, red, steel);
    // trolley + operator cab + spreader (only when the boom is down)
    if (!raised) {
        float tx = Lerp(-20.f, outX - 6.f, Clamp(e.p[1], 0.f, 1.f));
        vec3 tp = P(tx, 0.f, girderZ - 1.9f);
        boxY(g, tp, X2, vec3(3.5f, 3.9f, 1.3f), rgb(0.95f, 0.8f, 0.1f), steel, true);
        boxY(g, tp + X * 1.5f - Z * 2.6f, X2, vec3(1.4f, 1.4f, 1.3f), rgb(0.9f), steel, true);
        quad(g, *g.m, tp + X * 2.92f - Z * 3.7f - Y * 1.3f, tp + X * 2.92f - Z * 3.7f + Y * 1.3f, tp + X * 2.92f - Z * 1.5f + Y * 1.3f, tp + X * 2.92f - Z * 1.5f - Y * 1.3f,
             rgb(0.2f, 0.3f, 0.35f), M(MAT_GLASS), X);
        float sz = tx > gauge + 3.f ? 22.f : 14.f;  // spreader above the ship deck or over the apron
        vec3 sp = P(tx, 0.f, sz);
        if (detail)
            for (int k = 0; k < 4; k++) {
                vec3 off = X * ((k & 1) ? 1.5f : -1.5f) + Y * ((k & 2) ? 1.f : -1.f);
                rod(g, tp - Z * 1.3f + off, sp + off + Z * 0.5f, 0.05f, 4, rgb(0.2f), M(MAT_METAL_BRUSHED));
            }
        boxY(g, sp, Y2, vec3(6.2f, 1.25f, 0.35f), rgb(0.95f, 0.75f, 0.1f), steel, true);
        if (e.p[2] > 0.5f) {
            const Line& ln = kLines[e.seed % kLineCount];
            container(g, sp - Z * 1.65f, Y2, 12.19f, 2.44f, 2.59f, ln.color, 31u, true);
        }
    }
    // Warning / aviation lights and apron floodlights under the girder
    vec3 tip = hinge + bdir * boomLen + Z * 1.6f;
    lamp(g, tip, 0.6f, vec3(1.f, 0.06f, 0.03f), 1.f, EA_BLINK, e.seed & 255u);
    lamp(g, apex + Z * 1.2f, 0.6f, vec3(1.f, 0.06f, 0.03f), 1.f, EA_BLINK, (e.seed + 60u) & 255u);
    lamp(g, P(backX, 0.f, girderZ + 2.f), 0.5f, vec3(1.f, 0.06f, 0.03f), 0.9f);
    light(g, apex + Z * 1.2f, vec3(1.f, 0.08f, 0.04f) * 900.f, 18.f, 4);
    for (int k = 0; k < 3; k++) {
        vec3 lp = P(-20.f + k * 22.f, 0.f, girderZ - 2.2f);
        light(g, lp, vec3(1.f, 0.9f, 0.75f) * 22000.f, 70.f, 1, vec3(0, 0, -1), 0.35f);
        if (detail) lamp(g, lp, 0.7f, vec3(1.f, 0.92f, 0.8f), 0.6f, EA_NIGHT);
    }
}

// ------------------------------------------------------------------------------------------------ container stacks
// Stack height per stack by block variant: 0 import / export mix, 1 reefers (low), 2 empties (tall, by line), 3 sparse
int stackHeight(u32 h, int variant) {
    float r = hashToFloat(h);
    switch (variant) {
        case 1: return r < 0.12f ? 0 : (r < 0.42f ? 1 : (r < 0.8f ? 2 : 3));
        case 2: return r < 0.08f ? 0 : (r < 0.2f ? 2 : (r < 0.42f ? 3 : (r < 0.72f ? 4 : (r < 0.9f ? 5 : 6))));
        case 3: return r < 0.5f ? 0 : (r < 0.8f ? 1 : 2);
        default: return r < 0.08f ? 0 : (r < 0.25f ? 1 : (r < 0.52f ? 2 : (r < 0.82f ? 3 : 4)));
    }
}

// A straddle-carrier block: rows of ground slots 12.4 m apart along the row, rows 4.3 m apart (1.86 m legs lanes between
// them). Each slot holds one 40 ft stack or two 20 ft stacks of their own heights, boxes 2.5 cm apart on twistlocks and a
// few centimetres out of line. Faces get their detail from what they look onto (see CtrLod).
void genContainerBlock(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    using namespace ctr;
    int bays = (int)e.p[0], rows = (int)e.p[1];
    vec2 X = e.ax, Y = perp(e.ax);
    const float pitchX = 12.4f, pitchY = 4.3f, L40 = 12.192f, L20 = 6.058f, CW = 2.438f;
    vec2 origin = e.c - X * (bays * pitchX * 0.5f) - Y * (rows * pitchY * 0.5f);
    const int kMaxT = 7;
    const int ns = bays * 2;  // 20 ft sub-slots along a row
    std::vector<u8> cnt((size_t)ns * rows, 0), is40((size_t)bays * rows, 1);
    std::vector<float> tz((size_t)ns * rows * (kMaxT + 1), e.z);  // tier floor heights; index kMaxT = stack top
    const float p20 = e.variant == 1 ? 0.f : (e.variant == 3 ? 0.3f : 0.22f);
    for (int j = 0; j < rows; j++)
        for (int i = 0; i < bays; i++) {
            u32 hs = hash3i(i, j, (int)e.seed);
            bool outer = j == 0 || j == rows - 1;
            bool pair = hashToFloat(hash32(hs ^ 0x2020u)) < p20;
            is40[(size_t)j * bays + i] = pair ? 0 : 1;
            for (int s = 0; s < 2; s++) {
                if (!pair && s == 1) {
                    cnt[(size_t)j * ns + i * 2 + 1] = cnt[(size_t)j * ns + i * 2];
                    continue;
                }
                int h = stackHeight(pair ? hash3i(i * 2 + s, j, (int)e.seed ^ 0x51) : hs, e.variant);
                if (outer && hashToFloat(hash32(hs ^ (0x77u + (u32)s))) < 0.1f) h = 0;  // gaps along the lanes
                cnt[(size_t)j * ns + i * 2 + s] = (u8)Min(h, kMaxT);
            }
        }
    auto tierH = [&](int s, int j, int t, bool big) {
        u32 h = hash3i((big ? s / 2 : s) * 8 + t, j, (int)e.seed ^ 0x4C);
        float hc = big ? (e.variant == 1 ? 0.9f : 0.55f) : 0.1f;
        return hashToFloat(h) < hc ? 2.896f : 2.591f;
    };
    auto T = [&](int s, int j, int t) -> float& { return tz[((size_t)j * ns + s) * (kMaxT + 1) + t]; };
    for (int j = 0; j < rows; j++)
        for (int s = 0; s < ns; s++) {
            bool big = is40[(size_t)j * bays + s / 2] != 0;
            int n = cnt[(size_t)j * ns + s];
            float z = e.z;
            for (int t = 0; t < n; t++) {
                T(s, j, t) = z;
                z += tierH(s, j, t, big) + kGap;
            }
            T(s, j, kMaxT) = n ? z - kGap : e.z;
        }
    auto topAt = [&](int s, int j) { return T(s, j, kMaxT); };
    auto sx0 = [&](int s) { return (s / 2) * pitchX + ((s & 1) ? L20 + 0.076f : 0.f); };
    auto sx1 = [&](int s) { return (s / 2) * pitchX + ((s & 1) ? L40 : L20); };
    // which line a box belongs to: reefers and mixed blocks random, empties grouped per row
    auto boxLine = [&](int s, int j, int t, int& line, int& lease) {
        u32 h = hash3i(s + t * 61, j, (int)e.seed ^ 0x77);
        line = -1;
        lease = -1;
        if (e.variant == 2 && hashToFloat(hash32(h)) < 0.78f) {
            line = (int)((hash2i(j, (int)e.seed) >> 4) % kLineCount);
            return;
        }
        if (e.variant != 1 && hashToFloat(hash32(h + 9)) < 0.24f) lease = (int)(hash32(h + 3) % (u32)kLeaseCount);
        else line = (int)(h % (u32)kLineCount);
    };
    for (int j = 0; j < rows; j++)
        for (int i = 0; i < bays; i++) {
            bool big = is40[(size_t)j * bays + i] != 0;
            for (int s = i * 2; s < i * 2 + (big ? 1 : 2); s++) {
                int n = cnt[(size_t)j * ns + s];
                float len = big ? L40 : L20;
                int sEnd = big ? s + 1 : s;  // last sub-slot covered
                for (int t = 0; t < n; t++) {
                    Ctr k;
                    float zb = T(s, j, t), H = tierH(s, j, t, big), zt = zb + H;
                    u32 hc = hash3i(s * 8 + t, j, (int)e.seed ^ 0x3D);
                    float jx = (hashToFloat(hc) - 0.5f) * 0.04f, jy = (hashToFloat(hash32(hc)) - 0.5f) * 0.06f;
                    vec2 c2 = origin + X * (sx0(s) + len * 0.5f + jx) + Y * (j * pitchY + CW * 0.5f + 0.9f + jy);
                    k.base = vec3(c2, zb);
                    k.ax = X;
                    k.len = len;
                    k.wid = CW;
                    k.hgt = H;
                    k.seed = hash3i(s * 16 + t, j, (int)e.seed ^ 0x5EED);
                    k.stacked = t > 0;
                    k.reefer = e.variant == 1;
                    k.doorsPlus = (hash32(k.seed) & 1u) != 0;
                    int line, lease;
                    boxLine(s, j, t, line, lease);
                    k.line = line;
                    if (line >= 0) {
                        k.col = e.variant == 1 ? vec3(0.93f, 0.93f, 0.91f) : kLines[line].color;
                        k.code = kLines[line].code;
                    } else {
                        k.col = kLeaseCol[lease];
                        k.code = kLeaseCode[lease];
                    }
                    k.col = k.col * Lerp(0.9f, 1.06f, hashToFloat(hash32(k.seed + 11)));
                    k.fade = hashToFloat(hash32(k.seed + 12)) * 0.45f + (t >= 3 ? 0.08f : 0.f);
                    float rr = hashToFloat(hash32(k.seed + 13));
                    k.rust = rr * rr * 0.95f;
                    k.grime = hashToFloat(hash32(k.seed + 14));
                    // long faces: the lanes outside the block, the alleys between rows, or above a lower row
                    for (int side = 0; side < 2; side++) {
                        int jn = side == 0 ? j - 1 : j + 1;
                        u8 lod;
                        if (jn < 0 || jn >= rows) {
                            lod = t == 0 ? CL_HERO : CL_MID;
                            if (line >= 0 && t >= 1 && t <= 2) k.marks |= (u8)(1u << side);
                        } else {
                            float nt = topAt(s, jn);
                            for (int q = s + 1; q <= sEnd; q++) nt = Min(nt, topAt(q, jn));
                            lod = nt >= zt - 1.f ? CL_FLAT : CL_FLATC;
                        }
                        k.lod[side] = lod;
                    }
                    // ends: the block ends (boulevard / quay road), the gaps between stacks, or above a lower neighbour
                    for (int en = 0; en < 2; en++) {
                        int sn = en == 0 ? s - 1 : sEnd + 1;
                        u8 lod;
                        if (sn < 0 || sn >= ns) lod = t == 0 ? CL_HERO : CL_MID;
                        else lod = topAt(sn, j) >= zt - 1.f ? CL_FLAT : CL_FLATC;
                        k.lod[2 + en] = lod;
                    }
                    k.lod[4] = t == n - 1 ? 1 : 0;
                    k.coarse = t >= 2;
                    drawContainer(g, k);
                }
            }
        }
    // Reefer power racks along reefer blocks
    if (e.variant == 1) {
        for (int j = 0; j <= rows; j += 2) {
            vec2 a = origin + Y * (j * pitchY + 0.4f), b = a + X * (bays * pitchX);
            beam(g, vec3(a, e.z + 3.2f), vec3(b, e.z + 3.2f), 0.4f, 0.6f, rgb(0.8f, 0.75f, 0.2f), M(MAT_METAL_PAINTED));
            for (int i = 0; i <= bays; i += 4) {
                vec2 p = a + X * (i * pitchX);
                boxY(g, vec3(p, e.z + 1.6f), X, vec3(0.15f, 0.15f, 1.6f), rgb(0.5f), M(MAT_METAL_PAINTED));
            }
        }
    }
    // collision: one box per run of equal stack tops along each row (stack tops are walkable)
    for (int j = 0; j < rows; j++) {
        int s = 0;
        while (s < ns) {
            float top = topAt(s, j);
            int k2 = s;
            while (k2 + 1 < ns && fabsf(topAt(k2 + 1, j) - top) < 0.01f) k2++;
            if (top > e.z + 0.5f) {
                float x0 = sx0(s), x1 = sx1(k2);
                vec2 c = origin + X * ((x0 + x1) * 0.5f) + Y * (j * pitchY + CW * 0.5f + 0.9f);
                collide(g, vec3(c, (e.z + top) * 0.5f), X, vec3((x1 - x0) * 0.5f, CW * 0.5f, (top - e.z) * 0.5f));
            }
            s = k2 + 1;
        }
    }
}

// ------------------------------------------------------------------------------------------------ straddle carrier
void genStraddle(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 X = e.ax, Y = perp(e.ax);
    vec3 o(e.c, e.z);
    u32 frame = rgb(0.92f, 0.92f, 0.9f), acc = rgb(0.95f, 0.5f, 0.08f);
    float L = 4.6f, W = 2.3f, Hh = 14.5f;
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) {
            vec3 base = o + vec3(X * (sx * L) + Y * (sy * W), 0);
            beam(g, base + vec3(0, 0, 1.2f), base + vec3(0, 0, Hh - 1.f), 0.5f, 0.5f, frame, M(MAT_METAL_PAINTED));
            if (g.detail) rod(g, base + vec3(0, 0, 0.7f) - vec3(Y * 0.3f, 0), base + vec3(0, 0, 0.7f) + vec3(Y * 0.3f, 0), 0.7f, 10, rgb(0.06f), M(MAT_RUBBER));
        }
    for (int sy = -1; sy <= 1; sy += 2) {
        beam(g, o + vec3(-X * L + Y * (sy * W), 1.4f), o + vec3(X * L + Y * (sy * W), 1.4f), 0.5f, 0.9f, acc, M(MAT_METAL_PAINTED));
        beam(g, o + vec3(-X * (L + 0.4f) + Y * (sy * W), Hh - 0.6f), o + vec3(X * (L + 0.4f) + Y * (sy * W), Hh - 0.6f), 0.8f, 1.4f, frame, M(MAT_METAL_PAINTED));
    }
    beam(g, o + vec3(-X * L, Hh - 0.4f), o + vec3(X * L, Hh - 0.4f), 2.f * W + 0.6f, 0.6f, frame, M(MAT_METAL_PAINTED));
    // cab on top at the front, engine housing at the back
    boxY(g, o + vec3(X * (L - 0.8f) + Y * (W * 0.55f), Hh + 1.f), X, vec3(0.9f, 0.9f, 1.f), rgb(0.95f), M(MAT_METAL_PAINTED));
    quad(g, *g.m, o + vec3(X * (L + 0.11f) + Y * (W * 0.55f - 0.8f), Hh + 0.4f), o + vec3(X * (L + 0.11f) + Y * (W * 0.55f + 0.8f), Hh + 0.4f),
         o + vec3(X * (L + 0.11f) + Y * (W * 0.55f + 0.8f), Hh + 1.7f), o + vec3(X * (L + 0.11f) + Y * (W * 0.55f - 0.8f), Hh + 1.7f), rgb(0.15f, 0.2f, 0.25f),
         M(MAT_GLASS), vec3(X, 0));
    boxY(g, o + vec3(-X * (L - 1.2f), Hh + 0.6f), X, vec3(1.2f, W * 0.8f, 0.6f), acc, M(MAT_METAL_PAINTED));
    lamp(g, o + vec3(X * (L - 0.8f) + Y * (W * 0.55f), Hh + 2.2f), 0.3f, vec3(1.f, 0.6f, 0.05f), 0.9f, EA_BLINK, e.seed & 255u);
    if (e.p[0] > 0.5f) container(g, o + vec3(0, 0, 4.2f), Y, 12.19f, 2.44f, 2.59f, kLines[e.seed % kLineCount].color, 31u, true, CL_HERO, CL_MID, e.seed);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sy = -1; sy <= 1; sy += 2) collide(g, o + vec3(X * (sx * L) + Y * (sy * W), Hh * 0.5f), X, vec3(0.4f, 0.4f, Hh * 0.5f));
}

// ------------------------------------------------------------------------------------------------ container ship
void genShip(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    bool big = e.variant == 0;
    bool detail = g.detail;
    vec2 F = e.ax, S = perp(e.ax);  // F bow direction, S port side
    float half = e.hx, beam0 = e.hy;
    float draft = big ? 12.f : 8.f, freeboard = big ? 13.5f : 9.5f;
    vec3 hullC = big ? vec3(0.08f, 0.14f, 0.3f) : vec3(0.12f, 0.35f, 0.2f);
    vec3 red(0.55f, 0.1f, 0.08f), boot(0.06f, 0.06f, 0.06f);
    // hull loft: stations along the length (x from stern -half to bow +half), half-width at waterline / deck
    struct St { float x, wl, dk; };
    std::vector<St> st;
    int nst = detail ? 18 : 9;
    for (int i = 0; i <= nst; i++) {
        float t = (float)i / nst;
        float x = Lerp(-half, half, t);
        float wl = beam0, dk = beam0;
        float bowStart = half - (big ? 55.f : 38.f);
        if (x > bowStart) {
            float u = (x - bowStart) / (half - bowStart);
            wl = beam0 * sqrtf(Max(0.f, 1.f - u * u)) * (1.f - 0.15f * u);
            dk = beam0 * sqrtf(Max(0.f, 1.f - powf(u, 3.f)));
        }
        float sternStart = -half + (big ? 30.f : 20.f);
        if (x < sternStart) {
            float u = (sternStart - x) / (big ? 30.f : 20.f);
            wl = beam0 * (1.f - 0.55f * u * u);
            dk = beam0 * (1.f - 0.12f * u);
        }
        st.push_back({x, Max(wl, 0.3f), Max(dk, 0.8f)});
    }
    MeshData& m = *g.m;
    vec3 o(e.c, 0.f);
    auto P = [&](float x, float y, float z) { return o + vec3(F * x + S * y, z); };
    // side profile points per station (bottom center -> bilge -> waterline -> boot top -> deck edge)
    const int np = 6;
    for (int side = -1; side <= 1; side += 2) {
        u32 b0 = (u32)m.verts.size();
        for (size_t i = 0; i < st.size(); i++) {
            float w = st[i].wl, d = st[i].dk;
            float yb = Max(0.f, w - 3.f);
            float pz[np] = {-draft, -draft, -draft + 2.5f, 0.f, 2.f, freeboard};
            float py[np] = {0.f, yb, w, w, Lerp(w, d, 0.15f), d};
            vec3 cols[np] = {red, red, red, boot, hullC, hullC};
            for (int k = 0; k < np; k++) {
                vec3 p = P(st[i].x, side * py[k], pz[k]);
                vec3 n = normalize(vec3(S * (float)side, k < 2 ? -1.f : 0.f));
                m.addVertex(p - g.org, n, vec3(F, 0), vec2(st[i].x, pz[k]), rgbv(cols[k]), M(MAT_METAL_PAINTED));
            }
        }
        for (size_t i = 0; i + 1 < st.size(); i++)
            for (int k = 0; k + 1 < np; k++) {
                u32 a = b0 + (u32)(i * np + k), b = a + np, c = b + 1, d = a + 1;
                vec3 fn = cross(m.verts[b].pos - m.verts[a].pos, m.verts[d].pos - m.verts[a].pos);
                vec3 want(S * (float)side, 0);
                if (dot(fn, want) >= 0) m.quadIdx(a, b, c, d);
                else m.quadIdx(a, d, c, b);
            }
    }
    // transom at the stern and a stem strip at the bow
    {
        float w = st[0].dk, wl = st[0].wl;
        std::vector<vec3> tr = {P(-half, -wl, -draft + 2.5f), P(-half, wl, -draft + 2.5f), P(-half, w, freeboard), P(-half, -w, freeboard)};
        quad(g, m, tr[0], tr[1], tr[2], tr[3], rgbv(hullC), M(MAT_METAL_PAINTED), vec3(-F, 0));
    }
    // deck
    {
        std::vector<vec2> deck;
        for (size_t i = 0; i < st.size(); i++) deck.push_back(e.c + F * st[i].x - S * st[i].dk);
        for (size_t i = st.size(); i-- > 0;) deck.push_back(e.c + F * st[i].x + S * st[i].dk);
        polyFlat(g, m, deck, freeboard, rgb(0.45f, 0.5f, 0.45f), M(MAT_METAL_PAINTED));
    }
    // name on the bow (both sides) and stern
    if (detail) {
        float th = big ? 2.4f : 1.8f;
        float tw = textAdvance(e.text.c_str(), th, 0.3f);
        for (int side = -1; side <= 1; side += 2) {
            float x = half - (big ? 30.f : 22.f);
            float y = side * (st[st.size() - 3].dk + 0.05f);
            vec3 out(S * (float)side, 0);
            vec3 rt(perp(S * (float)side), 0);  // viewer's right when facing this side of the hull
            vec3 org = side > 0 ? P(x, y, freeboard - th - 1.2f) : P(x - tw, y, freeboard - th - 1.2f);
            strokeText(g, m, e.text.c_str(), org + out * 0.03f, rt, vec3(0, 0, 1), th, th * 0.13f, rgb(0.95f, 0.95f, 0.95f), M(MAT_METAL_PAINTED), 0.f, 0.3f);
        }
        const char* home = "PORTO SOL";
        float tw2 = textAdvance(home, th * 0.8f, 0.3f);
        vec2 sr = perp(-F);
        vec3 so = P(-half - 0.05f, 0.f, freeboard - th * 2.2f) - vec3(sr * (tw2 * 0.5f), 0);
        strokeText(g, m, home, so, vec3(sr, 0), vec3(0, 0, 1), th * 0.8f, th * 0.1f, rgb(0.95f), M(MAT_METAL_PAINTED), 0.f, 0.3f);
    }
    // superstructure (accommodation + bridge) near the stern, funnel behind it
    float supX = -half + (big ? 52.f : 30.f);
    float supL = big ? 14.f : 11.f, supW = big ? 16.f : 11.f, supH = big ? 24.f : 17.f;
    vec3 supBase = P(supX, 0.f, freeboard);
    boxY(g, supBase + vec3(0, 0, supH * 0.5f), F, vec3(supL * 0.5f, supW, supH * 0.5f), rgb(0.95f, 0.95f, 0.94f), M(MAT_METAL_PAINTED));
    int decks = (int)(supH / 2.9f);
    for (int k = 0; k < decks; k++) {
        float z = freeboard + 1.2f + k * 2.9f;
        for (int side = -1; side <= 1; side += 2) {
            vec3 out(S * (float)side, 0);
            vec3 c = supBase + out * (supW + 0.03f) + vec3(0, 0, z - freeboard + 0.6f);
            quad(g, m, c - vec3(F * (supL * 0.45f), 0), c + vec3(F * (supL * 0.45f), 0), c + vec3(F * (supL * 0.45f), 0.9f), c - vec3(F * (supL * 0.45f), 0.9f),
                 rgb(0.1f, 0.12f, 0.15f), M(MAT_GLASS), out);
            if ((k * 7 + side) % 3 != 0)
                quad(g, m, c - vec3(F * (supL * 0.3f), 0) + out * 0.02f, c + vec3(F * (supL * 0.1f), 0) + out * 0.02f, c + vec3(F * (supL * 0.1f), 0.9f) + out * 0.02f,
                     c - vec3(F * (supL * 0.3f), 0.9f) + out * 0.02f, rgb(1.f, 0.85f, 0.6f, 0.08f), emMat(EA_NIGHT), out);
        }
        vec3 fc = supBase + vec3(F * (supL * 0.5f + 0.03f), z - freeboard + 0.6f);
        quad(g, m, fc - vec3(S * (supW * 0.9f), 0), fc + vec3(S * (supW * 0.9f), 0), fc + vec3(S * (supW * 0.9f), 0.9f), fc - vec3(S * (supW * 0.9f), 0.9f),
             rgb(0.1f, 0.12f, 0.15f), M(MAT_GLASS), vec3(F, 0));
    }
    // bridge deck with wings to the full beam
    vec3 br = supBase + vec3(0, 0, supH + 1.5f);
    boxY(g, br, F, vec3(supL * 0.5f + 1.f, beam0 + 0.5f, 1.5f), rgb(0.95f, 0.95f, 0.94f), M(MAT_METAL_PAINTED), true);
    quad(g, m, br + vec3(F * (supL * 0.5f + 1.02f) - S * (beam0 * 0.95f), -0.6f), br + vec3(F * (supL * 0.5f + 1.02f) + S * (beam0 * 0.95f), -0.6f),
         br + vec3(F * (supL * 0.5f + 1.02f) + S * (beam0 * 0.95f), 0.9f), br + vec3(F * (supL * 0.5f + 1.02f) - S * (beam0 * 0.95f), 0.9f), rgb(0.08f, 0.1f, 0.12f),
         M(MAT_GLASS), vec3(F, 0));
    cyl(g, br + vec3(0, 0, 1.5f), 0.4f, 0.25f, 9.f, 6, rgb(0.9f), M(MAT_METAL_PAINTED), false);
    boxY(g, br + vec3(0, 0, 8.f), S, vec3(3.f, 0.25f, 0.2f), rgb(0.2f), M(MAT_METAL_PAINTED));
    lamp(g, br + vec3(0, 0, 10.8f), 0.35f, vec3(1.f, 0.95f, 0.85f), 0.9f);
    for (int side = -1; side <= 1; side += 2)
        lamp(g, br + vec3(S * (side * (beam0 + 0.3f)), 1.f), 0.35f, side > 0 ? vec3(1.f, 0.08f, 0.05f) : vec3(0.1f, 1.f, 0.3f), 0.9f);
    // funnel
    vec3 fun = P(supX - supL * 0.5f - 6.f, 0.f, freeboard + supH * 0.5f);
    boxY(g, fun + vec3(0, 0, supH * 0.35f), F, vec3(4.f, 3.4f, supH * 0.5f + 4.f), rgb(0.95f, 0.95f, 0.94f), M(MAT_METAL_PAINTED), true);
    const Line& ln = kLines[e.seed % kLineCount];
    boxY(g, fun + vec3(0, 0, supH * 0.72f), F, vec3(4.05f, 3.45f, 1.8f), rgbv(ln.color), M(MAT_METAL_PAINTED), true);
    boxY(g, fun + vec3(0, 0, supH * 0.35f + supH * 0.5f + 4.2f), F, vec3(3.6f, 3.f, 0.3f), rgb(0.1f), M(MAT_METAL_PAINTED), true);
    // lifeboat
    if (detail)
        for (int side = -1; side <= 1; side += 2)
            boxY(g, supBase + vec3(S * (side * (supW + 1.6f)) - F * 2.f, supH * 0.45f), F, vec3(4.f, 1.2f, 1.1f), rgb(1.f, 0.45f, 0.05f), M(MAT_METAL_PAINTED), true);
    collide(g, supBase + vec3(0, 0, supH * 0.5f), F, vec3(supL * 0.5f, supW, supH * 0.5f));
    collide(g, vec3(e.c, (freeboard - draft) * 0.5f), F, vec3(half - 8.f, beam0, (freeboard + draft) * 0.5f));
    // container bays forward and aft of the superstructure
    int rows = big ? 16 : 10;
    const float bayL = 12.8f, CW = 2.44f;
    float rowSpan = rows * CW;
    int tiersMax = big ? 7 : 4;
    Rng r(e.seed);
    std::vector<float> bayX;
    for (float x = supX + supL * 0.5f + 8.f; x + bayL < half - (big ? 32.f : 24.f); x += bayL + 1.4f) bayX.push_back(x + bayL * 0.5f);
    for (float x = supX - supL * 0.5f - 16.f; x - bayL > -half + 8.f; x -= bayL + 1.4f) bayX.push_back(x - bayL * 0.5f);
    for (size_t bi = 0; bi < bayX.size(); bi++) {
        float bx = bayX[bi];
        float distBow = half - bx;
        int tiers = tiersMax - (distBow < 60.f ? 2 : (distBow < 90.f ? 1 : 0));
        if (bx < supX) tiers = Max(2, tiersMax - 2);
        // hatch cover
        vec3 hc = P(bx, 0.f, freeboard + 0.6f);
        boxY(g, hc, F, vec3(bayL * 0.5f, rowSpan * 0.5f + 0.4f, 0.6f), rgb(0.35f, 0.42f, 0.38f), M(MAT_METAL_PAINTED), false);
        if (!detail) {
            // far: per bay a box with a mid-grey tint mixing the lines
            vec3 col = kLines[(bi * 3 + e.seed) % kLineCount].color * 0.6f + vec3(0.25f);
            boxY(g, P(bx, 0.f, freeboard + 1.2f + tiers * 1.3f), F, vec3(bayL * 0.48f, rowSpan * 0.5f, tiers * 1.3f), rgbv(col), M(MAT_CORRUGATED));
            continue;
        }
        for (int j = 0; j < rows; j++) {
            float y = -rowSpan * 0.5f + (j + 0.5f) * CW;
            int th = tiers - ((j == 0 || j == rows - 1) && (r.next() & 1) ? 1 : 0);
            for (int t = 0; t < th; t++) {
                int li = (int)(hash3i((int)bi, j, t + (int)e.seed) % kLineCount);
                vec3 col = kLines[li].color * (0.9f + 0.1f * hashToFloat(hash3i(j, t, (int)bi)));
                u32 faces = 4u | 8u;
                if (j == 0) faces |= 1u;
                if (j == rows - 1) faces |= 2u;
                if (t == th - 1) faces |= 16u;
                vec3 cc = P(bx, y, freeboard + 1.2f + 2.6f * t + 1.3f);
                // container axis along the ship: build with ax = F (long side along F)
                container(g, cc, F, 12.19f, 2.44f, 2.59f, col, (faces & 16u) | ((faces & 1u) ? 1u : 0u) | ((faces & 2u) ? 2u : 0u) | 4u | 8u, (bi & 1) != 0,
                          CL_FLATC, CL_FLAT, hash3i((int)bi, j, t + (int)e.seed));
            }
        }
        collide(g, P(bx, 0.f, freeboard + 1.2f + tiers * 1.3f), F, vec3(bayL * 0.5f, rowSpan * 0.5f, tiers * 1.3f));
        // lashing bridge between bays
        if (bi + 1 < bayX.size() && fabsf(bayX[bi + 1] - bx) < bayL + 2.f) {
            float lx = (bx + bayX[bi + 1]) * 0.5f;
            boxY(g, P(lx, 0.f, freeboard + 4.f), F, vec3(0.35f, rowSpan * 0.5f + 0.3f, 2.8f), rgb(0.85f, 0.8f, 0.3f), M(MAT_METAL_PAINTED));
        }
    }
    // feeder ships carry their own deck cranes
    if (!big) {
        for (int k = 0; k < 2; k++) {
            float cx = supX + 40.f + k * 55.f;
            vec3 pb = P(cx, beam0 * 0.55f, freeboard);
            cyl(g, pb, 1.4f, 1.2f, 9.f, detail ? 10 : 6, rgb(0.95f, 0.8f, 0.1f), M(MAT_METAL_PAINTED), true);
            boxY(g, pb + vec3(0, 0, 10.f), F, vec3(2.f, 1.6f, 1.2f), rgb(0.95f, 0.8f, 0.1f), M(MAT_METAL_PAINTED), true);
            vec3 jt = pb + vec3(F * 14.f - S * 8.f, 20.f);
            beam(g, pb + vec3(0, 0, 10.5f), jt, 0.8f, 0.8f, rgb(0.95f, 0.8f, 0.1f), M(MAT_METAL_PAINTED));
        }
    }
    // mooring lines to the quay bollards
    if (detail) {
        vec2 quaySide = S;  // the quay lies toward whichever side is closer to the island
        if (e.c.x > kPortX1 && dot(S, vec2(-1, 0)) < 0) quaySide = -S;
        for (int k = 0; k < 4; k++) {
            float x = k < 2 ? half - 12.f - k * 8.f : -half + 10.f + (k - 2) * 8.f;
            vec3 a = P(x, 0.f, freeboard) + vec3(quaySide * st[k < 2 ? st.size() - 3 : 2].dk, 0.f);
            vec3 b = vec3(e.c + F * (x + (k < 2 ? 14.f : -14.f)) + quaySide * (beam0 + 3.4f), gSites->portZ + 0.9f);
            rod(g, a, b, 0.1f, 4, rgb(0.8f, 0.75f, 0.6f), M(MAT_FABRIC));
        }
    }
    // deck floodlights + mast lights
    for (int k = 0; k < 3; k++) light(g, P(supX + 20.f + k * (half * 0.5f), 0.f, freeboard + 18.f), vec3(1.f, 0.9f, 0.75f) * 14000.f, 60.f, 1, vec3(0, 0, -1), 0.35f);
    lamp(g, P(half - 8.f, 0.f, freeboard + 9.f), 0.35f, vec3(1.f, 0.95f, 0.85f), 0.9f);
    cyl(g, P(half - 8.f, 0.f, freeboard), 0.3f, 0.2f, 9.f, 6, rgb(0.9f), M(MAT_METAL_PAINTED), false);
}

// ------------------------------------------------------------------------------------------------ rail
float railZ(vec2 p, float zP) {
    float t = gMap->heightAt(p.x, p.y);
    if (gMap->isWater(p.x, p.y) || (p.x > kPortX0 - 1.f && p.x < kPortX1 && p.y > kPortY0 && p.y < kPortY1)) return zP;
    return Max(t + 0.3f, zP - 0.4f);
}

void trackAlong(G& g, const std::vector<vec2>& pts, float zP, bool trestle) {
    vec2 mn, mx;
    cellBounds(g, mn, mx);
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        vec2 a = pts[i], b = pts[i + 1];
        vec2 mid = (a + b) * 0.5f;
        if (!g.owns(mid)) continue;
        vec2 d = normalize(b - a), n = perp(d);
        float za = railZ(a, zP), zb = railZ(b, zP);
        bool water = gMap->isWater(mid.x, mid.y);
        // ballast bed or trestle deck
        if (water || trestle) {
            quad(g, *g.m, vec3(a - n * 2.4f, za - 0.02f), vec3(b - n * 2.4f, zb - 0.02f), vec3(b + n * 2.4f, zb - 0.02f), vec3(a + n * 2.4f, za - 0.02f), rgb(0.7f),
                 M(MAT_CONCRETE), vec3(0, 0, 1));
            for (int s = -1; s <= 1; s += 2)
                quad(g, *g.m, vec3(a + n * (s * 2.4f), za - 0.02f), vec3(b + n * (s * 2.4f), zb - 0.02f), vec3(b + n * (s * 2.4f), zb - 1.1f),
                     vec3(a + n * (s * 2.4f), za - 1.1f), rgb(0.62f), M(MAT_CONCRETE), vec3(n * (float)s, 0));
            // pier every ~12 m
            float L = length(b - a);
            for (float t = 0.f; t < L; t += 12.f) {
                vec2 p = a + d * t;
                cyl(g, vec3(p, -4.f), 0.7f, 0.7f, Lerp(za, zb, t / Max(L, 1e-3f)) + 3.f, 8, rgb(0.6f), M(MAT_CONCRETE), false);
            }
        } else {
            quad(g, *g.m, vec3(a - n * 2.f, za - 0.05f), vec3(b - n * 2.f, zb - 0.05f), vec3(b + n * 2.f, zb - 0.05f), vec3(a + n * 2.f, za - 0.05f),
                 rgb(0.62f, 0.58f, 0.52f), M(MAT_STONE), vec3(0, 0, 1));
        }
        if (g.detail) {
            float L = length(b - a);
            for (float t = 0.35f; t < L; t += 0.7f) {
                vec2 p = a + d * t;
                float z = Lerp(za, zb, t / Max(L, 1e-3f));
                g.m->box(vec3(p, z + 0.05f) - g.org, vec3(n, 0), vec3(d, 0), vec3(0, 0, 1), vec3(1.3f, 0.12f, 0.06f), rgb(0.45f, 0.36f, 0.28f), M(MAT_WOOD), false);
            }
        }
        for (int s = -1; s <= 1; s += 2) beam(g, vec3(a + n * (s * 0.72f), za + 0.18f), vec3(b + n * (s * 0.72f), zb + 0.18f), 0.08f, 0.14f, rgb(0.45f, 0.42f, 0.4f),
                                             M(MAT_METAL_BRUSHED));
    }
}

void railcar(G& g, vec2 c, vec2 dir, float z, bool loco, u32 seed) {
    vec2 n = perp(dir);
    if (loco) {
        boxY(g, vec3(c, z + 2.4f), dir, vec3(11.f, 1.55f, 1.6f), rgb(0.95f, 0.75f, 0.1f), M(MAT_METAL_PAINTED));
        boxY(g, vec3(c + dir * 8.f, z + 4.3f), dir, vec3(2.4f, 1.5f, 0.6f), rgb(0.1f, 0.2f, 0.45f), M(MAT_METAL_PAINTED));
        quad(g, *g.m, vec3(c + dir * 10.42f - n * 1.2f, z + 3.9f), vec3(c + dir * 10.42f + n * 1.2f, z + 3.9f), vec3(c + dir * 10.42f + n * 1.2f, z + 4.7f),
             vec3(c + dir * 10.42f - n * 1.2f, z + 4.7f), rgb(0.1f, 0.12f, 0.15f), M(MAT_GLASS), vec3(dir, 0));
        boxY(g, vec3(c, z + 0.9f), dir, vec3(10.f, 1.4f, 0.35f), rgb(0.15f), M(MAT_METAL_PAINTED));
        if (g.detail) {
            const char* t = "SUNLINE";
            float th = 0.9f;
            float tw = textAdvance(t, th, 0.3f);
            for (int s = -1; s <= 1; s += 2) {
                vec3 out(n * (float)s, 0);
                vec3 rt = s > 0 ? vec3(-dir, 0) : vec3(dir, 0);
                vec3 o = vec3(c + n * (s * 1.57f), z + 2.1f) - rt * (tw * 0.5f);
                strokeText(g, *g.m, t, o, rt, vec3(0, 0, 1), th, 0.14f, rgb(0.1f, 0.2f, 0.45f), M(MAT_METAL_PAINTED), 0.f, 0.3f);
            }
            lamp(g, vec3(c + dir * 11.05f, z + 3.2f), 0.3f, vec3(1.f, 0.95f, 0.8f), 0.8f, EA_NIGHT);
        }
        collide(g, vec3(c, z + 2.2f), dir, vec3(11.f, 1.6f, 2.2f));
        return;
    }
    // well car with a pair of stacked containers
    boxY(g, vec3(c, z + 1.f), dir, vec3(10.f, 1.4f, 0.35f), rgb(0.35f, 0.2f, 0.15f), M(MAT_METAL_PAINTED));
    for (int s = -1; s <= 1; s += 2) boxY(g, vec3(c + n * (s * 1.35f), z + 1.6f), dir, vec3(10.f, 0.08f, 0.6f), rgb(0.35f, 0.2f, 0.15f), M(MAT_METAL_PAINTED));
    for (int k = 0; k < 2; k++) {
        if ((hash32(seed + k) & 7) == 0) continue;
        vec3 col = kLines[hash32(seed * 3 + k) % kLineCount].color;
        container(g, vec3(c, z + 1.4f + 1.3f + k * 2.6f), dir, 12.19f, 2.44f, 2.59f, col, k ? 31u : 15u, true, CL_FLATC, CL_FLATC, seed * 7u + (u32)k);
    }
    for (int s = -1; s <= 1; s += 2) boxY(g, vec3(c + dir * (s * 8.f), z + 0.5f), dir, vec3(1.2f, 1.2f, 0.45f), rgb(0.15f), M(MAT_METAL_PAINTED));
    collide(g, vec3(c, z + 2.6f), dir, vec3(10.f, 1.4f, 2.6f));
}

void genRailYard(const SiteElem& e, G& g) {
    int tracks = (int)e.p[0];
    vec2 X = e.ax, Y = perp(e.ax);
    for (int t = 0; t < tracks; t++) {
        float off = (t - (tracks - 1) * 0.5f) * 15.f;
        vec2 a = e.c - X * e.hx + Y * off, b = e.c + X * e.hx + Y * off;
        std::vector<vec2> pts;
        int n = (int)(e.hx * 2.f / 40.f);
        for (int k = 0; k <= n; k++) pts.push_back(lerp(a, b, (float)k / n));
        trackAlong(g, pts, e.z, false);
        // buffer stop at the east end
        if (g.owns(b)) {
            boxY(g, vec3(b - X * 1.f, e.z + 0.8f), X, vec3(0.6f, 1.5f, 0.8f), rgb(0.8f, 0.15f, 0.1f), M(MAT_METAL_PAINTED));
            lamp(g, vec3(b - X * 1.f, e.z + 1.8f), 0.25f, vec3(1.f, 0.1f, 0.05f), 0.8f);
        }
        // trains: well cars on the first two tracks, a locomotive on the third
        if (t < 2) {
            int cars = t == 0 ? 14 : 10;
            for (int k = 0; k < cars; k++) {
                vec2 cc = a + X * (30.f + k * 21.f + t * 20.f);
                if (g.owns(cc)) railcar(g, cc, X, e.z, false, e.seed * 31u + (u32)(t * 100 + k));
            }
        } else {
            vec2 cc = a + X * 70.f;
            if (g.owns(cc)) railcar(g, cc, X, e.z, true, e.seed);
            vec2 cc2 = a + X * 93.f;
            if (g.owns(cc2)) railcar(g, cc2, -X, e.z, true, e.seed + 1);
        }
    }
}

void genRail(const SiteElem& e, G& g) {
    if (e.variant == 1) {
        if (!g.owns(e.c)) return;
        vec3 p(e.c, e.z + 0.3f);
        boxY(g, p + vec3(0, 0, 0.7f), e.ax, vec3(0.5f, 1.6f, 0.7f), rgb(0.85f, 0.15f, 0.1f), M(MAT_METAL_PAINTED));
        for (int s = -1; s <= 1; s += 2) beam(g, p + vec3(perp(e.ax) * (s * 1.2f), 1.4f), p + vec3(-e.ax * 3.f + perp(e.ax) * (s * 1.2f), 0.f), 0.2f, 0.2f, rgb(0.3f),
                                             M(MAT_METAL_PAINTED));
        lamp(g, p + vec3(0, 0, 1.7f), 0.25f, vec3(1.f, 0.1f, 0.05f), 0.8f);
        return;
    }
    trackAlong(g, e.pts, gSites->portZ, e.p[0] > 0.5f);
}

// Rail-mounted gantry over the intermodal tracks
void genRmgCrane(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 span = e.ax, along = perp(e.ax);
    vec3 o(e.c, e.z);
    u32 blue = rgb(0.15f, 0.35f, 0.7f), white = rgb(0.93f);
    float halfSpan = 30.f, H = 24.f;
    for (int s = -1; s <= 1; s += 2)
        for (int a = -1; a <= 1; a += 2) {
            vec3 base = o + vec3(span * (s * halfSpan) + along * (a * 7.f), 0);
            beam(g, base + vec3(0, 0, 1.f), base + vec3(0, 0, H), 1.1f, 1.1f, blue, M(MAT_METAL_PAINTED));
            collide(g, base + vec3(0, 0, H * 0.5f), span, vec3(0.6f, 0.6f, H * 0.5f));
        }
    for (int a = -1; a <= 1; a += 2) beam(g, o + vec3(-span * (halfSpan + 6.f) + along * (a * 3.f), H + 1.f), o + vec3(span * (halfSpan + 6.f) + along * (a * 3.f), H + 1.f),
                                         1.2f, 2.4f, white, M(MAT_METAL_PAINTED));
    for (int s = -1; s <= 1; s += 2) beam(g, o + vec3(span * (s * halfSpan) - along * 7.f, H), o + vec3(span * (s * halfSpan) + along * 7.f, H), 1.f, 1.6f, blue, M(MAT_METAL_PAINTED));
    vec3 tr = o + vec3(span * 8.f, H + 2.4f);
    boxY(g, tr, span, vec3(3.f, 4.f, 1.2f), rgb(0.95f, 0.8f, 0.1f), M(MAT_METAL_PAINTED), true);
    boxY(g, tr - vec3(0, 0, 12.f), along, vec3(6.2f, 1.25f, 0.35f), rgb(0.95f, 0.75f, 0.1f), M(MAT_METAL_PAINTED), true);
    lamp(g, o + vec3(span * halfSpan, H + 2.6f), 0.4f, vec3(1.f, 0.06f, 0.03f), 1.f, EA_BLINK, e.seed & 255u);
    light(g, o + vec3(0, 0, H - 1.f), vec3(1.f, 0.9f, 0.75f) * 16000.f, 55.f, 1, vec3(0, 0, -1), 0.35f);
}

// ------------------------------------------------------------------------------------------------ gate complex
void genPortGate(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 road = e.ax, side = perp(e.ax);
    vec3 o(e.c, e.z);
    float H = 7.5f;
    boxY(g, o + vec3(0, 0, H + 0.5f), road, vec3(e.hx, e.hy, 0.55f), rgb(0.95f, 0.95f, 0.94f), M(MAT_METAL_PAINTED), true);
    quad(g, *g.m, o + vec3(-road * (e.hx + 0.02f) - side * e.hy, H - 0.05f), o + vec3(-road * (e.hx + 0.02f) + side * e.hy, H - 0.05f),
         o + vec3(-road * (e.hx + 0.02f) + side * e.hy, H + 1.05f), o + vec3(-road * (e.hx + 0.02f) - side * e.hy, H + 1.05f), rgb(0.1f, 0.35f, 0.6f),
         M(MAT_METAL_PAINTED), vec3(-road, 0));
    {
        float th = 0.8f;
        float tw = textAdvance(e.text.c_str(), th, 0.3f);
        vec2 viewR = perp(-road);
        strokeText(g, *g.m, e.text.c_str(), o + vec3(-road * (e.hx + 0.05f) - viewR * (tw * 0.5f), H + 0.1f), vec3(viewR, 0), vec3(0, 0, 1), th, 0.13f,
                   rgb(1.f, 1.f, 1.f, 0.4f), emMat(EA_NIGHT), 0.f, 0.3f);
    }
    for (int a = -1; a <= 1; a += 2)
        for (int s = -1; s <= 1; s += 2) {
            vec3 cp = o + vec3(road * (a * (e.hx - 3.f)) + side * (s * (e.hy - 1.f)), 0);
            cyl(g, cp, 0.4f, 0.4f, H, 8, rgb(0.9f), M(MAT_METAL_PAINTED), false);
            collide(g, cp + vec3(0, 0, H * 0.5f), road, vec3(0.4f, 0.4f, H * 0.5f));
        }
    // guard booths at the road edges: plinth, solid base, a glazed band all round with mullions, overhanging roof slab with
    // a gate number, AC unit, door on the terminal side, and a striped barrier arm (raised) reaching over the lanes
    for (int s = -1; s <= 1; s += 2) {
        vec3 bc = o + vec3(side * (s * (e.hy - 3.f)), 0.f);
        u32 st = rgb(0.9f, 0.88f, 0.85f), paint = M(MAT_METAL_PAINTED);
        boxY(g, bc + vec3(0, 0, 0.12f), road, vec3(2.1f, 1.5f, 0.15f), rgb(0.7f), M(MAT_CONCRETE));
        boxY(g, bc + vec3(0, 0, 0.62f), road, vec3(1.8f, 1.2f, 0.36f), st, M(MAT_STUCCO));
        boxY(g, bc + vec3(0, 0, 2.62f), road, vec3(1.8f, 1.2f, 0.2f), st, M(MAT_STUCCO));
        boxY(g, bc + vec3(0, 0, 2.95f), road, vec3(2.25f, 1.65f, 0.13f), rgb(0.25f, 0.27f, 0.3f), paint, true);
        collide(g, bc + vec3(0, 0, 1.45f), road, vec3(1.8f, 1.2f, 1.45f));
        // glass band (inset) with corner and mid mullions
        for (int f = 0; f < 4; f++) {
            vec2 n = f == 0 ? road : (f == 1 ? -road : (f == 2 ? side : -side));
            vec2 t = perp(n);
            float hl = f < 2 ? 1.2f : 1.8f, dn = f < 2 ? 1.8f : 1.2f;
            vec3 c = bc + vec3(n * (dn - 0.03f), 1.7f);
            quad(g, *g.m, c - vec3(t * hl, 0.72f), c + vec3(t * hl, -0.72f), c + vec3(t * hl, 0.72f), c + vec3(-t * hl, 0.72f), rgb(0.15f, 0.22f, 0.26f), M(MAT_GLASS),
                 vec3(n, 0.f));
            if (g.detail)
                for (float u : {-hl + 0.04f, 0.f, hl - 0.04f}) boxY(g, c + vec3(n * 0.02f + t * u, 0.f), n, vec3(0.03f, 0.04f, 0.72f), rgb(0.3f), paint);
        }
        if (g.detail) {
            // door on the terminal side, AC unit on the roof, gate number on the fascia facing the arrivals
            vec3 dc = bc + vec3(road * 1.81f + side * (s * 0.5f), 1.05f);
            quad(g, *g.m, dc - vec3(side * 0.45f, 1.05f), dc + vec3(side * 0.45f, -1.05f), dc + vec3(side * 0.45f, 1.05f), dc + vec3(-side * 0.45f, 1.05f),
                 rgb(0.35f, 0.4f, 0.45f), paint, vec3(road, 0.f));
            boxY(g, bc + vec3(-road * 0.8f, 3.35f), road, vec3(0.45f, 0.4f, 0.28f), rgb(0.85f), paint, false);
            const char* num = s < 0 ? "GATE 1" : "GATE 2";
            float th = 0.2f, tw = textAdvance(num, th, 0.3f);
            vec2 vr = perp(road);   // viewer's right when facing +road... text faces -road (arrivals)
            strokeText(g, *g.m, num, bc + vec3(-road * 2.26f + vr * (tw * 0.5f), 2.87f), vec3(-vr, 0.f), vec3(0, 0, 1), th, 0.035f, rgb(1.f, 0.85f, 0.2f), paint,
                       0.f, 0.3f);
            // barrier arm: pivot housing at the lane-side corner, arm raised about 80 degrees, red and white bands
            vec3 pivot = bc + vec3(-road * 1.4f - side * (s * 1.5f), 1.05f);
            boxY(g, pivot - vec3(0, 0, 0.5f), road, vec3(0.22f, 0.22f, 0.55f), rgb(0.9f, 0.75f, 0.1f), paint, false);
            vec3 tip = pivot + normalize(vec3(-side * (s * 0.18f), 1.f)) * 7.f;
            for (int k = 0; k < 7; k++)
                beam(g, lerp(pivot, tip, k / 7.f), lerp(pivot, tip, (k + 1) / 7.f), 0.1f, 0.1f, (k & 1) ? rgb(0.95f) : rgb(0.85f, 0.08f, 0.06f), paint);
            // traffic light for the lane
            cyl(g, pivot - vec3(0, 0, 1.05f) - vec3(road * 0.6f, 0.f), 0.06f, 0.06f, 2.6f, 6, rgb(0.2f), paint, false);
            vec3 hc = pivot + vec3(-road * 0.6f, 1.75f);
            boxY(g, hc, road, vec3(0.12f, 0.15f, 0.3f), rgb(0.1f), paint, true);
            lamp(g, hc + vec3(-road * 0.13f, 0.12f), 0.14f, vec3(1.f, 0.08f, 0.04f), 0.9f);
            lamp(g, hc + vec3(-road * 0.13f, -0.14f), 0.14f, vec3(0.1f, 0.25f, 0.12f), 0.3f);
        }
    }
    if (g.detail) {
        vec3 pc = o + vec3(-road * (e.hx + 12.f), 0);
        for (int s = -1; s <= 1; s += 2) beam(g, pc + vec3(side * (s * 11.f), 0.f), pc + vec3(side * (s * 11.f), 6.5f), 0.3f, 0.3f, rgb(0.4f), M(MAT_METAL_PAINTED));
        beam(g, pc + vec3(-side * 11.f, 6.5f), pc + vec3(side * 11.f, 6.5f), 0.4f, 0.6f, rgb(0.4f), M(MAT_METAL_PAINTED));
    }
    for (int k = -1; k <= 1; k++) light(g, o + vec3(road * (k * e.hx * 0.6f), H - 0.3f), vec3(1.f, 0.95f, 0.9f) * 9000.f, 30.f, 1, vec3(0, 0, -1), 0.3f);
}

// ------------------------------------------------------------------------------------------------ yard vehicles
// Tyre with sidewall and hub: centre c, axle direction ax (unit, pointing out of the vehicle), radius r, width w
void yardWheel(G& g, vec3 c, vec3 ax, float r, float w, int seg) {
    vec3 u = normalize(anyPerp(ax)), v = cross(ax, u);
    u32 tyre = rgb(0.035f), hub = rgb(0.55f, 0.56f, 0.58f), rubber = M(MAT_RUBBER);
    rod(g, c - ax * (w * 0.5f), c + ax * (w * 0.5f), r, seg, tyre, rubber);
    MeshData& m = *g.m;
    for (int side = -1; side <= 1; side += 2) {
        vec3 fc = c + ax * (side * w * 0.5f);
        vec3 n = ax * (float)side;
        u32 b0 = m.addVertex(fc - g.org, n, u, vec2(0, 0), hub, M(MAT_METAL_PAINTED));
        for (int k = 0; k <= seg; k++) {
            float a = kTwoPi * k / seg;
            m.addVertex(fc + (u * cosf(a) + v * sinf(a)) * (r * 0.55f) - g.org, n, u, vec2(cosf(a), sinf(a)), hub, M(MAT_METAL_PAINTED));
        }
        u32 r0 = (u32)m.verts.size();
        for (int k = 0; k <= seg; k++) {
            float a = kTwoPi * k / seg;
            vec3 dir = u * cosf(a) + v * sinf(a);
            m.addVertex(fc + dir * (r * 0.55f) - g.org, n, u, vec2(0, 0), tyre, rubber);
            m.addVertex(fc + dir * r - g.org, normalize(n + dir * 0.3f), u, vec2(0, 1), tyre, rubber);
        }
        for (int k = 0; k < seg; k++) {
            vec3 fn = cross(m.verts[b0 + 1 + k].pos - m.verts[b0].pos, m.verts[b0 + 2 + k].pos - m.verts[b0].pos);
            if (dot(fn, n) > 0.f) m.tri(b0, b0 + 1 + k, b0 + 2 + k);
            else m.tri(b0, b0 + 2 + k, b0 + 1 + k);
            u32 a0 = r0 + k * 2, a1 = a0 + 2;
            vec3 fr = cross(m.verts[a1].pos - m.verts[a0].pos, m.verts[a0 + 1].pos - m.verts[a0].pos);
            if (dot(fr, n) > 0.f) m.quadIdx(a0, a1, a1 + 1, a0 + 1);
            else m.quadIdx(a0, a0 + 1, a1 + 1, a1);
        }
    }
}

// Conventional tractor (long hood, day cab, chrome stacks, fuel tanks, three axles) on a skeletal container chassis (twin
// rails, cross members, bolsters with twistlocks, landing legs, bumper and lamps), loaded or empty.
// e.ax = heading, variant 0: 40 ft box, 1: 20 ft box, 2: empty 40 ft chassis; p[0] cab colour, p[1] box line (-1 lease)
void genPortTruck(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 F2 = e.ax, S2 = perp(e.ax);
    vec3 F(F2, 0.f), S(S2, 0.f), Z(0, 0, 1);
    bool detail = g.detail;
    const vec3 cabPal[] = {vec3(0.72f, 0.1f, 0.08f), vec3(0.92f), vec3(0.1f, 0.2f, 0.45f), vec3(0.15f, 0.35f, 0.2f), vec3(0.95f, 0.72f, 0.1f), vec3(0.12f)};
    u32 cab = rgbv(cabPal[(int)e.p[0] % 6]), paint = M(MAT_METAL_PAINTED), frame = rgb(0.1f), chrome = M(MAT_CHROME);
    float Lc = e.variant == 1 ? 6.4f : 12.5f;
    float total = 7.1f + Lc - 2.2f;
    vec3 o(e.c, e.z);
    float xf = total * 0.5f;  // tractor bumper
    auto P = [&](float x, float y, float z) { return o + F * x + S * y + Z * z; };
    // tractor: frame, bumper, hood with grille, cab with windscreen and side glass, sleeperless back wall, fenders
    boxY(g, P(xf - 3.6f, 0.f, 1.0f), F2, vec3(3.5f, 0.45f, 0.12f), frame, paint);
    boxY(g, P(xf - 0.12f, 0.f, 0.72f), F2, vec3(0.14f, 1.2f, 0.22f), rgb(0.75f), chrome, true);
    boxY(g, P(xf - 1.25f, 0.f, 1.62f), F2, vec3(1.0f, 1.0f, 0.5f), cab, paint);
    boxY(g, P(xf - 3.3f, 0.f, 2.2f), F2, vec3(1.05f, 1.2f, 1.1f), cab, paint, true);
    collide(g, P(xf - 2.2f, 0.f, 1.7f), F2, vec3(2.2f, 1.25f, 1.7f));
    if (detail) {
        quad(g, *g.m, P(xf - 0.25f, -0.85f, 1.2f), P(xf - 0.25f, 0.85f, 1.2f), P(xf - 0.25f, 0.85f, 2.0f), P(xf - 0.25f, -0.85f, 2.0f), rgb(0.55f, 0.56f, 0.58f), chrome, F);
        quad(g, *g.m, P(xf - 2.24f, -1.1f, 2.35f), P(xf - 2.24f, 1.1f, 2.35f), P(xf - 2.24f, 1.05f, 3.15f), P(xf - 2.24f, -1.05f, 3.15f), rgb(0.08f, 0.1f, 0.12f),
             M(MAT_GLASS), F);
        for (int s = -1; s <= 1; s += 2) {
            quad(g, *g.m, P(xf - 2.45f, s * 1.21f, 2.35f), P(xf - 3.5f, s * 1.21f, 2.35f), P(xf - 3.5f, s * 1.21f, 3.1f), P(xf - 2.45f, s * 1.21f, 3.1f),
                 rgb(0.08f, 0.1f, 0.12f), M(MAT_GLASS), S * (float)s);
            // headlamps, mirrors, fuel tank, steps, exhaust stack
            boxY(g, P(xf - 0.22f, s * 0.8f, 1.3f), F2, vec3(0.04f, 0.16f, 0.08f), rgb(1.f, 0.95f, 0.85f, 0.2f), emMat(EA_NIGHT), true);
            boxY(g, P(xf - 2.2f, s * 1.45f, 2.7f), F2, vec3(0.05f, 0.12f, 0.2f), rgb(0.1f), paint, true);
            rod(g, P(xf - 2.35f, s * 1.18f, 0.72f), P(xf - 3.85f, s * 1.18f, 0.72f), 0.3f, 10, rgb(0.8f), chrome);
            boxY(g, P(xf - 4.6f, s * 1.08f, 2.4f), F2, vec3(0.06f, 0.06f, 1.4f), rgb(0.85f), chrome);
            cyl(g, P(xf - 4.6f, s * 1.08f, 3.8f), 0.09f, 0.09f, 0.45f, 8, rgb(0.8f), chrome, false);
            // fenders over the drive axles
            boxY(g, P(xf - 6.25f, s * 1.05f, 1.2f), F2, vec3(1.1f, 0.2f, 0.04f), frame, paint);
        }
        boxY(g, P(xf - 6.2f, 0.f, 1.2f), F2, vec3(0.6f, 0.6f, 0.08f), rgb(0.2f), paint, true);   // fifth wheel
    }
    // wheels: steer axle and tandem drive
    float wr = 0.52f;
    for (int s = -1; s <= 1; s += 2) {
        vec3 ax = S * (float)s;
        int seg = detail ? 12 : 6;
        yardWheel(g, P(xf - 1.25f, s * 1.0f, wr), ax, wr, 0.3f, seg);
        for (float x : {xf - 5.6f, xf - 6.95f}) yardWheel(g, P(x, s * 0.95f, wr), ax, wr, 0.55f, seg);
    }
    // chassis: rails, bolsters with twistlock corners, cross members, landing legs, rear axles, bumper, lamps
    float c0 = xf - 5.0f, c1 = c0 - Lc;   // chassis front / rear along F
    float deck = 1.36f;
    boxY(g, P((c0 + c1) * 0.5f, 0.f, deck - 0.14f), F2, vec3(Lc * 0.5f, 0.5f, 0.12f), frame, paint);
    for (float x : {c0 - 0.15f, c1 + 0.15f}) boxY(g, P(x, 0.f, deck - 0.1f), F2, vec3(0.15f, 1.22f, 0.1f), frame, paint, true);
    collide(g, P((c0 + c1) * 0.5f, 0.f, deck * 0.5f + 0.1f), F2, vec3(Lc * 0.5f, 1.2f, deck * 0.5f));
    if (detail) {
        for (float x = c0 - 1.4f; x > c1 + 0.8f; x -= 1.6f) boxY(g, P(x, 0.f, deck - 0.12f), F2, vec3(0.05f, 1.15f, 0.05f), frame, paint);
        for (int s = -1; s <= 1; s += 2) {
            for (float x : {c0 - 0.1f, c1 + 0.1f}) boxY(g, P(x, s * 1.12f, deck + 0.03f), F2, vec3(0.07f, 0.07f, 0.04f), rgb(0.35f), paint, true);
            boxY(g, P(c0 - 2.2f, s * 0.6f, (deck - 0.2f) * 0.5f), F2, vec3(0.06f, 0.06f, (deck - 0.2f) * 0.5f), frame, paint);
            boxY(g, P(c0 - 2.2f, s * 0.6f, 0.04f), F2, vec3(0.18f, 0.18f, 0.04f), frame, paint, true);
            boxY(g, P(c1 + 0.1f, s * 1.0f, 0.95f), F2, vec3(0.06f, 0.14f, 0.07f), rgb(0.9f, 0.08f, 0.05f, 0.25f), emMat(EA_NIGHT), true);
        }
        boxY(g, P(c1 + 0.2f, 0.f, 0.7f), F2, vec3(0.08f, 1.15f, 0.1f), frame, paint, true);
    }
    for (int s = -1; s <= 1; s += 2)
        for (float x : {c1 + 1.9f, c1 + 3.2f}) yardWheel(g, P(x, s * 0.95f, wr), S * (float)s, wr, 0.55f, detail ? 12 : 6);
    // the box
    if (e.variant != 2) {
        int li = (int)e.p[1];
        vec3 col = li >= 0 ? kLines[li % kLineCount].color : kLeaseCol[(u32)e.seed % (u32)kLeaseCount];
        vec3 bc = P((c0 + c1) * 0.5f, 0.f, deck + 0.01f + 1.2955f);
        container(g, bc, F2, e.variant == 1 ? 6.06f : 12.19f, 2.44f, 2.591f, col, 31u, (e.seed & 1u) != 0, CL_MID, CL_MID, e.seed * 3u + 7u);
    }
}

// Reach stacker: big wheeled machine with a telescopic boom over the front axle and a top-lift spreader, carrying a 40 ft
// box across the machine low for travel. e.ax = heading, p[0] boom angle (deg), p[1] box line
void genReachStacker(const SiteElem& e, G& g) {
    if (!g.owns(e.c)) return;
    vec2 F2 = e.ax, S2 = perp(e.ax);
    vec3 F(F2, 0.f), S(S2, 0.f), Z(0, 0, 1);
    vec3 o(e.c, e.z);
    bool detail = g.detail;
    auto P = [&](float x, float y, float z) { return o + F * x + S * y + Z * z; };
    u32 yel = rgb(0.95f, 0.7f, 0.08f), blk = rgb(0.08f), paint = M(MAT_METAL_PAINTED);
    // chassis, counterweight, engine cover, cab
    boxY(g, P(-0.1f, 0.f, 1.45f), F2, vec3(4.9f, 1.45f, 0.6f), yel, paint, true);
    boxY(g, P(-4.7f, 0.f, 1.6f), F2, vec3(1.0f, 2.0f, 1.0f), yel, paint, true);
    boxY(g, P(-2.5f, 0.f, 2.45f), F2, vec3(1.2f, 1.5f, 0.45f), yel, paint);
    boxY(g, P(0.6f, -0.7f, 3.25f), F2, vec3(1.1f, 0.95f, 1.2f), yel, paint);
    collide(g, P(-0.3f, 0.f, 1.8f), F2, vec3(5.3f, 2.0f, 1.8f));
    if (detail) {
        for (int k = 0; k < 4; k++) {
            vec3 n = k == 0 ? F : (k == 1 ? -F : (k == 2 ? S : -S));
            float hx = (k < 2) ? 0.9f : 1.0f;
            vec3 c = P(0.6f, -0.7f, 3.55f) + n * (k < 2 ? 1.12f : 0.97f);
            vec3 t = k < 2 ? S : F;
            quad(g, *g.m, c - t * hx - Z * 0.55f, c + t * hx - Z * 0.55f, c + t * hx + Z * 0.6f, c - t * hx + Z * 0.6f, rgb(0.1f, 0.13f, 0.16f), M(MAT_GLASS), n);
        }
        // black / yellow hazard band on the counterweight, beacon on the cab
        for (int k = 0; k < 6; k++) {
            float y0 = -2.f + k * (4.f / 6.f);
            quad(g, *g.m, P(-5.72f, y0, 0.7f), P(-5.72f, y0 + 4.f / 12.f, 0.7f), P(-5.72f, y0 + 4.f / 12.f + 0.3f, 1.2f), P(-5.72f, y0 + 0.3f, 1.2f), blk, paint, -F);
        }
        lamp(g, P(0.6f, -0.7f, 4.55f), 0.22f, vec3(1.f, 0.55f, 0.05f), 0.9f, EA_BLINK, e.seed & 255u);
    }
    // wheels: dual drive wheels under the boom, single steer wheels at the back
    for (int s = -1; s <= 1; s += 2) {
        int seg = detail ? 14 : 6;
        for (float y : {1.55f, 2.2f}) yardWheel(g, P(3.1f, s * y, 0.9f), S * (float)s, 0.9f, 0.6f, seg);
        yardWheel(g, P(-3.8f, s * 1.6f, 0.8f), S * (float)s, 0.8f, 0.6f, seg);
    }
    // boom from the pivot over the engine to the head, lift cylinder, spreader and the box across the machine
    float ang = Clamp(e.p[0], 2.f, 45.f) * kDegToRad;
    vec3 piv = P(-3.4f, 0.f, 3.2f);
    vec3 dir = F * cosf(ang) + Z * sinf(ang);
    float blen = 11.5f;
    vec3 tip = piv + dir * blen;
    beam(g, piv, tip, 1.0f, 1.1f, yel, paint, cross(dir, S));
    beam(g, piv + dir * (blen * 0.55f) - cross(dir, S) * 0.05f, tip, 0.85f, 0.95f, yel, paint, cross(dir, S));
    rod(g, P(0.8f, 0.f, 1.9f), piv + dir * 5.5f, 0.18f, detail ? 8 : 5, rgb(0.8f), M(MAT_CHROME));
    vec3 head = tip - Z * 0.9f;
    boxY(g, head, F2, vec3(0.6f, 0.6f, 0.6f), yel, paint, true);
    boxY(g, head - Z * 0.85f, S2, vec3(6.1f, 0.55f, 0.22f), yel, paint, true);
    int li = (int)e.p[1];
    vec3 col = li >= 0 ? kLines[li % kLineCount].color : kLeaseCol[e.seed % (u32)kLeaseCount];
    container(g, head - Z * (1.08f + 1.2955f), S2, 12.19f, 2.44f, 2.591f, col, 31u, (e.seed & 2u) != 0, CL_MID, CL_MID, e.seed * 5u + 3u);
}

// Yard dressing: variant 0 jersey barrier run a..b, 1 lashing cages, 2 sign on a post (text, p[0] kind), 3 oil stains and
// wet patches over an area, 4 cone and drum cluster, 5 block ID board on a pole
void genPortDress(const SiteElem& e, G& g) {
    if (!g.detail) return;
    u32 conc = M(MAT_CONCRETE), paint = M(MAT_METAL_PAINTED);
    switch (e.variant) {
        case 0: {
            // precast barriers 3 m long following the line (split per cell by segment midpoint)
            vec2 d = normalize(e.b - e.a), n = perp(d);
            float L = length(e.b - e.a);
            int nseg = Max(1, (int)(L / 3.05f));
            Rng r(e.seed);
            for (int k = 0; k < nseg; k++) {
                vec2 c = e.a + d * (L * (k + 0.5f) / nseg);
                float jit = r.range(-0.03f, 0.03f), yaw = r.range(-0.015f, 0.015f);
                vec2 dd = normalize(d + n * yaw), nn = perp(dd);
                if (!g.owns(c)) continue;
                c += nn * jit;
                float hl = L / nseg * 0.5f - 0.03f;
                // F-shape profile: base 0.6 wide, top 0.2 wide, 0.81 tall
                const vec2 pr[6] = {vec2(-0.3f, 0.f), vec2(-0.3f, 0.08f), vec2(-0.2f, 0.33f), vec2(-0.1f, 0.81f), vec2(0.1f, 0.81f), vec2(0.2f, 0.33f)};
                vec2 prr[8] = {pr[0], pr[1], pr[2], pr[3], pr[4], pr[5], vec2(0.3f, 0.08f), vec2(0.3f, 0.f)};
                float gv = r.range(0.66f, 0.8f), warm = r.range(-0.015f, 0.02f);
                u32 col = rgb(gv + warm, gv, gv - warm * 1.5f);
                for (int i = 0; i + 1 < 8; i++) {
                    vec3 a0(c - dd * hl + nn * prr[i].x, e.z + prr[i].y), a1(c + dd * hl + nn * prr[i].x, e.z + prr[i].y);
                    vec3 b0(c - dd * hl + nn * prr[i + 1].x, e.z + prr[i + 1].y), b1(c + dd * hl + nn * prr[i + 1].x, e.z + prr[i + 1].y);
                    vec2 on = normalize(perp(prr[i + 1] - prr[i]));   // outward in the (lateral, up) plane
                    quad(g, *g.m, a0, a1, b1, b0, col, conc, vec3(nn * on.x, on.y));
                }
                for (int s = -1; s <= 1; s += 2) {
                    // end caps (the profile is convex: a fan)
                    MeshData& m = *g.m;
                    vec3 cn(dd * (float)s, 0.f);
                    u32 c0 = (u32)m.verts.size();
                    for (int i = 0; i < 8; i++) m.addVertex(vec3(c + dd * (s * hl) + nn * prr[i].x, e.z + prr[i].y) - g.org, cn, vec3(nn, 0.f), prr[i], col, conc);
                    for (int i = 1; i + 1 < 8; i++) {
                        vec3 fn = cross(m.verts[c0 + i].pos - m.verts[c0].pos, m.verts[c0 + i + 1].pos - m.verts[c0].pos);
                        if (dot(fn, cn) >= 0.f) m.tri(c0, c0 + i, c0 + i + 1);
                        else m.tri(c0, c0 + i + 1, c0 + i);
                    }
                }
                collide(g, vec3(c, e.z + 0.4f), dd, vec3(hl, 0.3f, 0.4f));
            }
            break;
        }
        case 1: {
            // lashing cages: steel frames (2 x 1.2 x 1.1) stacked, bars and twistlocks inside
            if (!g.owns(e.c)) return;
            vec2 X = e.ax, Y = perp(e.ax);
            Rng r(e.seed);
            int n = 2 + (int)(e.seed % 3u);
            for (int k = 0; k < n; k++) {
                int tiers = r.chance(0.5f) ? 2 : 1;
                vec2 c = e.c + X * (k * 2.25f);
                u32 fc = r.chance(0.5f) ? rgb(0.12f, 0.28f, 0.55f) : rgb(0.9f, 0.62f, 0.08f);
                for (int t = 0; t < tiers; t++) {
                    float z0 = e.z + t * 1.15f;
                    for (int cx = -1; cx <= 1; cx += 2)
                        for (int cy = -1; cy <= 1; cy += 2) boxY(g, vec3(c + X * (cx * 0.97f) + Y * (cy * 0.57f), z0 + 0.55f), X, vec3(0.04f, 0.04f, 0.55f), fc, paint);
                    for (float z : {z0 + 0.05f, z0 + 1.07f})
                        for (int cy = -1; cy <= 1; cy += 2) boxY(g, vec3(c + Y * (cy * 0.57f), z), X, vec3(1.0f, 0.04f, 0.04f), fc, paint);
                    for (float z : {z0 + 0.05f, z0 + 1.07f})
                        for (int cx = -1; cx <= 1; cx += 2) boxY(g, vec3(c + X * (cx * 0.97f), z), X, vec3(0.04f, 0.6f, 0.04f), fc, paint);
                    // mesh sides (dark wire panels) and the load: lashing bars leaning, twistlocks in a heap
                    for (int cy = -1; cy <= 1; cy += 2)
                        quad(g, *g.m, vec3(c - X * 0.95f + Y * (cy * 0.575f), z0 + 0.08f), vec3(c + X * 0.95f + Y * (cy * 0.575f), z0 + 0.08f),
                             vec3(c + X * 0.95f + Y * (cy * 0.575f), z0 + 1.04f), vec3(c - X * 0.95f + Y * (cy * 0.575f), z0 + 1.04f), rgb(0.18f, 0.18f, 0.2f), paint,
                             vec3(Y * (float)cy, 0.f));
                    for (int b = 0; b < 5; b++)
                        rod(g, vec3(c + X * r.range(-0.8f, 0.8f) + Y * r.range(-0.4f, 0.4f), z0 + 0.1f),
                            vec3(c + X * r.range(-0.9f, 0.9f) + Y * r.range(-0.45f, 0.45f), z0 + r.range(0.6f, 1.2f)), 0.02f, 4, rgb(0.5f, 0.45f, 0.4f), paint);
                    boxY(g, vec3(c, z0 + 0.25f), X, vec3(0.6f, 0.35f, 0.18f), rgb(0.35f, 0.3f, 0.25f), paint);
                }
                collide(g, vec3(c, e.z + tiers * 0.575f), X, vec3(1.f, 0.6f, tiers * 0.575f));
            }
            break;
        }
        case 2: {
            // sign on a post (or two): e.text, kind p[0]: 0 white regulatory, 1 yellow warning, 2 blue terminal info
            if (!g.owns(e.c)) return;
            vec2 face = e.ax, rt = perp(-face);
            int kind = (int)e.p[0];
            float w = e.hx, hgt = e.hy;
            u32 bg = kind == 1 ? rgb(0.95f, 0.78f, 0.08f) : (kind == 2 ? rgb(0.08f, 0.25f, 0.55f) : rgb(0.93f));
            u32 fg = kind == 2 ? rgb(0.95f) : rgb(0.06f);
            float z0 = e.z + e.h;
            int posts = w > 1.3f ? 2 : 1;
            for (int k = 0; k < posts; k++) {
                vec2 pp = posts == 1 ? e.c : e.c + rt * ((k ? 1.f : -1.f) * w * 0.35f);
                cyl(g, vec3(pp - face * 0.05f, e.z), 0.045f, 0.04f, z0 + hgt, 6, rgb(0.6f), M(MAT_METAL_BRUSHED), true);
            }
            vec3 c(e.c, z0 + hgt * 0.5f);
            quad(g, *g.m, c - vec3(rt * (w * 0.5f), hgt * 0.5f), c + vec3(rt * (w * 0.5f), -hgt * 0.5f), c + vec3(rt * (w * 0.5f), hgt * 0.5f),
                 c + vec3(-rt * (w * 0.5f), hgt * 0.5f), bg, paint, vec3(face, 0.f));
            quad(g, *g.m, c - vec3(rt * (w * 0.5f), hgt * 0.5f) - vec3(face * 0.02f, 0.f), c + vec3(rt * (w * 0.5f), -hgt * 0.5f) - vec3(face * 0.02f, 0.f),
                 c + vec3(rt * (w * 0.5f), hgt * 0.5f) - vec3(face * 0.02f, 0.f), c + vec3(-rt * (w * 0.5f), hgt * 0.5f) - vec3(face * 0.02f, 0.f), rgb(0.55f),
                 paint, vec3(-face, 0.f));
            // lines of text separated by '|'
            std::vector<std::string> lines;
            {
                std::string cur;
                for (char ch : e.text) {
                    if (ch == '|') { lines.push_back(cur); cur.clear(); }
                    else cur += ch;
                }
                lines.push_back(cur);
            }
            float th = Min(hgt / (lines.size() * 1.5f + 0.3f), 0.22f);
            for (size_t i = 0; i < lines.size(); i++) {
                float tw = textAdvance(lines[i].c_str(), th, 0.3f);
                float scale = tw > w * 0.9f ? w * 0.9f / tw : 1.f;
                float tz = z0 + hgt * 0.5f + (lines.size() * 0.5f - i - 0.85f) * th * 1.5f;
                strokeText(g, *g.m, lines[i].c_str(), vec3(e.c + face * 0.01f - rt * (tw * scale * 0.5f), tz), vec3(rt, 0.f), vec3(0, 0, 1), th * scale,
                           th * scale * 0.14f, fg, paint, 0.f, 0.3f);
            }
            if (kind == 1) {
                // black border stripe along the top and bottom of warning boards
                for (int s = -1; s <= 1; s += 2)
                    quad(g, *g.m, c + vec3(-rt * (w * 0.46f), s * hgt * 0.44f) + vec3(face * 0.012f, 0.f), c + vec3(rt * (w * 0.46f), s * hgt * 0.44f) + vec3(face * 0.012f, 0.f),
                         c + vec3(rt * (w * 0.46f), s * hgt * 0.4f) + vec3(face * 0.012f, 0.f), c + vec3(-rt * (w * 0.46f), s * hgt * 0.4f) + vec3(face * 0.012f, 0.f),
                         rgb(0.06f), paint, vec3(face, 0.f));
            }
            break;
        }
        case 3: {
            // oil stains and wet patches (decals): irregular blobs over the area
            Rng r(e.seed);
            int n = (int)e.p[0];
            vec2 X = e.ax, Y = perp(e.ax);
            for (int k = 0; k < n; k++) {
                vec2 c = e.c + X * r.range(-e.hx, e.hx) + Y * r.range(-e.hy, e.hy);
                if (!g.owns(c)) continue;
                // oil stains (dark, matte) and shallow puddles (smooth: they mirror the sky)
                bool oil = r.chance(0.7f);
                float rad = oil ? r.range(0.25f, 0.9f) : r.range(0.5f, 1.5f);
                float sx = r.range(0.6f, 1.4f);
                u32 col = oil ? rgb(r.range(0.12f, 0.25f)) : rgb(0.42f, 0.44f, 0.45f);
                u32 mat = M(oil ? MAT_ASPHALT_OLD : MAT_GLASS);
                MeshData& d = *g.d;
                u32 b0 = d.addVertex(vec3(c, e.z + 0.012f) - g.org, vec3(0, 0, 1), vec3(1, 0, 0), c, col, mat);
                const int seg = 10;
                float ph = r.f() * kTwoPi;
                for (int s = 0; s <= seg; s++) {
                    float a = kTwoPi * s / seg;
                    float rr = rad * (0.75f + 0.35f * sinf(a * 3.f + ph) * 0.5f + 0.2f * hashToFloat(hash2i(k, s % seg)));
                    vec2 p = c + X * (cosf(a) * rr * sx) + Y * (sinf(a) * rr);
                    d.addVertex(vec3(p, e.z + 0.012f) - g.org, vec3(0, 0, 1), vec3(1, 0, 0), p, col, mat);
                }
                for (int s = 0; s < seg; s++) {
                    vec3 fn = cross(d.verts[b0 + 1 + s].pos - d.verts[b0].pos, d.verts[b0 + 2 + s].pos - d.verts[b0].pos);
                    if (fn.z > 0.f) d.tri(b0, b0 + 1 + s, b0 + 2 + s);
                    else d.tri(b0, b0 + 2 + s, b0 + 1 + s);
                }
            }
            break;
        }
        case 4: {
            // traffic cones and a couple of oil drums
            if (!g.owns(e.c)) return;
            Rng r(e.seed);
            vec2 X = e.ax, Y = perp(e.ax);
            for (int k = 0; k < 5; k++) prop(g, vec3(e.c + X * (k * 1.6f) + Y * r.range(-0.2f, 0.2f), e.z), r.f() * kTwoPi, 1.f, PROP_CONE);
            for (int k = 0; k < 2; k++) {
                vec2 p = e.c - X * (1.6f + k * 0.65f) + Y * r.range(-0.3f, 0.3f);
                cyl(g, vec3(p, e.z), 0.29f, 0.29f, 0.88f, 10, k ? rgb(0.1f, 0.3f, 0.55f) : rgb(0.75f, 0.2f, 0.1f), paint, true);
                collide(g, vec3(p, e.z + 0.44f), X, vec3(0.3f, 0.3f, 0.44f));
            }
            break;
        }
        default: {
            // block ID board on a pole (row and bay labels for the straddle drivers)
            if (!g.owns(e.c)) return;
            vec2 face = e.ax, rt = perp(-face);
            cyl(g, vec3(e.c, e.z), 0.1f, 0.08f, 5.2f, 8, rgb(0.85f, 0.7f, 0.1f), paint, false);
            vec3 c(e.c + face * 0.12f, e.z + 4.4f);
            quad(g, *g.m, c + vec3(-rt * 0.9f, -0.7f), c + vec3(rt * 0.9f, -0.7f), c + vec3(rt * 0.9f, 0.7f), c + vec3(-rt * 0.9f, 0.7f), rgb(0.95f, 0.78f, 0.1f), paint,
                 vec3(face, 0.f));
            float th = 0.85f;
            float tw = textAdvance(e.text.c_str(), th, 0.25f);
            strokeText(g, *g.m, e.text.c_str(), vec3(e.c + face * 0.13f - rt * (tw * 0.5f), e.z + 4.4f - th * 0.5f), vec3(rt, 0.f), vec3(0, 0, 1), th, 0.14f, rgb(0.05f),
                       paint, 0.f, 0.25f);
            collide(g, vec3(e.c, e.z + 2.6f), face, vec3(0.1f, 0.1f, 2.6f));
            break;
        }
    }
}

}  // namespace port_mesh
}  // namespace World
