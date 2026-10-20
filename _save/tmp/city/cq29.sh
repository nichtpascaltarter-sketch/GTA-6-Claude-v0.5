#!/bin/bash
# nt_dev27.exe = $S/dev27 (batches 16-19: dev26 + the four MAT_PLASTIC call sites), after cq28.sh: the clang scan, MinGW,
# the exe. One compile at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq26.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
until grep -q "^cq28 done" /tmp/city/cq26.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/cq28.sh" > /dev/null; do sleep 30; done
st "cq29 start"
wmem 2000
cd $S/wc && nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -DWC_BEACONS -I$S/dev27 worldcheck.cpp > /tmp/city/msvcproxy_dev27.log 2>&1
st "dev27 clang scan exit $?: $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_dev27.log) diagnostics"
cd $S/dev27 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
wmem 2500
cd $S/dev27 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev27.log 2>&1
if grep -q "error" /tmp/city/syntax_dev27.log; then st "dev27 syntax errors"; exit 1; fi
st "dev27 syntax ok ($(grep -c warning /tmp/city/syntax_dev27.log) warnings)"
wmem 2500
rm -f /tmp/city/nt_dev27.exe
cd $S/dev27 && { time OUT=/tmp/city/nt_dev27.exe sh ./build.sh ; } > /tmp/city/build_dev27.log 2>&1; st "dev27 build rc=$?"
[ -f /tmp/city/nt_dev27.exe ] && st "dev27 exe ready" || st "no dev27 exe"
rm -rf $S/dev27/build
st "cq29 done"
