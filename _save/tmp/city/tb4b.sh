#!/bin/bash
# terrain-bake prototype, fourth run: where the sites and the roads change the ground and the coast (by region, the
# largest raises and cuts with the nearest road), after jobs34.sh (my games); -O1 to keep the compile small. One build of mine at a time, behind the memory gate.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/tb.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
until grep -q "^jobs34 done" /tmp/city/jobs30.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/jobs34.sh" > /dev/null; do sleep 20; done
st "tb4 start"
wmem 2500
cd $S/tb && nice -n 5 g++ -std=c++17 -O1 -I$S/proto_tb terrainbake.cpp -o terrainbake -lpthread > build4.log 2>&1; st "tb4 build rc=$?"
wmem 1500
cd $S/tb && TB_DETAIL_ONLY=1 ./terrainbake > run4.log 2>&1; st "tb4 run exit $?"
rm -f $S/tb/tb_* $S/tb/terrainbake
st "tb4 done"
