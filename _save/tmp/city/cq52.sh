#!/bin/bash
# Road-overlap fix, round 4: census (rfcheck fix vs the base census), diff, then the AI agent's two simulations on base and
# fix, seeds 1-3 (base runs only when missing).
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/rf.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq52 start"
cd $S/rf/t
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -I. -I src $S/rf/rfcheck.cpp -o /tmp/city/rfcheck_fix -lpthread > /tmp/city/rfcheck_fix_build.log 2>&1; st "rfcheck_fix build rc=$?"
wmem 2000
nice -n 5 /tmp/city/rfcheck_fix > /tmp/city/census_fix4.txt 2> /tmp/city/census_fix4_err.txt; st "rfcheck_fix run rc=$?"
python3 $S/rf/rfdiff2.py /tmp/city/census_base.txt /tmp/city/census_fix4.txt /tmp/city/ov_base.txt > /tmp/city/census_diff4.txt 2>&1; st "census diff rc=$?: $(grep -E '^edges:|^buildings:|beyond 150|^story places|^bus stops' /tmp/city/census_diff4.txt | tr '\n' ' ')"
touch /tmp/city/cq52.census
for SEED in 1 2 3; do
  for V in base fix; do
    O=/tmp/city/sim_mid_${V}_$SEED.txt; [ $V = fix ] && O=/tmp/city/sim_mid_fix4_$SEED.txt
    if [ ! -s $O ] || ! grep -q 'deadlocks' $O; then
      wmem 2500
      nice -n 5 /tmp/city/ts_$V --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 --seed $SEED -v > $O 2>&1
    fi
    st "mid $V seed $SEED: $(grep -o 'deadlocks (>60 s waits): [0-9]*' $O); waits at 281-288: $(grep -cE 'gateNode 28[1-8]\b' $O)"
    O=/tmp/city/sim_beach_${V}_$SEED.txt; [ $V = fix ] && O=/tmp/city/sim_beach_fix4_$SEED.txt
    if [ ! -s $O ] || ! grep -q 'deadlocks' $O; then
      wmem 2500
      nice -n 5 /tmp/city/ts_$V --sim 5250 600 450 --cars 140 --peds 120 --minutes 6 --seed $SEED -v > $O 2>&1
    fi
    st "beach $V seed $SEED: $(grep -o 'deadlocks (>60 s waits): [0-9]*' $O); waits at 2478: $(grep -cE 'gateNode 2478\b' $O)"
  done
done
st "cq52 done"
touch /tmp/city/cq52.done
