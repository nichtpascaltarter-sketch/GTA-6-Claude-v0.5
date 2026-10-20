#!/bin/sh
# detached (10-20 05:5x): the in-game proof of c14 (turn onto a flight + switchback up at the stair pace), after orch35.
# st20 = st19's tree (837b39c + test blocks + locoplayer block v2) with clean14b (animator 07d29e18, character.h bd6d781f
# = c13b's). Builds st20 now (gated), waits for orch35 to end, then one game at a time (gated as orch34/35):
#  st19n  = st19 (c13b) locoplayer, LOCO_PLAYERLAPS=1, LOCO_NPCSWITCH=4 (4 walkers on the test switchback, 0.5 m/s, 2 min)
#  st20n  = st20 (c14b) the same
#  st20pl = st20 locoplayer as st19pl (2 laps, no walkers)
#  st20p  = st20 locostairs at 0.5 (as st19p)
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > $T/orch36.pid
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
if [ ! -f $T/st20/bin/loco_st20.exe ]; then
  gate 2500 build_st20
  echo "build st20 $(date -u +%T)"
  sh $T/build.sh st20
fi
[ -f $T/st20/bin/loco_st20.exe ] || { echo "no exe for st20"; rm -f $T/orch36.pid; exit 1; }
echo "built st20 $(date -u +%T)"
while [ -f $T/orch35.pid ] && kill -0 "$(cat $T/orch35.pid)" 2>/dev/null; do sleep 30; done
echo "orch35 over $(date -u +%T)"
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
export LOCO_PLAYERLAPS=1 LOCO_NPCSWITCH=4
run st19 locoplayer st19n "locoplayer done"
run st20 locoplayer st20n "locoplayer done"
unset LOCO_PLAYERLAPS LOCO_NPCSWITCH
run st20 locoplayer st20pl "locoplayer done"
export LOCO_STAIRPACE=0.5
run st20 locostairs st20p "locostairs trips"
unset LOCO_STAIRPACE
echo "orch36 done $(date -u +%T)"
rm -f $T/orch36.pid
