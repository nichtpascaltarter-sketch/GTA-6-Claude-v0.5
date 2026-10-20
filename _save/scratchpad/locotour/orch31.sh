#!/bin/sh
# detached: st16a = HEAD c9ff700 (snapshot 57) + the test-only tour blocks (st15's) + LOCO_STAIRPACE; st16 = st16a +
# patches/stairpace.py (animator 0c9893ff, character.h b7eab762). Builds both, then one game at a time:
# locostairs at the stair pace 0.5 (st16 -> log_st16p.txt, st16a -> log_st16ap.txt), at the walkers' own pace
# (log_st16s / log_st16as), the locotour tracing 13,35,12,114,107 (log_st16t / log_st16at). Waits while $T/hold exists.
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
for t in st16 st16a; do
  [ -f $T/$t/bin/loco_$t.exe ] || sh $T/build.sh $t
  [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; exit 1; }
  echo "built $t $(date +%T)"
done
run() {   # run <tree> <autoplay> <tag> <done-grep>
  while [ -f $T/hold ]; do sleep 30; done
  for i in 1 2; do
    echo "run $3 $(date +%T)"
    sh $T/run_ap2.sh $1 loco_$1.exe $2 $3
    grep -q "$4" $T/log_$3.txt 2>/dev/null && break
  done
}
export LOCO_STAIRPACE=0.5
run st16 locostairs st16p "locostairs trips"
run st16a locostairs st16ap "locostairs trips"
unset LOCO_STAIRPACE
run st16 locostairs st16s "locostairs trips"
run st16a locostairs st16as "locostairs trips"
export LOCO_TRACEPED=13,35,12,114,107
run st16 locotour st16t "locotour steps all"
run st16a locotour st16at "locotour steps all"
unset LOCO_TRACEPED
rm -f $T/st16/bin/loco_st16.exe $T/st16a/bin/loco_st16a.exe
echo "orch31 done $(date +%T)"
