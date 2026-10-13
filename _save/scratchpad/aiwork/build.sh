#!/bin/sh
# Builds the traffic harness in two parts (world object cached). "world" rebuilds the world part,
# FIXROADS=1 builds it against a copy of roads.cpp with the surfaceHeight sidewalk-bump fix.
set -e
cd /home/user/GTA-6-Claude-v0.5
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
FLAGS="-O2 -g -std=c++17 -I src -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces -Wno-class-memaccess -Wno-unused-result -Wno-unused-but-set-variable"
WFLAGS=""
if [ "$FIXROADS" = "1" ]; then
  python3 - <<'PY'
import re
src=open('/home/user/GTA-6-Claude-v0.5/src/world/roads.cpp').read()
src=src.replace('#include "roads.h"','#include "/home/user/GTA-6-Claude-v0.5/src/world/roads.h"').replace('#include "../core/noise.h"','#include "/home/user/GTA-6-Claude-v0.5/src/core/noise.h"')
old_start=src.index('bool RoadNetwork::surfaceHeight')
old_end=src.index('bool RoadNetwork::nearRoad')
fixed='''bool RoadNetwork::surfaceHeight(vec2 p, float* z, float maxZ) const {
    thread_local std::vector<int> cand;
    cand.clear();
    edgesInRect(p - vec2(1.f), p + vec2(1.f), cand);
    bool found = false;
    float bestZ = -1e9f;
    for (int ei : cand) {
        const RoadEdge& e = edges[ei];
        float bd = 1e30f, bz = 0.f;
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            float t;
            float d = distPointSegment2D(p, e.pts[k].xy(), e.pts[k + 1].xy(), &t);
            if (d < bd) { bd = d; bz = Lerp(e.pts[k].z, e.pts[k + 1].z, t); }
        }
        if (bd <= e.halfWidth + e.sidewalk) {
            float zz = bz + (bd > e.halfWidth ? 0.15f : 0.f);
            if (zz <= maxZ && zz > bestZ) { bestZ = zz; found = true; }
        }
    }
    for (int ei : cand) {
        const RoadEdge& e = edges[ei];
        const RoadNode* ns[2] = {&nodes[e.n0], &nodes[e.n1]};
        for (auto* n : ns)
            if (n->radius > 0 && length(p - n->p) < n->radius && n->z <= maxZ && n->z > bestZ) { bestZ = n->z; found = true; }
    }
    if (found) *z = bestZ;
    return found;
}

'''
src=src[:old_start]+fixed+src[old_end:]
open('/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork/roads_fixed.cpp','w').write(src)
PY
  WFLAGS="-DTS_ROADS_CPP=\"$AW/roads_fixed.cpp\""
fi
if [ ! -f $AW/ts_world.o ] || [ "$1" = "world" ]; then
  nice g++ $FLAGS $WFLAGS -DTS_SPLIT -DTS_PART_WORLD -c tools/traffic_sim.cpp -o $AW/ts_world.o
fi
nice g++ $FLAGS -DTS_SPLIT -DTS_PART_AI -c tools/traffic_sim.cpp -o $AW/ts_ai.o
g++ $AW/ts_world.o $AW/ts_ai.o -o $AW/traffic_sim -lpthread
echo built
