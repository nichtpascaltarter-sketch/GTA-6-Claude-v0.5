#!/bin/bash
# dev22 + the aviation beacons (landmarks.cpp, leisure.cpp): worldcheck, clang GNU-extension scan, MinGW syntax,
# nt_dev22c.exe. One compile of mine at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq22c.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
wmem 2000
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/dev22 worldcheck.cpp -o wc_dev22c -lpthread > /tmp/city/wc_dev22c_build.log 2>&1; st "wc build rc=$?"
wmem 1200
cd /home/user/GTA-6-Claude-v0.5 && nice -n 5 $S/wc/wc_dev22c 20 > $S/wc/dev22c.txt 2>&1
st "wc run exit $?: $(grep -o '[0-9]* overlapping building pairs.*' $S/wc/dev22c.txt); $(grep -c 'worldcheck: passed' $S/wc/dev22c.txt) passed"
rm -f $S/wc/wc_dev22c
wmem 2000
cd $S/wc && nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -I$S/dev22 worldcheck.cpp > /tmp/city/msvcproxy_dev22c.log 2>&1
st "clang scan exit $?: $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_dev22c.log) diagnostics"
cd $S/dev22 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
wmem 2500
cd $S/dev22 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev22c.log 2>&1
if grep -q "error" /tmp/city/syntax_dev22c.log; then st "dev22c syntax errors"; exit 1; fi
st "dev22c syntax ok ($(grep -c warning /tmp/city/syntax_dev22c.log) warnings)"
wmem 2500
rm -f /tmp/city/nt_dev22c.exe
cd $S/dev22 && { time OUT=/tmp/city/nt_dev22c.exe sh ./build.sh ; } > /tmp/city/build_dev22c.log 2>&1; st "dev22c build rc=$?"
[ -f /tmp/city/nt_dev22c.exe ] && st "dev22c exe ready" || st "no dev22c exe"
rm -rf $S/dev22/build
