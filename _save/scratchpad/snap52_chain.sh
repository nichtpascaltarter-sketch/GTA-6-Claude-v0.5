#!/bin/sh
# snapshot 52 waits for showcase set 0 to finish (never alongside a showcase)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
until grep -qE "SHOWCASE DONE|SHOWCASE FAILED" $SP/showcase_run.log; do sleep 20; done
echo "showcase finished, starting snapshot 52 at $(date -u +%H:%M)"
exec sh $SP/snap_smoke.sh 61285ca26feef6d91c882c612d29f7c33dd22e83 $SP/snap_msg52.txt
