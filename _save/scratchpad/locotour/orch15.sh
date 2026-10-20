#!/bin/sh
# detached: st6 = st5 + the landing-prediction candidate (strike lead after the strike, held-back ratio, descent heel
# margin): build once the harness build of the same code is through, then one locostairs run (alone), exe deleted after
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco/sl
O=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/tasks/b7bh008tp.output
echo "start $(date +%T)"
until grep -q 'built\|retry 6\|rror' $O 2>/dev/null; do sleep 20; done
grep -q 'built' $O || { echo "harness build failed: $(cat $O)"; exit 1; }
echo "harness built $(date +%T)"
[ -f $T/st6/bin/loco_st6.exe ] || sh $T/build.sh st6
[ -f $T/st6/bin/loco_st6.exe ] || { echo "no exe for st6"; exit 1; }
echo "built st6 $(date +%T)"
for i in 1 2; do
  sh $T/run_st.sh st6
  grep -q "locostairs trips\|no stair found" $T/log_st6.txt && break
done
grep "locostairs" $T/log_st6.txt | grep -v "trace\|locostairs t \|deep" | tail -6
rm -f $T/st6/bin/loco_st6.exe
echo "orch15 done $(date +%T)"
