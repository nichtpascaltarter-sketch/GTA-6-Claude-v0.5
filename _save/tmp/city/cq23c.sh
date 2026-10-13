#!/bin/bash
# nt_dev23c.exe: $S/dev23 with the bag pile v2 (propmesh.cpp: the painted material so the green and white sacks show,
# the lying sack pulled in to 0.75 m). The clang MSVC-safety scan, MinGW syntax, then the exe. One compile at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq23c.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
wmem 2000
cd $S/wc && nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -I$S/dev23 worldcheck.cpp > /tmp/city/msvcproxy_dev23c.log 2>&1
st "clang scan exit $?: $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_dev23c.log) diagnostics"
cd $S/dev23 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
wmem 2500
cd $S/dev23 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev23c.log 2>&1
if grep -q "error" /tmp/city/syntax_dev23c.log; then st "syntax errors"; exit 1; fi
st "syntax ok ($(grep -c warning /tmp/city/syntax_dev23c.log) warnings)"
wmem 2500
rm -f /tmp/city/nt_dev23c.exe
cd $S/dev23 && { time OUT=/tmp/city/nt_dev23c.exe sh ./build.sh ; } > /tmp/city/build_dev23c.log 2>&1; st "build rc=$?"
[ -f /tmp/city/nt_dev23c.exe ] && st "exe ready" || st "no exe"
rm -rf $S/dev23/build
