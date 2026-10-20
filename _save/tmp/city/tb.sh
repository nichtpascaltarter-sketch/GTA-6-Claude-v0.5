#!/bin/bash
# terrain-bake prototype (docs/HANDBUILT_WORLD.md): after cq30.sh, build and run $S/tb/terrainbake.cpp against $S/proto_tb,
# then measure the compressed sizes (gzip -9 as a stand-in). One compile at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/tb.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
until grep -q "^cq30 done" /tmp/city/cq30.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/cq30.sh" > /dev/null; do sleep 20; done
st "start"
wmem 2000
cd $S/tb && nice -n 5 g++ -std=c++17 -O2 -I$S/proto_tb terrainbake.cpp -o terrainbake -lpthread > build.log 2>&1; st "build rc=$?"
wmem 1500
cd $S/tb && rm -f tb_* && /usr/bin/time -v ./terrainbake > run.log 2> time.log; st "run exit $?"
cd $S/tb && for f in tb_*; do printf "%s raw %d gzip9 %d\n" $f $(stat -c %s $f) $(gzip -9 -c $f | wc -c); done > sizes.txt
rm -f $S/tb/tb_* $S/tb/terrainbake
st "tb done"
