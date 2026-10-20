#!/bin/sh
# orch49.sh (10-20 11:5x; replaces orch48 - its st21p went on): one game at a time, gated (no hold, no fresh lead flag,
# no game running, memfree >= 3300), each round the first job of jobs49.txt whose exe is ready (built, its marker in it,
# not being built), skipping those whose logs hold their done line; two incomplete runs of a job: given up. jobs49.txt
# is read again each round (edit it to add jobs), one job a line:
#   tag|tree|exe|marker in the exe|build name (<name>.building)|autoplay|env (VAR=v ...)|done line
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > "$T/orch49.pid"
echo "start $(date -u +%T)"
unset LOCO_PLAYERLAPS LOCO_NPCSPIRAL LOCO_NPCSWITCH LOCO_STAIRPACE LOCO_TRACEPED LOCO_CHASE
gate() {
  n=0
  while :; do
    if [ -f "$T/hold" ]; then :
    elif [ -n "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ]; then :
    elif ps -eo cmd | grep -v grep | grep -q "run_ap2.sh"; then :
    elif [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 3300 ]; then break
    fi
    n=$((n+1)); [ $((n % 20)) -eq 0 ] && echo "  waiting ($1) $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    sleep 30
  done
}
tries=""
w=0
while :; do
  left=0; ran=0
  while IFS='|' read -r J TR EX MK BN AP EV DG; do
    case "$J" in ''|'#'*) continue;; esac
    grep -q "$DG" "$T/log_$J.txt" 2>/dev/null && continue
    [ "$(echo $tries | tr ' ' '\n' | grep -cx "$J")" -ge 2 ] && continue
    left=1
    { [ ! -f "$T/$BN.building" ] && grep -q "$MK" "$T/$TR/bin/$EX" 2>/dev/null; } || continue
    gate $J
    grep -q "$DG" "$T/log_$J.txt" 2>/dev/null && { ran=1; break; }
    echo "run $J $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    env $EV sh "$T/run_ap2.sh" $TR $EX $AP $J < /dev/null
    if grep -q "$DG" "$T/log_$J.txt" 2>/dev/null; then echo "done $J $(date -u +%T)"
    else echo "incomplete $J $(date -u +%T)"; [ -f "$T/log_$J.txt" ] && mv "$T/log_$J.txt" "$T/log_$J.incomplete$(date -u +%H%M).txt"; tries="$tries $J"
    fi
    ran=1
    break
  done < "$T/jobs49.txt"
  [ $left -eq 0 ] && break
  if [ $ran -eq 0 ]; then
    w=$((w+1)); [ $((w % 20)) -eq 0 ] && echo "  nothing ready $(date -u +%T)"
    [ $w -gt 480 ] && { echo "gave up waiting $(date -u +%T)"; break; }
    sleep 30
  fi
done
echo "orch49 done $(date -u +%T)"
mv "$T/orch49.pid" "$T/orch49.pid.done"
