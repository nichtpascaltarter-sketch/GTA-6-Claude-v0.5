#!/bin/sh
# Gate before launching one of my games, on top of tools/run.sh's own gates (run.sh's memory wait gives up after
# 10 minutes and launches anyway): wait while the lead's flag is fresh, until a shared game slot is free, and until
# there is room (scratchpad memgate.sh 3200); the lead's flag is checked again after the memory wait.
# usage: gate.sh [MB]   (games: 3200 (default); builds: 2600)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
NEED=${1:-3200}
# (a hold on my game launches, asked for by the coordinator: /tmp/faces/hold_games holds the HHMM to wait until; builds
# are not held. Removing the file releases it.)
held() { [ "$NEED" -ge 3200 ] && [ -e /tmp/faces/hold_games ] && [ "$(date +%H%M | sed 's/^0*//')" -lt "$(sed 's/^0*//' /tmp/faces/hold_games)" ]; }
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
