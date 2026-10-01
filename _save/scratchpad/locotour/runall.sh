#!/bin/sh
# after both builds: the before run, then the after run (one game at a time), then the montages
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
for t in before after; do
  until [ -f $T/$t/bin/loco_$t.exe ] || grep -q "build $t rc [1-9]" /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/tasks/*.output 2>/dev/null; do sleep 20; done
done
for t in before after; do
  [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; continue; }
  for i in 1 2; do
    sh $T/run.sh $t
    grep -q "locotour steps all" $T/log_$t.txt 2>/dev/null && break
    echo "run $t incomplete, retrying"
  done
  sh $T/montage.sh $t
done
echo "runall done"
