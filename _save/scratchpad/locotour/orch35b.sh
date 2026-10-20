#!/bin/sh
# detached (10-20 06:4x, replaces orch35 while it waited for orch34): c13's in-game proof, trimmed to what c13 changes -
# the player on stairs with chasers (locoplayer st19pl c13b / st19apl HEAD) and the stair pace (locostairs 0.5, st19p /
# st19ap). c13 does not change walking at the walkers' own pace off the stairs' hand-over nor flat walking, so the
# own-pace and tour pairs are left out. Writes orch35.pid (orch36 waits on it).
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
for t in st19a st19; do [ -f $T/$t/bin/loco_$t.exe ] || { echo "no exe for $t"; rm -f $T/orch35.pid; exit 1; }; done
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
echo "orch35b done $(date -u +%T)"
rm -f $T/orch35.pid
