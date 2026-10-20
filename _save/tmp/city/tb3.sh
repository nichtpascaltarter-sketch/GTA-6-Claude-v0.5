#!/bin/bash
# terrain-bake prototype, third run (Jobs::shutdown at exit, unbuffered stdout), after tb2.sh
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/tb.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
until grep -q "^tb2 done" /tmp/city/tb.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/tb2.sh" > /dev/null; do sleep 15; done
st "tb3 start"
wmem 2000
cd $S/tb && nice -n 5 g++ -std=c++17 -O2 -I$S/proto_tb terrainbake.cpp -o terrainbake -lpthread > build.log 2>&1; st "build rc=$?"
wmem 1500
cd $S/tb && rm -f tb_* && { time ./terrainbake ; } > run.log 2>&1; st "run exit $?"
rm -f $S/tb/tb_* $S/tb/terrainbake
st "tb3 done"
