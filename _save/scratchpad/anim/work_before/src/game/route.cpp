// A* routing over the road graph (GPS line to the waypoint / mission objective).
#include "gameworld.h"
#include <queue>

namespace Game {

bool GameWorld::computeRoute(vec2 from, vec2 to, bool driving, std::vector<vec2>& out) const {
    out.clear();
    float sA = 0, sB = 0;
    int eA = roads->nearestEdge(from, 400.f, &sA);
    int eB = roads->nearestEdge(to, 800.f, &sB);
    if (eA < 0 || eB < 0) {
        out.push_back(from);
        out.push_back(to);
        return false;
    }
    const World::RoadEdge& A = roads->edges[eA];
    const World::RoadEdge& B = roads->edges[eB];
    if (eA == eB) {
        out.push_back(from);
        int k0 = 0, k1 = 0;
        float acc = 0;
        for (size_t k = 0; k + 1 < A.pts.size(); k++) {
            float l = length(A.pts[k + 1].xy() - A.pts[k].xy());
            if (acc <= sA) k0 = (int)k;
            if (acc <= sB) k1 = (int)k;
            acc += l;
        }
        out.push_back(A.posAt(sA).xy());
        if (k0 < k1)
            for (int k = k0 + 1; k <= k1; k++) out.push_back(A.pts[k].xy());
        else
            for (int k = k0; k > k1; k--) out.push_back(A.pts[k].xy());
        out.push_back(A.posAt(sB).xy());
        out.push_back(to);
        return true;
    }
    int N = (int)roads->nodes.size();
    std::vector<float> g(N, 1e30f);
    std::vector<int> prevEdge(N, -1), prevNode(N, -1);
    std::vector<u8> closed(N, 0);
    typedef std::pair<float, int> QE;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    auto h = [&](int n) { return length(roads->nodes[n].p - B.posAt(sB).xy()); };
    auto canTravel = [&](const World::RoadEdge& e, int fromNode) {
        if (!driving) return true;
        if (!(e.flags & World::RF_ONEWAY)) return true;
        return e.n0 == fromNode;
    };
    // seed with both ends of the start edge
    {
        int ends[2] = {A.n0, A.n1};
        float cost[2] = {sA, A.length - sA};
        for (int i = 0; i < 2; i++) {
            if (driving && (A.flags & World::RF_ONEWAY) && i == 0) continue;
            int n = ends[i];
            if (cost[i] < g[n]) {
                g[n] = cost[i];
                prevEdge[n] = eA;
                prevNode[n] = -1;
                open.push(QE(g[n] + h(n), n));
            }
        }
    }
    int goalNode = -1;
    float best = 1e30f;
    int expanded = 0;
    while (!open.empty() && expanded < 60000) {
        QE top = open.top();
        open.pop();
        int n = top.second;
        if (closed[n]) continue;
        closed[n] = 1;
        expanded++;
        if (top.first >= best) break;
        // reaching an end of the goal edge
        if (n == B.n0 || n == B.n1) {
            float rest = n == B.n0 ? sB : B.length - sB;
            bool ok = !driving || !(B.flags & World::RF_ONEWAY) || n == B.n0;
            if (ok && g[n] + rest < best) {
                best = g[n] + rest;
                goalNode = n;
            }
        }
        for (int ei : roads->nodes[n].edges) {
            const World::RoadEdge& e = roads->edges[ei];
            if (!canTravel(e, n)) continue;
            int m = e.n0 == n ? e.n1 : e.n0;
            float c = g[n] + e.length * (e.cls == World::RC_HIGHWAY ? 0.6f : (e.cls == World::RC_DIRT ? 2.f : 1.f));
            if (c < g[m]) {
                g[m] = c;
                prevEdge[m] = ei;
                prevNode[m] = n;
                open.push(QE(c + h(m) * 0.6f, m));
            }
        }
    }
    if (goalNode < 0) {
        out.push_back(from);
        out.push_back(to);
        return false;
    }
    // reconstruct node chain
    std::vector<int> chainEdges, chainNodes;
    for (int n = goalNode; n >= 0 && prevNode[n] >= 0; n = prevNode[n]) {
        chainEdges.push_back(prevEdge[n]);
        chainNodes.push_back(n);
    }
    std::reverse(chainEdges.begin(), chainEdges.end());
    std::reverse(chainNodes.begin(), chainNodes.end());
    out.push_back(from);
    out.push_back(A.posAt(sA).xy());
    int startNode = chainNodes.empty() ? goalNode : prevNode[chainNodes.front()];
    if (startNode < 0) startNode = goalNode;
    out.push_back(roads->nodes[startNode].p);
    for (size_t i = 0; i < chainEdges.size(); i++) {
        const World::RoadEdge& e = roads->edges[chainEdges[i]];
        int toNode = chainNodes[i];
        bool fwd = e.n1 == toNode;
        if (fwd)
            for (size_t k = 1; k < e.pts.size(); k++) out.push_back(e.pts[k].xy());
        else
            for (int k = (int)e.pts.size() - 2; k >= 0; k--) out.push_back(e.pts[k].xy());
    }
    out.push_back(B.posAt(sB).xy());
    out.push_back(to);
    return true;
}

void GameWorld::updateGps(float dt) {
    gpsRecalcTimer -= dt;
    Ped* pl = playerPed();
    if (!pl) return;
    if (gpsRecalcTimer > 0.f) return;
    gpsRecalcTimer = 1.0f;
    vec2 p = pl->pos.toVec3().xy();
    bool driving = playerVehicle() >= 0;
    if (hasWaypoint) {
        if (length(p - waypoint) < 25.f) {
            hasWaypoint = false;
            gpsRoute.clear();
        } else computeRoute(p, waypoint, driving, gpsRoute);
    }
    if (missionTargetActive) computeRoute(p, missionTarget, driving, missionRoute);
    else missionRoute.clear();
}

}  // namespace Game
