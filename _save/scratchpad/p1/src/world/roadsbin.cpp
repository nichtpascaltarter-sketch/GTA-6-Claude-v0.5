// The road network's sections of world.bin (roadsbin.h).
#include "roadsbin.h"
#include <utility>

namespace World {

namespace roadsbin_detail {

bool fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

void putIds(ByteWriter& w, const std::vector<std::string>& ids) {
    w.putU32((u32)ids.size());
    for (const std::string& s : ids) w.putString(s);
}

bool getIds(ByteReader& r, std::vector<std::string>& ids) {
    ids.clear();
    const u32 n = r.getU32();
    if (r.bad || n > (u32)(r.end - r.p) / 4u) return false;   // (each id takes at least its length)
    ids.reserve(n);
    for (u32 i = 0; i < n && !r.bad; i++) ids.push_back(r.getString());
    return !r.bad;
}

}  // namespace roadsbin_detail

void writeRoads(const RoadNetwork& rn, const std::vector<std::string>* nodeIds, const std::vector<std::string>* edgeIds, WorldBinWriter& out) {
    using namespace roadsbin_detail;
    ByteWriter w;
    w.putU32(kRoadsSectionVersion);
    w.putU32((u32)rn.nodes.size());
    for (const RoadNode& n : rn.nodes) {
        w.putF32(n.p.x);
        w.putF32(n.p.y);
        w.putF32(n.z);
        w.putF32(n.radius);
        w.putU32((u32)n.control | (n.highway ? (1u << 8) : 0u));
    }
    w.putU32((u32)rn.edges.size());
    for (const RoadEdge& e : rn.edges) {
        w.putU32((u32)e.n0);
        w.putU32((u32)e.n1);
        w.putU32((u32)e.cls | ((u32)e.flags << 8) | ((u32)e.lanesF << 16) | ((u32)e.lanesB << 24));
        w.putF32(e.halfWidth);
        w.putF32(e.sidewalk);
        w.putF32(e.cut0);
        w.putF32(e.cut1);
        w.putU32(e.seed);
        w.putString(e.name);
        w.putU32((u32)e.pts.size());
        for (const vec3& p : e.pts) {
            w.putF32(p.x);
            w.putF32(p.y);
            w.putF32(p.z);
        }
    }
    out.addCompressed("roads", w.bytes);
    if (nodeIds && edgeIds) {
        ByteWriter ids;
        putIds(ids, *nodeIds);
        putIds(ids, *edgeIds);
        out.addCompressed("roads.ids", ids.bytes);
    }
}

bool readRoads(const WorldBinView& bin, RoadNetwork& out, std::vector<std::string>* nodeIds, std::vector<std::string>* edgeIds, std::string* error) {
    using namespace roadsbin_detail;
    out = RoadNetwork();   // (left empty on failure: never half a network)
    RoadNetwork net;
    if (nodeIds) nodeIds->clear();
    if (edgeIds) edgeIds->clear();
    std::vector<u8> bytes;
    if (!bin.bytes("roads", bytes)) return fail(error, "world.bin has no roads section, or it is damaged");
    ByteReader r(bytes.data(), bytes.size());
    const u32 version = r.getU32();
    if (version != kRoadsSectionVersion) return fail(error, "world.bin's roads section is version " + std::to_string(version) + ", not " + std::to_string(kRoadsSectionVersion));
    const u32 nodeCount = r.getU32();
    if (r.bad || nodeCount > (u32)(r.end - r.p) / 20u) return fail(error, "world.bin's roads section is truncated (nodes)");
    net.nodes.resize(nodeCount);
    for (RoadNode& n : net.nodes) {
        n.p.x = r.getF32();
        n.p.y = r.getF32();
        n.z = r.getF32();
        n.radius = r.getF32();
        const u32 bits = r.getU32();
        n.control = (u8)(bits & 255u);
        n.highway = ((bits >> 8) & 1u) != 0u;
    }
    const u32 edgeCount = r.getU32();
    if (r.bad || edgeCount > (u32)(r.end - r.p) / 40u) return fail(error, "world.bin's roads section is truncated (edges)");
    net.edges.resize(edgeCount);
    for (u32 i = 0; i < edgeCount; i++) {
        RoadEdge& e = net.edges[i];
        e.n0 = (int)r.getU32();
        e.n1 = (int)r.getU32();
        const u32 bits = r.getU32();
        e.cls = (RoadClass)(bits & 255u);
        e.flags = (u8)((bits >> 8) & 255u);
        e.lanesF = (u8)((bits >> 16) & 255u);
        e.lanesB = (u8)((bits >> 24) & 255u);
        e.halfWidth = r.getF32();
        e.sidewalk = r.getF32();
        e.cut0 = r.getF32();
        e.cut1 = r.getF32();
        e.seed = r.getU32();
        e.name = r.getString();
        const u32 pointCount = r.getU32();
        if (r.bad || pointCount < 2u || pointCount > (u32)(r.end - r.p) / 12u) return fail(error, "world.bin's roads section: edge " + std::to_string(i) + " is damaged");
        if (e.n0 < 0 || e.n1 < 0 || (u32)e.n0 >= nodeCount || (u32)e.n1 >= nodeCount || e.cls >= RC_COUNT)
            return fail(error, "world.bin's roads section: edge " + std::to_string(i) + " names a node or class that isn't there");
        e.pts.resize(pointCount);
        for (vec3& p : e.pts) {
            p.x = r.getF32();
            p.y = r.getF32();
            p.z = r.getF32();
        }
        // as compileRoads measures them
        e.dist.assign(e.pts.size(), 0.f);
        for (size_t k = 1; k < e.pts.size(); k++) e.dist[k] = e.dist[k - 1] + length(e.pts[k] - e.pts[k - 1]);
        e.length = e.dist.back();
    }
    if (!r.done()) return fail(error, "world.bin's roads section has bytes past its end, or is truncated");
    for (size_t i = 0; i < net.edges.size(); i++) {
        net.nodes[(size_t)net.edges[i].n0].edges.push_back((int)i);
        net.nodes[(size_t)net.edges[i].n1].edges.push_back((int)i);
    }
    net.buildHash();
    if ((nodeIds || edgeIds) && bin.find("roads.ids")) {
        std::vector<u8> idBytes;
        std::vector<std::string> nodes, edges;
        if (!bin.bytes("roads.ids", idBytes)) return fail(error, "world.bin's roads.ids section is damaged");
        ByteReader ri(idBytes.data(), idBytes.size());
        if (!getIds(ri, nodes) || !getIds(ri, edges) || !ri.done() || nodes.size() != net.nodes.size() || edges.size() != net.edges.size())
            return fail(error, "world.bin's roads.ids section doesn't match its roads");
        if (nodeIds) *nodeIds = std::move(nodes);
        if (edgeIds) *edgeIds = std::move(edges);
    }
    out = std::move(net);
    return true;
}

}  // namespace World
