#!/bin/sh
# detached: the follow-up candidate st5 = 894e7fb + change 1 + the no-plan rule + the toe clearance 5.5 cm + half an
# update's travel (+ the planned-heel trace): built once orch10 has its exes, run (locostairs) after orch10. Log orch11.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
G=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car/gate.sh
echo "start $(date +%T)"
until grep -q "built sepclean\|no exe" $T/orch10.log 2>/dev/null; do sleep 30; done
sh $G sh $T/check.sh $T/st5 2>&1 | tail -8
[ -f $T/st5/bin/loco_st5.exe ] || sh $T/build.sh st5
[ -f $T/st5/bin/loco_st5.exe ] || { echo "no exe for st5"; exit 1; }
echo "built st5 $(date +%T)"
until grep -q "orch10 done" $T/orch10.log 2>/dev/null; do sleep 30; done
for i in 1 2; do
  sh $T/run_st.sh st5
  grep -q "locostairs trips\|no stair found" $T/log_st5.txt && break
done
grep "locostairs" $T/log_st5.txt | grep -v "trace\|locostairs t \|deep" | tail -6
rm -f $T/st5/bin/loco_st5.exe
echo "orch11 done $(date +%T)"
