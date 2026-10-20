#!/bin/bash
# batch 16 (plastic props): $S/dev24 = HEAD 0d67933 (before), $S/dev25 = HEAD + propmesh.cpp (cones and barriers on the
# painted material). The clang MSVC-safety scan on dev25, MinGW syntax on both, nt_dev24.exe and nt_dev25.exe. One
# compile at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq25.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
wmem 2000
cd $S/wc && nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -I$S/dev25 worldcheck.cpp > /tmp/city/msvcproxy_dev25.log 2>&1
st "clang scan exit $?: $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_dev25.log) diagnostics"
for T in dev24 dev25; do
  cd $S/$T && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
  wmem 2500
  cd $S/$T && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_$T.log 2>&1
  if grep -q "error" /tmp/city/syntax_$T.log; then st "$T syntax errors"; continue; fi
  st "$T syntax ok ($(grep -c warning /tmp/city/syntax_$T.log) warnings)"
  wmem 2500
  rm -f /tmp/city/nt_$T.exe
  cd $S/$T && { time OUT=/tmp/city/nt_$T.exe sh ./build.sh ; } > /tmp/city/build_$T.log 2>&1; st "$T build rc=$?"
  [ -f /tmp/city/nt_$T.exe ] && st "$T exe ready" || st "no $T exe"
  rm -rf $S/$T/build
done
st "cq25 done"
