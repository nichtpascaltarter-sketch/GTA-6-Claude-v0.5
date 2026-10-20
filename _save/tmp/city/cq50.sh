#!/bin/bash
# Road-overlap fix, round 3 (deep overlaps outside the junction areas; leftover slots close up in order): ts_fix and both
# census harnesses rebuilt, --overlaps, the census diff, then the AI agent's two simulations on base and fix, seeds 1-3.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/rf.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq50 start"
cd $S/rf/t
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -I src tools/traffic_sim.cpp -o /tmp/city/ts_fix -lpthread > /tmp/city/ts_fix_build.log 2>&1; st "ts_fix build rc=$?"
wmem 2000
nice -n 5 /tmp/city/ts_fix --overlaps > /tmp/city/ov_fix.txt 2>&1; st "ts_fix --overlaps rc=$?: $(grep 'overlapping edge pairs' /tmp/city/ov_fix.txt); $(grep -o 'Road gen: [0-9]* stretches.*' /tmp/city/ov_fix.txt | head -1); $(grep -o 'Road network: .*' /tmp/city/ov_fix.txt | head -1)"
for V in base fix; do
  wmem 2500
  D=""; [ $V = base ] && D="-DRF_BASE"
  nice -n 5 g++ -O2 -std=c++17 -I. -I src $D $S/rf/rfcheck.cpp -o /tmp/city/rfcheck_$V -lpthread > /tmp/city/rfcheck_${V}_build.log 2>&1; st "rfcheck_$V build rc=$?"
  wmem 2000
  nice -n 5 /tmp/city/rfcheck_$V > /tmp/city/census_$V.txt 2> /tmp/city/census_${V}_err.txt; st "rfcheck_$V run rc=$?"
done
python3 $S/rf/rfdiff.py /tmp/city/census_base.txt /tmp/city/census_fix.txt /tmp/city/ov_base.txt > /tmp/city/census_diff.txt 2>&1; st "census diff rc=$?: $(sed -n 3,4p /tmp/city/census_diff.txt | tr '\n' ' ')"
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
st "cq50 done"
