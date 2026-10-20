#!/bin/sh
# Lead 21:42: after my current game/compile, start nothing new until the autoupdater's compile gate (pid 14554) is
# gone, or 22:30 UTC. gate.sh waits while /tmp/night/HOLD exists.
touch /tmp/night/HOLD
while :; do
  now=$(date -u +%H%M)
  [ "$now" -ge 2230 ] && { echo "upd hold: fallback 22:30" >> /tmp/night/hold_ai.log; break; }
  kill -0 14554 2>/dev/null || { echo "upd hold: pid 14554 gone $(date -u +%H:%M)" >> /tmp/night/hold_ai.log; break; }
  sleep 20
done
rm -f /tmp/night/HOLD
