#!/bin/bash
# batch 11 (garage wings kept on their lots) in $S/dev15 = main b907098 + batch 10's blockstyle.cpp + the fix: after the
# footprint checks (one compile of mine at a time), MinGW syntax check, then /tmp/city/nt_dev15.exe
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq15.status; }
st "waiting for the footprint checks"
until grep -q "wcstats done" /tmp/city/wcfp.status 2>/dev/null; do sleep 20; done
cd $S/dev15 && mkdir -p build/gen bin && [ -f build/gen/shaders_embedded.h ] || { g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
st "syntax check"
cd $S/dev15 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev15.log 2>&1
if grep -q "error" /tmp/city/syntax_dev15.log; then st "dev15 syntax errors"; exit 1; fi
st "dev15 syntax ok ($(grep -c warning /tmp/city/syntax_dev15.log) warnings)"
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
rm -f /tmp/city/nt_dev15.exe
cd $S/dev15 && { time OUT=/tmp/city/nt_dev15.exe sh ./build.sh ; } > /tmp/city/build_dev15.log 2>&1; st "dev15 build rc=$?"
[ -f /tmp/city/nt_dev15.exe ] && st "dev15 exe ready" || st "no dev15 exe"
