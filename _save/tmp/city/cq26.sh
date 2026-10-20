#!/bin/bash
# batches 17 + 18 (world side) in $S/dev26 = HEAD 0d67933 + batch 16's propmesh + the second bag pile (propmesh.cpp,
# cellgen.cpp, the props_render.cpp line), Solaris' corner beacons at 0.9 m, the beacon blink phases (worldtypes.h phase,
# sitegeo beaconLight, landmarks, leisure, airport, port, rural, buildmesh). After cq25.sh: worldcheck (beacon phases, bag
# variants), the clang scan, MinGW, nt_dev26.exe, then the ASan worldcheck. One compile at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq26.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
until grep -q "^cq25 done" /tmp/city/cq25.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/cq25.sh" > /dev/null; do sleep 30; done
st "start"
wmem 2000
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -DWC_BEACONS -I$S/dev26 worldcheck.cpp -o wc_dev26 -lpthread > /tmp/city/wc_dev26_build.log 2>&1; st "wc build rc=$?"
wmem 1200
cd /home/user/GTA-6-Claude-v0.5 && FP_BEACONS=1 FP_BAGS="2607,4300" FP_WORKS=2607,4300 nice -n 5 $S/wc/wc_dev26 20 > $S/wc/dev26.txt 2>&1
st "wc run exit $?: $(grep -o '[0-9]* overlapping building pairs' $S/wc/dev26.txt); $(grep -c 'worldcheck: passed' $S/wc/dev26.txt) passed; $(grep -o 'aviation lights: .*' $S/wc/dev26.txt); $(grep -o 'trash bag piles: [0-9]* on the map.*' $S/wc/dev26.txt)"
rm -f $S/wc/wc_dev26
wmem 2000
cd $S/wc && nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -DWC_BEACONS -I$S/dev26 worldcheck.cpp > /tmp/city/msvcproxy_dev26.log 2>&1
st "clang scan exit $?: $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_dev26.log) diagnostics"
cd $S/dev26 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
wmem 2500
cd $S/dev26 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev26.log 2>&1
if grep -q "error" /tmp/city/syntax_dev26.log; then st "dev26 syntax errors"; exit 1; fi
st "dev26 syntax ok ($(grep -c warning /tmp/city/syntax_dev26.log) warnings)"
wmem 2500
rm -f /tmp/city/nt_dev26.exe
cd $S/dev26 && { time OUT=/tmp/city/nt_dev26.exe sh ./build.sh ; } > /tmp/city/build_dev26.log 2>&1; st "dev26 build rc=$?"
[ -f /tmp/city/nt_dev26.exe ] && st "dev26 exe ready" || st "no dev26 exe"
rm -rf $S/dev26/build
wmem 4500
cd $S/wc && nice -n 5 g++ -std=c++17 -O1 -fno-omit-frame-pointer -fsanitize=address -fsanitize-recover=address -fsanitize=bounds -DWC_BEACONS -I$S/dev26 worldcheck.cpp -o wc_dev26_asan -lpthread > /tmp/city/wc_dev26_asan_build.log 2>&1
rc=$?; st "wc asan build rc=$rc"
if [ $rc -eq 0 ]; then
  wmem 3000
  cd /home/user/GTA-6-Claude-v0.5 && ASAN_OPTIONS=detect_leaks=0:halt_on_error=0 timeout 5400 nice -n 5 $S/wc/wc_dev26_asan 12 > /tmp/city/wc_dev26_asan.txt 2>&1
  st "wc asan run exit $? ($(grep -c 'ERROR: AddressSanitizer\|runtime error' /tmp/city/wc_dev26_asan.txt) reports, $(grep -c 'worldcheck: passed' /tmp/city/wc_dev26_asan.txt) passed)"
fi
rm -f $S/wc/wc_dev26_asan
st "cq26 done"
