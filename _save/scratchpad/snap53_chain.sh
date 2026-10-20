#!/bin/sh
# snapshot 53 waits for showcase set 1 to finish (never alongside a showcase)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
until grep -qE "SHOWCASE DONE|SHOWCASE FAILED" $SP/showcase_run.log; do sleep 20; done
echo "showcase finished, starting snapshot 53 at $(date -u +%H:%M)"
exec sh $SP/snap_smoke.sh 72e409523c66f9433f79617ef71e4a9ec92e2a05 $SP/snap_msg53.txt
