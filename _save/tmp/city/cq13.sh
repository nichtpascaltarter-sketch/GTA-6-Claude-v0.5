#!/bin/bash
# batch 9 on main d3c2c3f: $S/dev13 = git archive d3c2c3f + the dev12 world files; MinGW syntax check, then
# /tmp/city/nt_dev13.exe (build.sh). One compile of mine at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq13.status; }
st "start"
rm -rf $S/dev13 && mkdir -p $S/dev13 && cd /home/user/GTA-6-Claude-v0.5 && git archive d3c2c3f | tar -x -C $S/dev13
cp $S/dev12/src/world/*.cpp $S/dev12/src/world/*.h $S/dev13/src/world/
st "dev13 = d3c2c3f + dev12 world: $(cd $S/dev13 && git -C /home/user/GTA-6-Claude-v0.5 diff --no-index --stat $S/dev12/src/world $S/dev13/src/world | tail -1)"
cd $S/dev13 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/dev13 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev13.log 2>&1
if grep -q "error" /tmp/city/syntax_dev13.log; then st "dev13 syntax errors"; exit 1; fi
st "dev13 syntax ok ($(grep -c warning /tmp/city/syntax_dev13.log) warnings)"
rm -f /tmp/city/nt_dev13.exe
cd $S/dev13 && { time OUT=/tmp/city/nt_dev13.exe sh ./build.sh ; } > /tmp/city/build_dev13.log 2>&1; st "dev13 build rc=$?"
[ -f /tmp/city/nt_dev13.exe ] && st "dev13 exe ready" || st "no dev13 exe"
