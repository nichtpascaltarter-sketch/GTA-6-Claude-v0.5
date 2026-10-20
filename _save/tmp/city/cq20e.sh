#!/bin/bash
# the batch 13 and batch 12+14 exes again with batch 11b's interiors.cpp (dev20, dev22 carry it): nt_dev20b.exe, then
# nt_dev22b.exe. One compile of mine at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq20e.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
for T in dev20 dev22; do
  cd $S/$T && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
  wmem 2500
  cd $S/$T && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_${T}b.log 2>&1
  if grep -q "error" /tmp/city/syntax_${T}b.log; then st "${T}b syntax errors"; continue; fi
  st "${T}b syntax ok ($(grep -c warning /tmp/city/syntax_${T}b.log) warnings)"
  wmem 2500
  rm -f /tmp/city/nt_${T}b.exe
  cd $S/$T && { time OUT=/tmp/city/nt_${T}b.exe sh ./build.sh ; } > /tmp/city/build_${T}b.log 2>&1; st "${T}b build rc=$?"
  [ -f /tmp/city/nt_${T}b.exe ] && st "${T}b exe ready" || st "no ${T}b exe"
  rm -rf $S/$T/build
done
st "cq20e done"
