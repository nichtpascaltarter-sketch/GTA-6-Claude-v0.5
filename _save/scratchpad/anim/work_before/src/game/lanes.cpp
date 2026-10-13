// Lane graph built once from the road network: lanes with right-hand-traffic offsets matching the painted markings,
// Bezier connectors through intersections with legal lane assignment, conflict sets and yield rules, signal plans with
// protected left arrows, bus stops, and the pedestrian sidewalk graph (sidewalks, corners, crosswalks).
#include "ai_core.h"
#include "../world/buildings.h"

namespace AI {

float wrapPi(float a) {
    // remainder instead of repeated subtraction: a huge or infinite angle must not spin forever
    if (a >= -kPi && a <= kPi) return a;
    a = remainderf(a, kTwoPi);
    return a == a ? a : 0.f;
}

namespace lanes_detail {

float classValue(u8 cls) {
    switch (cls) {
        case World::RC_HIGHWAY: return 6.f;
        case World::RC_BOULEVARD: return 5.f;
        case World::RC_AVENUE: return 4.f;
        case World::RC_RURAL: return 3.5f;
        case World::RC_STREET: return 3.f;
        case World::RC_LANE: return 2.f;
        case World::RC_RAMP: return 1.5f;
        default: return 1.f;
    }
}

// Cubic Bezier sampling into a connector polyline
void sampleBezier(Connector& c, vec3 p0, vec3 p1, vec3 p2, vec3 p3) {
    float approx = length(p1 - p0) + length(p2 - p1) + length(p3 - p2);
    int n = Clamp((int)(approx / 1.0f), 6, 48);
    c.pts.clear();
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n, u = 1.f - t;
        vec3 p = p0 * (u * u * u) + p1 * (3.f * u * u * t) + p2 * (3.f * u * t * t) + p3 * (t * t * t);
        c.pts.push_back(p);
    }
}

void finishPath(Connector& c) {
    size_t n = c.pts.size();
    c.s.assign(n, 0.f);
    c.curv.assign(n, 0.f);
    for (size_t i = 1; i < n; i++) c.s[i] = c.s[i - 1] + length(c.pts[i].xy() - c.pts[i - 1].xy());
    c.length = n ? c.s.back() : 0.f;
    float maxK = 0.f;
    for (size_t i = 1; i + 1 < n; i++) {
        vec2 a = c.pts[i].xy() - c.pts[i - 1].xy(), b = c.pts[i + 1].xy() - c.pts[i].xy();
        float la = length(a), lb = length(b);
        if (la < 1e-4f || lb < 1e-4f) continue;
        float ang = atan2f(cross(a, b), dot(a, b));   // signed: + = turning left (same convention as laneCurv)
        float k = ang / (0.5f * (la + lb));
        c.curv[i] = k;
        maxK = Max(maxK, fabsf(k));
    }
    if (n >= 3) {
        c.curv[0] = c.curv[1];
        c.curv[n - 1] = c.curv[n - 2];
    }
    c.maxSpeed = maxK > 1e-4f ? Clamp(sqrtf(3.0f / maxK), 3.f, 40.f) : 40.f;
    c.minRadius = maxK > 1e-4f ? 1.f / maxK : 1e9f;
}

// conflict zone of two sampled paths: the ranges of each path that come within `r` of the other (false if never)
bool closeZone(const Connector& a, const Connector& b, float r, float& sa0, float& sa1, float& sb0, float& sb1) {
    vec2 amn(1e9f), amx(-1e9f), bmn(1e9f), bmx(-1e9f);
    for (auto& p : a.pts) { amn = vmin(amn, p.xy()); amx = vmax(amx, p.xy()); }
    for (auto& p : b.pts) { bmn = vmin(bmn, p.xy()); bmx = vmax(bmx, p.xy()); }
    if (amn.x > bmx.x + r || bmn.x > amx.x + r || amn.y > bmx.y + r || bmn.y > amx.y + r) return false;
    float r2 = r * r;
    bool any = false;
    sa0 = sb0 = 1e30f;
    sa1 = sb1 = -1e30f;
    for (size_t i = 0; i < a.pts.size(); i++) {
        vec2 p = a.pts[i].xy();
        if (p.x < bmn.x - r || p.x > bmx.x + r || p.y < bmn.y - r || p.y > bmx.y + r) continue;
        for (size_t j = 0; j < b.pts.size(); j++) {
            vec2 d = b.pts[j].xy() - p;
            if (d.x * d.x + d.y * d.y < r2) {
                any = true;
                sa0 = Min(sa0, a.s[i]);
                sa1 = Max(sa1, a.s[i]);
                sb0 = Min(sb0, b.s[j]);
                sb1 = Max(sb1, b.s[j]);
            }
        }
    }
    return any;
}

vec2 pathAt(const Connector& c, float u) {
    if (u <= 0.f) {
        vec2 t = normalize(c.pts[1].xy() - c.pts[0].xy());
        return c.pts[0].xy() + t * u;   // extends straight behind the entry
    }
    size_t n = c.pts.size();
    u = Min(u, c.length);
    size_t i = std::upper_bound(c.s.begin(), c.s.end(), u) - c.s.begin();
    if (i == 0) i = 1;
    if (i >= n) i = n - 1;
    float seg = c.s[i] - c.s[i - 1];
    float t = seg > 1e-5f ? (u - c.s[i - 1]) / seg : 0.f;
    return lerp(c.pts[i - 1].xy(), c.pts[i].xy(), t);
}

vec2 tangentAt(const Connector& c, float u) {
    size_t n = c.pts.size();
    u = Clamp(u, 0.f, c.length);
    size_t i = std::upper_bound(c.s.begin(), c.s.end(), u) - c.s.begin();
    if (i == 0) i = 1;
    if (i >= n) i = n - 1;
    return normalize(c.pts[i].xy() - c.pts[i - 1].xy());
}

}  // namespace lanes_detail

using namespace lanes_detail;

// ---------------------------------------------------------------------------------------------------------------------
// Geometry queries
float LaneGraph::pathLength(int path) const {
    if (isLane(path)) {
        const Lane& l = lanes[path];
        return l.u1;
    }
    return conn(path).length;
}

float LaneGraph::pathSpeed(int path) const {
    if (isLane(path)) return lanes[path].speed;
    const Connector& c = conn(path);
    return Min(c.maxSpeed, Max(lanes[c.from].speed, lanes[c.to].speed));
}

vec3 LaneGraph::lanePos(int lane, float u, float lateral) const {
    const Lane& l = lanes[lane];
    const World::RoadEdge& e = roads->edges[l.edge];
    float s = l.dir > 0 ? u : e.length - u;
    s = Clamp(s, 0.f, e.length);
    const std::vector<vec2>& vn = edgeNormal[l.edge];
    size_t n = e.pts.size();
    size_t i = std::upper_bound(e.dist.begin(), e.dist.end(), s) - e.dist.begin();
    if (i == 0) i = 1;
    if (i >= n) i = n - 1;
    float seg = e.dist[i] - e.dist[i - 1];
    float t = seg > 1e-5f ? (s - e.dist[i - 1]) / seg : 0.f;
    vec3 c = lerp(e.pts[i - 1], e.pts[i], t);
    vec2 nrm = lerp(vn[i - 1], vn[i], t);
    float off = l.offset + lateral * (float)l.dir;
    return c + vec3(nrm * off, 0.f);
}

vec2 LaneGraph::laneTangent(int lane, float u) const {
    const Lane& l = lanes[lane];
    const World::RoadEdge& e = roads->edges[l.edge];
    float s = l.dir > 0 ? u : e.length - u;
    s = Clamp(s, 0.f, e.length);
    size_t n = e.pts.size();
    size_t i = std::upper_bound(e.dist.begin(), e.dist.end(), s) - e.dist.begin();
    if (i == 0) i = 1;
    if (i >= n) i = n - 1;
    // blend segment directions near vertices for a smooth heading
    vec2 d = normalize(e.pts[i].xy() - e.pts[i - 1].xy());
    float seg = e.dist[i] - e.dist[i - 1];
    float t = seg > 1e-5f ? (s - e.dist[i - 1]) / seg : 0.f;
    if (t > 0.7f && i + 1 < n) d = normalize(lerp(d, normalize(e.pts[i + 1].xy() - e.pts[i].xy()), (t - 0.7f) / 0.6f));
    else if (t < 0.3f && i >= 2) d = normalize(lerp(d, normalize(e.pts[i - 1].xy() - e.pts[i - 2].xy()), (0.3f - t) / 0.6f));
    return l.dir > 0 ? d : -d;
}

float LaneGraph::laneCurv(int lane, float u) const {
    const Lane& l = lanes[lane];
    const World::RoadEdge& e = roads->edges[l.edge];
    float s = l.dir > 0 ? u : e.length - u;
    s = Clamp(s, 0.f, e.length);
    size_t n = e.pts.size();
    size_t i = std::upper_bound(e.dist.begin(), e.dist.end(), s) - e.dist.begin();
    if (i == 0) i = 1;
    if (i >= n) i = n - 1;
    const std::vector<float>& k = edgeCurv[l.edge];
    float seg = e.dist[i] - e.dist[i - 1];
    float t = seg > 1e-5f ? (s - e.dist[i - 1]) / seg : 0.f;
    float kk = Lerp(k[i - 1], k[i], t);
    return l.dir > 0 ? kk : -kk;
}

vec3 LaneGraph::pathPos(int path, float u, float lateral) const {
    if (isLane(path)) return lanePos(path, u, lateral);
    const Connector& c = conn(path);
    size_t n = c.pts.size();
    u = Clamp(u, 0.f, c.length);
    size_t i = std::upper_bound(c.s.begin(), c.s.end(), u) - c.s.begin();
    if (i == 0) i = 1;
    if (i >= n) i = n - 1;
    float seg = c.s[i] - c.s[i - 1];
    float t = seg > 1e-5f ? (u - c.s[i - 1]) / seg : 0.f;
    vec3 p = lerp(c.pts[i - 1], c.pts[i], t);
    if (lateral != 0.f) {
        vec2 d = normalize(c.pts[i].xy() - c.pts[i - 1].xy());
        p += vec3(rightOf(d) * lateral, 0.f);
    }
    return p;
}

vec2 LaneGraph::pathTangent(int path, float u) const {
    if (isLane(path)) return laneTangent(path, u);
    const Connector& c = conn(path);
    size_t n = c.pts.size();
    if (n < 2) return vec2(0.f, 1.f);
    u = Clamp(u, 0.f, c.length);
    size_t i = std::upper_bound(c.s.begin(), c.s.end(), u) - c.s.begin();
    if (i == 0) i = 1;
    if (i >= n) i = n - 1;
    // vertex tangents (central differences) interpolated along the segment: a continuous heading for the controllers
    auto vt = [&](size_t k) {
        size_t a = k > 0 ? k - 1 : 0, b = k + 1 < n ? k + 1 : n - 1;
        vec2 d = c.pts[b].xy() - c.pts[a].xy();
        float l = length(d);
        return l > 1e-5f ? d / l : vec2(0.f, 1.f);
    };
    float seg = c.s[i] - c.s[i - 1];
    float t = seg > 1e-5f ? Saturate((u - c.s[i - 1]) / seg) : 0.f;
    vec2 d = lerp(vt(i - 1), vt(i), t);
    float l = length(d);
    return l > 1e-5f ? d / l : normalize(c.pts[i].xy() - c.pts[i - 1].xy());
}

float LaneGraph::pathCurv(int path, float u) const {
    if (isLane(path)) return laneCurv(path, u);
    const Connector& c = conn(path);
    size_t n = c.pts.size();
    u = Clamp(u, 0.f, c.length);
    size_t i = std::upper_bound(c.s.begin(), c.s.end(), u) - c.s.begin();
    if (i >= n) i = n - 1;
    return c.curv[i];
}

float LaneGraph::projectPath(int path, vec2 p, float uHint, float* lateral) const {
    if (isLane(path)) {
        const Lane& l = lanes[path];
        const World::RoadEdge& e = roads->edges[l.edge];
        float sHint = l.dir > 0 ? uHint : e.length - uHint;
        size_t n = e.pts.size();
        size_t hi = std::upper_bound(e.dist.begin(), e.dist.end(), Clamp(sHint, 0.f, e.length)) - e.dist.begin();
        int k0 = Max(0, (int)hi - 4), k1 = Min((int)n - 2, (int)hi + 3);
        float bestD = 1e30f, bestS = sHint, bestLat = 0.f;
        const std::vector<vec2>& vn = edgeNormal[l.edge];
        for (int k = k0; k <= k1; k++) {
            // project onto the offset segment (lane centerline piece)
            vec2 a = e.pts[k].xy() + vn[k] * l.offset, b = e.pts[k + 1].xy() + vn[k + 1] * l.offset;
            float t;
            float d = distPointSegment2D(p, a, b, &t);
            if (d < bestD) {
                bestD = d;
                bestS = e.dist[k] + t * (e.dist[k + 1] - e.dist[k]);
                bestLat = cross(b - a, p - a) >= 0.f ? -d : d;  // right of n0->n1 is positive
            }
        }
        if (lateral) *lateral = bestLat * (float)l.dir;
        return l.dir > 0 ? bestS : e.length - bestS;
    }
    const Connector& c = conn(path);
    size_t n = c.pts.size();
    float bestD = 1e30f, bestU = uHint, bestLat = 0.f;
    for (size_t i = 0; i + 1 < n; i++) {
        vec2 a = c.pts[i].xy(), b = c.pts[i + 1].xy();
        float t;
        float d = distPointSegment2D(p, a, b, &t);
        if (d < bestD) {
            bestD = d;
            bestU = c.s[i] + t * (c.s[i + 1] - c.s[i]);
            bestLat = cross(b - a, p - a) >= 0.f ? -d : d;
        }
    }
    if (lateral) *lateral = bestLat;
    return bestU;
}

int LaneGraph::laneOf(int edge, int dir, int index) const {
    int grp = edge * 2 + (dir < 0 ? 1 : 0);
    if (grp < 0 || grp >= (int)groupFirst.size() || groupFirst[grp] < 0) return -1;
    if (index < 0 || index >= groupCount[grp]) return -1;
    return groupFirst[grp] + index;
}

void LaneGraph::lanesNear(vec2 mn, vec2 mx, std::vector<int>& out) const {
    out.clear();
    const float C = World::RoadNetwork::kHashCell;
    int x0 = Clamp((int)((mn.x + World::kWorldHalf) / C), 0, hashRes - 1), x1 = Clamp((int)((mx.x + World::kWorldHalf) / C), 0, hashRes - 1);
    int y0 = Clamp((int)((mn.y + World::kWorldHalf) / C), 0, hashRes - 1), y1 = Clamp((int)((mx.y + World::kWorldHalf) / C), 0, hashRes - 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            for (int l : laneHash[(size_t)y * hashRes + x]) out.push_back(l);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

void LaneGraph::spotsNear(vec2 p, float r, std::vector<int>& out) const {
    out.clear();
    const float C = World::RoadNetwork::kHashCell;
    int x0 = Clamp((int)((p.x - r + World::kWorldHalf) / C), 0, hashRes - 1), x1 = Clamp((int)((p.x + r + World::kWorldHalf) / C), 0, hashRes - 1);
    int y0 = Clamp((int)((p.y - r + World::kWorldHalf) / C), 0, hashRes - 1), y1 = Clamp((int)((p.y + r + World::kWorldHalf) / C), 0, hashRes - 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            for (int si : spotHash[(size_t)y * hashRes + x])
                if (length2(spots[si].pos.xy() - p) <= r * r) out.push_back(si);
}

void LaneGraph::walksNear(vec2 mn, vec2 mx, std::vector<int>& out) const {
    out.clear();
    const float C = World::RoadNetwork::kHashCell;
    int x0 = Clamp((int)((mn.x + World::kWorldHalf) / C), 0, hashRes - 1), x1 = Clamp((int)((mx.x + World::kWorldHalf) / C), 0, hashRes - 1);
    int y0 = Clamp((int)((mn.y + World::kWorldHalf) / C), 0, hashRes - 1), y1 = Clamp((int)((mx.y + World::kWorldHalf) / C), 0, hashRes - 1);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            for (int l : walkHash[(size_t)y * hashRes + x]) out.push_back(l);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

int LaneGraph::nearestLane(vec2 p, vec2 heading, float maxDist, float* uOut, float* latOut, u8 excludeFlags) const {
    thread_local std::vector<int> cand;
    lanesNear(p - vec2(maxDist), p + vec2(maxDist), cand);
    int best = -1;
    float bestScore = 1e30f, bestU = 0.f, bestLat = 0.f;
    bool useHeading = length2(heading) > 1e-6f;
    vec2 h = useHeading ? normalize(heading) : vec2(0, 1);
    for (int li : cand) {
        const Lane& l = lanes[li];
        if (l.flags & excludeFlags) continue;
        const World::RoadEdge& e = roads->edges[l.edge];
        const std::vector<vec2>& vn = edgeNormal[l.edge];
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            vec2 a = e.pts[k].xy() + vn[k] * l.offset, b = e.pts[k + 1].xy() + vn[k + 1] * l.offset;
            float t;
            float d = distPointSegment2D(p, a, b, &t);
            if (d > maxDist) continue;
            float s = e.dist[k] + t * (e.dist[k + 1] - e.dist[k]);
            float u = l.dir > 0 ? s : e.length - s;
            if (u < l.u0 - 0.5f || u > l.u1 + 0.5f) continue;
            float score = d;
            if (useHeading) {
                vec2 td = normalize(b - a) * (float)l.dir;
                float cs = dot(td, h);
                if (cs < 0.3f) continue;
                score += (1.f - cs) * 12.f;
            }
            if (score < bestScore) {
                bestScore = score;
                best = li;
                bestU = Clamp(u, l.u0, l.u1);
                bestLat = (cross(b - a, p - a) >= 0.f ? -d : d) * (float)l.dir;
            }
        }
    }
    if (uOut) *uOut = bestU;
    if (latOut) *latOut = bestLat;
    return best;
}

// ---------------------------------------------------------------------------------------------------------------------
// Signals
SignalState LaneGraph::movementSignal(int node, int approach, int turn, double time) const {
    const NodeInfo& n = nodes[node];
    if (n.control != 2 || n.phases.empty() || approach < 0 || approach >= (int)n.approaches.size()) return SIG_NONE;
    float t = (float)fmod(time + n.offset, (double)n.cycle);
    if (t < 0.f) t += n.cycle;
    const Phase* ph = &n.phases.back();
    for (const Phase& p : n.phases)
        if (t >= p.t0 && t < p.t1) {
            ph = &p;
            break;
        }
    int axis = n.approaches[approach].axis;
    if (ph->axis != axis) return SIG_RED;
    bool left = turn == TK_LEFT || turn == TK_UTURN;
    switch (ph->kind) {
        case PH_LEFT: return left ? SIG_ARROW : SIG_RED;
        case PH_LEFT_AMBER: return left ? SIG_AMBER : SIG_RED;
        case PH_GREEN: return SIG_GREEN;
        case PH_AMBER: return SIG_AMBER;
        default: return SIG_RED;
    }
}

PedSignal LaneGraph::pedSignal(int node, int approach, double time) const {
    const NodeInfo& n = nodes[node];
    if (n.control != 2 || n.phases.empty() || approach < 0 || approach >= (int)n.approaches.size()) return PED_UNCONTROLLED;
    float t = (float)fmod(time + n.offset, (double)n.cycle);
    if (t < 0.f) t += n.cycle;
    int axis = n.approaches[approach].axis;
    for (const Phase& p : n.phases) {
        if (!(t >= p.t0 && t < p.t1)) continue;
        if (p.kind != PH_GREEN || p.axis == axis) return PED_DONT;
        float f = (t - p.t0) / Max(p.t1 - p.t0, 0.1f);
        return f < 0.6f ? PED_WALK : PED_FLASH;
    }
    return PED_DONT;
}

int LaneGraph::approachForDir(int node, vec2 approachDir) const {
    const NodeInfo& n = nodes[node];
    int best = -1;
    float bestD = -2.f;
    vec2 d = normalize(approachDir);
    for (int i = 0; i < (int)n.approaches.size(); i++) {
        // traffic traveling along approachDir arrives from the approach pointing opposite to it
        float c = dot(-n.approaches[i].dir, d);
        if (c > bestD) {
            bestD = c;
            best = i;
        }
    }
    return best;
}

int LaneGraph::lampStateForEdge(int edge, vec2 propPos, double time) const {
    if (!roads || edge < 0 || edge >= (int)roads->edges.size()) return 2;
    const World::RoadEdge& e = roads->edges[edge];
    int node = length2(roads->nodes[e.n0].p - propPos) <= length2(roads->nodes[e.n1].p - propPos) ? e.n0 : e.n1;
    const NodeInfo& N = nodes[node];
    for (int ai = 0; ai < (int)N.approaches.size(); ai++) {
        const Approach& A = N.approaches[ai];
        if (A.edge != edge || A.outgoing != (e.n0 == node)) continue;
        SignalState s = movementSignal(node, ai, TK_STRAIGHT, time);
        return s == SIG_RED ? 0 : (s == SIG_AMBER ? 1 : 2);
    }
    return 2;
}

// ---------------------------------------------------------------------------------------------------------------------
// Walk graph geometry
vec3 LaneGraph::walkPos(int link, float x, float lateral, bool fromA) const {
    const WalkLink& w = walkLinks[link];
    float f = w.length > 1e-4f ? Clamp(x / w.length, 0.f, 1.f) : 0.f;
    if (!fromA) f = 1.f - f;
    // lateral is right of the walking direction
    if (w.kind == WL_SIDEWALK) {
        const World::RoadEdge& e = roads->edges[w.edge];
        float s = Lerp(w.sa, w.sb, f);
        s = Clamp(s, 0.f, e.length);
        size_t n = e.pts.size();
        size_t i = std::upper_bound(e.dist.begin(), e.dist.end(), s) - e.dist.begin();
        if (i == 0) i = 1;
        if (i >= n) i = n - 1;
        float seg = e.dist[i] - e.dist[i - 1];
        float t = seg > 1e-5f ? (s - e.dist[i - 1]) / seg : 0.f;
        vec3 c = lerp(e.pts[i - 1], e.pts[i], t);
        vec2 nrm = lerp(edgeNormal[w.edge][i - 1], edgeNormal[w.edge][i], t);
        // walking direction along the edge: +s when sb > sa and fromA
        float walkSign = ((w.sb >= w.sa) == fromA) ? 1.f : -1.f;
        float latE = w.lat + Clamp(lateral, -w.halfWidth, w.halfWidth) * walkSign;
        return c + vec3(nrm * latE, 0.15f);
    }
    const WalkNode& A = walkNodes[w.a];
    const WalkNode& B = walkNodes[w.b];
    vec3 p;
    vec2 tan;
    if (w.kind == WL_CORNER) {
        float u = 1.f - f;
        vec2 q = A.p.xy() * (u * u) + w.ctrl * (2.f * u * f) + B.p.xy() * (f * f);
        tan = normalize((w.ctrl - A.p.xy()) * (2.f * u) + (B.p.xy() - w.ctrl) * (2.f * f));
        p = vec3(q, Lerp(A.p.z, B.p.z, f));
    } else {
        p = lerp(A.p, B.p, f);
        tan = normalize(B.p.xy() - A.p.xy());
        if (w.kind != WL_PATH) p.z -= 0.15f;  // a crosswalk / zebra is on the road surface (a plaza path at its own level)
    }
    if (!fromA) tan = -tan;
    float lat = Clamp(lateral, -w.halfWidth, w.halfWidth);
    return p + vec3(rightOf(tan) * lat, 0.f);
}

vec2 LaneGraph::walkTangent(int link, float x, bool fromA) const {
    vec3 a = walkPos(link, x, 0.f, fromA), b = walkPos(link, Min(x + 0.8f, walkLinks[link].length), 0.f, fromA);
    vec2 d = b.xy() - a.xy();
    if (length2(d) < 1e-6f) {
        a = walkPos(link, Max(0.f, x - 0.8f), 0.f, fromA);
        d = b.xy() - a.xy();
    }
    return normalize(d);
}

int LaneGraph::nearestWalk(vec2 p, float maxDist, float* xOut, float* latOut) const {
    thread_local std::vector<int> cand;
    walksNear(p - vec2(maxDist), p + vec2(maxDist), cand);
    int best = -1;
    float bestD = maxDist, bestX = 0.f, bestLat = 0.f;
    for (int li : cand) {
        const WalkLink& w = walkLinks[li];
        if (w.kind == WL_PATH) {
            // a plaza walkway: straight between its nodes
            vec2 a = walkNodes[w.a].p.xy(), b = walkNodes[w.b].p.xy();
            float t;
            float d = distPointSegment2D(p, a, b, &t);
            if (d < bestD) {
                bestD = d;
                best = li;
                bestX = t * w.length;
                bestLat = cross(b - a, p - a) >= 0.f ? -d : d;   // (right of a->b is +)
            }
            continue;
        }
        if (w.kind != WL_SIDEWALK) continue;
        const World::RoadEdge& e = roads->edges[w.edge];
        const std::vector<vec2>& vn = edgeNormal[w.edge];
        float smin = Min(w.sa, w.sb), smax = Max(w.sa, w.sb);
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            if (e.dist[k + 1] < smin || e.dist[k] > smax) continue;
            vec2 a = e.pts[k].xy() + vn[k] * w.lat, b = e.pts[k + 1].xy() + vn[k + 1] * w.lat;
            float t;
            float d = distPointSegment2D(p, a, b, &t);
            if (d < bestD) {
                float s = Clamp(e.dist[k] + t * (e.dist[k + 1] - e.dist[k]), smin, smax);
                bestD = d;
                best = li;
                float f = fabsf(w.sb - w.sa) > 1e-4f ? (s - w.sa) / (w.sb - w.sa) : 0.f;
                bestX = Clamp(f, 0.f, 1.f) * w.length;
                float side = cross(b - a, p - a) >= 0.f ? -1.f : 1.f;  // right of n0->n1
                bestLat = side * d * ((w.sb >= w.sa) ? 1.f : -1.f);
            }
        }
    }
    if (xOut) *xOut = bestX;
    if (latOut) *latOut = bestLat;
    return best;
}

// ---------------------------------------------------------------------------------------------------------------------
// Build
void LaneGraph::build(const World::RoadNetwork& rn) {
    double t0 = TimeSeconds();
    roads = &rn;
    const int NE = (int)rn.edges.size(), NN = (int)rn.nodes.size();
    lanes.clear();
    conns.clear();
    nodes.assign(NN, NodeInfo());
    groupFirst.assign((size_t)NE * 2, -1);
    groupCount.assign((size_t)NE * 2, 0);
    edgeNormal.assign(NE, {});
    edgeCurv.assign(NE, {});
    walkNodes.clear();
    walkLinks.clear();

    // ---- smoothed edge geometry
    for (int ei = 0; ei < NE; ei++) {
        const World::RoadEdge& e = rn.edges[ei];
        size_t n = e.pts.size();
        std::vector<vec2>& vn = edgeNormal[ei];
        std::vector<float>& kv = edgeCurv[ei];
        vn.assign(n, vec2(0, 0));
        kv.assign(n, 0.f);
        if (n < 2) continue;
        for (size_t k = 0; k < n; k++) {
            vec2 tPrev = k > 0 ? e.pts[k].xy() - e.pts[k - 1].xy() : vec2(0, 0);
            vec2 tNext = k + 1 < n ? e.pts[k + 1].xy() - e.pts[k].xy() : vec2(0, 0);
            float lp = length(tPrev), ln = length(tNext);
            vec2 a = lp > 1e-5f ? tPrev / lp : (ln > 1e-5f ? tNext / ln : vec2(1, 0));
            vec2 b = ln > 1e-5f ? tNext / ln : a;
            vec2 th = normalize(a + b);
            float miter = 1.f / Max(0.5f, dot(th, b));
            vn[k] = rightOf(th) * miter;
            if (k > 0 && k + 1 < n && lp > 1e-5f && ln > 1e-5f) kv[k] = atan2f(cross(a, b), dot(a, b)) / (0.5f * (lp + ln));
        }
        // light smoothing of the curvature profile
        std::vector<float> ks = kv;
        for (size_t k = 1; k + 1 < n; k++) ks[k] = kv[k] * 0.5f + (kv[k - 1] + kv[k + 1]) * 0.25f;
        kv = ks;
    }

    // ---- per node approaches (sorted CCW)
    std::vector<int> degree(NN, 0);
    for (int ni = 0; ni < NN; ni++) degree[ni] = (int)rn.nodes[ni].edges.size();
    for (int ni = 0; ni < NN; ni++) {
        const World::RoadNode& rnode = rn.nodes[ni];
        NodeInfo& N = nodes[ni];
        std::vector<int> seenCount;
        for (size_t k = 0; k < rnode.edges.size(); k++) {
            int ei = rnode.edges[k];
            const World::RoadEdge& e = rn.edges[ei];
            // a loop edge appears twice: first occurrence = start, second = end
            int prior = 0;
            for (size_t q = 0; q < k; q++) prior += rnode.edges[q] == ei;
            bool outgoing = e.n0 == ni && (e.n1 != ni || prior == 0);
            Approach A;
            A.edge = ei;
            A.outgoing = outgoing;
            A.cut = outgoing ? e.cut0 : e.cut1;
            float probe = Clamp(Max(A.cut, 4.f), 0.f, e.length * 0.5f);
            vec3 p = e.posAt(outgoing ? probe : e.length - probe);
            vec2 d = p.xy() - rnode.p;
            if (length2(d) < 1e-4f) d = (outgoing ? e.pts[1] : e.pts[e.pts.size() - 2]).xy() - rnode.p;
            A.dir = normalize(d);
            A.ang = atan2f(A.dir.y, A.dir.x);
            A.hw = e.halfWidth;
            A.sw = (e.flags & World::RF_NOSIDEWALK) ? 0.f : e.sidewalk;
            N.approaches.push_back(A);
        }
        std::sort(N.approaches.begin(), N.approaches.end(), [](const Approach& a, const Approach& b) { return a.ang < b.ang; });
        N.control = N.approaches.size() >= 3 ? rnode.control : 0;
        N.deadEnd = N.approaches.size() == 1;
        int nh = 0;
        for (auto& a : N.approaches) nh += rn.edges[a.edge].cls == World::RC_HIGHWAY;
        N.gradeSeparated = N.approaches.size() >= 4 && nh == (int)N.approaches.size();
    }

    // ---- lane end cut-backs per edge end (0 = n0 end, 1 = n1 end)
    std::vector<float> laneCut((size_t)NE * 2, 0.f);
    auto laneOffsetsOf = [&](const World::RoadEdge& e, int dir, std::vector<float>& offs) {
        const World::RoadClassInfo& ri = World::roadInfo(e.cls);
        offs.clear();
        int cnt = dir > 0 ? e.lanesF : e.lanesB;
        bool twoWay = e.lanesF > 0 && e.lanesB > 0;
        float W = ri.laneWidth;
        for (int k = 0; k < cnt; k++) {
            float off;
            if (twoWay) {
                float laneStart = ri.median > 0.f ? ri.median * 0.5f : 0.f;
                off = (float)dir * (laneStart + (k + 0.5f) * W);
            } else {
                off = -W * cnt * 0.5f + (k + 0.5f) * W;
                if (dir < 0) off = -off;
            }
            offs.push_back(off);
        }
    };
    // an on/off-ramp meets the motorway on the motorway's centre line, its taper crossing the outer lanes: the ramp's lane
    // ends (starts) where its line clears the motorway's lanes, so the merge (diverge) connector runs down the taper
    // alongside the outer lane - instead of ramp traffic stopping to give way inside the lanes it is about to join
    auto rampClearCut = [&](const World::RoadEdge& e, int end, int ni) -> float {
        float clear = 0.f;
        std::vector<int> hwys;
        for (int oe : rn.nodes[ni].edges) {
            const World::RoadEdge& o = rn.edges[oe];
            if (o.cls != World::RC_HIGHWAY) continue;
            const World::RoadClassInfo& oi = World::roadInfo(o.cls);
            float laneStart = o.lanesF > 0 && o.lanesB > 0 && oi.median > 0.f ? oi.median * 0.5f : 0.f;
            clear = Max(clear, laneStart + (float)Max(o.lanesF, o.lanesB) * oi.laneWidth);
            hwys.push_back(oe);
        }
        if (hwys.empty()) return 0.f;
        clear += World::roadInfo(e.cls).laneWidth * 0.5f + 0.3f;
        for (float s = 0.f; s < e.length * 0.6f; s += 1.f) {
            vec2 p = e.posAt(end == 0 ? s : e.length - s).xy();
            float dmin = 1e9f;
            for (int oe : hwys) {
                const World::RoadEdge& o = rn.edges[oe];
                for (size_t k = 0; k + 1 < o.pts.size(); k++) {
                    float t;
                    dmin = Min(dmin, distPointSegment2D(p, o.pts[k].xy(), o.pts[k + 1].xy(), &t));
                }
            }
            if (dmin >= clear) return s;
        }
        return e.length * 0.6f;
    };
    for (int ei = 0; ei < NE; ei++) {
        const World::RoadEdge& e = rn.edges[ei];
        for (int end = 0; end < 2; end++) {
            int ni = end == 0 ? e.n0 : e.n1;
            const World::RoadNode& rnode = rn.nodes[ni];
            float cut = end == 0 ? e.cut0 : e.cut1;
            int deg = degree[ni];
            if (deg >= 3 && rnode.radius > 0.f) {
                laneCut[ei * 2 + end] = cut;
                if (e.cls == World::RC_RAMP) laneCut[ei * 2 + end] = Max(cut, rampClearCut(e, end, ni));
            } else if (deg == 1) {
                laneCut[ei * 2 + end] = Min(9.f, e.length * 0.3f);
            } else if (deg == 2) {
                // continuation: leave room for a smooth lane shift when the layouts differ
                int other = rnode.edges[0] == ei ? rnode.edges[1] : rnode.edges[0];
                const World::RoadEdge& o = rn.edges[other];
                std::vector<float> a, b;
                laneOffsetsOf(e, 1, a);
                laneOffsetsOf(o, 1, b);
                float shift = 0.f;
                size_t m = Max(a.size(), b.size());
                for (size_t k = 0; k < m && !a.empty() && !b.empty(); k++)
                    shift = Max(shift, fabsf(fabsf(a[Min(k, a.size() - 1)]) - fabsf(b[Min(k, b.size() - 1)])));
                bool sameLayout = e.lanesF == o.lanesF && e.lanesB == o.lanesB && shift < 0.3f;
                vec2 da = normalize((end == 0 ? e.pts[1] : e.pts[e.pts.size() - 2]).xy() - rnode.p);
                vec2 db = normalize((o.n0 == ni ? o.pts[1] : o.pts[o.pts.size() - 2]).xy() - rnode.p);
                float bend = 1.f + dot(da, db);  // 0 = straight continuation
                float c = sameLayout ? 1.5f + bend * 10.f : 4.f + shift * 2.5f + bend * 10.f;
                laneCut[ei * 2 + end] = Min(c, e.length * 0.3f);
            }
        }
        // guarantee a minimum lane length
        float total = laneCut[ei * 2] + laneCut[ei * 2 + 1];
        if (total > e.length - 1.f && total > 0.f) {
            float k = Max(0.f, e.length - 1.f) / total;
            laneCut[ei * 2] *= k;
            laneCut[ei * 2 + 1] *= k;
        }
    }

    // ---- lanes
    for (int ei = 0; ei < NE; ei++) {
        const World::RoadEdge& e = rn.edges[ei];
        if (e.pts.size() < 2 || e.length < 0.5f) continue;
        const World::RoadClassInfo& ri = World::roadInfo(e.cls);
        for (int dir = 1; dir >= -1; dir -= 2) {
            int cnt = dir > 0 ? e.lanesF : e.lanesB;
            if (cnt <= 0) continue;
            std::vector<float> offs;
            laneOffsetsOf(e, dir, offs);
            int grp = ei * 2 + (dir < 0 ? 1 : 0);
            groupFirst[grp] = (int)lanes.size();
            groupCount[grp] = (u8)cnt;
            for (int k = 0; k < cnt; k++) {
                Lane L;
                L.edge = ei;
                L.dir = (i8)dir;
                L.index = (u8)k;
                L.count = (u8)cnt;
                L.cls = e.cls;
                L.offset = offs[k];
                L.width = ri.laneWidth;
                L.fromNode = dir > 0 ? e.n0 : e.n1;
                L.toNode = dir > 0 ? e.n1 : e.n0;
                float cutStart = dir > 0 ? laneCut[ei * 2] : laneCut[ei * 2 + 1];
                float cutEnd = dir > 0 ? laneCut[ei * 2 + 1] : laneCut[ei * 2];
                L.u0 = cutStart;
                L.u1 = Max(e.length - cutEnd, cutStart + 0.5f);
                L.speed = ri.speed;
                if (e.cls == World::RC_HIGHWAY) L.flags |= LF_HIGHWAY;
                if (e.cls == World::RC_RAMP) L.flags |= LF_RAMP;
                if (e.cls == World::RC_DIRT || (e.flags & World::RF_UNPAVED)) L.flags |= LF_DIRT;
                if (e.flags & World::RF_ONEWAY) L.flags |= LF_ONEWAY;
                if (e.flags & World::RF_BRIDGE) L.flags |= LF_BRIDGE;
                L.group = grp;
                L.left = k > 0 ? (int)lanes.size() - 1 : -1;
                lanes.push_back(L);
            }
            for (int k = 0; k + 1 < cnt; k++) lanes[groupFirst[grp] + k].right = groupFirst[grp] + k + 1;
        }
    }

    // ---- approach lane lists, crosswalk/stop lines, ranks, axes
    for (int ni = 0; ni < NN; ni++) {
        NodeInfo& N = nodes[ni];
        int na = (int)N.approaches.size();
        for (int ai = 0; ai < na; ai++) {
            Approach& A = N.approaches[ai];
            const World::RoadEdge& e = rn.edges[A.edge];
            int inDir = A.outgoing ? -1 : 1;
            for (int k = 0;; k++) {
                int l = laneOf(A.edge, inDir, k);
                if (l < 0) break;
                if (e.n0 == e.n1 && lanes[l].toNode != ni) break;
                A.inLanes.push_back(l);
                lanes[l].approachIn = ai;
            }
            for (int k = 0;; k++) {
                int l = laneOf(A.edge, -inDir, k);
                if (l < 0) break;
                A.outLanes.push_back(l);
            }
            bool hwy = e.cls == World::RC_HIGHWAY || e.cls == World::RC_RAMP;
            A.crosswalk = N.control >= 1 && !hwy && na >= 3;
            A.crossS = A.cut + (A.crosswalk ? 2.3f : 1.2f);
            // stop line for controlled approaches (matches the painted stop line 4.4-4.85 m behind the cut)
            if (N.control >= 1 && na >= 3 && !hwy)
                for (int l : A.inLanes) lanes[l].stopU = Max(lanes[l].u0, lanes[l].u1 - 5.1f);
        }
        if (na < 2) continue;
        // main road (rank 1): the most collinear pair with the highest classes
        int bi = -1, bj = -1;
        float best = -1e9f;
        for (int i = 0; i < na; i++)
            for (int j = i + 1; j < na; j++) {
                float straight = -dot(N.approaches[i].dir, N.approaches[j].dir);
                if (straight < 0.5f) continue;
                float sc = straight * 2.f + (classValue(rn.edges[N.approaches[i].edge].cls) + classValue(rn.edges[N.approaches[j].edge].cls)) * 0.6f;
                if (sc > best) {
                    best = sc;
                    bi = i;
                    bj = j;
                }
            }
        if (bi < 0) {
            float bc = -1.f;
            for (int i = 0; i < na; i++) {
                float c = classValue(rn.edges[N.approaches[i].edge].cls);
                if (c > bc) {
                    bc = c;
                    bi = i;
                }
            }
        }
        for (int i = 0; i < na; i++) N.approaches[i].rank = (i == bi || i == bj) ? 1 : 0;
        // signal axes
        if (N.control == 2) {
            std::vector<int> axisOf(na, -1);
            axisOf[bi] = 0;
            if (bj >= 0) axisOf[bj] = 0;
            vec2 ax0 = N.approaches[bi].dir;
            vec2 ax1(0, 0);
            int axes = 1;
            for (int i = 0; i < na; i++) {
                if (axisOf[i] >= 0) continue;
                vec2 d = N.approaches[i].dir;
                if (fabsf(dot(d, ax0)) > 0.85f) axisOf[i] = 0;
                else if (axes == 1) {
                    axisOf[i] = 1;
                    ax1 = d;
                    axes = 2;
                } else if (fabsf(dot(d, ax1)) > 0.6f) axisOf[i] = 1;
                else {
                    axisOf[i] = 2;
                    axes = 3;
                }
            }
            for (int i = 0; i < na; i++) N.approaches[i].axis = (u8)axisOf[i];
            N.axisCount = (u8)axes;
        }
    }

    // ---- connectors
    std::vector<std::vector<int>> nodeConns(NN);
    int uturnPulled = 0, uturnBlockedCount = 0;   // dead ends whose turning circle had to be moved / is obstructed
    for (int ni = 0; ni < NN; ni++) {
        NodeInfo& N = nodes[ni];
        const World::RoadNode& rnode = rn.nodes[ni];
        int na = (int)N.approaches.size();
        N.firstConn = (int)conns.size();
        if (na == 0) continue;
        float uturnBack = 0.f, uturnScale = 1.f;
        auto addConn = [&](int ai, int li, int aj, int lo, u8 turn) {
            const Lane& Lin = lanes[li];
            const Lane& Lout = lanes[lo];
            Connector c;
            c.from = li;
            c.to = lo;
            c.node = ni;
            c.turn = turn;
            c.approach = (u8)ai;
            c.outApproach = (u8)aj;
            c.grade = N.gradeSeparated;
            vec3 p0 = lanePos(li, Lin.u1), p3 = lanePos(lo, Lout.u0);
            vec2 t0 = laneTangent(li, Lin.u1), t3 = laneTangent(lo, Lout.u0);
            float D = length(p3.xy() - p0.xy());
            if (turn == TK_UTURN) {
                // turning circle around the node: swing right to the circle, half a turn to the left, back to the lane
                // (pulled back toward the street / tightened when something stands in the way, see the dead-end code)
                vec2 c0 = rnode.p - t0 * uturnBack;
                float R = Clamp(fabsf(Lin.offset) + 3.4f, 4.8f, 6.5f) * uturnScale;
                vec2 rt = rightOf(t0), lf = -rt;
                float zc = (p0.z + p3.z) * 0.5f;
                vec3 b1(c0 + rt * R, zc), b2(c0 + lf * R, zc);
                Connector h1, h2;
                float k1 = Max(dot(b1.xy() - p0.xy(), t0) * 0.45f, 1.f);
                sampleBezier(h1, p0, p0 + vec3(t0 * k1, 0.f), b1 - vec3(t0 * k1, 0.f), b1);
                c.pts = h1.pts;
                for (int k = 1; k <= 12; k++) {
                    float th = -kHalfPi + kPi * k / 12.f;
                    c.pts.push_back(vec3(c0 + t0 * (R * cosf(th)) + lf * (R * sinf(th)), zc));
                }
                float k2 = Max(dot(b2.xy() - p3.xy(), -t3) * 0.45f, 1.f);
                sampleBezier(h2, b2, b2 - vec3(t0 * k2, 0.f), p3 - vec3(t3 * k2, 0.f), p3);
                c.pts.insert(c.pts.end(), h2.pts.begin() + 1, h2.pts.end());
            } else {
                vec3 p1, p2;
                float den = cross(t0, t3);
                bool arc = false;
                if (fabsf(den) > 0.15f) {
                    // tangent-line intersection X = p0 + t0*a = p3 - t3*b
                    vec2 w = p3.xy() - p0.xy();
                    float a = cross(w, t3) / den;
                    float b = cross(w, t0) / den;
                    if (a > 0.15f * D && b > 0.15f * D && a < 2.5f * D && b < 2.5f * D) {
                        p1 = p0 + vec3(t0 * (a * 0.56f), 0.f);
                        p2 = p3 - vec3(t3 * (b * 0.56f), 0.f);
                        arc = true;
                    }
                }
                if (!arc) {
                    float k = Max(D * 0.38f, 0.5f);
                    p1 = p0 + vec3(t0 * k, 0.f);
                    p2 = p3 - vec3(t3 * k, 0.f);
                }
                sampleBezier(c, p0, p1, p2, p3);
            }
            // z: blend linearly between the lane ends (the box is flat at node height)
            for (size_t i = 0; i < c.pts.size(); i++) {
                float f = c.pts.size() > 1 ? (float)i / (c.pts.size() - 1) : 0.f;
                c.pts[i].z = Lerp(p0.z, p3.z, f);
            }
            finishPath(c);
            int id = (int)conns.size();
            conns.push_back(c);
            lanes[li].out.push_back(id);
            nodeConns[ni].push_back(id);
        };
        if (N.deadEnd) {
            const Approach& A = N.approaches[0];
            const World::RoadEdge& e = rn.edges[A.edge];
            bool oneway = (e.flags & World::RF_ONEWAY) != 0;
            if (!oneway && !A.inLanes.empty() && !A.outLanes.empty()) {
                for (size_t k = 0; k < A.inLanes.size(); k++) {
                    int lo = A.outLanes[Min(k, A.outLanes.size() - 1)];
                    // the turning circle must be clear of buildings and site structures (walls, signs, fences): pull it
                    // back toward the street until it fits (one standing in a cul-de-sac bulb would otherwise trap every car)
                    bool placed = false;
                    const int kTries = 9;
                    for (int attempt = 0; attempt < kTries && !placed; attempt++) {
                        uturnBack = 1.5f * attempt;   // (never tighter: cars cannot follow a circle much under 5 m)
                        uturnScale = 1.f;
                        addConn(0, A.inLanes[k], 0, lo, TK_UTURN);
                        const Connector& c = conns.back();
                        bool blocked = false;
                        for (const vec3& q : c.pts)
                            if ((World::gBuildings && World::gBuildings->pointInBuilding(q.xy(), 4.2f)) ||   // body corners + overshoot at the apex
                                World::siteColliderNear(q, 4.2f, 2.5f)) {                                  // site walls, signs, fences too
                                blocked = true;
                                break;
                            }
                        if (!blocked) {
                            placed = true;
                            uturnPulled += attempt > 0;
                        } else if (attempt < kTries - 1) {
                            conns.pop_back();
                            lanes[A.inLanes[k]].out.pop_back();
                            nodeConns[ni].pop_back();
                        } else {
                            N.uturnBlocked = true;   // keep the last try; routing steers clear of this dead end
                            uturnBlockedCount++;
                        }
                    }
                    uturnBack = 0.f;
                    uturnScale = 1.f;
                }
            }
            N.connCount = (int)conns.size() - N.firstConn;
            continue;
        }
        // movement classification
        struct Move {
            int ai, aj;
            u8 turn;
            float ang;
        };
        std::vector<Move> moves;
        // where a ramp meets the motorway (merge / diverge): only the movements along the carriageway - nothing turns
        // across the other carriageway's lanes and the median barrier
        bool interchange = false;
        {
            int nh = 0, nr = 0;
            for (const Approach& A : N.approaches) {
                nh += rn.edges[A.edge].cls == World::RC_HIGHWAY;
                nr += rn.edges[A.edge].cls == World::RC_RAMP;
            }
            interchange = nh > 0 && nr > 0;
        }
        for (int ai = 0; ai < na; ai++) {
            const Approach& A = N.approaches[ai];
            if (A.inLanes.empty()) continue;
            vec2 tin = -A.dir;  // travel direction arriving at the node
            for (int aj = 0; aj < na; aj++) {
                if (aj == ai) continue;
                const Approach& B = N.approaches[aj];
                if (B.outLanes.empty()) continue;
                vec2 tout = B.dir;
                float ang = atan2f(cross(tin, tout), dot(tin, tout));
                u8 turn = TK_STRAIGHT;
                if (na == 2) turn = TK_STRAIGHT;
                else if (fabsf(ang) > 140.f * kDegToRad) continue;  // U-turn through an intersection: not allowed (nor the
                                                                     // hairpin back into a skewed road: it sweeps across the
                                                                     // start of the other road's lane on its way round)
                else if (ang > 35.f * kDegToRad) turn = TK_LEFT;
                else if (ang < -35.f * kDegToRad) turn = TK_RIGHT;
                if (interchange) {
                    if (fabsf(ang) > 60.f * kDegToRad) continue;
                    turn = TK_STRAIGHT;   // (a taper at up to 60 degrees is still a merge or a diverge)
                }
                if (N.gradeSeparated && turn != TK_STRAIGHT) continue;
                moves.push_back({ai, aj, turn, ang});
            }
        }
        // straight sources per outgoing approach: primary keeps lane order, secondaries map to their side
        std::vector<int> primarySrc(na, -1);
        for (int aj = 0; aj < na; aj++) {
            float bestA = 1e9f;
            for (const Move& m : moves) {
                if (m.aj != aj || m.turn != TK_STRAIGHT) continue;
                float score = fabsf(m.ang) - N.approaches[m.ai].rank * 0.3f;
                if (score < bestA) {
                    bestA = score;
                    primarySrc[aj] = m.ai;
                }
            }
        }
        for (int ai = 0; ai < na; ai++) {
            const Approach& A = N.approaches[ai];
            int Lin = (int)A.inLanes.size();
            if (Lin == 0) continue;
            bool hasLeft = false, hasRight = false, hasStraight = false;
            for (const Move& m : moves)
                if (m.ai == ai) {
                    hasLeft |= m.turn == TK_LEFT;
                    hasRight |= m.turn == TK_RIGHT;
                    hasStraight |= m.turn == TK_STRAIGHT;
                }
            // a diverge / fork (an exit ramp off the motorway, a Y): with more than one way on ahead, the best aligned is
            // the carriageway itself (every lane carries on), the others are reached only from the lanes on their side -
            // an exit on the right from the right-hand lane(s), never across the other lanes from the far side (where
            // this approach is itself the side stream into that road - a merge - it keeps the merge's mapping below)
            const Move* mainOn = nullptr;
            {
                float bestA = 1e9f;
                for (const Move& m : moves) {
                    if (m.ai != ai || m.turn != TK_STRAIGHT) continue;
                    float score = fabsf(m.ang) - N.approaches[m.aj].rank * 0.3f;
                    if (score < bestA) {
                        bestA = score;
                        mainOn = &m;
                    }
                }
            }
            std::vector<int> laneUse(Lin, 0);
            for (const Move& m : moves) {
                if (m.ai != ai) continue;
                const Approach& B = N.approaches[m.aj];
                int Lout = (int)B.outLanes.size();
                if (m.turn == TK_STRAIGHT && mainOn && &m != mainOn && primarySrc[m.aj] == ai) {
                    bool onRight = m.ang < mainOn->ang;
                    for (int j = 0; j < Min(Lin, Lout); j++) {
                        int k = onRight ? Lin - 1 - j : j, ko = onRight ? Lout - 1 - j : j;
                        addConn(ai, A.inLanes[k], m.aj, B.outLanes[ko], TK_STRAIGHT);
                        laneUse[k]++;
                    }
                } else if (m.turn == TK_STRAIGHT) {
                    bool primary = primarySrc[m.aj] == ai;
                    // which side does a secondary source come from?  compare lateral positions in the outgoing frame
                    bool fromRight = false;
                    if (!primary) {
                        vec3 pe = lanePos(A.inLanes[Lin - 1], lanes[A.inLanes[Lin - 1]].u1);
                        vec3 ps = lanePos(B.outLanes[Lout - 1], lanes[B.outLanes[Lout - 1]].u0);
                        vec2 tb = laneTangent(B.outLanes[0], lanes[B.outLanes[0]].u0);
                        fromRight = dot(pe.xy() - ps.xy(), rightOf(tb)) > -1.5f;
                    }
                    for (int k = 0; k < Lin; k++) {
                        // with a separate turn movement, the outer lanes keep their turn role on wide approaches
                        if (Lin >= 3 && hasLeft && k == 0 && hasStraight && na >= 4) continue;  // dedicated left lane
                        int ko;
                        if (primary || !fromRight) ko = Min(k, Lout - 1);
                        else ko = Max(0, Lout - Lin + k);
                        if (Lin > Lout && primary && k >= Lout && hasRight && na >= 3) continue;  // lane becomes right-turn only
                        addConn(ai, A.inLanes[k], m.aj, B.outLanes[ko], (Lin > Lout && k >= Lout - 1 && k > 0) ? TK_MERGE : TK_STRAIGHT);
                        laneUse[k]++;
                    }
                } else if (m.turn == TK_RIGHT) {
                    addConn(ai, A.inLanes[Lin - 1], m.aj, B.outLanes[Lout - 1], TK_RIGHT);
                    laneUse[Lin - 1]++;
                    // double right from the second lane on very wide approaches into wide roads
                    if (Lin >= 3 && Lout >= 2 && !hasStraight) {
                        addConn(ai, A.inLanes[Lin - 2], m.aj, B.outLanes[Lout - 2], TK_RIGHT);
                        laneUse[Lin - 2]++;
                    }
                } else if (m.turn == TK_LEFT) {
                    addConn(ai, A.inLanes[0], m.aj, B.outLanes[0], TK_LEFT);
                    laneUse[0]++;
                    if (Lin >= 3 && Lout >= 2 && !hasStraight) {
                        addConn(ai, A.inLanes[1], m.aj, B.outLanes[1], TK_LEFT);
                        laneUse[1]++;
                    }
                }
            }
            // every lane needs a way out
            for (int k = 0; k < Lin; k++) {
                if (laneUse[k]) continue;
                const Move* pick = nullptr;
                for (const Move& m : moves)
                    if (m.ai == ai && m.turn == TK_STRAIGHT) pick = &m;
                if (!pick)
                    for (const Move& m : moves)
                        if (m.ai == ai && ((k < Lin / 2 && m.turn == TK_LEFT) || (k >= Lin / 2 && m.turn == TK_RIGHT))) pick = &m;
                if (!pick)
                    for (const Move& m : moves)
                        if (m.ai == ai) pick = &m;
                if (pick) {
                    const Approach& B = N.approaches[pick->aj];
                    int Lout = (int)B.outLanes.size();
                    int ko = pick->turn == TK_RIGHT ? Lout - 1 : (pick->turn == TK_LEFT ? 0 : Min(k, Lout - 1));
                    addConn(ai, A.inLanes[k], pick->aj, B.outLanes[ko], pick->turn == TK_STRAIGHT ? TK_MERGE : pick->turn);
                } else {
                    // no legal exit (e.g. every other road is one-way against us): U-turn if the road is two-way
                    if (!A.outLanes.empty()) addConn(ai, A.inLanes[k], ai, A.outLanes[Min((size_t)k, A.outLanes.size() - 1)], TK_UTURN);
                }
            }
        }
        N.connCount = (int)conns.size() - N.firstConn;
    }

    // ---- conflicts and yield rules
    auto prioOf = [&](const NodeInfo& N, const Connector& c) -> int {
        int turnP = c.turn == TK_UTURN ? 0 : (c.turn == TK_LEFT ? 1 : 2);
        if (N.control == 2) return turnP;
        if (N.control == 1) return 0;
        return N.approaches[c.approach].rank * 4 + turnP;
    };
    for (int ni = 0; ni < NN; ni++) {
        NodeInfo& N = nodes[ni];
        for (int a = N.firstConn; a < N.firstConn + N.connCount; a++) conns[a].prio = (i8)prioOf(N, conns[a]);
        for (int a = N.firstConn; a < N.firstConn + N.connCount; a++) {
            Connector& ca = conns[a];
            for (int b = a + 1; b < N.firstConn + N.connCount; b++) {
                Connector& cb = conns[b];
                if (ca.from == cb.from) continue;   // same lane: sequential
                bool merge = ca.to == cb.to;
                bool sameGroupOut = lanes[ca.to].group == lanes[cb.to].group;
                if (N.gradeSeparated && !merge) continue;
                // traffic from the same approach into different lanes of the same road never conflicts
                if (!merge && ca.approach == cb.approach && sameGroupOut) continue;
                float sa0, sa1, sb0, sb1;
                float r = merge ? 3.3f : 2.95f;   // vehicle bodies sweep wider than their path centerlines
                bool isWide = false;
                if (!closeZone(ca, cb, r, sa0, sa1, sb0, sb1)) {
                    if (!merge) {
                        // close enough to matter for trucks and buses, whose bodies swing up to ~2 m off the path
                        if (ca.approach == cb.approach || !closeZone(ca, cb, 5.2f, sa0, sa1, sb0, sb1)) continue;
                        isWide = true;
                    } else {
                        sa0 = sa1 = ca.length;
                        sb0 = sb1 = cb.length;
                    }
                }
                if (merge) {
                    sa1 = ca.length;
                    sb1 = cb.length;
                }
                // side-by-side movements from the same approach (dual turns) are allowed together
                if (!merge && ca.approach == cb.approach) continue;
                Conflict x;
                x.other = b;
                x.s = sa0;
                x.sEnd = sa1;
                x.sOther = sb0;
                x.sOtherEnd = sb1;
                x.merge = merge;
                Conflict y;
                y.other = a;
                y.s = sb0;
                y.sEnd = sb1;
                y.sOther = sa0;
                y.sOtherEnd = sa1;
                y.merge = merge;
                int pa = ca.prio, pb = cb.prio;
                x.yield = pa < pb;
                y.yield = pb < pa;
                if (N.control == 2) {
                    // only movements that can be green together use the yield rule; opposing lefts do not yield
                    bool sameAxis = N.approaches[ca.approach].axis == N.approaches[cb.approach].axis;
                    if (!sameAxis) x.yield = y.yield = 0;
                }
                if (isWide) {
                    ca.wide.push_back(x);
                    cb.wide.push_back(y);
                } else {
                    ca.conflicts.push_back(x);
                    cb.conflicts.push_back(y);
                }
            }
        }
        // permissive left hold point inside the box: the furthest front-bumper position along the turn where a
        // typical car's footprint (4.8 x 2.0 m, aligned with the path) keeps 1.4 m from every path it yields to
        if (N.control == 2) {
            for (int a = N.firstConn; a < N.firstConn + N.connCount; a++) {
                Connector& c = conns[a];
                c.holdS = 0.f;
                if (c.turn != TK_LEFT) continue;
                bool anyYield = false;
                for (const Conflict& x : c.conflicts) anyYield |= x.yield && !x.merge;
                if (!anyYield) continue;
                float best = 0.f;
                for (float sf = 1.f; sf < c.length * 0.6f; sf += 0.5f) {
                    // footprint: rear at sf - 4.8, front at sf; sample its outline
                    bool ok = true;
                    for (int k = 0; k <= 6 && ok; k++) {
                        float along = sf - 4.8f * k / 6.f;
                        vec2 ctr = pathAt(c, along);
                        vec2 t = tangentAt(c, along);
                        vec2 side = rightOf(t);
                        vec2 corners[2] = {ctr + side * 1.0f, ctr - side * 1.0f};
                        for (const Conflict& x : c.conflicts) {
                            if (!x.yield || x.merge) continue;
                            const Connector& o = conns[x.other];
                            for (const vec3& q : o.pts) {
                                // the other vehicle's half width (1.0) + clearance (0.4)
                                if (length2(q.xy() - corners[0]) < 1.4f * 1.4f || length2(q.xy() - corners[1]) < 1.4f * 1.4f ||
                                    length2(q.xy() - ctr) < 1.4f * 1.4f) {
                                    ok = false;
                                    break;
                                }
                            }
                            if (!ok) break;
                        }
                    }
                    if (!ok) break;
                    best = sf;
                }
                c.holdS = best >= 2.f ? best : 0.f;
            }
        }
    }

    // ---- signal plans
    for (int ni = 0; ni < NN; ni++) {
        NodeInfo& N = nodes[ni];
        if (N.control != 2) continue;
        int axes = Max(1, (int)N.axisCount);
        float t = 0.f;
        u32 h = hash32((u32)ni * 2654435761u + 77u);
        for (int a = 0; a < axes; a++) {
            // protected left when an approach of this axis has 2+ lanes and a left turn with opposing traffic
            bool wide = false, lefts = false;
            int approachesOnAxis = 0;
            float bestClass = 0.f, vmax = 10.f;
            for (size_t ai = 0; ai < N.approaches.size(); ai++) {
                const Approach& A = N.approaches[ai];
                if (A.axis != a) continue;
                if (!A.inLanes.empty()) approachesOnAxis++;
                if (A.inLanes.size() >= 2) wide = true;
                bestClass = Max(bestClass, classValue(rn.edges[A.edge].cls));
                vmax = Max(vmax, World::roadInfo(rn.edges[A.edge].cls).speed);
                for (int c = N.firstConn; c < N.firstConn + N.connCount; c++)
                    if (conns[c].approach == ai && conns[c].turn == TK_LEFT) lefts = true;
            }
            if (approachesOnAxis == 0) continue;
            if (wide && lefts && approachesOnAxis >= 2) {
                N.leftPhase[a] = 1;
                N.phases.push_back({(u8)a, PH_LEFT, t, t + 6.f});
                t += 6.f;
                N.phases.push_back({(u8)a, PH_LEFT_AMBER, t, t + 2.f});
                t += 2.f;
            }
            float green = 11.f + (bestClass >= 5.f ? 7.f : (bestClass >= 4.f ? 4.f : 0.f)) + (float)((h >> (a * 3)) % 4);
            N.phases.push_back({(u8)a, PH_GREEN, t, t + green});
            t += green;
            // amber long enough to clear the line from where stopping stops being comfortable (1 s reaction, 3 m/s^2)
            float amber = Clamp(1.f + vmax * 1.1f / (2.f * 3.f), 3.f, 4.5f);
            N.phases.push_back({(u8)a, PH_AMBER, t, t + amber});
            t += amber;
            N.phases.push_back({(u8)a, PH_ALLRED, t, t + 1.5f});
            t += 1.5f;
        }
        N.cycle = Max(t, 1.f);
        N.offset = (float)(h % 1000) * 0.001f * N.cycle;
    }

    // ---- bus stops and benches (same deterministic placement as the road furniture generator, roadmesh.cpp)
    spots.clear();
    for (int ei = 0; ei < NE; ei++) {
        const World::RoadEdge& e = rn.edges[ei];
        bool hwy = e.cls == World::RC_HIGHWAY || e.cls == World::RC_RAMP;
        if ((e.flags & World::RF_UNPAVED) || hwy || !(e.sidewalk > 2.f)) continue;
        u32 h = e.seed;
        for (float s = e.cut0 + 15.f; s < e.length - e.cut1 - 15.f; s += 23.f) {
            h = hash32(h + 1u);
            int side = (h & 1) ? 1 : -1;
            float r = hashToFloat(h >> 1);
            if (r >= 0.22f) continue;
            vec3 c = e.posAt(s);
            vec3 t = e.tangentAt(s);
            vec2 rv = normalize(vec2(t.y, -t.x));
            ScenarioPoint sp;
            sp.edge = ei;
            sp.face = -rv * (float)side;   // toward the street
            if (r < 0.08f) {
                int dir = side;
                int grp = ei * 2 + (dir < 0 ? 1 : 0);
                if (groupFirst[grp] < 0) continue;
                int lane = groupFirst[grp] + groupCount[grp] - 1;
                float u = dir > 0 ? s : e.length - s;
                if (u > lanes[lane].u0 + 6.f && u < lanes[lane].u1 - 8.f) lanes[lane].busStops.push_back(u);
                sp.kind = SP_BUS_STOP;
                sp.lane = lane;
                sp.u = u;
                sp.pos = c + vec3(rv * ((float)side * (e.halfWidth + e.sidewalk * 0.45f)), 0.15f);
            } else {
                sp.kind = SP_BENCH;
                // the bench stands 0.9 m from the curb; a sitting ped is placed on its seat, facing the street
                sp.pos = c + vec3(rv * ((float)side * (e.halfWidth + 0.9f + 0.3f)), 0.15f);
            }
            spots.push_back(sp);
        }
    }
    for (auto& l : lanes) std::sort(l.busStops.begin(), l.busStops.end());

    // ---- pedestrian walk graph
    {
        // a dead end's sidewalks run on until they meet the sidewalk ring round its turning bulb (roads.cpp bulbRadius):
        // how far short of the node that is (no bulb: 2 m)
        auto deadEndBack = [&](int ni, float hw, float sw, float len) {
            float br = rn.bulbRadius(rn.nodes[ni]);
            if (br <= 0.f) return Min(2.f, len * 0.3f);
            float R = br + sw * 0.5f, lat = hw + sw * 0.5f;
            return Min(sqrtf(Max(R * R - lat * lat, 4.f)), len * 0.3f);
        };
        // walk nodes per (road node, approach, side)
        std::vector<std::vector<int>> wn(NN);  // per node: 2 * approaches entries (CW, CCW), -1 if none
        for (int ni = 0; ni < NN; ni++) {
            NodeInfo& N = nodes[ni];
            const World::RoadNode& rnode = rn.nodes[ni];
            int na = (int)N.approaches.size();
            wn[ni].assign((size_t)na * 2, -1);
            for (int ai = 0; ai < na; ai++) {
                const Approach& A = N.approaches[ai];
                if (A.sw <= 0.5f) continue;
                const World::RoadEdge& e = rn.edges[A.edge];
                float d = N.deadEnd ? deadEndBack(ni, A.hw, A.sw, e.length) : Min(Max(A.crossS, A.cut + 1.f), e.length * 0.45f);
                float s = A.outgoing ? d : e.length - d;
                for (int side = -1; side <= 1; side += 2) {
                    // CCW side (left of the outward direction) in edge frame: outgoing -> -N, incoming -> +N
                    float latSign = (side > 0) == A.outgoing ? -1.f : 1.f;
                    float lat = latSign * (A.hw + A.sw * 0.5f);
                    size_t n = e.pts.size();
                    size_t i = std::upper_bound(e.dist.begin(), e.dist.end(), Clamp(s, 0.f, e.length)) - e.dist.begin();
                    if (i == 0) i = 1;
                    if (i >= n) i = n - 1;
                    float seg = e.dist[i] - e.dist[i - 1];
                    float t = seg > 1e-5f ? (s - e.dist[i - 1]) / seg : 0.f;
                    vec3 c = lerp(e.pts[i - 1], e.pts[i], t);
                    vec2 nrm = lerp(edgeNormal[A.edge][i - 1], edgeNormal[A.edge][i], t);
                    WalkNode w;
                    w.p = c + vec3(nrm * lat, 0.15f);
                    w.roadNode = ni;
                    w.approach = ai;
                    w.side = (i8)side;
                    wn[ni][ai * 2 + (side > 0 ? 1 : 0)] = (int)walkNodes.size();
                    walkNodes.push_back(w);
                }
            }
            auto addLink = [&](WalkLink L) {
                vec3 pa = walkNodes[L.a].p, pb = walkNodes[L.b].p;
                if (L.kind == WL_CORNER) {
                    float len = 0.f;
                    vec2 prev = pa.xy();
                    for (int k = 1; k <= 8; k++) {
                        float f = k / 8.f, u = 1.f - f;
                        vec2 q = pa.xy() * (u * u) + L.ctrl * (2.f * u * f) + pb.xy() * (f * f);
                        len += length(q - prev);
                        prev = q;
                    }
                    L.length = len;
                } else {
                    L.length = length(pb.xy() - pa.xy());
                }
                if (L.length < 0.05f) return;
                int id = (int)walkLinks.size();
                walkLinks.push_back(L);
                walkNodes[L.a].links.push_back(id);
                walkNodes[L.b].links.push_back(id);
            };
            // corners between consecutive approaches (both with sidewalks)
            if (N.deadEnd) {
                int a = wn[ni][0], b = wn[ni][1];
                if (a >= 0 && b >= 0) {
                    const Approach& A = N.approaches[0];
                    float br = rn.bulbRadius(rnode);
                    if (br > 0.f) {
                        // round the turning bulb on its sidewalk ring (nobody walks across the circle the cars turn in):
                        // arcs of at most 45 degrees between ring nodes, each a quadratic through the arc's tangent point
                        vec2 C = rnode.p;
                        float R = br + A.sw * 0.5f;
                        float fa = atan2f(walkNodes[a].p.y - C.y, walkNodes[a].p.x - C.x);
                        float fb = atan2f(walkNodes[b].p.y - C.y, walkNodes[b].p.x - C.x);
                        float beyond = atan2f(-A.dir.y, -A.dir.x);
                        auto wrap = [](float x) {
                            while (x < 0.f) x += kTwoPi;
                            while (x >= kTwoPi) x -= kTwoPi;
                            return x;
                        };
                        float sweep = wrap(fb - fa);                   // counter-clockwise from a to b ...
                        if (wrap(beyond - fa) > sweep) sweep -= kTwoPi;   // ... unless that misses the far side of the bulb
                        int segs = Max(2, (int)ceilf(fabsf(sweep) / (45.f * kDegToRad)));
                        float zRing = (walkNodes[a].p.z + walkNodes[b].p.z) * 0.5f;
                        // (a ring running into another road, a building or a wall: no way round - the sidewalk just ends)
                        bool clear = true;
                        std::vector<int> roadsNear;
                        for (float t = 0.f; t <= 1.f && clear; t += 1.f / Max(8.f, fabsf(sweep) * R / 1.5f)) {
                            float f = fa + sweep * t;
                            vec2 q = C + vec2(cosf(f), sinf(f)) * R;
                            if ((World::gBuildings && World::gBuildings->pointInBuilding(q, 0.3f)) || World::siteColliderNear(vec3(q, zRing), 0.6f, 2.f)) clear = false;
                            roadsNear.clear();
                            rn.edgesInRect(q - vec2(1.f), q + vec2(1.f), roadsNear);
                            for (int oe : roadsNear) {
                                if (oe == A.edge || !clear) continue;
                                const World::RoadEdge& o = rn.edges[oe];
                                for (size_t k = 0; k + 1 < o.pts.size() && clear; k++) {
                                    float tt;
                                    if (distPointSegment2D(q, o.pts[k].xy(), o.pts[k + 1].xy(), &tt) < o.halfWidth + 0.8f) clear = false;
                                }
                            }
                        }
                        int prev = a;
                        for (int k = 1; k <= segs && clear; k++) {
                            int cur = b;
                            if (k < segs) {
                                float f = fa + sweep * k / segs;
                                WalkNode w;
                                w.p = vec3(C + vec2(cosf(f), sinf(f)) * R, zRing);
                                w.roadNode = ni;
                                w.approach = 0;
                                w.side = 0;
                                cur = (int)walkNodes.size();
                                walkNodes.push_back(w);
                            }
                            float f0 = atan2f(walkNodes[prev].p.y - C.y, walkNodes[prev].p.x - C.x);
                            float half = 0.5f * (sweep / segs);
                            float mid = f0 + half;
                            WalkLink L;
                            L.a = prev;
                            L.b = cur;
                            L.kind = WL_CORNER;
                            L.ctrl = C + vec2(cosf(mid), sinf(mid)) * (R / Max(cosf(half), 0.5f));
                            L.halfWidth = Max(A.sw * 0.5f - 0.3f, 0.3f);
                            addLink(L);
                            prev = cur;
                        }
                    } else {
                        WalkLink L;
                        L.a = a;
                        L.b = b;
                        L.kind = WL_CORNER;
                        L.ctrl = rnode.p - A.dir * (A.hw + A.sw + 4.5f) * 1.6f;
                        L.halfWidth = Max(A.sw * 0.5f - 0.3f, 0.3f);
                        addLink(L);
                    }
                }
            } else {
                for (int ai = 0; ai < na; ai++) {
                    int aj = (ai + 1) % na;
                    int a = wn[ni][ai * 2 + 1], b = wn[ni][aj * 2 + 0];
                    if (a < 0 || b < 0 || ai == aj) continue;
                    const Approach& A = N.approaches[ai];
                    const Approach& B = N.approaches[aj];
                    float gap = B.ang - A.ang;
                    if (gap < 0.f) gap += kTwoPi;
                    if (gap > 250.f * kDegToRad && na > 2) continue;
                    vec2 p1 = walkNodes[a].p.xy(), p2 = walkNodes[b].p.xy();
                    vec2 d1 = -A.dir, d2 = -B.dir;  // toward the node
                    float den = cross(d1, d2);
                    vec2 C = (p1 + p2) * 0.5f;
                    if (fabsf(den) > 0.2f) {
                        float t = cross(p2 - p1, d2) / den;
                        vec2 X = p1 + d1 * t;
                        if (t > 0.f && length(X - rnode.p) < (A.hw + A.sw + B.hw + B.sw) * 1.5f + 6.f) C = X;
                    }
                    WalkLink L;
                    L.a = a;
                    L.b = b;
                    L.kind = WL_CORNER;
                    L.ctrl = C;
                    L.halfWidth = Max(Min(A.sw, B.sw) * 0.5f - 0.3f, 0.3f);
                    addLink(L);
                }
                // crosswalks
                for (int ai = 0; ai < na; ai++) {
                    const Approach& A = N.approaches[ai];
                    int a = wn[ni][ai * 2 + 0], b = wn[ni][ai * 2 + 1];
                    if (a < 0 || b < 0 || na < 3) continue;
                    if (!A.crosswalk && (N.control != 0 || A.hw > 8.5f)) continue;
                    WalkLink L;
                    L.a = a;
                    L.b = b;
                    L.kind = WL_CROSSWALK;
                    L.node = ni;
                    L.approach = ai;
                    L.halfWidth = A.crosswalk ? 1.2f : 0.6f;
                    addLink(L);
                }
            }
        }
        // sidewalks along edges
        for (int ei = 0; ei < NE; ei++) {
            const World::RoadEdge& e = rn.edges[ei];
            if (e.sidewalk <= 0.5f || (e.flags & World::RF_NOSIDEWALK)) continue;
            // find the approach index of this edge at both nodes
            int a0 = -1, a1 = -1;
            for (int ai = 0; ai < (int)nodes[e.n0].approaches.size(); ai++)
                if (nodes[e.n0].approaches[ai].edge == ei && nodes[e.n0].approaches[ai].outgoing) a0 = ai;
            for (int ai = 0; ai < (int)nodes[e.n1].approaches.size(); ai++)
                if (nodes[e.n1].approaches[ai].edge == ei && !nodes[e.n1].approaches[ai].outgoing) a1 = ai;
            if (a0 < 0 || a1 < 0) continue;
            for (int side = -1; side <= 1; side += 2) {
                // right side of the edge (+N): at n0 (outgoing) it is the CW side, at n1 (incoming) the CCW side
                int wa = side > 0 ? wn[e.n0][a0 * 2 + 0] : wn[e.n0][a0 * 2 + 1];
                int wb = side > 0 ? wn[e.n1][a1 * 2 + 1] : wn[e.n1][a1 * 2 + 0];
                if (wa < 0 || wb < 0 || wa == wb) continue;
                const Approach& A0 = nodes[e.n0].approaches[a0];
                const Approach& A1 = nodes[e.n1].approaches[a1];
                float da = nodes[e.n0].deadEnd ? deadEndBack(e.n0, A0.hw, A0.sw, e.length) : Min(Max(A0.crossS, A0.cut + 1.f), e.length * 0.45f);
                float db = nodes[e.n1].deadEnd ? deadEndBack(e.n1, A1.hw, A1.sw, e.length) : Min(Max(A1.crossS, A1.cut + 1.f), e.length * 0.45f);
                WalkLink L;
                L.a = wa;
                L.b = wb;
                L.kind = WL_SIDEWALK;
                L.edge = ei;
                L.sa = da;
                L.sb = e.length - db;
                if (L.sb - L.sa < 0.5f) continue;
                L.lat = (float)side * (e.halfWidth + e.sidewalk * 0.5f);
                L.halfWidth = Max(e.sidewalk * 0.5f - 0.35f, 0.25f);
                L.length = L.sb - L.sa;
                int id = (int)walkLinks.size();
                walkLinks.push_back(L);
                walkNodes[wa].links.push_back(id);
                walkNodes[wb].links.push_back(id);
            }
        }
    }

    // ---- walkways off the street network (World::SiteSet::walks: plaza paths, mid-block zebras over site roads): split
    //      where they meet (a path crossing another, a stub ending on one), walk nodes at the ends and the meeting points
    //      (joined to a walk node already within 1.5 m - a sidewalk end, the next path), straight links between them
    int sitePaths = 0, siteZebras = 0;
    if (World::gSites && !World::gSites->walks.empty()) {
        const std::vector<World::SiteWalk>& SW = World::gSites->walks;
        // a gate path ending mid-block on a sidewalk (a cemetery or campus gate): the sidewalk link split there
        std::vector<std::vector<int>> edgeSide(NE);
        for (int li = 0; li < (int)walkLinks.size(); li++)
            if (walkLinks[li].kind == WL_SIDEWALK && walkLinks[li].edge >= 0) edgeSide[walkLinks[li].edge].push_back(li);
        auto splitSidewalk = [&](vec3 q) -> int {
            std::vector<int> cand;
            rn.edgesInRect(q.xy() - vec2(2.f), q.xy() + vec2(2.f), cand);
            int bestL = -1;
            float bestD = 1.5f, bestX = 0.f;
            for (int ei : cand)
                for (int li : edgeSide[ei]) {
                    const WalkLink& w = walkLinks[li];
                    int n = Max(2, (int)(w.length / 0.5f));
                    for (int k = 0; k <= n; k++) {
                        float x = w.length * k / n;
                        vec3 c = walkPos(li, x, 0.f, true);
                        float d = length(c.xy() - q.xy());
                        if (d < bestD && fabsf(c.z - q.z) < 1.2f) {
                            bestD = d;
                            bestL = li;
                            bestX = x;
                        }
                    }
                }
            if (bestL < 0 || bestX < 1.f || bestX > walkLinks[bestL].length - 1.f) return -1;
            WalkLink L = walkLinks[bestL];
            WalkNode nn;
            nn.p = walkPos(bestL, bestX, 0.f, true);
            int ni = (int)walkNodes.size();
            walkNodes.push_back(nn);
            float sm = Lerp(L.sa, L.sb, bestX / Max(L.length, 1e-3f));
            WalkLink L1 = L, L2 = L;
            L1.b = ni;
            L1.sb = sm;
            L1.length = fabsf(L1.sb - L1.sa);
            L2.a = ni;
            L2.sa = sm;
            L2.length = fabsf(L2.sb - L2.sa);
            walkLinks[bestL] = L1;
            int id2 = (int)walkLinks.size();
            walkLinks.push_back(L2);
            for (int& x : walkNodes[L.b].links)
                if (x == bestL) x = id2;
            walkNodes[ni].links.push_back(bestL);
            walkNodes[ni].links.push_back(id2);
            edgeSide[L.edge].push_back(id2);
            return ni;
        };
        auto nodeAt = [&](vec3 q) -> int {
            int best = -1;
            float bd = 1.5f;
            for (int ni = 0; ni < (int)walkNodes.size(); ni++) {
                float d = length(walkNodes[ni].p.xy() - q.xy());
                if (d < bd && fabsf(walkNodes[ni].p.z - q.z) < 1.2f) {
                    bd = d;
                    best = ni;
                }
            }
            if (best >= 0) return best;
            int split = splitSidewalk(q);
            if (split >= 0) return split;
            WalkNode n;
            n.p = q;
            walkNodes.push_back(n);
            return (int)walkNodes.size() - 1;
        };
        // where along each walkway another one meets it (t in 0..1): crossings of two paths, ends lying on one
        std::vector<std::vector<float>> cuts(SW.size());
        for (size_t i = 0; i < SW.size(); i++) {
            cuts[i].push_back(0.f);
            cuts[i].push_back(1.f);
        }
        for (size_t i = 0; i < SW.size(); i++) {
            vec2 a = SW[i].a.xy(), b = SW[i].b.xy();
            float li = length(b - a);
            if (li < 0.5f) continue;
            for (size_t j = 0; j < SW.size(); j++) {
                if (j == i) continue;
                vec2 c = SW[j].a.xy(), e = SW[j].b.xy();
                // an end of j on the inside of i
                for (vec2 q : {c, e}) {
                    float t;
                    float d = distPointSegment2D(q, a, b, &t);
                    if (d < 1.5f && t * li > 1.5f && (1.f - t) * li > 1.5f) cuts[i].push_back(t);
                }
                // the two crossing mid-way (paths only: a zebra meets its paths end to end)
                if (SW[i].kind == World::SW_PATH && SW[j].kind == World::SW_PATH) {
                    vec2 r = b - a, sv = e - c;
                    float den = cross(r, sv);
                    if (fabsf(den) > 1e-4f) {
                        float t = cross(c - a, sv) / den, u = cross(c - a, r) / den;
                        if (t > 0.f && t < 1.f && u > 0.f && u < 1.f && t * li > 1.5f && (1.f - t) * li > 1.5f) cuts[i].push_back(t);
                    }
                }
            }
        }
        for (size_t i = 0; i < SW.size(); i++) {
            const World::SiteWalk& sw = SW[i];
            if (length(sw.b.xy() - sw.a.xy()) < 0.5f) continue;
            std::vector<float>& ts = cuts[i];
            std::sort(ts.begin(), ts.end());
            int prev = -1;
            for (size_t k = 0; k < ts.size(); k++) {
                if (k > 0 && ts[k] - ts[k - 1] < 1e-3f) continue;
                int n = nodeAt(lerp(sw.a, sw.b, ts[k]));
                if (prev >= 0 && n != prev) {
                    WalkLink L;
                    L.a = prev;
                    L.b = n;
                    L.kind = sw.kind == World::SW_CROSSING ? WL_ZEBRA : WL_PATH;
                    L.halfWidth = Max(sw.halfWidth - 0.3f, 0.3f);
                    L.length = length(walkNodes[n].p.xy() - walkNodes[prev].p.xy());
                    int id = (int)walkLinks.size();
                    walkLinks.push_back(L);
                    walkNodes[prev].links.push_back(id);
                    walkNodes[n].links.push_back(id);
                    (L.kind == WL_ZEBRA ? siteZebras : sitePaths)++;
                }
                prev = n;
            }
        }
    }

    // ---- spatial hashes
    hashRes = rn.hashRes;
    laneHash.assign((size_t)hashRes * hashRes, {});
    walkHash.assign((size_t)hashRes * hashRes, {});
    const float C = World::RoadNetwork::kHashCell;
    auto cellOf = [&](vec2 p, int& x, int& y) {
        x = Clamp((int)((p.x + World::kWorldHalf) / C), 0, hashRes - 1);
        y = Clamp((int)((p.y + World::kWorldHalf) / C), 0, hashRes - 1);
    };
    auto addToHash = [&](std::vector<std::vector<int>>& H, int id, vec2 p, float r) {
        int x0, y0, x1, y1;
        cellOf(p - vec2(r), x0, y0);
        cellOf(p + vec2(r), x1, y1);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                std::vector<int>& v = H[(size_t)y * hashRes + x];
                if (v.empty() || v.back() != id) v.push_back(id);
            }
    };
    for (int li = 0; li < (int)lanes.size(); li++) {
        const Lane& l = lanes[li];
        float len = l.u1 - l.u0;
        int n = Max(1, (int)ceilf(len / 8.f));
        for (int k = 0; k <= n; k++) addToHash(laneHash, li, lanePos(li, l.u0 + len * k / n).xy(), 6.f);
    }
    for (int wi = 0; wi < (int)walkLinks.size(); wi++) {
        const WalkLink& w = walkLinks[wi];
        int n = Max(1, (int)ceilf(w.length / 8.f));
        for (int k = 0; k <= n; k++) addToHash(walkHash, wi, walkPos(wi, w.length * k / n, 0.f, true).xy(), 5.f);
    }
    spotHash.assign((size_t)hashRes * hashRes, {});
    for (int si = 0; si < (int)spots.size(); si++) addToHash(spotHash, si, spots[si].pos.xy(), 0.f);
    for (auto& v : laneHash) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    }
    for (auto& v : walkHash) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    }
    // ---- the lanes each zebra crosses: where on the lane the crossing starts (traffic_core.cpp stops short of it while
    //      somebody is on it)
    for (int wi = 0; wi < (int)walkLinks.size(); wi++) {
        const WalkLink& w = walkLinks[wi];
        if (w.kind != WL_ZEBRA) continue;
        vec2 a = walkNodes[w.a].p.xy(), b = walkNodes[w.b].p.xy();
        int n = Max(2, (int)(w.length / 0.5f));
        int laneSeen[16];
        float laneU[16];
        int nl = 0;
        for (int k = 0; k <= n; k++) {
            vec2 q = lerp(a, b, k / (float)n);
            float u = 0.f, lat = 0.f;
            int ln = nearestLane(q, vec2(0.f), 4.f, &u, &lat);
            if (ln < 0 || fabsf(lat) > lanes[ln].width * 0.5f || u < lanes[ln].u0 || u > lanes[ln].u1) continue;
            int j = 0;
            for (; j < nl && laneSeen[j] != ln; j++) {
            }
            if (j == nl && nl < 16) {
                laneSeen[nl] = ln;
                laneU[nl] = u;
                nl++;
            } else if (j < nl) {
                laneU[j] = Min(laneU[j], u);
            }
        }
        for (int j = 0; j < nl; j++) {
            LaneZebra z;
            z.u = laneU[j] - w.halfWidth - 0.3f;   // (the stripes' near edge)
            z.link = wi;
            std::vector<LaneZebra>& v = lanes[laneSeen[j]].zebras;
            v.push_back(z);
            std::sort(v.begin(), v.end(), [](const LaneZebra& p, const LaneZebra& q) { return p.u < q.u; });
        }
    }
    if (sitePaths + siteZebras > 0) LOG("Lane graph: %d plaza walkways and %d zebra crossings from the sites", sitePaths, siteZebras);
    buildSeconds = TimeSeconds() - t0;
    size_t cpts = 0;
    for (auto& c : conns) cpts += c.pts.size();
    LOG("Lane graph: %zu lanes, %zu connectors (%zu pts), %zu walk nodes, %zu walk links, dead-end turning circles moved %d / "
        "obstructed %d (%.2f s)", lanes.size(), conns.size(), cpts, walkNodes.size(), walkLinks.size(), uturnPulled, uturnBlockedCount, buildSeconds);
}

}  // namespace AI
