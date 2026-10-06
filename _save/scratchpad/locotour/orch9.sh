#!/bin/sh
# detached: the separation follow-up's checks, after orch8. push0 = b49bc7f + the stair follow-up (t4) + test blocks;
# push1 = push0 + the separation's velocity change. Each: locopush, melee, locostairs. Progress in orch9.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
until grep -q "orch8 done\|no exe for after_st4" $T/orch8.log 2>/dev/null; do sleep 30; done
echo "orch8 finished $(date +%T)"
for t in push0 push1; do
  sh /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car/gate.sh sh $T/check.sh $T/$t 2>&1 | tail -8
  [ -f $T/$t/bin/loco_$t.exe ] || sh $T/build.sh $t
  [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; exit 1; }
  echo "built $t $(date +%T)"
done
for t in push0 push1; do
  sh $T/run_ap.sh $t locopush ${t}_push
  grep "locopush" $T/log_${t}_push.txt | tail -6
  sh $T/run_ap.sh $t melee ${t}_melee --autoduration 20
  grep "autoplay melee" $T/log_${t}_melee.txt | tail -12
done
for t in push0 push1; do
  for i in 1 2; do
    sh $T/run_ap.sh $t locostairs ${t}_st
    grep -q "locostairs trips\|no stair found" $T/log_${t}_st.txt && break
  done
  grep "locostairs" $T/log_${t}_st.txt | grep -v "trace\|locostairs t \|deep" | tail -6
done
echo "orch9 done $(date +%T)"
