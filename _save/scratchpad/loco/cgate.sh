#!/bin/sh
# usage: cgate.sh cmd... - runs a compile once tools/memfree.sh reports >= 2500 MB and the lead's snapshot checks are not
# waiting (/tmp/neontide_lead_wants fresh); waits up to 30 min, niced
R=/home/user/GTA-6-Claude-v0.5
n=0
while [ $n -lt 120 ]; do
  if [ -z "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 2500 ]; then
    exec nice -n 10 "$@"
  fi
  sleep 15; n=$((n+1))
done
echo "cgate: memory still short, not running"; exit 99
