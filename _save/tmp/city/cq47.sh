#!/bin/bash
# Road-overlap fix ($S/rf/t): the traffic_sim harness (the AI agent's set-19 copy) built with HEAD's roads.cpp (base) and
# with the fix, then --overlaps on both.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/rf.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq47 start"
cd $S/rf/t
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -I src -DTS_ROADS_CPP='"../src/world/roads_base.cpp"' tools/traffic_sim.cpp -o /tmp/city/ts_base -lpthread > /tmp/city/ts_base_build.log 2>&1; st "ts_base build rc=$?"
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -I src tools/traffic_sim.cpp -o /tmp/city/ts_fix -lpthread > /tmp/city/ts_fix_build.log 2>&1; st "ts_fix build rc=$?"
for V in base fix; do
  wmem 2500
  cd $S/rf/t && nice -n 5 /tmp/city/ts_$V --overlaps > /tmp/city/ov_$V.txt 2>&1; st "ts_$V --overlaps rc=$?: $(grep 'overlapping edge pairs' /tmp/city/ov_$V.txt); $(grep -o 'Road gen: [0-9]* stretches.*' /tmp/city/ov_$V.txt | head -1); $(grep -o 'Road network: .*' /tmp/city/ov_$V.txt | head -1)"
done
st "cq47 done"
