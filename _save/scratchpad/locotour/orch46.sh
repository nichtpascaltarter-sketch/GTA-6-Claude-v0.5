#!/bin/sh
# orch46.sh (10-20 11:2x): the v3h switchback walkers for c14 beside orch45 - st19nh (c13b, bin/loco_st19h.exe) and
# st20nh (c14d, bin/loco_st20h.exe), LOCO_PLAYERLAPS=0 LOCO_NPCSWITCH=4. Each waits for its exe (bld46), holds orch45
# ($T/hold), waits for the game being run to end, gated (no fresh lead flag, memfree >= 3300), and lets orch45 go on.
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > "$T/orch46.pid"
echo "start $(date -u +%T)"
gate() { while :; do if [ -z "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 3300 ]; then break; fi; sleep 30; done; }
unset LOCO_PLAYERLAPS LOCO_NPCSPIRAL LOCO_NPCSWITCH LOCO_STAIRPACE LOCO_TRACEPED LOCO_CHASE
for job in "st19 loco_st19h.exe st19nh st19h" "st20 loco_st20h.exe st20nh st20h"; do
  set -- $job
  for i in 1 2; do
    if grep -q "locoplayer done" "$T/log_$3.txt" 2>/dev/null; then echo "have $3"; break; fi
    n=0
    while [ -f "$T/$4.building" ] || ! grep -q "a half circle round" "$T/$1/bin/$2" 2>/dev/null; do
      n=$((n+1)); [ $n -gt 240 ] && { echo "skip $3: no exe"; break 2; }
      sleep 30
    done
    touch "$T/hold"
    sleep 5
    while ps -eo cmd | grep -v grep | grep -q "run_ap2.sh"; do sleep 20; done
    gate
    echo "run $3 $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    LOCO_PLAYERLAPS=0 LOCO_NPCSWITCH=4 sh "$T/run_ap2.sh" $1 $2 locoplayer $3
    mv "$T/hold" "$T/hold.off"
    grep -q "locoplayer done" "$T/log_$3.txt" 2>/dev/null && { echo "done $3 $(date -u +%T)"; break; }
    echo "incomplete $3 $(date -u +%T)"
  done
done
echo "orch46 done $(date -u +%T)"
mv "$T/orch46.pid" "$T/orch46.pid.done"
