#!/bin/sh
# the walking-together fix in game: build after_pb1 (111efa9 + fix) and after_pb0 (111efa9), then one run each (one game
# at a time), then the summary lines
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
for t in after_pb1 after_pb0; do
  sh $T/build.sh $t
  [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; exit 1; }
done
for t in after_pb0 after_pb1; do
  for i in 1 2; do
    sh $T/run.sh $t
    grep -q "locotour steps all" $T/log_$t.txt 2>/dev/null && break
    echo "run $t incomplete, retrying"
  done
  rm -f $T/shots_$t/*.bmp
done
for t in after_pb0 after_pb1; do
  echo "== $t"
  grep "locotour steps all\|locotour feet\|close passes\|body language\|walking together\|peds cpu" $T/log_$t.txt
done
echo "pb_all done"
