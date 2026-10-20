#!/bin/bash
# the beacon-sync world side alone ($S/dev28 = HEAD b71d339 + worldtypes.h, sitegeo, landmarks (phases only), leisure,
# airport, port, rural, buildmesh): worldcheck with the beacon-phase check, the clang MSVC-safety scan, MinGW syntax.
# One compile at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq30.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
wmem 2000
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -DWC_BEACONS -I$S/dev28 worldcheck.cpp -o wc_dev28 -lpthread > /tmp/city/wc_dev28_build.log 2>&1; st "wc build rc=$?"
wmem 1200
cd /home/user/GTA-6-Claude-v0.5 && FP_BEACONS=1 nice -n 5 $S/wc/wc_dev28 20 > $S/wc/dev28.txt 2>&1
st "wc run exit $?: $(grep -o '[0-9]* overlapping building pairs' $S/wc/dev28.txt); $(grep -c 'worldcheck: passed' $S/wc/dev28.txt) passed; $(grep -o 'worldcheck: [0-9]* cells.*' $S/wc/dev28.txt); $(grep -o 'aviation lights: .*' $S/wc/dev28.txt)"
rm -f $S/wc/wc_dev28
wmem 2000
cd $S/wc && nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -DWC_BEACONS -I$S/dev28 worldcheck.cpp > /tmp/city/msvcproxy_dev28.log 2>&1
st "clang scan exit $?: $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_dev28.log) diagnostics"
cd $S/dev28 && mkdir -p build/gen && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
wmem 2500
cd $S/dev28 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev28.log 2>&1
st "dev28 syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_dev28.log) warnings=$(grep -c 'warning' /tmp/city/syntax_dev28.log)"
rm -rf $S/dev28/build
st "cq30 done"
