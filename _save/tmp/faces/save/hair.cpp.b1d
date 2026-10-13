// Hair and facial hair.
//
// Scalp hair is a shell over the head grid (garment engine with MAT_HAIR) whose coverage is bounded by a hairline
// curve in (theta, phi) head space (with temple recession for older men and an optional fringe) and whose thickness
// profile defines the style (buzz, short, afro, slicked, quiff...). The thickness tapers to zero at the hairline
// and the scalp under it is tinted with the hair color so the edge blends. Long styles add draped geometry: curtains
// (long straight, bob) and ponytails/braids are swept down the back and pushed out of the body's signed distance
// field so they rest on the neck, shoulders and upper back; their lower parts are skinned to the neck/chest so they
// stay on the back when the head turns. Braids and locs are ropes laid along combing paths over the scalp that hang
// free below it (cornrows, box braids, long locs, twists; long curls are coiled ropes over a curtain). Strand cards
// carry the soft outline of the other styles. Facial hair uses the same shell over the lower face (mustache, goatee,
// short and full beards) or a vertex color tint (stubble).
#include "anim_internal.h"
#include <algorithm>
#include <functional>

namespace Anim {
namespace detail {

// Hairline elevation (radians) as a function of |theta| (0 = front) for this character.
struct HairParams {
    int style = 0;
    float recession = 0.f;    // temple recession (0..1)
    float crownBald = 0.f;    // male pattern crown/top thinning (0..1)
    bool fringe = false;
    bool coversEars = false;
    float volume = 1.f;
    float partX = 0.f;        // head-space x of the parting (long hair / bob), 0 = centre
    float hairlineOff = 0.f;  // forehead height: hairline raised (+) / lowered (-) at the front (degrees)
    float fade = 0.f;         // short men's cuts: sides and back clippered down towards the hairline (0 none .. 1 skin)
    bool lineUp = false;      // edge-up: a crisp, straight hairline at the front and temples
    int rope = -1;            // braids / locs: RopeKind (-1: other styles)
    float ropeLen = 0.f;      // braids / locs: length hanging below the head (m); twists: their length
    bool ropeLong = false;    // cornrows: long braids from the rows (feed-in braids) instead of short tails
    vec3 ropeCol, ropeTip;    // braids / locs colour (extensions may differ from the natural hair) and at the ends
    float curlLen = 0.f;      // curly: length of the coils falling from the crown's volume (0: a rounded afro)
    vec3 col;
};

// Braided and locked styles (HAIR_BRAIDS / HAIR_LOCS): built from ropes, see "Braids and locs" below.
enum RopeKind { ROPE_CORNROWS = 0, ROPE_BOX, ROPE_LOCS, ROPE_TWISTS };

static HairParams hairParams(const BuildCtx& c) {
    const CharacterDesc& d = *c.d;
    HairParams h;
    h.style = Clamp(d.hairStyle, 0, HAIR_STYLE_COUNT - 1);
    Rng r(hash32(d.seed * 97u + 29u));
    bool male = d.gender == MALE;
    float rec = male ? Saturate(sstep(0.15f, 0.9f, d.age) * r.range(0.3f, 1.1f)) : 0.f;
    h.recession = rec;
    h.crownBald = male && d.age > 0.45f && r.chance(0.35f) ? sstep(0.45f, 0.9f, d.age) : 0.f;
    if (h.style == HAIR_BUZZ || h.style == HAIR_BRAIDS || h.style == HAIR_CURLY) h.crownBald *= 0.3f;
    h.fringe = (h.style == HAIR_BOB || h.style == HAIR_LONG) && r.chance(0.35f);
    h.coversEars = h.style == HAIR_LONG || h.style == HAIR_BOB || (h.style == HAIR_CURLY && !male);
    h.volume = r.range(0.9f, 1.12f);
    h.partX = r.chance(0.45f) ? 0.f : (r.chance(0.5f) ? -1.f : 1.f) * r.range(0.015f, 0.028f);
    h.col = d.hairColor;
    h.hairlineOff = c.D->foreheadH;
    {
        // fades and line-ups (own stream: the draws above keep their values)
        Rng f(hash32(d.seed * 0x3C6EF372u + 0x1Bu));
        const bool shortCut = h.style == HAIR_SHORT || h.style == HAIR_BUZZ || h.style == HAIR_QUIFF || h.style == HAIR_SLICKED;
        const float youth = 1.f - sstep(0.25f, 0.7f, d.age);
        if (male && shortCut && f.chance(0.25f + 0.4f * youth)) {
            h.fade = f.range(0.55f, 1.f);
            h.lineUp = f.chance(0.55f);
        }
    }
    if (h.style == HAIR_CURLY) {
        // curls: long coils falling to the jaw or the shoulders for most women (coily hair is more often worn as a
        // rounded afro), now and then a man's mop of curls (own stream)
        Rng q(hash32(d.seed * 0x1656667Bu + 0x29u));
        const float pLong = male ? 0.12f : (d.ancestry == 1 ? 0.35f : 0.8f);
        const float lenF = q.f();
        if (q.chance(pLong)) h.curlLen = male ? Lerp(0.05f, 0.11f, lenF) : Lerp(0.09f, 0.22f, lenF);
    }
    if (h.style == HAIR_BRAIDS || h.style == HAIR_LOCS) {
        // braids and locs: the variant, length and colour (own stream; braidsAreCornrows draws first from its own)
        Rng q(hash32(d.seed * 0x61C88647u + 0x3Du));
        const bool fem = !male;
        h.crownBald = 0.f;
        h.recession *= 0.4f;
        h.ropeCol = h.col;
        if (h.style == HAIR_BRAIDS) {
            h.rope = braidsAreCornrows(d.seed, (int)d.gender, d.hat) ? ROPE_CORNROWS : ROPE_BOX;
            h.ropeLong = fem && q.chance(0.45f);
            float lenF = q.f();
            h.ropeLen = h.rope == ROPE_BOX ? (fem ? Lerp(0.2f, 0.44f, lenF) : Lerp(0.1f, 0.22f, lenF))
                                           : (h.ropeLong ? Lerp(0.18f, 0.36f, lenF) : Lerp(0.012f, 0.024f, lenF));
            // braiding hair (extensions) in a colour of its own: burgundy, honey blonde, copper or jet black
            static const float kExt[4][3] = {{0.36f, 0.07f, 0.09f}, {0.62f, 0.45f, 0.24f}, {0.5f, 0.24f, 0.11f}, {0.03f, 0.03f, 0.035f}};
            const bool ext = fem && h.rope == ROPE_BOX && q.chance(0.3f);
            int e = q.irange(0, 3);
            if (ext) h.ropeCol = srgbToLinear(vec3(kExt[e][0], kExt[e][1], kExt[e][2]));
            h.ropeTip = h.ropeCol;
        } else {
            h.rope = q.chance(fem ? 0.75f : 0.45f) ? ROPE_LOCS : ROPE_TWISTS;
            float lenF = q.f();
            h.ropeLen = h.rope == ROPE_LOCS ? (fem ? Lerp(0.18f, 0.42f, lenF) : Lerp(0.12f, 0.34f, lenF)) : Lerp(0.045f, 0.085f, lenF);
            // sun-lightened ends on some (warmer and lighter towards the tips)
            const bool faded = q.chance(0.4f);
            h.ropeTip = faded ? lerp(h.col, vmax(h.col * 2.4f, srgbToLinear(vec3(0.3f, 0.17f, 0.08f))), 0.6f) : h.col;
            // twists often come with the sides faded
            bool fadeSides = male && h.rope == ROPE_TWISTS && q.chance(0.5f);
            if (fadeSides) h.fade = q.range(0.6f, 1.f);
        }
    }
    return h;
}

// Fade factor of a short cut at a scalp point: 0 where it is clippered to the skin (the lowest sides and nape) .. 1 at
// full length (the top); the fade line sits around the temples.
static float fadeKeep(const HairParams& h, float at, float ph) {
    if (h.fade <= 0.f) return 1.f;
    const float deg = kDegToRad;
    float side = sstep(48.f * deg, 75.f * deg, at);   // the front keeps its length; sides and back fade
    float k = sstep(-8.f * deg, 34.f * deg, ph);       // from the hairline up to the temple line
    return 1.f - h.fade * side * (1.f - k);
}

static float hairlinePhi(const HairParams& h, float at) {
    // (|theta| deg, phi deg) control points: forehead, temples, sideburn, over the ear, nape
    static const float T[][2] = {{0, 44}, {20, 43}, {32, 40}, {45, 33}, {58, 22}, {66, 9}, {72, 2}, {78, 9}, {86, 19}, {96, 23},
                                 {106, 18}, {116, 4}, {128, -8}, {145, -16}, {180, -20}};
    const int n = (int)(sizeof(T) / sizeof(T[0]));
    float a = at * kRadToDeg;
    float ph = T[n - 1][1];
    for (int i = 0; i + 1 < n; i++)
        if (a <= T[i + 1][0]) {
            float t = (a - T[i][0]) / (T[i + 1][0] - T[i][0]);
            ph = Lerp(T[i][1], T[i + 1][1], t);
            break;
        }
    // temple recession (M shape) and a slightly higher front for older men; the forehead height moves the front
    ph += h.recession * (14.f * bump(a, 34.f, 14.f) + 5.f * bump(a, 0.f, 25.f));
    ph += h.hairlineOff * (1.f - sstep(55.f, 110.f, a));
    if (h.coversEars && a > 72.f && a < 120.f) ph = Min(ph, -6.f + 10.f * bump(a, 72.f, 6.f));
    if (h.fringe && a < 42.f) ph = Min(ph, 27.f + 9.f * Sq(a / 42.f));
    return ph * kDegToRad;
}

// Hair thickness over a head grid vertex (before hairline tapering).
static float styleThickness(const BuildCtx& c, const HairParams& h, const BVert& v) {
    const float hs = c.D->headS;
    float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
    float ph = v.pb;
    const float deg = kDegToRad;
    float top = sstep(15.f * deg, 70.f * deg, ph);
    float back = sstep(95.f * deg, 160.f * deg, at);
    float front = sstep(70.f * deg, 15.f * deg, at);
    float nape = sstep(12.f * deg, -18.f * deg, ph) * back;
    float T = 0.f;
    switch (h.style) {
        case HAIR_BALD: return 0.f;
        case HAIR_BUZZ: T = 0.0025f; break;
        // (volume: real hair stands 1.5-3 cm off the crown; thinner shells read as a helmet)
        case HAIR_SHORT: T = Lerp(0.008f, 0.021f, top) * (1.f - 0.5f * nape) + 0.005f * front * top; break;
        case HAIR_QUIFF: T = Lerp(0.005f, 0.016f, top) + 0.03f * front * bump(ph, 50.f * deg, 16.f * deg); T *= 1.f - 0.4f * nape; break;
        // slicked back with height: lifted over the forehead (a pompadour's roll), flat and close at the sides
        case HAIR_SLICKED: T = (Lerp(0.0055f, 0.013f, top) + 0.022f * front * bump(ph, 54.f * deg, 18.f * deg)) * (1.f - 0.3f * nape); break;
        case HAIR_PONYTAIL: case HAIR_BUN: T = Lerp(0.005f, 0.0105f, top); break;
        case HAIR_BRAIDS: case HAIR_LOCS: {
            // the hair under the ropes (box braids and locs: the sections underneath, lumpy); cornrows have no shell,
            // this is the height of their rows (hats clear it)
            vec3 q = v.bp * 160.f;
            float lumps = 0.5f + 0.25f * (sinf(q.x + 1.7f * sinf(q.z * 0.7f)) + sinf(q.y * 1.3f + q.z));
            switch (h.rope) {
                case ROPE_CORNROWS: T = 0.0068f; break;
                case ROPE_BOX: T = 0.0068f * (0.85f + 0.3f * lumps); break;
                case ROPE_LOCS: T = 0.0098f * (0.8f + 0.4f * lumps) * Lerp(0.7f, 1.f, top); break;
                default: T = 0.0042f * (0.85f + 0.3f * lumps); break;
            }
            break;
        }
        case HAIR_LONG: T = Lerp(0.011f, 0.019f, top) + 0.006f * back; break;
        case HAIR_BOB: T = Lerp(0.013f, 0.021f, top) + 0.007f * back; break;
        case HAIR_CURLY: {
            // afro: outer surface approximates an ellipsoid around the skull
            vec3 A = c.head.origin + vec3(0, -0.014f, 0.085f) * hs;
            // (under long coils the crown is less of a ball: the coils carry the volume)
            vec3 rad = vec3(0.112f, 0.125f, 0.112f) * (hs * h.volume * (c.d->gender == FEMALE ? 1.05f : 0.93f) * (h.curlLen > 0.f ? 0.9f : 1.f));
            vec3 d = normalize(v.bp - c.head.C);
            vec3 o = c.head.C - A;
            vec3 dd(d.x / rad.x, d.y / rad.y, d.z / rad.z), oo(o.x / rad.x, o.y / rad.y, o.z / rad.z);
            float a = dot(dd, dd), b = dot(oo, dd), cc = dot(oo, oo) - 1.f;
            float disc = b * b - a * cc;
            float tOut = disc > 0.f ? (-b + sqrtf(disc)) / a : 0.f;
            float dist = length(v.bp - c.head.C);
            T = Max(0.005f, tOut - dist);
            T *= 1.f - 0.55f * front * sstep(60.f * deg, 30.f * deg, ph);
            T = Min(T, 0.075f * hs);
            // curls
            vec3 q = v.bp * 90.f;
            T *= 0.9f + 0.2f * (0.5f + 0.5f * sinf(q.x) * sinf(q.y + 1.3f) * sinf(q.z + 2.1f));
            break;
        }
        default: T = 0.008f; break;
    }
    if (h.crownBald > 0.f) T *= 1.f - h.crownBald * sstep(50.f * deg, 72.f * deg, ph);
    T *= Lerp(0.12f, 1.f, fadeKeep(h, at, ph));
    return T * hs;
}

float hairVolumeAt(const BuildCtx& c, const BVert& v) {
    HairParams h = hairParams(c);
    if (h.style == HAIR_BALD) return 0.f;
    float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
    if (v.pb < hairlinePhi(h, at)) return 0.f;
    return styleThickness(c, h, v);
}

// Smooth 3D value noise in [0, 1] (cell size 1 / freq) for the hairline's wander.
static float hairNoise3(vec3 p, float freq, u32 seed) {
    vec3 q = p * freq;
    float fx = floorf(q.x), fy = floorf(q.y), fz = floorf(q.z);
    int ix = (int)fx, iy = (int)fy, iz = (int)fz;
    float tx = sstep(q.x - fx), ty = sstep(q.y - fy), tz = sstep(q.z - fz);
    auto hh = [&](int x, int y, int z) { return hashToFloat(hash32((u32)x * 73856093u ^ (u32)y * 19349663u ^ (u32)z * 83492791u ^ seed)); };
    float a = Lerp(Lerp(hh(ix, iy, iz), hh(ix + 1, iy, iz), tx), Lerp(hh(ix, iy + 1, iz), hh(ix + 1, iy + 1, iz), tx), ty);
    float b = Lerp(Lerp(hh(ix, iy, iz + 1), hh(ix + 1, iy, iz + 1), tx), Lerp(hh(ix, iy + 1, iz + 1), hh(ix + 1, iy + 1, iz + 1), tx), ty);
    return Lerp(a, b, tz);
}

// Coverage of the hair region (meters, positive inside) with a gently irregular hairline: the edge wanders smoothly
// (a few millimetres over a centimetre or two, plus a finer ripple) instead of jumping from vertex to vertex, which cut
// the shell's edge into spikes.
static float hairCoverage(const BuildCtx& c, const HairParams& h, const BVert& v) {
    if (v.part != PART_HEAD || v.pc < 1.2f) return -1.f;
    float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
    float cv = (v.pb - hairlinePhi(h, at)) * 0.1f * c.D->headS;
    // (a line-up's front and temples are cut straight: no irregularity there)
    float jag = h.lineUp ? Lerp(0.0004f, 0.004f, sstep(60.f * kDegToRad, 85.f * kDegToRad, at)) : 0.004f;
    const u32 ns = hash32(c.d->seed * 0x3B9AC9FBu + 0x2Du);
    float wander = (hairNoise3(v.bp, 70.f, ns) - 0.5f) * 1.1f + (hairNoise3(v.bp, 190.f, ns ^ 0x5bd1e995u) - 0.5f) * 0.45f;
    cv += wander * jag;
    if (h.crownBald > 0.7f) {
        // horseshoe pattern: top of the head bald
        cv = Min(cv, (62.f * kDegToRad - v.pb) * 0.1f * c.D->headS + 0.01f * (1.f - h.crownBald));
    }
    return cv;
}

// How far in from the hairline (coverage, m) the hair reaches its full thickness T: thick styles build up over a few
// centimetres, so the front of the hair lies back from the hairline instead of standing up in a wig-like wall.
static float hairRampLen(float T) { return 0.012f + Min(1.3f * T, 0.013f); }   // (at most 2.5 cm: a quiff or an afro keeps its front)

// ------------------------------------------------------------------------------------------------
// Strand cards (conventions: CardKind in anim_internal.h). The shell stays underneath for coverage; cards lie on it
// in two layers combed along the style's flow, roots inside the shell and tips on / just above its surface.

// Head surface along (theta, phi) from the grid centre: point and outward normal.
static void headSurf(const BuildCtx& c, float th, float ph, vec3& p, vec3& n, float tHint = 0.f) {
    const HeadInfo& H = c.head;
    vec3 dir(cosf(ph) * sinf(th), cosf(ph) * cosf(th), sinf(ph));
    float t = c.sdf.castOut(H.C, dir, MK_HEAD, 0.25f * c.D->headS, tHint);
    p = H.C + dir * t;
    vec3 g = c.sdf.grad(p, MK_HEAD);
    n = length2(g) > 1e-12f ? normalize(g) : dir;
}
// A head grid-like vertex at (theta, phi) for the coverage / thickness functions.
static BVert headProbe(float th, float ph, vec3 p) {
    BVert v;
    v.pa = th < 0.f ? th + kTwoPi : (th >= kTwoPi ? th - kTwoPi : th);
    v.pb = ph;
    v.part = PART_HEAD;
    v.pc = 1.5f;
    v.p = v.bp = p;
    return v;
}
// Step from a scalp point along a tangent direction and land back on the scalp (ray from the grid centre).
static void scalpStep(const BuildCtx& c, vec3 q, vec3 f, float seg, float& th, float& ph, vec3& p, vec3& n) {
    vec3 dq = q + f * seg - c.head.C;
    vec3 dir = normalize(dq);
    th = atan2f(dir.x, dir.y);
    if (th < 0.f) th += kTwoPi;
    ph = asinf(Clamp(dir.z, -1.f, 1.f));
    headSurf(c, th, ph, p, n, Max(length(q - c.head.C) - 0.012f * c.D->headS, 0.f));
}

// Combing direction of the style at a scalp point (unit, tangent to the scalp).
static vec3 scalpFlow(const BuildCtx& c, const HairParams& h, vec3 p, vec3 n, u32 cardSeed) {
    const HeadInfo& H = c.head;
    vec3 hp = (p - H.origin) / c.D->headS;   // head space: x right, y forward, z up
    float top = sstep(0.1f, 0.16f, hp.z);
    vec3 f;
    switch (h.style) {
        case HAIR_SLICKED: f = vec3(0.f, -1.f, -0.35f - 0.5f * (1.f - top)); break;
        case HAIR_QUIFF: {
            float fr = sstep(0.015f, 0.07f, hp.y) * sstep(0.08f, 0.13f, hp.z);   // front top: up, then back over the top
            f = lerp(vec3(hp.x * 3.f, -1.f, -0.45f), vec3(0.f, -0.3f, 1.f), fr);
            break;
        }
        case HAIR_PONYTAIL: case HAIR_BUN: {
            vec3 tie = h.style == HAIR_PONYTAIL ? vec3(0.f, -0.097f, 0.066f) : vec3(0.f, -0.078f, 0.132f);
            f = tie - hp;
            break;
        }
        case HAIR_LONG: case HAIR_BOB: {
            // falls away from the parting, then straight down
            float side = hp.x >= h.partX ? 1.f : -1.f;
            float nearPart = 1.f - sstep(0.004f, 0.03f, fabsf(hp.x - h.partX));
            f = vec3(side * (0.2f + 1.3f * top + 0.8f * nearPart * top), -0.12f, -1.f + 0.6f * top);
            break;
        }
        case HAIR_CURLY: {
            Rng r(hash32(cardSeed * 911u + 3u));
            f = vec3(r.range(-1.f, 1.f), r.range(-1.f, 1.f), r.range(-1.f, 0.4f));
            break;
        }
        default: {   // natural short: from the crown whorl outwards, gravity on the sides and back
            vec3 whorl(0.012f, -0.05f, 0.158f);
            f = normalize(hp - whorl) + vec3(0.f, 0.f, -0.8f * (1.f - top));
            break;
        }
    }
    f = f - n * dot(f, n);
    return length2(f) > 1e-10f ? normalize(f) : normalize(anyPerp(n));
}

// Scalp cards over the shell (shellTop(v) = the shell's height above the scalp at a grid-like vertex).
static void buildScalpCards(OutfitCtx& o, const HairParams& h, float shellFrac) {
    BuildCtx& c = o.c;
    const HeadInfo& H = c.head;
    const float hs = c.D->headS;
    float len0, len1, w0, spacing, dens = 1.f, stand = 0.f;
    int NS;
    switch (h.style) {
        case HAIR_SHORT: len0 = 0.026f; len1 = 0.04f; w0 = 0.014f; spacing = 0.0125f; NS = 3; break;
        case HAIR_SLICKED: len0 = 0.05f; len1 = 0.075f; w0 = 0.015f; spacing = 0.013f; NS = 4; break;
        case HAIR_QUIFF: len0 = 0.03f; len1 = 0.05f; w0 = 0.014f; spacing = 0.0125f; NS = 4; break;
        case HAIR_PONYTAIL: case HAIR_BUN: len0 = 0.05f; len1 = 0.08f; w0 = 0.015f; spacing = 0.013f; NS = 4; break;
        case HAIR_LONG: case HAIR_BOB: len0 = 0.055f; len1 = 0.085f; w0 = 0.016f; spacing = 0.0135f; NS = 4; break;
        case HAIR_CURLY: len0 = 0.016f; len1 = 0.026f; w0 = 0.012f; spacing = 0.0135f; NS = 3; stand = 1.f; dens = 0.85f; break;
        default: return;   // bald, buzz cut, cornrows: the shell and scalp tint carry them
    }
    // the cards of the densest styles are capped (~3.4k triangles): past that the spacing opens up a little
    const int kCardBudget = 3400;
    MeshB m;
    for (int attempt = 0; attempt < 2; attempt++) {
        m = MeshB();
        Rng r(hash32(c.d->seed * 6131u + 17u));
        const float R0 = 0.095f * hs;
        const vec3 colRoot = h.col * 0.5f, colTip = h.col * 1.08f;
        CardPt pts[8];
        for (int layer = 0; layer < 2; layer++) {
            float sp = spacing * hs * (layer ? 1.2f : 1.f);
            float dph = sp / R0;
            float hRoot = layer ? 0.9f : 0.62f, hTip = layer ? 1.12f : 0.98f;
            for (float ph = -42.f * kDegToRad + dph * 0.5f * layer; ph < 87.f * kDegToRad; ph += dph) {
                float circ = kTwoPi * R0 * Max(cosf(ph), 0.05f);
                int nth = Max(3, (int)(circ / sp));
                float off = r.f();
                for (int i = 0; i < nth; i++) {
                    float th = kTwoPi * (i + off + 0.4f * (r.f() - 0.5f)) / nth;
                    float php = ph + dph * 0.4f * (r.f() - 0.5f);
                    if (th >= kTwoPi) th -= kTwoPi;
                    vec3 q, nq;
                    headSurf(c, th, php, q, nq);
                    BVert pr = headProbe(th, php, q);
                    float cvRoot = hairCoverage(c, h, pr);
                    if (cvRoot < 0.002f) continue;
                    u32 seed = r.next();
                    float len = r.range(len0, len1) * hs;
                    if (h.style == HAIR_QUIFF && (q - H.origin).y > 0.02f * hs && (q - H.origin).z > 0.1f * hs) len *= 1.35f;
                    // (narrower and sparser cards near the hairline: the edge thins out into single hairs)
                    const float nearEdge = sstep(0.003f, 0.022f, cvRoot);
                    float w = w0 * hs * r.range(0.85f, 1.15f) * Lerp(0.5f, 1.f, nearEdge);
                    float seg = len / NS;
                    int np = 0;
                    float thq = th, phq = php;
                    for (int sgi = 0; sgi <= NS; sgi++) {
                        float u = (float)sgi / NS;
                        BVert pq = headProbe(thq, phq, q);
                        float cq = hairCoverage(c, h, pq);
                        if (sgi > 0 && cq < -0.006f) break;   // tips may fall 6 mm past the hairline, no further
                        float Tf = styleThickness(c, h, pq);
                        float T = Tf * sstep(-0.006f, hairRampLen(Tf), cq);
                        float hgt = 0.0008f + T * Lerp(hRoot, hTip, sstep(0.f, 0.5f, u)) * (np == 0 ? 1.f : 1.f);
                        if (stand > 0.f) hgt += T * 0.25f * u;   // curls stand off the afro surface
                        if (hgt < T * shellFrac && sgi > 0 && layer == 0) hgt = T * shellFrac + 0.0006f;
                        pts[np].p = q + nq * hgt;
                        pts[np].n = nq;
                        pts[np].w = w * (1.f - 0.35f * u);
                        pts[np].sw = skin1(B_HEAD);
                        np++;
                        if (sgi == NS) break;
                        vec3 f = scalpFlow(c, h, q, nq, seed);
                        vec3 q1, n1;
                        scalpStep(c, q, f, seg, thq, phq, q1, n1);
                        q = q1;
                        nq = n1;
                    }
                    if (np >= 2)
                        setCardDepth(m, emitCard(m, pts, np, CARD_SCALP, seed, colRoot, colTip, dens * (layer ? 0.8f : 1.f) * Lerp(0.7f, 1.f, nearEdge), PART_HEAD, &H),
                                     layer ? 1u : 4u);
                }
            }
        }
        int tris = (int)(m.idx.size() / 3);
        if (tris <= kCardBudget) break;
        spacing *= sqrtf((float)tris / kCardBudget) * 1.02f;
    }
    // the hairline: a fringe of fine, short hairs along the front and the temples (roots just inside the hairline in two
    // staggered rows, tips a few millimetres out onto the skin, sparse and thin), so the edge reads as hair thinning out
    // rather than a cut line
    {
        Rng rf(hash32(c.d->seed * 0x2C2EF5u + 0x61u));
        const float R0 = 0.095f * hs, k01 = 0.1f * hs;
        const float step = 0.0038f * hs;
        const vec3 colRoot = h.col * 0.62f, colTip = h.col * 1.05f;
        CardPt pts[3];
        for (int side = 0; side < 2; side++)
            for (float at0 = side ? 0.5f * step / R0 : 0.f; at0 < 78.f * kDegToRad; at0 += step / R0) {
                for (int row = 0; row < 2; row++) {
                    float at = at0 + (row ? 0.5f : 0.f) * step / R0 + 0.3f * (rf.f() - 0.5f) * step / R0;
                    float th = side ? kTwoPi - at : at;
                    float want = (row ? 0.0042f : 0.0012f) * hs + 0.0012f * hs * rf.f();
                    // find the root at the wanted coverage (the hairline wanders: two Newton steps along phi)
                    float ph = hairlinePhi(h, at) + want / k01;
                    vec3 q, nq;
                    for (int it = 0; it < 2; it++) {
                        headSurf(c, th, ph, q, nq);
                        float cvq = hairCoverage(c, h, headProbe(th, ph, q));
                        ph += (want - cvq) / k01;
                    }
                    headSurf(c, th, ph, q, nq);
                    float cvq = hairCoverage(c, h, headProbe(th, ph, q));
                    if (cvq < 0.f || cvq > 0.009f * hs) continue;
                    u32 seed = rf.next();
                    float len = rf.range(0.005f, 0.0095f) * hs * (row ? 1.15f : 0.85f);
                    float w = rf.range(0.0032f, 0.0048f) * hs;
                    float seg = len / 2.f;
                    float thq = th, phq = ph;
                    int np = 0;
                    for (int sgi = 0; sgi <= 2; sgi++) {
                        BVert pq = headProbe(thq, phq, q);
                        float Tf = styleThickness(c, h, pq);
                        float T = Tf * sstep(-0.006f, hairRampLen(Tf), hairCoverage(c, h, pq));
                        pts[np].p = q + nq * (0.0004f * hs + T * 0.55f + 0.0003f * hs * sgi);
                        pts[np].n = nq;
                        pts[np].w = w * (1.f - 0.3f * sgi);
                        pts[np].sw = skin1(B_HEAD);
                        np++;
                        if (sgi == 2) break;
                        // the style's flow turned out across the hairline (baby hairs fall forwards onto the skin)
                        vec3 outw = vec3(0.f, 0.35f, -1.f) - nq * dot(vec3(0.f, 0.35f, -1.f), nq);
                        vec3 f = scalpFlow(c, h, q, nq, seed) * 0.55f + (length2(outw) > 1e-8f ? normalize(outw) : vec3(0.f)) * 0.8f;
                        f = f - nq * dot(f, nq);
                        if (length2(f) < 1e-10f) break;
                        vec3 q1, n1;
                        scalpStep(c, q, normalize(f), seg, thq, phq, q1, n1);
                        q = q1;
                        nq = n1;
                    }
                    setCardDepth(m, emitCard(m, pts, np, CARD_SCALP, seed, colRoot, colTip, rf.range(0.45f, 0.72f), PART_HEAD, &H), 0);
                }
            }
    }
    size_t t0 = o.out.idx.size() / 3;
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
    OutfitCtx::Layer L;
    L.cov = [](const BVert&) { return 1.f; };
    L.margin = 0.f;
    L.t0 = t0;
    L.t1 = o.out.idx.size() / 3;
    o.layers.push_back(L);   // hats hide the cards under them like the shell
}

// ------------------------------------------------------------------------------------------------
// Draped geometry (curtains, ponytails, braids)

static vec3 pushOutside(const BuildCtx& c, vec3 p, float minDist, u32 mask) {
    for (int it = 0; it < 4; it++) {
        float d = c.sdf.eval(p, mask);
        if (d >= minDist) break;
        vec3 g = c.sdf.grad(p, mask);
        float gl = length(g);
        if (gl < 1e-5f) break;
        p += g / gl * (minDist - d);
    }
    return p;
}

static SkinW drapeWeights(float t, float headKeep) {
    WAcc acc;
    float wh = 1.f - sstep(headKeep, headKeep + 0.45f, t);
    float wc = sstep(headKeep + 0.2f, 1.f, t) * 0.75f;
    acc.add(B_HEAD, wh);
    acc.add(B_CHEST, wc);
    acc.add(B_NECK, Max(0.f, 1.f - wh - wc));
    return acc.finish();
}

// A sheet of hair hanging from the head around the back/sides (theta range [thA, thB] through the back).
// (cards: strand cards over the sheet; a curly curtain has none, the coils lie over it instead)
static void addCurtain(OutfitCtx& o, const HairParams& h, float thA, float length, float lift, float flare, float jag, bool cards = true) {
    BuildCtx& c = o.c;
    const BodyDims& D = *c.D;
    const float hs = D.headS;
    const HeadInfo& H = c.head;
    const int K = 23, NR = 9;
    const u32 bodyMask = MK_HEAD | MK_NECK | MK_TORSO | MK_ARM_L | MK_ARM_R;
    Rng r(hash32(c.d->seed * 131u + 7u));
    const float ph1 = r.range(0.f, kTwoPi), ph2 = r.range(0.f, kTwoPi);
    std::vector<vec3> outer((size_t)K * NR);
    std::vector<vec3> nrm((size_t)K * NR);
    std::vector<float> tt((size_t)K * NR);
    for (int k = 0; k < K; k++) {
        float u = (float)k / (K - 1);
        float th = thA + (kTwoPi - 2.f * thA) * u;   // from +thA through the back (pi) to -thA
        float at = th > kPi ? kTwoPi - th : th;
        float ph0 = hairlinePhi(h, at) + 12.f * kDegToRad;
        ph0 = Max(ph0, 10.f * kDegToRad);
        vec3 dir(cosf(ph0) * sinf(th), cosf(ph0) * cosf(th), sinf(ph0));
        float t0 = c.sdf.castOut(H.C, dir, MK_HEAD, 0.25f * hs);
        BVert tmp;
        tmp.pa = th;
        tmp.pb = ph0;
        tmp.part = PART_HEAD;
        tmp.pc = 1.5f;
        tmp.bp = H.C + dir * t0;
        float T = styleThickness(c, h, tmp);
        vec3 p = H.C + dir * (t0 + T * 0.8f + lift);
        // (the ends vary smoothly from column to column: layered, not a sawtooth of alternating lengths)
        float wob = 0.5f + 0.3f * sinf(0.55f * (float)k + ph1) + 0.2f * sinf(1.3f * (float)k + ph2);
        float len = length * (1.f + jag * (wob - 0.5f)) * (1.f + 0.15f * Sq(cosf((th - kPi) * 0.5f)));
        float seg = len / (NR - 1);
        vec3 outDir = normalize(vec3(sinf(th), cosf(th), 0.f));
        for (int i = 0; i < NR; i++) {
            if (i > 0) {
                vec3 prev = outer[(size_t)k * NR + i - 1];
                p = prev + vec3(0, 0, -seg) + outDir * (flare * seg);
                p = pushOutside(c, p, 0.006f + T * 0.5f, bodyMask);
            }
            outer[(size_t)k * NR + i] = p;
            tt[(size_t)k * NR + i] = (float)i / (NR - 1);
        }
    }
    // normals from the grid
    for (int k = 0; k < K; k++)
        for (int i = 0; i < NR; i++) {
            vec3 du = outer[(size_t)Min(k + 1, K - 1) * NR + i] - outer[(size_t)Max(k - 1, 0) * NR + i];
            vec3 dv = outer[(size_t)k * NR + Min(i + 1, NR - 1)] - outer[(size_t)k * NR + Max(i - 1, 0)];
            vec3 n = normalize(cross(dv, du));
            vec3 radial = outer[(size_t)k * NR + i] - vec3(H.C.x, H.C.y, outer[(size_t)k * NR + i].z);
            if (dot(n, radial) < 0.f) n = -n;
            nrm[(size_t)k * NR + i] = n;
        }
    MeshB m;
    float thick = 0.007f * hs * h.volume;
    std::vector<u32> gOut((size_t)K * NR), gIn((size_t)K * NR);
    for (int k = 0; k < K; k++)
        for (int i = 0; i < NR; i++) {
            size_t id = (size_t)k * NR + i;
            float taper = 1.f - 0.7f * Sq(tt[id]);
            BVert v;
            v.p = outer[id];
            v.bp = v.p;
            v.n = nrm[id];
            v.t = vec3(0, 0, -1);
            v.uv = vec2(tt[id] * length, (float)k * 0.02f);
            v.col = h.col * (0.9f + 0.2f * hashToFloat(hash32((u32)(k * 31 + i))));
            v.mat = MAT_HAIR;
            v.part = PART_HAIR;
            v.sw = drapeWeights(tt[id], 0.12f);
            gOut[id] = m.add(v);
            v.p = outer[id] - nrm[id] * thick * taper;
            v.n = -nrm[id];
            v.col = h.col * 0.6f;
            gIn[id] = m.add(v);
        }
    auto quadF = [&](u32 a, u32 b, u32 cc, u32 d, vec3 f) {
        vec3 nn = cross(m.v[b].p - m.v[a].p, m.v[d].p - m.v[a].p);
        if (dot(nn, f) >= 0.f) m.quad(a, b, cc, d);
        else m.quad(a, d, cc, b);
    };
    for (int k = 0; k + 1 < K; k++)
        for (int i = 0; i + 1 < NR; i++) {
            size_t a = (size_t)k * NR + i, b = (size_t)(k + 1) * NR + i, cc = (size_t)(k + 1) * NR + i + 1, d = (size_t)k * NR + i + 1;
            quadF(gOut[a], gOut[b], gOut[cc], gOut[d], nrm[a]);
            quadF(gIn[a], gIn[b], gIn[cc], gIn[d], -nrm[a]);
        }
    // bottom edge and side edges
    for (int k = 0; k + 1 < K; k++) {
        size_t a = (size_t)k * NR + NR - 1, b = (size_t)(k + 1) * NR + NR - 1;
        quadF(gOut[a], gOut[b], gIn[b], gIn[a], vec3(0, 0, -1));
    }
    for (int side = 0; side < 2; side++) {
        int k = side ? K - 1 : 0;
        vec3 f = normalize(outer[(size_t)k * NR] - outer[(size_t)(side ? K - 2 : 1) * NR]);
        for (int i = 0; i + 1 < NR; i++) {
            size_t a = (size_t)k * NR + i, d = (size_t)k * NR + i + 1;
            quadF(gOut[a], gOut[d], gIn[d], gIn[a], f);
        }
    }
    m.computeNormals(0, m.idx.size());
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
    if (!cards) return;
    // strand cards hanging over the sheet: two offset layers, strands ending at slightly different lengths
    MeshB cm;
    Rng rc(hash32(c.d->seed * 733u + 5u));
    CardPt pts[NR];
    for (int layer = 0; layer < 2; layer++) {
        float step = layer ? 0.62f : 0.5f;
        for (float uk = layer * 0.31f; uk < (float)(K - 1); uk += step) {
            int k0 = (int)uk, k1 = Min(k0 + 1, K - 1);
            float fk = uk - (float)k0;
            u32 seed = rc.next();
            float lenFrac = rc.range(0.8f, 1.f);
            int np = 0;
            for (int i = 0; i < NR; i++) {
                float u = (float)i / (NR - 1);
                if (u > lenFrac + 1e-3f && np >= 2) break;
                size_t ia = (size_t)k0 * NR + i, ib = (size_t)k1 * NR + i;
                vec3 n = normalize(lerp(nrm[ia], nrm[ib], fk));
                float colW = sqrtf(length2(outer[ib] - outer[ia])) + 0.002f * hs;
                pts[np].p = lerp(outer[ia], outer[ib], fk) + n * ((layer ? 0.0026f : 0.0012f) * hs);
                pts[np].n = n;
                pts[np].w = colW * (layer ? 1.1f : 1.35f) * (1.f - 0.3f * u);
                pts[np].sw = drapeWeights(tt[ia], 0.12f);
                np++;
            }
            if (np >= 2) setCardDepth(cm, emitCard(cm, pts, np, CARD_SCALP, seed, h.col * 0.62f, h.col * 1.08f, layer ? 0.75f : 1.f, PART_HAIR, nullptr), layer ? 1u : 3u);
        }
    }
    o.out.append(cm);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

// Tube of hair swept from a head point down the back (ponytail / braid).
// Hair tie / scrunchie colour for a character: mostly black or dark, sometimes a colour that reads from a distance.
static vec3 tieColor(u32 seed) {
    static const vec3 kTie[] = {vec3(0.02f), vec3(0.02f), vec3(0.05f, 0.03f, 0.02f), vec3(0.45f, 0.03f, 0.04f), vec3(0.03f, 0.06f, 0.25f),
                                vec3(0.75f, 0.25f, 0.45f), vec3(0.8f), vec3(0.85f, 0.55f, 0.1f)};
    return kTie[hash32(seed * 0x45D9F3Bu + 0x7u) % 8u];
}

// A band (elastic tie) round an axis: a short closed tube of `seg` sides.
static void addBand(MeshB& m, vec3 cen, vec3 axis, float r, float halfLen, int seg, vec3 col, const SkinW& sw) {
    vec3 tng = normalize(axis);
    vec3 a = normalize(anyPerp(tng)), b = cross(tng, a);
    std::vector<u32> r0v(seg), r1v(seg);
    for (int k = 0; k < seg; k++) {
        float th = kTwoPi * k / seg;
        vec3 d = a * cosf(th) + b * sinf(th);
        BVert v;
        v.p = cen + d * r - tng * halfLen;
        v.bp = v.p;
        v.n = d;
        v.t = tng;
        v.col = col;
        v.mat = MAT_CLOTH;
        v.part = PART_ACC;
        v.sw = sw;
        r0v[k] = m.add(v);
        v.p = cen + d * r + tng * halfLen;
        r1v[k] = m.add(v);
    }
    for (int k = 0; k < seg; k++) {
        u32 a0 = r0v[k], a1 = r0v[(k + 1) % seg], b0 = r1v[k], b1 = r1v[(k + 1) % seg];
        vec3 nn = cross(m.v[b0].p - m.v[a0].p, m.v[a1].p - m.v[a0].p);
        if (dot(nn, m.v[a0].n) >= 0.f) m.quad(a0, b0, b1, a1);
        else m.quad(a0, a1, b1, b0);
    }
}

static void addHairTube(OutfitCtx& o, const HairParams& h, vec3 start, vec3 dir0, float length, float r0, float rMid, float rEnd, int seg,
                        bool tie) {
    BuildCtx& c = o.c;
    const u32 bodyMask = MK_HEAD | MK_NECK | MK_TORSO;
    const int NP = 10;
    std::vector<vec3> pts(NP);
    std::vector<float> rad(NP);
    std::vector<SkinW> sws(NP);
    vec3 p = start;
    vec3 dir = normalize(dir0);
    float step = length / (NP - 1);
    for (int i = 0; i < NP; i++) {
        float t = (float)i / (NP - 1);
        if (i > 0) {
            dir = normalize(lerp(dir, vec3(0, -0.15f, -1.f), 0.45f));
            p = p + dir * step;
            p = pushOutside(c, p, rad[i - 1] + 0.004f, bodyMask);
        }
        pts[i] = p;
        rad[i] = t < 0.35f ? Lerp(r0, rMid, t / 0.35f) : Lerp(rMid, rEnd, (t - 0.35f) / 0.65f);
        sws[i] = drapeWeights(t, 0.05f);
    }
    MeshB m;
    // addTube lives in clothing.cpp (same translation unit); rebuild a simple one here for hair material
    std::vector<u32> prev;
    for (int i = 0; i < NP; i++) {
        vec3 tng = i + 1 < NP ? normalize(pts[i + 1] - pts[i]) : normalize(pts[i] - pts[i - 1]);
        vec3 a = normalize(anyPerp(tng));
        vec3 b = cross(tng, a);
        std::vector<u32> ring(seg);
        for (int k = 0; k < seg; k++) {
            float th = kTwoPi * k / seg;
            vec3 d = a * cosf(th) + b * sinf(th);
            BVert v;
            v.p = pts[i] + d * rad[i];
            v.bp = v.p;
            v.n = d;
            v.t = tng;
            v.uv = vec2((float)i / (NP - 1) * length, th * rad[i]);
            v.col = h.col * (0.9f + 0.2f * hashToFloat(hash32((u32)(i * 17 + k))));
            v.mat = MAT_HAIR;
            v.part = PART_HAIR;
            v.sw = sws[i];
            ring[k] = m.add(v);
        }
        if (i > 0)
            for (int k = 0; k < seg; k++) {
                u32 a0 = prev[k], a1 = prev[(k + 1) % seg], b0 = ring[k], b1 = ring[(k + 1) % seg];
                vec3 nn = cross(m.v[b0].p - m.v[a0].p, m.v[a1].p - m.v[a0].p);
                if (dot(nn, m.v[a0].n) >= 0.f) m.quad(a0, b0, b1, a1);
                else m.quad(a0, a1, b1, b0);
            }
        prev = ring;
    }
    // tip cap
    BVert tip = m.v[prev[0]];
    tip.p = pts[NP - 1] + normalize(pts[NP - 1] - pts[NP - 2]) * rEnd;
    tip.n = normalize(pts[NP - 1] - pts[NP - 2]);
    u32 ti = m.add(tip);
    for (int k = 0; k < seg; k++) {
        u32 a0 = prev[k], a1 = prev[(k + 1) % seg];
        vec3 nn = cross(m.v[a1].p - m.v[a0].p, m.v[ti].p - m.v[a0].p);
        if (dot(nn, tip.n) >= 0.f) m.tri(a0, a1, ti);
        else m.tri(a0, ti, a1);
    }
    if (tie) {
        // hair tie at the base: a band a little proud of the gathered hair (dark or a colour, per person)
        vec3 tng = normalize(pts[1] - pts[0]);
        vec3 cc = lerp(pts[0], pts[1], 0.35f);
        float rr = Lerp(rad[0], rad[1], 0.35f) + 0.0035f * c.D->headS;
        addBand(m, cc, tng, rr, 0.0075f * c.D->headS, seg, tieColor(c.d->seed), sws[0]);
    }
    m.computeNormals(0, 0);
    if (tie) {
        // ponytail: strand cards around the tube
        Rng rc(hash32(c.d->seed * 419u + 23u));
        const int NCd = 7;
        CardPt cp[NP];
        for (int j = 0; j < NCd; j++) {
            float th = kTwoPi * ((float)j + 0.3f * rc.f()) / NCd;
            u32 seed = rc.next();
            for (int i = 0; i < NP; i++) {
                vec3 tng = i + 1 < NP ? normalize(pts[i + 1] - pts[i]) : normalize(pts[i] - pts[i - 1]);
                vec3 a = normalize(anyPerp(tng));
                vec3 b = cross(tng, a);
                vec3 dd = a * cosf(th) + b * sinf(th);
                cp[i].p = pts[i] + dd * (rad[i] + 0.0015f * c.D->headS);
                cp[i].n = dd;
                cp[i].w = kTwoPi * rad[i] / NCd * 1.6f + 0.004f;
                cp[i].sw = sws[i];
            }
            setCardDepth(m, emitCard(m, cp, NP, CARD_SCALP, seed, h.col * 0.62f, h.col * 1.08f, 0.9f, PART_HAIR, nullptr), 1);
        }
    }
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

static void addBun(OutfitCtx& o, const HairParams& h) {
    BuildCtx& c = o.c;
    const float hs = c.D->headS;
    const HeadInfo& H = c.head;
    float th = kPi, ph = 38.f * kDegToRad;
    vec3 dir(cosf(ph) * sinf(th), cosf(ph) * cosf(th), sinf(ph));
    float t0 = c.sdf.castOut(H.C, dir, MK_HEAD, 0.25f * hs);
    float R = 0.039f * hs * h.volume;
    vec3 cen = H.C + dir * (t0 + R * 0.75f);
    MeshB m;
    const int NU = 12, NV = 8;
    std::vector<u32> g((NU + 1) * (NV + 1));
    for (int i = 0; i <= NU; i++)
        for (int j = 0; j <= NV; j++) {
            float a = kTwoPi * i / NU, b = -kHalfPi + kPi * j / NV;
            vec3 d(cosf(a) * cosf(b), sinf(a) * cosf(b), sinf(b));
            vec3 q(d.x * R, d.y * R * 0.85f, d.z * R * 0.8f);
            q += d * (0.002f * sinf(a * 5.f + b * 3.f));
            BVert v;
            v.p = cen + q;
            v.bp = v.p;
            v.n = d;
            v.t = vec3(-sinf(a), cosf(a), 0);
            v.uv = vec2(a * R, b * R);
            v.col = h.col * (0.9f + 0.15f * sinf(a * 7.f));
            v.mat = MAT_HAIR;
            v.part = PART_HAIR;
            v.sw = skin1(B_HEAD);
            g[i * (NV + 1) + j] = m.add(v);
        }
    for (int i = 0; i < NU; i++)
        for (int j = 0; j < NV; j++) {
            u32 a = g[i * (NV + 1) + j], b = g[(i + 1) * (NV + 1) + j], cc = g[(i + 1) * (NV + 1) + j + 1], d = g[i * (NV + 1) + j + 1];
            vec3 nn = cross(m.v[b].p - m.v[a].p, m.v[d].p - m.v[a].p);
            if (dot(nn, m.v[a].n) >= 0.f) m.quad(a, b, cc, d);
            else m.quad(a, d, cc, b);
        }
    m.computeNormals(0, m.idx.size());
    // the band holding it, where the bun meets the head
    {
        vec3 base = H.C + dir * (t0 + 0.004f * hs);
        addBand(m, lerp(base, cen, 0.3f), dir, R * 0.62f, 0.0055f * hs, 12, tieColor(c.d->seed), skin1(B_HEAD));
    }
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

// ------------------------------------------------------------------------------------------------
// Braids and locs. A rope is traced over the scalp along a combing path (lifted by the hair under it) and hangs free
// once it turns down off the head or passes the hairline. Cornrows are flat plaits on the parted scalp, each a row of
// chevron lobes from the front hairline to the nape that ends in a short tail or a long braid; box braids and locs lie
// over a lumpy shell (the sections underneath) and fall to the shoulders or below; short locs / twists stand out from
// the scalp and droop.

// Per-rope random numbers: a hash of a rope seed and a small salt.
static float ropeRand(u32 x) { return hashToFloat(hash32(x * 0x9E3779B1u + 0x7F4A7C15u)); }

// One cross-section of a rope.
struct RopeSt {
    vec3 p;                    // centre (half ring: the middle of its base)
    vec3 t;                    // along the rope, root to tip
    vec3 up;                   // away from the scalp / the body
    vec3 bp;                   // scalp point under it (hanging: the centre)
    float w = 0, h = 0;        // half-width across; height of a half ring / half-height of a closed ring
    float u = 0;               // distance from the root (m)
    float shade = 1.f;         // colour multiplier (plait lobes light, the junctions between them dark)
    float chev = 0.f;          // the ring's sides trail this far along the rope (the plait's chevrons)
    float th = 0, ph = -9.f;   // head grid angles of the scalp point under it (ph < -2: hanging free)
    SkinW sw;
};

// How a rope runs: its root, size and the layer it lies in.
struct RopeSpec {
    float th = 0, ph = 0;      // root on the scalp (head grid angles)
    float hang = 0;            // length hanging free (m); 0: it ends where it leaves the scalp
    float w = 0, h = 0;        // half-width and height (half ring) / half-height (closed)
    float lift = 0;            // the rope's underside above the scalp (the hair under it)
    float spread = 0;          // flow behind the ears (ropeFlow)
    float stepS = 0.015f, stepH = 0.025f;
    bool half = false;         // a flat plait on the scalp (cornrows)
    float plait = 0;           // > 0: chevron lobes, one station every `plait` (m) on the scalp
    float clear = 0.003f;      // hanging: clearance from the body and the shell (outer layers more)
    float lump = 0;            // radius irregularity (locs)
    float flare = 0.05f;       // hanging: drift away from the head axis
    float wobble = 0.f;        // on the scalp: sideways meander (locs)
    vec3 plane = vec3(1.f, 0.f, 0.f);   // the plane it runs in over the top (ropeFlow / ropePlane)
    u32 seed = 0;
};

// Rings over the stations: closed (NS sides) or a half ring (NS segments over the top, its edges tucked into the
// scalp). The colour runs from `col` to `tipCol` over the last 45 %; an optional cap closes the tip. lodMark: which
// vertices the LODs replace (BuildCtx::F_SCALP on hair, see stripForLod): 0 none, 1 those lying on the scalp, 2 all.
static void emitRope(MeshB& m, const std::vector<RopeSt>& S, int NS, bool half, vec3 col, vec3 tipCol, bool capTip, int lodMark) {
    const int n = (int)S.size();
    if (n < 2) return;
    const int NV = half ? NS + 1 : NS;
    const float uEnd = Max(S[n - 1].u, 1e-4f);
    std::vector<u32> prev(NV), ring(NV);
    for (int i = 0; i < n; i++) {
        const RopeSt& s = S[i];
        vec3 side = cross(s.t, s.up);
        side = length2(side) > 1e-12f ? normalize(side) : normalize(anyPerp(s.t));
        vec3 up = normalize(cross(side, s.t));
        vec3 base = lerp(col, tipCol, sstep(0.55f, 1.f, s.u / uEnd)) * s.shade;
        for (int k = 0; k < NV; k++) {
            float a = (half ? kPi : kTwoPi) * (float)k / NS;
            float ca = cosf(a), sa = sinf(a);
            BVert v;
            v.p = s.p + side * (ca * s.w) + up * (half ? sa * s.h - (1.f - sa) * 0.0012f : sa * s.h) + s.t * (s.chev * fabsf(ca));
            v.n = normalize(side * (ca / Max(s.w, 1e-5f)) + up * (sa / Max(s.h, 1e-5f)));
            v.t = s.t;
            v.bp = s.bp;
            v.uv = vec2(s.u, a * s.w);
            // the sides of a flat plait and the underside of a hanging rope sit in their own shadow
            v.col = base * (half ? Lerp(0.5f, 1.f, sa) : Lerp(0.62f, 1.f, 0.5f + 0.5f * sa));
            v.mat = MAT_HAIR;
            v.sw = s.sw;
            if (s.ph > -2.f) {   // on the scalp: head parametrization, so a hat hides what it covers
                v.part = PART_HEAD;
                v.pa = s.th;
                v.pb = s.ph;
                v.pc = 1.5f;
            } else {
                v.part = PART_HAIR;
            }
            if (lodMark == 2 || (lodMark == 1 && s.ph > -2.f)) v.flags |= BuildCtx::F_SCALP;
            ring[k] = m.add(v);
        }
        if (i > 0)
            for (int k = 0; k < NS; k++) {
                int k1 = half ? k + 1 : (k + 1) % NS;
                u32 a0 = prev[k], a1 = prev[k1], b0 = ring[k], b1 = ring[k1];
                vec3 fn = cross(m.v[b0].p - m.v[a0].p, m.v[a1].p - m.v[a0].p);
                if (dot(fn, m.v[a0].n + m.v[b1].n) >= 0.f) m.quad(a0, b0, b1, a1);
                else m.quad(a0, a1, b1, b0);
            }
        std::swap(prev, ring);
    }
    if (capTip && !half) {
        const RopeSt& s = S[n - 1];
        BVert tip = m.v[prev[0]];
        tip.p = s.p + s.t * (Min(s.w, s.h) * 0.85f);
        tip.n = s.t;
        u32 ti = m.add(tip);
        for (int k = 0; k < NS; k++) {
            u32 a0 = prev[k], a1 = prev[(k + 1) % NS];
            vec3 fn = cross(m.v[a1].p - m.v[a0].p, m.v[ti].p - m.v[a0].p);
            if (dot(fn, s.t) >= 0.f) m.tri(a0, a1, ti);
            else m.tri(a0, ti, a1);
        }
    }
}

// Combing flow for braids and locs. Over the top and the sides a rope stays in its own plane through a front-to-back
// line at about temple height (`plane`: the plane's normal): rows near the middle run straight back, rows from the
// temples run back level above the ears, and no two rows cross. Once the scalp faces backwards behind the ears the
// rope runs straight down the back of the head (not on towards the back pole, where the rows would run together),
// drawn together towards the nape (spread < 0) or fanning out (spread > 0).
static vec3 ropeFlow(const BuildCtx& c, vec3 p, vec3 n, float yEar, float spread, vec3 plane) {
    vec3 hp = (p - c.head.origin) / c.D->headS;
    float behind = sstep(yEar + 0.01f, yEar - 0.03f, hp.y) * sstep(-0.15f, -0.55f, n.y);
    vec3 fp = cross(plane, n);
    fp = length2(fp) > 1e-8f ? normalize(fp) : vec3(0.f, -1.f, 0.f);
    // down the back in the plane x = const (steepest descent would run off sideways like water off a dome)
    vec3 fd = vec3(0.f, -n.z, n.y);
    if (fd.z > 0.f) fd = -fd;
    fd = length2(fd) > 1e-8f ? normalize(fd) : vec3(0.f, 0.f, -1.f);
    fd += vec3(hp.x * spread, 0.f, 0.f);
    vec3 f = lerp(fp, fd, behind);
    f = f - n * dot(f, n);
    return length2(f) > 1e-10f ? normalize(f) : normalize(anyPerp(n));
}

// The plane a rope from a scalp root runs in (see ropeFlow): through the root and the front-to-back line at height zL
// (head space) over the middle of the head.
static vec3 ropePlane(const BuildCtx& c, vec3 root, float zL) {
    vec3 hp = (root - c.head.origin) / c.D->headS;
    vec3 d(hp.x, 0.f, Max(hp.z - zL, 0.01f));
    return normalize(vec3(d.z, 0.f, -d.x));
}

// Skin weights of a hanging rope point: the head while it is level with the skull, the neck below it and the chest
// (upper back / shoulders) further down, so it stays on the back when the head turns.
static SkinW ropeWeights(const BuildCtx& c, vec3 p) {
    const float hs = c.D->headS, z0 = c.head.origin.z;
    float wh = sstep(z0 - 0.06f * hs, z0 + 0.03f * hs, p.z);
    float wc = (1.f - wh) * sstep(z0 - 0.08f * hs, z0 - 0.22f * hs, p.z) * 0.85f;
    WAcc acc;
    acc.add(B_HEAD, wh);
    acc.add(B_CHEST, wc);
    acc.add(B_NECK, Max(0.f, 1.f - wh - wc));
    return acc.finish();
}

static float ropeLump(const RopeSpec& R, float u) {
    if (R.lump <= 0.f) return 1.f;
    float f = 70.f + 40.f * ropeRand(R.seed * 3u + 1u), p1 = kTwoPi * ropeRand(R.seed * 5u + 2u), p2 = kTwoPi * ropeRand(R.seed * 7u + 3u);
    return 1.f + R.lump * (0.6f * sinf(u * f + p1) + 0.4f * sinf(u * 2.6f * f + p2));
}

// The part of a rope that lies on the scalp: stations from the root along the flow until it leaves the head.
static void traceRopeScalp(const BuildCtx& c, const HairParams& hp, const RopeSpec& R, float yEar, std::vector<RopeSt>& S) {
    S.clear();
    float th = R.th, ph = R.ph;
    vec3 q, nq;
    headSurf(c, th, ph, q, nq);
    float u = 0.f;
    for (int i = 0; i < 160; i++) {
        float k = Lerp(0.55f, 1.f, sstep(0.f, 0.025f, u)) * ropeLump(R, u);
        RopeSt s;
        s.w = R.w * k;
        s.h = R.h * k;
        if (R.plait > 0.f) {
            const bool junction = (i & 1) != 0;
            s.w *= junction ? 0.84f : 1.f;
            s.h *= junction ? 0.68f : 1.f;
            s.shade = junction ? 0.7f : 1.1f;
            s.chev = 0.6f * R.plait;
        }
        vec3 f = ropeFlow(c, q, nq, yEar, R.spread, R.plane);
        if (R.wobble > 0.f) {   // locs do not run in neat lines
            vec3 side = cross(nq, f);
            f = normalize(f + side * (R.wobble * sinf(u * 55.f + kTwoPi * ropeRand(R.seed + 21u))));
        }
        s.t = f;
        s.up = nq;
        s.bp = q;
        s.p = q + nq * (R.half ? R.lift : R.lift + s.h);
        s.u = u;
        s.th = th;
        s.ph = ph;
        s.sw = skin1(B_HEAD);
        S.push_back(s);
        float thn, phn;
        vec3 q1, n1;
        scalpStep(c, q, f, R.plait > 0.f ? R.plait : R.stepS, thn, phn, q1, n1);
        // leave the scalp past the hairline, or under the back of the skull (falling free from there)
        if (hairCoverage(c, hp, headProbe(thn, phn, q1)) < 0.0015f || (!R.half && n1.z < -0.3f)) break;
        u += length(q1 - q);
        q = q1;
        nq = n1;
        th = thn;
        ph = phn;
        if (u > 0.5f) break;
    }
}

// Continue a rope hanging free from its last station: it swings down (a little back and away from the head), resting
// on the shell of hair, the neck, shoulders and back.
static void hangRope(const BuildCtx& c, const RopeSpec& R, float shellT, std::vector<RopeSt>& S) {
    if (S.empty() || R.hang <= 0.f) return;
    const HeadInfo& H = c.head;
    const u32 bodyMask = MK_NECK | MK_TORSO | MK_ARM_L | MK_ARM_R;
    RopeSt last = S.back();
    vec3 p = last.p, dir = last.t;
    const int nh = Max(1, (int)ceilf(R.hang / R.stepH));
    float uh = 0.f;
    for (int i = 1; i <= nh; i++) {
        vec3 out = vec3(p.x - H.C.x, p.y - H.C.y, 0.f);
        out = length2(out) > 1e-8f ? normalize(out) : vec3(0.f, -1.f, 0.f);
        vec3 g = normalize(vec3(0.f, -0.1f, -1.f) + out * R.flare);
        dir = normalize(lerp(dir, g, 0.42f));
        const float uu = last.u + uh + R.stepH;
        const float k = ropeLump(R, uu) * (i == nh ? 0.8f : 1.f);   // the end tapers a little
        const float ww = R.w * k, hh = R.h * k, rr = Max(ww, hh);
        vec3 pn = p + dir * R.stepH;
        pn = pushOutside(c, pn, shellT + rr + R.clear, MK_HEAD);
        pn = pushOutside(c, pn, rr + R.clear, bodyMask);
        vec3 d = pn - p;
        float dl = length(d);
        if (dl > 1e-5f) dir = d / dl;
        uh += dl;
        p = pn;
        RopeSt s;
        s.p = p;
        s.t = dir;
        vec3 up = out - dir * dot(out, dir);
        s.up = length2(up) > 1e-8f ? normalize(up) : normalize(anyPerp(dir));
        s.bp = p;
        s.w = ww;
        s.h = hh;
        s.u = last.u + uh;
        s.sw = ropeWeights(c, p);
        S.push_back(s);
    }
}

// Roots spaced evenly by arc length along the front hairline between -thMax and thMax (just inside the hairline).
static void hairlineRoots(const BuildCtx& c, const HairParams& h, int n, float thMax, std::vector<vec2>& roots) {
    const int NSamp = 64;
    std::vector<vec3> pts(NSamp + 1);
    std::vector<float> th(NSamp + 1), ph(NSamp + 1), acc(NSamp + 1, 0.f);
    for (int i = 0; i <= NSamp; i++) {
        th[i] = Lerp(-thMax, thMax, (float)i / NSamp);
        ph[i] = hairlinePhi(h, fabsf(th[i])) + 0.6f * kDegToRad;
        vec3 n;
        headSurf(c, th[i] < 0.f ? th[i] + kTwoPi : th[i], ph[i], pts[i], n);
        if (i > 0) acc[i] = acc[i - 1] + length(pts[i] - pts[i - 1]);
    }
    roots.clear();
    for (int k = 0; k < n; k++) {
        float target = acc[NSamp] * ((float)k + 0.5f) / n;
        int i = 1;
        while (i < NSamp && acc[i] < target) i++;
        float f = (target - acc[i - 1]) / Max(acc[i] - acc[i - 1], 1e-6f);
        float t = Lerp(th[i - 1], th[i], f);
        roots.push_back(vec2(t < 0.f ? t + kTwoPi : t, Lerp(ph[i - 1], ph[i], f)));
    }
}

// Height (head space) of the front-to-back line the rope planes turn about: 3 cm under the temple roots, so the rows
// from the temples run back level above the ears.
static float ropeAxisZ(const BuildCtx& c, const HairParams& h) {
    const float th = 50.f * kDegToRad;
    vec3 p, n;
    headSurf(c, th, hairlinePhi(h, th), p, n);
    return (p - c.head.origin).z / c.D->headS - 0.03f;
}

static float earLineY(const BuildCtx& c) {
    const HeadInfo& H = c.head;
    return 0.5f * ((H.earPos[0] - H.origin).y + (H.earPos[1] - H.origin).y) / c.D->headS;
}

// Cornrows: rows of flat plaits from the front hairline straight back to the nape; each row fills the width between
// its neighbours (a parting of bare scalp stays between them) and ends in a short tail or a long braid.
static void buildCornrows(OutfitCtx& o, const HairParams& h, MeshB& m) {
    BuildCtx& c = o.c;
    const float hs = c.D->headS;
    const bool fem = c.d->gender == FEMALE;
    const float yEar = earLineY(c);
    Rng r(hash32(c.d->seed * 0x1B873593u + 0x51u));
    const int N = fem ? r.irange(10, 14) : r.irange(6, 9);
    std::vector<vec2> roots;
    hairlineRoots(c, h, N, 50.f * kDegToRad, roots);
    // nominal width from the spacing at the hairline (the rows spread over the crown and close up at the nape)
    float arc = 0.f;
    for (int i = 1; i < N; i++) {
        vec3 a, b, n;
        headSurf(c, roots[i - 1].x, roots[i - 1].y, a, n);
        headSurf(c, roots[i].x, roots[i].y, b, n);
        arc += length(b - a);
    }
    const float spacing = arc / Max(N - 1, 1);
    const float wNom = 0.42f * spacing;
    std::vector<std::vector<RopeSt>> rows(N);
    std::vector<RopeSpec> specs(N);
    const float zL = ropeAxisZ(c, h);
    for (int i = 0; i < N; i++) {
        RopeSpec& R = specs[i];
        R.th = roots[i].x;
        R.ph = roots[i].y;
        {
            vec3 rp, rn;
            headSurf(c, R.th, R.ph, rp, rn);
            R.plane = ropePlane(c, rp, zL);
        }
        R.w = wNom;
        R.h = 0.62f * wNom;
        R.lift = 0.0006f * hs;
        R.spread = fem ? -0.9f : -0.5f;
        R.half = true;
        R.plait = 1.3f * wNom;
        R.seed = r.next();
        traceRopeScalp(c, h, R, yEar, rows[i]);
    }
    // each row as wide as the room between its neighbours allows
    for (int i = 0; i < N; i++)
        for (RopeSt& s : rows[i]) {
            float dmin = 1e9f;
            for (int j = i - 1; j <= i + 1; j += 2) {
                if (j < 0 || j >= N) continue;
                for (const RopeSt& q : rows[j]) dmin = Min(dmin, length2(q.bp - s.bp));
            }
            float want = dmin < 1e8f ? Clamp(0.45f * sqrtf(dmin), 0.6f * wNom, 1.7f * wNom) : wNom;
            s.w *= want / wNom;
            s.h *= want / wNom;
        }
    for (int i = 0; i < N; i++) {
        std::vector<RopeSt>& S = rows[i];
        if (S.size() < 2) continue;
        const vec3 col = h.ropeCol * Lerp(0.92f, 1.08f, ropeRand(specs[i].seed));
        emitRope(m, S, 4, true, col, col, false, 2);
        // the tail: a round braid from the end of the row, short or hanging down the back
        const RopeSt& e = S.back();
        RopeSpec T = specs[i];
        T.w = T.h = (h.ropeLong ? 0.42f : 0.3f) * (e.w + e.h);
        T.half = false;
        T.plait = 0.f;
        T.hang = h.ropeLen * Lerp(0.85f, 1.1f, ropeRand(T.seed + 11u));
        T.stepH = h.ropeLong ? 0.028f : 0.012f;
        T.clear = 0.002f;
        T.flare = 0.02f;
        std::vector<RopeSt> tail(1, e);
        tail[0].p = e.p + e.up * T.h;
        tail[0].w = tail[0].h = T.w;
        tail[0].shade = 1.f;
        tail[0].chev = 0.f;
        hangRope(c, T, 0.f, tail);
        emitRope(m, tail, 4, false, col, col, true, h.ropeLong ? 0 : 2);   // (long braids stay: they are the silhouette)
    }
}

// Box braids and locs: ropes from the front hairline running back over the shell, from the crown and from the nape,
// all hanging down the back and sides; the front ones are the outer layer.
static void buildHangingRopes(OutfitCtx& o, const HairParams& h, float shellT, MeshB& m) {
    BuildCtx& c = o.c;
    const float hs = c.D->headS;
    const bool fem = c.d->gender == FEMALE;
    const bool locs = h.rope == ROPE_LOCS;
    const float yEar = earLineY(c);
    Rng r(hash32(c.d->seed * 0xCC9E2D51u + 0x2Fu));
    const int NS = locs ? 5 : 4;
    const float wBase = (locs ? r.range(0.0062f, 0.008f) : 0.0047f) * hs;
    const int nFront = locs ? (fem ? 9 : 8) : (fem ? 13 : 10);
    const int nMid = locs ? 7 : (fem ? 6 : 5);
    const int nNape = locs ? 7 : (fem ? 8 : 6);
    std::vector<vec2> roots;
    hairlineRoots(c, h, nFront, 50.f * kDegToRad, roots);
    // crown ring (back half) and nape row
    for (int i = 0; i < nMid; i++) {
        float t = ((float)i + 0.5f) / nMid;
        roots.push_back(vec2(Lerp(65.f, 295.f, t) * kDegToRad + r.range(-0.06f, 0.06f), (50.f + r.range(-4.f, 4.f)) * kDegToRad));
    }
    for (int i = 0; i < nNape; i++) {
        float t = ((float)i + 0.5f) / nNape;
        float th = Lerp(115.f, 245.f, t) * kDegToRad;
        float at = th > kPi ? kTwoPi - th : th;
        roots.push_back(vec2(th, hairlinePhi(h, at) + 4.f * kDegToRad));
    }
    std::vector<RopeSt> S;
    const float zL = ropeAxisZ(c, h);
    for (int i = 0; i < (int)roots.size(); i++) {
        const int layer = i < nFront ? 2 : (i < nFront + nMid ? 1 : 0);   // outer .. inner
        RopeSpec R;
        R.th = roots[i].x;
        R.ph = roots[i].y;
        {
            vec3 rp, rn;
            headSurf(c, R.th, R.ph, rp, rn);
            R.plane = ropePlane(c, rp, zL);
        }
        R.seed = r.next();
        float sz = Lerp(0.9f, 1.1f, ropeRand(R.seed));
        R.w = R.h = wBase * sz;
        R.lift = shellT * (layer == 2 ? 0.85f : (layer == 1 ? 0.55f : 0.35f));
        R.spread = locs ? 0.45f : -0.5f;
        R.stepS = locs ? 0.022f : 0.02f;
        R.stepH = locs ? 0.032f : 0.03f;
        R.clear = layer == 2 ? 0.008f : (layer == 1 ? 0.0045f : 0.002f);
        R.lump = locs ? 0.22f : 0.f;
        R.wobble = locs ? 0.35f : 0.f;
        R.flare = locs ? 0.1f : 0.06f;
        R.hang = h.ropeLen * (locs ? Lerp(0.8f, 1.15f, ropeRand(R.seed + 5u)) : Lerp(0.9f, 1.05f, ropeRand(R.seed + 5u)));
        traceRopeScalp(c, h, R, yEar, S);
        hangRope(c, R, shellT, S);
        const float tone = Lerp(0.88f, 1.1f, ropeRand(R.seed + 9u));
        emitRope(m, S, NS, false, h.ropeCol * tone, h.ropeTip * tone, true, 1);
    }
}

// Short locs / twists: ropes standing out from the scalp all over and drooping under their own weight.
static void buildTwists(OutfitCtx& o, const HairParams& h, float shellT, MeshB& m) {
    BuildCtx& c = o.c;
    const float hs = c.D->headS;
    Rng r(hash32(c.d->seed * 0x85EBCA6Bu + 0x13u));
    const float R0 = 0.095f * hs, sp = 0.024f * hs;
    const float dph = sp / R0;
    std::vector<RopeSt> S;
    for (float ph = -30.f * kDegToRad; ph < 86.f * kDegToRad; ph += dph) {
        float circ = kTwoPi * R0 * Max(cosf(ph), 0.08f);
        int nth = Max(3, (int)(circ / sp));
        float off = r.f();
        for (int i = 0; i < nth; i++) {
            float th = kTwoPi * (i + off + 0.35f * (r.f() - 0.5f)) / nth;
            float php = ph + dph * 0.35f * (r.f() - 0.5f);
            if (th >= kTwoPi) th -= kTwoPi;
            u32 seed = r.next();
            vec3 q, nq;
            headSurf(c, th, php, q, nq);
            BVert pr = headProbe(th, php, q);
            float at = th > kPi ? kTwoPi - th : th;
            if (hairCoverage(c, h, pr) < 0.004f || fadeKeep(h, at, php) < 0.55f) continue;
            const float len = h.ropeLen * Lerp(0.75f, 1.2f, ropeRand(seed)) * hs;
            const float w = 0.0058f * hs * Lerp(0.85f, 1.15f, ropeRand(seed + 1u));
            vec3 flow = scalpFlow(c, h, q, nq, seed);
            vec3 jit(ropeRand(seed + 2u) - 0.5f, ropeRand(seed + 3u) - 0.5f, ropeRand(seed + 4u) - 0.5f);
            vec3 dir = normalize(nq * 0.85f + flow * 0.55f + jit * 0.35f);
            S.clear();
            const int NSt = 4;
            vec3 p = q + nq * (shellT * 0.5f + w);
            for (int k = 0; k <= NSt; k++) {
                if (k > 0) {
                    dir = normalize(dir + vec3(0.f, 0.f, -0.62f) + flow * 0.1f);   // drooping under their weight
                    p = pushOutside(c, p + dir * (len / NSt), shellT + w + 0.0015f, MK_HEAD);
                }
                RopeSt s;
                s.p = p;
                s.t = dir;
                vec3 up = nq - dir * dot(nq, dir);
                s.up = length2(up) > 1e-8f ? normalize(up) : normalize(anyPerp(dir));
                s.bp = q;
                s.w = s.h = w * (k == 0 ? 0.8f : (k == NSt ? 0.85f : 1.f));
                s.u = len * k / NSt;
                s.sw = skin1(B_HEAD);
                if (k == 0) {   // the root keeps the head parametrization
                    s.th = th;
                    s.ph = php;
                }
                S.push_back(s);
            }
            const float tone = Lerp(0.88f, 1.1f, ropeRand(seed + 6u));
            emitRope(m, S, 4, false, h.ropeCol * tone, h.ropeTip * tone, true, 0);
        }
    }
}

// Long curls: coils hanging from round the sides and back of the head, over the crown's curly volume. Two rows: the
// lower one close in, the upper one falling over it (the volume); each coil is a rope wound round its hanging line
// (three stations a turn), tightening a little towards the end.
static void buildCoils(OutfitCtx& o, const HairParams& h) {
    BuildCtx& c = o.c;
    const float hs = c.D->headS;
    Rng r(hash32(c.d->seed * 0x5851F42Du + 0x3Bu));
    MeshB m;
    std::vector<RopeSt> S;
    const float thA = 56.f * kDegToRad;   // from the temples (framing the face) round the back
    for (int row = 0; row < 2; row++) {
        const int K = row ? 8 : 10;
        for (int i = 0; i < K; i++) {
            const float u = ((float)i + 0.5f + 0.5f * row) / (K + 0.5f * row);
            const float th = thA + (kTwoPi - 2.f * thA) * u + r.range(-0.04f, 0.04f);
            const float at = th > kPi ? kTwoPi - th : th;
            const float ph = hairlinePhi(h, at) + (row ? 26.f : 9.f) * kDegToRad;
            vec3 q, n;
            headSurf(c, th, ph, q, n);
            const float T = styleThickness(c, h, headProbe(th, ph, q));   // the curly volume there
            RopeSpec R;
            R.seed = r.next();
            R.w = R.h = 0.0105f * hs * Lerp(0.85f, 1.15f, ropeRand(R.seed));   // a clump of curl, not a single strand
            const float pitch = 0.036f * hs * Lerp(0.85f, 1.2f, ropeRand(R.seed + 1u)), helixR = 0.008f * hs;
            R.stepH = pitch / 3.f;
            R.clear = row ? 0.012f : 0.004f;
            R.flare = 0.3f;
            R.hang = h.curlLen * Lerp(0.75f, 1.15f, ropeRand(R.seed + 2u));
            RopeSt s0;
            s0.p = q + n * (T * 0.85f + R.h);
            vec3 out = normalize(vec3(n.x, n.y, 0.f) + vec3(0.f, 0.f, 1e-4f));
            s0.t = normalize(vec3(0.f, 0.f, -1.f) + out * 0.3f);
            s0.up = n;
            s0.bp = q;
            s0.w = s0.h = R.w * 0.8f;
            s0.th = th;
            s0.ph = ph;
            s0.sw = skin1(B_HEAD);
            S.assign(1, s0);
            hangRope(c, R, T, S);
            // wind it: each station off its hanging line round a helix, then the tangents along the coil
            const float phase = kTwoPi * ropeRand(R.seed + 3u), dirSign = ropeRand(R.seed + 4u) < 0.5f ? 1.f : -1.f;
            for (RopeSt& st : S) {
                vec3 a = cross(st.t, st.up);
                a = length2(a) > 1e-10f ? normalize(a) : normalize(anyPerp(st.t));
                vec3 b = normalize(cross(st.t, a));
                const float ang = phase + dirSign * kTwoPi * st.u / pitch;
                const float rr = helixR * sstep(0.f, 0.025f, st.u) * Lerp(1.f, 0.75f, sstep(0.f, R.hang, st.u));
                st.p += (a * cosf(ang) + b * sinf(ang)) * rr;
            }
            for (size_t j = 0; j < S.size(); j++) {
                vec3 tg = S[Min(j + 1, S.size() - 1)].p - S[j > 0 ? j - 1 : 0].p;
                if (length2(tg) > 1e-12f) S[j].t = normalize(tg);
            }
            const float tone = Lerp(0.88f, 1.12f, ropeRand(R.seed + 5u));
            emitRope(m, S, 4, false, h.col * tone, h.col * tone * 1.12f, true, 0);
        }
    }
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
}

static void buildRopes(OutfitCtx& o, const HairParams& h, float shellT) {
    MeshB m;
    if (h.rope == ROPE_CORNROWS) buildCornrows(o, h, m);
    else if (h.rope == ROPE_TWISTS) buildTwists(o, h, shellT, m);
    else buildHangingRopes(o, h, shellT, m);
    size_t t0 = o.out.idx.size() / 3;
    o.out.append(m);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
    if (h.rope == ROPE_CORNROWS) {
        OutfitCtx::Layer L;   // a hat hides the rows it covers
        L.cov = [](const BVert&) { return 1.f; };
        L.margin = 0.f;
        L.t0 = t0;
        L.t1 = o.out.idx.size() / 3;
        o.layers.push_back(L);
    }
}

// ------------------------------------------------------------------------------------------------
// Facial hair

// One layer of beard or stubble cards: length (x the beard's length), segments, card width (m), cards per square
// centimetre, strand density (card alpha), root and tip height over the skin (x the beard shell's thickness), the
// renderer's DEPTH bits, and how far out past the beard's edge (coverage, m) its roots may sit.
struct BeardLayer {
    float lenK;
    int segs;
    float width, perCm2, alpha, hRoot, hTip;
    u32 depth;
    float cvMin;
};

// Facial hair strand cards: roots scattered over the skin by area (independent of the head grid's spacing), thinning
// out over the beard's edge (fewer, shorter, sparser hairs), combed along the beard's growth: the mustache down and out
// from the philtrum, the chin down and a little forward, the cheeks and the jaw down, under the jaw back and down the
// neck. Each card is skinned like the skin under its root (barycentric blend), so the beard rides on the jaw and lips.
static void beardCards(BuildCtx& c, MeshB& cm, const std::function<float(const BVert&)>& cov, const std::function<float(const BVert&)>& thickAt,
                       const BeardLayer* layers, int nl, float lenBase, vec3 colRoot, vec3 colTip, u32 seed) {
    const HeadInfo& H = c.head;
    const float hs = c.D->headS;
    const float deg = kDegToRad;
    const int NC = H.cols;
    const float thMC = H.thetaMouth, phM = H.phiMouth;
    float cvMinAll = 0.f;
    for (int l = 0; l < nl; l++) cvMinAll = Min(cvMinAll, layers[l].cvMin);
    // coverage and the shell's thickness per grid vertex
    std::vector<float> cvv((size_t)H.rows * NC, -1.f), tv((size_t)H.rows * NC, 0.f);
    for (int j = 0; j < H.rows; j++)
        for (int k = 0; k < NC; k++) {
            const BVert& v = c.m.v[H.grid[(size_t)j * NC + k]];
            size_t gi = (size_t)j * NC + k;
            cvv[gi] = cov(v);
            if (cvv[gi] > cvMinAll - 0.004f) tv[gi] = thickAt(v);
        }
    // candidate triangles (the grid's quads split in two; the mouth slit's and the eye fissures' rows left out)
    struct Tri {
        u32 g[3];
        float area;
    };
    std::vector<Tri> tris;
    for (int j = 1; j + 1 < H.rows; j++) {
        if (j == H.rowMouthLo || j == H.rowEyeLo) continue;
        for (int k = 0; k < NC; k++) {
            int k1 = (k + 1) % NC;
            u32 q[4] = {(u32)(j * NC + k), (u32)(j * NC + k1), (u32)((j + 1) * NC + k1), (u32)((j + 1) * NC + k)};
            for (int h = 0; h < 2; h++) {
                Tri t;
                t.g[0] = q[0];
                t.g[1] = q[1 + h];
                t.g[2] = q[2 + h];
                if (Max(cvv[t.g[0]], Max(cvv[t.g[1]], cvv[t.g[2]])) < cvMinAll) continue;
                vec3 a = c.m.v[H.grid[t.g[0]]].p, b = c.m.v[H.grid[t.g[1]]].p, cc = c.m.v[H.grid[t.g[2]]].p;
                t.area = 0.5f * length(cross(b - a, cc - a));
                if (t.area <= 0.f) continue;
                tris.push_back(t);
            }
        }
    }
    if (tris.empty()) return;
    Rng r(seed);
    CardPt pts[4];
    for (int l = 0; l < nl; l++) {
        const BeardLayer& L = layers[l];
        // each triangle gets its share of the roots (stochastic rounding): a jittered-grid spread, without the clumps and
        // gaps of independent draws
        for (size_t ti = 0; ti < tris.size(); ti++) {
          const Tri& t = tris[ti];
          const int cnt = (int)(t.area * 1e4f * L.perCm2 + r.f());
          for (int i = 0; i < cnt; i++) {
            float r1 = r.f(), r2 = r.f();
            float sq = sqrtf(r1);
            float bw[3] = {1.f - sq, sq * (1.f - r2), sq * r2};
            vec3 p(0.f), nn(0.f);
            float cvs = 0.f, T = 0.f;
            WAcc acc;
            for (int e = 0; e < 3; e++) {
                const BVert& v = c.m.v[H.grid[t.g[e]]];
                p += v.p * bw[e];
                nn += v.n * bw[e];
                cvs += cvv[t.g[e]] * bw[e];
                T += tv[t.g[e]] * bw[e];
                for (int q = 0; q < 4; q++) acc.add(v.sw.b[q], v.sw.w[q] * bw[e]);
            }
            // thinning out over the edge: fewer roots, shorter and sparser hairs
            float edge = sstep(L.cvMin, L.cvMin + 0.008f, cvs);
            float keep = r.f();
            u32 cardSeed = r.next();
            float lenR = r.range(0.75f, 1.25f), wR = r.range(0.8f, 1.2f);
            if (keep > 0.15f + 0.85f * edge) continue;
            nn = length2(nn) > 1e-12f ? normalize(nn) : vec3(0, 1, 0);
            vec3 d = p - H.C;
            float th = atan2f(d.x, d.y), at = fabsf(th);
            float ph = atan2f(d.z, sqrtf(d.x * d.x + d.y * d.y));
            float sx = d.x >= 0.f ? 1.f : -1.f;
            bool mzone = ph > phM + 1.5f * deg && at < thMC * 1.35f;
            float chinFwd = sstep(40.f * deg, 10.f * deg, at) * sstep(-30.f * deg, -50.f * deg, ph);
            float under = sstep(-0.3f, -0.7f, nn.z);
            vec3 f0 = mzone ? vec3(sx * (0.25f + 0.6f * sstep(2.f * deg, 8.f * deg, at)), 0.15f, -1.f)
                            : lerp(vec3(sx * 0.12f, 0.15f + 0.3f * chinFwd, -1.f), vec3(0.f, -1.f, -0.45f), under);
            float len = lenBase * hs * L.lenK * lenR * (0.55f + 0.45f * edge);
            float seg = len / L.segs;
            vec3 q = p, nq = nn;
            float thq = th < 0.f ? th + kTwoPi : th, phq = ph;
            SkinW sw = acc.finish();
            int np = 0;
            for (int sgi = 0; sgi <= L.segs; sgi++) {
                float u = (float)sgi / L.segs;
                float hgt = 0.0003f * hs + T * Lerp(L.hRoot, L.hTip, u) + (L.depth == 0u ? 0.12f * len * u * u : 0.f);
                pts[np].p = q + nq * hgt;
                pts[np].n = nq;
                pts[np].w = L.width * hs * wR * (1.f - 0.3f * u) * (0.7f + 0.3f * edge);
                pts[np].sw = sw;
                np++;
                if (sgi == L.segs) break;
                vec3 f = f0 - nq * dot(f0, nq);
                if (length2(f) < 1e-10f) break;
                vec3 q1, n1;
                scalpStep(c, q, normalize(f), seg, thq, phq, q1, n1);
                q = q1;
                nq = n1;
            }
            if (np >= 2) setCardDepth(cm, emitCard(cm, pts, np, CARD_BEARD, cardSeed, colRoot, colTip, L.alpha * (0.6f + 0.4f * edge), PART_HEAD, &H), L.depth);
          }
        }
    }
}

static void buildFacialHair(OutfitCtx& o) {
    BuildCtx& c = o.c;
    const CharacterDesc& d = *c.d;
    if (d.gender != MALE) return;
    const HeadInfo& H = c.head;
    const float hs = c.D->headS;
    const int kind = d.facialHair;
    const float deg = kDegToRad;
    const float thMC = H.thetaMouth;
    vec3 fcol = d.hairColor * 0.85f;
    // beard region in head grid terms
    // head grid rows (face.cpp): lips from rowLipLo (skin under the lower vermilion) to rowLipHi (upper vermilion
    // border), the mustache over the philtrum up to the nose, the chin below the lower lip
    const float nrd = (float)(H.rows - 1);
    const int rLipLo = H.rowLipLo, rLipHi = H.rowLipHi;
    auto rowOf = [nrd](const BVert& v) { return (int)lrintf((v.pc - 1.2f) * nrd); };
    const float phM = H.phiMouth;
    // the vermilion's outline: the skin side of the upper border row and of the lower one, per grid column (signed
    // theta), out to the mouth corners
    struct LipEdge {
        float th, hi, lo;
    };
    std::vector<LipEdge> lipEdge;
    {
        const int NC = H.cols, rHi = H.rowLipHi, rLo = H.rowLipLo + 1;
        for (int k = 0; k < NC && rHi > 0 && rLo > 0; k++) {
            const BVert& a = c.m.v[H.grid[(size_t)rHi * NC + k]];
            const BVert& b = c.m.v[H.grid[(size_t)rLo * NC + k]];
            float th = a.pa > kPi ? a.pa - kTwoPi : a.pa;
            if (fabsf(th) <= thMC) lipEdge.push_back({th, a.pb, b.pb});
        }
        std::sort(lipEdge.begin(), lipEdge.end(), [](const LipEdge& x, const LipEdge& y) { return x.th < y.th; });
    }
    // how far a point lies outside the vermilion (about metres: angle x 0.1; negative on the lips): above the upper
    // border, below the lower one or beyond the mouth corner
    auto lipDist = [=](const BVert& v) -> float {
        float th = v.pa > kPi ? v.pa - kTwoPi : v.pa;
        float at = fabsf(th);
        float d = (at - thMC) * cosf(phM);
        if (!lipEdge.empty()) {
            float hi = lipEdge.front().hi, lo = lipEdge.front().lo;
            if (th >= lipEdge.back().th) {
                hi = lipEdge.back().hi;
                lo = lipEdge.back().lo;
            } else if (th > lipEdge.front().th) {
                size_t i = 1;
                while (i + 1 < lipEdge.size() && lipEdge[i].th < th) i++;
                const LipEdge &e0 = lipEdge[i - 1], &e1 = lipEdge[i];
                float u = Saturate((th - e0.th) / Max(e1.th - e0.th, 1e-6f));
                hi = Lerp(e0.hi, e1.hi, u);
                lo = Lerp(e0.lo, e1.lo, u);
            }
            d = Max(d, Max(v.pb - hi, lo - v.pb));
        }
        return d * 0.1f;
    };
    // the lips' clearance (in coverage units): the mustache reaches the vermilion border (its hairs hang over it), under
    // the lower lip the beard begins just below the border and fills in over the next few millimetres
    auto lipClear = [=](const BVert& v) -> float {
        float d = lipDist(v);
        return v.pb > phM ? (d - 0.0002f) * 6.f : (d - 0.0006f) * 5.f;
    };
    auto region = [=](const BVert& v, bool mustache, bool chin, bool cheeks, bool cap = true) -> float {
        if (v.part != PART_HEAD || v.pc < 1.2f) return -1.f;
        float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
        int j = rowOf(v);
        float best = -1.f;
        // clearance round the lips, so the beard ends at the vermilion instead of in a step over it
        float lipCap = lipClear(v);
        // mustache: from the upper lip's vermilion border up the philtrum within the mouth span (+ margin)
        if (mustache) {
            float m = (j >= rLipHi && j <= rLipHi + 3) ? Min((thMC + 5.f * deg - at) * 0.1f, 0.01f) : -1.f;
            best = Max(best, m);
        }
        // chin: under the lower lip, narrow
        if (chin) {
            float c1 = Min((22.f * deg - at) * 0.1f, j <= rLipLo && j >= 1 ? 0.01f : -1.f);
            // connect mustache to chin around the mouth corners
            float c2 = Min((thMC + 6.f * deg - at) * 0.1f, Min(((thMC + 6.f * deg) - fabsf(at - thMC - 2.f * deg) * 4.f) * 0.1f,
                                                                (j >= rLipLo && j <= rLipHi) ? 0.01f : -1.f));
            best = Max(best, Max(c1, at > thMC * 0.85f ? c2 : -1.f));
        }
        // cheeks + jaw + under the chin: up to a line from the sideburn to the mouth corner
        if (cheeks) {
            float phTop = Lerp(-22.f, 6.f, sstep(thMC * kRadToDeg, 72.f, at * kRadToDeg)) * deg;
            float cov = Min((phTop - v.pb) * 0.1f, (80.f * deg - at) * 0.1f);
            // keep lips clear
            bool lip = j > rLipLo && j <= rLipHi && at < thMC * 1.15f;
            if (lip) cov = -1.f;
            if (j < 1) cov = -1.f;
            best = Max(best, cov);
        }
        return cap ? Min(best, lipCap) : best;
    };
    if (kind < 0) {
        // clean-shaven: many men still show a faint shadow of the shaved beard (darker hair shows more)
        Rng rs(hash32(d.seed * 577u + 41u));
        float dark = 1.f - sstep(0.08f, 0.35f, dot(d.hairColor, vec3(0.3f, 0.59f, 0.11f)));
        if (d.age > 0.03f && rs.chance(0.5f)) {
            float amt = rs.range(0.1f, 0.26f) * (0.4f + 0.6f * dark);
            for (size_t i = 0; i < c.m.v.size(); i++) {
                BVert& v = c.m.v[i];
                float cv = region(v, true, true, true);
                if (cv > -0.004f) v.col = lerp(v.col, mulColor(v.col, vec3(0.74f, 0.76f, 0.82f)) + fcol * 0.06f, amt * sstep(-0.004f, 0.008f, cv));
            }
        }
        return;
    }
    if (kind == FH_STUBBLE) {
        // a few days' growth: the skin shadowed by the hairs (all the distant LODs keep), and on the full mesh the
        // short hairs themselves, lying close along the beard's growth and thinning out over its edge
        for (size_t i = 0; i < c.m.v.size(); i++) {
            BVert& v = c.m.v[i];
            float cv = region(v, true, true, true);
            if (cv > -0.004f) v.col = lerp(v.col, mulColor(v.col, vec3(0.6f)) + fcol * 0.22f, 0.42f * sstep(-0.004f, 0.006f, cv));
        }
        MeshB cm;
        auto cov = [=](const BVert& v) { return region(v, true, true, true); };
        auto thickAt = [](const BVert&) { return 0.f; };
        const BeardLayer stub = {1.f, 1, 0.0032f, 5.f, 0.75f, 0.f, 0.f, 6u, -0.002f};
        beardCards(c, cm, cov, thickAt, &stub, 1, 0.0028f, fcol * 0.8f, vmax(fcol * 1.1f, vec3(0.03f, 0.024f, 0.02f)), hash32(d.seed * 0x1F83D9ABu + 0x5Bu));
        size_t t0 = o.out.idx.size() / 3;
        o.out.append(cm);
        o.hideOut.resize(o.out.idx.size() / 3, 0);
        OutfitCtx::Layer L;
        L.cov = [](const BVert&) { return 1.f; };
        L.margin = 0.f;
        L.t0 = t0;
        L.t1 = o.out.idx.size() / 3;
        o.layers.push_back(L);
        return;
    }
    GarmentDef g;
    g.parts = 1u << PART_HEAD;
    g.mat = MAT_HAIR;
    g.col = fcol;
    g.hem = false;
    g.hideMargin = 0.006f;
    g.thick = 0.001f;
    g.smooth = 1;
    bool must = true, chin = kind == FH_GOATEE || kind == FH_BEARD || kind == FH_SHORTBEARD, cheeks = kind == FH_BEARD || kind == FH_SHORTBEARD;
    // the shell is the dense core only: it ends a few millimetres inside the beard's edge, where the strand cards over
    // the tinted skin carry the outline (a shell's own edge reads as a cut line)
    const float inset = kind == FH_BEARD ? 0.0035f : (kind == FH_SHORTBEARD ? 0.0028f : 0.0015f);   // (narrow mustaches and goatees keep more)
    const float skinShow = kind == FH_BEARD ? 0.1f : (kind == FH_SHORTBEARD ? 0.28f : 0.18f);
    // (the inset is from the beard's outer edge only: next to the lips the shell runs up to the lips' clearance)
    auto shellCov = [=](const BVert& v) { return v.part != PART_HEAD || v.pc < 1.2f ? -1.f : Min(region(v, must, chin, cheeks, false) - inset, lipClear(v)); };
    g.cov = shellCov;
    // the shell is the dense inner volume (darker, thin at its edge); the strand cards on it carry the soft outline
    // (a full beard: ~5 mm of volume, more under the chin, tapering to nothing over ~2 cm at the cheek line)
    float base = kind == FH_BEARD ? 0.0052f : (kind == FH_SHORTBEARD ? 0.003f : 0.003f);
    g.extraFn = [=](const BVert& v) -> float {
        float cv = shellCov(v);
        float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
        float chinBoost = kind == FH_BEARD ? 0.0045f * sstep(35.f * deg, 5.f * deg, at) * sstep(-35.f * deg, -55.f * deg, v.pb) : 0.f;
        return (base + chinBoost) * hs * sstep(0.f, kind == FH_BEARD ? 0.02f : 0.014f, cv);
    };
    // the shell fades into the skin colour over its outer ~8 mm (the strand cards carry the outline), so the beard
    // edge is soft rather than a painted-on patch
    g.colFn = [=](const BVert& v, vec3 cc) {
        // clumpy brightness (a beard is never one flat tone) with light scattered through it: on black hair the
        // strands' sheen and dark-brown tips keep it from reading as a painted mask
        float hv = hashToFloat(hash32((u32)(v.bp.x * 9000.f) ^ (u32)(v.bp.z * 7000.f) * 2654435761u));
        float strands = 0.5f + 0.5f * sinf(v.bp.x * 2300.f + 3.f * sinf(v.bp.z * 700.f));   // fine vertical streaks
        vec3 hairC = cc * 0.8f * (0.7f + 0.65f * hv) + vec3(0.016f, 0.012f, 0.009f) * (0.4f + 0.8f * hv + 0.6f * strands);
        // the edge: sparse (skin shows between the hairs), broken up per vertex rather than a smooth airbrushed fade
        float cv = shellCov(v);
        float t = sstep(0.0f, 0.013f, cv);
        float grain = hashToFloat(hash32((u32)(v.bp.x * 12000.f) * 73856093u ^ (u32)(v.bp.y * 12000.f) * 19349663u ^ (u32)(v.bp.z * 12000.f)));
        t = Saturate(t + (grain - 0.5f) * 0.6f * (1.f - t) * sstep(-0.002f, 0.004f, cv));
        // the skin shows between the hairs (more through a short beard), so the beard is never a flat painted mask
        vec3 inner = lerp(hairC, v.col * 0.6f, skinShow);
        return lerp(lerp(v.col, hairC, 0.4f), inner, t);
    };
    // tint the skin under the beard edge (stubble-like: the skin darkens towards the hair without turning into it)
    for (size_t i = 0; i < c.surfaceIdxEnd; i++) {
        if (i >= c.m.v.size()) break;
        BVert& v = c.m.v[i];
        float cv = region(v, must, chin, cheeks);
        if (cv > -0.006f) v.col = lerp(v.col, mulColor(v.col, vec3(0.6f)) + fcol * 0.25f, 0.6f * sstep(-0.006f, 0.005f, cv));
    }
    const size_t beardV0 = o.out.v.size();
    emitGarment(o, g);
    // (the far LOD paints the beard onto the face and drops this shell: see stripForLod)
    for (size_t i = beardV0; i < o.out.v.size(); i++) o.out.v[i].flags |= BuildCtx::F_BEARD;
    // strand cards over the beard (beardCards): an inner layer of short hairs on the shell, the main layer, and on full
    // beards sparse longer hairs whose tips stand off it (the soft outline)
    MeshB cm;
    const vec3 tipCol = vmax(fcol * 1.15f, vec3(0.045f, 0.036f, 0.028f));   // strand tips catch the light (black hair reads dark brown at the ends)
    const float lenBase = kind == FH_BEARD ? 0.014f : (kind == FH_SHORTBEARD ? 0.0065f : (kind == FH_MUSTACHE ? 0.0095f : 0.011f));
    // (the strand cards thin out over the last few millimetres before the lips, where the dense shell carries the
    // beard: cards rooted right at the vermilion stand up off the lip line and catch the sky as a grey ring)
    auto cov = [=](const BVert& v) {
        float d = lipDist(v);
        return Min(region(v, must, chin, cheeks, false), v.pb > phM ? (d - 0.0008f) * 2.2f : (d - 0.0012f) * 2.f);
    };
    auto thickAt = [&](const BVert& v) { return g.thick + g.extraFn(v); };
    BeardLayer layers[3];
    int nl = 0;
    layers[nl++] = {0.45f, 1, 0.0058f, kind == FH_BEARD ? 1.6f : 1.4f, 0.9f, 0.3f, 0.55f, 5u, -0.003f};
    layers[nl++] = {1.0f, 2, 0.0068f, kind == FH_BEARD ? 1.5f : 1.3f, 0.75f, 0.35f, 1.05f, 2u, -0.0015f};
    if (kind == FH_BEARD || kind == FH_GOATEE) layers[nl++] = {1.3f, 3, 0.0052f, kind == FH_BEARD ? 0.5f : 0.35f, 0.45f, 0.6f, 1.4f, 0u, 0.002f};
    beardCards(c, cm, cov, thickAt, layers, nl, lenBase, fcol * 0.6f, tipCol, hash32(d.seed * 389u + 11u));
    size_t t0 = o.out.idx.size() / 3;
    o.out.append(cm);
    o.hideOut.resize(o.out.idx.size() / 3, 0);
    OutfitCtx::Layer L;
    L.cov = [](const BVert&) { return 1.f; };
    L.margin = 0.f;
    L.t0 = t0;
    L.t1 = o.out.idx.size() / 3;
    o.layers.push_back(L);
}

// ------------------------------------------------------------------------------------------------

void buildHairLayer(OutfitCtx& o) {
    BuildCtx& c = o.c;
    const CharacterDesc& d = *c.d;
    HairParams h = hairParams(c);
    const float hs = c.D->headS;
    buildFacialHair(o);
    if (h.style == HAIR_BALD) {
        // slight shadow of shaved hair on the sides for older men
        return;
    }
    // tint the scalp under the hair so thin edges read as hair: full under the hair, fading out over the last few
    // millimetres in front of the hairline (fine short hairs), broken up so the edge is not an airbrushed band
    const u32 tintSeed = hash32(d.seed * 0x68E31DA4u + 0x3Bu);
    for (size_t i = 0; i < c.m.v.size(); i++) {
        BVert& v = c.m.v[i];
        if (v.part != PART_HEAD || v.pc < 1.2f) continue;
        float cv = hairCoverage(c, h, v);
        float at = v.pa > kPi ? kTwoPi - v.pa : v.pa;
        // a fade shows the skin through the clippered sides, darkening up towards the full-length hair
        float keep = fadeKeep(h, at, v.pb);
        float amt = (h.style == HAIR_BUZZ ? 0.8f : 0.6f) * Lerp(0.25f, 1.f, keep);
        if (h.rope == ROPE_CORNROWS) amt = 0.3f;   // the partings between the rows: bare scalp, a little shadowed
        float grain = hashToFloat(hash32((u32)i * 0x9E3779B1u ^ tintSeed));
        float edge = sstep(-0.0045f, 0.007f, cv);
        edge = Saturate(edge + (grain - 0.5f) * 0.5f * edge * (1.f - edge) * 4.f);
        if (cv > -0.0045f) v.col = lerp(v.col, h.col * 0.75f, amt * edge);
        // the line-up: a crisp, dense edge along the front hairline and the temples
        if (h.lineUp && at < 80.f * kDegToRad && cv > -0.0005f) v.col = lerp(v.col, h.col * 0.6f, 0.8f * (1.f - sstep(0.0015f, 0.005f, cv)));
    }
    GarmentDef g;
    g.parts = 1u << PART_HEAD;
    g.mat = MAT_HAIR;
    g.col = h.col;
    g.thick = 0.0008f;
    g.hem = false;
    g.swapUV = true;
    g.hideMargin = 0.014f;
    g.smooth = h.style == HAIR_CURLY ? 4 : 2;
    const BuildCtx* cp = &c;
    HairParams hp = h;
    g.cov = [=](const BVert& v) { return hairCoverage(*cp, hp, v); };
    // styles with strand cards: the shell sits a little lower and darker (the inner volume, in the cards' shade)
    const bool cards = h.style != HAIR_BUZZ && h.rope < 0;
    const bool ropes = h.rope >= 0;
    const float shellFrac = cards ? 0.82f : 1.f;
    g.extraFn = [=](const BVert& v) -> float {
        float cv = hairCoverage(*cp, hp, v);
        float T = styleThickness(*cp, hp, v);
        return T * shellFrac * sstep(0.f, hairRampLen(T), cv);
    };
    g.colFn = [=](const BVert& v, vec3 cc) {
        float n = hashToFloat(hash32((u32)(v.bp.x * 7000.f) * 2654435761u ^ (u32)(v.bp.y * 6000.f) ^ (u32)(v.bp.z * 5000.f) * 40503u));
        // (under braids and locs the shell is the hair of the sections beneath them: in their shade)
        vec3 hairC = cc * (0.82f + 0.3f * n) * (cards ? kCardShellShade : (ropes ? 0.7f : 1.f));
        // over its first few millimetres the shell takes on the (hair-tinted) skin under it, broken up per vertex, so
        // its edge is a thinning of hair rather than a cut line (the hairline's fine hairs lie over it)
        float cv = hairCoverage(*cp, hp, v);
        float t = sstep(0.f, 0.009f, cv);
        t = Saturate(t + (n - 0.5f) * 0.5f * t * (1.f - t) * 4.f);
        return lerp(lerp(v.col, hairC, 0.45f), hairC, t);
    };
    size_t shellV0 = o.out.v.size();
    if (h.rope != ROPE_CORNROWS) emitGarment(o, g);   // cornrows lie on the bare scalp
    if (cards) {
        // (the skin-blended edge is left out: the LODs brighten the card-shaded shell back by 1 / kCardShellShade)
        for (size_t i = shellV0; i < o.out.v.size(); i++)
            if (hairCoverage(c, h, o.out.v[i]) > 0.009f) o.out.v[i].flags |= BuildCtx::F_CARDSHELL;
        buildScalpCards(o, h, shellFrac);
    }
    // styles with draped parts
    const HeadInfo& H = c.head;
    Rng r(hash32(d.seed * 157u + 3u));
    if (h.style == HAIR_LONG) addCurtain(o, h, 62.f * kDegToRad, r.range(0.2f, 0.34f) * hs / 1.0f, 0.001f, 0.05f, 0.25f);
    if (h.style == HAIR_BOB) addCurtain(o, h, 48.f * kDegToRad, 0.075f * hs, 0.002f, 0.12f, 0.1f);
    if (h.style == HAIR_PONYTAIL) {
        float ph = 22.f * kDegToRad;
        vec3 dir(0, -cosf(ph), sinf(ph));
        float t0 = c.sdf.castOut(H.C, dir, MK_HEAD, 0.25f * hs);
        vec3 start = H.C + dir * (t0 + 0.004f * hs);
        addHairTube(o, h, start, vec3(0, -1, -0.3f), r.range(0.2f, 0.3f), 0.018f * hs, 0.024f * hs, 0.006f * hs, 10, true);
    }
    if (h.style == HAIR_BUN) addBun(o, h);
    if (h.style == HAIR_CURLY && h.curlLen > 0.f) {
        // the mass of the hair under the coils (in their shade), then the coils over it
        HairParams hc = h;
        hc.col = h.col * 0.7f;
        addCurtain(o, hc, 78.f * kDegToRad, h.curlLen * 0.8f * hs, 0.002f, 0.2f, 0.2f, false);
        buildCoils(o, h);
    }
    if (ropes) {
        // cornrows, box braids, locs, twists (the ropes rest on the shell: its thickness plus the lumps)
        const float shellT = (h.rope == ROPE_BOX ? 0.0068f : (h.rope == ROPE_LOCS ? 0.0098f : (h.rope == ROPE_TWISTS ? 0.0042f : 0.f))) * 1.12f * hs;
        buildRopes(o, h, shellT);
    }
    // long hair covers the ears
    if (h.coversEars) {
        for (size_t t = c.surfaceIdxEnd; t + 2 < c.m.idx.size(); t += 3)
            if (c.m.v[c.m.idx[t]].part == PART_EAR) o.hideBody[t / 3] = 1;
    }
}

}  // namespace detail
}  // namespace Anim
