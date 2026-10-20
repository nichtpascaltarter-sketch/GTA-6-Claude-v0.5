// Geometry toolkit for site meshes: primitives in world space, a stroke font for painted markings and signage,
// convex clipping to streaming cells and the generic pad (runway / apron / yard / plaza) renderer.
#include "sites.h"
#include "buildings.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace sitegeo {

const u32 kWhiteC = 0xffffffffu;
inline u32 rgb(float r, float g, float b, float a = 1.f) { return packRGBA8(r, g, b, a); }
inline u32 rgb(float v) { return packRGBA8(v, v, v, 1.f); }
inline u32 rgbv(vec3 c, float a = 1.f) { return packRGBA8(c.x, c.y, c.z, a); }
inline u32 M(MaterialId m, u32 param = 0) { return makeMat(m, param); }

// Emissive animation codes (world.hlsl, MAT_EMISSIVE param): pattern | phase << 4
enum EmAnim : u32 { EA_NONE = 0, EA_BLINK = 1, EA_CHASE = 2, EA_HUE = 3, EA_PULSE = 4, EA_RABBIT = 5, EA_NIGHT = 6, EA_SLOWBLINK = 7 };
inline u32 emMat(u32 anim = EA_NONE, u32 phase = 0) { return makeMat(MAT_EMISSIVE, anim == EA_NONE ? 0u : (anim | ((phase & 255u) << 4))); }

// Geometry sink for one streaming cell. All helper positions are world space; the cell origin is subtracted here.
struct G {
    MeshData* m = nullptr;   // opaque
    MeshData* d = nullptr;   // decals (depth biased)
    vec3 org;
    bool detail = true;
    int cx = 0, cy = 0;
    std::vector<CollisionBox>* col = nullptr;
    std::vector<PropInstance>* props = nullptr;
    std::vector<LightInstance>* lights = nullptr;
    bool owns(vec2 p) const { return inCell(p, cx, cy); }
};

// ------------------------------------------------------------------------------------------------ primitives
void quad(G& g, MeshData& m, vec3 a, vec3 b, vec3 c, vec3 d, u32 col, u32 mat, vec3 facing, float uvScale = 1.f) {
    float lu = length(b - a) * uvScale, lv = length(d - a) * uvScale;
    m.quadFacing(a - g.org, b - g.org, c - g.org, d - g.org, vec2(0, 0), vec2(lu, 0), vec2(lu, lv), vec2(0, lv), col, mat, facing);
}
// Double-sided thin panel
void panel2(G& g, vec3 a, vec3 b, vec3 c, vec3 d, u32 col, u32 mat) {
    vec3 n = normalize(cross(b - a, d - a));
    quad(g, *g.m, a, b, c, d, col, mat, n);
    quad(g, *g.m, a, b, c, d, col, mat, -n);
}
void box(G& g, vec3 c, vec3 ax, vec3 ay, vec3 he, u32 col, u32 mat, bool bottom = false) {
    vec3 az = normalize(cross(ax, ay));
    g.m->box(c - g.org, ax, ay, az, he, col, mat, bottom);
}
// Yaw-only box: dir = local x axis in the ground plane
void boxY(G& g, vec3 c, vec2 dir, vec3 he, u32 col, u32 mat, bool bottom = false) {
    vec2 d = normalize(dir);
    g.m->box(c - g.org, vec3(d, 0), vec3(perp(d), 0), vec3(0, 0, 1), he, col, mat, bottom);
}
// Axis-aligned box from min/max
void boxAA(G& g, vec3 mn, vec3 mx, u32 col, u32 mat, bool bottom = false) { g.m->boxAA(mn - g.org, mx - g.org, col, mat, bottom); }
// Beam between two points with a rectangular cross section (w across, h along `up`)
void beam(G& g, vec3 a, vec3 b, float w, float h, u32 col, u32 mat, vec3 up = vec3(0, 0, 1)) {
    vec3 x = b - a;
    float len = length(x);
    if (len < 1e-4f) return;
    x = x / len;
    vec3 y = cross(up, x);
    if (length2(y) < 1e-6f) y = anyPerp(x);
    y = normalize(y);
    vec3 z = cross(x, y);
    g.m->box((a + b) * 0.5f - g.org, x, y, z, vec3(len * 0.5f, w * 0.5f, h * 0.5f), col, mat, true);
}
// Round rod between two points (open tube)
void rod(G& g, vec3 a, vec3 b, float r, int seg, u32 col, u32 mat) {
    vec3 t = b - a;
    float len = length(t);
    if (len < 1e-4f) return;
    t = t / len;
    vec3 n = normalize(anyPerp(t));
    vec3 bn = cross(t, n);
    MeshData& m = *g.m;
    u32 base = (u32)m.verts.size();
    for (int k = 0; k <= seg; k++) {
        float an = kTwoPi * k / seg;
        vec3 dir = n * cosf(an) + bn * sinf(an);
        m.addVertex(a + dir * r - g.org, dir, t, vec2((float)k / seg * kTwoPi * r, 0), col, mat);
        m.addVertex(b + dir * r - g.org, dir, t, vec2((float)k / seg * kTwoPi * r, len), col, mat);
    }
    for (int k = 0; k < seg; k++) {
        u32 i0 = base + k * 2;
        m.quadIdx(i0, i0 + 2, i0 + 3, i0 + 1);
    }
}
void cyl(G& g, vec3 base, float r0, float r1, float h, int seg, u32 col, u32 mat, bool cap = true) {
    g.m->cylinder(base - g.org, r0, r1, h, seg, col, mat, cap);
}
// Surface of revolution around the vertical axis through `base`. prof: (radius, height) from bottom to top.
void lathe(G& g, vec3 base, const std::vector<vec2>& prof, int seg, u32 col, u32 mat, bool capTop = true, float a0 = 0.f) {
    MeshData& m = *g.m;
    size_t n = prof.size();
    if (n < 2) return;
    u32 start = (u32)m.verts.size();
    float v = 0.f;
    for (size_t i = 0; i < n; i++) {
        vec2 d = i + 1 < n ? prof[i + 1] - prof[i] : prof[i] - prof[i - 1];
        if (i > 0 && i + 1 < n) d = prof[i + 1] - prof[i - 1];
        // profile normal (outward in r, up in z)
        vec2 pn = normalize(vec2(d.y, -d.x));
        if (i > 0) v += length(prof[i] - prof[i - 1]);
        for (int k = 0; k <= seg; k++) {
            float an = a0 + kTwoPi * k / seg;
            vec3 radial(cosf(an), sinf(an), 0);
            vec3 nrm = normalize(radial * pn.x + vec3(0, 0, pn.y));
            vec3 tan(-sinf(an), cosf(an), 0);
            m.addVertex(base + radial * prof[i].x + vec3(0, 0, prof[i].y) - g.org, nrm, tan, vec2((float)k / seg * kTwoPi * Max(prof[i].x, 0.5f), v), col, mat);
        }
    }
    for (size_t i = 0; i + 1 < n; i++)
        for (int k = 0; k < seg; k++) {
            u32 a = start + (u32)(i * (seg + 1) + k), b = a + 1, c = a + seg + 1, d = c + 1;
            m.quadIdx(a, b, d, c);
        }
    if (capTop && prof.back().x > 0.01f) {
        u32 c = m.addVertex(base + vec3(0, 0, prof.back().y) - g.org, vec3(0, 0, 1), vec3(1, 0, 0), vec2(0, 0), col, mat);
        u32 first = (u32)m.verts.size();
        for (int k = 0; k <= seg; k++) {
            float an = a0 + kTwoPi * k / seg;
            vec3 p = base + vec3(cosf(an) * prof.back().x, sinf(an) * prof.back().x, prof.back().y);
            m.addVertex(p - g.org, vec3(0, 0, 1), vec3(1, 0, 0), vec2(cosf(an), sinf(an)) * prof.back().x, col, mat);
        }
        for (int k = 0; k < seg; k++) m.tri(c, first + k, first + k + 1);
    }
}
// Flat polygon (CCW or CW, triangulated) at height z
void polyFlat(G& g, MeshData& m, const std::vector<vec2>& pts, float z, u32 col, u32 mat, vec3 normal = vec3(0, 0, 1)) {
    if (pts.size() < 3) return;
    std::vector<vec3> p3;
    p3.reserve(pts.size());
    for (auto& p : pts) p3.push_back(vec3(p, z) - g.org);
    m.polygon(p3, normal, col, mat, 1.f);
    // world-space uv for tiling materials (polygon() used cell-relative xy)
    size_t n = pts.size();
    for (size_t i = 0; i < n; i++) m.verts[m.verts.size() - n + i].uv = pts[i];
}
// MAT_PAVERS lays 2:1 bricks, 6 x 12 per texture tile (2 uv units): at kPaverUV uv units per metre a brick is 20 x 10 cm.
constexpr float kPaverUV = 1.f / 0.6f;
// Plaza pavers: a greyer, less saturated brick than the texture's terracotta (vertex tint over MAT_PAVERS)
inline u32 paverTint() { return rgb(0.84f, 0.88f, 0.94f); }
// Paver uvs for horizontal vertices [v0, end) of m, anchored to the world so the bond runs on across cells (wrapped every
// 1200 m, a whole number of texture tiles, to keep the uvs small)
void paverUV(const G& g, MeshData& m, size_t v0) {
    vec2 w0(floorf(g.org.x / 1200.f) * 1200.f, floorf(g.org.y / 1200.f) * 1200.f);
    for (size_t i = v0; i < m.verts.size(); i++) m.verts[i].uv = (m.verts[i].pos.xy() + g.org.xy() - w0) * kPaverUV;
}
// Extruded prism: walls (outward) + optional roof. fp CCW from above.
void prism(G& g, const std::vector<vec2>& fp, float z0, float z1, u32 wallCol, u32 wallMat, u32 roofCol, u32 roofMat, bool roof = true) {
    int n = (int)fp.size();
    float u = 0;
    for (int i = 0; i < n; i++) {
        vec2 a = fp[i], b = fp[(i + 1) % n];
        float len = length(b - a);
        if (len < 1e-3f) continue;
        vec3 p0 = vec3(a, z0) - g.org, p1 = vec3(b, z0) - g.org;
        vec3 up(0, 0, z1 - z0);
        vec2 on(b.y - a.y, a.x - b.x);
        g.m->quadFacing(p0, p1, p1 + up, p0 + up, vec2(u, z0), vec2(u + len, z0), vec2(u + len, z1), vec2(u, z1), wallCol, wallMat, vec3(on, 0));
        u += len;
    }
    if (roof) polyFlat(g, *g.m, fp, z1, roofCol, roofMat);
}
// Facade walls (procedural windows via the facade shader). v measured from vBase.
void facadeRing(G& g, const std::vector<vec2>& fp, float z0, float z1, float vBase, u32 facadeId, float bay, u32 col = kWhiteC) {
    int n = (int)fp.size();
    for (int i = 0; i < n; i++) {
        vec2 a = fp[i], b = fp[(i + 1) % n];
        float len = length(b - a);
        if (len < 0.05f) continue;
        float bays = Max(1.f, roundf(len / bay));
        float uLen = bays * bay;
        float u0 = (float)i * 1000.f;
        vec3 p0 = vec3(a, z0) - g.org, p1 = vec3(b, z0) - g.org;
        vec3 up(0, 0, z1 - z0);
        vec2 on(b.y - a.y, a.x - b.x);
        g.m->quadFacing(p0, p1, p1 + up, p0 + up, vec2(u0, z0 - vBase), vec2(u0 + uLen, z0 - vBase), vec2(u0 + uLen, z1 - vBase), vec2(u0, z1 - vBase), col,
                        makeMat(MAT_FACADE, facadeId), vec3(on, 0));
    }
}
std::vector<vec2> rectPoly(vec2 c, vec2 ax, float hx, float hy) {
    vec2 ay = perp(ax);
    return {c - ax * hx - ay * hy, c + ax * hx - ay * hy, c + ax * hx + ay * hy, c - ax * hx + ay * hy};
}
std::vector<vec2> circleFP(vec2 c, float r, int seg, float a0 = 0.f) {
    std::vector<vec2> p;
    for (int i = 0; i < seg; i++) {
        float a = a0 + kTwoPi * i / seg;
        p.push_back(c + vec2(cosf(a), sinf(a)) * r);
    }
    return p;
}
std::vector<vec2> ellipseFP(vec2 c, vec2 ax, float rx, float ry, int seg) {
    std::vector<vec2> p;
    vec2 ay = perp(ax);
    for (int i = 0; i < seg; i++) {
        float a = kTwoPi * i / seg;
        p.push_back(c + ax * (cosf(a) * rx) + ay * (sinf(a) * ry));
    }
    return p;
}
void collide(G& g, vec3 c, vec2 ax, vec3 he) {
    if (!g.col) return;
    CollisionBox b;
    b.c = c;
    b.ax = normalize(ax);
    b.he = he;
    g.col->push_back(b);
}
void light(G& g, vec3 p, vec3 color, float radius, u8 type, vec3 dir = vec3(0), float cone = 0.f) {
    if (!g.lights) return;
    LightInstance li;
    li.pos = p;
    li.color = color;
    li.radius = radius;
    li.dir = dir;
    li.cone = cone;
    li.type = type;
    g.lights->push_back(li);
}
// Emissive lamp: a small box (LOD0) or an enlarged sprite-like box (far LOD) so it still reads as a point of light
void lamp(G& g, vec3 p, float size, vec3 color, float strength = 0.8f, u32 anim = EA_NONE, u32 phase = 0) {
    float s = g.detail ? size : Max(size * 2.5f, 0.9f);
    g.m->box(p - g.org, vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(s * 0.5f), rgbv(color, strength), emMat(anim, phase), true);
}
void prop(G& g, vec3 p, float yaw, float scale, PropType type, u8 variant = 0) {
    if (!g.props) return;
    PropInstance pi;
    pi.pos = p;
    pi.yaw = yaw;
    pi.scale = scale;
    pi.type = (u8)type;
    pi.variant = variant;
    pi.flags = 0;
    g.props->push_back(pi);
}

// ------------------------------------------------------------------------------------------------ stroke font
// Glyphs on a 6 x 9 unit grid (y up). Strokes separated by ';', points by ' ', coordinates "x,y".
const char* glyphDef(char ch) {
    switch (ch) {
        case '0': return "1,0 5,0 6,1 6,8 5,9 1,9 0,8 0,1 1,0";
        case '1': return "1,7 3,9 3,0;1,0 5,0";
        case '2': return "0,8 1,9 5,9 6,8 6,6 0,0.3 0,0 6,0";
        case '3': return "0,8 1,9 5,9 6,8 6,6 5,5 2,5;5,5 6,4 6,1 5,0 1,0 0,1";
        case '4': return "4.5,0 4.5,9 0,3 6,3";
        case '5': return "6,9 0.5,9 0,5 5,5 6,4 6,1 5,0 1,0 0,1";
        case '6': return "5,9 2,9 0,7 0,1 1,0 5,0 6,1 6,4 5,5 0,5";
        case '7': return "0,9 6,9 2,0";
        case '8': return "1,5 0,6 0,8 1,9 5,9 6,8 6,6 5,5 1,5 0,4 0,1 1,0 5,0 6,1 6,4 5,5";
        case '9': return "6,5 1,4 0,5 0,8 1,9 5,9 6,8 6,2 4,0 1,0";
        case 'A': return "0,0 0,6 3,9 6,6 6,0;0,4 6,4";
        case 'B': return "0,0 0,9 4.5,9 5.5,8 5.5,6 4.5,5 0,5;4.5,5 6,4 6,1 5,0 0,0";
        case 'C': return "6,8 5,9 1,9 0,8 0,1 1,0 5,0 6,1";
        case 'D': return "0,0 0,9 4,9 6,7 6,2 4,0 0,0";
        case 'E': return "6,9 0,9 0,0 6,0;0,4.5 4.5,4.5";
        case 'F': return "6,9 0,9 0,0;0,4.5 4.5,4.5";
        case 'G': return "6,8 5,9 1,9 0,8 0,1 1,0 5,0 6,1 6,4 3.5,4";
        case 'H': return "0,0 0,9;6,0 6,9;0,4.5 6,4.5";
        case 'I': return "1.5,9 4.5,9;3,9 3,0;1.5,0 4.5,0";
        case 'J': return "2,9 6,9;5,9 5,1 4,0 1,0 0,1 0,3";
        case 'K': return "0,0 0,9;6,9 0,3.5;2,5 6,0";
        case 'L': return "0,9 0,0 6,0";
        case 'M': return "0,0 0,9 3,4.5 6,9 6,0";
        case 'N': return "0,0 0,9 6,0 6,9";
        case 'O': return "1,0 5,0 6,1 6,8 5,9 1,9 0,8 0,1 1,0";
        case 'P': return "0,0 0,9 5,9 6,8 6,5.5 5,4.5 0,4.5";
        case 'Q': return "1,0 5,0 6,1 6,8 5,9 1,9 0,8 0,1 1,0;3.8,2.2 6.3,-0.6";
        case 'R': return "0,0 0,9 5,9 6,8 6,5.5 5,4.5 0,4.5;2.8,4.5 6,0";
        case 'S': return "6,8 5,9 1,9 0,8 0,6 1,5 5,4 6,3 6,1 5,0 1,0 0,1";
        case 'T': return "0,9 6,9;3,9 3,0";
        case 'U': return "0,9 0,1 1,0 5,0 6,1 6,9";
        case 'V': return "0,9 3,0 6,9";
        case 'W': return "0,9 1.5,0 3,5.5 4.5,0 6,9";
        case 'X': return "0,9 6,0;0,0 6,9";
        case 'Y': return "0,9 3,5 6,9;3,5 3,0";
        case 'Z': return "0,9 6,9 0,0 6,0";
        case '-': return "1,4.5 5,4.5";
        case '.': return "3,0 3,0.9";
        case ',': return "3,1 2.4,-1";
        case '\'': return "3,9 3,7";
        case '!': return "3,9 3,3;3,0.9 3,0";
        case '?': return "0,8 1,9 5,9 6,8 6,6 3,4 3,2.5;3,0.9 3,0";
        case '/': return "0,0 6,9";
        case ':': return "3,1.6 3,2.6;3,6.2 3,7.2";
        case '+': return "3,1.5 3,7.5;0,4.5 6,4.5";
        case '&': return "6,0 1,6 1,8 2,9 4,9 5,8 5,7 0,3 0,1 1,0 4,0 6,3";
        case '$': return "6,7.5 5,8.5 1,8.5 0,7.5 0,5.5 1,4.5 5,4.5 6,3.5 6,1.5 5,0.5 1,0.5 0,1.5;3,9.5 3,-0.5";
        case '%': return "0,0 6,9;0.5,9 1.5,9 1.5,7.5 0.5,7.5 0.5,9;4.5,1.5 5.5,1.5 5.5,0 4.5,0 4.5,1.5";
        case '#': return "2,0 2,9;4,0 4,9;0,3 6,3;0,6 6,6";
        default: return "";
    }
}

struct Stroke { std::vector<vec2> pts; };

void parseGlyph(char ch, std::vector<Stroke>& out) {
    out.clear();
    const char* s = glyphDef((char)toupper((unsigned char)ch));
    Stroke cur;
    while (*s) {
        if (*s == ';') {
            if (cur.pts.size() >= 2) out.push_back(cur);
            cur.pts.clear();
            s++;
            continue;
        }
        if (*s == ' ') { s++; continue; }
        char* end = nullptr;
        float x = strtof(s, &end);
        if (end == s) { s++; continue; }
        s = end;
        if (*s == ',') s++;
        float y = strtof(s, &end);
        s = end;
        cur.pts.push_back(vec2(x, y));
    }
    if (cur.pts.size() >= 2) out.push_back(cur);
}

// Parsed glyphs are cached once (thread-safe) so text-heavy cells stay cheap to generate
const std::vector<Stroke>& glyphStrokes(char ch) {
    static std::vector<Stroke> cache[128];
    static std::once_flag once;
    std::call_once(once, [] {
        for (int c = 32; c < 128; c++) parseGlyph((char)c, cache[c]);
    });
    unsigned char u = (unsigned char)toupper((unsigned char)ch);
    return cache[u < 128 ? u : 0];
}

float textAdvance(const char* txt, float height, float spacing = 0.35f) {
    float unit = height / 9.f;
    float w = 0;
    for (const char* c = txt; *c; c++) w += (*c == ' ' ? 4.5f : 6.f + 6.f * spacing) * unit;
    return w;
}

// Draws text as flat stroke quads (depth == 0, painted markings / sign faces) or as extruded boxes (3D letters).
// origin = bottom-left of the first glyph, right/up = unit axes of the text plane. Front side = cross(right, up).
float strokeText(G& g, MeshData& m, const char* txt, vec3 origin, vec3 right, vec3 up, float height, float strokeW, u32 col, u32 mat,
                 float depth = 0.f, float spacing = 0.35f) {
    float unit = height / 9.f;
    vec3 n = normalize(cross(right, up));
    float x = 0.f;
    for (const char* c = txt; *c; c++) {
        if (*c == ' ') { x += 4.5f * unit; continue; }
        const std::vector<Stroke>& strokes = glyphStrokes(*c);
        for (auto& st : strokes)
            for (size_t i = 0; i + 1 < st.pts.size(); i++) {
                vec2 a = st.pts[i] * unit + vec2(x, 0), b = st.pts[i + 1] * unit + vec2(x, 0);
                vec2 d = b - a;
                float len = length(d);
                if (len < 1e-4f) { d = vec2(0, 1); len = 0.f; }
                else d = d / len;
                vec2 e = perp(d) * (strokeW * 0.5f);
                vec2 ext = d * (strokeW * 0.5f);
                vec2 q[4] = {a - ext - e, b + ext - e, b + ext + e, a - ext + e};
                vec3 w[4];
                for (int k = 0; k < 4; k++) w[k] = origin + right * q[k].x + up * q[k].y;
                if (depth <= 0.f) {
                    m.quadFacing(w[0] - g.org, w[1] - g.org, w[2] - g.org, w[3] - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), col, mat, n);
                } else {
                    vec3 c3 = (w[0] + w[1] + w[2] + w[3]) * 0.25f + n * (depth * 0.5f);
                    vec3 ax3 = right * d.x + up * d.y;
                    vec3 ay3 = right * (-d.y) + up * d.x;
                    m.box(c3 - g.org, normalize(ax3), normalize(ay3), n, vec3(len * 0.5f + strokeW * 0.5f, strokeW * 0.5f, depth * 0.5f), col, mat, true);
                }
            }
        x += (6.f + 6.f * spacing) * unit;
    }
    return x;
}

// ------------------------------------------------------------------------------------------------ clipping
std::vector<vec2> clipConvex(const std::vector<vec2>& poly, vec2 mn, vec2 mx) {
    std::vector<vec2> out = poly, in;
    for (int e = 0; e < 4; e++) {
        in.swap(out);
        out.clear();
        size_t n = in.size();
        if (n == 0) break;
        for (size_t i = 0; i < n; i++) {
            vec2 a = in[i], b = in[(i + 1) % n];
            auto inside = [&](vec2 p) {
                switch (e) {
                    case 0: return p.x >= mn.x;
                    case 1: return p.x <= mx.x;
                    case 2: return p.y >= mn.y;
                    default: return p.y <= mx.y;
                }
            };
            auto isect = [&](vec2 p, vec2 q) {
                float t;
                switch (e) {
                    case 0: t = (mn.x - p.x) / (q.x - p.x); break;
                    case 1: t = (mx.x - p.x) / (q.x - p.x); break;
                    case 2: t = (mn.y - p.y) / (q.y - p.y); break;
                    default: t = (mx.y - p.y) / (q.y - p.y); break;
                }
                return lerp(p, q, t);
            };
            bool ia = inside(a), ib = inside(b);
            if (ia && ib) out.push_back(b);
            else if (ia && !ib) out.push_back(isect(a, b));
            else if (!ia && ib) {
                out.push_back(isect(a, b));
                out.push_back(b);
            }
        }
    }
    return out;
}
// Clip a segment to a box; returns false if nothing remains
bool clipSegment(vec2& a, vec2& b, vec2 mn, vec2 mx) {
    float t0 = 0.f, t1 = 1.f;
    vec2 d = b - a;
    for (int k = 0; k < 2; k++) {
        float p = k == 0 ? d.x : d.y;
        float lo = (k == 0 ? mn.x : mn.y) - (k == 0 ? a.x : a.y), hi = (k == 0 ? mx.x : mx.y) - (k == 0 ? a.x : a.y);
        if (fabsf(p) < 1e-8f) {
            if (lo > 0.f || hi < 0.f) return false;
            continue;
        }
        float ta = lo / p, tb = hi / p;
        if (ta > tb) std::swap(ta, tb);
        t0 = Max(t0, ta);
        t1 = Min(t1, tb);
        if (t0 > t1) return false;
    }
    vec2 na = a + d * t0, nb = a + d * t1;
    a = na;
    b = nb;
    return true;
}
void cellBounds(const G& g, vec2& mn, vec2& mx) {
    mn = cellOrigin(g.cx, g.cy);
    mx = mn + vec2(kCellSize);
}

// Paint a flat rectangle (decal) on a surface at height z: center, axis, half extents
void paintRect(G& g, vec2 c, vec2 ax, float hx, float hy, float z, u32 col, u32 mat) {
    vec2 ay = perp(ax);
    vec3 p0(c - ax * hx - ay * hy, z), p1(c + ax * hx - ay * hy, z), p2(c + ax * hx + ay * hy, z), p3(c - ax * hx + ay * hy, z);
    g.d->quadFacing(p0 - g.org, p1 - g.org, p2 - g.org, p3 - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), col, mat, vec3(0, 0, 1));
}
// Painted line segment of width w
void paintLine(G& g, vec2 a, vec2 b, float w, float z, u32 col, u32 mat) {
    vec2 d = b - a;
    float len = length(d);
    if (len < 1e-3f) return;
    paintRect(g, (a + b) * 0.5f, d / len, len * 0.5f, w * 0.5f, z, col, mat);
}
// Dashed painted line (dash / gap lengths), only the parts whose centers fall inside the cell
void paintDashed(G& g, vec2 a, vec2 b, float w, float z, float dash, float gap, u32 col, u32 mat) {
    vec2 d = b - a;
    float len = length(d);
    if (len < 1e-3f) return;
    d = d / len;
    for (float s = 0.f; s < len; s += dash + gap) {
        float e = Min(len, s + dash);
        vec2 p0 = a + d * s, p1 = a + d * e;
        if (!g.owns((p0 + p1) * 0.5f)) continue;
        paintLine(g, p0, p1, w, z, col, mat);
    }
}

// ------------------------------------------------------------------------------------------------ generic pads
void padStyle(const Pad& p, u32& col, u32& mat, float& dz) {
    col = p.color;
    dz = 0.f;
    switch (p.kind) {
        case PAD_RUNWAY: mat = M(MAT_ASPHALT); dz = 0.03f; break;
        case PAD_SHOULDER: mat = M(MAT_ASPHALT_OLD); dz = 0.015f; break;
        case PAD_TAXIWAY: mat = M(MAT_ASPHALT); dz = 0.02f; break;
        case PAD_APRON: mat = M(MAT_CONCRETE); dz = 0.f; if (col == kWhiteC) col = rgb(1.12f, 1.12f, 1.1f); break;
        case PAD_SERVICE: mat = M(MAT_ASPHALT_OLD); break;
        case PAD_PARKING: mat = M(MAT_ASPHALT_OLD); dz = 0.01f; break;
        case PAD_YARD: mat = M(MAT_CONCRETE); break;
        case PAD_PLAZA: mat = M(MAT_PAVERS); if (col == kWhiteC) col = paverTint(); break;
        case PAD_DECK: mat = M(MAT_WOOD); break;
        case PAD_TURF: mat = M(MAT_GRASS); break;
        case PAD_RAMP: mat = M(MAT_CONCRETE); break;
        case PAD_SAND: mat = M(MAT_SAND); break;
        default: mat = M(MAT_CONCRETE); break;
    }
}

void drawPads(G& g) {
    const SiteSet& S = *gSites;
    if (S.padHash.empty()) return;
    vec2 mn, mx;
    cellBounds(g, mn, mx);
    std::vector<int> cand;
    int res = S.padRes;
    int x0 = Clamp((int)((mn.x + kWorldHalf) / SiteSet::kPadCell), 0, res - 1), x1 = Clamp((int)((mx.x - 0.01f + kWorldHalf) / SiteSet::kPadCell), 0, res - 1);
    int y0 = Clamp((int)((mn.y + kWorldHalf) / SiteSet::kPadCell), 0, res - 1), y1 = Clamp((int)((mx.y - 0.01f + kWorldHalf) / SiteSet::kPadCell), 0, res - 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            for (int i : S.padHash[(size_t)y * res + x]) cand.push_back(i);
    std::sort(cand.begin(), cand.end());
    cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
    for (int pi : cand) {
        const Pad& p = S.pads[pi];
        if (!p.drawn) continue;
        std::vector<vec2> rect = rectPoly(p.c, p.ax, p.hx, p.hy);
        std::vector<vec2> poly = clipConvex(rect, mn, mx);
        if (poly.size() < 3) continue;
        u32 col, mat;
        float dz;
        padStyle(p, col, mat, dz);
        float z = p.z + dz;
        size_t v0 = g.m->verts.size();
        polyFlat(g, *g.m, poly, z, col, mat);
        bool pavers = p.kind == PAD_PLAZA;
        if (pavers) paverUV(g, *g.m, v0);
        if (!p.skirt && !(p.flags & 2)) continue;
        // Edge skirts (or raised curb faces) along the pad edges that fall inside this cell
        for (int k = 0; k < 4; k++) {
            vec2 a = rect[k], b = rect[(k + 1) % 4];
            if (!clipSegment(a, b, mn, mx)) continue;
            if (length2(b - a) < 0.01f) continue;
            vec2 on = normalize(vec2(b.y - a.y, a.x - b.x));
            float drop = (p.flags & 2) ? 0.15f + 0.3f : 0.55f;
            u32 smat = (p.flags & 2) ? M(MAT_CURB) : mat;
            quad(g, *g.m, vec3(a, z), vec3(b, z), vec3(b, z - drop), vec3(a, z - drop), col, smat, vec3(on, 0), (pavers && !(p.flags & 2)) ? kPaverUV : 1.f);
        }
    }
}

}  // namespace sitegeo
}  // namespace World
