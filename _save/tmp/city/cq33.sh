#!/bin/bash
# stage 0 step 3 in $S/s1 (formatter, schema, LZ sections, worldc framework): unit tests, ntwfmt on world/, the bake
# again (canonical terrain.ntw, same grids), worldc, the host load test, clang scans, MinGW syntax of the game and of
# the tools. After cq32 (my compiles one at a time).
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/s1.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
until grep -q "^cq32 done" /tmp/city/s0.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/cq32.sh" > /dev/null; do sleep 20; done
st "cq33 start"
wmem 2000
cd $S/s1 && mkdir -p build/gen && nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. $S/s0test/ntwtest.cpp -o $S/s0test/ntwtest > $S/s0test/ntwtest_build.log 2>&1; st "ntwtest build rc=$? ($(grep -c warning $S/s0test/ntwtest_build.log) warnings)"
$S/s0test/ntwtest > $S/s0test/ntwtest_run.log 2>&1; st "ntwtest run rc=$? ($(tail -1 $S/s0test/ntwtest_run.log))"
cd $S/s1 && nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/ntwfmt.cpp -o build/ntwfmt > $S/s0test/ntwfmt_build.log 2>&1; st "ntwfmt build rc=$? ($(grep -c warning $S/s0test/ntwfmt_build.log) warnings)"
cd $S/s1 && ./build/ntwfmt --check world > $S/s0test/ntwfmt_check1.log 2>&1; st "ntwfmt --check world (step 1's bake text) rc=$?: $(tail -1 $S/s0test/ntwfmt_check1.log)"
wmem 2000
cd $S/s1 && nice -n 5 g++ -std=c++17 -O2 -I. tools/terrainbake.cpp -o build/terrainbake -lpthread > $S/s0test/bake3_build.log 2>&1; st "bake (s1) build rc=$?"
wmem 1500
mkdir -p $S/s0test/rebake && cd $S/s1 && ./build/terrainbake --out=$S/s0test/rebake --commit=$(cd /home/user/GTA-6-Claude-v0.5 && git rev-parse --short HEAD) > $S/s0test/bake3_run.log 2>&1; st "bake (s1) run rc=$?"
for f in height water region coast; do cmp -s $S/s0test/rebake/$f.ntg $S/s1/world/terrain/$f.ntg && st "  $f.ntg identical to step 1's" || st "  $f.ntg DIFFERS from step 1's"; done
cp $S/s0test/rebake/terrain.ntw $S/s1/world/terrain/terrain.ntw; rm -rf $S/s0test/rebake; rm -f $S/s1/build/terrainbake
cd $S/s1 && ./build/ntwfmt --check world > $S/s0test/ntwfmt_check2.log 2>&1; st "ntwfmt --check world (re-baked) rc=$?: $(tail -1 $S/s0test/ntwfmt_check2.log)"
cd $S/s1 && nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/worldc.cpp -o build/worldc > $S/s0test/worldc3_build.log 2>&1; st "worldc (s1) build rc=$? ($(grep -c warning $S/s0test/worldc3_build.log) warnings)"
cd $S/s1 && ./build/worldc > $S/s0test/worldc3_run.log 2>&1; st "worldc (s1) run rc=$?: $(tail -1 $S/s0test/worldc3_run.log)"
cd $S/s1 && ./build/worldc --check=world/terrain/terrain.ntw >> $S/s0test/worldc3_run.log 2>&1; st "worldc --check rc=$?"
wmem 2000
cd $S/s1 && nice -n 5 g++ -std=c++17 -O2 -I. $S/s0test/handload.cpp -o $S/s0test/handload1 -lpthread > $S/s0test/handload1_build.log 2>&1; st "handload (s1) build rc=$?"
wmem 1500
cd $S/s1 && $S/s0test/handload1 build/gen/world.bin > $S/s0test/handload1_run.log 2>&1; st "handload (s1) run rc=$?: $(tail -1 $S/s0test/handload1_run.log)"
rm -f $S/s0test/handload1
wmem 2000
cd $S/s1 && for t in tools/worldc.cpp tools/ntwfmt.cpp $S/s0test/ntwtest.cpp; do nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -I. $t >> /tmp/city/msvcproxy_s1.log 2>&1; done; st "clang scans (worldc, ntwfmt, ntwtest): $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_s1.log) diagnostics"
cd $S/s1 && for t in tools/worldc.cpp tools/ntwfmt.cpp; do nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -I. $t >> /tmp/city/syntax_s1_tools.log 2>&1; done; st "MinGW syntax of worldc and ntwfmt: $(grep -c 'warning\|error' /tmp/city/syntax_s1_tools.log) diagnostics"
cd $S/s1 && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
wmem 2500
cd $S/s1 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_s1.log 2>&1
st "s1 game syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_s1.log) warnings=$(grep -c 'warning' /tmp/city/syntax_s1.log)"
st "cq33 done"
