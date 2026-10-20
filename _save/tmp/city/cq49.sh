#!/bin/bash
# Road-overlap fix: ts_fix rebuilt with the current roads.cpp, --overlaps, then the AI agent's two simulations (Midtown
# Bayshore/9th Ave, Sol Beach) on base and fix, seeds 1-3, with the deadlock counts and the waits at nodes 281-288 / 2478.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/rf.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
touch /tmp/city/cq49.start
while [ ! /tmp/city/census_diff.txt -nt /tmp/city/cq49.start ] || pgrep -f "bash /tmp/city/cq48.sh" > /dev/null; do sleep 15; done
st "cq49 start"
cd $S/rf/t
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -I src tools/traffic_sim.cpp -o /tmp/city/ts_fix -lpthread > /tmp/city/ts_fix_build.log 2>&1; st "ts_fix build rc=$?"
wmem 2000
nice -n 5 /tmp/city/ts_fix --overlaps > /tmp/city/ov_fix.txt 2>&1; st "ts_fix --overlaps rc=$?: $(grep 'overlapping edge pairs' /tmp/city/ov_fix.txt); $(grep -o 'Road gen: [0-9]* stretches.*' /tmp/city/ov_fix.txt | head -1); $(grep -o 'Road network: .*' /tmp/city/ov_fix.txt | head -1)"
for SEED in 1 2 3; do
  for V in base fix; do
    wmem 2500
    nice -n 5 /tmp/city/ts_$V --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 --seed $SEED -v > /tmp/city/sim_mid_${V}_$SEED.txt 2>&1
    st "mid $V seed $SEED: $(grep -o 'deadlocks (>60 s waits): [0-9]*' /tmp/city/sim_mid_${V}_$SEED.txt); waits at 281-288: $(grep -cE 'gateNode 28[1-8]\b' /tmp/city/sim_mid_${V}_$SEED.txt)"
    wmem 2500
    nice -n 5 /tmp/city/ts_$V --sim 5250 600 450 --cars 140 --peds 120 --minutes 6 --seed $SEED -v > /tmp/city/sim_beach_${V}_$SEED.txt 2>&1
    st "beach $V seed $SEED: $(grep -o 'deadlocks (>60 s waits): [0-9]*' /tmp/city/sim_beach_${V}_$SEED.txt); waits at 2478: $(grep -cE 'gateNode 2478\b' /tmp/city/sim_beach_${V}_$SEED.txt)"
  done
done
st "cq49 done"
