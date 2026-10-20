#!/bin/sh
# snapshot 59 (stage 0 step 2, --world=hand) waits for snapshot 58 to end (never two snapshots at once)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
until grep -qE "COMMITTED|FAILED|HOLD" $SP/snap58_run.log; do sleep 20; done
cd $REPO
rm -f "$SP/snap59_index"
GIT_INDEX_FILE=$SP/snap59_index git read-tree HEAD && GIT_INDEX_FILE=$SP/snap59_index git update-index --index-info < $SP/snap59_entries.txt && T=$(GIT_INDEX_FILE=$SP/snap59_index git write-tree) || { echo "HOLD snapshot 59: tree build failed"; exit 1; }
echo "snapshot 59 tree $T on $(git rev-parse --short HEAD) at $(date -u +%H:%M)"
exec sh $SP/snap_smoke.sh $T $SP/snap_msg59.txt
