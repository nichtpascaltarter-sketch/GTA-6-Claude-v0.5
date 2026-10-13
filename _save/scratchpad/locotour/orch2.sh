#!/bin/sh
# detached: the third pass-fix variant (lean only while someone goes by), then the stair gait runs. Progress in orch2.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
until ! pgrep -f "[b]uild.sh before_st" >/dev/null; do sleep 20; done
for t in after_pb3 before_st after_st; do
  [ -f $T/$t/bin/loco_$t.exe ] || sh $T/build.sh $t
  [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; exit 1; }
  echo "built $t $(date +%T)"
done
for i in 1 2; do
  sh $T/run.sh after_pb3
  grep -q "locotour steps all" $T/log_after_pb3.txt 2>/dev/null && break
  echo "run after_pb3 incomplete, retrying"
done
rm -f $T/shots_after_pb3/*.bmp
echo "== after_pb3"
grep "close passes\|body language\|walking together\|peds cpu" $T/log_after_pb3.txt
echo "pass runs done $(date +%T)"
touch $T/pass_reruns.done
for t in before_st after_st; do
  for i in 1 2; do
    sh $T/run_st.sh $t
    grep -q "locostairs trips" $T/log_$t.txt 2>/dev/null && break
    echo "run $t incomplete, retrying"
  done
  rm -f $T/shots_$t/*.bmp
  echo "ran $t $(date +%T)"
done
for t in before_st after_st; do
  echo "== $t"
  grep "locostairs" $T/log_$t.txt | tail -8
done
echo "orch2 done $(date +%T)"
