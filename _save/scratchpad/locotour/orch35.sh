#!/bin/sh
# detached (10-20): the in-game proof of the next candidate c13 (the run's landing lead + the stairs quick-step change).
# st19a = HEAD 837b39c + the locomotion test blocks + --autoplay locoplayer (block_player.cpp); st19 = st19a + clean13
# (animator 1a3b7124, character.h bd6d781f). Builds both (one at a time; the tree's build.sh takes the shared build
# lock and waits for room), waits for orch34 (the c12 sign-off runs) to end, then one game at a time, each run gated
# like orch34's (no hold, no fresh lead flag, memfree >= 3300): locoplayer (st19pl, st19apl), locostairs at the stair
# pace 0.5 (st19p, st19ap), at the walkers' own pace (st19s, st19as), the locotour (st19t, st19at).
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > $T/orch35.pid
echo "start $(date -u +%T)"
gate() {
  n=0
  while :; do
    if [ -f $T/hold ]; then :
    elif [ -n "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ]; then :
    elif [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge $1 ]; then break
    fi
    n=$((n+1)); [ $((n % 20)) -eq 0 ] && echo "  waiting ($2) $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    sleep 30
  done
}
for t in st19a st19; do
  if [ ! -f $T/$t/bin/loco_$t.exe ]; then
    gate 2500 build_$t
    echo "build $t $(date -u +%T)"
    sh $T/build.sh $t
  fi
  [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; exit 1; }
  echo "built $t $(date -u +%T)"
done
while [ -f $T/orch34.pid ] && kill -0 "$(cat $T/orch34.pid)" 2>/dev/null; do sleep 30; done
echo "orch34 over $(date -u +%T)"
run() {   # run <tree> <autoplay> <tag> <done-grep>
  for i in 1 2; do
    if grep -q "$4" $T/log_$3.txt 2>/dev/null; then echo "have $3"; return; fi
    gate 3300 $3
    echo "run $3 $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    sh $T/run_ap2.sh $1 loco_$1.exe $2 $3
    grep -q "$4" $T/log_$3.txt 2>/dev/null && { echo "done $3 $(date -u +%T)"; break; }
    echo "incomplete $3 $(date -u +%T)"
  done
}
run st19 locoplayer st19pl "locoplayer done"
run st19a locoplayer st19apl "locoplayer done"
export LOCO_STAIRPACE=0.5
run st19 locostairs st19p "locostairs trips"
run st19a locostairs st19ap "locostairs trips"
unset LOCO_STAIRPACE
run st19 locostairs st19s "locostairs trips"
run st19a locostairs st19as "locostairs trips"
export LOCO_TRACEPED=13,35,12,114,107
run st19 locotour st19t "locotour steps all"
run st19a locotour st19at "locotour steps all"
unset LOCO_TRACEPED
echo "orch35 done $(date -u +%T)"
rm -f $T/orch35.pid
