#!/bin/bash
# Road-overlap fix, round 5c after the 05:15 restart (tree $S/rf/t2 = bcb8a71's tree): ts_fix6 (--overlaps --deadends all),
# census fix6 + report, then the simulations: the base runs still missing and fix6 on all four districts, seeds 1-3, in two
# parallel streams (base, fix).
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/rf.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq58 start"
cd $S/rf/t2
cp src/world/roads.cpp $S/rf/roads_r5c.cpp
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -Wall -Wextra -I src tools/traffic_sim.cpp -o /tmp/city/ts_fix6 -lpthread > /tmp/city/ts_fix6_build.log 2>&1; st "ts_fix6 build rc=$? (roads.cpp warnings: $(grep -c 'roads.cpp.*warning' /tmp/city/ts_fix6_build.log))"
wmem 2000
nice -n 5 /tmp/city/ts_fix6 --overlaps --deadends all > /tmp/city/ov_fix6.txt 2>&1; st "ts_fix6 rc=$?: $(grep 'overlapping edge pairs' /tmp/city/ov_fix6.txt); $(grep '^dead ends' /tmp/city/ov_fix6.txt); dead-end diff lines: $(diff <(grep 'dead end node' /tmp/city/ov_base2.txt | sed 's/ edge [0-9]* / /' | sort) <(grep 'dead end node' /tmp/city/ov_fix6.txt | sed 's/ edge [0-9]* / /' | sort) | grep -c '^[<>]'); $(grep -o 'Road gen: [0-9]* stretches.*' /tmp/city/ov_fix6.txt | head -1); $(grep -o 'Road network: .*' /tmp/city/ov_fix6.txt | head -1)"
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -I. -I src $S/rf/rfcheck.cpp -o /tmp/city/rfcheck_fix6 -lpthread > /tmp/city/rfcheck_fix6_build.log 2>&1; st "rfcheck_fix6 build rc=$?"
wmem 2000
nice -n 5 /tmp/city/rfcheck_fix6 > /tmp/city/census_fix6r.txt 2> /tmp/city/census_fix6r_err.txt; st "rfcheck_fix6 run rc=$?"
python3 $S/rf/rfreport.py /tmp/city/census_baser.txt /tmp/city/census_fix6r.txt > /tmp/city/rf_report6.txt 2>&1
python3 $S/rf/rfdiff2.py /tmp/city/census_baser.txt /tmp/city/census_fix6r.txt /tmp/city/ov_base.txt > /tmp/city/census_diff6.txt 2>&1
st "census6: $(grep -E '^buildings|^changed edge|beyond 150 m of every|^bus 12|^story' /tmp/city/rf_report6.txt | tr '\n' ' ')"
touch /tmp/city/cq57.census
run() {   # V seed district x y
  local O=/tmp/city/sim_$3_$1_$2.txt
  if [ -s $O ] && grep -q 'deadlocks' $O; then st "$3 $1 seed $2 (kept): $(grep -o 'deadlocks (>60 s waits): [0-9]*' $O)"; return; fi
  wmem 2500
  nice -n 5 /tmp/city/ts_$1 --sim $4 $5 450 --cars 140 --peds 120 --minutes 6 --seed $2 -v > $O 2>&1
  st "$3 $1 seed $2: $(grep -o 'deadlocks (>60 s waits): [0-9]*' $O); by node: $(grep '^DEADLOCK' $O | grep -o 'gateNode [0-9]*' | sort | uniq -c | sort -rn | head -4 | awk '{print $3"x"$1}' | tr '\n' ' ')"
}
stream() {
  for SEED in 1 2 3; do
    run $1 $SEED mid 3600 3200; run $1 $SEED beach 5250 600; run $1 $SEED down 3650 -900; run $1 $SEED cutler 2900 -2950
  done
  touch /tmp/city/cq58.$1.done
}
rm -f /tmp/city/sim_mid_base_3.txt   # (cut short by the restart)
stream base &
sleep 20
stream fix6
wait
st "cq58 done"
touch /tmp/city/cq58.done
