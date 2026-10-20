#!/bin/sh
# orch44.sh (10-20 10:0x): the chase runs for c13, beside orch43 - LOCO_CHASE=1 locoplayer (the straight flight run and
# sprinted, up and down, two laps, two chasers at 3 and 5 m/s; block v3f), c13b st19ch (bin/loco_st19c.exe) and HEAD
# st19ach (bin/loco_st19ac.exe). Each waits for its exe (bld44), then holds orch43 ($T/hold) and waits for the game being
# run to end (one game at a time: the runs share the Wine log), gated (no fresh lead flag, memfree >= 3300), and lets
# orch43 go on after it. A run is skipped when its log holds "locoplayer done".
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > "$T/orch44.pid"
echo "start $(date -u +%T)"
gate() {
  n=0
  while :; do
    if [ -n "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ]; then :
    elif [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 3300 ]; then break
    fi
    n=$((n+1)); [ $((n % 20)) -eq 0 ] && echo "  waiting ($1) $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    sleep 30
  done
}
chase() {   # chase <tree> <exe> <tag>
  for i in 1 2; do
    if grep -q "locoplayer done" "$T/log_$3.txt" 2>/dev/null; then echo "have $3"; return; fi
    n=0
    while [ -f "$T/$1c.building" ] || ! grep -q "chasers:%s" "$T/$1/bin/$2" 2>/dev/null; do
      n=$((n+1)); [ $n -gt 360 ] && { echo "skip $3: no exe"; return; }
      sleep 30
    done
    touch "$T/hold"
    sleep 5
    while ps -eo cmd | grep -v grep | grep -q "run_ap2.sh"; do sleep 20; done
    gate $3
    echo "run $3 $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    LOCO_CHASE=1 sh "$T/run_ap2.sh" $1 $2 locoplayer $3
    mv "$T/hold" "$T/hold.off"
    grep -q "locoplayer done" "$T/log_$3.txt" 2>/dev/null && { echo "done $3 $(date -u +%T)"; break; }
    echo "incomplete $3 $(date -u +%T)"
  done
}
unset LOCO_PLAYERLAPS LOCO_NPCSPIRAL LOCO_NPCSWITCH LOCO_STAIRPACE LOCO_TRACEPED
chase st19 loco_st19c.exe st19ch
chase st19a loco_st19ac.exe st19ach
echo "orch44 done $(date -u +%T)"
mv "$T/orch44.pid" "$T/orch44.pid.done"
