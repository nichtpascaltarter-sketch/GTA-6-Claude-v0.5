#!/bin/sh
# detached: st11 = st10 + the PR #47 P2 fix (patches/p2fix.py). Build, then one locostairs run (walking) -> log_st11.txt
# (one game at a time, not while $T/hold exists). Log orch26.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
[ -f $T/st11/bin/loco_st11.exe ] || sh $T/build.sh st11
[ -f $T/st11/bin/loco_st11.exe ] || { echo "no exe for st11"; exit 1; }
echo "built st11 $(date +%T)"
while [ -f $T/hold ]; do sleep 30; done
for i in 1 2; do
  echo "run st11 $(date +%T)"
  sh $T/run_st2.sh st11 st11
  grep -q "locostairs trips\|no stair found" $T/log_st11.txt 2>/dev/null && break
done
rm -f $T/st11/bin/loco_st11.exe
echo "orch26 done $(date +%T)"
