#!/bin/sh
# Gate before launching one of my games or compiles, on top of tools/run.sh's / build.sh's own gates (run.sh's memory
# wait gives up after 10 minutes and launches anyway): the coordinator's holds, the lead's flag, a free shared game
# slot (games), and room (scratchpad memgate.sh); the lead's flag and the holds are checked again after the memory wait.
# usage: gate.sh [MB]   (games: 3200 (default); builds: 2600; host compiles: 2000)
# Holds (coordinator's requests): /tmp/faces/hold_games = HHMM, games only; /tmp/faces/hold_all = HHMM, games and
# compiles. Removing a file releases it. (Never edit this file in place while a gate runs: write a new one and mv it.)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
NEED=${1:-3200}
before() { [ -e "$1" ] && [ "$(date +%H%M | sed 's/^0*//')" -lt "$(sed 's/^0*//' "$1")" ]; }
held() { { [ "$NEED" -ge 3200 ] && before /tmp/faces/hold_games; } || before /tmp/faces/hold_all; }
while :; do
  while held; do sleep 15; done
  while [ -n "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ]; do sleep 15; done
  if [ "$NEED" -ge 3200 ]; then
    free=0
    for i in 1 2 3; do if flock -n /tmp/neontide_wine_slot.$i true 2>/dev/null; then free=1; break; fi; done
    if [ $free = 0 ]; then sleep 15; continue; fi
  fi
  sh $SP/memgate.sh $NEED
  [ -n "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && continue
  held && continue
  break
done
echo "gate passed $(date +%T) memfree $(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)"
