#!/bin/sh
# snapshot 55 (anim stance fix) starts once snapshot 54 has ended, on the HEAD it left
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
until grep -qE "COMMITTED|FAILED|HOLD" $SP/snap54_run.log; do sleep 20; done
cd $REPO
if [ "$(git rev-parse HEAD:src/anim/animator.cpp)" != 4ea1dde200dee12367d0926df638c683b6779a78 ] || [ "$(git rev-parse HEAD:src/anim/character.h)" != b745372ded59cba98aa4210bb107c51c9f6a8313 ]; then
  echo "HOLD snapshot 55: a base blob changed"; exit 1
fi
rm -f "$SP/snap55_index"
GIT_INDEX_FILE=$SP/snap55_index git read-tree HEAD && GIT_INDEX_FILE=$SP/snap55_index git update-index --index-info < $SP/snap55_entries.txt && T=$(GIT_INDEX_FILE=$SP/snap55_index git write-tree) || { echo "HOLD snapshot 55: tree build failed"; exit 1; }
echo "snapshot 55 tree $T on $(git rev-parse --short HEAD) at $(date -u +%H:%M)"
exec sh $SP/snap_smoke.sh $T $SP/snap_msg55.txt
