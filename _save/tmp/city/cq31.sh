#!/bin/bash
# stage 0 step 1 (the terrain bake as data) checked on its own tree: $S/s0step1 = HEAD + batch 17 + step 1's files.
# worldcheck (procedural world unchanged: same output as dev28's), the clang MSVC-safety scan over the bake tool (sites.cpp,
# gridcodec.cpp), MinGW syntax of the game and of gridcodec.cpp. After s0a.sh; one compile at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/s0.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
until grep -q "^s0a done" /tmp/city/s0.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/s0a.sh" > /dev/null; do sleep 20; done
st "cq31 start"
rm -rf $S/s0step1 && mkdir -p $S/s0step1
(cd /home/user/GTA-6-Claude-v0.5 && git archive HEAD) | tar -x -C $S/s0step1
for f in worldtypes.h sitegeo.cpp landmarks.cpp leisure.cpp airport.cpp port.cpp rural.cpp buildmesh.cpp; do cp $S/dev28/src/world/$f $S/s0step1/src/world/$f; done
for f in src/world/sites.h src/world/sites.cpp src/world/gridcodec.h src/world/gridcodec.cpp tools/terrainbake.cpp; do cp $S/s0/$f $S/s0step1/$f; done
mkdir -p $S/s0step1/world && cp -r $S/s0/world/terrain $S/s0step1/world/
st "s0step1 tree: $(ls $S/s0step1/world/terrain | tr '\n' ' ')"
wmem 2000
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -DWC_BEACONS -I$S/s0step1 worldcheck.cpp -o wc_s0step1 -lpthread > /tmp/city/wc_s0step1_build.log 2>&1; st "wc build rc=$?"
wmem 1200
cd /home/user/GTA-6-Claude-v0.5 && FP_BEACONS=1 nice -n 5 $S/wc/wc_s0step1 20 > $S/wc/s0step1.txt 2>&1; st "wc run exit $?"
rm -f $S/wc/wc_s0step1
sed -E 's/[0-9]+\.[0-9]+ s\b/T s/g' $S/wc/dev28.txt > /tmp/city/wc_a.txt
sed -E 's/[0-9]+\.[0-9]+ s\b/T s/g' $S/wc/s0step1.txt > /tmp/city/wc_b.txt
if cmp -s /tmp/city/wc_a.txt /tmp/city/wc_b.txt; then st "worldcheck s0step1 = dev28 (timings aside), $(grep -c 'worldcheck: passed' $S/wc/s0step1.txt) passed"; else st "worldcheck s0step1 DIFFERS from dev28: $(diff /tmp/city/wc_a.txt /tmp/city/wc_b.txt | head -5 | tr '\n' '|')"; fi
wmem 2000
cd $S/s0step1 && nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -I. tools/terrainbake.cpp > /tmp/city/msvcproxy_s0step1.log 2>&1
st "clang scan (terrainbake unity) exit $?: $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_s0step1.log) diagnostics"
cd $S/s0step1 && mkdir -p build/gen && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
wmem 2500
cd $S/s0step1 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_s0step1.log 2>&1
st "s0step1 game syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_s0step1.log) warnings=$(grep -c 'warning' /tmp/city/syntax_s0step1.log)"
cd $S/s0step1 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wextra -Isrc src/world/gridcodec.cpp > /tmp/city/syntax_gridcodec.log 2>&1
st "gridcodec MinGW syntax rc=$? diagnostics=$(grep -c 'warning\|error' /tmp/city/syntax_gridcodec.log)"
rm -rf $S/s0step1/build
st "cq31 done"
