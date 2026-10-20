#!/bin/sh
# detached (10-20 07:3x; replaces orch35b + orch36): every in-game run left, one game at a time, each gated (no hold,
# no fresh lead flag, memfree >= 3300) and skipped when its log already holds its done line - so a restart only needs
# this started again (relaunch.sh). Waits for orch34 (c12's tour pair) first.
#  st20sp  st20 (c14d, block v3) LOCO_PLAYERLAPS=0 LOCO_NPCSPIRAL=4: the test spiral's smoke test and c15's "before"
#  st19pl / st19apl  locoplayer, c13b / HEAD (c13's proof: the player on stairs + chasers)
#  st19p / st19ap    locostairs at 0.5, c13b / HEAD
#  st19n / st20n     locoplayer LOCO_PLAYERLAPS=1 LOCO_NPCSWITCH=4, c13b / c14d (c14's proof: switchback at 0.5)
#  st20pl / st20p    c14d as st19pl / st19p
#  st21sp / st21n / st21p  c15f as st20sp / st20n / st20p (c15's proof)
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > $T/orch40.pid
echo "start $(date -u +%T)"
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
while [ -f $T/orch34.pid ] && kill -0 "$(cat $T/orch34.pid)" 2>/dev/null; do sleep 30; done
echo "orch34 over $(date -u +%T)"
run() {   # run <tree> <autoplay> <tag> <done-grep> [needs-spiral]
  for i in 1 2; do
    if grep -q "$4" $T/log_$3.txt 2>/dev/null; then echo "have $3"; return; fi
    if [ ! -f $T/$1/bin/loco_$1.exe ] || { [ -n "$5" ] && ! grep -q "LOCO_NPCSPIRAL" $T/$1/bin/loco_$1.exe; }; then echo "skip $3: no fit exe for $1 yet"; return; fi
    gate $3
    echo "run $3 $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    sh $T/run_ap2.sh $1 loco_$1.exe $2 $3
    grep -q "$4" $T/log_$3.txt 2>/dev/null && { echo "done $3 $(date -u +%T)"; break; }
    echo "incomplete $3 $(date -u +%T)"
  done
}
sp() { export LOCO_PLAYERLAPS=0 LOCO_NPCSPIRAL=4; run "$@" spiral; unset LOCO_PLAYERLAPS LOCO_NPCSPIRAL; }
nw() { export LOCO_PLAYERLAPS=1 LOCO_NPCSWITCH=4; run "$@"; unset LOCO_PLAYERLAPS LOCO_NPCSWITCH; }
p5() { export LOCO_STAIRPACE=0.5; run "$@"; unset LOCO_STAIRPACE; }
unset LOCO_PLAYERLAPS LOCO_NPCSPIRAL LOCO_NPCSWITCH LOCO_STAIRPACE LOCO_TRACEPED
sp st20 locoplayer st20sp "locoplayer done"
run st19 locoplayer st19pl "locoplayer done"
run st19a locoplayer st19apl "locoplayer done"
p5 st19 locostairs st19p "locostairs trips"
p5 st19a locostairs st19ap "locostairs trips"
nw st19 locoplayer st19n "locoplayer done"
nw st20 locoplayer st20n "locoplayer done"
run st20 locoplayer st20pl "locoplayer done"
p5 st20 locostairs st20p "locostairs trips"
sp st20 locoplayer st20sp "locoplayer done"
sp st21 locoplayer st21sp "locoplayer done"
nw st21 locoplayer st21n "locoplayer done"
p5 st21 locostairs st21p "locostairs trips"
echo "orch40 done $(date -u +%T)"
rm -f $T/orch40.pid
