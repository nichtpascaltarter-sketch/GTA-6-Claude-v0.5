#!/bin/sh
# detached: st13 = st12 (shared-tree animator + turn) + a per-ped frame trace (LOCO_TRACEPED). Build, then one locotour
# run tracing peds 13, 35, 12 and 114 (the tour's big turning slides) -> log_st13t.txt. One game at a time; waits while
# $T/hold exists. Log orch28.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
[ -f $T/st13/bin/loco_st13.exe ] || sh $T/build.sh st13
[ -f $T/st13/bin/loco_st13.exe ] || { echo "no exe for st13"; exit 1; }
echo "built st13 $(date +%T)"
while [ -f $T/hold ]; do sleep 30; done
export LOCO_TRACEPED=13,35,12,114
for i in 1 2; do
  echo "run st13t $(date +%T)"
  sh $T/run_ap2.sh st13 loco_st13.exe locotour st13t
  grep -q "locotour steps all" $T/log_st13t.txt 2>/dev/null && break
done
rm -f $T/st13/bin/loco_st13.exe
echo "orch28 done $(date +%T)"
