#!/bin/sh
# removes the hold file once the memory priority allows (loco/prio_ok.sh)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
until sh $SP/loco/prio_ok.sh; do sleep 20; done
rm -f $SP/locotour/hold
echo "unhold $(date -u +%T)"
