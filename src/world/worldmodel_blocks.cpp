// The blocks compile (worldmodel.h compileBlocks): parcels, plazas and dressing into the game's blocks (blocksbin.h);
// and what the AI's graphs read from them (compileBlockSections): the parcels, and the lots with their drive graphs.
#include "worldmodel.h"
#include "catalogcodes.h"
#include "gridcodec.h"
#include "roomlight.h"
#include "worldmap.h"
#include "../core/rng.h"
#include <algorithm>
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

std::string fmt1(float v) {
    char b[32];
    snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

// ------------------------------------------------------------------------------------- lines, for the lots' graphs

// The RoadEdge's own s at `at` plan metres along it from its n0; -1 past its ends (by more than a centimetre)
float edgeS(const RoadEdge& e, float at) {
    if (e.pts.size() < 2 || e.dist.size() != e.pts.size() || at < -0.01f) return -1.f;
    float plan = 0.f;
    for (size_t k = 0; k + 1 < e.pts.size(); k++) {
        const float seg = distance(e.pts[k].xy(), e.pts[k + 1].xy());
        if (at <= plan + seg) return Lerp(e.dist[k], e.dist[k + 1], seg > 1e-6f ? Clamp((at - plan) / seg, 0.f, 1.f) : 0.f);
        plan += seg;
    }
    return at <= plan + 0.01f ? e.dist.back() : -1.f;
}

float planLength(const std::vector<vec2>& l) {
    float s = 0.f;
    for (size_t k = 0; k + 1 < l.size(); k++) s += distance(l[k], l[k + 1]);
    return s;
}
float planLength(const std::vector<vec3>& l) {
    float s = 0.f;
    for (size_t k = 0; k + 1 < l.size(); k++) s += distance(l[k].xy(), l[k + 1].xy());
    return s;
}

// The point `s` plan metres along `l` (clamped to it), and its unit direction there
vec2 pointAlong(const std::vector<vec2>& l, float s, vec2* tangent) {
    for (size_t k = 0; k + 1 < l.size(); k++) {
        const float seg = distance(l[k], l[k + 1]);
        if (s <= seg || k + 2 == l.size()) {
            *tangent = normalize(l[k + 1] - l[k]);
            return l[k] + (l[k + 1] - l[k]) * (seg > 1e-6f ? Clamp(s / seg, 0.f, 1.f) : 0.f);
        }
        s -= seg;
    }
    *tangent = vec2(1.f, 0.f);
    return l.empty() ? vec2(0.f) : l[0];
}

// `l` moved `d` to its left (to its right where d is negative), mitred at its corners
std::vector<vec2> offsetLine(const std::vector<vec2>& l, float d) {
    const auto leftOf = [](vec2 a, vec2 b) {
        const vec2 t = normalize(b - a);
        return vec2(-t.y, t.x);
    };
    std::vector<vec2> out;
    for (size_t k = 0; k < l.size(); k++) {
        vec2 n;
        if (k == 0) n = leftOf(l[0], l[1]);
        else if (k + 1 == l.size()) n = leftOf(l[k - 1], l[k]);
        else {
            const vec2 n1 = leftOf(l[k], l[k + 1]), m = normalize(leftOf(l[k - 1], l[k]) + n1);
            n = m * (1.f / Max(dot(m, n1), 0.3f));
        }
        out.push_back(l[k] + n * d);
    }
    return out;
}

// The point of `l` nearest p in plan: its distance, the plan metres along `l` to it, its segment and the fraction
// along that
struct LineNear {
    float d = 1e30f, s = 0.f;
    size_t seg = 0;
    float t = 0.f;
};
LineNear nearestOn(const std::vector<vec3>& l, vec2 p) {
    LineNear best;
    float s0 = 0.f;
    for (size_t k = 0; k + 1 < l.size(); k++) {
        const vec2 a = l[k].xy(), ab = l[k + 1].xy() - a;
        const float len2 = length2(ab), seg = sqrtf(len2);
        const float t = len2 > 1e-12f ? Clamp(dot(p - a, ab) / len2, 0.f, 1.f) : 0.f;
        const float d = distance(p, a + ab * t);
        if (d < best.d) {
            best.d = d;
            best.s = s0 + seg * t;
            best.seg = k;
            best.t = t;
        }
        s0 += seg;
    }
    return best;
}
// The unit direction of `l` at its point `n`
vec2 directionAt(const std::vector<vec3>& l, const LineNear& n) { return normalize(l[n.seg + 1].xy() - l[n.seg].xy()); }
// The point `s` plan metres along `l`, as nearestOn gives one
LineNear pointOn(const std::vector<vec3>& l, float s) {
    LineNear at;
    at.d = 0.f;
    at.s = s;
    for (size_t k = 0; k + 1 < l.size(); k++) {
        const float seg = distance(l[k].xy(), l[k + 1].xy());
        at.seg = k;
        at.t = seg > 1e-6f ? Clamp(s / seg, 0.f, 1.f) : 0.f;
        if (s <= seg) break;
        s -= seg;
    }
    return at;
}

// ------------------------------------------------------------------------------------------ a lot's drive graph

// An aisle as rows give it, before the graph: its points the way it runs (a one-way aisle's direction of travel)
struct Aisle {
    std::vector<vec2> pts;
    float width = 6.f;
    bool oneway = false;
    int level = 0;
    bool alive = true;
};

// Two aisles that run along each other share one (worldmodel.h compileBlockSections): a straight pair within 1.5 m,
// parallel, overlapping by a metre and running the same way (a two-way one turned to match), down its middle; or two
// polylines of as many points, each within 1.5 m of its partner. Merges `b` into `a`; false where they don't
bool mergeAisles(Aisle& a, const Aisle& b) {
    if (a.level != b.level || a.pts.size() != b.pts.size() || a.pts.size() < 2) return false;
    std::vector<vec2> q = b.pts;   // (turned round to run as `a` does, where neither is one way)
    if (a.pts.size() == 2) {
        const float la = distance(a.pts[0], a.pts[1]), lb = distance(q[0], q[1]);
        if (la < 1e-3f || lb < 1e-3f) return false;
        const vec2 da = (a.pts[1] - a.pts[0]) / la, na(-da.y, da.x);
        vec2 db = (q[1] - q[0]) / lb;
        if (fabsf(cross(da, db)) > 0.0872f) return false;   // (5 degrees)
        if (dot(da, db) < 0.f) {
            if (a.oneway || b.oneway) return false;
            std::swap(q[0], q[1]);
            db = -db;
        }
        const float h0 = dot(na, q[0] - a.pts[0]), h1 = dot(na, q[1] - a.pts[0]);
        if (fabsf(h0) > 1.5f || fabsf(h1) > 1.5f) return false;
        const float p0 = dot(da, q[0] - a.pts[0]), p1 = dot(da, q[1] - a.pts[0]);
        if (Min(la, p1) - Max(0.f, p0) < 1.f) return false;
        const float lo = Min(0.f, p0), hi = Max(la, p1), h = (h0 + h1) * 0.25f;
        const vec2 o = a.pts[0];
        a.pts[0] = o + da * lo + na * h;
        a.pts[1] = o + da * hi + na * h;
    } else {
        bool same = true, back = true;
        for (size_t k = 0; k < q.size(); k++) {
            same = same && distance(a.pts[k], q[k]) <= 1.5f;
            back = back && distance(a.pts[k], q[q.size() - 1 - k]) <= 1.5f;
        }
        if (!same && (!back || a.oneway || b.oneway)) return false;
        if (!same) std::reverse(q.begin(), q.end());
        for (size_t k = 0; k < q.size(); k++) a.pts[k] = (a.pts[k] + q[k]) * 0.5f;
    }
    a.width = Max(a.width, b.width);
    a.oneway = a.oneway && b.oneway;
    return true;
}

// The graph as it is built: nodes merged into others stay, pointing at the one they joined, until it is written
struct LotGraph {
    HandLot& lot;
    std::vector<int> into;        // per node: itself, or the node it was merged into
    std::vector<int> linkAisle;   // per link: the aisle it carries; -1 for a drive or a ramp

    explicit LotGraph(HandLot& l) : lot(l) {}
    bool alive(int n) const { return into[(size_t)n] == n; }
    bool open(int n) const {      // a node others may join: not an entry or an exit
        return alive(n) && lot.nodes[(size_t)n].kind != HandLot::LN_ENTRY && lot.nodes[(size_t)n].kind != HandLot::LN_EXIT;
    }
    int addNode(vec2 p, int level, u8 kind) {
        HandLot::Node n;
        n.p = vec3(p, (float)level * kLotLevelHeight);
        n.level = level;
        n.kind = kind;
        lot.nodes.push_back(n);
        into.push_back((int)into.size());
        return (int)lot.nodes.size() - 1;
    }
    // The node within 1 m of p on `level`, else a new one
    int nodeAt(vec2 p, int level, u8 kind) {
        int best = -1;
        float bd = 1.f;
        for (size_t i = 0; i < lot.nodes.size(); i++) {
            if (!open((int)i) || lot.nodes[i].level != level) continue;
            const float d = distance(lot.nodes[i].p.xy(), p);
            if (d <= bd) {
                bd = d;
                best = (int)i;
            }
        }
        if (best < 0) return addNode(p, level, kind);
        if (kind == HandLot::LN_RAMP) lot.nodes[(size_t)best].kind = kind;
        return best;
    }
    int addLink(int a, int b, std::vector<vec3> pts, float width, bool oneway, u8 kind, int aisle) {
        HandLot::Link k;
        k.a = a;
        k.b = b;
        k.width = width;
        k.oneway = oneway ? 1 : 0;
        k.kind = kind;
        k.pts = std::move(pts);
        k.pts.front() = lot.nodes[(size_t)a].p;
        k.pts.back() = lot.nodes[(size_t)b].p;
        lot.links.push_back(k);
        linkAisle.push_back(aisle);
        return (int)lot.links.size() - 1;
    }
    int degree(int n) const {
        int d = 0;
        for (const HandLot::Link& k : lot.links) d += (k.a == n) + (k.b == n);
        return d;
    }
    // Node `from` joins `to`: its links end there
    void merge(int from, int to) {
        for (HandLot::Link& k : lot.links) {
            if (k.a == from) {
                k.a = to;
                k.pts.front() = lot.nodes[(size_t)to].p;
            }
            if (k.b == from) {
                k.b = to;
                k.pts.back() = lot.nodes[(size_t)to].p;
            }
        }
        into[(size_t)from] = to;
    }
    // A node on link `li` at `n`: its end within a metre along it, else a new node there, splitting the link in two
    int nodeOn(int li, const LineNear& n) {
        const HandLot::Link k = lot.links[(size_t)li];   // (a copy: adding a link moves them)
        if (n.s <= 1.f) return k.a;
        if (n.s >= planLength(k.pts) - 1.f) return k.b;
        const vec3 p = lerp(k.pts[n.seg], k.pts[n.seg + 1], n.t);
        std::vector<vec3> head(k.pts.begin(), k.pts.begin() + (std::ptrdiff_t)n.seg + 1), tail(1, p);
        if (distance(head.back().xy(), p.xy()) > 1e-3f) head.push_back(p);
        else head.back() = p;
        for (size_t i = n.seg + 1; i < k.pts.size(); i++)
            if (i > n.seg + 1 || distance(k.pts[i].xy(), p.xy()) > 1e-3f) tail.push_back(k.pts[i]);
        if (head.size() < 2 || tail.size() < 2) return n.s < planLength(k.pts) * 0.5f ? k.a : k.b;
        const int m = addNode(p.xy(), lot.nodes[(size_t)k.a].level, HandLot::LN_AISLE);
        lot.nodes[(size_t)m].p = p;
        lot.links[(size_t)li].b = m;
        lot.links[(size_t)li].pts = head;
        HandLot::Link rest = k;
        rest.a = m;
        rest.pts = tail;
        lot.links.push_back(rest);
        linkAisle.push_back(linkAisle[(size_t)li]);
        return m;
    }
    // The nearest point of the aisles on `level` to p, or -1; not on the links of node `skip`
    int nearestAisle(vec2 p, int level, int skip, LineNear* at) const {
        int best = -1;
        for (size_t i = 0; i < lot.links.size(); i++) {
            const HandLot::Link& k = lot.links[i];
            if (k.kind != HandLot::LL_AISLE || lot.nodes[(size_t)k.a].level != level || k.a == skip || k.b == skip) continue;
            const LineNear q = nearestOn(k.pts, p);
            if (q.d < at->d) {
                *at = q;
                best = (int)i;
            }
        }
        return best;
    }
    // Where a drive from p joins the lot on level 0: the nearest of its aisles' points or of its nodes; -1: nothing
    int joinFrom(vec2 p) {
        LineNear at;
        const int li = nearestAisle(p, 0, -1, &at);
        int best = -1;
        float bd = at.d;
        for (size_t i = 0; i < lot.nodes.size(); i++) {
            if (!open((int)i) || lot.nodes[i].level != 0) continue;
            const float d = distance(lot.nodes[i].p.xy(), p);
            if (d < bd) {
                bd = d;
                best = (int)i;
            }
        }
        if (best >= 0) return best;
        return li >= 0 ? nodeOn(li, at) : -1;
    }
    // Each node's part of the graph, its links taken both ways
    std::vector<int> parts() const {
        std::vector<int> part(lot.nodes.size(), -1);
        std::vector<std::vector<int>> next(lot.nodes.size());
        for (const HandLot::Link& k : lot.links) {
            next[(size_t)k.a].push_back(k.b);
            next[(size_t)k.b].push_back(k.a);
        }
        int count = 0;
        for (size_t i = 0; i < lot.nodes.size(); i++) {
            if (!alive((int)i) || part[i] >= 0) continue;
            std::vector<int> stack(1, (int)i);
            part[i] = count;
            while (!stack.empty()) {
                const int n = stack.back();
                stack.pop_back();
                for (int m : next[(size_t)n])
                    if (part[(size_t)m] < 0) {
                        part[(size_t)m] = count;
                        stack.push_back(m);
                    }
            }
            count++;
        }
        return part;
    }
    // The nodes reached from `from` along the links' ways (`back`: against them, so the nodes that reach it)
    std::vector<u8> reach(int from, bool back) const {
        std::vector<u8> seen(lot.nodes.size(), 0);
        std::vector<int> stack(1, from);
        seen[(size_t)from] = 1;
        while (!stack.empty()) {
            const int n = stack.back();
            stack.pop_back();
            for (const HandLot::Link& k : lot.links) {
                int m = -1;
                if ((back ? k.b : k.a) == n) m = back ? k.a : k.b;
                else if (!k.oneway && (back ? k.a : k.b) == n) m = back ? k.b : k.a;
                if (m >= 0 && !seen[(size_t)m]) {
                    seen[(size_t)m] = 1;
                    stack.push_back(m);
                }
            }
        }
        return seen;
    }
};


// ------------------------------------------------------------------------------------- the walk graph and spots

// h.mm-h.mm;...: each range's from and to, in minutes of the day (to before from runs past midnight); false where the
// text isn't that
bool parseHours(const std::string& text, std::vector<u16>& out) {
    out.clear();
    const auto minutes = [](const std::string& t, int* m) {
        const size_t dot = t.find('.');
        const std::string h = t.substr(0, dot), mm = dot == std::string::npos ? "00" : t.substr(dot + 1);
        if (h.empty() || h.size() > 2 || mm.size() != 2) return false;
        for (char c : h + mm)
            if (c < '0' || c > '9') return false;
        const int hv = std::stoi(h), mv = std::stoi(mm);
        *m = hv * 60 + mv;
        return mv < 60 && *m <= 24 * 60;
    };
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(';', start);
        const std::string range = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const size_t dash = range.find('-');
        int a = 0, b = 0;
        if (dash == std::string::npos || !minutes(range.substr(0, dash), &a) || !minutes(range.substr(dash + 1), &b) || a == b) return false;
        out.push_back((u16)a);
        out.push_back((u16)b);
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return !out.empty();
}

// min-max, ages 0 to 255
bool parseAges(const std::string& text, int* lo, int* hi) {
    const size_t dash = text.find('-');
    if (dash == std::string::npos || dash == 0 || dash + 1 >= text.size()) return false;
    for (size_t i = 0; i < text.size(); i++)
        if (i != dash && (text[i] < '0' || text[i] > '9')) return false;
    if (dash > 3 || text.size() - dash - 1 > 3) return false;
    *lo = std::stoi(text.substr(0, dash));
    *hi = std::stoi(text.substr(dash + 1));
    return *lo <= *hi && *hi <= 255;
}

// A profile's height at s (s:z pairs, s rising), straight between them
float profileZ(const std::vector<vec2>& pr, float s) {
    if (s <= pr.front().x) return pr.front().y;
    for (size_t k = 0; k + 1 < pr.size(); k++)
        if (s <= pr[k + 1].x) return Lerp(pr[k].y, pr[k + 1].y, pr[k + 1].x > pr[k].x ? (s - pr[k].x) / (pr[k + 1].x - pr[k].x) : 0.f);
    return pr.back().y;
}

// The level slabs under a junction's corner sidewalks. The road mesh draws each corner between two approaches
// (roadmesh.cpp: from the one's kerb at its cut-back round to the next one's, out by the narrower sidewalk, 0.15 m up),
// but the ground query (roadnet.cpp roadSurfaceHeight) takes neither approach's sidewalk inside the cut-backs: there it
// answered the junction's pavement, within its radius, and beyond that the terrain, graded 0.3 m under the road, so a
// walk round the corner sank 0.45 m into the sidewalk it was drawn on (and a car's wheel as much). A corner's slabs
// follow its drawn sidewalk 5 cm in from the kerb and the outer edge (their sides stay behind its curb face and skirt),
// in pieces along it (six on a curve, four on a straight one), each level 2 cm under the drawn top at its lower end,
// so nothing of them shows; the ground query takes them as it takes a pad (blocksbin.h padHeight). (The terrain itself
// can't rise there: its 8 m grid would lift the road's edge with it.)
void junctionCorners(const RoadNetwork& net, std::vector<GroundPoly>& out) {
    struct Approach {
        float ang;
        vec2 dir;
        vec3 cutPt;
        float hwL, hwR, swL, swR;
    };
    std::vector<Approach> ap;
    for (size_t ni = 0; ni < net.nodes.size(); ni++) {
        const RoadNode& nd = net.nodes[ni];
        if (nd.radius <= 0.f || nd.edges.size() < 3) continue;
        ap.clear();
        for (int ei : nd.edges) {   // (as roadmesh.cpp gathers them)
            const RoadEdge& e = net.edges[(size_t)ei];
            const bool out0 = e.n0 == (int)ni;
            const vec3 p = e.posAt(out0 ? e.cut0 : e.length - e.cut1);
            vec2 d = normalize(p.xy() - nd.p);
            if (length2(p.xy() - nd.p) < 1e-4f) d = normalize((out0 ? e.pts[1] : e.pts[e.pts.size() - 2]).xy() - nd.p);
            ap.push_back(Approach{atan2f(d.y, d.x), d, p, e.halfWidthSide(out0 ? -1 : 1), e.halfWidthSide(out0 ? 1 : -1), e.sidewalkSide(out0 ? -1 : 1),
                                  e.sidewalkSide(out0 ? 1 : -1)});
        }
        std::sort(ap.begin(), ap.end(), [](const Approach& x, const Approach& y) { return x.ang < y.ang; });
        for (size_t i = 0; i < ap.size(); i++) {
            const Approach& A = ap[i];
            const Approach& B = ap[(i + 1) % ap.size()];
            const float swc = Min(A.swL, B.swR);
            if (swc < 0.2f) continue;
            const vec2 lA = perp(A.dir), lB = perp(B.dir);
            const vec2 p1 = A.cutPt.xy() + lA * A.hwL, p2 = B.cutPt.xy() - lB * B.hwR, po1 = p1 + lA * swc, po2 = p2 - lB * swc;
            const float den = cross(A.dir, B.dir);
            vec2 C = (p1 + p2) * 0.5f, Co = (po1 + po2) * 0.5f;
            bool curved = false;
            if (fabsf(den) > 0.15f) {
                const vec2 X = p1 + A.dir * (cross(p2 - p1, B.dir) / den);
                if (length(X - nd.p) < nd.radius * 2.5f) {
                    C = X;
                    Co = po1 + A.dir * (cross(po2 - po1, B.dir) / den);
                    curved = true;
                }
            }
            const int pieces = curved ? 6 : 4;
            vec2 qi[7], qo[7];
            float qz[7];
            for (int k = 0; k <= pieces; k++) {
                const float t = (float)k / (float)pieces, a = (1 - t) * (1 - t), b = 2 * (1 - t) * t, c = t * t;
                const vec2 q = p1 * a + C * b + p2 * c, o = po1 * a + Co * b + po2 * c;
                const vec2 across = normalize(o - q);
                qi[k] = q + across * 0.05f;
                qo[k] = o - across * 0.05f;
                qz[k] = Lerp(A.cutPt.z, B.cutPt.z, t);
            }
            for (int k = 0; k < pieces; k++) {
                GroundPoly g;
                g.kind = GK_PAVING;
                g.flat = 1;
                g.z = Min(qz[k], qz[k + 1]) + 0.15f - 0.02f;
                g.poly = {qi[k], qi[k + 1], qo[k + 1], qo[k]};
                out.push_back(g);
            }
        }
    }
}

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
            if (p.graded) out.pads.push_back(BlockPad{p.poly, p.grade, Max(p.blend, 0.f), (u8)!p.ground.empty()});
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
                out.propDress.push_back((int)(&d - model.dressing.data()));
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
        out.propDress.push_back((int)(&d - model.dressing.data()));
    }
    return errors.size() == errorsBefore;
}

bool compileBlockSections(const Model& model, const RoadIndex& index, const RoadNetwork& net, BlocksOut& out, std::vector<Error>& errors) {
    using namespace worldmodel_blocks_detail;
    const size_t errorsBefore = errors.size();
    const auto fail = [&errors](const Src& s, const char* code, const std::string& msg) { errors.push_back(Error{s, code, msg}); };
    const auto levels = [](int n) { return std::to_string(n) + (n == 1 ? " level" : " levels"); };
    HandBlocks& hb = out.blocks;
    hb.parcels.clear();
    hb.lots.clear();
    // the road edge a reference names; -1 where it names nothing (worldv's to report) or an edge that didn't compile
    const auto roadEdge = [&](const Ref& r) {
        const int e = r.target >= 0 && (size_t)r.target < index.roadEdge.size() ? index.roadEdge[(size_t)r.target] : -1;
        return e >= 0 && (size_t)e < net.edges.size() ? e : -1;
    };
    // ---------------------------------------------------------------- the parcels
    for (const Block& b : model.blocks)
        for (int pi : b.parcels) {
            const Parcel& p = model.parcels[(size_t)pi];
            if (p.poly.size() < 3) continue;   // (compileBlocks reports it)
            HandParcel h;
            h.id = p.id;
            h.use = (u8)parcelUse(p.use);
            h.frontEdge = roadEdge(p.front);
            h.frontSide = p.frontSide == SIDE_LEFT ? 0 : 1;
            if (p.graded) h.grade = p.grade;
            h.poly = p.poly;
            hb.parcels.push_back(h);
        }
    // ---------------------------------------------------------------- the lots
    for (const Block& b : model.blocks)
        for (int lotIndex : b.lots) {
            const Lot& l = model.lots[(size_t)lotIndex];
            const std::string what = "lot " + l.id;
            bool ok = true;
            HandLot lot;
            lot.id = l.id;
            lot.levels = l.levels;
            if (l.levels < 1) {
                fail(l.src, "block.lot", what + ": levels=" + std::to_string(l.levels) + ": a lot has a level or more");
                ok = false;
            }
            // the curb cuts: the edge, its own s and the side; the node behind that side's sidewalk
            const auto cut = [&](const LotCut& c, const char* name, HandLot::Cut& to, vec2* at) {
                const int e = roadEdge(c.edge);
                if (e < 0) return false;
                const RoadEdge& re = net.edges[(size_t)e];
                const float s = edgeS(re, c.at);
                if (s < 0.f) {
                    fail(c.edge.src, "block.lot", what + " " + name + "=: " + fmt1(c.at) + " m is past the ends of " + c.edge.text + ", 0 to " + fmt1(planLength(re.pts)) +
                                                      " m in plan");
                    ok = false;
                    return false;
                }
                to.edge = e;
                to.s = s;
                to.side = c.side == SIDE_LEFT ? 0 : 1;
                const vec3 t = re.tangentAt(s);
                const int sd = to.side == 0 ? -1 : 1;
                *at = re.posAt(s).xy() + normalize(vec2(t.y, -t.x)) * (float)sd * (re.halfWidthSide(sd) + re.sidewalkSide(sd));
                return true;
            };
            vec2 entryAt(0.f), exitAt(0.f);
            const bool hasEntry = l.hasEntry && cut(l.entry, "entry", lot.entry, &entryAt);   // (one naming nothing: worldv's)
            const bool hasExit = l.hasExit && cut(l.exit, "exit", lot.exit, &exitAt);
            if (l.rows.empty()) {
                fail(l.src, "block.lot", what + ": no rows of bays");
                ok = false;
            }
            // each row's bays, spread evenly along its line, and the aisle behind them
            struct RowBays {
                std::vector<vec2> c, dir;
                int aisle = -1;
            };
            std::vector<RowBays> rows(l.rows.size());
            std::vector<Aisle> aisles;
            for (size_t ri = 0; ri < l.rows.size(); ri++) {
                const LotRow& r = l.rows[ri];
                const std::string rw = what + " row";
                const float len = planLength(r.line);
                if (r.line.size() < 2 || len < 1.f) {
                    fail(r.src, "block.shape", rw + ": its line needs 2 points or more, a metre apart or more");
                    ok = false;
                    continue;
                }
                if (r.bays < 1 || r.w <= 0.f || r.len <= 0.f) {
                    fail(r.src, "block.lot", rw + ": " + std::to_string(r.bays) + " bays of " + fmt2(r.w) + " by " + fmt2(r.len) + " m: a row needs a bay or more, of some size");
                    ok = false;
                    continue;
                }
                if (l.levels >= 1 && (r.level < 0 || r.level >= l.levels)) {
                    fail(r.src, "block.lot", rw + ": on level " + std::to_string(r.level) + ", and the lot has " + levels(l.levels));
                    ok = false;
                    continue;
                }
                if (!r.aisle.empty() && (r.aisle.size() < 2 || planLength(r.aisle) < 2.f)) {
                    fail(r.src, "block.shape", rw + ": its aisle= needs 2 points or more, 2 m apart or more");
                    ok = false;
                    continue;
                }
                // they face yaw=, else square to the line, to its left
                RowBays& rb = rows[ri];
                const vec2 facing(-sinf(r.yaw * kDegToRad), cosf(r.yaw * kDegToRad));
                float across = 0.f, along = 0.f;   // the facing, to the line's left and along it, on average
                for (int i = 0; i < r.bays; i++) {
                    vec2 t;
                    const vec2 c = pointAlong(r.line, ((float)i + 0.5f) * len / (float)r.bays, &t), n(-t.y, t.x), d = r.hasYaw ? facing : n;
                    rb.c.push_back(c);
                    rb.dir.push_back(d);
                    across += dot(d, n) / (float)r.bays;
                    along += dot(d, t) / (float)r.bays;
                }
                // the aisle behind them, half their depth square to the line plus half its width away; one way, 4 m
                // wide, where they are more than 15 degrees off square, the way that turns a car into them by less
                // than 90 degrees
                Aisle a;
                a.level = r.level;
                a.oneway = fabsf(along) > 0.2588f;
                a.width = a.oneway ? 4.f : 6.f;
                const float depth = r.len * fabsf(across) + r.w * fabsf(along);
                a.pts = r.aisle.empty() ? offsetLine(r.line, (across >= 0.f ? -0.5f : 0.5f) * (depth + a.width)) : r.aisle;
                if (a.oneway) {
                    std::vector<vec3> l3;
                    for (vec2 q : a.pts) l3.push_back(vec3(q, 0.f));
                    float into = 0.f;
                    for (size_t i = 0; i < rb.c.size(); i++) into += dot(directionAt(l3, nearestOn(l3, rb.c[i])), rb.dir[i]);
                    if (into < 0.f) std::reverse(a.pts.begin(), a.pts.end());
                }
                rb.aisle = (int)aisles.size();
                aisles.push_back(a);
            }
            for (const LotRamp& r : l.ramps) {
                if (r.line.size() < 2 || planLength(r.line) < 2.f) {
                    fail(r.src, "block.shape", what + " ramp: its line needs 2 points or more, 2 m apart or more");
                    ok = false;
                } else if (l.levels >= 1 && (r.from == r.to || r.from < 0 || r.to < 0 || r.from >= l.levels || r.to >= l.levels)) {
                    fail(r.src, "block.lot", what + " ramp: from level " + std::to_string(r.from) + " to " + std::to_string(r.to) + ", and the lot has " + levels(l.levels));
                    ok = false;
                }
            }
            if (!ok || !hasEntry) continue;
            // rows that face each other across one aisle share it
            for (size_t i = 0; i < aisles.size(); i++)
                for (size_t j = i + 1; aisles[i].alive && j < aisles.size(); j++) {
                    if (!aisles[j].alive || !mergeAisles(aisles[i], aisles[j])) continue;
                    aisles[j].alive = false;
                    for (RowBays& rb : rows)
                        if (rb.aisle == (int)j) rb.aisle = (int)i;
                }
            // the graph: the aisles, a node at each end (ends within 1 m one node); a loop, two links halfway round
            LotGraph g(lot);
            for (size_t i = 0; i < aisles.size(); i++) {
                const Aisle& a = aisles[i];
                if (!a.alive) continue;
                std::vector<vec3> pts;
                for (vec2 q : a.pts) pts.push_back(vec3(q, (float)a.level * kLotLevelHeight));
                const int na = g.nodeAt(a.pts.front(), a.level, HandLot::LN_AISLE), nb = g.nodeAt(a.pts.back(), a.level, HandLot::LN_AISLE);
                if (na != nb) {
                    g.addLink(na, nb, pts, a.width, a.oneway, HandLot::LL_AISLE, (int)i);
                    continue;
                }
                const int end = g.addNode(a.pts.back(), a.level, HandLot::LN_AISLE);
                const int k = g.addLink(na, end, pts, a.width, a.oneway, HandLot::LL_AISLE, (int)i);
                const int half = g.nodeOn(k, pointOn(lot.links[(size_t)k].pts, planLength(pts) * 0.5f));
                if (half != na && half != end) g.merge(end, na);
            }
            // the ramps: from their foot to their top, rising a level's height a level
            for (const LotRamp& r : l.ramps) {
                const float len = planLength(r.line);
                std::vector<vec3> pts;
                float s = 0.f;
                for (size_t k = 0; k < r.line.size(); k++) {
                    if (k > 0) s += distance(r.line[k - 1], r.line[k]);
                    pts.push_back(vec3(r.line[k], Lerp((float)r.from, (float)r.to, s / len) * kLotLevelHeight));
                }
                const int foot = g.nodeAt(r.line.front(), r.from, HandLot::LN_RAMP), top = g.nodeAt(r.line.back(), r.to, HandLot::LN_RAMP);
                g.addLink(foot, top, pts, 6.f, false, HandLot::LL_RAMP, -1);
            }
            // an aisle or ramp that stops at an aisle's side (within 1 m) joins it there
            for (size_t n = 0; n < lot.nodes.size(); n++) {
                if (!g.open((int)n) || g.degree((int)n) != 1) continue;
                LineNear at;
                const int li = g.nearestAisle(lot.nodes[n].p.xy(), lot.nodes[n].level, (int)n, &at);
                if (li < 0 || at.d > 1.f) continue;
                int other = -1;   // (the far end of its own link: joining that would fold the link onto itself)
                for (const HandLot::Link& k : lot.links)
                    if (k.a == (int)n || k.b == (int)n) other = k.a == (int)n ? k.b : k.a;
                const int m = g.nodeOn(li, at);
                if (m == other) continue;
                if (lot.nodes[n].kind == HandLot::LN_RAMP) lot.nodes[(size_t)m].kind = HandLot::LN_RAMP;
                g.merge((int)n, m);
            }
            // the entry and the exit: a drive from each cut to the nearest point of the lot's ground level, one way
            // in and one way out where both exist
            const int entry = g.addNode(entryAt, 0, HandLot::LN_ENTRY);
            const int in = g.joinFrom(entryAt);
            if (in < 0) {
                fail(l.src, "block.lot", what + ": nothing on level 0 for its entry to join");
                continue;
            }
            g.addLink(entry, in, {vec3(entryAt, 0.f), lot.nodes[(size_t)in].p}, hasExit ? 4.f : 6.f, hasExit, HandLot::LL_DRIVE, -1);
            int leave = -1;
            if (hasExit) {
                leave = g.addNode(exitAt, 0, HandLot::LN_EXIT);
                const int last = g.joinFrom(exitAt);
                g.addLink(last, leave, {lot.nodes[(size_t)last].p, vec3(exitAt, 0.f)}, 4.f, true, HandLot::LL_DRIVE, -1);
            }
            // every part the entry doesn't reach joins it by its nearest pair of nodes on one level
            bool joined = true;
            for (;;) {
                const std::vector<int> part = g.parts();
                int bu = -1, bv = -1, stray = -1;
                float bd = 1e30f;
                for (size_t v = 0; v < lot.nodes.size(); v++) {
                    if (part[v] < 0 || part[v] == part[(size_t)entry]) continue;
                    if (stray < 0) stray = (int)v;
                    for (size_t u = 0; g.open((int)v) && u < lot.nodes.size(); u++) {
                        if (part[u] != part[(size_t)entry] || !g.open((int)u) || lot.nodes[u].level != lot.nodes[v].level) continue;
                        const float d = distance(lot.nodes[u].p.xy(), lot.nodes[v].p.xy());
                        if (d < bd) {
                            bd = d;
                            bu = (int)u;
                            bv = (int)v;
                        }
                    }
                }
                if (stray < 0) break;
                if (bu < 0) {
                    fail(l.src, "block.lot", what + ": its level " + std::to_string(lot.nodes[(size_t)stray].level) + " can't be reached from its entry (no ramp joins it)");
                    joined = false;
                    break;
                }
                g.addLink(bu, bv, {lot.nodes[(size_t)bu].p, lot.nodes[(size_t)bv].p}, 6.f, false, HandLot::LL_DRIVE, -1);
            }
            if (!joined) continue;
            // and along the one-way links: a node that can't reach the way out joins the nearest node on its level
            // that can, and one the entry doesn't reach joins the nearest it does, until every node is on a way
            // through (a one-way aisle's far end, say)
            const int way = leave >= 0 ? leave : entry;
            bool through = false;
            for (size_t pass = 0; pass <= lot.nodes.size() * 2; pass++) {
                const std::vector<u8> reached = g.reach(entry, false), leaves = g.reach(way, true);
                int stuck = -1;
                bool trapped = false;
                for (size_t n = 0; stuck < 0 && n < lot.nodes.size(); n++)
                    if (g.open((int)n) && !leaves[n]) {
                        stuck = (int)n;
                        trapped = true;
                    }
                for (size_t n = 0; stuck < 0 && n < lot.nodes.size(); n++)
                    if (g.open((int)n) && !reached[n]) stuck = (int)n;
                if (stuck < 0) {
                    through = true;
                    for (size_t n = 0; n < lot.nodes.size(); n++) through = through && (!g.alive((int)n) || (reached[n] && leaves[n]));
                    break;
                }
                int best = -1;
                float bd = 1e30f;
                for (size_t m = 0; m < lot.nodes.size(); m++) {
                    if (!g.open((int)m) || lot.nodes[m].level != lot.nodes[(size_t)stuck].level || !(trapped ? leaves[m] : reached[m])) continue;
                    bool linked = false;   // (one way into it, say: a second link beside that would undo the one way)
                    for (const HandLot::Link& k : lot.links) linked = linked || (k.a == (int)m && k.b == stuck) || (k.b == (int)m && k.a == stuck);
                    const float d = distance(lot.nodes[m].p.xy(), lot.nodes[(size_t)stuck].p.xy());
                    if (!linked && d < bd) {
                        bd = d;
                        best = (int)m;
                    }
                }
                if (best < 0) break;
                g.addLink(stuck, best, {lot.nodes[(size_t)stuck].p, lot.nodes[(size_t)best].p}, 6.f, false, HandLot::LL_DRIVE, -1);
            }
            if (!through) {
                fail(l.src, "block.lot", what + ": its one-way aisles leave a part of it that can't be reached from its entry, or can't be left");
                continue;
            }
            // the rows and their bays, each opening onto its aisle where that passes nearest
            for (size_t ri = 0; ri < l.rows.size(); ri++) {
                const LotRow& r = l.rows[ri];
                const RowBays& rb = rows[ri];
                const float lift = (float)r.level * kLotLevelHeight;
                HandLot::Row row;
                row.a = vec3(r.line.front(), lift);
                row.b = vec3(r.line.back(), lift);
                row.bays = r.bays;
                row.w = r.w;
                row.len = r.len;
                row.dir = rb.dir[0];
                row.level = r.level;
                for (size_t i = 0; i < rb.c.size(); i++) {
                    HandLot::Bay bay;
                    bay.c = vec3(rb.c[i], lift);
                    bay.dir = rb.dir[i];
                    bay.row = (int)ri;
                    LineNear best;
                    for (size_t k = 0; k < lot.links.size(); k++) {
                        if (g.linkAisle[k] != rb.aisle) continue;
                        const LineNear q = nearestOn(lot.links[k].pts, rb.c[i]);
                        if (q.d < best.d) {
                            best = q;
                            bay.link = (int)k;
                        }
                    }
                    bay.s = best.s;
                    if (i == 0) row.aisle = bay.link;
                    lot.bays.push_back(bay);
                }
                lot.rows.push_back(row);
            }
            // the nodes merged into others go; the links name the rest by their new index
            std::vector<int> renumber(lot.nodes.size(), -1);
            std::vector<HandLot::Node> kept;
            for (size_t n = 0; n < lot.nodes.size(); n++)
                if (g.alive((int)n)) {
                    renumber[n] = (int)kept.size();
                    kept.push_back(lot.nodes[n]);
                }
            for (HandLot::Link& k : lot.links) {
                k.a = renumber[(size_t)k.a];
                k.b = renumber[(size_t)k.b];
            }
            lot.nodes = std::move(kept);
            hb.lots.push_back(std::move(lot));
        }
    // ---------------------------------------------------------------- the walk graph's inputs: paths, plazas, steps
    hb.paths.clear();
    hb.plazas.clear();
    hb.steps.clear();
    hb.spots.clear();
    out.pathOnTerrain.clear();
    for (const Block& b : model.blocks) {
        for (int pi : b.paths) {
            const Path& p = model.paths[(size_t)pi];
            const std::string what = "path " + p.id;
            const float len = planLength(p.line);
            if (p.line.size() < 2 || len < 0.5f || p.w <= 0.f) {
                fail(p.src, "block.shape", what + ": a path needs a line of 2 points or more and a width");
                continue;
            }
            HandPath h;
            h.id = p.id;
            h.kind = (u8)pathKind(p.kind);
            h.w = p.w;
            const bool raised = h.kind == WK_FOOTBRIDGE || h.kind == WK_UNDERPASS;
            if (raised && (p.profile.size() < 2 || fabsf(p.profile.front().x) > 0.5f || fabsf(p.profile.back().x - len) > 0.5f)) {
                fail(p.src, "block.path", what + ": a " + p.kind + " takes its heights from profile=, s from 0 to its length (" + fmt1(len) + " m)");
                continue;
            }
            // its points, and a footbridge's or underpass's at its profile's too, at the profile's heights
            float s = 0.f;
            for (size_t k = 0; k < p.line.size(); k++) {
                if (k > 0) {
                    const float seg = distance(p.line[k - 1], p.line[k]);
                    for (size_t j = 0; raised && j < p.profile.size(); j++)
                        if (p.profile[j].x > s + 0.01f && p.profile[j].x < s + seg - 0.01f)
                            h.pts.push_back(vec3(p.line[k - 1] + (p.line[k] - p.line[k - 1]) * ((p.profile[j].x - s) / seg), p.profile[j].y));
                    s += seg;
                }
                h.pts.push_back(vec3(p.line[k], raised ? profileZ(p.profile, s) : 0.f));
            }
            hb.paths.push_back(h);
            out.pathOnTerrain.push_back(raised ? 0 : 1);
        }
        for (int zi : b.plazas) {
            const Plaza& z = model.plazas[(size_t)zi];
            if (z.poly.size() < 3) continue;   // (compileBlocks reports it)
            HandPlaza h;
            h.id = z.id;
            h.poly = z.poly;
            h.tags = z.tags;
            hb.plazas.push_back(h);
        }
        for (int si : b.steps) {
            const Steps& t = model.steps[(size_t)si];
            if (t.line.size() < 2 || planLength(t.line) < 0.2f || t.w <= 0.f || t.treads < 0 || t.rise < 0.f) {
                fail(t.src, "block.shape", "steps: a flight needs a line of 2 points or more (from its bottom), a width, and treads and rise of 0 or more");
                continue;
            }
            HandSteps h;
            h.bottom = vec3(t.line.front(), 0.f);
            h.top = vec3(t.line.back(), 0.f);
            h.w = t.w;
            h.treads = t.treads;   // (0: derived from the ground, placeOnTerrain)
            h.rise = t.rise;
            hb.steps.push_back(h);
        }
    }
    // ---------------------------------------------------------------- the spots: as written, then the props' seats and
    // the bus stops' waits
    // the props of a piece, by the dressing record each came from
    const auto pieceOf = [&](size_t prop) -> const LibItem* {
        return prop < out.propDress.size() && out.propDress[prop] >= 0 && (size_t)out.propDress[prop] < model.dressing.size()
                   ? piece(model, model.dressing[(size_t)out.propDress[prop]].kind)
                   : nullptr;
    };
    for (const Block& b : model.blocks)
        for (int si : b.spots) {
            const Spot& p = model.spots[(size_t)si];
            const std::string what = "spot" + (p.id.empty() ? std::string() : " " + p.id);
            bool known = false;
            HandSpot h;
            h.kind = (u8)spotKind(p.kind, &known);
            h.id = p.id;
            if (!known) continue;   // (the schema's to report)
            const bool queue = !p.line.empty();
            if (p.hasAt == queue || (queue && p.line.size() < 2)) {
                fail(p.src, "block.spot", what + ": a spot has an at= or a line= (a queue, 2 points or more), not both");
                continue;
            }
            for (vec2 q : p.line) h.line.push_back(vec3(q, 0.f));
            h.at = vec3(p.hasAt ? p.at : p.line.front(), 0.f);
            if (p.hasYaw) h.dir = vec2(-sinf(p.yaw * kDegToRad), cosf(p.yaw * kDegToRad));
            else if (h.line.size() >= 2) h.dir = normalize(p.line[0] - p.line[1]);   // (a queue faces its head)
            h.cap = p.cap > 0 ? p.cap : h.line.size() >= 2 ? 1 + (int)(planLength(p.line) / 0.75f) : 1;
            if (p.cover) h.flags |= HandSpot::COVER;
            if (!p.hours.empty() && !parseHours(p.hours, h.hours)) {
                fail(p.src, "block.spot", what + " hours=" + p.hours + ": not h.mm-h.mm, ranges joined by ;");
                continue;
            }
            h.roles = p.roles;
            int lo = 0, hi = 255;
            if (!p.ages.empty() && !parseAges(p.ages, &lo, &hi)) {
                fail(p.src, "block.spot", what + " ages=" + p.ages + ": not min-max, 0 to 255");
                continue;
            }
            h.ageMin = (u8)lo;
            h.ageMax = (u8)hi;
            // its prop: the nearest prop of the piece it names, within 2 m
            if (p.prop.target >= 0) {
                float bd = 2.f;
                for (size_t k = 0; k < hb.props.size(); k++) {
                    const LibItem* it = pieceOf(k);
                    const float d = distance(hb.props[k].pos.xy(), h.at.xy());
                    if (it && it == &model.library[(size_t)p.prop.target] && d <= bd) {
                        bd = d;
                        h.prop = (int)k;
                    }
                }
                if (h.prop < 0) {
                    fail(p.src, "block.spot", what + " prop=" + p.prop.text + ": no prop of it within 2 m");
                    continue;
                }
            }
            h.pose = p.pose;
            h.target = p.target;
            hb.spots.push_back(h);
        }
    // the bus stops on compiled edges, where their waits stand: on their side's sidewalk at their at=, else their wait=
    struct Stop {
        std::string id;
        vec2 at, kerb;                     // the wait, and the way to the kerb from it
        int seat = -1;                     // the shelter whose bench it takes
    };
    std::vector<Stop> stops;
    for (const BusStop& s : model.busStops) {
        const int e = s.edge >= 0 && (size_t)s.edge < index.roadEdge.size() ? index.roadEdge[(size_t)s.edge] : -1;
        if (e < 0 || (size_t)e >= net.edges.size()) continue;
        const RoadEdge& re = net.edges[(size_t)e];
        const float rs = edgeS(re, s.at);
        if (rs < 0.f) continue;   // (validate_roads reports a stop past its edge's ends)
        const int sd = s.side == SIDE_LEFT ? -1 : 1;
        const vec3 t = re.tangentAt(rs);
        const vec2 n = normalize(vec2(t.y, -t.x)) * (float)sd;   // out from the street on its side
        Stop w;
        w.id = s.id;
        w.kerb = -n;
        w.at = re.posAt(rs).xy() + n * (re.halfWidthSide(sd) + re.sidewalkSide(sd) * 0.5f);
        if (!s.wait.empty()) {
            vec2 c(0.f);
            for (vec2 q : s.wait) c += q;
            w.at = c * (1.f / (float)s.wait.size());
        }
        stops.push_back(w);
    }
    // the props' seats: a bench's, and a bus shelter's (the nearest stop's within 15 m: its wait, under cover)
    for (size_t k = 0; k < hb.props.size(); k++) {
        const PropInstance& pr = hb.props[k];
        const SeatSpec* seat = seatSpec(pr.type);
        const LibItem* it = pieceOf(k);
        if (!seat || !it) continue;
        const Dress& d = model.dressing[(size_t)out.propDress[k]];
        const float yaw = d.yaw * kDegToRad;   // (the piece's frame: +y forward, the way the sitter faces)
        const vec2 fwd(-sinf(yaw), cosf(yaw)), right(cosf(yaw), sinf(yaw));
        HandSpot h;
        h.kind = SPOT_BENCH;
        h.flags = HandSpot::IMPLIED;
        h.at = vec3(pr.pos.xy() + (right * seat->at.x + fwd * seat->at.y) * pr.scale, seat->at.z * pr.scale);   // (above the prop's base)
        h.dir = fwd;
        h.cap = Max(1, (int)(seat->width * pr.scale / kSeatPlace + 0.01f));
        h.prop = (int)k;
        if (pr.type == PROP_BUS_STOP) {
            h.flags |= HandSpot::COVER;
            int best = -1;
            float bd = 15.f;
            for (size_t i = 0; i < stops.size(); i++) {
                const float dd = distance(stops[i].at, pr.pos.xy());
                if (stops[i].seat < 0 && dd <= bd) {
                    bd = dd;
                    best = (int)i;
                }
            }
            if (best >= 0) {
                h.kind = SPOT_BUSSTOP_WAIT;
                h.target = stops[(size_t)best].id;
                stops[(size_t)best].seat = (int)k;
            }
        }
        hb.spots.push_back(h);
    }
    // and each stop without a shelter: a wait standing by the kerb
    for (const Stop& w : stops) {
        if (w.seat >= 0) continue;
        HandSpot h;
        h.kind = SPOT_BUSSTOP_WAIT;
        h.flags = HandSpot::IMPLIED;
        h.at = vec3(w.at, 0.f);
        h.dir = w.kerb;
        h.cap = 6;
        h.target = w.id;
        hb.spots.push_back(h);
    }
    // ---------------------------------------------------------------- the colliders: each prop's, as its piece gives it
    // (collide= box or cylinder, at its body= or else its size=, times the prop's scale), from the prop's base up
    hb.colliders.clear();
    for (size_t k = 0; k < hb.props.size(); k++) {
        const LibItem* it = pieceOf(k);
        if (!it || (it->collide != "box" && it->collide != "cylinder")) continue;
        const PropInstance& pr = hb.props[k];
        const vec3 dim = (it->body.x > 0.f ? it->body : it->size) * pr.scale;
        if (!(dim.x > 0.f && dim.y > 0.f && dim.z > 0.f)) continue;   // (a piece without a size: none to give)
        HandCollider c;
        c.kind = it->collide == "box" ? HandCollider::BOX : HandCollider::CYLINDER;
        c.prop = (int)k;
        c.c = pr.pos.xy();
        c.z0 = 0.f;   // (above the prop's base until placeOnTerrain)
        c.z1 = dim.z;
        const float yaw = pr.yaw - propYawOffset(pr.type);   // (the piece's frame: its width along its +x)
        c.he = c.kind == HandCollider::BOX ? vec2(dim.x, dim.y) * 0.5f : vec2(Max(dim.x, dim.y) * 0.5f);
        c.ax = c.kind == HandCollider::BOX ? vec2(cosf(yaw), sinf(yaw)) : vec2(1.f, 0.f);
        hb.colliders.push_back(c);
    }
    return errors.size() == errorsBefore;
}

bool placeOnTerrain(const BakedTerrain& baked, const RoadNetwork& roads, const std::vector<std::vector<vec2>>& decks, BlocksOut& blocks,
                    WorldBinWriter& out, std::string* error, std::string* report) {
    HandBlocks& hb = blocks.blocks;
    if (report) report->clear();
    // (a parcel that only grades the terrain, with no ground or dressing, is still something to do: its pad)
    if (roads.edges.empty() && hb.ground.empty() && hb.props.empty() && hb.lamps.empty() && blocks.pads.empty() && hb.parcels.empty() && hb.lots.empty() &&
        hb.paths.empty() && hb.plazas.empty() && hb.steps.empty() && hb.spots.empty() && hb.colliders.empty())
        return true;
    const auto fail = [error](const char* why) {
        if (error) *error = why;
        return false;
    };
    if (!baked.ok || baked.res != kHeightRes || baked.cell != kHeightCell || baked.originX != -kWorldHalf || baked.originY != -kWorldHalf ||
        baked.height.size() != (size_t)kHeightRes * kHeightRes || baked.water.size() != baked.height.size())
        return fail("the roads and blocks can't be graded into the terrain: its grid isn't the game's");
    const size_t n = baked.height.size();
    WorldMap map;
    map.height.resize(n);
    map.waterLevel.resize(n);
    for (size_t i = 0; i < n; i++) {
        map.height[i] = (float)baked.height[i] * baked.heightUnit;
        const int c = baked.water[i];
        map.waterLevel[i] = c <= 0 || c > (int)baked.levels.size() ? kNoWater : baked.levels[(size_t)c - 1];
    }
    // A levelled parcel's ground covers the terrain under it, which goes 0.3 m lower, as under the roads: the terrain's
    // 8 m grid would otherwise rise through the ground of a parcel levelled a little under its neighbour, between them
    for (const BlockPad& p : blocks.pads) map.flattenPoly(p.poly, p.covered ? p.z - 0.3f : p.z, p.blend);
    gradeTerrainForRoads(roads, map, &decks);
    // the slabs under the junctions' corner sidewalks (level, z absolute)
    {
        std::vector<GroundPoly> corners;
        worldmodel_blocks_detail::junctionCorners(roads, corners);
        for (GroundPoly& g : corners) {
            hb.ground.push_back(std::move(g));
            blocks.groundOnTerrain.push_back(0);
        }
    }
    std::vector<i16> delta(n, 0);
    size_t changed = 0;
    float largest = 0.f;
    for (size_t i = 0; i < n; i++) {
        const int bakedZ = baked.height[i];
        const int graded = Clamp((int)floorf(map.height[i] / baked.heightUnit + 0.5f), -32768, 32767);
        delta[i] = (i16)Clamp(graded - bakedZ, -32768, 32767);
        if (delta[i] != 0) changed++;
        largest = Max(largest, fabsf((float)delta[i] * baked.heightUnit));
    }
    std::vector<u8> bytes;
    if (!encodeGrid(delta.data(), kHeightRes, kHeightRes, GRID_I16, bytes)) return fail("the graded terrain doesn't encode");
    out.add("terrain.grade", WSK_GRID, bytes);
    if (report) {
        char line[200];
        snprintf(line, sizeof(line), "worldc: terrain graded under the roads%s: %zu texels changed, by up to %.2f m (%zu KB)", blocks.pads.empty() ? "" : " and parcels",
                 changed, largest, bytes.size() / 1024);
        *report = line;
    }
    // the blocks on the graded ground: the graded heights as the game will have them (to the height layer's unit)
    for (size_t i = 0; i < n; i++) map.height[i] = (float)(baked.height[i] + delta[i]) * baked.heightUnit;
    // (on a levelled parcel, its grade: the terrain lies lower under it, and under its front the roads' grading cut it)
    const auto groundAt = [&](vec2 p) {
        const float g = map.heightAt(p.x, p.y);
        float s = 0.f;
        float z = !roads.edges.empty() && roads.roadSurfaceHeight(p, &s, g + 3.f) && s > g ? s : g;
        for (const HandParcel& q : hb.parcels)
            if (!std::isnan(q.grade) && q.grade > z && pointInPolygon2D(p, q.poly.data(), (int)q.poly.size())) {
                z = q.grade;
                break;
            }
        return z;
    };
    for (PropInstance& p : hb.props) p.pos.z += groundAt(p.pos.xy());
    for (size_t i = 0; i < hb.lamps.size() && i < blocks.lampGroundAt.size(); i++) hb.lamps[i].pos.z += groundAt(blocks.lampGroundAt[i]);
    for (size_t i = 0; i < hb.ground.size() && i < blocks.groundOnTerrain.size(); i++) {
        if (!blocks.groundOnTerrain[i]) continue;
        vec2 c(0.f);
        for (const vec2& q : hb.ground[i].poly) c += q;
        c = c * (1.f / (float)hb.ground[i].poly.size());
        hb.ground[i].z += map.heightAt(c.x, c.y);
    }
    // the lots' points, their levels above the ground (compileBlockSections)
    for (HandLot& l : hb.lots) {
        for (HandLot::Row& r : l.rows) {
            r.a.z += groundAt(r.a.xy());
            r.b.z += groundAt(r.b.xy());
        }
        for (HandLot::Node& n : l.nodes) n.p.z += groundAt(n.p.xy());
        for (HandLot::Link& k : l.links)
            for (vec3& q : k.pts) q.z += groundAt(q.xy());
        for (HandLot::Bay& b : l.bays) b.c.z += groundAt(b.c.xy());
    }
    // the paths on the ground (a footbridge's or underpass's as its profile has them); a flight of steps from the level
    // ground at its foot (a pad's or levelled parcel's, else the ground) to that at its top, its treads and rise from
    // their difference where they aren't written (a rise near 0.17 m); a spot on its prop's base, else on the ground
    for (size_t i = 0; i < hb.paths.size(); i++)
        if (i < blocks.pathOnTerrain.size() && blocks.pathOnTerrain[i])
            for (vec3& q : hb.paths[i].pts) q.z += groundAt(q.xy());
    const auto levelAt = [&](vec2 p) {
        float z = groundAt(p);
        for (const GroundPoly& g : hb.ground)
            if (g.flat && g.z > z && pointInPolygon2D(p, g.poly.data(), (int)g.poly.size())) z = g.z;
        return z;
    };
    for (HandSteps& t : hb.steps) {
        t.bottom.z = levelAt(t.bottom.xy());
        t.top.z = levelAt(t.top.xy());
        const float dz = t.top.z - t.bottom.z;
        if (t.treads > 0 && t.rise > 0.f) {
            t.top.z = t.bottom.z + (float)t.treads * t.rise;   // (as written)
        } else if (t.treads > 0) {
            t.rise = Max(dz, 0.f) / (float)t.treads;
        } else {
            t.treads = dz > 0.05f ? Max(1, (int)floorf(dz / (t.rise > 0.f ? t.rise : 0.17f) + 0.5f)) : 0;
            t.rise = t.treads > 0 ? dz / (float)t.treads : 0.f;
        }
    }
    for (HandSpot& s : hb.spots) {
        s.at.z += s.prop >= 0 && (size_t)s.prop < hb.props.size() ? hb.props[(size_t)s.prop].pos.z : groundAt(s.at.xy());
        for (vec3& q : s.line) q.z += groundAt(q.xy());
    }
    for (HandCollider& c : hb.colliders) {
        const float base = c.prop >= 0 && (size_t)c.prop < hb.props.size() ? hb.props[(size_t)c.prop].pos.z : groundAt(c.c);
        c.z0 += base;
        c.z1 += base;
    }
    writeBlocks(hb, out);
    return true;
}

}  // namespace Hand
}  // namespace World
