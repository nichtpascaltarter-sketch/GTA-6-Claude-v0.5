#!/bin/sh
# usage: gate.sh cmd...   - runs a host compile / test only once tools/memfree.sh reports >= 2000 MB (waits up to 10 min)
cd /home/user/GTA-6-Claude-v0.5
n=0
while [ "$(sh tools/memfree.sh 2>/dev/null || echo 0)" -lt 2000 ] && [ $n -lt 40 ]; do sleep 15; n=$((n+1)); done
if [ "$(sh tools/memfree.sh 2>/dev/null || echo 0)" -lt 2000 ]; then echo "gate: memory still short, not running"; exit 99; fi
exec "$@"
