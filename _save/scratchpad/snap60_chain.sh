#!/bin/sh
# snapshot 60 (stage 0 steps 3+4) waits for showcase set 1 to finish (never alongside a showcase)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
until grep -qE "SHOWCASE DONE|SHOWCASE FAILED" $SP/showcase_run.log; do sleep 20; done
cd $REPO
rm -f "$SP/snap60_index"
GIT_INDEX_FILE=$SP/snap60_index git read-tree HEAD && GIT_INDEX_FILE=$SP/snap60_index git update-index --index-info < $SP/snap60_entries.txt && T=$(GIT_INDEX_FILE=$SP/snap60_index git write-tree) || { echo "HOLD snapshot 60: tree build failed"; exit 1; }
echo "snapshot 60 tree $T on $(git rev-parse --short HEAD) at $(date -u +%H:%M)"
exec sh $SP/snap_smoke.sh $T $SP/snap_msg60.txt
