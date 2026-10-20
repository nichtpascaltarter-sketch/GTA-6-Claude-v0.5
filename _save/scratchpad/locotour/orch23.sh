#!/bin/sh
# detached: st8 = st6 + the revised quick-step patch (qsdrag2: hold 0.85, snap at the end, the root's own vertical move with
# no foot held) + walker 180 traced; build now, run locostairs after orch22 (one game at a time). Log orch23.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
[ -f $T/st8/bin/loco_st8.exe ] || sh $T/build.sh st8
[ -f $T/st8/bin/loco_st8.exe ] || { echo "no exe for st8"; exit 1; }
echo "built st8 $(date +%T)"
until grep -q 'orch22 done' $T/orch22.log 2>/dev/null; do sleep 30; done
for i in 1 2; do
  sh $T/run_st.sh st8
  grep -q "locostairs trips\|no stair found" $T/log_st8.txt && break
done
grep "locostairs" $T/log_st8.txt | grep -v "trace\|locostairs t \|deep" | tail -8
rm -f $T/st8/bin/loco_st8.exe
echo "orch23 done $(date +%T)"
