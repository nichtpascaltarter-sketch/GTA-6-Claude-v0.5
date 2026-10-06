#!/bin/sh
# one at a time, detached from the session's task limit: the pass-fix builds and runs (111efa9 vs the fix without the
# group skip), then the stair gait builds and runs. Progress in orch.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
for t in after_pb2 after_pb0; do
  [ -f $T/$t/bin/loco_$t.exe ] || sh $T/build.sh $t
  [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; exit 1; }
  echo "built $t $(date +%T)"
done
for t in after_pb0 after_pb2; do
  for i in 1 2; do
    sh $T/run.sh $t
    grep -q "locotour steps all" $T/log_$t.txt 2>/dev/null && break
    echo "run $t incomplete, retrying"
  done
  rm -f $T/shots_$t/*.bmp
  echo "ran $t $(date +%T)"
done
for t in after_pb0 after_pb2; do
  echo "== $t"
  grep "close passes\|body language\|walking together\|peds cpu" $T/log_$t.txt
done
echo "pass runs done $(date +%T)"
touch $T/pass_reruns.done
for t in after_st before_st; do
  [ -f $T/$t/bin/loco_$t.exe ] || sh $T/build.sh $t
  [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; exit 1; }
  echo "built $t $(date +%T)"
done
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
echo "orch done $(date +%T)"
