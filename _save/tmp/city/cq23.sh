#!/bin/bash
# batches 12 + 14 + 15 on current HEAD: $S/dev23b = HEAD 67e6159 + batch 13 (the before), $S/dev23 = that + the storefront
# gate (buildmesh.cpp, facade.hlsli, world.hlsl), the soft bags (propmesh.cpp), the aviation beacons (landmarks.cpp,
# leisure.cpp). worldcheck and the clang scan on dev23, MinGW syntax on both, nt_dev23.exe, nt_dev23b.exe. One compile at
# a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq23.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
wmem 2000
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/dev23 worldcheck.cpp -o wc_dev23 -lpthread > /tmp/city/wc_dev23_build.log 2>&1; st "wc build rc=$?"
wmem 1200
cd /home/user/GTA-6-Claude-v0.5 && nice -n 5 $S/wc/wc_dev23 20 > $S/wc/dev23.txt 2>&1
st "wc run exit $?: $(grep -o '[0-9]* overlapping building pairs.*' $S/wc/dev23.txt); $(grep -c 'worldcheck: passed' $S/wc/dev23.txt) passed; $(grep 'worldcheck: [0-9]* cells' $S/wc/dev23.txt)"
rm -f $S/wc/wc_dev23
wmem 2000
cd $S/wc && nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -I$S/dev23 worldcheck.cpp > /tmp/city/msvcproxy_dev23.log 2>&1
st "clang scan exit $?: $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_dev23.log) diagnostics"
for T in dev23 dev23b; do
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
st "cq23 done"
