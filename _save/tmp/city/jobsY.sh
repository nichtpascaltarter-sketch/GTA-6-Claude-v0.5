#!/bin/bash
# batch 4 on main 06580c3: citylab report, MinGW syntax check, nt_dev4.exe (one compile at a time)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/jobsY.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
while ! grep -q "^rc=" $S/../tasks/bcid7fefs.output 2>/dev/null; do sleep 15; done
if ! grep -q "^rc=0" $S/../tasks/bcid7fefs.output; then st "citylab build failed"; exit 1; fi
waitmem 2500
cd /home/user/GTA-6-Claude-v0.5 && timeout 1500 nice -n 5 $S/citylab/citylab_dev4 --stats --farbreak --yardtrees --courts --twins 13,18,19,23,8,16 --find 18,-1317,-845,4 --find 52,2495,4012,4 --find 19,144,6475,1 --find 19,2495,4012,1 --find 35,4500,-4200 --find 25,4500,-4200 > /tmp/city/dev4_report2.txt 2>&1; st "dev4 report rc=$?"
cd $S/dev4 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
waitmem 2500
cd $S/dev4 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev4.log 2>&1
if grep -q "error" /tmp/city/syntax_dev4.log; then st "dev4 syntax errors"; exit 1; fi
st "dev4 syntax ok"
cd $S/dev4 && { time OUT=/tmp/city/nt_dev4.exe sh ./build.sh ; } > /tmp/city/build_dev4.log 2>&1; st "dev4 build rc=$?"
st "jobsY done"
