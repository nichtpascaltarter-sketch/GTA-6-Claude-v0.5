#!/bin/bash
# Road-overlap fix, round 4 (no edge moves: gone slots filled; junctions left inside the bigger road moved onto it; short
# hanging roads dropped): ts_fix rebuilt, --overlaps and --deadends all on base and fix.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/rf.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq51 start"
cd $S/rf/t
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -I src tools/traffic_sim.cpp -o /tmp/city/ts_fix -lpthread > /tmp/city/ts_fix_build.log 2>&1; st "ts_fix build rc=$?"
if [ ! -s /tmp/city/ov_base2.txt ]; then
  wmem 2000
  nice -n 5 /tmp/city/ts_base --overlaps --deadends all > /tmp/city/ov_base2.txt 2>&1; st "ts_base rc=$?: $(grep 'overlapping edge pairs' /tmp/city/ov_base2.txt); $(grep '^dead ends' /tmp/city/ov_base2.txt)"
fi
wmem 2000
nice -n 5 /tmp/city/ts_fix --overlaps --deadends all > /tmp/city/ov_fix4.txt 2>&1; st "ts_fix rc=$?: $(grep 'overlapping edge pairs' /tmp/city/ov_fix4.txt); $(grep '^dead ends' /tmp/city/ov_fix4.txt); $(grep -o 'Road gen: [0-9]* stretches.*' /tmp/city/ov_fix4.txt | head -1); $(grep -o 'Road network: .*' /tmp/city/ov_fix4.txt | head -1)"
st "cq51 done"
touch /tmp/city/cq51.done
