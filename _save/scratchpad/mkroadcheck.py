SP='/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad'
s=open('tools/worldcheck.cpp').read()
s=s.replace('#include "native_stubs.cpp"','#include "tools/native_stubs.cpp"').replace('#include "../src/','#include "src/')
s=s.replace('    bs.generate(map, roads);','    bool roadsOnly = getenv("ROADSONLY") != nullptr;\n    if (!roadsOnly) bs.generate(map, roads);')
s=s.replace('    for (int cy = 0; cy < cps; cy++)\n        for (int cx = 0; cx < cps; cx++) {\n            vec2 o = cellOrigin(cx, cy);','    for (int cy = 0; cy < cps && !roadsOnly; cy++)\n        for (int cx = 0; cx < cps; cx++) {\n            vec2 o = cellOrigin(cx, cy);')
a='''    for (size_t ni = 0; ni < roads.nodes.size(); ni++) {
        const RoadNode& nd = roads.nodes[ni];
        if (nd.edges.size() != 1) continue;'''
assert s.count(a)==1
s=s.replace(a,a.replace('ni < roads.nodes.size();','ni < roads.nodes.size() && !roadsOnly;'))
open(SP+'/roadcheck.cpp','w').write(s)
