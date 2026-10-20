#!/bin/sh
# detached: the toe-margin follow-up in game (after_st3 = 111efa9 + the stair gait with the 4.5 cm toe allowance, the
# deep-foot log on), one locostairs run; compare with log_before_st2.txt / log_after_st2.txt. Progress in orch7.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
sh /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car/gate.sh sh $T/check.sh $T/after_st3 2>&1 | tail -15
[ -f $T/after_st3/bin/loco_after_st3.exe ] || sh $T/build.sh after_st3
[ -f $T/after_st3/bin/loco_after_st3.exe ] || { echo "no exe for after_st3"; exit 1; }
echo "built after_st3 $(date +%T)"
for i in 1 2 3; do
  sh $T/run_st.sh after_st3
  grep -q "locostairs trips\|no stair found" $T/log_after_st3.txt 2>/dev/null && break
  echo "run after_st3 incomplete, retrying"
done
rm -f $T/shots_after_st3/*.bmp
echo "== after_st3"
grep "locostairs" $T/log_after_st3.txt | grep -v "trace\|locostairs t \|deep" | tail -6
echo "orch7 done $(date +%T)"
