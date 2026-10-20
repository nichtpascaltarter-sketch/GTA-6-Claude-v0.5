#!/bin/sh
# detached: st14 = st13 + patches/snapstance.py (stance changes among the walk's / stand's stances crossfade the upper
# body only; a held foot re-planted keeps its footprint). Build, then one locotour run tracing peds 13, 35, 12, 114 and
# 107 -> log_st14t.txt (vs log_st13t.txt), then one locostairs run -> log_st14s.txt (vs log_st11.txt). One game at a
# time; waits while $T/hold exists. Log orch29.log
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
[ -f $T/st14/bin/loco_st14.exe ] || sh $T/build.sh st14
[ -f $T/st14/bin/loco_st14.exe ] || { echo "no exe for st14"; exit 1; }
echo "built st14 $(date +%T)"
while [ -f $T/hold ]; do sleep 30; done
export LOCO_TRACEPED=13,35,12,114,107
for i in 1 2; do
  echo "run st14t $(date +%T)"
  sh $T/run_ap2.sh st14 loco_st14.exe locotour st14t
  grep -q "locotour steps all" $T/log_st14t.txt 2>/dev/null && break
done
unset LOCO_TRACEPED
while [ -f $T/hold ]; do sleep 30; done
for i in 1 2; do
  echo "run st14s $(date +%T)"
  sh $T/run_ap2.sh st14 loco_st14.exe locostairs st14s
  grep -q "locostairs trips" $T/log_st14s.txt 2>/dev/null && break
done
rm -f $T/st14/bin/loco_st14.exe
echo "orch29 done $(date +%T)"
