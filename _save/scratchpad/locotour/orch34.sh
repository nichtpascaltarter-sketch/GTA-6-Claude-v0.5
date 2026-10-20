#!/bin/sh
# detached (10-20, after the 02:19 container restart killed orch33 during st17p): the in-game sign-off runs of the
# slow-pace stair fix, one game at a time. st17 = st16a + clean12 (animator 63a9f73e, character.h 9dfb632f);
# st16a = c9ff700 + test blocks (animator 002915d0, character.h 1628ef2e). Both exes were built before the restart.
# Runs: locostairs at the stair pace 0.5 (st17p, st16ap), at the walkers' own pace (st17s, st16as), the locotour
# tracing 13,35,12,114,107 (st17t, st16at). Each run waits for: no $T/hold, no fresh /tmp/neontide_lead_wants (the
# lead's snapshot checks go first), tools/memfree.sh >= 3300 MB.
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > $T/orch34.pid
echo "start $(date -u +%T)"
[ -f $T/st17/bin/loco_st17.exe ] || { echo "no exe for st17"; exit 1; }
[ -f $T/st16a/bin/loco_st16a.exe ] || { echo "no exe for st16a"; exit 1; }
gate() {
  n=0
  while :; do
    if [ -f $T/hold ]; then :
    elif [ -n "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ]; then :
    elif [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 3300 ]; then break
    fi
    n=$((n+1)); [ $((n % 20)) -eq 0 ] && echo "  waiting ($1) $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    sleep 30
  done
}
run() {   # run <tree> <autoplay> <tag> <done-grep>
  for i in 1 2; do
    if grep -q "$4" $T/log_$3.txt 2>/dev/null; then echo "have $3"; return; fi
    gate $3
    echo "run $3 $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    sh $T/run_ap2.sh $1 loco_$1.exe $2 $3
    grep -q "$4" $T/log_$3.txt 2>/dev/null && { echo "done $3 $(date -u +%T)"; break; }
    echo "incomplete $3 $(date -u +%T)"
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
echo "orch34 done $(date -u +%T)"
rm -f $T/orch34.pid
