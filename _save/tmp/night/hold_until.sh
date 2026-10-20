#!/bin/sh
# keeps /tmp/night/HOLD until HHMM (UTC, default 0355), then removes it
U=${1:-0355}
while [ "$(date -u +%H%M)" -lt "$U" ]; do sleep 30; done
rm -f /tmp/night/HOLD
