// Road geometry generation per streaming cell.
#include "roads.h"
#include "../render/mesh.h"
#include "worldtypes.h"

namespace World {

namespace roadmesh_detail {

struct Section {
    vec3 c;       // center
    vec3 right;   // unit right vector (horizontal)
    float s;      // distance along edge
    float miter;  // width multiplier
};

inline u32 colorGray(float g) { return packRGBA8(g, g, g, 1.f); }

// Road surface z + lateral offset (road is flat across)
inline vec3 sectionPoint(const Section& sc, float lateral, float dz = 0.f) {
    return sc.c + sc.right * (lateral * sc.miter) + vec3(0, 0, dz);
}

void buildSections(const RoadEdge& e, std::vector<Section>& out) {
    out.clear();
    float s0 = e.cut0, s1 = e.length - e.cut1;
    if (s1 - s0 < 0.5f) return;
    std::vector<float> ss;
    ss.push_back(s0);
    for (size_t i = 1; i + 1 < e.pts.size(); i++)
        if (e.dist[i] > s0 + 0.3f && e.dist[i] < s1 - 0.3f) ss.push_back(e.dist[i]);
    ss.push_back(s1);
    for (float s : ss) {
        Section sc;
        sc.s = s;
        sc.c = e.posAt(s);
        vec3 t0 = e.tangentAt(Max(0.f, s - 0.5f)), t1 = e.tangentAt(Min(e.length, s + 0.5f));
        vec2 th = normalize(t0.xy() + t1.xy());
        vec2 tseg = normalize(t1.xy());
        sc.right = vec3(th.y, -th.x, 0);
        float cosHalf = Max(0.5f, dot(th, tseg));
        sc.miter = 1.f / cosHalf;
        out.push_back(sc);
    }
}

// Paint stripe along the strip between two sections at lateral offset (meters from center)
void stripe(MeshData& m, const Section& a, const Section& b, float lat, float width, float s0, float s1, u32 mat, vec2 org) {
    // Interpolate along the segment for dashed ranges
    float la = a.s, lb = b.s;
    if (s1 <= la || s0 >= lb) return;
    float t0 = Saturate((s0 - la) / Max(lb - la, 1e-4f)), t1 = Saturate((s1 - la) / Max(lb - la, 1e-4f));
    auto P = [&](float t, float off) {
        vec3 ca = sectionPoint(a, off, 0.02f), cb = sectionPoint(b, off, 0.02f);
        return lerp(ca, cb, t) - vec3(org, 0);
    };
    vec3 p0 = P(t0, lat - width * 0.5f), p1 = P(t0, lat + width * 0.5f), p2 = P(t1, lat + width * 0.5f), p3 = P(t1, lat - width * 0.5f);
    m.quadFacing(p0, p1, p2, p3, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), 0xffffffffu, mat, vec3(0, 0, 1));
}

}  // namespace roadmesh_detail

using namespace roadmesh_detail;

struct RoadCellOutput {
    MeshData road;      // asphalt, sidewalks, curbs, bridge structures
    MeshData decals;    // paint (depth-biased)
    std::vector<PropInstance> props;
    std::vector<LightInstance> lights;
};

void buildRoadCell(const RoadNetwork& net, const WorldMap& map, int cx, int cy, RoadCellOutput& out) {
    vec2 org = cellOrigin(cx, cy);
    std::vector<int> cand;
    net.edgesInRect(org - vec2(4.f), org + vec2(kCellSize + 4.f), cand);
    std::vector<Section> secs;
    const u32 white = 0xffffffffu;
    const u32 matAsphalt = makeMat(MAT_ASPHALT), matAsphaltOld = makeMat(MAT_ASPHALT_OLD);
    const u32 matSidewalk = makeMat(MAT_SIDEWALK), matCurb = makeMat(MAT_CURB), matConcrete = makeMat(MAT_CONCRETE);
    const u32 matWhite = makeMat(MAT_PAINT_WHITE), matYellow = makeMat(MAT_PAINT_YELLOW), matGrass = makeMat(MAT_GRASS);
    const u32 matDirt = makeMat(MAT_DIRT);

    for (int ei : cand) {
        const RoadEdge& e = net.edges[ei];
        const RoadClassInfo& ri = roadInfo(e.cls);
        buildSections(e, secs);
        if (secs.size() < 2) continue;
        bool unpaved = (e.flags & RF_UNPAVED) != 0;
        u32 surf = unpaved ? matDirt : ((e.seed & 3) == 0 ? matAsphaltOld : matAsphalt);
        float hw = e.halfWidth;
        float sw = e.sidewalk;
        bool hwy = e.cls == RC_HIGHWAY || e.cls == RC_RAMP;
        bool twoWay = e.lanesB > 0 && e.lanesF > 0;
        float lanesW = ri.laneWidth;
        for (size_t i = 0; i + 1 < secs.size(); i++) {
            const Section& a = secs[i];
            const Section& b = secs[i + 1];
            vec2 mid = (a.c.xy() + b.c.xy()) * 0.5f;
            if (!inCell(mid, cx, cy)) continue;
            vec3 o3(org, 0);
            float groundA = map.heightAt(a.c.x, a.c.y), groundB = map.heightAt(b.c.x, b.c.y);
            float wA = map.waterAt(a.c.x, a.c.y), wB = map.waterAt(b.c.x, b.c.y);
            float baseA = Max(groundA, wA > kNoWater + 1 ? wA - 3.f : groundA);
            float baseB = Max(groundB, wB > kNoWater + 1 ? wB - 3.f : groundB);
            bool deck = (a.c.z - groundA > 2.2f) || (b.c.z - groundB > 2.2f);
            // ---------- road surface
            {
                vec3 l0 = sectionPoint(a, -hw) - o3, r0 = sectionPoint(a, hw) - o3;
                vec3 l1 = sectionPoint(b, -hw) - o3, r1 = sectionPoint(b, hw) - o3;
                // uv: u across (meters from left), v along (meters)
                out.road.quadFacing(r0, r1, l1, l0, vec2(2 * hw, a.s), vec2(2 * hw, b.s), vec2(0, b.s), vec2(0, a.s), white, surf, vec3(0, 0, 1));
            }
            // ---------- sides: sidewalks+curbs, or shoulders/skirts, or bridge barriers
            for (int side = -1; side <= 1; side += 2) {
                float sgn = (float)side;
                if (sw > 0.f && !(hwy)) {
                    // curb face
                    vec3 c0 = sectionPoint(a, sgn * hw) - o3, c1 = sectionPoint(b, sgn * hw) - o3;
                    vec3 up(0, 0, 0.15f);
                    out.road.quadFacing(c0, c1, c1 + up, c0 + up, vec2(a.s, 0), vec2(b.s, 0), vec2(b.s, 0.15f), vec2(a.s, 0.15f), white, matCurb, a.right * -sgn);
                    // sidewalk top
                    vec3 i0 = sectionPoint(a, sgn * hw, 0.15f) - o3, i1 = sectionPoint(b, sgn * hw, 0.15f) - o3;
                    vec3 x0 = sectionPoint(a, sgn * (hw + sw), 0.15f) - o3, x1 = sectionPoint(b, sgn * (hw + sw), 0.15f) - o3;
                    out.road.quadFacing(i0, x0, x1, i1, vec2(0, a.s), vec2(sw, a.s), vec2(sw, b.s), vec2(0, b.s), white, matSidewalk, vec3(0, 0, 1));
                    // outer skirt down into the terrain
                    vec3 s0 = x0 - vec3(0, 0, 0.9f), s1 = x1 - vec3(0, 0, 0.9f);
                    out.road.quadFacing(x0, x1, s1, s0, vec2(a.s, 0), vec2(b.s, 0), vec2(b.s, 1), vec2(a.s, 1), white, matConcrete, a.right * sgn);
                } else if (!deck) {
                    // shoulder skirt so the terrain never shows a gap
                    vec3 x0 = sectionPoint(a, sgn * hw) - o3, x1 = sectionPoint(b, sgn * hw) - o3;
                    vec3 s0 = sectionPoint(a, sgn * (hw + 1.2f), -0.6f) - o3, s1 = sectionPoint(b, sgn * (hw + 1.2f), -0.6f) - o3;
                    u32 m = unpaved ? matDirt : matDirt;
                    out.road.quadFacing(x0, s0, s1, x1, vec2(0, a.s), vec2(1.2f, a.s), vec2(1.2f, b.s), vec2(0, b.s), white, m, vec3(0, 0, 1));
                }
                if (deck) {
                    // Barrier walls on the deck edges
                    float bw = 0.35f, bh = 0.95f;
                    float lat = sgn * (hw + (sw > 0 && !hwy ? sw : 0.f) + bw * 0.5f);
                    vec3 p0 = sectionPoint(a, lat - bw * 0.5f) - o3, p1 = sectionPoint(b, lat - bw * 0.5f) - o3;
                    vec3 q0 = sectionPoint(a, lat + bw * 0.5f) - o3, q1 = sectionPoint(b, lat + bw * 0.5f) - o3;
                    vec3 up(0, 0, bh);
                    out.road.quadFacing(p0 + up, q0 + up, q1 + up, p1 + up, vec2(0, a.s), vec2(bw, a.s), vec2(bw, b.s), vec2(0, b.s), white, matConcrete, vec3(0, 0, 1));
                    out.road.quadFacing(q1, q0, q0 + up, q1 + up, vec2(b.s, 0), vec2(a.s, 0), vec2(a.s, bh), vec2(b.s, bh), white, matConcrete, a.right * sgn);
                    out.road.quadFacing(p0, p1, p1 + up, p0 + up, vec2(a.s, 0), vec2(b.s, 0), vec2(b.s, bh), vec2(a.s, bh), white, matConcrete, a.right * -sgn);
                    // Deck side face down to the slab bottom
                    float outerLat = sgn * (hw + (sw > 0 && !hwy ? sw : 0.f) + bw);
                    vec3 d0 = sectionPoint(a, outerLat) - o3, d1 = sectionPoint(b, outerLat) - o3;
                    vec3 dn(0, 0, -1.3f);
                    out.road.quadFacing(d0, d1, d1 + dn, d0 + dn, vec2(a.s, 0), vec2(b.s, 0), vec2(b.s, 1.3f), vec2(a.s, 1.3f), white, matConcrete, a.right * sgn);
                }
            }
            if (deck) {
                // Deck underside
                float outer = hw + (sw > 0 && !hwy ? sw : 0.f) + 0.35f;
                vec3 l0 = sectionPoint(a, -outer, -1.3f) - o3, r0 = sectionPoint(a, outer, -1.3f) - o3;
                vec3 l1 = sectionPoint(b, -outer, -1.3f) - o3, r1 = sectionPoint(b, outer, -1.3f) - o3;
                out.road.quadFacing(l0, l1, r1, r0, vec2(0, a.s), vec2(0, b.s), vec2(2 * outer, b.s), vec2(2 * outer, a.s), white, matConcrete, vec3(0, 0, -1));
                // Pillars every ~32 m
                float spacing = hwy ? 34.f : 28.f;
                int k0 = (int)ceilf(a.s / spacing), k1 = (int)floorf(b.s / spacing);
                for (int k = k0; k <= k1; k++) {
                    float s = k * spacing;
                    if (s < a.s || s >= b.s) continue;
                    float t = (s - a.s) / Max(b.s - a.s, 1e-4f);
                    vec3 c = lerp(a.c, b.c, t);
                    float g = Lerp(baseA, baseB, t);
                    if (c.z - g < 3.f) continue;
                    vec3 rv = normalize(lerp(a.right, b.right, t));
                    vec3 fw = normalize(cross(vec3(0, 0, 1), rv));
                    float top = c.z - 1.3f;
                    float h = top - g + 1.f;
                    if (hwy || outer > 7.f) {
                        // Hammerhead pier: column + cap beam
                        out.road.box(vec3(c.x, c.y, g - 1.f + h * 0.5f - 0.6f) - o3, rv, fw, vec3(0, 0, 1), vec3(1.1f, 1.1f, h * 0.5f - 0.6f), white, matConcrete);
                        out.road.box(vec3(c.x, c.y, top - 0.6f) - o3, rv, fw, vec3(0, 0, 1), vec3(outer * 0.85f, 1.0f, 0.6f), white, matConcrete, true);
                    } else {
                        for (int sgn = -1; sgn <= 1; sgn += 2) {
                            vec3 pc = c + rv * (sgn * outer * 0.55f);
                            out.road.cylinder(vec3(pc.x, pc.y, g - 1.f) - o3, 0.55f, 0.55f, h, 10, white, matConcrete, false);
                        }
                    }
                }
            }
            // ---------- Median for boulevards: raised planted strip
            if (ri.median > 0.f && !hwy && twoWay) {
                float mw = ri.median * 0.5f;
                vec3 up(0, 0, 0.18f);
                for (int side = -1; side <= 1; side += 2) {
                    vec3 c0 = sectionPoint(a, side * mw) - o3, c1 = sectionPoint(b, side * mw) - o3;
                    out.road.quadFacing(c0, c1, c1 + up, c0 + up, vec2(a.s, 0), vec2(b.s, 0), vec2(b.s, 0.18f), vec2(a.s, 0.18f), white, matCurb, a.right * (float)side);
                }
                vec3 l0 = sectionPoint(a, -mw, 0.18f) - o3, r0 = sectionPoint(a, mw, 0.18f) - o3;
                vec3 l1 = sectionPoint(b, -mw, 0.18f) - o3, r1 = sectionPoint(b, mw, 0.18f) - o3;
                out.road.quadFacing(r0, r1, l1, l0, vec2(2 * mw, a.s), vec2(2 * mw, b.s), vec2(0, b.s), vec2(0, a.s), white, matGrass, vec3(0, 0, 1));
            } else if (hwy && twoWay) {
                // Jersey barrier
                float bw = 0.3f, bh = 0.85f;
                vec3 p0 = sectionPoint(a, -bw) - o3, p1 = sectionPoint(b, -bw) - o3, q0 = sectionPoint(a, bw) - o3, q1 = sectionPoint(b, bw) - o3;
                vec3 up(0, 0, bh);
                out.road.quadFacing(p0 + up, q0 + up, q1 + up, p1 + up, vec2(0, a.s), vec2(0.6f, a.s), vec2(0.6f, b.s), vec2(0, b.s), white, matConcrete, vec3(0, 0, 1));
                out.road.quadFacing(q1, q0, q0 + up, q1 + up, vec2(b.s, 0), vec2(a.s, 0), vec2(a.s, bh), vec2(b.s, bh), white, matConcrete, a.right);
                out.road.quadFacing(p0, p1, p1 + up, p0 + up, vec2(a.s, 0), vec2(b.s, 0), vec2(b.s, bh), vec2(a.s, bh), white, matConcrete, a.right * -1.f);
            }
            // ---------- Lane markings
            if (!unpaved && e.cls != RC_LANE) {
                float medianHalf = (ri.median > 0.f && twoWay) ? ri.median * 0.5f : 0.f;
                float laneStart = medianHalf;  // lanes start next to the median / center line
                if (!twoWay) laneStart = -lanesW * e.lanesF * 0.5f;
                // Center line (two-way without median)
                if (twoWay && medianHalf == 0.f && !hwy) {
                    if (e.cls == RC_STREET) {
                        // dashed yellow
                        for (float s = floorf(a.s / 12.f) * 12.f; s < b.s; s += 12.f) stripe(out.decals, a, b, 0.f, 0.12f, s, s + 4.f, matYellow, org);
                    } else {
                        stripe(out.decals, a, b, -0.12f, 0.12f, a.s, b.s, matYellow, org);
                        stripe(out.decals, a, b, 0.12f, 0.12f, a.s, b.s, matYellow, org);
                    }
                } else if (hwy && twoWay) {
                    stripe(out.decals, a, b, -0.5f, 0.15f, a.s, b.s, matYellow, org);
                    stripe(out.decals, a, b, 0.5f, 0.15f, a.s, b.s, matYellow, org);
                    laneStart = 0.6f;
                }
                // Lane dividers
                for (int dir = -1; dir <= 1; dir += 2) {
                    int lanes = dir > 0 ? e.lanesF : e.lanesB;
                    if (!twoWay) { lanes = e.lanesF; if (dir < 0) continue; }
                    for (int l = 1; l < lanes; l++) {
                        float lat = twoWay ? dir * (laneStart + l * lanesW) : laneStart + l * lanesW;
                        for (float s = floorf(a.s / 12.f) * 12.f; s < b.s; s += 12.f) stripe(out.decals, a, b, lat, 0.13f, s, s + 3.f, matWhite, org);
                    }
                    // edge line
                    if (hwy || e.cls == RC_RURAL || e.cls == RC_BOULEVARD) {
                        float lat = twoWay ? dir * (laneStart + lanes * lanesW + 0.15f) : laneStart + lanes * lanesW + 0.15f;
                        stripe(out.decals, a, b, lat, 0.15f, a.s, b.s, matWhite, org);
                        if (!twoWay) stripe(out.decals, a, b, laneStart - 0.15f, 0.15f, a.s, b.s, matYellow, org);
                    }
                }
            }
            // ---------- Street furniture along the edge (deterministic per segment)
            if (!deck && !unpaved) {
                float spacing = hwy ? 55.f : (e.cls == RC_RURAL ? 70.f : 32.f);
                bool lit = e.cls != RC_DIRT && !(e.cls == RC_RURAL && map.regionAt(mid.x, mid.y) != REG_FARMLAND && false);
                if (e.cls == RC_RURAL) lit = (e.seed & 1) == 0;
                int k0 = (int)ceilf((a.s + 6.f) / spacing), k1 = (int)floorf((b.s - 6.f) / spacing);
                for (int k = k0; k <= k1 && lit; k++) {
                    float s = k * spacing;
                    if (s < a.s || s >= b.s || s < e.cut0 + 8.f || s > e.length - e.cut1 - 8.f) continue;
                    float t = (s - a.s) / Max(b.s - a.s, 1e-4f);
                    vec3 c = lerp(a.c, b.c, t);
                    vec3 rv = normalize(lerp(a.right, b.right, t));
                    int side = (k & 1) ? 1 : -1;
                    if (hwy) side = 0;
                    PropInstance pi;
                    float off = hw + (sw > 0 ? 0.6f : 1.4f);
                    if (side == 0) {
                        // highway median lights (double arm)
                        pi.pos = c + vec3(0, 0, 0.85f);
                        pi.type = PROP_STREETLIGHT_DOUBLE;
                    } else {
                        pi.pos = c + rv * (side * off) + vec3(0, 0, sw > 0 ? 0.15f : 0.f);
                        pi.type = PROP_STREETLIGHT;
                    }
                    pi.yaw = atan2f(-rv.y * side, -rv.x * side);  // arm points over the road
                    if (side == 0) pi.yaw = atan2f(rv.y, rv.x);
                    pi.scale = hwy ? 1.35f : (e.cls <= RC_AVENUE ? 1.1f : 1.f);
                    pi.variant = (u8)(e.seed % 3);
                    pi.flags = 0;
                    out.props.push_back(pi);
                    float armLen = 2.2f * pi.scale;
                    float poleH = 8.5f * pi.scale;
                    LightInstance li;
                    if (side == 0) {
                        for (int s2 = -1; s2 <= 1; s2 += 2) {
                            li.pos = pi.pos + rv * (s2 * armLen) + vec3(0, 0, poleH);
                            li.color = vec3(1.0f, 0.78f, 0.52f) * 9000.f;
                            li.radius = 36.f;
                            li.dir = vec3(0, 0, -1);
                            li.cone = 0.25f;
                            li.type = 0;
                            out.lights.push_back(li);
                        }
                    } else {
                        li.pos = pi.pos - rv * (side * armLen) + vec3(0, 0, poleH);
                        bool warm = (e.seed >> 3) % 3 != 0;
                        li.color = (warm ? vec3(1.0f, 0.72f, 0.42f) : vec3(0.85f, 0.9f, 1.0f)) * 7000.f;
                        li.radius = 30.f;
                        li.dir = vec3(0, 0, -1);
                        li.cone = 0.2f;
                        li.type = 0;
                        out.lights.push_back(li);
                    }
                }
                // Palms in boulevard medians
                if (ri.median > 0.f && !hwy && twoWay) {
                    for (float s = ceilf(a.s / 14.f) * 14.f; s < b.s; s += 14.f) {
                        if (s < e.cut0 + 10.f || s > e.length - e.cut1 - 10.f) continue;
                        float t = (s - a.s) / Max(b.s - a.s, 1e-4f);
                        PropInstance pi;
                        pi.pos = lerp(a.c, b.c, t) + vec3(0, 0, 0.18f);
                        pi.yaw = hashToFloat(hash2i((int)s, (int)e.seed)) * kTwoPi;
                        pi.scale = 0.85f + hashToFloat(hash2i((int)s, (int)e.seed + 1)) * 0.35f;
                        pi.type = PROP_PALM_TALL;
                        pi.variant = (u8)(hash2i((int)s, 3) % 4);
                        pi.flags = 0;
                        out.props.push_back(pi);
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- Intersections
    // Iterate nodes near the cell via incident edges of candidate edges
    std::vector<int> nodeCand;
    for (int ei : cand) {
        nodeCand.push_back(net.edges[ei].n0);
        nodeCand.push_back(net.edges[ei].n1);
    }
    std::sort(nodeCand.begin(), nodeCand.end());
    nodeCand.erase(std::unique(nodeCand.begin(), nodeCand.end()), nodeCand.end());
    for (int ni : nodeCand) {
        const RoadNode& nd = net.nodes[ni];
        if (!inCell(nd.p, cx, cy)) continue;
        int deg = (int)nd.edges.size();
        if (deg == 1) {
            // Dead end: cul-de-sac bulb for residential lanes
            const RoadEdge& e = net.edges[nd.edges[0]];
            if (e.cls == RC_LANE || e.cls == RC_STREET) {
                float r = e.halfWidth + 4.5f;
                vec3 c(nd.p, nd.z);
                std::vector<vec3> ring;
                for (int k = 0; k < 20; k++) {
                    float ang = kTwoPi * k / 20.f;
                    ring.push_back(c + vec3(cosf(ang) * r, sinf(ang) * r, 0.01f) - vec3(org, 0));
                }
                out.road.polygon(ring, vec3(0, 0, 1), white, matAsphalt, 1.f);
                if (e.sidewalk > 0) {
                    for (int k = 0; k < 20; k++) {
                        float a0 = kTwoPi * k / 20.f, a1 = kTwoPi * (k + 1) / 20.f;
                        vec3 i0 = c + vec3(cosf(a0) * r, sinf(a0) * r, 0.15f) - vec3(org, 0), i1 = c + vec3(cosf(a1) * r, sinf(a1) * r, 0.15f) - vec3(org, 0);
                        vec3 x0 = c + vec3(cosf(a0) * (r + e.sidewalk), sinf(a0) * (r + e.sidewalk), 0.15f) - vec3(org, 0);
                        vec3 x1 = c + vec3(cosf(a1) * (r + e.sidewalk), sinf(a1) * (r + e.sidewalk), 0.15f) - vec3(org, 0);
                        out.road.quadFacing(i0, x0, x1, i1, vec2(0, 0), vec2(e.sidewalk, 0), vec2(e.sidewalk, 1), vec2(0, 1), white, matSidewalk, vec3(0, 0, 1));
                        vec3 inward = normalize(vec3(c.xy() - (i0 + vec3(org, 0)).xy(), 0));
                        out.road.quadFacing(i1 - vec3(0, 0, 0.15f), i0 - vec3(0, 0, 0.15f), i0, i1, vec2(0, 0), vec2(1, 0), vec2(1, 0.15f), vec2(0, 0.15f), white, matCurb, inward);
                    }
                }
            }
            continue;
        }
        if (nd.radius <= 0.f || deg < 3) continue;
        // Gather approach geometry
        struct Approach {
            float ang;
            vec2 dir;
            vec3 cutPt;
            float hw, sw;
            int edge;
            bool outgoing;  // edge starts at this node
        };
        std::vector<Approach> ap;
        for (int ei : nd.edges) {
            const RoadEdge& e = net.edges[ei];
            bool out0 = e.n0 == ni;
            float s = out0 ? e.cut0 : e.length - e.cut1;
            vec3 p = e.posAt(s);
            vec2 d = normalize(p.xy() - nd.p);
            if (length2(p.xy() - nd.p) < 1e-4f) d = normalize((out0 ? e.pts[1] : e.pts[e.pts.size() - 2]).xy() - nd.p);
            Approach A;
            A.ang = atan2f(d.y, d.x);
            A.dir = d;
            A.cutPt = p;
            A.hw = e.halfWidth;
            A.sw = e.sidewalk;
            A.edge = ei;
            A.outgoing = out0;
            ap.push_back(A);
        }
        std::sort(ap.begin(), ap.end(), [](const Approach& x, const Approach& y) { return x.ang < y.ang; });
        int n = (int)ap.size();
        std::vector<vec3> poly;
        std::vector<std::pair<std::vector<vec3>, std::vector<vec3>>> corners;  // inner curve, outer curve (for sidewalks)
        vec3 o3(org, 0);
        for (int i = 0; i < n; i++) {
            const Approach& A = ap[i];
            const Approach& B = ap[(i + 1) % n];
            vec2 lA = perp(A.dir), lB = perp(B.dir);
            // approach i: right point then left point (CCW around the node)
            vec3 Ar = A.cutPt + vec3(-lA * A.hw, 0.f), Al = A.cutPt + vec3(lA * A.hw, 0.f);
            vec3 Br = B.cutPt + vec3(-lB * B.hw, 0.f);
            poly.push_back(Ar);
            poly.push_back(Al);
            // Corner curve from Al to Br via the intersection of the two road edge lines
            vec2 p1 = Al.xy(), d1 = A.dir, p2 = Br.xy(), d2 = B.dir;
            float den = cross(d1, d2);
            vec2 C = (p1 + p2) * 0.5f;
            bool curved = false;
            if (fabsf(den) > 0.15f) {
                float t = cross(p2 - p1, d2) / den;
                vec2 X = p1 + d1 * t;
                if (length(X - nd.p) < nd.radius * 2.5f) { C = X; curved = true; }
            }
            std::vector<vec3> inner, outer;
            int segs = curved ? 6 : 1;
            float swc = Min(A.sw, B.sw);
            for (int k = 1; k < segs; k++) {
                float t = (float)k / segs;
                vec2 q = p1 * ((1 - t) * (1 - t)) + C * (2 * (1 - t) * t) + p2 * (t * t);
                float z = Lerp(Al.z, Br.z, t);
                inner.push_back(vec3(q, z));
            }
            for (auto& q : inner) poly.push_back(q);
            if (swc > 0.f) {
                // outer sidewalk curve offset by sidewalk width
                vec2 po1 = p1 + lA * swc, po2 = p2 - lB * swc;
                vec2 Co = C;
                if (curved) {
                    float den2 = cross(d1, d2);
                    float t2 = cross(po2 - po1, d2) / den2;
                    Co = po1 + d1 * t2;
                } else Co = (po1 + po2) * 0.5f;
                std::vector<vec3> in2, out2;
                in2.push_back(Al);
                for (auto& q : inner) in2.push_back(q);
                in2.push_back(Br);
                for (int k = 0; k <= segs; k++) {
                    float t = (float)k / segs;
                    vec2 q = po1 * ((1 - t) * (1 - t)) + Co * (2 * (1 - t) * t) + po2 * (t * t);
                    out2.push_back(vec3(q, Lerp(Al.z, Br.z, t)));
                }
                corners.push_back({in2, out2});
            }
        }
        for (auto& p : poly) p = p - o3 + vec3(0, 0, 0.005f);
        out.road.polygon(poly, vec3(0, 0, 1), white, matAsphalt, 1.f);
        // Sidewalk corners with curb faces
        for (auto& cr : corners) {
            auto& in2 = cr.first;
            auto& out2 = cr.second;
            size_t m = Min(in2.size(), out2.size());
            for (size_t k = 0; k + 1 < m; k++) {
                vec3 i0 = in2[k] - o3 + vec3(0, 0, 0.15f), i1 = in2[k + 1] - o3 + vec3(0, 0, 0.15f);
                vec3 x0 = out2[k] - o3 + vec3(0, 0, 0.15f), x1 = out2[k + 1] - o3 + vec3(0, 0, 0.15f);
                out.road.quadFacing(i0, x0, x1, i1, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), white, matSidewalk, vec3(0, 0, 1));
                vec3 dn(0, 0, -0.15f);
                vec3 toRoad = normalize(vec3((i0 - x0).xy() + (i1 - x1).xy(), 0));
                out.road.quadFacing(i1 + dn, i0 + dn, i0, i1, vec2(0, 0), vec2(1, 0), vec2(1, 0.15f), vec2(0, 0.15f), white, matCurb, toRoad);
                out.road.quadFacing(x0, x1, x1 + vec3(0, 0, -0.9f), x0 + vec3(0, 0, -0.9f), vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), white, matConcrete, toRoad * -1.f);
            }
        }
        // Crosswalks + stop lines at controlled intersections
        if (nd.control >= 1) {
            for (const Approach& A : ap) {
                const RoadEdge& e = net.edges[A.edge];
                if (e.cls == RC_HIGHWAY || e.cls == RC_RAMP) continue;
                vec2 l = perp(A.dir);
                float w = A.hw;
                // zebra: stripes 0.5 m wide, along the approach direction, 3 m long
                for (float x = -w + 0.6f; x < w - 0.3f; x += 1.1f) {
                    vec3 c0 = A.cutPt + vec3(l * x + A.dir * 0.8f, 0.03f) - o3;
                    vec3 c1 = A.cutPt + vec3(l * (x + 0.55f) + A.dir * 0.8f, 0.03f) - o3;
                    vec3 c2 = c1 + vec3(A.dir * 3.f, 0), c3 = c0 + vec3(A.dir * 3.f, 0);
                    out.decals.quadFacing(c0, c1, c2, c3, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), white, matWhite, vec3(0, 0, 1));
                }
                // stop line on the incoming half (right side when driving toward the node)
                vec3 s0 = A.cutPt + vec3(A.dir * 4.4f, 0.03f) - o3;
                vec3 s1 = s0 + vec3(A.dir * 0.45f, 0);
                // incoming lanes are on the -l side of the outward direction
                vec3 a0 = s0 + vec3(l * 0.2f, 0), a1 = s0 + vec3(l * (-(w - 0.3f)), 0);
                vec3 b0 = s1 + vec3(l * 0.2f, 0), b1 = s1 + vec3(l * (-(w - 0.3f)), 0);
                out.decals.quadFacing(a0, a1, b1, b0, vec2(0, 0), vec2(1, 0), vec2(1, 1), vec2(0, 1), white, matWhite, vec3(0, 0, 1));
            }
            // Traffic lights / stop signs at corners
            for (int i = 0; i < n; i++) {
                const Approach& A = ap[i];
                const RoadEdge& e = net.edges[A.edge];
                if (e.cls == RC_HIGHWAY || e.cls == RC_RAMP) continue;
                vec2 l = perp(A.dir);
                PropInstance pi;
                // right side of the incoming traffic (which drives toward the node on the lane at -l side)
                vec2 pos = A.cutPt.xy() + A.dir * 1.5f - l * (A.hw + Max(A.sw * 0.5f, 0.8f));
                pi.pos = vec3(pos, A.cutPt.z + (A.sw > 0 ? 0.15f : 0.f));
                pi.yaw = atan2f(-A.dir.y, -A.dir.x);
                pi.scale = 1.f;
                pi.type = nd.control == 2 ? PROP_TRAFFIC_LIGHT : PROP_STOP_SIGN;
                pi.variant = (u8)(e.cls <= RC_AVENUE ? 1 : 0);
                pi.flags = (u16)A.edge;
                out.props.push_back(pi);
            }
        }
    }
}

}  // namespace World
