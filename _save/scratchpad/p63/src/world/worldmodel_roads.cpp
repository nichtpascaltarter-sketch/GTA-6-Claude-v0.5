// The world model's roads compile (worldmodel.h): the model's nodes and edges to today's RoadNetwork.
#include "worldmodel.h"
#include <cstdio>

namespace World {
namespace Hand {

namespace worldmodel_roads_detail {

// FNV-1a over the id, then the engine's hash: an edge's seed follows its id, not its place in the files
u32 idSeed(const std::string& id) {
    u32 h = 2166136261u;
    for (unsigned char c : id) h = (h ^ c) * 16777619u;
    return hash32(h);
}

std::string xy(vec2 p) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f,%.2f", p.x, p.y);
    return buf;
}

std::string metres(float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", v);
    return buf;
}

// The paved width beyond the lanes on one side: shoulder and parking as written (they add up), else the class's
float kerbSide(float shoulder, float parking, bool written, float classWidth) {
    return written ? Max(shoulder, 0.f) + Max(parking, 0.f) : classWidth;
}

float profileAt(const std::vector<vec2>& p, float s) {
    if (s <= p.front().x) return p.front().y;
    for (size_t k = 1; k < p.size(); k++)
        if (s <= p[k].x) return Lerp(p[k - 1].y, p[k].y, (s - p[k - 1].x) / (p[k].x - p[k - 1].x));
    return p.back().y;
}

}  // namespace worldmodel_roads_detail

bool compileRoads(const Model& model, RoadNetwork& out, RoadIndex& index, std::vector<Error>& errors) {
    using namespace worldmodel_roads_detail;
    const size_t errorsBefore = errors.size();
    out = RoadNetwork();
    index = RoadIndex();
    const auto fail = [&errors](const Src& s, const char* code, const std::string& msg) { errors.push_back(Error{s, code, msg}); };

    out.nodes.resize(model.nodes.size());
    for (size_t i = 0; i < model.nodes.size(); i++) {
        out.nodes[i].p = model.nodes[i].at;
        out.nodes[i].z = model.nodes[i].z;
        index.nodeOf.push_back((int)i);
        index.roadNode.push_back((int)i);
    }
    index.roadEdge.assign(model.edges.size(), -1);
    for (size_t i = 0; i < model.edges.size(); i++) {
        const Edge& m = model.edges[i];
        const std::string what = "edge " + m.id;
        if (m.from.target < 0 || m.to.target < 0) {
            const Ref& r = m.from.target < 0 ? m.from : m.to;
            fail(r.src.file >= 0 ? r.src : m.src, "road.node", what + ": " + (m.from.target < 0 ? "from=" : "to=") + r.text + " names no node");
            continue;
        }
        if (m.from.target == m.to.target) {
            fail(m.src, "road.loop", what + ": from= and to= are the same node; a loop needs a node of its own along it");
            continue;
        }
        const Node& a = model.nodes[(size_t)m.from.target];
        const Node& b = model.nodes[(size_t)m.to.target];
        bool ok = m.shape.size() >= 2;
        for (size_t k = 1; ok && k < m.shape.size(); k++) ok = length(m.shape[k] - m.shape[k - 1]) > 0.005f;
        if (!ok) {
            fail(m.src, "road.shape", what + ": its shape needs at least 2 points, no two of them in one place");
            continue;
        }
        if (length(m.shape.front() - a.at) > 0.02f || length(m.shape.back() - b.at) > 0.02f) {
            fail(m.src, "road.shape", what + ": its shape runs from " + xy(m.shape.front()) + " to " + xy(m.shape.back()) + ", not from " + a.id + " (" + xy(a.at) +
                                          ") to " + b.id + " (" + xy(b.at) + ")");
            continue;
        }
        // heights by the distance along the shape in plan: the profile's, else straight from node to node. The points are
        // the shape's, and one more wherever the profile bends between two of them, so that the polyline carries the
        // profile (the road mesher and the grading run straight from point to point).
        std::vector<float> s(m.shape.size(), 0.f);
        for (size_t k = 1; k < m.shape.size(); k++) s[k] = s[k - 1] + length(m.shape[k] - m.shape[k - 1]);
        const float planLength = s.back();
        if (!m.profile.empty()) {
            bool rising = m.profile.size() >= 2;
            for (size_t k = 1; rising && k < m.profile.size(); k++) rising = m.profile[k].x > m.profile[k - 1].x;
            if (!rising || fabsf(m.profile.front().x) > 0.01f || fabsf(m.profile.back().x - planLength) > 0.5f) {
                fail(m.src, "road.profile", what + ": its profile's s should rise from 0 to the shape's length, " + metres(planLength) + " m");
                continue;
            }
            if (fabsf(m.profile.front().y - a.z) > 0.05f || fabsf(m.profile.back().y - b.z) > 0.05f) {
                fail(m.src, "road.profile", what + ": its profile should start at " + a.id + "'s z (" + metres(a.z) + ") and end at " + b.id + "'s (" + metres(b.z) + "), not " +
                                                metres(m.profile.front().y) + " and " + metres(m.profile.back().y));
                continue;
            }
        }
        std::vector<vec2> plan;
        std::vector<float> planS;
        for (size_t k = 0; k < m.shape.size(); k++) {
            plan.push_back(m.shape[k]);
            planS.push_back(s[k]);
            if (k + 1 == m.shape.size()) break;
            for (const vec2& pr : m.profile)
                if (pr.x > s[k] + 0.05f && pr.x < s[k + 1] - 0.05f) {
                    plan.push_back(lerp(m.shape[k], m.shape[k + 1], (pr.x - s[k]) / (s[k + 1] - s[k])));
                    planS.push_back(pr.x);
                }
        }
        std::vector<float> z(plan.size());
        for (size_t k = 0; k < z.size(); k++) z[k] = m.profile.empty() ? Lerp(a.z, b.z, planS[k] / planLength) : profileAt(m.profile, planS[k]);
        z.front() = a.z;   // (every edge meets its nodes at their height)
        z.back() = b.z;

        RoadEdge e;
        e.n0 = m.from.target;
        e.n1 = m.to.target;
        e.cls = m.cls;
        e.flags = m.flags;
        if (!m.decks.empty()) e.flags |= RF_BRIDGE;
        if (m.lanesF == 0 || m.lanesB == 0) e.flags |= RF_ONEWAY;
        if (m.cls == RC_DIRT) e.flags |= RF_UNPAVED;
        e.lanesF = (u8)m.lanesF;
        e.lanesB = (u8)m.lanesB;
        const RoadClassInfo& ri = roadInfo(m.cls);
        const float laneW = m.laneWidth > 0.f ? m.laneWidth : ri.laneWidth;
        const float median = m.median >= 0.f ? m.median : ri.median;
        const bool kerbWritten = m.shoulderL >= 0.f || m.parkingL >= 0.f;   // (a written compound sets both sides)
        const float kerbW = Max(kerbSide(m.shoulderL, m.parkingL, kerbWritten, ri.shoulder), kerbSide(m.shoulderR, m.parkingR, kerbWritten, ri.shoulder));
        e.halfWidth = laneW * (e.lanesF + e.lanesB) * 0.5f + median * 0.5f + kerbW;
        e.sidewalk = m.sidewalkL >= 0.f ? Max(m.sidewalkL, m.sidewalkR) : ri.sidewalk;
        if (e.sidewalk <= 0.f) e.flags |= RF_NOSIDEWALK;
        for (size_t k = 0; k < plan.size(); k++) e.pts.push_back(vec3(plan[k].x, plan[k].y, z[k]));
        e.dist.assign(e.pts.size(), 0.f);
        for (size_t k = 1; k < e.pts.size(); k++) e.dist[k] = e.dist[k - 1] + length(e.pts[k] - e.pts[k - 1]);
        e.length = e.dist.back();
        e.seed = idSeed(m.id);
        e.name = m.name;
        index.roadEdge[i] = (int)out.edges.size();
        index.edgeOf.push_back((int)i);
        out.edges.push_back(e);
    }
    for (size_t i = 0; i < out.edges.size(); i++) {
        out.nodes[(size_t)out.edges[i].n0].edges.push_back((int)i);
        out.nodes[(size_t)out.edges[i].n1].edges.push_back((int)i);
        if (out.edges[i].cls == RC_HIGHWAY) out.nodes[(size_t)out.edges[i].n0].highway = out.nodes[(size_t)out.edges[i].n1].highway = true;
    }
    out.finishJunctions();
    // control= where written. The lane graph knows three: priority by road class (0), stop signs on every approach (1)
    // and signals (2). A yield, or a stop on only some approaches, is priority until it reads the node's lists.
    for (size_t i = 0; i < model.nodes.size(); i++) {
        const Node& n = model.nodes[i];
        RoadNode& rnode = out.nodes[i];
        if (n.control == Node::CTL_NONE || n.control == Node::CTL_YIELD) {
            rnode.control = 0;
        } else if (n.control == Node::CTL_SIGNAL) {
            rnode.control = 2;
        } else if (n.control == Node::CTL_STOP) {
            bool every = true;   // stops=all, no list, or every leg in it
            if (!n.stopsAll && !n.stops.empty())
                for (int ei : rnode.edges) {
                    bool listed = false;
                    for (const Ref& r : n.stops) listed = listed || r.target == index.edgeOf[(size_t)ei];
                    every = every && listed;
                }
            rnode.control = every ? 1 : 0;
        }
    }
    out.buildHash();
    return errors.size() == errorsBefore;
}

}  // namespace Hand
}  // namespace World
