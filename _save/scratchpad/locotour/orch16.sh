#!/bin/sh
# detached: st6 again with a big-slide log (test-only app.cpp lines; the anim code unchanged) to attribute the 30 cm
# ascent slide of the first st6 run (deterministic run). Log orch16.log; the game log -> log_st6.txt
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
[ -f $T/st6/bin/loco_st6.exe ] || sh $T/build.sh st6
[ -f $T/st6/bin/loco_st6.exe ] || { echo "no exe for st6"; exit 1; }
echo "built st6 $(date +%T)"
for i in 1 2; do
  sh $T/run_st.sh st6
  grep -q "locostairs trips\|no stair found" $T/log_st6.txt && break
done
grep "locostairs" $T/log_st6.txt | grep -v "trace\|locostairs t \|deep" | tail -8
rm -f $T/st6/bin/loco_st6.exe
echo "orch16 done $(date +%T)"
