// SkyLine metro geometry built per streaming cell through the site system: segmental box-girder viaduct with slab
// track, running rails, third rail, cable troughs, parapets and a night-lit fascia strip; hammerhead piers and straddle
// bents; stations with platforms, a wing roof, lighting, benches, signage and route maps, stairs + escalators to the
// street with fare gates, ticket machines and entrance totems. Detail cells add collision, props and lights; far cells
// get a cheap shell.
#include "transit.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace transit_mesh {

using namespace sitegeo;
using namespace transit_dims;

// Brand and material palette
const vec3 kTeal(0.02f, 0.46f, 0.52f);          // SkyLine teal (signage, fascia, gates)
const vec3 kTealGlow(0.15f, 0.95f, 1.0f);       // fascia LED at night
const vec3 kSignText(0.97f, 0.97f, 0.95f);
inline u32 cConcrete(float v = 1.f) { return rgb(0.9f * v, 0.89f * v, 0.86f * v); }

// Corridor frame at arc length s: origin on the centerline at top-of-rail height, tangent (with grade), right (outer,
// banked) and up (banked)
struct TF {
    vec3 o, t, r, u;
    float s;
    vec3 at(float lateral, float up) const { return o + r * lateral + u * up; }
};

TF frameAt(const MetroLine& L, float s) {
    TF f;
    f.s = s;
    vec2 pos, tan;
    float z, bk;
    L.frame(s, pos, tan, &z, &bk);
    float z2 = L.railZ(s + 1.f), z1 = L.railZ(s - 1.f);
    f.t = normalize(vec3(tan, (z2 - z1) * 0.5f));
    vec3 r0 = normalize(cross(f.t, vec3(0, 0, 1)));
    vec3 u0 = cross(r0, f.t);
    f.r = r0 * cosf(bk) + u0 * sinf(bk);
    f.u = u0 * cosf(bk) - r0 * sinf(bk);
    f.o = vec3(pos, z);
    return f;
}

// Quad between two corridor frames for the profile edge (n0,v0) -> (n1,v1); `facing` is the outward direction in the
// profile plane (lateral, up); uv: u along the corridor (meters), v along the profile edge
void sweep(G& g, MeshData& m, const TF& a, const TF& b, float n0, float v0, float n1, float v1, vec2 facing, u32 col, u32 mat, float vOff = 0.f) {
    vec3 p0 = a.at(n0, v0), p1 = b.at(n0, v0), p2 = b.at(n1, v1), p3 = a.at(n1, v1);
    vec3 f = (a.r + b.r) * (0.5f * facing.x) + (a.u + b.u) * (0.5f * facing.y);
    float el = sqrtf((n1 - n0) * (n1 - n0) + (v1 - v0) * (v1 - v0));
    m.quadFacing(p0 - g.org, p1 - g.org, p2 - g.org, p3 - g.org, vec2(a.s, vOff), vec2(b.s, vOff), vec2(b.s, vOff + el), vec2(a.s, vOff + el), col, mat, f);
}

// Oriented box aligned with a corridor frame (center given as lateral/up offsets at the mid frame)
void frameBox(G& g, const TF& f, float lateral, float up, vec3 he, u32 col, u32 mat, bool bottom = false) {
    g.m->box(f.at(lateral, up) - g.org, f.t, f.r, f.u, he, col, mat, bottom);
}

float segmentShade(float s) { return 0.93f + 0.09f * hashToFloat(hash32((u32)(s * 0.3334f) * 2654435761u + 77u)); }

// Station zone blend at s: 0 plain viaduct, 1 full station deck (widening over 4 m past the platform ends)
float stationBlend(const MetroLine& L, float s, int* stIdx = nullptr) {
    float best = 0.f;
    for (int i = 0; i < (int)L.stations.size(); i++) {
        float d = fabsf(L.delta(L.stations[i].s, s));
        float w = SmoothStep(kPlatformHalfLen + 6.f, kPlatformHalfLen + 1.f, d);
        if (w > best) {
            best = w;
            if (stIdx) *stIdx = i;
        }
    }
    return best;
}

// ------------------------------------------------------------------------------------------------ viaduct segment
void deckSegment(G& g, const MetroLine& L, const TF& a, const TF& b, float wa, float wb, bool detail) {
    MeshData& m = *g.m;
    float sh = segmentShade((a.s + b.s) * 0.5f);
    u32 cGirder = cConcrete(sh), cDeck = rgb(0.62f * sh, 0.61f * sh, 0.58f * sh), cPara = cConcrete(sh * 1.04f);
    u32 mConc = M(MAT_CONCRETE), mPanel = M(MAT_CONCRETE_PANEL);
    const float dT = -kDeckTopBelowRail;           // deck top (relative to top of rail)
    const float dB = dT - kGirderDepth;             // girder bottom
    // cross-section half widths: the station deck widens the cantilevers and the box
    float halfA = Lerp(kDeckHalf, kStationHalf, wa), halfB = Lerp(kDeckHalf, kStationHalf, wb);
    float webTopA = Lerp(2.9f, 4.6f, wa), webTopB = Lerp(2.9f, 4.6f, wb);
    float webBotA = Lerp(2.3f, 3.8f, wa), webBotB = Lerp(2.3f, 3.8f, wb);
    float cantA = dT - Lerp(0.32f, 0.4f, wa), cantB = dT - Lerp(0.32f, 0.4f, wb);
    float botA = dB - 0.2f * wa, botB = dB - 0.2f * wb;
    auto sweepVar = [&](float n0a, float v0a, float n1a, float v1a, float n0b, float v0b, float n1b, float v1b, vec2 facing, u32 col, u32 mat) {
        vec3 p0 = a.at(n0a, v0a), p1 = b.at(n0b, v0b), p2 = b.at(n1b, v1b), p3 = a.at(n1a, v1a);
        vec3 f = (a.r + b.r) * (0.5f * facing.x) + (a.u + b.u) * (0.5f * facing.y);
        float el = length(p3 - p0);
        m.quadFacing(p0 - g.org, p1 - g.org, p2 - g.org, p3 - g.org, vec2(a.s, 0), vec2(b.s, 0), vec2(b.s, el), vec2(a.s, el), col, mat, f);
    };
    for (int sd = -1; sd <= 1; sd += 2) {
        float S = (float)sd;
        // cantilever underside (slightly haunched toward the web) and fascia
        sweepVar(S * webTopA, cantA - 0.12f, S * halfA, cantA, S * webTopB, cantB - 0.12f, S * halfB, cantB, vec2(0, -1), cGirder, mConc);
        sweepVar(S * halfA, cantA, S * halfA, dT, S * halfB, cantB, S * halfB, dT, vec2(S, 0), cGirder, mConc);
        // web (sloped) and bottom flange edge
        sweepVar(S * webTopA, cantA - 0.12f, S * webBotA, botA, S * webTopB, cantB - 0.12f, S * webBotB, botB, vec2(S, -0.25f), cGirder, mConc);
        // fascia LED strip (night only)
        if (wa < 0.5f && wb < 0.5f) {
            vec3 p0 = a.at(S * (halfA + 0.012f), dT - 0.1f), p1 = b.at(S * (halfB + 0.012f), dT - 0.1f);
            vec3 q0 = a.at(S * (halfA + 0.012f), dT - 0.16f), q1 = b.at(S * (halfB + 0.012f), dT - 0.16f);
            m.quadFacing(p0 - g.org, p1 - g.org, q1 - g.org, q0 - g.org, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgbv(kTealGlow, 0.55f),
                         emMat(EA_NIGHT), (a.r + b.r) * (0.5f * S));
        }
        // parapet (plain viaduct only; the station platforms have their own back walls)
        if (wa < 0.05f && wb < 0.05f) {
            float pi = kDeckHalf - 0.28f, po = kDeckHalf;
            sweep(g, m, a, b, S * pi, dT, S * pi, dT + kParapetH, vec2(-S, 0), cPara, mPanel);
            sweep(g, m, a, b, S * pi, dT + kParapetH, S * (po + 0.04f), dT + kParapetH, vec2(0, 1), cPara, mPanel);
            sweep(g, m, a, b, S * (po + 0.04f), dT + kParapetH, S * (po + 0.04f), dT + 0.05f, vec2(S, 0), cPara, mPanel);
            if (detail) {
                // cable trough with lid along the parapet
                float c0 = kDeckHalf - 0.9f, c1 = kDeckHalf - 0.3f;
                sweep(g, m, a, b, S * c0, dT, S * c0, dT + 0.28f, vec2(-S, 0), rgb(0.7f), mConc);
                sweep(g, m, a, b, S * c0, dT + 0.28f, S * c1, dT + 0.28f, vec2(0, 1), rgb(0.66f), mConc);
            }
        }
    }
    // girder bottom
    sweepVar(-webBotA, botA, webBotA, botA, -webBotB, botB, webBotB, botB, vec2(0, -1), cGirder, mConc);
    // deck top between and around the tracks
    float deckEdgeA = (wa < 0.05f && wb < 0.05f) ? kDeckHalf - 0.28f : kPlatformEdge;
    sweep(g, m, a, b, -deckEdgeA, dT, deckEdgeA, dT, vec2(0, 1), cDeck, mConc);
    // slab track plinths, rails, third rail
    for (int tr = -1; tr <= 1; tr += 2) {
        float c = tr * kTrackOffset;
        u32 cSlab = rgb(0.72f * sh, 0.71f * sh, 0.68f * sh);
        float sT = -0.24f;   // slab top below the top of rail
        sweep(g, m, a, b, c - 1.3f, sT, c + 1.3f, sT, vec2(0, 1), cSlab, mConc);
        sweep(g, m, a, b, c - 1.3f, dT, c - 1.3f, sT, vec2(-1, 0), cSlab, mConc);
        sweep(g, m, a, b, c + 1.3f, sT, c + 1.3f, dT, vec2(1, 0), cSlab, mConc);
        for (int rr = -1; rr <= 1; rr += 2) {
            float rc = c + rr * kGauge * 0.5f;
            if (detail) {
                sweep(g, m, a, b, rc - 0.036f, 0.f, rc + 0.036f, 0.f, vec2(0, 1), rgb(0.78f, 0.78f, 0.8f), M(MAT_METAL_BRUSHED));
                sweep(g, m, a, b, rc - 0.036f, sT, rc - 0.036f, 0.f, vec2(-1, 0), rgb(0.36f, 0.27f, 0.2f), M(MAT_METAL_PAINTED));
                sweep(g, m, a, b, rc + 0.036f, 0.f, rc + 0.036f, sT, vec2(1, 0), rgb(0.36f, 0.27f, 0.2f), M(MAT_METAL_PAINTED));
            } else {
                sweep(g, m, a, b, rc - 0.05f, 0.f, rc + 0.05f, 0.f, vec2(0, 1), rgb(0.5f, 0.47f, 0.44f), M(MAT_METAL_BRUSHED));
            }
        }
        if (detail) {
            // rail fastenings (twin-block sleepers cast into the slab) every 0.65 m
            TF mid = frameAt(L, (a.s + b.s) * 0.5f);
            float len = b.s - a.s;
            int n = Max(1, (int)(len / 0.65f));
            for (int k = 0; k < n; k++) {
                float s = a.s + (k + 0.5f) * len / n;
                TF f = frameAt(L, s);
                for (int rr = -1; rr <= 1; rr += 2)
                    frameBox(g, f, c + rr * kGauge * 0.5f, sT + 0.05f, vec3(0.09f, 0.26f, 0.05f), rgb(0.55f, 0.54f, 0.52f), mConc, false);
            }
            (void)mid;
            // third rail on the outer side of each track, under a yellow cover board on insulator posts
            float tc = c + tr * (kGauge * 0.5f + 0.72f);
            u32 cCover = rgb(0.85f, 0.7f, 0.12f);
            sweep(g, m, a, b, tc - 0.2f, 0.18f, tc + 0.2f, 0.18f, vec2(0, 1), cCover, M(MAT_PLASTIC));
            sweep(g, m, a, b, tc + tr * 0.2f, 0.18f, tc + tr * 0.2f, 0.02f, vec2((float)tr, 0), cCover, M(MAT_PLASTIC));
            sweep(g, m, a, b, tc - 0.05f, 0.1f, tc + 0.05f, 0.1f, vec2(0, 1), rgb(0.4f, 0.4f, 0.42f), M(MAT_METAL_BRUSHED));
            for (float s = ceilf(a.s / 4.f) * 4.f; s < b.s; s += 4.f) {
                TF f = frameAt(L, s);
                frameBox(g, f, tc, (sT + 0.1f) * 0.5f, vec3(0.06f, 0.06f, (0.1f - sT) * 0.5f), rgb(0.35f, 0.3f, 0.25f), M(MAT_PLASTIC));
            }
        }
    }
}

// Collision for a segment: walkable deck + parapets (station platforms are added by the station generator)
void deckCollision(G& g, const TF& a, const TF& b, float wBlend) {
    if (!g.col) return;
    vec3 c = (a.o + b.o) * 0.5f;
    vec2 ax = normalize(vec2(b.o.x - a.o.x, b.o.y - a.o.y));
    float len = length(vec2(b.o.x - a.o.x, b.o.y - a.o.y));
    float top = c.z - kDeckTopBelowRail;
    float half = Lerp(kDeckHalf, kStationHalf, wBlend);
    collide(g, vec3(c.x, c.y, top - 0.6f), ax, vec3(len * 0.5f + 0.05f, half, 0.6f));
    if (wBlend < 0.05f)
        for (int sd = -1; sd <= 1; sd += 2) {
            vec3 pc = (a.at(sd * (kDeckHalf - 0.14f), 0.f) + b.at(sd * (kDeckHalf - 0.14f), 0.f)) * 0.5f;
            collide(g, vec3(pc.x, pc.y, top + kParapetH * 0.5f), ax, vec3(len * 0.5f + 0.05f, 0.16f, kParapetH * 0.5f));
        }
}

// ------------------------------------------------------------------------------------------------ piers
void genPier(G& g, const MetroLine& L, const MetroPier& pr) {
    TF f = frameAt(L, pr.s);
    vec2 t2 = normalize(vec2(f.t.x, f.t.y)), r2(t2.y, -t2.x);
    float girderBot = f.o.z - kDeckTopBelowRail - kGirderDepth - (pr.station ? 0.2f : 0.f);
    float capH = pr.station ? 1.9f : 1.6f;
    float capTop = girderBot - 0.12f;        // bearings between the cap and the girder
    float capBot = capTop - capH;
    float gz = pr.groundZ;
    float wz = gMap->waterAt(f.o.x, f.o.y);
    bool water = wz > kNoWater + 1.f && wz > gz;
    float footZ = gz - 0.6f;
    u32 cCol = cConcrete(0.97f), cCap = cConcrete(1.0f);
    u32 mConc = M(MAT_CONCRETE);
    int seg = g.detail ? 20 : 8;
    auto column = [&](vec2 base, float rx, float ry) {
        // elliptical column (rx along the corridor, ry across), slight taper, with a plinth
        MeshData& m = *g.m;
        float h = capBot - footZ;
        u32 start = (u32)m.verts.size();
        for (int k = 0; k <= seg; k++) {
            float an = kTwoPi * k / seg;
            vec2 d = t2 * (cosf(an)) + r2 * (sinf(an));
            vec2 n2 = normalize(t2 * (cosf(an) / rx) + r2 * (sinf(an) / ry));
            vec2 p0 = base + t2 * (cosf(an) * rx) + r2 * (sinf(an) * ry);
            vec2 p1 = base + t2 * (cosf(an) * rx * 0.94f) + r2 * (sinf(an) * ry * 0.94f);
            float u = (float)k / seg * kTwoPi * (rx + ry) * 0.5f;
            m.addVertex(vec3(p0, footZ) - g.org, vec3(n2, 0), vec3(-n2.y, n2.x, 0), vec2(u, 0), cCol, mConc);
            m.addVertex(vec3(p1, capBot + 0.02f) - g.org, vec3(n2, 0), vec3(-n2.y, n2.x, 0), vec2(u, h), cCol, mConc);
            (void)d;
        }
        for (int k = 0; k < seg; k++) {
            u32 i0 = start + k * 2;
            m.quadIdx(i0, i0 + 2, i0 + 3, i0 + 1);
        }
        if (g.col) collide(g, vec3(base, (footZ + capBot) * 0.5f), t2, vec3(rx * 0.97f, ry * 0.97f, (capBot - footZ) * 0.5f));
        if (g.detail && !water) {
            // plinth ring and a light grime band at the foot
            g.m->box(vec3(base, gz + 0.12f) - g.org, vec3(t2, 0), vec3(r2, 0), vec3(0, 0, 1), vec3(rx + 0.12f, ry + 0.12f, 0.14f), cConcrete(0.8f), mConc, false);
        }
    };
    auto capBeam = [&](vec2 c, float half, float thick) {
        // hammerhead: trapezoid in the cross-section (narrow at the column, wide under the girder)
        MeshData& m = *g.m;
        float bw = pr.bent ? half : 1.3f;
        vec3 T3(t2, 0), R3(r2, 0);
        vec3 A0 = vec3(c, capBot) - R3 * bw, A1 = vec3(c, capBot) + R3 * bw;
        vec3 B0 = vec3(c, capTop) - R3 * half, B1 = vec3(c, capTop) + R3 * half;
        vec3 d = T3 * thick;
        // front / back faces
        for (int fb = -1; fb <= 1; fb += 2) {
            vec3 o = d * (float)fb;
            m.quadFacing(A0 + o - g.org, A1 + o - g.org, B1 + o - g.org, B0 + o - g.org, vec2(0, 0), vec2(bw * 2, 0), vec2(half * 2, capH), vec2(0, capH), cCap, mConc,
                         T3 * (float)fb);
        }
        // sloped undersides and top
        m.quadFacing(A0 - d - g.org, A0 + d - g.org, B0 + d - g.org, B0 - d - g.org, vec2(0, 0), vec2(thick * 2, 0), vec2(thick * 2, 3), vec2(0, 3), cCap, mConc,
                     -R3 + vec3(0, 0, -0.5f));
        m.quadFacing(A1 - d - g.org, A1 + d - g.org, B1 + d - g.org, B1 - d - g.org, vec2(0, 0), vec2(thick * 2, 0), vec2(thick * 2, 3), vec2(0, 3), cCap, mConc,
                     R3 + vec3(0, 0, -0.5f));
        m.quadFacing(B0 - d - g.org, B1 - d - g.org, B1 + d - g.org, B0 + d - g.org, vec2(0, 0), vec2(half * 2, 0), vec2(half * 2, thick * 2), vec2(0, thick * 2), cCap,
                     mConc, vec3(0, 0, 1));
        if (pr.bent) m.quadFacing(A0 - d - g.org, A1 - d - g.org, A1 + d - g.org, A0 + d - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), cCap, mConc, vec3(0, 0, -1));
        if (g.detail) {
            // elastomeric bearings under the girder webs
            float web = pr.station ? 3.6f : 2.2f;
            for (int sd = -1; sd <= 1; sd += 2)
                for (int fb = -1; fb <= 1; fb += 2)
                    g.m->box(vec3(c, capTop + 0.06f) + R3 * (sd * web) + T3 * (fb * thick * 0.5f) - g.org, T3, R3, vec3(0, 0, 1), vec3(0.3f, 0.35f, 0.06f), rgb(0.15f),
                             M(MAT_RUBBER), false);
        }
        if (g.col) collide(g, vec3(c, (capBot + capTop) * 0.5f), t2, vec3(thick, half, capH * 0.5f));
    };
    if (pr.bent) {
        for (int sd = -1; sd <= 1; sd += 2) column(f.o.xy() + r2 * (sd * pr.lateral), 0.8f, 0.8f);
        capBeam(f.o.xy(), pr.lateral + 1.0f, 0.9f);
    } else {
        column(f.o.xy(), pr.station ? 1.2f : 0.95f, pr.station ? 1.7f : 1.25f);
        capBeam(f.o.xy(), pr.station ? 5.4f : 3.3f, pr.station ? 1.2f : 1.0f);
    }
}

// ------------------------------------------------------------------------------------------------ element generator
void genViaduct(const SiteElem& e, G& g) {
    const TransitNet& N = *gTransit;
    if (!N.ready) return;
    const MetroLine& L = N.metro;
    float s0 = e.p[0], s1 = e.p[1];
    float step = g.detail ? 4.f : 16.f;
    int nSeg = Max(1, (int)ceilf((s1 - s0) / step));
    float h = (s1 - s0) / nSeg;
    TF prev = frameAt(L, s0);
    float wPrev = stationBlend(L, s0);
    for (int k = 0; k < nSeg; k++) {
        float sb = s0 + (k + 1) * h;
        TF cur = frameAt(L, sb);
        float wCur = stationBlend(L, sb);
        vec2 mid = (vec2(prev.o.x, prev.o.y) + vec2(cur.o.x, cur.o.y)) * 0.5f;
        if (g.owns(mid)) {
            deckSegment(g, L, prev, cur, wPrev, wCur, g.detail);
            if (g.detail) deckCollision(g, prev, cur, Max(wPrev, wCur));
        }
        prev = cur;
        wPrev = wCur;
    }
    // piers whose base is in this cell (the chunk owns the piers in [s0, s1))
    for (const MetroPier& pr : L.piers) {
        if (pr.s < s0 || pr.s >= s1) continue;
        vec2 pos, tan;
        L.frame(pr.s, pos, tan);
        if (!g.owns(pos)) continue;
        genPier(g, L, pr);
    }
}


// ------------------------------------------------------------------------------------------------ stations
// Station frame helpers (stations sit on straight, level track)
struct SF {
    const MetroStation* st;
    vec3 d, r;      // along, right (horizontal)
    float zp;       // platform level
    vec3 P(float along, float lateral, float z) const { return st->local(along, lateral, z); }
};

void sbox(G& g, const SF& f, float along, float lateral, float z, vec3 he, u32 col, u32 mat, bool bottom = false) {
    g.m->box(f.P(along, lateral, z) - g.org, f.d, f.r, vec3(0, 0, 1), he, col, mat, bottom);
}
void scollide(G& g, const SF& f, float along, float lateral, float z, vec3 he) {
    if (!g.col) return;
    collide(g, f.P(along, lateral, z), vec2(f.d.x, f.d.y), he);
}
// Quad in the station frame: corners given as (along, lateral, z)
void squad(G& g, MeshData& m, const SF& f, vec3 a, vec3 b, vec3 c, vec3 d, u32 col, u32 mat, vec3 facingLocal) {
    vec3 A = f.P(a.x, a.y, a.z), B = f.P(b.x, b.y, b.z), C = f.P(c.x, c.y, c.z), D = f.P(d.x, d.y, d.z);
    vec3 face = f.d * facingLocal.x + f.r * facingLocal.y + vec3(0, 0, facingLocal.z);
    float lu = length(B - A), lv = length(D - A);
    m.quadFacing(A - g.org, B - g.org, C - g.org, D - g.org, vec2(0, 0), vec2(lu, 0), vec2(lu, lv), vec2(0, lv), col, mat, face);
}
// Text on a vertical plane of the station frame: starts at (along, lateral, z) running along +dirAlong (1 or -1 along
// the track) with the letter faces pointing toward `faceLat` (sign of the lateral normal)
void signText(G& g, const SF& f, const char* txt, float alongCenter, float lateral, float zBase, float height, float runSign, float faceLat, u32 col, u32 mat,
              float depth = 0.f) {
    float w = textAdvance(txt, height);
    vec3 rightW = f.d * runSign;
    vec3 origin = f.P(alongCenter - runSign * w * 0.5f, lateral, zBase);
    // strokeText faces cross(right, up); flip the run direction so the text faces the wanted side
    vec3 n = cross(rightW, vec3(0, 0, 1));
    if (dot(n, f.r * faceLat) < 0.f) {
        rightW = -rightW;
        origin = f.P(alongCenter + runSign * w * 0.5f, lateral, zBase);
    }
    strokeText(g, *g.m, txt, origin, rightW, vec3(0, 0, 1), height, height * 0.13f, col, mat, depth);
}

std::string upper(const std::string& s) {
    std::string o = s;
    for (auto& c : o) c = (char)toupper((unsigned char)c);
    return o;
}

// Station name panel (two faces) hanging at (along, lateral, z), long axis along the track
void namePanel(G& g, const SF& f, const std::string& name, float along, float lateral, float z) {
    std::string nm = upper(name);
    float th = 0.26f;
    float w = Max(2.4f, textAdvance(nm.c_str(), th) + 0.7f);
    sbox(g, f, along, lateral, z, vec3(w * 0.5f, 0.05f, 0.3f), rgbv(kTeal), M(MAT_METAL_PAINTED));
    sbox(g, f, along, lateral, z + 0.31f, vec3(w * 0.5f + 0.02f, 0.06f, 0.015f), rgb(0.85f), M(MAT_METAL_BRUSHED));
    for (int sd = -1; sd <= 1; sd += 2) {
        signText(g, f, nm.c_str(), along + 0.18f, lateral + sd * 0.055f, z - th * 0.5f, th, 1.f, (float)sd, rgbv(kSignText, 0.22f), emMat());
        // SkyLine roundel at the start of the panel
        vec3 c = f.P(along - w * 0.5f + 0.3f, lateral + sd * 0.056f, z);
        vec3 nrm = f.r * (float)sd;
        vec3 ax = f.d, ay(0, 0, 1);
        MeshData& m = *g.m;
        for (int k = 0; k < 12; k++) {
            float a0 = kTwoPi * k / 12, a1 = kTwoPi * (k + 1) / 12;
            vec3 p0 = c + (ax * cosf(a0) + ay * sinf(a0)) * 0.19f, p1 = c + (ax * cosf(a1) + ay * sinf(a1)) * 0.19f;
            vec3 q0 = c + (ax * cosf(a0) + ay * sinf(a0)) * 0.13f, q1 = c + (ax * cosf(a1) + ay * sinf(a1)) * 0.13f;
            m.quadFacing(p0 - g.org, p1 - g.org, q1 - g.org, q0 - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgbv(kSignText, 0.22f), emMat(), nrm);
        }
        m.quadFacing(c + ax * -0.15f + ay * -0.03f - g.org, c + ax * 0.15f + ay * -0.03f - g.org, c + ax * 0.15f + ay * 0.03f - g.org, c + ax * -0.15f + ay * 0.03f - g.org,
                     vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgbv(kSignText, 0.22f), emMat(), nrm);
    }
}

// Route map board: the loop as a rounded rectangle with every station, this one highlighted
void routeMap(G& g, const SF& f, int self, float along, float lateral, float faceLat) {
    const MetroLine& L = gTransit->metro;
    float hw = 1.25f, hh = 0.62f, zc = f.zp + 1.55f;
    MeshData& m = *g.m;
    // posts and frame
    for (int k = -1; k <= 1; k += 2) sbox(g, f, along + k * (hw + 0.05f), lateral, f.zp + 1.1f, vec3(0.05f, 0.05f, 1.1f), rgb(0.25f), M(MAT_METAL_PAINTED));
    sbox(g, f, along, lateral, zc, vec3(hw + 0.04f, 0.04f, hh + 0.04f), rgb(0.2f), M(MAT_METAL_PAINTED));
    if (!g.detail) return;
    vec3 nrm = f.r * faceLat;
    float off = 0.045f;
    // backlit face
    vec3 c = f.P(along, lateral + faceLat * off, zc);
    vec3 ax = f.d * faceLat, ay(0, 0, 1);   // map x axis reads left to right for a viewer facing the board
    m.quadFacing(c - ax * hw - ay * hh - g.org, c + ax * hw - ay * hh - g.org, c + ax * hw + ay * hh - g.org, c - ax * hw + ay * hh - g.org, vec2(0), vec2(1, 0),
                 vec2(1, 1), vec2(0, 1), rgb(0.93f, 0.94f, 0.92f, 0.07f), emMat(), nrm);
    // map transform: loop bounding box into the board (with a title band on top)
    vec2 mn(1e9f), mx(-1e9f);
    for (int i = 0; i < L.count(); i += 8) {
        mn = vmin(mn, L.p[i]);
        mx = vmax(mx, L.p[i]);
    }
    float sx = (hw * 1.5f) / Max(mx.x - mn.x, 1.f), sy = (hh * 1.25f) / Max(mx.y - mn.y, 1.f);
    float sc = Min(sx, sy);
    vec2 mc = (mn + mx) * 0.5f;
    auto mapPt = [&](vec2 w, float lift) { vec2 q = (w - mc) * sc; return c + ax * (q.x - hw * 0.12f) + ay * (q.y - hh * 0.12f) + nrm * lift; };
    u32 lineCol = rgbv(kTeal * 1.3f, 0.35f);
    for (int i = 0; i < L.count(); i += 12) {
        int j = (i + 12) % L.count();
        vec3 a = mapPt(L.p[i], 0.004f), b = mapPt(L.p[j], 0.004f);
        vec3 dd = normalize(b - a), side = cross(nrm, dd) * 0.018f;
        m.quadFacing(a - side - g.org, b - side - g.org, b + side - g.org, a + side - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), lineCol, emMat(), nrm);
    }
    for (int i = 0; i < (int)L.stations.size(); i++) {
        const MetroStation& s2 = L.stations[i];
        vec3 p = mapPt(s2.pos, 0.006f);
        float r = i == self ? 0.045f : 0.028f;
        u32 dc = i == self ? rgb(1.f, 0.15f, 0.1f, 0.5f) : rgb(1.f, 1.f, 1.f, 0.3f);
        m.quadFacing(p - ax * r - ay * r - g.org, p + ax * r - ay * r - g.org, p + ax * r + ay * r - g.org, p - ax * r + ay * r - g.org, vec2(0), vec2(1, 0), vec2(1, 1),
                     vec2(0, 1), dc, emMat(), nrm);
        // label outside the loop
        vec2 outward = normalize(s2.pos - mc);
        std::string nm = upper(s2.name);
        float th = 0.045f;
        float tw = textAdvance(nm.c_str(), th);
        vec3 lp = p + ax * (outward.x > 0.3f ? 0.07f : (outward.x < -0.3f ? -0.07f - tw : -tw * 0.5f)) +
                  ay * (outward.y > 0.3f ? 0.06f : (outward.y < -0.3f ? -0.1f : -th * 0.5f));
        strokeText(g, m, nm.c_str(), lp + nrm * 0.002f, ax, ay, th, th * 0.16f, rgb(0.08f, 0.1f, 0.12f), M(MAT_PAINT_WHITE));
    }
    // title
    strokeText(g, m, "SKYLINE LOOP", c - ax * (hw - 0.1f) + ay * (hh - 0.16f) + nrm * 0.004f, ax, ay, 0.09f, 0.013f, rgbv(kTeal), M(MAT_PAINT_WHITE));
    strokeText(g, m, "YOU ARE HERE", c + ax * (hw - 0.75f) - ay * (hh - 0.06f) + nrm * 0.004f, ax, ay, 0.05f, 0.008f, rgb(0.8f, 0.1f, 0.08f), M(MAT_PAINT_WHITE));
}

// Stair + escalator from the platform landing down to the street (one per platform side)
void stairFlight(G& g, const SF& f, const MetroStation& st, int side, bool farLod) {
    using namespace transit_dims;
    float S = side == 0 ? 1.f : -1.f;
    int E = st.exitEnd[side];
    float along0 = E * (kPlatformHalfLen - 3.4f);       // top of the stair (landing edge)
    vec2 footGuess = st.local(E * 6.f, S * 9.4f, 0.f).xy();
    float gz = gMap->heightAt(footGuess.x, footGuess.y);
    float rise = f.zp - gz;
    const float riserH = 0.17f, tread = 0.29f, landing = 1.6f;
    int risers = Max(4, (int)ceilf(rise / riserH));
    float rh = rise / risers;
    int perFlight = 17;
    u32 cSteel = rgb(0.32f, 0.34f, 0.36f), cTread = rgb(0.6f, 0.6f, 0.58f), cRail = rgb(0.82f, 0.84f, 0.86f);
    u32 mSteel = M(MAT_METAL_PAINTED), mConc = M(MAT_CONCRETE);
    float latC = S * 9.4f, halfW = 1.1f;
    // walk down from the top: along decreases toward the station center (dirSign = -E)
    float dirSign = -(float)E;
    float a = along0, z = f.zp;
    int k = 0;
    std::vector<vec3> railPts;   // (along, z) along the stair for stringers / handrails
    railPts.push_back(vec3(a, z, 0));
    while (k < risers) {
        int n = Min(perFlight, risers - k);
        for (int i = 0; i < n; i++, k++) {
            float zTop = z - rh;   // tread surface of this step
            float aMid = a + dirSign * tread * 0.5f;
            if (!farLod) {
                sbox(g, f, aMid, latC, zTop - 0.04f, vec3(tread * 0.5f + 0.01f, halfW, 0.045f), cTread, mConc);
                // riser plate
                sbox(g, f, a + dirSign * 0.01f, latC, zTop + rh * 0.5f, vec3(0.012f, halfW, rh * 0.5f), rgb(0.5f), mSteel);
            }
            scollide(g, f, aMid, latC, zTop - 0.2f, vec3(tread * 0.5f + 0.02f, halfW, 0.2f));
            a += dirSign * tread;
            z = zTop;
        }
        railPts.push_back(vec3(a, z, 0));
        if (k < risers) {
            // landing
            float aMid = a + dirSign * landing * 0.5f;
            if (!farLod) sbox(g, f, aMid, latC, z - 0.08f, vec3(landing * 0.5f, halfW, 0.08f), cTread, mConc);
            scollide(g, f, aMid, latC, z - 0.2f, vec3(landing * 0.5f, halfW, 0.2f));
            a += dirSign * landing;
            railPts.push_back(vec3(a, z, 0));
        }
    }
    float aFoot = a;
    // stringers (both sides) and handrails following the flight profile
    for (int sd = -1; sd <= 1; sd += 2) {
        float lat = latC + sd * (halfW + 0.06f);
        for (size_t i = 0; i + 1 < railPts.size(); i++) {
            vec3 p0 = railPts[i], p1 = railPts[i + 1];
            vec3 A = f.P(p0.x, lat, p0.y), B = f.P(p1.x, lat, p1.y);
            beam(g, A + vec3(0, 0, -0.25f), B + vec3(0, 0, -0.25f), 0.12f, 0.42f, cSteel, mSteel, vec3(0, 0, 1));
            if (farLod) continue;
            // handrail + posts
            beam(g, A + vec3(0, 0, 1.0f), B + vec3(0, 0, 1.0f), 0.05f, 0.05f, cRail, M(MAT_METAL_BRUSHED));
            beam(g, A + vec3(0, 0, 0.5f), B + vec3(0, 0, 0.5f), 0.03f, 0.03f, cRail, M(MAT_METAL_BRUSHED));
            float len = length(vec2(p1.x - p0.x, p1.y - p0.y));
            int posts = Max(1, (int)(len / 1.6f));
            for (int q = 0; q <= posts; q++) {
                float t = (float)q / posts;
                vec3 P = lerp(A, B, t);
                g.m->box(P + vec3(0, 0, 0.5f) - g.org, f.d, f.r, vec3(0, 0, 1), vec3(0.025f, 0.025f, 0.5f), cRail, M(MAT_METAL_BRUSHED), false);
            }
            // side guard collision following the slope (4 boxes per flight)
            for (int q = 0; q < 4; q++) {
                float t0 = q / 4.f, t1 = (q + 1) / 4.f;
                float am = Lerp(p0.x, p1.x, (t0 + t1) * 0.5f), zm = Lerp(p0.y, p1.y, (t0 + t1) * 0.5f);
                float hl = fabsf(p1.x - p0.x) / 8.f + 0.05f;
                scollide(g, f, am, lat, zm + 0.45f, vec3(hl, 0.05f, 0.6f));
            }
        }
    }
    // escalator beside the stair (outboard): truss, glass balustrades, step band; walkable like a stair
    {
        float latE = S * 11.3f, hwE = 0.62f;
        float runE = rise / tanf(30.f * kDegToRad);
        float aTop = along0, aBot = along0 + dirSign * runE;
        vec3 top = f.P(aTop, latE, f.zp), bot = f.P(aBot, latE, gz);
        // truss box under the steps
        vec3 dn(0, 0, -1.1f);
        beam(g, top + dn * 0.5f, bot + dn * 0.5f, hwE * 2.f + 0.3f, 1.1f, rgb(0.5f, 0.52f, 0.55f), M(MAT_METAL_BRUSHED));
        if (!farLod) {
            // step band (dark ribbed) and balustrades
            beam(g, top + vec3(0, 0, 0.03f), bot + vec3(0, 0, 0.03f), hwE * 2.f, 0.06f, rgb(0.18f), M(MAT_METAL_BRUSHED));
            for (int sd = -1; sd <= 1; sd += 2) {
                vec3 o = f.r * (sd * (hwE + 0.08f));
                beam(g, top + o + vec3(0, 0, 0.5f), bot + o + vec3(0, 0, 0.5f), 0.06f, 1.0f, rgb(0.45f, 0.55f, 0.6f), M(MAT_GLASS));
                beam(g, top + o + vec3(0, 0, 1.03f), bot + o + vec3(0, 0, 1.03f), 0.12f, 0.08f, rgb(0.08f), M(MAT_RUBBER));
            }
            // comb plates / newel ends
            sbox(g, f, aTop - dirSign * 0.6f, latE, f.zp + 0.02f, vec3(0.6f, hwE + 0.1f, 0.02f), rgb(0.7f), M(MAT_METAL_BRUSHED));
            sbox(g, f, aBot + dirSign * 0.6f, latE, gz + 0.02f, vec3(0.6f, hwE + 0.1f, 0.02f), rgb(0.7f), M(MAT_METAL_BRUSHED));
        }
        // walkable steps (0.2 m rise)
        int n = Max(2, (int)ceilf(rise / 0.2f));
        for (int i = 0; i < n; i++) {
            float t = (i + 0.5f) / n;
            float am = Lerp(aTop, aBot, t);
            float zt = Lerp(f.zp, gz, (i + 1.f) / n);
            scollide(g, f, am, latE, zt - 0.2f, vec3(fabsf(aBot - aTop) / n * 0.5f + 0.02f, hwE, 0.2f));
        }
        for (int sd = -1; sd <= 1; sd += 2)
            for (int q = 0; q < 6; q++) {
                float t = (q + 0.5f) / 6.f;
                scollide(g, f, Lerp(aTop, aBot, t), latE + sd * (hwE + 0.08f), Lerp(f.zp, gz, t) + 0.5f, vec3(fabsf(aBot - aTop) / 12.f + 0.05f, 0.05f, 0.7f));
            }
    }
    // canopy over the stair and escalator: stepped roof panels per flight on slim posts
    if (!farLod) {
        for (size_t i = 0; i + 1 < railPts.size(); i += 1) {
            vec3 p0 = railPts[i], p1 = railPts[i + 1];
            float zr0 = p0.y + 3.0f, zr1 = p1.y + 3.0f;
            vec3 A0 = f.P(p0.x, S * 7.9f, zr0), A1 = f.P(p1.x, S * 7.9f, zr1), B0 = f.P(p0.x, S * 12.3f, zr0 + 0.25f), B1 = f.P(p1.x, S * 12.3f, zr1 + 0.25f);
            g.m->quadFacing(A0 - g.org, A1 - g.org, B1 - g.org, B0 - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgb(0.92f, 0.93f, 0.94f), M(MAT_ROOF_METAL),
                            vec3(0, 0, 1));
            g.m->quadFacing(A0 - g.org, B0 - g.org, B1 - g.org, A1 - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgb(0.75f, 0.77f, 0.8f), M(MAT_METAL_PAINTED),
                            vec3(0, 0, -1));
            // edge fascia in brand teal
            beam(g, B0, B1, 0.08f, 0.3f, rgbv(kTeal), M(MAT_METAL_PAINTED));
            if (i % 2 == 0) {
                vec3 base = f.P(p1.x, S * 12.35f, 0.f);
                float zb = i + 2 == railPts.size() ? gz : p1.y - 0.3f;
                g.m->box(vec3(base.x, base.y, (zb + zr1 + 0.25f) * 0.5f) - g.org, f.d, f.r, vec3(0, 0, 1), vec3(0.06f, 0.06f, (zr1 + 0.25f - zb) * 0.5f), rgb(0.3f),
                         mSteel, false);
            }
            // stair light under each roof panel
            vec3 lp = (A0 + A1 + B0 + B1) * 0.25f - vec3(0, 0, 0.15f);
            lamp(g, lp, 0.18f, vec3(1.f, 0.95f, 0.85f), 0.6f, EA_NIGHT);
            light(g, lp - vec3(0, 0, 0.2f), vec3(1.f, 0.93f, 0.82f) * 1400.f, 10.f, 1);
        }
    }
    // street landing: totem pylon with the SkyLine roundel, two ticket machines under a small canopy
    if (!farLod) {
        float aT = aFoot + dirSign * 2.2f;
        vec3 tb = f.P(aT, S * 11.9f, 0.f);
        float tz = gMap->heightAt(tb.x, tb.y);
        // totem: a slim panel (faces +-along) on a plinth with the roundel, the brand and the station name
        g.m->box(vec3(tb.x, tb.y, tz + 0.15f) - g.org, f.d, f.r, vec3(0, 0, 1), vec3(0.22f, 0.8f, 0.15f), rgb(0.3f), M(MAT_CONCRETE), false);
        g.m->box(vec3(tb.x, tb.y, tz + 2.0f) - g.org, f.d, f.r, vec3(0, 0, 1), vec3(0.12f, 0.72f, 1.7f), rgbv(kTeal), M(MAT_METAL_PAINTED), false);
        collide(g, vec3(tb.x, tb.y, tz + 1.85f), vec2(f.d.x, f.d.y), vec3(0.22f, 0.8f, 1.85f));
        std::string nm = upper(st.name);
        float nh = Min(0.2f, 0.2f * 1.2f / Max(textAdvance(nm.c_str(), 0.2f), 0.1f));
        for (int sd = -1; sd <= 1; sd += 2) {
            vec3 c = vec3(tb.x, tb.y, tz + 3.15f) + f.d * (sd * 0.125f);
            vec3 nrm = f.d * (float)sd, ax = f.r, ay(0, 0, 1);
            for (int q = 0; q < 16; q++) {
                float a0 = kTwoPi * q / 16, a1 = kTwoPi * (q + 1) / 16;
                vec3 p0 = c + (ax * cosf(a0) + ay * sinf(a0)) * 0.3f, p1 = c + (ax * cosf(a1) + ay * sinf(a1)) * 0.3f;
                vec3 q0 = c + (ax * cosf(a0) + ay * sinf(a0)) * 0.21f, q1 = c + (ax * cosf(a1) + ay * sinf(a1)) * 0.21f;
                g.m->quadFacing(p0 - g.org, p1 - g.org, q1 - g.org, q0 - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgbv(kSignText, 0.3f), emMat(), nrm);
            }
            g.m->quadFacing(c - ax * 0.26f - ay * 0.045f - g.org, c + ax * 0.26f - ay * 0.045f - g.org, c + ax * 0.26f + ay * 0.045f - g.org, c - ax * 0.26f + ay * 0.045f - g.org,
                            vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgbv(kSignText, 0.3f), emMat(), nrm);
            vec3 rd = ax * (-(float)sd);   // reading direction whose front faces this side of the panel
            vec3 face = vec3(tb.x, tb.y, 0.f) + f.d * (sd * 0.125f);
            strokeText(g, *g.m, "SKYLINE", vec3(face.x, face.y, tz + 2.5f) - rd * (textAdvance("SKYLINE", 0.17f) * 0.5f), rd, ay, 0.17f, 0.024f, rgbv(kSignText, 0.25f),
                       emMat());
            strokeText(g, *g.m, nm.c_str(), vec3(face.x, face.y, tz + 2.1f) - rd * (textAdvance(nm.c_str(), nh) * 0.5f), rd, ay, nh, nh * 0.14f, rgbv(kSignText, 0.25f),
                       emMat());
        }
        light(g, vec3(tb.x, tb.y, tz + 3.9f), vec3(0.3f, 0.9f, 1.f) * 300.f, 6.f, 2);
        // ticket machines facing the sidewalk side
        for (int q = 0; q < 2; q++) {
            vec3 mb = f.P(aFoot + dirSign * (3.3f + q * 1.1f), S * 12.0f, 0.f);
            float mz = gMap->heightAt(mb.x, mb.y);
            g.m->box(vec3(mb.x, mb.y, mz + 0.85f) - g.org, f.d, f.r, vec3(0, 0, 1), vec3(0.45f, 0.3f, 0.85f), rgb(0.2f, 0.22f, 0.25f), M(MAT_METAL_PAINTED), false);
            vec3 sc = vec3(mb.x, mb.y, mz + 1.3f) + f.r * (S * 0.305f);
            vec3 nrm = f.r * S;
            g.m->quadFacing(sc - f.d * 0.28f - vec3(0, 0, 0.2f) - g.org, sc + f.d * 0.28f - vec3(0, 0, 0.2f) - g.org, sc + f.d * 0.28f + vec3(0, 0, 0.2f) - g.org,
                            sc - f.d * 0.28f + vec3(0, 0, 0.2f) - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgb(0.35f, 0.75f, 0.9f, 0.3f), emMat(), nrm);
            g.m->box(vec3(mb.x, mb.y, mz + 1.78f) - g.org, f.d, f.r, vec3(0, 0, 1), vec3(0.47f, 0.32f, 0.08f), rgbv(kTeal), M(MAT_METAL_PAINTED), false);
            collide(g, vec3(mb.x, mb.y, mz + 0.85f), vec2(f.d.x, f.d.y), vec3(0.47f, 0.32f, 0.85f));
        }
    }
}

void genStation(const SiteElem& e, G& g) {
    const TransitNet& N = *gTransit;
    if (!N.ready || e.variant >= N.metro.stations.size()) return;
    const MetroStation& st = N.metro.stations[e.variant];
    SF f;
    f.st = &st;
    f.d = vec3(st.dir, 0);
    f.r = vec3(st.right(), 0);
    f.zp = st.platformZ();
    const float H = kPlatformHalfLen;
    const float deckTop = st.railZ - kDeckTopBelowRail;
    bool own = g.owns(st.pos);    // the station center's cell owns the station-wide parts
    MeshData& m = *g.m;
    u32 mTile = M(MAT_TILE), mConc = M(MAT_CONCRETE), mPaint = M(MAT_METAL_PAINTED);
    u32 cFloor = rgb(0.74f, 0.74f, 0.72f), cWhite = rgb(0.93f, 0.94f, 0.95f);
    // ---- platforms: split into 4 m pieces owned by the cell containing each piece
    for (int side = 0; side < 2; side++) {
        float S = side == 0 ? 1.f : -1.f;
        int E = st.exitEnd[side];
        for (float a0 = -H; a0 < H - 0.01f; a0 += 4.f) {
            float a1 = Min(H, a0 + 4.f);
            vec2 mid = st.local((a0 + a1) * 0.5f, S * 5.4f, 0.f).xy();
            if (!g.owns(mid)) continue;
            float i0 = kPlatformEdge, i1 = kPlatformOuter;
            // floor, edge face and coping
            squad(g, m, f, vec3(a0, S * i0, f.zp), vec3(a1, S * i0, f.zp), vec3(a1, S * i1, f.zp), vec3(a0, S * i1, f.zp), cFloor, mTile, vec3(0, 0, 1));
            squad(g, m, f, vec3(a0, S * i0, deckTop), vec3(a1, S * i0, deckTop), vec3(a1, S * i0, f.zp), vec3(a0, S * i0, f.zp), rgb(0.55f), mConc, vec3(0, -S, 0));
            paintRect(g, f.P((a0 + a1) * 0.5f, S * (i0 + 0.3f), 0.f).xy(), st.dir, (a1 - a0) * 0.5f, 0.28f, f.zp + 0.004f, rgb(0.95f, 0.78f, 0.1f), M(MAT_PAINT_YELLOW));
            paintRect(g, f.P((a0 + a1) * 0.5f, S * (i0 + 0.06f), 0.f).xy(), st.dir, (a1 - a0) * 0.5f, 0.05f, f.zp + 0.004f, kWhiteC, M(MAT_PAINT_WHITE));
            scollide(g, f, (a0 + a1) * 0.5f, S * (i0 + i1) * 0.5f, (deckTop + f.zp) * 0.5f, vec3((a1 - a0) * 0.5f + 0.02f, (i1 - i0) * 0.5f, (f.zp - deckTop) * 0.5f));
            // back wall: concrete upstand + railing, open where the stair landing joins the platform
            bool opening = E * (a0 + a1) * 0.5f > H - 3.6f;
            if (!opening) {
                float w0 = kPlatformOuter, w1 = kPlatformOuter + 0.18f;
                sbox(g, f, (a0 + a1) * 0.5f, S * (w0 + w1) * 0.5f, f.zp + 0.35f, vec3((a1 - a0) * 0.5f, 0.09f, 0.35f), cConcrete(1.02f), M(MAT_CONCRETE_PANEL));
                if (g.detail) {
                    sbox(g, f, (a0 + a1) * 0.5f, S * (w0 + w1) * 0.5f, f.zp + 1.12f, vec3((a1 - a0) * 0.5f, 0.04f, 0.03f), rgb(0.85f), M(MAT_METAL_BRUSHED));
                    for (int b = 0; b < 2; b++) sbox(g, f, (a0 + a1) * 0.5f, S * (w0 + w1) * 0.5f, f.zp + 0.82f + b * 0.15f, vec3((a1 - a0) * 0.5f, 0.015f, 0.012f), rgb(0.8f),
                                                     M(MAT_METAL_BRUSHED));
                    sbox(g, f, a0 + 0.05f, S * (w0 + w1) * 0.5f, f.zp + 0.9f, vec3(0.03f, 0.03f, 0.22f), rgb(0.8f), M(MAT_METAL_BRUSHED));
                } else {
                    sbox(g, f, (a0 + a1) * 0.5f, S * (w0 + w1) * 0.5f, f.zp + 0.95f, vec3((a1 - a0) * 0.5f, 0.02f, 0.18f), rgb(0.6f), M(MAT_METAL_BRUSHED));
                }
                scollide(g, f, (a0 + a1) * 0.5f, S * (w0 + w1) * 0.5f, f.zp + 0.6f, vec3((a1 - a0) * 0.5f + 0.02f, 0.12f, 0.6f));
            }
            // platform end walls
            if (a0 <= -H + 0.01f || a1 >= H - 0.01f) {
                float ae = a0 <= -H + 0.01f ? -H : H;
                sbox(g, f, ae, S * (i0 + i1) * 0.5f, f.zp + 0.55f, vec3(0.06f, (i1 - i0) * 0.5f, 0.55f), rgb(0.8f), M(MAT_METAL_BRUSHED));
                scollide(g, f, ae, S * (i0 + i1) * 0.5f, f.zp + 0.6f, vec3(0.08f, (i1 - i0) * 0.5f, 0.6f));
                if (g.detail) {
                    // "no entry" plate at the platform end
                    sbox(g, f, ae - (ae > 0 ? 0.07f : -0.07f), S * (i0 + 0.6f), f.zp + 0.8f, vec3(0.01f, 0.25f, 0.18f), rgb(0.85f, 0.1f, 0.08f), mPaint);
                }
            }
        }
    }
    // ---- wing roof over both platforms and tracks (station-wide, owned by the center cell)
    const float roofHalf = 8.4f, roofL = H + 2.0f;
    auto roofZ = [&](float lat) { float x = lat / roofHalf; return f.zp + 3.95f + 1.55f * (1.f - x * x); };
    const int prof = g.detail ? 14 : 6;
    if (own) {
        for (int i = 0; i < prof; i++) {
            float l0 = -roofHalf + 2.f * roofHalf * i / prof, l1 = -roofHalf + 2.f * roofHalf * (i + 1) / prof;
            float z0 = roofZ(l0), z1 = roofZ(l1);
            vec3 up = normalize(vec3(0, -(z1 - z0), l1 - l0));   // local (along, lateral, z) normal of the profile edge
            bool sky = fabsf(l0 + l1) * 0.5f < 1.4f;
            u32 topCol = sky ? rgb(0.55f, 0.66f, 0.72f) : rgb(0.93f, 0.94f, 0.95f);
            squad(g, m, f, vec3(-roofL, l0, z0), vec3(roofL, l0, z0), vec3(roofL, l1, z1), vec3(-roofL, l1, z1), topCol, sky ? M(MAT_GLASS) : M(MAT_ROOF_METAL),
                  vec3(0, up.y, up.z));
            squad(g, m, f, vec3(-roofL, l0, z0 - 0.22f), vec3(roofL, l0, z0 - 0.22f), vec3(roofL, l1, z1 - 0.22f), vec3(-roofL, l1, z1 - 0.22f),
                  rgb(0.78f, 0.8f, 0.82f), M(MAT_METAL_PAINTED), vec3(0, -up.y, -up.z));
            // gable ends
            for (int en = -1; en <= 1; en += 2)
                squad(g, m, f, vec3(en * roofL, l0, z0 - 0.22f), vec3(en * roofL, l1, z1 - 0.22f), vec3(en * roofL, l1, z1), vec3(en * roofL, l0, z0), rgbv(kTeal),
                      mPaint, vec3((float)en, 0, 0));
        }
        for (int sd = -1; sd <= 1; sd += 2) {
            float l = sd * roofHalf, z = roofZ(l);
            squad(g, m, f, vec3(-roofL, l, z - 0.22f), vec3(roofL, l, z - 0.22f), vec3(roofL, l, z + 0.02f), vec3(-roofL, l, z + 0.02f), rgbv(kTeal), mPaint,
                  vec3(0, (float)sd, 0));
            // roof edge LED line (night)
            squad(g, m, f, vec3(-roofL, l * 1.003f, z - 0.2f), vec3(roofL, l * 1.003f, z - 0.2f), vec3(roofL, l * 1.003f, z - 0.14f), vec3(-roofL, l * 1.003f, z - 0.14f),
                  rgbv(kTealGlow, 0.5f), emMat(EA_NIGHT), vec3(0, (float)sd, 0));
        }
        // portal frames: columns on the platforms' back edges + curved rafters
        for (float a = -H + 1.f; a <= H - 0.9f; a += (2.f * H - 2.f) / 8.f) {
            for (int sd = -1; sd <= 1; sd += 2) {
                float lat = sd * 6.95f;
                float zt = roofZ(lat) - 0.22f;
                sbox(g, f, a, lat, (f.zp + zt) * 0.5f, vec3(0.13f, 0.13f, (zt - f.zp) * 0.5f), cWhite, mPaint);
                scollide(g, f, a, lat, (f.zp + zt) * 0.5f, vec3(0.15f, 0.15f, (zt - f.zp) * 0.5f));
                if (g.detail) sbox(g, f, a, lat, f.zp + 0.05f, vec3(0.2f, 0.2f, 0.05f), rgb(0.5f), mPaint);
            }
            if (g.detail)
                for (int i = 0; i < prof; i++) {
                    float l0 = -roofHalf + 2.f * roofHalf * i / prof, l1 = -roofHalf + 2.f * roofHalf * (i + 1) / prof;
                    beam(g, f.P(a, l0, roofZ(l0) - 0.34f), f.P(a, l1, roofZ(l1) - 0.34f), 0.16f, 0.24f, cWhite, mPaint, vec3(0, 0, 1));
                }
        }
        // lighting under the roof: lamp strips + lights along both platforms
        for (int sd = -1; sd <= 1; sd += 2) {
            float lat = sd * 5.3f;
            float zl = roofZ(lat) - 0.3f;
            for (float a = -H + 4.5f; a <= H - 4.4f; a += 8.4f) {
                sbox(g, f, a, lat, zl, vec3(1.2f, 0.09f, 0.04f), rgb(1.f, 0.97f, 0.9f, 0.7f), emMat(EA_NIGHT));
                light(g, f.P(a, lat, zl - 0.35f), vec3(0.95f, 0.97f, 1.f) * 2600.f, 13.f, 1);
            }
        }
        // hanging name panels, next-train displays and speakers
        for (int side = 0; side < 2; side++) {
            float S = side == 0 ? 1.f : -1.f;
            for (int k = -1; k <= 1; k += 2) {
                float a = k * 17.f;
                float zPanel = roofZ(S * 4.9f) - 1.25f;
                for (int h2 = -1; h2 <= 1; h2 += 2) sbox(g, f, a + h2 * 1.1f, S * 4.9f, (zPanel + roofZ(S * 4.9f)) * 0.5f, vec3(0.01f, 0.01f, (roofZ(S * 4.9f) - zPanel) * 0.5f),
                                                          rgb(0.5f), M(MAT_METAL_BRUSHED));
                namePanel(g, f, st.name, a, S * 4.9f, zPanel);
                if (g.detail) {
                    // next-train display (amber dot matrix)
                    float zd = zPanel - 0.05f;
                    sbox(g, f, a + k * 8.f, S * 5.6f, zd, vec3(0.75f, 0.07f, 0.2f), rgb(0.08f), mPaint);
                    for (int fs = -1; fs <= 1; fs += 2) {
                        const char* txt = fs > 0 ? "NEXT TRAIN 2 MIN" : "SKYLINE LOOP";
                        signText(g, f, txt, a + k * 8.f, S * 5.6f + fs * 0.075f, zd - 0.06f, 0.11f, 1.f, (float)fs, rgb(1.f, 0.55f, 0.1f, 0.35f), emMat());
                    }
                    sbox(g, f, a - k * 6.f, S * 6.2f, roofZ(S * 6.2f) - 0.45f, vec3(0.12f, 0.12f, 0.1f), rgb(0.85f), mPaint);
                }
            }
        }
    }
    // ---- back-wall lettering, route maps, benches, bins (per platform piece ownership)
    for (int side = 0; side < 2; side++) {
        float S = side == 0 ? 1.f : -1.f;
        int E = st.exitEnd[side];
        std::string nm = upper(st.name);
        for (int k = -1; k <= 1; k++) {
            float a = k * 22.f;
            vec2 at = st.local(a, S * kPlatformOuter, 0.f).xy();
            if (!g.owns(at)) continue;
            if (E * a > H - 8.f) continue;
            // teal band with the station name facing the track (read from the trains)
            float bandW = textAdvance(nm.c_str(), 0.34f) + 1.2f;
            sbox(g, f, a, S * (kPlatformOuter + 0.2f), f.zp + 1.68f, vec3(bandW * 0.5f, 0.03f, 0.3f), rgbv(kTeal), mPaint);
            sbox(g, f, a, S * (kPlatformOuter + 0.2f), f.zp + 1.05f, vec3(0.04f, 0.04f, 0.35f), rgb(0.4f), mPaint);
            signText(g, f, nm.c_str(), a, S * (kPlatformOuter + 0.2f) - S * 0.035f, f.zp + 1.51f, 0.34f, 1.f, -S, rgbv(kSignText, 0.25f), emMat());
        }
        // route map boards and benches
        float aMap = -E * 8.f;
        vec2 atMap = st.local(aMap, S * 6.9f, 0.f).xy();
        if (g.owns(atMap)) routeMap(g, f, e.variant, aMap, S * 6.9f, -S);
        if (g.props) {
            for (int k = 0; k < 4; k++) {
                float a = -E * (k * 9.f - 20.f);
                if (fabsf(a - aMap) < 2.5f) a += 3.f;
                vec3 bp = f.P(a, S * 6.35f, f.zp);
                if (!g.owns(bp.xy())) continue;
                vec2 back = st.right() * S;   // benches face the track: their back toward the back wall
                prop(g, bp, atan2f(-back.x, back.y), 1.f, k == 3 ? PROP_BIN : PROP_BENCH);
            }
        }
    }
    // ---- stair landings with fare gates, then the stairs and escalators
    for (int side = 0; side < 2; side++) {
        float S = side == 0 ? 1.f : -1.f;
        int E = st.exitEnd[side];
        vec2 at = st.local(E * (H - 1.5f), S * 9.8f, 0.f).xy();
        if (!g.owns(at)) continue;
        float a0 = E * (H - 3.4f), a1 = E * (H + 0.6f);
        float am = (a0 + a1) * 0.5f, ah = fabsf(a1 - a0) * 0.5f;
        // landing slab from the platform back wall out to the escalator, on its own cantilever
        sbox(g, f, am, S * 9.9f, f.zp - 0.25f, vec3(ah, 2.55f, 0.25f), cFloor, mTile, true);
        scollide(g, f, am, S * 9.9f, f.zp - 0.25f, vec3(ah, 2.55f, 0.25f));
        sbox(g, f, am, S * 9.9f, f.zp - 1.1f, vec3(ah * 0.8f, 1.2f, 0.6f), cConcrete(0.95f), mConc);
        // outer railing of the landing
        sbox(g, f, a1, S * 9.9f, f.zp + 0.55f, vec3(0.04f, 2.55f, 0.55f), rgb(0.8f), M(MAT_METAL_BRUSHED));
        scollide(g, f, a1, S * 9.9f, f.zp + 0.6f, vec3(0.06f, 2.55f, 0.6f));
        // fare gates across the opening: cabinets with flap panels and a teal light on top
        for (int q = 0; q < 4; q++) {
            float a = E * (H - 3.3f + q * 1.3f);
            sbox(g, f, a, S * (kPlatformOuter + 0.55f), f.zp + 0.5f, vec3(0.14f, 0.55f, 0.5f), rgb(0.72f, 0.74f, 0.77f), M(MAT_METAL_BRUSHED));
            sbox(g, f, a, S * (kPlatformOuter + 0.55f), f.zp + 1.01f, vec3(0.15f, 0.56f, 0.02f), rgbv(kTealGlow, 0.4f), emMat());
            scollide(g, f, a, S * (kPlatformOuter + 0.55f), f.zp + 0.5f, vec3(0.16f, 0.55f, 0.5f));
            if (g.detail && q < 3) {
                for (int fl = -1; fl <= 1; fl += 2)
                    sbox(g, f, a + E * (0.14f + 0.2f) + fl * 0.0f, S * (kPlatformOuter + 0.55f + fl * 0.12f), f.zp + 0.75f, vec3(0.2f, 0.01f, 0.2f), rgb(0.5f, 0.7f, 0.75f),
                         M(MAT_GLASS));
            }
        }
        // direction sign over the landing: next stops in this platform's direction
        {
            const MetroLine& L = N.metro;
            int nS = (int)L.stations.size();
            int self = e.variant;
            // outer platform (side 0) serves the counter-clockwise service (increasing s)
            int n1 = side == 0 ? (self + 1) % nS : (self - 1 + nS) % nS;
            int n2 = side == 0 ? (self + 2) % nS : (self - 2 + nS) % nS;
            std::string txt = "TO " + upper(L.stations[n1].name) + " - " + upper(L.stations[n2].name);
            float w = textAdvance(txt.c_str(), 0.17f) + 0.5f;
            float zs = f.zp + 2.75f;
            sbox(g, f, E * (H - 1.4f), S * (kPlatformOuter - 0.4f), zs, vec3(0.04f, w * 0.5f, 0.2f), rgbv(kTeal), mPaint);
            for (int fs = -1; fs <= 1; fs += 2) {
                vec3 c = f.P(E * (H - 1.4f) + fs * 0.045f, S * (kPlatformOuter - 0.4f), zs - 0.085f);
                vec3 rt = f.r * (-(float)fs);   // reading direction whose front faces +-along
                strokeText(g, *g.m, txt.c_str(), c - rt * (w * 0.5f - 0.25f), rt, vec3(0, 0, 1), 0.17f, 0.024f, rgbv(kSignText, 0.22f), emMat());
            }
        }
    }
    for (int side = 0; side < 2; side++) {
        float S = side == 0 ? 1.f : -1.f;
        int E = st.exitEnd[side];
        vec2 at = st.local(E * (H - 12.f), S * 10.f, 0.f).xy();
        if (!g.owns(at)) continue;
        stairFlight(g, f, st, side, !g.detail);
    }
}

// ------------------------------------------------------------------------------------------------ bus stops
// Porto Sol Transit stop: the city's standard shelter (for new stops; existing shelters are reused), a flag pole at the
// curb with a two-faced blade sign (PST band, route badges in the route colours, stop name), a lit timetable case facing
// the sidewalk and a downlight under the shelter roof.
const vec3 kPstBlue(0.04f, 0.18f, 0.36f);

void routeBadge(G& g, vec3 c, vec3 right, vec3 up, vec3 nrm, float size, const BusRoute& R) {
    MeshData& m = *g.m;
    vec3 a = c - right * size * 0.5f - up * size * 0.5f;
    m.quadFacing(a - g.org, a + right * size - g.org, a + right * size + up * size - g.org, a + up * size - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1),
                 rgbv(R.color * 1.6f, 0.2f), emMat(EA_NIGHT), nrm);
    float th = size * 0.52f;
    float w = textAdvance(R.number.c_str(), th);
    strokeText(g, m, R.number.c_str(), c - right * (w * 0.5f) - up * (th * 0.5f) + nrm * 0.003f, right, up, th, th * 0.17f, rgb(1.f, 1.f, 1.f, 0.25f), emMat());
}

void genBusStop(const SiteElem& e, G& g) {
    const TransitNet& N = *gTransit;
    if (!N.ready || e.variant >= N.busStops.size()) return;
    const BusStop& b = N.busStops[e.variant];
    MeshData& m = *g.m;
    vec3 al(b.along, 0.f), fc(b.face, 0.f);   // travel direction, toward the street
    // ---- shelter (new stops) and its downlight
    if (b.ownShelter && g.owns(b.pos)) {
        float yaw = atan2f(b.face.x, -b.face.y);
        prop(g, vec3(b.pos, b.z), yaw, 1.f, PROP_BUS_STOP, (u8)(b.seed & 3u));
        light(g, vec3(b.pos, b.z) + vec3(0, 0, 2.3f), vec3(0.95f, 0.97f, 1.f) * 90.f, 7.f, 0);
    }
    if (!g.owns(b.flag)) return;
    vec3 base(b.flag, b.z);
    u32 galv = rgb(0.62f, 0.64f, 0.66f), mBr = M(MAT_METAL_BRUSHED), mPaint = M(MAT_METAL_PAINTED);
    // pole
    cyl(g, base, 0.045f, 0.04f, 3.15f, g.detail ? 8 : 5, galv, mBr);
    collide(g, base + vec3(0, 0, 1.5f), b.along, vec3(0.06f, 0.06f, 1.5f));
    // blade sign: plane spanned by the pole and the sidewalk direction, faces point along the street
    vec3 inward = -fc;                       // from the curb toward the buildings
    const float bw = 0.62f, z0 = 2.2f, z1 = 3.08f;
    vec3 bc = base + inward * (bw * 0.5f + 0.05f) + vec3(0, 0, (z0 + z1) * 0.5f);
    box(g, bc, inward, vec3(0, 0, 1), vec3(bw * 0.5f, (z1 - z0) * 0.5f, 0.016f), rgb(0.93f, 0.94f, 0.95f), mPaint);
    // mounting brackets
    for (int k = 0; k < 2; k++) box(g, base + inward * 0.04f + vec3(0, 0, z0 + 0.12f + k * 0.62f), inward, vec3(0, 0, 1), vec3(0.06f, 0.03f, 0.03f), galv, mBr);
    if (!g.detail) return;
    // route list (routes serving this stop)
    std::vector<int> routes;
    for (int r = 0; r < (int)N.busRoutes.size(); r++)
        if (b.routeMask & (1u << r)) routes.push_back(r);
    for (int fs = -1; fs <= 1; fs += 2) {
        vec3 nrm = al * (float)fs;
        vec3 right = normalize(cross(vec3(0, 0, 1), nrm));   // reading direction for a viewer facing -nrm
        vec3 face = bc + nrm * 0.018f;
        // top band: PST
        vec3 tb = face + vec3(0, 0, (z1 - z0) * 0.5f - 0.1f);
        m.quadFacing(tb - right * (bw * 0.5f) - vec3(0, 0, 0.1f) - g.org, tb + right * (bw * 0.5f) - vec3(0, 0, 0.1f) - g.org,
                     tb + right * (bw * 0.5f) + vec3(0, 0, 0.1f) - g.org, tb - right * (bw * 0.5f) + vec3(0, 0, 0.1f) - g.org, vec2(0), vec2(1, 0), vec2(1, 1),
                     vec2(0, 1), rgbv(kPstBlue), mPaint, nrm);
        {
            float th = 0.095f;
            const char* txt = "BUS";
            float w = textAdvance(txt, th);
            strokeText(g, m, txt, tb - right * (w * 0.5f) - vec3(0, 0, th * 0.5f) + nrm * 0.002f, right, vec3(0, 0, 1), th, th * 0.18f, rgb(1.f, 1.f, 1.f, 0.15f),
                       emMat(EA_NIGHT));
        }
        // route badges (two per row)
        float bs = 0.2f;
        for (size_t k = 0; k < routes.size() && k < 4; k++) {
            int row = (int)k / 2, col = (int)k % 2;
            int inRow = Min(2, (int)routes.size() - row * 2);
            float x = inRow == 1 ? 0.f : (col == 0 ? -0.14f : 0.14f);
            vec3 c = face + right * x + vec3(0, 0, (z1 - z0) * 0.5f - 0.34f - row * 0.25f) + nrm * 0.002f;
            routeBadge(g, c, right, vec3(0, 0, 1), nrm, bs, N.busRoutes[routes[k]]);
        }
        // stop name (fine print at the bottom)
        {
            std::string nm = upper(b.name);
            float th = Min(0.05f, (bw - 0.06f) / Max(textAdvance(nm.c_str(), 1.f), 0.1f));
            float w = textAdvance(nm.c_str(), th);
            strokeText(g, m, nm.c_str(), face - right * (w * 0.5f) - vec3(0, 0, (z1 - z0) * 0.5f - 0.05f) + nrm * 0.002f, right, vec3(0, 0, 1), th, th * 0.16f,
                       rgb(0.08f, 0.1f, 0.14f), mPaint);
        }
    }
    // timetable case on the sidewalk side of the pole (lit panel with one line per route)
    {
        vec3 tc = base + inward * 0.1f + vec3(0, 0, 1.45f);
        box(g, tc, al, vec3(0, 0, 1), vec3(0.2f, 0.3f, 0.035f), rgb(0.2f, 0.22f, 0.25f), mPaint);
        vec3 nrm = inward;
        vec3 right = normalize(cross(vec3(0, 0, 1), nrm));
        vec3 face = tc + nrm * 0.037f;
        m.quadFacing(face - right * 0.17f - vec3(0, 0, 0.26f) - g.org, face + right * 0.17f - vec3(0, 0, 0.26f) - g.org, face + right * 0.17f + vec3(0, 0, 0.26f) - g.org,
                     face - right * 0.17f + vec3(0, 0, 0.26f) - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgb(0.95f, 0.96f, 0.93f, 0.06f), emMat(), nrm);
        strokeText(g, m, "TIMETABLE", face - right * 0.15f + vec3(0, 0, 0.19f) + nrm * 0.002f, right, vec3(0, 0, 1), 0.035f, 0.006f, rgbv(kPstBlue), mPaint);
        for (size_t k = 0; k < routes.size() && k < 4; k++) {
            const BusRoute& R = N.busRoutes[routes[k]];
            vec3 row = face - right * 0.15f + vec3(0, 0, 0.1f - k * 0.1f) + nrm * 0.002f;
            m.quadFacing(row - g.org, row + right * 0.05f - g.org, row + right * 0.05f + vec3(0, 0, 0.05f) - g.org, row + vec3(0, 0, 0.05f) - g.org, vec2(0), vec2(1, 0),
                         vec2(1, 1), vec2(0, 1), rgbv(R.color * 1.5f), mPaint, nrm);
            std::string line = R.number + " " + upper(R.name);
            strokeText(g, m, line.c_str(), row + right * 0.065f + nrm * 0.001f, right, vec3(0, 0, 1), 0.03f, 0.005f, rgb(0.1f, 0.1f, 0.12f), mPaint);
            std::string every = StrFormat("EVERY %d MIN", Max(1, (int)(R.headway / 60.f + 0.5f)));
            strokeText(g, m, every.c_str(), row + right * 0.065f - vec3(0, 0, 0.04f) + nrm * 0.001f, right, vec3(0, 0, 1), 0.022f, 0.004f, rgb(0.3f, 0.3f, 0.32f), mPaint);
        }
    }
    // yellow curb along the bus bay (no parking)
    paintRect(g, b.flag + b.face * 0.3f - b.along * 6.5f, b.along, 8.f, 0.1f, b.z + 0.004f, rgb(0.95f, 0.8f, 0.1f), M(MAT_PAINT_YELLOW));
}

// ------------------------------------------------------------------------------------------------ ferry piers
// Concrete T-head pier on pile bents: a ramp up from the shore, the walkway with railings and lamp posts, the T-head
// with fenders, bollards and a boarding gate on the berth face, a waiting shelter with benches, ticket machine and
// timetable, and a name totem at the pier root. Pieces are emitted by the cell that holds their center.
const vec3 kFerryTeal(0.02f, 0.42f, 0.5f);

struct PF {   // pier frame: along = distance from the base toward the head, lateral = right of `dir`
    vec2 base, dir, rt;
    float z;
    vec3 P(float along, float lateral, float zz) const { return vec3(base + dir * along + rt * lateral, zz); }
    vec2 Q(float along, float lateral) const { return base + dir * along + rt * lateral; }
};

void pbox(G& g, const PF& f, float along, float lateral, float z, vec3 he, u32 col, u32 mat, bool bottom = true) {
    g.m->box(f.P(along, lateral, z) - g.org, vec3(f.dir, 0), vec3(f.rt, 0), vec3(0, 0, 1), he, col, mat, bottom);
}
void pcollide(G& g, const PF& f, float along, float lateral, float z, vec3 he) { collide(g, f.P(along, lateral, z), f.dir, he); }

// Railing along a pier-frame segment (posts every ~2 m, top and mid rails)
void pierRail(G& g, const PF& f, vec2 a, vec2 b, u32 col) {
    vec3 A = f.P(a.x, a.y, f.z), B = f.P(b.x, b.y, f.z);
    float L = length(B - A);
    if (L < 0.1f) return;
    int n = Max(1, (int)ceilf(L / 2.f));
    for (int k = 0; k <= n; k++) {
        vec3 p = lerp(A, B, (float)k / n);
        g.m->box(p + vec3(0, 0, 0.53f) - g.org, vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.03f, 0.03f, 0.53f), col, M(MAT_METAL_BRUSHED), false);
    }
    beam(g, A + vec3(0, 0, 1.06f), B + vec3(0, 0, 1.06f), 0.06f, 0.05f, col, M(MAT_METAL_BRUSHED));
    beam(g, A + vec3(0, 0, 0.55f), B + vec3(0, 0, 0.55f), 0.03f, 0.03f, col, M(MAT_METAL_BRUSHED));
    vec2 ax = normalize(vec2(B.x - A.x, B.y - A.y));
    vec3 c = (A + B) * 0.5f;
    collide(g, vec3(c.x, c.y, f.z + 0.6f), ax, vec3(L * 0.5f, 0.06f, 0.6f));
}

void genFerryPier(const SiteElem& e, G& g) {
    using namespace transit_ferry;
    const TransitNet& N = *gTransit;
    if (!N.ready || e.variant >= N.piers.size()) return;
    const FerryPier& fp = N.piers[e.variant];
    PF f;
    f.base = fp.base;
    f.dir = fp.dir;
    f.rt = fp.right();
    f.z = fp.deckZ;
    MeshData& m = *g.m;
    u32 cDeck = rgb(0.7f, 0.69f, 0.66f), cEdge = rgb(0.6f, 0.59f, 0.56f), cPile = rgb(0.55f, 0.54f, 0.5f), cRail = rgb(0.78f, 0.8f, 0.82f);
    u32 mConc = M(MAT_CONCRETE);
    const float W = fp.halfWidth, Lw = fp.length - kHeadDepth;
    const float seabedMin = fp.waterZ - 6.f;
    auto seabed = [&](vec2 p) { return Max(gMap->heightAt(p.x, p.y) - 0.5f, seabedMin); };
    // ---- approach ramp from the shore (descends inland at 1:12 until it meets the ground)
    {
        float k = 0.f;
        float z = f.z;
        std::vector<float> ks, zs;
        ks.push_back(0.f);
        zs.push_back(z);
        while (k < 30.f) {
            float k2 = k + 2.f;
            vec2 q = f.Q(-k2, 0.f);
            float gz = gMap->heightAt(q.x, q.y);
            float z2 = f.z - k2 / 12.f;
            ks.push_back(k2);
            zs.push_back(Max(z2, gz));
            k = k2;
            if (z2 <= gz + 0.05f) break;
        }
        for (size_t i = 0; i + 1 < ks.size(); i++) {
            float a0 = -ks[i + 1], a1 = -ks[i];
            vec2 mid = f.Q((a0 + a1) * 0.5f, 0.f);
            if (!g.owns(mid)) continue;
            float z0 = zs[i + 1], z1 = zs[i];
            vec3 p0 = f.P(a0, -W, z0), p1 = f.P(a1, -W, z1), p2 = f.P(a1, W, z1), p3 = f.P(a0, W, z0);
            m.quadFacing(p0 - g.org, p1 - g.org, p2 - g.org, p3 - g.org, vec2(0), vec2(2, 0), vec2(2, 2 * W), vec2(0, 2 * W), cDeck, mConc, vec3(0, 0, 1));
            for (int sd = -1; sd <= 1; sd += 2) {
                vec3 q0 = f.P(a0, sd * W, z0), q1 = f.P(a1, sd * W, z1);
                vec2 g0 = f.Q(a0, sd * W), g1 = f.Q(a1, sd * W);
                float b0 = gMap->heightAt(g0.x, g0.y) - 0.3f, b1 = gMap->heightAt(g1.x, g1.y) - 0.3f;
                m.quadFacing(q0 - g.org, q1 - g.org, vec3(q1.x, q1.y, Min(b1, z1 - 0.3f)) - g.org, vec3(q0.x, q0.y, Min(b0, z0 - 0.3f)) - g.org, vec2(0), vec2(2, 0),
                             vec2(2, 1), vec2(0, 1), cEdge, mConc, vec3(f.rt * (float)sd, 0));
                if (g.detail) {
                    PF fr = f;
                    fr.z = z1;
                    beam(g, f.P(a0, sd * (W - 0.1f), z0 + 1.02f), f.P(a1, sd * (W - 0.1f), z1 + 1.02f), 0.06f, 0.05f, cRail, M(MAT_METAL_BRUSHED));
                    g.m->box(f.P(a1, sd * (W - 0.1f), z1 + 0.51f) - g.org, vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0.03f, 0.03f, 0.51f), cRail,
                             M(MAT_METAL_BRUSHED), false);
                }
            }
            // walkable steps (4 per 2 m piece)
            for (int q = 0; q < 4; q++) {
                float t0 = q / 4.f, t1 = (q + 1) / 4.f;
                float zz = Lerp(z0, z1, (t0 + t1) * 0.5f);
                pcollide(g, f, Lerp(a0, a1, (t0 + t1) * 0.5f), 0.f, zz - 0.3f, vec3(0.26f, W, 0.3f));
            }
            for (int sd = -1; sd <= 1; sd += 2) pcollide(g, f, (a0 + a1) * 0.5f, sd * (W - 0.1f), (z0 + z1) * 0.5f + 0.6f, vec3(1.f, 0.06f, 0.6f));
        }
    }
    // ---- walkway (4 m pieces)
    for (float a0 = 0.f; a0 < Lw - 0.01f; a0 += 4.f) {
        float a1 = Min(Lw, a0 + 4.f);
        vec2 mid = f.Q((a0 + a1) * 0.5f, 0.f);
        if (!g.owns(mid)) continue;
        float am = (a0 + a1) * 0.5f, ah = (a1 - a0) * 0.5f;
        pbox(g, f, am, 0.f, f.z - 0.2f, vec3(ah, W, 0.2f), cDeck, mConc);
        pbox(g, f, am, 0.f, f.z - 0.55f, vec3(ah, W - 0.4f, 0.15f), cEdge, mConc);   // edge beam
        pcollide(g, f, am, 0.f, f.z - 0.2f, vec3(ah + 0.02f, W, 0.2f));
        if (g.detail) {
            for (int sd = -1; sd <= 1; sd += 2) pierRail(g, f, vec2(a0, sd * (W - 0.08f)), vec2(a1, sd * (W - 0.08f)), cRail);
            // expansion joint strip
            pbox(g, f, a0 + 0.02f, 0.f, f.z + 0.003f, vec3(0.02f, W - 0.1f, 0.004f), rgb(0.25f), M(MAT_RUBBER), false);
        }
        // pile bent every 8 m
        if (fmodf(a0, 8.f) < 0.5f) {
            for (int sd = -1; sd <= 1; sd += 2) {
                vec2 q = f.Q(am, sd * (W - 0.8f));
                float zb = seabed(q);
                cyl(g, vec3(q, zb), 0.32f, 0.32f, f.z - 0.4f - zb, g.detail ? 10 : 6, cPile, mConc, false);
            }
            pbox(g, f, am, 0.f, f.z - 0.75f, vec3(0.4f, W - 0.3f, 0.2f), cPile, mConc);   // pile cap
        }
        // lamp post every 16 m (alternating sides)
        int li = (int)floorf(a0 / 16.f + 0.01f);
        if (fmodf(a0, 16.f) < 0.5f && g.detail) {
            float sd = (li & 1) ? 1.f : -1.f;
            vec3 base = f.P(am, sd * (W - 0.25f), f.z);
            cyl(g, base, 0.06f, 0.05f, 4.2f, 8, rgb(0.2f, 0.22f, 0.24f), M(MAT_METAL_PAINTED));
            g.m->box(base + vec3(f.rt * (-sd * 0.35f), 4.15f) - g.org, vec3(f.rt, 0), vec3(f.dir, 0), vec3(0, 0, 1), vec3(0.4f, 0.12f, 0.06f), rgb(0.2f, 0.22f, 0.24f),
                     M(MAT_METAL_PAINTED));
            lamp(g, base + vec3(f.rt * (-sd * 0.55f), 4.08f), 0.18f, vec3(1.f, 0.9f, 0.75f), 0.8f, EA_NIGHT);
            light(g, base + vec3(f.rt * (-sd * 0.55f), 4.f), vec3(1.f, 0.85f, 0.65f) * 900.f, 14.f, 0, vec3(0, 0, -1), 0.3f);
        }
    }
    // ---- T-head
    const float H = kHeadHalf, D = kHeadDepth;
    for (float l0 = -H; l0 < H - 0.01f; l0 += 6.f) {
        float l1 = Min(H, l0 + 6.f);
        float lm = (l0 + l1) * 0.5f, lh = (l1 - l0) * 0.5f;
        vec2 mid = f.Q(fp.length - D * 0.5f, lm);
        if (!g.owns(mid)) continue;
        pbox(g, f, fp.length - D * 0.5f, lm, f.z - 0.2f, vec3(D * 0.5f, lh, 0.2f), cDeck, mConc);
        pbox(g, f, fp.length - D * 0.5f, lm, f.z - 0.6f, vec3(D * 0.5f - 0.3f, lh, 0.2f), cEdge, mConc);
        pcollide(g, f, fp.length - D * 0.5f, lm, f.z - 0.2f, vec3(D * 0.5f, lh + 0.02f, 0.2f));
        // piles: 3 rows under the head
        for (int r = 0; r < 3; r++) {
            vec2 q = f.Q(fp.length - 0.8f - r * (D - 1.6f) * 0.5f, lm);
            float zb = seabed(q);
            cyl(g, vec3(q, zb), 0.34f, 0.34f, f.z - 0.4f - zb, g.detail ? 10 : 6, cPile, mConc, false);
        }
        if (!g.detail) continue;
        // back railing (landward face), except where the walkway joins
        if (fabsf(lm) > W + 0.5f) pierRail(g, f, vec2(fp.length - D + 0.08f, l0), vec2(fp.length - D + 0.08f, l1), cRail);
        else {
            // the walkway joins here: nothing
        }
        // berth face: fenders every 3 m and a bollard every 6 m; railing except at the boarding gate
        for (float t = l0 + 1.5f; t < l1; t += 3.f) {
            vec3 fc = f.P(fp.length + 0.18f, t, f.z - 1.2f);
            cyl(g, fc, 0.18f, 0.18f, 1.25f, 8, rgb(0.05f), M(MAT_RUBBER), true);
        }
        vec3 bol = f.P(fp.length - 0.45f, lm, f.z);
        cyl(g, bol, 0.16f, 0.14f, 0.5f, 8, rgb(0.15f, 0.15f, 0.16f), M(MAT_METAL_PAINTED), true);
        cyl(g, bol + vec3(0, 0, 0.5f), 0.22f, 0.22f, 0.07f, 8, rgb(0.15f, 0.15f, 0.16f), M(MAT_METAL_PAINTED), true);
        pcollide(g, f, fp.length - 0.45f, lm, f.z + 0.3f, vec3(0.2f, 0.2f, 0.3f));
        float g0 = -1.6f, g1 = 1.6f;   // gate opening
        if (l1 <= g0 || l0 >= g1) pierRail(g, f, vec2(fp.length - 0.08f, l0), vec2(fp.length - 0.08f, l1), cRail);
        else {
            if (l0 < g0) pierRail(g, f, vec2(fp.length - 0.08f, l0), vec2(fp.length - 0.08f, g0), cRail);
            if (l1 > g1) pierRail(g, f, vec2(fp.length - 0.08f, g1), vec2(fp.length - 0.08f, l1), cRail);
        }
        // side railings of the head
        if (l0 <= -H + 0.01f) pierRail(g, f, vec2(fp.length - D, -H + 0.08f), vec2(fp.length, -H + 0.08f), cRail);
        if (l1 >= H - 0.01f) pierRail(g, f, vec2(fp.length - D, H - 0.08f), vec2(fp.length, H - 0.08f), cRail);
        // edge paint
        paintRect(g, f.Q(fp.length - 0.3f, lm), f.rt, lh, 0.12f, f.z + 0.004f, rgb(0.95f, 0.8f, 0.1f), M(MAT_PAINT_YELLOW));
    }
    // ---- boarding gate, gangway plate, shelter and signage (owned by the head center cell)
    if (g.owns(f.Q(fp.length - D * 0.5f, 0.f))) {
        u32 cSteel = rgb(0.25f, 0.27f, 0.3f);
        // gate posts and header sign
        for (int sd = -1; sd <= 1; sd += 2) {
            pbox(g, f, fp.length - 0.25f, sd * 1.7f, f.z + 1.3f, vec3(0.08f, 0.08f, 1.3f), cSteel, M(MAT_METAL_PAINTED));
            pcollide(g, f, fp.length - 0.25f, sd * 1.7f, f.z + 1.3f, vec3(0.1f, 0.1f, 1.3f));
        }
        pbox(g, f, fp.length - 0.25f, 0.f, f.z + 2.75f, vec3(0.06f, 1.9f, 0.22f), rgbv(kFerryTeal), M(MAT_METAL_PAINTED));
        if (g.detail) {
            for (int fs = -1; fs <= 1; fs += 2) {
                vec3 nrm(f.dir * (float)fs, 0.f);
                vec3 right = normalize(cross(vec3(0, 0, 1), nrm));
                const char* txt = "FERRY BOARDING";
                float th = 0.16f, w = textAdvance(txt, th);
                vec3 c = f.P(fp.length - 0.25f + fs * 0.065f, 0.f, f.z + 2.75f);
                strokeText(g, m, txt, c - right * (w * 0.5f) - vec3(0, 0, th * 0.5f), right, vec3(0, 0, 1), th, th * 0.15f, rgb(1.f, 1.f, 1.f, 0.2f), emMat(EA_NIGHT));
            }
        }
        // gangway plate bridging to the ferry door
        pbox(g, f, fp.length + 0.35f, 0.f, f.z - 0.05f, vec3(0.45f, 1.2f, 0.05f), rgb(0.45f, 0.46f, 0.48f), M(MAT_METAL_BRUSHED));
        pcollide(g, f, fp.length + 0.35f, 0.f, f.z - 0.1f, vec3(0.5f, 1.2f, 0.1f));
        // waiting shelter on the head (left of the gate)
        float sa = fp.length - D * 0.5f - 0.3f, sl = -9.5f;
        for (int k = 0; k < 4; k++) {
            float da = (k & 1) ? 1.6f : -1.6f, dl = (k & 2) ? 4.4f : -4.4f;
            pbox(g, f, sa + da, sl + dl, f.z + 1.4f, vec3(0.07f, 0.07f, 1.4f), cSteel, M(MAT_METAL_PAINTED));
            pcollide(g, f, sa + da, sl + dl, f.z + 1.4f, vec3(0.09f, 0.09f, 1.4f));
        }
        pbox(g, f, sa, sl, f.z + 2.85f, vec3(2.1f, 5.f, 0.08f), rgb(0.93f, 0.94f, 0.95f), M(MAT_METAL_PAINTED));
        pbox(g, f, sa, sl, f.z + 3.02f, vec3(2.15f, 5.05f, 0.1f), rgbv(kFerryTeal), M(MAT_METAL_PAINTED));
        if (g.detail) {
            // back glass screen (landward), benches, soffit light
            pbox(g, f, sa - 1.65f, sl, f.z + 1.2f, vec3(0.02f, 4.3f, 0.9f), rgb(0.6f, 0.75f, 0.8f), M(MAT_GLASS));
            for (int k = -1; k <= 1; k += 2) {
                pbox(g, f, sa - 0.9f, sl + k * 2.2f, f.z + 0.45f, vec3(0.25f, 1.6f, 0.04f), rgb(0.55f, 0.4f, 0.28f), M(MAT_WOOD));
                pbox(g, f, sa - 1.12f, sl + k * 2.2f, f.z + 0.75f, vec3(0.03f, 1.6f, 0.25f), rgb(0.55f, 0.4f, 0.28f), M(MAT_WOOD));
                for (int q = -1; q <= 1; q += 2) pbox(g, f, sa - 0.9f, sl + k * 2.2f + q * 1.4f, f.z + 0.22f, vec3(0.2f, 0.04f, 0.22f), cSteel, M(MAT_METAL_PAINTED));
                pcollide(g, f, sa - 0.95f, sl + k * 2.2f, f.z + 0.35f, vec3(0.3f, 1.6f, 0.35f));
            }
            m.quadFacing(f.P(sa - 1.8f, sl - 4.6f, f.z + 2.76f) - g.org, f.P(sa + 1.8f, sl - 4.6f, f.z + 2.76f) - g.org, f.P(sa + 1.8f, sl + 4.6f, f.z + 2.76f) - g.org,
                         f.P(sa - 1.8f, sl + 4.6f, f.z + 2.76f) - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgb(1.f, 0.96f, 0.9f, 0.02f), emMat(), vec3(0, 0, -1));
            light(g, f.P(sa, sl, f.z + 2.6f), vec3(1.f, 0.95f, 0.85f) * 160.f, 9.f, 0);
            // name on the shelter fascia (both long faces)
            std::string nm = upper(fp.name) + " FERRY";
            for (int fs = -1; fs <= 1; fs += 2) {
                vec3 nrm(f.dir * (float)fs, 0.f);
                vec3 right = normalize(cross(vec3(0, 0, 1), nrm));
                float th = 0.13f, w = textAdvance(nm.c_str(), th);
                vec3 c = f.P(sa + fs * 2.16f, sl, f.z + 3.02f);
                strokeText(g, m, nm.c_str(), c - right * (w * 0.5f) - vec3(0, 0, th * 0.5f), right, vec3(0, 0, 1), th, th * 0.15f, rgb(1.f, 1.f, 1.f, 0.2f), emMat(EA_NIGHT));
            }
            // ticket machine and timetable board by the gate
            pbox(g, f, fp.length - 1.2f, 3.2f, f.z + 0.8f, vec3(0.3f, 0.35f, 0.8f), rgbv(kFerryTeal), M(MAT_METAL_PAINTED));
            pbox(g, f, fp.length - 1.2f - 0.31f, 3.2f, f.z + 1.15f, vec3(0.01f, 0.24f, 0.18f), rgb(0.3f, 0.7f, 0.85f, 0.35f), emMat());
            pcollide(g, f, fp.length - 1.2f, 3.2f, f.z + 0.8f, vec3(0.32f, 0.37f, 0.8f));
            {
                vec3 nrm(-f.dir, 0.f);
                vec3 right = normalize(cross(vec3(0, 0, 1), nrm));
                vec3 c = f.P(fp.length - 1.2f, -3.4f, f.z + 1.5f);
                pbox(g, f, fp.length - 1.2f, -3.4f, f.z + 0.75f, vec3(0.05f, 0.05f, 0.75f), cSteel, M(MAT_METAL_PAINTED));
                pbox(g, f, fp.length - 1.2f, -3.4f, f.z + 1.5f, vec3(0.04f, 0.55f, 0.4f), cSteel, M(MAT_METAL_PAINTED));
                vec3 face = c + nrm * 0.045f;
                m.quadFacing(face - right * 0.5f - vec3(0, 0, 0.35f) - g.org, face + right * 0.5f - vec3(0, 0, 0.35f) - g.org, face + right * 0.5f + vec3(0, 0, 0.35f) - g.org,
                             face - right * 0.5f + vec3(0, 0, 0.35f) - g.org, vec2(0), vec2(1, 0), vec2(1, 1), vec2(0, 1), rgb(0.95f, 0.96f, 0.93f, 0.06f), emMat(), nrm);
                strokeText(g, m, "BAY FERRY", face - right * 0.44f + vec3(0, 0, 0.22f) + nrm * 0.002f, right, vec3(0, 0, 1), 0.07f, 0.011f, rgbv(kFerryTeal), M(MAT_METAL_PAINTED));
                int k = 0;
                for (const FerryPier& o : N.piers) {
                    if (&o == &fp) continue;
                    std::string ln = "TO " + upper(o.name);
                    strokeText(g, m, ln.c_str(), face - right * 0.44f + vec3(0, 0, 0.08f - k * 0.1f) + nrm * 0.002f, right, vec3(0, 0, 1), 0.05f, 0.008f, rgb(0.1f, 0.1f, 0.12f),
                               M(MAT_METAL_PAINTED));
                    k++;
                }
                strokeText(g, m, "EVERY 6 MIN", face - right * 0.44f - vec3(0, 0, 0.26f) + nrm * 0.002f, right, vec3(0, 0, 1), 0.045f, 0.007f, rgb(0.3f, 0.3f, 0.32f), M(MAT_METAL_PAINTED));
            }
        }
    }
    // ---- name totem at the pier root
    vec2 tq = f.Q(-3.f, W + 1.4f);
    if (g.owns(tq)) {
        float gz = gMap->heightAt(tq.x, tq.y);
        vec3 tb(tq, gz);
        g.m->box(tb + vec3(0, 0, 1.6f) - g.org, vec3(f.dir, 0), vec3(f.rt, 0), vec3(0, 0, 1), vec3(0.5f, 0.12f, 1.6f), rgbv(kFerryTeal), M(MAT_METAL_PAINTED), true);
        collide(g, tb + vec3(0, 0, 1.6f), f.dir, vec3(0.5f, 0.14f, 1.6f));
        if (g.detail) {
            for (int fs = -1; fs <= 1; fs += 2) {
                vec3 nrm(f.rt * (float)fs, 0.f);
                vec3 right = normalize(cross(vec3(0, 0, 1), nrm));
                vec3 c = tb + nrm * 0.125f;
                strokeText(g, m, "FERRY", c - right * (textAdvance("FERRY", 0.16f) * 0.5f) + vec3(0, 0, 2.7f), right, vec3(0, 0, 1), 0.16f, 0.026f, rgb(1.f, 1.f, 1.f, 0.2f),
                           emMat(EA_NIGHT));
                std::string nm = upper(fp.name);
                float th = Min(0.12f, 0.9f / Max(textAdvance(nm.c_str(), 1.f), 0.1f));
                strokeText(g, m, nm.c_str(), c - right * (textAdvance(nm.c_str(), th) * 0.5f) + vec3(0, 0, 2.35f), right, vec3(0, 0, 1), th, th * 0.15f, rgb(1.f, 1.f, 1.f, 0.2f),
                           emMat(EA_NIGHT));
                strokeText(g, m, "PORTO SOL TRANSIT", c - right * (textAdvance("PORTO SOL TRANSIT", 0.05f) * 0.5f) + vec3(0, 0, 0.5f), right, vec3(0, 0, 1), 0.05f, 0.008f,
                           rgb(0.9f), M(MAT_METAL_PAINTED));
            }
            lamp(g, tb + vec3(0, 0, 3.25f), 0.25f, vec3(0.3f, 0.95f, 1.f), 0.6f, EA_NIGHT);
        }
    }
}

}  // namespace transit_mesh
}  // namespace World
