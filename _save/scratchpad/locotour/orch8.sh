#!/bin/sh
# detached: after orch7's run, the re-plan version in game (after_st4 = after_st3 + re-fitting a landing whose predicted
# spot moved 3 cm, the shift eased in by the landing), one locostairs run -> log_after_st4.txt. Progress in orch8.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
until grep -q "orch7 done\|no exe for after_st3" $T/orch7.log 2>/dev/null; do sleep 30; done
echo "orch7 finished $(date +%T)"
sh /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car/gate.sh sh $T/check.sh $T/after_st4 2>&1 | tail -15
[ -f $T/after_st4/bin/loco_after_st4.exe ] || sh $T/build.sh after_st4
[ -f $T/after_st4/bin/loco_after_st4.exe ] || { echo "no exe for after_st4"; exit 1; }
echo "built after_st4 $(date +%T)"
for i in 1 2 3; do
  sh $T/run_st.sh after_st4
  grep -q "locostairs trips\|no stair found" $T/log_after_st4.txt 2>/dev/null && break
  echo "run after_st4 incomplete, retrying"
done
rm -f $T/shots_after_st4/*.bmp
echo "== after_st4"
grep "locostairs" $T/log_after_st4.txt | grep -v "trace\|locostairs t \|deep" | tail -6
echo "orch8 done $(date +%T)"
