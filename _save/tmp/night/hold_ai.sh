#!/bin/sh
# Lead 17:15: after my current game, start no game/compile until the AI agent's nightwalk run exists AND the faces
# gate (pid 17353) is gone, or 18:00 UTC (fallback). gate.sh waits while /tmp/night/HOLD exists.
touch /tmp/night/HOLD
A="--autoplay"; B="nightwalk"
while :; do
  now=$(date -u +%H%M)
  [ "$now" -ge 1800 ] && { echo "fallback" >> /tmp/night/hold_ai.log; break; }
  ai=$(ps -eo args | awk -v a="$A" -v b="$B" 'index($0, a " " b) > 0 && index($0, "awk") == 0' | head -1)
  if [ -n "$ai" ] && ! kill -0 17353 2>/dev/null; then echo "conditions met" >> /tmp/night/hold_ai.log; break; fi
  sleep 20
done
rm -f /tmp/night/HOLD
echo "released $(date -u +%H:%M:%S)" >> /tmp/night/hold_ai.log
