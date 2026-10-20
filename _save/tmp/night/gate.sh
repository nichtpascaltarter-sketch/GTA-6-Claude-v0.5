#!/bin/sh
# gate.sh NEED_MB : wait until the lead's flag is stale (>= 5 min) and tools/memfree.sh >= NEED_MB (default 3000)
NEED=${1:-3000}
cd /home/user/GTA-6-Claude-v0.5
# the lead can ask for a pause: no new game while /tmp/night/HOLD exists
while [ -f /tmp/night/HOLD ]; do sleep 30; done
while :; do
  if [ -z "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && [ "$(sh tools/memfree.sh 2>/dev/null || echo 0)" -ge "$NEED" ]; then break; fi
  sleep 30
done
