#!/bin/sh
# detached, replaces orch32 after its first run: st17 = st16a + patches/stairpace.py with hunk 11 (animator 63a9f73e,
# character.h 9dfb632f: the stair-pace candidate + the forefoot stand when a swing lands short of its fit). Builds st17
# while st16p (orch32's run of the earlier candidate, run_ap2 PID 22319) goes on, waits for that run to end, then one game
# at a time: locostairs at the stair pace 0.5 (st17p, base st16ap), at the walkers' own pace (st17s, st16as), the locotour
# tracing 13,35,12,114,107 (st17t, st16at). Waits while $T/hold exists.
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
[ -f $T/st17/bin/loco_st17.exe ] || sh $T/build.sh st17
[ -f $T/st17/bin/loco_st17.exe ] || { echo "no exe for st17"; exit 1; }
[ -f $T/st16a/bin/loco_st16a.exe ] || { echo "no exe for st16a"; exit 1; }
echo "built st17 $(date +%T)"
while kill -0 22319 2>/dev/null; do sleep 20; done
echo "st16p run over $(date +%T)"
rm -f $T/st16/bin/loco_st16.exe
run() {   # run <tree> <autoplay> <tag> <done-grep>
  while [ -f $T/hold ]; do sleep 30; done
  for i in 1 2; do
    echo "run $3 $(date +%T)"
    sh $T/run_ap2.sh $1 loco_$1.exe $2 $3
    grep -q "$4" $T/log_$3.txt 2>/dev/null && break
  done
}
export LOCO_STAIRPACE=0.5
run st17 locostairs st17p "locostairs trips"
run st16a locostairs st16ap "locostairs trips"
unset LOCO_STAIRPACE
run st17 locostairs st17s "locostairs trips"
run st16a locostairs st16as "locostairs trips"
export LOCO_TRACEPED=13,35,12,114,107
run st17 locotour st17t "locotour steps all"
run st16a locotour st16at "locotour steps all"
unset LOCO_TRACEPED
rm -f $T/st17/bin/loco_st17.exe $T/st16a/bin/loco_st16a.exe
echo "orch33 done $(date +%T)"
