#!/bin/sh
# detached: st15 = st13 + patches/pivot.py (a twisted foot pivots where it touches, its pose rolled before the pin
# body only; a held foot re-planted keeps its footprint). Build, then one locotour run tracing peds 13, 35, 12, 114 and
# 107 -> log_st15t.txt (vs log_st13t.txt), then one locostairs run -> log_st15s.txt (vs log_st11.txt). One game at a
# time; waits while $T/hold exists. Log orch30.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
[ -f $T/st15/bin/loco_st15.exe ] || sh $T/build.sh st15
[ -f $T/st15/bin/loco_st15.exe ] || { echo "no exe for st15"; exit 1; }
echo "built st15 $(date +%T)"
while [ -f $T/hold ]; do sleep 30; done
export LOCO_TRACEPED=13,35,12,114,107
for i in 1 2; do
  echo "run st15t $(date +%T)"
  sh $T/run_ap2.sh st15 loco_st15.exe locotour st15t
  grep -q "locotour steps all" $T/log_st15t.txt 2>/dev/null && break
done
unset LOCO_TRACEPED
while [ -f $T/hold ]; do sleep 30; done
for i in 1 2; do
  echo "run st15s $(date +%T)"
  sh $T/run_ap2.sh st15 loco_st15.exe locostairs st15s
  grep -q "locostairs trips" $T/log_st15s.txt 2>/dev/null && break
done
rm -f $T/st15/bin/loco_st15.exe
echo "orch30 done $(date +%T)"
