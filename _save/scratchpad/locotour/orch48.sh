#!/bin/sh
# orch48.sh (10-20 11:4x; replaces orch47 - its st19nh went on - and reads its list from jobs48.txt each round, so jobs
# can be added): one game at a time, gated (no hold, no
# fresh lead flag, no game running, memfree >= 3300), each time the first job of the list whose exe is ready (built,
# its marker in it, not being built), skipping those whose logs hold their done line:
#  st21ng   (orch45 started it)  LOCO_PLAYERLAPS=1 LOCO_NPCSWITCH=4, c15f (v3g)              c15: elsewhere, against st20ng
#  st19nh / st20nh  LOCO_PLAYERLAPS=0 LOCO_NPCSWITCH=4, c13b (v3h) / c14d (v3i = v3h + the spiral's walker)   c14
#  st21p    locostairs at 0.5, c15f                                                        c15: elsewhere
#  st20spi / st21spi  LOCO_PLAYERLAPS=0 LOCO_NPCSPIRAL=1, c14d / c15f (v3i)                c15: the test spiral
# (orch45's list, for the record:)
#  st20pl   locoplayer, c14d (v3e; orch43 started it)            c14: the player, against st19pl
#  st19chg  LOCO_CHASE=1, c13b + v3g / st19achg HEAD + v3g      c13 addendum: the chasers at 3 and 5 m/s, up and down
#  st19ng   LOCO_PLAYERLAPS=1 LOCO_NPCSWITCH=4, c13b / st20ng c14d (v3g: one way at a time)   c14: the switchback at 0.5
#  st20sp   LOCO_PLAYERLAPS=0 LOCO_NPCSPIRAL=2, c14d / st21sp c15f (v3g)                      c15: the test spiral
#  st21ng   as st20ng, c15f;  st21p  locostairs at 0.5, c15f                                  c15: elsewhere
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > "$T/orch48.pid"
echo "start $(date -u +%T)"
unset LOCO_PLAYERLAPS LOCO_NPCSPIRAL LOCO_NPCSWITCH LOCO_STAIRPACE LOCO_TRACEPED LOCO_CHASE
G="one way at a time"
attrs() {   # TR tree, EX exe, MK marker in it, BN its build's name, AP autoplay, EV env, DG done line
  AP=locoplayer; DG="locoplayer done"; EV=""
  case $1 in
    st20pl) TR=st20; EX=loco_st20.exe; MK="8 treads of 17 cm"; BN=st20;;
    st19chg) TR=st19; EX=loco_st19g.exe; MK=$G; BN=st19g; EV="LOCO_CHASE=1";;
    st19achg) TR=st19a; EX=loco_st19ag.exe; MK=$G; BN=st19ag; EV="LOCO_CHASE=1";;
    st19ng) TR=st19; EX=loco_st19g.exe; MK=$G; BN=st19g; EV="LOCO_PLAYERLAPS=1 LOCO_NPCSWITCH=4";;
    st20ng) TR=st20; EX=loco_st20g.exe; MK=$G; BN=st20g; EV="LOCO_PLAYERLAPS=1 LOCO_NPCSWITCH=4";;
    st20sp) TR=st20; EX=loco_st20g.exe; MK=$G; BN=st20g; EV="LOCO_PLAYERLAPS=0 LOCO_NPCSPIRAL=2";;
    st21sp) TR=st21; EX=loco_st21.exe; MK=$G; BN=st21; EV="LOCO_PLAYERLAPS=0 LOCO_NPCSPIRAL=2";;
    st21ng) TR=st21; EX=loco_st21.exe; MK=$G; BN=st21; EV="LOCO_PLAYERLAPS=1 LOCO_NPCSWITCH=4";;
    st21p) TR=st21; EX=loco_st21.exe; MK=$G; BN=st21; AP=locostairs; EV="LOCO_STAIRPACE=0.5"; DG="locostairs trips";;
    st19nh) TR=st19; EX=loco_st19h.exe; MK="a half circle round"; BN=st19h; EV="LOCO_PLAYERLAPS=0 LOCO_NPCSWITCH=4";;
    st20nh) TR=st20; EX=loco_st20h.exe; MK="a half circle round"; BN=st20h; EV="LOCO_PLAYERLAPS=0 LOCO_NPCSWITCH=4";;
    st20spi) TR=st20; EX=loco_st20h.exe; MK="on the walking line"; BN=st20h; EV="LOCO_PLAYERLAPS=0 LOCO_NPCSPIRAL=1";;
    st21spi) TR=st21; EX=loco_st21i.exe; MK="on the walking line"; BN=st21i; EV="LOCO_PLAYERLAPS=0 LOCO_NPCSPIRAL=1";;
    st21pl) TR=st21; EX=loco_st21i.exe; MK="on the walking line"; BN=st21i;;
    *) TR=""; EX=""; MK="no such job"; BN=none;;
  esac
}
ready() { [ ! -f "$T/$BN.building" ] && grep -q "$MK" "$T/$TR/bin/$EX" 2>/dev/null; }
gate() {
  n=0
  while :; do
    if [ -f "$T/hold" ]; then :
    elif [ -n "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ]; then :
    elif ps -eo cmd | grep -v grep | grep -q "run_ap2.sh"; then :   # (a game still running: orch43's st20pl, orch44's)
    elif [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 3300 ]; then break
    fi
    n=$((n+1)); [ $((n % 20)) -eq 0 ] && echo "  waiting ($1) $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    sleep 30
  done
}
JOBS=""
tries=""
w=0
while :; do
  JOBS=$(grep -v '^#' "$T/jobs48.txt" 2>/dev/null | tr '\n' ' ')
  left=0; ran=0
  for j in $JOBS; do
    attrs $j
    grep -q "$DG" "$T/log_$j.txt" 2>/dev/null && continue
    [ "$(echo $tries | tr ' ' '\n' | grep -cx "$j")" -ge 2 ] && continue   # (two incomplete runs: given up)
    left=1
    ready || continue
    gate $j
    attrs $j
    # (the gate may have waited out a run of this same job by another chain)
    grep -q "$DG" "$T/log_$j.txt" 2>/dev/null && { ran=1; break; }
    echo "run $j $(date -u +%T) memfree $(sh $R/tools/memfree.sh 2>/dev/null)"
    env $EV sh "$T/run_ap2.sh" $TR $EX $AP $j
    if grep -q "$DG" "$T/log_$j.txt" 2>/dev/null; then echo "done $j $(date -u +%T)"
    else echo "incomplete $j $(date -u +%T)"; [ -f "$T/log_$j.txt" ] && mv "$T/log_$j.txt" "$T/log_$j.incomplete$(date -u +%H%M).txt"; tries="$tries $j"
    fi
    ran=1
    break   # (back to the top of the list)
  done
  [ $left -eq 0 ] && break
  if [ $ran -eq 0 ]; then
    w=$((w+1)); [ $((w % 20)) -eq 0 ] && echo "  nothing ready $(date -u +%T)"
    [ $w -gt 480 ] && { echo "gave up waiting $(date -u +%T)"; break; }
    sleep 30
  fi
done
echo "orch48 done $(date -u +%T)"
mv "$T/orch48.pid" "$T/orch48.pid.done"
