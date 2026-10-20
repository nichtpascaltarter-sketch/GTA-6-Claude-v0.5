#!/bin/bash
# stage 0 step 2 in $S/s0: worldc (build with -Wall -Wextra, run, windres), the host load test, the clang MSVC-safety scans,
# MinGW syntax of the game, then the game exe through build.sh (OUT=/tmp/city/nt_s0.exe). After cq31; one compile at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/s0.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
until grep -q "^cq31 done" /tmp/city/s0.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/cq31.sh" > /dev/null; do sleep 20; done
st "cq32 start"
wmem 2000
cd $S/s0 && mkdir -p build/gen && nice -n 5 g++ -O2 -std=c++17 -Wall -Wextra -I. tools/worldc.cpp -o build/worldc > $S/s0test/worldc_build.log 2>&1; st "worldc build rc=$? ($(grep -c 'warning' $S/s0test/worldc_build.log) warnings)"
cd $S/s0 && { time ./build/worldc --world=world --out=build/gen/world.bin ; } > $S/s0test/worldc_run.log 2>&1; st "worldc run rc=$?: $(grep 'wrote' $S/s0test/worldc_run.log)"
(cd $S/s0/build/gen && x86_64-w64-mingw32-windres world.rc -O coff -o world_res.o > $S/s0test/windres.log 2>&1); st "windres rc=$?: world_res.o $(stat -c %s $S/s0/build/gen/world_res.o 2>/dev/null) bytes; $(x86_64-w64-mingw32-objdump -h $S/s0/build/gen/world_res.o 2>/dev/null | grep -o '\.rsrc *[0-9a-f]*')"
wmem 2000
cd $S/s0 && nice -n 5 g++ -std=c++17 -O2 -I. $S/s0test/handload.cpp -o $S/s0test/handload -lpthread > $S/s0test/handload_build.log 2>&1; st "handload build rc=$?"
wmem 1500
cd $S/s0 && $S/s0test/handload build/gen/world.bin > $S/s0test/handload_run.log 2>&1; st "handload run rc=$?: $(tail -1 $S/s0test/handload_run.log)"
rm -f $S/s0test/handload
wmem 2000
cd $S/s0 && nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -I. tools/worldc.cpp > /tmp/city/msvcproxy_worldc.log 2>&1; st "clang scan worldc exit $?: $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_worldc.log) diagnostics"
wmem 2000
cd $S/s0 && nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -I. $S/s0test/handload.cpp > /tmp/city/msvcproxy_handload.log 2>&1; st "clang scan handload unity exit $?: $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_handload.log) diagnostics"
cd $S/s0 && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
wmem 2500
cd $S/s0 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_s0.log 2>&1
st "s0 game syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_s0.log) warnings=$(grep -c 'warning' /tmp/city/syntax_s0.log)"
wmem 2800
cd $S/s0 && OUT=/tmp/city/nt_s0.exe nice -n 6 ./build.sh > /tmp/city/build_s0.log 2>&1; st "nt_s0 build rc=$?: $(ls -la /tmp/city/nt_s0.exe 2>/dev/null | awk '{print $5}') bytes"
st "cq32 done"
