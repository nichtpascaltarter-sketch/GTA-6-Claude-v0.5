#!/bin/sh
# detached: st10 = st9 + the step re-scan (patches/rescan.py). Build it now; after orch24 (one game at a time, not while
# $T/hold exists): st10 stops -> log_st10s.txt, then st10 walking -> log_st10.txt. Log orch25.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
[ -f $T/st10/bin/loco_st10.exe ] || sh $T/build.sh st10
[ -f $T/st10/bin/loco_st10.exe ] || { echo "no exe for st10"; exit 1; }
echo "built st10 $(date +%T)"
until grep -q 'orch24 done' $T/orch24.log 2>/dev/null; do sleep 30; done
run() {
  while [ -f $T/hold ]; do sleep 30; done
  for i in 1 2; do
    echo "run $2 $(date +%T)"
    sh $T/run_st2.sh $1 $2 $3
    grep -q "locostairs trips\|no stair found" $T/log_$2.txt 2>/dev/null && break
  done
}
run st10 st10s stops
run st10 st10
rm -f $T/st10/bin/loco_st10.exe
echo "orch25 done $(date +%T)"
