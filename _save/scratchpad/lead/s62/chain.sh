#!/bin/sh
# snapshot 62's smoke, started once showcase set 3 has finished (never alongside a showcase). usage: chain.sh TREE MSG
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
until grep -qE "SHOWCASE DONE|SHOWCASE FAILED" $SP/showcase_s3_chain.log; do sleep 30; done
echo "$(date -u +%H:%M) showcase ended; starting snapshot 62 on tree $1"
exec sh $SP/snap_smoke.sh "$1" "$2"
