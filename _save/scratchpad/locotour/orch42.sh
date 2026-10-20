#!/bin/sh
# detached (10-20 08:5x; replaces orch41: its v3c walkers never moved - BRAIN_GOTO ignores a goal under 0.6 m): every
# in-game run left, one game at a time, gated (no hold, no fresh lead flag, memfree >= 3300), each skipped when its log
# holds its done line. A locoplayer run waits for its tree's v3d exe ("locoplayer spiral t%" in it) and no build of that
# tree (bld42 rebuilds st19, st19a, st20, st21 in that order).
#  st19pl / st19apl  locoplayer, c13b / HEAD                   (c13: the player on stairs + chasers)
#  st19n / st20n     LOCO_PLAYERLAPS=1 LOCO_NPCSWITCH=4, c13b / c14d  (c14: the switchback at 0.5)
#  st20pl            locoplayer, c14d                          (c14 against st19pl)
#  st20p             locostairs at 0.5, c14d                   (c14 against st19p)
#  st20sp / st21sp   LOCO_PLAYERLAPS=0 LOCO_NPCSPIRAL=4, c14d / c15f   (c15: the test spiral)
#  st21n / st21p     as st20n / st20p, c15f                    (c15 elsewhere)
# (st19p / st19ap were done by orch41.)
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > "$T/orch42.pid"
echo "start $(date -u +%T)"
# (st20sp's v3c run: its walkers never moved -> set aside)
if [ -f "$T/log_st20sp.txt" ] && ! grep -q "locoplayer spiral t" "$T/log_st20sp.txt"; then mv "$T/log_st20sp.txt" "$T/log_st20sp.v3c.txt"; fi
gate() {
  n=0
  while :; do
    if [ -f "$T/hold" ]; then :
    elif [ -n "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ]; then :
    elif [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 3300 ]; then break
    fi
    n=$((n+1)); [ $((n % 20)) -eq 0 ] && echo "  waiting ($1) $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    sleep 30
  done
}
run() {   # run <tree> <autoplay> <tag> <done-grep> [v3d]
  for i in 1 2; do
    if grep -q "$4" "$T/log_$3.txt" 2>/dev/null; then echo "have $3"; return; fi
    n=0
    while [ -f "$T/$1.building" ] || [ ! -f "$T/$1/bin/loco_$1.exe" ] || { [ -n "$5" ] && ! grep -q "locoplayer spiral t%" "$T/$1/bin/loco_$1.exe"; }; do
      n=$((n+1)); [ $n -gt 240 ] && { echo "skip $3: no exe for $1"; return; }
      sleep 30
    done
    gate $3
    echo "run $3 $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    sh "$T/run_ap2.sh" $1 loco_$1.exe $2 $3
    grep -q "$4" "$T/log_$3.txt" 2>/dev/null && { echo "done $3 $(date -u +%T)"; break; }
    echo "incomplete $3 $(date -u +%T)"
  done
}
sp() { export LOCO_PLAYERLAPS=0 LOCO_NPCSPIRAL=4; run "$@" v3d; unset LOCO_PLAYERLAPS LOCO_NPCSPIRAL; }
nw() { export LOCO_PLAYERLAPS=1 LOCO_NPCSWITCH=4; run "$@" v3d; unset LOCO_PLAYERLAPS LOCO_NPCSWITCH; }
p5() { export LOCO_STAIRPACE=0.5; run "$@"; unset LOCO_STAIRPACE; }
unset LOCO_PLAYERLAPS LOCO_NPCSPIRAL LOCO_NPCSWITCH LOCO_STAIRPACE LOCO_TRACEPED
p5 st20 locostairs st20p "locostairs trips"
run st19 locoplayer st19pl "locoplayer done" v3d
run st19a locoplayer st19apl "locoplayer done" v3d
nw st19 locoplayer st19n "locoplayer done"
nw st20 locoplayer st20n "locoplayer done"
run st20 locoplayer st20pl "locoplayer done" v3d
sp st20 locoplayer st20sp "locoplayer done"
sp st21 locoplayer st21sp "locoplayer done"
nw st21 locoplayer st21n "locoplayer done"
p5 st21 locostairs st21p "locostairs trips"
echo "orch42 done $(date -u +%T)"
mv "$T/orch42.pid" "$T/orch42.pid.done"
