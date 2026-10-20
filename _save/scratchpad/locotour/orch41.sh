#!/bin/sh
# detached (10-20 08:1x; replaces orch40, whose st20sp found no open spot and whose st19pl had no switchback): every
# in-game run left, one game at a time, gated (no hold, no fresh lead flag, memfree >= 3300), each skipped when its log
# holds its done line. While bld41 rebuilds the trees with block v3c, the locostairs runs go first (they do not use the
# block); a locoplayer run waits for its tree's v3c exe ("within 80 m" in it) and no build of that tree.
#  st19p / st19ap    locostairs at 0.5, c13b / HEAD            (c13)
#  st20p / st21p     locostairs at 0.5, c14d / c15f            (c14, c15 elsewhere)
#  st20sp / st21sp   LOCO_PLAYERLAPS=0 LOCO_NPCSPIRAL=4, c14d / c15f   (c15)
#  st19pl / st19apl  locoplayer, c13b / HEAD                   (c13: the player on stairs + chasers)
#  st19n / st20n / st21n  LOCO_PLAYERLAPS=1 LOCO_NPCSWITCH=4, c13b / c14d / c15f  (c14; c15 elsewhere)
#  st20pl            locoplayer, c14d                          (c14 against st19pl)
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > $T/orch41.pid
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
run() {   # run <tree> <autoplay> <tag> <done-grep> [v3c]
  for i in 1 2; do
    if grep -q "$4" $T/log_$3.txt 2>/dev/null; then echo "have $3"; return; fi
    n=0
    while [ -f $T/$1.building ] || [ ! -f $T/$1/bin/loco_$1.exe ] || { [ -n "$5" ] && ! grep -q "within 80 m" $T/$1/bin/loco_$1.exe; }; do
      n=$((n+1)); [ $n -gt 240 ] && { echo "skip $3: no exe for $1"; return; }
      sleep 30
    done
    gate $3
    echo "run $3 $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    sh $T/run_ap2.sh $1 loco_$1.exe $2 $3
    grep -q "$4" $T/log_$3.txt 2>/dev/null && { echo "done $3 $(date -u +%T)"; break; }
    echo "incomplete $3 $(date -u +%T)"
  done
}
sp() { export LOCO_PLAYERLAPS=0 LOCO_NPCSPIRAL=4; run "$@" v3c; unset LOCO_PLAYERLAPS LOCO_NPCSPIRAL; }
nw() { export LOCO_PLAYERLAPS=1 LOCO_NPCSWITCH=4; run "$@" v3c; unset LOCO_PLAYERLAPS LOCO_NPCSWITCH; }
p5() { export LOCO_STAIRPACE=0.5; run "$@"; unset LOCO_STAIRPACE; }
unset LOCO_PLAYERLAPS LOCO_NPCSPIRAL LOCO_NPCSWITCH LOCO_STAIRPACE LOCO_TRACEPED
p5 st19 locostairs st19p "locostairs trips"
p5 st19a locostairs st19ap "locostairs trips"
sp st20 locoplayer st20sp "locoplayer done"
run st19 locoplayer st19pl "locoplayer done" v3c
run st19a locoplayer st19apl "locoplayer done" v3c
nw st19 locoplayer st19n "locoplayer done"
nw st20 locoplayer st20n "locoplayer done"
p5 st20 locostairs st20p "locostairs trips"
run st20 locoplayer st20pl "locoplayer done" v3c
sp st21 locoplayer st21sp "locoplayer done"
nw st21 locoplayer st21n "locoplayer done"
p5 st21 locostairs st21p "locostairs trips"
echo "orch41 done $(date -u +%T)"
rm -f $T/orch41.pid
