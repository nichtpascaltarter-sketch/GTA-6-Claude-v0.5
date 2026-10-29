// The road network's sections of world.bin (roadsbin.h).
#include "roadsbin.h"
#include <cstdio>
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

void writeRoadOverrides(const RoadOverrides& ov, WorldBinWriter& out) {
    if (ov.empty()) return;
    ByteWriter w;
    w.putU32(kRoadOverridesSectionVersion);
    w.putU32((u32)ov.edges.size());
    for (const RoadOverrides::EdgeExtra& x : ov.edges) {
        const float f[9] = {x.speed, x.laneWidth, x.median, x.kerbL, x.kerbR, x.parkingL, x.parkingR, x.sidewalkL, x.sidewalkR};
        for (float v : f) w.putF32(v);
        w.putU32(x.flags);
    }
    w.putU32((u32)ov.nodes.size());
    for (const RoadOverrides::NodeRule& n : ov.nodes) {
        w.putU32((u32)n.node);
        w.putU32((u32)n.control | ((u32)n.flags << 8));
        w.putF32(n.bulb);
        for (const std::vector<int>* list : {&n.stops, &n.yields, &n.noRedTurn}) {
            w.putU32((u32)list->size());
            for (int e : *list) w.putU32((u32)e);
        }
    }
    out.addCompressed("roads.overrides", w.bytes);
}

bool readRoadOverrides(const WorldBinView& bin, const RoadNetwork& roads, RoadOverrides& out, std::string* error) {
    using namespace roadsbin_detail;
    out = RoadOverrides();   // (left empty on failure: never half the lists)
    if (!bin.find("roads.overrides")) return true;
    std::vector<u8> bytes;
    if (!bin.bytes("roads.overrides", bytes)) return fail(error, "world.bin's roads.overrides section is damaged");
    ByteReader r(bytes.data(), bytes.size());
    const u32 version = r.getU32();
    if (version != kRoadOverridesSectionVersion)
        return fail(error, "world.bin's roads.overrides section is version " + std::to_string(version) + ", not " + std::to_string(kRoadOverridesSectionVersion));
    RoadOverrides ov;
    const u32 n = r.getU32();
    if (r.bad || n > (u32)(r.end - r.p) / 40u) return fail(error, "world.bin's roads.overrides section is truncated (edges)");
    if (n != 0 && (size_t)n != roads.edges.size())
        return fail(error, "world.bin's roads.overrides section is for " + std::to_string(n) + " edges, and its roads have " + std::to_string(roads.edges.size()));
    ov.edges.resize(n);
    for (RoadOverrides::EdgeExtra& x : ov.edges) {
        x.speed = r.getF32();
        x.laneWidth = r.getF32();
        x.median = r.getF32();
        x.kerbL = r.getF32();
        x.kerbR = r.getF32();
        x.parkingL = r.getF32();
        x.parkingR = r.getF32();
        x.sidewalkL = r.getF32();
        x.sidewalkR = r.getF32();
        x.flags = (u8)(r.getU32() & 255u);
    }
    const u32 rules = r.getU32();
    if (r.bad || rules > (u32)(r.end - r.p) / 24u) return fail(error, "world.bin's roads.overrides section is truncated (node rules)");
    ov.nodes.resize(rules);
    for (u32 i = 0; i < rules; i++) {
        RoadOverrides::NodeRule& x = ov.nodes[i];
        x.node = (int)r.getU32();
        const u32 bits = r.getU32();
        x.control = (u8)(bits & 255u);
        x.flags = (u8)((bits >> 8) & 255u);
        x.bulb = r.getF32();
        if (r.bad || x.node < 0 || (size_t)x.node >= roads.nodes.size() || x.control > RoadOverrides::CTRL_SIGNAL)
            return fail(error, "world.bin's roads.overrides section: node rule " + std::to_string(i) + " names a node or control that isn't there");
        for (std::vector<int>* list : {&x.stops, &x.yields, &x.noRedTurn}) {
            const u32 k = r.getU32();
            if (r.bad || k > (u32)(r.end - r.p) / 4u) return fail(error, "world.bin's roads.overrides section is truncated (node rule " + std::to_string(i) + ")");
            list->resize(k);
            for (int& e : *list) {
                e = (int)r.getU32();
                if (e < 0 || (size_t)e >= roads.edges.size())
                    return fail(error, "world.bin's roads.overrides section: node rule " + std::to_string(i) + " names an edge that isn't there");
            }
        }
    }
    if (!r.done()) return fail(error, "world.bin's roads.overrides section has bytes past its end, or is truncated");
    out = std::move(ov);
    return true;
}

void applyRoadSides(const RoadOverrides& ov, RoadNetwork& roads) {
    // each dead end's turning circle as written; the lookup table then reaches as far as the widest bulb
    bool bulbs = false;
    for (const RoadOverrides::NodeRule& r : ov.nodes)
        if (r.node >= 0 && (size_t)r.node < roads.nodes.size() && r.bulb > 0.f) {
            roads.nodes[(size_t)r.node].bulb = r.bulb;
            bulbs = true;
        }
    if (bulbs && !roads.hash.empty()) roads.buildHash();
    if (ov.edges.size() != roads.edges.size()) return;
    for (size_t i = 0; i < roads.edges.size(); i++) {
        RoadEdge& e = roads.edges[i];
        const RoadOverrides::EdgeExtra& x = ov.edges[i];
        for (int k = 0; k < 2; k++) {
            const int side = k == 0 ? -1 : 1;
            const float line = roadKerbLine(x.laneWidth, x.median, e.lanesF, e.lanesB, k == 0 ? x.kerbL : x.kerbR, side);
            e.kerbTrim[k] = Max(e.halfWidth - line, 0.f);
            e.walkTrim[k] = Max(e.sidewalk - (k == 0 ? x.sidewalkL : x.sidewalkR), 0.f);
        }
    }
}

bool loadRoadsFile(const char* path, RoadNetwork& roads, RoadOverrides& ov, std::string* error, std::vector<std::string>* nodeIds,
                   std::vector<std::string>* edgeIds) {
    using namespace roadsbin_detail;
    roads = RoadNetwork();
    ov = RoadOverrides();
    if (nodeIds) nodeIds->clear();
    if (edgeIds) edgeIds->clear();
    const std::string name = path ? path : "(no path)";
    FILE* f = path ? fopen(path, "rb") : nullptr;
    if (!f) return fail(error, "can't open " + name);
    std::vector<u8> data;
    long size = -1;
    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
    bool ok = size >= 0 && fseek(f, 0, SEEK_SET) == 0;
    if (ok) {
        data.resize((size_t)size);
        ok = data.empty() || fread(data.data(), 1, data.size(), f) == data.size();
    }
    fclose(f);
    if (!ok) return fail(error, "can't read " + name);
    WorldBinView view;
    std::string why;
    if (!view.open(data.data(), data.size(), &why)) return fail(error, name + ": " + why);
    RoadNetwork net;
    RoadOverrides o;
    if (!readRoads(view, net, nodeIds, edgeIds, &why) || !readRoadOverrides(view, net, o, &why)) {
        if (nodeIds) nodeIds->clear();
        if (edgeIds) edgeIds->clear();
        return fail(error, name + ": " + why);
    }
    applyRoadSides(o, net);
    roads = std::move(net);
    ov = std::move(o);
    return true;
}

}  // namespace World
