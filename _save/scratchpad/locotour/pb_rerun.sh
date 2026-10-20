#!/bin/sh
# the walking-together fix in game again, with each close pass logged and split by walking group: syntax check, build
# after_pb1 and after_pb0, one locotour run each (one game at a time), then the stair runs may go
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
sh /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car/gate.sh sh $T/check.sh $T/after_pb1 -DLOCO_AFTER 2>&1 | tail -15
for t in after_pb1 after_pb0; do
  sh $T/build.sh $t
  [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; touch $T/pass_reruns.done; exit 1; }
done
for t in after_pb0 after_pb1; do
  for i in 1 2; do
    sh $T/run.sh $t
    grep -q "locotour steps all" $T/log_$t.txt 2>/dev/null && break
    echo "run $t incomplete, retrying"
  done
  rm -f $T/shots_$t/*.bmp
done
touch $T/pass_reruns.done
for t in after_pb0 after_pb1; do
  echo "== $t"
  grep "close passes\|body language\|walking together\|peds cpu" $T/log_$t.txt
done
echo "pb_rerun done"
