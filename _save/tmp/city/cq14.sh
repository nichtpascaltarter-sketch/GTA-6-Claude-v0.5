#!/bin/bash
# batch 10 (the storefront glass stopgap dropped) in $S/dev14 = main b907098 with the two stopgap lines removed from
# the roads fix) with the two stopgap lines removed from blockstyle.cpp: MinGW syntax check, then /tmp/city/nt_dev14.exe.
# One compile of mine at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq14.status; }
st "start"
cd $S/dev14 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/dev14 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev14.log 2>&1
if grep -q "error" /tmp/city/syntax_dev14.log; then st "dev14b syntax errors"; exit 1; fi
st "dev14b syntax ok ($(grep -c warning /tmp/city/syntax_dev14.log) warnings)"
rm -f /tmp/city/nt_dev14.exe
cd $S/dev14 && { time OUT=/tmp/city/nt_dev14.exe sh ./build.sh ; } > /tmp/city/build_dev14.log 2>&1; st "dev14b build rc=$?"
[ -f /tmp/city/nt_dev14.exe ] && st "dev14b exe ready" || st "no dev14b exe"
