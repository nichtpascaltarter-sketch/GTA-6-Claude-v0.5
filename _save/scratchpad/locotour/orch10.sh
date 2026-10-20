#!/bin/sh
# detached: change 1 (the separation's velocity) in game. push0 = 894e7fb + test blocks, push1 = push0 + the change,
# sepclean = 894e7fb + the change (the clean exe for the AI agent's crowd tests). Runs: locopush, melee, locostairs on
# push0 and push1. Progress in orch10.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
G=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car/gate.sh
echo "start $(date +%T)"
for t in push0 push1 sepclean; do
  sh $G sh $T/check.sh $T/$t 2>&1 | tail -8
  [ -f $T/$t/bin/loco_$t.exe ] || sh $T/build.sh $t
  [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; exit 1; }
  echo "built $t $(date +%T)"
done
for t in push0 push1; do
  sh $T/run_ap.sh $t locopush ${t}_push
  grep "locopush" $T/log_${t}_push.txt | tail -7
  sh $T/run_ap.sh $t melee ${t}_melee --autoduration 20
  grep "autoplay melee" $T/log_${t}_melee.txt | tail -12
  echo "$t short runs done $(date +%T)"
done
for t in push0 push1; do
  for i in 1 2; do
    sh $T/run_st.sh $t
    grep -q "locostairs trips\|no stair found" $T/log_$t.txt && break
  done
  grep "locostairs" $T/log_$t.txt | grep -v "trace\|locostairs t \|deep" | tail -6
done
rm -f $T/push0/bin/loco_push0.exe $T/push1/bin/loco_push1.exe
echo "orch10 done $(date +%T)"
