#!/bin/bash
# stage 0 step 1: codec unit test, then the terrain bake (tools/terrainbake.cpp) in $S/s0 -> $S/s0/world/terrain.
# One build of mine at a time, behind the memory gate.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/s0.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "s0a start"
wmem 2000
cd $S/s0 && nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. $S/s0test/gridtest.cpp -o $S/s0test/gridtest > $S/s0test/build.log 2>&1; st "gridtest build rc=$?"
$S/s0test/gridtest > $S/s0test/run.log 2>&1; st "gridtest run rc=$? ($(tail -1 $S/s0test/run.log))"
wmem 2000
cd $S/s0 && mkdir -p build && nice -n 5 g++ -std=c++17 -O2 -I. tools/terrainbake.cpp -o build/terrainbake -lpthread > $S/s0test/bake_build.log 2>&1; st "bake build rc=$?"
wmem 1500
cd $S/s0 && mkdir -p world/terrain && ./build/terrainbake --commit=$(cd /home/user/GTA-6-Claude-v0.5 && git rev-parse --short HEAD) > $S/s0test/bake_run.log 2>&1; st "bake run rc=$?"
st "s0a done"
