#!/bin/sh
# snapshot 57 (stage 0 step 1, terrain bake) waits for showcase set 4 to finish (never alongside a showcase)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
until grep -qE "SHOWCASE DONE|SHOWCASE FAILED" $SP/showcase_run.log; do sleep 20; done
cd $REPO
rm -f "$SP/snap57_index"
GIT_INDEX_FILE=$SP/snap57_index git read-tree HEAD && GIT_INDEX_FILE=$SP/snap57_index git update-index --index-info < $SP/snap57_entries.txt && T=$(GIT_INDEX_FILE=$SP/snap57_index git write-tree) || { echo "HOLD snapshot 57: tree build failed"; exit 1; }
echo "snapshot 57 tree $T on $(git rev-parse --short HEAD) at $(date -u +%H:%M)"
exec sh $SP/snap_smoke.sh $T $SP/snap_msg57.txt
