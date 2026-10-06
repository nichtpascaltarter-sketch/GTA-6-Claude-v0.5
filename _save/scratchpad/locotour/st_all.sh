#!/bin/sh
# the stair gait in game: build before_st (111efa9) and after_st (111efa9 + the stair gait), then one locostairs run each
# (one game at a time), then the summary lines
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
sh /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car/gate.sh sh $T/check.sh $T/after_st 2>&1 | tail -15
for t in after_st before_st; do
  [ -f $T/$t/bin/loco_$t.exe ] || sh $T/build.sh $t
  [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; exit 1; }
done
# (one game at a time: after the pass-fix run)
until grep -q "== after_pb1" /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/tasks/bpi1s5ym0.output 2>/dev/null; do sleep 30; done
until ! pgrep -f "[l]oco_after_pb" >/dev/null; do sleep 30; done
for t in before_st after_st; do
  for i in 1 2; do
    sh $T/run_st.sh $t
    grep -q "locostairs trips" $T/log_$t.txt 2>/dev/null && break
    echo "run $t incomplete, retrying"
  done
  rm -f $T/shots_$t/*.bmp
done
for t in before_st after_st; do
  echo "== $t"
  grep "locostairs" $T/log_$t.txt | tail -8
done
echo "st_all done"
