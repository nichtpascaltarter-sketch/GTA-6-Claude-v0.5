#!/bin/sh
# detached: the stair gait in game - syntax check, build before_st2 and after_st2, one locostairs run each. Progress in orch5.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
sh /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car/gate.sh sh $T/check.sh $T/after_st2 2>&1 | tail -15
for t in after_st2 before_st2; do
  [ -f $T/$t/bin/loco_$t.exe ] || sh $T/build.sh $t
  [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; exit 1; }
  echo "built $t $(date +%T)"
done
for t in before_st2 after_st2; do
  for i in 1 2; do
    sh $T/run_st.sh $t
    grep -q "locostairs trips\|no stair found" $T/log_$t.txt 2>/dev/null && break
    echo "run $t incomplete, retrying"
  done
  rm -f $T/shots_$t/*.bmp
  echo "ran $t $(date +%T)"
done
for t in before_st2 after_st2; do
  echo "== $t"
  grep "locostairs" $T/log_$t.txt | tail -8
done
echo "orch5 done $(date +%T)"
