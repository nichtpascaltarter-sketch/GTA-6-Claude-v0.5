#!/bin/bash
# Road-overlap fix, round 5b (tree $S/rf/t2 = 2c80a38): deep = centreline on the bigger road's asphalt; stub-joined junctions merge; the rest of a cut road that would run on beside the bigger road
# for under 40 m goes too; the slot-filling splits only next to real junctions. ts_fix5 (--overlaps --deadends all), both
# census harnesses rebuilt (they now print the bus routes), the census diff.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/rf.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq54 start"
cd $S/rf/t2
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -Wall -Wextra -I src tools/traffic_sim.cpp -o /tmp/city/ts_fix5 -lpthread > /tmp/city/ts_fix5_build.log 2>&1; st "ts_fix5 build rc=$? (roads.cpp warnings: $(grep -c 'roads.cpp.*warning' /tmp/city/ts_fix5_build.log))"
wmem 2000
nice -n 5 /tmp/city/ts_fix5 --overlaps --deadends all > /tmp/city/ov_fix5.txt 2>&1; st "ts_fix5 rc=$?: $(grep 'overlapping edge pairs' /tmp/city/ov_fix5.txt); $(grep '^dead ends' /tmp/city/ov_fix5.txt); dead-end diff lines: $(diff <(grep 'dead end node' /tmp/city/ov_base2.txt | sed 's/ edge [0-9]* / /' | sort) <(grep 'dead end node' /tmp/city/ov_fix5.txt | sed 's/ edge [0-9]* / /' | sort) | grep -c '^[<>]'); $(grep -o 'Road gen: [0-9]* stretches.*' /tmp/city/ov_fix5.txt | head -1); $(grep -o 'Road network: .*' /tmp/city/ov_fix5.txt | head -1)"
for V in base fix5; do
  wmem 2500
  D=""; [ $V = base ] && D="-DRF_BASE"
  nice -n 5 g++ -O2 -std=c++17 -I. -I src $D $S/rf/rfcheck.cpp -o /tmp/city/rfcheck_$V -lpthread > /tmp/city/rfcheck_${V}_build.log 2>&1; st "rfcheck_$V build rc=$?"
  wmem 2000
  nice -n 5 /tmp/city/rfcheck_$V > /tmp/city/census_${V}r.txt 2> /tmp/city/census_${V}r_err.txt; st "rfcheck_$V run rc=$?"
done
python3 $S/rf/rfdiff2.py /tmp/city/census_baser.txt /tmp/city/census_fix5r.txt /tmp/city/ov_base.txt > /tmp/city/census_diff5.txt 2>&1; st "census diff rc=$?: $(grep -E '^edges:|^buildings:|beyond 150|^story places|^bus stops|^route 12' /tmp/city/census_diff5.txt | tr '\n' ' ')"
st "cq54 done"
touch /tmp/city/cq54.done
