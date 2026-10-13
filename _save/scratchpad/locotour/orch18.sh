#!/bin/sh
# detached: st7 = st6 (landing predictions + bigslide log) + the quick-step patch (qsdrag.py); build, one locostairs run
# (alone), exe deleted after. Log orch18.log; game log -> log_st7.txt
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
[ -f $T/st7/bin/loco_st7.exe ] || sh $T/build.sh st7
[ -f $T/st7/bin/loco_st7.exe ] || { echo "no exe for st7"; exit 1; }
echo "built st7 $(date +%T)"
for i in 1 2; do
  sh $T/run_st.sh st7
  grep -q "locostairs trips\|no stair found" $T/log_st7.txt && break
done
grep "locostairs" $T/log_st7.txt | grep -v "trace\|locostairs t \|deep" | tail -8
rm -f $T/st7/bin/loco_st7.exe
echo "orch18 done $(date +%T)"
