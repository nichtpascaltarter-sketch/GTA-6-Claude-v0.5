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
    u32 mc = pkc(ln.mark), mat = M(MAT_METAL_PAINTED);
    float h = f.h, w = f.w;
    bool big = w > 9.f;
    bool doorRight = f.r.x * k.ax.x + f.r.y * k.ax.y > 0.f ? k.doorsPlus : !k.doorsPlus;   // is the door end on the right?
    float th = big ? 0.82f : 0.52f;
    float tw = textAdvance(ln.name, th, 0.28f);
    int kind = (int)(roll % 10u);   // 0-5 name + logo, 6-8 logo only, 9 nothing
    if (kind == 9) return;
    float logoS = big ? 0.5f : 0.36f;
    if (kind <= 5 && tw < w - 2.2f) {
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
                fbox(g, f, u0, u0 + 0.09f, v - 0.05f, v + 0.05f, dO - 0.005f, dO + 0.04f, barC, mat, 31);
            }
        // locking bars with guides, cam keepers top and bottom, handles
        for (int j = 0; j < 4; j++) {
            float u = barU[j];
            fbox(g, f, u - 0.014f, u + 0.014f, 0.04f, h - 0.06f, dO, dO + 0.035f, barC, mat, 1 | 2 | 4);
            for (float v : {0.08f, h - hdr - 0.1f}) fbox(g, f, u - 0.045f, u + 0.045f, v, v + 0.11f, dO - 0.01f, dO + 0.05f, barC, mat, 31);
            for (float v : {0.62f, 1.62f}) fbox(g, f, u - 0.04f, u + 0.04f, v, v + 0.06f, dO, dO + 0.045f, barC, mat, 31);
            float dir = (j & 1) ? -1.f : 1.f;
            float hu0 = dir > 0 ? u : u - 0.3f;
            fbox(g, f, hu0, hu0 + 0.3f, 1.08f, 1.12f, dO + 0.035f, dO + 0.06f, barC, mat, 31);
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
        paintText(g, f, cr, nr, 0.f, k.code, vec2(tu, tv), 0.12f, 0.02f, tc, mat, 0.3f);
        paintText(g, f, cr, nr, 0.f, serial, vec2(tu, tv - 0.19f), 0.12f, 0.02f, tc, mat, 0.3f);
        paintText(g, f, cr, nr, 0.f, type, vec2(tu, tv - 0.38f), 0.1f, 0.017f, tc, mat, 0.3f);
        // dangerous goods placard on some boxes
        if ((hash32(k.seed ^ 0xD6u) & 15u) == 0u) {
            vec2 cc(uR0 + lw * 0.5f, 0.9f);
            float s = 0.13f;
            vec2 top[3] = {cc + vec2(-s, 0), cc + vec2(s, 0), cc + vec2(0, s)};
            vec2 bot[3] = {cc + vec2(s, 0), cc + vec2(-s, 0), cc + vec2(0, -s)};
            paintPoly(g, f, cr, nr, 0.f, top, 3, pkc(vec3(0.85f, 0.12f, 0.08f)), mat);
            paintPoly(g, f, cr, nr, 0.f, bot, 3, pkc(vec3(0.92f)), mat);
        }
        return;
    }
    if (lod == CL_MID || lod == CL_FLATC) {
        ctrFrame(g, f, pt, lod == CL_MID ? CL_MID : CL_FLATC, sill, hdr, false);
        if (lod == CL_MID) {
            PCol cl[40], cr[40];
            int nl = corrColumns(cl, 40, uL0, uL1, lw / 4.f, kDFlat, kDFlat, false);
            int nr = corrColumns(cr, 40, uR0, uR1, lw / 4.f, kDFlat, kDFlat, false);
            corrStrip(g, f, cl, nl, sill, h - hdr, pt, mat);
            corrStrip(g, f, cr, nr, sill, h - hdr, pt, mat);
            fpanel(g, f, uL1 - 0.004f, uR0 + 0.004f, sill, h - hdr, kDFlat + 0.002f, dark, dark, mat);
        } else {
            fpanel(g, f, 0.f, w, 0.f, h, kDFlat, pkc(pt.at(w * 0.5f, false)), pkc(pt.at(w * 0.5f, true)), mat);
        }
        for (int j = 0; j < 4; j++) fpanel(g, f, barU[j] - 0.015f, barU[j] + 0.015f, 0.05f, h - 0.06f, kDFlat + 0.025f, barC, barC, mat);
        return;
    }
    fpanel(g, f, 0.f, w, 0.f, h, kDFlat, pkc(pt.at(w * 0.5f, false) * 0.55f), pkc(pt.at(w * 0.5f, true) * 0.6f), mat);
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
        ctrFrame(g, f, pt, CL_FLATC, kRailB, kRailT, false);
        fpanel(g, f, 0.f, w, 0.f, h, kDFlat, pkc(pt.at(w * 0.5f, false)), pkc(pt.at(w * 0.5f, true)), mat);
        return;
    }
    fpanel(g, f, 0.f, w, 0.f, h, kDFlat, pkc(pt.at(w * 0.5f, false) * 0.55f), pkc(pt.at(w * 0.5f, true) * 0.6f), mat);
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
        if (k.stacked) fpanel(g, f, 0.f, f.w, -kGap, 0.f, -0.05f, pkc(vec3(0.03f)), pkc(vec3(0.03f)), mat);
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
            int n = corrColumns(cc, 200, kPostW, f.w - kPostW, kPitch, hero ? kDOut : kDFlat, kDIn, hero);
            corrStrip(g, f, cc, n, kRailB, H - kRailT, pt, mat);
            if (k.marks & (1u << fi)) ctrMarks(g, f, k, cc, n, kDFlat, hash32(k.seed ^ (0xA5u + fi)));
        } else if (lod == CL_FLATC) {
            ctrFrame(g, f, pt, CL_FLATC, kRailB, kRailT, false);
            // posts and rails shaded into a panel split at the posts
            u32 postC = pkc(pt.base * 0.8f);
            fpanel(g, f, 0.f, kPostW, 0.f, H, kDFlat, postC, postC, mat);
            fpanel(g, f, f.w - kPostW, f.w, 0.f, H, kDFlat, postC, postC, mat);
            fpanel(g, f, kPostW, f.w - kPostW, 0.f, H, kDFlat, pkc(pt.at(f.w * 0.5f, false) * 0.85f), pkc(pt.at(f.w * 0.5f, true)), mat);
            if (k.marks & (1u << fi)) ctrMarks(g, f, k, nullptr, 0, kDFlat, hash32(k.seed ^ (0xA5u + fi)));
        } else {
            fpanel(g, f, 0.f, f.w, 0.f, H, kDFlat, pkc(pt.at(f.w * 0.5f, false) * 0.5f), pkc(pt.at(f.w * 0.5f, true) * 0.6f), mat);
        }
    }
    if (k.lod[4]) {
        // roof: panels along the length (bleached, dirt and rust patches), casting tops at the corners
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
        u32 castC = pkc(lerp(pt.base * 0.5f, kCast, 0.6f));
        for (int cx = -1; cx <= 1; cx += 2)
            for (int cy = -1; cy <= 1; cy += 2) {
                vec3 cc = b0 + X * (cx * (hx - kCastW * 0.5f)) + Y * (cy * (hy - 0.081f)) + Z * (H + 0.003f);
                quad(g, m, cc - X * (kCastW * 0.5f) - Y * 0.081f, cc + X * (kCastW * 0.5f) - Y * 0.081f, cc + X * (kCastW * 0.5f) + Y * 0.081f,
                     cc - X * (kCastW * 0.5f) + Y * 0.081f, castC, mat, Z);
            }
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
