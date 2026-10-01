#!/bin/bash
# batch 4 exes on main 6780edd: dev4 (MinGW syntax check first), then base4 (= batch 3 on 6780edd); one build at a time
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/jobsV.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
while ! grep -q "dev4 report" /tmp/city/jobsU.status 2>/dev/null; do sleep 20; done
for d in dev4 base4; do
  cd $S/$d && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
done
waitmem 2500
cd $S/dev4 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev4.log 2>&1
if grep -q "error" /tmp/city/syntax_dev4.log; then st "dev4 syntax errors"; exit 1; fi
st "dev4 syntax ok"
waitmem 3000
cd $S/dev4 && { time OUT=/tmp/city/nt_dev4.exe sh ./build.sh ; } > /tmp/city/build_dev4.log 2>&1; st "dev4 build rc=$?"
waitmem 3000
cd $S/base4 && { time OUT=/tmp/city/nt_base4.exe sh ./build.sh ; } > /tmp/city/build_base4.log 2>&1; st "base4 build rc=$?"
st "jobsV done"
